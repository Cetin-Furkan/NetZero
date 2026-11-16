# Project NetZero v2 (`7.4.0-rc2-netzero-v2`)

**Cross-Subsystem Fused, C23 (`-std=gnu23`), Deep-ISA Hardware-Accelerated Linux Socket, VFS, IPC & L4S Networking Overhaul**

> **100% POSIX & Linux Binary ABI Compatible** — Zero userspace recompilation required. Every Wayland compositor, game engine, browser, database, HTTP/2 & QUIC stack automatically accelerates on boot.

---

## Headline Bare-Metal Results (`1,950` Runs vs. CachyOS `7.3-rc6`)

Verified across **65 metrics x 10 bare-metal trials** on an **Intel Core i7-1165G7 (Tiger Lake Willow Cove)** laptop under identical power/governor conditions (`powersave` / `balance_performance`):

| Subsystem | Benchmark | Stock CachyOS `7.3-rc6` | **Project NetZero v2** | Speedup / Savings |
| :--- | :--- | :---: | :---: | :---: |
| **Kernel RAM** | `UDP` 64-QUIC Socket Hoarded Pages | `335.40 pages` | **`25.40 pages`** | **`-92.4%` (`13.20x` less RAM)** |
| **Kernel RAM** | `AF_UNIX` 2,000-Socket `SUnreclaim` Slab | `7,074.80 KiB` | **`1,612.00 KiB`** | **`-77.2%` (`4.39x` less RAM)** |
| **Kernel RAM** | `TCP` Socket Struct Size (`/proc/net/protocols`) | `2,432 B` | **`2,176 B`** | **`-256 B` per TCP socket (`-24.5%` pages)** |
| **Desktop IPC** | `AF_UNIX` `64 B` Ping-Pong Latency | `3,029.92 ns` | **`707.70 ns`** | **`4.28x` faster (`-76.6%`)** |
| **Wayland IPC** | `AF_UNIX` `256 B` Wayland Wire RTT | `3,103.92 ns` | **`769.57 ns`** | **`4.03x` faster (`-75.2%`)** |
| **Desktop Stream** | `AF_UNIX` `4 KiB` Stream Throughput | `4.58 GiB/s` | **`10.90 GiB/s` (`11.59` p50)** | **`2.38x` faster (`+138.1%`)** |
| **Bulk IPC** | `AF_UNIX` `64 KiB` Bulk Stream Throughput | `10.60 GiB/s` | **`18.50 GiB/s` (`18.71` max)** | **`1.75x` faster (`+74.6%`)** |
| **Compositor** | 8-Client Wayland Compositor Sim | `340.39 kcommits/s` | **`421.36 kcommits/s`** | **`+23.8%` faster & `-20.0%` kernel CPU** |
| **Event Loop** | `epoll_wait` (`128` of `1,024` active sockets) | `40.21 ns/event` | **`17.36 ns/event`** | **`2.32x` faster (`-56.8%`)** |
| **Event Loop** | `epoll_wait` (`32` of `256` CPU cycles) | `102.41 cycles/ev` | **`49.86 cycles/ev` (`47.3` p50)** | **`2.05x` faster (`< 50` cycles/ev!)** |
| **Syscall Fusion** | `vfs_write_read_64B` (`read` + `write` pair) | `547.57 ns` | **`309.81 ns` (`150.6 ns/call`)** | **`1.77x` faster (`-43.4%`)** |
| **TCP / HTTP/2** | 64-Connection HTTP/2 Multiplexing | `373.78 kRPS` | **`406.76 kRPS`** | **`+8.8%` (`+33,000 RPS`)** |
| **UDP / QUIC** | Unconnected 4-Dest UDP / QUIC `1200B` Batch | `0.83 Mpps` / `8.92 Gbps` | **`0.93 Mpps` / `9.61 Gbps`** | **`+12.1%` UDP / `+7.7%` QUIC** |
| **Live Mutter** | Live GNOME Wayland `wl_display_roundtrip` `p99` | `20.60 us` | **`16.90 us`** | **`-18.0%` lower tail latency** |
| **Live Engine** | Wayland `engine-v2` Worst GPU Spike Under Storm | `1,841.19 us` | **`1,558.63 us`** | **`-15.3%` smoother (`124.95 FPS`)** |
| **Live Wi-Fi 7** | Saturated 4-Stream Upload `p99` Ping (`Bufferbloat`) | `88.26 ± 52.36 ms` | **`63.51 ± 0.91 ms`** | **`-28.0%` (`+2.7 ms` over `60.8 ms` idle!)** |

---

## Core Breakthroughs in Version 2

1. **Direct Cross-Subsystem Kernel Fusion ("No Syscall Wrappers, Zero Stack Bloat"):**
   - `read()`, `write()`, `send()`, and `recv()` on `AF_UNIX` streams fuse directly into [`unix_ring_direct_send()`](kernel-overlay/net/unix/af_unix.c) / `unix_ring_direct_recv()` straight from the raw `__user` pointer — **0 `kiocb`, 0 `iov_iter`, 0 `msghdr`, 0 `128 B` `sockaddr_storage` stack zeroing**.
   - [`sock_sendmsg_nosec()`](kernel-overlay/net/socket.c) and `sock_recvmsg_nosec()` jump straight into leaf `tcp_sendmsg()`, `udp_sendmsg()`, `tcp_recvmsg()`, and `udp_recvmsg()`, bypassing `inet_sendmsg` / `inet_recvmsg` intermediate layers.
   - [`vfs_poll()`](kernel-overlay/include/linux/poll.h) dispatches `socket_file_ops` directly to `sock_poll()` across `epoll`, `poll`, `ppoll`, `select`, and `io_uring`, paired with a zero-stack-bloat non-blocking [`do_sys_poll_nowait_small()`](kernel-overlay/fs/select.c) fastpath.
2. **C23 (`-std=gnu23`) + Hand-Crafted Hardware ISA Primitives ([`netzero_tigerlake.h`](kernel-overlay/include/linux/netzero_tigerlake.h)):**
   - Unlocks hardware instructions compilers cannot auto-emit under kernel `-mno-sse -mno-avx`: GP-register Castagnoli `crc32l`/`crc32q` (`3 cycles`, zero FPU), BMI1 `tzcntq`/`blsrq` (`1 cycle`), `rdpid`, `prefetchw`, `movdir64b`, `FSRM rep movsb`, and FPU-guarded `512-bit` `AVX-512` `vmovdqu64` (`256 B/iter`), all runtime-guarded via `cpu_feature_enabled()` static keys for full cross-CPU portability.
3. **Adaptive 2-Tier `unix_ring` (`16 KiB` -> `256 KiB`) + Willow Cove `48 KiB` L1D Pipeline Slicing:**
   - Starts every IPC socket at a tiny `16 KiB` (`Order-2`) footprint and live-upgrades bulk streams (`> 8 KiB`) to `256 KiB` (`Order-6`) via `AVX-512` without dropping a single byte, slicing publishes into `32 KiB` (`<= 48 KiB` L1D) sub-commits for **`18.50 GiB/s`** sustained IPC throughput while preserving exact `SCM_RIGHTS` fd-passing causality (`UNIXCB(skb).ring_seq`).
4. **`sch_eiffel_dualq` Lockless L4S Dual-Queue AQM + WAN/LAN Split ECN:**
   - New kernel packet scheduler ([`sch_eiffel_dualq.c`](kernel-overlay/net/sched/sch_eiffel_dualq.c)) combining Coupled PI2/L4S latency isolation with an `O(1)` 2-level bitmapped EDT timing wheel (`tzcntq`/`blsrq`), paired with middlebox-clean public WAN SYNs (`tcp_ecn = 2`) and automatic loopback/RFC1918 LAN `AccECN + L4S` negotiation ([`tcp_ecn.h`](kernel-overlay/include/net/tcp_ecn.h)).
5. **Upstream Linux `7.4-rc2` Intel SOF (`sof-hda-dsp` / `ALC236`) Audio Fix:**
   - Fixes the `devm_kzalloc_flex()` + `__counted_by(num_components)` out-of-bounds/NULL-slot bug in [`sound/soc/soc-core.c`](kernel-overlay/sound/soc/soc-core.c) that caused `"Dummy Output"` on modern Intel SOF laptops.

---

## Repository Structure

```text
NetZero/
├── LICENSE                                       # GNU GPL v2.0 (Linux Kernel License)
├── README.md                                     # Project overview & quickstart
├── docs/
│   ├── NETZERO_V2_WHITEPAPER.md                  # Exhaustive subsystem-by-subsystem engineering whitepaper
│   ├── BAREMETAL_3WAY_BENCHMARKS.md              # Full 65-metric x 10-trial 3-way statistical report
│   ├── PORTABILITY_AND_CPU_TUNING.md             # Running & tuning on Intel, AMD Zen, GRUB, Fedora/Debian
│   └── ARCHITECTURE_AND_RESEARCH_ORIGIN.md       # Original research & design evolution
├── patches/
│   ├── netzero-v2-7.4-rc2-full.patch             # Complete unified patch (60 files against Linux 7e1392a42ea2)
│   ├── netzero-v1-reference.patch                # Historical v1 patch for comparison
│   ├── series                                    # Patch series order
│   └── split/                                    # 7 modular subsystem patches (01..07)
├── kernel-overlay/                               # Full 60-file tree mirror of every modified/added kernel file
├── configs/
│   ├── 99-netzero.conf                           # Recommended sysctl tuning (eiffel_dualq + bbr + tcp_ecn=2)
│   ├── tigerlake-host.config                     # Verified Tiger Lake desktop host .config
│   └── kvm-minimal.config                        # Fast QEMU/KVM verification .config
├── scripts/
│   ├── fetch-and-patch.sh                        # Clones upstream Linux @ 7e1392a42ea2 & applies NetZero v2
│   ├── build-host-kernel.sh                      # Builds kernel & modules (supports ARCH_MARCH=tigerlake|native)
│   ├── install-netzero.sh                        # Safe side-by-side systemd-boot installer (one-shot boot safety)
│   └── uninstall-netzero.sh                      # Clean 1-command uninstaller
├── vm-verify/                                    # Rootless 9p QEMU/KVM instant kernel & ABI torture runner
└── benchmarks/                                   # Complete C + Wayland + Python Grand Benchmark Suite & raw JSONs
```

---

## Quickstart: Build, Verify & Install

### 1. Clone NetZero, Fetch Base Kernel & Apply Patch
```bash
git clone https://github.com/Cetin-Furkan/NetZero.git
cd NetZero
./scripts/fetch-and-patch.sh
```
*(Or apply [`patches/netzero-v2-7.4-rc2-full.patch`](patches/netzero-v2-7.4-rc2-full.patch) / copy [`kernel-overlay/`](kernel-overlay/) onto your own Linux `7.4-rc2` checkout.)*

### 2. Compile the Kernel
```bash
# On Intel Tiger Lake:
./scripts/build-host-kernel.sh

# On any other Intel or AMD x86_64 CPU:
ARCH_MARCH=native ./scripts/build-host-kernel.sh
```
*(Set `FULL_CONFIG=1` if you want to compile every distro module instead of `localmodconfig` + full audio preservation.)*

### 3. Install Safely Side-by-Side (Arch / CachyOS + `systemd-boot`)
```bash
sudo ./scripts/install-netzero.sh && sudo reboot
```
- Your stock kernel entries remain completely untouched.
- `install-netzero.sh` arms **one-shot boot (`bootctl set-oneshot netzero.conf`)**: if anything ever goes wrong, a simple reboot automatically boots back into your previous default kernel.
- Once verified, make NetZero v2 your permanent default:
  ```bash
  sudo bootctl set-default netzero.conf
  ```
- To uninstall completely at any time:
  ```bash
  sudo ./scripts/uninstall-netzero.sh
  ```

### 4. Run the 7-Suite Bare-Metal Benchmark & POSIX ABI Torture Suite
```bash
cd benchmarks
make
python3 ./run_grand_bench.py --trials 10
```

---

## Deep-Dive Documentation
- **[Complete Engineering Whitepaper (`docs/NETZERO_V2_WHITEPAPER.md`)](docs/NETZERO_V2_WHITEPAPER.md)**
- **[Full 1,950-Run Bare-Metal Benchmark Report (`docs/BAREMETAL_3WAY_BENCHMARKS.md`)](docs/BAREMETAL_3WAY_BENCHMARKS.md)**
- **[CPU Portability & Multi-Distro Tuning (`docs/PORTABILITY_AND_CPU_TUNING.md`)](docs/PORTABILITY_AND_CPU_TUNING.md)**
