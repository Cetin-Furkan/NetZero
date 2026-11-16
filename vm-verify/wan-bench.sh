#!/bin/bash
set -euo pipefail

BIN="/tmp/work/benchmarks/wan_bloat_bench"

cleanup() {
    ip netns pids ns_srv 2>/dev/null | xargs -r kill -9 2>/dev/null || true
    ip netns del ns_cli 2>/dev/null || true
    ip netns del ns_rtr 2>/dev/null || true
    ip netns del ns_srv 2>/dev/null || true
}
trap cleanup EXIT
cleanup

ip netns add ns_cli
ip netns add ns_rtr
ip netns add ns_srv

ip link add veth_c type veth peer name veth_rc
ip link set veth_c netns ns_cli
ip link set veth_rc netns ns_rtr

ip link add veth_rs type veth peer name veth_s
ip link set veth_rs netns ns_rtr
ip link set veth_s netns ns_srv

ip -n ns_cli link set lo up
ip -n ns_cli addr add 10.99.1.2/24 dev veth_c
ip -n ns_cli link set veth_c txqueuelen 2048 up
ip -n ns_cli route add default via 10.99.1.1

ip -n ns_rtr link set lo up
ip -n ns_rtr addr add 10.99.1.1/24 dev veth_rc
ip -n ns_rtr addr add 10.99.2.1/24 dev veth_rs
ip -n ns_rtr link set veth_rc txqueuelen 2048 up
ip -n ns_rtr link set veth_rs txqueuelen 2048 up
ip netns exec ns_rtr sysctl -qw net.ipv4.ip_forward=1

ip -n ns_srv link set lo up
ip -n ns_srv addr add 10.99.2.2/24 dev veth_s
ip -n ns_srv link set veth_s txqueuelen 2048 up
ip -n ns_srv route add default via 10.99.2.1

# 10 ms propagation RTT on WAN link (server return path)
ip netns exec ns_srv tc qdisc add dev veth_s root netem delay 10ms limit 10000

# 100 Mbit/s broadband/Wi-Fi bottleneck on router WAN uplink (2 MiB max buffer)
ip netns exec ns_rtr tc qdisc add dev veth_rs root handle 1: tbf rate 100mbit burst 128kb limit 2097152

if ip netns exec ns_cli tc qdisc replace dev veth_c root eiffel_dualq 2>/dev/null; then
    echo "[WAN Testbed] Mode: NetZero (Sender=eiffel_dualq TCQ_F_NOLOCK + Bottleneck=tbf/eiffel_dualq + AccECN=3)"
    ip netns exec ns_rtr tc qdisc add dev veth_rs parent 1:1 handle 10: eiffel_dualq
    ip netns exec ns_cli sysctl -qw net.ipv4.tcp_ecn=3
    ip netns exec ns_srv sysctl -qw net.ipv4.tcp_ecn=3
else
    echo "[WAN Testbed] Mode: Baseline Stock (Sender=pfifo_fast + Bottleneck=tbf/bfifo + Default ECN)"
    ip netns exec ns_cli tc qdisc replace dev veth_c root pfifo_fast 2>/dev/null || true
fi

ip netns exec ns_srv "$BIN" --server &
SRV_PID=$!
sleep 0.15

ip netns exec ns_cli "$BIN" --client 10.99.2.2 bbr 4 "L5B_wan_4xbbr_plus_rpc"
sleep 0.20
ip netns exec ns_cli "$BIN" --client 10.99.2.2 cubic 2 "L5B_wan_2xcubic_plus_rpc"

echo "--- Bottleneck Qdisc Telemetry ---"
ip netns exec ns_cli tc -s qdisc show dev veth_c
ip netns exec ns_rtr tc -s qdisc show dev veth_rs

kill -9 "$SRV_PID" 2>/dev/null || true
