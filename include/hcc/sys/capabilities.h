/**
 * Define HCC Capabilities
 * @author Innogrid HCC
 */

#ifndef _HCC_CAPABILITIES_H
#define _HCC_CAPABILITIES_H

enum {
       CAP_CHANGE_HCC_CAP = 0,
       CAP_CAN_MIGRATE,
       CAP_DISTANT_FORK,
       CAP_FORK_DELAY,
       CAP_CHECKPOINTABLE,
       CAP_USE_REMOTE_MEMORY,
       CAP_USE_INTRA_CLUSTER_KERSTREAMS,
       CAP_USE_INTER_CLUSTER_KERSTREAMS,
       CAP_USE_WORLD_VISIBLE_KERSTREAMS,
       CAP_SEE_LOCAL_PROC_STAT,
       CAP_DEBUG,
       CAP_SYSCALL_EXIT_HOOK,
       CAP_SIZE /* keep as last capability */
};

typedef struct hcc_cap_struct
{
	int hcc_cap_effective;
	int hcc_cap_permitted;
	int hcc_cap_inheritable_permitted;
	int hcc_cap_inheritable_effective;
} hcc_cap_t;

typedef struct hcc_cap_pid_desc
{
	pid_t pid;
	hcc_cap_t *caps;
} hcc_cap_pid_t;

#endif /* _HCC_CAPABILITIES_H */
