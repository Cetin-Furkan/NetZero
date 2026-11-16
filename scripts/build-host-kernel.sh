#!/bin/bash
# Project NetZero v2 — Portable Host Kernel Builder
# Defaults to ARCH_MARCH=tigerlake (set ARCH_MARCH=native for AMD Zen / Intel Alder/Raptor Lake).
# Defaults to fast localmodconfig + full audio preservation (set FULL_CONFIG=1 for all distro modules).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${SRC_DIR:-$REPO_DIR/linux-src}"
OUT_DIR="${OUT_DIR:-$REPO_DIR/build-host}"
ARCH_MARCH="${ARCH_MARCH:-tigerlake}"

if [ ! -d "$SRC_DIR" ]; then
    echo "Linux source tree not found at $SRC_DIR."
    echo "Run '$REPO_DIR/scripts/fetch-and-patch.sh' first (or pass SRC_DIR=/path/to/linux)."
    exit 1
fi

echo "[1/4] Checking kernel source tree ($SRC_DIR)..."
cd "$SRC_DIR"
if [ "$(git config --bool core.sparseCheckout || echo false)" = "true" ]; then
    git sparse-checkout disable
fi

echo "[2/4] Importing base kernel config..."
mkdir -p "$OUT_DIR"
if [ -f /proc/config.gz ]; then
    zcat /proc/config.gz > "$OUT_DIR/.config"
elif [ -f "/boot/config-$(uname -r)" ]; then
    cp "/boot/config-$(uname -r)" "$OUT_DIR/.config"
else
    cp "$REPO_DIR/configs/tigerlake-host.config" "$OUT_DIR/.config"
fi
grep -E '^(CONFIG_SND|CONFIG_SOUNDWIRE|CONFIG_HDA)' "$OUT_DIR/.config" > "$OUT_DIR/.sound_keep.cfg" || true

if [ "${FULL_CONFIG:-0}" = "1" ]; then
    echo "[3/4] Preparing full hardware module config (FULL_CONFIG=1)..."
    make LLVM=1 O="$OUT_DIR" olddefconfig
else
    echo "[3/4] Trimming unused drivers via localmodconfig (set FULL_CONFIG=1 to build all modules)..."
    (yes "" || true) | make LLVM=1 O="$OUT_DIR" localmodconfig >/dev/null
    # Preserve all Intel SOF / HDA / ALC236 / SoundWire audio codecs & topologies
    cat "$OUT_DIR/.sound_keep.cfg" >> "$OUT_DIR/.config"
fi

"$SRC_DIR/scripts/config" --file "$OUT_DIR/.config" \
    --set-str CONFIG_LOCALVERSION "-netzero-v2" \
    --disable CONFIG_LOCALVERSION_AUTO \
    --enable CONFIG_LTO_CLANG_NONE \
    --disable CONFIG_LTO_CLANG_THIN \
    --disable CONFIG_LTO_CLANG_FULL \
    --enable CONFIG_DEBUG_INFO_NONE \
    --disable CONFIG_DEBUG_INFO \
    --disable CONFIG_DEBUG_INFO_DWARF5 \
    --disable CONFIG_DEBUG_INFO_BTF \
    --disable CONFIG_DEBUG_INFO_BTF_MODULES \
    --enable CONFIG_NET_SCH_EIFFEL_DUALQ \
    --enable CONFIG_TCP_CONG_BBR \
    --enable CONFIG_TCP_CONG_CUBIC \
    --enable CONFIG_DEFAULT_BBR \
    --disable CONFIG_DEFAULT_CUBIC \
    --module CONFIG_USB_STORAGE \
    --module CONFIG_USB_UAS \
    --module CONFIG_VFAT_FS \
    --module CONFIG_EXFAT_FS \
    --module CONFIG_NTFS3_FS \
    --module CONFIG_BT_HCIBTUSB \
    --module CONFIG_TUN

make LLVM=1 O="$OUT_DIR" olddefconfig

# Keep -mno-sse/-mno-avx AFTER -march so compilers schedule for target uarch + emit BMI1/BMI2/LZCNT/POPCNT/RDPID
# on GP registers without emitting unguarded XMM/ZMM instructions outside kernel_fpu_begin()/end().
NZ_KCFLAGS="-O3 -march=${ARCH_MARCH} -mtune=${ARCH_MARCH} -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx"
echo "[4/4] Compiling NetZero v2 kernel ($NZ_KCFLAGS) with $(nproc) threads..."
make LLVM=1 O="$OUT_DIR" KCFLAGS="$NZ_KCFLAGS" -j"$(nproc)" bzImage modules

KREL="$(make -s LLVM=1 O="$OUT_DIR" kernelrelease)"
echo "============================================================"
echo "[OK] Built Project NetZero v2 host kernel: $KREL"
echo "     Image: $OUT_DIR/arch/x86/boot/bzImage"
echo "     Next : sudo $REPO_DIR/scripts/install-netzero.sh"
echo "============================================================"
