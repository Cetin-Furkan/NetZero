#!/bin/bash
# Project NetZero v2 — Safe Side-by-Side Installer (Arch / CachyOS + systemd-boot)
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo: sudo $0" >&2
    exit 1
fi

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${SRC_DIR:-$REPO_DIR/linux-src}"
OUT_DIR="${OUT_DIR:-$REPO_DIR/build-host}"

if [ ! -f "$OUT_DIR/arch/x86/boot/bzImage" ]; then
    echo "ERROR: $OUT_DIR/arch/x86/boot/bzImage not found. Run scripts/build-host-kernel.sh first." >&2
    exit 1
fi

KREL="$(make -s LLVM=1 -C "$SRC_DIR" O="$OUT_DIR" kernelrelease)"
echo "[1/6] Installing kernel modules for $KREL..."
make LLVM=1 -C "$SRC_DIR" O="$OUT_DIR" modules_install

echo "[2/6] Installing /boot/vmlinuz-linux-netzero (existing kernels untouched)..."
install -Dm644 "$OUT_DIR/arch/x86/boot/bzImage" /boot/vmlinuz-linux-netzero

echo "[3/6] Generating /boot/initramfs-linux-netzero.img via mkinitcpio..."
mkinitcpio -k "$KREL" -g /boot/initramfs-linux-netzero.img

echo "[4/6] Creating systemd-boot entry /boot/loader/entries/netzero.conf..."
BOOT_OPTS="$(grep -m1 '^options ' /boot/loader/entries/cachyos-rc.conf 2>/dev/null || grep -m1 '^options ' /boot/loader/entries/*.conf | head -n1)"
if [ -z "$BOOT_OPTS" ]; then
    echo "ERROR: Could not detect kernel cmdline options from /boot/loader/entries/*.conf" >&2
    exit 1
fi

UCODE_LINE=""
if [ -f /boot/intel-ucode.img ]; then
    UCODE_LINE="initrd  /intel-ucode.img"
elif [ -f /boot/amd-ucode.img ]; then
    UCODE_LINE="initrd  /amd-ucode.img"
fi

cat > /boot/loader/entries/netzero.conf <<EOF
title   Arch Linux (Project NetZero $KREL)
linux   /vmlinuz-linux-netzero
${UCODE_LINE}
initrd  /initramfs-linux-netzero.img
$BOOT_OPTS
EOF

echo "[5/6] Installing /etc/sysctl.d/99-netzero.conf (WAN-clean ECN + Eiffel DualQ L4S + BBR)..."
install -Dm644 "$REPO_DIR/configs/99-netzero.conf" /etc/sysctl.d/99-netzero.conf

echo "[6/6] Arming safe one-shot boot into netzero.conf for your next reboot..."
bootctl set-oneshot netzero.conf

echo "============================================================"
echo "[OK] Installed & armed Project NetZero ($KREL)!"
echo "  -> Run 'sudo reboot' now to boot into NetZero."
echo "  -> Safety net: if you reboot again without setting default, systemd-boot"
echo "     automatically returns to your previous default kernel."
echo "  -> Once verified after reboot, make NetZero permanent with:"
echo "     sudo bootctl set-default netzero.conf"
echo "============================================================"
