/**
 * Define HCC Capabilities (not exported outside kernel)
 * @author Jean Parpaillon (c) Inria 2006
 */

#ifndef _HCC_CAPABILITIES_H_INTERNAL
#define _HCC_CAPABILITIES_H_INTERNAL

#ifdef CONFIG_HCC_CAP

#include <linux/capability.h>
#include <hcc/sys/capabilities.h>

typedef struct kernel_hcc_cap_struct {
	kernel_cap_t effective;
	kernel_cap_t permitted;
	kernel_cap_t inheritable_permitted;
	kernel_cap_t inheritable_effective;
} kernel_hcc_cap_t;

/*
 * MACROS
 */
#define __HCC_CAP_SUPPORTED_BASE CAP_TO_MASK(CAP_CHANGE_HCC_CAP)
#ifdef CONFIG_CLUSTER_WIDE_PROC_INFRA
#define __HCC_CAP_SUPPORTED_PROCFS CAP_TO_MASK(CAP_SEE_LOCAL_PROC_STAT)
#else
#define __HCC_CAP_SUPPORTED_PROCFS 0
#endif
#ifdef CONFIG_HCC_MM
#define __HCC_CAP_SUPPORTED_MM CAP_TO_MASK(CAP_USE_REMOTE_MEMORY)
#else
#define __HCC_CAP_SUPPORTED_MM 0
#endif
#ifdef CONFIG_HCC_EPM
#define __HCC_CAP_SUPPORTED_EPM CAP_TO_MASK(CAP_CAN_MIGRATE)	 \
				|CAP_TO_MASK(CAP_DISTANT_FORK)   \
				|CAP_TO_MASK(CAP_CHECKPOINTABLE)
#else
#define __HCC_CAP_SUPPORTED_EPM 0
#endif
#define __HCC_CAP_SUPPORTED_DEBUG 0
#ifdef CONFIG_HCC_SYSCALL_EXIT_HOOK
#define __HCC_CAP_SUPPORTED_SEH CAP_TO_MASK(CAP_SYSCALL_EXIT_HOOK)
#else
#define __HCC_CAP_SUPPORTED_SEH 0
#endif

#if _KERNEL_CAPABILITY_U32S != 2
#error Fix up hand-coded capability macro initializers
#endif

#define HCC_CAP_SUPPORTED ((kernel_cap_t){{ __HCC_CAP_SUPPORTED_BASE   \
					   |__HCC_CAP_SUPPORTED_PROCFS \
					   |__HCC_CAP_SUPPORTED_MM     \
					   |__HCC_CAP_SUPPORTED_EPM    \
					   |__HCC_CAP_SUPPORTED_DEBUG  \
					   |__HCC_CAP_SUPPORTED_SEH, 0 }})

#define HCC_CAP_INIT_PERM_SET HCC_CAP_SUPPORTED
#define HCC_CAP_INIT_EFF_SET \
	((kernel_cap_t){{ CAP_TO_MASK(CAP_CHANGE_HCC_CAP), 0 }})
#define HCC_CAP_INIT_INH_PERM_SET HCC_CAP_INIT_PERM_SET
#define HCC_CAP_INIT_INH_EFF_SET HCC_CAP_INIT_EFF_SET

struct task_struct;
struct linux_binprm;

int can_use_hcc_cap(struct task_struct *task, int cap);
int can_parent_inherite_hcc_cap(struct task_struct *task, int cap);

void hcc_cap_fork(struct task_struct *task, unsigned long clone_flags);
int hcc_cap_prepare_binprm(struct linux_binprm *bprm);
void hcc_cap_finish_exec(struct linux_binprm *bprm);

#endif /* CONFIG_HCC_CAP */

#endif /* _HCC_CAPABILITIES_H_INTERNAL */
