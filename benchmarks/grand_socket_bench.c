// SPDX-License-Identifier: GPL-2.0
// Project NetZero — Grand Multi-Layer Bare-Metal Socket & IPC Benchmark Engine
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <linux/io_uring.h>
#include <linux/futex.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

static inline uint64_t nsec_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline uint64_t rdtsc_ordered(void)
{
	unsigned int aux;
	_mm_lfence();
	return __rdtscp(&aux);
}

static inline double rusage_sys_ms(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return (double)ru.ru_stime.tv_sec * 1000.0 + (double)ru.ru_stime.tv_usec / 1000.0;
}

static void pin_cpu(int cpu)
{
	int ncpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpus <= 0)
		return;
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu % ncpus, &set);
	sched_setaffinity(0, sizeof(set), &set);
}

static void unpin_all_cpus(void)
{
	int ncpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpus <= 0)
		return;
	cpu_set_t set;
	CPU_ZERO(&set);
	for (int i = 0; i < ncpus; i++)
		CPU_SET(i, &set);
	sched_setaffinity(0, sizeof(set), &set);
}

struct gb_futex_lock {
	_Atomic uint32_t state;
	uint8_t pad[60];
};

struct gb_fcont_ctx {
	struct gb_futex_lock *locks;
	int nlocks;
	int iters;
};

static void *gb_futex_cont_worker(void *arg)
{
	struct gb_fcont_ctx *c = arg;
	unpin_all_cpus();
	for (int i = 0; i < c->iters; i++) {
		struct gb_futex_lock *lk = &c->locks[(i * 7) & (c->nlocks - 1)];
		uint32_t expected = 0;
		if (!atomic_compare_exchange_strong_explicit(&lk->state, &expected, 1,
				memory_order_acquire, memory_order_relaxed)) {
			if (expected != 2)
				expected = atomic_exchange_explicit(&lk->state, 2, memory_order_acquire);
			while (expected != 0) {
				syscall(SYS_futex, (uint32_t *)&lk->state, FUTEX_WAIT_PRIVATE, 2, NULL, NULL, 0);
				expected = atomic_exchange_explicit(&lk->state, 2, memory_order_acquire);
			}
		}
		__asm__ __volatile__("" ::: "memory");
		if (atomic_fetch_sub_explicit(&lk->state, 1, memory_order_release) != 1) {
			atomic_store_explicit(&lk->state, 0, memory_order_release);
			syscall(SYS_futex, (uint32_t *)&lk->state, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
		}
	}
	return NULL;
}

struct gb_unpinned_pp_ctx {
	int rx_fd;
	int tx_fd;
	int iters;
};

static void *gb_unpinned_efd_worker(void *arg)
{
	struct gb_unpinned_pp_ctx *c = arg;
	unpin_all_cpus();
	uint64_t val = 1;
	for (int i = 0; i < c->iters; i++) {
		if (read(c->rx_fd, &val, 8) != 8)
			break;
		val = 1;
		if (write(c->tx_fd, &val, 8) != 8)
			break;
	}
	return NULL;
}

static void *gb_unpinned_pipe_worker(void *arg)
{
	struct gb_unpinned_pp_ctx *c = arg;
	unpin_all_cpus();
	char buf[64];
	for (int i = 0; i < c->iters; i++) {
		if (read(c->rx_fd, buf, 64) != 64)
			break;
		if (write(c->tx_fd, buf, 64) != 64)
			break;
	}
	return NULL;
}

static atomic_int gb_hog_stop = 0;
static void *gb_cpu_hog_worker(void *arg)
{
	(void)arg;
	unpin_all_cpus();
	uint64_t v = 0x9e3779b97f4a7c15ULL;
	while (!atomic_load_explicit(&gb_hog_stop, memory_order_relaxed)) {
		for (int i = 0; i < 4096; i++)
			v = v * 6364136223846793005ULL + 1ULL;
	}
	if (v == 0)
		fprintf(stderr, "%lu", (unsigned long)v);
	return NULL;
}

struct gb_bore_ctx {
	int req_efd;
	int ack_efd;
	int frames;
	uint64_t *lats;
	_Atomic uint64_t *send_ts;
};

static void *gb_bore_responder(void *arg)
{
	struct gb_bore_ctx *b = arg;
	unpin_all_cpus();
	for (int i = 0; i < b->frames; i++) {
		uint64_t v = 0;
		if (read(b->req_efd, &v, 8) != 8)
			break;
		uint64_t tw = nsec_now();
		uint64_t ts = atomic_load_explicit(b->send_ts, memory_order_acquire);
		b->lats[i] = tw > ts ? (tw - ts) : 0;
		while (nsec_now() - tw < 40000ULL)
			_mm_pause();
		v = 1;
		if (write(b->ack_efd, &v, 8) != 8)
			break;
	}
	return NULL;
}

static int gb_cmp_u64(const void *a, const void *b)
{
	uint64_t va = *(const uint64_t *)a, vb = *(const uint64_t *)b;
	return (va > vb) - (va < vb);
}

static void raise_nofile_limit(void)
{
	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		rlim_t target = rl.rlim_max < 65536 ? rl.rlim_max : 65536;
		if (rl.rlim_cur < target) {
			rl.rlim_cur = target;
			setrlimit(RLIMIT_NOFILE, &rl);
		}
	}
}

static int g_trial_offset = 0;

static void emit_sample(const char *suite, const char *metric, int trial,
			double val, const char *unit, const char *better)
{
	printf("{\"suite\":\"%s\",\"metric\":\"%s\",\"trial\":%d,\"value\":%.6f,"
	       "\"unit\":\"%s\",\"better\":\"%s\"}\n",
	       suite, metric, trial + g_trial_offset, val, unit, better);
	fflush(stdout);
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t va = *(const uint64_t *)a;
	uint64_t vb = *(const uint64_t *)b;
	return (va > vb) - (va < vb);
}

static long read_meminfo_kib(const char *key)
{
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f)
		return -1;
	char line[256];
	size_t klen = strlen(key);
	long val = -1;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
			val = strtol(line + klen + 1, NULL, 10);
			break;
		}
	}
	fclose(f);
	return val;
}

static long read_sockstat_mem_pages(const char *proto_prefix)
{
	FILE *f = fopen("/proc/net/sockstat", "r");
	if (!f)
		return -1;
	char line[256];
	size_t plen = strlen(proto_prefix);
	long pages = -1;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, proto_prefix, plen) == 0) {
			char *m = strstr(line, "mem ");
			if (m)
				pages = strtol(m + 4, NULL, 10);
			break;
		}
	}
	fclose(f);
	return pages;
}

// Dummy background thread so files->count >= 2 (multi-threaded CLONE_FILES mode!)
static atomic_bool g_keep_mt_alive = true;
static void *mt_holder_fn(void *arg)
{
	(void)arg;
	while (atomic_load_explicit(&g_keep_mt_alive, memory_order_relaxed))
		usleep(20000);
	return NULL;
}

// ============================================================================
// SUITE 1: Kernel Memory & Page Hoarding Stress (N trials)
// ============================================================================
static void run_suite1_memory(int trials)
{
	fprintf(stderr, "[Suite 1] Running Kernel Slab & UDP/TCP Memory Hoarding (%d trials)...\n", trials);

	for (int t = 0; t < trials; t++) {
		// 1A: 1,000 AF_UNIX stream socketpairs (2,000 sockets) + 8,000 x 64B messages
		int (*sv)[2] = malloc(sizeof(int[2]) * 1000);
		char msg64[64];
		memset(msg64, 0x5A, sizeof(msg64));
		usleep(10000);
		long base_sunrec = read_meminfo_kib("SUnreclaim");
		int opened = 0;
		for (int i = 0; i < 1000; i++) {
			if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv[i]) == 0) {
				opened++;
				for (int m = 0; m < 8; m++) {
					ssize_t w = send(sv[i][0], msg64, sizeof(msg64), MSG_DONTWAIT);
					(void)w;
				}
			} else {
				sv[i][0] = sv[i][1] = -1;
			}
		}
		long peak_sunrec = read_meminfo_kib("SUnreclaim");
		for (int i = 0; i < 1000; i++) {
			if (sv[i][0] >= 0) {
				close(sv[i][0]);
				close(sv[i][1]);
			}
		}
		free(sv);
		double delta_kib = (peak_sunrec > base_sunrec) ? (double)(peak_sunrec - base_sunrec) : 0.0;
		if (opened == 1000)
			emit_sample("S1_Memory", "unix_2000socks_8000msg_sunreclaim_kib", t, delta_kib, "KiB", "lower");

		// 1B: 64 QUIC/Game UDP sockets (2 MiB SO_RCVBUF), 32x1200B burst, drain 48 completely + leave 1 pkt on 16
		int usocks[64];
		struct sockaddr_in addrs[64];
		int tx = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
		long udp_base = read_sockstat_mem_pages("UDP:");
		char pkt1200[1200];
		memset(pkt1200, 0x42, sizeof(pkt1200));

		for (int i = 0; i < 64; i++) {
			usocks[i] = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
			int rcvbuf = 2 * 1024 * 1024;
			setsockopt(usocks[i], SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
			memset(&addrs[i], 0, sizeof(addrs[i]));
			addrs[i].sin_family = AF_INET;
			addrs[i].sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			addrs[i].sin_port = 0;
			bind(usocks[i], (struct sockaddr *)&addrs[i], sizeof(addrs[i]));
			socklen_t alen = sizeof(addrs[i]);
			getsockname(usocks[i], (struct sockaddr *)&addrs[i], &alen);
			for (int p = 0; p < 32; p++)
				sendto(tx, pkt1200, sizeof(pkt1200), 0, (struct sockaddr *)&addrs[i], sizeof(addrs[i]));
		}
		// Drain 48 completely; drain 31 of 32 on remaining 16
		char rbuf[1500];
		for (int i = 0; i < 64; i++) {
			int to_read = (i < 48) ? 32 : 31;
			for (int p = 0; p < to_read; p++)
				recv(usocks[i], rbuf, sizeof(rbuf), MSG_DONTWAIT);
		}
		long udp_after = read_sockstat_mem_pages("UDP:");
		close(tx);
		for (int i = 0; i < 64; i++)
			close(usocks[i]);
		long held = (udp_after > udp_base) ? (udp_after - udp_base) : 0;
		emit_sample("S1_Memory", "udp_64quic_socks_hoarded_pages", t, (double)held, "pages", "lower");
	}
}

// ============================================================================
// SUITE 2: Layer 1 & Layer 2 VFS, Hazard-Slot fdget, accept4, cmsg, epoll
// ============================================================================
struct shared_fd_ctx {
	int fd;
	int iters;
	pthread_barrier_t *bar;
};

static void *shared_fd_worker(void *arg)
{
	struct shared_fd_ctx *c = (struct shared_fd_ctx *)arg;
	char buf[16];
	pthread_barrier_wait(c->bar);
	for (int i = 0; i < c->iters; i++) {
		ssize_t r = recv(c->fd, buf, sizeof(buf), MSG_DONTWAIT);
		(void)r;
	}
	return NULL;
}

struct efd_pp_ctx {
	int rx_fd;
	int tx_fd;
	int iters;
	int cpu;
};

static void *eventfd_pong_worker(void *arg)
{
	struct efd_pp_ctx *c = (struct efd_pp_ctx *)arg;
	pin_cpu(c->cpu);
	uint64_t v = 0, one = 1;
	for (int i = 0; i < c->iters; i++) {
		if (read(c->rx_fd, &v, sizeof(v)) != (ssize_t)sizeof(v))
			break;
		if (write(c->tx_fd, &one, sizeof(one)) != (ssize_t)sizeof(one))
			break;
	}
	return NULL;
}

static void *pipe_64b_pong_worker(void *arg)
{
	struct efd_pp_ctx *c = (struct efd_pp_ctx *)arg;
	pin_cpu(c->cpu);
	char buf[64];
	for (int i = 0; i < c->iters; i++) {
		size_t got = 0;
		while (got < 64) {
			ssize_t r = read(c->rx_fd, buf + got, 64 - got);
			if (r <= 0)
				return NULL;
			got += (size_t)r;
		}
		size_t sent = 0;
		while (sent < 64) {
			ssize_t w = write(c->tx_fd, buf + sent, 64 - sent);
			if (w <= 0)
				return NULL;
			sent += (size_t)w;
		}
	}
	return NULL;
}

struct poll_pp_ctx {
	int sock_fd;
	int idle_efd;
	int iters;
	int cpu;
};

static void *poll_2fd_pong_worker(void *arg)
{
	struct poll_pp_ctx *c = (struct poll_pp_ctx *)arg;
	pin_cpu(c->cpu);
	struct pollfd pfds[2] = {
		{ .fd = c->idle_efd, .events = POLLIN },
		{ .fd = c->sock_fd,  .events = POLLIN },
	};
	char buf[64];
	for (int i = 0; i < c->iters; i++) {
		if (poll(pfds, 2, -1) <= 0)
			break;
		if (recv(c->sock_fd, buf, 64, MSG_DONTWAIT) != 64)
			break;
		if (send(c->sock_fd, buf, 64, MSG_NOSIGNAL) != 64)
			break;
	}
	return NULL;
}

struct gb_uring {
	int ring_fd;
	void *sq_ring, *cq_ring;
	size_t sq_ring_sz, cq_ring_sz, sqes_sz;
	struct io_uring_sqe *sqes;
	uint32_t *sq_tail, *sq_mask, *sq_array;
	uint32_t *cq_head, *cq_tail, *cq_mask;
	struct io_uring_cqe *cqes;
};

static int gb_uring_init(struct gb_uring *u, uint32_t entries)
{
	struct io_uring_params p;
	memset(&p, 0, sizeof(p));
	memset(u, 0, sizeof(*u));
	int fd = (int)syscall(__NR_io_uring_setup, entries, &p);
	if (fd < 0)
		return -1;
	u->ring_fd = fd;
	u->sq_ring_sz = p.sq_off.array + p.sq_entries * sizeof(uint32_t);
	u->cq_ring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
	u->sqes_sz = p.sq_entries * sizeof(struct io_uring_sqe);
	u->sq_ring = mmap(NULL, u->sq_ring_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
	if (u->sq_ring == MAP_FAILED) {
		close(fd);
		return -1;
	}
	if (p.features & IORING_FEAT_SINGLE_MMAP) {
		u->cq_ring = u->sq_ring;
	} else {
		u->cq_ring = mmap(NULL, u->cq_ring_sz, PROT_READ | PROT_WRITE,
				  MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
	}
	u->sqes = mmap(NULL, u->sqes_sz, PROT_READ | PROT_WRITE,
		       MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
	u->sq_tail  = (uint32_t *)((char *)u->sq_ring + p.sq_off.tail);
	u->sq_mask  = (uint32_t *)((char *)u->sq_ring + p.sq_off.ring_mask);
	u->sq_array = (uint32_t *)((char *)u->sq_ring + p.sq_off.array);
	u->cq_head  = (uint32_t *)((char *)u->cq_ring + p.cq_off.head);
	u->cq_tail  = (uint32_t *)((char *)u->cq_ring + p.cq_off.tail);
	u->cq_mask  = (uint32_t *)((char *)u->cq_ring + p.cq_off.ring_mask);
	u->cqes     = (struct io_uring_cqe *)((char *)u->cq_ring + p.cq_off.cqes);
	return 0;
}

static void gb_uring_destroy(struct gb_uring *u)
{
	if (u->cq_ring && u->cq_ring != u->sq_ring)
		munmap(u->cq_ring, u->cq_ring_sz);
	if (u->sq_ring)
		munmap(u->sq_ring, u->sq_ring_sz);
	if (u->sqes)
		munmap(u->sqes, u->sqes_sz);
	if (u->ring_fd >= 0)
		close(u->ring_fd);
}

static inline void gb_uring_push_sr(struct gb_uring *u, uint8_t opcode, int fd,
				    void *buf, uint32_t len, uint64_t ud)
{
	uint32_t tail = *u->sq_tail;
	uint32_t idx = tail & *u->sq_mask;
	struct io_uring_sqe *sqe = &u->sqes[idx];
	memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = opcode;
	sqe->fd = fd;
	sqe->addr = (uintptr_t)buf;
	sqe->len = len;
	sqe->user_data = ud;
	u->sq_array[idx] = idx;
	__atomic_store_n(u->sq_tail, tail + 1, __ATOMIC_RELEASE);
}

static void *iouring_pong_worker(void *arg)
{
	struct efd_pp_ctx *c = (struct efd_pp_ctx *)arg;
	pin_cpu(c->cpu);
	struct gb_uring u;
	if (gb_uring_init(&u, 16) != 0)
		return NULL;
	char buf[64];
	for (int i = 0; i < c->iters; i++) {
		gb_uring_push_sr(&u, IORING_OP_RECV, c->rx_fd, buf, 64, 1);
		if (syscall(__NR_io_uring_enter, u.ring_fd, 1, 1,
			    IORING_ENTER_GETEVENTS, NULL, 0) < 0)
			break;
		__atomic_store_n(u.cq_head, *u.cq_head + 1, __ATOMIC_RELEASE);

		gb_uring_push_sr(&u, IORING_OP_SEND, c->rx_fd, buf, 64, 2);
		if (syscall(__NR_io_uring_enter, u.ring_fd, 1, 1,
			    IORING_ENTER_GETEVENTS, NULL, 0) < 0)
			break;
		__atomic_store_n(u.cq_head, *u.cq_head + 1, __ATOMIC_RELEASE);
	}
	gb_uring_destroy(&u);
	return NULL;
}

static void run_suite2_vfs_epoll(int trials)
{
	fprintf(stderr, "[Suite 2] Running VFS, MT Hazard-Slot fdget, accept4, cmsg & epoll (%d trials)...\n", trials);
	pin_cpu(0);

	int sv[2];
	socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv);

	// Warmup
	char tmp[64];
	for (int i = 0; i < 50000; i++)
		recv(sv[0], tmp, sizeof(tmp), MSG_DONTWAIT);

	// 2A: MT recv(-EAGAIN) (1,000,000 calls/trial)
	const int N_RECV = 1000000;
	for (int t = 0; t < trials; t++) {
		uint64_t t0 = nsec_now();
		uint64_t c0 = rdtsc_ordered();
		for (int i = 0; i < N_RECV; i++) {
			ssize_t r = recv(sv[0], tmp, sizeof(tmp), MSG_DONTWAIT);
			(void)r;
		}
		uint64_t c1 = rdtsc_ordered();
		uint64_t t1 = nsec_now();
		emit_sample("S2_VFS_Epoll", "mt_recv_eagain_ns", t, (double)(t1 - t0) / N_RECV, "ns/call", "lower");
		emit_sample("S2_VFS_Epoll", "mt_recv_eagain_cycles", t, (double)(c1 - c0) / N_RECV, "cycles/call", "lower");
	}

	// 2B: 8-thread Shared-FD MESI Contention Storm (8 x 200,000 = 1.6M calls/trial)
	const int N_THR = 8;
	const int PER_THR = 200000;
	for (int t = 0; t < trials; t++) {
		pthread_t th[N_THR];
		pthread_barrier_t bar;
		pthread_barrier_init(&bar, NULL, N_THR + 1);
		struct shared_fd_ctx ctx = { .fd = sv[0], .iters = PER_THR, .bar = &bar };
		for (int k = 0; k < N_THR; k++)
			pthread_create(&th[k], NULL, shared_fd_worker, &ctx);
		pthread_barrier_wait(&bar);
		uint64_t t0 = nsec_now();
		for (int k = 0; k < N_THR; k++)
			pthread_join(th[k], NULL);
		uint64_t t1 = nsec_now();
		pthread_barrier_destroy(&bar);
		double total_calls = (double)N_THR * PER_THR;
		double mops = total_calls / ((double)(t1 - t0) / 1000.0);
		emit_sample("S2_VFS_Epoll", "shared_fd_8threads_mops", t, mops, "Mops/s", "higher");
	}

	// 2C: Fast VFS write()+read() stream round-trip (300,000 pairs/trial)
	const int N_RW = 300000;
	memset(tmp, 0x33, sizeof(tmp));
	for (int t = 0; t < trials; t++) {
		uint64_t t0 = nsec_now();
		for (int i = 0; i < N_RW; i++) {
			if (write(sv[0], tmp, 64) == 64) {
				ssize_t r = read(sv[1], tmp, 64);
				(void)r;
			}
		}
		uint64_t t1 = nsec_now();
		emit_sample("S2_VFS_Epoll", "vfs_write_read_64B_ns", t, (double)(t1 - t0) / N_RW, "ns/pair", "lower");
	}

	// 2D: Lazy accept4(-EAGAIN) on empty non-blocking TCP listener (500,000 calls/trial)
	int lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	struct sockaddr_in laddr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = 0
	};
	bind(lfd, (struct sockaddr *)&laddr, sizeof(laddr));
	listen(lfd, 128);
	const int N_ACC = 500000;
	for (int t = 0; t < trials; t++) {
		uint64_t t0 = nsec_now();
		uint64_t c0 = rdtsc_ordered();
		for (int i = 0; i < N_ACC; i++) {
			int cfd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
			if (cfd >= 0)
				close(cfd);
		}
		uint64_t c1 = rdtsc_ordered();
		uint64_t t1 = nsec_now();
		emit_sample("S2_VFS_Epoll", "mt_accept4_eagain_ns", t, (double)(t1 - t0) / N_ACC, "ns/call", "lower");
		emit_sample("S2_VFS_Epoll", "mt_accept4_eagain_cycles", t, (double)(c1 - c0) / N_ACC, "cycles/call", "lower");
	}
	close(lfd);

	// 2E: 56B cmsg (SCM_RIGHTS + SCM_CREDENTIALS) round-trip (100,000 pairs/trial)
	int csv[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, csv);
	int one = 1;
	setsockopt(csv[1], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one));
	int dummy_pipe[2];
	pipe(dummy_pipe);
	const int N_CMSG = 100000;
	for (int t = 0; t < trials; t++) {
		char payload[16] = "waylandsync1234";
		char rxpayload[16];
		char cbuf_tx[CMSG_SPACE(sizeof(int)) + CMSG_SPACE(sizeof(struct ucred))];
		char cbuf_rx[128];
		memset(cbuf_tx, 0, sizeof(cbuf_tx));

		struct iovec iov_tx = { .iov_base = payload, .iov_len = sizeof(payload) };
		struct msghdr msg_tx = {
			.msg_iov = &iov_tx, .msg_iovlen = 1,
			.msg_control = cbuf_tx, .msg_controllen = sizeof(cbuf_tx)
		};
		struct cmsghdr *cm = CMSG_FIRSTHDR(&msg_tx);
		cm->cmsg_level = SOL_SOCKET;
		cm->cmsg_type = SCM_RIGHTS;
		cm->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cm), &dummy_pipe[0], sizeof(int));

		struct cmsghdr *cm2 = CMSG_NXTHDR(&msg_tx, cm);
		cm2->cmsg_level = SOL_SOCKET;
		cm2->cmsg_type = SCM_CREDENTIALS;
		cm2->cmsg_len = CMSG_LEN(sizeof(struct ucred));
		struct ucred uc = { .pid = getpid(), .uid = getuid(), .gid = getgid() };
		memcpy(CMSG_DATA(cm2), &uc, sizeof(uc));

		uint64_t t0 = nsec_now();
		for (int i = 0; i < N_CMSG; i++) {
			sendmsg(csv[0], &msg_tx, 0);
			struct iovec iov_rx = { .iov_base = rxpayload, .iov_len = sizeof(rxpayload) };
			struct msghdr msg_rx = {
				.msg_iov = &iov_rx, .msg_iovlen = 1,
				.msg_control = cbuf_rx, .msg_controllen = sizeof(cbuf_rx)
			};
			if (recvmsg(csv[1], &msg_rx, MSG_CMSG_CLOEXEC) > 0) {
				for (struct cmsghdr *rc = CMSG_FIRSTHDR(&msg_rx); rc; rc = CMSG_NXTHDR(&msg_rx, rc)) {
					if (rc->cmsg_level == SOL_SOCKET && rc->cmsg_type == SCM_RIGHTS) {
						int rfd;
						memcpy(&rfd, CMSG_DATA(rc), sizeof(int));
						close(rfd);
					}
				}
			}
		}
		uint64_t t1 = nsec_now();
		emit_sample("S2_VFS_Epoll", "cmsg_scm_rights_creds_ns", t, (double)(t1 - t0) / N_CMSG, "ns/pair", "lower");
	}
	close(dummy_pipe[0]);
	close(dummy_pipe[1]);
	close(csv[0]);
	close(csv[1]);
	close(sv[0]);
	close(sv[1]);

	// 2F: epoll_wait 32 ready out of 256 (80,000 calls/trial = 2.56M events/trial)
	{
		int ep = epoll_create1(EPOLL_CLOEXEC);
		int ep_sv[256][2];
		for (int i = 0; i < 256; i++) {
			socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, ep_sv[i]);
			struct epoll_event ev = { .events = EPOLLIN, .data.u32 = (uint32_t)i };
			epoll_ctl(ep, EPOLL_CTL_ADD, ep_sv[i][0], &ev);
			if (i < 32)
				send(ep_sv[i][1], "x", 1, 0);
		}
		struct epoll_event out[64];
		const int N_EP32 = 80000;
		for (int t = 0; t < trials; t++) {
			uint64_t t0 = nsec_now();
			uint64_t c0 = rdtsc_ordered();
			long tot_ev = 0;
			for (int i = 0; i < N_EP32; i++)
				tot_ev += epoll_wait(ep, out, 64, 0);
			uint64_t c1 = rdtsc_ordered();
			uint64_t t1 = nsec_now();
			emit_sample("S2_VFS_Epoll", "epoll_wait_32_of_256_ns_ev", t, (double)(t1 - t0) / tot_ev, "ns/ev", "lower");
			emit_sample("S2_VFS_Epoll", "epoll_wait_32_of_256_cyc_ev", t, (double)(c1 - c0) / tot_ev, "cycles/ev", "lower");
		}
		close(ep);
		for (int i = 0; i < 256; i++) {
			close(ep_sv[i][0]);
			close(ep_sv[i][1]);
		}
	}

	// 2G: epoll_wait 128 ready out of 1,024 (25,000 calls/trial = 3.2M events/trial)
	{
		int ep = epoll_create1(EPOLL_CLOEXEC);
		int (*ep_sv)[2] = malloc(sizeof(int[2]) * 1024);
		for (int i = 0; i < 1024; i++) {
			socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, ep_sv[i]);
			struct epoll_event ev = { .events = EPOLLIN, .data.u32 = (uint32_t)i };
			epoll_ctl(ep, EPOLL_CTL_ADD, ep_sv[i][0], &ev);
			if (i < 128)
				send(ep_sv[i][1], "x", 1, 0);
		}
		struct epoll_event out[256];
		const int N_EP128 = 25000;
		for (int t = 0; t < trials; t++) {
			uint64_t t0 = nsec_now();
			long tot_ev = 0;
			for (int i = 0; i < N_EP128; i++)
				tot_ev += epoll_wait(ep, out, 256, 0);
			uint64_t t1 = nsec_now();
			emit_sample("S2_VFS_Epoll", "epoll_wait_128_of_1024_ns_ev", t, (double)(t1 - t0) / tot_ev, "ns/ev", "lower");
		}
		close(ep);
		for (int i = 0; i < 1024; i++) {
			close(ep_sv[i][0]);
			close(ep_sv[i][1]);
		}
		free(ep_sv);
	}

	// 2H: 2-thread eventfd wakeup ping-pong (PipeWire/GNOME pattern, 100,000 RTTs/trial)
	{
		const int N_EFD = 100000;
		for (int t = 0; t < trials; t++) {
			int e_a2b = eventfd(0, EFD_CLOEXEC);
			int e_b2a = eventfd(0, EFD_CLOEXEC);
			struct efd_pp_ctx ectx = {
				.rx_fd = e_a2b, .tx_fd = e_b2a,
				.iters = N_EFD + 2000, .cpu = 2
			};
			pthread_t th;
			pthread_create(&th, NULL, eventfd_pong_worker, &ectx);
			pin_cpu(0);

			uint64_t one = 1, v = 0;
			for (int i = 0; i < 2000; i++) {
				if (write(e_a2b, &one, 8) != 8)
					break;
				if (read(e_b2a, &v, 8) != 8)
					break;
			}
			uint64_t t0 = nsec_now();
			for (int i = 0; i < N_EFD; i++) {
				if (write(e_a2b, &one, 8) != 8)
					break;
				if (read(e_b2a, &v, 8) != 8)
					break;
			}
			uint64_t t1 = nsec_now();
			pthread_join(th, NULL);
			close(e_a2b);
			close(e_b2a);
			emit_sample("S2_VFS_Epoll", "eventfd_pingpong_ns", t,
				    (double)(t1 - t0) / N_EFD, "ns/RTT", "lower");
		}
	}

	// 2I: 2-thread 64B pipe2 ping-pong (100,000 RTTs/trial)
	{
		const int N_PIPE = 100000;
		for (int t = 0; t < trials; t++) {
			int p_a2b[2], p_b2a[2];
			if (pipe2(p_a2b, O_CLOEXEC) != 0 || pipe2(p_b2a, O_CLOEXEC) != 0)
				continue;
			struct efd_pp_ctx pctx = {
				.rx_fd = p_a2b[0], .tx_fd = p_b2a[1],
				.iters = N_PIPE + 2000, .cpu = 2
			};
			pthread_t th;
			pthread_create(&th, NULL, pipe_64b_pong_worker, &pctx);
			pin_cpu(0);

			char buf[64];
			memset(buf, 0x5A, sizeof(buf));
			for (int i = 0; i < 2000; i++) {
				if (write(p_a2b[1], buf, 64) != 64)
					break;
				size_t got = 0;
				while (got < 64) {
					ssize_t r = read(p_b2a[0], buf + got, 64 - got);
					if (r <= 0)
						break;
					got += (size_t)r;
				}
			}
			uint64_t t0 = nsec_now();
			for (int i = 0; i < N_PIPE; i++) {
				if (write(p_a2b[1], buf, 64) != 64)
					break;
				size_t got = 0;
				while (got < 64) {
					ssize_t r = read(p_b2a[0], buf + got, 64 - got);
					if (r <= 0)
						break;
					got += (size_t)r;
				}
			}
			uint64_t t1 = nsec_now();
			pthread_join(th, NULL);
			close(p_a2b[0]);
			close(p_a2b[1]);
			close(p_b2a[0]);
			close(p_b2a[1]);
			emit_sample("S2_VFS_Epoll", "pipe_64B_pingpong_ns", t,
				    (double)(t1 - t0) / N_PIPE, "ns/RTT", "lower");
		}
	}

	// 2J: Milestone 2 GLib/GTK4 Ready-First Blocking ppoll() on 4 FDs (500,000 calls/trial)
	{
		int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		int p[2], wsv[2], dsv[2];
		if (efd >= 0 && pipe2(p, O_NONBLOCK | O_CLOEXEC) == 0 &&
		    socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, wsv) == 0 &&
		    socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, dsv) == 0) {
			send(wsv[1], "W", 1, 0);
			struct pollfd pfds[4] = {
				{ .fd = efd,    .events = POLLIN },
				{ .fd = p[0],   .events = POLLIN },
				{ .fd = wsv[0], .events = POLLIN },
				{ .fd = dsv[0], .events = POLLIN },
			};
			struct timespec ts = { .tv_sec = 10, .tv_nsec = 0 };
			const int N_PP4 = 500000;
			pin_cpu(0);
			for (int i = 0; i < 5000; i++)
				ppoll(pfds, 4, &ts, NULL);
			for (int t = 0; t < trials; t++) {
				uint64_t t0 = nsec_now();
				for (int i = 0; i < N_PP4; i++)
					ppoll(pfds, 4, &ts, NULL);
				uint64_t t1 = nsec_now();
				emit_sample("S2_VFS_Epoll", "ppoll_4fd_ready_ns", t,
					    (double)(t1 - t0) / N_PP4, "ns/call", "lower");
			}
			close(dsv[0]); close(dsv[1]);
			close(wsv[0]); close(wsv[1]);
			close(p[0]);   close(p[1]);
			close(efd);
		}
	}

	// 2K: Milestone 2 2-thread Blocking poll(2 FDs) + AF_UNIX 64B Ping-Pong (80,000 RTTs/trial)
	{
		const int N_POLL_PP = 80000;
		for (int t = 0; t < trials; t++) {
			int psv[2];
			int efd_a = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
			int efd_b = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
			if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, psv) != 0)
				continue;
			struct poll_pp_ctx pctx = {
				.sock_fd = psv[1], .idle_efd = efd_b,
				.iters = N_POLL_PP + 1000, .cpu = 2
			};
			pthread_t th;
			pthread_create(&th, NULL, poll_2fd_pong_worker, &pctx);
			pin_cpu(0);
			struct pollfd pfds[2] = {
				{ .fd = efd_a,  .events = POLLIN },
				{ .fd = psv[0], .events = POLLIN },
			};
			char buf[64];
			memset(buf, 0x44, sizeof(buf));
			for (int i = 0; i < 1000; i++) {
				send(psv[0], buf, 64, MSG_NOSIGNAL);
				poll(pfds, 2, -1);
				recv(psv[0], buf, 64, MSG_DONTWAIT);
			}
			uint64_t t0 = nsec_now();
			for (int i = 0; i < N_POLL_PP; i++) {
				send(psv[0], buf, 64, MSG_NOSIGNAL);
				poll(pfds, 2, -1);
				recv(psv[0], buf, 64, MSG_DONTWAIT);
			}
			uint64_t t1 = nsec_now();
			pthread_join(th, NULL);
			close(efd_a); close(efd_b);
			close(psv[0]); close(psv[1]);
			emit_sample("S2_VFS_Epoll", "poll_2fd_pingpong_ns", t,
				    (double)(t1 - t0) / N_POLL_PP, "ns/RTT", "lower");
		}
	}

	// 2L: Milestone 2 Hardware CRC32C epoll_ctl(MOD) on Active Set in 1,024-FD Epoll (400,000 ops/trial)
	{
		int ep = epoll_create1(EPOLL_CLOEXEC);
		int (*ep_sv)[2] = malloc(sizeof(int[2]) * 1024);
		for (int i = 0; i < 1024; i++) {
			socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, ep_sv[i]);
			struct epoll_event ev = { .events = EPOLLIN | EPOLLONESHOT, .data.u32 = (uint32_t)i };
			epoll_ctl(ep, EPOLL_CTL_ADD, ep_sv[i][0], &ev);
		}
		const int N_MOD = 400000;
		pin_cpu(0);
		struct epoll_event ev = { .events = EPOLLIN | EPOLLONESHOT, .data.u32 = 0 };
		for (int i = 0; i < 4096; i++)
			epoll_ctl(ep, EPOLL_CTL_MOD, ep_sv[i & 7][0], &ev);
		for (int t = 0; t < trials; t++) {
			uint64_t t0 = nsec_now();
			for (int i = 0; i < N_MOD; i++) {
				int idx = i & 7; /* 8 active connections inside 1,024-FD epoll */
				ev.events = (i & 8) ? (EPOLLIN | EPOLLOUT | EPOLLONESHOT) : (EPOLLIN | EPOLLONESHOT);
				ev.data.u32 = (uint32_t)idx;
				epoll_ctl(ep, EPOLL_CTL_MOD, ep_sv[idx][0], &ev);
			}
			uint64_t t1 = nsec_now();
			emit_sample("S2_VFS_Epoll", "epoll_ctl_mod_1024fd_ns", t,
				    (double)(t1 - t0) / N_MOD, "ns/op", "lower");
		}
		close(ep);
		for (int i = 0; i < 1024; i++) {
			close(ep_sv[i][0]);
			close(ep_sv[i][1]);
		}
		free(ep_sv);
	}

	// 2M & 2N: Milestone 2 Unboxed io_uring AF_UNIX 64B Ping-Pong & Batch-32 Throughput
	{
		struct gb_uring u;
		if (gb_uring_init(&u, 128) == 0) {
			const int N_UPP = 60000;
			for (int t = 0; t < trials; t++) {
				int usv[2];
				if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, usv) != 0)
					continue;
				struct efd_pp_ctx uctx = {
					.rx_fd = usv[1], .tx_fd = usv[1],
					.iters = N_UPP + 1000, .cpu = 2
				};
				pthread_t th;
				pthread_create(&th, NULL, iouring_pong_worker, &uctx);
				pin_cpu(0);
				char buf[64];
				memset(buf, 0x66, sizeof(buf));
				for (int i = 0; i < 1000; i++) {
					gb_uring_push_sr(&u, IORING_OP_SEND, usv[0], buf, 64, 1);
					syscall(__NR_io_uring_enter, u.ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
					__atomic_store_n(u.cq_head, *u.cq_head + 1, __ATOMIC_RELEASE);
					gb_uring_push_sr(&u, IORING_OP_RECV, usv[0], buf, 64, 2);
					syscall(__NR_io_uring_enter, u.ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
					__atomic_store_n(u.cq_head, *u.cq_head + 1, __ATOMIC_RELEASE);
				}
				uint64_t t0 = nsec_now();
				for (int i = 0; i < N_UPP; i++) {
					gb_uring_push_sr(&u, IORING_OP_SEND, usv[0], buf, 64, 1);
					syscall(__NR_io_uring_enter, u.ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
					__atomic_store_n(u.cq_head, *u.cq_head + 1, __ATOMIC_RELEASE);
					gb_uring_push_sr(&u, IORING_OP_RECV, usv[0], buf, 64, 2);
					syscall(__NR_io_uring_enter, u.ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
					__atomic_store_n(u.cq_head, *u.cq_head + 1, __ATOMIC_RELEASE);
				}
				uint64_t t1 = nsec_now();
				pthread_join(th, NULL);
				close(usv[0]); close(usv[1]);
				emit_sample("S2_VFS_Epoll", "iouring_unix_64B_pingpong_ns", t,
					    (double)(t1 - t0) / N_UPP, "ns/RTT", "lower");
			}

			int bsv[2];
			if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, bsv) == 0) {
				char tx[64], rx[64];
				memset(tx, 0x77, sizeof(tx));
				const int ROUNDS = 5000; /* 5000 * 64 = 320,000 SQEs/trial */
				pin_cpu(0);
				for (int t = 0; t < trials; t++) {
					uint64_t t0 = nsec_now();
					for (int r = 0; r < ROUNDS; r++) {
						for (int k = 0; k < 32; k++)
							gb_uring_push_sr(&u, IORING_OP_SEND, bsv[0], tx, 64, (uint64_t)k);
						for (int k = 0; k < 32; k++)
							gb_uring_push_sr(&u, IORING_OP_RECV, bsv[1], rx, 64, (uint64_t)(32 + k));
						syscall(__NR_io_uring_enter, u.ring_fd, 64, 64,
							IORING_ENTER_GETEVENTS, NULL, 0);
						__atomic_store_n(u.cq_head, *u.cq_head + 64, __ATOMIC_RELEASE);
					}
					uint64_t t1 = nsec_now();
					double mops = ((double)(ROUNDS * 64) / 1e6) / ((double)(t1 - t0) / 1e9);
					emit_sample("S2_VFS_Epoll", "iouring_unix_batch32_mops", t,
						    mops, "Mops/s", "higher");
				}
				close(bsv[0]); close(bsv[1]);
			}
			gb_uring_destroy(&u);
		}
	}

	// 2O: Milestone 3 Hardware CRC32Q Futex (FUTEX_WAKE_PRIVATE key+hash fastpath & 8-thread contention)
	{
		alignas(64) uint32_t fwords[64] = {0};
		const int N_FWAKE = 500000;
		pin_cpu(0);
		for (int t = 0; t < trials; t++) {
			uint64_t t0 = nsec_now();
			for (int i = 0; i < N_FWAKE; i++)
				syscall(SYS_futex, &fwords[i & 63], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
			uint64_t t1 = nsec_now();
			emit_sample("S2_VFS_Epoll", "futex_private_wake_ns", t,
				    (double)(t1 - t0) / N_FWAKE, "ns/op", "lower");
		}

		const int NLOCKS = 16, NTHR = 8, ITERS = 40000;
		struct gb_futex_lock *locks = aligned_alloc(64, sizeof(*locks) * NLOCKS);
		unpin_all_cpus();
		for (int t = 0; t < trials; t++) {
			memset(locks, 0, sizeof(*locks) * NLOCKS);
			struct gb_fcont_ctx fctx = { .locks = locks, .nlocks = NLOCKS, .iters = ITERS };
			pthread_t th[NTHR];
			uint64_t t0 = nsec_now();
			for (int i = 0; i < NTHR; i++)
				pthread_create(&th[i], NULL, gb_futex_cont_worker, &fctx);
			for (int i = 0; i < NTHR; i++)
				pthread_join(th[i], NULL);
			uint64_t t1 = nsec_now();
			double mops = ((double)(NTHR * ITERS) / 1e6) / ((double)(t1 - t0) / 1e9);
			emit_sample("S2_VFS_Epoll", "futex_8thread_contended_mops", t,
				    mops, "Mops/s", "higher");
		}
		free(locks);
	}

	// 2P: Milestone 3 Unpinned WF_SYNC Same-Core Desktop IPC Handoff (eventfd & pipe 64B)
	{
		const int N_UP = 80000;
		unpin_all_cpus();
		for (int t = 0; t < trials; t++) {
			int e1 = eventfd(0, EFD_CLOEXEC), e2 = eventfd(0, EFD_CLOEXEC);
			struct gb_unpinned_pp_ctx ectx = { .rx_fd = e1, .tx_fd = e2, .iters = N_UP + 1000 };
			pthread_t th;
			pthread_create(&th, NULL, gb_unpinned_efd_worker, &ectx);
			unpin_all_cpus();
			uint64_t val = 1;
			for (int i = 0; i < 1000; i++) {
				write(e1, &val, 8);
				read(e2, &val, 8);
			}
			uint64_t t0 = nsec_now();
			for (int i = 0; i < N_UP; i++) {
				val = 1;
				write(e1, &val, 8);
				read(e2, &val, 8);
			}
			uint64_t t1 = nsec_now();
			pthread_join(th, NULL);
			close(e1); close(e2);
			emit_sample("S2_VFS_Epoll", "wfsync_eventfd_unpinned_ns", t,
				    (double)(t1 - t0) / N_UP, "ns/RTT", "lower");
		}

		for (int t = 0; t < trials; t++) {
			int p1[2], p2[2];
			if (pipe2(p1, O_CLOEXEC) != 0 || pipe2(p2, O_CLOEXEC) != 0)
				continue;
			struct gb_unpinned_pp_ctx pctx = { .rx_fd = p1[0], .tx_fd = p2[1], .iters = N_UP + 1000 };
			pthread_t th;
			pthread_create(&th, NULL, gb_unpinned_pipe_worker, &pctx);
			unpin_all_cpus();
			char buf[64];
			memset(buf, 0x5a, sizeof(buf));
			for (int i = 0; i < 1000; i++) {
				write(p1[1], buf, 64);
				read(p2[0], buf, 64);
			}
			uint64_t t0 = nsec_now();
			for (int i = 0; i < N_UP; i++) {
				write(p1[1], buf, 64);
				read(p2[0], buf, 64);
			}
			uint64_t t1 = nsec_now();
			pthread_join(th, NULL);
			close(p1[0]); close(p1[1]);
			close(p2[0]); close(p2[1]);
			emit_sample("S2_VFS_Epoll", "wfsync_pipe64B_unpinned_ns", t,
				    (double)(t1 - t0) / N_UP, "ns/RTT", "lower");
		}
	}

	// 2Q: Milestone 3 BORE-EEVDF Interactive Wakeup Latency Under 8-Thread CPU Hog Load
	{
		const int NHOGS = 8, FRAMES = 120;
		pthread_t hogs[NHOGS];
		unpin_all_cpus();
		atomic_store(&gb_hog_stop, 0);
		for (int i = 0; i < NHOGS; i++)
			pthread_create(&hogs[i], NULL, gb_cpu_hog_worker, NULL);
		usleep(20000); /* Let hogs accumulate >= 4 ms burst_ns */

		for (int t = 0; t < trials; t++) {
			int req_efd = eventfd(0, EFD_CLOEXEC), ack_efd = eventfd(0, EFD_CLOEXEC);
			uint64_t lats[FRAMES];
			_Atomic uint64_t send_ts = 0;
			struct gb_bore_ctx bctx = {
				.req_efd = req_efd, .ack_efd = ack_efd,
				.frames = FRAMES, .lats = lats, .send_ts = &send_ts
			};
			pthread_t rth;
			pthread_create(&rth, NULL, gb_bore_responder, &bctx);
			for (int i = 0; i < FRAMES; i++) {
				usleep(600);
				atomic_store_explicit(&send_ts, nsec_now(), memory_order_release);
				uint64_t v = 1;
				write(req_efd, &v, 8);
				read(ack_efd, &v, 8);
			}
			pthread_join(rth, NULL);
			close(req_efd); close(ack_efd);
			qsort(lats, FRAMES, sizeof(uint64_t), gb_cmp_u64);
			emit_sample("S2_VFS_Epoll", "bore_interactive_8hog_p50_us", t,
				    (double)lats[FRAMES / 2] / 1000.0, "us", "lower");
			emit_sample("S2_VFS_Epoll", "bore_interactive_8hog_p99_us", t,
				    (double)lats[(FRAMES * 99) / 100] / 1000.0, "us", "lower");
		}

		atomic_store(&gb_hog_stop, 1);
		for (int i = 0; i < NHOGS; i++)
			pthread_join(hogs[i], NULL);
	}
}

// ============================================================================
// SUITE 3: Layer 3 AF_UNIX Desktop IPC & 8-Client Wayland Compositor Sim
// ============================================================================
struct unix_pp_ctx {
	int fd;
	size_t msg_sz;
	int iters;
	int cpu;
};

static void *unix_pong_thread(void *arg)
{
	struct unix_pp_ctx *c = (struct unix_pp_ctx *)arg;
	pin_cpu(c->cpu);
	char buf[512];
	for (int i = 0; i < c->iters; i++) {
		size_t got = 0;
		while (got < c->msg_sz) {
			ssize_t r = recv(c->fd, buf + got, c->msg_sz - got, 0);
			if (r <= 0)
				return NULL;
			got += (size_t)r;
		}
		size_t sent = 0;
		while (sent < c->msg_sz) {
			ssize_t w = send(c->fd, buf + sent, c->msg_sz - sent, 0);
			if (w <= 0)
				return NULL;
			sent += (size_t)w;
		}
	}
	return NULL;
}

struct unix_stream_rx_ctx {
	int fd;
	size_t total_bytes;
	int cpu;
};

static void *unix_stream_rx_thread(void *arg)
{
	struct unix_stream_rx_ctx *c = (struct unix_stream_rx_ctx *)arg;
	pin_cpu(c->cpu);
	char *buf = malloc(65536);
	size_t got = 0;
	while (got < c->total_bytes) {
		ssize_t r = recv(c->fd, buf, 65536, 0);
		if (r <= 0)
			break;
		got += (size_t)r;
	}
	free(buf);
	return NULL;
}

struct comp_client_ctx {
	int fd;
	int frames;
	int dmabuf_fd;
	pthread_barrier_t *bar;
};

static void *comp_client_thread(void *arg)
{
	struct comp_client_ctx *c = (struct comp_client_ctx *)arg;
	char wire_commit[128];
	memset(wire_commit, 0x77, sizeof(wire_commit));
	char reply[32];
	pthread_barrier_wait(c->bar);

	for (int f = 0; f < c->frames; f++) {
		// Every 8th frame, attach a DMA-BUF FD via SCM_RIGHTS interleaved with wire bytes!
		if ((f & 7) == 0) {
			char cbuf[CMSG_SPACE(sizeof(int))];
			memset(cbuf, 0, sizeof(cbuf));
			struct iovec iov = { .iov_base = wire_commit, .iov_len = sizeof(wire_commit) };
			struct msghdr msg = {
				.msg_iov = &iov, .msg_iovlen = 1,
				.msg_control = cbuf, .msg_controllen = sizeof(cbuf)
			};
			struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
			cm->cmsg_level = SOL_SOCKET;
			cm->cmsg_type = SCM_RIGHTS;
			cm->cmsg_len = CMSG_LEN(sizeof(int));
			memcpy(CMSG_DATA(cm), &c->dmabuf_fd, sizeof(int));
			sendmsg(c->fd, &msg, 0);
		} else {
			send(c->fd, wire_commit, sizeof(wire_commit), 0);
		}
		size_t got = 0;
		while (got < 32) {
			ssize_t r = recv(c->fd, reply + got, 32 - got, 0);
			if (r <= 0)
				return NULL;
			got += (size_t)r;
		}
	}
	return NULL;
}

static int send_dbus_3iov(int fd, const void *hdr16, const void *fld48, const void *body128)
{
	struct iovec iov[3] = {
		{ .iov_base = (void *)hdr16,   .iov_len = 16  },
		{ .iov_base = (void *)fld48,   .iov_len = 48  },
		{ .iov_base = (void *)body128, .iov_len = 128 }
	};
	int idx = 0;
	while (idx < 3) {
		struct msghdr msg = {
			.msg_iov = &iov[idx],
			.msg_iovlen = (size_t)(3 - idx)
		};
		ssize_t w = sendmsg(fd, &msg, MSG_NOSIGNAL);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				sched_yield();
				continue;
			}
			return -1;
		}
		if (w == 0)
			return -1;
		size_t rem = (size_t)w;
		while (idx < 3 && rem >= iov[idx].iov_len) {
			rem -= iov[idx].iov_len;
			idx++;
		}
		if (idx < 3 && rem > 0) {
			iov[idx].iov_base = (char *)iov[idx].iov_base + rem;
			iov[idx].iov_len -= rem;
		}
	}
	return 0;
}

static int recv_exact_192(int fd, uint8_t *buf)
{
	size_t got = 0;
	while (got < 192) {
		ssize_t r = recv(fd, buf + got, 192 - got, 0);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0)
			return -1;
		got += (size_t)r;
	}
	return 0;
}

struct dbus_ep_ctx {
	int fd;
	int rpcs;
	pthread_barrier_t *bar;
};

static void *dbus_client_thread(void *arg)
{
	struct dbus_ep_ctx *c = (struct dbus_ep_ctx *)arg;
	uint8_t hdr[16], fld[48], body[128], reply[192];
	memset(hdr, 0x6C, sizeof(hdr));
	memset(fld, 0x42, sizeof(fld));
	memset(body, 0x24, sizeof(body));
	pthread_barrier_wait(c->bar);

	for (int i = 0; i < c->rpcs; i++) {
		if (send_dbus_3iov(c->fd, hdr, fld, body) != 0)
			return NULL;
		if (recv_exact_192(c->fd, reply) != 0)
			return NULL;
	}
	return NULL;
}

static void *dbus_service_thread(void *arg)
{
	struct dbus_ep_ctx *c = (struct dbus_ep_ctx *)arg;
	uint8_t req[192];
	pthread_barrier_wait(c->bar);

	for (int i = 0; i < c->rpcs; i++) {
		if (recv_exact_192(c->fd, req) != 0)
			return NULL;
		if (send_dbus_3iov(c->fd, req, req + 16, req + 64) != 0)
			return NULL;
	}
	return NULL;
}

static void run_suite3_unix_ipc(int trials)
{
	fprintf(stderr, "[Suite 3] Running AF_UNIX Desktop IPC & 8-Client Compositor Sim (%d trials)...\n", trials);

	// 3A & 3B: Cross-core 64B and 256B Ping-Pong RTT (120,000 RTTs/trial with p99 tracking)
	const size_t sizes[2] = { 64, 256 };
	const char *mnames_mean[2] = { "unix_64B_pingpong_mean_ns", "unix_256B_wayland_wire_mean_ns" };
	const char *mnames_p99[2]  = { "unix_64B_pingpong_p99_ns",  "unix_256B_wayland_wire_p99_ns" };
	const int N_PP = 120000;
	uint64_t *lats = malloc(sizeof(uint64_t) * N_PP);

	for (int s = 0; s < 2; s++) {
		size_t sz = sizes[s];
		for (int t = 0; t < trials; t++) {
			int sv[2];
			socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
			struct unix_pp_ctx ctx = { .fd = sv[1], .msg_sz = sz, .iters = N_PP + 2000, .cpu = 2 };
			pthread_t th;
			pthread_create(&th, NULL, unix_pong_thread, &ctx);
			pin_cpu(0);

			char buf[512];
			memset(buf, 0xAB, sz);
			// 2,000 warmup RTTs
			for (int i = 0; i < 2000; i++) {
				send(sv[0], buf, sz, 0);
				size_t got = 0;
				while (got < sz)
					got += (size_t)recv(sv[0], buf + got, sz - got, 0);
			}
			uint64_t sum_ns = 0;
			for (int i = 0; i < N_PP; i++) {
				uint64_t t0 = nsec_now();
				send(sv[0], buf, sz, 0);
				size_t got = 0;
				while (got < sz)
					got += (size_t)recv(sv[0], buf + got, sz - got, 0);
				uint64_t dt = nsec_now() - t0;
				lats[i] = dt;
				sum_ns += dt;
			}
			pthread_join(th, NULL);
			close(sv[0]);
			close(sv[1]);

			qsort(lats, N_PP, sizeof(uint64_t), cmp_u64);
			emit_sample("S3_AF_UNIX", mnames_mean[s], t, (double)sum_ns / N_PP, "ns/RTT", "lower");
			emit_sample("S3_AF_UNIX", mnames_p99[s], t, (double)lats[(size_t)(N_PP * 0.99)], "ns/RTT", "lower");
		}
	}
	free(lats);

	// 3C & 3D: 4 KiB PipeWire stream (512 MiB/trial) & 64 KiB bulk stream (1 GiB/trial)
	for (int mode = 0; mode < 2; mode++) {
		size_t chunk = (mode == 0) ? 4096 : 65536;
		size_t total = (mode == 0) ? (512ULL * 1024 * 1024) : (1024ULL * 1024 * 1024);
		const char *mname = (mode == 0) ? "unix_4KB_stream_gibs" : "unix_64KB_stream_gibs";
		char *txbuf = calloc(1, chunk);

		for (int t = 0; t < trials; t++) {
			int sv[2];
			socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
			int sndbuf = 512 * 1024;
			setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
			setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &sndbuf, sizeof(sndbuf));

			struct unix_stream_rx_ctx rx = { .fd = sv[1], .total_bytes = total, .cpu = 2 };
			pthread_t th;
			pthread_create(&th, NULL, unix_stream_rx_thread, &rx);
			pin_cpu(0);

			uint64_t t0 = nsec_now();
			size_t sent = 0;
			while (sent < total) {
				ssize_t w = send(sv[0], txbuf, chunk, 0);
				if (w <= 0)
					break;
				sent += (size_t)w;
			}
			shutdown(sv[0], SHUT_WR);
			pthread_join(th, NULL);
			uint64_t t1 = nsec_now();
			close(sv[0]);
			close(sv[1]);

			double gibs = ((double)total / (1024.0 * 1024.0 * 1024.0)) / ((double)(t1 - t0) / 1e9);
			emit_sample("S3_AF_UNIX", mname, t, gibs, "GiB/s", "higher");
		}
		free(txbuf);
	}

	// 3E: 8-Client Simulated Wayland Compositor Reactor (8 clients x 12,000 frames = 96,000 client commits/trial)
	const int N_CLI = 8;
	const int FRAMES = 12000;
	int dummy_pipe[2];
	pipe(dummy_pipe);

	for (int t = 0; t < trials; t++) {
		int ep = epoll_create1(EPOLL_CLOEXEC);
		int sv[N_CLI][2];
		pthread_t th[N_CLI];
		pthread_barrier_t bar;
		pthread_barrier_init(&bar, NULL, N_CLI + 1);
		struct comp_client_ctx cctx[N_CLI];

		for (int i = 0; i < N_CLI; i++) {
			socketpair(AF_UNIX, SOCK_STREAM, 0, sv[i]);
			fcntl(sv[i][0], F_SETFL, O_NONBLOCK);
			struct epoll_event ev = { .events = EPOLLIN, .data.u32 = (uint32_t)i };
			epoll_ctl(ep, EPOLL_CTL_ADD, sv[i][0], &ev);
			cctx[i] = (struct comp_client_ctx){
				.fd = sv[i][1], .frames = FRAMES,
				.dmabuf_fd = dummy_pipe[0], .bar = &bar
			};
			pthread_create(&th[i], NULL, comp_client_thread, &cctx[i]);
		}

		size_t client_rem[N_CLI];
		for (int i = 0; i < N_CLI; i++)
			client_rem[i] = 0;
		int completed_commits = 0;
		int target_commits = N_CLI * FRAMES;
		char frame_done[32] = "wl_frame_callback_done_vsync_00";

		pthread_barrier_wait(&bar);
		double sys0 = rusage_sys_ms();
		uint64_t t0 = nsec_now();

		struct epoll_event evs[16];
		while (completed_commits < target_commits) {
			int n = epoll_wait(ep, evs, 16, -1);
			for (int e = 0; e < n; e++) {
				uint32_t cid = evs[e].data.u32;
				for (;;) {
					char rx[512];
					char cbuf[128];
					struct iovec iov = { .iov_base = rx, .iov_len = sizeof(rx) };
					struct msghdr msg = {
						.msg_iov = &iov, .msg_iovlen = 1,
						.msg_control = cbuf, .msg_controllen = sizeof(cbuf)
					};
					ssize_t r = recvmsg(sv[cid][0], &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
					if (r <= 0)
						break;
					for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
						if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
							int nfd = (int)((cm->cmsg_len - CMSG_LEN(0)) / sizeof(int));
							int *fds = (int *)CMSG_DATA(cm);
							for (int k = 0; k < nfd; k++)
								close(fds[k]);
						}
					}
					client_rem[cid] += (size_t)r;
					while (client_rem[cid] >= 128) {
						client_rem[cid] -= 128;
						send(sv[cid][0], frame_done, 32, 0);
						completed_commits++;
					}
				}
			}
		}

		for (int i = 0; i < N_CLI; i++)
			pthread_join(th[i], NULL);
		uint64_t t1 = nsec_now();
		double sys1 = rusage_sys_ms();
		pthread_barrier_destroy(&bar);
		close(ep);
		for (int i = 0; i < N_CLI; i++) {
			close(sv[i][0]);
			close(sv[i][1]);
		}

		double kcommits_s = ((double)target_commits / 1000.0) / ((double)(t1 - t0) / 1e9);
		emit_sample("S3_AF_UNIX", "wayland_sim_8client_kcommits_s", t, kcommits_s, "kcommits/s", "higher");
		emit_sample("S3_AF_UNIX", "wayland_sim_8client_sys_cpu_ms", t, sys1 - sys0, "ms", "lower");
	}
	close(dummy_pipe[0]);
	close(dummy_pipe[1]);

	// 3F: Wayland/PipeWire/D-Bus 1-memfd SCM_RIGHTS send+recv+close RTT (100,000 pairs/trial)
	{
		int sv[2];
		socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv);
		int mfd = memfd_create("netzero_scm_bench", MFD_CLOEXEC);
		char init_mfd[64] = {0};
		ssize_t mw = write(mfd, init_mfd, sizeof(init_mfd));
		(void)mw;
		pin_cpu(0);

		char tx_pl[32] = "wl_shm_create_pool_frame_sync01";
		char rx_pl[32];
		union {
			char buf[CMSG_SPACE(sizeof(int))];
			struct cmsghdr align;
		} tx_c, rx_c;
		memset(&tx_c, 0, sizeof(tx_c));

		struct iovec tx_iov = { .iov_base = tx_pl, .iov_len = sizeof(tx_pl) };
		struct msghdr tx_msg = {
			.msg_iov = &tx_iov, .msg_iovlen = 1,
			.msg_control = tx_c.buf, .msg_controllen = sizeof(tx_c.buf)
		};
		struct cmsghdr *cm = CMSG_FIRSTHDR(&tx_msg);
		cm->cmsg_level = SOL_SOCKET;
		cm->cmsg_type = SCM_RIGHTS;
		cm->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cm), &mfd, sizeof(int));

		const int N_SCM = 100000;
		for (int i = 0; i < 2000; i++) {
			if (sendmsg(sv[0], &tx_msg, 0) > 0) {
				struct iovec rx_iov = { .iov_base = rx_pl, .iov_len = sizeof(rx_pl) };
				struct msghdr rx_msg = {
					.msg_iov = &rx_iov, .msg_iovlen = 1,
					.msg_control = rx_c.buf, .msg_controllen = sizeof(rx_c.buf)
				};
				if (recvmsg(sv[1], &rx_msg, MSG_CMSG_CLOEXEC) > 0) {
					for (struct cmsghdr *rc = CMSG_FIRSTHDR(&rx_msg); rc; rc = CMSG_NXTHDR(&rx_msg, rc)) {
						if (rc->cmsg_level == SOL_SOCKET && rc->cmsg_type == SCM_RIGHTS) {
							int rfd;
							memcpy(&rfd, CMSG_DATA(rc), sizeof(int));
							close(rfd);
						}
					}
				}
			}
		}

		for (int t = 0; t < trials; t++) {
			uint64_t t0 = nsec_now();
			for (int i = 0; i < N_SCM; i++) {
				if (sendmsg(sv[0], &tx_msg, 0) > 0) {
					struct iovec rx_iov = { .iov_base = rx_pl, .iov_len = sizeof(rx_pl) };
					struct msghdr rx_msg = {
						.msg_iov = &rx_iov, .msg_iovlen = 1,
						.msg_control = rx_c.buf, .msg_controllen = sizeof(rx_c.buf)
					};
					if (recvmsg(sv[1], &rx_msg, MSG_CMSG_CLOEXEC) > 0) {
						for (struct cmsghdr *rc = CMSG_FIRSTHDR(&rx_msg); rc; rc = CMSG_NXTHDR(&rx_msg, rc)) {
							if (rc->cmsg_level == SOL_SOCKET && rc->cmsg_type == SCM_RIGHTS) {
								int rfd;
								memcpy(&rfd, CMSG_DATA(rc), sizeof(int));
								close(rfd);
							}
						}
					}
				}
			}
			uint64_t t1 = nsec_now();
			emit_sample("S3_AF_UNIX", "scm_rights_memfd_pair_ns", t,
				    (double)(t1 - t0) / N_SCM, "ns/pair", "lower");
		}
		close(mfd);
		close(sv[0]);
		close(sv[1]);
	}

	// 3G: Realistic dbus-broker 2-hop simulation (4 clients + 1 epoll broker + 4 services, 3-iovec 16+48+128B)
	{
		const int N_DBUS = 4;
		const int RPCS_PER_CLI = 15000;
		const int TARGET_RPCS = N_DBUS * RPCS_PER_CLI;

		for (int t = 0; t < trials; t++) {
			int ep = epoll_create1(EPOLL_CLOEXEC);
			int cli_sv[4][2], svc_sv[4][2];
			pthread_t cli_th[4], svc_th[4];
			struct dbus_ep_ctx cctx[4], sctx[4];
			pthread_barrier_t bar;
			pthread_barrier_init(&bar, NULL, 2 * N_DBUS + 1);

			for (int i = 0; i < N_DBUS; i++) {
				socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, cli_sv[i]);
				socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, svc_sv[i]);
				fcntl(cli_sv[i][0], F_SETFL, O_NONBLOCK);
				fcntl(svc_sv[i][0], F_SETFL, O_NONBLOCK);

				struct epoll_event cev = { .events = EPOLLIN, .data.u32 = (uint32_t)i };
				struct epoll_event sev = { .events = EPOLLIN, .data.u32 = (uint32_t)(N_DBUS + i) };
				epoll_ctl(ep, EPOLL_CTL_ADD, cli_sv[i][0], &cev);
				epoll_ctl(ep, EPOLL_CTL_ADD, svc_sv[i][0], &sev);

				cctx[i] = (struct dbus_ep_ctx){ .fd = cli_sv[i][1], .rpcs = RPCS_PER_CLI, .bar = &bar };
				sctx[i] = (struct dbus_ep_ctx){ .fd = svc_sv[i][1], .rpcs = RPCS_PER_CLI, .bar = &bar };
				pthread_create(&cli_th[i], NULL, dbus_client_thread, &cctx[i]);
				pthread_create(&svc_th[i], NULL, dbus_service_thread, &sctx[i]);
			}

			pin_cpu(0);
			uint8_t ep_buf[8][512];
			size_t ep_len[8] = {0};
			int completed = 0;

			pthread_barrier_wait(&bar);
			uint64_t t0 = nsec_now();

			struct epoll_event evs[16];
			while (completed < TARGET_RPCS) {
				int n = epoll_wait(ep, evs, 16, -1);
				for (int e = 0; e < n; e++) {
					uint32_t tag = evs[e].data.u32;
					int is_svc = (tag >= (uint32_t)N_DBUS);
					int idx = is_svc ? (int)(tag - (uint32_t)N_DBUS) : (int)tag;
					int rfd = is_svc ? svc_sv[idx][0] : cli_sv[idx][0];
					int dst_fd = is_svc ? cli_sv[idx][0] : svc_sv[idx][0];

					for (;;) {
						ssize_t r = recv(rfd, ep_buf[tag] + ep_len[tag],
								 sizeof(ep_buf[tag]) - ep_len[tag], MSG_DONTWAIT);
						if (r <= 0)
							break;
						ep_len[tag] += (size_t)r;
						size_t consumed = 0;
						while (ep_len[tag] - consumed >= 192) {
							uint8_t *m = ep_buf[tag] + consumed;
							send_dbus_3iov(dst_fd, m, m + 16, m + 64);
							consumed += 192;
							if (is_svc)
								completed++;
						}
						if (consumed > 0) {
							size_t left = ep_len[tag] - consumed;
							if (left > 0)
								memmove(ep_buf[tag], ep_buf[tag] + consumed, left);
							ep_len[tag] = left;
						}
					}
				}
			}

			for (int i = 0; i < N_DBUS; i++) {
				pthread_join(cli_th[i], NULL);
				pthread_join(svc_th[i], NULL);
			}
			uint64_t t1 = nsec_now();

			pthread_barrier_destroy(&bar);
			close(ep);
			for (int i = 0; i < N_DBUS; i++) {
				close(cli_sv[i][0]); close(cli_sv[i][1]);
				close(svc_sv[i][0]); close(svc_sv[i][1]);
			}

			double krps = ((double)TARGET_RPCS / 1000.0) / ((double)(t1 - t0) / 1e9);
			emit_sample("S3_AF_UNIX", "dbus_broker_sim_2hop_krps", t, krps, "kRPS", "higher");
		}
	}
}

// ============================================================================
// SUITE 4: Layer 4 & Layer 5A TCP & UDP/QUIC Datapath
// ============================================================================
struct tcp_storm_srv_ctx {
	int lfd;
	int count;
};

static void *tcp_storm_srv(void *arg)
{
	struct tcp_storm_srv_ctx *s = (struct tcp_storm_srv_ctx *)arg;
	pin_cpu(2);
	for (int i = 0; i < s->count; i++) {
		int cfd = accept4(s->lfd, NULL, NULL, SOCK_CLOEXEC);
		if (cfd < 0)
			continue;
		char b = 'K';
		send(cfd, &b, 1, 0);
		recv(cfd, &b, 1, 0);
		close(cfd);
	}
	return NULL;
}

struct tcp_mux_srv_ctx {
	int lfd;
	int nconns;
	int total_rpcs;
};

static void *tcp_mux_srv(void *arg)
{
	struct tcp_mux_srv_ctx *s = (struct tcp_mux_srv_ctx *)arg;
	pin_cpu(2);
	int ep = epoll_create1(EPOLL_CLOEXEC);
	int *cfds = malloc(sizeof(int) * s->nconns);
	size_t *rem = calloc((size_t)s->nconns, sizeof(size_t));

	for (int i = 0; i < s->nconns; i++) {
		cfds[i] = accept4(s->lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
		int one = 1;
		setsockopt(cfds[i], IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		struct epoll_event ev = { .events = EPOLLIN, .data.u32 = (uint32_t)i };
		epoll_ctl(ep, EPOLL_CTL_ADD, cfds[i], &ev);
	}

	char buf[1024];
	char resp[128];
	memset(resp, 0x52, sizeof(resp));
	int handled = 0;
	struct epoll_event evs[64];
	while (handled < s->total_rpcs) {
		int n = epoll_wait(ep, evs, 64, -1);
		for (int e = 0; e < n; e++) {
			uint32_t idx = evs[e].data.u32;
			for (;;) {
				ssize_t r = recv(cfds[idx], buf, sizeof(buf), MSG_DONTWAIT);
				if (r <= 0)
					break;
				rem[idx] += (size_t)r;
				while (rem[idx] >= 128) {
					rem[idx] -= 128;
					send(cfds[idx], resp, 128, 0);
					handled++;
				}
			}
		}
	}

	close(ep);
	for (int i = 0; i < s->nconns; i++)
		close(cfds[i]);
	free(cfds);
	free(rem);
	return NULL;
}

struct tcp_bulk_pair_ctx {
	struct sockaddr_in addr;
	size_t bytes_per_stream;
};

static void *tcp_bulk_accept_sink(void *arg)
{
	int lfd = *(int *)arg;
	char *buf = malloc(131072);
	int cfd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
	if (cfd >= 0) {
		while (recv(cfd, buf, 131072, 0) > 0) {}
		close(cfd);
	}
	free(buf);
	return NULL;
}

static void *tcp_bulk_tx_worker(void *arg)
{
	struct tcp_bulk_pair_ctx *c = (struct tcp_bulk_pair_ctx *)arg;
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	connect(fd, (struct sockaddr *)&c->addr, sizeof(c->addr));
	char *buf = calloc(1, 131072);
	size_t sent = 0;
	while (sent < c->bytes_per_stream) {
		size_t chunk = c->bytes_per_stream - sent;
		if (chunk > 131072)
			chunk = 131072;
		ssize_t w = send(fd, buf, chunk, 0);
		if (w <= 0)
			break;
		sent += (size_t)w;
	}
	struct linger lg = { .l_onoff = 1, .l_linger = 0 };
	setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
	close(fd);
	free(buf);
	return NULL;
}

static void run_suite4_tcp_udp(int trials)
{
	fprintf(stderr, "[Suite 4] Running TCP Handshake Storm, 64-Conn HTTP/2 Mux, Bulk TCP & UDP/QUIC (%d trials)...\n", trials);

	// 4A: 2,000-Connection TCP Handshake + 1B RPC Storm per trial (20,000 conns total!)
	const int N_CONN = 2000;
	for (int t = 0; t < trials; t++) {
		int lfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
		int one = 1;
		setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		struct sockaddr_in addr = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
			.sin_port = 0
		};
		bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
		listen(lfd, 512);
		socklen_t alen = sizeof(addr);
		getsockname(lfd, (struct sockaddr *)&addr, &alen);

		struct tcp_storm_srv_ctx sctx = { .lfd = lfd, .count = N_CONN };
		pthread_t sth;
		pthread_create(&sth, NULL, tcp_storm_srv, &sctx);
		pin_cpu(0);

		uint64_t t0 = nsec_now();
		for (int i = 0; i < N_CONN; i++) {
			int cfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
			if (connect(cfd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
				char b = 'Q';
				recv(cfd, &b, 1, 0);
				send(cfd, &b, 1, 0);
			}
			struct linger lg = { .l_onoff = 1, .l_linger = 0 };
			setsockopt(cfd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
			close(cfd);
		}
		pthread_join(sth, NULL);
		uint64_t t1 = nsec_now();
		close(lfd);

		double us_per_conn = ((double)(t1 - t0) / 1000.0) / N_CONN;
		emit_sample("S4_TCP_UDP", "tcp_2000conn_handshake_us", t, us_per_conn, "us/conn", "lower");
	}

	// 4B: 64-Connection Multiplexed TCP RPC Storm (64,000 RPCs/trial, pipelined window of 64)
	const int N_MUX_CONNS = 64;
	const int N_MUX_RPCS  = 64000;
	for (int t = 0; t < trials; t++) {
		int lfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
		struct sockaddr_in addr = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
			.sin_port = 0
		};
		bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
		listen(lfd, 128);
		socklen_t alen = sizeof(addr);
		getsockname(lfd, (struct sockaddr *)&addr, &alen);

		struct tcp_mux_srv_ctx sctx = { .lfd = lfd, .nconns = N_MUX_CONNS, .total_rpcs = N_MUX_RPCS };
		pthread_t sth;
		pthread_create(&sth, NULL, tcp_mux_srv, &sctx);
		pin_cpu(0);

		int ep = epoll_create1(EPOLL_CLOEXEC);
		int cfds[N_MUX_CONNS];
		size_t rem[N_MUX_CONNS];
		char req[128];
		memset(req, 0x47, sizeof(req));

		for (int i = 0; i < N_MUX_CONNS; i++) {
			cfds[i] = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
			int one = 1;
			setsockopt(cfds[i], IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
			connect(cfds[i], (struct sockaddr *)&addr, sizeof(addr));
			fcntl(cfds[i], F_SETFL, O_NONBLOCK);
			rem[i] = 0;
			struct epoll_event ev = { .events = EPOLLIN, .data.u32 = (uint32_t)i };
			epoll_ctl(ep, EPOLL_CTL_ADD, cfds[i], &ev);
		}

		uint64_t t0 = nsec_now();
		int sent_rpcs = 0, recv_rpcs = 0;
		for (int i = 0; i < N_MUX_CONNS; i++) {
			send(cfds[i], req, 128, 0);
			sent_rpcs++;
		}
		struct epoll_event evs[64];
		char rx[512];
		while (recv_rpcs < N_MUX_RPCS) {
			int n = epoll_wait(ep, evs, 64, -1);
			for (int e = 0; e < n; e++) {
				uint32_t idx = evs[e].data.u32;
				for (;;) {
					ssize_t r = recv(cfds[idx], rx, sizeof(rx), MSG_DONTWAIT);
					if (r <= 0)
						break;
					rem[idx] += (size_t)r;
					while (rem[idx] >= 128) {
						rem[idx] -= 128;
						recv_rpcs++;
						if (sent_rpcs < N_MUX_RPCS) {
							send(cfds[idx], req, 128, 0);
							sent_rpcs++;
						}
					}
				}
			}
		}
		pthread_join(sth, NULL);
		uint64_t t1 = nsec_now();

		close(ep);
		for (int i = 0; i < N_MUX_CONNS; i++) {
			struct linger lg = { .l_onoff = 1, .l_linger = 0 };
			setsockopt(cfds[i], SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
			close(cfds[i]);
		}
		close(lfd);

		double krps = ((double)N_MUX_RPCS / 1000.0) / ((double)(t1 - t0) / 1e9);
		emit_sample("S4_TCP_UDP", "tcp_64conn_http2_mux_krps", t, krps, "kRPS", "higher");
	}

	// 4C: TCP Bulk Throughput (1-stream 1 GiB & 8-stream 2 GiB aggregate + Peak TCP sockstat pages)
	for (int mode = 0; mode < 2; mode++) {
		int nstreams = (mode == 0) ? 1 : 8;
		size_t per_stream = (mode == 0) ? (1024ULL * 1024 * 1024) : (256ULL * 1024 * 1024);
		const char *mname = (mode == 0) ? "tcp_bulk_1stream_gibs" : "tcp_bulk_8stream_gibs";

		for (int t = 0; t < trials; t++) {
			int lfds[8];
			pthread_t rth[8], tth[8];
			struct tcp_bulk_pair_ctx tctx[8];
			long base_tcp_pages = read_sockstat_mem_pages("TCP:");

			for (int s = 0; s < nstreams; s++) {
				lfds[s] = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
				struct sockaddr_in a = {
					.sin_family = AF_INET,
					.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
					.sin_port = 0
				};
				bind(lfds[s], (struct sockaddr *)&a, sizeof(a));
				listen(lfds[s], 16);
				socklen_t alen = sizeof(a);
				getsockname(lfds[s], (struct sockaddr *)&a, &alen);
				tctx[s].addr = a;
				tctx[s].bytes_per_stream = per_stream;
				pthread_create(&rth[s], NULL, tcp_bulk_accept_sink, &lfds[s]);
			}

			double sys0 = rusage_sys_ms();
			uint64_t t0 = nsec_now();
			for (int s = 0; s < nstreams; s++)
				pthread_create(&tth[s], NULL, tcp_bulk_tx_worker, &tctx[s]);

			usleep(25000);
			long mid_tcp_pages = read_sockstat_mem_pages("TCP:");

			for (int s = 0; s < nstreams; s++) {
				pthread_join(tth[s], NULL);
				pthread_join(rth[s], NULL);
				close(lfds[s]);
			}
			uint64_t t1 = nsec_now();
			double sys1 = rusage_sys_ms();

			double total_gib = ((double)nstreams * per_stream) / (1024.0 * 1024.0 * 1024.0);
			double gibs = total_gib / ((double)(t1 - t0) / 1e9);
			emit_sample("S4_TCP_UDP", mname, t, gibs, "GiB/s", "higher");
			if (mode == 1) {
				long active_p = (mid_tcp_pages > base_tcp_pages) ? (mid_tcp_pages - base_tcp_pages) : 0;
				emit_sample("S1_Memory", "tcp_8stream_active_pages", t, (double)active_p, "pages", "lower");
				emit_sample("S4_TCP_UDP", "tcp_bulk_8stream_sys_cpu_ms", t, sys1 - sys0, "ms", "lower");
			}
		}
	}

	// 4D: Unconnected UDP 4-Destination sendto() Rate (400,000 pkts/trial -> 4-way route cache!)
	{
		int rxfds[4];
		struct sockaddr_in dsts[4];
		for (int i = 0; i < 4; i++) {
			rxfds[i] = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
			int rcvbuf = 4 * 1024 * 1024;
			setsockopt(rxfds[i], SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
			dsts[i] = (struct sockaddr_in){
				.sin_family = AF_INET,
				.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
				.sin_port = 0
			};
			bind(rxfds[i], (struct sockaddr *)&dsts[i], sizeof(dsts[i]));
			socklen_t alen = sizeof(dsts[i]);
			getsockname(rxfds[i], (struct sockaddr *)&dsts[i], &alen);
		}
		int txfd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
		char upkt[64];
		memset(upkt, 0x21, sizeof(upkt));
		const int N_UDP = 400000;

		for (int t = 0; t < trials; t++) {
			pin_cpu(0);
			uint64_t t0 = nsec_now();
			for (int i = 0; i < N_UDP; i++) {
				sendto(txfd, upkt, sizeof(upkt), MSG_DONTWAIT,
				       (struct sockaddr *)&dsts[i & 3], sizeof(dsts[0]));
				if ((i & 255) == 255) {
					char drain[128];
					for (int k = 0; k < 4; k++)
						while (recv(rxfds[k], drain, sizeof(drain), MSG_DONTWAIT) > 0) {}
				}
			}
			uint64_t t1 = nsec_now();
			for (int k = 0; k < 4; k++) {
				char drain[128];
				while (recv(rxfds[k], drain, sizeof(drain), MSG_DONTWAIT) > 0) {}
			}
			double mpps = ((double)N_UDP / 1e6) / ((double)(t1 - t0) / 1e9);
			emit_sample("S4_TCP_UDP", "udp_unconn_4dest_mpps", t, mpps, "Mpps", "higher");
		}
		close(txfd);
		for (int i = 0; i < 4; i++)
			close(rxfds[i]);
	}

	// 4E: QUIC 1200-Byte Batched sendmmsg/recvmmsg Multiplexer (256,000 pkts/trial)
	{
		int rx = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
		int rcvbuf = 8 * 1024 * 1024;
		setsockopt(rx, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
		struct sockaddr_in dst = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
			.sin_port = 0
		};
		bind(rx, (struct sockaddr *)&dst, sizeof(dst));
		socklen_t alen = sizeof(dst);
		getsockname(rx, (struct sockaddr *)&dst, &alen);

		int tx = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
		connect(tx, (struct sockaddr *)&dst, sizeof(dst));

		enum { BATCH = 32, PKTS = 256000 };
		char txbufs[BATCH][1200], rxbufs[BATCH][1200];
		struct iovec txiov[BATCH], rxiov[BATCH];
		struct mmsghdr txm[BATCH], rxm[BATCH];
		memset(txm, 0, sizeof(txm));
		memset(rxm, 0, sizeof(rxm));
		for (int b = 0; b < BATCH; b++) {
			memset(txbufs[b], 0x99, 1200);
			txiov[b] = (struct iovec){ .iov_base = txbufs[b], .iov_len = 1200 };
			txm[b].msg_hdr.msg_iov = &txiov[b];
			txm[b].msg_hdr.msg_iovlen = 1;
			rxiov[b] = (struct iovec){ .iov_base = rxbufs[b], .iov_len = 1200 };
			rxm[b].msg_hdr.msg_iov = &rxiov[b];
			rxm[b].msg_hdr.msg_iovlen = 1;
		}

		for (int t = 0; t < trials; t++) {
			uint64_t t0 = nsec_now();
			int sent = 0;
			while (sent < PKTS) {
				int s = sendmmsg(tx, txm, BATCH, MSG_DONTWAIT);
				if (s > 0)
					sent += s;
				while (recvmmsg(rx, rxm, BATCH, MSG_DONTWAIT, NULL) > 0) {}
			}
			uint64_t t1 = nsec_now();
			double gbps = ((double)PKTS * 1200.0 * 8.0 / 1e9) / ((double)(t1 - t0) / 1e9);
			emit_sample("S4_TCP_UDP", "quic_1200B_batch_gbps", t, gbps, "Gbps", "higher");
		}
		close(tx);
		close(rx);
	}
}

// ============================================================================
// SUITE 5 Mode (--qdisc-only): Active Root Qdisc Pacing & Contention Test
// ============================================================================
struct qdisc_paced_tx_ctx {
	struct sockaddr_in addr;
	uint64_t rate_bps;
	atomic_bool *stop;
	atomic_uint_fast64_t *bytes_out;
};

static void *qdisc_paced_sink(void *arg)
{
	int lfd = *(int *)arg;
	char *buf = malloc(65536);
	for (;;) {
		int cfd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
		if (cfd < 0)
			break;
		while (recv(cfd, buf, 65536, 0) > 0) {}
		close(cfd);
	}
	free(buf);
	return NULL;
}

static void *qdisc_paced_tx(void *arg)
{
	struct qdisc_paced_tx_ctx *c = (struct qdisc_paced_tx_ctx *)arg;
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	unsigned int max_rate = (unsigned int)c->rate_bps;
	setsockopt(fd, SOL_SOCKET, SO_MAX_PACING_RATE, &max_rate, sizeof(max_rate));
	if (connect(fd, (struct sockaddr *)&c->addr, sizeof(c->addr)) != 0) {
		close(fd);
		return NULL;
	}
	char *buf = calloc(1, 32768);
	while (!atomic_load_explicit(c->stop, memory_order_relaxed)) {
		ssize_t w = send(fd, buf, 32768, 0);
		if (w <= 0)
			break;
		atomic_fetch_add_explicit(c->bytes_out, (uint64_t)w, memory_order_relaxed);
	}
	struct linger lg = { .l_onoff = 1, .l_linger = 0 };
	setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
	close(fd);
	free(buf);
	return NULL;
}

static void *qdisc_rpc_echo_srv(void *arg)
{
	int lfd = *(int *)arg;
	int cfd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
	if (cfd >= 0) {
		int one = 1;
		setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		char b[32];
		ssize_t r;
		while ((r = recv(cfd, b, sizeof(b), 0)) > 0)
			send(cfd, b, (size_t)r, 0);
		close(cfd);
	}
	return NULL;
}

static void run_qdisc_contention_trial(int trial)
{
	int lfds[8];
	pthread_t rth[8], tth[8];
	struct qdisc_paced_tx_ctx tctx[8];
	atomic_bool stop = false;
	atomic_uint_fast64_t total_bytes = 0;

	for (int s = 0; s < 8; s++) {
		lfds[s] = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
		struct sockaddr_in a = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
			.sin_port = 0
		};
		bind(lfds[s], (struct sockaddr *)&a, sizeof(a));
		listen(lfds[s], 16);
		socklen_t alen = sizeof(a);
		getsockname(lfds[s], (struct sockaddr *)&a, &alen);
		tctx[s] = (struct qdisc_paced_tx_ctx){
			.addr = a,
			.rate_bps = 125000000ULL, // 1 Gbps per flow = 8 Gbps aggregate EDT/wheel pacing!
			.stop = &stop,
			.bytes_out = &total_bytes
		};
		pthread_create(&rth[s], NULL, qdisc_paced_sink, &lfds[s]);
	}

	int rpc_lfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_in rpca = {
		.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0
	};
	bind(rpc_lfd, (struct sockaddr *)&rpca, sizeof(rpca));
	listen(rpc_lfd, 8);
	socklen_t rlen = sizeof(rpca);
	getsockname(rpc_lfd, (struct sockaddr *)&rpca, &rlen);
	pthread_t rpc_sth;
	pthread_create(&rpc_sth, NULL, qdisc_rpc_echo_srv, &rpc_lfd);

	int rpc_cfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	int one = 1;
	setsockopt(rpc_cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	connect(rpc_cfd, (struct sockaddr *)&rpca, sizeof(rpca));

	uint64_t t0 = nsec_now();
	for (int s = 0; s < 8; s++)
		pthread_create(&tth[s], NULL, qdisc_paced_tx, &tctx[s]);

	// Measure 4,000 live RPC round-trips while 8 paced flows hammer the root qdisc!
	const int N_RPC = 4000;
	uint64_t *lats = malloc(sizeof(uint64_t) * N_RPC);
	char msg[32] = "qdisc_probe_0123456789abcdef012";
	for (int i = 0; i < N_RPC; i++) {
		uint64_t p0 = nsec_now();
		send(rpc_cfd, msg, 32, 0);
		size_t got = 0;
		while (got < 32)
			got += (size_t)recv(rpc_cfd, msg + got, 32 - got, 0);
		lats[i] = nsec_now() - p0;
		if ((i & 15) == 0)
			usleep(100);
	}

	atomic_store_explicit(&stop, true, memory_order_relaxed);
	for (int s = 0; s < 8; s++)
		pthread_join(tth[s], NULL);
	uint64_t t1 = nsec_now();

	close(rpc_cfd);
	pthread_join(rpc_sth, NULL);
	close(rpc_lfd);
	for (int s = 0; s < 8; s++) {
		shutdown(lfds[s], SHUT_RDWR);
		close(lfds[s]);
		pthread_join(rth[s], NULL);
	}

	qsort(lats, N_RPC, sizeof(uint64_t), cmp_u64);
	double gbps = ((double)atomic_load(&total_bytes) * 8.0 / 1e9) / ((double)(t1 - t0) / 1e9);
	emit_sample("S5_Qdisc", "qdisc_8flow_paced_gbps", trial, gbps, "Gbps", "higher");
	emit_sample("S5_Qdisc", "qdisc_loaded_rpc_p50_us", trial, (double)lats[N_RPC / 2] / 1000.0, "us", "lower");
	emit_sample("S5_Qdisc", "qdisc_loaded_rpc_p99_us", trial, (double)lats[(size_t)(N_RPC * 0.99)] / 1000.0, "us", "lower");
	free(lats);
}

// ============================================================================
// Background IPC Storm Generator (--ipc-storm) for Wayland Multitasking Test
// ============================================================================
static atomic_bool g_storm_stop = false;
static void on_sigterm(int sig)
{
	(void)sig;
	atomic_store(&g_storm_stop, true);
}

static void *bg_storm_worker(void *arg)
{
	int idx = (int)(intptr_t)arg;
	pin_cpu(4 + (idx % 4)); // Keep storm on logical siblings while leaving primary cores realistic
	int sv[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	char buf[1024];
	memset(buf, 0x55, sizeof(buf));
	while (!atomic_load_explicit(&g_storm_stop, memory_order_relaxed)) {
		for (int i = 0; i < 64; i++) {
			send(sv[0], buf, 256, 0);
			recv(sv[1], buf, 256, 0);
		}
		sched_yield();
	}
	close(sv[0]);
	close(sv[1]);
	return NULL;
}

int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);
	raise_nofile_limit();

	if (argc >= 2 && strcmp(argv[1], "--ipc-storm") == 0) {
		signal(SIGTERM, on_sigterm);
		signal(SIGINT, on_sigterm);
		pthread_t th[4];
		for (int i = 0; i < 4; i++)
			pthread_create(&th[i], NULL, bg_storm_worker, (void *)(intptr_t)i);
		for (int i = 0; i < 4; i++)
			pthread_join(th[i], NULL);
		return 0;
	}

	if (argc >= 3 && strcmp(argv[1], "--qdisc-trial") == 0) {
		int t = atoi(argv[2]);
		run_qdisc_contention_trial(t);
		return 0;
	}

	int trials = 10;
	if (argc >= 3 && strcmp(argv[1], "--trial") == 0) {
		g_trial_offset = atoi(argv[2]);
		if (g_trial_offset < 0)
			g_trial_offset = 0;
		trials = 1;
	} else if (argc >= 2) {
		trials = atoi(argv[1]);
		if (trials < 1)
			trials = 10;
	}

	pthread_t mt_holder;
	pthread_create(&mt_holder, NULL, mt_holder_fn, NULL);

	run_suite1_memory(trials);
	run_suite2_vfs_epoll(trials);
	run_suite3_unix_ipc(trials);
	run_suite4_tcp_udp(trials);

	atomic_store(&g_keep_mt_alive, false);
	pthread_join(mt_holder, NULL);
	return 0;
}
