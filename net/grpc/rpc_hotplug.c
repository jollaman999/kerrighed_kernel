/*
 *  Copyright (C) 2019-2021 Innogrid HCC.
 */

#include <linux/notifier.h>
#include <linux/kernel.h>
#include <hcc/hccnodemask.h>
#include <net/grpc/grpcid.h>
#include <net/grpc/grpc.h>
#include <hcc/ghotplug.h>

#include "rpc_internal.h"

static void rpc_remove(hccnodemask_t * vector)
{
	printk("Have to send all the tx_queue before stopping the node\n");
};


/**
 *
 * Notifier related part
 *
 */

#ifdef CONFIG_HCC
static int rpc_notification(struct notifier_block *nb, ghotplug_event_t event,
			    void *data){
	struct hotplug_node_set *node_set = data;
	
	switch(event){
	case GHOTPLUG_NOTIFY_REMOVE:
		rpc_remove(&node_set->v);
		break;
	default:
		break;
	}
	
	return NOTIFY_OK;
};
#endif

int rpc_hotplug_init(void){
#ifdef CONFIG_HCC
	register_ghotplug_notifier(rpc_notification, GHOTPLUG_PRIO_RPC);
#endif
	return 0;
};

void rpc_hotplug_cleanup(void){
};
