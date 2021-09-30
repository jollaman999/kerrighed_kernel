#ifndef __KRGINIT_H__
#define __KRGINIT_H__

#include <hcc/types.h>
#include <linux/rwsem.h>

enum hcc_init_flags_t {
	KRG_INITFLAGS_NODEID,
	KRG_INITFLAGS_SESSIONID,
	KRG_INITFLAGS_AUTONODEID,
};

/* Tools */
extern hcc_node_t hcc_node_id;
extern hcc_node_t hcc_nb_nodes;
extern hcc_node_t hcc_nb_nodes_min;
extern hcc_session_t hcc_session_id;
extern hcc_subsession_t hcc_subsession_id;
extern int hcc_init_flags;
extern struct rw_semaphore hcc_init_sem;

#define SET_KRG_INIT_FLAGS(p) hcc_init_flags |= (1<<p)
#define CLR_KRG_INIT_FLAGS(p) hcc_init_flags &= ~(1<<p)
#define ISSET_KRG_INIT_FLAGS(p) (hcc_init_flags & (1<<p))

#endif
