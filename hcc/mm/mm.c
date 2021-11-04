/** KerMM module initialization.
 *  @file mm.c
 *
 *  Copyright (C) 2001-2006, INRIA, Universite de Rennes 1, EDF.
 *  Copyright (C) 2006-2009, Renaud Lottiaux, Kerlabs.
 *
 *  Implementation of functions used to initialize and finalize the
 *  kermm module.
 */

#include <linux/mm.h>
#include <asm/mman.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <hcc/krgsyms.h>
#include <hcc/mm.h>
#include <hcc/hotplug.h>
#include <hcc/page_table_tree.h>
#include <gdm/gdm.h>
#include "mm_struct.h"
#include "memory_int_linker.h"
#include "memory_io_linker.h"
#include "mm_struct_io_linker.h"
#include "mm_server.h"
#include "injection.h"


/** Initialisation of the DSM module.
 *  @author Renaud Lottiaux
 *
 *  Start object server, object manager and gdm set manager threads.
 *  Register kermm services in the /proc/hcc/services.
 */
int init_kermm(void)
{
	printk("KerMM initialisation : start\n");

	krgsyms_register (KRGSYMS_VM_OPS_NULL, &null_vm_ops);
	krgsyms_register (KRGSYMS_VM_OPS_FILE_GENERIC, (void *)&generic_file_vm_ops);
	special_mapping_vm_ops_krgsyms_register ();
	krgsyms_register (KRGSYMS_VM_OPS_MEMORY_GDM_VMOPS,
			  &anon_memory_gdm_vmops);

	krgsyms_register (KRGSYMS_ARCH_UNMAP_AREA, arch_unmap_area);
	krgsyms_register (KRGSYMS_ARCH_UNMAP_AREA_TOPDOWN,
			  arch_unmap_area_topdown);
	krgsyms_register (KRGSYMS_ARCH_GET_UNMAP_AREA, arch_get_unmapped_area);
	krgsyms_register (KRGSYMS_ARCH_GET_UNMAP_AREA_TOPDOWN,
			  arch_get_unmapped_area_topdown);
	krgsyms_register (KRGSYMS_ARCH_GET_UNMAP_EXEC_AREA, arch_get_unmapped_exec_area);

	krgsyms_register (KRGSYMS_GDM_PT_OPS, &gdm_pt_set_ops);

	register_io_linker (MEMORY_LINKER, &memory_linker);
	register_io_linker (MM_STRUCT_LINKER, &mm_struct_io_linker);

	mm_struct_init ();
	mm_server_init();
	mm_injection_init();

	printk ("KerMM initialisation done\n");

	return 0;
}



/** Cleanup of the DSM module.
 *  @author Renaud Lottiaux
 *
 *  Kill object manager, object server and gdm set manager threads.
 */
void cleanup_kermm (void)
{
	printk ("KerMM termination : start\n");

	mm_injection_finalize();
	mm_server_finalize();
	mm_struct_finalize();

	krgsyms_unregister (KRGSYMS_VM_OPS_FILE_GENERIC);
	special_mapping_vm_ops_krgsyms_unregister ();
	krgsyms_unregister (KRGSYMS_VM_OPS_MEMORY_GDM_VMOPS);
	krgsyms_unregister (KRGSYMS_ARCH_UNMAP_AREA);
	krgsyms_unregister (KRGSYMS_ARCH_UNMAP_AREA_TOPDOWN);
	krgsyms_unregister (KRGSYMS_ARCH_GET_UNMAP_AREA);
	krgsyms_unregister (KRGSYMS_ARCH_GET_UNMAP_AREA_TOPDOWN);
	krgsyms_unregister (KRGSYMS_ARCH_GET_UNMAP_EXEC_AREA);

	printk ("KerMM termination done\n");
}
