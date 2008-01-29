/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License in COPYING.GPL for more details.
 */

#ifndef __omx_endpoint_h__
#define __omx_endpoint_h__

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/idr.h>

#include "omx_io.h"

struct omx_iface;
struct page;

enum omx_endpoint_status {
	/* endpoint is free and may be open */
	OMX_ENDPOINT_STATUS_FREE,
	/* endpoint is already being open by somebody else */
	OMX_ENDPOINT_STATUS_INITIALIZING,
	/* endpoint is ready to be used */
	OMX_ENDPOINT_STATUS_OK,
	/* endpoint is being closed by somebody else */
	OMX_ENDPOINT_STATUS_CLOSING,
};

struct omx_endpoint {
	uint8_t board_index;
	uint8_t endpoint_index;
	uint32_t session_id;

	pid_t opener_pid;
	char opener_comm[TASK_COMM_LEN];

	enum omx_endpoint_status status;
	spinlock_t status_lock;

	struct kref refcount;
	struct list_head list_elt; /* the list entry for the cleanup list */

	struct omx_iface * iface;

	void * sendq, * recvq, * exp_eventq, * unexp_eventq;
	unsigned long next_exp_eventq_offset;
	unsigned long next_free_unexp_eventq_offset, next_reserved_unexp_eventq_offset;
	unsigned long next_recvq_offset;
	wait_queue_head_t waiters;
	spinlock_t event_lock;

	struct page ** sendq_pages;

	spinlock_t user_regions_lock;
	struct omx_user_region * user_regions[OMX_USER_REGION_MAX];

	struct list_head pull_handles_running_list;
	struct list_head pull_handles_done_but_timer_list;
	struct idr pull_handles_idr;
	rwlock_t pull_handles_lock;

	/* descriptor exported to user-space, modified by user-space and the driver,
	 * so we can export some info to user-space by writing into it, but we
	 * cannot rely on reading from it
	 */
	struct omx_endpoint_desc * userdesc;
};

extern int omx_iface_attach_endpoint(struct omx_endpoint * endpoint);
extern void omx_iface_detach_endpoint(struct omx_endpoint * endpoint, int ifacelocked);
extern int omx_endpoint_close(struct omx_endpoint * endpoint, int ifacelocked);
extern struct omx_endpoint * omx_endpoint_acquire_by_iface_index(struct omx_iface * iface, uint8_t index);
extern void __omx_endpoint_last_release(struct kref *kref);
extern void omx_endpoints_cleanup(void);
extern int omx_endpoint_get_info(uint32_t board_index, uint32_t endpoint_index, uint32_t * closed, uint32_t * pid, char * command, size_t len);

static inline void
omx_endpoint_reacquire(struct omx_endpoint * endpoint)
{
	kref_get(&endpoint->refcount);
}

static inline void
omx_endpoint_release(struct omx_endpoint * endpoint)
{
	kref_put(&endpoint->refcount, __omx_endpoint_last_release);
}

extern int omx_ioctl_bench(struct omx_endpoint * endpoint, void __user * uparam);

#endif /* __omx_endpoint_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
