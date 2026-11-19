# Project NetZero v3 — Hardening, Verification & Root-Cause Forensics

Every kernel optimization in Project NetZero v3 is gated behind **dual-kernel QEMU/KVM verification**:
1. **Release KVM (`SMP=8`, Tiger Lake `-cpu host`):** Validates performance and 100% POSIX/Linux ABI conformance across 14 stress/torture suites + real `/usr/bin/dbus-broker` + `GDBus`.
2. **Debug KVM (`SMP=8`, `CONFIG_PROVE_LOCKING` / `LOCKDEP` + `CONFIG_UBSAN` + `CONFIG_DEBUG_ATOMIC_SLEEP` + `CONFIG_DEBUG_LIST`):** Validates zero lock-order inversions, zero sleeping-in-atomic violations, zero undefined behavior, zero list corruptions, and zero `refcount_t` underflows/overflows.

Every `vm-verify/verify-v3-payload.sh` run audits the complete `dmesg` ring buffer with a strict regex gate and **fails hard** on any `BUG:`, `Oops:`, `WARNING:`, `UBSAN:`, `KASAN:`, `LOCKDEP`, `suspicious RCU`, `sleeping function called from invalid context`, `scheduling while atomic`, `list_del corruption`, or `refcount_t:` splat.

---

## 1. Automated 14-Part POSIX ABI & Concurrency Torture Suite (`benchmarks/netzero_bench.c`)

| Torture ID | Subsystem Tested | Exact Stress & ABI Edge Cases Verified | Result (Release & Debug KVM) |
| :--- | :--- | :--- | :---: |
| `ABI Torture 1` | `fs/file.c` Hazard-Slot `fdget` | Multi-threaded `CLONE_FILES` `15,000` concurrent `dup2`/`close`/`recv` races | **PASS** |
| `ABI Torture 2–4` | `procfs` / `EPOLLET` / `AF_UNIX` | `/proc/self/fd` symlink, `EPOLLET` `1→0→1` edge, `SCM_RIGHTS` `'AAABBBCCC'` causality + cyclic GC | **PASS** |
| `M1 Torture A` | `fs/eventfd.c` | `EFD_NONBLOCK`, `EFD_SEMAPHORE`, `ULLONG_MAX-1` saturation + blocking wakeup, `EPOLLET`, `EFAULT`, 4W+4R exact sum (`250,000`) | **PASS** |
| `M1 Torture B` | `fs/pipe.c`, `timerfd`, `pidfd` | Small/multi-page `pipe2`, `O_DIRECT`, `EFAULT` non-destructive check, `EPIPE`/`SIGPIPE`, `timerfd`, `80`-`pidfd` epoll/poll | **PASS** |
| `M1 Torture C` | `AF_UNIX` 4-FD Sideband Ring | 1–4 FD inline ring, 12-FD fallback, `MSG_PEEK`, `MSG_CMSG_CLOEXEC`, `MSG_CTRUNC`, `EMFILE` ×100, unread close ×200, cyclic GC | **PASS** |
| `M1 Torture D` | `AF_UNIX` Multi-Iovec `sendmsg` | 1..4 `iovec`, zero-len, `16K`/`64K`/`256K` ring wrap, `2 MiB` stream, `MSG_DONTWAIT`, `EFAULT`, `SIGPIPE` | **PASS** |
| `M1 Torture E` | `net/socket.c` `accept4` Peek | TCP `2,000/2,000` + `AF_UNIX` `2,000/2,000` nonblock races, `EINVAL`/`EBADF`/`ENOTSOCK`/`CLOEXEC`/`NONBLOCK` | **PASS** |
| `M1 Torture F` | `dbus-broker` `SCM_RIGHTS` + `EPOLLOUT` | `eventfd`/`memfd`/`pipe`/`AF_UNIX` FD passing + `GDBus` split reads + 16× flow-control (`0.54 ms`) | **PASS** |
| `M1 Torture G` | `AF_UNIX` Ring Expand & Watermark | `512 B` small-burst expansion (`> 64 KiB`, `send` + 3-iov) + 25% watermark `EPOLLET|EPOLLOUT` + `poll(POLLOUT)` | **PASS** |
| `M1 Torture H` | `dbus-broker` `SIOCOUTQ` & `sendmmsg` | `ioctl(SIOCOUTQ) <= 128` + `GDBus` 2-step read + mixed `sendmmsg` (inline + SKB) `2,000`-iter race | **PASS** |
| `M2 Torture A` | `fs/select.c` Small `poll`/`ppoll` | Ready-first 6-FD, blocking wakeup, `EINTR` signal handling, pin-free private vs `fork`/`CLONE_FILES`, `> 8` slowpath | **PASS** |
| `M2 Torture B` | `fs/eventpoll.c` `CRC32Q` `ctl_cache` | `1,024`-FD `ADD`/`MOD`/`DEL`, `dup()` aliasing, close-without-`DEL` FD slot reuse | **PASS** |
| `M2 Torture C` | `fs/eventpoll.c` Pin-Free Leaf `ep_item_poll` | Mixed `AF_UNIX` stream/dgram + TCP + UDP + `eventfd` + `pipe` + `timerfd` + `pidfd` + nested epoll | **PASS** |
| `M2 Torture D` | `io_uring` Unboxed `SEND`/`RECV` | `IORING_CQE_F_SOCK_NONEMPTY`, async poll retry, multi-chunk `MSG_WAITALL` (`AF_UNIX` + TCP), `OP_READ`/`OP_WRITEV`, `-EFAULT` | **PASS** |
| `M3 Torture A` | `kernel/futex/core.c` `CRC32Q` `futex` | Unaligned `+1`/`+2` `-EINVAL`, `PROT_NONE`/kernel-addr `-EFAULT`, `MAP_SHARED` global hash, 8T contended lock (`42.48 Mops/s` KVM) | **PASS** |
| `M3 Torture B` | `WF_SYNC` + BORE-EEVDF Scheduler | Unpinned `eventfd` RTT + 8× 100%-CPU hog storm vs short-burst interactive responder (`1.84 µs` p50 KVM / `2.10 µs` bare metal) | **PASS** |
| **Live Daemon** | `/usr/bin/dbus-broker` + `libgio` `GDBus` | 6 clients × 100 loops (`1,200` RPCs + `600` `SCM_RIGHTS` FDs + `1,200` signals, `0` timeouts) | **PASS** |

---

## 2. Hardened Kernel Edge Cases Caught & Fixed During Development

1. **`AF_UNIX` `splice()` (`msg == NULL`) Wakeup Loop on `rx_ring` (`net/unix/af_unix.c`):**
   - *Bug:* `unix_stream_read_generic()` gated `rx_ring` drain on `msg != NULL`. When `unix_stream_splice_read()` (`msg == NULL`) woke from `unix_stream_data_wait()` after a peer wrote into `rx_ring`, it could spin instead of falling back to `copy_splice_read()`.
   - *Fix:* Broke out cleanly with `-EAGAIN` when `!msg` observes unread `rx_ring` data, letting `unix_stream_splice_read()` drain safely through `copy_splice_read()`.
2. **`eventfd_read_fast()` Non-Destructive `-EFAULT` Validation (`fs/eventfd.c`):**
   - *Hardening:* Added upfront `if (unlikely(!access_ok(buf, sizeof(ucnt)))) return -EFAULT;` before acquiring `ctx->wqh.lock` so invalid userspace buffers never drain `ctx->count`.
3. **EEVDF Augmented RB-Tree `min_slice`/`max_slice` Invariant (`kernel/sched/fair.c`):**
   - *Invariant:* Linux `7.4-rc2` EEVDF maintains `RB_AUG(slice, min_slice, min)` and `RB_AUG(slice, max_slice, max)` inside `cfs_rq->tasks_timeline`. Mutating `se->slice` while `se` is queued in the RB-tree would silently corrupt augmented subtree metadata.
   - *Hardening:* BORE-EEVDF's `nz_bore_slice(se)` is invoked *only* inside `update_deadline()` (on `cfs_rq->curr`, outside the tree) and `place_entity()` (prior to `__enqueue_entity()`). `se->burst_ns` uses saturating `u32` arithmetic (`<= U32_MAX`) inside `sched_entity`'s existing 4-byte hole (zero struct growth).
4. **`WF_SYNC` `wakee_flips` Decay Trap (`kernel/sched/fair.c`):**
   - *Finding:* `record_wakee()` decays `wakee_flips` only once per second (`+ HZ`). Multi-threaded desktop apps (GNOME Shell, Chromium, `grand_socket_bench`) exceed `wakee_flips > 2` after spawning startup threads, silently disabling same-core handoff if gated on `wakee_flips <= 2`.
   - *Fix:* Gated same-core handoff on `want_affine` (which already filters M:N fanout via `!wake_wide(p)`) + `READ_ONCE(p->last_wakee) == current` + `< 200 µs` burst length, immediately slashing unpinned `eventfd`/`pipe` RTT by `1.70x` and boosting 8-client Wayland compositor sim by `+23.6%` in KVM.
5. **Upstream Linux `7.4-rc2` Intel SOF Audio `__counted_by` Fix (`sound/soc/soc-core.c`):**
   - *Bug:* `devm_kzalloc_flex()` + `__counted_by(num_components)` left `sof-hda-dsp` / `ALC236` failing with `"Dummy Output"` on Tiger Lake laptops.
   - *Fix:* Patched [`sound/soc/soc-core.c`](../kernel-overlay/sound/soc/soc-core.c) so full SOF/HDA/SoundWire audio initializes cleanly on boot.

---

## 3. Userspace GPU Forensics Note (Intel Iris Xe + `nvidia-utils` Chrome Crash)

During bare-metal testing on Intel-only Tiger Lake Iris Xe (`00:02.0`), Chromium exhibited `SyncEGL.cpp:72 (0x3009)`, `MakeFromBackendTexture() failed`, transparent UI `SharedImage` holes exposing the desktop wallpaper, and GPU process `SIGSEGV` (`exit_code=139`) whenever WebGPU (`navigator.gpu.requestAdapter()`) probed Vulkan ICDs.

Disassembly of `/usr/lib/libGLX_nvidia.so` proved this was **not a kernel bug**: `nvidia-utils` (pulled in as an indirect dependency) installed `/usr/share/vulkan/icd.d/nvidia_icd.json`; when `libvulkan.so.1` probed it on an Intel-only laptop, `libGLX_nvidia.so` found no NVIDIA GPU and called `eglReleaseThread()` on Chrome's GPU main thread, unbinding Chrome's active Mesa `EGLContext`.

**Fix for Intel-only laptops with `nvidia-utils` installed:** Pin Mesa in `~/.config/environment.d/10-intel-gpu.conf`:
```ini
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/intel_icd.json
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
```
Result: `0` `SyncEGL` errors and **`1.82x–1.87x` lower user CPU** in Vulkan/EGL rendering (`engine-v2` storm user CPU dropped from `75.70 ms` → `40.41–41.61 ms`).
