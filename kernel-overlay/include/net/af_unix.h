/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NET_AFUNIX_H
#define __LINUX_NET_AFUNIX_H

#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/path.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <net/sock.h>
#include <uapi/linux/un.h>

struct iovec;

#if IS_ENABLED(CONFIG_UNIX)
extern const struct proto_ops unix_stream_ops;
struct unix_sock *unix_get_socket(struct file *filp);
void unix_wfree(struct sk_buff *skb);
int unix_stream_sendmsg(struct socket *sock, struct msghdr *msg, size_t len);
int unix_dgram_sendmsg(struct socket *sock, struct msghdr *msg, size_t len);
int unix_stream_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
			int flags);
int unix_dgram_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
		       int flags);
ssize_t unix_ring_direct_send(struct socket *sock, const void __user *ubuf,
			      size_t len, unsigned int flags, bool *handled);
ssize_t unix_ring_direct_sendv(struct socket *sock,
			       const struct iovec *iov,
			       unsigned long nr_segs,
			       size_t total_len,
			       unsigned int flags,
			       bool *handled);
ssize_t unix_ring_direct_recv(struct socket *sock, void __user *ubuf,
			      size_t size, unsigned int flags, bool *handled);
long unix_inq_len(struct sock *sk);
__poll_t unix_stream_poll_nowait(struct socket *sock, __poll_t requested);
__poll_t unix_poll(struct file *file, struct socket *sock, poll_table *wait);
__poll_t unix_dgram_poll(struct file *file, struct socket *sock,
			 poll_table *wait);
#else
static inline struct unix_sock *unix_get_socket(struct file *filp)
{
	return NULL;
}
#endif

struct unix_address {
	refcount_t	refcnt;
	int		len;
	struct sockaddr_un name[];
};

struct scm_stat {
	atomic_t nr_fds;
	unsigned long nr_unix_fds;
};

struct unix_ring;

/* The AF_UNIX socket */
struct unix_sock {
	/* WARNING: sk has to be the first member */
	struct sock		sk;
	struct unix_address	*addr;
	struct path		path;
	struct mutex		iolock, bindlock;
	struct sock		*peer;
	struct sock		*listener;
	struct unix_vertex	*vertex;
	spinlock_t		lock;
	int			inq_len;
	struct socket_wq	peer_wq;
#define peer_wait		peer_wq.wait
	union {
		wait_queue_entry_t	peer_wake;
		struct {
			struct unix_ring	*rx_ring;
#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
			struct sk_buff		*oob_skb;
#endif
		};
	};
	struct scm_stat		scm_stat;
	bool			recvmsg_inq;
	bool			scm_rights_notrunc;
	bool			tx_ring_full;
	atomic_t		io_fast;
};

#define unix_sk(ptr) container_of_const(ptr, struct unix_sock, sk)
#define unix_peer(sk) (unix_sk(sk)->peer)

#define unix_state_lock(s)	spin_lock(&unix_sk(s)->lock)
#define unix_state_unlock(s)	spin_unlock(&unix_sk(s)->lock)

#endif
