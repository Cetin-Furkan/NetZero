#!/usr/bin/env python3
"""
Project NetZero — Grand Multi-Layer Bare-Metal Benchmark & Statistical Comparator
Runs 7 comprehensive suites (N >= 10 trials per metric):
  S1: Kernel Slab & UDP/TCP Memory Hoarding
  S2: VFS, Lockless MT Hazard-Slot fdget, accept4, SCM_RIGHTS cmsg, & epoll
  S3: AF_UNIX Desktop IPC (64B/256B RTT + p99, 4KB/64KB Stream, 8-Client Wayland Sim)
  S4: TCP Handshake Storm, 64-Conn HTTP/2 Mux, 1/8-Stream Bulk TCP, UDP 4-Dest & QUIC
  S5: Active Root Qdisc (eiffel_dualq vs stock) 8-Flow Paced Contention + Loaded RPC
  S6: Live GNOME/Mutter Wayland Roundtrip + engine-v2 (--world=0) Clean & Under Storm
  S7: Live Wi-Fi 7 Internet Idle Ping, HTTPS TTFB, 4x Download + Bloat, 4x Upload + Bloat
"""

import argparse
import concurrent.futures
import datetime
import json
import math
import os
import platform
import re
import signal
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
ENGINE_DIR = Path(os.environ.get("ENGINE_V2_DIR", str(Path.home() / "projects" / "engine-v2")))


def sh_out(cmd: str, timeout: float = 30.0) -> str:
    try:
        p = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
        return p.stdout.strip()
    except Exception:
        return ""


def run_with_rusage(args, cwd=None, env=None, timeout=60.0):
    """Run a child process and return (returncode, wall_ms, stdout, stderr, rusage)."""
    with tempfile.TemporaryFile(mode="w+b") as out_f, tempfile.TemporaryFile(mode="w+b") as err_f:
        t0 = time.perf_counter()
        proc = subprocess.Popen(args, cwd=cwd, env=env, stdout=out_f, stderr=err_f)
        deadline = t0 + timeout
        _, status, ru = os.wait4(proc.pid, 0)
        t1 = time.perf_counter()
        if t1 > deadline:
            pass
        out_f.seek(0)
        err_f.seek(0)
        stdout = out_f.read().decode("utf-8", errors="replace")
        stderr = err_f.read().decode("utf-8", errors="replace")
        rc = os.waitstatus_to_exitcode(status)
        return rc, (t1 - t0) * 1000.0, stdout, stderr, ru


def parse_protocols_table() -> dict:
    out = {}
    try:
        with open("/proc/net/protocols", "r", encoding="utf-8") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 3 and parts[0] != "protocol":
                    try:
                        out[parts[0]] = int(parts[1])
                    except ValueError:
                        pass
    except Exception:
        pass
    return out


def compute_stats(vals: list[float]) -> dict:
    if not vals:
        return {}
    s = sorted(vals)
    n = len(s)
    mean = statistics.fmean(s)
    stdev = statistics.stdev(s) if n >= 2 else 0.0
    p50 = statistics.median(s)
    p95 = s[min(n - 1, int(math.floor(n * 0.95)))]
    p99 = s[min(n - 1, int(math.floor(n * 0.99)))]
    return {
        "n": n,
        "mean": round(mean, 4),
        "stdev": round(stdev, 4),
        "cv_pct": round((stdev / abs(mean) * 100.0) if mean != 0 else 0.0, 2),
        "median": round(p50, 4),
        "p95": round(p95, 4),
        "p99": round(p99, 4),
        "min": round(s[0], 4),
        "max": round(s[-1], 4),
    }


class Collector:
    def __init__(self):
        self.metrics: dict[str, dict] = {}

    def add(self, suite: str, metric: str, value: float, unit: str, better: str):
        key = f"{suite}::{metric}"
        if key not in self.metrics:
            self.metrics[key] = {
                "suite": suite,
                "metric": metric,
                "unit": unit,
                "better": better,
                "samples": [],
            }
        self.metrics[key]["samples"].append(round(float(value), 6))

    def ingest_jsonl(self, text: str):
        for line in text.splitlines():
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
                self.add(obj["suite"], obj["metric"], obj["value"], obj["unit"], obj["better"])
            except Exception:
                pass

    def finalize(self) -> dict:
        out = {}
        for k, v in self.metrics.items():
            entry = dict(v)
            entry["stats"] = compute_stats(v["samples"])
            out[k] = entry
        return out


def ensure_binaries():
    march = os.environ.get("ARCH_MARCH", "native")
    cc = os.environ.get("CC", "clang")
    c_bin = BENCH_DIR / "grand_socket_bench"
    w_bin = BENCH_DIR / "live_wayland_ping"
    if not c_bin.exists():
        subprocess.run(
            [cc, "-Wall", "-Wextra", "-O3", f"-march={march}", "-pthread",
             str(BENCH_DIR / "grand_socket_bench.c"), "-o", str(c_bin)],
            check=True,
        )
    if not w_bin.exists():
        wl_flags = sh_out("pkg-config --cflags --libs wayland-client").split()
        if wl_flags:
            subprocess.run(
                [cc, "-Wall", "-Wextra", "-O3", f"-march={march}",
                 str(BENCH_DIR / "live_wayland_ping.c"), *wl_flags, "-o", str(w_bin)],
                check=True,
            )


def parse_ping_rtts(output: str) -> list[float]:
    return [float(m) for m in re.findall(r"time=([0-9.]+)\s*ms", output)]


def run_suite5_qdisc(col: Collector, trials: int):
    default_qdisc = sh_out("sysctl -n net.core.default_qdisc") or "fq_codel"
    print(f"[Suite 5] Running Active Root Qdisc ({default_qdisc}) 8-Flow Paced Contention ({trials} trials)...", flush=True)
    c_bin = BENCH_DIR / "grand_socket_bench"
    for t in range(trials):
        cmd = (
            f"unshare -Urn sh -c 'ip link set lo up && "
            f"(tc qdisc replace dev lo root {default_qdisc} 2>/dev/null || tc qdisc replace dev lo root fq_codel) && "
            f"{c_bin} --qdisc-trial {t}'"
        )
        p = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=30)
        col.ingest_jsonl(p.stdout)


def run_engine_v2_bench(col: Collector, trials: int, under_storm: bool):
    mode_tag = "storm" if under_storm else "clean"
    label = "Under 4-Thread Socket Storm" if under_storm else "Clean Desktop"
    print(f"[Suite 6] Running Live Wayland engine-v2 (--world=0 --bench=300) [{label}] ({trials} trials)...", flush=True)

    env = os.environ.copy()
    env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    env.setdefault("WAYLAND_DISPLAY", "wayland-0")

    engine_bin = ENGINE_DIR / "build/release/bin/engine"
    if not engine_bin.exists():
        print(f"  [SKIP] Optional {engine_bin} not present on this host.", flush=True)
        return
    storm_proc = None
    if under_storm:
        storm_proc = subprocess.Popen(
            [str(BENCH_DIR / "grand_socket_bench"), "--ipc-storm"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.25)

    try:
        # 1 warmup run on clean so shader/pipeline cache is equally hot on both kernels
        if not under_storm:
            run_with_rusage([str(engine_bin), "--world=0", "--bench=60"], cwd=str(ENGINE_DIR), env=env, timeout=30)

        bench_re = re.compile(
            r"\[Bench\].*?frames=(\d+)\s+samples=(\d+)\s+wall_ms=([0-9.]+)\s+"
            r"gpu_total_us\(avg=([0-9.]+),min=([0-9.]+),max=([0-9.]+)\)"
        )
        for t in range(trials):
            rc, total_wall_ms, stdout, stderr, ru = run_with_rusage(
                [str(engine_bin), "--world=0", "--bench=300"],
                cwd=str(ENGINE_DIR),
                env=env,
                timeout=45,
            )
            if rc != 0:
                print(f"  [WARN] engine-v2 trial {t} exited with rc={rc}: {stderr[:200]}", file=sys.stderr)
                continue
            m = bench_re.search(stdout)
            if m:
                frames = int(m.group(1))
                wall_ms = float(m.group(3))
                gpu_avg = float(m.group(4))
                gpu_max = float(m.group(6))
                fps = (frames * 1000.0 / wall_ms) if wall_ms > 0 else 0.0
                sys_ms = ru.ru_stime * 1000.0
                user_ms = ru.ru_utime * 1000.0

                col.add("S6_Live_Wayland", f"engine_v2_world0_{mode_tag}_fps", fps, "FPS", "higher")
                col.add("S6_Live_Wayland", f"engine_v2_world0_{mode_tag}_wall_ms", wall_ms, "ms", "lower")
                col.add("S6_Live_Wayland", f"engine_v2_world0_{mode_tag}_sys_cpu_ms", sys_ms, "ms", "lower")
                col.add("S6_Live_Wayland", f"engine_v2_world0_{mode_tag}_user_cpu_ms", user_ms, "ms", "lower")
                col.add("S6_Live_Wayland", f"engine_v2_world0_{mode_tag}_gpu_avg_us", gpu_avg, "us", "lower")
                col.add("S6_Live_Wayland", f"engine_v2_world0_{mode_tag}_gpu_max_us", gpu_max, "us", "lower")
                col.add("S6_Live_Wayland", f"engine_v2_world0_{mode_tag}_total_proc_ms", total_wall_ms, "ms", "lower")
    finally:
        if storm_proc is not None:
            storm_proc.send_signal(signal.SIGTERM)
            try:
                storm_proc.wait(timeout=3)
            except Exception:
                storm_proc.kill()


def run_suite6_wayland(col: Collector, trials: int):
    env = os.environ.copy()
    env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    env.setdefault("WAYLAND_DISPLAY", "wayland-0")
    wl_bin = BENCH_DIR / "live_wayland_ping"
    if wl_bin.exists():
        print(f"[Suite 6] Running Live GNOME/Mutter wl_display_roundtrip ({trials} trials x 2,000 RTs)...", flush=True)
        p = subprocess.run(
            [str(wl_bin), str(trials), "2000"],
            env=env,
            capture_output=True,
            text=True,
            timeout=60,
        )
        col.ingest_jsonl(p.stdout)

    # Engine-v2 unit wire & io_uring dispatch tests (10 trials each)
    for test_bin, mprefix in [
        (ENGINE_DIR / "build/test_wayland_wire", "engine_v2_test_wayland_wire"),
        (ENGINE_DIR / "build/test_ring_dispatch", "engine_v2_test_ring_dispatch"),
    ]:
        if test_bin.exists():
            print(f"[Suite 6] Running {test_bin.name} ({trials} trials)...", flush=True)
            for _ in range(trials):
                rc, wall_ms, _, _, ru = run_with_rusage([str(test_bin)], cwd=str(ENGINE_DIR), env=env, timeout=30)
                if rc == 0:
                    col.add("S6_Live_Wayland", f"{mprefix}_wall_ms", wall_ms, "ms", "lower")
                    col.add("S6_Live_Wayland", f"{mprefix}_sys_ms", ru.ru_stime * 1000.0, "ms", "lower")

    run_engine_v2_bench(col, trials, under_storm=False)
    run_engine_v2_bench(col, trials, under_storm=True)


def curl_down_worker(worker_idx: int, max_sec: float) -> float:
    """Returns bytes downloaded from HTTP 200 stream (multi-CDN to avoid HTTP 429 rate-limits)."""
    urls = [
        "https://cachefly.cachefly.net/10mb.test",
        "https://speed.cloudflare.com/__down?bytes=15000000",
        "https://proof.ovh.net/files/10Mb.dat",
    ]
    order = [urls[(worker_idx + k) % len(urls)] for k in range(len(urls))]
    for url in order:
        p = subprocess.run(
            [
                "curl", "-sS", "-L", "-o", "/dev/null",
                "--max-time", f"{max_sec:.1f}",
                "-H", "User-Agent: Mozilla/5.0",
                "-w", "%{http_code} %{size_download}",
                url,
            ],
            capture_output=True,
            text=True,
        )
        parts = p.stdout.strip().split()
        if len(parts) == 2 and parts[0] == "200":
            try:
                val = float(parts[1])
                if val > 10000:
                    return val
            except ValueError:
                pass
        elif len(parts) == 2 and parts[0] == "000":
            # Timed out mid-stream at --max-time after transferring real payload bytes!
            try:
                val = float(parts[1])
                if val > 100000:
                    return val
            except ValueError:
                pass
    return 0.0


def curl_up_worker(worker_idx: int, upload_file: str, max_sec: float) -> float:
    """Returns bytes uploaded on verified HTTP 2xx stream with multi-endpoint fallback."""
    endpoints = [
        "https://speed.cloudflare.com/__up",
        "https://httpbin.org/post",
        "https://postman-echo.com/post",
    ]
    # Stagger endpoints when Cloudflare rate-limits >30 POSTs/min from 1 IP
    order = endpoints if worker_idx < 2 else [endpoints[1], endpoints[0], endpoints[2]]
    for url in order:
        p = subprocess.run(
            [
                "curl", "-sS", "-o", "/dev/null",
                "--max-time", f"{max_sec:.1f}",
                "-H", "User-Agent: Mozilla/5.0",
                "-H", "Expect:",
                "-X", "POST", "--data-binary", f"@{upload_file}",
                "-w", "%{http_code} %{size_upload}",
                url,
            ],
            capture_output=True,
            text=True,
        )
        parts = p.stdout.strip().split()
        if len(parts) == 2:
            code = parts[0]
            try:
                sent = float(parts[1])
            except ValueError:
                sent = 0.0
            # Accept HTTP 2xx OR --max-time cutoff ('000') when >100 KB actually transmitted
            if (code.startswith("2") and sent > 10000) or (code == "000" and sent > 100000):
                return sent
    return 0.0


def run_suite7_internet(col: Collector, trials: int):
    print(f"[Suite 7] Running Live Wi-Fi 7 Internet Speed, TTFB & Bufferbloat ({trials} trials)...", flush=True)

    # 7A: Idle Ping (10 trials x 10 ICMP probes @ 100ms = 100 probes)
    for t in range(trials):
        p = subprocess.run(["ping", "-c", "10", "-i", "0.1", "-W", "2", "1.1.1.1"], capture_output=True, text=True)
        rtts = parse_ping_rtts(p.stdout)
        if rtts:
            rtts_s = sorted(rtts)
            col.add("S7_Live_Internet", "idle_ping_mean_ms", statistics.fmean(rtts_s), "ms", "lower")
            col.add("S7_Live_Internet", "idle_ping_p99_ms", rtts_s[-1], "ms", "lower")
            if len(rtts_s) >= 2:
                col.add("S7_Live_Internet", "idle_ping_jitter_stdev_ms", statistics.stdev(rtts_s), "ms", "lower")

    # 7B: HTTPS TCP + TLS + TTFB Latency (10 trials)
    for t in range(trials):
        p = subprocess.run(
            [
                "curl", "-sS", "-o", "/dev/null", "--max-time", "5",
                "-w", "%{time_connect} %{time_appconnect} %{time_starttransfer}",
                "https://speed.cloudflare.com/__down?bytes=64",
            ],
            capture_output=True,
            text=True,
        )
        parts = p.stdout.strip().split()
        if len(parts) == 3:
            try:
                t_conn = float(parts[0]) * 1000.0
                t_tls = float(parts[1]) * 1000.0
                t_ttfb = float(parts[2]) * 1000.0
                if t_conn > 0 and t_ttfb > 0:
                    col.add("S7_Live_Internet", "https_tcp_connect_ms", t_conn, "ms", "lower")
                    col.add("S7_Live_Internet", "https_tls_handshake_ms", t_tls, "ms", "lower")
                    col.add("S7_Live_Internet", "https_ttfb_ms", t_ttfb, "ms", "lower")
            except ValueError:
                pass

    # Prepare 3 MiB payload file for upload testing
    up_payload = "/tmp/netzero_upload_payload_3m.bin"
    with open(up_payload, "wb") as f:
        f.write(b"\x42" * (3 * 1024 * 1024))

    try:
        # 7C: 4-Stream Parallel Download + Loaded Ping (10 trials x ~3.0s)
        print(f"[Suite 7]   -> 4-Stream HTTPS Download + Simultaneous Loaded Ping ({trials} trials)...", flush=True)
        for t in range(trials):
            ping_proc = subprocess.Popen(
                ["ping", "-i", "0.1", "-W", "2", "1.1.1.1"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
            )
            t0 = time.perf_counter()
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
                futs = [ex.submit(curl_down_worker, idx, 3.0) for idx in range(4)]
                tot_bytes = sum(fut.result() for fut in futs)
            dt = time.perf_counter() - t0
            ping_proc.send_signal(signal.SIGINT)
            ping_out, _ = ping_proc.communicate(timeout=3)
            rtts = sorted(parse_ping_rtts(ping_out))

            if tot_bytes > 0 and dt > 0:
                mbps = (tot_bytes * 8.0 / 1e6) / dt
                col.add("S7_Live_Internet", "download_4stream_mbps", mbps, "Mbit/s", "higher")
            if rtts:
                col.add("S7_Live_Internet", "download_loaded_ping_p50_ms", statistics.median(rtts), "ms", "lower")
                col.add("S7_Live_Internet", "download_loaded_ping_p99_ms", rtts[min(len(rtts) - 1, int(len(rtts) * 0.99))], "ms", "lower")
            time.sleep(0.2)

        # 7D: 4-Stream Parallel Upload + Loaded Ping (10 trials x ~3.0s)
        print(f"[Suite 7]   -> 4-Stream HTTPS Upload + Simultaneous Loaded Ping ({trials} trials)...", flush=True)
        for t in range(trials):
            ping_proc = subprocess.Popen(
                ["ping", "-i", "0.1", "-W", "2", "1.1.1.1"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
            )
            t0 = time.perf_counter()
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
                futs = [ex.submit(curl_up_worker, idx, up_payload, 3.0) for idx in range(4)]
                tot_bytes = sum(fut.result() for fut in futs)
            dt = time.perf_counter() - t0
            ping_proc.send_signal(signal.SIGINT)
            ping_out, _ = ping_proc.communicate(timeout=3)
            rtts = sorted(parse_ping_rtts(ping_out))

            if tot_bytes > 0 and dt > 0:
                mbps = (tot_bytes * 8.0 / 1e6) / dt
                col.add("S7_Live_Internet", "upload_4stream_mbps", mbps, "Mbit/s", "higher")
            if rtts:
                col.add("S7_Live_Internet", "upload_loaded_ping_p50_ms", statistics.median(rtts), "ms", "lower")
                col.add("S7_Live_Internet", "upload_loaded_ping_p99_ms", rtts[min(len(rtts) - 1, int(len(rtts) * 0.99))], "ms", "lower")
            time.sleep(0.2)
    finally:
        try:
            os.unlink(up_payload)
        except OSError:
            pass


def format_single_summary(data: dict) -> str:
    meta = data["metadata"]
    lines = [
        f"# Grand Benchmark Summary — `{meta['kernel_release']}`",
        "",
        f"- **Timestamp:** `{meta['timestamp']}`",
        f"- **Kernel:** `{meta['uname']}`",
        f"- **Default Qdisc / CC / ECN:** `{meta['default_qdisc']}` / `{meta['tcp_cc']}` / `tcp_ecn={meta['tcp_ecn']}`",
        f"- **Governor / EPP:** `{meta['governor']}` / `{meta['epp']}`",
        f"- **Protocol Struct Sizes (`/proc/net/protocols`):** "
        f"`TCP={meta['protocols'].get('TCP')} B`, `TCPv6={meta['protocols'].get('TCPv6')} B`, "
        f"`UDP={meta['protocols'].get('UDP')} B`, `UNIX-STREAM={meta['protocols'].get('UNIX-STREAM')} B`",
        "",
        "| Suite | Metric | Mean ± StdDev | Median (p50) | p99 | Min .. Max | Unit |",
        "| :--- | :--- | :---: | :---: | :---: | :---: | :---: |",
    ]
    for key in sorted(data["metrics"].keys()):
        m = data["metrics"][key]
        st = m["stats"]
        lines.append(
            f"| `{m['suite']}` | `{m['metric']}` | **{st['mean']:.2f}** ± {st['stdev']:.2f} "
            f"| {st['median']:.2f} | {st['p99']:.2f} | {st['min']:.2f} .. {st['max']:.2f} | `{m['unit']}` |"
        )
    lines.append("")
    return "\n".join(lines)


def compare_runs(netzero_path: Path, stock_path: Path) -> str:
    nz = json.loads(netzero_path.read_text(encoding="utf-8"))
    st = json.loads(stock_path.read_text(encoding="utf-8"))

    nz_k = nz["metadata"]["kernel_release"]
    st_k = st["metadata"]["kernel_release"]

    lines = [
        f"# Bare-Metal Grand Benchmark Comparison (`N=10` Trials/Metric)",
        f"**Stock Kernel (`{st_k}`)** vs **Project NetZero (`{nz_k}`)**",
        "",
        "## 1. Kernel Struct Sizes (`/proc/net/protocols`)",
        "| Protocol | Stock Kernel | Project NetZero | Delta |",
        "| :--- | :---: | :---: | :---: |",
    ]
    for proto in ["TCP", "TCPv6", "UNIX-STREAM", "UNIX", "UDP", "UDPv6", "NETLINK"]:
        sv = st["metadata"]["protocols"].get(proto)
        nv = nz["metadata"]["protocols"].get(proto)
        if sv is not None and nv is not None:
            diff = nv - sv
            pct = (diff / sv * 100.0) if sv else 0.0
            lines.append(f"| `{proto}` | `{sv} B` | **`{nv} B`** | `{diff:+d} B` (`{pct:+.1f}%`) |")

    lines += [
        "",
        "## 2. Full 7-Suite Statistical A/B Results (`Mean ± StdDev` over 10 Trials)",
        "| Suite | Metric | Stock Mean ± SD | NetZero Mean ± SD | Improvement | Unit |",
        "| :--- | :--- | :---: | :---: | :---: | :---: |",
    ]

    all_keys = sorted(set(st["metrics"].keys()) & set(nz["metrics"].keys()))
    for k in all_keys:
        ms = st["metrics"][k]
        mn = nz["metrics"][k]
        s_mean = ms["stats"]["mean"]
        s_sd = ms["stats"]["stdev"]
        n_mean = mn["stats"]["mean"]
        n_sd = mn["stats"]["stdev"]
        better = mn["better"]

        if s_mean > 0 and n_mean == 0 and better == "lower":
            imp = f"**-100.0%** (`{s_mean:.1f} -> 0`)"
        elif s_mean > 0 and n_mean > 0:
            if better == "lower":
                pct = (n_mean - s_mean) / s_mean * 100.0
                ratio = s_mean / n_mean
                imp = f"**{pct:+.1f}%** (`{ratio:.2f}x`)" if ratio >= 1.05 else f"{pct:+.1f}%"
            else:
                pct = (n_mean - s_mean) / s_mean * 100.0
                ratio = n_mean / s_mean
                imp = f"**{pct:+.1f}%** (`{ratio:.2f}x`)" if ratio >= 1.05 else f"{pct:+.1f}%"
        else:
            imp = f"`{s_mean:.1f} -> {n_mean:.1f}`"

        lines.append(
            f"| `{mn['suite']}` | `{mn['metric']}` | {s_mean:.2f} ± {s_sd:.2f} "
            f"| **{n_mean:.2f}** ± {n_sd:.2f} | {imp} | `{mn['unit']}` |"
        )
    lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", type=int, default=10, help="Trials per benchmark (>=10)")
    ap.add_argument("--only-s7", action="store_true", help="Re-run only Suite 7 (Internet) into existing JSON")
    ap.add_argument("--compare", nargs=2, metavar=("NETZERO_JSON", "STOCK_JSON"))
    ap.add_argument("--out-compare", type=str, default=str(BENCH_DIR / "FINAL_BAREMETAL_COMPARISON.md"))
    args = ap.parse_args()

    if args.compare:
        rep = compare_runs(Path(args.compare[0]), Path(args.compare[1]))
        Path(args.out_compare).write_text(rep, encoding="utf-8")
        print(rep)
        return

    ensure_binaries()
    krel = platform.release()
    safe_krel = re.sub(r"[^A-Za-z0-9._+-]", "_", krel)
    json_path = BENCH_DIR / f"results_{safe_krel}.json"
    md_path = BENCH_DIR / f"summary_{safe_krel}.md"

    if args.only_s7 and json_path.exists():
        existing = json.loads(json_path.read_text(encoding="utf-8"))
        col = Collector()
        run_suite7_internet(col, args.trials)
        new_m = col.finalize()
        for k in list(existing["metrics"].keys()):
            if k.startswith("S7_Live_Internet::"):
                del existing["metrics"][k]
        existing["metrics"].update(new_m)
        json_path.write_text(json.dumps(existing, indent=2), encoding="utf-8")
        md_path.write_text(format_single_summary(existing), encoding="utf-8")
        print(f"[DONE] Updated Suite 7 cleanly in {json_path}")
        return

    print("======================================================================", flush=True)
    print(f" Project NetZero — Grand Bare-Metal Benchmark Suite ({args.trials} trials/test)", flush=True)
    print(f" Active Kernel: {krel}", flush=True)
    print("======================================================================", flush=True)

    t_suite_start = time.perf_counter()
    col = Collector()

    # Suites 1-4 in C
    c_proc = subprocess.Popen(
        [str(BENCH_DIR / "grand_socket_bench"), str(args.trials)],
        stdout=subprocess.PIPE,
        stderr=sys.stderr,
        text=True,
    )
    c_out, _ = c_proc.communicate()
    col.ingest_jsonl(c_out)

    # Suite 5: Active Qdisc Contention
    run_suite5_qdisc(col, args.trials)

    # Suite 6: Live Wayland + engine-v2
    run_suite6_wayland(col, args.trials)

    # Suite 7: Live Wi-Fi 7 Internet
    run_suite7_internet(col, args.trials)

    elapsed_s = time.perf_counter() - t_suite_start

    payload = {
        "metadata": {
            "timestamp": datetime.datetime.now().isoformat(),
            "kernel_release": krel,
            "uname": sh_out("uname -a"),
            "default_qdisc": sh_out("sysctl -n net.core.default_qdisc"),
            "tcp_cc": sh_out("sysctl -n net.ipv4.tcp_congestion_control"),
            "tcp_ecn": sh_out("sysctl -n net.ipv4.tcp_ecn"),
            "governor": sh_out("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"),
            "epp": sh_out("cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference"),
            "protocols": parse_protocols_table(),
            "total_suite_seconds": round(elapsed_s, 2),
        },
        "metrics": col.finalize(),
    }

    json_path = BENCH_DIR / f"results_{safe_krel}.json"
    md_path = BENCH_DIR / f"summary_{safe_krel}.md"
    json_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    summary_md = format_single_summary(payload)
    md_path.write_text(summary_md, encoding="utf-8")

    print("======================================================================", flush=True)
    print(f"[DONE] Completed all 7 suites ({len(payload['metrics'])} metrics x {args.trials} trials) in {elapsed_s:.1f}s!", flush=True)
    print(f"  JSON : {json_path}", flush=True)
    print(f"  Table: {md_path}", flush=True)
    print("======================================================================", flush=True)


if __name__ == "__main__":
    main()
