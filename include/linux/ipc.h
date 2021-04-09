#ifndef _LINUX_IPC_H
#define _LINUX_IPC_H

#include <linux/spinlock.h>
#include <linux/uidgid.h>
#include <uapi/linux/ipc.h>

#ifdef CONFIG_KRG_IPC
struct krgipc_ops;
#endif

/* used by in-kernel data structures */
struct kern_ipc_perm
{
	spinlock_t	lock;
	bool		deleted;
	int		id;
	key_t		key;
	kuid_t		uid;
	kgid_t		gid;
	kuid_t		cuid;
	kgid_t		cgid;
	umode_t		mode; 
	unsigned long	seq;
	void		*security;
#ifdef CONFIG_KRG_IPC
	struct krgipc_ops *krgops;
#endif
};

#ifdef CONFIG_KRG_IPC
struct ipc_namespace;

bool ipc_used(struct ipc_namespace *ns);
void cleanup_ipc_objects (void);
#endif

#endif /* _LINUX_IPC_H */
