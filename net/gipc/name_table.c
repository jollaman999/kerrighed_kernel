/*
 * net/gipc/name_table.c: GIPC name table code
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2004-2008, Wind River Systems
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
#include "config.h"
#include "dbg.h"
#include "name_table.h"
#include "name_distr.h"
#include "addr.h"
#include "node_subscr.h"
#include "subscr.h"
#include "port.h"
#include "cluster.h"
#include "bcast.h"

static int gipc_nametbl_size = 1024;		/* must be a power of 2 */

/**
 * struct sub_seq - container for all published instances of a name sequence
 * @lower: name sequence lower bound
 * @upper: name sequence upper bound
 * @node_list: circular list of publications made by own node
 * @cluster_list: circular list of publications made by own cluster
 * @zone_list: circular list of publications made by own zone
 * @node_list_size: number of entries in "node_list"
 * @cluster_list_size: number of entries in "cluster_list"
 * @zone_list_size: number of entries in "zone_list"
 *
 * Note: The zone list always contains at least one entry, since all
 *       publications of the associated name sequence belong to it.
 *       (The cluster and node lists may be empty.)
 */

struct sub_seq {
	u32 lower;
	u32 upper;
	struct publication *node_list;
	struct publication *cluster_list;
	struct publication *zone_list;
	u32 node_list_size;
	u32 cluster_list_size;
	u32 zone_list_size;
};

/**
 * struct name_seq - container for all published instances of a name type
 * @type: 32 bit 'type' value for name sequence
 * @sseq: pointer to dynamically-sized array of sub-sequences of this 'type';
 *        sub-sequences are sorted in ascending order
 * @alloc: number of sub-sequences currently in array
 * @first_free: array index of first unused sub-sequence entry
 * @ns_list: links to adjacent name sequences in hash chain
 * @subscriptions: list of subscriptions for this 'type'
 * @lock: spinlock controlling access to publication lists of all sub-sequences
 */

struct name_seq {
	u32 type;
	struct sub_seq *sseqs;
	u32 alloc;
	u32 first_free;
	struct hlist_node ns_list;
	struct list_head subscriptions;
	spinlock_t lock;
};

/**
 * struct name_table - table containing all existing port name publications
 * @types: pointer to fixed-sized array of name sequence lists,
 *         accessed via hashing on 'type'; name sequence lists are *not* sorted
 * @local_publ_count: number of publications issued by this node
 */

struct name_table {
	struct hlist_head *types;
	u32 local_publ_count;
};

static struct name_table table = { NULL } ;
static atomic_t rsv_publ_ok = ATOMIC_INIT(0);
DEFINE_RWLOCK(gipc_nametbl_lock);


static int hash(int x)
{
	return(x & (gipc_nametbl_size - 1));
}

/**
 * publ_create - create a publication structure
 */

static struct publication *publ_create(u32 type, u32 lower, u32 upper,
				       u32 scope, u32 node, u32 port_ref,
				       u32 key)
{
	struct publication *publ = kzalloc(sizeof(*publ), GFP_ATOMIC);
	if (publ == NULL) {
		warn("Publication creation failure, no memory\n");
		return NULL;
	}

	publ->type = type;
	publ->lower = lower;
	publ->upper = upper;
	publ->scope = scope;
	publ->node = node;
	publ->ref = port_ref;
	publ->key = key;
	INIT_LIST_HEAD(&publ->local_list);
	INIT_LIST_HEAD(&publ->pport_list);
	INIT_LIST_HEAD(&publ->subscr.nodesub_list);
	return publ;
}

/**
 * gipc_subseq_alloc - allocate a specified number of sub-sequence structures
 */

static struct sub_seq *gipc_subseq_alloc(u32 cnt)
{
	struct sub_seq *sseq = kcalloc(cnt, sizeof(struct sub_seq), GFP_ATOMIC);
	return sseq;
}

/**
 * gipc_nameseq_create - create a name sequence structure for the specified 'type'
 *
 * Allocates a single sub-sequence structure and sets it to all 0's.
 */

static struct name_seq *gipc_nameseq_create(u32 type, struct hlist_head *seq_head)
{
	struct name_seq *nseq = kzalloc(sizeof(*nseq), GFP_ATOMIC);
	struct sub_seq *sseq = gipc_subseq_alloc(1);

	if (!nseq || !sseq) {
		warn("Name sequence creation failed, no memory\n");
		kfree(nseq);
		kfree(sseq);
		return NULL;
	}

	spin_lock_init(&nseq->lock);
	nseq->type = type;
	nseq->sseqs = sseq;
	dbg("gipc_nameseq_create(): nseq = %p, type %u, ssseqs %p, ff: %u\n",
	    nseq, type, nseq->sseqs, nseq->first_free);
	nseq->alloc = 1;
	INIT_HLIST_NODE(&nseq->ns_list);
	INIT_LIST_HEAD(&nseq->subscriptions);
	hlist_add_head(&nseq->ns_list, seq_head);
	return nseq;
}

/**
 * nameseq_find_subseq - find sub-sequence (if any) matching a name instance
 *
 * Very time-critical, so binary searches through sub-sequence array.
 */

static struct sub_seq *nameseq_find_subseq(struct name_seq *nseq,
					   u32 instance)
{
	struct sub_seq *sseqs = nseq->sseqs;
	int low = 0;
	int high = nseq->first_free - 1;
	int mid;

	while (low <= high) {
		mid = (low + high) / 2;
		if (instance < sseqs[mid].lower)
			high = mid - 1;
		else if (instance > sseqs[mid].upper)
			low = mid + 1;
		else
			return &sseqs[mid];
	}
	return NULL;
}

/**
 * nameseq_locate_subseq - determine position of name instance in sub-sequence
 *
 * Returns index in sub-sequence array of the entry that contains the specified
 * instance value; if no entry contains that value, returns the position
 * where a new entry for it would be inserted in the array.
 *
 * Note: Similar to binary search code for locating a sub-sequence.
 */

static u32 nameseq_locate_subseq(struct name_seq *nseq, u32 instance)
{
	struct sub_seq *sseqs = nseq->sseqs;
	int low = 0;
	int high = nseq->first_free - 1;
	int mid;

	while (low <= high) {
		mid = (low + high) / 2;
		if (instance < sseqs[mid].lower)
			high = mid - 1;
		else if (instance > sseqs[mid].upper)
			low = mid + 1;
		else
			return mid;
	}
	return low;
}

/**
 * gipc_nameseq_insert_publ -
 */

static struct publication *gipc_nameseq_insert_publ(struct name_seq *nseq,
						    u32 type, u32 lower, u32 upper,
						    u32 scope, u32 node, u32 port, u32 key)
{
	struct subscription *s;
	struct subscription *st;
	struct publication *publ;
	struct sub_seq *sseq;
	int created_subseq = 0;

	sseq = nameseq_find_subseq(nseq, lower);
	dbg("nameseq_ins: for seq %p, {%u,%u}, found sseq %p\n",
	    nseq, type, lower, sseq);
	if (sseq) {

		/* Lower end overlaps existing entry => need an exact match */

		if ((sseq->lower != lower) || (sseq->upper != upper)) {
			warn("Cannot publish {%u,%u,%u}, overlap error\n",
			     type, lower, upper);
			return NULL;
		}
	} else {
		u32 inspos;
		struct sub_seq *freesseq;

		/* Find where lower end should be inserted */

		inspos = nameseq_locate_subseq(nseq, lower);

		/* Fail if upper end overlaps into an existing entry */

		if ((inspos < nseq->first_free) &&
		    (upper >= nseq->sseqs[inspos].lower)) {
			warn("Cannot publish {%u,%u,%u}, overlap error\n",
			     type, lower, upper);
			return NULL;
		}

		/* Ensure there is space for new sub-sequence */

		if (nseq->first_free == nseq->alloc) {
			struct sub_seq *sseqs = gipc_subseq_alloc(nseq->alloc * 2);

			if (!sseqs) {
				warn("Cannot publish {%u,%u,%u}, no memory\n",
				     type, lower, upper);
				return NULL;
			}
			dbg("Allocated %u more sseqs\n", nseq->alloc);
			memcpy(sseqs, nseq->sseqs,
			       nseq->alloc * sizeof(struct sub_seq));
			kfree(nseq->sseqs);
			nseq->sseqs = sseqs;
			nseq->alloc *= 2;
		}
		dbg("Have %u sseqs for type %u\n", nseq->alloc, type);

		/* Insert new sub-sequence */

		dbg("ins in pos %u, ff = %u\n", inspos, nseq->first_free);
		sseq = &nseq->sseqs[inspos];
		freesseq = &nseq->sseqs[nseq->first_free];
		memmove(sseq + 1, sseq, (freesseq - sseq) * sizeof (*sseq));
		memset(sseq, 0, sizeof (*sseq));
		nseq->first_free++;
		sseq->lower = lower;
		sseq->upper = upper;
		created_subseq = 1;
	}
	dbg("inserting {%u,%u,%u} from <0x%x:%u> into sseq %p(%u,%u) of seq %p\n",
	    type, lower, upper, node, port, sseq,
	    sseq->lower, sseq->upper, nseq);

	/* Insert a publication: */

	publ = publ_create(type, lower, upper, scope, node, port, key);
	if (!publ)
		return NULL;
	dbg("inserting publ %p, node=0x%x publ->node=0x%x, subscr->node=%p\n",
	    publ, node, publ->node, publ->subscr.node);

	sseq->zone_list_size++;
	if (!sseq->zone_list)
		sseq->zone_list = publ->zone_list_next = publ;
	else {
		publ->zone_list_next = sseq->zone_list->zone_list_next;
		sseq->zone_list->zone_list_next = publ;
	}

	if (in_own_cluster(node)) {
		sseq->cluster_list_size++;
		if (!sseq->cluster_list)
			sseq->cluster_list = publ->cluster_list_next = publ;
		else {
			publ->cluster_list_next =
			sseq->cluster_list->cluster_list_next;
			sseq->cluster_list->cluster_list_next = publ;
		}
	}

	if (node == gipc_own_addr) {
		sseq->node_list_size++;
		if (!sseq->node_list)
			sseq->node_list = publ->node_list_next = publ;
		else {
			publ->node_list_next = sseq->node_list->node_list_next;
			sseq->node_list->node_list_next = publ;
		}
	}

	/*
	 * Any subscriptions waiting for notification?
	 */
	list_for_each_entry_safe(s, st, &nseq->subscriptions, nameseq_list) {
		dbg("calling report_overlap()\n");
		gipc_subscr_report_overlap(s,
					   publ->lower,
					   publ->upper,
					   GIPC_PUBLISHED,
					   publ->ref,
					   publ->node,
					   created_subseq);
	}
	return publ;
}

/**
 * gipc_nameseq_remove_publ -
 *
 * NOTE: There may be cases where GIPC is asked to remove a publication
 * that is not in the name table.  For example, if another node issues a
 * publication for a name sequence that overlaps an existing name sequence
 * the publication will not be recorded, which means the publication won't
 * be found when the name sequence is later withdrawn by that node.
 * A failed withdraw request simply returns a failure indication and lets the
 * caller issue any error or warning messages associated with such a problem.
 */

static struct publication *gipc_nameseq_remove_publ(struct name_seq *nseq, u32 inst,
						    u32 node, u32 ref, u32 key)
{
	struct publication *publ;
	struct publication *curr;
	struct publication *prev;
	struct sub_seq *sseq = nameseq_find_subseq(nseq, inst);
	struct sub_seq *free;
	struct subscription *s, *st;
	int removed_subseq = 0;

	if (!sseq)
		return NULL;

	dbg("gipc_nameseq_remove_publ: seq: %p, sseq %p, {%u,%u}, key %u\n",
	    nseq, sseq, nseq->type, inst, key);

	/* Remove publication from zone scope list */

	prev = sseq->zone_list;
	publ = sseq->zone_list->zone_list_next;
	while ((publ->key != key) || (publ->ref != ref) ||
	       (publ->node && (publ->node != node))) {
		prev = publ;
		publ = publ->zone_list_next;
		if (prev == sseq->zone_list) {

			/* Prevent endless loop if publication not found */

			return NULL;
		}
	}
	if (publ != sseq->zone_list)
		prev->zone_list_next = publ->zone_list_next;
	else if (publ->zone_list_next != publ) {
		prev->zone_list_next = publ->zone_list_next;
		sseq->zone_list = publ->zone_list_next;
	} else {
		sseq->zone_list = NULL;
	}
	sseq->zone_list_size--;

	/* Remove publication from cluster scope list, if present */

	if (in_own_cluster(node)) {
		prev = sseq->cluster_list;
		curr = sseq->cluster_list->cluster_list_next;
		while (curr != publ) {
			prev = curr;
			curr = curr->cluster_list_next;
			if (prev == sseq->cluster_list) {

				/* Prevent endless loop for malformed list */

				err("Unable to de-list cluster publication\n"
				    "{%u%u}, node=0x%x, ref=%u, key=%u)\n",
				    publ->type, publ->lower, publ->node,
				    publ->ref, publ->key);
				goto end_cluster;
			}
		}
		if (publ != sseq->cluster_list)
			prev->cluster_list_next = publ->cluster_list_next;
		else if (publ->cluster_list_next != publ) {
			prev->cluster_list_next = publ->cluster_list_next;
			sseq->cluster_list = publ->cluster_list_next;
		} else {
			sseq->cluster_list = NULL;
		}
		sseq->cluster_list_size--;
	}
end_cluster:

	/* Remove publication from node scope list, if present */

	if (node == gipc_own_addr) {
		prev = sseq->node_list;
		curr = sseq->node_list->node_list_next;
		while (curr != publ) {
			prev = curr;
			curr = curr->node_list_next;
			if (prev == sseq->node_list) {

				/* Prevent endless loop for malformed list */

				err("Unable to de-list node publication\n"
				    "{%u%u}, node=0x%x, ref=%u, key=%u)\n",
				    publ->type, publ->lower, publ->node,
				    publ->ref, publ->key);
				goto end_node;
			}
		}
		if (publ != sseq->node_list)
			prev->node_list_next = publ->node_list_next;
		else if (publ->node_list_next != publ) {
			prev->node_list_next = publ->node_list_next;
			sseq->node_list = publ->node_list_next;
		} else {
			sseq->node_list = NULL;
		}
		sseq->node_list_size--;
	}
end_node:

	/* Contract subseq list if no more publications for that subseq */

	if (!sseq->zone_list) {
		free = &nseq->sseqs[nseq->first_free--];
		memmove(sseq, sseq + 1, (free - (sseq + 1)) * sizeof (*sseq));
		removed_subseq = 1;
	}

	/* Notify any waiting subscriptions */

	list_for_each_entry_safe(s, st, &nseq->subscriptions, nameseq_list) {
		gipc_subscr_report_overlap(s,
					   publ->lower,
					   publ->upper,
					   GIPC_WITHDRAWN,
					   publ->ref,
					   publ->node,
					   removed_subseq);
	}

	return publ;
}

/**
 * gipc_nameseq_subscribe: attach a subscription, and issue
 * the prescribed number of events if there is any sub-
 * sequence overlapping with the requested sequence
 */

static void gipc_nameseq_subscribe(struct name_seq *nseq, struct subscription *s)
{
	struct sub_seq *sseq = nseq->sseqs;

	list_add(&s->nameseq_list, &nseq->subscriptions);

	if (!sseq)
		return;

	while (sseq != &nseq->sseqs[nseq->first_free]) {
		struct publication *zl = sseq->zone_list;
		if (zl && gipc_subscr_overlap(s,sseq->lower,sseq->upper)) {
			struct publication *crs = zl;
			int must_report = 1;

			do {
				gipc_subscr_report_overlap(s,
							   sseq->lower,
							   sseq->upper,
							   GIPC_PUBLISHED,
							   crs->ref,
							   crs->node,
							   must_report);
				must_report = 0;
				crs = crs->zone_list_next;
			} while (crs != zl);
		}
		sseq++;
	}
}

static struct name_seq *nametbl_find_seq(u32 type)
{
	struct hlist_head *seq_head;
	struct hlist_node *seq_node;
	struct name_seq *ns;

	dbg("find_seq %u,(%u,0x%x) table = %p, hash[type] = %u\n",
	    type, htonl(type), type, table.types, hash(type));

	seq_head = &table.types[hash(type)];
	hlist_for_each_entry(ns, seq_node, seq_head, ns_list) {
		if (ns->type == type) {
			dbg("found %p\n", ns);
			return ns;
		}
	}

	return NULL;
};

struct publication *gipc_nametbl_insert_publ(u32 type, u32 lower, u32 upper,
					     u32 scope, u32 node, u32 port, u32 key)
{
	struct name_seq *seq = nametbl_find_seq(type);

	dbg("gipc_nametbl_insert_publ: {%u,%u,%u} found %p\n", type, lower, upper, seq);
	if (lower > upper) {
		warn("Failed to publish illegal {%u,%u,%u}\n",
		     type, lower, upper);
		return NULL;
	}

	dbg("Publishing {%u,%u,%u} from 0x%x\n", type, lower, upper, node);
	if (!seq) {
		seq = gipc_nameseq_create(type, &table.types[hash(type)]);
		dbg("gipc_nametbl_insert_publ: created %p\n", seq);
	}
	if (!seq)
		return NULL;

	return gipc_nameseq_insert_publ(seq, type, lower, upper,
					scope, node, port, key);
}

struct publication *gipc_nametbl_remove_publ(u32 type, u32 lower,
					     u32 node, u32 ref, u32 key)
{
	struct publication *publ;
	struct name_seq *seq = nametbl_find_seq(type);

	if (!seq)
		return NULL;

	dbg("Withdrawing {%u,%u} from 0x%x\n", type, lower, node);
	publ = gipc_nameseq_remove_publ(seq, lower, node, ref, key);

	if (!seq->first_free && list_empty(&seq->subscriptions)) {
		hlist_del_init(&seq->ns_list);
		kfree(seq->sseqs);
		kfree(seq);
	}
	return publ;
}

/*
 * gipc_nametbl_translate(): Translate gipc_name -> gipc_portid.
 *                      Very time-critical.
 *
 * Note: on entry 'destnode' is the search domain used during translation;
 *       on exit it passes back the node address of the matching port (if any)
 */

u32 gipc_nametbl_translate(u32 type, u32 instance, u32 *destnode)
{
	struct sub_seq *sseq;
	struct publication *publ = NULL;
	struct name_seq *seq;
	u32 ref;

	if (!in_scope(*destnode, gipc_own_addr))
		return 0;

	read_lock_bh(&gipc_nametbl_lock);
	seq = nametbl_find_seq(type);
	if (unlikely(!seq))
		goto not_found;
	sseq = nameseq_find_subseq(seq, instance);
	if (unlikely(!sseq))
		goto not_found;
	spin_lock_bh(&seq->lock);

	/* Closest-First Algorithm: */
	if (likely(!*destnode)) {
		publ = sseq->node_list;
		if (publ) {
			sseq->node_list = publ->node_list_next;
found:
			ref = publ->ref;
			*destnode = publ->node;
			spin_unlock_bh(&seq->lock);
			read_unlock_bh(&gipc_nametbl_lock);
			return ref;
		}
		publ = sseq->cluster_list;
		if (publ) {
			sseq->cluster_list = publ->cluster_list_next;
			goto found;
		}
		publ = sseq->zone_list;
		if (publ) {
			sseq->zone_list = publ->zone_list_next;
			goto found;
		}
	}

	/* Round-Robin Algorithm: */
	else if (*destnode == gipc_own_addr) {
		publ = sseq->node_list;
		if (publ) {
			sseq->node_list = publ->node_list_next;
			goto found;
		}
	} else if (in_own_cluster(*destnode)) {
		publ = sseq->cluster_list;
		if (publ) {
			sseq->cluster_list = publ->cluster_list_next;
			goto found;
		}
	} else {
		publ = sseq->zone_list;
		if (publ) {
			sseq->zone_list = publ->zone_list_next;
			goto found;
		}
	}
	spin_unlock_bh(&seq->lock);
not_found:
	*destnode = 0;
	read_unlock_bh(&gipc_nametbl_lock);
	return 0;
}

/**
 * gipc_nametbl_mc_translate - find multicast destinations
 *
 * Creates list of all local ports that overlap the given multicast address;
 * also determines if any off-node ports overlap.
 *
 * Note: Publications with a scope narrower than 'limit' are ignored.
 * (i.e. local node-scope publications mustn't receive messages arriving
 * from another node, even if the multcast link brought it here)
 *
 * Returns non-zero if any off-node ports overlap
 */

int gipc_nametbl_mc_translate(u32 type, u32 lower, u32 upper, u32 limit,
			      struct port_list *dports)
{
	struct name_seq *seq;
	struct sub_seq *sseq;
	struct sub_seq *sseq_stop;
	int res = 0;

	read_lock_bh(&gipc_nametbl_lock);
	seq = nametbl_find_seq(type);
	if (!seq)
		goto exit;

	spin_lock_bh(&seq->lock);

	sseq = seq->sseqs + nameseq_locate_subseq(seq, lower);
	sseq_stop = seq->sseqs + seq->first_free;
	for (; sseq != sseq_stop; sseq++) {
		struct publication *publ;

		if (sseq->lower > upper)
			break;

		publ = sseq->node_list;
		if (publ) {
			do {
				if (publ->scope <= limit)
					gipc_port_list_add(dports, publ->ref);
				publ = publ->node_list_next;
			} while (publ != sseq->node_list);
		}

		if (sseq->cluster_list_size != sseq->node_list_size)
			res = 1;
	}

	spin_unlock_bh(&seq->lock);
exit:
	read_unlock_bh(&gipc_nametbl_lock);
	return res;
}

/**
 * gipc_nametbl_publish_rsv - publish port name using a reserved name type
 */

int gipc_nametbl_publish_rsv(u32 ref, unsigned int scope,
			struct gipc_name_seq const *seq)
{
	int res;

	atomic_inc(&rsv_publ_ok);
	res = gipc_publish(ref, scope, seq);
	atomic_dec(&rsv_publ_ok);
	return res;
}

/**
 * gipc_nametbl_publish - add name publication to network name tables
 */

struct publication *gipc_nametbl_publish(u32 type, u32 lower, u32 upper,
				    u32 scope, u32 port_ref, u32 key)
{
	struct publication *publ;

	if (table.local_publ_count >= gipc_max_publications) {
		warn("Publication failed, local publication limit reached (%u)\n",
		     gipc_max_publications);
		return NULL;
	}
	if ((type < GIPC_RESERVED_TYPES) && !atomic_read(&rsv_publ_ok)) {
		warn("Publication failed, reserved name {%u,%u,%u}\n",
		     type, lower, upper);
		return NULL;
	}

	write_lock_bh(&gipc_nametbl_lock);
	table.local_publ_count++;
	publ = gipc_nametbl_insert_publ(type, lower, upper, scope,
				   gipc_own_addr, port_ref, key);
	if (publ && (scope != GIPC_NODE_SCOPE)) {
		gipc_named_publish(publ);
	}
	write_unlock_bh(&gipc_nametbl_lock);
	return publ;
}

/**
 * gipc_nametbl_withdraw - withdraw name publication from network name tables
 */

int gipc_nametbl_withdraw(u32 type, u32 lower, u32 ref, u32 key)
{
	struct publication *publ;

	dbg("gipc_nametbl_withdraw: {%u,%u}, key=%u\n", type, lower, key);
	write_lock_bh(&gipc_nametbl_lock);
	publ = gipc_nametbl_remove_publ(type, lower, gipc_own_addr, ref, key);
	if (likely(publ)) {
		table.local_publ_count--;
		if (publ->scope != GIPC_NODE_SCOPE)
			gipc_named_withdraw(publ);
		write_unlock_bh(&gipc_nametbl_lock);
		list_del_init(&publ->pport_list);
		kfree(publ);
		return 1;
	}
	write_unlock_bh(&gipc_nametbl_lock);
	err("Unable to remove local publication\n"
	    "(type=%u, lower=%u, ref=%u, key=%u)\n",
	    type, lower, ref, key);
	return 0;
}

/**
 * gipc_nametbl_subscribe - add a subscription object to the name table
 */

void gipc_nametbl_subscribe(struct subscription *s)
{
	u32 type = s->seq.type;
	struct name_seq *seq;

	write_lock_bh(&gipc_nametbl_lock);
	seq = nametbl_find_seq(type);
	if (!seq) {
		seq = gipc_nameseq_create(type, &table.types[hash(type)]);
	}
	if (seq){
		spin_lock_bh(&seq->lock);
		dbg("gipc_nametbl_subscribe:found %p for {%u,%u,%u}\n",
		    seq, type, s->seq.lower, s->seq.upper);
		gipc_nameseq_subscribe(seq, s);
		spin_unlock_bh(&seq->lock);
	} else {
		warn("Failed to create subscription for {%u,%u,%u}\n",
		     s->seq.type, s->seq.lower, s->seq.upper);
	}
	write_unlock_bh(&gipc_nametbl_lock);
}

/**
 * gipc_nametbl_unsubscribe - remove a subscription object from name table
 */

void gipc_nametbl_unsubscribe(struct subscription *s)
{
	struct name_seq *seq;

	write_lock_bh(&gipc_nametbl_lock);
	seq = nametbl_find_seq(s->seq.type);
	if (seq != NULL){
		spin_lock_bh(&seq->lock);
		list_del_init(&s->nameseq_list);
		spin_unlock_bh(&seq->lock);
		if ((seq->first_free == 0) && list_empty(&seq->subscriptions)) {
			hlist_del_init(&seq->ns_list);
			kfree(seq->sseqs);
			kfree(seq);
		}
	}
	write_unlock_bh(&gipc_nametbl_lock);
}


/**
 * subseq_list: print specified sub-sequence contents into the given buffer
 */

static void subseq_list(struct sub_seq *sseq, struct print_buf *buf, u32 depth,
			u32 index)
{
	char portIdStr[27];
	char *scopeStr;
	struct publication *publ = sseq->zone_list;

	gipc_printf(buf, "%-10u %-10u ", sseq->lower, sseq->upper);

	if (depth == 2 || !publ) {
		gipc_printf(buf, "\n");
		return;
	}

	do {
		sprintf (portIdStr, "<%u.%u.%u:%u>",
			 gipc_zone(publ->node), gipc_cluster(publ->node),
			 gipc_node(publ->node), publ->ref);
		gipc_printf(buf, "%-26s ", portIdStr);
		if (depth > 3) {
			if (publ->node != gipc_own_addr)
				scopeStr = "";
			else if (publ->scope == GIPC_NODE_SCOPE)
				scopeStr = "node";
			else if (publ->scope == GIPC_CLUSTER_SCOPE)
				scopeStr = "cluster";
			else
				scopeStr = "zone";
			gipc_printf(buf, "%-10u %s", publ->key, scopeStr);
		}

		publ = publ->zone_list_next;
		if (publ == sseq->zone_list)
			break;

		gipc_printf(buf, "\n%33s", " ");
	} while (1);

	gipc_printf(buf, "\n");
}

/**
 * nameseq_list: print specified name sequence contents into the given buffer
 */

static void nameseq_list(struct name_seq *seq, struct print_buf *buf, u32 depth,
			 u32 type, u32 lowbound, u32 upbound, u32 index)
{
	struct sub_seq *sseq;
	char typearea[11];

	if (seq->first_free == 0)
		return;

	sprintf(typearea, "%-10u", seq->type);

	if (depth == 1) {
		gipc_printf(buf, "%s\n", typearea);
		return;
	}

	for (sseq = seq->sseqs; sseq != &seq->sseqs[seq->first_free]; sseq++) {
		if ((lowbound <= sseq->upper) && (upbound >= sseq->lower)) {
			gipc_printf(buf, "%s ", typearea);
			spin_lock_bh(&seq->lock);
			subseq_list(sseq, buf, depth, index);
			spin_unlock_bh(&seq->lock);
			sprintf(typearea, "%10s", " ");
		}
	}
}

/**
 * nametbl_header - print name table header into the given buffer
 */

static void nametbl_header(struct print_buf *buf, u32 depth)
{
	gipc_printf(buf, "Type       ");

	if (depth > 1)
		gipc_printf(buf, "Lower      Upper      ");
	if (depth > 2)
		gipc_printf(buf, "Port Identity              ");
	if (depth > 3)
		gipc_printf(buf, "Publication");

	gipc_printf(buf, "\n-----------");

	if (depth > 1)
		gipc_printf(buf, "--------------------- ");
	if (depth > 2)
		gipc_printf(buf, "-------------------------- ");
	if (depth > 3)
		gipc_printf(buf, "------------------");

	gipc_printf(buf, "\n");
}

/**
 * nametbl_list - print specified name table contents into the given buffer
 */

static void nametbl_list(struct print_buf *buf, u32 depth_info,
			 u32 type, u32 lowbound, u32 upbound)
{
	struct hlist_head *seq_head;
	struct hlist_node *seq_node;
	struct name_seq *seq;
	int all_types;
	u32 depth;
	u32 i;

	all_types = (depth_info & GIPC_NTQ_ALLTYPES);
	depth = (depth_info & ~GIPC_NTQ_ALLTYPES);

	if (depth == 0)
		return;

	if (all_types) {
		/* display all entries in name table to specified depth */
		nametbl_header(buf, depth);
		lowbound = 0;
		upbound = ~0;
		for (i = 0; i < gipc_nametbl_size; i++) {
			seq_head = &table.types[i];
			hlist_for_each_entry(seq, seq_node, seq_head, ns_list) {
				nameseq_list(seq, buf, depth, seq->type,
					     lowbound, upbound, i);
			}
		}
	} else {
		/* display only the sequence that matches the specified type */
		if (upbound < lowbound) {
			gipc_printf(buf, "invalid name sequence specified\n");
			return;
		}
		nametbl_header(buf, depth);
		i = hash(type);
		seq_head = &table.types[i];
		hlist_for_each_entry(seq, seq_node, seq_head, ns_list) {
			if (seq->type == type) {
				nameseq_list(seq, buf, depth, type,
					     lowbound, upbound, i);
				break;
			}
		}
	}
}

#if 0
void gipc_nametbl_print(struct print_buf *buf, const char *str)
{
	gipc_printf(buf, str);
	read_lock_bh(&gipc_nametbl_lock);
	nametbl_list(buf, 0, 0, 0, 0);
	read_unlock_bh(&gipc_nametbl_lock);
}
#endif

#define MAX_NAME_TBL_QUERY 32768

struct sk_buff *gipc_nametbl_get(const void *req_tlv_area, int req_tlv_space)
{
	struct sk_buff *buf;
	struct gipc_name_table_query *argv;
	struct tlv_desc *rep_tlv;
	struct print_buf b;
	int str_len;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_NAME_TBL_QUERY))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);

	buf = gipc_cfg_reply_alloc(TLV_SPACE(MAX_NAME_TBL_QUERY));
	if (!buf)
		return NULL;

	rep_tlv = (struct tlv_desc *)buf->data;
	gipc_printbuf_init(&b, TLV_DATA(rep_tlv), MAX_NAME_TBL_QUERY);
	argv = (struct gipc_name_table_query *)TLV_DATA(req_tlv_area);
	read_lock_bh(&gipc_nametbl_lock);
	nametbl_list(&b, ntohl(argv->depth), ntohl(argv->type),
		     ntohl(argv->lowbound), ntohl(argv->upbound));
	read_unlock_bh(&gipc_nametbl_lock);
	str_len = gipc_printbuf_validate(&b);

	skb_put(buf, TLV_SPACE(str_len));
	TLV_SET(rep_tlv, GIPC_TLV_ULTRA_STRING, NULL, str_len);

	return buf;
}

#if 0
void gipc_nametbl_dump(void)
{
	nametbl_list(GIPC_CONS, 0, 0, 0, 0);
}
#endif

int gipc_nametbl_init(void)
{
	table.types = kcalloc(gipc_nametbl_size, sizeof(struct hlist_head),
			      GFP_ATOMIC);
	if (!table.types)
		return -ENOMEM;

	table.local_publ_count = 0;
	return 0;
}

void gipc_nametbl_stop(void)
{
	u32 i;

	if (!table.types)
		return;

	/* Verify name table is empty, then release it */

	write_lock_bh(&gipc_nametbl_lock);
	for (i = 0; i < gipc_nametbl_size; i++) {
		if (!hlist_empty(&table.types[i]))
			err("gipc_nametbl_stop(): hash chain %u is non-null\n", i);
	}
	kfree(table.types);
	table.types = NULL;
	write_unlock_bh(&gipc_nametbl_lock);
}

