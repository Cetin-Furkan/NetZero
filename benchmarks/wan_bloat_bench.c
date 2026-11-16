#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static inline uint64_t mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static long read_tcp_retrans_segs(void) {
    FILE *f = fopen("/proc/net/snmp", "r");
    if (!f) return 0;
    char hdr[1024], val[1024];
    long retrans = 0;
    while (fgets(hdr, sizeof(hdr), f)) {
        if (strncmp(hdr, "Tcp:", 4) == 0) {
            if (!fgets(val, sizeof(val), f)) break;
            char * save1 = NULL, *save2 = NULL;
            char *h = strtok_r(hdr, " \t\n", &save1);
            char *v = strtok_r(val, " \t\n", &save2);
            while (h && v) {
                if (strcmp(h, "RetransSegs") == 0) {
                    retrans = atol(v);
                    break;
                }
                h = strtok_r(NULL, " \t\n", &save1);
                v = strtok_r(NULL, " \t\n", &save2);
            }
            break;
        }
    }
    fclose(f);
    return retrans;
}

static void *sink_conn_fn(void *arg) {
    int fd = (int)(intptr_t)arg;
    char *buf = malloc(65536);
    while (recv(fd, buf, 65536, 0) > 0) {}
    free(buf);
    close(fd);
    return NULL;
}

static void *echo_conn_fn(void *arg) {
    int fd = (int)(intptr_t)arg;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    char buf[64];
    while (1) {
        ssize_t got = 0;
        while (got < 64) {
            ssize_t r = recv(fd, buf + got, 64 - (size_t)got, 0);
            if (r <= 0) goto out;
            got += r;
        }
        ssize_t sent = 0;
        while (sent < 64) {
            ssize_t w = send(fd, buf + sent, 64 - (size_t)sent, MSG_NOSIGNAL);
            if (w <= 0) goto out;
            sent += w;
        }
    }
out:
    close(fd);
    return NULL;
}

static void *accept_loop_fn(void *arg) {
    int lfd = *(int *)arg;
    while (1) {
        int c = accept(lfd, NULL, NULL);
        if (c < 0) continue;
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&th, &attr, sink_conn_fn, (void *)(intptr_t)c);
        pthread_attr_destroy(&attr);
    }
    return NULL;
}

static int make_listener(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind");
        exit(1);
    }
    listen(fd, 256);
    return fd;
}

static void run_server(void) {
    int sink_lfd = make_listener(9100);
    int echo_lfd = make_listener(9101);
    pthread_t sink_th;
    pthread_create(&sink_th, NULL, accept_loop_fn, &sink_lfd);

    while (1) {
        int c = accept(echo_lfd, NULL, NULL);
        if (c < 0) continue;
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&th, &attr, echo_conn_fn, (void *)(intptr_t)c);
        pthread_attr_destroy(&attr);
    }
}

struct bulk_ctx {
    struct sockaddr_in dst;
    const char *cc;
    atomic_int *stop_flag;
    atomic_ullong *bytes_sent;
};

static void *bulk_sender_fn(void *arg) {
    struct bulk_ctx *ctx = arg;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->cc && ctx->cc[0])
        setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, ctx->cc, strlen(ctx->cc));
    if (connect(fd, (struct sockaddr *)&ctx->dst, sizeof(ctx->dst)) < 0) {
        perror("connect bulk");
        close(fd);
        return NULL;
    }
    char *buf = calloc(1, 65536);
    while (!atomic_load_explicit(ctx->stop_flag, memory_order_relaxed)) {
        ssize_t w = send(fd, buf, 65536, MSG_NOSIGNAL);
        if (w > 0)
            atomic_fetch_add_explicit(ctx->bytes_sent, (unsigned long long)w, memory_order_relaxed);
        else if (w < 0 && errno != EINTR)
            break;
    }
    free(buf);
    close(fd);
    return NULL;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static double rpc_ping(int fd) {
    char buf[64] = {0x55};
    uint64_t t0 = mono_ns();
    ssize_t sent = 0;
    while (sent < 64) {
        ssize_t w = send(fd, buf + sent, 64 - (size_t)sent, MSG_NOSIGNAL);
        if (w <= 0) return -1.0;
        sent += w;
    }
    ssize_t got = 0;
    while (got < 64) {
        ssize_t r = recv(fd, buf + got, 64 - (size_t)got, 0);
        if (r <= 0) return -1.0;
        got += r;
    }
    uint64_t t1 = mono_ns();
    return (double)(t1 - t0) / 1e6;
}

static void run_client(const char *srv_ip, const char *bulk_cc, int nbulk, const char *label) {
    struct sockaddr_in echo_dst = {.sin_family = AF_INET, .sin_port = htons(9101)};
    struct sockaddr_in bulk_dst = {.sin_family = AF_INET, .sin_port = htons(9100)};
    inet_pton(AF_INET, srv_ip, &echo_dst.sin_addr);
    inet_pton(AF_INET, srv_ip, &bulk_dst.sin_addr);

    int rpc_fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(rpc_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(rpc_fd, IPPROTO_TCP, TCP_CONGESTION, "bbr", 3);
    if (connect(rpc_fd, (struct sockaddr *)&echo_dst, sizeof(echo_dst)) < 0) {
        perror("connect rpc");
        exit(1);
    }

    double base_rtt = 999.0;
    for (int i = 0; i < 8; i++) {
        double r = rpc_ping(rpc_fd);
        if (r > 0 && r < base_rtt) base_rtt = r;
    }

    atomic_int stop_flag = 0;
    atomic_ullong bytes_sent = 0;
    struct bulk_ctx bctx = {
        .dst = bulk_dst,
        .cc = bulk_cc,
        .stop_flag = &stop_flag,
        .bytes_sent = &bytes_sent,
    };

    long retrans_before = read_tcp_retrans_segs();
    uint64_t start_ns = mono_ns();

    pthread_t *bth = calloc((size_t)nbulk, sizeof(pthread_t));
    for (int i = 0; i < nbulk; i++)
        pthread_create(&bth[i], NULL, bulk_sender_fn, &bctx);

    /* Let bottleneck queue reach steady-state saturation (300 ms) */
    usleep(300000);

    const int SAMPLES = 200;
    double rtts[SAMPLES];
    int valid = 0;
    for (int i = 0; i < SAMPLES; i++) {
        double r = rpc_ping(rpc_fd);
        if (r > 0) rtts[valid++] = r;
        usleep(4000);
    }

    atomic_store(&stop_flag, 1);
    for (int i = 0; i < nbulk; i++)
        pthread_join(bth[i], NULL);
    uint64_t end_ns = mono_ns();
    long retrans_after = read_tcp_retrans_segs();

    close(rpc_fd);
    free(bth);

    qsort(rtts, (size_t)valid, sizeof(double), cmp_double);
    double p50 = valid ? rtts[valid / 2] : -1.0;
    double p99 = valid ? rtts[(valid * 99) / 100] : -1.0;
    double elapsed_s = (double)(end_ns - start_ns) / 1e9;
    double mbps = ((double)atomic_load(&bytes_sent) * 8.0 / 1e6) / elapsed_s;

    printf("[BENCH] %-23s : %6.1f Mbit/s | base=%.2fms p50=%6.2fms p99=%6.2fms | retrans=%ld\n",
           label, mbps, base_rtt, p50, p99, retrans_after - retrans_before);
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--server") == 0) {
        run_server();
        return 0;
    }
    if (argc >= 6 && strcmp(argv[1], "--client") == 0) {
        run_client(argv[2], argv[3], atoi(argv[4]), argv[5]);
        return 0;
    }
    fprintf(stderr, "Usage: %s --server | --client <ip> <cc> <nbulk> <label>\n", argv[0]);
    return 1;
}
