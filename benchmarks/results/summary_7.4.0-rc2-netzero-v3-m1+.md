# Grand Benchmark Summary — `7.4.0-rc2-netzero-v3-m1+`

- **Timestamp:** `2026-11-19T13:00:09.490433`
- **Kernel:** `Linux arch-cetin 7.4.0-rc2-netzero-v3-m1+ #5 SMP PREEMPT_DYNAMIC Thu Nov 19 04:03:17 +03 2026 x86_64 GNU/Linux`
- **Default Qdisc / CC / ECN:** `eiffel_dualq` / `bbr` / `tcp_ecn=2`
- **Governor / EPP:** `powersave` / `balance_performance`
- **Protocol Struct Sizes (`/proc/net/protocols`):** `TCP=2176 B`, `TCPv6=2304 B`, `UDP=1344 B`, `UNIX-STREAM=1088 B`

| Suite | Metric | Mean ± StdDev | Median (p50) | p99 | Min .. Max | Unit |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `S1_Memory` | `tcp_8stream_active_pages` | **3899.90** ± 1277.58 | 3923.00 | 5179.00 | 1355.00 .. 5179.00 | `pages` |
| `S1_Memory` | `udp_64quic_socks_hoarded_pages` | **30.60** ± 1.71 | 31.00 | 32.00 | 27.00 .. 32.00 | `pages` |
| `S1_Memory` | `unix_2000socks_8000msg_sunreclaim_kib` | **1362.00** ± 167.47 | 1264.00 | 1736.00 | 1232.00 .. 1736.00 | `KiB` |
| `S2_VFS_Epoll` | `cmsg_scm_rights_creds_ns` | **1286.41** ± 18.99 | 1288.57 | 1307.80 | 1256.88 .. 1307.80 | `ns/pair` |
| `S2_VFS_Epoll` | `epoll_wait_128_of_1024_ns_ev` | **17.64** ± 0.09 | 17.64 | 17.78 | 17.50 .. 17.78 | `ns/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_cyc_ev` | **48.57** ± 0.47 | 48.40 | 49.31 | 47.84 .. 49.31 | `cycles/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_ns_ev` | **17.33** ± 0.17 | 17.27 | 17.59 | 17.07 .. 17.59 | `ns/ev` |
| `S2_VFS_Epoll` | `eventfd_pingpong_ns` | **3076.95** ± 15.86 | 3075.61 | 3101.11 | 3052.63 .. 3101.11 | `ns/RTT` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_cycles` | **2587.77** ± 25.30 | 2581.97 | 2635.98 | 2556.59 .. 2635.98 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_ns` | **923.15** ± 9.03 | 921.08 | 940.35 | 912.02 .. 940.35 | `ns/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_cycles` | **269.78** ± 2.22 | 270.14 | 272.81 | 265.37 .. 272.81 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_ns` | **96.24** ± 0.79 | 96.37 | 97.32 | 94.66 .. 97.32 | `ns/call` |
| `S2_VFS_Epoll` | `pipe_64B_pingpong_ns` | **3297.35** ± 27.61 | 3298.89 | 3348.59 | 3256.53 .. 3348.59 | `ns/RTT` |
| `S2_VFS_Epoll` | `shared_fd_8threads_mops` | **10.28** ± 0.12 | 10.28 | 10.54 | 10.06 .. 10.54 | `Mops/s` |
| `S2_VFS_Epoll` | `vfs_write_read_64B_ns` | **275.53** ± 2.64 | 275.29 | 279.64 | 272.13 .. 279.64 | `ns/pair` |
| `S3_AF_UNIX` | `dbus_broker_sim_2hop_krps` | **213.36** ± 2.00 | 212.93 | 217.45 | 211.21 .. 217.45 | `kRPS` |
| `S3_AF_UNIX` | `scm_rights_memfd_pair_ns` | **613.80** ± 10.98 | 608.85 | 629.92 | 602.24 .. 629.92 | `ns/pair` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_mean_ns` | **888.82** ± 94.31 | 853.23 | 1081.36 | 814.63 .. 1081.36 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_p99_ns` | **4631.30** ± 557.65 | 4378.00 | 5984.00 | 4221.00 .. 5984.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_4KB_stream_gibs` | **11.58** ± 0.18 | 11.62 | 11.77 | 11.11 .. 11.77 | `GiB/s` |
| `S3_AF_UNIX` | `unix_64B_pingpong_mean_ns` | **768.02** ± 82.45 | 744.18 | 997.87 | 720.74 .. 997.87 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64B_pingpong_p99_ns` | **3983.90** ± 701.76 | 3887.50 | 5847.00 | 3425.00 .. 5847.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64KB_stream_gibs` | **18.70** ± 0.15 | 18.73 | 18.87 | 18.45 .. 18.87 | `GiB/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_kcommits_s` | **461.79** ± 14.76 | 464.07 | 480.20 | 422.48 .. 480.20 | `kcommits/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_sys_cpu_ms` | **186.37** ± 8.10 | 184.42 | 206.88 | 180.07 .. 206.88 | `ms` |
| `S4_TCP_UDP` | `quic_1200B_batch_gbps` | **10.22** ± 0.11 | 10.24 | 10.38 | 10.06 .. 10.38 | `Gbps` |
| `S4_TCP_UDP` | `tcp_2000conn_handshake_us` | **15.52** ± 0.48 | 15.53 | 16.08 | 14.84 .. 16.08 | `us/conn` |
| `S4_TCP_UDP` | `tcp_64conn_http2_mux_krps` | **410.47** ± 2.97 | 410.98 | 415.82 | 405.56 .. 415.82 | `kRPS` |
| `S4_TCP_UDP` | `tcp_bulk_1stream_gibs` | **8.72** ± 0.13 | 8.68 | 9.00 | 8.53 .. 9.00 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_gibs` | **5.98** ± 0.19 | 5.92 | 6.46 | 5.78 .. 6.46 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_sys_cpu_ms` | **286.75** ± 8.78 | 290.14 | 295.62 | 265.38 .. 295.62 | `ms` |
| `S4_TCP_UDP` | `udp_unconn_4dest_mpps` | **0.93** ± 0.01 | 0.93 | 0.95 | 0.92 .. 0.95 | `Mpps` |
| `S5_Qdisc` | `qdisc_8flow_paced_gbps` | **9.18** ± 0.08 | 9.15 | 9.36 | 9.07 .. 9.36 | `Gbps` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p50_us` | **7.33** ± 0.13 | 7.29 | 7.70 | 7.24 .. 7.70 | `us` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p99_us` | **17.57** ± 3.40 | 17.33 | 25.68 | 13.02 .. 25.68 | `us` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_sys_ms` | **0.97** ± 0.92 | 0.77 | 2.47 | 0.00 .. 2.47 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_wall_ms` | **8.07** ± 0.32 | 7.97 | 8.73 | 7.74 .. 8.73 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_sys_ms` | **1.00** ± 0.76 | 1.37 | 2.00 | 0.00 .. 2.00 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_wall_ms` | **1.74** ± 0.65 | 1.53 | 3.55 | 1.38 .. 3.55 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_fps` | **124.96** ± 0.05 | 124.98 | 125.03 | 124.84 .. 125.03 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_avg_us` | **461.27** ± 12.91 | 457.31 | 478.65 | 447.81 .. 478.65 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_max_us` | **548.63** ± 34.36 | 543.15 | 605.26 | 493.38 .. 605.26 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_sys_cpu_ms` | **101.04** ± 5.06 | 102.22 | 107.10 | 93.06 .. 107.10 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_total_proc_ms` | **2567.08** ± 6.21 | 2566.32 | 2579.52 | 2559.57 .. 2579.52 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_user_cpu_ms` | **85.55** ± 5.94 | 86.54 | 94.78 | 75.60 .. 94.78 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_wall_ms` | **2400.68** ± 1.01 | 2400.39 | 2402.98 | 2399.48 .. 2402.98 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_fps` | **125.11** ± 0.34 | 125.21 | 125.29 | 124.17 .. 125.29 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_avg_us` | **491.52** ± 175.80 | 436.14 | 991.83 | 433.49 .. 991.83 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_max_us` | **746.51** ± 654.27 | 546.48 | 2607.55 | 496.87 .. 2607.55 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_sys_cpu_ms` | **93.05** ± 17.42 | 86.10 | 132.25 | 79.27 .. 132.25 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_total_proc_ms` | **2592.82** ± 15.68 | 2589.25 | 2622.88 | 2573.06 .. 2622.88 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_user_cpu_ms` | **80.82** ± 21.47 | 74.95 | 138.68 | 64.44 .. 138.68 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_wall_ms` | **2397.92** ± 6.49 | 2395.89 | 2415.98 | 2394.50 .. 2415.98 | `ms` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_mean_us` | **8.62** ± 0.13 | 8.58 | 8.91 | 8.49 .. 8.91 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p50_us` | **8.45** ± 0.05 | 8.46 | 8.50 | 8.34 .. 8.50 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p99_us` | **12.76** ± 0.43 | 12.71 | 13.45 | 12.26 .. 13.45 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_sys_ms` | **3.62** ± 1.21 | 4.06 | 5.14 | 1.58 .. 5.14 | `ms` |
| `S7_Live_Internet` | `download_4stream_mbps` | **23.19** ± 0.52 | 23.15 | 23.87 | 22.43 .. 23.87 | `Mbit/s` |
| `S7_Live_Internet` | `download_loaded_ping_p50_ms` | **48.26** ± 2.47 | 49.08 | 49.25 | 41.25 .. 49.25 | `ms` |
| `S7_Live_Internet` | `download_loaded_ping_p99_ms` | **52.96** ± 2.03 | 52.70 | 55.90 | 50.00 .. 55.90 | `ms` |
| `S7_Live_Internet` | `https_tcp_connect_ms` | **30.01** ± 3.89 | 28.42 | 39.76 | 27.63 .. 39.76 | `ms` |
| `S7_Live_Internet` | `https_tls_handshake_ms` | **64.28** ± 4.97 | 62.51 | 74.92 | 59.79 .. 74.92 | `ms` |
| `S7_Live_Internet` | `https_ttfb_ms` | **111.76** ± 6.20 | 109.25 | 121.98 | 103.11 .. 121.98 | `ms` |
| `S7_Live_Internet` | `idle_ping_jitter_stdev_ms` | **1.46** ± 0.59 | 1.27 | 2.79 | 0.92 .. 2.79 | `ms` |
| `S7_Live_Internet` | `idle_ping_mean_ms` | **23.11** ± 0.52 | 22.84 | 24.15 | 22.64 .. 24.15 | `ms` |
| `S7_Live_Internet` | `idle_ping_p99_ms` | **25.86** ± 2.24 | 25.00 | 31.30 | 23.90 .. 31.30 | `ms` |
| `S7_Live_Internet` | `upload_4stream_mbps` | **7.31** ± 0.13 | 7.32 | 7.53 | 7.08 .. 7.53 | `Mbit/s` |
| `S7_Live_Internet` | `upload_loaded_ping_p50_ms` | **68.42** ± 4.79 | 68.35 | 74.80 | 60.10 .. 74.80 | `ms` |
| `S7_Live_Internet` | `upload_loaded_ping_p99_ms` | **107.74** ± 42.14 | 89.75 | 188.00 | 83.00 .. 188.00 | `ms` |
