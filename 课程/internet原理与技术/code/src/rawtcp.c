#include "rawtcp.h"

#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

/* TCP pseudo header for checksum calculation. */
struct rawtcp_pseudo_header {
    uint32_t src_addr;
    uint32_t dst_addr;
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_len;
};

/* Monotonic time in milliseconds. */
uint64_t rawtcp_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

/* Internet checksum (RFC 1071). */
uint16_t rawtcp_checksum(const void *buf, int len) {
    const uint16_t *data = (const uint16_t *)buf;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *data++;
        len -= 2;
    }
    if (len == 1) {
        sum += *((const uint8_t *)data);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/* TCP checksum with pseudo header + TCP header (with options) + payload. */
uint16_t rawtcp_tcp_checksum(const struct iphdr *ip,
                             const uint8_t *tcp_hdr, int tcp_hdr_len,
                             const uint8_t *payload, int payload_len) {
    struct rawtcp_pseudo_header pseudo;
    uint32_t tcp_len = (uint32_t)(tcp_hdr_len + payload_len);
    uint32_t buf_len = (uint32_t)(sizeof(struct rawtcp_pseudo_header) + tcp_len);
    uint8_t buf[2048];

    if (buf_len > sizeof(buf)) {
        return 0;
    }

    pseudo.src_addr = ip->saddr;
    pseudo.dst_addr = ip->daddr;
    pseudo.zero = 0;
    pseudo.protocol = IPPROTO_TCP;
    pseudo.tcp_len = htons((uint16_t)tcp_len);

    memcpy(buf, &pseudo, sizeof(pseudo));
    memcpy(buf + sizeof(pseudo), tcp_hdr, (size_t)tcp_hdr_len);
    if (payload_len > 0) {
        memcpy(buf + sizeof(pseudo) + (size_t)tcp_hdr_len, payload, payload_len);
    }

    return rawtcp_checksum(buf, (int)buf_len);
}

/* Pack MSS option. */
int rawtcp_mss_pack(uint16_t mss, uint8_t *out, int out_len) {
    if (out_len < 4) {
        return -1;
    }
    out[0] = RAWTCP_OPT_MSS;
    out[1] = 4;
    out[2] = (uint8_t)(mss >> 8);
    out[3] = (uint8_t)(mss & 0xFFU);
    return 4;
}

/* Pack Window Scale option. */
int rawtcp_ws_pack(uint8_t ws, uint8_t *out, int out_len) {
    if (out_len < 3) {
        return -1;
    }
    out[0] = RAWTCP_OPT_WS;
    out[1] = 3;
    out[2] = ws;
    return 3;
}

/* Pack SACK Permitted option. */
int rawtcp_sack_perm_pack(uint8_t *out, int out_len) {
    if (out_len < 2) {
        return -1;
    }
    out[0] = RAWTCP_OPT_SACK_PERM;
    out[1] = 2;
    return 2;
}

/* Pack SACK blocks option. */
int rawtcp_sack_pack(const struct rawtcp_sack_block *blocks, int block_count,
                     uint8_t *out, int out_len) {
    if (block_count <= 0) {
        return 0;
    }
    if (block_count > RAWTCP_MAX_SACK_BLOCKS) {
        block_count = RAWTCP_MAX_SACK_BLOCKS;
    }
    int len = 2 + 8 * block_count;
    if (out_len < len) {
        return -1;
    }
    out[0] = RAWTCP_OPT_SACK;
    out[1] = (uint8_t)len;
    for (int i = 0; i < block_count; ++i) {
        uint32_t left = htonl(blocks[i].left_edge);
        uint32_t right = htonl(blocks[i].right_edge);
        memcpy(out + 2 + i * 8, &left, sizeof(left));
        memcpy(out + 2 + i * 8 + 4, &right, sizeof(right));
    }
    return len;
}

/* Add SACK block to list and merge overlaps. */
void rawtcp_sack_add_block(struct rawtcp_sack_block *blocks, int *count,
                           struct rawtcp_sack_block block) {
    if (!blocks || !count) {
        return;
    }
    if (block.left_edge >= block.right_edge) {
        return;
    }
    for (int i = 0; i < *count; ++i) {
        struct rawtcp_sack_block *cur = &blocks[i];
        if (!(block.right_edge < cur->left_edge || block.left_edge > cur->right_edge)) {
            if (block.left_edge < cur->left_edge) {
                cur->left_edge = block.left_edge;
            }
            if (block.right_edge > cur->right_edge) {
                cur->right_edge = block.right_edge;
            }
            return;
        }
    }
    if (*count < RAWTCP_MAX_SACK_BLOCKS) {
        blocks[*count] = block;
        (*count)++;
    }
}

/* Parse SACK blocks from options buffer. */
int rawtcp_sack_parse(const uint8_t *opts, int opt_len,
                      struct rawtcp_sack_block *blocks, int max_blocks) {
    int count = 0;
    int i = 0;
    while (i < opt_len) {
        uint8_t kind = opts[i];
        if (kind == RAWTCP_OPT_END) {
            break;
        }
        if (kind == RAWTCP_OPT_NOP) {
            i++;
            continue;
        }
        if (i + 1 >= opt_len) {
            break;
        }
        uint8_t len = opts[i + 1];
        if (len < 2 || i + len > opt_len) {
            break;
        }
        if (kind == RAWTCP_OPT_SACK) {
            int block_count = (len - 2) / 8;
            if (block_count > max_blocks) {
                block_count = max_blocks;
            }
            for (int b = 0; b < block_count; ++b) {
                uint32_t left = 0;
                uint32_t right = 0;
                memcpy(&left, opts + i + 2 + b * 8, sizeof(left));
                memcpy(&right, opts + i + 2 + b * 8 + 4, sizeof(right));
                blocks[count].left_edge = ntohl(left);
                blocks[count].right_edge = ntohl(right);
                count++;
                if (count >= max_blocks) {
                    return count;
                }
            }
        }
        i += len;
    }
    return count;
}

/* Build SYN/SYN+ACK options (MSS + SACK Permitted + Window Scale). */
int rawtcp_build_syn_options(uint16_t mss, bool sack_permitted, uint8_t ws,
                             uint8_t *out, int out_len) {
    int len = 0;
    int n = rawtcp_mss_pack(mss, out + len, out_len - len);
    if (n < 0) {
        return -1;
    }
    len += n;
    if (sack_permitted) {
        n = rawtcp_sack_perm_pack(out + len, out_len - len);
        if (n < 0) {
            return -1;
        }
        len += n;
    }
    if (ws <= RAWTCP_MAX_WS) {
        n = rawtcp_ws_pack(ws, out + len, out_len - len);
        if (n < 0) {
            return -1;
        }
        len += n;
    }
    while (len % 4 != 0) {
        if (len >= out_len) {
            return -1;
        }
        out[len++] = RAWTCP_OPT_NOP;
    }
    return len;
}

/* Parse TCP options into state. */
int rawtcp_parse_options(const uint8_t *opts, int opt_len,
                         struct rawtcp_option_state *out) {
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    int i = 0;
    while (i < opt_len) {
        uint8_t kind = opts[i];
        if (kind == RAWTCP_OPT_END) {
            break;
        }
        if (kind == RAWTCP_OPT_NOP) {
            i++;
            continue;
        }
        if (i + 1 >= opt_len) {
            break;
        }
        uint8_t len = opts[i + 1];
        if (len < 2 || i + len > opt_len) {
            break;
        }
        if (kind == RAWTCP_OPT_MSS && len == 4) {
            uint16_t mss = 0;
            memcpy(&mss, opts + i + 2, sizeof(mss));
            out->mss_present = true;
            out->mss = ntohs(mss);
        } else if (kind == RAWTCP_OPT_WS && len == 3) {
            out->ws_present = true;
            out->ws = opts[i + 2];
        } else if (kind == RAWTCP_OPT_SACK_PERM && len == 2) {
            out->sack_permitted = true;
        } else if (kind == RAWTCP_OPT_SACK) {
            out->sack_block_count = rawtcp_sack_parse(opts + i, len,
                                                      out->sack_blocks,
                                                      RAWTCP_MAX_SACK_BLOCKS);
        }
        i += len;
    }
    return 0;
}

/* Apply window scale to window field value. */
uint32_t rawtcp_window_scale_apply(uint16_t window, uint8_t scale) {
    if (scale > RAWTCP_MAX_WS) {
        scale = RAWTCP_MAX_WS;
    }
    return (uint32_t)window << scale;
}

/* Compose window field from scaled window size. */
uint16_t rawtcp_window_scale_compose(uint32_t window, uint8_t scale) {
    if (scale > RAWTCP_MAX_WS) {
        scale = RAWTCP_MAX_WS;
    }
    uint32_t field = window >> scale;
    if (field > 0xFFFFU) {
        field = 0xFFFFU;
    }
    return (uint16_t)field;
}

/* Build and send a raw IP/TCP packet with optional TCP options. */
int rawtcp_send_packet_ex(int sock,
                          uint32_t src_ip, uint32_t dst_ip,
                          uint16_t src_port, uint16_t dst_port,
                          uint32_t seq, uint32_t ack,
                          uint16_t flags,
                          uint16_t window,
                          const uint8_t *options, int options_len,
                          const uint8_t *payload, int payload_len) {
    uint8_t packet[RAWTCP_MAX_PACKET_SIZE];
    struct iphdr *ip = (struct iphdr *)packet;
    struct tcphdr *tcp = (struct tcphdr *)(packet + sizeof(struct iphdr));
    int ip_len = sizeof(struct iphdr);
    int tcp_len = sizeof(struct tcphdr) + options_len;
    int total_len = ip_len + tcp_len + payload_len;

    if (options_len < 0 || options_len > RAWTCP_TCP_OPT_MAX) {
        return -1;
    }
    if (options_len % 4 != 0) {
        return -1;
    }

    memset(packet, 0, (size_t)total_len);

    ip->version = 4;
    ip->ihl = ip_len / 4;
    ip->tos = 0;
    ip->tot_len = htons((uint16_t)total_len);
    ip->id = htons((uint16_t)(rawtcp_now_ms() & 0xFFFFU));
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IPPROTO_TCP;
    ip->saddr = src_ip;
    ip->daddr = dst_ip;
    ip->check = 0;
    ip->check = rawtcp_checksum(ip, ip_len);

    tcp->source = htons(src_port);
    tcp->dest = htons(dst_port);
    tcp->seq = htonl(seq);
    tcp->ack_seq = htonl(ack);
    tcp->doff = tcp_len / 4;
    tcp->window = htons(window);
    tcp->urg_ptr = 0;

    tcp->syn = (flags & TH_SYN) ? 1 : 0;
    tcp->ack = (flags & TH_ACK) ? 1 : 0;
    tcp->fin = (flags & TH_FIN) ? 1 : 0;
    tcp->psh = (flags & TH_PUSH) ? 1 : 0;
    tcp->rst = (flags & TH_RST) ? 1 : 0;

    if (options_len > 0 && options) {
        memcpy(packet + ip_len + sizeof(struct tcphdr), options, (size_t)options_len);
    }
    if (payload_len > 0) {
        memcpy(packet + ip_len + tcp_len, payload, (size_t)payload_len);
    }

    tcp->check = 0;
    tcp->check = rawtcp_tcp_checksum(ip,
                                     (const uint8_t *)tcp,
                                     tcp_len,
                                     payload, payload_len);

    struct sockaddr_in dst_addr;
    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(dst_port);
    dst_addr.sin_addr.s_addr = dst_ip;

    return sendto(sock, packet, (size_t)total_len, 0,
                  (struct sockaddr *)&dst_addr, sizeof(dst_addr));
}

/* Build and send a raw IP/TCP packet without options. */
int rawtcp_send_packet(int sock,
                       uint32_t src_ip, uint32_t dst_ip,
                       uint16_t src_port, uint16_t dst_port,
                       uint32_t seq, uint32_t ack,
                       uint16_t flags,
                       uint16_t window,
                       const uint8_t *payload, int payload_len) {
    return rawtcp_send_packet_ex(sock,
                                 src_ip, dst_ip,
                                 src_port, dst_port,
                                 seq, ack,
                                 flags,
                                 window,
                                 NULL, 0,
                                 payload, payload_len);
}
