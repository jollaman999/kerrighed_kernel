#ifndef __HCC_SCHEDULER_INFO_H__
#define __HCC_SCHEDULER_INFO_H__

#ifdef CONFIG_HCC_SCHED

#include <linux/list.h>

struct module;
struct task_struct;
struct epm_action;
struct ghost;

struct hcc_sched_module_info_type {
	struct list_head list;		/* reserved for hcc_sched_info */
	struct list_head instance_head;	/* subsystem internal */
	const char *name;
	struct module *owner;
	/* can block */
	struct hcc_sched_module_info *(*copy)(struct task_struct *,
					      struct hcc_sched_module_info *);
	/* may be called from interrupt context */
	void (*free)(struct hcc_sched_module_info *);
	/* can block */
	int (*export)(struct epm_action *, struct ghost *,
		      struct hcc_sched_module_info *);
	/* can block */
	struct hcc_sched_module_info *(*import)(struct epm_action *,
						struct ghost *,
						struct task_struct *);
};

/* struct to include in module specific task hcc_sched_info struct */
/* modification is reserved for hcc_sched_info subsystem internal */
struct hcc_sched_module_info {
	struct list_head info_list;
	struct list_head instance_list;
	struct hcc_sched_module_info_type *type;
};

int hcc_sched_module_info_register(struct hcc_sched_module_info_type *type);
/*
 * must only be called at module unloading (See comment in
 * hcc_sched_info_copy())
 */
void hcc_sched_module_info_unregister(struct hcc_sched_module_info_type *type);
/* Must be called under rcu_read_lock() */
struct hcc_sched_module_info *
hcc_sched_module_info_get(struct task_struct *task,
			  struct hcc_sched_module_info_type *type);

/* fork() / exit() */
extern int hcc_sched_info_copy(struct task_struct *tsk);
extern void hcc_sched_info_free(struct task_struct *tsk);

#endif /* CONFIG_HCC_SCHED */

#endif /* __HCC_SCHEDULER_INFO_H__ */
