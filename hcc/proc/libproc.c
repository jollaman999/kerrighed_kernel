/*
 *  hcc/proc/libproc.c
 *
 *  Copyright (C) 2007 Louis Rilling - Kerlabs
 */

#include <hcc/sys/types.h>
#include <hcc/krginit.h>
#include <hcc/pid.h>
#include <gdm/io_linker.h>

/* Generic function to assign a default owner to a pid-named gdm object */
hcc_node_t global_pid_default_owner(struct gdm_set *set, objid_t objid,
					  const krgnodemask_t *nodes,
					  int nr_nodes)
{
	hcc_node_t node;

	BUG_ON(!(objid & GLOBAL_PID_MASK));
	node = ORIG_NODE(objid);
	if (node < 0 || node >= KERRIGHED_MAX_NODES)
		/* Invalid ID */
		node = hcc_node_id;
	if (node != hcc_node_id
	    && unlikely(!__krgnode_isset(node, nodes)))
		node = __next_krgnode_in_ring(node, nodes);
	return node;
}
