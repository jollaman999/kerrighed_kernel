/**
 *
 *  Copyright (C) 2019-2021 Innogrid HCC.
 *
 */

#include <linux/timer.h>
#include <linux/workqueue.h>
#include <hcc/hccnodemask.h>
#include <hcc/hccinit.h>

#include <hcc/workqueue.h>
#include <net/grpc/rpcid.h>
#include <net/grpc/rpc.h>

#include "rpc_internal.h"

static struct timer_list rpc_timer;
struct work_struct rpc_work;
struct rpc_service pingpong_service;

static void rpc_pingpong_handler (struct rpc_desc *rpc_desc,
				  void *data,
				  size_t size){
	unsigned long l = *(unsigned long*)data;

	l++;
	
	grpc_pack(rpc_desc, 0, &l, sizeof(l));
};

static void rpc_worker(struct work_struct *data)
{
	static unsigned long l = 0;
	hccnodemask_t n;
	int r;

	r = 0;
	l++;
	
	hccnodes_clear(n);
	hccnode_set(0, n);

	r = rpc_async(RPC_PINGPONG, 0, &l, sizeof(l));
	if(r<0)
		return;
	
}

static void rpc_timer_cb(unsigned long _arg)
{
	return;
	queue_work(hcc_wq, &rpc_work);
	mod_timer(&rpc_timer, jiffies + 2*HZ);
}

int rpc_monitor_init(void){
	rpc_register_void(RPC_PINGPONG,
			  rpc_pingpong_handler, 0);
	
	init_timer(&rpc_timer);
	rpc_timer.function = rpc_timer_cb;
	rpc_timer.data = 0;
	if(hcc_node_id != 0)
		mod_timer(&rpc_timer, jiffies + 10*HZ);
	INIT_WORK(&rpc_work, rpc_worker);

	return 0;
}

void rpc_monitor_cleanup(void){
}
