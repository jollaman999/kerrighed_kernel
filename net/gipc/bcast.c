/*
 * net/gipc/bcast.c: GIPC broadcast code
 *
 * Copyright (c) 2004-2006, Ericsson AB
 * Copyright (c) 2004, Intel Corporation.
 * Copyright (c) 2005, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "msg.h"
#include "dbg.h"
#include "link.h"
#include "net.h"
#include "node.h"
#include "port.h"
#include "addr.h"
#include "node_subscr.h"
#include "name_distr.h"
#include "bearer.h"
#include "name_table.h"
#include "bcast.h"

#define MAX_PKT_DEFAULT_MCAST 1500	/* bcast link max packet size (fixed) */

#define BCLINK_WIN_DEFAULT 20		/* bcast link window size (default) */

#define BCLINK_LOG_BUF_SIZE 0

/*
 * Loss rate for incoming broadcast frames; used to test retransmission code.
 * Set to N to cause every N'th frame to be discarded; 0 => don't discard any.
 */

#define GIPC_BCAST_LOSS_RATE 0

/**
 * struct bcbearer_pair - a pair of bearers used by broadcast link
 * @primary: pointer to primary bearer
 * @secondary: pointer to secondary bearer
 *
 * Bearers must have same priority and same set of reachable destinations
 * to be paired.
 */

struct bcbearer_pair {
	struct bearer *primary;
	struct bearer *secondary;
};

/**
 * struct bcbearer - bearer used by broadcast link
 * @bearer: (non-standard) broadcast bearer structure
 * @media: (non-standard) broadcast media structure
 * @bpairs: array of bearer pairs
 * @bpairs_temp: temporary array of bearer pairs used by gipc_bcbearer_sort()
 * @remains: temporary node map used by gipc_bcbearer_send()
 * @remains_new: temporary node map used gipc_bcbearer_send()
 *
 * Note: The fields labelled "temporary" are incorporated into the bearer
 * to avoid consuming potentially limited stack space through the use of
 * large local variables within multicast routines.  Concurrent access is
 * prevented through use of the spinlock "bc_lock".
 */

struct bcbearer {
	struct bearer bearer;
	struct media media;
	struct bcbearer_pair bpairs[MAX_BEARERS];
	struct bcbearer_pair bpairs_temp[GIPC_MAX_LINK_PRI + 1];
	struct gipc_node_map remains;
	struct gipc_node_map remains_new;
};

/**
 * struct bclink - link used for broadcast messages
 * @link: (non-standard) broadcast link structure
 * @node: (non-standard) node structure representing b'cast link's peer node
 *
 * Handles sequence numbering, fragmentation, bundling, etc.
 */

struct bclink {
	struct link link;
	struct gipc_node node;
};


static struct bcbearer *bcbearer = NULL;
static struct bclink *bclink = NULL;
static struct link *bcl = NULL;
static DEFINE_SPINLOCK(bc_lock);

const char gipc_bclink_name[] = "multicast-link";


static u32 buf_seqno(struct sk_buff *buf)
{
	return msg_seqno(buf_msg(buf));
}

static u32 bcbuf_acks(struct sk_buff *buf)
{
	return (u32)(unsigned long)GIPC_SKB_CB(buf)->handle;
}

static void bcbuf_set_acks(struct sk_buff *buf, u32 acks)
{
	GIPC_SKB_CB(buf)->handle = (void *)(unsigned long)acks;
}

static void bcbuf_decr_acks(struct sk_buff *buf)
{
	bcbuf_set_acks(buf, bcbuf_acks(buf) - 1);
}


/**
 * bclink_set_gap - set gap according to contents of current deferred pkt queue
 *
 * Called with 'node' locked, bc_lock unlocked
 */

static void bclink_set_gap(struct gipc_node *n_ptr)
{
	struct sk_buff *buf = n_ptr->bclink.deferred_head;

	n_ptr->bclink.gap_after = n_ptr->bclink.gap_to =
		mod(n_ptr->bclink.last_in);
	if (unlikely(buf != NULL))
		n_ptr->bclink.gap_to = mod(buf_seqno(buf) - 1);
}

/**
 * bclink_ack_allowed - test if ACK or NACK message can be sent at this moment
 *
 * This mechanism endeavours to prevent all nodes in network from trying
 * to ACK or NACK at the same time.
 *
 * Note: GIPC uses a different trigger to distribute ACKs than it does to
 *       distribute NACKs, but tries to use the same spacing (divide by 16).
 */

static int bclink_ack_allowed(u32 n)
{
	return((n % GIPC_MIN_LINK_WIN) == gipc_own_tag);
}


/**
 * bclink_retransmit_pkt - retransmit broadcast packets
 * @after: sequence number of last packet to *not* retransmit
 * @to: sequence number of last packet to retransmit
 *
 * Called with bc_lock locked
 */

static void bclink_retransmit_pkt(u32 after, u32 to)
{
	struct sk_buff *buf;

	buf = bcl->first_out;
	while (buf && less_eq(buf_seqno(buf), after)) {
		buf = buf->next;
	}
	gipc_link_retransmit(bcl, buf, mod(to - after));
}

/**
 * gipc_bclink_acknowledge - handle acknowledgement of broadcast packets
 * @n_ptr: node that sent acknowledgement info
 * @acked: broadcast sequence # that has been acknowledged
 *
 * Node is locked, bc_lock unlocked.
 */

void gipc_bclink_acknowledge(struct gipc_node *n_ptr, u32 acked)
{
	struct sk_buff *crs;
	struct sk_buff *next;
	unsigned int released = 0;

	if (less_eq(acked, n_ptr->bclink.acked))
		return;

	spin_lock_bh(&bc_lock);

	/* Skip over packets that node has previously acknowledged */

	crs = bcl->first_out;
	while (crs && less_eq(buf_seqno(crs), n_ptr->bclink.acked)) {
		crs = crs->next;
	}

	/* Update packets that node is now acknowledging */

	while (crs && less_eq(buf_seqno(crs), acked)) {
		next = crs->next;
		bcbuf_decr_acks(crs);
		if (bcbuf_acks(crs) == 0) {
			bcl->first_out = next;
			bcl->out_queue_size--;
			buf_discard(crs);
			released = 1;
		}
		crs = next;
	}
	n_ptr->bclink.acked = acked;

	/* Try resolving broadcast link congestion, if necessary */

	if (unlikely(bcl->next_out))
		gipc_link_push_queue(bcl);
	if (unlikely(released && !list_empty(&bcl->waiting_ports)))
		gipc_link_wakeup_ports(bcl, 0);
	spin_unlock_bh(&bc_lock);
}

/**
 * bclink_send_ack - unicast an ACK msg
 *
 * gipc_net_lock and node lock set
 */

static void bclink_send_ack(struct gipc_node *n_ptr)
{
	struct link *l_ptr = n_ptr->active_links[n_ptr->addr & 1];

	if (l_ptr != NULL)
		gipc_link_send_proto_msg(l_ptr, STATE_MSG, 0, 0, 0, 0, 0);
}

/**
 * bclink_send_nack- broadcast a NACK msg
 *
 * gipc_net_lock and node lock set
 */

static void bclink_send_nack(struct gipc_node *n_ptr)
{
	struct sk_buff *buf;
	struct gipc_msg *msg;

	if (!less(n_ptr->bclink.gap_after, n_ptr->bclink.gap_to))
		return;

	buf = buf_acquire(INT_H_SIZE);
	if (buf) {
		msg = buf_msg(buf);
		msg_init(msg, BCAST_PROTOCOL, STATE_MSG,
			 INT_H_SIZE, n_ptr->addr);
		msg_set_mc_netid(msg, gipc_net_id);
		msg_set_bcast_ack(msg, mod(n_ptr->bclink.last_in));
		msg_set_bcgap_after(msg, n_ptr->bclink.gap_after);
		msg_set_bcgap_to(msg, n_ptr->bclink.gap_to);
		msg_set_bcast_tag(msg, gipc_own_tag);

		if (gipc_bearer_send(&bcbearer->bearer, buf, NULL)) {
			bcl->stats.sent_nacks++;
			buf_discard(buf);
		} else {
			gipc_bearer_schedule(bcl->b_ptr, bcl);
			bcl->proto_msg_queue = buf;
			bcl->stats.bearer_congs++;
		}

		/*
		 * Ensure we doesn't send another NACK msg to the node
		 * until 16 more deferred messages arrive from it
		 * (i.e. helps prevent all nodes from NACK'ing at same time)
		 */

		n_ptr->bclink.nack_sync = gipc_own_tag;
	}
}

/**
 * gipc_bclink_check_gap - send a NACK if a sequence gap exists
 *
 * gipc_net_lock and node lock set
 */

void gipc_bclink_check_gap(struct gipc_node *n_ptr, u32 last_sent)
{
	if (!n_ptr->bclink.supported ||
	    less_eq(last_sent, mod(n_ptr->bclink.last_in)))
		return;

	bclink_set_gap(n_ptr);
	if (n_ptr->bclink.gap_after == n_ptr->bclink.gap_to)
		n_ptr->bclink.gap_to = last_sent;
	bclink_send_nack(n_ptr);
}

/**
 * gipc_bclink_peek_nack - process a NACK msg meant for another node
 *
 * Only gipc_net_lock set.
 */

static void gipc_bclink_peek_nack(u32 dest, u32 sender_tag, u32 gap_after, u32 gap_to)
{
	struct gipc_node *n_ptr = gipc_node_find(dest);
	u32 my_after, my_to;

	if (unlikely(!n_ptr || !gipc_node_is_up(n_ptr)))
		return;
	gipc_node_lock(n_ptr);
	/*
	 * Modify gap to suppress unnecessary NACKs from this node
	 */
	my_after = n_ptr->bclink.gap_after;
	my_to = n_ptr->bclink.gap_to;

	if (less_eq(gap_after, my_after)) {
		if (less(my_after, gap_to) && less(gap_to, my_to))
			n_ptr->bclink.gap_after = gap_to;
		else if (less_eq(my_to, gap_to))
			n_ptr->bclink.gap_to = n_ptr->bclink.gap_after;
	} else if (less_eq(gap_after, my_to)) {
		if (less_eq(my_to, gap_to))
			n_ptr->bclink.gap_to = gap_after;
	} else {
		/*
		 * Expand gap if missing bufs not in deferred queue:
		 */
		struct sk_buff *buf = n_ptr->bclink.deferred_head;
		u32 prev = n_ptr->bclink.gap_to;

		for (; buf; buf = buf->next) {
			u32 seqno = buf_seqno(buf);

			if (mod(seqno - prev) != 1) {
				buf = NULL;
				break;
			}
			if (seqno == gap_after)
				break;
			prev = seqno;
		}
		if (buf == NULL)
			n_ptr->bclink.gap_to = gap_after;
	}
	/*
	 * Some nodes may send a complementary NACK now:
	 */
	if (bclink_ack_allowed(sender_tag + 1)) {
		if (n_ptr->bclink.gap_to != n_ptr->bclink.gap_after) {
			bclink_send_nack(n_ptr);
			bclink_set_gap(n_ptr);
		}
	}
	gipc_node_unlock(n_ptr);
}

/**
 * gipc_bclink_send_msg - broadcast a packet to all nodes in cluster
 */

int gipc_bclink_send_msg(struct sk_buff *buf)
{
	int res;

	spin_lock_bh(&bc_lock);

	res = gipc_link_send_buf(bcl, buf);
	if (unlikely(res == -ELINKCONG))
		buf_discard(buf);
	else
		bcl->stats.sent_info++;

	if (bcl->out_queue_size > bcl->stats.max_queue_sz)
		bcl->stats.max_queue_sz = bcl->out_queue_size;
	bcl->stats.queue_sz_counts++;
	bcl->stats.accu_queue_sz += bcl->out_queue_size;

	spin_unlock_bh(&bc_lock);
	return res;
}

/**
 * gipc_bclink_recv_pkt - receive a broadcast packet, and deliver upwards
 *
 * gipc_net_lock is read_locked, no other locks set
 */

void gipc_bclink_recv_pkt(struct sk_buff *buf)
{
#if (GIPC_BCAST_LOSS_RATE)
	static int rx_count = 0;
#endif
	struct gipc_msg *msg = buf_msg(buf);
	struct gipc_node* node = gipc_node_find(msg_prevnode(msg));
	u32 next_in;
	u32 seqno;
	struct sk_buff *deferred;

	msg_dbg(msg, "<BC<<<");

	if (unlikely(!node || !gipc_node_is_up(node) || !node->bclink.supported ||
		     (msg_mc_netid(msg) != gipc_net_id))) {
		buf_discard(buf);
		return;
	}

	if (unlikely(msg_user(msg) == BCAST_PROTOCOL)) {
		msg_dbg(msg, "<BCNACK<<<");
		if (msg_destnode(msg) == gipc_own_addr) {
			gipc_node_lock(node);
			gipc_bclink_acknowledge(node, msg_bcast_ack(msg));
			gipc_node_unlock(node);
			spin_lock_bh(&bc_lock);
			bcl->stats.recv_nacks++;
			bcl->owner->next = node;   /* remember requestor */
			bclink_retransmit_pkt(msg_bcgap_after(msg),
					      msg_bcgap_to(msg));
			bcl->owner->next = NULL;
			spin_unlock_bh(&bc_lock);
		} else {
			gipc_bclink_peek_nack(msg_destnode(msg),
					      msg_bcast_tag(msg),
					      msg_bcgap_after(msg),
					      msg_bcgap_to(msg));
		}
		buf_discard(buf);
		return;
	}

#if (GIPC_BCAST_LOSS_RATE)
	if (++rx_count == GIPC_BCAST_LOSS_RATE) {
		rx_count = 0;
		buf_discard(buf);
		return;
	}
#endif

	gipc_node_lock(node);
receive:
	deferred = node->bclink.deferred_head;
	next_in = mod(node->bclink.last_in + 1);
	seqno = msg_seqno(msg);

	if (likely(seqno == next_in)) {
		bcl->stats.recv_info++;
		node->bclink.last_in++;
		bclink_set_gap(node);
		if (unlikely(bclink_ack_allowed(seqno))) {
			bclink_send_ack(node);
			bcl->stats.sent_acks++;
		}
		if (likely(msg_isdata(msg))) {
			gipc_node_unlock(node);
			gipc_port_recv_mcast(buf, NULL);
		} else if (msg_user(msg) == MSG_BUNDLER) {
			bcl->stats.recv_bundles++;
			bcl->stats.recv_bundled += msg_msgcnt(msg);
			gipc_node_unlock(node);
			gipc_link_recv_bundle(buf);
		} else if (msg_user(msg) == MSG_FRAGMENTER) {
			bcl->stats.recv_fragments++;
			if (gipc_link_recv_fragment(&node->bclink.defragm,
						    &buf, &msg))
				bcl->stats.recv_fragmented++;
			gipc_node_unlock(node);
			gipc_net_route_msg(buf);
		} else {
			gipc_node_unlock(node);
			gipc_net_route_msg(buf);
		}
		if (deferred && (buf_seqno(deferred) == mod(next_in + 1))) {
			gipc_node_lock(node);
			buf = deferred;
			msg = buf_msg(buf);
			node->bclink.deferred_head = deferred->next;
			goto receive;
		}
		return;
	} else if (less(next_in, seqno)) {
		u32 gap_after = node->bclink.gap_after;
		u32 gap_to = node->bclink.gap_to;

		if (gipc_link_defer_pkt(&node->bclink.deferred_head,
					&node->bclink.deferred_tail,
					buf)) {
			node->bclink.nack_sync++;
			bcl->stats.deferred_recv++;
			if (seqno == mod(gap_after + 1))
				node->bclink.gap_after = seqno;
			else if (less(gap_after, seqno) && less(seqno, gap_to))
				node->bclink.gap_to = seqno;
		}
		if (bclink_ack_allowed(node->bclink.nack_sync)) {
			if (gap_to != gap_after)
				bclink_send_nack(node);
			bclink_set_gap(node);
		}
	} else {
		bcl->stats.duplicates++;
		buf_discard(buf);
	}
	gipc_node_unlock(node);
}

u32 gipc_bclink_get_last_sent(void)
{
	u32 last_sent = mod(bcl->next_out_no - 1);

	if (bcl->next_out)
		last_sent = mod(buf_seqno(bcl->next_out) - 1);
	return last_sent;
}

u32 gipc_bclink_acks_missing(struct gipc_node *n_ptr)
{
	return (n_ptr->bclink.supported &&
		(gipc_bclink_get_last_sent() != n_ptr->bclink.acked));
}


/**
 * gipc_bcbearer_send - send a packet through the broadcast pseudo-bearer
 *
 * Send through as many bearers as necessary to reach all nodes
 * that support GIPC multicasting.
 *
 * Returns 0 if packet sent successfully, non-zero if not
 */

static int gipc_bcbearer_send(struct sk_buff *buf,
			      struct gipc_bearer *unused1,
			      struct gipc_media_addr *unused2)
{
	static int send_count = 0;

	int bp_index;
	int swap_time;

	/* Prepare buffer for broadcasting (if first time trying to send it) */

	if (likely(!msg_non_seq(buf_msg(buf)))) {
		struct gipc_msg *msg;

		assert(gipc_cltr_bcast_nodes.count != 0);
		bcbuf_set_acks(buf, gipc_cltr_bcast_nodes.count);
		msg = buf_msg(buf);
		msg_set_non_seq(msg, 1);
		msg_set_mc_netid(msg, gipc_net_id);
	}

	/* Determine if bearer pairs should be swapped following this attempt */

	if ((swap_time = (++send_count >= 10)))
		send_count = 0;

	/* Send buffer over bearers until all targets reached */

	bcbearer->remains = gipc_cltr_bcast_nodes;

	for (bp_index = 0; bp_index < MAX_BEARERS; bp_index++) {
		struct bearer *p = bcbearer->bpairs[bp_index].primary;
		struct bearer *s = bcbearer->bpairs[bp_index].secondary;

		if (!p)
			break;	/* no more bearers to try */

		gipc_nmap_diff(&bcbearer->remains, &p->nodes, &bcbearer->remains_new);
		if (bcbearer->remains_new.count == bcbearer->remains.count)
			continue;	/* bearer pair doesn't add anything */

		if (!p->publ.blocked &&
		    !p->media->send_msg(buf, &p->publ, &p->media->bcast_addr)) {
			if (swap_time && s && !s->publ.blocked)
				goto swap;
			else
				goto update;
		}

		if (!s || s->publ.blocked ||
		    s->media->send_msg(buf, &s->publ, &s->media->bcast_addr))
			continue;	/* unable to send using bearer pair */
swap:
		bcbearer->bpairs[bp_index].primary = s;
		bcbearer->bpairs[bp_index].secondary = p;
update:
		if (bcbearer->remains_new.count == 0)
			return 0;

		bcbearer->remains = bcbearer->remains_new;
	}

	/* Unable to reach all targets */

	bcbearer->bearer.publ.blocked = 1;
	bcl->stats.bearer_congs++;
	return 1;
}

/**
 * gipc_bcbearer_sort - create sets of bearer pairs used by broadcast bearer
 */

void gipc_bcbearer_sort(void)
{
	struct bcbearer_pair *bp_temp = bcbearer->bpairs_temp;
	struct bcbearer_pair *bp_curr;
	int b_index;
	int pri;

	spin_lock_bh(&bc_lock);

	/* Group bearers by priority (can assume max of two per priority) */

	memset(bp_temp, 0, sizeof(bcbearer->bpairs_temp));

	for (b_index = 0; b_index < MAX_BEARERS; b_index++) {
		struct bearer *b = &gipc_bearers[b_index];

		if (!b->active || !b->nodes.count)
			continue;

		if (!bp_temp[b->priority].primary)
			bp_temp[b->priority].primary = b;
		else
			bp_temp[b->priority].secondary = b;
	}

	/* Create array of bearer pairs for broadcasting */

	bp_curr = bcbearer->bpairs;
	memset(bcbearer->bpairs, 0, sizeof(bcbearer->bpairs));

	for (pri = GIPC_MAX_LINK_PRI; pri >= 0; pri--) {

		if (!bp_temp[pri].primary)
			continue;

		bp_curr->primary = bp_temp[pri].primary;

		if (bp_temp[pri].secondary) {
			if (gipc_nmap_equal(&bp_temp[pri].primary->nodes,
					    &bp_temp[pri].secondary->nodes)) {
				bp_curr->secondary = bp_temp[pri].secondary;
			} else {
				bp_curr++;
				bp_curr->primary = bp_temp[pri].secondary;
			}
		}

		bp_curr++;
	}

	spin_unlock_bh(&bc_lock);
}

/**
 * gipc_bcbearer_push - resolve bearer congestion
 *
 * Forces bclink to push out any unsent packets, until all packets are gone
 * or congestion reoccurs.
 * No locks set when function called
 */

void gipc_bcbearer_push(void)
{
	struct bearer *b_ptr;

	spin_lock_bh(&bc_lock);
	b_ptr = &bcbearer->bearer;
	if (b_ptr->publ.blocked) {
		b_ptr->publ.blocked = 0;
		gipc_bearer_lock_push(b_ptr);
	}
	spin_unlock_bh(&bc_lock);
}


int gipc_bclink_stats(char *buf, const u32 buf_size)
{
	struct print_buf pb;

	if (!bcl)
		return 0;

	gipc_printbuf_init(&pb, buf, buf_size);

	spin_lock_bh(&bc_lock);

	gipc_printf(&pb, "Link <%s>\n"
			 "  Window:%u packets\n",
		    bcl->name, bcl->queue_limit[0]);
	gipc_printf(&pb, "  RX packets:%u fragments:%u/%u bundles:%u/%u\n",
		    bcl->stats.recv_info,
		    bcl->stats.recv_fragments,
		    bcl->stats.recv_fragmented,
		    bcl->stats.recv_bundles,
		    bcl->stats.recv_bundled);
	gipc_printf(&pb, "  TX packets:%u fragments:%u/%u bundles:%u/%u\n",
		    bcl->stats.sent_info,
		    bcl->stats.sent_fragments,
		    bcl->stats.sent_fragmented,
		    bcl->stats.sent_bundles,
		    bcl->stats.sent_bundled);
	gipc_printf(&pb, "  RX naks:%u defs:%u dups:%u\n",
		    bcl->stats.recv_nacks,
		    bcl->stats.deferred_recv,
		    bcl->stats.duplicates);
	gipc_printf(&pb, "  TX naks:%u acks:%u dups:%u\n",
		    bcl->stats.sent_nacks,
		    bcl->stats.sent_acks,
		    bcl->stats.retransmitted);
	gipc_printf(&pb, "  Congestion bearer:%u link:%u  Send queue max:%u avg:%u\n",
		    bcl->stats.bearer_congs,
		    bcl->stats.link_congs,
		    bcl->stats.max_queue_sz,
		    bcl->stats.queue_sz_counts
		    ? (bcl->stats.accu_queue_sz / bcl->stats.queue_sz_counts)
		    : 0);

	spin_unlock_bh(&bc_lock);
	return gipc_printbuf_validate(&pb);
}

int gipc_bclink_reset_stats(void)
{
	if (!bcl)
		return -ENOPROTOOPT;

	spin_lock_bh(&bc_lock);
	memset(&bcl->stats, 0, sizeof(bcl->stats));
	spin_unlock_bh(&bc_lock);
	return 0;
}

int gipc_bclink_set_queue_limits(u32 limit)
{
	if (!bcl)
		return -ENOPROTOOPT;
	if ((limit < GIPC_MIN_LINK_WIN) || (limit > GIPC_MAX_LINK_WIN))
		return -EINVAL;

	spin_lock_bh(&bc_lock);
	gipc_link_set_queue_limits(bcl, limit);
	spin_unlock_bh(&bc_lock);
	return 0;
}

int gipc_bclink_init(void)
{
	bcbearer = kzalloc(sizeof(*bcbearer), GFP_ATOMIC);
	bclink = kzalloc(sizeof(*bclink), GFP_ATOMIC);
	if (!bcbearer || !bclink) {
 nomem:
		warn("Multicast link creation failed, no memory\n");
		kfree(bcbearer);
		bcbearer = NULL;
		kfree(bclink);
		bclink = NULL;
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&bcbearer->bearer.cong_links);
	bcbearer->bearer.media = &bcbearer->media;
	bcbearer->media.send_msg = gipc_bcbearer_send;
	sprintf(bcbearer->media.name, "gipc-multicast");

	bcl = &bclink->link;
	INIT_LIST_HEAD(&bcl->waiting_ports);
	bcl->next_out_no = 1;
	spin_lock_init(&bclink->node.lock);
	bcl->owner = &bclink->node;
	bcl->max_pkt = MAX_PKT_DEFAULT_MCAST;
	gipc_link_set_queue_limits(bcl, BCLINK_WIN_DEFAULT);
	bcl->b_ptr = &bcbearer->bearer;
	bcl->state = WORKING_WORKING;
	strlcpy(bcl->name, gipc_bclink_name, GIPC_MAX_LINK_NAME);

	if (BCLINK_LOG_BUF_SIZE) {
		char *pb = kmalloc(BCLINK_LOG_BUF_SIZE, GFP_ATOMIC);

		if (!pb)
			goto nomem;
		gipc_printbuf_init(&bcl->print_buf, pb, BCLINK_LOG_BUF_SIZE);
	}

	return 0;
}

void gipc_bclink_stop(void)
{
	spin_lock_bh(&bc_lock);
	if (bcbearer) {
		gipc_link_stop(bcl);
		if (BCLINK_LOG_BUF_SIZE)
			kfree(bcl->print_buf.buf);
		bcl = NULL;
		kfree(bclink);
		bclink = NULL;
		kfree(bcbearer);
		bcbearer = NULL;
	}
	spin_unlock_bh(&bc_lock);
}

