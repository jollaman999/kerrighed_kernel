/** GDM module initialization.
 *  @file gdm.c
 *
 *  Implementation of functions used to initialize and finalize the
 *  GDM module. It also implements some device file system functions for
 *  testing purpose.
 *
 *  Copyright (C) 2001-2006, INRIA, Universite de Rennes 1, EDF.
 *  Copyright (C) 2006-2007, Renaud Lottiaux, Kerlabs.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <hcc/hotplug.h>
#include <gdm/gdm.h>
#include <gdm/object_server.h>
#include "procfs.h"
#include "protocol_action.h"
#include <gdm/name_space.h>
#include <gdm/gdm_set.h>
#include "gdm_bench.h"

#ifndef CONFIG_KRG_MONOLITHIC
MODULE_AUTHOR ("Renaud Lottiaux");
MODULE_DESCRIPTION ("Kerrighed Distributed Data Manager");
MODULE_LICENSE ("GPL");
#endif

event_counter_t total_get_object_counter = 0;
event_counter_t total_grab_object_counter = 0;
event_counter_t total_remove_object_counter = 0;
event_counter_t total_flush_object_counter = 0;

int (*kh_copy_gdm_info)(unsigned long clone_flags, struct task_struct * tsk);

struct kmem_cache *gdm_info_cachep;

int gdm_hotplug_init(void);
void gdm_hotplug_cleanup(void);


/** Initialize the gdm field of the krg_task field of the given task.
 *  @author  Renaud Lottiaux
 *
 *  @param tsk   Task to fill the gdm struct.
 */
int initialize_gdm_info_struct (struct task_struct *task)
{
	struct gdm_info_struct *gdm_info;

	gdm_info = kmem_cache_alloc (gdm_info_cachep, GFP_KERNEL);
	if (!gdm_info)
		return -ENOMEM;

	gdm_info->get_object_counter = 0;
	gdm_info->grab_object_counter = 0;
	gdm_info->remove_object_counter = 0;
	gdm_info->flush_object_counter = 0;
	gdm_info->wait_obj = NULL;

	task->gdm_info = gdm_info;

	return 0;
}



int kcb_copy_gdm_info(unsigned long clone_flags, struct task_struct * tsk)
{
	return initialize_gdm_info_struct(tsk);
}



/** Initialisation of the GDM sub-system module.
 *  @author Renaud Lottiaux
 */
int init_gdm (void)
{
	printk ("GDM initialisation : start\n");

        gdm_info_cachep = KMEM_CACHE(gdm_info_struct, SLAB_PANIC);

	gdm_ns_init();

	io_linker_init();

	gdm_set_init();

	init_gdm_objects();

	procfs_gdm_init ();

	object_server_init ();

	start_run_queue_thread ();

	hook_register(&kh_copy_gdm_info, kcb_copy_gdm_info);

	gdm_hotplug_init();

	init_gdm_test ();

	/*
	  process_add(0, hcc_nb_nodes);
	  process_synchronize(0);
	  process_remove(0);
	*/

	krgsyms_register (KRGSYMS_GDM_TREE_OPS, &gdm_tree_set_ops);

	printk ("GDM initialisation done\n");

	return 0;
}



/** Cleanup of the GDM sub-system.
 *  @author Renaud Lottiaux
 */
void cleanup_gdm (void)
{
	printk ("GDM termination : start\n");

	krgsyms_unregister (KRGSYMS_GDM_TREE_OPS);

	gdm_hotplug_cleanup();

	stop_run_queue_thread ();

	procfs_gdm_finalize ();

	object_server_finalize ();

	gdm_set_finalize();

	io_linker_finalize();

	gdm_ns_finalize();

	printk ("GDM termination done\n");
}
