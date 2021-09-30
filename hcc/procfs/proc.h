#ifndef KRG_PROCFS_H
#define KRG_PROCFS_H

#include <hcc/sys/types.h>

/*--------------------------------------------------------------------------*
 *                                                                          *
 *                              EXTERN FUNCTIONS                            *
 *                                                                          *
 *--------------------------------------------------------------------------*/

int krg_procfs_init(void);
int krg_procfs_finalize(void);

int create_proc_node_info(hcc_node_t node);
int remove_proc_node_info(hcc_node_t node);

#endif /* KRG_PROCFS_H */
