# Project NetZero — 5-Way Bare-Metal Benchmark Comparison (Median / p50 across 10 Trials)

| Suite | Metric | CachyOS | NetZero v1 | NetZero v2 | v3-M1 (`#5`) | **v3-M2 (`#7`)** | v3-M2 vs CachyOS | v3-M2 vs v3-M1 | Unit |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `S1_Memory` | `tcp_8stream_active_pages` | 4638.50 | 3105.50 | 3703.50 | 3923.00 | **3118.00** | **+32.8%** (`1.49x`) | **+20.5%** (`1.26x`) | `pages` |
| `S1_Memory` | `udp_64quic_socks_hoarded_pages` | 240.00 | 32.00 | 28.00 | 31.00 | **32.00** | **+86.7%** (`7.50x`) | -3.2% | `pages` |
| `S1_Memory` | `unix_2000socks_8000msg_sunreclaim_kib` | 7018.00 | 1264.00 | 1642.00 | 1264.00 | **1740.00** | **+75.2%** (`4.03x`) | -37.7% | `KiB` |
| `S2_VFS_Epoll` | `cmsg_scm_rights_creds_ns` | 1278.97 | 1247.35 | 1317.25 | 1288.57 | **1228.44** | +4.0% | +4.7% | `ns/pair` |
| `S2_VFS_Epoll` | `epoll_ctl_mod_1024fd_ns` | — | — | — | — | **149.18** | — | — | `ns/op` |
| `S2_VFS_Epoll` | `epoll_wait_128_of_1024_ns_ev` | 39.79 | 18.01 | 17.37 | 17.64 | **12.62** | **+68.3%** (`3.15x`) | **+28.5%** (`1.40x`) | `ns/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_cyc_ev` | 101.20 | 51.88 | 47.33 | 48.40 | **38.82** | **+61.6%** (`2.61x`) | **+19.8%** (`1.25x`) | `cycles/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_ns_ev` | 36.10 | 18.51 | 16.88 | 17.27 | **13.85** | **+61.6%** (`2.61x`) | **+19.8%** (`1.25x`) | `ns/ev` |
| `S2_VFS_Epoll` | `eventfd_pingpong_ns` | — | — | — | 3075.61 | **3076.08** | — | -0.0% | `ns/RTT` |
| `S2_VFS_Epoll` | `iouring_unix_64B_pingpong_ns` | — | — | — | — | **4012.59** | — | — | `ns/RTT` |
| `S2_VFS_Epoll` | `iouring_unix_batch32_mops` | — | — | — | — | **12.63** | — | — | `Mops/s` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_cycles` | 2560.89 | 2653.03 | 2700.92 | 2581.97 | **2610.71** | -1.9% | -1.1% | `cycles/call` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_ns` | 914.02 | 946.46 | 963.51 | 921.08 | **931.34** | -1.9% | -1.1% | `ns/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_cycles` | 387.80 | 356.36 | 360.61 | 270.14 | **269.98** | **+30.4%** (`1.44x`) | +0.1% | `cycles/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_ns` | 138.50 | 127.13 | 128.64 | 96.37 | **96.31** | **+30.5%** (`1.44x`) | +0.1% | `ns/call` |
| `S2_VFS_Epoll` | `pipe_64B_pingpong_ns` | — | — | — | 3298.89 | **3342.09** | — | -1.3% | `ns/RTT` |
| `S2_VFS_Epoll` | `poll_2fd_pingpong_ns` | — | — | — | — | **3835.42** | — | — | `ns/RTT` |
| `S2_VFS_Epoll` | `ppoll_4fd_ready_ns` | — | — | — | — | **252.83** | — | — | `ns/call` |
| `S2_VFS_Epoll` | `shared_fd_8threads_mops` | 5.43 | 5.70 | 5.75 | 10.28 | **10.10** | **+86.1%** (`1.86x`) | -1.8% | `Mops/s` |
| `S2_VFS_Epoll` | `vfs_write_read_64B_ns` | 538.60 | 316.27 | 301.29 | 275.29 | **276.40** | **+48.7%** (`1.95x`) | -0.4% | `ns/pair` |
| `S3_AF_UNIX` | `dbus_broker_sim_2hop_krps` | — | — | — | 212.93 | **211.21** | — | -0.8% | `kRPS` |
| `S3_AF_UNIX` | `scm_rights_memfd_pair_ns` | — | — | — | 608.85 | **608.83** | — | +0.0% | `ns/pair` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_mean_ns` | 3038.22 | 723.42 | 766.11 | 853.23 | **800.61** | **+73.6%** (`3.79x`) | +6.2% | `ns/RTT` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_p99_ns` | 5490.00 | 892.00 | 3709.00 | 4378.00 | **4101.50** | **+25.3%** (`1.34x`) | +6.3% | `ns/RTT` |
| `S3_AF_UNIX` | `unix_4KB_stream_gibs` | 4.62 | 8.79 | 11.59 | 11.62 | **12.05** | **+161.0%** (`2.61x`) | +3.7% | `GiB/s` |
| `S3_AF_UNIX` | `unix_64B_pingpong_mean_ns` | 2954.56 | 684.97 | 702.91 | 744.18 | **723.54** | **+75.5%** (`4.08x`) | +2.8% | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64B_pingpong_p99_ns` | 5012.00 | 834.00 | 3198.50 | 3887.50 | **3475.00** | **+30.7%** (`1.44x`) | **+10.6%** (`1.12x`) | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64KB_stream_gibs` | 10.70 | 6.27 | 18.52 | 18.73 | **19.02** | **+77.8%** (`1.78x`) | +1.5% | `GiB/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_kcommits_s` | 341.14 | 190.62 | 433.35 | 464.07 | **456.39** | **+33.8%** (`1.34x`) | -1.7% | `kcommits/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_sys_cpu_ms` | 254.35 | 471.39 | 201.28 | 184.42 | **185.93** | **+26.9%** (`1.37x`) | -0.8% | `ms` |
| `S4_TCP_UDP` | `quic_1200B_batch_gbps` | 8.97 | 9.38 | 9.67 | 10.24 | **9.75** | +8.7% | -4.8% | `Gbps` |
| `S4_TCP_UDP` | `tcp_2000conn_handshake_us` | 17.20 | 17.40 | 17.48 | 15.53 | **15.44** | **+10.2%** (`1.11x`) | +0.6% | `us/conn` |
| `S4_TCP_UDP` | `tcp_64conn_http2_mux_krps` | 375.39 | 379.88 | 401.10 | 410.98 | **403.37** | +7.5% | -1.9% | `kRPS` |
| `S4_TCP_UDP` | `tcp_bulk_1stream_gibs` | 8.27 | 8.27 | 8.71 | 8.68 | **8.69** | +5.0% | +0.0% | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_gibs` | 5.83 | 5.51 | 5.62 | 5.92 | **5.90** | +1.3% | -0.3% | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_sys_cpu_ms` | 289.40 | 308.63 | 301.27 | 290.14 | **289.27** | +0.0% | +0.3% | `ms` |
| `S4_TCP_UDP` | `udp_unconn_4dest_mpps` | 0.83 | 0.90 | 0.93 | 0.93 | **0.93** | **+12.6%** (`1.13x`) | +0.6% | `Mpps` |
| `S5_Qdisc` | `qdisc_8flow_paced_gbps` | 9.80 | 9.24 | 9.15 | 9.15 | **9.15** | -6.6% | -0.1% | `Gbps` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p50_us` | 6.59 | 7.53 | 7.38 | 7.29 | **7.36** | -11.6% | -1.0% | `us` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p99_us` | 22.46 | 19.86 | 17.57 | 17.33 | **18.12** | **+19.3%** (`1.24x`) | -4.6% | `us` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_sys_ms` | 0.40 | 1.72 | 1.73 | 0.77 | **3.10** | -674.4% | -304.9% | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_wall_ms` | 7.81 | 7.92 | 8.10 | 7.97 | **10.65** | -36.4% | -33.7% | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_sys_ms` | 0.91 | 1.46 | 0.45 | 1.37 | **1.48** | -62.6% | -8.0% | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_wall_ms` | 1.39 | 1.66 | 1.59 | 1.53 | **1.73** | -24.3% | -12.9% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_fps` | 124.97 | 124.99 | 124.97 | 124.98 | **124.96** | -0.0% | -0.0% | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_avg_us` | 473.37 | 455.50 | 457.61 | 457.31 | **452.69** | +4.4% | +1.0% | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_max_us` | 563.85 | 538.08 | 552.82 | 543.15 | **545.34** | +3.3% | -0.4% | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_sys_cpu_ms` | 90.20 | 95.38 | 98.25 | 102.22 | **100.41** | -11.3% | +1.8% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_total_proc_ms` | 2548.53 | 2546.29 | 2549.20 | 2566.32 | **2543.85** | +0.2% | +0.9% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_user_cpu_ms` | 69.85 | 69.78 | 70.40 | 86.54 | **58.89** | **+15.7%** (`1.19x`) | **+31.9%** (`1.47x`) | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_wall_ms` | 2400.62 | 2400.26 | 2400.64 | 2400.39 | **2400.73** | -0.0% | -0.0% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_fps` | 123.60 | 124.43 | 124.92 | 125.21 | **125.32** | +1.4% | +0.1% | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_avg_us` | 549.90 | 521.25 | 483.47 | 436.14 | **435.79** | **+20.8%** (`1.26x`) | +0.1% | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_max_us` | 2230.89 | 2679.40 | 1825.76 | 546.48 | **539.24** | **+75.8%** (`4.14x`) | +1.3% | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_sys_cpu_ms` | 102.25 | 105.74 | 103.67 | 86.10 | **77.63** | **+24.1%** (`1.32x`) | +9.8% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_total_proc_ms` | 2609.26 | 2605.75 | 2604.89 | 2589.25 | **2559.61** | +1.9% | +1.1% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_user_cpu_ms` | 75.70 | 68.61 | 68.64 | 74.95 | **40.41** | **+46.6%** (`1.87x`) | **+46.1%** (`1.85x`) | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_wall_ms` | 2427.26 | 2411.07 | 2401.45 | 2395.89 | **2393.93** | +1.4% | +0.1% | `ms` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_mean_us` | 11.19 | 11.53 | 12.67 | 8.58 | **8.90** | **+20.5%** (`1.26x`) | -3.7% | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p50_us` | 10.54 | 10.45 | 12.32 | 8.46 | **8.56** | **+18.8%** (`1.23x`) | -1.2% | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p99_us` | 20.07 | 17.37 | 16.69 | 12.71 | **13.45** | **+33.0%** (`1.49x`) | -5.8% | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_sys_ms` | 4.60 | 4.47 | 3.79 | 4.06 | **2.82** | **+38.7%** (`1.63x`) | **+30.5%** (`1.44x`) | `ms` |
| `S7_Live_Internet` | `download_4stream_mbps` | 30.36 | 22.39 | 31.03 | 23.15 | **22.94** | -24.4% | -0.9% | `Mbit/s` |
| `S7_Live_Internet` | `download_loaded_ping_p50_ms` | 88.00 | 94.78 | 98.08 | 49.08 | **48.80** | **+44.5%** (`1.80x`) | +0.6% | `ms` |
| `S7_Live_Internet` | `download_loaded_ping_p99_ms` | 217.00 | 311.00 | 282.50 | 52.70 | **56.70** | **+73.9%** (`3.83x`) | -7.6% | `ms` |
| `S7_Live_Internet` | `https_tcp_connect_ms` | 64.78 | 64.71 | 66.64 | 28.42 | **29.80** | **+54.0%** (`2.17x`) | -4.9% | `ms` |
| `S7_Live_Internet` | `https_tls_handshake_ms` | 127.07 | 128.11 | 135.72 | 62.51 | **64.51** | **+49.2%** (`1.97x`) | -3.2% | `ms` |
| `S7_Live_Internet` | `https_ttfb_ms` | 214.95 | 210.53 | 234.32 | 109.25 | **110.38** | **+48.7%** (`1.95x`) | -1.0% | `ms` |
| `S7_Live_Internet` | `idle_ping_jitter_stdev_ms` | 0.29 | 0.34 | 0.42 | 1.27 | **0.96** | -230.8% | **+24.6%** (`1.33x`) | `ms` |
| `S7_Live_Internet` | `idle_ping_mean_ms` | 60.82 | 60.65 | 60.77 | 22.84 | **22.84** | **+62.4%** (`2.66x`) | +0.0% | `ms` |
| `S7_Live_Internet` | `idle_ping_p99_ms` | 61.35 | 61.10 | 61.30 | 25.00 | **24.20** | **+60.6%** (`2.54x`) | +3.2% | `ms` |
| `S7_Live_Internet` | `upload_4stream_mbps` | 19.94 | 20.10 | 19.08 | 7.32 | **7.07** | -64.6% | -3.5% | `Mbit/s` |
| `S7_Live_Internet` | `upload_loaded_ping_p50_ms` | 60.65 | 60.23 | 60.40 | 68.35 | **66.70** | -10.0% | +2.4% | `ms` |
| `S7_Live_Internet` | `upload_loaded_ping_p99_ms` | 63.50 | 62.80 | 63.40 | 89.75 | **84.55** | -33.1% | +5.8% | `ms` |
