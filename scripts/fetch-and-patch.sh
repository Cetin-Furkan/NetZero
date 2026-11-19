#!/bin/bash
# Project NetZero v3 — Automated Upstream Linux Fetch & Patch Script
# Clones upstream Linux at exact base commit 7e1392a42ea288d70a266f6553fe06f57f8647ea
# and applies patches/netzero-v3-7.4-rc2-full.patch cleanly.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${SRC_DIR:-$REPO_DIR/linux-src}"
BASE_COMMIT="7e1392a42ea288d70a266f6553fe06f57f8647ea"
PATCH_FILE="$REPO_DIR/patches/netzero-v3-7.4-rc2-full.patch"

if [ ! -d "$SRC_DIR/.git" ]; then
    echo "[1/3] Initializing Linux repository in $SRC_DIR (shallow fetch of $BASE_COMMIT)..."
    mkdir -p "$SRC_DIR"
    git -C "$SRC_DIR" init
    git -C "$SRC_DIR" remote add origin https://github.com/torvalds/linux.git
    git -C "$SRC_DIR" fetch --depth 1 origin "$BASE_COMMIT"
    git -C "$SRC_DIR" checkout FETCH_HEAD
else
    echo "[1/3] Existing Linux git tree found at $SRC_DIR."
fi

echo "[2/3] Checking patch applicability..."
if git -C "$SRC_DIR" apply --check "$PATCH_FILE" 2>/dev/null; then
    echo "[3/3] Applying Project NetZero v3 patch ($PATCH_FILE)..."
    git -C "$SRC_DIR" apply "$PATCH_FILE"
    echo "[OK] Successfully patched $SRC_DIR to Project NetZero v3!"
else
    echo "[INFO] Patch appears already applied or tree has local changes. (You can also overlay $REPO_DIR/kernel-overlay/ directly.)"
fi
