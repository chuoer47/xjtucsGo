# 原始 Socket TCP 模拟实现说明

## 总体结构
- `src/rawtcp_server.c`：原始 Socket TCP 服务器 + 文件下载/上传逻辑。
- `src/rawtcp.c` + `include/rawtcp.h`：IP/TCP 头构造、校验和、发送等基础能力。
- `src/tcp_client.c`：标准 TCP 客户端（下载/上传）。

## 传输层实现（子集）

### 三次握手
- 客户端发起 `SYN`。
- 服务器返回 `SYN+ACK`（随机初始序列号）。
- 客户端 `ACK` 后进入 `ESTABLISHED`。

### 数据传输与可靠性
- 服务器维护 `snd_iss/snd_nxt/rcv_nxt`。
- 采用简化 Go-Back-N（可结合 SACK 优化重传）：
  - 维护固定窗口段数（默认 16 段）。
  - 累计 ACK 前移窗口。
  - 超时（200ms）重传未确认段；启用 SACK 时仅重传未被 SACK 的段。
- 客户端请求阶段只接受按序数据；上传阶段支持缓存乱序段并按序重组写入。
 - 发送窗口同时受“对端通告窗口（Window Scale 解析后）”与本地窗口段数限制。

### 四次挥手（简化）
- 服务器在数据发送完并确认后发送 `FIN`。
- 客户端 ACK 服务器 FIN。
- 客户端发送 FIN，服务器 ACK 后连接关闭。

## 应用层协议
- 下载请求：`GET <filename>\n`
- 上传请求：`PUT <filename>\n` + `8 bytes 文件大小(大端)`
- 响应头：`1 byte status + 8 bytes 长度(大端)`
  - 下载：长度为文件大小，随后是文件数据。
  - 上传：长度为允许上传的最大值（当前与客户端文件大小一致）。
  - `status=1`：失败，仅发送头部后关闭。

## 关键限制与说明
- 支持 MSS / Window Scale / SACK（仅在握手中协商，SACK 在 ACK 中携带）。
- 单连接顺序处理，多连接并发未实现。
- 需要 root 或 CAP_NET_RAW 权限。
- 服务器侧需阻止内核发送 RST，否则客户端会被内核重置。

## Wireshark 验证要点
- 握手：SYN → SYN+ACK → ACK
- 握手中包含 MSS / Window Scale / SACK Permitted 选项
- 数据段：序列号递增，ACK 累计确认
- 乱序场景可见 SACK 块与选择性重传
- 挥手：FIN → ACK → FIN → ACK
