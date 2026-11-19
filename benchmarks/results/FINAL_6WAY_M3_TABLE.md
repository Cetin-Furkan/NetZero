# Project NetZero — 6-Way Bare-Metal Benchmark Comparison (Median / p50 across 10 Trials)

| Suite | Metric | CachyOS | v1 | v2 | v3-M1 | v3-M2 | **v3-M3 (`#8`)** | v3-M3 vs CachyOS | v3-M3 vs v3-M2 | Unit |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `S1_Memory` | `tcp_8stream_active_pages` | 4638.50 | 3105.50 | 3703.50 | 3923.00 | 3118.00 | **4654.00** | -0.3% | -49.3% | `pages` |
| `S1_Memory` | `udp_64quic_socks_hoarded_pages` | 240.00 | 32.00 | 28.00 | 31.00 | 32.00 | **31.00** | **+87.1%** (`7.74x`) | +3.1% | `pages` |
| `S1_Memory` | `unix_2000socks_8000msg_sunreclaim_kib` | 7018.00 | 1264.00 | 1642.00 | 1264.00 | 1740.00 | **1526.00** | **+78.3%** (`4.60x`) | **+12.3%** (`1.14x`) | `KiB` |
| `S2_VFS_Epoll` | `bore_interactive_8hog_p50_us` | — | — | — | — | — | **2.10** | — | — | `us` |
| `S2_VFS_Epoll` | `bore_interactive_8hog_p99_us` | — | — | — | — | — | **6.11** | — | — | `us` |
| `S2_VFS_Epoll` | `cmsg_scm_rights_creds_ns` | 1278.97 | 1247.35 | 1317.25 | 1288.57 | 1228.44 | **1267.20** | +0.9% | -3.2% | `ns/pair` |
| `S2_VFS_Epoll` | `epoll_ctl_mod_1024fd_ns` | — | — | — | — | 149.18 | **156.60** | — | -5.0% | `ns/op` |
| `S2_VFS_Epoll` | `epoll_wait_128_of_1024_ns_ev` | 39.79 | 18.01 | 17.37 | 17.64 | 12.62 | **11.79** | **+70.4%** (`3.37x`) | +6.5% | `ns/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_cyc_ev` | 101.20 | 51.88 | 47.32 | 48.40 | 38.82 | **37.88** | **+62.6%** (`2.67x`) | +2.4% | `cycles/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_ns_ev` | 36.10 | 18.51 | 16.88 | 17.27 | 13.85 | **13.58** | **+62.4%** (`2.66x`) | +2.0% | `ns/ev` |
| `S2_VFS_Epoll` | `eventfd_pingpong_ns` | — | — | — | 3075.61 | 3076.08 | **3146.71** | — | -2.3% | `ns/RTT` |
| `S2_VFS_Epoll` | `futex_8thread_contended_mops` | — | — | — | — | — | **63.82** | — | — | `Mops/s` |
| `S2_VFS_Epoll` | `futex_private_wake_ns` | — | — | — | — | — | **93.50** | — | — | `ns/op` |
| `S2_VFS_Epoll` | `iouring_unix_64B_pingpong_ns` | — | — | — | — | 4012.59 | **3930.94** | — | +2.0% | `ns/RTT` |
| `S2_VFS_Epoll` | `iouring_unix_batch32_mops` | — | — | — | — | 12.63 | **13.09** | — | +3.6% | `Mops/s` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_cycles` | 2560.89 | 2653.03 | 2700.92 | 2581.97 | 2610.71 | **2606.86** | -1.8% | +0.1% | `cycles/call` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_ns` | 914.02 | 946.46 | 963.51 | 921.08 | 931.34 | **932.34** | -2.0% | -0.1% | `ns/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_cycles` | 387.80 | 356.36 | 360.61 | 270.14 | 269.98 | **269.97** | **+30.4%** (`1.44x`) | +0.0% | `cycles/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_ns` | 138.50 | 127.13 | 128.64 | 96.37 | 96.31 | **96.31** | **+30.5%** (`1.44x`) | +0.0% | `ns/call` |
| `S2_VFS_Epoll` | `pipe_64B_pingpong_ns` | — | — | — | 3298.89 | 3342.09 | **3339.33** | — | +0.1% | `ns/RTT` |
| `S2_VFS_Epoll` | `poll_2fd_pingpong_ns` | — | — | — | — | 3835.42 | **3732.14** | — | +2.7% | `ns/RTT` |
| `S2_VFS_Epoll` | `ppoll_4fd_ready_ns` | — | — | — | — | 252.83 | **242.45** | — | +4.1% | `ns/call` |
| `S2_VFS_Epoll` | `shared_fd_8threads_mops` | 5.43 | 5.70 | 5.75 | 10.28 | 10.10 | **10.07** | **+85.6%** (`1.86x`) | -0.3% | `Mops/s` |
| `S2_VFS_Epoll` | `vfs_write_read_64B_ns` | 538.60 | 316.27 | 301.29 | 275.29 | 276.40 | **282.33** | **+47.6%** (`1.91x`) | -2.1% | `ns/pair` |
| `S2_VFS_Epoll` | `wfsync_eventfd_unpinned_ns` | — | — | — | — | — | **1847.09** | — | — | `ns/RTT` |
| `S2_VFS_Epoll` | `wfsync_pipe64B_unpinned_ns` | — | — | — | — | — | **2002.98** | — | — | `ns/RTT` |
| `S3_AF_UNIX` | `dbus_broker_sim_2hop_krps` | — | — | — | 212.93 | 211.21 | **200.75** | — | -5.0% | `kRPS` |
| `S3_AF_UNIX` | `scm_rights_memfd_pair_ns` | — | — | — | 608.85 | 608.83 | **610.32** | — | -0.2% | `ns/pair` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_mean_ns` | 3038.22 | 723.42 | 766.11 | 853.23 | 800.61 | **857.27** | **+71.8%** (`3.54x`) | -7.1% | `ns/RTT` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_p99_ns` | 5490.00 | 892.00 | 3709.00 | 4378.00 | 4101.50 | **4399.50** | **+19.9%** (`1.25x`) | -7.3% | `ns/RTT` |
| `S3_AF_UNIX` | `unix_4KB_stream_gibs` | 4.62 | 8.79 | 11.59 | 11.62 | 12.05 | **12.08** | **+161.7%** (`2.62x`) | +0.3% | `GiB/s` |
| `S3_AF_UNIX` | `unix_64B_pingpong_mean_ns` | 2954.56 | 684.97 | 702.91 | 744.18 | 723.54 | **772.34** | **+73.9%** (`3.83x`) | -6.7% | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64B_pingpong_p99_ns` | 5012.00 | 834.00 | 3198.50 | 3887.50 | 3475.00 | **3895.50** | **+22.3%** (`1.29x`) | -12.1% | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64KB_stream_gibs` | 10.70 | 6.27 | 18.52 | 18.73 | 19.02 | **18.90** | **+76.7%** (`1.77x`) | -0.6% | `GiB/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_kcommits_s` | 341.14 | 190.62 | 433.35 | 464.07 | 456.39 | **480.59** | **+40.9%** (`1.41x`) | +5.3% | `kcommits/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_sys_cpu_ms` | 254.35 | 471.39 | 201.27 | 184.42 | 185.93 | **178.53** | **+29.8%** (`1.42x`) | +4.0% | `ms` |
| `S4_TCP_UDP` | `quic_1200B_batch_gbps` | 8.97 | 9.38 | 9.67 | 10.24 | 9.75 | **10.33** | **+15.2%** (`1.15x`) | +5.9% | `Gbps` |
| `S4_TCP_UDP` | `tcp_2000conn_handshake_us` | 17.20 | 17.40 | 17.48 | 15.53 | 15.44 | **15.73** | +8.5% | -1.9% | `us/conn` |
| `S4_TCP_UDP` | `tcp_64conn_http2_mux_krps` | 375.39 | 379.88 | 401.10 | 410.98 | 403.37 | **421.01** | **+12.2%** (`1.12x`) | +4.4% | `kRPS` |
| `S4_TCP_UDP` | `tcp_bulk_1stream_gibs` | 8.27 | 8.27 | 8.71 | 8.68 | 8.69 | **8.65** | +4.5% | -0.5% | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_gibs` | 5.83 | 5.51 | 5.62 | 5.92 | 5.90 | **5.57** | -4.4% | -5.6% | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_sys_cpu_ms` | 289.40 | 308.63 | 301.27 | 290.14 | 289.27 | **305.83** | -5.7% | -5.7% | `ms` |
| `S4_TCP_UDP` | `udp_unconn_4dest_mpps` | 0.83 | 0.90 | 0.93 | 0.93 | 0.93 | **0.95** | **+14.2%** (`1.14x`) | +1.4% | `Mpps` |
| `S5_Qdisc` | `qdisc_8flow_paced_gbps` | 9.80 | 9.24 | 9.15 | 9.15 | 9.15 | **9.18** | -6.3% | +0.3% | `Gbps` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p50_us` | 6.59 | 7.53 | 7.38 | 7.29 | 7.36 | **7.20** | -9.1% | +2.2% | `us` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p99_us` | 22.46 | 19.86 | 17.57 | 17.33 | 18.12 | **17.63** | **+21.5%** (`1.27x`) | +2.7% | `us` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_sys_ms` | 0.40 | 1.72 | 1.73 | 0.77 | 3.10 | **2.30** | -473.3% | **+26.0%** (`1.35x`) | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_wall_ms` | 7.81 | 7.92 | 8.10 | 7.97 | 10.65 | **9.50** | -21.6% | **+10.9%** (`1.12x`) | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_sys_ms` | 0.91 | 1.46 | 0.45 | 1.37 | 1.48 | **1.06** | -15.7% | **+28.8%** (`1.40x`) | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_wall_ms` | 1.39 | 1.66 | 1.59 | 1.53 | 1.73 | **1.54** | -10.6% | **+11.0%** (`1.12x`) | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_fps` | 124.97 | 124.99 | 124.97 | 124.98 | 124.96 | **124.70** | -0.2% | -0.2% | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_avg_us` | 473.37 | 455.50 | 457.61 | 457.30 | 452.69 | **453.31** | +4.2% | -0.1% | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_max_us` | 563.85 | 538.08 | 552.82 | 543.15 | 545.34 | **541.57** | +4.0% | +0.7% | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_sys_cpu_ms` | 90.20 | 95.38 | 98.25 | 102.22 | 100.41 | **93.14** | -3.3% | +7.2% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_total_proc_ms` | 2548.53 | 2546.29 | 2549.20 | 2566.32 | 2543.85 | **2552.06** | -0.1% | -0.3% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_user_cpu_ms` | 69.85 | 69.78 | 70.40 | 86.54 | 58.89 | **59.63** | **+14.6%** (`1.17x`) | -1.2% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_wall_ms` | 2400.62 | 2400.26 | 2400.64 | 2400.39 | 2400.73 | **2405.81** | -0.2% | -0.2% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_fps` | 123.60 | 124.43 | 124.93 | 125.21 | 125.32 | **125.34** | +1.4% | +0.0% | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_avg_us` | 549.90 | 521.25 | 483.47 | 436.14 | 435.78 | **435.83** | **+20.7%** (`1.26x`) | -0.0% | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_max_us` | 2230.89 | 2679.40 | 1825.76 | 546.48 | 539.24 | **530.97** | **+76.2%** (`4.20x`) | +1.5% | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_sys_cpu_ms` | 102.25 | 105.74 | 103.67 | 86.10 | 77.63 | **83.08** | **+18.8%** (`1.23x`) | -7.0% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_total_proc_ms` | 2609.26 | 2605.75 | 2604.89 | 2589.25 | 2559.61 | **2566.51** | +1.6% | -0.3% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_user_cpu_ms` | 75.70 | 68.61 | 68.64 | 74.95 | 40.41 | **41.61** | **+45.0%** (`1.82x`) | -3.0% | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_wall_ms` | 2427.26 | 2411.07 | 2401.45 | 2395.89 | 2393.93 | **2393.40** | +1.4% | +0.0% | `ms` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_mean_us` | 11.19 | 11.53 | 12.67 | 8.58 | 8.90 | **10.14** | +9.4% | -13.9% | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p50_us` | 10.54 | 10.45 | 12.32 | 8.46 | 8.56 | **8.58** | **+18.6%** (`1.23x`) | -0.2% | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p99_us` | 20.07 | 17.37 | 16.69 | 12.71 | 13.45 | **17.86** | **+11.0%** (`1.12x`) | -32.8% | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_sys_ms` | 4.60 | 4.47 | 3.79 | 4.06 | 2.82 | **3.80** | **+17.4%** (`1.21x`) | -34.6% | `ms` |
| `S7_Live_Internet` | `download_4stream_mbps` | 30.36 | 22.39 | 31.03 | 23.15 | 22.94 | **6.95** | -77.1% | -69.7% | `Mbit/s` |
| `S7_Live_Internet` | `download_loaded_ping_p50_ms` | 88.00 | 94.78 | 98.08 | 49.08 | 48.80 | **220.50** | -150.6% | -351.8% | `ms` |
| `S7_Live_Internet` | `download_loaded_ping_p99_ms` | 217.00 | 311.00 | 282.50 | 52.70 | 56.70 | **536.00** | -147.0% | -845.3% | `ms` |
| `S7_Live_Internet` | `https_tcp_connect_ms` | 64.78 | 64.71 | 66.64 | 28.42 | 29.80 | **36.77** | **+43.2%** (`1.76x`) | -23.4% | `ms` |
| `S7_Live_Internet` | `https_tls_handshake_ms` | 127.07 | 128.11 | 135.71 | 62.51 | 64.51 | **127.58** | -0.4% | -97.8% | `ms` |
| `S7_Live_Internet` | `https_ttfb_ms` | 214.95 | 210.53 | 234.32 | 109.25 | 110.38 | **224.69** | -4.5% | -103.6% | `ms` |
| `S7_Live_Internet` | `idle_ping_jitter_stdev_ms` | 0.29 | 0.34 | 0.42 | 1.27 | 0.96 | **13.50** | -4554.5% | -1306.9% | `ms` |
| `S7_Live_Internet` | `idle_ping_mean_ms` | 60.82 | 60.64 | 60.77 | 22.84 | 22.84 | **44.65** | **+26.6%** (`1.36x`) | -95.5% | `ms` |
| `S7_Live_Internet` | `idle_ping_p99_ms` | 61.35 | 61.10 | 61.30 | 25.00 | 24.20 | **68.10** | -11.0% | -181.4% | `ms` |
| `S7_Live_Internet` | `upload_4stream_mbps` | 19.94 | 20.10 | 19.08 | 7.32 | 7.07 | **1.61** | -91.9% | -77.2% | `Mbit/s` |
| `S7_Live_Internet` | `upload_loaded_ping_p50_ms` | 60.65 | 60.23 | 60.40 | 68.35 | 66.70 | **330.75** | -445.3% | -395.9% | `ms` |
| `S7_Live_Internet` | `upload_loaded_ping_p99_ms` | 63.50 | 62.80 | 63.40 | 89.75 | 84.55 | **1673.50** | -2535.4% | -1879.3% | `ms` |
