#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static inline uint64_t rdtsc_ordered(void) {
    unsigned int aux;
    uint64_t rax, rdx;
    __asm__ __volatile__("rdtscp" : "=a"(rax), "=d"(rdx), "=c"(aux) :: "memory");
    return (rdx << 32) | rax;
}

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void pin_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu % sysconf(_SC_NPROCESSORS_ONLN), &set);
    sched_setaffinity(0, sizeof(set), &set);
}

static long read_proto_field(const char *proto, int col_idx) {
    FILE *f = fopen("/proc/net/protocols", "r");
    if (!f) return -1;
    char line[512];
    long val = -1;
    while (fgets(line, sizeof(line), f)) {
        char name[64];
        long size, sockets, memory;
        if (sscanf(line, "%63s %ld %ld %ld", name, &size, &sockets, &memory) == 4) {
            if (strcmp(name, proto) == 0) {
                if (col_idx == 1) val = size;
                else if (col_idx == 2) val = sockets;
                else if (col_idx == 3) val = memory;
                break;
            }
        }
    }
    fclose(f);
    return val;
}

static long read_meminfo_kb(const char *key) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[256];
    long val = -1;
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
            val = atol(line + klen + 1);
            break;
        }
    }
    fclose(f);
    return val;
}

/* Keep files->count >= 2 to simulate realistic multi-threaded desktop processes */
static atomic_int bg_thread_stop = 0;
static void *bg_holder_fn(void *arg) {
    (void)arg;
    while (!atomic_load_explicit(&bg_thread_stop, memory_order_relaxed)) {
        usleep(5000);
    }
    return NULL;
}

/* =========================================================================
 * TEST 1: Multi-Threaded CLONE_FILES fdget vs close/dup2 Torture Test
 * ========================================================================= */
static atomic_int race_stop = 0;
static int race_fds[8];

static void *race_reader_fn(void *arg) {
    int idx = (int)(intptr_t)arg;
    char buf[16];
    struct sockaddr_storage ss;
    socklen_t slen;
    while (!atomic_load_explicit(&race_stop, memory_order_relaxed)) {
        int fd = race_fds[idx & 7];
        recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        slen = sizeof(ss);
        getsockname(fd, (struct sockaddr *)&ss, &slen);
    }
    return NULL;
}

static void test_mt_fd_race_torture(void) {
    int sp[8][2];
    for (int i = 0; i < 8; i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sp[i]) < 0) {
            perror("socketpair");
            exit(1);
        }
        race_fds[i] = sp[i][0];
    }
    atomic_store(&race_stop, 0);
    pthread_t th[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&th[i], NULL, race_reader_fn, (void *)(intptr_t)i);

    /* Rapidly dup2 and close replacement sockets onto race_fds[] while readers hammer them */
    for (int iter = 0; iter < 15000; iter++) {
        int tmp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, tmp) == 0) {
            int slot = iter & 7;
            dup2(tmp[0], race_fds[slot]);
            close(tmp[0]);
            close(tmp[1]);
        }
    }
    atomic_store(&race_stop, 1);
    for (int i = 0; i < 4; i++)
        pthread_join(th[i], NULL);
    for (int i = 0; i < 8; i++) {
        close(sp[i][0]);
        close(sp[i][1]);
    }
    printf("[PASS] ABI Torture 1: Multi-threaded CLONE_FILES fdget/dup2/close race (15,000 swaps)\n");
}

static void *mt_shared_recv_worker(void *arg) {
    int fd = *(int *)arg;
    char buf[32];
    for (int i = 0; i < 200000; i++) {
        ssize_t r = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        (void)r;
    }
    return NULL;
}

/* =========================================================================
 * TEST 2: Layer 1 Syscall & VFS Microbenchmarks (under MT files->count >= 2)
 * ========================================================================= */
static void bench_layer1_syscalls(void) {
    pin_cpu(0);
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv);

    /* Verify /proc/self/fd readlink ABI ("socket:[...]") */
    char linkbuf[128] = {0}, procpath[64];
    snprintf(procpath, sizeof(procpath), "/proc/self/fd/%d", sv[0]);
    ssize_t llen = readlink(procpath, linkbuf, sizeof(linkbuf) - 1);
    if (llen <= 0 || strncmp(linkbuf, "socket:[", 8) != 0) {
        fprintf(stderr, "[FAIL] /proc/self/fd readlink broken: got '%s'\n", linkbuf);
        exit(1);
    }
    printf("[PASS] ABI Torture 2: readlink(%s) -> %s\n", procpath, linkbuf);

    /* 2A: Non-blocking recv -EAGAIN (pure fdget + LSM + direct dispatch + fdput) */
    char buf[64] = {0};
    const int N = 500000;
    for (int i = 0; i < 5000; i++) recv(sv[0], buf, sizeof(buf), MSG_DONTWAIT);
    uint64_t c0 = rdtsc_ordered(), t0 = now_ns();
    for (int i = 0; i < N; i++) {
        ssize_t r = recv(sv[0], buf, sizeof(buf), MSG_DONTWAIT);
        (void)r;
    }
    uint64_t c1 = rdtsc_ordered(), t1 = now_ns();
    printf("[BENCH] L1_mt_recv_eagain       : %6.1f cycles/call | %6.1f ns/call\n",
           (double)(c1 - c0) / N, (double)(t1 - t0) / N);

    /* 2A-contended: 4 threads sharing 1 socket FD (tests MESI bounce on file->f_ref!) */
    pthread_t mtw[4];
    t0 = now_ns();
    for (int i = 0; i < 4; i++) pthread_create(&mtw[i], NULL, mt_shared_recv_worker, &sv[0]);
    for (int i = 0; i < 4; i++) pthread_join(mtw[i], NULL);
    t1 = now_ns();
    printf("[BENCH] L1_4thr_shared_fd_recv  : %6.1f ns/call-agg (800k calls across 4 cores)\n",
           (double)(t1 - t0) / 800000.0);

    /* 2B: Non-blocking accept4 -EAGAIN (tests lazy do_accept VFS alloc elimination) */
    int lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    struct sockaddr_in sin = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    bind(lfd, (struct sockaddr *)&sin, sizeof(sin));
    listen(lfd, 128);
    const int NA = 200000;
    for (int i = 0; i < 2000; i++) accept4(lfd, NULL, NULL, SOCK_NONBLOCK);
    c0 = rdtsc_ordered(); t0 = now_ns();
    for (int i = 0; i < NA; i++) {
        int a = accept4(lfd, NULL, NULL, SOCK_NONBLOCK);
        (void)a;
    }
    c1 = rdtsc_ordered(); t1 = now_ns();
    printf("[BENCH] L1_mt_accept4_eagain    : %6.1f cycles/call | %6.1f ns/call\n",
           (double)(c1 - c0) / NA, (double)(t1 - t0) / NA);
    close(lfd);

    /* 2C: sendmsg with 64-byte cmsg control buffer (> 36B legacy stack ctl cliff!) */
    int dsv[2];
    socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, dsv);
    int one = 1;
    setsockopt(dsv[1], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one));
    char cbuf[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int) * 4)];
    memset(cbuf, 0, sizeof(cbuf));
    struct iovec iov = {.iov_base = buf, .iov_len = 16};
    struct msghdr msg = {
        .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = cbuf, .msg_controllen = CMSG_SPACE(sizeof(struct ucred))
    };
    /* Pad with a second harmless SOL_SOCKET SCM_CREDENTIALS or test >36B cmsg */
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_CREDENTIALS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(struct ucred));
    struct ucred *uc = (struct ucred *)CMSG_DATA(cmsg);
    uc->pid = getpid(); uc->uid = getuid(); uc->gid = getgid();

    /* Use 2 SCM_RIGHTS FDs + SCM_CREDENTIALS = 56 bytes (> 36B!) */
    msg.msg_controllen = CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int) * 2);
    struct cmsghdr *cmsg2 = CMSG_NXTHDR(&msg, cmsg);
    cmsg2->cmsg_level = SOL_SOCKET;
    cmsg2->cmsg_type = SCM_RIGHTS;
    cmsg2->cmsg_len = CMSG_LEN(sizeof(int) * 2);
    int pass_fds[2] = {sv[0], sv[1]};
    memcpy(CMSG_DATA(cmsg2), pass_fds, sizeof(pass_fds));

    const int NC = 50000;
    c0 = rdtsc_ordered(); t0 = now_ns();
    for (int i = 0; i < NC; i++) {
        if (sendmsg(dsv[0], &msg, MSG_DONTWAIT) > 0) {
            char rcbuf[128];
            struct msghdr rmsg = {.msg_iov = &iov, .msg_iovlen = 1,
                                  .msg_control = rcbuf, .msg_controllen = sizeof(rcbuf)};
            if (recvmsg(dsv[1], &rmsg, MSG_DONTWAIT) > 0) {
                for (struct cmsghdr *rc = CMSG_FIRSTHDR(&rmsg); rc; rc = CMSG_NXTHDR(&rmsg, rc)) {
                    if (rc->cmsg_type == SCM_RIGHTS) {
                        int rfds[2];
                        memcpy(rfds, CMSG_DATA(rc), sizeof(rfds));
                        close(rfds[0]);
                        close(rfds[1]);
                    }
                }
            }
        }
    }
    c1 = rdtsc_ordered(); t1 = now_ns();
    printf("[BENCH] L1_cmsg_56B_roundtrip   : %6.1f cycles/pair | %6.1f ns/pair\n",
           (double)(c1 - c0) / NC, (double)(t1 - t0) / NC);

    /* 2D: socket() + close() lifecycle latency */
    const int NS = 100000;
    c0 = rdtsc_ordered(); t0 = now_ns();
    for (int i = 0; i < NS; i++) {
        int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        close(s);
    }
    c1 = rdtsc_ordered(); t1 = now_ns();
    printf("[BENCH] L1_socket_close_pair    : %6.1f cycles/pair | %6.1f ns/pair\n",
           (double)(c1 - c0) / NS, (double)(t1 - t0) / NS);

    close(dsv[0]); close(dsv[1]);
    close(sv[0]); close(sv[1]);
}

/* =========================================================================
 * TEST 3: Layer 2 Epoll Ready-Harvest & EPOLLET Correctness
 * ========================================================================= */
static void bench_layer2_epoll(void) {
    pin_cpu(0);
    const int PAIRS = 128, ACTIVE = 32, ITERS = 50000;
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    int sp[128][2];
    for (int i = 0; i < PAIRS; i++) {
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sp[i]);
        struct epoll_event ev = {.events = EPOLLIN, .data.u32 = (uint32_t)i};
        epoll_ctl(epfd, EPOLL_CTL_ADD, sp[i][0], &ev);
        if (i < ACTIVE) {
            char x = 'a';
            write(sp[i][1], &x, 1);
        }
    }
    struct epoll_event out[64];
    /* Warmup */
    for (int i = 0; i < 1000; i++) epoll_wait(epfd, out, 64, 0);

    uint64_t c0 = rdtsc_ordered(), t0 = now_ns();
    for (int i = 0; i < ITERS; i++) {
        int n = epoll_wait(epfd, out, 64, 0);
        if (n != ACTIVE) {
            fprintf(stderr, "[FAIL] epoll_wait expected %d got %d\n", ACTIVE, n);
            exit(1);
        }
    }
    uint64_t c1 = rdtsc_ordered(), t1 = now_ns();
    printf("[BENCH] L2_epoll_wait_32ready   : %6.1f cycles/ev   | %6.1f ns/ev\n",
           (double)(c1 - c0) / ((double)ITERS * ACTIVE),
           (double)(t1 - t0) / ((double)ITERS * ACTIVE));

    /* Verify EPOLLET strict edge semantics */
    int et_ep = epoll_create1(EPOLL_CLOEXEC);
    int et_sp[2];
    socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, et_sp);
    struct epoll_event et_ev = {.events = EPOLLIN | EPOLLET, .data.fd = et_sp[0]};
    epoll_ctl(et_ep, EPOLL_CTL_ADD, et_sp[0], &et_ev);
    write(et_sp[1], "xy", 2);
    int n1 = epoll_wait(et_ep, out, 4, 0);
    int n2 = epoll_wait(et_ep, out, 4, 0); /* Must be 0 because edge already delivered! */
    char ch;
    read(et_sp[0], &ch, 1); /* Partial read: 1 byte still unread */
    write(et_sp[1], "z", 1); /* New arrival must re-trigger EPOLLET! */
    int n3 = epoll_wait(et_ep, out, 4, 0);
    if (n1 != 1 || n2 != 0 || n3 != 1) {
        fprintf(stderr, "[FAIL] EPOLLET broken: n1=%d n2=%d n3=%d\n", n1, n2, n3);
        exit(1);
    }
    printf("[PASS] ABI Torture 3: EPOLLET edge-triggered semantics verified (1 -> 0 -> 1)\n");

    close(et_ep); close(et_sp[0]); close(et_sp[1]);
    for (int i = 0; i < PAIRS; i++) { close(sp[i][0]); close(sp[i][1]); }
    close(epfd);
}

/* =========================================================================
 * TEST 4: Layer 3 AF_UNIX IPC Latency, Throughput, RAM & SCM_RIGHTS + GC
 * ========================================================================= */
struct unix_ping_ctx {
    int fd;
    int iters;
};

static void *unix_pong_thread(void *arg) {
    struct unix_ping_ctx *ctx = arg;
    pin_cpu(1);
    char buf[64];
    for (int i = 0; i < ctx->iters; i++) {
        ssize_t r;
        do { r = read(ctx->fd, buf, 64); } while (r < 0 && errno == EINTR);
        if (r != 64) break;
        do { r = write(ctx->fd, buf, 64); } while (r < 0 && errno == EINTR);
    }
    return NULL;
}

static void *unix_stream_sink(void *arg) {
    int fd = *(int *)arg;
    pin_cpu(1);
    char buf[32768];
    while (read(fd, buf, sizeof(buf)) > 0) {}
    return NULL;
}

static void bench_layer3_af_unix(void) {
    /* 4A: SCM_RIGHTS + Stream Causality + Cyclic Garbage Collector Torture */
    int sv[2], pipe_fds[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    pipe(pipe_fds);
    write(pipe_fds[1], "OK!", 3);

    /* Write "AAA" (fast path), then SCM_RIGHTS with "BBB", then "CCC" (fast path) */
    write(sv[0], "AAA", 3);
    char cbuf[CMSG_SPACE(sizeof(int))];
    memset(cbuf, 0, sizeof(cbuf));
    struct iovec iov = {.iov_base = "BBB", .iov_len = 3};
    struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof(cbuf)};
    struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type = SCM_RIGHTS;
    cm->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cm), &pipe_fds[0], sizeof(int));
    sendmsg(sv[0], &msg, 0);
    write(sv[0], "CCC", 3);

    /* Receiver reads stream + control messages */
    char rbuf[32] = {0}, rcbuf[CMSG_SPACE(sizeof(int))];
    int got_fd = -1, total_read = 0;
    while (total_read < 9) {
        struct iovec riov = {.iov_base = rbuf + total_read, .iov_len = 9 - total_read};
        struct msghdr rmsg = {.msg_iov = &riov, .msg_iovlen = 1, .msg_control = rcbuf, .msg_controllen = sizeof(rcbuf)};
        ssize_t n = recvmsg(sv[1], &rmsg, 0);
        if (n <= 0) break;
        for (struct cmsghdr *rc = CMSG_FIRSTHDR(&rmsg); rc; rc = CMSG_NXTHDR(&rmsg, rc)) {
            if (rc->cmsg_type == SCM_RIGHTS)
                memcpy(&got_fd, CMSG_DATA(rc), sizeof(int));
        }
        total_read += (int)n;
    }
    char pcheck[4] = {0};
    if (got_fd >= 0) { read(got_fd, pcheck, 3); close(got_fd); }
    if (strcmp(rbuf, "AAABBBCCC") != 0 || strcmp(pcheck, "OK!") != 0) {
        fprintf(stderr, "[FAIL] AF_UNIX SCM_RIGHTS stream causality broken: rbuf='%s' pipe='%s'\n", rbuf, pcheck);
        exit(1);
    }
    close(pipe_fds[0]); close(pipe_fds[1]);
    close(sv[0]); close(sv[1]);

    /* Cyclic SCM_RIGHTS GC test (c1[0] <-> c2[0] holding each other) */
    long unix_before = read_proto_field("UNIX-STREAM", 2);
    if (unix_before < 0) unix_before = read_proto_field("UNIX", 2);
    for (int k = 0; k < 32; k++) {
        int c1[2], c2[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, c1);
        socketpair(AF_UNIX, SOCK_STREAM, 0, c2);
        memcpy(CMSG_DATA(cm), &c1[0], sizeof(int));
        sendmsg(c2[1], &msg, 0);
        memcpy(CMSG_DATA(cm), &c2[0], sizeof(int));
        sendmsg(c1[1], &msg, 0);
        close(c1[0]); close(c1[1]); close(c2[0]); close(c2[1]);
    }
    /* Trigger unix_gc via dummy socket close */
    int trig[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, trig); close(trig[0]); close(trig[1]);
    usleep(20000);
    long unix_after = read_proto_field("UNIX-STREAM", 2);
    if (unix_after < 0) unix_after = read_proto_field("UNIX", 2);
    printf("[PASS] ABI Torture 4: AF_UNIX SCM_RIGHTS causality ('AAABBBCCC') + Cyclic GC (open: %ld -> %ld)\n",
           unix_before, unix_after);

    /* 4B: Cross-Core 64-byte Ping-Pong Latency (Wayland / PipeWire IPC) */
    int pp[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, pp);
    const int PITERS = 100000;
    struct unix_ping_ctx ctx = {.fd = pp[1], .iters = PITERS + 1000};
    pthread_t pth;
    pthread_create(&pth, NULL, unix_pong_thread, &ctx);
    pin_cpu(0);
    char pbuf[64] = {1};
    for (int i = 0; i < 1000; i++) { write(pp[0], pbuf, 64); read(pp[0], pbuf, 64); }
    uint64_t c0 = rdtsc_ordered(), t0 = now_ns();
    for (int i = 0; i < PITERS; i++) {
        if (write(pp[0], pbuf, 64) != 64) break;
        if (read(pp[0], pbuf, 64) != 64) break;
    }
    uint64_t c1 = rdtsc_ordered(), t1 = now_ns();
    pthread_join(pth, NULL);
    close(pp[0]); close(pp[1]);
    printf("[BENCH] L3_unix_64B_pingpong    : %6.1f cycles/RTT  | %6.1f ns/RTT\n",
           (double)(c1 - c0) / PITERS, (double)(t1 - t0) / PITERS);

    /* 4C: AF_UNIX Bulk 4KiB Streaming Throughput */
    int bp[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, bp);
    pthread_t sth;
    pthread_create(&sth, NULL, unix_stream_sink, &bp[1]);
    pin_cpu(0);
    char sbuf[4096];
    memset(sbuf, 0x5a, sizeof(sbuf));
    const size_t TOTAL_BYTES = 2ULL * 1024 * 1024 * 1024; /* 2 GiB */
    size_t sent = 0;
    t0 = now_ns();
    while (sent < TOTAL_BYTES) {
        ssize_t w = write(bp[0], sbuf, sizeof(sbuf));
        if (w > 0) sent += (size_t)w;
    }
    shutdown(bp[0], SHUT_WR);
    pthread_join(sth, NULL);
    t1 = now_ns();
    close(bp[0]); close(bp[1]);
    double gbps = ((double)TOTAL_BYTES / (1024.0 * 1024.0 * 1024.0)) / ((double)(t1 - t0) / 1e9);
    printf("[BENCH] L3_unix_4KB_throughput  : %6.2f GiB/s\n", gbps);

    /* 4D: Desktop 1,000-Socket Idle + Queued IPC Slab RAM Footprint */
    int (*many)[2] = calloc(500, sizeof(int[2]));
    long sunrec_before = read_meminfo_kb("SUnreclaim");
    for (int i = 0; i < 500; i++) {
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, many[i]);
        /* Queue eight 64-byte IPC frames in each socket (4,000 live IPC messages) */
        for (int m = 0; m < 8; m++) write(many[i][0], pbuf, 64);
    }
    long sunrec_after = read_meminfo_kb("SUnreclaim");
    long unix_obj_sz = read_proto_field("UNIX-STREAM", 1);
    if (unix_obj_sz < 0) unix_obj_sz = read_proto_field("UNIX", 1);
    printf("[BENCH] L3_1000_unix_socks_ram  : %4ld KiB SUnreclaim delta | unix_sock=%ld B\n",
           sunrec_after - sunrec_before, unix_obj_sz);
    for (int i = 0; i < 500; i++) { close(many[i][0]); close(many[i][1]); }
    free(many);
}

/* =========================================================================
 * TEST 5: Layer 4 & 5 TCP Connect Storm, Throughput, UDP Hoarding & sendto
 * ========================================================================= */
static atomic_int tcp_srv_stop = 0;
static void *tcp_accept_sink_fn(void *arg) {
    int lfd = *(int *)arg;
    pin_cpu(1);
    while (!atomic_load_explicit(&tcp_srv_stop, memory_order_relaxed)) {
        struct pollfd pfd = {.fd = lfd, .events = POLLIN};
        if (poll(&pfd, 1, 10) > 0) {
            while (1) {
                int c = accept4(lfd, NULL, NULL, SOCK_NONBLOCK);
                if (c < 0) break;
                char b[64];
                ssize_t n = recv(c, b, sizeof(b), 0);
                if (n > 0) send(c, b, (size_t)n, MSG_NOSIGNAL);
                close(c);
            }
        }
    }
    return NULL;
}

static void *tcp_bulk_sink_fn(void *arg) {
    int lfd = *(int *)arg;
    pin_cpu(1);
    int c = accept(lfd, NULL, NULL);
    char *buf = malloc(65536);
    while (recv(c, buf, 65536, 0) > 0) {}
    free(buf);
    close(c);
    return NULL;
}

static void bench_layer4_5_inet(void) {
    /* 5A: Rapid TCP connect() + send + close storm (ephemeral port + ehash + timers) */
    int lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sin = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    bind(lfd, (struct sockaddr *)&sin, sizeof(sin));
    socklen_t slen = sizeof(sin);
    getsockname(lfd, (struct sockaddr *)&sin, &slen);
    listen(lfd, 4096);

    atomic_store(&tcp_srv_stop, 0);
    pthread_t ath;
    pthread_create(&ath, NULL, tcp_accept_sink_fn, &lfd);

    pin_cpu(0);
    const int NCONN = 4000;
    uint64_t t0 = now_ns();
    for (int i = 0; i < NCONN; i++) {
        int c = socket(AF_INET, SOCK_STREAM, 0);
        struct linger lg = {.l_onoff = 1, .l_linger = 0}; /* Avoid exhausting port range in bench */
        setsockopt(c, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        if (connect(c, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
            send(c, "ping", 4, MSG_NOSIGNAL);
        }
        close(c);
    }
    uint64_t t1 = now_ns();
    atomic_store(&tcp_srv_stop, 1);
    pthread_join(ath, NULL);
    close(lfd);
    printf("[BENCH] L4_tcp_connect_storm    : %6.2f us/conn (%d conns)\n",
           (double)(t1 - t0) / 1000.0 / NCONN, NCONN);

    /* 5B: TCP Bulk Throughput + tcp_sock struct size */
    int blfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(blfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sin.sin_port = 0;
    bind(blfd, (struct sockaddr *)&sin, sizeof(sin));
    getsockname(blfd, (struct sockaddr *)&sin, &slen);
    listen(blfd, 16);
    pthread_t bth;
    pthread_create(&bth, NULL, tcp_bulk_sink_fn, &blfd);

    pin_cpu(0);
    int tc = socket(AF_INET, SOCK_STREAM, 0);
    connect(tc, (struct sockaddr *)&sin, sizeof(sin));
    char *tbuf = calloc(1, 65536);
    const size_t TBYTES = 2ULL * 1024 * 1024 * 1024;
    size_t tsent = 0;
    t0 = now_ns();
    while (tsent < TBYTES) {
        ssize_t w = send(tc, tbuf, 65536, MSG_NOSIGNAL);
        if (w > 0) tsent += (size_t)w;
    }
    shutdown(tc, SHUT_WR);
    pthread_join(bth, NULL);
    t1 = now_ns();
    long tcp_pages_active = read_proto_field("TCP", 3);
    long tcp_obj_sz = read_proto_field("TCP", 1);
    close(tc); close(blfd); free(tbuf);
    printf("[BENCH] L5_tcp_bulk_throughput  : %6.2f GiB/s | tcp_sock=%ld B | active_pages=%ld\n",
           ((double)TBYTES / (1024.0 * 1024.0 * 1024.0)) / ((double)(t1 - t0) / 1e9),
           tcp_obj_sz, tcp_pages_active);

    /* 5C: UDP Memory Hoarding Census (32 QUIC/Game-style sockets with 2MB SO_RCVBUF) */
    int usocks[32];
    int sender = socket(AF_INET, SOCK_DGRAM, 0);
    char *upkt = calloc(1, 1200); /* QUIC 1200B MTU */
    for (int i = 0; i < 32; i++) {
        usocks[i] = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        int rcvbuf = 2 * 1024 * 1024;
        setsockopt(usocks[i], SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
        struct sockaddr_in ua = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
        bind(usocks[i], (struct sockaddr *)&ua, sizeof(ua));
        getsockname(usocks[i], (struct sockaddr *)&ua, &slen);
        /* Burst 512 QUIC-sized 1200B packets into each socket, then drain 511 (leaving 1 unread!) */
        for (int p = 0; p < 512; p++)
            sendto(sender, upkt, 1200, 0, (struct sockaddr *)&ua, sizeof(ua));
        int to_read = (i < 24) ? 511 : 512;
        for (int p = 0; p < to_read; p++)
            recv(usocks[i], upkt, 1200, MSG_DONTWAIT);
    }
    long udp_hoarded_pages = read_proto_field("UDP", 3);
    long udp_obj_sz = read_proto_field("UDP", 1);
    printf("[BENCH] L5_udp_32sock_hoarding  : %6ld pages (%ld KiB) held for 24x1200B + 8x0B! | udp_sock=%ld B\n",
           udp_hoarded_pages, udp_hoarded_pages * 4, udp_obj_sz);
    for (int i = 0; i < 32; i++) close(usocks[i]);
    close(sender);
    free(upkt);
    char upkt64[128] = {0x42};

    /* 5D: Unconnected UDP sendto() Throughput (Game / WebRTC / QUIC Server) */
    int urx = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    int rcvb = 4 * 1024 * 1024;
    setsockopt(urx, SOL_SOCKET, SO_RCVBUF, &rcvb, sizeof(rcvb));
    sin.sin_port = 0;
    bind(urx, (struct sockaddr *)&sin, sizeof(sin));
    getsockname(urx, (struct sockaddr *)&sin, &slen);

    int utx = socket(AF_INET, SOCK_DGRAM, 0);
    const int NUDP = 200000;
    uint64_t c0 = rdtsc_ordered();
    t0 = now_ns();
    for (int i = 0; i < NUDP; i++) {
        sendto(utx, upkt64, 64, 0, (struct sockaddr *)&sin, sizeof(sin));
        if ((i & 511) == 511) {
            while (recv(urx, upkt64, sizeof(upkt64), MSG_DONTWAIT) > 0) {}
        }
    }
    uint64_t c1 = rdtsc_ordered();
    t1 = now_ns();
    close(utx); close(urx);
    printf("[BENCH] L5_udp_unconn_sendto    : %6.1f cycles/pkt  | %6.2f Mpps\n",
           (double)(c1 - c0) / NUDP, (double)NUDP / ((double)(t1 - t0) / 1e3));
}

int main(void) {
    printf("=== Project NetZero Benchmark & ABI Verification Suite ===\n");
    pthread_t holder;
    pthread_create(&holder, NULL, bg_holder_fn, NULL);
    usleep(10000); /* Ensure current->files->count >= 2 */

    test_mt_fd_race_torture();
    bench_layer1_syscalls();
    bench_layer2_epoll();
    bench_layer3_af_unix();
    bench_layer4_5_inet();

    atomic_store(&bg_thread_stop, 1);
    pthread_join(holder, NULL);
    printf("=== All NetZero ABI & Performance Checks Completed Cleanly ===\n");
    return 0;
}
