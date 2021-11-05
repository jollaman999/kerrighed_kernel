/*
 *  Copyright (C) 2019-2021 Innogrid HCC.
 */

#include <linux/workqueue.h>
#include <linux/slab.h>

#include <hcc/hotplug.h>
#include <hcc/namespace.h>
#include <net/grpc/rpcid.h>
#include <net/grpc/rpc.h>

#include "hotplug_internal.h"

struct workqueue_struct *hcc_ha_wq;

struct hotplug_context *hotplug_ctx_alloc(struct hcc_namespace *ns)
{
	struct hotplug_context *ctx;

	BUG_ON(!ns);
	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	get_hcc_ns(ns);
	ctx->ns = ns;
	kref_init(&ctx->kref);

	return ctx;
}

void hotplug_ctx_release(struct kref *kref)
{
	struct hotplug_context *ctx;

	ctx = container_of(kref, struct hotplug_context, kref);
	put_hcc_ns(ctx->ns);
	kfree(ctx);
}

int init_hotplug(void)
{
	hcc_ha_wq = create_workqueue("hccHA");
	BUG_ON(hcc_ha_wq == NULL);

	hotplug_hooks_init();

	hotplug_add_init();
#ifdef CONFIG_HCC_HOTPLUG_DEL
	hotplug_remove_init();
#endif
	hotplug_failure_init();
	hotplug_cluster_init();
	hotplug_namespace_init();
	hotplug_membership_init();

	return 0;
};

void cleanup_hotplug(void)
{
};
