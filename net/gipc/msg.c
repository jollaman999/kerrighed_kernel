/*
 * net/gipc/msg.c: GIPC message header routines
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

#include "core.h"
#include "addr.h"
#include "dbg.h"
#include "msg.h"
#include "bearer.h"


#ifdef CONFIG_GIPC_DEBUG

void gipc_msg_dbg(struct print_buf *buf, struct gipc_msg *msg, const char *str)
{
	u32 usr = msg_user(msg);
	gipc_printf(buf, str);

	switch (usr) {
	case MSG_BUNDLER:
		gipc_printf(buf, "BNDL::");
		gipc_printf(buf, "MSGS(%u):", msg_msgcnt(msg));
		break;
	case BCAST_PROTOCOL:
		gipc_printf(buf, "BCASTP::");
		break;
	case MSG_FRAGMENTER:
		gipc_printf(buf, "FRAGM::");
		switch (msg_type(msg)) {
		case FIRST_FRAGMENT:
			gipc_printf(buf, "FIRST:");
			break;
		case FRAGMENT:
			gipc_printf(buf, "BODY:");
			break;
		case LAST_FRAGMENT:
			gipc_printf(buf, "LAST:");
			break;
		default:
			gipc_printf(buf, "UNKNOWN:%x",msg_type(msg));

		}
		gipc_printf(buf, "NO(%u/%u):",msg_long_msgno(msg),
			    msg_fragm_no(msg));
		break;
	case GIPC_LOW_IMPORTANCE:
	case GIPC_MEDIUM_IMPORTANCE:
	case GIPC_HIGH_IMPORTANCE:
	case GIPC_CRITICAL_IMPORTANCE:
		gipc_printf(buf, "DAT%u:", msg_user(msg));
		if (msg_short(msg)) {
			gipc_printf(buf, "CON:");
			break;
		}
		switch (msg_type(msg)) {
		case GIPC_CONN_MSG:
			gipc_printf(buf, "CON:");
			break;
		case GIPC_MCAST_MSG:
			gipc_printf(buf, "MCST:");
			break;
		case GIPC_NAMED_MSG:
			gipc_printf(buf, "NAM:");
			break;
		case GIPC_DIRECT_MSG:
			gipc_printf(buf, "DIR:");
			break;
		default:
			gipc_printf(buf, "UNKNOWN TYPE %u",msg_type(msg));
		}
		if (msg_routed(msg) && !msg_non_seq(msg))
			gipc_printf(buf, "ROUT:");
		if (msg_reroute_cnt(msg))
			gipc_printf(buf, "REROUTED(%u):",
				    msg_reroute_cnt(msg));
		break;
	case NAME_DISTRIBUTOR:
		gipc_printf(buf, "NMD::");
		switch (msg_type(msg)) {
		case PUBLICATION:
			gipc_printf(buf, "PUBL(%u):", (msg_size(msg) - msg_hdr_sz(msg)) / 20);	/* Items */
			break;
		case WITHDRAWAL:
			gipc_printf(buf, "WDRW:");
			break;
		default:
			gipc_printf(buf, "UNKNOWN:%x",msg_type(msg));
		}
		if (msg_routed(msg))
			gipc_printf(buf, "ROUT:");
		if (msg_reroute_cnt(msg))
			gipc_printf(buf, "REROUTED(%u):",
				    msg_reroute_cnt(msg));
		break;
	case CONN_MANAGER:
		gipc_printf(buf, "CONN_MNG:");
		switch (msg_type(msg)) {
		case CONN_PROBE:
			gipc_printf(buf, "PROBE:");
			break;
		case CONN_PROBE_REPLY:
			gipc_printf(buf, "PROBE_REPLY:");
			break;
		case CONN_ACK:
			gipc_printf(buf, "CONN_ACK:");
			gipc_printf(buf, "ACK(%u):",msg_msgcnt(msg));
			break;
		default:
			gipc_printf(buf, "UNKNOWN TYPE:%x",msg_type(msg));
		}
		if (msg_routed(msg))
			gipc_printf(buf, "ROUT:");
		if (msg_reroute_cnt(msg))
			gipc_printf(buf, "REROUTED(%u):",msg_reroute_cnt(msg));
		break;
	case LINK_PROTOCOL:
		gipc_printf(buf, "PROT:TIM(%u):",msg_timestamp(msg));
		switch (msg_type(msg)) {
		case STATE_MSG:
			gipc_printf(buf, "STATE:");
			gipc_printf(buf, "%s:",msg_probe(msg) ? "PRB" :"");
			gipc_printf(buf, "NXS(%u):",msg_next_sent(msg));
			gipc_printf(buf, "GAP(%u):",msg_seq_gap(msg));
			gipc_printf(buf, "LSTBC(%u):",msg_last_bcast(msg));
			break;
		case RESET_MSG:
			gipc_printf(buf, "RESET:");
			if (msg_size(msg) != msg_hdr_sz(msg))
				gipc_printf(buf, "BEAR:%s:",msg_data(msg));
			break;
		case ACTIVATE_MSG:
			gipc_printf(buf, "ACTIVATE:");
			break;
		default:
			gipc_printf(buf, "UNKNOWN TYPE:%x",msg_type(msg));
		}
		gipc_printf(buf, "PLANE(%c):",msg_net_plane(msg));
		gipc_printf(buf, "SESS(%u):",msg_session(msg));
		break;
	case CHANGEOVER_PROTOCOL:
		gipc_printf(buf, "TUNL:");
		switch (msg_type(msg)) {
		case DUPLICATE_MSG:
			gipc_printf(buf, "DUPL:");
			break;
		case ORIGINAL_MSG:
			gipc_printf(buf, "ORIG:");
			gipc_printf(buf, "EXP(%u)",msg_msgcnt(msg));
			break;
		default:
			gipc_printf(buf, "UNKNOWN TYPE:%x",msg_type(msg));
		}
		break;
	case ROUTE_DISTRIBUTOR:
		gipc_printf(buf, "ROUTING_MNG:");
		switch (msg_type(msg)) {
		case EXT_ROUTING_TABLE:
			gipc_printf(buf, "EXT_TBL:");
			gipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		case LOCAL_ROUTING_TABLE:
			gipc_printf(buf, "LOCAL_TBL:");
			gipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		case SLAVE_ROUTING_TABLE:
			gipc_printf(buf, "DP_TBL:");
			gipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		case ROUTE_ADDITION:
			gipc_printf(buf, "ADD:");
			gipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		case ROUTE_REMOVAL:
			gipc_printf(buf, "REMOVE:");
			gipc_printf(buf, "TO:%x:",msg_remote_node(msg));
			break;
		default:
			gipc_printf(buf, "UNKNOWN TYPE:%x",msg_type(msg));
		}
		break;
	case LINK_CONFIG:
		gipc_printf(buf, "CFG:");
		switch (msg_type(msg)) {
		case DSC_REQ_MSG:
			gipc_printf(buf, "DSC_REQ:");
			break;
		case DSC_RESP_MSG:
			gipc_printf(buf, "DSC_RESP:");
			break;
		default:
			gipc_printf(buf, "UNKNOWN TYPE:%x:",msg_type(msg));
			break;
		}
		break;
	default:
		gipc_printf(buf, "UNKNOWN USER:");
	}

	switch (usr) {
	case CONN_MANAGER:
	case GIPC_LOW_IMPORTANCE:
	case GIPC_MEDIUM_IMPORTANCE:
	case GIPC_HIGH_IMPORTANCE:
	case GIPC_CRITICAL_IMPORTANCE:
		switch (msg_errcode(msg)) {
		case GIPC_OK:
			break;
		case GIPC_ERR_NO_NAME:
			gipc_printf(buf, "NO_NAME:");
			break;
		case GIPC_ERR_NO_PORT:
			gipc_printf(buf, "NO_PORT:");
			break;
		case GIPC_ERR_NO_NODE:
			gipc_printf(buf, "NO_PROC:");
			break;
		case GIPC_ERR_OVERLOAD:
			gipc_printf(buf, "OVERLOAD:");
			break;
		case GIPC_CONN_SHUTDOWN:
			gipc_printf(buf, "SHUTDOWN:");
			break;
		default:
			gipc_printf(buf, "UNKNOWN ERROR(%x):",
				    msg_errcode(msg));
		}
	default:{}
	}

	gipc_printf(buf, "HZ(%u):", msg_hdr_sz(msg));
	gipc_printf(buf, "SZ(%u):", msg_size(msg));
	gipc_printf(buf, "SQNO(%u):", msg_seqno(msg));

	if (msg_non_seq(msg))
		gipc_printf(buf, "NOSEQ:");
	else {
		gipc_printf(buf, "ACK(%u):", msg_ack(msg));
	}
	gipc_printf(buf, "BACK(%u):", msg_bcast_ack(msg));
	gipc_printf(buf, "PRND(%x)", msg_prevnode(msg));

	if (msg_isdata(msg)) {
		if (msg_named(msg)) {
			gipc_printf(buf, "NTYP(%u):", msg_nametype(msg));
			gipc_printf(buf, "NINST(%u)", msg_nameinst(msg));
		}
	}

	if ((usr != LINK_PROTOCOL) && (usr != LINK_CONFIG) &&
	    (usr != MSG_BUNDLER)) {
		if (!msg_short(msg)) {
			gipc_printf(buf, ":ORIG(%x:%u):",
				    msg_orignode(msg), msg_origport(msg));
			gipc_printf(buf, ":DEST(%x:%u):",
				    msg_destnode(msg), msg_destport(msg));
		} else {
			gipc_printf(buf, ":OPRT(%u):", msg_origport(msg));
			gipc_printf(buf, ":DPRT(%u):", msg_destport(msg));
		}
		if (msg_routed(msg) && !msg_non_seq(msg))
			gipc_printf(buf, ":TSEQN(%u)", msg_transp_seqno(msg));
	}
	if (msg_user(msg) == NAME_DISTRIBUTOR) {
		gipc_printf(buf, ":ONOD(%x):", msg_orignode(msg));
		gipc_printf(buf, ":DNOD(%x):", msg_destnode(msg));
		if (msg_routed(msg)) {
			gipc_printf(buf, ":CSEQN(%u)", msg_transp_seqno(msg));
		}
	}

	if (msg_user(msg) ==  LINK_CONFIG) {
		u32* raw = (u32*)msg;
		struct gipc_media_addr* orig = (struct gipc_media_addr*)&raw[5];
		gipc_printf(buf, ":REQL(%u):", msg_req_links(msg));
		gipc_printf(buf, ":DDOM(%x):", msg_dest_domain(msg));
		gipc_printf(buf, ":NETID(%u):", msg_bc_netid(msg));
		gipc_media_addr_printf(buf, orig);
	}
	if (msg_user(msg) == BCAST_PROTOCOL) {
		gipc_printf(buf, "BCNACK:AFTER(%u):", msg_bcgap_after(msg));
		gipc_printf(buf, "TO(%u):", msg_bcgap_to(msg));
	}
	gipc_printf(buf, "\n");
	if ((usr == CHANGEOVER_PROTOCOL) && (msg_msgcnt(msg))) {
		gipc_msg_dbg(buf, msg_get_wrapped(msg), "      /");
	}
	if ((usr == MSG_FRAGMENTER) && (msg_type(msg) == FIRST_FRAGMENT)) {
		gipc_msg_dbg(buf, msg_get_wrapped(msg), "      /");
	}
}

#endif
