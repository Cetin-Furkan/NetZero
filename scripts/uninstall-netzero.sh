#!/bin/bash
# Project NetZero v2 — Clean Side-by-Side Uninstaller
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo: sudo $0" >&2
    exit 1
fi

echo "[1/4] Restoring default systemd-boot entry (if needed)..."
if [ -f /boot/loader/entries/cachyos-rc.conf ]; then
    bootctl set-default cachyos-rc.conf || true
fi

echo "[2/4] Removing /boot NetZero image, initramfs, and boot entry..."
rm -f /boot/loader/entries/netzero.conf \
      /boot/vmlinuz-linux-netzero \
      /boot/initramfs-linux-netzero.img

echo "[3/4] Removing /etc/sysctl.d/99-netzero.conf..."
rm -f /etc/sysctl.d/99-netzero.conf
sysctl --system >/dev/null 2>&1 || true

echo "[4/4] Removing /usr/lib/modules/*-netzero*..."
rm -rf /usr/lib/modules/*-netzero*

echo "[OK] Project NetZero completely removed. Reboot to run your stock kernel."
