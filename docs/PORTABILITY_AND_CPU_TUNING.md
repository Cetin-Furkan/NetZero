# Hardware Portability, CPU Microarchitecture Tuning & Multi-Distro Guide

Although **Project NetZero v2** was co-designed around **Intel Tiger Lake (`Willow Cove`, Core i7-1165G7)**, **100% of its custom assembly instructions are guarded by Linux static CPU feature keys (`cpu_feature_enabled(...)`)** in [`kernel-overlay/include/linux/netzero_tigerlake.h`](../kernel-overlay/include/linux/netzero_tigerlake.h).

That means **any modern `x86_64` CPU (Intel or AMD)** can safely compile and boot NetZero v2 without illegal-instruction faults (`#UD`)!

---

## 1. How Hardware Primitives Adapt Across CPUs Automatically

| Primitive | Instruction | Intel Tiger Lake / Ice Lake / Rocket Lake / Sapphire Rapids | Intel Alder / Raptor / Arrow / Lunar Lake (Hybrid P+E) | AMD Zen 4 / Zen 5 (`Ryzen 7000/8000/9000`) | AMD Zen 2 / Zen 3 (`Ryzen 3000/5000`) |
| :--- | :--- | :---: | :---: | :---: | :---: |
| `nz_crc32c_u32/64` | `crc32l` / `crc32q` | Native HW | Native HW | Native HW | Native HW |
| `nz_tzcnt64` / `nz_blsr64` | `tzcntq` / `blsrq` (`BMI1`) | Native HW | Native HW | Native HW | Native HW |
| `nz_rdpid_cpu` | `rdpid` | Native HW | Native HW | Native HW | Native HW |
| `nz_prefetchw_range` | `prefetchw` | Native HW | Native HW | Native HW | Native HW |
| `nz_avx512_kcopy` | `vmovdqu64 %zmm0..3` | **Native `512-bit` AVX-512** | Auto-falls back to **Native `FSRM rep movsb`** | **Native `512-bit` AVX-512** | Auto-falls back to **Native `FSRM` / `memcpy`** |
| `nz_movdir64b` | `movdir64b` | Native HW | Native HW | Auto-falls back to `memcpy` | Auto-falls back to `memcpy` |

---

## 2. Selecting the Optimal `ARCH_MARCH` Flag When Building

`scripts/build-host-kernel.sh` accepts `ARCH_MARCH=<uarch>`:

- **Intel 11th Gen Tiger Lake (Default):**
  ```bash
  ./scripts/build-host-kernel.sh
  ```
- **Auto-detect the current machine's exact CPU (`-march=native`):**
  ```bash
  ARCH_MARCH=native ./scripts/build-host-kernel.sh
  ```
  *(Note: If using `ARCH_MARCH=native` or building for a different CPU, you can also override the 6 subsystem `Makefile` `-march=tigerlake` flags via `sed -i 's/-march=tigerlake -mtune=tigerlake/-march=native -mtune=native/g' linux-src/fs/Makefile linux-src/net/*/Makefile linux-src/net/Makefile linux-src/io_uring/Makefile` before building.)*
- **Portable Generic `x86-64-v3` Binary (safe to distribute as a prebuilt package across almost all 2015+ Intel/AMD PCs):**
  ```bash
  ARCH_MARCH=x86-64-v3 FULL_CONFIG=1 ./scripts/build-host-kernel.sh
  ```

---

## 3. Bootloader Notes (systemd-boot, GRUB, Fedora, Ubuntu/Debian)

- **Arch Linux / CachyOS with `systemd-boot`:**
  - Fully automated by `sudo ./scripts/install-netzero.sh` (uses `bootctl set-oneshot netzero.conf` so a second reboot automatically returns to your previous kernel unless you run `sudo bootctl set-default netzero.conf`).
- **Arch Linux / CachyOS with `GRUB`:**
  ```bash
  sudo make LLVM=1 -C linux-src O=build-host modules_install
  sudo install -Dm644 build-host/arch/x86/boot/bzImage /boot/vmlinuz-linux-netzero
  sudo mkinitcpio -k "$(make -s LLVM=1 -C linux-src O=build-host kernelrelease)" -g /boot/initramfs-linux-netzero.img
  sudo install -Dm644 configs/99-netzero.conf /etc/sysctl.d/99-netzero.conf
  sudo grub-mkconfig -o /boot/grub/grub.cfg
  ```
- **Debian / Ubuntu / Fedora:**
  ```bash
  sudo make LLVM=1 -C linux-src O=build-host modules_install install
  sudo install -Dm644 configs/99-netzero.conf /etc/sysctl.d/99-netzero.conf
  ```
