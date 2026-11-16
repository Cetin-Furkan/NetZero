# Grand Benchmark Summary — `7.3.0-rc6-2-cachyos-rc`

- **Timestamp:** `2026-11-15T01:05:57.315247`
- **Kernel:** `Linux arch-cetin 7.3.0-rc6-2-cachyos-rc #1 SMP PREEMPT_DYNAMIC Fri, 09 Oct 2026 20:06:37 +0000 x86_64 GNU/Linux`
- **Default Qdisc / CC / ECN:** `fq_codel` / `bbr` / `tcp_ecn=3`
- **Governor / EPP:** `powersave` / `balance_performance`
- **Protocol Struct Sizes (`/proc/net/protocols`):** `TCP=2432 B`, `TCPv6=2560 B`, `UDP=1344 B`, `UNIX-STREAM=1152 B`

| Suite | Metric | Mean ± StdDev | Median (p50) | p99 | Min .. Max | Unit |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `S1_Memory` | `tcp_8stream_active_pages` | **4574.90** ± 1030.16 | 4638.50 | 6222.00 | 3160.00 .. 6222.00 | `pages` |
| `S1_Memory` | `udp_64quic_socks_hoarded_pages` | **335.40** ± 130.19 | 240.00 | 492.00 | 224.00 .. 492.00 | `pages` |
| `S1_Memory` | `unix_2000socks_8000msg_sunreclaim_kib` | **7074.80** ± 289.83 | 7018.00 | 7676.00 | 6784.00 .. 7676.00 | `KiB` |
| `S2_VFS_Epoll` | `cmsg_scm_rights_creds_ns` | **1287.14** ± 34.26 | 1278.97 | 1380.49 | 1259.30 .. 1380.49 | `ns/pair` |
| `S2_VFS_Epoll` | `epoll_wait_128_of_1024_ns_ev` | **40.21** ± 1.43 | 39.79 | 44.21 | 39.31 .. 44.21 | `ns/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_cyc_ev` | **102.41** ± 4.90 | 101.20 | 115.99 | 99.03 .. 115.99 | `cycles/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_ns_ev` | **36.54** ± 1.75 | 36.10 | 41.38 | 35.33 .. 41.38 | `ns/ev` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_cycles` | **2563.79** ± 47.19 | 2560.89 | 2641.40 | 2483.69 .. 2641.40 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_ns` | **914.99** ± 16.85 | 914.02 | 942.67 | 886.31 .. 942.67 | `ns/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_cycles` | **390.76** ± 13.61 | 387.80 | 427.83 | 380.41 .. 427.83 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_ns` | **139.56** ± 4.86 | 138.50 | 152.79 | 135.86 .. 152.79 | `ns/call` |
| `S2_VFS_Epoll` | `shared_fd_8threads_mops` | **5.39** ± 0.18 | 5.43 | 5.71 | 5.04 .. 5.71 | `Mops/s` |
| `S2_VFS_Epoll` | `vfs_write_read_64B_ns` | **547.57** ± 19.84 | 538.60 | 586.36 | 532.52 .. 586.36 | `ns/pair` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_mean_ns` | **3103.92** ± 279.69 | 3038.22 | 3862.62 | 2867.98 .. 3862.62 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_p99_ns` | **6015.50** ± 1392.73 | 5490.00 | 9445.00 | 4798.00 .. 9445.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_4KB_stream_gibs` | **4.58** ± 0.20 | 4.62 | 4.85 | 4.15 .. 4.85 | `GiB/s` |
| `S3_AF_UNIX` | `unix_64B_pingpong_mean_ns` | **3029.92** ± 206.73 | 2954.56 | 3457.60 | 2790.81 .. 3457.60 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64B_pingpong_p99_ns` | **5655.60** ± 1666.95 | 5012.00 | 9592.00 | 4416.00 .. 9592.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64KB_stream_gibs` | **10.60** ± 0.33 | 10.70 | 10.80 | 9.69 .. 10.80 | `GiB/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_kcommits_s` | **340.39** ± 13.16 | 341.14 | 362.42 | 316.19 .. 362.42 | `kcommits/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_sys_cpu_ms` | **256.38** ± 10.37 | 254.35 | 277.46 | 244.85 .. 277.46 | `ms` |
| `S4_TCP_UDP` | `quic_1200B_batch_gbps` | **8.92** ± 0.29 | 8.97 | 9.30 | 8.48 .. 9.30 | `Gbps` |
| `S4_TCP_UDP` | `tcp_2000conn_handshake_us` | **17.26** ± 0.29 | 17.20 | 17.69 | 16.89 .. 17.69 | `us/conn` |
| `S4_TCP_UDP` | `tcp_64conn_http2_mux_krps` | **373.78** ± 9.53 | 375.39 | 389.67 | 352.77 .. 389.67 | `kRPS` |
| `S4_TCP_UDP` | `tcp_bulk_1stream_gibs` | **8.45** ± 0.74 | 8.27 | 9.49 | 7.15 .. 9.49 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_gibs` | **5.90** ± 0.30 | 5.83 | 6.44 | 5.59 .. 6.44 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_sys_cpu_ms` | **287.60** ± 12.49 | 289.40 | 306.60 | 266.00 .. 306.60 | `ms` |
| `S4_TCP_UDP` | `udp_unconn_4dest_mpps` | **0.83** ± 0.03 | 0.83 | 0.88 | 0.79 .. 0.88 | `Mpps` |
| `S5_Qdisc` | `qdisc_8flow_paced_gbps` | **8.66** ± 2.86 | 9.80 | 10.39 | 3.06 .. 10.39 | `Gbps` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p50_us` | **6.59** ± 0.06 | 6.59 | 6.68 | 6.49 .. 6.68 | `us` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p99_us` | **23.08** ± 3.57 | 22.46 | 29.98 | 19.18 .. 29.98 | `us` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_sys_ms` | **0.73** ± 0.80 | 0.40 | 1.73 | 0.00 .. 1.73 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_wall_ms` | **7.83** ± 0.17 | 7.81 | 8.24 | 7.63 .. 8.24 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_sys_ms` | **0.78** ± 0.73 | 0.91 | 1.73 | 0.00 .. 1.73 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_wall_ms` | **1.57** ± 0.53 | 1.39 | 3.03 | 1.28 .. 3.03 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_fps` | **124.96** ± 0.05 | 124.97 | 125.07 | 124.85 .. 125.07 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_avg_us` | **469.30** ± 13.05 | 473.37 | 484.99 | 451.65 .. 484.99 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_max_us` | **563.36** ± 32.56 | 563.85 | 615.42 | 512.55 .. 615.42 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_sys_cpu_ms` | **90.15** ± 5.54 | 90.20 | 96.89 | 78.63 .. 96.89 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_total_proc_ms` | **2547.22** ± 6.98 | 2548.53 | 2556.16 | 2535.94 .. 2556.16 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_user_cpu_ms` | **68.63** ± 8.19 | 69.85 | 82.40 | 51.12 .. 82.40 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_wall_ms` | **2400.76** ± 1.05 | 2400.62 | 2402.97 | 2398.70 .. 2402.97 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_fps` | **123.77** ± 1.35 | 123.60 | 125.33 | 121.89 .. 125.33 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_avg_us` | **528.25** ± 81.42 | 549.90 | 662.62 | 441.54 .. 662.62 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_max_us` | **1841.19** ± 1217.52 | 2230.89 | 3770.05 | 522.71 .. 3770.05 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_sys_cpu_ms` | **102.19** ± 7.44 | 102.25 | 114.70 | 91.05 .. 114.70 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_total_proc_ms` | **2608.61** ± 21.63 | 2609.26 | 2645.89 | 2574.06 .. 2645.89 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_user_cpu_ms` | **77.76** ± 9.59 | 75.70 | 100.44 | 68.30 .. 100.44 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_wall_ms` | **2424.09** ± 26.50 | 2427.26 | 2461.24 | 2393.63 .. 2461.24 | `ms` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_mean_us` | **11.20** ± 0.07 | 11.19 | 11.31 | 11.10 .. 11.31 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p50_us` | **10.54** ± 0.02 | 10.54 | 10.56 | 10.51 .. 10.56 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p99_us` | **20.60** ± 3.55 | 20.07 | 27.44 | 16.85 .. 27.44 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_sys_ms` | **4.67** ± 1.11 | 4.60 | 6.54 | 2.67 .. 6.54 | `ms` |
| `S7_Live_Internet` | `download_4stream_mbps` | **31.69** ± 4.40 | 30.36 | 38.70 | 25.66 .. 38.70 | `Mbit/s` |
| `S7_Live_Internet` | `download_loaded_ping_p50_ms` | **90.58** ± 7.67 | 88.00 | 104.00 | 81.90 .. 104.00 | `ms` |
| `S7_Live_Internet` | `download_loaded_ping_p99_ms` | **216.40** ± 42.87 | 217.00 | 275.00 | 161.00 .. 275.00 | `ms` |
| `S7_Live_Internet` | `https_tcp_connect_ms` | **63.76** ± 4.85 | 64.78 | 70.87 | 57.09 .. 70.87 | `ms` |
| `S7_Live_Internet` | `https_tls_handshake_ms` | **126.24** ± 10.99 | 127.07 | 145.19 | 110.93 .. 145.19 | `ms` |
| `S7_Live_Internet` | `https_ttfb_ms` | **222.41** ± 33.74 | 214.95 | 306.08 | 183.85 .. 306.08 | `ms` |
| `S7_Live_Internet` | `idle_ping_jitter_stdev_ms` | **0.33** ± 0.16 | 0.29 | 0.60 | 0.13 .. 0.60 | `ms` |
| `S7_Live_Internet` | `idle_ping_mean_ms` | **60.83** ± 0.08 | 60.82 | 60.99 | 60.74 .. 60.99 | `ms` |
| `S7_Live_Internet` | `idle_ping_p99_ms` | **61.42** ± 0.40 | 61.35 | 62.10 | 61.00 .. 62.10 | `ms` |
| `S7_Live_Internet` | `upload_4stream_mbps` | **20.04** ± 3.45 | 19.94 | 25.50 | 15.47 .. 25.50 | `Mbit/s` |
| `S7_Live_Internet` | `upload_loaded_ping_p50_ms` | **61.84** ± 3.71 | 60.65 | 72.35 | 60.30 .. 72.35 | `ms` |
| `S7_Live_Internet` | `upload_loaded_ping_p99_ms` | **88.26** ± 52.36 | 63.50 | 192.00 | 62.00 .. 192.00 | `ms` |
