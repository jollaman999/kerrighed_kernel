#ifndef __HCC_SERVICES__
#define __HCC_SERVICES__

#include <linux/ioctl.h>
#include <hcc/types.h>
#include <hcc/hccnodemask.h>

/*--------------------------------------------------------------------------*
 *                                                                          *
 *                                 MACROS                                   *
 *                                                                          *
 *--------------------------------------------------------------------------*/

#define HCC_PROC_MAGIC 0xD1

#define TOOLS_PROC_BASE 0
#define COMM_PROC_BASE 32
#define KERMM_PROC_BASE 96
#define KERPROC_PROC_BASE 128
#define EPM_PROC_BASE 192
#define IPC_PROC_BASE 224

/*
 * Tools related HCC syscalls
 */

#define KSYS_SET_CAP          _IOW(HCC_PROC_MAGIC, \
                                   TOOLS_PROC_BASE + 0, \
                                   hcc_cap_t )

#define KSYS_GET_CAP          _IOR(HCC_PROC_MAGIC, \
                                   TOOLS_PROC_BASE + 1, \
                                   hcc_cap_t )

#define KSYS_SET_PID_CAP      _IOW(HCC_PROC_MAGIC, \
                                   TOOLS_PROC_BASE + 2, \
                                   hcc_cap_pid_t )

#define KSYS_GET_PID_CAP      _IOR(HCC_PROC_MAGIC, \
                                   TOOLS_PROC_BASE + 3, \
                                   hcc_cap_pid_t )

#define KSYS_SET_FATHER_CAP   _IOW(HCC_PROC_MAGIC, \
                                   TOOLS_PROC_BASE + 4, \
                                   hcc_cap_t )

#define KSYS_GET_FATHER_CAP   _IOR(HCC_PROC_MAGIC, \
                                   TOOLS_PROC_BASE + 5, \
                                   hcc_cap_t )

#define KSYS_NB_MAX_NODES     _IOR(HCC_PROC_MAGIC, \
                                   TOOLS_PROC_BASE + 6,  \
				   int)
#define KSYS_NB_MAX_CLUSTERS  _IOR(HCC_PROC_MAGIC, \
                                   TOOLS_PROC_BASE + 7,  \
				   int)

#define KSYS_GET_SUPPORTED_CAP	_IOR(HCC_PROC_MAGIC, \
				     TOOLS_PROC_BASE + 8, \
				     int)

/*
 * Communications related HCC syscalls
 */

#define KSYS_GET_NODE_ID       _IOR(HCC_PROC_MAGIC, \
				    COMM_PROC_BASE + 0, \
                                    int)
#define KSYS_GET_NODES_COUNT   _IOR(HCC_PROC_MAGIC, \
				    COMM_PROC_BASE + 1,   \
                                    int)
/* Removed: #define KSYS_HOTPLUG_START     _IOW(HCC_PROC_MAGIC, \ */
/*                                              COMM_PROC_BASE + 3,   \ */
/*                                              __hccnodemask_t) */
#define KSYS_HOTPLUG_RESTART   _IOW(HCC_PROC_MAGIC, \
                                    COMM_PROC_BASE + 4,   \
                                    __hccnodemask_t)
#define KSYS_HOTPLUG_SHUTDOWN  _IOW(HCC_PROC_MAGIC, \
                                    COMM_PROC_BASE + 5,   \
                                    __hccnodemask_t)
#define KSYS_HOTPLUG_REBOOT    _IOW(HCC_PROC_MAGIC, \
                                    COMM_PROC_BASE + 6,   \
                                    __hccnodemask_t)
#define KSYS_HOTPLUG_STATUS    _IOR(HCC_PROC_MAGIC, \
                                    COMM_PROC_BASE + 7,   \
				    struct hotplug_clusters)
#define KSYS_HOTPLUG_ADD       _IOW(HCC_PROC_MAGIC, \
                                    COMM_PROC_BASE + 8,   \
                                    struct __hotplug_node_set)
#define KSYS_HOTPLUG_REMOVE    _IOW(HCC_PROC_MAGIC, \
                                    COMM_PROC_BASE + 9,   \
                                    struct __hotplug_node_set)
#define KSYS_HOTPLUG_FAIL      _IOW(HCC_PROC_MAGIC, \
                                    COMM_PROC_BASE + 10,   \
                                    struct __hotplug_node_set)
#define KSYS_HOTPLUG_NODES     _IOWR(HCC_PROC_MAGIC, \
                                     COMM_PROC_BASE + 11,  \
				     struct hotplug_nodes)
#define KSYS_HOTPLUG_POWEROFF  _IOW(HCC_PROC_MAGIC, \
                                    COMM_PROC_BASE + 12,  \
				    struct __hotplug_node_set)
/* Removed: #define KSYS_HOTPLUG_WAIT_FOR_START _IO(HCC_PROC_MAGIC, \ */
/*                                                  COMM_PROC_BASE + 13) */
#define KSYS_HOTPLUG_SET_CREATOR	_IO(HCC_PROC_MAGIC, \
					    COMM_PROC_BASE + 14)
#define KSYS_HOTPLUG_READY		_IO(HCC_PROC_MAGIC, \
					    COMM_PROC_BASE + 15)


/*
 *  Memory related HCC syscalls
 */

#define KSYS_CHANGE_MAP_LOCAL_VALUE  _IOW(HCC_PROC_MAGIC, \
			                  KERMM_PROC_BASE + 0, \
					   struct kermm_new_local_data)

/*
 * Enhanced Process Management related hcc syscalls
 */

#define KSYS_PROCESS_MIGRATION         _IOW(HCC_PROC_MAGIC, \
                                            EPM_PROC_BASE + 0, \
                                            migration_infos_t)
#define KSYS_THREAD_MIGRATION	       _IOW(HCC_PROC_MAGIC, \
                                            EPM_PROC_BASE + 1,\
                                            migration_infos_t)
#define KSYS_APP_FREEZE                _IOW(HCC_PROC_MAGIC, \
                                            EPM_PROC_BASE + 2, \
                                            struct checkpoint_info)
#define KSYS_APP_UNFREEZE              _IOW(HCC_PROC_MAGIC, \
                                            EPM_PROC_BASE + 3, \
                                            struct checkpoint_info)
#define KSYS_APP_CHKPT                 _IOW(HCC_PROC_MAGIC, \
                                            EPM_PROC_BASE + 4, \
                                            struct checkpoint_info)
#define KSYS_APP_RESTART               _IOW(HCC_PROC_MAGIC, \
                                            EPM_PROC_BASE + 5, \
                                            struct restart_request)
#define KSYS_APP_SET_USERDATA          _IOW(HCC_PROC_MAGIC, \
                                            EPM_PROC_BASE + 6, \
                                            __u64)
#define KSYS_APP_GET_USERDATA          _IOW(HCC_PROC_MAGIC, \
                                            EPM_PROC_BASE + 7, \
                                            struct app_userdata_request)
#define KSYS_APP_CR_DISABLE		_IO(HCC_PROC_MAGIC, \
					   EPM_PROC_BASE + 8)
#define KSYS_APP_CR_ENABLE		_IO(HCC_PROC_MAGIC, \
					   EPM_PROC_BASE + 9)
#define KSYS_APP_CR_EXCLUDE		_IOW(HCC_PROC_MAGIC,	\
					     EPM_PROC_BASE + 10,	\
					     struct cr_mm_region)


/*
 * IPC related hcc syscalls
 */
#define KSYS_IPC_MSGQ_CHKPT            _IOW(HCC_PROC_MAGIC,       \
					    IPC_PROC_BASE + 0,		\
					    int[2])
#define KSYS_IPC_MSGQ_RESTART          _IOW(HCC_PROC_MAGIC, \
					    IPC_PROC_BASE + 1,	  \
					    int)
#define KSYS_IPC_SEM_CHKPT             _IOW(HCC_PROC_MAGIC,       \
					    IPC_PROC_BASE + 2,		\
					    int[2])
#define KSYS_IPC_SEM_RESTART           _IOW(HCC_PROC_MAGIC, \
					    IPC_PROC_BASE + 3,	  \
					    int)
#define KSYS_IPC_SHM_CHKPT             _IOW(HCC_PROC_MAGIC,       \
					    IPC_PROC_BASE + 4,		\
					    int[2])
#define KSYS_IPC_SHM_RESTART           _IOW(HCC_PROC_MAGIC, \
					    IPC_PROC_BASE + 5,	  \
					    int)

/*
 * HotPlug
 */

struct hotplug_nodes {
	char *nodes;
};

struct hotplug_clusters {
	char clusters[HCC_MAX_CLUSTERS];
};

/* __hotplug_node_set is the ioctl parameter (sized by HCC_HARD_MAX_NODES)
   hotplug_node_set is the structure actually used by kernel (size by HCC_MAX_NODES)
*/
struct __hotplug_node_set {
	int subclusterid;
	__hccnodemask_t v;
};


/*
 * Ctnr
 */
typedef struct kermm_new_local_data {
	unsigned long data_place;
} kermm_new_local_data_t;

#endif
