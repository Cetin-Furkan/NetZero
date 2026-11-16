#!/bin/bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /dev/shm
mount -t tmpfs tmpfs /tmp
mount -t tmpfs tmpfs /run
chmod 1777 /tmp /dev/shm

mkdir -p /tmp/work
mount -t 9p -o trans=virtio,version=9p2000.L,msize=524288,rw workshare /tmp/work

ip link set lo up 2>/dev/null || true
sysctl -w net.ipv4.ip_local_port_range="10000 65000" >/dev/null 2>&1 || true

echo "============================================================"
echo "[NetZero KVM] Booted: $(uname -srm)"
echo "[NetZero KVM] CPUs: $(nproc) | Mem: $(awk '/MemTotal/{print $2 " " $3}' /proc/meminfo)"
echo "============================================================"

CMD=""
for arg in $(cat /proc/cmdline); do
    case "$arg" in
        netzero_sh=*)
            CMD="${arg#netzero_sh=}"
            ;;
    esac
done

RC=0
if [ -n "$CMD" ] && [ -x "$CMD" ]; then
    "$CMD" || RC=$?
elif [ -x /tmp/work/vm-verify/verify-v2-payload.sh ]; then
    /tmp/work/vm-verify/verify-v2-payload.sh || RC=$?
fi

echo "[NetZero KVM] Payload finished with exit_code=$RC"
sync
echo o > /proc/sysrq-trigger
while :; do :; done


