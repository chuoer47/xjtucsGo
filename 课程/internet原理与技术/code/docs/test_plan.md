# 测试说明

## 环境
- Linux
- Root 权限（Raw Socket）
- 可选：iptables 屏蔽 RST

## 测试步骤
1. 编译
```bash
make
```

2. 启动服务端
```bash
sudo ./rawtcp_server 9090 ./tests/data
```
可选：指定 MSS/WindowScale/SACK
```bash
sudo ./rawtcp_server 9090 ./tests/data 1200 16 8 1
```

3. 使用客户端请求
```bash
./tcp_client 127.0.0.1 9090 download small.txt ./out_small.txt
./tcp_client 127.0.0.1 9090 upload ./out_small.txt uploaded_small.txt
```

4. 对比文件内容
```bash
cmp -s ./tests/data/small.txt ./out_small.txt
```

## 重点验证
- 小/中/大文件下载
- 小/中/大文件上传
- 上传失败场景（本地文件不存在 / 服务端目标已存在）
- 丢包情况下的重传（可通过 tc/netem 制造丢包）
- Wireshark 抓包检查 TCP 报文
- MSS/Window Scale/SACK 选项是否出现在握手与 ACK 中
