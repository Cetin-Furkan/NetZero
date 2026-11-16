#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BZIMAGE="${BZIMAGE:-$REPO_DIR/build-host/arch/x86/boot/bzImage}"
PAYLOAD="${1:-$REPO_DIR/vm-verify/verify-v2-payload.sh}"
SMP="${SMP:-8}"
MEM="${MEM:-4G}"

if [ ! -f "$BZIMAGE" ]; then
    echo "ERROR: Kernel image not found at $BZIMAGE (set BZIMAGE=/path/to/bzImage)" >&2
    exit 1
fi

chmod +x "$REPO_DIR/vm-verify/guest-init.sh" "$PAYLOAD" 2>/dev/null || true

GUEST_PAYLOAD="/tmp/work/${PAYLOAD#$REPO_DIR/}"

exec qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -smp "$SMP" \
    -m "$MEM" \
    -nic none \
    -nographic \
    -no-reboot \
    -kernel "$BZIMAGE" \
    -fsdev local,id=rootdev,path=/,security_model=none,readonly=on \
    -device virtio-9p-pci,fsdev=rootdev,mount_tag=/dev/root \
    -fsdev local,id=workdev,path="$REPO_DIR",security_model=none \
    -device virtio-9p-pci,fsdev=workdev,mount_tag=workshare \
    -append "root=/dev/root rootfstype=9p rootflags=trans=virtio,version=9p2000.L,msize=524288,cache=mmap ro console=ttyS0 quiet loglevel=3 panic=-1 sysrq_always_enabled=1 init=$REPO_DIR/vm-verify/guest-init.sh netzero_sh=$GUEST_PAYLOAD"
