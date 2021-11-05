/*
 * include/net/gipc/gipc.h: Main include file for GIPC users
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

#ifndef _NET_GIPC_H_
#define _NET_GIPC_H_

#ifdef __KERNEL__

#include <linux/gipc.h>
#include <linux/skbuff.h>

/* 
 * Native API
 */

#ifdef CONFIG_HCC_GRPC
extern int gipc_net_id;
int gipc_core_start_net(unsigned long addr);
#endif

/*
 * GIPC operating mode routines
 */

u32 gipc_get_addr(void);

#define GIPC_NOT_RUNNING  0
#define GIPC_NODE_MODE    1
#define GIPC_NET_MODE     2

typedef void (*gipc_mode_event)(void *usr_handle, int mode, u32 addr);

int gipc_attach(unsigned int *userref, gipc_mode_event, void *usr_handle);

void gipc_detach(unsigned int userref);

int gipc_get_mode(void);

/*
 * GIPC port manipulation routines
 */

typedef void (*gipc_msg_err_event) (void *usr_handle,
				    u32 portref,
				    struct sk_buff **buf,
				    unsigned char const *data,
				    unsigned int size,
				    int reason, 
				    struct gipc_portid const *attmpt_destid);

typedef void (*gipc_named_msg_err_event) (void *usr_handle,
					  u32 portref,
					  struct sk_buff **buf,
					  unsigned char const *data,
					  unsigned int size,
					  int reason, 
					  struct gipc_name_seq const *attmpt_dest);

typedef void (*gipc_conn_shutdown_event) (void *usr_handle,
					  u32 portref,
					  struct sk_buff **buf,
					  unsigned char const *data,
					  unsigned int size,
					  int reason);

typedef void (*gipc_msg_event) (void *usr_handle,
				u32 portref,
				struct sk_buff **buf,
				unsigned char const *data,
				unsigned int size,
				unsigned int importance, 
				struct gipc_portid const *origin);

typedef void (*gipc_named_msg_event) (void *usr_handle,
				      u32 portref,
				      struct sk_buff **buf,
				      unsigned char const *data,
				      unsigned int size,
				      unsigned int importance, 
				      struct gipc_portid const *orig,
				      struct gipc_name_seq const *dest);

typedef void (*gipc_conn_msg_event) (void *usr_handle,
				     u32 portref,
				     struct sk_buff **buf,
				     unsigned char const *data,
				     unsigned int size);

typedef void (*gipc_continue_event) (void *usr_handle, 
				     u32 portref);

int gipc_createport(unsigned int gipc_user, 
		    void *usr_handle, 
		    unsigned int importance, 
		    gipc_msg_err_event error_cb, 
		    gipc_named_msg_err_event named_error_cb, 
		    gipc_conn_shutdown_event conn_error_cb, 
		    gipc_msg_event message_cb, 
		    gipc_named_msg_event named_message_cb, 
		    gipc_conn_msg_event conn_message_cb, 
		    gipc_continue_event continue_event_cb,/* May be zero */
		    u32 *portref);

int gipc_deleteport(u32 portref);

int gipc_ownidentity(u32 portref, struct gipc_portid *port);

int gipc_portimportance(u32 portref, unsigned int *importance);
int gipc_set_portimportance(u32 portref, unsigned int importance);

int gipc_portunreliable(u32 portref, unsigned int *isunreliable);
int gipc_set_portunreliable(u32 portref, unsigned int isunreliable);

int gipc_portunreturnable(u32 portref, unsigned int *isunreturnable);
int gipc_set_portunreturnable(u32 portref, unsigned int isunreturnable);

int gipc_publish(u32 portref, unsigned int scope, 
		 struct gipc_name_seq const *name_seq);
int gipc_withdraw(u32 portref, unsigned int scope,
		  struct gipc_name_seq const *name_seq); /* 0: all */

int gipc_connect2port(u32 portref, struct gipc_portid const *port);

int gipc_disconnect(u32 portref);

int gipc_shutdown(u32 ref); /* Sends SHUTDOWN msg */

int gipc_isconnected(u32 portref, int *isconnected);

int gipc_peer(u32 portref, struct gipc_portid *peer);

int gipc_ref_valid(u32 portref); 

/*
 * GIPC messaging routines
 */

#define GIPC_PORT_IMPORTANCE 100	/* send using current port setting */


int gipc_send(u32 portref,
	      unsigned int num_sect,
	      struct iovec const *msg_sect);

int gipc_send_buf(u32 portref,
		  struct sk_buff *buf,
		  unsigned int dsz);

int gipc_send2name(u32 portref, 
		   struct gipc_name const *name, 
		   u32 domain,	/* 0:own zone */
		   unsigned int num_sect,
		   struct iovec const *msg_sect);

int gipc_send_buf2name(u32 portref,
		       struct gipc_name const *name,
		       u32 domain,
		       struct sk_buff *buf,
		       unsigned int dsz);

int gipc_forward2name(u32 portref, 
		      struct gipc_name const *name, 
		      u32 domain,   /*0: own zone */
		      unsigned int section_count,
		      struct iovec const *msg_sect,
		      struct gipc_portid const *origin,
		      unsigned int importance);

int gipc_forward_buf2name(u32 portref,
			  struct gipc_name const *name,
			  u32 domain,
			  struct sk_buff *buf,
			  unsigned int dsz,
			  struct gipc_portid const *orig,
			  unsigned int importance);

int gipc_send2port(u32 portref,
		   struct gipc_portid const *dest,
		   unsigned int num_sect,
		   struct iovec const *msg_sect);

int gipc_send_buf2port(u32 portref,
		       struct gipc_portid const *dest,
		       struct sk_buff *buf,
		       unsigned int dsz);

int gipc_forward2port(u32 portref,
		      struct gipc_portid const *dest,
		      unsigned int num_sect,
		      struct iovec const *msg_sect,
		      struct gipc_portid const *origin,
		      unsigned int importance);

int gipc_forward_buf2port(u32 portref,
			  struct gipc_portid const *dest,
			  struct sk_buff *buf,
			  unsigned int dsz,
			  struct gipc_portid const *orig,
			  unsigned int importance);

int gipc_multicast(u32 portref, 
		   struct gipc_name_seq const *seq, 
		   u32 domain,	/* 0:own zone */
		   unsigned int section_count,
		   struct iovec const *msg);

#if 0
int gipc_multicast_buf(u32 portref, 
		       struct gipc_name_seq const *seq, 
		       u32 domain,	/* 0:own zone */
		       void *buf,
		       unsigned int size);
#endif

/*
 * GIPC subscription routines
 */

int gipc_ispublished(struct gipc_name const *name);

/*
 * Get number of available nodes within specified domain (excluding own node)
 */

unsigned int gipc_available_nodes(const u32 domain);

#endif

#endif
