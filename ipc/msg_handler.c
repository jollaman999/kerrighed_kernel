/** All the code for IPC messages accross the cluster
 *  @file msg_handler.c
 *
 *  Copyright (C) 2001-2006, INRIA, Universite de Rennes 1, EDF.
 *  Copyright (C) 2007-2008 Matthieu Fertré - INRIA
 */

#ifndef NO_MSG

#include <linux/ipc.h>
#include <linux/ipc_namespace.h>
#include <linux/msg.h>
#include <linux/syscalls.h>
#include <linux/remote_sleep.h>

#include <kddm/kddm.h>
#include <net/krgrpc/rpc.h>
#include <kerrighed/hotplug.h>
#include <kerrighed/remote_cred.h>
#include "ipc_handler.h"
#include "msg_handler.h"
#include "msg_io_linker.h"
#include "ipcmap_io_linker.h"
#include "util.h"
#include "krgmsg.h"
#include "krgipc_mobility.h"

struct msgkrgops {
	struct krgipc_ops krgops;
	struct kddm_set *master_kddm_set;
};

struct kddm_set *krgipc_ops_master_set(struct krgipc_ops *ipcops)
{
	struct msgkrgops *msgops;

	msgops = container_of(ipcops, struct msgkrgops, krgops);

	return msgops->master_kddm_set;
}

/*****************************************************************************/
/*                                                                           */
/*                                KERNEL HOOKS                               */
/*                                                                           */
/*****************************************************************************/

static struct kern_ipc_perm *kcb_ipc_msg_lock(struct ipc_ids *ids, int id)
{
	msq_object_t *msq_object;
	struct msg_queue *msq;
	int index;

	index = ipcid_to_idx(id);

	msq_object = _kddm_grab_object_no_ft(ids->krgops->data_kddm_set, index);

	if (!msq_object)
		goto error;

	msq = msq_object->local_msq;

	BUG_ON(!msq);

	spin_lock(&msq->q_perm.lock);

	BUG_ON(msq->q_perm.deleted);

	return &(msq->q_perm);

error:
	_kddm_put_object(ids->krgops->data_kddm_set, index);

	return ERR_PTR(-EINVAL);
}

static void kcb_ipc_msg_unlock(struct kern_ipc_perm *ipcp)
{
	int index;
	long task_state;

	/*
	 * We may enter in interruptible state, and kddm_put_object might
	 * schedule and reset to running.
	 * Fortunately, wakeup only happens with ipcp->mutex held, so we can
	 * restore the state right before mutex_unlock.
	 */
	task_state = current->state;

	index = ipcid_to_idx(ipcp->id);

	_kddm_put_object(ipcp->krgops->data_kddm_set, index);

	__set_current_state(task_state);

	spin_unlock(&ipcp->lock);
}

static struct kern_ipc_perm *kcb_ipc_msg_findkey(struct ipc_ids *ids, key_t key)
{
	long *key_index;
	int id = -1;

	key_index = _kddm_get_object_no_ft(ids->krgops->key_kddm_set, key);

	if (key_index)
		id = *key_index;

	_kddm_put_object(ids->krgops->key_kddm_set, key);

	if (id != -1)
		return kcb_ipc_msg_lock(ids, id);

	return NULL;
}

/** Notify the creation of a new IPC msg queue to Kerrighed.
 *
 *  @author Matthieu Fertré
 */
int krg_ipc_msg_newque(struct ipc_namespace *ns, struct msg_queue *msq)
{
	struct kddm_set *master_set;
	msq_object_t *msq_object;
	kerrighed_node_t *master_node;
	long *key_index;
	int index, err = 0;

	BUG_ON(!msg_ids(ns).krgops);

	index = ipcid_to_idx(msq->q_perm.id);

	msq_object = _kddm_grab_object_manual_ft(
		msg_ids(ns).krgops->data_kddm_set, index);

	BUG_ON(msq_object);

	msq_object = kmem_cache_alloc(msq_object_cachep, GFP_KERNEL);
	if (!msq_object) {
		err = -ENOMEM;
		goto err_put;
	}

	msq_object->local_msq = msq;
	msq_object->local_msq->master_node = kerrighed_node_id;
	msq_object->mobile_msq.q_perm.id = -1;
	INIT_LIST_HEAD(&msq_object->mobile_msq.q_messages);
	INIT_LIST_HEAD(&msq_object->mobile_msq.q_receivers);
	INIT_LIST_HEAD(&msq_object->mobile_msq.q_senders);

	_kddm_set_object(msg_ids(ns).krgops->data_kddm_set, index, msq_object);

	if (msq->q_perm.key != IPC_PRIVATE)
	{
		key_index = _kddm_grab_object(msg_ids(ns).krgops->key_kddm_set,
					      msq->q_perm.key);
		*key_index = index;
		_kddm_put_object(msg_ids(ns).krgops->key_kddm_set,
				 msq->q_perm.key);
	}

	master_set = krgipc_ops_master_set(msg_ids(ns).krgops);

	master_node = _kddm_grab_object(master_set, index);
	*master_node = kerrighed_node_id;

	msq->q_perm.krgops = msg_ids(ns).krgops;

	_kddm_put_object(master_set, index);

err_put:
	_kddm_put_object(msg_ids(ns).krgops->data_kddm_set, index);

	return err;
}

void krg_ipc_msg_freeque(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp)
{
	int index;
	key_t key;
	struct kddm_set *master_set;
	struct msg_queue *msq = container_of(ipcp, struct msg_queue, q_perm);

	index = ipcid_to_idx(msq->q_perm.id);
	key = msq->q_perm.key;

	if (key != IPC_PRIVATE) {
		_kddm_grab_object_no_ft(ipcp->krgops->key_kddm_set, key);
		_kddm_remove_frozen_object(ipcp->krgops->key_kddm_set, key);
	}

	master_set = krgipc_ops_master_set(ipcp->krgops);

	_kddm_grab_object_no_ft(master_set, index);
	_kddm_remove_frozen_object(master_set, index);

	local_msg_unlock(msq);

	_kddm_remove_frozen_object(ipcp->krgops->data_kddm_set, index);

	krg_ipc_rmid(&msg_ids(ns), index);
}

/*****************************************************************************/

DEFINE_REMOTE_SLEEPERS_QUEUE(msg_remote_sleepers);

struct msgsnd_msg
{
	kerrighed_node_t requester;
	int msqid;
	int msgflg;
	long mtype;
	pid_t tgid;
	size_t msgsz;
};

long krg_ipc_msgsnd(int msqid, long mtype, void __user *mtext,
		    size_t msgsz, int msgflg, struct ipc_namespace *ns,
		    pid_t tgid)
{
	struct rpc_desc * desc;
	struct kddm_set *master_set;
	kerrighed_node_t* master_node;
	void *buffer;
	long r;
	int err;
	int index;
	struct msgsnd_msg msg;

	down_read(&krgipcmsg_rwsem);

	index = ipcid_to_idx(msqid);

	master_set = krgipc_ops_master_set(msg_ids(ns).krgops);

	master_node = _kddm_get_object_no_ft(master_set, index);
	if (!master_node) {
		_kddm_put_object(master_set, index);
		r = -EINVAL;
		goto exit;
	}

	if (*master_node == kerrighed_node_id) {
		/* inverting the following 2 lines can conduct to deadlock
		 * if the send is blocked */
		_kddm_put_object(master_set, index);
		r = __do_msgsnd(msqid, mtype, mtext, msgsz,
				msgflg, ns, tgid);
		goto exit;
	}

	msg.requester = kerrighed_node_id;
	msg.msqid = msqid;
	msg.mtype = mtype;
	msg.msgflg = msgflg;
	msg.tgid = tgid;
	msg.msgsz = msgsz;

	buffer = kmalloc(msgsz, GFP_KERNEL);
	if (!buffer) {
		r = -ENOMEM;
		goto exit;
	}

	r = copy_from_user(buffer, mtext, msgsz);
	if (r)
		goto exit_free_buffer;

	desc = rpc_begin(IPC_MSG_SEND, master_set->ns->rpc_comm, *master_node);
	_kddm_put_object(master_set, index);

	r = rpc_pack_type(desc, msg);
	if (r)
		goto cancel;

	r = pack_creds(desc, current_cred());
	if (r)
		goto cancel;

	r = rpc_pack(desc, 0, buffer, msgsz);
	if (r)
		goto cancel;

	r = unpack_remote_sleep_res_prepare(desc);
	if (r)
		goto cancel;

	err = unpack_remote_sleep_res_type(desc, r);
	if (err) {
		r = err;
		goto cancel;
	}

exit_rpc:
	rpc_end(desc, 0);
exit_free_buffer:
	kfree(buffer);
exit:
	up_read(&krgipcmsg_rwsem);

	return r;

cancel:
	rpc_cancel(desc);
	goto exit_rpc;
}

static void handle_do_msg_send(struct rpc_desc *desc, void *_msg, size_t size)
{
	void *mtext = NULL;
	int err;
	long r;
	struct msgsnd_msg *msg = _msg;
	struct ipc_namespace *ns;
	const struct cred *old_cred = NULL;
	DEFINE_REMOTE_SLEEPERS_WAIT(wait);

	ns = find_get_krg_ipcns();
	BUG_ON(!ns);

	old_cred = unpack_override_creds(desc);
	if (IS_ERR(old_cred)) {
		old_cred = NULL;
		goto cancel;
	}

	mtext = kmalloc(msg->msgsz, GFP_KERNEL);
	if (!mtext)
		goto cancel;

	err = rpc_unpack(desc, 0, mtext, msg->msgsz);
	if (err)
		goto cancel;

	err = remote_sleep_prepare(desc, &msg_remote_sleepers, &wait);
	if (err) {
		if (err == -ERESTARTSYS) {
			r = err;
			goto pack_res;
		}
		goto cancel;
	}

	r = __do_msgsnd(msg->msqid, msg->mtype, mtext, msg->msgsz, msg->msgflg,
			ns, msg->tgid);

	remote_sleep_finish(&msg_remote_sleepers, &wait);

pack_res:
	err = rpc_pack_type(desc, r);
	if (err)
		goto cancel;

exit:
	if (mtext)
		kfree(mtext);

	if (old_cred)
		revert_creds(old_cred);

	put_ipc_ns(ns);
	return;

cancel:
	rpc_cancel(desc);
	goto exit;
}

struct msgrcv_msg
{
	kerrighed_node_t requester;
	int msqid;
	int msgflg;
	long msgtyp;
	pid_t tgid;
	size_t msgsz;
};

long krg_ipc_msgrcv(int msqid, long *pmtype, void __user *mtext,
		    size_t msgsz, long msgtyp, int msgflg,
		    struct ipc_namespace *ns, pid_t tgid)
{
	struct rpc_desc * desc;
	struct kddm_set *master_set;
	kerrighed_node_t *master_node;
	void *buffer = NULL;
	long r;
	int err;
	int index;
	struct msgrcv_msg msg;

	down_read(&krgipcmsg_rwsem);

	/* TODO: manage ipc namespace */
	index = ipcid_to_idx(msqid);

	master_set = krgipc_ops_master_set(msg_ids(ns).krgops);

	master_node = _kddm_get_object_no_ft(master_set, index);
	if (!master_node) {
		_kddm_put_object(master_set, index);
		r = -EINVAL;
		goto exit;
	}

	if (*master_node == kerrighed_node_id) {
		/*inverting the following 2 lines can conduct to deadlock
		 * if the receive is blocked */
		_kddm_put_object(master_set, index);
		r = __do_msgrcv(msqid, pmtype, mtext, msgsz, msgtyp,
				msgflg, ns, tgid);
		goto exit;
	}

	msg.requester = kerrighed_node_id;
	msg.msqid = msqid;
	msg.msgtyp = msgtyp;
	msg.msgflg = msgflg;
	msg.tgid = tgid;
	msg.msgsz = msgsz;

	desc = rpc_begin(IPC_MSG_RCV, master_set->ns->rpc_comm, *master_node);
	_kddm_put_object(master_set, index);

	err = rpc_pack_type(desc, msg);
	if (err)
		goto cancel;

	err = pack_creds(desc, current_cred());
	if (err)
		goto cancel;

	err = unpack_remote_sleep_res_prepare(desc);
	if (err)
		goto cancel;

	err = unpack_remote_sleep_res_type(desc, r);
	if (err)
		goto cancel;

	if (r < 0)
		goto exit_end_rpc;

	/* get the real msg type */
	err = rpc_unpack(desc, 0, pmtype, sizeof(long));
	if (err)
		goto cancel;

	buffer = kmalloc(r, GFP_KERNEL);
	if (!buffer) {
		err = -ENOMEM;
		goto cancel;
	}

	err = rpc_unpack(desc, 0, buffer, r);
	if (err)
		goto cancel;

	err = copy_to_user(mtext, buffer, r);
	if (err)
		goto cancel;

exit_end_rpc:
	if (buffer)
		kfree(buffer);
	rpc_end(desc, 0);
exit:
	up_read(&krgipcmsg_rwsem);
	return r;

cancel:
	r = err;
	if (r == -ECANCELED)
		r = -EPIPE;
	rpc_cancel(desc);
	goto exit_end_rpc;
}

static void handle_do_msg_rcv(struct rpc_desc *desc, void *_msg, size_t size)
{
	void *mtext = NULL;
	long msgsz, pmtype;
	int r;
	struct msgrcv_msg *msg = _msg;
	struct ipc_namespace *ns;
	const struct cred *old_cred = NULL;
	DEFINE_REMOTE_SLEEPERS_WAIT(wait);

	ns = find_get_krg_ipcns();
	BUG_ON(!ns);

	old_cred = unpack_override_creds(desc);
	if (IS_ERR(old_cred)) {
		old_cred = NULL;
		goto cancel;
	}

	mtext = kmalloc(msg->msgsz, GFP_KERNEL);
	if (!mtext)
		goto cancel;

	r = remote_sleep_prepare(desc, &msg_remote_sleepers, &wait);
	if (r) {
		if (r == -ERESTARTSYS) {
			msgsz = -ERESTARTSYS;
			goto pack_res;
		}
		goto cancel;
	}

	msgsz = __do_msgrcv(msg->msqid, &pmtype, mtext, msg->msgsz,
			    msg->msgtyp, msg->msgflg, ns, msg->tgid);

	remote_sleep_finish(&msg_remote_sleepers, &wait);

pack_res:
	r = rpc_pack_type(desc, msgsz);
	if (r)
		goto cancel;

	if (msgsz <= 0)
		goto exit;

	r = rpc_pack_type(desc, pmtype); /* send the real type of msg */
	if (r)
		goto cancel;

	r = rpc_pack(desc, 0, mtext, msgsz);
	if (r)
		goto cancel;

exit:
	if (mtext)
		kfree(mtext);

	if (old_cred)
		revert_creds(old_cred);

	put_ipc_ns(ns);
	return;

cancel:
	rpc_cancel(desc);
	goto exit;
}


/*****************************************************************************/
/*                                                                           */
/*                              INITIALIZATION                               */
/*                                                                           */
/*****************************************************************************/

static int ipc_flusher(struct kddm_set *set, objid_t objid,
		       struct kddm_obj *obj_entry, void *data)
{
	kerrighed_node_t node;

	/*
	 * TODO: choose a better node to flush to.
	 * This may be done looking at the processes blocked.
	 */
	node = first_krgnode(krgnode_online_map);

	return node;
}

static int flush_one_msg_queue(struct ipc_namespace *ns, struct msg_queue *msq)
{
	int index;
	struct kddm_set *data_set, *node_set;
	kerrighed_node_t dest;

	index = ipcid_to_idx(msq->q_perm.id);
	data_set = msg_ids(ns).krgops->data_kddm_set;
	node_set = krgipc_ops_master_set(msg_ids(ns).krgops);

	dest = first_krgnode(krgnode_online_map);

	if (msq->master_node == kerrighed_node_id) {
		msq_object_t *msq_object;
		kerrighed_node_t *master_node;

		msq_object = _kddm_grab_object_no_ft(data_set, index);
		BUG_ON(!msq_object);
		_kddm_put_object(data_set, index);

		master_node = _kddm_grab_object_no_ft(node_set, index);
		BUG_ON(!master_node);
		*master_node = dest;
		_kddm_put_object(node_set, index);
	}

	_kddm_flush_object(data_set, index, dest);
	_kddm_flush_object(node_set, index, dest);

	return 0;
}

static int flush_msg_queues(struct ipc_namespace *ns)
{
	struct kern_ipc_perm *perm;
	struct msg_queue *msq;
	struct ipc_ids *ids;
	struct msgkrgops *msgops;
	kerrighed_node_t node;
	int err, total, in_use, next_id;

	node = first_krgnode(krgnode_online_map);

	ids = &msg_ids(ns);
	msgops = container_of(ids->krgops, struct msgkrgops, krgops);

	in_use = ids->in_use;

	err = 0;

	for (total = 0, next_id = 0; total < in_use; next_id++) {
		perm = idr_find(&ids->ipcs_idr, next_id);
		if (!perm)
			continue;

		msq = container_of(perm, struct msg_queue, q_perm);
		err = flush_one_msg_queue(ns, msq);
		if (err)
			goto error;

		total++;
	}

error:
	return err;
}

int krg_msg_flush_set(struct ipc_namespace *ns)
{
	int err;
	struct msgkrgops *msgops;

	down_write(&msg_ids(ns).rw_mutex);

	msgops = container_of(msg_ids(ns).krgops, struct msgkrgops, krgops);

	err = flush_msg_queues(ns);
	if (err)
		goto error;

	_kddm_flush_set(msgops->krgops.map_kddm_set, ipc_flusher, NULL);
	_kddm_flush_set(msgops->krgops.key_kddm_set, ipc_flusher, NULL);
	_kddm_flush_set(msgops->master_kddm_set, ipc_flusher, NULL);

error:
	up_write(&msg_ids(ns).rw_mutex);
	return err;
}

int krg_msg_init_ns(struct ipc_namespace *ns)
{
	int r;

	struct msgkrgops *msg_ops = kmalloc(sizeof(struct msgkrgops),
					    GFP_KERNEL);
	if (!msg_ops) {
		r = -ENOMEM;
		goto err;
	}

	msg_ops->krgops.map_kddm_set = create_new_kddm_set(
		kddm_def_ns, MSGMAP_KDDM_ID, IPCMAP_LINKER,
		KDDM_RR_DEF_OWNER, sizeof(ipcmap_object_t),
		KDDM_LOCAL_EXCLUSIVE);

	if (IS_ERR(msg_ops->krgops.map_kddm_set)) {
		r = PTR_ERR(msg_ops->krgops.map_kddm_set);
		goto err_map;
	}

	msg_ops->krgops.key_kddm_set = create_new_kddm_set(
		kddm_def_ns, MSGKEY_KDDM_ID, MSGKEY_LINKER,
		KDDM_RR_DEF_OWNER, sizeof(long),
		KDDM_LOCAL_EXCLUSIVE);

	if (IS_ERR(msg_ops->krgops.key_kddm_set)) {
		r = PTR_ERR(msg_ops->krgops.key_kddm_set);
		goto err_key;
	}

	msg_ops->krgops.data_kddm_set = create_new_kddm_set(
		kddm_def_ns, MSG_KDDM_ID, MSG_LINKER,
		KDDM_RR_DEF_OWNER, sizeof(msq_object_t),
		KDDM_LOCAL_EXCLUSIVE | KDDM_NEED_SAFE_WALK);

	if (IS_ERR(msg_ops->krgops.data_kddm_set)) {
		r = PTR_ERR(msg_ops->krgops.data_kddm_set);
		goto err_data;
	}

	msg_ops->master_kddm_set = create_new_kddm_set(
		kddm_def_ns, MSGMASTER_KDDM_ID, MSGMASTER_LINKER,
		KDDM_RR_DEF_OWNER, sizeof(kerrighed_node_t),
		KDDM_LOCAL_EXCLUSIVE);

	if (IS_ERR(msg_ops->master_kddm_set)) {
		r = PTR_ERR(msg_ops->master_kddm_set);
		goto err_master;
	}

	msg_ops->krgops.ipc_lock = kcb_ipc_msg_lock;
	msg_ops->krgops.ipc_unlock = kcb_ipc_msg_unlock;
	msg_ops->krgops.ipc_findkey = kcb_ipc_msg_findkey;

	msg_ids(ns).krgops = &msg_ops->krgops;

	return 0;

err_master:
	_destroy_kddm_set(msg_ops->krgops.data_kddm_set);
err_data:
	_destroy_kddm_set(msg_ops->krgops.key_kddm_set);
err_key:
	_destroy_kddm_set(msg_ops->krgops.map_kddm_set);
err_map:
	kfree(msg_ops);
err:
	return r;
}

void krg_msg_exit_ns(struct ipc_namespace *ns)
{
	if (msg_ids(ns).krgops) {
		struct msgkrgops *msg_ops;

		msg_ops = container_of(msg_ids(ns).krgops, struct msgkrgops,
				       krgops);

		_destroy_kddm_set(msg_ops->krgops.map_kddm_set);
		_destroy_kddm_set(msg_ops->krgops.key_kddm_set);
		_destroy_kddm_set(msg_ops->krgops.data_kddm_set);
		_destroy_kddm_set(msg_ops->master_kddm_set);

		kfree(msg_ops);
	}
}

void msg_handler_init(void)
{
	msq_object_cachep = kmem_cache_create("msg_queue_object",
					      sizeof(msq_object_t),
					      0, SLAB_PANIC, NULL);

	register_io_linker(MSG_LINKER, &msq_linker);
	register_io_linker(MSGKEY_LINKER, &msqkey_linker);
	register_io_linker(MSGMASTER_LINKER, &msqmaster_linker);

	rpc_register_void(IPC_MSG_SEND, handle_do_msg_send, 0);
	rpc_register_void(IPC_MSG_RCV, handle_do_msg_rcv, 0);
	rpc_register_void(IPC_MSG_CHKPT, handle_msg_checkpoint, 0);
}



void msg_handler_finalize(void)
{
}

#endif
