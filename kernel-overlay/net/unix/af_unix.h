/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __AF_UNIX_H
#define __AF_UNIX_H

#include <linux/pid_types.h>
#include <linux/preempt.h>
#include <linux/processor.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#define UNIX_HASH_MOD	(256 - 1)
#define UNIX_HASH_SIZE	(256 * 2)
#define UNIX_HASH_BITS	8

struct sock *unix_peer_get(struct sock *sk);

struct unix_skb_parms {
	DECLARE_PIDS(pid, PIDTYPE_TGID);	/* skb credentials by pid type */
	kuid_t			uid;
	kgid_t			gid;
	struct scm_fp_list	*fp;		/* Passed files		*/
#ifdef CONFIG_SECURITY_NETWORK
	u32			secid;		/* Security ID		*/
#endif
	u32			consumed;
	u64			ring_seq;	/* unix_ring byte barrier */
} __randomize_layout;

bool unix_stream_ring_readable(const struct sock *sk);

/*
 * Two-level consumer serialization for SOCK_STREAM rx_ring:
 *   u->io_fast == 0 : free
 *   u->io_fast == 1 : held by in-atomic fastpath (<= 4 KiB, preempt & pagefault disabled)
 *   u->io_fast == 2 : held by slowpath mutex owner (&u->iolock)
 *
 * Fastpath acquires via single cmpxchg_acquire(0 -> 1) and releases via plain
 * store_release(0) with zero atomic RMW on unlock. Slowpath holds &u->iolock
 * and drains any bounded <= 4 KiB in-atomic holder before setting 2.
 */
static inline bool unix_io_try_fast(struct unix_sock *u)
{
	int zero = 0;

	preempt_disable();
	pagefault_disable();
	if (likely(atomic_try_cmpxchg_acquire(&u->io_fast, &zero, 1)))
		return true;
	pagefault_enable();
	preempt_enable();
	return false;
}

static inline void unix_io_unlock_fast(struct unix_sock *u)
{
	atomic_set_release(&u->io_fast, 0);
	pagefault_enable();
	preempt_enable();
}

static inline void unix_io_lock(struct unix_sock *u)
{
	mutex_lock(&u->iolock);
	for (;;) {
		int zero = 0;

		if (likely(atomic_try_cmpxchg_acquire(&u->io_fast, &zero, 2)))
			break;
		cpu_relax();
	}
}

static inline bool unix_io_trylock(struct unix_sock *u)
{
	int zero = 0;

	if (!mutex_trylock(&u->iolock))
		return false;
	if (!atomic_try_cmpxchg_acquire(&u->io_fast, &zero, 2)) {
		mutex_unlock(&u->iolock);
		return false;
	}
	return true;
}

static inline void unix_io_unlock(struct unix_sock *u)
{
	atomic_set_release(&u->io_fast, 0);
	mutex_unlock(&u->iolock);
}

#define UNIXCB(skb)	(*(struct unix_skb_parms *)&((skb)->cb))

/* GC for SCM_RIGHTS */
void unix_add_edges(struct scm_fp_list *fpl, struct unix_sock *receiver);
void unix_del_edges(struct scm_fp_list *fpl);
void unix_update_edges(struct unix_sock *receiver);
int unix_prepare_fpl(struct scm_fp_list *fpl);
void unix_destroy_fpl(struct scm_fp_list *fpl);
void unix_peek_fpl(struct scm_fp_list *fpl);
void unix_schedule_gc(struct user_struct *user);

/* SOCK_DIAG */
long unix_inq_len(struct sock *sk);
long unix_outq_len(struct sock *sk);

/* sysctl */
#ifdef CONFIG_SYSCTL
int unix_sysctl_register(struct net *net);
void unix_sysctl_unregister(struct net *net);
#else
static inline int unix_sysctl_register(struct net *net)
{
	return 0;
}

static inline void unix_sysctl_unregister(struct net *net)
{
}
#endif

/* BPF SOCKMAP */
int __unix_dgram_recvmsg(struct sock *sk, struct msghdr *msg, size_t size, int flags);
int __unix_stream_recvmsg(struct sock *sk, struct msghdr *msg, size_t size, int flags);

#ifdef CONFIG_BPF_SYSCALL
extern struct proto unix_dgram_proto;
extern struct proto unix_stream_proto;

int unix_dgram_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore);
int unix_stream_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore);
void __init unix_bpf_build_proto(void);
#else
static inline void __init unix_bpf_build_proto(void)
{
}
#endif

#endif
