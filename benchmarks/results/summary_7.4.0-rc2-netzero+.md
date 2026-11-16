# Grand Benchmark Summary — `7.4.0-rc2-netzero+`

- **Timestamp:** `2026-11-15T00:57:28.644187`
- **Kernel:** `Linux arch-cetin 7.4.0-rc2-netzero+ #1 SMP PREEMPT_DYNAMIC Sun Nov 15 00:21:34 +03 2026 x86_64 GNU/Linux`
- **Default Qdisc / CC / ECN:** `eiffel_dualq` / `bbr` / `tcp_ecn=3`
- **Governor / EPP:** `powersave` / `balance_performance`
- **Protocol Struct Sizes (`/proc/net/protocols`):** `TCP=2176 B`, `TCPv6=2304 B`, `UDP=1408 B`, `UNIX-STREAM=1088 B`

| Suite | Metric | Mean ± StdDev | Median (p50) | p99 | Min .. Max | Unit |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `S1_Memory` | `tcp_8stream_active_pages` | **3302.70** ± 1257.43 | 3105.50 | 5586.00 | 1982.00 .. 5586.00 | `pages` |
| `S1_Memory` | `udp_64quic_socks_hoarded_pages` | **32.00** ± 0.00 | 32.00 | 32.00 | 32.00 .. 32.00 | `pages` |
| `S1_Memory` | `unix_2000socks_8000msg_sunreclaim_kib` | **1238.80** ± 84.16 | 1264.00 | 1280.00 | 1000.00 .. 1280.00 | `KiB` |
| `S2_VFS_Epoll` | `cmsg_scm_rights_creds_ns` | **1272.11** ± 68.52 | 1247.35 | 1403.19 | 1219.81 .. 1403.19 | `ns/pair` |
| `S2_VFS_Epoll` | `epoll_wait_128_of_1024_ns_ev` | **18.08** ± 0.20 | 18.01 | 18.41 | 17.79 .. 18.41 | `ns/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_cyc_ev` | **53.72** ± 5.17 | 51.88 | 67.69 | 50.58 .. 67.69 | `cycles/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_ns_ev` | **19.16** ± 1.84 | 18.51 | 24.15 | 18.04 .. 24.15 | `ns/ev` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_cycles` | **2638.84** ± 67.53 | 2653.03 | 2713.63 | 2530.99 .. 2713.63 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_ns` | **941.40** ± 24.09 | 946.46 | 968.08 | 902.92 .. 968.08 | `ns/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_cycles` | **357.72** ± 11.88 | 356.36 | 381.19 | 344.25 .. 381.19 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_ns` | **127.62** ± 4.24 | 127.13 | 135.99 | 122.81 .. 135.99 | `ns/call` |
| `S2_VFS_Epoll` | `shared_fd_8threads_mops` | **5.64** ± 0.29 | 5.70 | 6.08 | 5.08 .. 6.08 | `Mops/s` |
| `S2_VFS_Epoll` | `vfs_write_read_64B_ns` | **314.47** ± 11.01 | 316.27 | 329.21 | 297.66 .. 329.21 | `ns/pair` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_mean_ns` | **734.63** ± 39.40 | 723.42 | 846.33 | 715.98 .. 846.33 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_p99_ns` | **890.30** ± 21.54 | 892.00 | 937.00 | 866.00 .. 937.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_4KB_stream_gibs` | **8.57** ± 0.56 | 8.79 | 8.94 | 7.11 .. 8.94 | `GiB/s` |
| `S3_AF_UNIX` | `unix_64B_pingpong_mean_ns` | **696.38** ± 31.35 | 684.97 | 781.61 | 680.26 .. 781.61 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64B_pingpong_p99_ns` | **842.90** ± 20.53 | 834.00 | 893.00 | 827.00 .. 893.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64KB_stream_gibs` | **6.24** ± 0.16 | 6.27 | 6.36 | 5.81 .. 6.36 | `GiB/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_kcommits_s` | **190.67** ± 2.50 | 190.62 | 194.69 | 187.77 .. 194.69 | `kcommits/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_sys_cpu_ms` | **471.29** ± 3.62 | 471.39 | 477.95 | 465.28 .. 477.95 | `ms` |
| `S4_TCP_UDP` | `quic_1200B_batch_gbps` | **9.29** ± 0.24 | 9.38 | 9.57 | 8.87 .. 9.57 | `Gbps` |
| `S4_TCP_UDP` | `tcp_2000conn_handshake_us` | **18.32** ± 2.01 | 17.40 | 23.79 | 17.25 .. 23.79 | `us/conn` |
| `S4_TCP_UDP` | `tcp_64conn_http2_mux_krps` | **374.05** ± 15.17 | 379.88 | 386.64 | 335.99 .. 386.64 | `kRPS` |
| `S4_TCP_UDP` | `tcp_bulk_1stream_gibs` | **8.09** ± 0.51 | 8.27 | 8.55 | 7.03 .. 8.55 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_gibs` | **5.49** ± 0.20 | 5.51 | 5.76 | 5.05 .. 5.76 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_sys_cpu_ms` | **309.23** ± 8.71 | 308.63 | 327.19 | 297.18 .. 327.19 | `ms` |
| `S4_TCP_UDP` | `udp_unconn_4dest_mpps` | **0.89** ± 0.04 | 0.90 | 0.93 | 0.80 .. 0.93 | `Mpps` |
| `S5_Qdisc` | `qdisc_8flow_paced_gbps` | **9.22** ± 0.10 | 9.24 | 9.37 | 9.02 .. 9.37 | `Gbps` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p50_us` | **7.53** ± 0.25 | 7.53 | 7.89 | 7.23 .. 7.89 | `us` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p99_us` | **19.98** ± 3.72 | 19.86 | 28.09 | 13.82 .. 28.09 | `us` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_sys_ms` | **1.56** ± 0.36 | 1.72 | 1.82 | 0.84 .. 1.82 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_wall_ms` | **8.02** ± 0.35 | 7.92 | 8.99 | 7.78 .. 8.99 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_sys_ms` | **1.26** ± 0.56 | 1.46 | 1.74 | 0.00 .. 1.74 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_wall_ms` | **1.85** ± 0.62 | 1.66 | 3.59 | 1.48 .. 3.59 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_fps` | **124.93** ± 0.14 | 124.99 | 125.03 | 124.57 .. 125.03 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_avg_us` | **457.87** ± 7.56 | 455.50 | 475.35 | 451.41 .. 475.35 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_max_us` | **544.25** ± 20.66 | 538.08 | 579.22 | 518.64 .. 579.22 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_sys_cpu_ms` | **96.60** ± 4.53 | 95.38 | 103.93 | 90.85 .. 103.93 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_total_proc_ms` | **2547.08** ± 8.97 | 2546.29 | 2565.49 | 2535.51 .. 2565.49 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_user_cpu_ms` | **70.87** ± 4.04 | 69.78 | 78.65 | 65.78 .. 78.65 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_wall_ms` | **2401.26** ± 2.70 | 2400.26 | 2408.31 | 2399.41 .. 2408.31 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_fps` | **123.95** ± 1.66 | 124.43 | 125.20 | 119.73 .. 125.20 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_avg_us` | **521.99** ± 74.90 | 521.25 | 632.73 | 435.19 .. 632.73 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_max_us` | **2387.52** ± 1500.49 | 2679.40 | 4637.50 | 510.94 .. 4637.50 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_sys_cpu_ms` | **113.09** ± 32.56 | 105.74 | 201.65 | 84.17 .. 201.65 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_total_proc_ms` | **2623.86** ± 47.32 | 2605.75 | 2717.29 | 2579.54 .. 2717.29 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_user_cpu_ms` | **71.56** ± 12.46 | 68.61 | 96.74 | 55.51 .. 96.74 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_wall_ms` | **2420.70** ± 33.21 | 2411.07 | 2505.69 | 2396.11 .. 2505.69 | `ms` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_mean_us` | **11.94** ± 0.99 | 11.53 | 13.61 | 10.94 .. 13.61 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p50_us` | **11.22** ± 1.24 | 10.45 | 13.17 | 10.38 .. 13.17 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p99_us` | **17.81** ± 1.56 | 17.37 | 20.75 | 16.08 .. 20.75 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_sys_ms` | **4.30** ± 2.20 | 4.47 | 7.70 | 0.00 .. 7.70 | `ms` |
| `S7_Live_Internet` | `download_4stream_mbps` | **23.23** ± 3.78 | 22.39 | 29.42 | 18.23 .. 29.42 | `Mbit/s` |
| `S7_Live_Internet` | `download_loaded_ping_p50_ms` | **97.13** ± 9.43 | 94.78 | 117.00 | 87.10 .. 117.00 | `ms` |
| `S7_Live_Internet` | `download_loaded_ping_p99_ms` | **311.60** ± 65.71 | 311.00 | 424.00 | 208.00 .. 424.00 | `ms` |
| `S7_Live_Internet` | `https_tcp_connect_ms` | **62.70** ± 4.64 | 64.71 | 66.77 | 54.55 .. 66.77 | `ms` |
| `S7_Live_Internet` | `https_tls_handshake_ms` | **124.57** ± 9.04 | 128.11 | 133.82 | 110.80 .. 133.82 | `ms` |
| `S7_Live_Internet` | `https_ttfb_ms` | **218.17** ± 32.28 | 210.53 | 279.24 | 185.85 .. 279.24 | `ms` |
| `S7_Live_Internet` | `idle_ping_jitter_stdev_ms` | **0.35** ± 0.14 | 0.34 | 0.59 | 0.13 .. 0.59 | `ms` |
| `S7_Live_Internet` | `idle_ping_mean_ms` | **60.62** ± 0.12 | 60.65 | 60.77 | 60.40 .. 60.77 | `ms` |
| `S7_Live_Internet` | `idle_ping_p99_ms` | **61.13** ± 0.20 | 61.10 | 61.60 | 60.90 .. 61.60 | `ms` |
| `S7_Live_Internet` | `upload_4stream_mbps` | **20.00** ± 0.74 | 20.10 | 20.89 | 18.64 .. 20.89 | `Mbit/s` |
| `S7_Live_Internet` | `upload_loaded_ping_p50_ms` | **60.30** ± 0.37 | 60.23 | 61.20 | 59.90 .. 61.20 | `ms` |
| `S7_Live_Internet` | `upload_loaded_ping_p99_ms` | **77.27** ± 31.60 | 62.80 | 156.00 | 61.10 .. 156.00 | `ms` |
