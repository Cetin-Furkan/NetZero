# Grand Benchmark Summary — `7.4.0-rc2-netzero-v3-m3+`

- **Timestamp:** `2026-11-20T01:33:11.795060`
- **Kernel:** `Linux arch-cetin 7.4.0-rc2-netzero-v3-m3+ #8 SMP PREEMPT_DYNAMIC Fri Nov 20 01:19:28 +03 2026 x86_64 GNU/Linux`
- **Default Qdisc / CC / ECN:** `eiffel_dualq` / `bbr` / `tcp_ecn=2`
- **Governor / EPP:** `powersave` / `balance_performance`
- **Protocol Struct Sizes (`/proc/net/protocols`):** `TCP=2176 B`, `TCPv6=2304 B`, `UDP=1344 B`, `UNIX-STREAM=1088 B`

| Suite | Metric | Mean ± StdDev | Median (p50) | p99 | Min .. Max | Unit |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `S1_Memory` | `tcp_8stream_active_pages` | **4786.00** ± 675.90 | 4654.00 | 5877.00 | 3680.00 .. 5877.00 | `pages` |
| `S1_Memory` | `udp_64quic_socks_hoarded_pages` | **30.10** ± 2.88 | 31.00 | 34.00 | 27.00 .. 34.00 | `pages` |
| `S1_Memory` | `unix_2000socks_8000msg_sunreclaim_kib` | **1593.60** ± 159.01 | 1526.00 | 1976.00 | 1484.00 .. 1976.00 | `KiB` |
| `S2_VFS_Epoll` | `bore_interactive_8hog_p50_us` | **2.13** ± 0.12 | 2.10 | 2.45 | 2.03 .. 2.45 | `us` |
| `S2_VFS_Epoll` | `bore_interactive_8hog_p99_us` | **52.73** ± 90.34 | 6.11 | 230.90 | 2.60 .. 230.90 | `us` |
| `S2_VFS_Epoll` | `cmsg_scm_rights_creds_ns` | **1293.78** ± 52.99 | 1267.20 | 1410.59 | 1253.26 .. 1410.59 | `ns/pair` |
| `S2_VFS_Epoll` | `epoll_ctl_mod_1024fd_ns` | **167.20** ± 22.25 | 156.60 | 214.53 | 152.59 .. 214.53 | `ns/op` |
| `S2_VFS_Epoll` | `epoll_wait_128_of_1024_ns_ev` | **12.56** ± 2.56 | 11.79 | 19.84 | 11.53 .. 19.84 | `ns/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_cyc_ev` | **38.12** ± 1.16 | 37.88 | 40.55 | 36.59 .. 40.55 | `cycles/ev` |
| `S2_VFS_Epoll` | `epoll_wait_32_of_256_ns_ev` | **13.66** ± 0.42 | 13.58 | 14.53 | 13.11 .. 14.53 | `ns/ev` |
| `S2_VFS_Epoll` | `eventfd_pingpong_ns` | **3168.81** ± 60.70 | 3146.71 | 3267.40 | 3094.91 .. 3267.40 | `ns/RTT` |
| `S2_VFS_Epoll` | `futex_8thread_contended_mops` | **63.34** ± 1.03 | 63.82 | 64.17 | 61.37 .. 64.17 | `Mops/s` |
| `S2_VFS_Epoll` | `futex_private_wake_ns` | **92.52** ± 2.73 | 93.50 | 96.22 | 88.85 .. 96.22 | `ns/op` |
| `S2_VFS_Epoll` | `iouring_unix_64B_pingpong_ns` | **4104.19** ± 273.93 | 3930.94 | 4652.05 | 3875.91 .. 4652.05 | `ns/RTT` |
| `S2_VFS_Epoll` | `iouring_unix_batch32_mops` | **12.56** ± 1.51 | 13.09 | 13.60 | 8.53 .. 13.60 | `Mops/s` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_cycles` | **2627.93** ± 62.91 | 2606.86 | 2727.04 | 2552.94 .. 2727.04 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_accept4_eagain_ns` | **938.89** ± 22.63 | 932.34 | 977.03 | 915.50 .. 977.03 | `ns/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_cycles` | **271.22** ± 5.92 | 269.97 | 285.70 | 266.04 .. 285.70 | `cycles/call` |
| `S2_VFS_Epoll` | `mt_recv_eagain_ns` | **96.75** ± 2.11 | 96.31 | 101.92 | 94.91 .. 101.92 | `ns/call` |
| `S2_VFS_Epoll` | `pipe_64B_pingpong_ns` | **3368.30** ± 80.30 | 3339.33 | 3501.85 | 3282.31 .. 3501.85 | `ns/RTT` |
| `S2_VFS_Epoll` | `poll_2fd_pingpong_ns` | **3799.26** ± 118.69 | 3732.14 | 4006.23 | 3691.95 .. 4006.23 | `ns/RTT` |
| `S2_VFS_Epoll` | `ppoll_4fd_ready_ns` | **246.03** ± 12.12 | 242.45 | 280.15 | 240.08 .. 280.15 | `ns/call` |
| `S2_VFS_Epoll` | `shared_fd_8threads_mops` | **9.97** ± 0.43 | 10.07 | 10.40 | 9.16 .. 10.40 | `Mops/s` |
| `S2_VFS_Epoll` | `vfs_write_read_64B_ns` | **288.18** ± 19.15 | 282.33 | 341.68 | 275.18 .. 341.68 | `ns/pair` |
| `S2_VFS_Epoll` | `wfsync_eventfd_unpinned_ns` | **1848.53** ± 63.77 | 1847.09 | 1982.88 | 1761.12 .. 1982.88 | `ns/RTT` |
| `S2_VFS_Epoll` | `wfsync_pipe64B_unpinned_ns` | **2033.82** ± 94.37 | 2002.98 | 2214.16 | 1922.13 .. 2214.16 | `ns/RTT` |
| `S3_AF_UNIX` | `dbus_broker_sim_2hop_krps` | **203.88** ± 9.20 | 200.75 | 216.36 | 189.05 .. 216.36 | `kRPS` |
| `S3_AF_UNIX` | `scm_rights_memfd_pair_ns` | **624.38** ± 43.74 | 610.32 | 743.62 | 598.39 .. 743.62 | `ns/pair` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_mean_ns` | **864.99** ± 58.92 | 857.27 | 1000.76 | 797.32 .. 1000.76 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_256B_wayland_wire_p99_ns` | **4350.40** ± 170.57 | 4399.50 | 4614.00 | 4103.00 .. 4614.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_4KB_stream_gibs` | **11.80** ± 0.88 | 12.08 | 12.30 | 9.39 .. 12.30 | `GiB/s` |
| `S3_AF_UNIX` | `unix_64B_pingpong_mean_ns` | **792.86** ± 63.18 | 772.34 | 939.02 | 739.51 .. 939.02 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64B_pingpong_p99_ns` | **3946.60** ± 320.24 | 3895.50 | 4483.00 | 3583.00 .. 4483.00 | `ns/RTT` |
| `S3_AF_UNIX` | `unix_64KB_stream_gibs` | **18.88** ± 0.16 | 18.90 | 19.07 | 18.59 .. 19.07 | `GiB/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_kcommits_s` | **476.62** ± 17.40 | 480.59 | 495.87 | 442.06 .. 495.87 | `kcommits/s` |
| `S3_AF_UNIX` | `wayland_sim_8client_sys_cpu_ms` | **178.87** ± 7.89 | 178.53 | 191.15 | 162.35 .. 191.15 | `ms` |
| `S4_TCP_UDP` | `quic_1200B_batch_gbps` | **10.18** ± 0.33 | 10.33 | 10.57 | 9.62 .. 10.57 | `Gbps` |
| `S4_TCP_UDP` | `tcp_2000conn_handshake_us` | **15.77** ± 0.81 | 15.73 | 17.49 | 14.71 .. 17.49 | `us/conn` |
| `S4_TCP_UDP` | `tcp_64conn_http2_mux_krps` | **417.90** ± 16.02 | 421.01 | 434.80 | 387.28 .. 434.80 | `kRPS` |
| `S4_TCP_UDP` | `tcp_bulk_1stream_gibs` | **8.53** ± 0.41 | 8.65 | 8.86 | 7.42 .. 8.86 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_gibs` | **5.64** ± 0.28 | 5.57 | 6.08 | 5.29 .. 6.08 | `GiB/s` |
| `S4_TCP_UDP` | `tcp_bulk_8stream_sys_cpu_ms` | **302.25** ± 12.15 | 305.83 | 318.20 | 281.89 .. 318.20 | `ms` |
| `S4_TCP_UDP` | `udp_unconn_4dest_mpps` | **0.95** ± 0.02 | 0.95 | 0.96 | 0.92 .. 0.96 | `Mpps` |
| `S5_Qdisc` | `qdisc_8flow_paced_gbps` | **9.20** ± 0.09 | 9.18 | 9.35 | 9.08 .. 9.35 | `Gbps` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p50_us` | **7.29** ± 0.24 | 7.20 | 7.92 | 7.11 .. 7.92 | `us` |
| `S5_Qdisc` | `qdisc_loaded_rpc_p99_us` | **17.70** ± 1.28 | 17.63 | 19.39 | 15.48 .. 19.39 | `us` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_sys_ms` | **2.56** ± 1.02 | 2.30 | 4.49 | 1.57 .. 4.49 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_ring_dispatch_wall_ms` | **9.40** ± 1.55 | 9.50 | 11.03 | 7.81 .. 11.03 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_sys_ms` | **0.80** ± 0.72 | 1.06 | 1.51 | 0.00 .. 1.51 | `ms` |
| `S6_Live_Wayland` | `engine_v2_test_wayland_wire_wall_ms` | **1.72** ± 0.62 | 1.54 | 3.48 | 1.39 .. 3.48 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_fps` | **124.70** ± 0.04 | 124.70 | 124.78 | 124.65 .. 124.78 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_avg_us` | **457.39** ± 10.18 | 453.31 | 473.18 | 445.94 .. 473.18 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_gpu_max_us` | **540.38** ± 21.29 | 541.57 | 578.28 | 507.55 .. 578.28 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_sys_cpu_ms` | **95.90** ± 7.93 | 93.14 | 111.01 | 85.83 .. 111.01 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_total_proc_ms` | **2553.52** ± 9.50 | 2552.06 | 2568.99 | 2541.22 .. 2568.99 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_user_cpu_ms` | **58.17** ± 6.01 | 59.63 | 63.70 | 44.84 .. 63.70 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_clean_wall_ms` | **2405.82** ± 0.74 | 2405.80 | 2406.68 | 2404.17 .. 2406.68 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_fps` | **125.34** ± 0.03 | 125.34 | 125.38 | 125.28 .. 125.38 | `FPS` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_avg_us` | **436.86** ± 2.27 | 435.82 | 441.07 | 434.54 .. 441.07 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_gpu_max_us` | **550.78** ± 48.28 | 530.97 | 654.01 | 508.38 .. 654.01 | `us` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_sys_cpu_ms` | **86.03** ± 12.60 | 83.08 | 102.24 | 69.05 .. 102.24 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_total_proc_ms` | **2564.78** ± 27.52 | 2566.51 | 2626.60 | 2530.77 .. 2626.60 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_user_cpu_ms` | **42.01** ± 5.07 | 41.61 | 51.47 | 35.63 .. 51.47 | `ms` |
| `S6_Live_Wayland` | `engine_v2_world0_storm_wall_ms` | **2393.55** ± 0.54 | 2393.41 | 2394.70 | 2392.77 .. 2394.70 | `ms` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_mean_us` | **10.80** ± 2.76 | 10.14 | 18.21 | 8.76 .. 18.21 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p50_us` | **9.24** ± 1.47 | 8.58 | 12.89 | 8.49 .. 12.89 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_p99_us` | **25.50** ± 25.90 | 17.86 | 98.73 | 12.51 .. 98.73 | `us` |
| `S6_Live_Wayland` | `mutter_wl_roundtrip_sys_ms` | **3.78** ± 1.75 | 3.80 | 6.46 | 0.91 .. 6.46 | `ms` |
| `S7_Live_Internet` | `download_4stream_mbps` | **7.75** ± 2.71 | 6.95 | 13.81 | 4.97 .. 13.81 | `Mbit/s` |
| `S7_Live_Internet` | `download_loaded_ping_p50_ms` | **212.40** ± 58.75 | 220.50 | 293.00 | 119.00 .. 293.00 | `ms` |
| `S7_Live_Internet` | `download_loaded_ping_p99_ms` | **560.10** ± 121.69 | 536.00 | 873.00 | 463.00 .. 873.00 | `ms` |
| `S7_Live_Internet` | `https_tcp_connect_ms` | **243.78** ± 644.21 | 36.77 | 2076.93 | 27.69 .. 2076.93 | `ms` |
| `S7_Live_Internet` | `https_tls_handshake_ms` | **330.39** ± 643.19 | 127.58 | 2160.07 | 106.52 .. 2160.07 | `ms` |
| `S7_Live_Internet` | `https_ttfb_ms` | **440.91** ± 644.75 | 224.69 | 2272.34 | 201.84 .. 2272.34 | `ms` |
| `S7_Live_Internet` | `idle_ping_jitter_stdev_ms` | **12.61** ± 4.76 | 13.50 | 20.46 | 4.75 .. 20.46 | `ms` |
| `S7_Live_Internet` | `idle_ping_mean_ms` | **44.58** ± 6.54 | 44.65 | 55.24 | 34.64 .. 55.24 | `ms` |
| `S7_Live_Internet` | `idle_ping_p99_ms` | **67.88** ± 14.04 | 68.10 | 89.00 | 46.90 .. 89.00 | `ms` |
| `S7_Live_Internet` | `upload_4stream_mbps` | **2.35** ± 2.31 | 1.61 | 8.23 | 0.28 .. 8.23 | `Mbit/s` |
| `S7_Live_Internet` | `upload_loaded_ping_p50_ms` | **486.50** ± 342.69 | 330.75 | 1031.00 | 115.00 .. 1031.00 | `ms` |
| `S7_Live_Internet` | `upload_loaded_ping_p99_ms` | **1705.80** ± 1088.79 | 1673.50 | 3455.00 | 346.00 .. 3455.00 | `ms` |
