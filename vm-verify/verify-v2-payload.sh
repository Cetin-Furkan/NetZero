#!/bin/bash
set -euo pipefail

echo "=== [1] Kernel & Sysctls ==="
uname -a
sysctl -w net.ipv4.tcp_ecn=2 >/dev/null || true
echo "Default qdisc: $(cat /proc/sys/net/core/default_qdisc)"
echo "TCP CC: $(cat /proc/sys/net/ipv4/tcp_congestion_control)"
echo "TCP ECN: $(cat /proc/sys/net/ipv4/tcp_ecn)"

echo "=== [2] Protocol Struct Sizes (/proc/net/protocols) ==="
cat /proc/net/protocols

echo "=== [3] NetZero ABI Torture & Microbenchmark Suite ==="
/tmp/work/benchmarks/netzero_bench

echo "=== [4] Grand Socket Bench (S2 + S3 + S4 including 64KB Stream & 8-Client Wayland Sim) ==="
/tmp/work/benchmarks/grand_socket_bench --trial 0

echo "=== [5] Kernel Log Warning / Error Audit ==="
dmesg --level=emerg,alert,crit,err,warn || true
echo "=== [DONE] NetZero v2 KVM Verification Complete ==="
