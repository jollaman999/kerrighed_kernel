/*
 * include/linux/gipc.h: Include file for GIPC socket interface
 * 
 * Copyright (c) 2003-2006, Ericsson AB
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

#ifndef _LINUX_GIPC_H_
#define _LINUX_GIPC_H_

#include <linux/types.h>

/*
 * GIPC addressing primitives
 */
 
struct gipc_portid {
	__u32 ref;
	__u32 node;
};

struct gipc_name {
	__u32 type;
	__u32 instance;
};

struct gipc_name_seq {
	__u32 type;
	__u32 lower;
	__u32 upper;
};

static inline __u32 gipc_addr(unsigned int zone,
			      unsigned int cluster,
			      unsigned int node)
{
	return (zone << 24) | (cluster << 12) | node;
}

static inline unsigned int gipc_zone(__u32 addr)
{
	return addr >> 24;
}

static inline unsigned int gipc_cluster(__u32 addr)
{
	return (addr >> 12) & 0xfff;
}

static inline unsigned int gipc_node(__u32 addr)
{
	return addr & 0xfff;
}

/*
 * Application-accessible port name types
 */

#define GIPC_CFG_SRV		0	/* configuration service name type */
#define GIPC_TOP_SRV		1	/* topology service name type */
#define GIPC_RESERVED_TYPES	64	/* lowest user-publishable name type */

/* 
 * Publication scopes when binding port names and port name sequences
 */

#define GIPC_ZONE_SCOPE		1
#define GIPC_CLUSTER_SCOPE	2
#define GIPC_NODE_SCOPE		3

/*
 * Limiting values for messages
 */

#define GIPC_MAX_USER_MSG_SIZE	66000

/*
 * Message importance levels
 */

#define GIPC_LOW_IMPORTANCE		0  /* default */
#define GIPC_MEDIUM_IMPORTANCE		1
#define GIPC_HIGH_IMPORTANCE		2
#define GIPC_CRITICAL_IMPORTANCE	3

/* 
 * Msg rejection/connection shutdown reasons
 */

#define GIPC_OK			0
#define GIPC_ERR_NO_NAME	1
#define GIPC_ERR_NO_PORT	2
#define GIPC_ERR_NO_NODE	3
#define GIPC_ERR_OVERLOAD	4
#define GIPC_CONN_SHUTDOWN	5

/*
 * GIPC topology subscription service definitions
 */

#define GIPC_SUB_PORTS     	0x01  	/* filter for port availability */
#define GIPC_SUB_SERVICE     	0x02  	/* filter for service availability */
#define GIPC_SUB_CANCEL         0x04    /* cancel a subscription */
#if 0
/* The following filter options are not currently implemented */
#define GIPC_SUB_NO_BIND_EVTS	0x04	/* filter out "publish" events */
#define GIPC_SUB_NO_UNBIND_EVTS	0x08	/* filter out "withdraw" events */
#define GIPC_SUB_SINGLE_EVT	0x10	/* expire after first event */
#endif

#define GIPC_WAIT_FOREVER	~0	/* timeout for permanent subscription */

struct gipc_subscr {
	struct gipc_name_seq seq;	/* name sequence of interest */
	__u32 timeout;			/* subscription duration (in ms) */
        __u32 filter;   		/* bitmask of filter options */
	char usr_handle[8];		/* available for subscriber use */
};

#define GIPC_PUBLISHED		1	/* publication event */
#define GIPC_WITHDRAWN		2	/* withdraw event */
#define GIPC_SUBSCR_TIMEOUT	3	/* subscription timeout event */

struct gipc_event {
	__u32 event;			/* event type */
	__u32 found_lower;		/* matching name seq instances */
	__u32 found_upper;		/*    "      "    "     "      */
	struct gipc_portid port;	/* associated port */
	struct gipc_subscr s;		/* associated subscription */
};

/*
 * Socket API
 */

#ifndef AF_GIPC
#define AF_GIPC		30
#endif

#ifndef PF_GIPC
#define PF_GIPC		AF_GIPC
#endif

#ifndef SOL_GIPC
#define SOL_GIPC	271
#endif

#define GIPC_ADDR_NAMESEQ	1
#define GIPC_ADDR_MCAST		1
#define GIPC_ADDR_NAME		2
#define GIPC_ADDR_ID		3

struct sockaddr_gipc {
	unsigned short family;
	unsigned char  addrtype;
	signed   char  scope;
	union {
		struct gipc_portid id;
		struct gipc_name_seq nameseq;
		struct {
			struct gipc_name name;
			__u32 domain; /* 0: own zone */
		} name;
	} addr;
};

/*
 * Ancillary data objects supported by recvmsg()
 */

#define GIPC_ERRINFO	1	/* error info */
#define GIPC_RETDATA	2	/* returned data */
#define GIPC_DESTNAME	3	/* destination name */

/*
 * GIPC-specific socket option values
 */

#define GIPC_IMPORTANCE		127	/* Default: GIPC_LOW_IMPORTANCE */
#define GIPC_SRC_DROPPABLE	128	/* Default: 0 (resend congested msg) */
#define GIPC_DEST_DROPPABLE	129	/* Default: based on socket type */
#define GIPC_CONN_TIMEOUT	130	/* Default: 8000 (ms)  */
#define GIPC_NODE_RECVQ_DEPTH	131	/* Default: none (read only) */
#define GIPC_SOCK_RECVQ_DEPTH	132	/* Default: none (read only) */

#endif
