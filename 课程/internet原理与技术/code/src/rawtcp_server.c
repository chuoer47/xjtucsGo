#include "rawtcp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define REQ_MAX_LEN 1024
#define HEADER_LEN 9

enum conn_state {
    STATE_LISTEN = 0,
    STATE_SYN_RECEIVED,
    STATE_ESTABLISHED,
    STATE_FIN_WAIT,
    STATE_CLOSED
};

enum app_mode {
    APP_NONE = 0,
    APP_GET,
    APP_PUT
};

/* In-flight data segment for Go-Back-N retransmission. */
struct inflight_seg {
    uint32_t seq;
    uint64_t offset;
    uint16_t len;
    uint64_t last_sent_ms;
    int retransmits;
    bool sacked;
};

/* Buffered out-of-order segment for upload reassembly. */
struct recv_seg {
    uint32_t seq;
    uint16_t len;
    uint8_t data[RAWTCP_MAX_MSS];
    bool used;
};

/* Single-connection server context (one client at a time). */
struct server_ctx {
    int sock;
    uint16_t listen_port;
    char root_dir[PATH_MAX];
    int mss;
    int window_segs;
    uint16_t local_mss;
    uint16_t peer_mss;
    uint16_t eff_mss;
    uint8_t local_ws;
    uint8_t peer_ws;
    bool ws_enabled;
    bool sack_enabled;
    bool sack_permitted;
    uint32_t local_window_bytes;
    uint32_t peer_window_bytes;

    enum conn_state state;

    uint32_t client_ip;
    uint16_t client_port;
    uint32_t server_ip;

    uint32_t client_isn;
    uint32_t snd_iss;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;

    uint64_t syn_last_sent_ms;
    int syn_retransmits;

    char req_buf[REQ_MAX_LEN];
    size_t req_len;
    bool request_ready;
    enum app_mode app_mode;

    int file_fd;
    uint64_t file_size;
    uint8_t header[HEADER_LEN];
    uint64_t data_total_len;
    uint32_t data_start_seq;
    uint64_t acked_offset;
    uint64_t next_send_offset;

    struct inflight_seg inflight[RAWTCP_MAX_WINDOW_SEGS];
    int inflight_count;

    struct recv_seg ooo_buf[RAWTCP_MAX_WINDOW_SEGS];
    int ooo_count;

    char upload_path[PATH_MAX];
    uint8_t put_size_buf[8];
    size_t put_size_read;
    bool put_size_ready;
    uint64_t upload_total_len;
    uint64_t upload_received;
    uint32_t upload_start_seq;
    bool upload_active;
    bool upload_done;
    bool upload_error;

    bool sending;
    bool fin_sent;
    bool fin_acked;
    uint32_t fin_seq;
    bool client_fin_received;
};

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s <listen_port> <file_root_dir> [mss] [window_segs] [window_scale] [enable_sack]\n",
            prog);
}

/* Reject absolute paths and path traversal. */
static bool is_safe_path(const char *name) {
    if (name[0] == '\0' || name[0] == '/') {
        return false;
    }
    if (strstr(name, "..") != NULL) {
        return false;
    }
    return true;
}

static uint64_t parse_be64(const uint8_t *buf) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | buf[i];
    }
    return v;
}

static int open_requested_file(struct server_ctx *ctx, const char *name) {
    char path[PATH_MAX];

    if (!is_safe_path(name)) {
        return -1;
    }

    if (snprintf(path, sizeof(path), "%s/%s", ctx->root_dir, name) >= (int)sizeof(path)) {
        return -1;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }

    ctx->file_size = (uint64_t)st.st_size;
    return fd;
}

static int build_upload_path(struct server_ctx *ctx, const char *name, char *out, size_t out_len) {
    if (!is_safe_path(name)) {
        return -1;
    }
    if (snprintf(out, out_len, "%s/%s", ctx->root_dir, name) >= (int)out_len) {
        return -1;
    }
    return 0;
}

static int open_upload_file(struct server_ctx *ctx, const char *name) {
    char path[PATH_MAX];

    if (build_upload_path(ctx, name, path, sizeof(path)) != 0) {
        return -1;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }

    return fd;
}

static void cleanup_partial_upload(struct server_ctx *ctx) {
    if (ctx->app_mode != APP_PUT || !ctx->upload_active) {
        return;
    }
    if (ctx->upload_done && !ctx->upload_error) {
        return;
    }
    if (ctx->upload_path[0] == '\0') {
        return;
    }
    char path[PATH_MAX];
    if (build_upload_path(ctx, ctx->upload_path, path, sizeof(path)) != 0) {
        return;
    }
    (void)unlink(path);
}

/* Application header: 1 byte status + 8 bytes big-endian size. */
static void build_header(struct server_ctx *ctx, uint8_t status,
                         uint64_t advertised_len, uint64_t payload_len) {
    ctx->header[0] = status;
    uint64_t size = advertised_len;

    ctx->header[1] = (uint8_t)((size >> 56) & 0xFFU);
    ctx->header[2] = (uint8_t)((size >> 48) & 0xFFU);
    ctx->header[3] = (uint8_t)((size >> 40) & 0xFFU);
    ctx->header[4] = (uint8_t)((size >> 32) & 0xFFU);
    ctx->header[5] = (uint8_t)((size >> 24) & 0xFFU);
    ctx->header[6] = (uint8_t)((size >> 16) & 0xFFU);
    ctx->header[7] = (uint8_t)((size >> 8) & 0xFFU);
    ctx->header[8] = (uint8_t)(size & 0xFFU);

    ctx->data_total_len = HEADER_LEN + payload_len;
}

/* Read data stream (header + file) from a given offset. */
static int read_stream_data(struct server_ctx *ctx, uint64_t offset, uint8_t *buf, int len) {
    int copied = 0;

    if (offset >= ctx->data_total_len) {
        return 0;
    }

    if (offset < HEADER_LEN) {
        int header_rem = (int)(HEADER_LEN - offset);
        int to_copy = header_rem < len ? header_rem : len;
        memcpy(buf, ctx->header + offset, (size_t)to_copy);
        copied += to_copy;
        offset += (uint64_t)to_copy;
    }

    if (copied < len && ctx->file_fd >= 0) {
        uint64_t file_offset = offset - HEADER_LEN;
        int to_read = len - copied;
        ssize_t r = pread(ctx->file_fd, buf + copied, (size_t)to_read, (off_t)file_offset);
        if (r < 0) {
            return copied;
        }
        copied += (int)r;
    }

    return copied;
}

/* Reset connection state after close or error. */
static void reset_connection(struct server_ctx *ctx) {
    cleanup_partial_upload(ctx);
    ctx->state = STATE_LISTEN;
    ctx->client_ip = 0;
    ctx->client_port = 0;
    ctx->server_ip = 0;
    ctx->client_isn = 0;
    ctx->snd_iss = 0;
    ctx->snd_nxt = 0;
    ctx->rcv_nxt = 0;
    ctx->peer_mss = RAWTCP_MIN_MSS;
    ctx->eff_mss = ctx->local_mss;
    ctx->mss = ctx->local_mss;
    ctx->peer_ws = 0;
    ctx->ws_enabled = false;
    ctx->sack_permitted = false;
    ctx->peer_window_bytes = 0xFFFFU;
    ctx->syn_last_sent_ms = 0;
    ctx->syn_retransmits = 0;
    ctx->req_len = 0;
    ctx->request_ready = false;
    ctx->app_mode = APP_NONE;
    if (ctx->file_fd >= 0) {
        close(ctx->file_fd);
    }
    ctx->file_fd = -1;
    ctx->file_size = 0;
    ctx->ooo_count = 0;
    for (int i = 0; i < RAWTCP_MAX_WINDOW_SEGS; ++i) {
        ctx->ooo_buf[i].used = false;
    }
    ctx->upload_path[0] = '\0';
    ctx->put_size_read = 0;
    ctx->put_size_ready = false;
    ctx->upload_total_len = 0;
    ctx->upload_received = 0;
    ctx->upload_start_seq = 0;
    ctx->upload_active = false;
    ctx->upload_done = false;
    ctx->upload_error = false;
    ctx->data_total_len = 0;
    ctx->data_start_seq = 0;
    ctx->acked_offset = 0;
    ctx->next_send_offset = 0;
    ctx->inflight_count = 0;
    ctx->sending = false;
    ctx->fin_sent = false;
    ctx->fin_acked = false;
    ctx->fin_seq = 0;
    ctx->client_fin_received = false;
}

/* Compute window field for outgoing packets. */
static uint16_t advertised_window(const struct server_ctx *ctx) {
    uint8_t scale = ctx->ws_enabled ? ctx->local_ws : 0;
    return rawtcp_window_scale_compose(ctx->local_window_bytes, scale);
}

/* Update peer advertised window in bytes. */
static void update_peer_window(struct server_ctx *ctx, uint16_t win_field) {
    uint8_t scale = ctx->ws_enabled ? ctx->peer_ws : 0;
    ctx->peer_window_bytes = rawtcp_window_scale_apply(win_field, scale);
}

static uint64_t inflight_bytes(const struct server_ctx *ctx) {
    if (ctx->next_send_offset < ctx->acked_offset) {
        return 0;
    }
    return ctx->next_send_offset - ctx->acked_offset;
}

/* Mark in-flight segments covered by SACK blocks. */
static void mark_sacked_segments(struct server_ctx *ctx,
                                 const struct rawtcp_sack_block *blocks,
                                 int count) {
    if (!ctx->sack_permitted || count <= 0) {
        return;
    }
    for (int i = 0; i < ctx->inflight_count; ++i) {
        uint32_t seg_start = ctx->inflight[i].seq;
        uint32_t seg_end = seg_start + ctx->inflight[i].len;
        for (int b = 0; b < count; ++b) {
            uint32_t left = blocks[b].left_edge;
            uint32_t right = blocks[b].right_edge;
            if (seg_start >= left && seg_end <= right) {
                ctx->inflight[i].sacked = true;
                break;
            }
        }
    }
}

static void ooo_reset(struct server_ctx *ctx) {
    ctx->ooo_count = 0;
    for (int i = 0; i < RAWTCP_MAX_WINDOW_SEGS; ++i) {
        ctx->ooo_buf[i].used = false;
    }
}

static struct recv_seg *ooo_find(struct server_ctx *ctx, uint32_t seq) {
    for (int i = 0; i < RAWTCP_MAX_WINDOW_SEGS; ++i) {
        if (ctx->ooo_buf[i].used && ctx->ooo_buf[i].seq == seq) {
            return &ctx->ooo_buf[i];
        }
    }
    return NULL;
}

static bool ooo_store(struct server_ctx *ctx, uint32_t seq, const uint8_t *payload, uint16_t len) {
    if (ooo_find(ctx, seq)) {
        return true;
    }
    if (ctx->ooo_count >= RAWTCP_MAX_WINDOW_SEGS) {
        return false;
    }
    for (int i = 0; i < RAWTCP_MAX_WINDOW_SEGS; ++i) {
        if (!ctx->ooo_buf[i].used) {
            ctx->ooo_buf[i].seq = seq;
            ctx->ooo_buf[i].len = len;
            if (len > 0) {
                memcpy(ctx->ooo_buf[i].data, payload, len);
            }
            ctx->ooo_buf[i].used = true;
            ctx->ooo_count++;
            return true;
        }
    }
    return false;
}

static int build_sack_options(const struct server_ctx *ctx, uint8_t *out, int out_len) {
    if (!ctx->sack_permitted || ctx->ooo_count == 0) {
        return 0;
    }
    struct rawtcp_sack_block blocks[RAWTCP_MAX_SACK_BLOCKS];
    int count = 0;
    for (int i = 0; i < RAWTCP_MAX_WINDOW_SEGS; ++i) {
        if (!ctx->ooo_buf[i].used) {
            continue;
        }
        struct rawtcp_sack_block block;
        block.left_edge = ctx->ooo_buf[i].seq;
        block.right_edge = ctx->ooo_buf[i].seq + ctx->ooo_buf[i].len;
        rawtcp_sack_add_block(blocks, &count, block);
        if (count >= RAWTCP_MAX_SACK_BLOCKS) {
            break;
        }
    }
    if (count == 0) {
        return 0;
    }
    int len = rawtcp_sack_pack(blocks, count, out, out_len);
    if (len < 0) {
        return 0;
    }
    while (len % 4 != 0 && len < out_len) {
        out[len++] = RAWTCP_OPT_NOP;
    }
    return len;
}

/* Send pure ACK with current rcv_nxt. */
static void send_ack(struct server_ctx *ctx) {
    if (ctx->app_mode == APP_PUT && ctx->upload_active && ctx->ooo_count > 0) {
        uint8_t options[RAWTCP_TCP_OPT_MAX];
        int opt_len = build_sack_options(ctx, options, (int)sizeof(options));
        if (opt_len > 0) {
            rawtcp_send_packet_ex(ctx->sock,
                                  ctx->server_ip, ctx->client_ip,
                                  ctx->listen_port, ctx->client_port,
                                  ctx->snd_nxt, ctx->rcv_nxt,
                                  TH_ACK,
                                  advertised_window(ctx),
                                  options, opt_len,
                                  NULL, 0);
            return;
        }
    }
    rawtcp_send_packet(ctx->sock,
                       ctx->server_ip, ctx->client_ip,
                       ctx->listen_port, ctx->client_port,
                       ctx->snd_nxt, ctx->rcv_nxt,
                       TH_ACK,
                       advertised_window(ctx),
                       NULL, 0);
}

/* Send SYN+ACK during handshake. */
static void send_synack(struct server_ctx *ctx) {
    uint8_t options[RAWTCP_TCP_OPT_MAX];
    int opt_len = rawtcp_build_syn_options(ctx->local_mss,
                                           ctx->sack_permitted,
                                           ctx->local_ws,
                                           options, (int)sizeof(options));
    if (opt_len < 0) {
        opt_len = 0;
    }
    rawtcp_send_packet_ex(ctx->sock,
                          ctx->server_ip, ctx->client_ip,
                          ctx->listen_port, ctx->client_port,
                          ctx->snd_iss, ctx->rcv_nxt,
                          TH_SYN | TH_ACK,
                          advertised_window(ctx),
                          options, opt_len,
                          NULL, 0);
}

/* Send FIN to close connection. */
static void send_fin(struct server_ctx *ctx) {
    rawtcp_send_packet(ctx->sock,
                       ctx->server_ip, ctx->client_ip,
                       ctx->listen_port, ctx->client_port,
                       ctx->fin_seq, ctx->rcv_nxt,
                       TH_FIN | TH_ACK,
                       advertised_window(ctx),
                       NULL, 0);
}

static void inflight_pop_acked(struct server_ctx *ctx, uint32_t ack_seq) {
    int i = 0;
    while (i < ctx->inflight_count) {
        uint32_t seg_end = ctx->inflight[i].seq + ctx->inflight[i].len;
        if (ack_seq >= seg_end) {
            i++;
        } else {
            break;
        }
    }
    if (i > 0) {
        memmove(ctx->inflight, ctx->inflight + i,
                sizeof(ctx->inflight[0]) * (size_t)(ctx->inflight_count - i));
        ctx->inflight_count -= i;
    }
}

/* Handle cumulative ACK for outgoing data (and optional SACK blocks). */
static void handle_ack(struct server_ctx *ctx, uint32_t ack_seq,
                       const struct rawtcp_option_state *opts) {
    if (ctx->fin_sent && ack_seq == ctx->fin_seq + 1) {
        ctx->fin_acked = true;
    }

    if (!ctx->sending) {
        return;
    }

    if (ack_seq < ctx->data_start_seq) {
        return;
    }

    uint64_t new_acked = (uint64_t)(ack_seq - ctx->data_start_seq);
    if (new_acked > ctx->data_total_len) {
        new_acked = ctx->data_total_len;
    }
    if (new_acked > ctx->acked_offset) {
        ctx->acked_offset = new_acked;
        inflight_pop_acked(ctx, ack_seq);
    }
    if (opts && opts->sack_block_count > 0) {
        mark_sacked_segments(ctx, opts->sack_blocks, opts->sack_block_count);
    }

    if (!ctx->fin_sent && ctx->acked_offset >= ctx->data_total_len) {
        if (ctx->app_mode == APP_GET || (ctx->app_mode == APP_PUT && ctx->upload_error)) {
            ctx->fin_seq = ctx->data_start_seq + (uint32_t)ctx->data_total_len;
            ctx->fin_sent = true;
            ctx->snd_nxt = ctx->fin_seq + 1;
            send_fin(ctx);
            ctx->state = STATE_FIN_WAIT;
        } else if (ctx->app_mode == APP_PUT) {
            ctx->sending = false;
            ctx->inflight_count = 0;
        }
    }
}

/* Initialize sending state after request parsed. */
static void start_sending(struct server_ctx *ctx) {
    ctx->sending = true;
    ctx->data_start_seq = ctx->snd_nxt;
    ctx->acked_offset = 0;
    ctx->next_send_offset = 0;
}

/* Fill window with new data segments. */
static void maybe_send_data(struct server_ctx *ctx) {
    if (!ctx->sending || ctx->fin_sent) {
        return;
    }

    while (ctx->inflight_count < ctx->window_segs &&
           ctx->next_send_offset < ctx->data_total_len) {
        uint64_t in_flight = inflight_bytes(ctx);
        if (ctx->peer_window_bytes == 0 || in_flight >= ctx->peer_window_bytes) {
            break;
        }
        uint64_t remaining = ctx->data_total_len - ctx->next_send_offset;
        uint64_t avail = ctx->peer_window_bytes - in_flight;
        uint64_t max_len = remaining;
        if (max_len > (uint64_t)ctx->mss) {
            max_len = (uint64_t)ctx->mss;
        }
        if (max_len > avail) {
            max_len = avail;
        }
        if (max_len == 0) {
            break;
        }
        uint16_t seg_len = (uint16_t)max_len;
        uint8_t payload[RAWTCP_MAX_MSS];
        int copied = read_stream_data(ctx, ctx->next_send_offset, payload, seg_len);
        if (copied <= 0) {
            break;
        }

        uint32_t seq = ctx->data_start_seq + (uint32_t)ctx->next_send_offset;
        rawtcp_send_packet(ctx->sock,
                           ctx->server_ip, ctx->client_ip,
                           ctx->listen_port, ctx->client_port,
                           seq, ctx->rcv_nxt,
                           TH_ACK | TH_PUSH,
                           advertised_window(ctx),
                           payload, copied);

        struct inflight_seg *seg = &ctx->inflight[ctx->inflight_count++];
        seg->seq = seq;
        seg->offset = ctx->next_send_offset;
        seg->len = (uint16_t)copied;
        seg->last_sent_ms = rawtcp_now_ms();
        seg->retransmits = 0;
        seg->sacked = false;

        ctx->next_send_offset += (uint64_t)copied;
        ctx->snd_nxt = ctx->data_start_seq + (uint32_t)ctx->next_send_offset;
    }
}

/* Go-Back-N style retransmit on timeout. */
static void retransmit_if_needed(struct server_ctx *ctx) {
    if (!ctx->sending || ctx->inflight_count == 0) {
        return;
    }

    uint64_t now = rawtcp_now_ms();
    if (!ctx->sack_permitted) {
        if (now - ctx->inflight[0].last_sent_ms < RAWTCP_RETRANS_TIMEOUT_MS) {
            return;
        }
        for (int i = 0; i < ctx->inflight_count; ++i) {
            struct inflight_seg *seg = &ctx->inflight[i];
            if (seg->retransmits >= RAWTCP_MAX_RETRANS) {
                ctx->state = STATE_CLOSED;
                return;
            }
            uint8_t payload[RAWTCP_MAX_MSS];
            int copied = read_stream_data(ctx, seg->offset, payload, seg->len);
            if (copied <= 0) {
                continue;
            }
            rawtcp_send_packet(ctx->sock,
                               ctx->server_ip, ctx->client_ip,
                               ctx->listen_port, ctx->client_port,
                               seg->seq, ctx->rcv_nxt,
                               TH_ACK | TH_PUSH,
                               advertised_window(ctx),
                               payload, copied);
            seg->last_sent_ms = now;
            seg->retransmits++;
        }
        return;
    }

    for (int i = 0; i < ctx->inflight_count; ++i) {
        struct inflight_seg *seg = &ctx->inflight[i];
        if (seg->sacked) {
            continue;
        }
        if (now - seg->last_sent_ms < RAWTCP_RETRANS_TIMEOUT_MS) {
            continue;
        }
        if (seg->retransmits >= RAWTCP_MAX_RETRANS) {
            ctx->state = STATE_CLOSED;
            return;
        }
        uint8_t payload[RAWTCP_MAX_MSS];
        int copied = read_stream_data(ctx, seg->offset, payload, seg->len);
        if (copied <= 0) {
            continue;
        }
        rawtcp_send_packet(ctx->sock,
                           ctx->server_ip, ctx->client_ip,
                           ctx->listen_port, ctx->client_port,
                           seg->seq, ctx->rcv_nxt,
                           TH_ACK | TH_PUSH,
                           advertised_window(ctx),
                           payload, copied);
        seg->last_sent_ms = now;
        seg->retransmits++;
    }
}

/* Parse application request when a line is complete. */
static void handle_request_ready(struct server_ctx *ctx) {
    if (ctx->request_ready) {
        return;
    }

    for (size_t i = 0; i < ctx->req_len; ++i) {
        if (ctx->req_buf[i] == '\n') {
            ctx->req_buf[i] = '\0';
            if (i > 0 && ctx->req_buf[i - 1] == '\r') {
                ctx->req_buf[i - 1] = '\0';
            }
            ctx->request_ready = true;
            break;
        }
    }

    if (!ctx->request_ready) {
        return;
    }

    const char *line = ctx->req_buf;
    while (*line == ' ' || *line == '\t') {
        line++;
    }

    if (strncmp(line, "GET ", 4) == 0) {
        const char *name = line + 4;
        while (*name == ' ') {
            name++;
        }
        size_t name_len = strlen(name);
        while (name_len > 0 && (name[name_len - 1] == ' ' || name[name_len - 1] == '\t')) {
            name_len--;
        }
        char cleaned[PATH_MAX];
        if (name_len >= sizeof(cleaned)) {
            ctx->file_fd = -1;
            ctx->file_size = 0;
            ctx->app_mode = APP_GET;
            build_header(ctx, 1, 0, 0);
            start_sending(ctx);
            return;
        }
        memcpy(cleaned, name, name_len);
        cleaned[name_len] = '\0';
        if (*name == '\0') {
            ctx->file_fd = -1;
            ctx->file_size = 0;
            ctx->app_mode = APP_GET;
            build_header(ctx, 1, 0, 0);
            start_sending(ctx);
            return;
        }

        ctx->file_fd = open_requested_file(ctx, cleaned);
        ctx->app_mode = APP_GET;
        if (ctx->file_fd < 0) {
            ctx->file_size = 0;
            build_header(ctx, 1, 0, 0);
        } else {
            build_header(ctx, 0, ctx->file_size, ctx->file_size);
        }
        start_sending(ctx);
        return;
    }

    if (strncmp(line, "PUT ", 4) == 0) {
        const char *name = line + 4;
        while (*name == ' ') {
            name++;
        }
        size_t name_len = strlen(name);
        while (name_len > 0 && (name[name_len - 1] == ' ' || name[name_len - 1] == '\t')) {
            name_len--;
        }
        ctx->app_mode = APP_PUT;
        ctx->upload_error = false;
        ctx->upload_active = false;
        ctx->upload_done = false;
        ctx->upload_received = 0;
        ctx->put_size_read = 0;
        ctx->put_size_ready = false;
        ctx->upload_total_len = 0;
        ctx->upload_start_seq = 0;
        ooo_reset(ctx);

        if (name_len == 0 || name_len >= sizeof(ctx->upload_path)) {
            ctx->upload_error = true;
            ctx->upload_path[0] = '\0';
            return;
        }
        memcpy(ctx->upload_path, name, name_len);
        ctx->upload_path[name_len] = '\0';
        if (!is_safe_path(ctx->upload_path)) {
            ctx->upload_error = true;
        }
        return;
    }

    ctx->file_fd = -1;
    ctx->file_size = 0;
    ctx->app_mode = APP_GET;
    build_header(ctx, 1, 0, 0);
    start_sending(ctx);
}

static void prepare_put(struct server_ctx *ctx) {
    uint8_t status = ctx->upload_error ? 1 : 0;

    if (!ctx->upload_error) {
        ctx->file_fd = open_upload_file(ctx, ctx->upload_path);
        if (ctx->file_fd < 0) {
            ctx->upload_error = true;
            status = 1;
        }
    }

    build_header(ctx, status, ctx->upload_total_len, 0);
    start_sending(ctx);
    ctx->upload_start_seq = ctx->rcv_nxt;
    ctx->upload_active = !ctx->upload_error;
    ctx->upload_done = (ctx->upload_total_len == 0);
    ctx->upload_received = 0;
    ooo_reset(ctx);
}

static void flush_ooo(struct server_ctx *ctx) {
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (int i = 0; i < RAWTCP_MAX_WINDOW_SEGS; ++i) {
            if (!ctx->ooo_buf[i].used) {
                continue;
            }
            if (ctx->ooo_buf[i].seq != ctx->rcv_nxt) {
                continue;
            }
            uint16_t seg_len = ctx->ooo_buf[i].len;
            uint64_t offset = (uint64_t)(ctx->rcv_nxt - ctx->upload_start_seq);
            if (offset < ctx->upload_total_len) {
                uint64_t remaining = ctx->upload_total_len - offset;
                uint16_t write_len = seg_len;
                if ((uint64_t)write_len > remaining) {
                    write_len = (uint16_t)remaining;
                }
                if (write_len > 0) {
                    ssize_t w = pwrite(ctx->file_fd, ctx->ooo_buf[i].data,
                                       (size_t)write_len, (off_t)offset);
                    if (w < 0 || (uint16_t)w != write_len) {
                        ctx->upload_error = true;
                        ctx->state = STATE_CLOSED;
                        return;
                    }
                }
            }
            ctx->rcv_nxt += seg_len;
            ctx->ooo_buf[i].used = false;
            ctx->ooo_count--;
            progressed = true;
            break;
        }
    }
    uint64_t contig = ctx->rcv_nxt - ctx->upload_start_seq;
    if (contig > ctx->upload_total_len) {
        contig = ctx->upload_total_len;
    }
    ctx->upload_received = contig;
    if (ctx->upload_received >= ctx->upload_total_len) {
        ctx->upload_done = true;
    }
}

static void handle_upload_inorder(struct server_ctx *ctx,
                                  const uint8_t *payload, int payload_len) {
    if (payload_len <= 0) {
        return;
    }
    if (!ctx->upload_active || ctx->upload_error) {
        ctx->rcv_nxt += (uint32_t)payload_len;
        return;
    }

    uint64_t offset = (uint64_t)(ctx->rcv_nxt - ctx->upload_start_seq);
    int write_len = payload_len;
    if (offset < ctx->upload_total_len) {
        uint64_t remaining = ctx->upload_total_len - offset;
        if ((uint64_t)write_len > remaining) {
            write_len = (int)remaining;
        }
        if (write_len > 0) {
            ssize_t w = pwrite(ctx->file_fd, payload, (size_t)write_len, (off_t)offset);
            if (w < 0 || w != write_len) {
                ctx->upload_error = true;
                ctx->state = STATE_CLOSED;
                return;
            }
        }
    }

    ctx->rcv_nxt += (uint32_t)payload_len;
    flush_ooo(ctx);
}

static void handle_upload_out_of_order(struct server_ctx *ctx,
                                       uint32_t seq,
                                       const uint8_t *payload,
                                       int payload_len) {
    if (!ctx->upload_active || ctx->upload_error) {
        send_ack(ctx);
        return;
    }
    if (seq < ctx->rcv_nxt) {
        send_ack(ctx);
        return;
    }
    uint64_t offset = (uint64_t)(seq - ctx->upload_start_seq);
    if (offset >= ctx->upload_total_len) {
        send_ack(ctx);
        return;
    }
    uint16_t len = (uint16_t)payload_len;
    if (len > RAWTCP_MAX_MSS) {
        len = RAWTCP_MAX_MSS;
    }
    uint64_t remaining = ctx->upload_total_len - offset;
    uint16_t write_len = len;
    if ((uint64_t)write_len > remaining) {
        write_len = (uint16_t)remaining;
    }
    if (write_len > 0) {
        ssize_t w = pwrite(ctx->file_fd, payload, (size_t)write_len, (off_t)offset);
        if (w < 0 || (uint16_t)w != write_len) {
            ctx->upload_error = true;
            ctx->state = STATE_CLOSED;
            return;
        }
    }
    (void)ooo_store(ctx, seq, payload, len);
    send_ack(ctx);
}

/* Accept request bytes or upload payload and ACK. */
static void handle_payload(struct server_ctx *ctx, uint32_t seq, const uint8_t *payload, int payload_len) {
    if (payload_len <= 0) {
        return;
    }

    if (seq != ctx->rcv_nxt) {
        if (ctx->app_mode == APP_PUT && ctx->upload_active) {
            handle_upload_out_of_order(ctx, seq, payload, payload_len);
        } else {
            send_ack(ctx);
        }
        return;
    }

    int idx = 0;
    while (idx < payload_len) {
        if (!ctx->request_ready) {
            char c = (char)payload[idx];
            if (ctx->req_len + 1 < sizeof(ctx->req_buf)) {
                ctx->req_buf[ctx->req_len++] = c;
                ctx->req_buf[ctx->req_len] = '\0';
            }
            idx++;
            ctx->rcv_nxt += 1;
            if (c == '\n') {
                handle_request_ready(ctx);
            }
            continue;
        }

        if (ctx->app_mode == APP_PUT && !ctx->put_size_ready) {
            int need = 8 - (int)ctx->put_size_read;
            int avail = payload_len - idx;
            int take = need < avail ? need : avail;
            memcpy(ctx->put_size_buf + ctx->put_size_read, payload + idx, (size_t)take);
            ctx->put_size_read += (size_t)take;
            ctx->rcv_nxt += (uint32_t)take;
            idx += take;
            if (ctx->put_size_read == 8) {
                ctx->put_size_ready = true;
                ctx->upload_total_len = parse_be64(ctx->put_size_buf);
                prepare_put(ctx);
            }
            continue;
        }

        if (ctx->app_mode == APP_PUT && ctx->upload_active) {
            int remaining = payload_len - idx;
            handle_upload_inorder(ctx, payload + idx, remaining);
            idx = payload_len;
            continue;
        }

        int remaining = payload_len - idx;
        ctx->rcv_nxt += (uint32_t)remaining;
        idx = payload_len;
    }

    send_ack(ctx);
}

/* Process FIN from client. */
static void handle_fin(struct server_ctx *ctx, uint32_t seq, int payload_len) {
    uint32_t fin_seq = seq + (uint32_t)payload_len;
    if (fin_seq == ctx->rcv_nxt) {
        ctx->rcv_nxt += 1;
        ctx->client_fin_received = true;
        send_ack(ctx);
        if (!ctx->fin_sent && !ctx->sending) {
            if (ctx->app_mode == APP_PUT && ctx->upload_active && !ctx->upload_done) {
                ctx->upload_error = true;
            }
            ctx->fin_seq = ctx->snd_nxt;
            ctx->fin_sent = true;
            ctx->snd_nxt = ctx->fin_seq + 1;
            send_fin(ctx);
            ctx->state = STATE_FIN_WAIT;
        }
    }
}

static void maybe_send_upload_fin(struct server_ctx *ctx) {
    if (ctx->app_mode != APP_PUT || ctx->upload_error) {
        return;
    }
    if (!ctx->upload_done || !ctx->client_fin_received || ctx->fin_sent) {
        return;
    }
    ctx->fin_seq = ctx->snd_nxt;
    ctx->fin_sent = true;
    ctx->snd_nxt = ctx->fin_seq + 1;
    send_fin(ctx);
    ctx->state = STATE_FIN_WAIT;
}

/* Parse incoming IP/TCP packet and drive state machine. */
static void process_packet(struct server_ctx *ctx, const uint8_t *buf, int len) {
    if (len < (int)sizeof(struct iphdr)) {
        return;
    }

    const struct iphdr *ip = (const struct iphdr *)buf;
    if (ip->protocol != IPPROTO_TCP) {
        return;
    }

    int ip_len = ip->ihl * 4;
    if (len < ip_len + (int)sizeof(struct tcphdr)) {
        return;
    }

    const struct tcphdr *tcp = (const struct tcphdr *)(buf + ip_len);
    int tcp_len = tcp->doff * 4;
    if (len < ip_len + tcp_len) {
        return;
    }

    int payload_len = len - ip_len - tcp_len;
    const uint8_t *payload = buf + ip_len + tcp_len;
    int opt_len = tcp_len - (int)sizeof(struct tcphdr);
    const uint8_t *opts = (const uint8_t *)tcp + sizeof(struct tcphdr);
    struct rawtcp_option_state opt_state;
    memset(&opt_state, 0, sizeof(opt_state));
    if (opt_len > 0) {
        rawtcp_parse_options(opts, opt_len, &opt_state);
    }

    uint16_t dst_port = ntohs(tcp->dest);
    uint16_t src_port = ntohs(tcp->source);
    uint16_t win_field = ntohs(tcp->window);

    if (dst_port != ctx->listen_port) {
        return;
    }

    uint32_t seq = ntohl(tcp->seq);
    uint32_t ack_seq = ntohl(tcp->ack_seq);

    if (tcp->rst) {
        reset_connection(ctx);
        return;
    }

    if (ctx->state == STATE_LISTEN) {
        if (tcp->syn) {
            ctx->client_ip = ip->saddr;
            ctx->client_port = src_port;
            ctx->server_ip = ip->daddr;
            ctx->client_isn = seq;
            ctx->rcv_nxt = seq + 1;
            if (opt_state.mss_present) {
                ctx->peer_mss = opt_state.mss;
            } else {
                ctx->peer_mss = RAWTCP_MIN_MSS;
            }
            if (ctx->peer_mss < RAWTCP_MIN_MSS) {
                ctx->peer_mss = RAWTCP_MIN_MSS;
            }
            if (ctx->peer_mss > RAWTCP_MAX_MSS) {
                ctx->peer_mss = RAWTCP_MAX_MSS;
            }
            ctx->eff_mss = ctx->local_mss;
            if (ctx->peer_mss < ctx->eff_mss) {
                ctx->eff_mss = ctx->peer_mss;
            }
            ctx->mss = ctx->eff_mss;
            ctx->peer_ws = opt_state.ws_present ? opt_state.ws : 0;
            ctx->ws_enabled = opt_state.ws_present;
            ctx->sack_permitted = ctx->sack_enabled && opt_state.sack_permitted;
            ctx->peer_window_bytes = win_field;
            ctx->snd_iss = (uint32_t)(rawtcp_now_ms() & 0xFFFFFFF0U);
            ctx->snd_nxt = ctx->snd_iss + 1;
            ctx->syn_last_sent_ms = rawtcp_now_ms();
            ctx->syn_retransmits = 0;
            ctx->state = STATE_SYN_RECEIVED;
            send_synack(ctx);
        }
        return;
    }

    if (ctx->client_ip != ip->saddr || ctx->client_port != src_port) {
        return;
    }

    if (ctx->state == STATE_SYN_RECEIVED) {
        if (tcp->ack && ack_seq == ctx->snd_iss + 1) {
            ctx->state = STATE_ESTABLISHED;
        }
        if (tcp->ack) {
            update_peer_window(ctx, win_field);
        }
        if (payload_len > 0 && seq == ctx->rcv_nxt) {
            handle_payload(ctx, seq, payload, payload_len);
        }
        if (tcp->fin) {
            handle_fin(ctx, seq, payload_len);
        }
        if (tcp->ack) {
            handle_ack(ctx, ack_seq, &opt_state);
        }
        return;
    }

    if (ctx->state == STATE_ESTABLISHED || ctx->state == STATE_FIN_WAIT) {
        if (payload_len > 0) {
            handle_payload(ctx, seq, payload, payload_len);
        } else if (seq != ctx->rcv_nxt) {
            send_ack(ctx);
        }

        if (tcp->fin) {
            handle_fin(ctx, seq, payload_len);
        }

        if (tcp->ack) {
            update_peer_window(ctx, win_field);
            handle_ack(ctx, ack_seq, &opt_state);
        }
    }
}

/* Retransmit SYN+ACK if handshake ACK not received. */
static void handle_syn_retransmit(struct server_ctx *ctx) {
    if (ctx->state != STATE_SYN_RECEIVED) {
        return;
    }
    uint64_t now = rawtcp_now_ms();
    if (now - ctx->syn_last_sent_ms < RAWTCP_SYN_TIMEOUT_MS) {
        return;
    }
    if (ctx->syn_retransmits >= RAWTCP_MAX_RETRANS) {
        reset_connection(ctx);
        return;
    }
    send_synack(ctx);
    ctx->syn_last_sent_ms = now;
    ctx->syn_retransmits++;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    struct server_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

    ctx.listen_port = (uint16_t)atoi(argv[1]);
    if (ctx.listen_port == 0) {
        usage(argv[0]);
        return 1;
    }

    strncpy(ctx.root_dir, argv[2], sizeof(ctx.root_dir) - 1);
    ctx.root_dir[sizeof(ctx.root_dir) - 1] = '\0';

    ctx.local_mss = RAWTCP_DEFAULT_MSS;
    ctx.mss = ctx.local_mss;
    ctx.window_segs = RAWTCP_DEFAULT_WINDOW_SEGS;
    ctx.local_ws = RAWTCP_DEFAULT_WS;
    ctx.sack_enabled = RAWTCP_ENABLE_SACK ? true : false;

    if (argc >= 4) {
        ctx.local_mss = (uint16_t)atoi(argv[3]);
        if (ctx.local_mss < RAWTCP_MIN_MSS || ctx.local_mss > RAWTCP_MAX_MSS) {
            ctx.local_mss = RAWTCP_DEFAULT_MSS;
        }
    }

    if (argc >= 5) {
        ctx.window_segs = atoi(argv[4]);
        if (ctx.window_segs <= 0 || ctx.window_segs > RAWTCP_MAX_WINDOW_SEGS) {
            ctx.window_segs = RAWTCP_DEFAULT_WINDOW_SEGS;
        }
    }

    if (argc >= 6) {
        ctx.local_ws = (uint8_t)atoi(argv[5]);
        if (ctx.local_ws > RAWTCP_MAX_WS) {
            ctx.local_ws = RAWTCP_DEFAULT_WS;
        }
    }

    if (argc >= 7) {
        int sack_flag = atoi(argv[6]);
        ctx.sack_enabled = (sack_flag != 0);
    }

    ctx.local_window_bytes = 0xFFFFU;
    if (ctx.local_ws > 0) {
        ctx.local_window_bytes = 0xFFFFU << ctx.local_ws;
    }

    ctx.file_fd = -1;
    reset_connection(&ctx);

    ctx.sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (ctx.sock < 0) {
        perror("socket");
        return 1;
    }

    int on = 1;
    if (setsockopt(ctx.sock, IPPROTO_IP, IP_HDRINCL, &on, sizeof(on)) < 0) {
        perror("setsockopt IP_HDRINCL");
        close(ctx.sock);
        return 1;
    }

    printf("[rawtcp] listen port %u, root %s, mss %u, window %d, ws %u, sack %s\n",
           ctx.listen_port, ctx.root_dir, ctx.local_mss, ctx.window_segs,
           ctx.local_ws, ctx.sack_enabled ? "on" : "off");

    for (;;) {
        struct pollfd pfd;
        pfd.fd = ctx.sock;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 50);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[RAWTCP_MAX_PACKET_SIZE];
            ssize_t n = recvfrom(ctx.sock, buf, sizeof(buf), 0, NULL, NULL);
            if (n > 0) {
                process_packet(&ctx, buf, (int)n);
            }
        }

        handle_syn_retransmit(&ctx);
        if (ctx.state == STATE_ESTABLISHED || ctx.state == STATE_FIN_WAIT) {
            retransmit_if_needed(&ctx);
            maybe_send_data(&ctx);
            maybe_send_upload_fin(&ctx);
        }

        if (ctx.fin_sent && ctx.client_fin_received && ctx.fin_acked) {
            reset_connection(&ctx);
        }

        if (ctx.state == STATE_CLOSED) {
            reset_connection(&ctx);
        }
    }

    return 0;
}
