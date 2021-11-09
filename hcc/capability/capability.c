/*
 *  hcc/capability/capability.c
 *
 *  Copyright (C) 1999-2006 INRIA, Universite de Rennes 1, EDF
 *  Copyright (C) 2019-2021 Innogrid HCC.
 */

#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/cred.h>
#include <linux/pid_namespace.h>
#include <linux/rcupdate.h>
#include <hcc/capabilities.h>
#ifdef CONFIG_HCC_GPM
#include <linux/pid_namespace.h>
#include <hcc/children.h>
#endif
#include <linux/uaccess.h>

#include <hcc/hcc_syscalls.h>
#include <hcc/hcc_services.h>
#include <hcc/remote_cred.h>
#ifdef CONFIG_HCC_PROC
#include <hcc/remote_syscall.h>
#include <net/grpc/grpc.h>
#include <net/grpc/grpcid.h>
#endif

int can_use_hcc_cap(struct task_struct *task, int cap)
{
	return (cap_raised(task->hcc_caps.effective, cap)
		&& !atomic_read(&task->hcc_cap_unavailable[cap])
		&& !atomic_read(&task->hcc_cap_unavailable_private[cap]));
}

void hcc_cap_fork(struct task_struct *task, unsigned long clone_flags)
{
	kernel_hcc_cap_t *caps = &current->hcc_caps;
	kernel_hcc_cap_t *new_caps = &task->hcc_caps;
	kernel_cap_t new_hcc_effective;
	int i;

#ifdef CONFIG_HCC_GPM
	if (hcc_current && !in_hcc_do_fork())
		/* Migration/restart: do not recompute hcc caps */
		return;
#endif

	/*
	 * Compute the new capabilities and reset the private
	 * hcc_cap_unavailable array
	 */
	new_hcc_effective = cap_intersect(caps->inheritable_effective,
					  caps->inheritable_permitted);

	new_caps->permitted = caps->inheritable_permitted;
	new_caps->effective = new_hcc_effective;

	for (i = 0; i < CAP_SIZE; i++)
		atomic_set(&task->hcc_cap_unavailable_private[i], 0);
	/* The other fields have been inherited by copy. */
}

int hcc_cap_prepare_binprm(struct linux_binprm *bprm)
{
	/* The model needs changes with filesystem support ... */
#if 0
	cap_clear(bprm->hcc_cap_forced);
	cap_set_full(bprm->hcc_cap_permitted);
	cap_set_full(bprm->hcc_cap_effective);
#endif /* 0 */
	return 0;
}

void hcc_cap_finish_exec(struct linux_binprm *bprm)
{
	/* The model needs changes with filesystem support ... */
#if 0
	kernel_hcc_cap_t *caps = &current->hcc_caps;
	kernel_cap_t new_hcc_permitted, new_hcc_effective;

	task_lock(current);
	new_hcc_permitted = cap_intersect(caps->inheritable_permitted,
					  bprm->hcc_cap_permitted);
	new_hcc_permitted = cap_combine(new_hcc_permitted,
					bprm->hcc_cap_forced);

	new_hcc_effective = cap_intersect(bprm->hcc_cap_effective,
					  new_hcc_permitted);
	new_hcc_effective = cap_intersect(caps->inheritable_effective,
					  new_hcc_effective);

	caps->permitted = new_hcc_permitted;
	caps->effective = new_hcc_effective;
	task_unlock(current);
#endif /* 0 */
}

static int hcc_set_cap(struct task_struct *tsk,
		       const kernel_hcc_cap_t *requested_cap)
{
	kernel_hcc_cap_t *caps = &tsk->hcc_caps;
	kernel_cap_t tmp_cap;
	struct nsproxy *nsp;
	int res;
	int i;

	res = 0;
	rcu_read_lock();
	nsp = rcu_dereference(tsk->nsproxy);
	if (!nsp || !nsp->hcc_ns)
		res = -EPERM;
	rcu_read_unlock();
	if (res)
		goto out;

	res = -EINVAL;
	if (!cap_issubset(requested_cap->effective, requested_cap->permitted)
	    || !cap_issubset(requested_cap->inheritable_permitted,
			     requested_cap->permitted)
	    || !cap_issubset(requested_cap->inheritable_effective,
			     requested_cap->inheritable_permitted))
		goto out;

	res = -ENOSYS;
	tmp_cap = HCC_GCAP_SUPPORTED;
	if (!cap_issubset(requested_cap->permitted, tmp_cap))
		goto out;

	res = -EPERM;
	if (!permissions_ok(tsk))
		goto out;

	task_lock(tsk);

	if (!cap_raised(caps->effective, CAP_CHANGE_HCC_GCAP))
		goto out_unlock;

	res = -EBUSY;
	for (i = 0; i < CAP_SIZE; i++)
		if (atomic_read(&tsk->hcc_cap_used[i])
		    && !cap_raised(requested_cap->effective, i))
			goto out_unlock;

	tmp_cap = cap_intersect(caps->permitted, requested_cap->permitted);
	caps->permitted = tmp_cap;
	tmp_cap = cap_intersect(caps->permitted, requested_cap->effective);
	caps->effective = tmp_cap;
	tmp_cap = cap_intersect(caps->permitted,
				requested_cap->inheritable_effective);
	caps->inheritable_effective = tmp_cap;
	tmp_cap = cap_intersect(caps->permitted,
				requested_cap->inheritable_permitted);
	caps->inheritable_permitted = tmp_cap;

	res = 0;

out_unlock:
	task_unlock(tsk);

out:
	return res;
}

#ifdef CONFIG_HCC_PROC
static int remote_set_pid_cap(pid_t pid, const kernel_hcc_cap_t *cap);
#endif

static int hcc_set_father_cap(struct task_struct *tsk,
			      const kernel_hcc_cap_t *requested_cap)
{
	int retval = 0;

	read_lock(&tasklist_lock);
#ifdef CONFIG_HCC_GPM
	if (tsk->real_parent != baby_sitter) {
#endif
		retval = hcc_set_cap(tsk->real_parent, requested_cap);
		read_unlock(&tasklist_lock);
#ifdef CONFIG_HCC_GPM
	} else {
		struct children_gdm_object *parent_children_obj;
		pid_t real_parent_tgid;
		pid_t parent_pid, real_parent_pid;
		int retval;

		read_unlock(&tasklist_lock);

		parent_children_obj =
			hcc_parent_children_readlock(tsk, &real_parent_tgid);
		if (!parent_children_obj)
			/* Parent is init. Do not change init's capabilities! */
			return -EPERM;
		hcc_get_parent(parent_children_obj, tsk,
			       &parent_pid, &real_parent_pid);
		retval = remote_set_pid_cap(real_parent_pid, requested_cap);
		hcc_children_unlock(parent_children_obj);
	}
#endif

	return retval;
}

static int hcc_set_pid_cap(pid_t pid, const kernel_hcc_cap_t *requested_cap)
{
	struct task_struct *tsk;
	int retval = -ESRCH;

	rcu_read_lock();
	tsk = find_task_by_vpid(pid);
	if (tsk)
		retval = hcc_set_cap(tsk, requested_cap);
	rcu_read_unlock();
#ifdef CONFIG_HCC_PROC
	if (!tsk)
		retval = remote_set_pid_cap(pid, requested_cap);
#endif

	return retval;
}

#ifdef CONFIG_HCC_PROC
static int handle_set_pid_cap(struct grpc_desc* desc, void *_msg, size_t size)
{
	struct pid *pid;
	kernel_hcc_cap_t cap;
	const struct cred *old_cred;
	int ret;

	pid = hcc_handle_remote_syscall_begin(desc, _msg, size,
					      &cap, &old_cred);
	if (IS_ERR(pid)) {
		ret = PTR_ERR(pid);
		goto out;
	}

	ret = hcc_set_cap(pid_task(pid, PIDTYPE_PID), &cap);

	hcc_handle_remote_syscall_end(pid, old_cred);

out:
	return ret;
}

static int remote_set_pid_cap(pid_t pid, const kernel_hcc_cap_t *cap)
{
	return hcc_remote_syscall_simple(PROC_SET_PID_CAP, pid,
					 cap, sizeof(*cap));
}
#endif /* CONFIG_HCC_PROC */

static int hcc_get_cap(struct task_struct *tsk, kernel_hcc_cap_t *resulting_cap)
{
	kernel_hcc_cap_t *caps = &tsk->hcc_caps;
	int res;

	task_lock(tsk);

	if (resulting_cap && permissions_ok(tsk)) {
		*resulting_cap = *caps;
		res = 0;
	} else {
		res = -EPERM;
	}

	task_unlock(tsk);

	return res;
}

#ifdef CONFIG_HCC_PROC
static int remote_get_pid_cap(pid_t pid, kernel_hcc_cap_t *cap);
#endif

static int hcc_get_father_cap(struct task_struct *son,
			      kernel_hcc_cap_t *resulting_cap)
{
	int retval = 0;

	read_lock(&tasklist_lock);
#ifdef CONFIG_HCC_GPM
	if (son->real_parent != baby_sitter) {
#endif
		retval = hcc_get_cap(son->real_parent, resulting_cap);
		read_unlock(&tasklist_lock);
#ifdef CONFIG_HCC_GPM
	} else {
		struct children_gdm_object *parent_children_obj;
		pid_t real_parent_tgid;
		pid_t parent_pid, real_parent_pid;
		int retval;

		read_unlock(&tasklist_lock);

		parent_children_obj =
			hcc_parent_children_readlock(son, &real_parent_tgid);
		if (!parent_children_obj)
			/* Parent is init. */
			return hcc_get_cap(task_active_pid_ns(son)->child_reaper,
					   resulting_cap);
		hcc_get_parent(parent_children_obj, son,
			       &parent_pid, &real_parent_pid);
		retval = remote_get_pid_cap(real_parent_pid, resulting_cap);
		hcc_children_unlock(parent_children_obj);
	}
#endif

	return retval;
}

int can_parent_inherite_hcc_cap(struct task_struct *son, int cap)
{
	int retval = 0;

	if (son->real_parent != baby_sitter) {
		retval = (cap_raised(son->real_parent->hcc_caps.effective, cap) &
			cap_raised(son->real_parent->hcc_caps.inheritable_effective, cap));
	} else {
		kernel_hcc_cap_t pcap;

		hcc_get_father_cap(son, &pcap);
		retval = (cap_raised(pcap.effective, cap) &
			cap_raised(pcap.inheritable_effective, cap));
	}

	return retval;
}

static int hcc_get_pid_cap(pid_t pid, kernel_hcc_cap_t *resulting_cap)
{
	struct task_struct *tsk;
	int retval = -ESRCH;

	rcu_read_lock();
	tsk = find_task_by_vpid(pid);
	if (tsk)
		retval = hcc_get_cap(tsk, resulting_cap);
	rcu_read_unlock();
#ifdef CONFIG_HCC_PROC
	if (!tsk)
		retval = remote_get_pid_cap(pid, resulting_cap);
#endif

	return retval;
}

#ifdef CONFIG_HCC_PROC
static int handle_get_pid_cap(struct grpc_desc *desc, void *_msg, size_t size)
{
	struct pid *pid;
	kernel_hcc_cap_t cap;
	const struct cred *old_cred;
	int ret;

	pid = hcc_handle_remote_syscall_begin(desc, _msg, size,
					      NULL, &old_cred);
	if (IS_ERR(pid)) {
		ret = PTR_ERR(pid);
		goto out;
	}

	ret = hcc_get_cap(pid_task(pid, PIDTYPE_PID), &cap);
	if (ret)
		goto out_end;

	ret = grpc_pack_type(desc, cap);
	if (ret)
		goto err_cancel;

out_end:
	hcc_handle_remote_syscall_end(pid, old_cred);

out:
	return ret;

err_cancel:
	rpc_cancel(desc);
	goto out_end;
}

static int remote_get_pid_cap(pid_t pid, kernel_hcc_cap_t *cap)
{
	struct grpc_desc *desc;
	int err = -ESRCH;
	int res;

	desc = hcc_remote_syscall_begin(PROC_GET_PID_CAP, pid, NULL, 0);
	if (IS_ERR(desc)) {
		err = PTR_ERR(desc);
		goto out;
	}

	err = grpc_unpack_type(desc, res);
	if (err)
		goto err_cancel;
	if (res) {
		err = res;
		goto out_end;
	}
	err = grpc_unpack_type(desc, *cap);
	if (err)
		goto err_cancel;

out_end:
	hcc_remote_syscall_end(desc, pid);

out:
	return err;

err_cancel:
	rpc_cancel(desc);
	if (err > 0)
		err = -EPIPE;
	goto out_end;
}
#endif /* CONFIG_HCC_PROC */

/* HCC syscalls interface */

static int user_to_kernel_hcc_cap(const hcc_cap_t __user *user_caps,
				  kernel_hcc_cap_t *caps)
{
	hcc_cap_t ucaps;

	if (copy_from_user(&ucaps, user_caps, sizeof(ucaps)))
		return -EFAULT;

	BUILD_BUG_ON(sizeof(kernel_cap_t) != 2 * sizeof(__u32));

	caps->permitted = (kernel_cap_t){{ ucaps.hcc_cap_permitted, 0 }};
	caps->effective = (kernel_cap_t){{ ucaps.hcc_cap_effective, 0 }};
	caps->inheritable_permitted =
		(kernel_cap_t){{ ucaps.hcc_cap_inheritable_permitted, 0 }};
	caps->inheritable_effective =
		(kernel_cap_t){{ ucaps.hcc_cap_inheritable_effective, 0 }};

	return 0;
}

static int proc_set_pid_cap(void __user *arg)
{
	struct hcc_cap_pid_desc desc;
	kernel_hcc_cap_t caps;
	int r = -EFAULT;

	if (copy_from_user(&desc, arg, sizeof(desc)))
		goto out;

	if (user_to_kernel_hcc_cap(desc.caps, &caps))
		goto out;

	r = hcc_set_pid_cap(desc.pid, &caps);

out:
	return r;
}

static int proc_set_father_cap(void __user *arg)
{
	kernel_hcc_cap_t caps;
	int r;

	r = user_to_kernel_hcc_cap(arg, &caps);
	if (!r)
		r = hcc_set_father_cap(current, &caps);

	return r;
}

static int proc_set_cap(void __user *arg)
{
	kernel_hcc_cap_t caps;
	int r;

	r = user_to_kernel_hcc_cap(arg, &caps);
	if (!r)
		r = hcc_set_cap(current, &caps);

	return r;
}

static int kernel_to_user_hcc_cap(const kernel_hcc_cap_t *caps,
				  hcc_cap_t __user *user_caps)
{
	hcc_cap_t ucaps;
	int r = 0;

	ucaps.hcc_cap_permitted = caps->permitted.cap[0];
	ucaps.hcc_cap_effective = caps->effective.cap[0];
	ucaps.hcc_cap_inheritable_permitted =
		caps->inheritable_permitted.cap[0];
	ucaps.hcc_cap_inheritable_effective =
		caps->inheritable_effective.cap[0];

	if (copy_to_user(user_caps, &ucaps, sizeof(ucaps)))
		r = -EFAULT;

	return r;
}

static int proc_get_cap(void __user *arg)
{
	kernel_hcc_cap_t caps;
	int r;

	r = hcc_get_cap(current, &caps);
	if (!r)
		r = kernel_to_user_hcc_cap(&caps, arg);

	return r;
}

static int proc_get_father_cap(void __user *arg)
{
	kernel_hcc_cap_t caps;
	int r;

	r = hcc_get_father_cap(current, &caps);
	if (!r)
		r = kernel_to_user_hcc_cap(&caps, arg);

	return r;
}

static int proc_get_pid_cap(void __user *arg)
{
	struct hcc_cap_pid_desc desc;
	kernel_hcc_cap_t caps;
	int r = -EFAULT;

	BUG_ON(sizeof(int) != sizeof(pid_t));

	if (copy_from_user(&desc, arg, sizeof(desc)))
		goto out;

	r = hcc_get_pid_cap(desc.pid, &caps);

	if (!r)
		r = kernel_to_user_hcc_cap(&caps, desc.caps);

out:
	return r;
}

static int proc_get_supported_cap(void __user *arg)
{
	int __user *set = arg;
	return put_user(HCC_GCAP_SUPPORTED.cap[0], set);
}

int init_hcc_cap(void)
{
	int r;

	r = register_proc_service(KSYS_SET_CAP, proc_set_cap);
	if (r != 0)
		goto out;

	r = register_proc_service(KSYS_GET_CAP, proc_get_cap);
	if (r != 0)
		goto unreg_set_cap;

	r = register_proc_service(KSYS_SET_FATHER_CAP, proc_set_father_cap);
	if (r != 0)
		goto unreg_get_cap;

	r = register_proc_service(KSYS_GET_FATHER_CAP, proc_get_father_cap);
	if (r != 0)
		goto unreg_set_father_cap;

	r = register_proc_service(KSYS_SET_PID_CAP, proc_set_pid_cap);
	if (r != 0)
		goto unreg_get_father_cap;

	r = register_proc_service(KSYS_GET_PID_CAP, proc_get_pid_cap);
	if (r != 0)
		goto unreg_set_pid_cap;

	r = register_proc_service(KSYS_GET_SUPPORTED_CAP,
				  proc_get_supported_cap);
	if (r != 0)
		goto unreg_get_pid_cap;

#ifdef CONFIG_HCC_PROC
	rpc_register_int(PROC_GET_PID_CAP, handle_get_pid_cap, 0);
	rpc_register_int(PROC_SET_PID_CAP, handle_set_pid_cap, 0);
#endif

 out:
	return r;

 unreg_get_pid_cap:
	unregister_proc_service(KSYS_GET_PID_CAP);
 unreg_set_pid_cap:
	unregister_proc_service(KSYS_SET_PID_CAP);
 unreg_get_father_cap:
	unregister_proc_service(KSYS_GET_FATHER_CAP);
 unreg_set_father_cap:
	unregister_proc_service(KSYS_SET_FATHER_CAP);
 unreg_get_cap:
	unregister_proc_service(KSYS_GET_CAP);
 unreg_set_cap:
	unregister_proc_service(KSYS_SET_CAP);
	goto out;
}

void cleanup_hcc_cap(void)
{
	unregister_proc_service(KSYS_GET_SUPPORTED_CAP);
	unregister_proc_service(KSYS_GET_PID_CAP);
	unregister_proc_service(KSYS_SET_PID_CAP);
	unregister_proc_service(KSYS_GET_FATHER_CAP);
	unregister_proc_service(KSYS_SET_FATHER_CAP);
	unregister_proc_service(KSYS_GET_CAP);
	unregister_proc_service(KSYS_SET_CAP);

	return;
}
