#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <linux/io_uring.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FAIL(fmt, ...) do { \
    fprintf(stderr, "[FAIL] %s:%d (%s): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stderr); \
    exit(1); \
} while (0)

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) FAIL(fmt, ##__VA_ARGS__); \
} while (0)

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

static int count_open_fds(void) {
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9')
            count++;
    }
    closedir(d);
    return count - 1; /* Exclude the opendir() fd itself */
}

static long read_file_nr_alloc(void) {
    FILE *f = fopen("/proc/sys/fs/file-nr", "r");
    if (!f) return -1;
    long alloc = -1;
    if (fscanf(f, "%ld", &alloc) != 1)
        alloc = -1;
    fclose(f);
    return alloc;
}

static volatile sig_atomic_t g_sigpipe_count = 0;
static void sigpipe_counter_handler(int sig) {
    (void)sig;
    g_sigpipe_count++;
}

static int make_tagged_memfd(const char *tag, size_t pad_bytes) {
    int fd = memfd_create("nz_torture", MFD_CLOEXEC);
    CHECK(fd >= 0, "memfd_create failed: %s", strerror(errno));
    size_t tlen = strlen(tag);
    CHECK(write(fd, tag, tlen) == (ssize_t)tlen, "memfd write tag failed");
    if (pad_bytes > tlen) {
        char zeros[4096];
        memset(zeros, 0xA5, sizeof(zeros));
        size_t rem = pad_bytes - tlen;
        while (rem > 0) {
            size_t chunk = rem > sizeof(zeros) ? sizeof(zeros) : rem;
            CHECK(write(fd, zeros, chunk) == (ssize_t)chunk, "memfd write pad failed");
            rem -= chunk;
        }
    }
    CHECK(lseek(fd, 0, SEEK_SET) == 0, "memfd lseek failed");
    return fd;
}

static void verify_memfd_tag(int fd, const char *expected_tag) {
    char buf[128] = {0};
    size_t tlen = strlen(expected_tag);
    CHECK(tlen < sizeof(buf), "tag too long");
    ssize_t r = pread(fd, buf, tlen, 0);
    CHECK(r == (ssize_t)tlen && memcmp(buf, expected_tag, tlen) == 0,
          "memfd content mismatch: expected '%s', got r=%zd '%.*s'",
          expected_tag, r, (int)(r > 0 ? r : 0), buf);
}

static ssize_t send_with_fds(int sock, const void *data, size_t len,
                             const int *fds, int nfds, int flags) {
    struct iovec iov = { .iov_base = (void *)data, .iov_len = len };
    union {
        char buf[CMSG_SPACE(sizeof(int) * 16)];
        struct cmsghdr align;
    } c;
    memset(&c, 0, sizeof(c));
    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (nfds > 0) {
        CHECK(nfds <= 16, "nfds too large");
        msg.msg_control = c.buf;
        msg.msg_controllen = CMSG_SPACE((size_t)nfds * sizeof(int));
        struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN((size_t)nfds * sizeof(int));
        memcpy(CMSG_DATA(cm), fds, (size_t)nfds * sizeof(int));
    }
    return sendmsg(sock, &msg, flags);
}

static ssize_t recv_with_fds_ctl(int sock, void *data, size_t len,
                                 size_t ctl_len, int *out_fds, int out_cap,
                                 int *out_nfds, int *out_msg_flags, int flags) {
    struct iovec iov = { .iov_base = data, .iov_len = len };
    union {
        char buf[CMSG_SPACE(sizeof(int) * 16)];
        struct cmsghdr align;
    } c;
    memset(&c, 0, sizeof(c));
    CHECK(ctl_len <= sizeof(c.buf), "ctl_len too large");
    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (ctl_len > 0) {
        msg.msg_control = c.buf;
        msg.msg_controllen = ctl_len;
    }
    ssize_t r = recvmsg(sock, &msg, flags);
    if (out_msg_flags)
        *out_msg_flags = msg.msg_flags;
    if (out_nfds)
        *out_nfds = 0;
    if (r >= 0 && ctl_len > 0) {
        int got = 0;
        for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
            if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
                size_t bytes = cm->cmsg_len - CMSG_LEN(0);
                int n = (int)(bytes / sizeof(int));
                for (int i = 0; i < n; i++) {
                    int fd;
                    memcpy(&fd, (char *)CMSG_DATA(cm) + (size_t)i * sizeof(int), sizeof(int));
                    CHECK(out_fds && got < out_cap, "received more fds (%d) than out_cap (%d)!", got + 1, out_cap);
                    out_fds[got++] = fd;
                }
            }
        }
        if (out_nfds)
            *out_nfds = got;
    }
    return r;
}

static ssize_t recv_with_fds(int sock, void *data, size_t len,
                             int *out_fds, int max_fds, int *out_nfds,
                             int *out_msg_flags, int flags) {
    size_t ctl_len = max_fds > 0 ? CMSG_SPACE((size_t)max_fds * sizeof(int)) : 0;
    return recv_with_fds_ctl(sock, data, len, ctl_len, out_fds, 16, out_nfds, out_msg_flags, flags);
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

/* =========================================================================
 * MILESTONE 1 TORTURE A: eventfd ABI, Saturation, EFD_SEMAPHORE, EPOLLET & 4W+4R Race
 * ========================================================================= */
struct efd_et_ctx {
    int efd;
    int ack_efd;
    int iters;
};

static void *efd_et_waiter_fn(void *arg) {
    struct efd_et_ctx *c = arg;
    int ep = epoll_create1(EPOLL_CLOEXEC);
    CHECK(ep >= 0, "epoll_create1 failed");
    struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.fd = c->efd };
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, c->efd, &ev) == 0, "epoll_ctl ADD failed");

    for (int i = 0; i < c->iters; i++) {
        struct epoll_event out;
        int n = epoll_wait(ep, &out, 1, 5000);
        CHECK(n == 1 && (out.events & EPOLLIN),
              "EPOLLET eventfd missed wakeup at iter %d (n=%d events=0x%x)",
              i, n, n == 1 ? out.events : 0);
        uint64_t v = 0;
        ssize_t r = read(c->efd, &v, sizeof(v));
        CHECK(r == 8 && v == 1, "EPOLLET eventfd read got r=%zd v=%" PRIu64, r, v);
        uint64_t ack = 1;
        CHECK(write(c->ack_efd, &ack, sizeof(ack)) == 8, "ack write failed");
    }
    close(ep);
    return NULL;
}

struct efd_sat_ctx {
    int efd;
    atomic_int *done;
};

static void *efd_sat_writer_fn(void *arg) {
    struct efd_sat_ctx *c = arg;
    uint64_t one = 1;
    ssize_t w = write(c->efd, &one, sizeof(one));
    CHECK(w == 8, "blocking eventfd write after saturation drain failed: w=%zd errno=%d", w, errno);
    atomic_store_explicit(c->done, 1, memory_order_release);
    return NULL;
}

struct efd_race_ctx {
    int efd;
    int tid;
    int writes_per_thr;
    atomic_int *writers_done;
    atomic_uint_fast64_t *total_read;
    pthread_barrier_t *bar;
};

static void *efd_race_writer_fn(void *arg) {
    struct efd_race_ctx *c = arg;
    uint64_t inc = (uint64_t)(c->tid + 1);
    pthread_barrier_wait(c->bar);
    for (int i = 0; i < c->writes_per_thr; i++) {
        for (;;) {
            ssize_t w = write(c->efd, &inc, sizeof(inc));
            if (w == 8) break;
            CHECK(w < 0 && (errno == EAGAIN || errno == EINTR),
                  "eventfd race writer unexpected w=%zd errno=%d", w, errno);
            sched_yield();
        }
    }
    atomic_fetch_add_explicit(c->writers_done, 1, memory_order_release);
    return NULL;
}

static void *efd_race_reader_fn(void *arg) {
    struct efd_race_ctx *c = arg;
    int ep = epoll_create1(EPOLL_CLOEXEC);
    CHECK(ep >= 0, "epoll_create1 failed");
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = c->efd };
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, c->efd, &ev) == 0, "epoll_ctl failed");

    pthread_barrier_wait(c->bar);
    uint64_t local_sum = 0;
    for (;;) {
        uint64_t v = 0;
        ssize_t r = read(c->efd, &v, sizeof(v));
        if (r == 8) {
            CHECK(v > 0, "eventfd returned 0 on non-empty read!");
            local_sum += v;
            continue;
        }
        CHECK(r < 0 && (errno == EAGAIN || errno == EINTR),
              "eventfd race reader unexpected r=%zd errno=%d", r, errno);
        if (atomic_load_explicit(c->writers_done, memory_order_acquire) == 4) {
            /* Final drain check after all writers have published completion */
            r = read(c->efd, &v, sizeof(v));
            if (r == 8) {
                local_sum += v;
                continue;
            }
            break;
        }
        struct epoll_event out;
        epoll_wait(ep, &out, 1, 2);
    }
    atomic_fetch_add_explicit(c->total_read, local_sum, memory_order_relaxed);
    close(ep);
    return NULL;
}

struct efd_sem_ctx {
    int efd;
    int ops;
    atomic_uint_fast64_t *total_read;
    pthread_barrier_t *bar;
};

static void *efd_sem_writer_fn(void *arg) {
    struct efd_sem_ctx *c = arg;
    uint64_t one = 1;
    pthread_barrier_wait(c->bar);
    for (int i = 0; i < c->ops; i++) {
        ssize_t w;
        do { w = write(c->efd, &one, sizeof(one)); } while (w < 0 && errno == EINTR);
        CHECK(w == 8, "sem writer write failed: w=%zd errno=%d", w, errno);
    }
    return NULL;
}

static void *efd_sem_reader_fn(void *arg) {
    struct efd_sem_ctx *c = arg;
    uint64_t local = 0;
    pthread_barrier_wait(c->bar);
    for (int i = 0; i < c->ops; i++) {
        uint64_t v = 0;
        ssize_t r;
        do { r = read(c->efd, &v, sizeof(v)); } while (r < 0 && errno == EINTR);
        CHECK(r == 8 && v == 1, "sem reader expected v=1, got r=%zd v=%" PRIu64, r, v);
        local += v;
    }
    atomic_fetch_add_explicit(c->total_read, local, memory_order_relaxed);
    return NULL;
}

static void test_eventfd_abi_and_race(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds failed");

    /* 1. Basic counter accumulation, EFD_NONBLOCK, invalid args, EFAULT */
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    CHECK(efd >= 0, "eventfd failed: %s", strerror(errno));
    CHECK((fcntl(efd, F_GETFL) & O_NONBLOCK) != 0, "EFD_NONBLOCK not set");
    CHECK((fcntl(efd, F_GETFD) & FD_CLOEXEC) != 0, "EFD_CLOEXEC not set");

    uint64_t v = 0;
    CHECK(read(efd, &v, 8) == -1 && errno == EAGAIN, "empty EFD_NONBLOCK read must be EAGAIN");
    CHECK(read(efd, &v, 4) == -1 && errno == EINVAL, "short eventfd read must be EINVAL");
    CHECK(write(efd, &v, 4) == -1 && errno == EINVAL, "short eventfd write must be EINVAL");

    uint64_t bad_max = ULLONG_MAX;
    CHECK(write(efd, &bad_max, 8) == -1 && errno == EINVAL, "write(ULLONG_MAX) must be EINVAL");

    uint64_t zero = 0;
    CHECK(write(efd, &zero, 8) == 8, "write(0) must succeed");
    CHECK(read(efd, &v, 8) == -1 && errno == EAGAIN, "after write(0) read must still be EAGAIN");

    uint64_t a = 5, b = 10, c = 27;
    CHECK(write(efd, &a, 8) == 8, "write(5) failed");
    CHECK(write(efd, &b, 8) == 8, "write(10) failed");
    CHECK(write(efd, &c, 8) == 8, "write(27) failed");

    void *bad_page = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(bad_page != MAP_FAILED, "mmap PROT_NONE failed");
    CHECK(write(efd, bad_page, 8) == -1 && errno == EFAULT, "eventfd write EFAULT failed");
    CHECK(read(efd, &v, 8) == 8 && v == 42, "accumulated count expected 42, got %" PRIu64, v);
    CHECK(read(efd, &v, 8) == -1 && errno == EAGAIN, "post-drain read must be EAGAIN");

    /* Verify eventfd read EFAULT (note: stock Linux consumes counter before copy_to_user) */
    uint64_t ninety_nine = 99;
    CHECK(write(efd, &ninety_nine, 8) == 8, "write(99) failed");
    CHECK(read(efd, bad_page, 8) == -1 && errno == EFAULT, "eventfd read EFAULT failed");
    (void)read(efd, &v, 8); /* Drain if implementation preserved counter on EFAULT */
    munmap(bad_page, 4096);

    /* 2. ULLONG_MAX - 1 saturation & poll POLLIN/POLLOUT checks */
    uint64_t near_max = ULLONG_MAX - 2; /* room for exactly 1 more */
    uint64_t one = 1, two = 2;
    struct pollfd pfd = { .fd = efd, .events = POLLIN | POLLOUT };

    CHECK(write(efd, &near_max, 8) == 8, "write(ULLONG_MAX-2) failed");
    CHECK(poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN) && (pfd.revents & POLLOUT),
          "poll at ULLONG_MAX-2 expected POLLIN|POLLOUT, got 0x%x", pfd.revents);
    CHECK(write(efd, &two, 8) == -1 && errno == EAGAIN, "overflow write(2) must return EAGAIN");
    CHECK(write(efd, &one, 8) == 8, "write(1) to hit ULLONG_MAX-1 failed");
    CHECK(poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN) && !(pfd.revents & POLLOUT),
          "poll at ULLONG_MAX-1 expected POLLIN only, got 0x%x", pfd.revents);
    CHECK(write(efd, &one, 8) == -1 && errno == EAGAIN, "saturated write(1) must return EAGAIN");
    CHECK(read(efd, &v, 8) == 8 && v == ULLONG_MAX - 1,
          "saturated read expected ULLONG_MAX-1, got %" PRIu64, v);
    CHECK(poll(&pfd, 1, 0) == 1 && !(pfd.revents & POLLIN) && (pfd.revents & POLLOUT),
          "poll after drain expected POLLOUT only, got 0x%x", pfd.revents);
    close(efd);

    /* 2b. Blocking write saturation wakeup test */
    int befd = eventfd(0, EFD_CLOEXEC);
    CHECK(befd >= 0, "blocking eventfd failed");
    uint64_t max_ok = ULLONG_MAX - 1;
    CHECK(write(befd, &max_ok, 8) == 8, "write(ULLONG_MAX-1) failed");
    atomic_int sat_done = 0;
    struct efd_sat_ctx sctx = { .efd = befd, .done = &sat_done };
    pthread_t sth;
    CHECK(pthread_create(&sth, NULL, efd_sat_writer_fn, &sctx) == 0, "pthread_create failed");
    usleep(3000);
    CHECK(atomic_load_explicit(&sat_done, memory_order_acquire) == 0,
          "blocking eventfd write did not block at ULLONG_MAX-1!");
    CHECK(read(befd, &v, 8) == 8 && v == ULLONG_MAX - 1, "drain saturated blocking efd failed");
    pthread_join(sth, NULL);
    CHECK(atomic_load(&sat_done) == 1, "sat writer did not complete");
    CHECK(read(befd, &v, 8) == 8 && v == 1, "expected unblocked write value 1, got %" PRIu64, v);
    close(befd);

    /* 3. EFD_SEMAPHORE 1-by-1 decrement */
    int sefd = eventfd(3, EFD_SEMAPHORE | EFD_NONBLOCK | EFD_CLOEXEC);
    CHECK(sefd >= 0, "EFD_SEMAPHORE eventfd failed");
    CHECK(write(sefd, &two, 8) == 8, "sem write(2) failed");
    pfd.fd = sefd;
    pfd.events = POLLIN;
    for (int i = 0; i < 5; i++) {
        CHECK(poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN), "sem poll failed at step %d", i);
        CHECK(read(sefd, &v, 8) == 8 && v == 1, "sem read expected 1 at step %d, got %" PRIu64, i, v);
    }
    CHECK(read(sefd, &v, 8) == -1 && errno == EAGAIN, "sem 6th read must be EAGAIN");
    CHECK(poll(&pfd, 1, 0) == 0, "sem empty poll must be 0");
    close(sefd);

    /* 4. poll() & epoll(EPOLLET) wakeup accuracy (single-thread + cross-thread) */
    int et_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int ack_efd = eventfd(0, EFD_CLOEXEC);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    CHECK(et_efd >= 0 && ack_efd >= 0 && ep >= 0, "et setup failed");
    struct epoll_event ev = { .events = EPOLLIN | EPOLLET, .data.fd = et_efd };
    struct epoll_event out;
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, et_efd, &ev) == 0, "epoll_ctl failed");
    CHECK(epoll_wait(ep, &out, 1, 0) == 0, "empty EPOLLET must be 0");
    CHECK(write(et_efd, &one, 8) == 8, "write(1) failed");
    CHECK(epoll_wait(ep, &out, 1, 0) == 1 && (out.events & EPOLLIN), "EPOLLET first edge missed");
    CHECK(epoll_wait(ep, &out, 1, 0) == 0, "EPOLLET fired twice without new write!");
    CHECK(write(et_efd, &one, 8) == 8, "second write(1) failed");
    CHECK(epoll_wait(ep, &out, 1, 0) == 1, "EPOLLET re-arm on second write missed");
    CHECK(read(et_efd, &v, 8) == 8 && v == 2, "expected 2, got %" PRIu64, v);
    CHECK(epoll_wait(ep, &out, 1, 0) == 0, "post-read EPOLLET must be 0");
    close(ep);

    struct efd_et_ctx etctx = { .efd = et_efd, .ack_efd = ack_efd, .iters = 200 };
    pthread_t et_th;
    CHECK(pthread_create(&et_th, NULL, efd_et_waiter_fn, &etctx) == 0, "pthread_create failed");
    for (int i = 0; i < 200; i++) {
        CHECK(write(et_efd, &one, 8) == 8, "cross-thread write failed");
        uint64_t ack = 0;
        CHECK(read(ack_efd, &ack, 8) == 8 && ack == 1, "cross-thread ack failed");
    }
    pthread_join(et_th, NULL);
    close(et_efd);
    close(ack_efd);

    /* 5. 4-writer + 4-reader concurrent race verifying exact sum conservation */
    int refd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    CHECK(refd >= 0, "race eventfd failed");
    const int WRITES_PER_THR = 25000;
    const uint64_t EXPECTED_SUM = (uint64_t)WRITES_PER_THR * (1 + 2 + 3 + 4);
    atomic_int writers_done = 0;
    atomic_uint_fast64_t total_read = 0;
    pthread_barrier_t bar;
    pthread_barrier_init(&bar, NULL, 8);
    pthread_t wth[4], rth[4];
    struct efd_race_ctx wctx[4], rctx[4];
    for (int i = 0; i < 4; i++) {
        wctx[i] = (struct efd_race_ctx){
            .efd = refd, .tid = i, .writes_per_thr = WRITES_PER_THR,
            .writers_done = &writers_done, .total_read = &total_read, .bar = &bar
        };
        rctx[i] = wctx[i];
        CHECK(pthread_create(&wth[i], NULL, efd_race_writer_fn, &wctx[i]) == 0, "create writer");
        CHECK(pthread_create(&rth[i], NULL, efd_race_reader_fn, &rctx[i]) == 0, "create reader");
    }
    for (int i = 0; i < 4; i++) pthread_join(wth[i], NULL);
    for (int i = 0; i < 4; i++) pthread_join(rth[i], NULL);
    pthread_barrier_destroy(&bar);
    uint64_t got_sum = atomic_load(&total_read);
    CHECK(got_sum == EXPECTED_SUM,
          "eventfd 4W+4R sum conservation VIOLATED: expected %" PRIu64 ", got %" PRIu64,
          EXPECTED_SUM, got_sum);
    CHECK(read(refd, &v, 8) == -1 && errno == EAGAIN, "residual count left in eventfd!");
    close(refd);

    /* 5b. 4-writer + 4-reader blocking EFD_SEMAPHORE race */
    int sem_efd = eventfd(0, EFD_SEMAPHORE | EFD_CLOEXEC);
    CHECK(sem_efd >= 0, "sem race eventfd failed");
    const int SEM_OPS = 10000;
    atomic_uint_fast64_t sem_read = 0;
    pthread_barrier_init(&bar, NULL, 8);
    struct efd_sem_ctx sem_ctx = {
        .efd = sem_efd, .ops = SEM_OPS, .total_read = &sem_read, .bar = &bar
    };
    for (int i = 0; i < 4; i++) {
        CHECK(pthread_create(&wth[i], NULL, efd_sem_writer_fn, &sem_ctx) == 0, "create sem writer");
        CHECK(pthread_create(&rth[i], NULL, efd_sem_reader_fn, &sem_ctx) == 0, "create sem reader");
    }
    for (int i = 0; i < 4; i++) pthread_join(wth[i], NULL);
    for (int i = 0; i < 4; i++) pthread_join(rth[i], NULL);
    pthread_barrier_destroy(&bar);
    CHECK(atomic_load(&sem_read) == (uint64_t)(4 * SEM_OPS),
          "EFD_SEMAPHORE 4W+4R count mismatch: %" PRIu64, (uint64_t)atomic_load(&sem_read));
    pfd.fd = sem_efd; pfd.events = POLLIN;
    CHECK(poll(&pfd, 1, 0) == 0, "sem_efd not empty after 4W+4R");
    close(sem_efd);

    CHECK(count_open_fds() == fd_base, "FD leak in test_eventfd_abi_and_race: %d -> %d",
          fd_base, count_open_fds());
    printf("[PASS] M1 Torture A: eventfd ABI, ULLONG_MAX-1 sat, EFD_SEMAPHORE, EPOLLET & 4W+4R race (sum=%" PRIu64 " exact)\n",
           got_sum);
}

/* =========================================================================
 * MILESTONE 1 TORTURE B: pipe2, timerfd & 80-pidfd epoll/poll ABI
 * ========================================================================= */
struct pipe_stream_ctx {
    int wfd;
    size_t total_bytes;
};

static void *pipe_stream_writer_fn(void *arg) {
    struct pipe_stream_ctx *c = arg;
    static const size_t chunks[] = { 1, 7, 64, 513, 4096, 12288, 37, 4095 };
    uint8_t buf[12288];
    size_t sent = 0;
    size_t step = 0;
    while (sent < c->total_bytes) {
        size_t want = chunks[step++ & 7];
        if (want > c->total_bytes - sent) want = c->total_bytes - sent;
        for (size_t i = 0; i < want; i++)
            buf[i] = (uint8_t)(((sent + i) * 131U + 17U) & 0xffU);
        size_t off = 0;
        while (off < want) {
            ssize_t w = write(c->wfd, buf + off, want - off);
            if (w < 0 && errno == EINTR) continue;
            CHECK(w > 0, "pipe stream writer failed: w=%zd errno=%d", w, errno);
            off += (size_t)w;
        }
        sent += want;
    }
    close(c->wfd);
    return NULL;
}

static void test_pipe_and_timerfd_pidfd_abi(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds failed");

    /* --- Part 1: pipe2() small / multi-page / O_NONBLOCK / O_DIRECT / EFAULT / EPIPE / poll --- */
    int p[2];
    CHECK(pipe2(p, O_NONBLOCK | O_CLOEXEC) == 0, "pipe2 failed: %s", strerror(errno));
    CHECK((fcntl(p[0], F_GETFL) & O_NONBLOCK) != 0, "pipe read end missing O_NONBLOCK");
    CHECK((fcntl(p[1], F_GETFL) & O_NONBLOCK) != 0, "pipe write end missing O_NONBLOCK");
    CHECK((fcntl(p[0], F_GETFD) & FD_CLOEXEC) != 0, "pipe read end missing FD_CLOEXEC");
    CHECK((fcntl(p[1], F_GETFD) & FD_CLOEXEC) != 0, "pipe write end missing FD_CLOEXEC");

    char small_rx[64];
    CHECK(read(p[0], small_rx, sizeof(small_rx)) == -1 && errno == EAGAIN,
          "empty nonblock pipe read must be EAGAIN");
    struct pollfd pfds[2] = {
        { .fd = p[0], .events = POLLIN },
        { .fd = p[1], .events = POLLOUT }
    };
    CHECK(poll(pfds, 2, 0) == 1 && pfds[0].revents == 0 && (pfds[1].revents & POLLOUT),
          "empty pipe poll mismatch: r=0x%x w=0x%x", pfds[0].revents, pfds[1].revents);

    /* Small writes + EFAULT non-destructive check + reads */
    CHECK(write(p[1], "hello", 5) == 5, "pipe write hello failed");
    CHECK(write(p[1], " world", 6) == 6, "pipe write world failed");
    void *bad_page = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(bad_page != MAP_FAILED, "mmap PROT_NONE failed");
    CHECK(write(p[1], bad_page, 16) == -1 && errno == EFAULT, "pipe write EFAULT failed");
    CHECK(read(p[0], bad_page, 5) == -1 && errno == EFAULT, "pipe read EFAULT failed");
    munmap(bad_page, 4096);

    CHECK(read(p[0], small_rx, 5) == 5 && memcmp(small_rx, "hello", 5) == 0,
          "pipe read after EFAULT corrupted data!");
    CHECK(read(p[0], small_rx, 6) == 6 && memcmp(small_rx, " world", 6) == 0,
          "pipe second small read failed");
    CHECK(read(p[0], small_rx, 1) == -1 && errno == EAGAIN, "pipe should be empty");

    /* Multi-page fill until EAGAIN + full drain verification */
    uint8_t *wchunk = malloc(12288);
    uint8_t *rchunk = malloc(16384);
    CHECK(wchunk && rchunk, "malloc failed");
    size_t written_total = 0;
    for (;;) {
        for (size_t i = 0; i < 12288; i++)
            wchunk[i] = (uint8_t)(((written_total + i) * 31U + 7U) & 0xffU);
        ssize_t w = write(p[1], wchunk, 12288);
        if (w > 0) {
            written_total += (size_t)w;
        } else {
            CHECK(errno == EAGAIN, "pipe fill unexpected errno=%d", errno);
            break;
        }
    }
    CHECK(written_total >= 4096, "pipe capacity unexpectedly tiny: %zu", written_total);
    CHECK(poll(pfds, 2, 0) == 1 && (pfds[0].revents & POLLIN) && !(pfds[1].revents & POLLOUT),
          "full pipe poll mismatch: r=0x%x w=0x%x", pfds[0].revents, pfds[1].revents);

    size_t read_total = 0;
    while (read_total < written_total) {
        ssize_t r = read(p[0], rchunk, 16384);
        CHECK(r > 0, "pipe drain failed at %zu/%zu: errno=%d", read_total, written_total, errno);
        for (ssize_t i = 0; i < r; i++) {
            uint8_t exp = (uint8_t)(((read_total + (size_t)i) * 31U + 7U) & 0xffU);
            CHECK(rchunk[i] == exp, "pipe multi-page byte mismatch at %zu", read_total + (size_t)i);
        }
        read_total += (size_t)r;
    }
    CHECK(read(p[0], rchunk, 16) == -1 && errno == EAGAIN, "pipe not empty after drain");
    CHECK(poll(pfds, 2, 0) == 1 && pfds[0].revents == 0 && (pfds[1].revents & POLLOUT),
          "post-drain pipe poll mismatch");
    close(p[0]);
    close(p[1]);
    free(wchunk);
    free(rchunk);

    /* Cross-thread 2 MiB mixed-size pipe stream with epoll(EPOLLET) */
    int sp[2];
    CHECK(pipe2(sp, O_CLOEXEC) == 0, "stream pipe2 failed");
    CHECK(fcntl(sp[0], F_SETFL, O_NONBLOCK) == 0, "set nonblock failed");
    int pep = epoll_create1(EPOLL_CLOEXEC);
    CHECK(pep >= 0, "epoll_create1 failed");
    struct epoll_event pev = { .events = EPOLLIN | EPOLLET, .data.fd = sp[0] };
    CHECK(epoll_ctl(pep, EPOLL_CTL_ADD, sp[0], &pev) == 0, "epoll_ctl pipe failed");
    const size_t STREAM_BYTES = 2 * 1024 * 1024;
    struct pipe_stream_ctx psctx = { .wfd = sp[1], .total_bytes = STREAM_BYTES };
    pthread_t pth;
    CHECK(pthread_create(&pth, NULL, pipe_stream_writer_fn, &psctx) == 0, "pthread_create failed");
    uint8_t pbuf[8192];
    size_t got_stream = 0;
    while (got_stream < STREAM_BYTES) {
        struct epoll_event out;
        int n = epoll_wait(pep, &out, 1, 5000);
        CHECK(n == 1, "pipe EPOLLET stalled at %zu/%zu", got_stream, STREAM_BYTES);
        for (;;) {
            ssize_t r = read(sp[0], pbuf, sizeof(pbuf));
            if (r > 0) {
                for (ssize_t i = 0; i < r; i++) {
                    uint8_t exp = (uint8_t)(((got_stream + (size_t)i) * 131U + 17U) & 0xffU);
                    CHECK(pbuf[i] == exp, "pipe stream mismatch at %zu", got_stream + (size_t)i);
                }
                got_stream += (size_t)r;
            } else if (r == 0) {
                break;
            } else {
                CHECK(errno == EAGAIN || errno == EINTR, "pipe stream read err=%d", errno);
                if (errno == EAGAIN) break;
            }
        }
    }
    pthread_join(pth, NULL);
    close(pep);
    close(sp[0]);

    /* Packet mode (O_DIRECT) pipe fallback preservation */
    int dp[2];
    CHECK(pipe2(dp, O_DIRECT | O_NONBLOCK | O_CLOEXEC) == 0, "O_DIRECT pipe2 failed");
    CHECK(write(dp[1], "AAA", 3) == 3 && write(dp[1], "BBB", 3) == 3, "O_DIRECT writes failed");
    CHECK(read(dp[0], small_rx, 16) == 3 && memcmp(small_rx, "AAA", 3) == 0,
          "O_DIRECT packet boundary broken on packet 1!");
    CHECK(read(dp[0], small_rx, 16) == 3 && memcmp(small_rx, "BBB", 3) == 0,
          "O_DIRECT packet boundary broken on packet 2!");
    close(dp[0]);
    close(dp[1]);

    /* EPIPE + SIGPIPE + POLLERR + POLLHUP */
    struct sigaction sa_new = {0}, sa_old = {0};
    sa_new.sa_handler = sigpipe_counter_handler;
    CHECK(sigaction(SIGPIPE, &sa_new, &sa_old) == 0, "sigaction SIGPIPE failed");
    g_sigpipe_count = 0;

    CHECK(pipe2(p, O_NONBLOCK | O_CLOEXEC) == 0, "epipe pipe2 failed");
    close(p[0]);
    struct pollfd epfd = { .fd = p[1], .events = POLLOUT };
    CHECK(poll(&epfd, 1, 0) == 1 && (epfd.revents & POLLERR),
          "broken pipe poll expected POLLERR, got 0x%x", epfd.revents);
    CHECK(write(p[1], "x", 1) == -1 && errno == EPIPE, "broken pipe write must return EPIPE");
    CHECK(g_sigpipe_count == 1, "broken pipe write must deliver SIGPIPE (got %d)", (int)g_sigpipe_count);
    close(p[1]);
    CHECK(sigaction(SIGPIPE, &sa_old, NULL) == 0, "restore SIGPIPE failed");

    CHECK(pipe2(p, O_NONBLOCK | O_CLOEXEC) == 0, "hup pipe2 failed");
    CHECK(write(p[1], "EOF!", 4) == 4, "write EOF! failed");
    close(p[1]);
    epfd.fd = p[0]; epfd.events = POLLIN;
    CHECK(poll(&epfd, 1, 0) == 1 && (epfd.revents & POLLIN) && (epfd.revents & POLLHUP),
          "closed-writer pipe expected POLLIN|POLLHUP, got 0x%x", epfd.revents);
    CHECK(read(p[0], small_rx, 4) == 4 && memcmp(small_rx, "EOF!", 4) == 0, "read EOF! failed");
    CHECK(read(p[0], small_rx, 4) == 0, "expected EOF (0)");
    CHECK(poll(&epfd, 1, 0) == 1 && !(epfd.revents & POLLIN) && (epfd.revents & POLLHUP),
          "drained closed-writer pipe expected POLLHUP only, got 0x%x", epfd.revents);
    close(p[0]);

    /* --- Part 2: timerfd_create() + epoll/poll firing & read() tick count --- */
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    CHECK(tfd >= 0, "timerfd_create failed: %s", strerror(errno));
    uint64_t ticks = 0;
    CHECK(read(tfd, &ticks, 8) == -1 && errno == EAGAIN, "unarmed timerfd read must be EAGAIN");
    struct pollfd tpfd = { .fd = tfd, .events = POLLIN };
    CHECK(poll(&tpfd, 1, 0) == 0, "unarmed timerfd poll must be 0");

    int tep = epoll_create1(EPOLL_CLOEXEC);
    CHECK(tep >= 0, "timerfd epoll_create1 failed");
    struct epoll_event tev = { .events = EPOLLIN | EPOLLET, .data.fd = tfd };
    CHECK(epoll_ctl(tep, EPOLL_CTL_ADD, tfd, &tev) == 0, "epoll_ctl timerfd failed");

    /* One-shot 5ms */
    struct itimerspec its = {0};
    its.it_value.tv_nsec = 5000000L;
    CHECK(timerfd_settime(tfd, 0, &its, NULL) == 0, "timerfd_settime oneshot failed");
    struct epoll_event tout;
    CHECK(epoll_wait(tep, &tout, 1, 2000) == 1 && (tout.events & EPOLLIN),
          "timerfd oneshot epoll_wait failed");
    CHECK(poll(&tpfd, 1, 0) == 1 && (tpfd.revents & POLLIN), "timerfd oneshot poll failed");
    CHECK(read(tfd, &ticks, 8) == 8 && ticks == 1, "timerfd oneshot expected 1 tick, got %" PRIu64, ticks);
    CHECK(read(tfd, &ticks, 8) == -1 && errno == EAGAIN, "timerfd post-read must be EAGAIN");
    CHECK(poll(&tpfd, 1, 0) == 0, "timerfd post-read poll must be 0");

    /* Periodic 2ms, sleep 16ms -> expect >= 3 accumulated ticks */
    its.it_value.tv_nsec = 2000000L;
    its.it_interval.tv_nsec = 2000000L;
    CHECK(timerfd_settime(tfd, 0, &its, NULL) == 0, "timerfd_settime periodic failed");
    usleep(16000);
    CHECK(epoll_wait(tep, &tout, 1, 1000) == 1 && (tout.events & EPOLLIN),
          "timerfd periodic epoll_wait failed");
    CHECK(poll(&tpfd, 1, 0) == 1 && (tpfd.revents & POLLIN), "timerfd periodic poll failed");
    CHECK(read(tfd, &ticks, 8) == 8 && ticks >= 3,
          "timerfd periodic expected >=3 ticks, got %" PRIu64, ticks);
    CHECK(poll(&tpfd, 1, 2000) == 1 && (tpfd.revents & POLLIN), "timerfd blocking poll failed");
    CHECK(read(tfd, &ticks, 8) == 8 && ticks >= 1, "timerfd next tick read failed");

    memset(&its, 0, sizeof(its));
    CHECK(timerfd_settime(tfd, 0, &its, NULL) == 0, "timerfd disarm failed");
    (void)read(tfd, &ticks, 8);
    CHECK(poll(&tpfd, 1, 0) == 0, "disarmed timerfd poll must be 0");
    close(tep);
    close(tfd);

    /* --- Part 3: pidfd_open() 80-child epoll/poll readiness (dbus-broker pattern) --- */
    const int N_PIDFDS = 80;
    int gate_a[2], gate_b[2];
    CHECK(pipe2(gate_a, O_CLOEXEC) == 0 && pipe2(gate_b, O_CLOEXEC) == 0, "gate pipe2 failed");
    pid_t pids[80];
    for (int i = 0; i < N_PIDFDS; i++) {
        pid_t pid = fork();
        CHECK(pid >= 0, "fork %d failed: %s", i, strerror(errno));
        if (pid == 0) {
            /* Async-signal-safe only! (multithreaded process) */
            close(gate_a[1]);
            close(gate_b[1]);
            int gfd = (i < 40) ? gate_a[0] : gate_b[0];
            close((i < 40) ? gate_b[0] : gate_a[0]);
            char b;
            (void)read(gfd, &b, 1);
            close(gfd);
            _exit(0);
        }
        pids[i] = pid;
    }
    close(gate_a[0]);
    close(gate_b[0]);

    int pidfds[80];
    int pep_pid = epoll_create1(EPOLL_CLOEXEC);
    CHECK(pep_pid >= 0, "pidfd epoll_create1 failed");
    for (int i = 0; i < N_PIDFDS; i++) {
        pidfds[i] = (int)syscall(SYS_pidfd_open, pids[i], 0U);
        CHECK(pidfds[i] >= 0, "pidfd_open(%d) failed: %s", (int)pids[i], strerror(errno));
        struct epoll_event ev = { .events = EPOLLIN, .data.u32 = (uint32_t)i };
        CHECK(epoll_ctl(pep_pid, EPOLL_CTL_ADD, pidfds[i], &ev) == 0, "epoll_ctl pidfd %d failed", i);
    }

    struct epoll_event pevs[80];
    CHECK(epoll_wait(pep_pid, pevs, 80, 0) == 0, "pidfds fired before children exited!");
    struct pollfd ppfd = { .fd = pidfds[0], .events = POLLIN };
    CHECK(poll(&ppfd, 1, 0) == 0, "pidfd[0] poll fired before exit!");

    /* Release Wave A (children 0..39) */
    close(gate_a[1]);
    uint8_t seen[80] = {0};
    int wave_a_cnt = 0;
    while (wave_a_cnt < 40) {
        int n = epoll_wait(pep_pid, pevs, 80, 5000);
        CHECK(n > 0, "pidfd wave A epoll_wait timed out at %d/40", wave_a_cnt);
        for (int k = 0; k < n; k++) {
            uint32_t idx = pevs[k].data.u32;
            CHECK(idx < 40, "wave B pidfd %u fired prematurely!", idx);
            CHECK(!seen[idx], "duplicate pidfd %u", idx);
            seen[idx] = 1;
            wave_a_cnt++;
            CHECK(epoll_ctl(pep_pid, EPOLL_CTL_DEL, pidfds[idx], NULL) == 0, "epoll_ctl DEL pidfd");
        }
    }
    CHECK(poll(&ppfd, 1, 0) == 1 && (ppfd.revents & POLLIN), "exited pidfd[0] poll must be POLLIN");

    /* Release Wave B (children 40..79) */
    close(gate_b[1]);
    int wave_b_cnt = 0;
    while (wave_b_cnt < 40) {
        int n = epoll_wait(pep_pid, pevs, 80, 5000);
        CHECK(n > 0, "pidfd wave B epoll_wait timed out at %d/40", wave_b_cnt);
        for (int k = 0; k < n; k++) {
            uint32_t idx = pevs[k].data.u32;
            CHECK(idx >= 40 && idx < 80, "unexpected pidfd idx %u in wave B", idx);
            CHECK(!seen[idx], "duplicate pidfd %u", idx);
            seen[idx] = 1;
            wave_b_cnt++;
            CHECK(epoll_ctl(pep_pid, EPOLL_CTL_DEL, pidfds[idx], NULL) == 0, "epoll_ctl DEL pidfd");
        }
    }

    for (int i = 0; i < N_PIDFDS; i++) {
        int status = 0;
        CHECK(waitpid(pids[i], &status, 0) == pids[i], "waitpid %d failed", i);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child %d bad status 0x%x", i, status);
        close(pidfds[i]);
    }
    close(pep_pid);

    CHECK(count_open_fds() == fd_base, "FD leak in test_pipe_and_timerfd_pidfd_abi: %d -> %d",
          fd_base, count_open_fds());
    printf("[PASS] M1 Torture B: pipe2 (small/multi-page/O_DIRECT/EPIPE/SIGPIPE) + timerfd + 80-pidfd epoll/poll ABI\n");
}

/* =========================================================================
 * MILESTONE 1 TORTURE C: Exhaustive 5-Part SCM_RIGHTS Sideband Ring Torture
 * ========================================================================= */
static void test_scm_rights_sideband_torture(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds failed");

    /* --- Part 1: 1..4 non-socket fds (memfd + eventfd + pipe) interleaved with plain writes --- */
    {
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0,
              "socketpair failed");

        /* 1A: Exact "AAA" + [memfd1, memfd2]+"BBB" + "CCC" boundary alignment */
        int m1 = make_tagged_memfd("SIDEBAND_MEMFD_1", 0);
        int m2 = make_tagged_memfd("SIDEBAND_MEMFD_2", 0);
        int fds2[2] = { m1, m2 };
        CHECK(write(sv[0], "AAA", 3) == 3, "write AAA failed");
        CHECK(send_with_fds(sv[0], "BBB", 3, fds2, 2, 0) == 3, "send BBB+2fds failed");
        CHECK(write(sv[0], "CCC", 3) == 3, "write CCC failed");

        char rb[64] = {0};
        int rfds[16], rnfds = 0, mflags = 0;
        /* Receive exact 3 bytes "AAA" -> MUST NOT prematurely deliver fds */
        ssize_t r = recv_with_fds(sv[1], rb, 3, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
        CHECK(r == 3 && memcmp(rb, "AAA", 3) == 0 && rnfds == 0,
              "AAA boundary violated: r=%zd data='%.*s' nfds=%d", r, (int)(r > 0 ? r : 0), rb, rnfds);

        /* Receive exact 3 bytes "BBB" -> must deliver exact [m1, m2] */
        r = recv_with_fds(sv[1], rb, 3, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
        CHECK(r == 3 && memcmp(rb, "BBB", 3) == 0 && rnfds == 2,
              "BBB delivery broken: r=%zd data='%.*s' nfds=%d", r, (int)(r > 0 ? r : 0), rb, rnfds);
        verify_memfd_tag(rfds[0], "SIDEBAND_MEMFD_1");
        verify_memfd_tag(rfds[1], "SIDEBAND_MEMFD_2");
        close(rfds[0]);
        close(rfds[1]);

        /* Receive "CCC" -> 0 fds */
        r = recv_with_fds(sv[1], rb, sizeof(rb), rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
        CHECK(r == 3 && memcmp(rb, "CCC", 3) == 0 && rnfds == 0,
              "CCC delivery broken: r=%zd data='%.*s' nfds=%d", r, (int)(r > 0 ? r : 0), rb, rnfds);

        /* 1A-large: Large-buffer recvmsg(64) across "AAA" + [m1,m2]+"BBB" + "CCC":
         * Both stock Linux ("AAABBB"+fds, then "CCC") and strict boundary ("AAA", "BBB"+fds, "CCC")
         * are valid POSIX SOCK_STREAM; fds MUST arrive only with a read covering byte offset 3! */
        CHECK(write(sv[0], "AAA", 3) == 3, "write AAA2 failed");
        CHECK(send_with_fds(sv[0], "BBB", 3, fds2, 2, 0) == 3, "send BBB2+2fds failed");
        CHECK(write(sv[0], "CCC", 3) == 3, "write CCC2 failed");
        {
            char acc[16] = {0};
            size_t got = 0;
            int got_fds_total = 0;
            while (got < 9) {
                r = recv_with_fds(sv[1], acc + got, 64 > (sizeof(acc) - 1 - got) ? (sizeof(acc) - 1 - got) : 64,
                                  rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
                CHECK(r > 0, "large-buf recv failed at got=%zu", got);
                size_t start_off = got;
                size_t end_off = got + (size_t)r;
                if (rnfds > 0) {
                    CHECK(start_off <= 3 && end_off > 3,
                          "fds delivered on wrong byte range [%zu, %zu)!", start_off, end_off);
                    CHECK(rnfds == 2, "expected 2 fds, got %d", rnfds);
                    verify_memfd_tag(rfds[0], "SIDEBAND_MEMFD_1");
                    verify_memfd_tag(rfds[1], "SIDEBAND_MEMFD_2");
                    close(rfds[0]);
                    close(rfds[1]);
                    got_fds_total += rnfds;
                }
                got = end_off;
            }
            CHECK(got == 9 && memcmp(acc, "AAABBBCCC", 9) == 0 && got_fds_total == 2,
                  "large-buf stream broken: got=%zu '%s' fds=%d", got, acc, got_fds_total);
        }
        close(m1);
        close(m2);

        /* 1B: All counts 1, 2, 3, 4 mixing memfd + eventfd + pipe */
        for (int cnt = 1; cnt <= 4; cnt++) {
            int mfd_a = make_tagged_memfd("MIX_MFD_A", 0);
            int mfd_b = make_tagged_memfd("MIX_MFD_B", 0);
            int efd = eventfd(777, EFD_NONBLOCK | EFD_CLOEXEC);
            int pp[2];
            CHECK(efd >= 0 && pipe2(pp, O_NONBLOCK | O_CLOEXEC) == 0, "mix fd create failed");
            CHECK(write(pp[1], "PIPEOK", 6) == 6, "pipe write failed");
            int send_list[4] = { mfd_a, efd, pp[0], mfd_b };

            CHECK(write(sv[0], "PRE", 3) == 3, "write PRE");
            CHECK(send_with_fds(sv[0], "MID", 3, send_list, cnt, 0) == 3, "send MID+%d fds", cnt);
            CHECK(write(sv[0], "END", 3) == 3, "write END");

            r = recv_with_fds(sv[1], rb, 3, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 3 && memcmp(rb, "PRE", 3) == 0 && rnfds == 0, "PRE cnt=%d failed", cnt);

            r = recv_with_fds(sv[1], rb, 3, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 3 && memcmp(rb, "MID", 3) == 0 && rnfds == cnt,
                  "MID cnt=%d failed: r=%zd rnfds=%d", cnt, r, rnfds);
            verify_memfd_tag(rfds[0], "MIX_MFD_A");
            if (cnt >= 2) {
                uint64_t ev = 0;
                CHECK(read(rfds[1], &ev, 8) == 8 && ev == 777, "received eventfd mismatch: %" PRIu64, ev);
            }
            if (cnt >= 3) {
                char pb[8] = {0};
                CHECK(read(rfds[2], pb, 6) == 6 && memcmp(pb, "PIPEOK", 6) == 0, "received pipe mismatch");
            }
            if (cnt >= 4) {
                verify_memfd_tag(rfds[3], "MIX_MFD_B");
            }
            for (int i = 0; i < rnfds; i++) close(rfds[i]);

            r = recv_with_fds(sv[1], rb, 32, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 3 && memcmp(rb, "END", 3) == 0 && rnfds == 0, "END cnt=%d failed", cnt);

            close(mfd_a); close(mfd_b); close(efd); close(pp[0]); close(pp[1]);
        }

        /* 1C: Back-to-back sideband messages + >8 slot spill (10 consecutive messages queued!) */
        int mfds10[10];
        char tag[32];
        for (int i = 0; i < 10; i++) {
            snprintf(tag, sizeof(tag), "QUEUE_SLOT_%02d", i);
            mfds10[i] = make_tagged_memfd(tag, 0);
            char pl[4] = { 'S', (char)('0' + i), '!', '\0' };
            CHECK(send_with_fds(sv[0], pl, 3, &mfds10[i], 1, 0) == 3, "queue send %d failed", i);
        }
        for (int i = 0; i < 10; i++) {
            snprintf(tag, sizeof(tag), "QUEUE_SLOT_%02d", i);
            char exp_pl[3] = { 'S', (char)('0' + i), '!' };
            r = recv_with_fds(sv[1], rb, 64, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 3 && memcmp(rb, exp_pl, 3) == 0 && rnfds == 1,
                  "slot %d boundary/FIFO broken: r=%zd rnfds=%d", i, r, rnfds);
            verify_memfd_tag(rfds[0], tag);
            close(rfds[0]);
            close(mfds10[i]);
        }

        /* 1D: Ring wraparound (> 300 KiB) with interleaved sideband fds */
        uint8_t bulk[4096];
        memset(bulk, 0x5C, sizeof(bulk));
        uint8_t drain[4096];
        for (int round = 0; round < 80; round++) {
            /* 80 * 4096 = 327,680 B > 256 KiB ring! */
            CHECK(write(sv[0], bulk, 4096) == 4096, "wrap bulk write %d", round);
            snprintf(tag, sizeof(tag), "WRAP_MFD_%03d", round);
            int wm = make_tagged_memfd(tag, 0);
            CHECK(send_with_fds(sv[0], "WRP", 3, &wm, 1, 0) == 3, "wrap fd send %d", round);
            size_t got = 0;
            while (got < 4096) {
                r = recv_with_fds(sv[1], drain + got, 4096 - got, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
                CHECK(r > 0 && rnfds == 0, "wrap plain drain %d got premature fds=%d", round, rnfds);
                got += (size_t)r;
            }
            CHECK(memcmp(drain, bulk, 4096) == 0, "wrap plain data mismatch %d", round);
            r = recv_with_fds(sv[1], rb, 64, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 3 && memcmp(rb, "WRP", 3) == 0 && rnfds == 1,
                  "wrap fd recv %d failed: r=%zd rnfds=%d", round, r, rnfds);
            verify_memfd_tag(rfds[0], tag);
            close(rfds[0]);
            close(wm);
        }
        close(sv[0]);
        close(sv[1]);
    }

    /* --- Part 2: Mixed AF_UNIX socket fd + memfd + cyclic socket pair GC --- */
    {
        int sv[2], peer[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == 0, "sv socketpair");
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, peer) == 0, "peer socketpair");
        int mfd = make_tagged_memfd("MIXED_SOCK_MFD", 0);
        int mf[2] = { peer[0], mfd };
        CHECK(send_with_fds(sv[0], "MIX", 3, mf, 2, 0) == 3, "mixed send failed");

        char rb[16] = {0};
        int rfds[16], rnfds = 0, mflags = 0;
        ssize_t r = recv_with_fds(sv[1], rb, sizeof(rb), rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
        CHECK(r == 3 && memcmp(rb, "MIX", 3) == 0 && rnfds == 2,
              "mixed recv failed: r=%zd rnfds=%d", r, rnfds);
        CHECK(write(rfds[0], "PING", 4) == 4, "write to passed AF_UNIX fd failed");
        char ping[4] = {0};
        CHECK(read(peer[1], ping, 4) == 4 && memcmp(ping, "PING", 4) == 0,
              "passed AF_UNIX fd broken");
        verify_memfd_tag(rfds[1], "MIXED_SOCK_MFD");
        close(rfds[0]); close(rfds[1]);
        close(mfd); close(peer[0]); close(peer[1]);
        close(sv[0]); close(sv[1]);

        /* Cyclic GC test: 64 cycles (256 AF_UNIX sockets + 64x32KiB memfds) */
        long unix_before = read_proto_field("UNIX-STREAM", 2);
        if (unix_before < 0) unix_before = read_proto_field("UNIX", 2);
        for (int k = 0; k < 64; k++) {
            int c1[2], c2[2];
            CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, c1) == 0, "gc c1");
            CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, c2) == 0, "gc c2");
            int cm = make_tagged_memfd("GC_CYCLE_MFD", 32768);
            int f1[2] = { c1[0], cm };
            int f2[2] = { c2[0], cm };
            CHECK(send_with_fds(c2[1], "C1", 2, f1, 2, 0) == 2, "gc send 1");
            CHECK(send_with_fds(c1[1], "C2", 2, f2, 2, 0) == 2, "gc send 2");
            close(cm);
            close(c1[0]); close(c1[1]);
            close(c2[0]); close(c2[1]);
        }
        int trig[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, trig) == 0) {
            close(trig[0]); close(trig[1]);
        }
        long unix_after = -1;
        for (int w = 0; w < 50; w++) {
            usleep(10000);
            unix_after = read_proto_field("UNIX-STREAM", 2);
            if (unix_after < 0) unix_after = read_proto_field("UNIX", 2);
            if (unix_after <= unix_before) break;
        }
        long max_slack = (unix_before < 32) ? 0 : 32;
        CHECK(unix_after >= 0 && (unix_after - unix_before) <= max_slack,
              "AF_UNIX cyclic GC LEAKED sockets! before=%ld after=%ld (256 cycled)",
              unix_before, unix_after);
    }

    /* --- Part 3: > 4 fds (12 memfds in one sendmsg) fallback path --- */
    {
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == 0, "sv12");
        int fds12[12];
        char tag[32];
        for (int i = 0; i < 12; i++) {
            snprintf(tag, sizeof(tag), "TWELVE_MFD_%02d", i);
            fds12[i] = make_tagged_memfd(tag, 0);
        }
        CHECK(write(sv[0], "PRE12", 5) == 5, "PRE12 write");
        CHECK(send_with_fds(sv[0], "TWELVE_FDS", 10, fds12, 12, 0) == 10, "send 12 fds failed");
        CHECK(write(sv[0], "POST12", 6) == 6, "POST12 write");

        char rb[32] = {0};
        int rfds[16], rnfds = 0, mflags = 0;
        ssize_t r = recv_with_fds(sv[1], rb, 5, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
        CHECK(r == 5 && memcmp(rb, "PRE12", 5) == 0 && rnfds == 0, "PRE12 recv mismatch");

        r = recv_with_fds(sv[1], rb, 10, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
        CHECK(r == 10 && memcmp(rb, "TWELVE_FDS", 10) == 0 && rnfds == 12 && !(mflags & MSG_CTRUNC),
              "12-fd fallback recv failed: r=%zd rnfds=%d mflags=0x%x", r, rnfds, mflags);
        for (int i = 0; i < 12; i++) {
            snprintf(tag, sizeof(tag), "TWELVE_MFD_%02d", i);
            verify_memfd_tag(rfds[i], tag);
            CHECK((fcntl(rfds[i], F_GETFD) & FD_CLOEXEC) != 0, "12-fd[%d] missing CLOEXEC", i);
            close(rfds[i]);
            close(fds12[i]);
        }

        r = recv_with_fds(sv[1], rb, sizeof(rb), rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
        CHECK(r == 6 && memcmp(rb, "POST12", 6) == 0 && rnfds == 0, "POST12 recv mismatch");
        close(sv[0]); close(sv[1]);
    }

    /* --- Part 4: MSG_PEEK + MSG_CMSG_CLOEXEC + MSG_CTRUNC + RLIMIT_NOFILE EMFILE (100 iters) --- */
    {
        struct rlimit orig_rl;
        CHECK(getrlimit(RLIMIT_NOFILE, &orig_rl) == 0, "getrlimit failed");

        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0, "sv4");
        int it_base_fds = count_open_fds();

        for (int iter = 0; iter < 100; iter++) {
            int m[4];
            m[0] = make_tagged_memfd("P4_MFD_0", 0);
            m[1] = make_tagged_memfd("P4_MFD_1", 0);
            m[2] = make_tagged_memfd("P4_MFD_2", 0);
            m[3] = make_tagged_memfd("P4_MFD_3", 0);

            char rb[16] = {0};
            int rfds[16], rnfds = 0, mflags = 0;

            /* 4a: MSG_PEEK twice without consuming, then real recvmsg */
            CHECK(send_with_fds(sv[0], "PEEK", 4, m, 2, 0) == 4, "send PEEK iter %d", iter);
            for (int p = 0; p < 2; p++) {
                ssize_t r = recv_with_fds(sv[1], rb, 4, rfds, 16, &rnfds, &mflags,
                                          MSG_PEEK | MSG_CMSG_CLOEXEC);
                CHECK(r == 4 && memcmp(rb, "PEEK", 4) == 0 && rnfds == 2,
                      "MSG_PEEK #%d iter %d failed: r=%zd rnfds=%d", p, iter, r, rnfds);
                CHECK((fcntl(rfds[0], F_GETFD) & FD_CLOEXEC) != 0 &&
                      (fcntl(rfds[1], F_GETFD) & FD_CLOEXEC) != 0,
                      "MSG_PEEK missing FD_CLOEXEC");
                verify_memfd_tag(rfds[0], "P4_MFD_0");
                verify_memfd_tag(rfds[1], "P4_MFD_1");
                close(rfds[0]); close(rfds[1]);
            }
            ssize_t r = recv_with_fds(sv[1], rb, 4, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 4 && memcmp(rb, "PEEK", 4) == 0 && rnfds == 2,
                  "post-PEEK consume iter %d failed: r=%zd rnfds=%d", iter, r, rnfds);
            verify_memfd_tag(rfds[0], "P4_MFD_0");
            verify_memfd_tag(rfds[1], "P4_MFD_1");
            close(rfds[0]); close(rfds[1]);
            CHECK(recv_with_fds(sv[1], rb, 4, rfds, 16, &rnfds, &mflags, MSG_DONTWAIT) == -1 &&
                  errno == EAGAIN, "post-PEEK socket not empty");

            /* 4a-boundary: MSG_PEEK on plain bytes right before sideband fds */
            if (iter < 10) {
                CHECK(write(sv[0], "PK0", 3) == 3, "write PK0");
                CHECK(send_with_fds(sv[0], "PK1", 3, m, 2, 0) == 3, "send PK1+2fds");
                r = recv_with_fds(sv[1], rb, 3, rfds, 16, &rnfds, &mflags, MSG_PEEK | MSG_CMSG_CLOEXEC);
                CHECK(r == 3 && memcmp(rb, "PK0", 3) == 0 && rnfds == 0,
                      "MSG_PEEK plain prefix prematurely returned %d fds!", rnfds);
                r = recv_with_fds(sv[1], rb, 3, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
                CHECK(r == 3 && memcmp(rb, "PK0", 3) == 0 && rnfds == 0, "consume PK0 failed");
                r = recv_with_fds(sv[1], rb, 3, rfds, 16, &rnfds, &mflags, MSG_PEEK | MSG_CMSG_CLOEXEC);
                CHECK(r == 3 && memcmp(rb, "PK1", 3) == 0 && rnfds == 2, "MSG_PEEK PK1 failed");
                close(rfds[0]); close(rfds[1]);
                r = recv_with_fds(sv[1], rb, 3, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
                CHECK(r == 3 && memcmp(rb, "PK1", 3) == 0 && rnfds == 2, "consume PK1 failed");
                close(rfds[0]); close(rfds[1]);
            }

            /* 4b: MSG_CMSG_CLOEXEC set vs clear */
            CHECK(send_with_fds(sv[0], "C1", 2, &m[0], 1, 0) == 2, "send C1");
            r = recv_with_fds(sv[1], rb, 2, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 2 && rnfds == 1 && (fcntl(rfds[0], F_GETFD) & FD_CLOEXEC) != 0,
                  "MSG_CMSG_CLOEXEC bit NOT set at iter %d", iter);
            close(rfds[0]);

            CHECK(send_with_fds(sv[0], "C0", 2, &m[0], 1, 0) == 2, "send C0");
            r = recv_with_fds(sv[1], rb, 2, rfds, 16, &rnfds, &mflags, 0);
            CHECK(r == 2 && rnfds == 1 && (fcntl(rfds[0], F_GETFD) & FD_CLOEXEC) == 0,
                  "FD_CLOEXEC unexpectedly set without MSG_CMSG_CLOEXEC at iter %d", iter);
            close(rfds[0]);

            /* 4c: Small cmsg buffer (MSG_CTRUNC) & msg_control == NULL */
            /* Exact 1-fd buffer (CMSG_LEN(sizeof(int)) = 20B on x86_64 -> fits 1 fd, truncates 3) */
            CHECK(send_with_fds(sv[0], "TR1", 3, m, 4, 0) == 3, "send TR1");
            r = recv_with_fds_ctl(sv[1], rb, 3, CMSG_LEN(sizeof(int)), rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 3 && memcmp(rb, "TR1", 3) == 0 && (mflags & MSG_CTRUNC) != 0 && rnfds == 1,
                  "1-fd cbuf MSG_CTRUNC failed iter %d: r=%zd rnfds=%d mflags=0x%x",
                  iter, r, rnfds, mflags);
            CHECK((fcntl(rfds[0], F_GETFD) & FD_CLOEXEC) != 0, "TR1 fd missing CLOEXEC");
            close(rfds[0]);

            /* 2-fd buffer (CMSG_SPACE(sizeof(int)) = 24B on x86_64 -> fits 2 fds, truncates 2) */
            CHECK(send_with_fds(sv[0], "TR2", 3, m, 4, 0) == 3, "send TR2");
            r = recv_with_fds_ctl(sv[1], rb, 3, CMSG_SPACE(sizeof(int)), rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 3 && memcmp(rb, "TR2", 3) == 0 && (mflags & MSG_CTRUNC) != 0 && rnfds >= 1 && rnfds <= 2,
                  "2-fd cbuf MSG_CTRUNC failed iter %d: r=%zd rnfds=%d mflags=0x%x",
                  iter, r, rnfds, mflags);
            for (int i = 0; i < rnfds; i++) close(rfds[i]);

            CHECK(send_with_fds(sv[0], "TR0", 3, m, 4, 0) == 3, "send TR0");
            r = recv_with_fds_ctl(sv[1], rb, 3, 0, NULL, 0, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
            CHECK(r == 3 && memcmp(rb, "TR0", 3) == 0 && (mflags & MSG_CTRUNC) != 0,
                  "NULL cbuf MSG_CTRUNC failed iter %d: r=%zd mflags=0x%x", iter, r, mflags);

            /* 4-byte tiny cbuf (< sizeof(struct cmsghdr)) */
            CHECK(send_with_fds(sv[0], "TRS", 3, m, 2, 0) == 3, "send TRS");
            {
                char tiny_cbuf[4];
                struct iovec tiov = { .iov_base = rb, .iov_len = 3 };
                struct msghdr tmsg = {
                    .msg_iov = &tiov, .msg_iovlen = 1,
                    .msg_control = tiny_cbuf, .msg_controllen = sizeof(tiny_cbuf)
                };
                r = recvmsg(sv[1], &tmsg, MSG_CMSG_CLOEXEC);
                CHECK(r == 3 && (tmsg.msg_flags & MSG_CTRUNC) != 0,
                      "tiny 4B cbuf MSG_CTRUNC failed iter %d", iter);
            }

            /* 4d: RLIMIT_NOFILE (EMFILE) rollback: 0 free slots & 2 free slots for 4 fds */
            int max_fd = sv[0];
            if (sv[1] > max_fd) max_fd = sv[1];
            for (int i = 0; i < 4; i++) if (m[i] > max_fd) max_fd = m[i];
            rlim_t low_lim = (rlim_t)(max_fd + 16);
            CHECK(low_lim <= orig_rl.rlim_max, "RLIMIT_NOFILE max too small for EMFILE test");
            {
                struct rlimit rl = orig_rl;
                rl.rlim_cur = low_lim;
                CHECK(setrlimit(RLIMIT_NOFILE, &rl) == 0, "lower RLIMIT_NOFILE failed");

                size_t fill_cap = (size_t)low_lim + 16;
                int *fillers = malloc(fill_cap * sizeof(int));
                CHECK(fillers != NULL, "malloc fillers failed");
                size_t nfill = 0;
                for (;;) {
                    int d = dup(sv[0]);
                    if (d < 0) {
                        CHECK(errno == EMFILE, "filler dup expected EMFILE, got %d", errno);
                        break;
                    }
                    CHECK(nfill < fill_cap, "dup exceeded low_lim!");
                    fillers[nfill++] = d;
                }
                CHECK(nfill >= 2, "expected >=2 filler slots, got %zu", nfill);

                /* Case 0 free slots */
                CHECK(send_with_fds(sv[0], "EMF0", 4, m, 4, 0) == 4, "send EMF0");
                r = recv_with_fds(sv[1], rb, 4, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
                if (r >= 0) {
                    CHECK(r == 4 && memcmp(rb, "EMF0", 4) == 0 &&
                          (mflags & MSG_CTRUNC) != 0 && rnfds == 0,
                          "EMFILE(0 free) unexpected: r=%zd rnfds=%d mflags=0x%x", r, rnfds, mflags);
                } else {
                    CHECK(errno == EMFILE, "EMFILE(0 free) bad errno=%d", errno);
                }

                /* Case 2 free slots (for 4 incoming fds) */
                close(fillers[--nfill]);
                close(fillers[--nfill]);
                if (r < 0) {
                    /* Drain EMF0 after freeing slots + restoring limit */
                    while (nfill > 0) close(fillers[--nfill]);
                    CHECK(setrlimit(RLIMIT_NOFILE, &orig_rl) == 0, "restore rlimit");
                    r = recv_with_fds(sv[1], rb, 4, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
                    CHECK(r == 4, "drain EMF0 failed");
                    for (int i = 0; i < rnfds; i++) close(rfds[i]);
                } else {
                    CHECK(send_with_fds(sv[0], "EMF2", 4, m, 4, 0) == 4, "send EMF2");
                    r = recv_with_fds(sv[1], rb, 4, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
                    if (r >= 0) {
                        CHECK(r == 4 && memcmp(rb, "EMF2", 4) == 0 &&
                              (mflags & MSG_CTRUNC) != 0 && rnfds <= 2,
                              "EMFILE(2 free) unexpected: r=%zd rnfds=%d mflags=0x%x",
                              r, rnfds, mflags);
                        for (int i = 0; i < rnfds; i++) {
                            CHECK((fcntl(rfds[i], F_GETFD) & FD_CLOEXEC) != 0,
                                  "EMFILE partial fd missing CLOEXEC");
                            close(rfds[i]);
                        }
                    } else {
                        CHECK(errno == EMFILE, "EMFILE(2 free) bad errno=%d", errno);
                        while (nfill > 0) close(fillers[--nfill]);
                        CHECK(setrlimit(RLIMIT_NOFILE, &orig_rl) == 0, "restore rlimit");
                        r = recv_with_fds(sv[1], rb, 4, rfds, 16, &rnfds, &mflags, MSG_CMSG_CLOEXEC);
                        CHECK(r == 4, "drain EMF2 failed");
                        for (int i = 0; i < rnfds; i++) close(rfds[i]);
                    }
                    while (nfill > 0) close(fillers[--nfill]);
                    CHECK(setrlimit(RLIMIT_NOFILE, &orig_rl) == 0, "restore rlimit");
                }
                free(fillers);
            }

            for (int i = 0; i < 4; i++) close(m[i]);
            CHECK(count_open_fds() == it_base_fds,
                  "FD leak at Part 4 iter %d: expected %d, got %d",
                  iter, it_base_fds, count_open_fds());
        }
        close(sv[0]);
        close(sv[1]);
    }

    /* --- Part 5: Peer close() with unread sideband fds in flight (200x) --- */
    {
        long file_nr_before = read_file_nr_alloc();
        long shmem_before = read_meminfo_kb("Shmem");
        int fd_before_p5 = count_open_fds();

        for (int iter = 0; iter < 200; iter++) {
            int sv[2];
            CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == 0, "p5 sv");
            int m[4];
            for (int i = 0; i < 4; i++)
                m[i] = make_tagged_memfd("UNREAD_MEMFD_32K", 32768);
            /* Queue 2 sideband slots (8 unread struct file refs, 128 KiB backing memfd per iter = 25.6 MiB total!) */
            CHECK(send_with_fds(sv[0], "UNR1", 4, m, 4, 0) == 4, "p5 send 1");
            CHECK(send_with_fds(sv[0], "UNR2", 4, m, 4, 0) == 4, "p5 send 2");
            for (int i = 0; i < 4; i++) close(m[i]);
            /* Alternate closing receiver first vs sender first */
            if (iter & 1) {
                close(sv[1]);
                close(sv[0]);
            } else {
                close(sv[0]);
                close(sv[1]);
            }
        }
        usleep(30000); /* Allow delayed_fput / RCU cleanup to settle */

        int fd_after_p5 = count_open_fds();
        long file_nr_after = read_file_nr_alloc();
        long shmem_after = read_meminfo_kb("Shmem");
        CHECK(fd_after_p5 == fd_before_p5,
              "Part 5 process FD leak: %d -> %d", fd_before_p5, fd_after_p5);
        long shmem_delta = (shmem_before >= 0 && shmem_after >= 0) ? (shmem_after - shmem_before) : 0;
        long max_shmem_delta = (file_nr_before > 0 && file_nr_before < 2000) ? 1024 : 10240;
        CHECK(shmem_delta <= max_shmem_delta,
              "Part 5 KERNEL MEMFD/SHMEM LEAK! Shmem delta=%ld KiB (25600 KiB sent unread)",
              shmem_delta);
        long file_delta = (file_nr_before >= 0 && file_nr_after >= 0) ? (file_nr_after - file_nr_before) : 0;
        long max_file_delta = (file_nr_before > 0 && file_nr_before < 2000) ? 64 : 600;
        CHECK(file_delta <= max_file_delta,
              "Part 5 KERNEL struct file LEAK! file-nr delta=%ld (1600 unread refs sent)",
              file_delta);
    }

    CHECK(count_open_fds() == fd_base, "FD leak in test_scm_rights_sideband_torture: %d -> %d",
          fd_base, count_open_fds());
    printf("[PASS] M1 Torture C: 5-part SCM_RIGHTS sideband torture (1-4 fds, GC, 12-fd, PEEK/CLOEXEC/CTRUNC/EMFILE x100, unread close x200)\n");
}

/* =========================================================================
 * MILESTONE 1 TORTURE D: AF_UNIX Multi-iovec sendmsg (dbus-broker pattern)
 * ========================================================================= */
struct iovec_rx_ctx {
    int fd;
    size_t total_bytes;
};

static void *iovec_stream_rx_fn(void *arg) {
    struct iovec_rx_ctx *c = arg;
    uint8_t buf[8192];
    size_t got = 0;
    while (got < c->total_bytes) {
        ssize_t r = recv(c->fd, buf, sizeof(buf), 0);
        if (r < 0 && errno == EINTR) continue;
        CHECK(r > 0, "iovec rx failed at %zu/%zu: errno=%d", got, c->total_bytes, errno);
        for (ssize_t i = 0; i < r; i++) {
            uint8_t exp = (uint8_t)(((got + (size_t)i) * 73U + 29U) & 0xffU);
            CHECK(buf[i] == exp, "iovec 2MiB stream mismatch at byte %zu: got 0x%02x exp 0x%02x",
                  got + (size_t)i, buf[i], exp);
        }
        got += (size_t)r;
    }
    return NULL;
}

static void recv_exact_verify(int fd, const uint8_t *expected, size_t len, const char *ctx_name) {
    uint8_t *buf = malloc(len ? len : 1);
    CHECK(buf != NULL, "malloc failed");
    size_t got = 0;
    while (got < len) {
        ssize_t r = recv(fd, buf + got, len - got, 0);
        if (r < 0 && errno == EINTR) continue;
        CHECK(r > 0, "%s: recv failed at %zu/%zu (errno=%d)", ctx_name, got, len, errno);
        got += (size_t)r;
    }
    CHECK(memcmp(buf, expected, len) == 0, "%s: byte-for-byte mismatch (%zu bytes)", ctx_name, len);
    free(buf);
}

static void test_unix_sendmsg_iovec_dbus(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds failed");

    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == 0, "socketpair failed");

    /* 1. 1, 2, 3, 4, and 5 iovec scatter-gather + zero-length middle/edge/all-zero iovecs */
    uint8_t segs[5][256];
    for (int s = 0; s < 5; s++)
        for (int j = 0; j < 256; j++)
            segs[s][j] = (uint8_t)((s * 53 + j * 11 + 3) & 0xff);

    const size_t lens[5] = { 16, 48, 128, 77, 33 }; /* 3-iovec matches dbus-broker 16+48+128! */
    for (int nseg = 1; nseg <= 5; nseg++) {
        struct iovec iov[5];
        uint8_t expected[1024];
        size_t tot = 0;
        for (int i = 0; i < nseg; i++) {
            iov[i].iov_base = segs[i];
            iov[i].iov_len = lens[i];
            memcpy(expected + tot, segs[i], lens[i]);
            tot += lens[i];
        }
        struct msghdr msg = { .msg_iov = iov, .msg_iovlen = (size_t)nseg };
        ssize_t w = sendmsg(sv[0], &msg, 0);
        CHECK(w == (ssize_t)tot, "sendmsg %d-iovec expected %zu, got %zd", nseg, tot, w);
        recv_exact_verify(sv[1], expected, tot, "scatter-gather");
    }

    /* Zero-length middle iovec (NULL base and valid base), first/last zero-len, all-zero */
    {
        uint8_t expected[256];
        memcpy(expected, segs[0], 16);
        memcpy(expected + 16, segs[2], 128);
        struct iovec iov3[3] = {
            { .iov_base = segs[0], .iov_len = 16 },
            { .iov_base = NULL,    .iov_len = 0  },
            { .iov_base = segs[2], .iov_len = 128 }
        };
        struct msghdr msg = { .msg_iov = iov3, .msg_iovlen = 3 };
        CHECK(sendmsg(sv[0], &msg, 0) == 144, "3-iovec middle NULL zero-len failed");
        recv_exact_verify(sv[1], expected, 144, "zero-mid-NULL");

        struct iovec iov4[4] = {
            { .iov_base = NULL,    .iov_len = 0 },
            { .iov_base = segs[0], .iov_len = 16 },
            { .iov_base = segs[1], .iov_len = 0 },
            { .iov_base = segs[2], .iov_len = 128 }
        };
        msg.msg_iov = iov4;
        msg.msg_iovlen = 4;
        CHECK(sendmsg(sv[0], &msg, 0) == 144, "4-iovec zero-first+mid failed");
        recv_exact_verify(sv[1], expected, 144, "zero-first+mid");

        struct iovec iov_zero[3] = {
            { .iov_base = NULL, .iov_len = 0 },
            { .iov_base = segs[0], .iov_len = 0 },
            { .iov_base = NULL, .iov_len = 0 }
        };
        msg.msg_iov = iov_zero;
        msg.msg_iovlen = 3;
        CHECK(sendmsg(sv[0], &msg, 0) == 0, "all-zero 3-iovec must return 0");
    }

    /* 2. Partial wrap across 16 KiB / 64 KiB / 256 KiB ring boundaries */
    {
        static const size_t targets[] = { 16384 - 37, 65536 - 53, 262144 - 71 };
        uint8_t pad[4096];
        memset(pad, 0xCC, sizeof(pad));
        uint8_t drain[4096];
        size_t pos = 0;

        for (size_t t = 0; t < 3; t++) {
            while (pos < targets[t]) {
                size_t step = targets[t] - pos;
                if (step > sizeof(pad)) step = sizeof(pad);
                CHECK(write(sv[0], pad, step) == (ssize_t)step, "wrap pad write");
                size_t dr = 0;
                while (dr < step) {
                    ssize_t r = read(sv[1], drain, step - dr);
                    CHECK(r > 0, "wrap pad drain");
                    dr += (size_t)r;
                }
                pos += step;
            }
            /* Now ring head/tail sit right before the power-of-2 boundary! Fire 4-iovec across it! */
            uint8_t big0[64], big1[1024], big2[0 + 1], big3[2048];
            uint8_t exp_wrap[64 + 1024 + 2048];
            for (size_t i = 0; i < sizeof(big0); i++) big0[i] = (uint8_t)(i ^ 0x11);
            for (size_t i = 0; i < sizeof(big1); i++) big1[i] = (uint8_t)(i ^ 0x22);
            big2[0] = 0;
            for (size_t i = 0; i < sizeof(big3); i++) big3[i] = (uint8_t)(i ^ 0x33);
            memcpy(exp_wrap, big0, sizeof(big0));
            memcpy(exp_wrap + 64, big1, sizeof(big1));
            memcpy(exp_wrap + 64 + 1024, big3, sizeof(big3));

            struct iovec wiov[4] = {
                { .iov_base = big0, .iov_len = sizeof(big0) },
                { .iov_base = big1, .iov_len = sizeof(big1) },
                { .iov_base = big2, .iov_len = 0 },
                { .iov_base = big3, .iov_len = sizeof(big3) }
            };
            struct msghdr wmsg = { .msg_iov = wiov, .msg_iovlen = 4 };
            size_t want = sizeof(exp_wrap);
            ssize_t w = sendmsg(sv[0], &wmsg, 0);
            CHECK(w == (ssize_t)want, "boundary wrap sendmsg failed: w=%zd", w);
            recv_exact_verify(sv[1], exp_wrap, want, "boundary-wrap-4iovec");
            pos += want;
        }
    }

    /* 2b. Cross-thread 2 MiB continuous 3-iovec & 4-iovec stream */
    {
        const size_t TOTAL = 2 * 1024 * 1024;
        struct iovec_rx_ctx rctx = { .fd = sv[1], .total_bytes = TOTAL };
        pthread_t rth;
        CHECK(pthread_create(&rth, NULL, iovec_stream_rx_fn, &rctx) == 0, "pthread_create rx");

        uint8_t s0[16], s1[48], s2[128], s3[64];
        size_t sent = 0;
        size_t iter = 0;
        while (sent < TOTAL) {
            size_t rem = TOTAL - sent;
            struct iovec iov[4];
            size_t nseg;
            size_t chunk_len;
            if (rem >= 256 && (iter & 1)) {
                /* 4-iovec: 16 + 48 + 128 + 64 = 256 */
                for (size_t i = 0; i < 16; i++) s0[i] = (uint8_t)(((sent + i) * 73U + 29U) & 0xffU);
                for (size_t i = 0; i < 48; i++) s1[i] = (uint8_t)(((sent + 16 + i) * 73U + 29U) & 0xffU);
                for (size_t i = 0; i < 128; i++) s2[i] = (uint8_t)(((sent + 64 + i) * 73U + 29U) & 0xffU);
                for (size_t i = 0; i < 64; i++) s3[i] = (uint8_t)(((sent + 192 + i) * 73U + 29U) & 0xffU);
                iov[0] = (struct iovec){ s0, 16 };
                iov[1] = (struct iovec){ s1, 48 };
                iov[2] = (struct iovec){ s2, 128 };
                iov[3] = (struct iovec){ s3, 64 };
                nseg = 4; chunk_len = 256;
            } else if (rem >= 192) {
                /* 3-iovec dbus-broker pattern: 16 + 48 + 128 = 192 */
                for (size_t i = 0; i < 16; i++) s0[i] = (uint8_t)(((sent + i) * 73U + 29U) & 0xffU);
                for (size_t i = 0; i < 48; i++) s1[i] = (uint8_t)(((sent + 16 + i) * 73U + 29U) & 0xffU);
                for (size_t i = 0; i < 128; i++) s2[i] = (uint8_t)(((sent + 64 + i) * 73U + 29U) & 0xffU);
                iov[0] = (struct iovec){ s0, 16 };
                iov[1] = (struct iovec){ s1, 48 };
                iov[2] = (struct iovec){ s2, 128 };
                nseg = 3; chunk_len = 192;
            } else {
                for (size_t i = 0; i < rem; i++) s2[i] = (uint8_t)(((sent + i) * 73U + 29U) & 0xffU);
                iov[0] = (struct iovec){ s2, rem };
                nseg = 1; chunk_len = rem;
            }
            struct msghdr msg = { .msg_iov = iov, .msg_iovlen = nseg };
            ssize_t w = sendmsg(sv[0], &msg, 0);
            CHECK(w == (ssize_t)chunk_len, "stream sendmsg expected %zu, got %zd", chunk_len, w);
            sent += (size_t)w;
            iter++;
        }
        pthread_join(rth, NULL);
    }

    /* 3. MSG_DONTWAIT fill -> partial write -> -EAGAIN -> drain -> byte-exact verify */
    {
        size_t cap = 4 * 1024 * 1024;
        uint8_t *log_buf = malloc(cap);
        CHECK(log_buf != NULL, "malloc log_buf");
        size_t accepted = 0;
        uint8_t a[16], b[48], c[128];
        for (;;) {
            CHECK(accepted + 192 <= cap, "socket buffer unexpectedly > 4MiB");
            for (size_t i = 0; i < 16; i++) a[i] = (uint8_t)((accepted + i) & 0xff);
            for (size_t i = 0; i < 48; i++) b[i] = (uint8_t)((accepted + 16 + i) & 0xff);
            for (size_t i = 0; i < 128; i++) c[i] = (uint8_t)((accepted + 64 + i) & 0xff);
            struct iovec iov[3] = { { a, 16 }, { b, 48 }, { c, 128 } };
            struct msghdr msg = { .msg_iov = iov, .msg_iovlen = 3 };
            ssize_t w = sendmsg(sv[0], &msg, MSG_DONTWAIT);
            if (w > 0) {
                uint8_t flat[192];
                memcpy(flat, a, 16);
                memcpy(flat + 16, b, 48);
                memcpy(flat + 64, c, 128);
                memcpy(log_buf + accepted, flat, (size_t)w);
                accepted += (size_t)w;
            } else {
                CHECK(w == -1 && errno == EAGAIN, "MSG_DONTWAIT fill unexpected w=%zd errno=%d", w, errno);
                break;
            }
        }
        CHECK(accepted > 0, "MSG_DONTWAIT accepted 0 bytes on empty socket!");
        /* Second MSG_DONTWAIT while full must still be EAGAIN */
        struct iovec iov1 = { a, 16 };
        struct msghdr m1 = { .msg_iov = &iov1, .msg_iovlen = 1 };
        CHECK(sendmsg(sv[0], &m1, MSG_DONTWAIT) == -1 && errno == EAGAIN,
              "full socket MSG_DONTWAIT must return EAGAIN");
        recv_exact_verify(sv[1], log_buf, accepted, "MSG_DONTWAIT-fill-drain");
        free(log_buf);
    }

    /* 4. EFAULT bad pointer check (kernel pointer + PROT_NONE user page) */
    {
        void *kbad = (void *)(uintptr_t)0xffff800000000000ULL;
        void *ubad = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(ubad != MAP_FAILED, "mmap PROT_NONE failed");

        struct iovec kiov[3] = {
            { .iov_base = segs[0], .iov_len = 16 },
            { .iov_base = kbad,    .iov_len = 48 },
            { .iov_base = segs[2], .iov_len = 128 }
        };
        struct msghdr kmsg = { .msg_iov = kiov, .msg_iovlen = 3 };
        CHECK(sendmsg(sv[0], &kmsg, MSG_DONTWAIT) == -1 && errno == EFAULT,
              "3-iovec kernel bad pointer must return EFAULT");

        struct iovec uiov0[3] = {
            { .iov_base = ubad,    .iov_len = 16 },
            { .iov_base = segs[1], .iov_len = 48 },
            { .iov_base = segs[2], .iov_len = 128 }
        };
        struct msghdr umsg0 = { .msg_iov = uiov0, .msg_iovlen = 3 };
        CHECK(sendmsg(sv[0], &umsg0, MSG_DONTWAIT) == -1 && errno == EFAULT,
              "3-iovec PROT_NONE in iov[0] must return EFAULT");

        struct iovec uiov1[3] = {
            { .iov_base = segs[0], .iov_len = 16 },
            { .iov_base = ubad,    .iov_len = 48 },
            { .iov_base = segs[2], .iov_len = 128 }
        };
        struct msghdr umsg1 = { .msg_iov = uiov1, .msg_iovlen = 3 };
        ssize_t er = sendmsg(sv[0], &umsg1, MSG_DONTWAIT);
        if (er < 0) {
            CHECK(errno == EFAULT, "3-iovec PROT_NONE in iov[1] expected EFAULT, got %d", errno);
        } else {
            CHECK(er == 16, "3-iovec PROT_NONE in iov[1] unexpected partial len %zd", er);
            recv_exact_verify(sv[1], segs[0], 16, "EFAULT-partial-iov0");
        }
        munmap(ubad, 4096);

        /* Verify stream is still 100% healthy after EFAULT */
        char s_a[] = "SENTINEL", s_b[] = "_AFTER", s_c[] = "_EFAULT";
        struct iovec siov[3] = { { s_a, 8 }, { s_b, 6 }, { s_c, 7 } };
        struct msghdr smsg = { .msg_iov = siov, .msg_iovlen = 3 };
        CHECK(sendmsg(sv[0], &smsg, 0) == 21, "post-EFAULT sentinel sendmsg failed");
        recv_exact_verify(sv[1], (const uint8_t *)"SENTINEL_AFTER_EFAULT", 21, "post-EFAULT-sentinel");
    }

    /* 5. SIGPIPE / MSG_NOSIGNAL on closed peer */
    {
        close(sv[1]);
        struct sigaction sa_new = {0}, sa_old = {0};
        sa_new.sa_handler = sigpipe_counter_handler;
        CHECK(sigaction(SIGPIPE, &sa_new, &sa_old) == 0, "sigaction SIGPIPE");
        g_sigpipe_count = 0;

        struct iovec iov[3] = { { segs[0], 16 }, { segs[1], 48 }, { segs[2], 128 } };
        struct msghdr msg = { .msg_iov = iov, .msg_iovlen = 3 };
        CHECK(sendmsg(sv[0], &msg, MSG_NOSIGNAL) == -1 && errno == EPIPE,
              "closed peer MSG_NOSIGNAL must return EPIPE");
        CHECK(g_sigpipe_count == 0, "MSG_NOSIGNAL delivered SIGPIPE!");

        CHECK(sendmsg(sv[0], &msg, 0) == -1 && errno == EPIPE,
              "closed peer without MSG_NOSIGNAL must return EPIPE");
        CHECK(g_sigpipe_count == 1, "missing SIGPIPE without MSG_NOSIGNAL (got %d)", (int)g_sigpipe_count);

        CHECK(sendmsg(sv[0], &msg, MSG_DONTWAIT | MSG_NOSIGNAL) == -1 && errno == EPIPE,
              "closed peer MSG_DONTWAIT|MSG_NOSIGNAL must return EPIPE");
        CHECK(g_sigpipe_count == 1, "MSG_DONTWAIT|MSG_NOSIGNAL delivered SIGPIPE!");

        CHECK(sigaction(SIGPIPE, &sa_old, NULL) == 0, "restore SIGPIPE");
        close(sv[0]);
    }

    CHECK(count_open_fds() == fd_base, "FD leak in test_unix_sendmsg_iovec_dbus: %d -> %d",
          fd_base, count_open_fds());
    printf("[PASS] M1 Torture D: AF_UNIX 1..4-iovec sendmsg (zero-len, 16K/64K/256K wrap, 2MiB stream, DONTWAIT, EFAULT, SIGPIPE)\n");
}

/* =========================================================================
 * MILESTONE 1 TORTURE E: accept4 Non-Blocking Race (TCP + AF_UNIX, 4A + 2C)
 * ========================================================================= */
struct acc_race_ctx {
    int lfd;
    int family;
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int target_conns;
    int per_client;
    atomic_int *accepted;
    atomic_int *connected;
    atomic_int *bad_flags;
    atomic_int *eagain_hits;
    atomic_int *stop;
    pthread_barrier_t *bar;
};

struct acc_thr_arg {
    struct acc_race_ctx *ctx;
    int use_epoll;
};

static void *accept4_acceptor_fn(void *arg) {
    struct acc_thr_arg *ta = arg;
    struct acc_race_ctx *c = ta->ctx;
    int ep = -1;
    if (ta->use_epoll) {
        ep = epoll_create1(EPOLL_CLOEXEC);
        CHECK(ep >= 0, "accept4 epoll_create1 failed");
        struct epoll_event ev = { .events = EPOLLIN, .data.fd = c->lfd };
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, c->lfd, &ev) == 0, "accept4 epoll_ctl failed");
    }

    pthread_barrier_wait(c->bar);
    while (!atomic_load_explicit(c->stop, memory_order_acquire)) {
        if (ta->use_epoll) {
            struct epoll_event out;
            int n = epoll_wait(ep, &out, 1, 2);
            if (n <= 0) continue;
        }
        for (;;) {
            int cfd = accept4(c->lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    atomic_fetch_add_explicit(c->eagain_hits, 1, memory_order_relaxed);
                    break;
                }
                if (errno == EINTR) continue;
                FAIL("accept4 unexpected error: %s", strerror(errno));
            }
            int fl = fcntl(cfd, F_GETFL);
            int fdfl = fcntl(cfd, F_GETFD);
            if ((fl & O_NONBLOCK) == 0 || (fdfl & FD_CLOEXEC) == 0)
                atomic_fetch_add_explicit(c->bad_flags, 1, memory_order_relaxed);

            char ack = 'A';
            ssize_t w = send(cfd, &ack, 1, MSG_NOSIGNAL);
            CHECK(w == 1, "accepted socket ack send failed: w=%zd errno=%d", w, errno);
            close(cfd);

            if (atomic_fetch_add_explicit(c->accepted, 1, memory_order_acq_rel) + 1 >= c->target_conns) {
                atomic_store_explicit(c->stop, 1, memory_order_release);
                break;
            }
        }
        if (!ta->use_epoll)
            sched_yield();
    }
    if (ep >= 0) close(ep);
    return NULL;
}

static void *accept4_client_fn(void *arg) {
    struct acc_race_ctx *c = arg;
    pthread_barrier_wait(c->bar);
    for (int i = 0; i < c->per_client; i++) {
        int fd = socket(c->family, SOCK_STREAM | SOCK_CLOEXEC, 0);
        CHECK(fd >= 0, "client socket failed: %s", strerror(errno));
        for (;;) {
            if (connect(fd, (struct sockaddr *)&c->addr, c->addrlen) == 0)
                break;
            CHECK(errno == EINTR || errno == EAGAIN,
                  "client connect failed at iter %d (family=%d): %s", i, c->family, strerror(errno));
            usleep(100);
        }
        char ch = 0;
        ssize_t r;
        do { r = recv(fd, &ch, 1, 0); } while (r < 0 && errno == EINTR);
        CHECK(r == 1 && ch == 'A', "client recv ack failed at iter %d (r=%zd ch=0x%x)", i, r, ch);
        if (c->family == AF_INET) {
            struct linger lg = { .l_onoff = 1, .l_linger = 0 };
            setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        }
        close(fd);
        atomic_fetch_add_explicit(c->connected, 1, memory_order_relaxed);
    }
    return NULL;
}

static void run_accept4_race_for_family(int family, const char *label) {
    int lfd = socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    CHECK(lfd >= 0, "%s listener socket failed", label);

    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    socklen_t slen = 0;

    if (family == AF_INET) {
        int one = 1;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
        sin->sin_family = AF_INET;
        sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sin->sin_port = 0;
        CHECK(bind(lfd, (struct sockaddr *)sin, sizeof(*sin)) == 0, "TCP bind failed");
        slen = sizeof(*sin);
        CHECK(getsockname(lfd, (struct sockaddr *)sin, &slen) == 0, "TCP getsockname failed");
    } else {
        struct sockaddr_un *sun = (struct sockaddr_un *)&ss;
        sun->sun_family = AF_UNIX;
        sun->sun_path[0] = '\0'; /* Abstract namespace: works even on ro rootfs */
        int nlen = snprintf(sun->sun_path + 1, sizeof(sun->sun_path) - 1,
                            "netzero_m1_accept4_%d", (int)getpid());
        slen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + nlen);
        CHECK(bind(lfd, (struct sockaddr *)sun, slen) == 0, "AF_UNIX bind failed: %s", strerror(errno));
    }

    /* Non-listening nonblock socket must return EINVAL (NOT EAGAIN!) */
    CHECK(accept4(lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC) == -1 && errno == EINVAL,
          "%s non-listening accept4 must return EINVAL, got errno=%d", label, errno);

    CHECK(listen(lfd, 4096) == 0, "%s listen failed", label);

    /* Invalid flags must return EINVAL even when queue is empty */
    CHECK(accept4(lfd, NULL, NULL, 0x12345) == -1 && errno == EINVAL,
          "%s accept4(0x12345) must return EINVAL", label);
    CHECK(accept4(lfd, NULL, NULL, ~0) == -1 && errno == EINVAL,
          "%s accept4(~0) must return EINVAL", label);

    /* Empty non-blocking listener must return EAGAIN */
    CHECK(accept4(lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC) == -1 && errno == EAGAIN,
          "%s empty accept4 must return EAGAIN", label);

    const int PER_CLIENT = 1000;
    const int TARGET = PER_CLIENT * 2;
    atomic_int accepted = 0, connected = 0, bad_flags = 0, eagain_hits = 0, stop = 0;
    pthread_barrier_t bar;
    pthread_barrier_init(&bar, NULL, 7); /* 4 acceptors + 2 clients + main */

    struct acc_race_ctx rctx = {
        .lfd = lfd, .family = family, .addr = ss, .addrlen = slen,
        .target_conns = TARGET, .per_client = PER_CLIENT,
        .accepted = &accepted, .connected = &connected,
        .bad_flags = &bad_flags, .eagain_hits = &eagain_hits,
        .stop = &stop, .bar = &bar
    };
    struct acc_thr_arg aargs[4];
    pthread_t ath[4], cth[2];
    for (int i = 0; i < 4; i++) {
        aargs[i] = (struct acc_thr_arg){ .ctx = &rctx, .use_epoll = (i >= 2) ? 1 : 0 };
        CHECK(pthread_create(&ath[i], NULL, accept4_acceptor_fn, &aargs[i]) == 0, "create acceptor");
    }
    for (int i = 0; i < 2; i++)
        CHECK(pthread_create(&cth[i], NULL, accept4_client_fn, &rctx) == 0, "create client");

    pthread_barrier_wait(&bar);
    uint64_t deadline = now_ns() + 15000000000ULL;
    while (atomic_load_explicit(&accepted, memory_order_acquire) < TARGET) {
        if (now_ns() > deadline) {
            FAIL("%s accept4 race TIMED OUT (lost wakeup/connection!): accepted=%d connected=%d/%d",
                 label, atomic_load(&accepted), atomic_load(&connected), TARGET);
        }
        usleep(1000);
    }
    atomic_store_explicit(&stop, 1, memory_order_release);

    for (int i = 0; i < 2; i++) pthread_join(cth[i], NULL);
    for (int i = 0; i < 4; i++) pthread_join(ath[i], NULL);
    pthread_barrier_destroy(&bar);
    close(lfd);

    CHECK(atomic_load(&accepted) == TARGET && atomic_load(&connected) == TARGET,
          "%s count mismatch: accepted=%d connected=%d target=%d",
          label, atomic_load(&accepted), atomic_load(&connected), TARGET);
    CHECK(atomic_load(&bad_flags) == 0,
          "%s accept4 dropped O_NONBLOCK/FD_CLOEXEC on %d fds!", label, atomic_load(&bad_flags));
}

static void test_accept4_nonblock_race(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds failed");

    /* Bad FD & non-socket FD checks */
    CHECK(accept4(99999, NULL, NULL, SOCK_NONBLOCK) == -1 && errno == EBADF,
          "accept4(99999) must return EBADF");
    int p[2];
    CHECK(pipe2(p, O_NONBLOCK | O_CLOEXEC) == 0, "pipe2 failed");
    CHECK(accept4(p[0], NULL, NULL, SOCK_NONBLOCK) == -1 && errno == ENOTSOCK,
          "accept4(pipe) must return ENOTSOCK");
    close(p[0]);
    close(p[1]);

    run_accept4_race_for_family(AF_INET, "TCP");
    run_accept4_race_for_family(AF_UNIX, "AF_UNIX");

    CHECK(count_open_fds() == fd_base, "FD leak in test_accept4_nonblock_race: %d -> %d",
          fd_base, count_open_fds());
    printf("[PASS] M1 Torture E: accept4 nonblock race (TCP 2000/2000 + AF_UNIX 2000/2000, EINVAL/EBADF/ENOTSOCK/CLOEXEC/NONBLOCK)\n");
}

/* =========================================================================
 * MILESTONE 1 TORTURE F: dbus-broker SCM_RIGHTS EPOLLET | EPOLLOUT Wakeup
 * ========================================================================= */
struct dbus_epout_rx_ctx {
    int fd;
    int count;
    atomic_int *received;
    pthread_barrier_t *bar;
};

static void *dbus_epout_rx_fn(void *arg) {
    struct dbus_epout_rx_ctx *c = arg;
    pthread_barrier_wait(c->bar);
    for (int i = 0; i < c->count; i++) {
        struct pollfd pfd = { .fd = c->fd, .events = POLLIN };
        int pr = poll(&pfd, 1, 500);
        CHECK(pr == 1 && (pfd.revents & POLLIN),
              "dbus-broker EPOLLOUT peer poll failed/timed out at msg %d (pr=%d revents=0x%x)",
              i, pr, pr == 1 ? pfd.revents : 0);

        char rb[32] = {0};
        char exp_pl[32];
        snprintf(exp_pl, sizeof(exp_pl), "DBUS_FD_MSG_%02d!!", i);
        int exp_nfds = ((i & 3) == 3) ? 2 : 1;
        int rfds[16], rnfds = 0, mflags = 0;
        ssize_t r = recv_with_fds(c->fd, rb, 16, rfds, 16, &rnfds, &mflags,
                                  MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        CHECK(r == 16 && memcmp(rb, exp_pl, 16) == 0 && rnfds == exp_nfds,
              "dbus-broker EPOLLOUT peer recv msg %d broken: r=%zd rnfds=%d (exp %d)",
              i, r, rnfds, exp_nfds);
        if ((i & 3) == 1 || (i & 3) == 3) {
            char tag[32];
            snprintf(tag, sizeof(tag), "DBUS_EPOUT_M_%02d", i);
            verify_memfd_tag(rfds[0], tag);
        }
        for (int k = 0; k < rnfds; k++)
            close(rfds[k]);
        atomic_fetch_add_explicit(c->received, 1, memory_order_release);
    }
    return NULL;
}

static void test_scm_rights_epollout_dbus_broker(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds failed");

    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0,
          "socketpair failed");
    int ep = epoll_create1(EPOLL_CLOEXEC);
    CHECK(ep >= 0, "epoll_create1 failed");
    struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.fd = sv[0] };
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev) == 0, "epoll_ctl ADD failed");

    /* 1. Drain initial EPOLLOUT edge and confirm edge is armed & quiet */
    struct epoll_event out;
    int n = epoll_wait(ep, &out, 1, 100);
    CHECK(n == 1 && (out.events & EPOLLOUT),
          "initial EPOLLET EPOLLOUT drain failed: n=%d events=0x%x", n, n > 0 ? out.events : 0);
    CHECK(epoll_wait(ep, &out, 1, 0) == 0, "EPOLLET EPOLLOUT fired twice before any send!");

    /* 2. Single-shot sendmsg(16B + SCM_RIGHTS) -> peer recvmsg -> assert EPOLLET EPOLLOUT
     *    across eventfd, memfd, pipe (sideband ring) and AF_UNIX (SKB control) */
    const char *kind_names[4] = { "eventfd", "memfd", "pipe", "AF_UNIX" };
    for (int k = 0; k < 4; k++) {
        int send_fd = -1;
        int extra_fd = -1;
        if (k == 0) {
            send_fd = eventfd(0, EFD_CLOEXEC);
            CHECK(send_fd >= 0, "eventfd failed");
        } else if (k == 1) {
            send_fd = make_tagged_memfd("EPOUT_SINGLE_MFD", 0);
        } else if (k == 2) {
            int pp[2];
            CHECK(pipe2(pp, O_CLOEXEC) == 0, "pipe2 failed");
            send_fd = pp[0];
            extra_fd = pp[1];
        } else {
            int dummy[2];
            CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, dummy) == 0, "dummy sp");
            send_fd = dummy[0];
            extra_fd = dummy[1];
        }

        char pl[16] = "hello_scm_fd!!!";
        CHECK(send_with_fds(sv[0], pl, 16, &send_fd, 1, MSG_DONTWAIT | MSG_NOSIGNAL) == 16,
              "send_with_fds(%s) failed: %s", kind_names[k], strerror(errno));
        CHECK(epoll_wait(ep, &out, 1, 0) == 0,
              "spurious EPOLLOUT before peer recvmsg (%s)", kind_names[k]);

        char rb[16] = {0};
        int rfds[16], rnfds = 0, mflags = 0;
        ssize_t r = recv_with_fds(sv[1], rb, 16, rfds, 16, &rnfds, &mflags,
                                  MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        CHECK(r == 16 && memcmp(rb, pl, 16) == 0 && rnfds == 1,
              "peer recv_with_fds(%s) failed: r=%zd rnfds=%d", kind_names[k], r, rnfds);
        if (k == 1)
            verify_memfd_tag(rfds[0], "EPOUT_SINGLE_MFD");
        close(rfds[0]);

        n = epoll_wait(ep, &out, 1, 500);
        CHECK(n == 1 && (out.events & EPOLLOUT),
              "LOST EPOLLET|EPOLLOUT wakeup after peer consumed SCM_RIGHTS(%s)! n=%d events=0x%x",
              kind_names[k], n, n > 0 ? out.events : 0);
        CHECK(epoll_wait(ep, &out, 1, 0) == 0,
              "EPOLLET|EPOLLOUT edge did not clear after delivery (%s)", kind_names[k]);

        close(send_fd);
        if (extra_fd >= 0) close(extra_fd);
    }

    /* 2b. GDBus 16B-header + 176B-body split read of 192B SCM_RIGHTS(memfd) message */
    {
        int mfd = make_tagged_memfd("GDBUS_SPLIT_HDR", 0);
        uint8_t msg192[192];
        for (int i = 0; i < 192; i++) msg192[i] = (uint8_t)(i ^ 0x5A);
        CHECK(send_with_fds(sv[0], msg192, 192, &mfd, 1, MSG_DONTWAIT | MSG_NOSIGNAL) == 192,
              "GDBus 192B send_with_fds failed");

        uint8_t hdr16[16] = {0}, body176[176] = {0};
        int rfds[16], rnfds = 0, mflags = 0;
        ssize_t r = recv_with_fds(sv[1], hdr16, 16, rfds, 16, &rnfds, &mflags,
                                  MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        CHECK(r == 16 && memcmp(hdr16, msg192, 16) == 0 && rnfds == 1,
              "GDBus 16B header recv failed: r=%zd rnfds=%d", r, rnfds);
        verify_memfd_tag(rfds[0], "GDBUS_SPLIT_HDR");
        close(rfds[0]);
        CHECK(recv(sv[1], body176, 176, MSG_DONTWAIT) == 176 &&
              memcmp(body176, msg192 + 16, 176) == 0,
              "GDBus 176B body recv failed");

        n = epoll_wait(ep, &out, 1, 500);
        CHECK(n == 1 && (out.events & EPOLLOUT),
              "LOST EPOLLET|EPOLLOUT wakeup after GDBus split 16B+176B SCM_RIGHTS read! n=%d events=0x%x",
              n, n > 0 ? out.events : 0);
        CHECK(epoll_wait(ep, &out, 1, 0) == 0, "GDBus EPOLLET edge did not clear");
        close(mfd);
    }

    /* 3. Exact dbus-broker flow-control pattern: 16 back-to-back sendmmsg/sendmsg
     *    SCM_RIGHTS sends, holding sent FD open until EPOLLET|EPOLLOUT wakes sender */
    {
        const int N_MSGS = 16;
        atomic_int received = 0;
        pthread_barrier_t bar;
        pthread_barrier_init(&bar, NULL, 2);
        struct dbus_epout_rx_ctx rctx = {
            .fd = sv[1], .count = N_MSGS, .received = &received, .bar = &bar
        };
        pthread_t rth;
        CHECK(pthread_create(&rth, NULL, dbus_epout_rx_fn, &rctx) == 0, "pthread_create rx");

        pthread_barrier_wait(&bar);
        uint64_t t0 = now_ns();

        for (int i = 0; i < N_MSGS; i++) {
            int fds_to_send[2] = { -1, -1 };
            int extra_close = -1;
            int nfds = 1;
            if ((i & 3) == 0) {
                fds_to_send[0] = eventfd(0, EFD_CLOEXEC);
                CHECK(fds_to_send[0] >= 0, "eventfd failed");
            } else if ((i & 3) == 1) {
                char tag[32];
                snprintf(tag, sizeof(tag), "DBUS_EPOUT_M_%02d", i);
                fds_to_send[0] = make_tagged_memfd(tag, 0);
            } else if ((i & 3) == 2) {
                int pp[2];
                CHECK(pipe2(pp, O_CLOEXEC) == 0, "pipe2 failed");
                fds_to_send[0] = pp[0];
                extra_close = pp[1];
            } else {
                char tag[32];
                snprintf(tag, sizeof(tag), "DBUS_EPOUT_M_%02d", i);
                fds_to_send[0] = make_tagged_memfd(tag, 0);
                fds_to_send[1] = eventfd(1, EFD_CLOEXEC);
                CHECK(fds_to_send[1] >= 0, "eventfd2 failed");
                nfds = 2;
            }

            char pl[32];
            snprintf(pl, sizeof(pl), "DBUS_FD_MSG_%02d!!", i);
            struct iovec iov = { .iov_base = pl, .iov_len = 16 };
            union {
                char buf[CMSG_SPACE(sizeof(int) * 2)];
                struct cmsghdr align;
            } cu;
            memset(&cu, 0, sizeof(cu));
            struct mmsghdr mm;
            memset(&mm, 0, sizeof(mm));
            mm.msg_hdr.msg_iov = &iov;
            mm.msg_hdr.msg_iovlen = 1;
            mm.msg_hdr.msg_control = cu.buf;
            mm.msg_hdr.msg_controllen = CMSG_SPACE((size_t)nfds * sizeof(int));
            struct cmsghdr *cm = CMSG_FIRSTHDR(&mm.msg_hdr);
            cm->cmsg_level = SOL_SOCKET;
            cm->cmsg_type = SCM_RIGHTS;
            cm->cmsg_len = CMSG_LEN((size_t)nfds * sizeof(int));
            memcpy(CMSG_DATA(cm), fds_to_send, (size_t)nfds * sizeof(int));

            if ((i & 1) == 0) {
                int sm = sendmmsg(sv[0], &mm, 1, MSG_DONTWAIT | MSG_NOSIGNAL);
                CHECK(sm == 1 && mm.msg_len == 16,
                      "dbus-broker sendmmsg msg %d failed: sm=%d len=%u errno=%d",
                      i, sm, mm.msg_len, errno);
            } else {
                ssize_t w = sendmsg(sv[0], &mm.msg_hdr, MSG_DONTWAIT | MSG_NOSIGNAL);
                CHECK(w == 16, "dbus-broker sendmsg msg %d failed: w=%zd errno=%d", i, w, errno);
            }

            /* Pause outbound queue holding sent FD(s) open until EPOLLET|EPOLLOUT fires! */
            n = epoll_wait(ep, &out, 1, 500);
            CHECK(n == 1 && (out.events & EPOLLOUT),
                  "dbus-broker flow-control STALLED waiting for EPOLLET|EPOLLOUT at msg %d/16! (n=%d events=0x%x)",
                  i, n, n > 0 ? out.events : 0);

            /* Only close sent FD(s) AFTER EPOLLOUT arrives (exact dbus-broker semantics) */
            for (int k = 0; k < nfds; k++)
                close(fds_to_send[k]);
            if (extra_close >= 0)
                close(extra_close);
        }

        pthread_join(rth, NULL);
        uint64_t t1 = now_ns();
        pthread_barrier_destroy(&bar);

        double dt_ms = (double)(t1 - t0) / 1e6;
        CHECK(atomic_load(&received) == N_MSGS,
              "dbus-broker rx count mismatch: %d/%d", atomic_load(&received), N_MSGS);
        CHECK(dt_ms < 100.0,
              "dbus-broker 16x SCM_RIGHTS EPOLLOUT flow-control too slow / stalled: %.2f ms (>= 100 ms)",
              dt_ms);

        close(ep);
        close(sv[0]);
        close(sv[1]);

        CHECK(count_open_fds() == fd_base,
              "FD leak in test_scm_rights_epollout_dbus_broker: %d -> %d",
              fd_base, count_open_fds());
        printf("[PASS] M1 Torture F: dbus-broker SCM_RIGHTS EPOLLET|EPOLLOUT wakeup (eventfd/memfd/pipe/AF_UNIX + GDBus split + 16x flow-control in %.2f ms)\n",
               dt_ms);
    }
}

/* =========================================================================
 * MILESTONE 1 TORTURE G: AF_UNIX Dynamic Small-Msg Ring Expansion & 25% Watermark
 * ========================================================================= */
static void test_unix_ring_expand_and_watermark(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds failed");

    /* 1. Burst 200x 512B small messages (100 KiB total, all < 4096B) non-blocking into
     *    fresh AF_UNIX SOCK_STREAM without reading -> ring MUST expand past 16 KiB (> 64 KiB)!
     *    Test both send() (unix_ring_send_ubuf_slow) and 3-iovec sendmsg() (direct_sendv_slow). */
    for (int mode = 0; mode < 2; mode++) {
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0,
              "socketpair failed");

        const int N_PKTS = 200;
        const size_t PKT_SZ = 512;
        const size_t TOTAL_WANT = (size_t)N_PKTS * PKT_SZ; /* 102,400 B = 100 KiB */
        uint8_t *sent_log = malloc(TOTAL_WANT);
        CHECK(sent_log != NULL, "malloc sent_log");
        size_t sent = 0;

        for (int i = 0; i < N_PKTS; i++) {
            uint8_t pkt[512];
            for (size_t b = 0; b < PKT_SZ; b++)
                pkt[b] = (uint8_t)(((sent + b) * 59U + 13U + (unsigned)mode) & 0xffU);
            ssize_t w;
            if (mode == 0) {
                w = send(sv[0], pkt, PKT_SZ, MSG_DONTWAIT | MSG_NOSIGNAL);
            } else {
                struct iovec iov[3] = {
                    { .iov_base = pkt,       .iov_len = 16  },
                    { .iov_base = pkt + 16,  .iov_len = 48  },
                    { .iov_base = pkt + 64,  .iov_len = 448 }
                };
                struct msghdr msg = { .msg_iov = iov, .msg_iovlen = 3 };
                w = sendmsg(sv[0], &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
            }
            if (w > 0) {
                memcpy(sent_log + sent, pkt, (size_t)w);
                sent += (size_t)w;
                if ((size_t)w < PKT_SZ)
                    break;
            } else {
                CHECK(w == -1 && errno == EAGAIN,
                      "small-burst mode=%d unexpected w=%zd errno=%d", mode, w, errno);
                break;
            }
        }
        CHECK(sent > 65536,
              "AF_UNIX ring trapped at 16 KiB on 512B small burst (mode=%d): accepted only %zu B (need > 64 KiB)!",
              mode, sent);

        /* Verify every single byte reads back intact */
        uint8_t *rx_buf = malloc(sent);
        CHECK(rx_buf != NULL, "malloc rx_buf");
        size_t got = 0;
        while (got < sent) {
            ssize_t r = recv(sv[1], rx_buf + got, sent - got, MSG_DONTWAIT);
            CHECK(r > 0, "small-burst drain mode=%d failed at %zu/%zu (errno=%d)", mode, got, sent, errno);
            got += (size_t)r;
        }
        CHECK(memcmp(rx_buf, sent_log, sent) == 0, "small-burst mode=%d byte corruption!", mode);
        CHECK(recv(sv[1], rx_buf, 16, MSG_DONTWAIT) == -1 && errno == EAGAIN, "residual bytes left");
        free(rx_buf);
        free(sent_log);

        /* 2. On mode == 0 (already expanded ring): fill completely to -EAGAIN, drain 25%+ (64+ KiB),
         *    and verify BOTH epoll_wait(EPOLLET | EPOLLOUT) AND poll(POLLOUT) immediately report writable! */
        if (mode == 0) {
            int ep = epoll_create1(EPOLL_CLOEXEC);
            CHECK(ep >= 0, "epoll_create1 failed");
            struct epoll_event ev = { .events = EPOLLOUT | EPOLLET, .data.fd = sv[0] };
            CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev) == 0, "epoll_ctl ADD failed");

            struct epoll_event out;
            int n = epoll_wait(ep, &out, 1, 100);
            CHECK(n == 1 && (out.events & EPOLLOUT), "initial EPOLLOUT drain failed");
            CHECK(epoll_wait(ep, &out, 1, 0) == 0, "initial EPOLLOUT edge did not clear");

            uint8_t page[4096];
            memset(page, 0xE7, sizeof(page));
            size_t filled = 0;
            for (;;) {
                ssize_t w = send(sv[0], page, sizeof(page), MSG_DONTWAIT | MSG_NOSIGNAL);
                if (w > 0) {
                    filled += (size_t)w;
                    CHECK(filled <= 8 * 1024 * 1024, "ring fill exceeded 8 MiB unexpectedly");
                } else {
                    CHECK(w == -1 && errno == EAGAIN, "ring fill unexpected errno=%d", errno);
                    break;
                }
            }
            CHECK(filled >= 128 * 1024, "expanded ring unexpectedly small: %zu B", filled);

            struct pollfd pfd = { .fd = sv[0], .events = POLLOUT };
            CHECK(poll(&pfd, 1, 0) == 0, "full ring poll(POLLOUT) unexpectedly writable (0x%x)", pfd.revents);
            CHECK(epoll_wait(ep, &out, 1, 0) == 0, "full ring epoll(EPOLLOUT) unexpectedly fired");

            /* Drain 25%+ (on 256 KiB ring: 64 KiB + 4 KiB = 68 KiB, well below old 50% = 128 KiB!) */
            size_t to_drain = (filled / 4) + 4096;
            if (to_drain < 65536) to_drain = 65536;
            if (to_drain > filled) to_drain = filled;
            uint8_t drain_buf[4096];
            size_t drained = 0;
            while (drained < to_drain) {
                size_t want = to_drain - drained;
                if (want > sizeof(drain_buf)) want = sizeof(drain_buf);
                ssize_t r = recv(sv[1], drain_buf, want, MSG_DONTWAIT);
                CHECK(r > 0, "watermark drain failed at %zu/%zu", drained, to_drain);
                drained += (size_t)r;
            }

            /* Both EPOLLET|EPOLLOUT and poll(POLLOUT) MUST immediately report writable! */
            n = epoll_wait(ep, &out, 1, 500);
            CHECK(n == 1 && (out.events & EPOLLOUT),
                  "EPOLLET|EPOLLOUT wedged after draining %zu/%zu B (25%%+ watermark)! n=%d events=0x%x",
                  drained, filled, n, n > 0 ? out.events : 0);
            CHECK(poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLOUT),
                  "poll(POLLOUT) wedged on tx_ring_full after draining %zu/%zu B! revents=0x%x",
                  drained, filled, pfd.revents);

            /* Confirm a fresh non-blocking write immediately succeeds */
            CHECK(send(sv[0], page, sizeof(page), MSG_DONTWAIT | MSG_NOSIGNAL) == (ssize_t)sizeof(page),
                  "post-watermark send failed");

            close(ep);
        }

        close(sv[0]);
        close(sv[1]);
    }

    CHECK(count_open_fds() == fd_base, "FD leak in test_unix_ring_expand_and_watermark: %d -> %d",
          fd_base, count_open_fds());
    printf("[PASS] M1 Torture G: AF_UNIX 512B small-burst expansion (>64 KiB, send+3iov) & 25%% watermark EPOLLET|EPOLLOUT + poll(POLLOUT)\n");
}

struct siocoutq_race_ctx {
    int tx_fd;
    int rx_fd;
    int iters;
    atomic_int *ready;
};

static void *siocoutq_gdbus_consumer_fn(void *arg) {
    struct siocoutq_race_ctx *ctx = arg;
    uint8_t hdr[16], body[512];
    atomic_store_explicit(ctx->ready, 1, memory_order_release);

    for (int i = 0; i < ctx->iters; i++) {
        /* Step 1: GDBus 16-byte D-Bus header recvmsg() (pops inline SCM_RIGHTS fd while 240B remain!) */
        char cbuf[CMSG_SPACE(sizeof(int))];
        struct iovec iov = { .iov_base = hdr, .iov_len = sizeof(hdr) };
        struct msghdr mh = {
            .msg_iov = &iov, .msg_iovlen = 1,
            .msg_control = cbuf, .msg_controllen = sizeof(cbuf),
        };
        ssize_t r;
        do { r = recvmsg(ctx->rx_fd, &mh, 0); } while (r < 0 && errno == EINTR);
        CHECK(r == 16, "GDBus header recvmsg iter=%d r=%zd errno=%d", i, r, errno);

        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        CHECK(cm && cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS &&
              cm->cmsg_len == CMSG_LEN(sizeof(int)), "missing SCM_RIGHTS at iter=%d", i);
        int rfd = -1;
        memcpy(&rfd, CMSG_DATA(cm), sizeof(int));
        CHECK(rfd >= 0, "bad rfd iter=%d", i);
        close(rfd);

        /* Jitter so dbus-broker thread frequently wakes on EPOLLOUT #1 while SIOCOUTQ == 240 > 128 */
        for (volatile int s = 0; s < (i & 63); s++) {}

        /* Step 2: GDBus 240-byte body read (alternate recvmsg / recv / read) */
        size_t rem = 240;
        while (rem > 0) {
            ssize_t br;
            if ((i % 3) == 0) {
                struct iovec biov = { .iov_base = body + (240 - rem), .iov_len = rem };
                struct msghdr bmh = { .msg_iov = &biov, .msg_iovlen = 1 };
                br = recvmsg(ctx->rx_fd, &bmh, 0);
            } else if ((i % 3) == 1) {
                br = recv(ctx->rx_fd, body + (240 - rem), rem, 0);
            } else {
                br = read(ctx->rx_fd, body + (240 - rem), rem);
            }
            if (br < 0 && errno == EINTR) continue;
            CHECK(br > 0, "GDBus body read iter=%d br=%zd errno=%d", i, br, errno);
            rem -= (size_t)br;
        }
        CHECK(hdr[0] == (uint8_t)(i & 0xFF) && body[239] == (uint8_t)((i ^ 0xA5) & 0xFF),
              "GDBus payload mismatch iter=%d", i);
    }
    return NULL;
}

/*
 * M1 Torture H: Exact reproduction of /usr/bin/dbus-broker socket_dispatch_write()
 * (0x2f0b5..0x2f0d0: EPOLLET|EPOLLOUT + ioctl(SIOCOUTQ) <= 128) paired with:
 *   H1. GDBus 2-step read (16B header pops SCM_RIGHTS while 240B > 128B remain in rx_ring)
 *   H2. sendmmsg mixed batch [64B + inline memfd FD, 256B plain]
 *   H3. sendmmsg mixed batch [64B + AF_UNIX socketpair FD (skb!), 256B plain (unix_ring!)]
 *   H4. Pure ioctl(SIOCOUTQ) + EPOLLET|EPOLLOUT with zero SCM_RIGHTS
 *   H5. 2,000-iteration cross-core race between dbus-broker SIOCOUTQ<=128 loop and GDBus 2-step reader
 */
static void test_dbus_broker_siocoutq_gdbus_split_and_batch(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds failed");

    int mfd = memfd_create("nz_siocoutq_h", MFD_CLOEXEC);
    CHECK(mfd >= 0, "memfd_create failed");

    /* --- H1: Deterministic GDBus 16B header + 240B body split across recvmsg / recv / read --- */
    for (int rmode = 0; rmode < 3; rmode++) {
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0, "socketpair H1");
        int ep = epoll_create1(EPOLL_CLOEXEC);
        CHECK(ep >= 0, "epoll_create1 H1");
        struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.fd = sv[0] };
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev) == 0, "epoll_ctl H1");
        struct epoll_event out;
        CHECK(epoll_wait(ep, &out, 1, 100) == 1 && (out.events & EPOLLOUT), "init EPOLLOUT H1");
        CHECK(epoll_wait(ep, &out, 1, 0) == 0, "clear EPOLLOUT H1");

        uint8_t msgbuf[256];
        memset(msgbuf, 0x41 + rmode, sizeof(msgbuf));
        char cbuf[CMSG_SPACE(sizeof(int))];
        memset(cbuf, 0, sizeof(cbuf));
        struct iovec iov = { .iov_base = msgbuf, .iov_len = sizeof(msgbuf) };
        struct msghdr mh = {
            .msg_iov = &iov, .msg_iovlen = 1,
            .msg_control = cbuf, .msg_controllen = sizeof(cbuf),
        };
        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &mfd, sizeof(int));

        struct mmsghdr mm = { .msg_hdr = mh };
        CHECK(sendmmsg(sv[0], &mm, 1, MSG_NOSIGNAL) == 1 && mm.msg_len == 256, "sendmmsg H1");
        CHECK(epoll_wait(ep, &out, 1, 0) == 0, "spurious EPOLLOUT before read H1");

        /* Consumer reads 16B header (pops FD while 240B > 128B remain in rx_ring!) */
        uint8_t rx_hdr[16], rx_body[240];
        char rcbuf[CMSG_SPACE(sizeof(int))];
        struct iovec riov = { .iov_base = rx_hdr, .iov_len = sizeof(rx_hdr) };
        struct msghdr rmh = {
            .msg_iov = &riov, .msg_iovlen = 1,
            .msg_control = rcbuf, .msg_controllen = sizeof(rcbuf),
        };
        CHECK(recvmsg(sv[1], &rmh, 0) == 16, "recvmsg 16B H1");
        struct cmsghdr *rcm = CMSG_FIRSTHDR(&rmh);
        CHECK(rcm && rcm->cmsg_type == SCM_RIGHTS, "expected SCM_RIGHTS on 16B header H1");
        int got_fd = -1;
        memcpy(&got_fd, CMSG_DATA(rcm), sizeof(int));
        CHECK(got_fd >= 0, "bad got_fd H1");
        close(got_fd);

        /* EPOLLOUT #1 fires on FD pop, AND SIOCOUTQ is 240 (> 128), so dbus-broker goes back to sleep! */
        CHECK(epoll_wait(ep, &out, 1, 200) == 1 && (out.events & EPOLLOUT), "missing EPOLLOUT #1 H1");
        int outq = -1;
        CHECK(ioctl(sv[0], SIOCOUTQ, &outq) == 0 && outq == 240, "expected SIOCOUTQ=240, got %d", outq);
        CHECK(epoll_wait(ep, &out, 1, 0) == 0, "EPOLLOUT edge did not clear while 240B pending H1");

        /* Consumer reads remaining 240B body */
        ssize_t br = -1;
        if (rmode == 0) {
            struct iovec biov = { .iov_base = rx_body, .iov_len = sizeof(rx_body) };
            struct msghdr bmh = { .msg_iov = &biov, .msg_iovlen = 1 };
            br = recvmsg(sv[1], &bmh, 0);
        } else if (rmode == 1) {
            br = recv(sv[1], rx_body, sizeof(rx_body), 0);
        } else {
            br = read(sv[1], rx_body, sizeof(rx_body));
        }
        CHECK(br == 240, "body read rmode=%d failed br=%zd", rmode, br);

        /* EPOLLOUT #2 MUST fire now AND SIOCOUTQ MUST be 0 (<= 128)! */
        int n = epoll_wait(ep, &out, 1, 200);
        CHECK(n == 1 && (out.events & EPOLLOUT),
              "DEADLOCK REPRODUCED (H1 rmode=%d): EPOLLET|EPOLLOUT never fired when 240B body drained!", rmode);
        CHECK(ioctl(sv[0], SIOCOUTQ, &outq) == 0 && outq == 0, "expected SIOCOUTQ=0 after drain, got %d", outq);

        close(ep);
        close(sv[0]);
        close(sv[1]);
    }

    /* --- H2 & H3: sendmmsg batch [64B + FD (inline memfd OR AF_UNIX skb!), 256B plain] --- */
    int unix_pass_pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, unix_pass_pair) == 0, "socketpair pass");
    for (int use_unix_fd = 0; use_unix_fd <= 1; use_unix_fd++) {
        int pass_fd = use_unix_fd ? unix_pass_pair[0] : mfd;
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0, "socketpair H2/H3");
        int ep = epoll_create1(EPOLL_CLOEXEC);
        CHECK(ep >= 0, "epoll_create1 H2/H3");
        struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.fd = sv[0] };
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev) == 0, "epoll_ctl H2/H3");
        struct epoll_event out;
        CHECK(epoll_wait(ep, &out, 1, 100) == 1, "init EPOLLOUT H2/H3");
        CHECK(epoll_wait(ep, &out, 1, 0) == 0, "clear EPOLLOUT H2/H3");

        uint8_t m1[64], m2[256];
        memset(m1, 0x11, sizeof(m1));
        memset(m2, 0x22, sizeof(m2));
        char cbuf[CMSG_SPACE(sizeof(int))];
        memset(cbuf, 0, sizeof(cbuf));
        struct iovec iov1 = { .iov_base = m1, .iov_len = sizeof(m1) };
        struct iovec iov2 = { .iov_base = m2, .iov_len = sizeof(m2) };
        struct mmsghdr batch[2];
        memset(batch, 0, sizeof(batch));
        batch[0].msg_hdr.msg_iov = &iov1;
        batch[0].msg_hdr.msg_iovlen = 1;
        batch[0].msg_hdr.msg_control = cbuf;
        batch[0].msg_hdr.msg_controllen = sizeof(cbuf);
        struct cmsghdr *cm = CMSG_FIRSTHDR(&batch[0].msg_hdr);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &pass_fd, sizeof(int));

        batch[1].msg_hdr.msg_iov = &iov2;
        batch[1].msg_hdr.msg_iovlen = 1;

        CHECK(sendmmsg(sv[0], batch, 2, MSG_NOSIGNAL) == 2, "sendmmsg batch H2/H3");

        /* Consumer reads msg1 (64B + FD), leaving 256B plain (> 128B) in unix_ring */
        uint8_t rx[256];
        char rcbuf[CMSG_SPACE(sizeof(int))];
        struct iovec riov = { .iov_base = rx, .iov_len = 64 };
        struct msghdr rmh = {
            .msg_iov = &riov, .msg_iovlen = 1,
            .msg_control = rcbuf, .msg_controllen = sizeof(rcbuf),
        };
        CHECK(recvmsg(sv[1], &rmh, 0) == 64, "recvmsg msg1 H2/H3");
        struct cmsghdr *rcm = CMSG_FIRSTHDR(&rmh);
        CHECK(rcm && rcm->cmsg_type == SCM_RIGHTS, "expected SCM_RIGHTS msg1 H2/H3");
        int got_fd = -1;
        memcpy(&got_fd, CMSG_DATA(rcm), sizeof(int));
        close(got_fd);

        CHECK(epoll_wait(ep, &out, 1, 200) == 1 && (out.events & EPOLLOUT), "missing EPOLLOUT #1 H2/H3");
        int outq = -1;
        CHECK(ioctl(sv[0], SIOCOUTQ, &outq) == 0 && outq == 256,
              "expected SIOCOUTQ=256 (use_unix_fd=%d), got %d", use_unix_fd, outq);
        CHECK(epoll_wait(ep, &out, 1, 0) == 0, "EPOLLOUT did not clear H2/H3");

        /* Consumer drains msg2 (256B plain) from unix_ring */
        CHECK(read(sv[1], rx, 256) == 256, "read msg2 H2/H3");
        int n = epoll_wait(ep, &out, 1, 200);
        CHECK(n == 1 && (out.events & EPOLLOUT),
              "DEADLOCK REPRODUCED (use_unix_fd=%d): EPOLLET|EPOLLOUT never fired after msg2 drained!",
              use_unix_fd);
        CHECK(ioctl(sv[0], SIOCOUTQ, &outq) == 0 && outq == 0, "SIOCOUTQ!=0 H2/H3");

        close(ep);
        close(sv[0]);
        close(sv[1]);
    }
    close(unix_pass_pair[0]);
    close(unix_pass_pair[1]);

    /* --- H4: Pure ioctl(SIOCOUTQ) + EPOLLET|EPOLLOUT with ZERO SCM_RIGHTS ever sent --- */
    {
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0, "socketpair H4");
        int ep = epoll_create1(EPOLL_CLOEXEC);
        CHECK(ep >= 0, "epoll_create1 H4");
        struct epoll_event ev = { .events = EPOLLOUT | EPOLLET, .data.fd = sv[0] };
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev) == 0, "epoll_ctl H4");
        struct epoll_event out;
        CHECK(epoll_wait(ep, &out, 1, 100) == 1, "init EPOLLOUT H4");
        CHECK(epoll_wait(ep, &out, 1, 0) == 0, "clear EPOLLOUT H4");

        uint8_t plain[512], rx[512];
        memset(plain, 0x77, sizeof(plain));
        CHECK(write(sv[0], plain, sizeof(plain)) == 512, "write H4");
        int outq = -1;
        CHECK(ioctl(sv[0], SIOCOUTQ, &outq) == 0 && outq == 512, "SIOCOUTQ=512 H4");
        CHECK(read(sv[1], rx, sizeof(rx)) == 512, "read H4");
        CHECK(epoll_wait(ep, &out, 1, 200) == 1 && (out.events & EPOLLOUT),
              "pure SIOCOUTQ + EPOLLET|EPOLLOUT missed wakeup!");
        CHECK(ioctl(sv[0], SIOCOUTQ, &outq) == 0 && outq == 0, "SIOCOUTQ=0 H4");

        close(ep);
        close(sv[0]);
        close(sv[1]);
    }

    /* --- H5: 2,000-iteration cross-core race (dbus-broker SIOCOUTQ<=128 loop vs GDBus 2-step reader) --- */
    {
        int sv[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == 0, "socketpair H5");
        CHECK(fcntl(sv[0], F_SETFL, O_NONBLOCK) == 0, "nonblock tx H5");
        int ep = epoll_create1(EPOLL_CLOEXEC);
        CHECK(ep >= 0, "epoll_create1 H5");
        struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT | EPOLLET, .data.fd = sv[0] };
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev) == 0, "epoll_ctl H5");
        struct epoll_event out;
        CHECK(epoll_wait(ep, &out, 1, 100) == 1, "init EPOLLOUT H5");

        atomic_int ready = 0;
        struct siocoutq_race_ctx rctx = {
            .tx_fd = sv[0], .rx_fd = sv[1], .iters = 2000, .ready = &ready,
        };
        pthread_t cthr;
        CHECK(pthread_create(&cthr, NULL, siocoutq_gdbus_consumer_fn, &rctx) == 0, "pthread_create H5");
        while (!atomic_load_explicit(&ready, memory_order_acquire))
            sched_yield();

        uint8_t txbuf[256];
        memset(txbuf, 0x5A, sizeof(txbuf));
        for (int i = 0; i < rctx.iters; i++) {
            txbuf[0] = (uint8_t)(i & 0xFF);
            txbuf[255] = (uint8_t)((i ^ 0xA5) & 0xFF);
            char cbuf[CMSG_SPACE(sizeof(int))];
            memset(cbuf, 0, sizeof(cbuf));
            struct iovec iov = { .iov_base = txbuf, .iov_len = sizeof(txbuf) };
            struct mmsghdr mm;
            memset(&mm, 0, sizeof(mm));
            mm.msg_hdr.msg_iov = &iov;
            mm.msg_hdr.msg_iovlen = 1;
            mm.msg_hdr.msg_control = cbuf;
            mm.msg_hdr.msg_controllen = sizeof(cbuf);
            struct cmsghdr *cm = CMSG_FIRSTHDR(&mm.msg_hdr);
            cm->cmsg_level = SOL_SOCKET;
            cm->cmsg_type = SCM_RIGHTS;
            cm->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy(CMSG_DATA(cm), &mfd, sizeof(int));

            CHECK(sendmmsg(sv[0], &mm, 1, MSG_NOSIGNAL) == 1 && mm.msg_len == 256,
                  "sendmmsg H5 iter=%d", i);

            /* Exact dbus-broker socket_dispatch_write() condition: wait on EPOLLET|EPOLLOUT until SIOCOUTQ <= 128! */
            for (;;) {
                int outq = -1;
                CHECK(ioctl(sv[0], SIOCOUTQ, &outq) == 0, "SIOCOUTQ H5 iter=%d", i);
                if (outq <= 128)
                    break;
                int n = epoll_wait(ep, &out, 1, 1000);
                CHECK(n == 1 && (out.events & EPOLLOUT),
                      "H5 cross-core dbus-broker SIOCOUTQ<=128 wedged at iter=%d (outq=%d)!", i, outq);
            }
        }

        pthread_join(cthr, NULL);
        close(ep);
        close(sv[0]);
        close(sv[1]);
    }

    close(mfd);
    CHECK(count_open_fds() == fd_base, "FD leak in M1 Torture H: %d -> %d", fd_base, count_open_fds());
    printf("[PASS] M1 Torture H: dbus-broker ioctl(SIOCOUTQ)<=128 + GDBus 2-step read + mixed sendmmsg (inline+skb) 2,000-iter race\n");
}

/* =========================================================================
 * MILESTONE 2 TORTURE A: Two-Stage Blocking & Non-Blocking poll()/ppoll()
 * ========================================================================= */
static volatile sig_atomic_t g_m2_sigusr1 = 0;
static void m2_sigusr1_handler(int sig) {
    (void)sig;
    g_m2_sigusr1 = 1;
}

struct m2_poll_waker_arg {
    int mode; /* 0=write_efd, 1=write_pipe, 2=send_sock, 3=kill_sigusr1 */
    int fd;
    pthread_t target_tid;
    int delay_us;
};

static void *m2_poll_waker_fn(void *arg) {
    struct m2_poll_waker_arg *w = arg;
    usleep((useconds_t)w->delay_us);
    if (w->mode == 0) {
        uint64_t v = 1;
        CHECK(write(w->fd, &v, 8) == 8, "waker write efd");
    } else if (w->mode == 1) {
        CHECK(write(w->fd, "P", 1) == 1, "waker write pipe");
    } else if (w->mode == 2) {
        CHECK(send(w->fd, "S", 1, MSG_NOSIGNAL) == 1, "waker send sock");
    } else if (w->mode == 3) {
        pthread_kill(w->target_tid, SIGUSR1);
    }
    return NULL;
}

static void test_m2_small_poll_torture(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds");

    /* 1. nfds == 0 non-blocking and short blocking timeout */
    CHECK(poll(NULL, 0, 0) == 0, "poll(NULL, 0, 0)");
    uint64_t t0 = now_ns();
    CHECK(poll(NULL, 0, 3) == 0, "poll(NULL, 0, 3ms)");
    CHECK(now_ns() - t0 >= 2000000ULL, "poll(NULL, 0, 3ms) woke too early");

    /* 2. Stage-1 Ready-First blocking ppoll() (nfds=4, 10s timeout must return in <5ms!) */
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int pfd[2], usv[2];
    CHECK(efd >= 0 && pipe2(pfd, O_NONBLOCK | O_CLOEXEC) == 0 &&
          socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, usv) == 0,
          "setup M2 poll fds");
    CHECK(send(usv[1], "ready!", 6, 0) == 6, "seed unix ready");

    struct pollfd pfds[12];
    memset(pfds, 0, sizeof(pfds));
    pfds[0] = (struct pollfd){ .fd = efd,    .events = POLLIN,  .revents = 0x7f };
    pfds[1] = (struct pollfd){ .fd = -1,     .events = POLLIN,  .revents = 0x7f };
    pfds[2] = (struct pollfd){ .fd = 99997,  .events = POLLIN,  .revents = 0 };
    pfds[3] = (struct pollfd){ .fd = usv[0], .events = POLLIN,  .revents = 0 };
    pfds[4] = (struct pollfd){ .fd = usv[0], .events = POLLOUT, .revents = 0 }; /* duplicate FD */
    pfds[5] = (struct pollfd){ .fd = pfd[0], .events = POLLIN,  .revents = 0x7f };

    struct timespec ts10 = { .tv_sec = 10, .tv_nsec = 0 };
    t0 = now_ns();
    int rc = ppoll(pfds, 6, &ts10, NULL);
    uint64_t dt = now_ns() - t0;
    CHECK(rc == 3, "Stage-1 ready ppoll expected 3 (POLLNVAL+POLLIN+POLLOUT), got %d", rc);
    CHECK(dt < 50000000ULL, "Stage-1 ready ppoll blocked unexpectedly: %" PRIu64 " ns", dt);
    CHECK(pfds[0].revents == 0 && pfds[1].revents == 0 &&
          (pfds[2].revents & POLLNVAL) &&
          (pfds[3].revents & POLLIN) && !(pfds[3].revents & POLLOUT) &&
          (pfds[4].revents & POLLOUT) && !(pfds[4].revents & POLLIN) &&
          pfds[5].revents == 0,
          "Stage-1 revents mismatch: %x %x %x %x %x %x",
          pfds[0].revents, pfds[1].revents, pfds[2].revents,
          pfds[3].revents, pfds[4].revents, pfds[5].revents);

    char drain[16];
    CHECK(recv(usv[0], drain, sizeof(drain), 0) == 6, "drain usv");

    /* 3. Stage-2 Blocking wakeups across eventfd, pipe, AF_UNIX (nfds=4 and nfds=8) + timeout */
    struct pollfd bpfds[8] = {
        { .fd = efd,    .events = POLLIN },
        { .fd = pfd[0], .events = POLLIN },
        { .fd = usv[0], .events = POLLIN },
        { .fd = -1,     .events = POLLIN },
        { .fd = efd,    .events = POLLIN },
        { .fd = pfd[0], .events = POLLIN },
        { .fd = usv[0], .events = POLLIN },
        { .fd = -1,     .events = POLLIN },
    };
    CHECK(poll(bpfds, 4, 3) == 0, "Stage-2 3ms timeout must return 0");

    for (int m = 0; m < 3; m++) {
        struct m2_poll_waker_arg warg = {
            .mode = m,
            .fd = (m == 0) ? efd : (m == 1 ? pfd[1] : usv[1]),
            .delay_us = 1500
        };
        pthread_t wth;
        CHECK(pthread_create(&wth, NULL, m2_poll_waker_fn, &warg) == 0, "create waker");
        int n = poll(bpfds, 8, 1000);
        pthread_join(wth, NULL);
        CHECK(n == 2, "Stage-2 duplicate-FD wakeup mode %d expected 2, got %d", m, n);
        CHECK((bpfds[m].revents & POLLIN) && (bpfds[m + 4].revents & POLLIN),
              "Stage-2 wakeup revents missing on mode %d", m);
        if (m == 0) { uint64_t v; CHECK(read(efd, &v, 8) == 8, "drain efd"); }
        else if (m == 1) { CHECK(read(pfd[0], drain, 1) == 1, "drain pipe"); }
        else { CHECK(recv(usv[0], drain, 1, 0) == 1, "drain sock"); }
    }

    /* 4. Signal interruption (-ERESTARTNOHAND -> EINTR) on Stage-2 blocking poll */
    struct sigaction sa, osa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = m2_sigusr1_handler;
    CHECK(sigaction(SIGUSR1, &sa, &osa) == 0, "sigaction SIGUSR1");
    g_m2_sigusr1 = 0;
    struct m2_poll_waker_arg sarg = {
        .mode = 3, .target_tid = pthread_self(), .delay_us = 1500
    };
    pthread_t sth;
    CHECK(pthread_create(&sth, NULL, m2_poll_waker_fn, &sarg) == 0, "create sig waker");
    rc = poll(bpfds, 3, 2000);
    int saved_err = errno;
    pthread_join(sth, NULL);
    sigaction(SIGUSR1, &osa, NULL);
    CHECK(rc == -1 && saved_err == EINTR && g_m2_sigusr1 == 1,
          "Stage-2 signal interruption failed: rc=%d errno=%d sig=%d", rc, saved_err, (int)g_m2_sigusr1);

    /* 5. Single-threaded private files_struct (files->count == 1 -> __pollwait_pinfree!) via fork() */
    pid_t pid = fork();
    CHECK(pid >= 0, "fork for single-threaded poll test");
    if (pid == 0) {
        /* Child has files->count == 1! Parent will write to pfd[1] in 2ms. */
        struct pollfd cpfds[2] = {
            { .fd = efd,    .events = POLLIN },
            { .fd = pfd[0], .events = POLLIN },
        };
        int cn = poll(cpfds, 2, 2000);
        if (cn != 1 || !(cpfds[1].revents & POLLIN))
            _exit(2);
        char ch = 0;
        if (read(pfd[0], &ch, 1) != 1 || ch != 'Z')
            _exit(3);
        _exit(0);
    }
    usleep(2000);
    CHECK(write(pfd[1], "Z", 1) == 1, "parent wake child pipe");
    int wstatus = 0;
    CHECK(waitpid(pid, &wstatus, 0) == pid && WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0,
          "single-threaded __pollwait_pinfree child failed: status=0x%x", wstatus);

    /* 6. nfds = 10 (>8 slowpath) sanity check */
    for (int i = 0; i < 10; i++)
        pfds[i] = (struct pollfd){ .fd = (i == 9) ? usv[0] : efd, .events = POLLIN };
    CHECK(send(usv[1], "X", 1, 0) == 1, "send slowpath");
    CHECK(poll(pfds, 10, 500) == 1 && (pfds[9].revents & POLLIN), "nfds=10 slowpath");
    CHECK(recv(usv[0], drain, 1, 0) == 1, "drain slowpath");

    close(efd);
    close(pfd[0]);
    close(pfd[1]);
    close(usv[0]);
    close(usv[1]);
    CHECK(count_open_fds() == fd_base, "FD leak in M2 Torture A: %d -> %d", fd_base, count_open_fds());
    printf("[PASS] M2 Torture A: Two-Stage Small poll()/ppoll() (Ready-First 6-FD, Blocking Wakeup, EINTR, Pin-Free Fork, >8 Slowpath)\n");
}

/* =========================================================================
 * MILESTONE 2 TORTURE B: Hardware CRC32C epoll_ctl Cache & Close Race
 * ========================================================================= */
static void test_m2_epoll_ctl_crc_cache_torture(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds");

    int ep = epoll_create1(EPOLL_CLOEXEC);
    CHECK(ep >= 0, "epoll_create1");

    /* Empty rbtree fast-out check */
    struct epoll_event ev = { .events = EPOLLIN, .data.u64 = 1 };
    int dummy = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    CHECK(epoll_ctl(ep, EPOLL_CTL_MOD, dummy, &ev) == -1 && errno == ENOENT, "empty MOD ENOENT");
    CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, dummy, NULL) == -1 && errno == ENOENT, "empty DEL ENOENT");
    close(dummy);

    /* 512 socketpairs (1024 FDs -> 64x collision factor on 16-slot CRC32C ctl_cache!) */
    const int PAIRS = 512;
    int (*sv)[2] = malloc(sizeof(int[2]) * PAIRS);
    CHECK(sv != NULL, "malloc sv");

    for (int i = 0; i < PAIRS; i++) {
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv[i]) == 0,
              "socketpair %d", i);
        ev.events = EPOLLIN | EPOLLONESHOT;
        ev.data.u32 = (uint32_t)i;
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[i][0], &ev) == 0, "CTL_ADD %d", i);
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[i][0], &ev) == -1 && errno == EEXIST,
              "CTL_ADD duplicate EEXIST %d", i);
    }

    /* dup() aliasing test: two different FDs pointing to SAME struct file * */
    int dup_fd = dup(sv[0][0]);
    CHECK(dup_fd >= 0, "dup");
    ev.events = EPOLLIN;
    ev.data.u32 = 0xdeadbeefU;
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, dup_fd, &ev) == 0, "CTL_ADD dup_fd");
    ev.events = EPOLLIN | EPOLLOUT;
    CHECK(epoll_ctl(ep, EPOLL_CTL_MOD, dup_fd, &ev) == 0, "CTL_MOD dup_fd");
    CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, dup_fd, NULL) == 0, "CTL_DEL dup_fd");
    CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, dup_fd, NULL) == -1 && errno == ENOENT, "CTL_DEL dup_fd ENOENT");
    close(dup_fd);

    /* Trigger, drain, MOD re-arm across colliding cache buckets */
    struct epoll_event outs[64];
    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < PAIRS; i += 8) {
            CHECK(send(sv[i][1], "M", 1, 0) == 1, "send %d", i);
        }
        int total = 0;
        while (total < PAIRS / 8) {
            int n = epoll_wait(ep, outs, 64, 500);
            CHECK(n > 0, "epoll_wait round %d stalled (%d/%d)", round, total, PAIRS / 8);
            for (int k = 0; k < n; k++) {
                uint32_t idx = outs[k].data.u32;
                char c;
                CHECK(recv(sv[idx][0], &c, 1, 0) == 1 && c == 'M', "recv %u", idx);
                /* EPOLLONESHOT is now disarmed; re-arm via EPOLL_CTL_MOD (hits/populates CRC32C cache!) */
                ev.events = EPOLLIN | EPOLLONESHOT;
                ev.data.u32 = idx;
                CHECK(epoll_ctl(ep, EPOLL_CTL_MOD, sv[idx][0], &ev) == 0, "CTL_MOD %u", idx);
                total++;
            }
        }
        CHECK(epoll_wait(ep, outs, 64, 0) == 0, "spurious event after drain");
    }

    /* Close half WITHOUT EPOLL_CTL_DEL (tests eventpoll_release_file ctl_cache invalidation!),
     * then immediately allocate fresh sockets reusing the exact same FD numbers and ADD them! */
    for (int i = 0; i < PAIRS / 2; i++) {
        close(sv[i][0]);
        close(sv[i][1]);
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv[i]) == 0,
              "re-open socketpair %d", i);
        ev.events = EPOLLIN;
        ev.data.u32 = (uint32_t)(10000 + i);
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[i][0], &ev) == 0,
              "re-ADD recycled FD %d (stale ctl_cache bug!)", sv[i][0]);
    }

    for (int i = 0; i < PAIRS; i++) {
        CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, sv[i][0], NULL) == 0, "final DEL %d", i);
        CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, sv[i][0], NULL) == -1 && errno == ENOENT, "double DEL %d", i);
        close(sv[i][0]);
        close(sv[i][1]);
    }
    free(sv);
    close(ep);

    CHECK(count_open_fds() == fd_base, "FD leak in M2 Torture B: %d -> %d", fd_base, count_open_fds());
    printf("[PASS] M2 Torture B: Hardware CRC32C epoll_ctl Cache (1,024-FD ADD/MOD/DEL, dup() Aliasing, Close-Without-DEL FD Reuse)\n");
}

/* =========================================================================
 * MILESTONE 2 TORTURE C: Pin-Free Multi-Leaf epoll_wait & Nested Epoll
 * ========================================================================= */
static void test_m2_epoll_mixed_leaf_torture(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds");

    int ep_outer = epoll_create1(EPOLL_CLOEXEC);
    int ep_inner = epoll_create1(EPOLL_CLOEXEC);
    CHECK(ep_outer >= 0 && ep_inner >= 0, "epoll_create1 outer/inner");

    /* 1. AF_UNIX STREAM, 2. AF_UNIX DGRAM, 3. TCP, 4. UDP, 5. eventfd, 6. pipe, 7. timerfd, 8. pidfd */
    int usv[2], dsv[2], pfd[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, usv) == 0, "unix stream");
    CHECK(socketpair(AF_UNIX, SOCK_DGRAM  | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, dsv) == 0, "unix dgram");
    CHECK(pipe2(pfd, O_NONBLOCK | O_CLOEXEC) == 0, "pipe2");
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    CHECK(efd >= 0 && tfd >= 0, "efd/tfd");

    /* TCP loopback pair */
    int tlfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    struct sockaddr_in sin = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0 };
    CHECK(bind(tlfd, (struct sockaddr *)&sin, sizeof(sin)) == 0 && listen(tlfd, 4) == 0, "tcp listen");
    socklen_t slen = sizeof(sin);
    CHECK(getsockname(tlfd, (struct sockaddr *)&sin, &slen) == 0, "tcp getsockname");
    int tcfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    CHECK(connect(tcfd, (struct sockaddr *)&sin, sizeof(sin)) == 0, "tcp connect");
    int tafd = accept4(tlfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    CHECK(tafd >= 0, "tcp accept4");

    /* UDP loopback pair */
    int urx = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    struct sockaddr_in usin = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0 };
    CHECK(bind(urx, (struct sockaddr *)&usin, sizeof(usin)) == 0, "udp bind");
    slen = sizeof(usin);
    CHECK(getsockname(urx, (struct sockaddr *)&usin, &slen) == 0, "udp getsockname");
    int utx = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    /* pidfd child */
    pid_t child = fork();
    CHECK(child >= 0, "fork pidfd");
    if (child == 0) {
        usleep(2000);
        _exit(0);
    }
    int pidfd = (int)syscall(SYS_pidfd_open, child, 0);
    CHECK(pidfd >= 0, "pidfd_open");

    /* Put eventfd + timerfd + pidfd inside ep_inner, and nest ep_inner + sockets + pipe inside ep_outer! */
    struct epoll_event ev;
    ev = (struct epoll_event){ .events = EPOLLIN, .data.u32 = 101 };
    CHECK(epoll_ctl(ep_inner, EPOLL_CTL_ADD, efd, &ev) == 0, "add efd");
    ev = (struct epoll_event){ .events = EPOLLIN, .data.u32 = 102 };
    CHECK(epoll_ctl(ep_inner, EPOLL_CTL_ADD, tfd, &ev) == 0, "add tfd");
    ev = (struct epoll_event){ .events = EPOLLIN, .data.u32 = 103 };
    CHECK(epoll_ctl(ep_inner, EPOLL_CTL_ADD, pidfd, &ev) == 0, "add pidfd");

    ev = (struct epoll_event){ .events = EPOLLIN, .data.u32 = 1 };
    CHECK(epoll_ctl(ep_outer, EPOLL_CTL_ADD, ep_inner, &ev) == 0, "add nested ep_inner");
    ev = (struct epoll_event){ .events = EPOLLIN | EPOLLET, .data.u32 = 2 };
    CHECK(epoll_ctl(ep_outer, EPOLL_CTL_ADD, usv[0], &ev) == 0, "add unix stream");
    ev = (struct epoll_event){ .events = EPOLLIN, .data.u32 = 3 };
    CHECK(epoll_ctl(ep_outer, EPOLL_CTL_ADD, dsv[0], &ev) == 0, "add unix dgram");
    ev = (struct epoll_event){ .events = EPOLLIN, .data.u32 = 4 };
    CHECK(epoll_ctl(ep_outer, EPOLL_CTL_ADD, tafd, &ev) == 0, "add tcp");
    ev = (struct epoll_event){ .events = EPOLLIN, .data.u32 = 5 };
    CHECK(epoll_ctl(ep_outer, EPOLL_CTL_ADD, urx, &ev) == 0, "add udp");
    ev = (struct epoll_event){ .events = EPOLLIN, .data.u32 = 6 };
    CHECK(epoll_ctl(ep_outer, EPOLL_CTL_ADD, pfd[0], &ev) == 0, "add pipe");

    /* Crucial EPOLLET non-stale check: trigger usv[0], then drain BEFORE epoll_wait -> MUST return 0! */
    CHECK(send(usv[1], "stale?", 6, 0) == 6, "send stale check");
    char tmp[16];
    CHECK(recv(usv[0], tmp, sizeof(tmp), 0) == 6, "drain before epoll_wait");
    struct epoll_event outs[16];
    CHECK(epoll_wait(ep_outer, outs, 16, 0) == 0,
          "EPOLLET returned stale readiness after socket was already drained!");

    /* Now fire ALL 8 leaf sources! */
    uint64_t one = 1;
    CHECK(write(efd, &one, 8) == 8, "fire efd");
    struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = 1000000 } };
    CHECK(timerfd_settime(tfd, 0, &its, NULL) == 0, "fire tfd");
    CHECK(send(usv[1], "U", 1, 0) == 1, "fire unix stream");
    CHECK(send(dsv[1], "D", 1, 0) == 1, "fire unix dgram");
    CHECK(send(tcfd, "T", 1, 0) == 1, "fire tcp");
    CHECK(sendto(utx, "P", 1, 0, (struct sockaddr *)&usin, sizeof(usin)) == 1, "fire udp");
    CHECK(write(pfd[1], "I", 1) == 1, "fire pipe");

    unsigned int outer_mask = 0, inner_mask = 0;
    uint64_t deadline = now_ns() + 2000000000ULL;
    while ((outer_mask != 0x7eU || inner_mask != 0x7U) && now_ns() < deadline) {
        int n = epoll_wait(ep_outer, outs, 16, 20);
        for (int i = 0; i < n; i++) {
            uint32_t id = outs[i].data.u32;
            outer_mask |= (1U << id);
            if (id == 1) {
                struct epoll_event iouts[8];
                int in = epoll_wait(ep_inner, iouts, 8, 0);
                for (int j = 0; j < in; j++) {
                    uint32_t iid = iouts[j].data.u32 - 101;
                    inner_mask |= (1U << iid);
                    if (iid == 0) { uint64_t v; (void)read(efd, &v, 8); }
                    else if (iid == 1) { uint64_t v; (void)read(tfd, &v, 8); }
                    else if (iid == 2) { epoll_ctl(ep_inner, EPOLL_CTL_DEL, pidfd, NULL); }
                }
            } else if (id == 2) { (void)recv(usv[0], tmp, 1, 0); }
            else if (id == 3) { (void)recv(dsv[0], tmp, 1, 0); }
            else if (id == 4) { (void)recv(tafd, tmp, 1, 0); }
            else if (id == 5) { (void)recv(urx, tmp, 1, 0); }
            else if (id == 6) { (void)read(pfd[0], tmp, 1); }
        }
    }
    int wstatus = 0;
    waitpid(child, &wstatus, 0);
    CHECK(outer_mask == 0x7eU && inner_mask == 0x7U,
          "mixed leaf epoll missed events: outer=0x%x inner=0x%x", outer_mask, inner_mask);

    close(pidfd);
    close(tfd);
    close(efd);
    close(pfd[0]); close(pfd[1]);
    close(utx); close(urx);
    struct linger lg = { .l_onoff = 1, .l_linger = 0 };
    setsockopt(tcfd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    close(tcfd); close(tafd); close(tlfd);
    close(dsv[0]); close(dsv[1]);
    close(usv[0]); close(usv[1]);
    close(ep_outer);
    close(ep_inner);

    CHECK(count_open_fds() == fd_base, "FD leak in M2 Torture C: %d -> %d", fd_base, count_open_fds());
    printf("[PASS] M2 Torture C: Pin-Free Multi-Leaf epoll_wait (AF_UNIX Stream/Dgram + TCP + UDP + eventfd + pipe + timerfd + pidfd + Nested Epoll)\n");
}

/* =========================================================================
 * MILESTONE 2 TORTURE D: Unboxed io_uring SEND/RECV + Socket READ/WRITE/WRITEV
 * ========================================================================= */
struct nz_uring {
    int ring_fd;
    void *sq_ring, *cq_ring;
    size_t sq_ring_sz, cq_ring_sz, sqes_sz;
    struct io_uring_sqe *sqes;
    uint32_t *sq_head, *sq_tail, *sq_mask, *sq_array;
    uint32_t *cq_head, *cq_tail, *cq_mask;
    struct io_uring_cqe *cqes;
};

static int nz_uring_init(struct nz_uring *u, uint32_t entries) {
    struct io_uring_params p;
    memset(&p, 0, sizeof(p));
    memset(u, 0, sizeof(*u));
    int fd = (int)syscall(__NR_io_uring_setup, entries, &p);
    if (fd < 0) return -errno;
    u->ring_fd = fd;
    u->sq_ring_sz = p.sq_off.array + p.sq_entries * sizeof(uint32_t);
    u->cq_ring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    u->sqes_sz = p.sq_entries * sizeof(struct io_uring_sqe);

    u->sq_ring = mmap(NULL, u->sq_ring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    CHECK(u->sq_ring != MAP_FAILED, "mmap sq_ring");
    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        u->cq_ring = u->sq_ring;
    } else {
        u->cq_ring = mmap(NULL, u->cq_ring_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
        CHECK(u->cq_ring != MAP_FAILED, "mmap cq_ring");
    }
    u->sqes = mmap(NULL, u->sqes_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    CHECK(u->sqes != MAP_FAILED, "mmap sqes");

    u->sq_head  = (uint32_t *)((char *)u->sq_ring + p.sq_off.head);
    u->sq_tail  = (uint32_t *)((char *)u->sq_ring + p.sq_off.tail);
    u->sq_mask  = (uint32_t *)((char *)u->sq_ring + p.sq_off.ring_mask);
    u->sq_array = (uint32_t *)((char *)u->sq_ring + p.sq_off.array);
    u->cq_head  = (uint32_t *)((char *)u->cq_ring + p.cq_off.head);
    u->cq_tail  = (uint32_t *)((char *)u->cq_ring + p.cq_off.tail);
    u->cq_mask  = (uint32_t *)((char *)u->cq_ring + p.cq_off.ring_mask);
    u->cqes     = (struct io_uring_cqe *)((char *)u->cq_ring + p.cq_off.cqes);
    return 0;
}

static void nz_uring_destroy(struct nz_uring *u) {
    if (u->cq_ring && u->cq_ring != u->sq_ring)
        munmap(u->cq_ring, u->cq_ring_sz);
    if (u->sq_ring)
        munmap(u->sq_ring, u->sq_ring_sz);
    if (u->sqes)
        munmap(u->sqes, u->sqes_sz);
    close(u->ring_fd);
}

static struct io_uring_sqe *nz_uring_get_sqe(struct nz_uring *u) {
    uint32_t tail = *u->sq_tail;
    uint32_t idx = tail & *u->sq_mask;
    struct io_uring_sqe *sqe = &u->sqes[idx];
    memset(sqe, 0, sizeof(*sqe));
    u->sq_array[idx] = idx;
    __atomic_store_n(u->sq_tail, tail + 1, __ATOMIC_RELEASE);
    return sqe;
}

static int nz_uring_submit_and_wait(struct nz_uring *u, uint32_t to_submit, uint32_t min_complete) {
    int r;
    do {
        r = (int)syscall(__NR_io_uring_enter, u->ring_fd, to_submit, min_complete,
                         min_complete ? IORING_ENTER_GETEVENTS : 0, NULL, 0);
        to_submit = 0;
    } while (r < 0 && errno == EINTR);
    return r;
}

static struct io_uring_cqe nz_uring_pop_cqe(struct nz_uring *u) {
    uint32_t head = *u->cq_head;
    uint32_t tail = __atomic_load_n(u->cq_tail, __ATOMIC_ACQUIRE);
    CHECK(head != tail, "expected CQE ready");
    struct io_uring_cqe cqe = u->cqes[head & *u->cq_mask];
    __atomic_store_n(u->cq_head, head + 1, __ATOMIC_RELEASE);
    return cqe;
}

struct m2_chunk_writer_arg {
    int fd;
    int chunks;
    size_t chunk_sz;
};

static void *m2_chunk_writer_fn(void *arg) {
    struct m2_chunk_writer_arg *a = arg;
    uint8_t *buf = malloc(a->chunk_sz);
    CHECK(buf != NULL, "malloc chunk");
    for (int i = 0; i < a->chunks; i++) {
        usleep(1000);
        memset(buf, 0x30 + i, a->chunk_sz);
        CHECK(send(a->fd, buf, a->chunk_sz, MSG_NOSIGNAL) == (ssize_t)a->chunk_sz, "chunk send");
    }
    free(buf);
    return NULL;
}

static void test_m2_iouring_unboxed_and_rw_torture(void) {
    int fd_base = count_open_fds();
    CHECK(fd_base >= 0, "count_open_fds");

    struct nz_uring ring;
    int init_rc = nz_uring_init(&ring, 64);
    CHECK(init_rc == 0, "io_uring_setup failed: %d", init_rc);

    int usv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, usv) == 0, "socketpair");

    /* 1. Unboxed IORING_OP_SEND (128B) + two 64B IORING_OP_RECV checking IORING_CQE_F_SOCK_NONEMPTY! */
    char tx128[128], rx64_a[64] = {0}, rx64_b[64] = {0};
    for (int i = 0; i < 128; i++) tx128[i] = (char)(i + 1);

    struct io_uring_sqe *sqe = nz_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_SEND;
    sqe->fd = usv[0];
    sqe->addr = (uintptr_t)tx128;
    sqe->len = 128;
    sqe->user_data = 1;
    CHECK(nz_uring_submit_and_wait(&ring, 1, 1) >= 0, "submit SEND");
    struct io_uring_cqe cqe = nz_uring_pop_cqe(&ring);
    CHECK(cqe.user_data == 1 && cqe.res == 128, "SEND cqe res=%d", cqe.res);

    sqe = nz_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_RECV;
    sqe->fd = usv[1];
    sqe->addr = (uintptr_t)rx64_a;
    sqe->len = 64;
    sqe->user_data = 2;
    sqe = nz_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_RECV;
    sqe->fd = usv[1];
    sqe->addr = (uintptr_t)rx64_b;
    sqe->len = 64;
    sqe->user_data = 3;
    CHECK(nz_uring_submit_and_wait(&ring, 2, 2) >= 0, "submit 2x RECV");
    struct io_uring_cqe c1 = nz_uring_pop_cqe(&ring);
    struct io_uring_cqe c2 = nz_uring_pop_cqe(&ring);
    if (c1.user_data == 3) { struct io_uring_cqe tmpc = c1; c1 = c2; c2 = tmpc; }
    CHECK(c1.res == 64 && (c1.flags & IORING_CQE_F_SOCK_NONEMPTY),
          "first 64B RECV must set IORING_CQE_F_SOCK_NONEMPTY (res=%d flags=0x%x)", c1.res, c1.flags);
    CHECK(c2.res == 64 && !(c2.flags & IORING_CQE_F_SOCK_NONEMPTY),
          "second 64B RECV must clear IORING_CQE_F_SOCK_NONEMPTY (res=%d flags=0x%x)", c2.res, c2.flags);
    CHECK(memcmp(rx64_a, tx128, 64) == 0 && memcmp(rx64_b, tx128 + 64, 64) == 0, "RECV payload mismatch");

    /* 2. Unboxed async poll retry (RECV submitted on empty socket, then fulfilled by writer) */
    memset(rx64_a, 0, sizeof(rx64_a));
    sqe = nz_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_RECV;
    sqe->fd = usv[1];
    sqe->addr = (uintptr_t)rx64_a;
    sqe->len = 64;
    sqe->user_data = 4;
    CHECK(nz_uring_submit_and_wait(&ring, 1, 0) == 1, "submit async RECV");
    usleep(1000);
    CHECK(send(usv[0], tx128, 64, 0) == 64, "fulfill async RECV");
    CHECK(nz_uring_submit_and_wait(&ring, 0, 1) >= 0, "wait async RECV");
    cqe = nz_uring_pop_cqe(&ring);
    CHECK(cqe.user_data == 4 && cqe.res == 64 && memcmp(rx64_a, tx128, 64) == 0,
          "async poll unboxed RECV failed: res=%d", cqe.res);

    /* 3. Unboxed MSG_WAITALL across 4 delayed 2048B chunks (8192B total) on AF_UNIX AND TCP! */
    uint8_t *rx8k = calloc(1, 8192);
    CHECK(rx8k != NULL, "calloc rx8k");
    struct m2_chunk_writer_arg cw = { .fd = usv[0], .chunks = 4, .chunk_sz = 2048 };
    pthread_t cwth;
    CHECK(pthread_create(&cwth, NULL, m2_chunk_writer_fn, &cw) == 0, "create unix chunk writer");
    sqe = nz_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_RECV;
    sqe->fd = usv[1];
    sqe->addr = (uintptr_t)rx8k;
    sqe->len = 8192;
    sqe->msg_flags = MSG_WAITALL;
    sqe->user_data = 5;
    CHECK(nz_uring_submit_and_wait(&ring, 1, 1) >= 0, "wait MSG_WAITALL unix");
    pthread_join(cwth, NULL);
    cqe = nz_uring_pop_cqe(&ring);
    CHECK(cqe.user_data == 5 && cqe.res == 8192 && rx8k[0] == 0x30 && rx8k[6144] == 0x33,
          "unix MSG_WAITALL unboxed RECV failed: res=%d", cqe.res);

    /* 4. TCP loopback unboxed SEND/RECV + MSG_WAITALL */
    int tlfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in sin = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0 };
    CHECK(bind(tlfd, (struct sockaddr *)&sin, sizeof(sin)) == 0 && listen(tlfd, 4) == 0, "tcp bind/listen");
    socklen_t slen = sizeof(sin);
    CHECK(getsockname(tlfd, (struct sockaddr *)&sin, &slen) == 0, "tcp getsockname");
    int tcfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    CHECK(connect(tcfd, (struct sockaddr *)&sin, sizeof(sin)) == 0, "tcp connect");
    int tafd = accept4(tlfd, NULL, NULL, SOCK_CLOEXEC);
    CHECK(tafd >= 0, "tcp accept");

    memset(rx8k, 0, 8192);
    struct m2_chunk_writer_arg tcw = { .fd = tcfd, .chunks = 4, .chunk_sz = 2048 };
    CHECK(pthread_create(&cwth, NULL, m2_chunk_writer_fn, &tcw) == 0, "create tcp chunk writer");
    sqe = nz_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_RECV;
    sqe->fd = tafd;
    sqe->addr = (uintptr_t)rx8k;
    sqe->len = 8192;
    sqe->msg_flags = MSG_WAITALL;
    sqe->user_data = 6;
    CHECK(nz_uring_submit_and_wait(&ring, 1, 1) >= 0, "wait MSG_WAITALL tcp");
    pthread_join(cwth, NULL);
    cqe = nz_uring_pop_cqe(&ring);
    CHECK(cqe.user_data == 6 && cqe.res == 8192 && rx8k[0] == 0x30 && rx8k[6144] == 0x33,
          "tcp MSG_WAITALL unboxed RECV failed: res=%d", cqe.res);
    free(rx8k);

    /* 5. Socket READ / WRITE / WRITEV fusion (both syscall writev/readv AND io_uring OP_WRITEV/OP_READ!) */
    char seg1[16] = "HDR_0123456789AB";
    char seg2[32] = "BODY_ABCDEFGHIJKLMNOPQRSTUVWXYZ!";
    struct iovec iov2[2] = {
        { .iov_base = seg1, .iov_len = 16 },
        { .iov_base = seg2, .iov_len = 32 },
    };
    CHECK(writev(usv[0], iov2, 2) == 48, "syscall writev unix");
    char rx48[48] = {0};
    sqe = nz_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_READ;
    sqe->fd = usv[1];
    sqe->addr = (uintptr_t)rx48;
    sqe->len = 48;
    sqe->user_data = 7;
    CHECK(nz_uring_submit_and_wait(&ring, 1, 1) >= 0, "io_uring OP_READ unix");
    cqe = nz_uring_pop_cqe(&ring);
    CHECK(cqe.user_data == 7 && cqe.res == 48 &&
          memcmp(rx48, seg1, 16) == 0 && memcmp(rx48 + 16, seg2, 32) == 0,
          "io_uring OP_READ mismatch: res=%d", cqe.res);

    memset(rx48, 0, sizeof(rx48));
    sqe = nz_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_WRITEV;
    sqe->fd = usv[0];
    sqe->addr = (uintptr_t)iov2;
    sqe->len = 2;
    sqe->user_data = 8;
    CHECK(nz_uring_submit_and_wait(&ring, 1, 1) >= 0, "io_uring OP_WRITEV unix");
    cqe = nz_uring_pop_cqe(&ring);
    CHECK(cqe.user_data == 8 && cqe.res == 48, "io_uring OP_WRITEV res=%d", cqe.res);
    CHECK(read(usv[1], rx48, 48) == 48 && memcmp(rx48, seg1, 16) == 0 && memcmp(rx48 + 16, seg2, 32) == 0,
          "read after OP_WRITEV mismatch");

    /* 6. -EFAULT check on invalid user pointer in unboxed SEND / RECV */
    sqe = nz_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_SEND;
    sqe->fd = usv[0];
    sqe->addr = (uintptr_t)~0ULL;
    sqe->len = 64;
    sqe->user_data = 9;
    CHECK(nz_uring_submit_and_wait(&ring, 1, 1) >= 0, "submit EFAULT SEND");
    cqe = nz_uring_pop_cqe(&ring);
    CHECK(cqe.user_data == 9 && cqe.res == -EFAULT, "expected -EFAULT on bad SEND addr, got %d", cqe.res);

    struct linger lg = { .l_onoff = 1, .l_linger = 0 };
    setsockopt(tcfd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    close(tcfd); close(tafd); close(tlfd);
    close(usv[0]); close(usv[1]);
    nz_uring_destroy(&ring);

    CHECK(count_open_fds() == fd_base, "FD leak in M2 Torture D: %d -> %d", fd_base, count_open_fds());
    printf("[PASS] M2 Torture D: Unboxed io_uring SEND/RECV (SOCK_NONEMPTY, Async Poll, MSG_WAITALL, OP_READ/OP_WRITEV, -EFAULT)\n");
}

/* =========================================================================
 * MILESTONE 3 TORTURE A & B: Hardware CRC32Q Futex + WF_SYNC + BORE-EEVDF
 * ========================================================================= */
static void unpin_all_cpus_nb(void) {
    int ncpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus <= 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int i = 0; i < ncpus; i++) CPU_SET(i, &set);
    sched_setaffinity(0, sizeof(set), &set);
}

static inline long sys_futex_nb(uint32_t *uaddr, int op, uint32_t val,
                                const struct timespec *timeout) {
    return syscall(SYS_futex, uaddr, op, val, timeout, NULL, 0);
}

struct m3_futex_lock {
    _Atomic uint32_t state; /* 0=unlocked, 1=locked, 2=contended */
    uint8_t pad[60];
};

static inline void m3_lock_acquire(struct m3_futex_lock *lk) {
    uint32_t c = 0;
    if (atomic_compare_exchange_strong_explicit(&lk->state, &c, 1,
            memory_order_acquire, memory_order_relaxed))
        return;
    if (c != 2)
        c = atomic_exchange_explicit(&lk->state, 2, memory_order_acquire);
    while (c != 0) {
        sys_futex_nb((uint32_t *)&lk->state, FUTEX_WAIT_PRIVATE, 2, NULL);
        c = atomic_exchange_explicit(&lk->state, 2, memory_order_acquire);
    }
}

static inline void m3_lock_release(struct m3_futex_lock *lk) {
    if (atomic_fetch_sub_explicit(&lk->state, 1, memory_order_release) != 1) {
        atomic_store_explicit(&lk->state, 0, memory_order_release);
        sys_futex_nb((uint32_t *)&lk->state, FUTEX_WAKE_PRIVATE, 1, NULL);
    }
}

struct m3_fcontention_arg {
    struct m3_futex_lock *locks;
    uint64_t *counters;
    int nlocks;
    int iters;
};

static void *m3_futex_worker_fn(void *arg) {
    struct m3_fcontention_arg *a = arg;
    for (int i = 0; i < a->iters; i++) {
        int idx = (i * 7) & (a->nlocks - 1);
        m3_lock_acquire(&a->locks[idx]);
        a->counters[idx]++;
        m3_lock_release(&a->locks[idx]);
    }
    return NULL;
}

struct m3_shared_futex_arg {
    _Atomic uint32_t *word;
    int ops;
};

static void *m3_shared_futex_peer_fn(void *arg) {
    struct m3_shared_futex_arg *a = arg;
    for (int i = 1; i <= a->ops; i++) {
        while (atomic_load_explicit(a->word, memory_order_acquire) != (uint32_t)(i * 2 - 1))
            sys_futex_nb((uint32_t *)a->word, FUTEX_WAIT, (uint32_t)(i * 2 - 2), NULL);
        atomic_store_explicit(a->word, (uint32_t)(i * 2), memory_order_release);
        sys_futex_nb((uint32_t *)a->word, FUTEX_WAKE, 1, NULL);
    }
    return NULL;
}

static void test_m3_futex_crc32c_and_shared_torture(void) {
    /* 1. Alignment (-EINVAL) and PROT_NONE (-EFAULT) safety checks */
    alignas(8) uint8_t raw[16] = {0};
    CHECK(sys_futex_nb((uint32_t *)(raw + 1), FUTEX_WAKE_PRIVATE, 1, NULL) == -1 && errno == EINVAL,
          "unaligned +1 futex_wake must return EINVAL");
    CHECK(sys_futex_nb((uint32_t *)(raw + 2), FUTEX_WAIT_PRIVATE, 0, NULL) == -1 && errno == EINVAL,
          "unaligned +2 futex_wait must return EINVAL");

    void *bad = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(bad != MAP_FAILED, "mmap PROT_NONE");
    CHECK(sys_futex_nb((uint32_t *)bad, FUTEX_WAIT_PRIVATE, 0, NULL) == -1 && errno == EFAULT,
          "PROT_NONE futex_wait must return EFAULT (errno=%d)", errno);
    /* Empty-bucket wake fastpath still runs get_futex_key(access_ok); kernel addr must EFAULT */
    CHECK(sys_futex_nb((uint32_t *)(uintptr_t)~3ULL, FUTEX_WAKE_PRIVATE, 1, NULL) == -1 && errno == EFAULT,
          "kernel-addr futex_wake must return EFAULT (errno=%d)", errno);
    munmap(bad, 4096);

    /* 2. 8-thread contended FUTEX_PRIVATE across 64 cacheline locks (CRC32Q private hash) */
    const int NLOCKS = 64, NTHR = 8, ITERS = 15000;
    struct m3_futex_lock *locks = aligned_alloc(64, sizeof(*locks) * NLOCKS);
    uint64_t *counters = calloc(NLOCKS, sizeof(uint64_t));
    CHECK(locks && counters, "alloc futex locks");
    memset(locks, 0, sizeof(*locks) * NLOCKS);
    struct m3_fcontention_arg farg = { .locks = locks, .counters = counters, .nlocks = NLOCKS, .iters = ITERS };
    pthread_t th[NTHR];
    uint64_t t0 = now_ns();
    for (int i = 0; i < NTHR; i++)
        CHECK(pthread_create(&th[i], NULL, m3_futex_worker_fn, &farg) == 0, "create futex thr");
    for (int i = 0; i < NTHR; i++)
        pthread_join(th[i], NULL);
    uint64_t t1 = now_ns();
    uint64_t sum = 0;
    for (int i = 0; i < NLOCKS; i++) sum += counters[i];
    CHECK(sum == (uint64_t)NTHR * ITERS, "futex counter mismatch: %" PRIu64 " vs %d", sum, NTHR * ITERS);
    free(counters);
    free(locks);

    /* 3. Shared MAP_SHARED futex (FUTEX_WAIT / FUTEX_WAKE without PRIVATE -> global CRC32Q hash) */
    _Atomic uint32_t *sh = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(sh != MAP_FAILED, "mmap shared futex");
    atomic_store(sh, 0);
    struct m3_shared_futex_arg sarg = { .word = sh, .ops = 2000 };
    pthread_t sth;
    CHECK(pthread_create(&sth, NULL, m3_shared_futex_peer_fn, &sarg) == 0, "create shared peer");
    for (int i = 1; i <= sarg.ops; i++) {
        atomic_store_explicit(sh, (uint32_t)(i * 2 - 1), memory_order_release);
        sys_futex_nb((uint32_t *)sh, FUTEX_WAKE, 1, NULL);
        while (atomic_load_explicit(sh, memory_order_acquire) != (uint32_t)(i * 2))
            sys_futex_nb((uint32_t *)sh, FUTEX_WAIT, (uint32_t)(i * 2 - 1), NULL);
    }
    pthread_join(sth, NULL);
    munmap((void *)sh, 4096);

    double mops = ((double)(NTHR * ITERS) / 1e6) / ((double)(t1 - t0) / 1e9);
    printf("[PASS] M3 Torture A: Hardware CRC32Q Futex (Private + Shared + -EINVAL/-EFAULT + 8T Contention: %.2f Mops/s)\n",
           mops);
}

struct m3_efd_pp_arg {
    int rx_efd;
    int tx_efd;
    int iters;
};

static void *m3_unpinned_efd_peer(void *arg) {
    struct m3_efd_pp_arg *a = arg;
    unpin_all_cpus_nb();
    uint64_t v = 1;
    for (int i = 0; i < a->iters; i++) {
        CHECK(read(a->rx_efd, &v, 8) == 8, "efd peer read");
        v = 1;
        CHECK(write(a->tx_efd, &v, 8) == 8, "efd peer write");
    }
    return NULL;
}

static atomic_int m3_hog_stop = 0;
static void *m3_cpu_hog_fn(void *arg) {
    (void)arg;
    unpin_all_cpus_nb();
    uint64_t x = 0x123456789abcdefULL;
    while (!atomic_load_explicit(&m3_hog_stop, memory_order_relaxed)) {
        for (int i = 0; i < 4096; i++)
            x = x * 6364136223846793005ULL + 1ULL;
    }
    if (x == 0) printf("%" PRIu64, x);
    return NULL;
}

struct m3_bore_resp_arg {
    int req_efd;
    int ack_efd;
    int frames;
    uint64_t *wake_lat_ns;
    _Atomic uint64_t *send_ts;
};

static void *m3_bore_responder_fn(void *arg) {
    struct m3_bore_resp_arg *a = arg;
    unpin_all_cpus_nb();
    for (int i = 0; i < a->frames; i++) {
        uint64_t v = 0;
        CHECK(read(a->req_efd, &v, 8) == 8, "bore req read");
        uint64_t twake = now_ns();
        uint64_t tsend = atomic_load_explicit(a->send_ts, memory_order_acquire);
        a->wake_lat_ns[i] = twake > tsend ? (twake - tsend) : 0;
        /* Simulate 40 us Wayland compositor / audio DSP short burst (< 250 us Tier 0) */
        while (now_ns() - twake < 40000ULL)
            __asm__ __volatile__("pause" ::: "memory");
        v = 1;
        CHECK(write(a->ack_efd, &v, 8) == 8, "bore ack write");
    }
    return NULL;
}

static int cmp_u64_nb(const void *a, const void *b) {
    uint64_t va = *(const uint64_t *)a, vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

static void test_m3_wfsync_and_bore_scheduler_torture(void) {
    int fd_base = count_open_fds();
    unpin_all_cpus_nb();

    /* 1. Unpinned WF_SYNC eventfd ping-pong (15,000 RTTs) */
    int e1 = eventfd(0, EFD_CLOEXEC), e2 = eventfd(0, EFD_CLOEXEC);
    CHECK(e1 >= 0 && e2 >= 0, "eventfd m3");
    const int EFD_ITERS = 15000;
    struct m3_efd_pp_arg earg = { .rx_efd = e1, .tx_efd = e2, .iters = EFD_ITERS + 500 };
    pthread_t eth;
    CHECK(pthread_create(&eth, NULL, m3_unpinned_efd_peer, &earg) == 0, "create efd peer");
    uint64_t one = 1, rx = 0;
    for (int i = 0; i < 500; i++) {
        CHECK(write(e1, &one, 8) == 8 && read(e2, &rx, 8) == 8, "warmup efd");
    }
    uint64_t t0 = now_ns();
    for (int i = 0; i < EFD_ITERS; i++) {
        CHECK(write(e1, &one, 8) == 8 && read(e2, &rx, 8) == 8, "bench efd");
    }
    uint64_t t1 = now_ns();
    pthread_join(eth, NULL);
    close(e1); close(e2);
    double efd_rtt_ns = (double)(t1 - t0) / EFD_ITERS;

    /* 2. BORE-EEVDF Interactive Wakeup Latency Under 8-Thread CPU Hog Storm */
    int req_efd = eventfd(0, EFD_CLOEXEC), ack_efd = eventfd(0, EFD_CLOEXEC);
    CHECK(req_efd >= 0 && ack_efd >= 0, "bore efds");
    const int NHOGS = 8, FRAMES = 160;
    pthread_t hogs[NHOGS], rth;
    atomic_store(&m3_hog_stop, 0);
    for (int i = 0; i < NHOGS; i++)
        CHECK(pthread_create(&hogs[i], NULL, m3_cpu_hog_fn, NULL) == 0, "create hog");
    usleep(15000); /* Let hogs build >= 4 ms burst_ns so they enter Tier 3 batch slice */

    uint64_t lats[FRAMES];
    _Atomic uint64_t send_ts = 0;
    struct m3_bore_resp_arg barg = {
        .req_efd = req_efd, .ack_efd = ack_efd,
        .frames = FRAMES, .wake_lat_ns = lats, .send_ts = &send_ts
    };
    CHECK(pthread_create(&rth, NULL, m3_bore_responder_fn, &barg) == 0, "create bore resp");
    for (int i = 0; i < FRAMES; i++) {
        usleep(600);
        atomic_store_explicit(&send_ts, now_ns(), memory_order_release);
        one = 1;
        CHECK(write(req_efd, &one, 8) == 8, "bore req write");
        CHECK(read(ack_efd, &rx, 8) == 8, "bore ack read");
    }
    pthread_join(rth, NULL);
    atomic_store(&m3_hog_stop, 1);
    for (int i = 0; i < NHOGS; i++)
        pthread_join(hogs[i], NULL);
    close(req_efd); close(ack_efd);

    qsort(lats, FRAMES, sizeof(uint64_t), cmp_u64_nb);
    double p50_us = (double)lats[FRAMES / 2] / 1000.0;
    double p99_us = (double)lats[(FRAMES * 99) / 100] / 1000.0;

    CHECK(count_open_fds() == fd_base, "FD leak in M3 Torture B: %d -> %d", fd_base, count_open_fds());
    printf("[PASS] M3 Torture B: WF_SYNC Unpinned eventfd RTT = %.1f ns | BORE-EEVDF 8-Hog Wakeup p50 = %.2f us, p99 = %.2f us\n",
           efd_rtt_ns, p50_us, p99_us);
}

int main(void) {
    printf("=== Project NetZero Benchmark & ABI Verification Suite ===\n");
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < 65536) {
        rl.rlim_cur = rl.rlim_max < 65536 ? rl.rlim_max : 65536;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
    pthread_t holder;
    pthread_create(&holder, NULL, bg_holder_fn, NULL);
    usleep(10000); /* Ensure current->files->count >= 2 */

    test_mt_fd_race_torture();
    test_eventfd_abi_and_race();
    test_pipe_and_timerfd_pidfd_abi();
    test_scm_rights_sideband_torture();
    test_unix_sendmsg_iovec_dbus();
    test_accept4_nonblock_race();
    test_scm_rights_epollout_dbus_broker();
    test_unix_ring_expand_and_watermark();
    test_dbus_broker_siocoutq_gdbus_split_and_batch();
    test_m2_small_poll_torture();
    test_m2_epoll_ctl_crc_cache_torture();
    test_m2_epoll_mixed_leaf_torture();
    test_m2_iouring_unboxed_and_rw_torture();
    test_m3_futex_crc32c_and_shared_torture();
    test_m3_wfsync_and_bore_scheduler_torture();

    bench_layer1_syscalls();
    bench_layer2_epoll();
    bench_layer3_af_unix();
    bench_layer4_5_inet();

    atomic_store(&bg_thread_stop, 1);
    pthread_join(holder, NULL);
    printf("=== All NetZero ABI & Performance Checks Completed Cleanly ===\n");
    return 0;
}
