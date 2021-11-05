/*
 * net/gipc/core.c: GIPC module code
 *
 * Copyright (c) 2003-2006, Ericsson AB
 * Copyright (c) 2005-2006, Wind River Systems
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/random.h>

#include "core.h"
#include "dbg.h"
#include "ref.h"
#include "net.h"
#include "user_reg.h"
#include "name_table.h"
#include "subscr.h"
#include "config.h"


#define GIPC_MOD_VER "1.6.4"

#ifndef CONFIG_GIPC_ZONES
#define CONFIG_GIPC_ZONES 3
#endif

#ifndef CONFIG_GIPC_CLUSTERS
#define CONFIG_GIPC_CLUSTERS 1
#endif

#ifndef CONFIG_GIPC_NODES
#define CONFIG_GIPC_NODES 255
#endif

#ifndef CONFIG_GIPC_SLAVE_NODES
#define CONFIG_GIPC_SLAVE_NODES 0
#endif

#ifndef CONFIG_GIPC_PORTS
#define CONFIG_GIPC_PORTS 8191
#endif

#ifndef CONFIG_GIPC_LOG
#define CONFIG_GIPC_LOG 0
#endif

/* global variables used by multiple sub-systems within GIPC */

int gipc_mode = GIPC_NOT_RUNNING;
int gipc_random;
atomic_t gipc_user_count = ATOMIC_INIT(0);

const char gipc_alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.";

/* configurable GIPC parameters */

u32 gipc_own_addr;
int gipc_max_zones;
int gipc_max_clusters;
int gipc_max_nodes;
int gipc_max_slaves;
int gipc_max_ports;
int gipc_max_subscriptions;
int gipc_max_publications;
int gipc_net_id;
int gipc_remote_management;


int gipc_get_mode(void)
{
	return gipc_mode;
}

/**
 * gipc_core_stop_net - shut down GIPC networking sub-systems
 */

void gipc_core_stop_net(void)
{
	gipc_eth_media_stop();
	gipc_net_stop();
}

/**
 * start_net - start GIPC networking sub-systems
 */

int gipc_core_start_net(unsigned long addr)
{
	int res;

	if ((res = gipc_net_start(addr)) ||
	    (res = gipc_eth_media_start())) {
		gipc_core_stop_net();
	}
	return res;
}

/**
 * gipc_core_stop - switch GIPC from SINGLE NODE to NOT RUNNING mode
 */

void gipc_core_stop(void)
{
	if (gipc_mode != GIPC_NODE_MODE)
		return;

	gipc_mode = GIPC_NOT_RUNNING;

	gipc_netlink_stop();
	gipc_handler_stop();
	gipc_cfg_stop();
	gipc_subscr_stop();
	gipc_reg_stop();
	gipc_nametbl_stop();
	gipc_ref_table_stop();
	gipc_socket_stop();
}

/**
 * gipc_core_start - switch GIPC from NOT RUNNING to SINGLE NODE mode
 */

int gipc_core_start(void)
{
	int res;

	if (gipc_mode != GIPC_NOT_RUNNING)
		return -ENOPROTOOPT;

	get_random_bytes(&gipc_random, sizeof(gipc_random));
	gipc_mode = GIPC_NODE_MODE;

	if ((res = gipc_handler_start()) ||
	    (res = gipc_ref_table_init(gipc_max_ports, gipc_random)) ||
	    (res = gipc_reg_start()) ||
	    (res = gipc_nametbl_init()) ||
	    (res = gipc_k_signal((Handler)gipc_subscr_start, 0)) ||
	    (res = gipc_k_signal((Handler)gipc_cfg_init, 0)) ||
	    (res = gipc_netlink_start()) ||
	    (res = gipc_socket_init())) {
		gipc_core_stop();
	}
	return res;
}


static int __init gipc_init(void)
{
	int res;

	gipc_log_resize(CONFIG_GIPC_LOG);
	info("Activated (version " GIPC_MOD_VER
	     " compiled " __DATE__ " " __TIME__ ")\n");

	gipc_own_addr = 0;
	gipc_remote_management = 1;
	gipc_max_publications = 10000;
	gipc_max_subscriptions = 2000;
	gipc_max_ports = delimit(CONFIG_GIPC_PORTS, 127, 65536);
	gipc_max_zones = delimit(CONFIG_GIPC_ZONES, 1, 255);
	gipc_max_clusters = delimit(CONFIG_GIPC_CLUSTERS, 1, 1);
	gipc_max_nodes = delimit(CONFIG_GIPC_NODES, 8, 2047);
	gipc_max_slaves = delimit(CONFIG_GIPC_SLAVE_NODES, 0, 2047);
	gipc_net_id = 4711;

	if ((res = gipc_core_start()))
		err("Unable to start in single node mode\n");
	else
		info("Started in single node mode\n");
	return res;
}

static void __exit gipc_exit(void)
{
	gipc_core_stop_net();
	gipc_core_stop();
	info("Deactivated\n");
	gipc_log_resize(0);
}

module_init(gipc_init);
module_exit(gipc_exit);

MODULE_DESCRIPTION("GIPC: Transparent Inter Process Communication");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(GIPC_MOD_VER);

/* Native GIPC API for kernel-space applications (see gipc.h) */

EXPORT_SYMBOL(gipc_attach);
EXPORT_SYMBOL(gipc_detach);
EXPORT_SYMBOL(gipc_get_addr);
EXPORT_SYMBOL(gipc_get_mode);
EXPORT_SYMBOL(gipc_createport);
EXPORT_SYMBOL(gipc_deleteport);
EXPORT_SYMBOL(gipc_ownidentity);
EXPORT_SYMBOL(gipc_portimportance);
EXPORT_SYMBOL(gipc_set_portimportance);
EXPORT_SYMBOL(gipc_portunreliable);
EXPORT_SYMBOL(gipc_set_portunreliable);
EXPORT_SYMBOL(gipc_portunreturnable);
EXPORT_SYMBOL(gipc_set_portunreturnable);
EXPORT_SYMBOL(gipc_publish);
EXPORT_SYMBOL(gipc_withdraw);
EXPORT_SYMBOL(gipc_connect2port);
EXPORT_SYMBOL(gipc_disconnect);
EXPORT_SYMBOL(gipc_shutdown);
EXPORT_SYMBOL(gipc_isconnected);
EXPORT_SYMBOL(gipc_peer);
EXPORT_SYMBOL(gipc_ref_valid);
EXPORT_SYMBOL(gipc_send);
EXPORT_SYMBOL(gipc_send_buf);
EXPORT_SYMBOL(gipc_send2name);
EXPORT_SYMBOL(gipc_forward2name);
EXPORT_SYMBOL(gipc_send_buf2name);
EXPORT_SYMBOL(gipc_forward_buf2name);
EXPORT_SYMBOL(gipc_send2port);
EXPORT_SYMBOL(gipc_forward2port);
EXPORT_SYMBOL(gipc_send_buf2port);
EXPORT_SYMBOL(gipc_forward_buf2port);
EXPORT_SYMBOL(gipc_multicast);
/* EXPORT_SYMBOL(gipc_multicast_buf); not available yet */
EXPORT_SYMBOL(gipc_ispublished);
EXPORT_SYMBOL(gipc_available_nodes);

/* GIPC API for external bearers (see gipc_bearer.h) */

EXPORT_SYMBOL(gipc_block_bearer);
EXPORT_SYMBOL(gipc_continue);
EXPORT_SYMBOL(gipc_disable_bearer);
EXPORT_SYMBOL(gipc_enable_bearer);
EXPORT_SYMBOL(gipc_recv_msg);
EXPORT_SYMBOL(gipc_register_media);

/* GIPC API for external APIs (see gipc_port.h) */

EXPORT_SYMBOL(gipc_createport_raw);
EXPORT_SYMBOL(gipc_reject_msg);
EXPORT_SYMBOL(gipc_send_buf_fast);
EXPORT_SYMBOL(gipc_acknowledge);
EXPORT_SYMBOL(gipc_get_port);
EXPORT_SYMBOL(gipc_get_handle);

