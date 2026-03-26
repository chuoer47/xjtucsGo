# Raw Socket TCP 文件下载/上传服务

## 概述
- 服务端：使用 Raw Socket 手动构造 TCP/IP 报文，实现简化 TCP（握手/可靠传输/挥手）。
- 客户端：使用系统标准 TCP，支持下载与上传。

## 编译
```bash
make
```

## 运行
> 需要 root 或 CAP_NET_RAW 权限。

## Windows 说明（重要）
- 本项目服务端基于 Linux Raw Socket 实现 TCP 报文构造，依赖 `IP_HDRINCL` 与 Linux 内核行为，无法直接在 Windows 上生成可用的 `.exe` 服务端程序。
- Windows 对原始 TCP 报文有严格限制（现代系统几乎不可用），因此无法保证在 Windows 上实现同等功能。
- 客户端使用标准 TCP，可考虑在 Windows 上重写/移植，但当前仓库未提供 Windows `.exe`。
### 1. 可选：阻止内核发送 RST
原始 Socket 处理同一端口时，内核可能发送 RST 影响连接。
```bash
sudo iptables -A OUTPUT -p tcp --tcp-flags RST RST -j DROP
```
测试完成后清理：
```bash
sudo iptables -D OUTPUT -p tcp --tcp-flags RST RST -j DROP
```

### 2. 启动服务端
```bash
sudo ./rawtcp_server 9090 ./tests/data
```
可选参数：`mss` `window_segs` `window_scale` `enable_sack`
```bash
sudo ./rawtcp_server 9090 ./tests/data 1460 16 4 1
```
参数说明：
- `mss`：SYN/SYN+ACK 中通告的 MSS，实际发送取双方协商后的最小值。
- `window_segs`：发送端内部窗口（段数），用于限制同时在途的段数量。
- `window_scale`：Window Scale 扩大因子（0-14）。
- `enable_sack`：是否启用 SACK（0/1），默认开启。

### 3. 启动客户端
```bash
./tcp_client 127.0.0.1 9090 download large.bin ./downloaded_large.bin
./tcp_client 127.0.0.1 9090 upload ./local.bin uploaded.bin
```
客户端会输出协商后的 MSS 和 SACK 状态（由内核 TCP 栈解析）。

## 应用层协议
- 下载请求：`GET <filename>\n`
- 上传请求：`PUT <filename>\n` + `8 bytes 文件大小(大端)`
- 响应头：`1 byte status + 8 bytes 长度(大端)`
  - 下载：长度为文件大小
  - 上传：长度为允许上传的最大值（当前与客户端文件大小一致）

## 测试
```bash
sudo BLOCK_RST=1 ./tests/run_tests.sh
```

## 目录结构
- `src/`：服务端与客户端源代码
- `include/`：公共头文件
- `docs/`：实现说明与测试说明
- `tests/`：测试脚本与测试用例
