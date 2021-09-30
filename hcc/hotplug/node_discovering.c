#include <linux/module.h>

#include <hcc/sys/types.h>
#include <hcc/krgnodemask.h>
#include <hcc/krginit.h>
#include <hcc/hotplug.h>

krgnodemask_t krgnode_possible_map;
krgnodemask_t krgnode_present_map;
krgnodemask_t krgnode_online_map;
EXPORT_SYMBOL(krgnode_online_map);

struct universe_elem universe[KERRIGHED_MAX_NODES];

void krg_node_reachable(hcc_node_t);
void krg_node_unreachable(hcc_node_t);

void krg_node_arrival(hcc_node_t nodeid)
{
	printk("krg_node_arrival: nodeid = %d\n", nodeid);
	set_krgnode_present(nodeid);
	krg_node_reachable(nodeid);
#ifdef CONFIG_KRG_HOTPLUG
	universe[nodeid].state = 1;
#endif
}

void krg_node_departure(hcc_node_t nodeid)
{
	printk("krg_node_departure: nodeid = %d\n", nodeid);
#ifdef CONFIG_KRG_HOTPLUG
	universe[nodeid].state = 0;
#endif
	clear_krgnode_present(nodeid);
	krg_node_unreachable(nodeid);
}

void init_node_discovering(void)
{
	int i;

	krgnodes_setall(krgnode_possible_map);
	krgnodes_clear(krgnode_present_map);
	krgnodes_clear(krgnode_online_map);
	
#ifdef CONFIG_KRG_HOTPLUG
	for (i = 0; i < KERRIGHED_MAX_NODES; i++) {
		universe[i].state = 0;
		universe[i].subid = -1;
	}
#endif

	if (ISSET_KRG_INIT_FLAGS(KRG_INITFLAGS_NODEID)) {
#ifdef CONFIG_KRG_HOTPLUG
		universe[hcc_node_id].state = 1;
#endif
		set_krgnode_present(hcc_node_id);
	}
}
