# Grand Benchmark Summary — `7.4.0-rc2-netzero-v3-m2+`

- **Timestamp:** `2026-11-19T16:37:26.086896`
- **Kernel:** `Linux arch-cetin 7.4.0-rc2-netzero-v3-m2+ #7 SMP PREEMPT_DYNAMIC Thu Nov 19 16:29:13 +03 2026 x86_64 GNU/Linux`
- **Default Qdisc / CC / ECN:** `eiffel_dualq` / `bbr` / `tcp_ecn=2`
- **Governor / EPP:** `powersave` / `balance_performance`
- **Protocol Struct Sizes (`/proc/net/protocols`):** `TCP=2176 B`, `TCPv6=2304 B`, `UDP=1344 B`, `UNIX-STREAM=1088 B`

| Suite | Metric | Mean ± StdDev | Median (p50) | p99 | Min .. Max | Unit |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `S1_Memory` | `tcp_8stream_active_pages` | **3070.30** ± 905.60 | 3118.00 | 4430.00 | 1521.00 .. 4430.00 | `pages` |
| `S1_Memory` | `udp_64quic_socks_hoarded_pages` | **29.50** ± 10.60 | 32.00 | 39.00 | 0.00 .. 39.00 | `pages` |
| `S1_Memory` | `unix_2000socks_8000msg_sunreclaim_kib` | **1718.40** ± 130.94 | 1740.00 | 1948.00 | 1500.00 .. 1948.00 | `KiB` |
| `S2_VFS_Epoll` | `cmsg_scm_rights_creds_ns` | **1227.35** ± 8.07 | 1228.44 | 1239.38 | 1215.33 .. 1239.38 | `ns/pair` |
| `S2_VFS_Epoll` | `epoll_ctl_mod_1024fd_ns` | **148.47** ± 2.62 | 149.18 | 152.13 | 144.49 .. 152.13 | `ns/op` |
| `S2_VFS_Epoll` | `epoll_wait_128_of_1024_ns_ev` | **12.62** ± 0.12 | 12.62 | 12.86 | 12.50 .. 12.86 | `ns/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_cyc_ev` | **38.84** ± 0.30 | 38.82 | 39.38 | 38.28 .. 39.38 | `cycles/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_ns_ev` | **13.85** ± 0.11 | 13.85 | 14.05 | 13.66 .. 14.05 | `ns/ev` |
| `S2_VFS_Epoll` | `eventfd_pingpong_ns` | **3102.28** ± 91.81 | 3076.08 | 3358.22 | 3036.86 .. 3358.22 | `ns/RTT` |
| `S2_VFS_Epoll` | `iouring_unix_64B_pingpong_ns` | **3985.13** ± 78.58 | 4012.59 | 4085.99 | 3832.69 .. 4085.99 | `ns/RTT` |
| `S2_VFS_Epoll` | `iouring_unix_batch32_mops` | **12.64** ± 0.12 | 12.63 | 12.83 | 12.47 .. 12.83 | `Mops/s` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_cycles` | **2608.26** ± 39.08 | 2610.71 | 2660.11 | 2551.44 .. 2660.11 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_ns` | **930.47** ± 13.94 | 931.34 | 948.97 | 910.20 .. 948.97 | `ns/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_cycles` | **269.81** ± 2.05 | 269.98 | 272.87 | 265.53 .. 272.87 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_ns` | **96.25** ± 0.73 | 96.31 | 97.34 | 94.72 .. 97.34 | `ns/call` |
| `S2_VFS_Epoll` | `pipe_64B_pingpong_ns` | **3406.70** ± 240.24 | 3342.09 | 4081.66 | 3240.37 .. 4081.66 | `ns/RTT` |
| `S2_VFS_Epoll` | `poll_2fd_pingpong_ns` | **3840.56** ± 23.49 | 3835.42 | 3887.47 | 3803.11 .. 3887.47 | `ns/RTT` |
| `S2_VFS_Epoll` | `ppoll_4fd_ready_ns` | **251.35** ± 4.98 | 252.83 | 257.18 | 238.79 .. 257.18 | `ns/call` |
| `S2_VFS_Epoll` | `shared_fd_8threads_mops` | **10.07** ± 0.20 | 10.10 | 10.32 | 9.80 .. 10.32 | `Mops/s` |
| `S2_VFS_Epoll` | `vfs_write_read_64B_ns` | **277.05** ± 2.93 | 276.40 | 281.50 | 274.02 .. 281.50 | `ns/pair` |
| `S3_AF_UNIX` | `dbus_broker_sim_2hop_krps` | **210.51** ± 1.87 | 211.21 | 213.23 | 208.04 .. 213.23 | `kRPS` |
| `S3_AF_UNIX` | `scm_rights_memfd_pair_ns` | **609.50** ± 7.18 | 608.83 | 625.49 | 600.27 .. 625.49 | `ns/pair` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_mean_ns` | **803.02** ± 12.81 | 800.61 | 823.26 | 784.75 .. 823.26 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_p99_ns` | **4127.30** ± 176.77 | 4101.50 | 4369.00 | 3878.00 .. 4369.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_4KB_stream_gibs` | **12.01** ± 0.16 | 12.05 | 12.20 | 11.69 .. 12.20 | `GiB/s` |
| `S3_AF_UNIX` | `unix_64B_pingpong_mean_ns` | **732.34** ± 28.46 | 723.54 | 807.46 | 709.84 .. 807.46 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64B_pingpong_p99_ns` | **3545.70** ± 230.84 | 3475.00 | 4185.00 | 3381.00 .. 4185.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64KB_stream_gibs` | **19.05** ± 0.15 | 19.02 | 19.26 | 18.77 .. 19.26 | `GiB/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_kcommits_s` | **459.06** ± 8.35 | 456.39 | 474.68 | 450.15 .. 474.68 | `kcommits/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_sys_cpu_ms` | **186.98** ± 4.06 | 185.93 | 196.53 | 182.65 .. 196.53 | `ms` |
| `S4_TCP_UDP` | `quic_1200B_batch_gbps` | **9.74** ± 0.09 | 9.75 | 9.91 | 9.61 .. 9.91 | `Gbps` |
| `S4_TCP_UDP` | `tcp_2000conn_handshake_us` | **15.58** ± 0.31 | 15.44 | 16.21 | 15.32 .. 16.21 | `us/conn` |
| `S4_TCP_UDP` | `tcp_64conn_http2_mux_krps` | **402.86** ± 2.69 | 403.37 | 408.03 | 398.21 .. 408.03 | `kRPS` |
| `S4_TCP_UDP` | `tcp_bulk_1stream_gibs` | **8.74** ± 0.28 | 8.69 | 9.10 | 8.39 .. 9.10 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_gibs` | **5.91** ± 0.10 | 5.90 | 6.09 | 5.75 .. 6.09 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_sys_cpu_ms` | **289.99** ± 5.19 | 289.27 | 298.85 | 280.69 .. 298.85 | `ms` |
| `S4_TCP_UDP` | `udp_unconn_4dest_mpps` | **0.94** ± 0.01 | 0.93 | 0.95 | 0.93 .. 0.95 | `Mpps` |
| `S5_Qdisc` | `qdisc_8flow_paced_gbps` | **8.54** ± 1.98 | 9.15 | 9.29 | 2.92 .. 9.29 | `Gbps` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p50_us` | **7.35** ± 0.04 | 7.36 | 7.39 | 7.26 .. 7.39 | `us` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p99_us` | **18.91** ± 4.83 | 18.12 | 31.03 | 13.94 .. 31.03 | `us` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_sys_ms` | **2.88** ± 0.92 | 3.10 | 4.06 | 1.71 .. 4.06 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_wall_ms` | **9.95** ± 1.38 | 10.65 | 11.55 | 7.82 .. 11.55 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_sys_ms` | **1.14** ± 0.79 | 1.48 | 2.19 | 0.00 .. 2.19 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_wall_ms` | **2.17** ± 1.48 | 1.73 | 6.34 | 1.45 .. 6.34 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_fps` | **124.96** ± 0.02 | 124.96 | 125.00 | 124.93 .. 125.00 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_avg_us` | **458.15** ± 10.16 | 452.69 | 476.42 | 448.18 .. 476.42 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_max_us` | **545.49** ± 18.02 | 545.34 | 569.48 | 519.27 .. 569.48 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_sys_cpu_ms` | **97.29** ± 8.05 | 100.41 | 105.15 | 78.70 .. 105.15 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_total_proc_ms` | **2544.79** ± 8.15 | 2543.85 | 2561.49 | 2536.00 .. 2561.49 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_user_cpu_ms` | **59.23** ± 6.78 | 58.89 | 75.45 | 50.72 .. 75.45 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_wall_ms` | **2400.71** ± 0.37 | 2400.73 | 2401.28 | 2399.96 .. 2401.28 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_fps` | **124.86** ± 1.10 | 125.32 | 125.36 | 121.78 .. 125.36 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_avg_us` | **514.75** ± 216.20 | 435.79 | 1126.73 | 433.00 .. 1126.73 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_max_us` | **1238.65** ± 1166.82 | 539.24 | 3526.72 | 513.91 .. 3526.72 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_sys_cpu_ms` | **83.69** ± 16.94 | 77.63 | 128.55 | 70.60 .. 128.55 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_total_proc_ms` | **2569.11** ± 31.83 | 2559.61 | 2618.72 | 2534.88 .. 2618.72 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_user_cpu_ms` | **49.42** ± 29.45 | 40.41 | 132.02 | 30.76 .. 132.02 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_wall_ms` | **2402.77** ± 21.70 | 2393.93 | 2463.39 | 2393.13 .. 2463.39 | `ms` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_mean_us` | **8.88** ± 0.12 | 8.90 | 9.08 | 8.69 .. 9.08 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p50_us` | **8.56** ± 0.01 | 8.56 | 8.58 | 8.54 .. 8.58 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p99_us` | **14.30** ± 2.63 | 13.45 | 19.16 | 11.22 .. 19.16 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_sys_ms` | **2.92** ± 1.30 | 2.82 | 5.27 | 1.20 .. 5.27 | `ms` |
| `S7_Live_Internet` | `download_4stream_mbps` | **22.68** ± 0.46 | 22.94 | 23.10 | 22.05 .. 23.10 | `Mbit/s` |
| `S7_Live_Internet` | `download_loaded_ping_p50_ms` | **48.81** ± 0.24 | 48.80 | 49.20 | 48.40 .. 49.20 | `ms` |
| `S7_Live_Internet` | `download_loaded_ping_p99_ms` | **72.27** ± 37.21 | 56.70 | 174.00 | 52.60 .. 174.00 | `ms` |
| `S7_Live_Internet` | `https_tcp_connect_ms` | **33.37** ± 7.04 | 29.80 | 47.41 | 27.47 .. 47.41 | `ms` |
| `S7_Live_Internet` | `https_tls_handshake_ms` | **69.09** ± 10.83 | 64.51 | 94.64 | 60.45 .. 94.64 | `ms` |
| `S7_Live_Internet` | `https_ttfb_ms` | **116.74** ± 12.42 | 110.38 | 144.01 | 106.65 .. 144.01 | `ms` |
| `S7_Live_Internet` | `idle_ping_jitter_stdev_ms` | **1.23** ± 0.63 | 0.96 | 2.46 | 0.65 .. 2.46 | `ms` |
| `S7_Live_Internet` | `idle_ping_mean_ms` | **22.89** ± 0.54 | 22.84 | 23.84 | 22.17 .. 23.84 | `ms` |
| `S7_Live_Internet` | `idle_ping_p99_ms` | **25.15** ± 2.36 | 24.20 | 29.60 | 23.20 .. 29.60 | `ms` |
| `S7_Live_Internet` | `upload_4stream_mbps` | **7.10** ± 0.17 | 7.07 | 7.36 | 6.86 .. 7.36 | `Mbit/s` |
| `S7_Live_Internet` | `upload_loaded_ping_p50_ms` | **66.07** ± 3.39 | 66.70 | 70.30 | 60.25 .. 70.30 | `ms` |
| `S7_Live_Internet` | `upload_loaded_ping_p99_ms` | **85.58** ± 7.00 | 84.55 | 96.80 | 77.00 .. 96.80 | `ms` |
