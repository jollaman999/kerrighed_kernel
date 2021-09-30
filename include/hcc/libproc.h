#ifndef __LIBPROC_H__
#define __LIBPROC_H__

#include <kddm/io_linker.h>

hcc_node_t global_pid_default_owner(struct kddm_set *set, objid_t objid,
					  const krgnodemask_t *nodes,
					  int nr_nodes);

#endif /* __LIBPROC_H__ */
