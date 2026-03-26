#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT_DIR"

if [[ ${EUID} -ne 0 ]]; then
  echo "Please run as root (raw socket requires CAP_NET_RAW)."
  exit 1
fi

BLOCK_RST=${BLOCK_RST:-0}
RST_RULE='-p tcp --tcp-flags RST RST -j DROP'
ADDED_RST=0

cleanup() {
  if [[ $ADDED_RST -eq 1 ]]; then
    iptables -D OUTPUT $RST_RULE || true
  fi
}
trap cleanup EXIT

if [[ "$BLOCK_RST" == "1" ]]; then
  if ! iptables -C OUTPUT $RST_RULE >/dev/null 2>&1; then
    iptables -A OUTPUT $RST_RULE
    ADDED_RST=1
  fi
fi

make

mkdir -p tests/data

if [[ ! -f tests/data/small.txt ]]; then
  echo "hello rawtcp" > tests/data/small.txt
fi

if [[ ! -f tests/data/medium.bin ]]; then
  dd if=/dev/urandom of=tests/data/medium.bin bs=1M count=1 status=none
fi

if [[ ! -f tests/data/large.bin ]]; then
  dd if=/dev/urandom of=tests/data/large.bin bs=1M count=25 status=none
fi

rm -f tests/data/upload_small.txt tests/data/upload_medium.bin tests/data/upload_large.bin tests/data/upload_empty.bin

./rawtcp_server 9090 tests/data &
SERVER_PID=$!

sleep 1

echo "==> Test: download small file"
./tcp_client 127.0.0.1 9090 download small.txt /tmp/rawtcp_small.out
echo "==> Test: download medium file"
./tcp_client 127.0.0.1 9090 download medium.bin /tmp/rawtcp_medium.out
echo "==> Test: download large file"
./tcp_client 127.0.0.1 9090 download large.bin /tmp/rawtcp_large.out

cmp -s tests/data/small.txt /tmp/rawtcp_small.out
cmp -s tests/data/medium.bin /tmp/rawtcp_medium.out
cmp -s tests/data/large.bin /tmp/rawtcp_large.out

echo "==> Test: download missing file"
if ./tcp_client 127.0.0.1 9090 download no_such.bin /tmp/rawtcp_missing.out; then
  echo "Expected missing-file download to fail."
  kill "$SERVER_PID"
  wait "$SERVER_PID" 2>/dev/null || true
  exit 1
fi

echo "==> Test: upload small file"
./tcp_client 127.0.0.1 9090 upload tests/data/small.txt upload_small.txt
echo "==> Test: upload medium file"
./tcp_client 127.0.0.1 9090 upload tests/data/medium.bin upload_medium.bin
echo "==> Test: upload large file"
./tcp_client 127.0.0.1 9090 upload tests/data/large.bin upload_large.bin

cmp -s tests/data/small.txt tests/data/upload_small.txt
cmp -s tests/data/medium.bin tests/data/upload_medium.bin
cmp -s tests/data/large.bin tests/data/upload_large.bin

if [[ ! -f tests/data/empty.bin ]]; then
  : > tests/data/empty.bin
fi
echo "==> Test: upload empty file"
./tcp_client 127.0.0.1 9090 upload tests/data/empty.bin upload_empty.bin
cmp -s tests/data/empty.bin tests/data/upload_empty.bin

echo "==> Test: upload missing local file"
if ./tcp_client 127.0.0.1 9090 upload /tmp/no_such_file.bin upload_missing.bin; then
  echo "Expected missing-file upload to fail."
  kill "$SERVER_PID"
  wait "$SERVER_PID" 2>/dev/null || true
  exit 1
fi

echo "==> Test: download path traversal"
if ./tcp_client 127.0.0.1 9090 download ../small.txt /tmp/rawtcp_traversal.out; then
  echo "Expected path traversal download to fail."
  kill "$SERVER_PID"
  wait "$SERVER_PID" 2>/dev/null || true
  exit 1
fi

echo "==> Test: upload path traversal"
if ./tcp_client 127.0.0.1 9090 upload tests/data/small.txt ../bad.txt; then
  echo "Expected path traversal upload to fail."
  kill "$SERVER_PID"
  wait "$SERVER_PID" 2>/dev/null || true
  exit 1
fi

echo "==> Test: upload existing target"
if ./tcp_client 127.0.0.1 9090 upload tests/data/small.txt small.txt; then
  echo "Expected upload to existing file to fail."
  kill "$SERVER_PID"
  wait "$SERVER_PID" 2>/dev/null || true
  exit 1
fi

kill "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null || true

./rawtcp_server 9091 tests/data 1200 16 8 1 &
SERVER_PID=$!
sleep 1
echo "==> Test: MSS/WS/SACK options (port 9091)"
./tcp_client 127.0.0.1 9091 download small.txt /tmp/rawtcp_mss.out
cmp -s tests/data/small.txt /tmp/rawtcp_mss.out

echo "All tests passed."

kill "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null || true
