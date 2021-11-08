#include <linux/notifier.h>
#include <hcc/ghotplug.h>
#include <hcc/hccnodemask.h>
#include <hcc/sys/types.h>
#include <hcc/hccinit.h>

static void membership_online_add(hccnodemask_t *vector)
{
	hcc_node_t i;

	__for_each_hccnode_mask(i, vector){
		if(hccnode_online(i))
			continue;
		set_hccnode_online(i);
		hcc_nb_nodes++;
	}
}

static void membership_online_remove(hccnodemask_t *vector)
{
	hcc_node_t i;

	__for_each_hccnode_mask(i, vector){
		if(!hccnode_online(i))
			continue;
		clear_hccnode_online(i);
		hcc_nb_nodes--;
	}
}

static
int membership_online_notification(struct notifier_block *nb,
				   ghotplug_event_t event,
				   void *data)
{
	
	switch(event){
	case HOTPLUG_NOTIFY_ADD:{
		struct ghotplug_context *ctx = data;
		membership_online_add(&ctx->node_set.v);
		break;
	}

	case HOTPLUG_NOTIFY_REMOVE_LOCAL:{
		hcc_node_t node;
		for_each_online_hccnode(node)
			if(node != hcc_node_id)
				clear_hccnode_online(node);
	}
		
	case HOTPLUG_NOTIFY_REMOVE_ADVERT:{
		struct ghotplug_node_set *node_set = data;
		membership_online_remove(&node_set->v);
		break;
	}

	default:
		break;

	} /* switch */
	
	return NOTIFY_OK;
}

static
int membership_present_notification(struct notifier_block *nb,
				    ghotplug_event_t event, void *data)
{
	switch(event){
	default:
		break;
	} /* switch */

	return NOTIFY_OK;
}

int ghotplug_membership_init(void)
{
	register_ghotplug_notifier(membership_present_notification,
				  HOTPLUG_PRIO_MEMBERSHIP_PRESENT);
	register_ghotplug_notifier(membership_online_notification,
				  HOTPLUG_PRIO_MEMBERSHIP_ONLINE);
	return 0;
}

void ghotplug_membership_cleanup(void)
{
}
