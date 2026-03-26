#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define HEADER_LEN 9
#define BUFFER_SIZE 8192
#define RETRY_MAX 3

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s <server_ip> <port> download <remote_filename> <output_path>\n"
            "  %s <server_ip> <port> upload <local_filename> <server_save_path>\n",
            prog, prog);
}

static ssize_t send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

static int send_with_retry(int fd, const uint8_t *buf, size_t len) {
    size_t sent = 0;
    int retries = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (++retries > RETRY_MAX) {
                    return -1;
                }
                continue;
            }
            if (++retries > RETRY_MAX) {
                return -1;
            }
            continue;
        }
        if (n == 0) {
            if (++retries > RETRY_MAX) {
                return -1;
            }
            continue;
        }
        sent += (size_t)n;
        retries = 0;
    }
    return 0;
}

static ssize_t recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t recvd = 0;
    while (recvd < len) {
        ssize_t n = recv(fd, p + recvd, len - recvd, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return -1;
        }
        recvd += (size_t)n;
    }
    return (ssize_t)recvd;
}

static uint64_t parse_be64(const uint8_t *buf) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | buf[i];
    }
    return v;
}

static void write_be64(uint64_t v, uint8_t *buf) {
    buf[0] = (uint8_t)((v >> 56) & 0xFFU);
    buf[1] = (uint8_t)((v >> 48) & 0xFFU);
    buf[2] = (uint8_t)((v >> 40) & 0xFFU);
    buf[3] = (uint8_t)((v >> 32) & 0xFFU);
    buf[4] = (uint8_t)((v >> 24) & 0xFFU);
    buf[5] = (uint8_t)((v >> 16) & 0xFFU);
    buf[6] = (uint8_t)((v >> 8) & 0xFFU);
    buf[7] = (uint8_t)(v & 0xFFU);
}

static int connect_server(const char *server_ip, int port, int *out_mss) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid server IP\n");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    int mss = 0;
    socklen_t optlen = sizeof(mss);
    if (getsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, &mss, &optlen) == 0) {
        printf("Negotiated MSS: %d\n", mss);
    }
    struct tcp_info info;
    memset(&info, 0, sizeof(info));
    optlen = sizeof(info);
    if (getsockopt(sock, IPPROTO_TCP, TCP_INFO, &info, &optlen) == 0) {
        printf("SACK enabled: %s\n", (info.tcpi_options & TCPI_OPT_SACK) ? "yes" : "no");
    }

    if (out_mss) {
        *out_mss = mss;
    }
    return sock;
}

static int download_file(int sock, const char *filename, const char *output_path) {
    char request[1024];
    snprintf(request, sizeof(request), "GET %s\n", filename);
    if (send_all(sock, request, strlen(request)) < 0) {
        perror("send");
        return -1;
    }

    uint8_t header[HEADER_LEN];
    if (recv_all(sock, header, HEADER_LEN) < 0) {
        fprintf(stderr, "Failed to read response header\n");
        return -1;
    }

    uint8_t status = header[0];
    uint64_t file_size = parse_be64(header + 1);

    if (status != 0) {
        fprintf(stderr, "Server reported error (file not found or invalid request).\n");
        return -1;
    }

    FILE *out = fopen(output_path, "wb");
    if (!out) {
        perror("fopen");
        return -1;
    }

    uint8_t buffer[BUFFER_SIZE];
    uint64_t received = 0;
    while (received < file_size) {
        size_t to_read = BUFFER_SIZE;
        uint64_t remaining = file_size - received;
        if (remaining < to_read) {
            to_read = (size_t)remaining;
        }

        ssize_t n = recv(sock, buffer, to_read, 0);
        if (n <= 0) {
            fprintf(stderr, "Connection closed early (received %llu/%llu).\n",
                    (unsigned long long)received, (unsigned long long)file_size);
            fclose(out);
            return -1;
        }

        if (fwrite(buffer, 1, (size_t)n, out) != (size_t)n) {
            perror("fwrite");
            fclose(out);
            return -1;
        }

        received += (uint64_t)n;
    }

    fclose(out);
    printf("Download complete: %s (%llu bytes)\n", output_path,
           (unsigned long long)file_size);
    return 0;
}

static int upload_file(int sock, const char *local_path, const char *remote_path, int mss) {
    int fd = open(local_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "Invalid local file\n");
        close(fd);
        return -1;
    }

    uint64_t file_size = (uint64_t)st.st_size;

    char request[1024];
    snprintf(request, sizeof(request), "PUT %s\n", remote_path);
    if (send_all(sock, request, strlen(request)) < 0) {
        perror("send");
        close(fd);
        return -1;
    }

    uint8_t size_buf[8];
    write_be64(file_size, size_buf);
    if (send_all(sock, size_buf, sizeof(size_buf)) < 0) {
        perror("send");
        close(fd);
        return -1;
    }

    uint8_t header[HEADER_LEN];
    if (recv_all(sock, header, HEADER_LEN) < 0) {
        fprintf(stderr, "Failed to read response header\n");
        close(fd);
        return -1;
    }

    uint8_t status = header[0];
    uint64_t allowed = parse_be64(header + 1);
    if (status != 0) {
        fprintf(stderr, "Server rejected upload (permission denied or file exists).\n");
        close(fd);
        return -1;
    }
    if (allowed < file_size) {
        fprintf(stderr, "Server reported allowed size %llu < file size %llu\n",
                (unsigned long long)allowed, (unsigned long long)file_size);
        close(fd);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int chunk = mss > 0 ? mss : 1460;
    if (chunk <= 0 || chunk > BUFFER_SIZE) {
        chunk = BUFFER_SIZE;
    }

    uint8_t buffer[BUFFER_SIZE];
    uint64_t offset = 0;
    while (offset < file_size) {
        uint64_t remaining = file_size - offset;
        size_t to_read = remaining < (uint64_t)chunk ? (size_t)remaining : (size_t)chunk;
        ssize_t r = pread(fd, buffer, to_read, (off_t)offset);
        if (r <= 0) {
            perror("pread");
            close(fd);
            return -1;
        }
        if (send_with_retry(sock, buffer, (size_t)r) != 0) {
            fprintf(stderr, "Send failed after retries\n");
            close(fd);
            return -1;
        }
        offset += (uint64_t)r;
    }

    shutdown(sock, SHUT_WR);

    uint8_t drain[512];
    for (;;) {
        ssize_t n = recv(sock, drain, sizeof(drain), 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }

    close(fd);
    printf("Upload complete: %s -> %s (%llu bytes)\n", local_path, remote_path,
           (unsigned long long)file_size);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        usage(argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *mode = argv[3];

    if (port <= 0 || port > 65535) {
        usage(argv[0]);
        return 1;
    }

    int mss = 0;
    int sock = connect_server(server_ip, port, &mss);
    if (sock < 0) {
        return 1;
    }

    int ret = 0;
    if (strcmp(mode, "download") == 0) {
        ret = download_file(sock, argv[4], argv[5]);
    } else if (strcmp(mode, "upload") == 0) {
        ret = upload_file(sock, argv[4], argv[5], mss);
    } else {
        usage(argv[0]);
        ret = -1;
    }

    close(sock);
    return ret == 0 ? 0 : 1;
}
