/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Intel Corporation. All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/nospec.h>

#include <rdma/ib_cache.h>

#include "core_priv.h"

struct ib_pkey_cache {
	int             table_len;
	struct pkey_cache_table_entry {
		u16             pkey;
		unsigned char	index;
	}		entry[0];
};

struct ib_gid_cache {
	int             table_len;
	struct gid_cache_table_entry {
		union ib_gid    gid;
		unsigned char	index;
	}		entry[0];
};

struct ib_update_work {
	struct work_struct work;
	struct ib_device  *device;
	u8                 port_num;
};

static inline int start_port(struct ib_device *device)
{
	return (device->node_type == RDMA_NODE_IB_SWITCH) ? 0 : 1;
}

static inline int end_port(struct ib_device *device)
{
	return (device->node_type == RDMA_NODE_IB_SWITCH) ?
		0 : device->phys_port_cnt;
}

int ib_get_cached_gid(struct ib_device *device,
		      u8                port_num,
		      int               index,
		      union ib_gid     *gid)
{
	struct ib_gid_cache *cache;
	unsigned long flags;
	int i, ret = 0;
	u8 idx;

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	idx = array_index_nospec(port_num - start_port(device),
				 end_port(device) - start_port(device) + 1);
	cache = device->cache.gid_cache[idx];

	if (index < 0 || index >= cache->table_len) {
		ret = -EINVAL;
		goto out_unlock;
	}

	for (i = 0; i < cache->table_len; ++i)
		if (cache->entry[i].index == index)
			break;

	if (i < cache->table_len) {
		gmb();
		*gid = cache->entry[i].gid;
	} else {
		gmb();
		ret = ib_query_gid(device, port_num, index, gid);
		if (ret)
			printk(KERN_WARNING "ib_query_gid failed (%d) for %s (index %d)\n",
			       ret, device->name, index);
	}

out_unlock:
	read_unlock_irqrestore(&device->cache.lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_get_cached_gid);

int ib_find_cached_gid(struct ib_device *device,
		       union ib_gid	*gid,
		       u8               *port_num,
		       u16              *index)
{
	struct ib_gid_cache *cache;
	unsigned long flags;
	int p, i;
	int ret = -ENOENT;

	*port_num = -1;
	if (index)
		*index = -1;

	read_lock_irqsave(&device->cache.lock, flags);

	for (p = 0; p <= end_port(device) - start_port(device); ++p) {
		cache = device->cache.gid_cache[p];
		for (i = 0; i < cache->table_len; ++i) {
			if (!memcmp(gid, &cache->entry[i].gid, sizeof *gid)) {
				*port_num = p + start_port(device);
				if (index)
					*index = cache->entry[i].index;
				ret = 0;
				goto found;
			}
		}
	}

found:
	read_unlock_irqrestore(&device->cache.lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_find_cached_gid);

int ib_get_cached_pkey(struct ib_device *device,
		       u8                port_num,
		       int               index,
		       u16              *pkey)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int i, ret = 0;
	u8 idx;

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	idx = array_index_nospec(port_num - start_port(device),
				 end_port(device) - start_port(device) + 1);
	cache = device->cache.pkey_cache[idx];

	if (index < 0 || index >= cache->table_len) {
		ret = -EINVAL;
		goto out_unlock;
	}

	for (i = 0; i < cache->table_len; ++i)
		if (cache->entry[i].index == index)
			break;

	if (i < cache->table_len)
		*pkey = cache->entry[i].pkey;
	else
		*pkey = 0x0000;

out_unlock:
	read_unlock_irqrestore(&device->cache.lock, flags);
	return ret;
}
EXPORT_SYMBOL(ib_get_cached_pkey);

int ib_find_cached_pkey(struct ib_device *device,
			u8                port_num,
			u16               pkey,
			u16              *index)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int i;
	int ret = -ENOENT;
	int partial_ix = -1;
	u8 idx;

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	idx = array_index_nospec(port_num - start_port(device),
				 end_port(device) - start_port(device) + 1);
	cache = device->cache.pkey_cache[idx];

	*index = -1;

	for (i = 0; i < cache->table_len; ++i)
		if ((cache->entry[i].pkey & 0x7fff) == (pkey & 0x7fff)) {
			if (cache->entry[i].pkey & 0x8000) {
				*index = cache->entry[i].index;
				ret = 0;
				break;
			} else
				partial_ix = cache->entry[i].index;
		}

	if (ret && partial_ix >= 0) {
		*index = partial_ix;
		ret = 0;
	}

	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_find_cached_pkey);

int ib_find_exact_cached_pkey(struct ib_device *device,
			      u8                port_num,
			      u16               pkey,
			      u16              *index)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	int i;
	int ret = -ENOENT;
	u8 idx;

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);

	idx = array_index_nospec(port_num - start_port(device),
				 end_port(device) - start_port(device) + 1);
	cache = device->cache.pkey_cache[idx];

	*index = -1;

	for (i = 0; i < cache->table_len; ++i)
		if (cache->entry[i].pkey == pkey) {
			*index = cache->entry[i].index;
			ret = 0;
			break;
		}

	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_find_exact_cached_pkey);

int ib_get_cached_lmc(struct ib_device *device,
		      u8                port_num,
		      u8                *lmc)
{
	unsigned long flags;
	int ret = 0;
	u8 idx;

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, flags);
	idx = array_index_nospec(port_num - start_port(device),
				 end_port(device) - start_port(device) + 1);
	*lmc = device->cache.lmc_cache[idx];
	read_unlock_irqrestore(&device->cache.lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_lmc);

static void ib_cache_update(struct ib_device *device,
			    u8                port)
{
	struct ib_port_attr       *tprops = NULL;
	struct ib_pkey_cache      *pkey_cache = NULL, *old_pkey_cache;
	struct ib_gid_cache       *gid_cache = NULL, *old_gid_cache;
	int                        i, j;
	int                        ret;
	union ib_gid		   gid, empty_gid;
	u16			   pkey;
	u8 idx;

	tprops = kmalloc(sizeof *tprops, GFP_KERNEL);
	if (!tprops)
		return;

	ret = ib_query_port(device, port, tprops);
	if (ret) {
		printk(KERN_WARNING "ib_query_port failed (%d) for %s\n",
		       ret, device->name);
		goto err;
	}

	pkey_cache = kmalloc(sizeof *pkey_cache + tprops->pkey_tbl_len *
			     sizeof *pkey_cache->entry, GFP_KERNEL);
	if (!pkey_cache)
		goto err;

	pkey_cache->table_len = 0;

	gid_cache = kmalloc(sizeof *gid_cache + tprops->gid_tbl_len *
			    sizeof *gid_cache->entry, GFP_KERNEL);
	if (!gid_cache)
		goto err;

	gid_cache->table_len = 0;

	for (i = 0, j = 0; i < tprops->pkey_tbl_len; ++i) {
		ret = ib_query_pkey(device, port, i, &pkey);
		if (ret) {
			printk(KERN_WARNING "ib_query_pkey failed (%d) for %s (index %d)\n",
			       ret, device->name, i);
			goto err;
		}
		/* pkey 0xffff must be the default pkeyand 0x0000 must be the invalid
		 * pkey per IBTA spec */
		if (pkey) {
			pkey_cache->entry[j].index = i;
			pkey_cache->entry[j++].pkey = pkey;
		}
	}
	pkey_cache->table_len = j;

	memset(&empty_gid, 0, sizeof empty_gid);
	for (i = 0, j = 0; i < tprops->gid_tbl_len; ++i) {
		ret = ib_query_gid(device, port, i, &gid);
		if (ret) {
			printk(KERN_WARNING "ib_query_gid failed (%d) for %s (index %d)\n",
			       ret, device->name, i);
			goto err;
		}
		/* if the lower 8 bytes the device GID entry is all 0,
		 * our entry is a blank, invalid entry...
		 * depending on device, the upper 8 bytes might or might
		 * not be prefilled with a valid subnet prefix, so
		 * don't rely on them for determining a valid gid
		 * entry
		 */
		if (memcmp(&gid + 8, &empty_gid + 8, sizeof gid - 8)) {
			gid_cache->entry[j].index = i;
			gid_cache->entry[j++].gid = gid;
		}
	}
	gid_cache->table_len = j;

	old_pkey_cache = pkey_cache;
	pkey_cache = kmalloc(sizeof *pkey_cache + old_pkey_cache->table_len *
			     sizeof *pkey_cache->entry, GFP_KERNEL);
	if (!pkey_cache)
		pkey_cache = old_pkey_cache;
	else {
		pkey_cache->table_len = old_pkey_cache->table_len;
		memcpy(&pkey_cache->entry[0], &old_pkey_cache->entry[0],
		       pkey_cache->table_len * sizeof *pkey_cache->entry);
		kfree(old_pkey_cache);
	}

	old_gid_cache = gid_cache;
	gid_cache = kmalloc(sizeof *gid_cache + old_gid_cache->table_len *
			    sizeof *gid_cache->entry, GFP_KERNEL);
	if (!gid_cache)
		gid_cache = old_gid_cache;
	else {
		gid_cache->table_len = old_gid_cache->table_len;
		memcpy(&gid_cache->entry[0], &old_gid_cache->entry[0],
		       gid_cache->table_len * sizeof *gid_cache->entry);
		kfree(old_gid_cache);
	}

	write_lock_irq(&device->cache.lock);

	idx = array_index_nospec(port - start_port(device),
				 end_port(device) - start_port(device) + 1);

	old_pkey_cache = device->cache.pkey_cache[idx];
	old_gid_cache  = device->cache.gid_cache [idx];

	device->cache.pkey_cache[idx] = pkey_cache;
	device->cache.gid_cache [idx] = gid_cache;

	device->cache.lmc_cache[idx] = tprops->lmc;

	write_unlock_irq(&device->cache.lock);

	kfree(old_pkey_cache);
	kfree(old_gid_cache);
	kfree(tprops);
	return;

err:
	kfree(pkey_cache);
	kfree(gid_cache);
	kfree(tprops);
}

static void ib_cache_task(struct work_struct *_work)
{
	struct ib_update_work *work =
		container_of(_work, struct ib_update_work, work);

	ib_cache_update(work->device, work->port_num);
	kfree(work);
}

static void ib_cache_event(struct ib_event_handler *handler,
			   struct ib_event *event)
{
	struct ib_update_work *work;

	if (event->event == IB_EVENT_PORT_ERR    ||
	    event->event == IB_EVENT_PORT_ACTIVE ||
	    event->event == IB_EVENT_LID_CHANGE  ||
	    event->event == IB_EVENT_PKEY_CHANGE ||
	    event->event == IB_EVENT_SM_CHANGE   ||
	    event->event == IB_EVENT_CLIENT_REREGISTER ||
	    event->event == IB_EVENT_GID_CHANGE) {
		work = kmalloc(sizeof *work, GFP_ATOMIC);
		if (work) {
			INIT_WORK(&work->work, ib_cache_task);
			work->device   = event->device;
			work->port_num = event->element.port_num;
			queue_work(ib_wq, &work->work);
		}
	}
}

static void ib_cache_setup_one(struct ib_device *device)
{
	int p;

	rwlock_init(&device->cache.lock);

	device->cache.pkey_cache =
		kmalloc(sizeof *device->cache.pkey_cache *
			(end_port(device) - start_port(device) + 1), GFP_KERNEL);
	device->cache.gid_cache =
		kmalloc(sizeof *device->cache.gid_cache *
			(end_port(device) - start_port(device) + 1), GFP_KERNEL);

	device->cache.lmc_cache = kmalloc(sizeof *device->cache.lmc_cache *
					  (end_port(device) -
					   start_port(device) + 1),
					  GFP_KERNEL);

	if (!device->cache.pkey_cache || !device->cache.gid_cache ||
	    !device->cache.lmc_cache) {
		printk(KERN_WARNING "Couldn't allocate cache "
		       "for %s\n", device->name);
		goto err;
	}

	for (p = 0; p <= end_port(device) - start_port(device); ++p) {
		device->cache.pkey_cache[p] = NULL;
		device->cache.gid_cache [p] = NULL;
		ib_cache_update(device, p + start_port(device));
	}

	INIT_IB_EVENT_HANDLER(&device->cache.event_handler,
			      device, ib_cache_event);
	if (ib_register_event_handler(&device->cache.event_handler))
		goto err_cache;

	return;

err_cache:
	for (p = 0; p <= end_port(device) - start_port(device); ++p) {
		kfree(device->cache.pkey_cache[p]);
		kfree(device->cache.gid_cache[p]);
	}

err:
	kfree(device->cache.pkey_cache);
	kfree(device->cache.gid_cache);
	kfree(device->cache.lmc_cache);
}

static void ib_cache_cleanup_one(struct ib_device *device)
{
	int p;

	ib_unregister_event_handler(&device->cache.event_handler);
	flush_workqueue(ib_wq);

	for (p = 0; p <= end_port(device) - start_port(device); ++p) {
		kfree(device->cache.pkey_cache[p]);
		kfree(device->cache.gid_cache[p]);
	}

	kfree(device->cache.pkey_cache);
	kfree(device->cache.gid_cache);
	kfree(device->cache.lmc_cache);
}

static struct ib_client cache_client = {
	.name   = "cache",
	.add    = ib_cache_setup_one,
	.remove = ib_cache_cleanup_one
};

int __init ib_cache_setup(void)
{
	return ib_register_client(&cache_client);
}

void __exit ib_cache_cleanup(void)
{
	ib_unregister_client(&cache_client);
}
