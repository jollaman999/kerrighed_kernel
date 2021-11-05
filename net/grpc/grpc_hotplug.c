/*
 *  Copyright (C) 2019-2021 Innogrid HCC.
 */

#include <linux/notifier.h>
#include <linux/kernel.h>
#include <hcc/krgnodemask.h>
#include <net/krgrpc/rpcid.h>
#include <net/krgrpc/rpc.h>
#include <hcc/hotplug.h>

#include "rpc_internal.h"

static void rpc_remove(krgnodemask_t * vector)
{
	printk("Have to send all the tx_queue before stopping the node\n");
};


/**
 *
 * Notifier related part
 *
 */

#ifdef CONFIG_KERRIGHED
static int rpc_notification(struct notifier_block *nb, hotplug_event_t event,
			    void *data){
	struct hotplug_node_set *node_set = data;
	
	switch(event){
	case HOTPLUG_NOTIFY_REMOVE:
		rpc_remove(&node_set->v);
		break;
	default:
		break;
	}
	
	return NOTIFY_OK;
};
#endif

int rpc_hotplug_init(void){
#ifdef CONFIG_KERRIGHED
	register_hotplug_notifier(rpc_notification, HOTPLUG_PRIO_RPC);
#endif
	return 0;
};

void rpc_hotplug_cleanup(void){
};
