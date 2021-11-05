/*
 * net/gipc/net.c: GIPC network routing code
 *
 * Copyright (c) 1995-2006, Ericsson AB
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
#include "bearer.h"
#include "net.h"
#include "zone.h"
#include "addr.h"
#include "name_table.h"
#include "name_distr.h"
#include "subscr.h"
#include "link.h"
#include "msg.h"
#include "port.h"
#include "bcast.h"
#include "discover.h"
#include "config.h"

/*
 * The GIPC locking policy is designed to ensure a very fine locking
 * granularity, permitting complete parallel access to individual
 * port and node/link instances. The code consists of three major
 * locking domains, each protected with their own disjunct set of locks.
 *
 * 1: The routing hierarchy.
 *    Comprises the structures 'zone', 'cluster', 'node', 'link'
 *    and 'bearer'. The whole hierarchy is protected by a big
 *    read/write lock, gipc_net_lock, to enssure that nothing is added
 *    or removed while code is accessing any of these structures.
 *    This layer must not be called from the two others while they
 *    hold any of their own locks.
 *    Neither must it itself do any upcalls to the other two before
 *    it has released gipc_net_lock and other protective locks.
 *
 *   Within the gipc_net_lock domain there are two sub-domains;'node' and
 *   'bearer', where local write operations are permitted,
 *   provided that those are protected by individual spin_locks
 *   per instance. Code holding gipc_net_lock(read) and a node spin_lock
 *   is permitted to poke around in both the node itself and its
 *   subordinate links. I.e, it can update link counters and queues,
 *   change link state, send protocol messages, and alter the
 *   "active_links" array in the node; but it can _not_ remove a link
 *   or a node from the overall structure.
 *   Correspondingly, individual bearers may change status within a
 *   gipc_net_lock(read), protected by an individual spin_lock ber bearer
 *   instance, but it needs gipc_net_lock(write) to remove/add any bearers.
 *
 *
 *  2: The transport level of the protocol.
 *     This consists of the structures port, (and its user level
 *     representations, such as user_port and gipc_sock), reference and
 *     gipc_user (port.c, reg.c, socket.c).
 *
 *     This layer has four different locks:
 *     - The gipc_port spin_lock. This is protecting each port instance
 *       from parallel data access and removal. Since we can not place
 *       this lock in the port itself, it has been placed in the
 *       corresponding reference table entry, which has the same life
 *       cycle as the module. This entry is difficult to access from
 *       outside the GIPC core, however, so a pointer to the lock has
 *       been added in the port instance, -to be used for unlocking
 *       only.
 *     - A read/write lock to protect the reference table itself (teg.c).
 *       (Nobody is using read-only access to this, so it can just as
 *       well be changed to a spin_lock)
 *     - A spin lock to protect the registry of kernel/driver users (reg.c)
 *     - A global spin_lock (gipc_port_lock), which only task is to ensure
 *       consistency where more than one port is involved in an operation,
 *       i.e., whe a port is part of a linked list of ports.
 *       There are two such lists; 'port_list', which is used for management,
 *       and 'wait_list', which is used to queue ports during congestion.
 *
 *  3: The name table (name_table.c, name_distr.c, subscription.c)
 *     - There is one big read/write-lock (gipc_nametbl_lock) protecting the
 *       overall name table structure. Nothing must be added/removed to
 *       this structure without holding write access to it.
 *     - There is one local spin_lock per sub_sequence, which can be seen
 *       as a sub-domain to the gipc_nametbl_lock domain. It is used only
 *       for translation operations, and is needed because a translation
 *       steps the root of the 'publication' linked list between each lookup.
 *       This is always used within the scope of a gipc_nametbl_lock(read).
 *     - A local spin_lock protecting the queue of subscriber events.
*/

DEFINE_RWLOCK(gipc_net_lock);
struct network gipc_net = { NULL };

struct gipc_node *gipc_net_select_remote_node(u32 addr, u32 ref)
{
	return gipc_zone_select_remote_node(gipc_net.zones[gipc_zone(addr)], addr, ref);
}

u32 gipc_net_select_router(u32 addr, u32 ref)
{
	return gipc_zone_select_router(gipc_net.zones[gipc_zone(addr)], addr, ref);
}

#if 0
u32 gipc_net_next_node(u32 a)
{
	if (gipc_net.zones[gipc_zone(a)])
		return gipc_zone_next_node(a);
	return 0;
}
#endif

void gipc_net_remove_as_router(u32 router)
{
	u32 z_num;

	for (z_num = 1; z_num <= gipc_max_zones; z_num++) {
		if (!gipc_net.zones[z_num])
			continue;
		gipc_zone_remove_as_router(gipc_net.zones[z_num], router);
	}
}

void gipc_net_send_external_routes(u32 dest)
{
	u32 z_num;

	for (z_num = 1; z_num <= gipc_max_zones; z_num++) {
		if (gipc_net.zones[z_num])
			gipc_zone_send_external_routes(gipc_net.zones[z_num], dest);
	}
}

static int net_init(void)
{
	memset(&gipc_net, 0, sizeof(gipc_net));
	gipc_net.zones = kcalloc(gipc_max_zones + 1, sizeof(struct _zone *), GFP_ATOMIC);
	if (!gipc_net.zones) {
		return -ENOMEM;
	}
	return 0;
}

static void net_stop(void)
{
	u32 z_num;

	if (!gipc_net.zones)
		return;

	for (z_num = 1; z_num <= gipc_max_zones; z_num++) {
		gipc_zone_delete(gipc_net.zones[z_num]);
	}
	kfree(gipc_net.zones);
	gipc_net.zones = NULL;
}

static void net_route_named_msg(struct sk_buff *buf)
{
	struct gipc_msg *msg = buf_msg(buf);
	u32 dnode;
	u32 dport;

	if (!msg_named(msg)) {
		msg_dbg(msg, "gipc_net->drop_nam:");
		buf_discard(buf);
		return;
	}

	dnode = addr_domain(msg_lookup_scope(msg));
	dport = gipc_nametbl_translate(msg_nametype(msg), msg_nameinst(msg), &dnode);
	dbg("gipc_net->lookup<%u,%u>-><%u,%x>\n",
	    msg_nametype(msg), msg_nameinst(msg), dport, dnode);
	if (dport) {
		msg_set_destnode(msg, dnode);
		msg_set_destport(msg, dport);
		gipc_net_route_msg(buf);
		return;
	}
	msg_dbg(msg, "gipc_net->rej:NO NAME: ");
	gipc_reject_msg(buf, GIPC_ERR_NO_NAME);
}

void gipc_net_route_msg(struct sk_buff *buf)
{
	struct gipc_msg *msg;
	u32 dnode;

	if (!buf)
		return;
	msg = buf_msg(buf);

	msg_incr_reroute_cnt(msg);
	if (msg_reroute_cnt(msg) > 6) {
		if (msg_errcode(msg)) {
			msg_dbg(msg, "NET>DISC>:");
			buf_discard(buf);
		} else {
			msg_dbg(msg, "NET>REJ>:");
			gipc_reject_msg(buf, msg_destport(msg) ?
					GIPC_ERR_NO_PORT : GIPC_ERR_NO_NAME);
		}
		return;
	}

	msg_dbg(msg, "gipc_net->rout: ");

	/* Handle message for this node */
	dnode = msg_short(msg) ? gipc_own_addr : msg_destnode(msg);
	if (in_scope(dnode, gipc_own_addr)) {
		if (msg_isdata(msg)) {
			if (msg_mcast(msg))
				gipc_port_recv_mcast(buf, NULL);
			else if (msg_destport(msg))
				gipc_port_recv_msg(buf);
			else
				net_route_named_msg(buf);
			return;
		}
		switch (msg_user(msg)) {
		case ROUTE_DISTRIBUTOR:
			gipc_cltr_recv_routing_table(buf);
			break;
		case NAME_DISTRIBUTOR:
			gipc_named_recv(buf);
			break;
		case CONN_MANAGER:
			gipc_port_recv_proto_msg(buf);
			break;
		default:
			msg_dbg(msg,"DROP/NET/<REC<");
			buf_discard(buf);
		}
		return;
	}

	/* Handle message for another node */
	msg_dbg(msg, "NET>SEND>: ");
	gipc_link_send(buf, dnode, msg_link_selector(msg));
}

int gipc_net_start(u32 addr)
{
	char addr_string[16];
	int res;

	if (gipc_mode != GIPC_NODE_MODE)
		return -ENOPROTOOPT;

	gipc_subscr_stop();
	gipc_cfg_stop();

	gipc_own_addr = addr;
	gipc_mode = GIPC_NET_MODE;
	gipc_named_reinit();
	gipc_port_reinit();

	if ((res = gipc_bearer_init()) ||
	    (res = net_init()) ||
	    (res = gipc_cltr_init()) ||
	    (res = gipc_bclink_init())) {
		return res;
	}

	gipc_k_signal((Handler)gipc_subscr_start, 0);
	gipc_k_signal((Handler)gipc_cfg_init, 0);

	info("Started in network mode\n");
	info("Own node address %s, network identity %u\n",
	     addr_string_fill(addr_string, gipc_own_addr), gipc_net_id);
	return 0;
}

void gipc_net_stop(void)
{
	if (gipc_mode != GIPC_NET_MODE)
		return;
	write_lock_bh(&gipc_net_lock);
	gipc_bearer_stop();
	gipc_mode = GIPC_NODE_MODE;
	gipc_bclink_stop();
	net_stop();
	write_unlock_bh(&gipc_net_lock);
	info("Left network mode \n");
}

