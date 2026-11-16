# Grand Benchmark Summary — `7.4.0-rc2-netzero-v2+`

- **Timestamp:** `2026-11-15T22:04:58.477885`
- **Kernel:** `Linux arch-cetin 7.4.0-rc2-netzero-v2+ #2 SMP PREEMPT_DYNAMIC Sun Nov 15 02:13:11 +03 2026 x86_64 GNU/Linux`
- **Default Qdisc / CC / ECN:** `eiffel_dualq` / `bbr` / `tcp_ecn=2`
- **Governor / EPP:** `powersave` / `balance_performance`
- **Protocol Struct Sizes (`/proc/net/protocols`):** `TCP=2176 B`, `TCPv6=2304 B`, `UDP=1344 B`, `UNIX-STREAM=1088 B`

| Suite | Metric | Mean ± StdDev | Median (p50) | p99 | Min .. Max | Unit |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `S1_Memory` | `tcp_8stream_active_pages` | **3454.60** ± 1020.05 | 3703.50 | 5099.00 | 2152.00 .. 5099.00 | `pages` |
| `S1_Memory` | `udp_64quic_socks_hoarded_pages` | **25.40** ± 8.93 | 28.00 | 29.00 | 0.00 .. 29.00 | `pages` |
| `S1_Memory` | `unix_2000socks_8000msg_sunreclaim_kib` | **1612.00** ± 185.87 | 1642.00 | 1872.00 | 1240.00 .. 1872.00 | `KiB` |
| `S2_VFS_Epoll` | `cmsg_scm_rights_creds_ns` | **1331.04** ± 53.89 | 1317.25 | 1474.14 | 1294.60 .. 1474.14 | `ns/pair` |
| `S2_VFS_Epoll` | `epoll_wait_128_of_1024_ns_ev` | **17.36** ± 0.21 | 17.37 | 17.65 | 17.00 .. 17.65 | `ns/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_cyc_ev` | **49.86** ± 6.42 | 47.33 | 67.31 | 46.55 .. 67.31 | `cycles/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_ns_ev` | **17.79** ± 2.29 | 16.88 | 24.01 | 16.61 .. 24.01 | `ns/ev` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_cycles` | **2696.05** ± 66.40 | 2700.92 | 2816.48 | 2580.90 .. 2816.48 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_ns` | **961.77** ± 23.69 | 963.51 | 1004.73 | 920.70 .. 1004.73 | `ns/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_cycles` | **369.37** ± 18.68 | 360.61 | 412.25 | 355.13 .. 412.25 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_ns` | **131.77** ± 6.66 | 128.64 | 147.07 | 126.69 .. 147.07 | `ns/call` |
| `S2_VFS_Epoll` | `shared_fd_8threads_mops` | **5.74** ± 0.34 | 5.75 | 6.22 | 5.06 .. 6.22 | `Mops/s` |
| `S2_VFS_Epoll` | `vfs_write_read_64B_ns` | **309.81** ± 23.59 | 301.29 | 371.02 | 294.70 .. 371.02 | `ns/pair` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_mean_ns` | **769.57** ± 22.56 | 766.11 | 809.41 | 732.34 .. 809.41 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_p99_ns` | **3732.60** ± 278.00 | 3709.00 | 4173.00 | 3320.00 .. 4173.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_4KB_stream_gibs` | **10.90** ± 1.43 | 11.59 | 11.85 | 8.14 .. 11.85 | `GiB/s` |
| `S3_AF_UNIX` | `unix_64B_pingpong_mean_ns` | **707.70** ± 42.39 | 702.91 | 821.42 | 675.38 .. 821.42 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64B_pingpong_p99_ns` | **2694.90** ± 976.45 | 3198.50 | 3659.00 | 913.00 .. 3659.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64KB_stream_gibs` | **18.50** ± 0.19 | 18.52 | 18.71 | 18.12 .. 18.71 | `GiB/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_kcommits_s` | **421.36** ± 23.41 | 433.35 | 438.93 | 371.02 .. 438.93 | `kcommits/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_sys_cpu_ms` | **205.10** ± 9.76 | 201.28 | 230.05 | 198.91 .. 230.05 | `ms` |
| `S4_TCP_UDP` | `quic_1200B_batch_gbps` | **9.61** ± 0.36 | 9.67 | 10.18 | 9.02 .. 10.18 | `Gbps` |
| `S4_TCP_UDP` | `tcp_2000conn_handshake_us` | **18.12** ± 2.03 | 17.48 | 23.85 | 17.15 .. 23.85 | `us/conn` |
| `S4_TCP_UDP` | `tcp_64conn_http2_mux_krps` | **406.76** ± 21.84 | 401.10 | 434.88 | 363.20 .. 434.88 | `kRPS` |
| `S4_TCP_UDP` | `tcp_bulk_1stream_gibs` | **8.49** ± 0.85 | 8.71 | 9.34 | 7.05 .. 9.34 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_gibs` | **5.74** ± 0.33 | 5.62 | 6.26 | 5.35 .. 6.26 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_sys_cpu_ms` | **294.76** ± 13.89 | 301.27 | 307.31 | 269.95 .. 307.31 | `ms` |
| `S4_TCP_UDP` | `udp_unconn_4dest_mpps` | **0.93** ± 0.02 | 0.93 | 0.95 | 0.91 .. 0.95 | `Mpps` |
| `S5_Qdisc` | `qdisc_8flow_paced_gbps` | **9.16** ± 0.14 | 9.15 | 9.40 | 8.99 .. 9.40 | `Gbps` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p50_us` | **7.50** ± 0.28 | 7.38 | 8.09 | 7.31 .. 8.09 | `us` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p99_us` | **22.27** ± 14.37 | 17.57 | 62.42 | 14.01 .. 62.42 | `us` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_sys_ms` | **2.16** ± 1.37 | 1.73 | 4.54 | 0.00 .. 4.54 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_wall_ms` | **8.91** ± 1.43 | 8.10 | 11.02 | 7.81 .. 11.02 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_sys_ms` | **0.66** ± 0.71 | 0.45 | 1.48 | 0.00 .. 1.48 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_wall_ms` | **1.77** ± 0.56 | 1.59 | 3.30 | 1.41 .. 3.30 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_fps` | **124.95** ± 0.08 | 124.97 | 125.06 | 124.79 .. 125.06 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_avg_us` | **461.47** ± 10.06 | 457.61 | 481.85 | 452.40 .. 481.85 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_max_us` | **559.93** ± 23.29 | 552.82 | 605.78 | 538.23 .. 605.78 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_sys_cpu_ms` | **98.19** ± 6.55 | 98.25 | 112.90 | 87.19 .. 112.90 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_total_proc_ms` | **2551.34** ± 7.41 | 2549.20 | 2569.90 | 2542.96 .. 2569.90 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_user_cpu_ms` | **71.57** ± 5.52 | 70.40 | 84.23 | 65.28 .. 84.23 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_wall_ms` | **2400.93** ± 1.54 | 2400.64 | 2404.02 | 2398.80 .. 2404.02 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_fps` | **123.36** ± 3.01 | 124.92 | 125.27 | 116.77 .. 125.27 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_avg_us` | **541.13** ± 156.02 | 483.47 | 944.61 | 439.19 .. 944.61 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_max_us` | **1558.63** ± 955.66 | 1825.76 | 2959.06 | 515.73 .. 2959.06 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_sys_cpu_ms` | **105.05** ± 16.38 | 103.67 | 132.29 | 78.64 .. 132.29 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_total_proc_ms` | **2625.38** ± 60.96 | 2604.89 | 2758.47 | 2554.09 .. 2758.47 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_user_cpu_ms` | **76.15** ± 23.34 | 68.64 | 131.41 | 55.06 .. 131.41 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_wall_ms` | **2433.20** ± 61.32 | 2401.45 | 2569.18 | 2394.80 .. 2569.18 | `ms` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_mean_us` | **12.45** ± 0.45 | 12.67 | 12.72 | 11.30 .. 12.72 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p50_us` | **12.08** ± 0.76 | 12.32 | 12.39 | 9.93 .. 12.39 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p99_us` | **16.90** ± 0.74 | 16.69 | 18.14 | 16.07 .. 18.14 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_sys_ms` | **4.31** ± 1.85 | 3.79 | 7.07 | 1.55 .. 7.07 | `ms` |
| `S7_Live_Internet` | `download_4stream_mbps` | **32.09** ± 2.53 | 31.03 | 35.44 | 28.55 .. 35.44 | `Mbit/s` |
| `S7_Live_Internet` | `download_loaded_ping_p50_ms` | **96.92** ± 10.27 | 98.08 | 116.00 | 80.00 .. 116.00 | `ms` |
| `S7_Live_Internet` | `download_loaded_ping_p99_ms` | **302.10** ± 59.88 | 282.50 | 394.00 | 226.00 .. 394.00 | `ms` |
| `S7_Live_Internet` | `https_tcp_connect_ms` | **65.84** ± 4.61 | 66.64 | 72.93 | 58.70 .. 72.93 | `ms` |
| `S7_Live_Internet` | `https_tls_handshake_ms` | **134.79** ± 10.65 | 135.72 | 146.32 | 115.74 .. 146.32 | `ms` |
| `S7_Live_Internet` | `https_ttfb_ms` | **249.89** ± 65.90 | 234.32 | 427.79 | 197.25 .. 427.79 | `ms` |
| `S7_Live_Internet` | `idle_ping_jitter_stdev_ms` | **0.52** ± 0.22 | 0.42 | 0.93 | 0.30 .. 0.93 | `ms` |
| `S7_Live_Internet` | `idle_ping_mean_ms` | **60.79** ± 0.19 | 60.77 | 61.13 | 60.40 .. 61.13 | `ms` |
| `S7_Live_Internet` | `idle_ping_p99_ms` | **61.79** ± 0.96 | 61.30 | 63.30 | 61.00 .. 63.30 | `ms` |
| `S7_Live_Internet` | `upload_4stream_mbps` | **18.14** ± 2.25 | 19.08 | 19.74 | 12.83 .. 19.74 | `Mbit/s` |
| `S7_Live_Internet` | `upload_loaded_ping_p50_ms` | **60.37** ± 0.15 | 60.40 | 60.60 | 60.10 .. 60.60 | `ms` |
| `S7_Live_Internet` | `upload_loaded_ping_p99_ms` | **63.51** ± 0.91 | 63.40 | 64.70 | 62.20 .. 64.70 | `ms` |
