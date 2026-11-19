#!/bin/bash
set -euo pipefail

echo "=== [1] Kernel & Sysctls ==="
uname -a
sysctl -w net.ipv4.tcp_ecn=2 >/dev/null || true
echo "Default qdisc: $(cat /proc/sys/net/core/default_qdisc)"
echo "TCP CC: $(cat /proc/sys/net/ipv4/tcp_congestion_control)"
echo "TCP ECN: $(cat /proc/sys/net/ipv4/tcp_ecn)"

IS_DEBUG=0
if [ -e /proc/lockdep_stats ] || [ -e /proc/lockdep ]; then
    IS_DEBUG=1
    echo "Kernel mode: DEBUG (lockdep active)"
else
    echo "Kernel mode: RELEASE"
fi

echo "=== [2] Protocol Struct Sizes (/proc/net/protocols) ==="
cat /proc/net/protocols

echo "=== [3] NetZero v3 Milestone 1 POSIX ABI Torture & Microbenchmark Suite ==="
/tmp/work/benchmarks/netzero_bench

if [ -x /usr/bin/dbus-broker ] && command -v python3 >/dev/null 2>&1; then
    echo "=== [3b] Real /usr/bin/dbus-broker + libgio GDBus Portal & SCM_RIGHTS Storm ==="
    python3 /tmp/work/benchmarks/dbus_broker_portal_torture.py 100
fi

echo "=== [4] Grand Socket Bench (S1-S4 + Milestone 1 Metrics) ==="
GRAND_OUT="/tmp/grand_v3_m1.jsonl"
rm -f "$GRAND_OUT"
if [ "$IS_DEBUG" -eq 1 ]; then
    echo "--- Running grand_socket_bench --trial 0 (debug kernel) ---"
    /tmp/work/grand_bench/grand_socket_bench --trial 0 | tee "$GRAND_OUT"
else
    TRIALS="${1:-3}"
    echo "--- Running grand_socket_bench $TRIALS (release kernel) ---"
    /tmp/work/grand_bench/grand_socket_bench "$TRIALS" | tee "$GRAND_OUT"
fi

if command -v python3 >/dev/null 2>&1 && [ -s "$GRAND_OUT" ]; then
    echo "=== [4b] Grand Socket Bench Summary (Mean / Median) ==="
    python3 - "$GRAND_OUT" <<'PYEOF'
import json, statistics, sys
from collections import defaultdict

rows = defaultdict(list)
meta = {}
with open(sys.argv[1], "r", encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if not line.startswith("{"):
            continue
        obj = json.loads(line)
        key = f"{obj['suite']}::{obj['metric']}"
        rows[key].append(float(obj["value"]))
        meta[key] = (obj["unit"], obj["better"])

for key in sorted(rows.keys()):
    vals = rows[key]
    unit, better = meta[key]
    mean = statistics.fmean(vals)
    med = statistics.median(vals)
    print(f"  {key:52s} | mean={mean:10.2f} | median={med:10.2f} | n={len(vals)} ({unit}, {better})")
PYEOF
fi

echo "=== [5] Kernel Log Warning / Error / Sanitizer Audit ==="
dmesg --level=emerg,alert,crit,err,warn || true

DMESG_LOG="/tmp/dmesg_audit.log"
dmesg > "$DMESG_LOG" || true
python3 - "$DMESG_LOG" <<'PYEOF'
import re
import sys

with open(sys.argv[1], "r", encoding="utf-8", errors="replace") as f:
    lines = f.read().splitlines()

if not lines:
    sys.stderr.write("[FAIL] dmesg output is empty; refusing to silently pass kernel log audit!\n")
    sys.exit(1)

SPLAT_RE = re.compile(
    r"BUG:|kernel BUG at|Oops:|WARNING:|------------\[ cut here \]------------|"
    r"UBSAN:|KASAN:|KCSAN:|general protection fault|Call Trace:|RIP: 0010:|"
    r"possible circular locking dependency|possible recursive locking|"
    r"inconsistent lock state|possible irq lock inversion|held lock freed|"
    r"INFO: suspicious RCU|sleeping function called from invalid context|"
    r"scheduling while atomic|list_del corruption|list_add corruption|"
    r"stack-out-of-bounds|use-after-free|slab-out-of-bounds|refcount_t:"
)

FORBIDDEN_IN_WX_RE = re.compile(
    r"BUG:|kernel BUG at|Oops:|UBSAN:|KASAN:|KCSAN:|general protection fault|"
    r"possible circular locking dependency|possible recursive locking|"
    r"inconsistent lock state|possible irq lock inversion|held lock freed|"
    r"INFO: suspicious RCU|sleeping function called from invalid context|"
    r"scheduling while atomic|list_del corruption|list_add corruption|"
    r"stack-out-of-bounds|use-after-free|slab-out-of-bounds|refcount_t:"
)

filtered = []
ignored_wx = 0
i = 0
n = len(lines)
while i < n:
    if "------------[ cut here ]------------" in lines[i]:
        j = i + 1
        while j < n and "---[ end trace " not in lines[j]:
            j += 1
        if j < n:
            stanza = lines[i : j + 1]
            blob = "\n".join(stanza)
            if (
                "Found insecure W+X mapping" in blob
                and "arch/x86/mm/dump_pagetables.c:" in blob
                and "note_page" in blob
                and "Comm: swapper/" in blob
                and not FORBIDDEN_IN_WX_RE.search(blob)
            ):
                ignored_wx += 1
                i = j + 1
                continue
            filtered.extend(stanza)
            i = j + 1
            continue
        # Unterminated cut-here block: keep all lines so it fails hard.
        filtered.extend(lines[i:])
        break
    filtered.append(lines[i])
    i += 1

if ignored_wx:
    print(f"[INFO] Ignored {ignored_wx} known early-boot CONFIG_DEBUG_WX dump_pagetables.c stanza(s).")

hits = [idx for idx, ln in enumerate(filtered) if SPLAT_RE.search(ln)]
if hits:
    sys.stderr.write("============================================================\n")
    sys.stderr.write("[FAIL] FATAL KERNEL SPLAT DETECTED IN DMESG!\n")
    sys.stderr.write("============================================================\n")
    shown = set()
    for idx in hits:
        for k in range(max(0, idx - 2), min(len(filtered), idx + 3)):
            if k not in shown:
                shown.add(k)
                sys.stderr.write(f"{k + 1}:{filtered[k]}\n")
    sys.exit(1)

print("[PASS] Zero runtime Oops/BUG/WARNING/UBSAN/lockdep/list/atomic-sleep splats in dmesg!")
PYEOF

echo "=== [DONE] NetZero v3 Milestone 1 KVM Verification Complete ==="
