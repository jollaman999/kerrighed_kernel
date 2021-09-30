/*
 *  hcc/scheduler/placement.c
 *
 *  Copyright (C) 2007-2008 Louis Rilling - Kerlabs
 *  Copyright (C) 2007 Marko Novak - Xlab
 */

#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <hcc/sys/types.h>
#include <hcc/scheduler/process_set.h>
#include <hcc/scheduler/policy.h>
#include <hcc/scheduler/scheduler.h>

struct task_struct;

static hcc_node_t scheduler_new_task_node(struct scheduler *scheduler,
						struct task_struct *parent)
{
	struct scheduler_policy *p;
	hcc_node_t node = KERRIGHED_NODE_ID_NONE;

	p = scheduler_get_scheduler_policy(scheduler);
	if (!p)
		goto out;
	node = scheduler_policy_new_task_node(p, parent);
	scheduler_policy_put(p);
out:
	return node;
}


/*
 * The parsing order of schedulers is:
 * - all universal scheduler in reversed attachment order (last attached to all
 *   processes is parsed first);
 * - all schedulers attached to parent, in reversed attachment order.
 *
 * The first scheduler returning a valid node id wins.
 */
hcc_node_t new_task_node(struct task_struct *parent)
{
	hcc_node_t node = KERRIGHED_NODE_ID_NONE;
	struct scheduler *s;
#define QUERY_SCHEDULER(s)				   \
		node = scheduler_new_task_node(s, parent); \
		if (node != KERRIGHED_NODE_ID_NONE) {	   \
			scheduler_put(s);		   \
			goto out;			   \
		}

	rcu_read_lock();
	do_each_scheduler_universal(s) {
		QUERY_SCHEDULER(s);
	} while_each_scheduler_universal(s);
	do_each_scheduler_task(s, parent) {
		QUERY_SCHEDULER(s);
	} while_each_scheduler_task(s, parent);
out:
	rcu_read_unlock();

	return node;
}
