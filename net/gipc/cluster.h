/*
 * net/gipc/cluster.h: Include file for GIPC cluster management routines
 *
 * Copyright (c) 2000-2006, Ericsson AB
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

#ifndef _GIPC_CLUSTER_H
#define _GIPC_CLUSTER_H

#include "addr.h"
#include "zone.h"

#define LOWEST_SLAVE  2048u

/**
 * struct cluster - GIPC cluster structure
 * @addr: network address of cluster
 * @owner: pointer to zone that cluster belongs to
 * @nodes: array of pointers to all nodes within cluster
 * @highest_node: id of highest numbered node within cluster
 * @highest_slave: (used for secondary node support)
 */

struct cluster {
	u32 addr;
	struct _zone *owner;
	struct gipc_node **nodes;
	u32 highest_node;
	u32 highest_slave;
};


extern struct gipc_node **gipc_local_nodes;
extern u32 gipc_highest_allowed_slave;
extern struct gipc_node_map gipc_cltr_bcast_nodes;

void gipc_cltr_remove_as_router(struct cluster *c_ptr, u32 router);
void gipc_cltr_send_ext_routes(struct cluster *c_ptr, u32 dest);
struct gipc_node *gipc_cltr_select_node(struct cluster *c_ptr, u32 selector);
u32 gipc_cltr_select_router(struct cluster *c_ptr, u32 ref);
void gipc_cltr_recv_routing_table(struct sk_buff *buf);
struct cluster *gipc_cltr_create(u32 addr);
void gipc_cltr_delete(struct cluster *c_ptr);
void gipc_cltr_attach_node(struct cluster *c_ptr, struct gipc_node *n_ptr);
void gipc_cltr_send_slave_routes(struct cluster *c_ptr, u32 dest);
void gipc_cltr_broadcast(struct sk_buff *buf);
int gipc_cltr_init(void);
u32 gipc_cltr_next_node(struct cluster *c_ptr, u32 addr);
void gipc_cltr_bcast_new_route(struct cluster *c_ptr, u32 dest, u32 lo, u32 hi);
void gipc_cltr_send_local_routes(struct cluster *c_ptr, u32 dest);
void gipc_cltr_bcast_lost_route(struct cluster *c_ptr, u32 dest, u32 lo, u32 hi);

static inline struct cluster *gipc_cltr_find(u32 addr)
{
	struct _zone *z_ptr = gipc_zone_find(addr);

	if (z_ptr)
		return z_ptr->clusters[1];
	return NULL;
}

#endif
