#ifndef RAWTCP_H
#define RAWTCP_H

#include <stdint.h>
#include <stdbool.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#define RAWTCP_MAX_PACKET_SIZE 65535
#define RAWTCP_TCP_OPT_MAX 40

#define RAWTCP_MIN_MSS 536
#define RAWTCP_DEFAULT_MSS 1460
#define RAWTCP_MAX_MSS 1460

#define RAWTCP_DEFAULT_WINDOW_SEGS 16
#define RAWTCP_MAX_WINDOW_SEGS 64

#define RAWTCP_DEFAULT_WS 4
#define RAWTCP_MAX_WS 14

#define RAWTCP_RETRANS_TIMEOUT_MS 200
#define RAWTCP_SYN_TIMEOUT_MS 500
#define RAWTCP_MAX_RETRANS 15

#define RAWTCP_ENABLE_SACK 1
#define RAWTCP_MAX_SACK_BLOCKS 4

#define RAWTCP_OPT_END 0
#define RAWTCP_OPT_NOP 1
#define RAWTCP_OPT_MSS 2
#define RAWTCP_OPT_WS 3
#define RAWTCP_OPT_SACK_PERM 4
#define RAWTCP_OPT_SACK 5

struct rawtcp_sack_block {
    uint32_t left_edge;
    uint32_t right_edge;
};

struct rawtcp_option_state {
    bool mss_present;
    uint16_t mss;
    bool ws_present;
    uint8_t ws;
    bool sack_permitted;
    struct rawtcp_sack_block sack_blocks[RAWTCP_MAX_SACK_BLOCKS];
    int sack_block_count;
};

uint16_t rawtcp_checksum(const void *buf, int len);
uint16_t rawtcp_tcp_checksum(const struct iphdr *ip,
                             const uint8_t *tcp_hdr, int tcp_hdr_len,
                             const uint8_t *payload, int payload_len);
int rawtcp_send_packet(int sock,
                       uint32_t src_ip, uint32_t dst_ip,
                       uint16_t src_port, uint16_t dst_port,
                       uint32_t seq, uint32_t ack,
                       uint16_t flags,
                       uint16_t window,
                       const uint8_t *payload, int payload_len);
int rawtcp_send_packet_ex(int sock,
                          uint32_t src_ip, uint32_t dst_ip,
                          uint16_t src_port, uint16_t dst_port,
                          uint32_t seq, uint32_t ack,
                          uint16_t flags,
                          uint16_t window,
                          const uint8_t *options, int options_len,
                          const uint8_t *payload, int payload_len);

int rawtcp_mss_pack(uint16_t mss, uint8_t *out, int out_len);
int rawtcp_ws_pack(uint8_t ws, uint8_t *out, int out_len);
int rawtcp_sack_perm_pack(uint8_t *out, int out_len);
int rawtcp_sack_pack(const struct rawtcp_sack_block *blocks, int block_count,
                     uint8_t *out, int out_len);
int rawtcp_build_syn_options(uint16_t mss, bool sack_permitted, uint8_t ws,
                             uint8_t *out, int out_len);
int rawtcp_parse_options(const uint8_t *opts, int opt_len,
                         struct rawtcp_option_state *out);
void rawtcp_sack_add_block(struct rawtcp_sack_block *blocks, int *count,
                           struct rawtcp_sack_block block);
int rawtcp_sack_parse(const uint8_t *opts, int opt_len,
                      struct rawtcp_sack_block *blocks, int max_blocks);
uint32_t rawtcp_window_scale_apply(uint16_t window, uint8_t scale);
uint16_t rawtcp_window_scale_compose(uint32_t window, uint8_t scale);

uint64_t rawtcp_now_ms(void);

#endif
