#!/bin/bash
set -euo pipefail

echo "--- Kernel & Active Sysctls ---"
uname -a
echo "CC available: $(cat /proc/sys/net/ipv4/tcp_available_congestion_control)"
echo "Default qdisc: $(cat /proc/sys/net/core/default_qdisc)"

echo "--- Struct Sizes & Protocol Census (/proc/net/protocols) ---"
cat /proc/net/protocols

echo "--- Running NetZero C ABI & Cycle Benchmark Suite ---"
/tmp/work/benchmarks/netzero_bench

echo "--- Running Layer 5B WAN Bottleneck Bufferbloat & L4S Suite ---"
/tmp/work/vm-verify/wan-bench.sh

echo "--- Verifying ss (NETLINK_SOCK_DIAG) Compatibility ---"
ss -s
ss -tan | head -n 5
ss -xan | head -n 5
