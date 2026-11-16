// SPDX-License-Identifier: GPL-2.0
/*
 * net/sched/sch_eiffel_dualq.c - Lockless 2-Level tzcnt Timing-Wheel EDT Pacer
 *                                + GSO-Preserving RFC 9332 DualQ Coupled L4S AQM
 *
 * Architecture:
 *  1. Lockless MPSC ingress ring (TCQ_F_NOLOCK | TCQ_F_CPUSTATS) eliminating
 *     root qdisc spinlock contention across multi-core senders, while
 *     seamlessly supporting child attachment under TBF/HTB shapers.
 *  2. Automatic SK_PACING_FQ TCP EDT offload + zero-allocation 256-slot atomic
 *     4-tuple UDP/QUIC pacing engine.
 *  3. O(1) 2-level radix tzcnt (__ffs64) hierarchical timing wheel
 *     (64 x 65.5us L0 + 64 x 4.19ms L1 + far-horizon overflow) bypassing the
 *     wheel completely for unpaced/ready packets.
 *  4. GSO-preserving RFC 9332 DualQ Coupled AQM:
 *     - L-queue (ECT(1) / CE): 15:1 WRR priority + 1ms..2ms shallow ramp CE
 *       marking without splitting GSO super-packets.
 *     - C-queue (Not-ECT / ECT(0)): 15ms PI2 target with coupled p_C = (p')^2
 *       and p_L = max(p'_L, 2 * p') for L4S/Classic throughput fairness.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netzero_tigerlake.h>
#include <linux/random.h>
#include <linux/skb_array.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/inet_ecn.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/sch_generic.h>
#include <net/sock.h>
#include <net/tcp.h>

#define EIFFEL_RING_DEFAULT	2048U
#define EIFFEL_LIMIT_DEFAULT	10240U
#define EIFFEL_UDP_FLOWS	256U

/* Timing wheel constants: L0 = 65.536 us (2^16 ns), L1 = 4.194 ms (2^22 ns) */
#define EIFFEL_W_SHIFT		16U
#define EIFFEL_SLACK_NS		(1ULL << 14)		/* 16.384 us slack */
#define EIFFEL_HORIZON_NS	(2ULL * NSEC_PER_SEC)	/* 2.0 s horizon */

/* Sojourn clock shift: 1 tick = 1,024 ns (~1.024 us) */
#define EIFFEL_T_SHIFT		10U
#define EIFFEL_L_MIN_T		977U	/* ~1.0 ms L4S ramp start */
#define EIFFEL_L_RANGE_T	976U	/* ~1.0 ms ramp width (1.0..2.0 ms) */
#define EIFFEL_C_TARGET_T	14648U	/* ~15.0 ms Classic target */
#define EIFFEL_TUPDATE_T	15625U	/* ~16.0 ms PI2 update interval */

struct eiffel_skb_cb {
	u64	edt_ns;
	u32	enq_t;
	u8	is_l4s;
	u8	ecn;
	u16	pad;
};

static inline struct eiffel_skb_cb *eiffel_cb(struct sk_buff *skb)
{
	qdisc_cb_private_validate(skb, sizeof(struct eiffel_skb_cb));
	return (struct eiffel_skb_cb *)qdisc_skb_cb(skb)->data;
}

struct eiffel_dualq_sched_data {
	/*Producer-hot cache lines: lockless MPSC ring + UDP 4-tuple EDT table*/
	struct skb_array	ingress;
	atomic64_t		udp_flow_edt[EIFFEL_UDP_FLOWS];

	/*Consumer-only cache lines: ready DualQ + 2-level tzcnt timing wheel */
	struct qdisc_skb_head	l_queue;
	struct qdisc_skb_head	c_queue;
	u32			ready_backlog;
	s32			c_credit;
	u32			limit;

	/* Coupled PI2 + L4S ramp state */
	u32			last_prob_t;
	u32			prev_qdelay_t;
	u32			c_sojourn_t;
	u32			l_sojourn_t;
	u16			base_prob;	/* p' in [0..65535] */

	/* 2-level radix tzcnt timing wheel */
	u64			wheel_clk;
	u64			l0_bmap;
	u64			l1_bmap;
	u32			wheel_qlen;
	struct qdisc_skb_head	l0[64];
	struct qdisc_skb_head	l1[64];
	struct qdisc_skb_head	overflow;
	struct qdisc_watchdog	watchdog;

	/* Diagnostics */
	u64			stat_ce_marks;
	u64			stat_paced_pkts;
	u64			stat_aqm_drops;
};

static u8 eiffel_get_ecn(const struct sk_buff *skb)
{
	switch (skb_protocol(skb, true)) {
	case cpu_to_be16(ETH_P_IP):
		if (skb_network_header(skb) + sizeof(struct iphdr) <=
		    skb_tail_pointer(skb))
			return ip_hdr(skb)->tos & INET_ECN_MASK;
		break;
	case cpu_to_be16(ETH_P_IPV6):
		if (skb_network_header(skb) + sizeof(struct ipv6hdr) <=
		    skb_tail_pointer(skb))
			return ipv6_get_dsfield(ipv6_hdr(skb)) & INET_ECN_MASK;
		break;
	}
	return INET_ECN_NOT_ECT;
}

static u64 eiffel_skb_edt_ns(const struct sk_buff *skb)
{
	const ktime_t mono_max = NSEC_PER_SEC * TIME_UPTIME_SEC_MAX;

	if (!skb->tstamp)
		return 0;
	if (likely(skb->tstamp_type == SKB_CLOCK_MONOTONIC))
		return (u64)max_t(s64, skb->tstamp, 0);
	if (skb->tstamp_type == SKB_CLOCK_TAI)
		return (u64)max_t(s64,
			ktime_sub(skb->tstamp, ktime_mono_to_any(0, TK_OFFS_TAI)), 0);
	if (likely(skb->tstamp > mono_max))
		return (u64)max_t(s64,
			ktime_sub(skb->tstamp, ktime_mono_to_real(0)), 0);
	return (u64)max_t(s64, skb->tstamp, 0);
}

static int eiffel_dualq_enqueue(struct sk_buff *skb, struct Qdisc *sch,
				struct sk_buff **to_free)
{
	struct eiffel_dualq_sched_data *q = qdisc_priv(sch);
	unsigned int pkt_len = qdisc_pkt_len(skb);
	struct eiffel_skb_cb *cb = eiffel_cb(skb);
	struct sock *sk = skb->sk;
	u64 now_ns, edt_ns;
	u8 ecn;

	now_ns = ktime_get_ns();
	edt_ns = eiffel_skb_edt_ns(skb);

	if (sk && sk_fullsock(sk) && !sk_listener_or_tw(sk)) {
		if (READ_ONCE(sk->sk_pacing_status) != SK_PACING_FQ)
			smp_store_release(&sk->sk_pacing_status, SK_PACING_FQ);

		/* Lockless 4-tuple pacing for UDP / QUIC sockets */
		if (!edt_ns && !sk_is_tcp(sk)) {
			unsigned long rate = READ_ONCE(sk->sk_pacing_rate);

			if (rate && rate != ~0UL) {
				u32 h = ((sk->sk_state == TCP_ESTABLISHED) ?
					 sk->sk_hash : skb_get_hash(skb)) &
					(EIFFEL_UDP_FLOWS - 1);
				u64 delay_ns = min_t(u64,
					div64_ul((u64)pkt_len * NSEC_PER_SEC, rate),
					NSEC_PER_SEC);
				u64 prev = (u64)atomic64_read(&q->udp_flow_edt[h]);
				u64 base = max(now_ns, prev);

				if (base - now_ns < EIFFEL_HORIZON_NS) {
					atomic64_set(&q->udp_flow_edt[h],
						     base + delay_ns);
					edt_ns = base;
				}
			}
		}
	}

	ecn = eiffel_get_ecn(skb);
	cb->edt_ns = edt_ns;
	cb->enq_t = (u32)(max(now_ns, edt_ns) >> EIFFEL_T_SHIFT);
	cb->ecn = ecn;
	cb->is_l4s = (ecn == INET_ECN_ECT_1 || ecn == INET_ECN_CE);

	if (unlikely(skb_array_produce(&q->ingress, skb))) {
		tcf_set_qdisc_drop_reason(skb, QDISC_DROP_OVERLIMIT);
		if (qdisc_is_percpu_stats(sch))
			return qdisc_drop_cpu(skb, sch, to_free);
		return qdisc_drop(skb, sch, to_free);
	}

	qdisc_update_stats_at_enqueue(sch, pkt_len);
	return NET_XMIT_SUCCESS;
}

static inline void eiffel_ready_push(struct eiffel_dualq_sched_data *q,
				     struct sk_buff *skb)
{
	if (eiffel_cb(skb)->is_l4s)
		__qdisc_enqueue_tail(skb, &q->l_queue);
	else
		__qdisc_enqueue_tail(skb, &q->c_queue);
	q->ready_backlog += qdisc_pkt_len(skb);
}

static void eiffel_wheel_place(struct eiffel_dualq_sched_data *q,
			       struct sk_buff *skb, u64 now_ns)
{
	u64 edt_ns = min(eiffel_cb(skb)->edt_ns, now_ns + EIFFEL_HORIZON_NS);
	u64 slot;

	if (edt_ns <= now_ns + EIFFEL_SLACK_NS) {
		eiffel_ready_push(q, skb);
		return;
	}

	slot = edt_ns >> EIFFEL_W_SHIFT;
	if (slot <= q->wheel_clk) {
		eiffel_ready_push(q, skb);
		return;
	}

	eiffel_cb(skb)->edt_ns = edt_ns;
	q->stat_paced_pkts++;
	q->wheel_qlen++;

	if ((slot >> 6) == (q->wheel_clk >> 6)) {
		u32 idx = slot & 63;

		__qdisc_enqueue_tail(skb, &q->l0[idx]);
		q->l0_bmap |= (1ULL << idx);
	} else if ((slot >> 12) == (q->wheel_clk >> 12)) {
		u32 idx = (slot >> 6) & 63;

		__qdisc_enqueue_tail(skb, &q->l1[idx]);
		q->l1_bmap |= (1ULL << idx);
	} else {
		__qdisc_enqueue_tail(skb, &q->overflow);
	}
}

static void eiffel_drain_qhead_to_ready(struct eiffel_dualq_sched_data *q,
					struct qdisc_skb_head *qh)
{
	struct sk_buff *skb;

	while ((skb = __qdisc_dequeue_head(qh)) != NULL) {
		q->wheel_qlen--;
		eiffel_ready_push(q, skb);
	}
}

static void eiffel_rebucket_qhead(struct eiffel_dualq_sched_data *q,
				  struct qdisc_skb_head *src, u64 now_ns)
{
	struct qdisc_skb_head tmp = *src;
	struct sk_buff *skb;

	src->head = NULL;
	src->tail = NULL;
	WRITE_ONCE(src->qlen, 0);

	while ((skb = __qdisc_dequeue_head(&tmp)) != NULL) {
		q->wheel_qlen--;
		eiffel_wheel_place(q, skb, now_ns);
	}
}

static void eiffel_wheel_advance(struct eiffel_dualq_sched_data *q, u64 now_ns)
{
	u64 now_clk = now_ns >> EIFFEL_W_SHIFT;

	if (!q->wheel_qlen) {
		q->wheel_clk = now_clk;
		return;
	}

	while (q->wheel_clk <= now_clk && q->wheel_qlen) {
		u64 l0_base = q->wheel_clk & ~63ULL;

		/* 1. Drain all matured slots in current L0 64-slot epoch via BMI1 tzcnt/blsr */
		while (q->l0_bmap) {
			u32 idx = nz_tzcnt64(q->l0_bmap);
			u64 slot = l0_base | idx;

			if (slot > now_clk) {
				q->wheel_clk = now_clk;
				return;
			}
			q->l0_bmap = nz_blsr64(q->l0_bmap);
			eiffel_drain_qhead_to_ready(q, &q->l0[idx]);
		}

		if ((now_clk >> 6) == (q->wheel_clk >> 6)) {
			q->wheel_clk = now_clk;
			return;
		}

		/* 2. Advance to next L1 64-slot boundary */
		q->wheel_clk = l0_base + 64ULL;

		/* Re-bucket far-horizon overflow if we crossed a 4096-slot boundary */
		if ((q->wheel_clk & 4095ULL) == 0 && q->overflow.qlen)
			eiffel_rebucket_qhead(q, &q->overflow, now_ns);

		/* 3. Jump via BMI1 tzcnt/blsr to next occupied L1 bucket in current 4096-epoch */
		if (q->l1_bmap) {
			u32 l1_idx = nz_tzcnt64(q->l1_bmap);
			u64 cascade_clk = (q->wheel_clk & ~4095ULL) |
					  ((u64)l1_idx << 6);

			if (cascade_clk > now_clk) {
				q->wheel_clk = now_clk;
				return;
			}
			q->wheel_clk = max(q->wheel_clk, cascade_clk);
			q->l1_bmap = nz_blsr64(q->l1_bmap);
			eiffel_rebucket_qhead(q, &q->l1[l1_idx], now_ns);
		} else if (q->overflow.qlen) {
			u64 next_ovf = (q->wheel_clk & ~4095ULL) + 4096ULL;

			if (next_ovf > now_clk) {
				q->wheel_clk = now_clk;
				return;
			}
			q->wheel_clk = next_ovf;
			eiffel_rebucket_qhead(q, &q->overflow, now_ns);
		} else {
			q->wheel_clk = now_clk;
			return;
		}
	}
}

static void eiffel_schedule_watchdog(struct eiffel_dualq_sched_data *q,
				     u64 now_ns)
{
	u64 next_slot, wakeup_ns;

	if (q->l0_bmap)
		next_slot = (q->wheel_clk & ~63ULL) | nz_tzcnt64(q->l0_bmap);
	else if (q->l1_bmap)
		next_slot = (q->wheel_clk & ~4095ULL) |
			    ((u64)nz_tzcnt64(q->l1_bmap) << 6);
	else if (q->overflow.qlen)
		next_slot = (q->wheel_clk & ~4095ULL) + 4096ULL;
	else
		return;

	wakeup_ns = max(next_slot << EIFFEL_W_SHIFT, now_ns + 4000ULL);
	qdisc_watchdog_schedule_range_ns(&q->watchdog, wakeup_ns,
					 EIFFEL_SLACK_NS);
}

static void eiffel_drop_ready_skb(struct Qdisc *sch,
				  struct eiffel_dualq_sched_data *q,
				  struct sk_buff *skb,
				  u32 *drop_cnt, u32 *drop_len)
{
	unsigned int len = qdisc_pkt_len(skb);

	q->stat_aqm_drops++;
	if (qdisc_is_percpu_stats(sch)) {
		qdisc_qstats_cpu_backlog_dec(sch, skb);
		qdisc_qstats_cpu_qlen_dec(sch);
		qdisc_qstats_cpu_drop(sch);
	} else {
		qdisc_qstats_backlog_dec(sch, skb);
		qdisc_qlen_dec(sch);
		qdisc_qstats_drop(sch);
		(*drop_cnt)++;
		(*drop_len) += len;
	}
	kfree_skb_reason(skb, (enum skb_drop_reason)QDISC_DROP_CONGESTED);
}

/* Returns true if skb should be dropped by DualQ Coupled AQM */
static bool eiffel_dualq_aqm(struct Qdisc *sch,
			     struct eiffel_dualq_sched_data *q,
			     struct sk_buff *skb, u64 now_ns)
{
	struct eiffel_skb_cb *cb = eiffel_cb(skb);
	u32 now_t = (u32)(now_ns >> EIFFEL_T_SHIFT);
	s32 diff = (s32)(now_t - cb->enq_t);
	u32 sojourn_t = diff > 0 ? (u32)diff : 0;
	u32 mtu = psched_mtu(qdisc_dev(sch));

	if (cb->is_l4s)
		q->l_sojourn_t = sojourn_t;
	else
		q->c_sojourn_t = sojourn_t;

	/* Update RFC 9332 PI2 coupled base probability p' every ~16 ms */
	if ((u32)(now_t - q->last_prob_t) >= EIFFEL_TUPDATE_T) {
		u32 qdelay_t = max(q->c_queue.qlen ? q->c_sojourn_t : 0U,
				   q->l_queue.qlen ? q->l_sojourn_t : 0U);
		s64 err = (s64)qdelay_t - (s64)EIFFEL_C_TARGET_T;
		s64 prev_err = (s64)q->prev_qdelay_t - (s64)EIFFEL_C_TARGET_T;
		s64 delta = (err - prev_err) * 3 + (err >> 2);

		q->base_prob = (u16)clamp_t(s64, (s64)q->base_prob + delta,
					    0, 65535);
		q->prev_qdelay_t = qdelay_t;
		q->last_prob_t = now_t;
	}

	/* Never mark or drop when remaining standing queue is < 2 MTUs */
	if (q->ready_backlog < 2 * mtu)
		return false;

	if (cb->is_l4s) {
		/* GSO-preserving shallow ramp (1.0..2.0 ms) + coupled 2*p' */
		u32 ramp_p = 0;
		u32 coupled_p = min_t(u32, 65535U, (u32)q->base_prob * 2U);
		u32 p_l;

		if (sojourn_t >= EIFFEL_L_MIN_T + EIFFEL_L_RANGE_T)
			ramp_p = 65535U;
		else if (sojourn_t > EIFFEL_L_MIN_T)
			ramp_p = ((sojourn_t - EIFFEL_L_MIN_T) * 65535U) /
				 EIFFEL_L_RANGE_T;

		p_l = max(ramp_p, coupled_p);
		if (p_l >= 65535U ||
		    (p_l > 0 && (get_random_u32() & 0xFFFFU) < p_l)) {
			if (INET_ECN_set_ce(skb))
				q->stat_ce_marks++;
		}
		return false;
	}

	/* Classic C-queue: coupled squared probability p_C = (p')^2 */
	if (q->base_prob > 0) {
		u32 p_c = ((u32)q->base_prob * (u32)q->base_prob) >> 16;

		if (p_c > 0 && (get_random_u32() & 0xFFFFU) < p_c) {
			if (cb->ecn != INET_ECN_NOT_ECT &&
			    q->base_prob < 32768U &&
			    INET_ECN_set_ce(skb)) {
				q->stat_ce_marks++;
				return false;
			}
			return true; /* Drop Classic packet to signal congestion */
		}
	}
	return false;
}

static struct sk_buff *eiffel_pop_ready(struct eiffel_dualq_sched_data *q)
{
	struct sk_buff *skb = NULL;
	bool has_l = q->l_queue.qlen > 0;
	bool has_c = q->c_queue.qlen > 0;

	if (has_l && (!has_c || q->c_credit <= 0)) {
		skb = __qdisc_dequeue_head(&q->l_queue);
		if (has_c && skb)
			q->c_credit += (s32)qdisc_pkt_len(skb);
	} else if (has_c) {
		skb = __qdisc_dequeue_head(&q->c_queue);
		if (has_l && skb)
			q->c_credit -= (s32)(15U * qdisc_pkt_len(skb));
	} else {
		q->c_credit = 0;
		q->l_sojourn_t = 0;
		q->c_sojourn_t = 0;
		return NULL;
	}

	if (skb)
		q->ready_backlog -= qdisc_pkt_len(skb);
	return skb;
}

static struct sk_buff *eiffel_dualq_dequeue(struct Qdisc *sch)
{
	struct eiffel_dualq_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb = NULL, *in_skb;
	u32 drop_cnt = 0, drop_len = 0;
	bool need_retry = true;
	u64 now_ns;

retry:
	now_ns = ktime_get_ns();

	/* 1. Drain lockless MPSC ingress ring into ready DualQ or timing wheel */
	if (!__skb_array_empty(&q->ingress)) {
		while ((in_skb = __skb_array_consume(&q->ingress)) != NULL)
			eiffel_wheel_place(q, in_skb, now_ns);
	}

	/* 2. Advance O(1) tzcnt timing wheel to release matured EDT packets */
	eiffel_wheel_advance(q, now_ns);

	/* 3. Enforce memory safety bound on extreme overload */
	while (unlikely(q->l_queue.qlen + q->c_queue.qlen > q->limit)) {
		struct sk_buff *ovf = q->c_queue.qlen ?
			__qdisc_dequeue_head(&q->c_queue) :
			__qdisc_dequeue_head(&q->l_queue);
		if (!ovf)
			break;
		q->ready_backlog -= qdisc_pkt_len(ovf);
		eiffel_drop_ready_skb(sch, q, ovf, &drop_cnt, &drop_len);
	}

	/* 4. Dequeue via 15:1 WRR DualQ + RFC 9332 Coupled AQM */
	while ((skb = eiffel_pop_ready(q)) != NULL) {
		if (unlikely(eiffel_dualq_aqm(sch, q, skb, now_ns))) {
			eiffel_drop_ready_skb(sch, q, skb, &drop_cnt, &drop_len);
			continue;
		}
		break;
	}

	if (likely(skb)) {
		if (!qdisc_dev(sch)->pacing_offload &&
		    skb->tstamp_type == SKB_CLOCK_MONOTONIC)
			skb->tstamp = 0;
		qdisc_update_stats_at_dequeue(sch, skb);
	} else if (need_retry &&
		   (READ_ONCE(sch->state) & QDISC_STATE_NON_EMPTY)) {
		clear_bit(__QDISC_STATE_MISSED, &sch->state);
		clear_bit(__QDISC_STATE_DRAINING, &sch->state);
		smp_mb__after_atomic();
		need_retry = false;
		goto retry;
	} else if (q->wheel_qlen > 0) {
		eiffel_schedule_watchdog(q, now_ns);
	}

	if (unlikely(drop_cnt))
		qdisc_tree_reduce_backlog(sch, drop_cnt, drop_len);

	return skb;
}

static void eiffel_purge_qhead(struct qdisc_skb_head *qh)
{
	if (qh->qlen) {
		rtnl_kfree_skbs(qh->head, qh->tail);
		qh->head = NULL;
		qh->tail = NULL;
		WRITE_ONCE(qh->qlen, 0);
	}
}

static void eiffel_dualq_reset(struct Qdisc *sch)
{
	struct eiffel_dualq_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;
	int i;

	qdisc_watchdog_cancel(&q->watchdog);

	if (q->ingress.ring.queue) {
		while ((skb = __skb_array_consume(&q->ingress)) != NULL)
			rtnl_kfree_skbs(skb, skb);
	}

	eiffel_purge_qhead(&q->l_queue);
	eiffel_purge_qhead(&q->c_queue);
	for (i = 0; i < 64; i++) {
		eiffel_purge_qhead(&q->l0[i]);
		eiffel_purge_qhead(&q->l1[i]);
	}
	eiffel_purge_qhead(&q->overflow);

	q->ready_backlog = 0;
	q->c_credit = 0;
	q->base_prob = 0;
	q->prev_qdelay_t = 0;
	q->l_sojourn_t = 0;
	q->c_sojourn_t = 0;
	q->l0_bmap = 0;
	q->l1_bmap = 0;
	q->wheel_qlen = 0;

	if (qdisc_is_percpu_stats(sch)) {
		for_each_possible_cpu(i) {
			struct gnet_stats_queue *qs;

			qs = per_cpu_ptr(sch->cpu_qstats, i);
			qs->backlog = 0;
			qs->qlen = 0;
		}
	}
}

static int eiffel_dualq_init(struct Qdisc *sch, struct nlattr *opt,
			     struct netlink_ext_ack *extack)
{
	struct eiffel_dualq_sched_data *q = qdisc_priv(sch);
	unsigned int ring_len;
	int err, i;

	qdisc_watchdog_init(&q->watchdog, sch);
	ring_len = max_t(unsigned int, qdisc_dev(sch)->tx_queue_len,
			 EIFFEL_RING_DEFAULT);
	q->limit = max_t(unsigned int, ring_len * 2, EIFFEL_LIMIT_DEFAULT);
	sch->limit = q->limit;
	q->wheel_clk = ktime_get_ns() >> EIFFEL_W_SHIFT;
	q->last_prob_t = (u32)(ktime_get_ns() >> EIFFEL_T_SHIFT);

	for (i = 0; i < EIFFEL_UDP_FLOWS; i++)
		atomic64_set(&q->udp_flow_edt[i], 0);

	err = skb_array_init(&q->ingress, ring_len, GFP_KERNEL);
	if (err)
		return err;

	return 0;
}

static void eiffel_dualq_destroy(struct Qdisc *sch)
{
	struct eiffel_dualq_sched_data *q = qdisc_priv(sch);

	qdisc_watchdog_cancel(&q->watchdog);
	if (q->ingress.ring.queue)
		ptr_ring_cleanup(&q->ingress.ring, NULL);
}

static int eiffel_dualq_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	return skb->len;
}

static struct Qdisc_ops eiffel_dualq_qdisc_ops __read_mostly = {
	.id		=	"eiffel_dualq",
	.priv_size	=	sizeof(struct eiffel_dualq_sched_data),
	.enqueue	=	eiffel_dualq_enqueue,
	.dequeue	=	eiffel_dualq_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	eiffel_dualq_init,
	.reset		=	eiffel_dualq_reset,
	.destroy	=	eiffel_dualq_destroy,
	.dump		=	eiffel_dualq_dump,
	.owner		=	THIS_MODULE,
	.static_flags	=	TCQ_F_NOLOCK | TCQ_F_CPUSTATS,
};
MODULE_ALIAS_NET_SCH("eiffel_dualq");

static int __init eiffel_dualq_module_init(void)
{
	return register_qdisc(&eiffel_dualq_qdisc_ops);
}

static void __exit eiffel_dualq_module_exit(void)
{
	unregister_qdisc(&eiffel_dualq_qdisc_ops);
}

module_init(eiffel_dualq_module_init);
module_exit(eiffel_dualq_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Lockless 2-Level tzcnt Timing-Wheel EDT Pacer + DualQ L4S AQM");
