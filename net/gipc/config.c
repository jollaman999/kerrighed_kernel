/*
 * net/gipc/config.c: GIPC configuration management code
 *
 * Copyright (c) 2002-2006, Ericsson AB
 * Copyright (c) 2004-2007, Wind River Systems
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
#include "dbg.h"
#include "bearer.h"
#include "port.h"
#include "link.h"
#include "zone.h"
#include "addr.h"
#include "name_table.h"
#include "node.h"
#include "config.h"
#include "discover.h"

struct subscr_data {
	char usr_handle[8];
	u32 domain;
	u32 port_ref;
	struct list_head subd_list;
};

struct manager {
	u32 user_ref;
	u32 port_ref;
	u32 subscr_ref;
	u32 link_subscriptions;
	struct list_head link_subscribers;
};

static struct manager mng = { 0};

static DEFINE_SPINLOCK(config_lock);

static const void *req_tlv_area;	/* request message TLV area */
static int req_tlv_space;		/* request message TLV area size */
static int rep_headroom;		/* reply message headroom to use */


void gipc_cfg_link_event(u32 addr, char *name, int up)
{
	/* GIPC DOESN'T HANDLE LINK EVENT SUBSCRIPTIONS AT THE MOMENT */
}


struct sk_buff *gipc_cfg_reply_alloc(int payload_size)
{
	struct sk_buff *buf;

	buf = alloc_skb(rep_headroom + payload_size, GFP_ATOMIC);
	if (buf)
		skb_reserve(buf, rep_headroom);
	return buf;
}

int gipc_cfg_append_tlv(struct sk_buff *buf, int tlv_type,
			void *tlv_data, int tlv_data_size)
{
	struct tlv_desc *tlv = (struct tlv_desc *)skb_tail_pointer(buf);
	int new_tlv_space = TLV_SPACE(tlv_data_size);

	if (skb_tailroom(buf) < new_tlv_space) {
		dbg("gipc_cfg_append_tlv unable to append TLV\n");
		return 0;
	}
	skb_put(buf, new_tlv_space);
	tlv->tlv_type = htons(tlv_type);
	tlv->tlv_len  = htons(TLV_LENGTH(tlv_data_size));
	if (tlv_data_size && tlv_data)
		memcpy(TLV_DATA(tlv), tlv_data, tlv_data_size);
	return 1;
}

struct sk_buff *gipc_cfg_reply_unsigned_type(u16 tlv_type, u32 value)
{
	struct sk_buff *buf;
	__be32 value_net;

	buf = gipc_cfg_reply_alloc(TLV_SPACE(sizeof(value)));
	if (buf) {
		value_net = htonl(value);
		gipc_cfg_append_tlv(buf, tlv_type, &value_net,
				    sizeof(value_net));
	}
	return buf;
}

struct sk_buff *gipc_cfg_reply_string_type(u16 tlv_type, char *string)
{
	struct sk_buff *buf;
	int string_len = strlen(string) + 1;

	buf = gipc_cfg_reply_alloc(TLV_SPACE(string_len));
	if (buf)
		gipc_cfg_append_tlv(buf, tlv_type, string, string_len);
	return buf;
}




#if 0

/* Now obsolete code for handling commands not yet implemented the new way */

int gipc_cfg_cmd(const struct gipc_cmd_msg * msg,
		 char *data,
		 u32 sz,
		 u32 *ret_size,
		 struct gipc_portid *orig)
{
	int rv = -EINVAL;
	u32 cmd = msg->cmd;

	*ret_size = 0;
	switch (cmd) {
	case GIPC_REMOVE_LINK:
	case GIPC_CMD_BLOCK_LINK:
	case GIPC_CMD_UNBLOCK_LINK:
		if (!cfg_check_connection(orig))
			rv = link_control(msg->argv.link_name, msg->cmd, 0);
		break;
	case GIPC_ESTABLISH:
		{
			int connected;

			gipc_isconnected(mng.conn_port_ref, &connected);
			if (connected || !orig) {
				rv = GIPC_FAILURE;
				break;
			}
			rv = gipc_connect2port(mng.conn_port_ref, orig);
			if (rv == GIPC_OK)
				orig = 0;
			break;
		}
	case GIPC_GET_PEER_ADDRESS:
		*ret_size = link_peer_addr(msg->argv.link_name, data, sz);
		break;
	case GIPC_GET_ROUTES:
		rv = GIPC_OK;
		break;
	default: {}
	}
	if (*ret_size)
		rv = GIPC_OK;
	return rv;
}

static void cfg_cmd_event(struct gipc_cmd_msg *msg,
			  char *data,
			  u32 sz,
			  struct gipc_portid const *orig)
{
	int rv = -EINVAL;
	struct gipc_cmd_result_msg rmsg;
	struct iovec msg_sect[2];
	int *arg;

	msg->cmd = ntohl(msg->cmd);

	cfg_prepare_res_msg(msg->cmd, msg->usr_handle, rv, &rmsg, msg_sect,
			    data, 0);
	if (ntohl(msg->magic) != GIPC_MAGIC)
		goto exit;

	switch (msg->cmd) {
	case GIPC_CREATE_LINK:
		if (!cfg_check_connection(orig))
			rv = disc_create_link(&msg->argv.create_link);
		break;
	case GIPC_LINK_SUBSCRIBE:
		{
			struct subscr_data *sub;

			if (mng.link_subscriptions > 64)
				break;
			sub = kmalloc(sizeof(*sub),
							    GFP_ATOMIC);
			if (sub == NULL) {
				warn("Memory squeeze; dropped remote link subscription\n");
				break;
			}
			INIT_LIST_HEAD(&sub->subd_list);
			gipc_createport(mng.user_ref,
					(void *)sub,
					GIPC_HIGH_IMPORTANCE,
					0,
					0,
					(gipc_conn_shutdown_event)cfg_linksubscr_cancel,
					0,
					0,
					(gipc_conn_msg_event)cfg_linksubscr_cancel,
					0,
					&sub->port_ref);
			if (!sub->port_ref) {
				kfree(sub);
				break;
			}
			memcpy(sub->usr_handle,msg->usr_handle,
			       sizeof(sub->usr_handle));
			sub->domain = msg->argv.domain;
			list_add_tail(&sub->subd_list, &mng.link_subscribers);
			gipc_connect2port(sub->port_ref, orig);
			rmsg.retval = GIPC_OK;
			gipc_send(sub->port_ref, 2u, msg_sect);
			mng.link_subscriptions++;
			return;
		}
	default:
		rv = gipc_cfg_cmd(msg, data, sz, (u32 *)&msg_sect[1].iov_len, orig);
	}
	exit:
	rmsg.result_len = htonl(msg_sect[1].iov_len);
	rmsg.retval = htonl(rv);
	gipc_cfg_respond(msg_sect, 2u, orig);
}
#endif

static struct sk_buff *cfg_enable_bearer(void)
{
	struct gipc_bearer_config *args;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_BEARER_CONFIG))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);

	args = (struct gipc_bearer_config *)TLV_DATA(req_tlv_area);
	if (gipc_enable_bearer(args->name,
			       ntohl(args->detect_scope),
			       ntohl(args->priority)))
		return gipc_cfg_reply_error_string("unable to enable bearer");

	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_disable_bearer(void)
{
	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_BEARER_NAME))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);

	if (gipc_disable_bearer((char *)TLV_DATA(req_tlv_area)))
		return gipc_cfg_reply_error_string("unable to disable bearer");

	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_own_addr(void)
{
	u32 addr;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_NET_ADDR))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);

	addr = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (addr == gipc_own_addr)
		return gipc_cfg_reply_none();
	if (!gipc_addr_node_valid(addr))
		return gipc_cfg_reply_error_string(GIPC_CFG_INVALID_VALUE
						   " (node address)");
	if (gipc_mode == GIPC_NET_MODE)
		return gipc_cfg_reply_error_string(GIPC_CFG_NOT_SUPPORTED
						   " (cannot change node address once assigned)");

	/*
	 * Must release all spinlocks before calling start_net() because
	 * Linux version of GIPC calls eth_media_start() which calls
	 * register_netdevice_notifier() which may block!
	 *
	 * Temporarily releasing the lock should be harmless for non-Linux GIPC,
	 * but Linux version of eth_media_start() should really be reworked
	 * so that it can be called with spinlocks held.
	 */

	spin_unlock_bh(&config_lock);
	gipc_core_start_net(addr);
	spin_lock_bh(&config_lock);
	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_remote_mng(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);

	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	gipc_remote_management = (value != 0);
	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_publications(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);

	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value != delimit(value, 1, 65535))
		return gipc_cfg_reply_error_string(GIPC_CFG_INVALID_VALUE
						   " (max publications must be 1-65535)");
	gipc_max_publications = value;
	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_subscriptions(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);

	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value != delimit(value, 1, 65535))
		return gipc_cfg_reply_error_string(GIPC_CFG_INVALID_VALUE
						   " (max subscriptions must be 1-65535");
	gipc_max_subscriptions = value;
	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_ports(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value == gipc_max_ports)
		return gipc_cfg_reply_none();
	if (value != delimit(value, 127, 65535))
		return gipc_cfg_reply_error_string(GIPC_CFG_INVALID_VALUE
						   " (max ports must be 127-65535)");
	if (gipc_mode != GIPC_NOT_RUNNING)
		return gipc_cfg_reply_error_string(GIPC_CFG_NOT_SUPPORTED
			" (cannot change max ports while GIPC is active)");
	gipc_max_ports = value;
	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_zones(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value == gipc_max_zones)
		return gipc_cfg_reply_none();
	if (value != delimit(value, 1, 255))
		return gipc_cfg_reply_error_string(GIPC_CFG_INVALID_VALUE
						   " (max zones must be 1-255)");
	if (gipc_mode == GIPC_NET_MODE)
		return gipc_cfg_reply_error_string(GIPC_CFG_NOT_SUPPORTED
			" (cannot change max zones once GIPC has joined a network)");
	gipc_max_zones = value;
	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_clusters(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value != delimit(value, 1, 1))
		return gipc_cfg_reply_error_string(GIPC_CFG_INVALID_VALUE
						   " (max clusters fixed at 1)");
	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_nodes(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value == gipc_max_nodes)
		return gipc_cfg_reply_none();
	if (value != delimit(value, 8, 2047))
		return gipc_cfg_reply_error_string(GIPC_CFG_INVALID_VALUE
						   " (max nodes must be 8-2047)");
	if (gipc_mode == GIPC_NET_MODE)
		return gipc_cfg_reply_error_string(GIPC_CFG_NOT_SUPPORTED
			" (cannot change max nodes once GIPC has joined a network)");
	gipc_max_nodes = value;
	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_max_slaves(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value != 0)
		return gipc_cfg_reply_error_string(GIPC_CFG_NOT_SUPPORTED
						   " (max secondary nodes fixed at 0)");
	return gipc_cfg_reply_none();
}

static struct sk_buff *cfg_set_netid(void)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, GIPC_TLV_UNSIGNED))
		return gipc_cfg_reply_error_string(GIPC_CFG_TLV_ERROR);
	value = ntohl(*(__be32 *)TLV_DATA(req_tlv_area));
	if (value == gipc_net_id)
		return gipc_cfg_reply_none();
	if (value != delimit(value, 1, 9999))
		return gipc_cfg_reply_error_string(GIPC_CFG_INVALID_VALUE
						   " (network id must be 1-9999)");
	if (gipc_mode == GIPC_NET_MODE)
		return gipc_cfg_reply_error_string(GIPC_CFG_NOT_SUPPORTED
			" (cannot change network id once GIPC has joined a network)");
	gipc_net_id = value;
	return gipc_cfg_reply_none();
}

struct sk_buff *gipc_cfg_do_cmd(u32 orig_node, u16 cmd, const void *request_area,
				int request_space, int reply_headroom)
{
	struct sk_buff *rep_tlv_buf;

	spin_lock_bh(&config_lock);

	/* Save request and reply details in a well-known location */

	req_tlv_area = request_area;
	req_tlv_space = request_space;
	rep_headroom = reply_headroom;

	/* Check command authorization */

	if (likely(orig_node == gipc_own_addr)) {
		/* command is permitted */
	} else if (cmd >= 0x8000) {
		rep_tlv_buf = gipc_cfg_reply_error_string(GIPC_CFG_NOT_SUPPORTED
							  " (cannot be done remotely)");
		goto exit;
	} else if (!gipc_remote_management) {
		rep_tlv_buf = gipc_cfg_reply_error_string(GIPC_CFG_NO_REMOTE);
		goto exit;
	}
	else if (cmd >= 0x4000) {
		u32 domain = 0;

		if ((gipc_nametbl_translate(GIPC_ZM_SRV, 0, &domain) == 0) ||
		    (domain != orig_node)) {
			rep_tlv_buf = gipc_cfg_reply_error_string(GIPC_CFG_NOT_ZONE_MSTR);
			goto exit;
		}
	}

	/* Call appropriate processing routine */

	switch (cmd) {
	case GIPC_CMD_NOOP:
		rep_tlv_buf = gipc_cfg_reply_none();
		break;
	case GIPC_CMD_GET_NODES:
		rep_tlv_buf = gipc_node_get_nodes(req_tlv_area, req_tlv_space);
		break;
	case GIPC_CMD_GET_LINKS:
		rep_tlv_buf = gipc_node_get_links(req_tlv_area, req_tlv_space);
		break;
	case GIPC_CMD_SHOW_LINK_STATS:
		rep_tlv_buf = gipc_link_cmd_show_stats(req_tlv_area, req_tlv_space);
		break;
	case GIPC_CMD_RESET_LINK_STATS:
		rep_tlv_buf = gipc_link_cmd_reset_stats(req_tlv_area, req_tlv_space);
		break;
	case GIPC_CMD_SHOW_NAME_TABLE:
		rep_tlv_buf = gipc_nametbl_get(req_tlv_area, req_tlv_space);
		break;
	case GIPC_CMD_GET_BEARER_NAMES:
		rep_tlv_buf = gipc_bearer_get_names();
		break;
	case GIPC_CMD_GET_MEDIA_NAMES:
		rep_tlv_buf = gipc_media_get_names();
		break;
	case GIPC_CMD_SHOW_PORTS:
		rep_tlv_buf = gipc_port_get_ports();
		break;
#if 0
	case GIPC_CMD_SHOW_PORT_STATS:
		rep_tlv_buf = port_show_stats(req_tlv_area, req_tlv_space);
		break;
	case GIPC_CMD_RESET_PORT_STATS:
		rep_tlv_buf = gipc_cfg_reply_error_string(GIPC_CFG_NOT_SUPPORTED);
		break;
#endif
	case GIPC_CMD_SET_LOG_SIZE:
		rep_tlv_buf = gipc_log_resize_cmd(req_tlv_area, req_tlv_space);
		break;
	case GIPC_CMD_DUMP_LOG:
		rep_tlv_buf = gipc_log_dump();
		break;
	case GIPC_CMD_SET_LINK_TOL:
	case GIPC_CMD_SET_LINK_PRI:
	case GIPC_CMD_SET_LINK_WINDOW:
		rep_tlv_buf = gipc_link_cmd_config(req_tlv_area, req_tlv_space, cmd);
		break;
	case GIPC_CMD_ENABLE_BEARER:
		rep_tlv_buf = cfg_enable_bearer();
		break;
	case GIPC_CMD_DISABLE_BEARER:
		rep_tlv_buf = cfg_disable_bearer();
		break;
	case GIPC_CMD_SET_NODE_ADDR:
		rep_tlv_buf = cfg_set_own_addr();
		break;
	case GIPC_CMD_SET_REMOTE_MNG:
		rep_tlv_buf = cfg_set_remote_mng();
		break;
	case GIPC_CMD_SET_MAX_PORTS:
		rep_tlv_buf = cfg_set_max_ports();
		break;
	case GIPC_CMD_SET_MAX_PUBL:
		rep_tlv_buf = cfg_set_max_publications();
		break;
	case GIPC_CMD_SET_MAX_SUBSCR:
		rep_tlv_buf = cfg_set_max_subscriptions();
		break;
	case GIPC_CMD_SET_MAX_ZONES:
		rep_tlv_buf = cfg_set_max_zones();
		break;
	case GIPC_CMD_SET_MAX_CLUSTERS:
		rep_tlv_buf = cfg_set_max_clusters();
		break;
	case GIPC_CMD_SET_MAX_NODES:
		rep_tlv_buf = cfg_set_max_nodes();
		break;
	case GIPC_CMD_SET_MAX_SLAVES:
		rep_tlv_buf = cfg_set_max_slaves();
		break;
	case GIPC_CMD_SET_NETID:
		rep_tlv_buf = cfg_set_netid();
		break;
	case GIPC_CMD_GET_REMOTE_MNG:
		rep_tlv_buf = gipc_cfg_reply_unsigned(gipc_remote_management);
		break;
	case GIPC_CMD_GET_MAX_PORTS:
		rep_tlv_buf = gipc_cfg_reply_unsigned(gipc_max_ports);
		break;
	case GIPC_CMD_GET_MAX_PUBL:
		rep_tlv_buf = gipc_cfg_reply_unsigned(gipc_max_publications);
		break;
	case GIPC_CMD_GET_MAX_SUBSCR:
		rep_tlv_buf = gipc_cfg_reply_unsigned(gipc_max_subscriptions);
		break;
	case GIPC_CMD_GET_MAX_ZONES:
		rep_tlv_buf = gipc_cfg_reply_unsigned(gipc_max_zones);
		break;
	case GIPC_CMD_GET_MAX_CLUSTERS:
		rep_tlv_buf = gipc_cfg_reply_unsigned(gipc_max_clusters);
		break;
	case GIPC_CMD_GET_MAX_NODES:
		rep_tlv_buf = gipc_cfg_reply_unsigned(gipc_max_nodes);
		break;
	case GIPC_CMD_GET_MAX_SLAVES:
		rep_tlv_buf = gipc_cfg_reply_unsigned(gipc_max_slaves);
		break;
	case GIPC_CMD_GET_NETID:
		rep_tlv_buf = gipc_cfg_reply_unsigned(gipc_net_id);
		break;
	case GIPC_CMD_NOT_NET_ADMIN:
		rep_tlv_buf =
			gipc_cfg_reply_error_string(GIPC_CFG_NOT_NET_ADMIN);
		break;
	default:
		rep_tlv_buf = gipc_cfg_reply_error_string(GIPC_CFG_NOT_SUPPORTED
							  " (unknown command)");
		break;
	}

	/* Return reply buffer */
exit:
	spin_unlock_bh(&config_lock);
	return rep_tlv_buf;
}

static void cfg_named_msg_event(void *userdata,
				u32 port_ref,
				struct sk_buff **buf,
				const unchar *msg,
				u32 size,
				u32 importance,
				struct gipc_portid const *orig,
				struct gipc_name_seq const *dest)
{
	struct gipc_cfg_msg_hdr *req_hdr;
	struct gipc_cfg_msg_hdr *rep_hdr;
	struct sk_buff *rep_buf;

	/* Validate configuration message header (ignore invalid message) */

	req_hdr = (struct gipc_cfg_msg_hdr *)msg;
	if ((size < sizeof(*req_hdr)) ||
	    (size != TCM_ALIGN(ntohl(req_hdr->tcm_len))) ||
	    (ntohs(req_hdr->tcm_flags) != TCM_F_REQUEST)) {
		warn("Invalid configuration message discarded\n");
		return;
	}

	/* Generate reply for request (if can't, return request) */

	rep_buf = gipc_cfg_do_cmd(orig->node,
				  ntohs(req_hdr->tcm_type),
				  msg + sizeof(*req_hdr),
				  size - sizeof(*req_hdr),
				  BUF_HEADROOM + MAX_H_SIZE + sizeof(*rep_hdr));
	if (rep_buf) {
		skb_push(rep_buf, sizeof(*rep_hdr));
		rep_hdr = (struct gipc_cfg_msg_hdr *)rep_buf->data;
		memcpy(rep_hdr, req_hdr, sizeof(*rep_hdr));
		rep_hdr->tcm_len = htonl(rep_buf->len);
		rep_hdr->tcm_flags &= htons(~TCM_F_REQUEST);
	} else {
		rep_buf = *buf;
		*buf = NULL;
	}

	/* NEED TO ADD CODE TO HANDLE FAILED SEND (SUCH AS CONGESTION) */
	gipc_send_buf2port(port_ref, orig, rep_buf, rep_buf->len);
}

int gipc_cfg_init(void)
{
	struct gipc_name_seq seq;
	int res;

	memset(&mng, 0, sizeof(mng));
	INIT_LIST_HEAD(&mng.link_subscribers);

	res = gipc_attach(&mng.user_ref, NULL, NULL);
	if (res)
		goto failed;

	res = gipc_createport(mng.user_ref, NULL, GIPC_CRITICAL_IMPORTANCE,
			      NULL, NULL, NULL,
			      NULL, cfg_named_msg_event, NULL,
			      NULL, &mng.port_ref);
	if (res)
		goto failed;

	seq.type = GIPC_CFG_SRV;
	seq.lower = seq.upper = gipc_own_addr;
	res = gipc_nametbl_publish_rsv(mng.port_ref, GIPC_ZONE_SCOPE, &seq);
	if (res)
		goto failed;

	return 0;

failed:
	err("Unable to create configuration service\n");
	gipc_detach(mng.user_ref);
	mng.user_ref = 0;
	return res;
}

void gipc_cfg_stop(void)
{
	if (mng.user_ref) {
		gipc_detach(mng.user_ref);
		mng.user_ref = 0;
	}
}
