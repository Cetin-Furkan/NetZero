# Project NetZero v3 (`7.4.0-rc2-netzero-v3-m3`)

**Cross-Subsystem Fused, Hardware-Accelerated Linux 7.4 Kernel Overhaul for Desktop IPC, Event Multiplexing, `io_uring`, BORE-EEVDF Scheduling, `futex` Synchronization & Wi-Fi 7 / L4S Networking**

> **100% POSIX & Linux Binary ABI Compatible** — Zero userspace recompilation required. Every Wayland compositor (GNOME Mutter, Sway, Hyprland, KWin), browser (Chromium/Firefox), `dbus-broker`, PipeWire audio graph, Rust/Tokio runtime, database, and HTTP/2 or QUIC stack accelerates automatically on boot.
>
> **Verified Clean Under Debug Sanitizers** — Passed all 14 POSIX/ABI torture suites + live `/usr/bin/dbus-broker` + `GDBus` portal storms in both Release KVM (`SMP=8`) and Debug KVM (`CONFIG_PROVE_LOCKING` / `LOCKDEP` + `UBSAN` + `DEBUG_ATOMIC_SLEEP`) with **`0` runtime warnings or splats**.

---

## Headline Bare-Metal Results (`~4,400` Total Runs Across 6 Kernels)

Measured on bare-metal **Intel Core i7-1165G7 (Tiger Lake Willow Cove, `4C/8T`, `48 KiB` L1D, `1.25 MiB` private L2/core, `12 MiB` L3, Intel Iris Xe Graphics, Intel BE200 Wi-Fi 7 `iwlmld`)** under identical power/governor conditions (`powersave` / `balance_performance`, `eiffel_dualq` + `bbr` + `tcp_ecn=2`, `10` trials per metric, median `p50`):

| Subsystem | Benchmark | Stock CachyOS `7.3-rc6` | NetZero v2 | **NetZero v3 (`M3`)** | Speedup / Savings vs CachyOS |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **Scheduler (BORE-EEVDF)** | Interactive Wakeup Under 8× 100%-CPU Hogs (`p50` / `p99`) | *(~3,000 µs stock slice)* | — | **`2.10 µs` / `6.11 µs`** | **Sub-7 µs interactivity under full load** |
| **Live GPU Storm** | `engine-v2` Worst GPU Frame Spike Under 4-Thread Storm | `2,230.89 µs` | `1,825.76 µs` | **`530.97 µs`** *(mean `550.8 ± 48.3`)* | **`4.20x` lower frame spike (`125.34 ± 0.03 FPS`)** |
| **Desktop IPC** | `AF_UNIX` `64 B` Ping-Pong Latency | `2,954.56 ns` | `702.91 ns` | **`772.34 ns`** *(M2: `723.54 ns`)* | **`3.83x–4.08x` faster (`-74%` to `-76%`)** |
| **Event Loop** | `epoll_wait` (`128` ready of `1,024` FDs) | `39.79 ns/ev` | `17.37 ns/ev` | **`11.79 ns/ev`** | **`3.37x` faster (`-70.4%`)** |
| **Event Loop** | `epoll_wait` (`32` ready of `256` FDs, CPU cycles) | `101.20 cyc/ev` | `47.33 cyc/ev` | **`37.88 cyc/ev`** (`13.58 ns/ev`) | **`2.67x` faster (`< 38` cycles/event!)** |
| **Compositor Sim** | 8-Client Wayland Compositor Throughput & Kernel CPU | `341.14 k/s` (`254.4 ms`) | `433.35 k/s` (`201.3 ms`) | **`480.59 k/s` (`178.5 ms`)** | **`1.41x` faster & `1.42x` lower kernel CPU** |
| **Live Mutter** | Live GNOME Wayland `wl_display_roundtrip` (`p50` / `sys`) | `10.54 µs` (`4.60 ms`) | `12.32 µs` (`3.79 ms`) | **`8.58 µs` (`3.80 ms`)** | **`1.23x` faster (`-18.6%` RTT)** |
| **Desktop Stream** | `AF_UNIX` `4 KiB` / `64 KiB` Stream Throughput | `4.62` / `10.70 GiB/s` | `11.59` / `18.52 GiB/s` | **`12.08` / `18.90 GiB/s`** | **`2.61x` (`4K`) / `1.77x` (`64K`) faster** |
| **WF_SYNC IPC** | Unpinned `eventfd` / `pipe 64B` Ping-Pong RTT | — | *(cross-core `~3.1–3.3 µs`)* | **`1,847 ns` / `2,003 ns`** | **`1.70x` faster same-core L1/L2 handoff** |
| **Thread Sync** | `futex` `CRC32Q` Wake / 8-Thread Contended Lock | — | — | **`93.50 ns` / `63.82 Mops/s`** | **3-cycle hardware `crc32q` hashing** |
| **Async I/O** | Unboxed `io_uring` `AF_UNIX` Batch-32 Throughput | — | — | **`13.09 Mops/s`** (`13.60` peak) | **Zero `async_data` heap boxing** |
| **Shared FD** | 8-Thread Shared Socket `recv(EAGAIN)` Scalability | `5.43 Mops/s` | `5.75 Mops/s` | **`10.07 Mops/s`** | **`1.85x` higher multi-core scaling** |
| **TCP / HTTP/2** | 64-Conn HTTP/2 Multiplexing & 2,000-Conn Handshake | `375.39 kRPS` / `17.20 µs` | `401.10 kRPS` / `17.48 µs` | **`421.01 kRPS` / `15.73 µs`** | **`+12.2%` HTTP/2 RPS & `+8.5%` handshake** |
| **UDP / QUIC** | QUIC `1200 B` Batch / Unconnected 4-Dest UDP | `8.97 Gbps` / `0.83 Mpps` | `9.67 Gbps` / `0.93 Mpps` | **`10.33 Gbps` / `0.95 Mpps`** | **`+15.2%` QUIC / `+14.5%` UDP** |
| **Kernel RAM** | `UDP` 64-QUIC Hoarded Pages / `AF_UNIX` 2,000-Sock Slab | `240 pages` / `7,018 KiB` | `28 pages` / `1,642 KiB` | **`31 pages` / `1,526 KiB`** | **`7.74x` less UDP / `4.60x` less UNIX RAM** |

---

## Architecture Overview: Three Milestones Beyond NetZero v2

Project NetZero v3 builds directly on v2's C23 (`-std=gnu23`), Tiger Lake deep-ISA primitives ([`netzero_tigerlake.h`](kernel-overlay/include/linux/netzero_tigerlake.h)), adaptive 2-tier `unix_ring`, `sch_eiffel_dualq` L4S qdisc, and Intel SOF audio fix — adding three complete, hardened kernel milestones across **83 files (`+8,106 / -1,022` lines)**:

### Milestone 1 — Desktop IPC & Event Core
- **Lockless/Fused `eventfd`, `timerfd`, `pidfd`, & `pipe2`:** Direct `read`/`write` fastpaths in [`fs/read_write.c`](kernel-overlay/fs/read_write.c), [`fs/eventfd.c`](kernel-overlay/fs/eventfd.c), [`fs/pipe.c`](kernel-overlay/fs/pipe.c), [`fs/timerfd.c`](kernel-overlay/fs/timerfd.c), and [`fs/pidfs.c`](kernel-overlay/fs/pidfs.c) bypassing generic `kiocb`/`iov_iter` boilerplate while preserving full `O_NONBLOCK`, `EFD_SEMAPHORE`, `O_DIRECT`, `SIGPIPE`, and upfront `access_ok()` semantics.
- **Inline 4-FD `SCM_RIGHTS` Sideband Ring & Multi-Iovec `dbus-broker` Fastpath:** [`net/unix/af_unix.c`](kernel-overlay/net/unix/af_unix.c) handles 1–4 FD `SCM_RIGHTS` control messages (Wayland `dmabuf`/`memfd` + D-Bus portals) and 1–4 iovec `sendmsg()` directly through the adaptive `unix_ring` with exact `UNIXCB(skb).ring_seq` ordering, cyclic GC safety, `SIOCOUTQ` accuracy, 25%-watermark `EPOLLET|EPOLLOUT` wakeup, and hardened `splice()` (`msg == NULL`) draining.
- **Peek-Before-Alloc `accept4` & Multi-Threaded Hazard-Slot `fdget`:** [`net/socket.c`](kernel-overlay/net/socket.c) and [`fs/file.c`](kernel-overlay/fs/file.c) eliminate `struct file` allocation on empty non-blocking `accept4()` and cut 8-thread shared-FD atomic refcount contention by **`1.85x`**.

### Milestone 2 — `epoll`, Blocking `poll`/`ppoll` & Unboxed `io_uring`
- **Zero-Byte `fast_file_kind` + Pin-Free `ep_item_poll()` + Batched `__put_user`:** [`fs/eventpoll.c`](kernel-overlay/fs/eventpoll.c) packs `u8 fast_file_kind` into an existing 3-byte alignment hole inside `struct epitem` (keeping `sizeof(struct epitem) == 116 B`), skips atomic `get_file()`/`fput()` under `ep->mtx` (guarded by `file_ref_read()` + `eventpoll_release_file()`), prefetches upcoming items into the `48 KiB` L1D, and emits 8-event batches under a single `user_write_access_begin()` window (**`11.79 ns/ev`**, **`37.88 cycles/ev`**).
- **16-Slot Hardware `CRC32Q` `epoll_ctl(MOD)` Direct-Mapped Cache & Zero-IRQ `ep_done_scan()`:** Turns `O(log N)` RB-tree lookups on active `EPOLLONESHOT` FDs into `O(1)` 3-cycle `crc32q` L1D hits (`149–156 ns/op` across `1,024` FDs).
- **Two-Stage Small `poll()`/`ppoll()` (`<= 8` FDs):** [`fs/select.c`](kernel-overlay/fs/select.c) keeps both non-blocking *and* blocking small polls (Wayland client loops, PipeWire, D-Bus) in a compact stack frame with pin-free private `files_struct` waitqueue registration (`242.45 ns/call`).
- **Unboxed `io_uring` Socket Fusion:** [`io_uring/net.c`](kernel-overlay/io_uring/net.c) and [`io_uring/rw.c`](kernel-overlay/io_uring/rw.c) bypass `req->async_data` heap allocation for single-buffer `IORING_OP_SEND`/`RECV`/`READ`/`WRITEV`, fusing straight into `unix_ring` and leaf TCP/UDP (**`13.09 Mops/s`** batch-32).

### Milestone 3 — Zero-Byte BORE-EEVDF, End-to-End `WF_SYNC`, `CRC32Q` `futex` & Wi-Fi 7 Bufferbloat
- **Zero-Byte BORE-EEVDF Burst-Adaptive Priority:** Packs saturating `u32 burst_ns` into the existing 4-byte hole in `struct sched_entity` ([`include/linux/sched.h`](kernel-overlay/include/linux/sched.h), **0 bytes struct growth**). Maps short bursts (`< 250 µs` / `< 1 ms` / `< 4 ms`) to tighter EEVDF request slices (`nz_bore_slice()` in [`kernel/sched/fair.c`](kernel-overlay/kernel/sched/fair.c)) strictly outside `tasks_timeline` (preserving augmented RB-tree `min_slice`/`max_slice` invariants and EEVDF `entity_eligible()` zero-starvation fairness). Result: **`2.10 µs` p50 / `6.11 µs` p99** interactive wakeup under 8× 100%-CPU hogs and **`4.20x` lower** live GPU storm spikes.
- **End-to-End `WF_SYNC` Same-Core L1/L2 IPC Handoff:** Propagates `WF_SYNC` through [`fs/eventfd.c`](kernel-overlay/fs/eventfd.c) (`wake_up_locked_sync_poll`), [`io_uring/tw.c`](kernel-overlay/io_uring/tw.c) (`wake_up_state_sync` + `TWA_SIGNAL_NO_IPI_SYNC`), and [`kernel/sched/fair.c`](kernel-overlay/kernel/sched/fair.c) (`select_task_rq_fair()` keeping short-burst reciprocal `p->last_wakee == current` pairs on `this_cpu`, where EEVDF `DELAY_DEQUEUE` keeps both hot in L1D/L2 via local `ttwu_runnable()`). Cuts unpinned `eventfd` and `pipe` RTT by **`1.70x`** (`3.15 µs` → `1.85 µs`).
- **3-Cycle Hardware `CRC32Q` `futex`:** [`kernel/futex/core.c`](kernel-overlay/kernel/futex/core.c) replaces software `jhash2` in both `__futex_hash_private()` and global `__futex_hash()` with Tiger Lake `crc32q` (`nz_crc32c_u64`), paired with a straight-line 32-bit `FUTEX_PRIVATE` `get_futex_key()` fastpath (**`93.50 ns`** wake, **`63.82 Mops/s`** 8-thread contention).
- **Intel BE200 Wi-Fi 7 (`iwlmld`/`mac80211`) & TCP RX Bufferbloat Tuning:** Tightens [`net/mac80211/main.c`](kernel-overlay/net/mac80211/main.c) `tx_sk_pacing_shift` (`7` → `9`, cutting 4-stream `iwlmld` TFD hardware standing queue by `4x`), [`net/mac80211/tx.c`](kernel-overlay/net/mac80211/tx.c) FQ-CoDel (`target = 5 ms`, `interval = 50 ms`, RFC 9331 L4S `ECT(1)` `ce_threshold = 2 ms`), [`include/net/cfg80211.h`](kernel-overlay/include/net/cfg80211.h) AQL defaults (`1.5 ms` / `4 ms` / `10 ms`), and adds RTT-inflation-aware `tcp_rcvbuf_grow()` moderation in [`net/ipv4/tcp_input.c`](kernel-overlay/net/ipv4/tcp_input.c).

---

## Documentation Index

| Document | Contents |
| :--- | :--- |
| [`docs/NETZERO_V3_ENGINEERING_DEEP_DIVE.md`](docs/NETZERO_V3_ENGINEERING_DEEP_DIVE.md) | Exhaustive file-by-file, cacheline-by-cacheline engineering whitepaper for Milestones 1, 2, and 3 |
| [`docs/BAREMETAL_6WAY_BENCHMARKS.md`](docs/BAREMETAL_6WAY_BENCHMARKS.md) | Full 80-metric × 10-trial 6-way bare-metal statistical report (`CachyOS` vs `v1` vs `v2` vs `v3-M1` vs `v3-M2` vs `v3-M3`) |
| [`docs/HARDENING_AND_VERIFICATION.md`](docs/HARDENING_AND_VERIFICATION.md) | Release + Debug (`LOCKDEP`/`UBSAN`/`DEBUG_ATOMIC_SLEEP`) KVM torture suite, hardened kernel edge cases & Intel Iris Xe / `nvidia-utils` Chrome GPU forensics |
| [`docs/NETZERO_V2_WHITEPAPER.md`](docs/NETZERO_V2_WHITEPAPER.md) | Foundational NetZero v2 architecture (C23, Tiger Lake ISA, adaptive `unix_ring`, `sch_eiffel_dualq`) |
| [`docs/PORTABILITY_AND_CPU_TUNING.md`](docs/PORTABILITY_AND_CPU_TUNING.md) | Running & tuning NetZero on Intel Alder/Raptor Lake, AMD Zen 3/4/5, GRUB, Fedora & Debian |
| [`docs/ARCHITECTURE_AND_RESEARCH_ORIGIN.md`](docs/ARCHITECTURE_AND_RESEARCH_ORIGIN.md) | Original kernel socket-stack research & design evolution |

---

## Repository Structure

```text
NetZero/
├── LICENSE                                       # GNU GPL v2.0 (Linux Kernel License)
├── README.md                                     # Project overview, 6-way highlights & quickstart
├── docs/
│   ├── NETZERO_V3_ENGINEERING_DEEP_DIVE.md       # Complete v3 Milestones 1-3 technical deep dive
│   ├── BAREMETAL_6WAY_BENCHMARKS.md              # Full 80-metric x 10-trial 6-way bare-metal report
│   ├── HARDENING_AND_VERIFICATION.md             # LOCKDEP/UBSAN KVM verification & hardened bug fixes
│   ├── NETZERO_V2_WHITEPAPER.md                  # Foundational v2 whitepaper
│   ├── PORTABILITY_AND_CPU_TUNING.md             # Cross-CPU / cross-distro tuning guide
│   └── ARCHITECTURE_AND_RESEARCH_ORIGIN.md       # Original research notes
├── patches/
│   ├── netzero-v3-7.4-rc2-full.patch             # Complete unified v3 patch (83 files, 12,554 lines)
│   ├── netzero-v2-7.4-rc2-full.patch             # Historical v2 unified patch
│   ├── netzero-v1-reference.patch                # Historical v1 patch
│   ├── series                                    # Modular v2 base patch series
│   └── split/                                    # 7 modular v2 base patches (01..07)
├── kernel-overlay/                               # Complete 83-file tree mirror of every modified/added file
├── configs/
│   ├── 99-netzero.conf                           # Recommended sysctl tuning (eiffel_dualq + bbr + tcp_ecn=2)
│   ├── tigerlake-host.config                     # Verified Tiger Lake v3-M3 host .config
│   └── kvm-minimal.config                        # Fast QEMU/KVM verification .config
├── scripts/
│   ├── fetch-and-patch.sh                        # Clones Linux @ 7e1392a42ea2 & applies NetZero v3
│   ├── build-host-kernel.sh                      # Builds v3 kernel & modules (ARCH_MARCH=tigerlake|native)
│   ├── install-netzero.sh                        # Safe side-by-side systemd-boot installer (one-shot safety)
│   └── uninstall-netzero.sh                      # Clean 1-command uninstaller
├── vm-verify/                                    # Rootless 9p QEMU/KVM Release & Debug verification scripts
└── benchmarks/                                   # Complete C23 + Wayland + dbus-broker + Python suite & 6-way JSONs
```

---

## Quickstart: Build, Verify & Install

### 1. Clone, Fetch Upstream Linux `7.4-rc2` & Apply NetZero v3
```bash
git clone https://github.com/Cetin-Furkan/NetZero.git
cd NetZero
./scripts/fetch-and-patch.sh
```
*(Or apply [`patches/netzero-v3-7.4-rc2-full.patch`](patches/netzero-v3-7.4-rc2-full.patch) / overlay [`kernel-overlay/`](kernel-overlay/) onto Linux `7e1392a42ea288d70a266f6553fe06f57f8647ea`.)*

### 2. Compile the Kernel (`LLVM=1` / Clang)
```bash
# On Intel Tiger Lake:
./scripts/build-host-kernel.sh

# On any other Intel or AMD x86_64 CPU:
ARCH_MARCH=native ./scripts/build-host-kernel.sh
```
*(Set `FULL_CONFIG=1` to build all distro modules instead of `localmodconfig` + preserved Intel SOF/HDA audio.)*

### 3. Install Safely Side-by-Side (Arch / CachyOS + `systemd-boot`)
```bash
sudo ./scripts/install-netzero.sh && sudo reboot
```
- Your existing kernel entries remain untouched, and `install-netzero.sh` arms **one-shot boot (`bootctl set-oneshot netzero.conf`)** so a simple reboot always falls back safely if needed.
- Make NetZero v3 permanent once verified:
  ```bash
  sudo bootctl set-default netzero.conf
  ```
- Uninstall cleanly at any time:
  ```bash
  sudo ./scripts/uninstall-netzero.sh
  ```

### 4. Run the Full 7-Suite Bare-Metal Benchmark & 14-Part POSIX ABI Torture Suite
```bash
cd benchmarks
make all
./netzero_bench
python3 ./dbus_broker_portal_torture.py 100
python3 ./run_grand_bench.py
```

---

## License

All kernel patches, kernel overlay files, and benchmarks are licensed under **GNU GPL v2.0** ([`LICENSE`](LICENSE)), matching the Linux kernel.
