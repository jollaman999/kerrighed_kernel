/*
 * net/gipc/user_reg.c: GIPC user registry code
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2004-2005, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "user_reg.h"

/*
 * GIPC user registry keeps track of users of the gipc_port interface.
 *
 * The registry utilizes an array of "GIPC user" entries;
 * a user's ID is the index of their associated array entry.
 * Array entry 0 is not used, so userid 0 is not valid;
 * GIPC sometimes uses this value to denote an anonymous user.
 * The list of free entries is initially chained from last entry to entry 1.
 */

/**
 * struct gipc_user - registered GIPC user info
 * @next: index of next free registry entry (or -1 for an allocated entry)
 * @callback: ptr to routine to call when GIPC mode changes (NULL if none)
 * @usr_handle: user-defined value passed to callback routine
 * @ports: list of user ports owned by the user
 */

struct gipc_user {
	int next;
	gipc_mode_event callback;
	void *usr_handle;
	struct list_head ports;
};

#define MAX_USERID 64
#define USER_LIST_SIZE ((MAX_USERID + 1) * sizeof(struct gipc_user))

static struct gipc_user *users = NULL;
static u32 next_free_user = MAX_USERID + 1;
static DEFINE_SPINLOCK(reg_lock);

/**
 * reg_init - create GIPC user registry (but don't activate it)
 *
 * If registry has been pre-initialized it is left "as is".
 * NOTE: This routine may be called when GIPC is inactive.
 */

static int reg_init(void)
{
	u32 i;

	spin_lock_bh(&reg_lock);
	if (!users) {
		users = kzalloc(USER_LIST_SIZE, GFP_ATOMIC);
		if (users) {
			for (i = 1; i <= MAX_USERID; i++) {
				users[i].next = i - 1;
			}
			next_free_user = MAX_USERID;
		}
	}
	spin_unlock_bh(&reg_lock);
	return users ? 0 : -ENOMEM;
}

/**
 * reg_callback - inform GIPC user about current operating mode
 */

static void reg_callback(struct gipc_user *user_ptr)
{
	gipc_mode_event cb;
	void *arg;

	spin_lock_bh(&reg_lock);
	cb = user_ptr->callback;
	arg = user_ptr->usr_handle;
	spin_unlock_bh(&reg_lock);

	if (cb)
		cb(arg, gipc_mode, gipc_own_addr);
}

/**
 * gipc_reg_start - activate GIPC user registry
 */

int gipc_reg_start(void)
{
	u32 u;
	int res;

	if ((res = reg_init()))
		return res;

	for (u = 1; u <= MAX_USERID; u++) {
		if (users[u].callback)
			gipc_k_signal((Handler)reg_callback,
				      (unsigned long)&users[u]);
	}
	return 0;
}

/**
 * gipc_reg_stop - shut down & delete GIPC user registry
 */

void gipc_reg_stop(void)
{
	int id;

	if (!users)
		return;

	for (id = 1; id <= MAX_USERID; id++) {
		if (users[id].callback)
			reg_callback(&users[id]);
	}
	kfree(users);
	users = NULL;
}

/**
 * gipc_attach - register a GIPC user
 *
 * NOTE: This routine may be called when GIPC is inactive.
 */

int gipc_attach(u32 *userid, gipc_mode_event cb, void *usr_handle)
{
	struct gipc_user *user_ptr;

	if ((gipc_mode == GIPC_NOT_RUNNING) && !cb)
		return -ENOPROTOOPT;
	if (!users)
		reg_init();

	spin_lock_bh(&reg_lock);
	if (!next_free_user) {
		spin_unlock_bh(&reg_lock);
		return -EBUSY;
	}
	user_ptr = &users[next_free_user];
	*userid = next_free_user;
	next_free_user = user_ptr->next;
	user_ptr->next = -1;
	spin_unlock_bh(&reg_lock);

	user_ptr->callback = cb;
	user_ptr->usr_handle = usr_handle;
	INIT_LIST_HEAD(&user_ptr->ports);
	atomic_inc(&gipc_user_count);

	if (cb && (gipc_mode != GIPC_NOT_RUNNING))
		gipc_k_signal((Handler)reg_callback, (unsigned long)user_ptr);
	return 0;
}

/**
 * gipc_detach - deregister a GIPC user
 */

void gipc_detach(u32 userid)
{
	struct gipc_user *user_ptr;
	struct list_head ports_temp;
	struct user_port *up_ptr, *temp_up_ptr;

	if ((userid == 0) || (userid > MAX_USERID))
		return;

	spin_lock_bh(&reg_lock);
	if ((!users) || (users[userid].next >= 0)) {
		spin_unlock_bh(&reg_lock);
		return;
	}

	user_ptr = &users[userid];
	user_ptr->callback = NULL;
	INIT_LIST_HEAD(&ports_temp);
	list_splice(&user_ptr->ports, &ports_temp);
	user_ptr->next = next_free_user;
	next_free_user = userid;
	spin_unlock_bh(&reg_lock);

	atomic_dec(&gipc_user_count);

	list_for_each_entry_safe(up_ptr, temp_up_ptr, &ports_temp, uport_list) {
		gipc_deleteport(up_ptr->ref);
	}
}

/**
 * gipc_reg_add_port - register a user's driver port
 */

int gipc_reg_add_port(struct user_port *up_ptr)
{
	struct gipc_user *user_ptr;

	if (up_ptr->user_ref == 0)
		return 0;
	if (up_ptr->user_ref > MAX_USERID)
		return -EINVAL;
	if ((gipc_mode == GIPC_NOT_RUNNING) || !users )
		return -ENOPROTOOPT;

	spin_lock_bh(&reg_lock);
	user_ptr = &users[up_ptr->user_ref];
	list_add(&up_ptr->uport_list, &user_ptr->ports);
	spin_unlock_bh(&reg_lock);
	return 0;
}

/**
 * gipc_reg_remove_port - deregister a user's driver port
 */

int gipc_reg_remove_port(struct user_port *up_ptr)
{
	if (up_ptr->user_ref == 0)
		return 0;
	if (up_ptr->user_ref > MAX_USERID)
		return -EINVAL;
	if (!users )
		return -ENOPROTOOPT;

	spin_lock_bh(&reg_lock);
	list_del_init(&up_ptr->uport_list);
	spin_unlock_bh(&reg_lock);
	return 0;
}

