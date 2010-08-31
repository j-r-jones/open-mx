/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/list.h>

#include "omx_io.h"
#include "omx_common.h"
#include "omx_iface.h"
#include "omx_endpoint.h"

/*************************
 * Wait Queues and Wakeup
 */

struct omx_event_waiter {
	struct list_head list_elt;
	struct task_struct *task;
	uint8_t status;
};

/* called with the endpoint event_lock read-held */
static INLINE void
omx_wakeup_waiter_list(struct omx_endpoint *endpoint,
		       uint32_t status)
{
	struct omx_event_waiter *waiter, *next;

	/*TODO: Utiliser des rcu */

	/* wake up everybody with the event status */
	list_for_each_entry_safe(waiter, next, &endpoint->waiters, list_elt) {
		waiter->status = status;
		wake_up_process(waiter->task);
	}
}

static void
omx_wakeup_on_timeout_handler(unsigned long data)
{
	struct omx_event_waiter *waiter = (struct omx_event_waiter*) data;

	/* wakeup with the timeout status */
	waiter->status = OMX_CMD_WAIT_EVENT_STATUS_TIMEOUT;
	wake_up_process(waiter->task);
}

static void
omx_wakeup_on_progress_timeout_handler(unsigned long data)
{
	struct omx_event_waiter *waiter = (struct omx_event_waiter*) data;

	/* wakeup with the progress status */
	waiter->status = OMX_CMD_WAIT_EVENT_STATUS_PROGRESS;
	wake_up_process(waiter->task);
}

/*****************
 * Initialization
 */

void
omx_endpoint_queues_init(struct omx_endpoint *endpoint)
{
	union omx_evt * evt;

	/* sanity checks */
	BUILD_BUG_ON(PAGE_SIZE%OMX_SENDQ_ENTRY_SIZE != 0 && OMX_SENDQ_ENTRY_SIZE%PAGE_SIZE != 0);
	BUILD_BUG_ON(PAGE_SIZE%OMX_RECVQ_ENTRY_SIZE != 0 && OMX_RECVQ_ENTRY_SIZE%PAGE_SIZE != 0);
	BUILD_BUG_ON(sizeof(union omx_evt) != OMX_EVENTQ_ENTRY_SIZE);
	BUILD_BUG_ON(OMX_UNEXP_EVENTQ_ENTRY_NR != OMX_RECVQ_ENTRY_NR);

	/* initialize all expected events to none */
	for(evt = endpoint->exp_eventq;
	    (void *) evt < endpoint->exp_eventq + OMX_EXP_EVENTQ_SIZE;
	    evt++)
		evt->generic.type = OMX_EVT_NONE;
	/* set the expected eventq counter (required for slot and id) */
	atomic64_set(&endpoint->exp_eventq_counter, 0);

	/* set the last free expected event slot */
	endpoint->last_free_exp_eventq_offset = OMX_EXP_EVENTQ_SIZE;

	/* initialize all unexpected events to none */
	for(evt = endpoint->unexp_eventq;
	    (void *) evt < endpoint->unexp_eventq + OMX_UNEXP_EVENTQ_SIZE;
	    evt++)
		evt->generic.type = OMX_EVT_NONE;
	/* set the unexpected eventq counter, and then the reserved counter */
	atomic64_set(&endpoint->unexp_eventq_counter, 0);
	atomic64_set(&endpoint->unexp_reserved_counter, 0);

	/* set the last free unexpected event slot */
	endpoint->last_free_unexp_eventq_offset = OMX_UNEXP_EVENTQ_SIZE;

	/* set the recvq counter */
	atomic64_set(&endpoint->recvq_counter, 0);

	INIT_LIST_HEAD(&endpoint->waiters);
	spin_lock_init(&endpoint->event_lock);
	spin_lock_init(&endpoint->release_exp_lock);
	spin_lock_init(&endpoint->release_unexp_lock);
}

/******************************************
 * Report an expected event to users-space
 */

int
omx_notify_exp_event(struct omx_endpoint *endpoint, const void *event, int length)
{
	union omx_evt *slot;
	unsigned long exp_counter, offset;
	uint8_t evt_id;

	exp_counter = atomic64_inc_return(&endpoint->exp_eventq_counter) - 1;
	offset = (exp_counter * OMX_EVENTQ_ENTRY_SIZE) % OMX_EXP_EVENTQ_SIZE;
	evt_id = (exp_counter % OMX_EVENTQ_MAX_ID) + 1;

	slot = endpoint->exp_eventq + offset;
	if (unlikely(slot == endpoint->exp_eventq + endpoint->last_free_exp_eventq_offset)) {
		/* the application sucks, it did not check
		 * the expected eventq before posting requests
		 */
		dprintk(EVENT,
			"Open-MX: Expected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		omx_counter_inc(endpoint->iface, EXP_EVENTQ_FULL);
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_EXP_EVENTQ_FULL;
		return -EBUSY;
	}

	/* store the event and then the actual id */
	memcpy(slot, event, length);
	wmb();
	((struct omx_evt_generic *) slot)->id = evt_id;

	/* wake up waiters */
	dprintk(EVENT, "notify_exp waking up everybody\n");
	spin_lock_bh(&endpoint->event_lock);
	omx_wakeup_waiter_list(endpoint, OMX_CMD_WAIT_EVENT_STATUS_EVENT);
	spin_unlock_bh(&endpoint->event_lock);

	return 0;
}

/********************************************
 * Report an unexpected event to users-space
 * without any recvq slot needed
 */

int
omx_notify_unexp_event(struct omx_endpoint *endpoint, const void *event, int length)
{
	union omx_evt *slot;
	unsigned long unexp_counter, reserved_counter, free_offset, reserved_offset;
	uint8_t evt_id;

	unexp_counter = atomic64_inc_return(&endpoint->unexp_eventq_counter) - 1;
	free_offset = (unexp_counter * OMX_EVENTQ_ENTRY_SIZE) % OMX_UNEXP_EVENTQ_SIZE;
	evt_id = (unexp_counter % OMX_EVENTQ_MAX_ID) + 1;

	slot = endpoint->unexp_eventq + free_offset;
	if (unlikely(slot == endpoint->unexp_eventq + endpoint->last_free_unexp_eventq_offset)) {
		/* the application sucks, it did not check
		 * the unexpected eventq before posting requests
		 */
		dprintk(EVENT,
			"Open-MX: Unexpected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		omx_counter_inc(endpoint->iface, UNEXP_EVENTQ_FULL);
		atomic64_dec(&endpoint->unexp_eventq_counter);
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL;
		return -EBUSY;
	}

	/* find and update the next reserved slot in the queue */
	reserved_counter = atomic64_inc_return(&endpoint->unexp_reserved_counter) - 1;
	reserved_offset = (reserved_counter * OMX_EVENTQ_ENTRY_SIZE) % OMX_UNEXP_EVENTQ_SIZE;
	slot = endpoint->unexp_eventq + reserved_offset;

	/* store the event and then the actual id */
	memcpy(slot, event, length);
	wmb();
	((struct omx_evt_generic *) slot)->id = evt_id;

	/* wake up waiters */
	dprintk(EVENT, "notify_unexp waking up everybody\n");
	spin_lock_bh(&endpoint->event_lock);
	omx_wakeup_waiter_list(endpoint, OMX_CMD_WAIT_EVENT_STATUS_EVENT);
	spin_unlock_bh(&endpoint->event_lock);

	return 0;
}

/********************************************
 * Report an unexpected event to users-space
 * with a recvq slot needed
 */

/*
 * The recvq accounting is trivial since there are as many recvq slots
 * than unexp event slots, the latter are accounted, and we allocate only
 * one recvq slot per prepare()/commit() functions below (and no slot
 * in notify() above).
 */

/* Reserve one more slot and returns the corresponding recvq slot to the caller */
int
omx_prepare_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint,
					  unsigned long *recvq_offset_p)
{
	union omx_evt *slot;
	unsigned long offset, counter;

	/* check that there's a slot available and reserve it */
	counter = atomic64_inc_return(&endpoint->unexp_eventq_counter) - 1;
	offset = (counter * OMX_EVENTQ_ENTRY_SIZE) % OMX_UNEXP_EVENTQ_SIZE;
	slot = endpoint->unexp_eventq + offset;

	if (unlikely(slot == endpoint->unexp_eventq + endpoint->last_free_unexp_eventq_offset)) {
		dprintk(EVENT,
			"Open-MX: Unexpected event queue full, no event slot available for endpoint %d\n",
			endpoint->endpoint_index);
		omx_counter_inc(endpoint->iface, EXP_EVENTQ_FULL);
		atomic64_dec(&endpoint->unexp_eventq_counter);
		endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL;	
		return -EBUSY;
	}

	/* take the next recvq slot and return it now */
	counter = atomic64_inc_return(&endpoint->recvq_counter) - 1;
	*recvq_offset_p = (counter * OMX_RECVQ_ENTRY_SIZE) % OMX_RECVQ_SIZE;
	/* all slots have the same size, so there can't be a slot that wraps around the end */
	return 0;
}

/* Reserve nr more slots and returns the corresponding recvq slots to the caller */
int
omx_prepare_notify_unexp_events_with_recvq(struct omx_endpoint *endpoint,
					   int nr,
					   unsigned long *recvq_offset_p)
{
	union omx_evt *slot;
	unsigned long offset, counter;
	int i;

	/* check that there are enough slots available */
	counter = atomic64_read(&endpoint->unexp_eventq_counter);
	for(i=0; i<nr; i++) {
		offset = (counter * OMX_EVENTQ_ENTRY_SIZE) % OMX_UNEXP_EVENTQ_SIZE;
		slot = endpoint->unexp_eventq + offset;
		if (unlikely(slot == endpoint->unexp_eventq + endpoint->last_free_unexp_eventq_offset)) {
			dprintk(EVENT,
				"Open-MX: Unexpected event queue full, no event slot available for endpoint %d\n",
				endpoint->endpoint_index);
			omx_counter_inc(endpoint->iface, EXP_EVENTQ_FULL);
			endpoint->userdesc->status |= OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL;
			return -EBUSY;
		}
		counter++;
	}

	/* update the next free slot in the queue */
	atomic64_set(&endpoint->unexp_eventq_counter, counter);

	/* take the next recvq slots and return them now */
	for(i=0; i<nr; i++) {
		counter = atomic64_inc_return(&endpoint->recvq_counter) - 1;
		recvq_offset_p[i] = (counter * OMX_RECVQ_ENTRY_SIZE) % OMX_RECVQ_SIZE;	
	 	/* all slots have the same size, so there can't be a slot that wraps around the end */
	}
	return 0;
}

/*
 * Store the event in the next reserved slot
 * (not always the one reserved during omx_commit_notify_unexp_event()
 *  since prepare/commit calls could have been overlapped).
 */
void
omx_commit_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint, 
					 const void *event, int length)
{
	union omx_evt *slot;
	unsigned long reserved_counter, reserved_offset;
	uint8_t evt_id;

	reserved_counter = atomic64_inc_return(&endpoint->unexp_reserved_counter) - 1;
	reserved_offset = (reserved_counter * OMX_EVENTQ_ENTRY_SIZE) % OMX_UNEXP_EVENTQ_SIZE;
	evt_id = (reserved_counter % OMX_EVENTQ_MAX_ID) + 1;
	
	/* the caller should have called prepare() earlier */
	BUG_ON(reserved_offset == 
	       ((unsigned long)atomic64_read(&endpoint->unexp_eventq_counter) 
		* OMX_EVENTQ_ENTRY_SIZE) % OMX_UNEXP_EVENTQ_SIZE);

	/* get the next reserved slot in the queue */
	slot = endpoint->unexp_eventq + reserved_offset;

	/* store the event and then the actual id */
	memcpy(slot, event, length);
	wmb();
	((struct omx_evt_generic *) slot)->id = evt_id;

	/* wake up waiters */
	dprintk(EVENT, "commit_notify_unexp waking up everybody\n");
	spin_lock_bh(&endpoint->event_lock);
	omx_wakeup_waiter_list(endpoint, OMX_CMD_WAIT_EVENT_STATUS_EVENT);
	spin_unlock_bh(&endpoint->event_lock);
}

/*
 * Store an dummy "ignored" event in the next reserved slot
 * (not always the one reserved during omx_commit_notify_unexp_event()
 *  since prepare/commit calls could have been overlapped).
 * We can't cancel for real since the recvq slot could not be the last one.
 */
void
omx_cancel_notify_unexp_event_with_recvq(struct omx_endpoint *endpoint)
{
	union omx_evt *slot;
	unsigned long reserved_counter, reserved_offset;

	reserved_counter = atomic64_inc_return(&endpoint->unexp_reserved_counter) - 1;
	reserved_offset = (reserved_counter * OMX_EVENTQ_ENTRY_SIZE) % OMX_UNEXP_EVENTQ_SIZE;

	/* the caller should have called prepare() earlier */
	BUG_ON(reserved_offset == 
	       ((unsigned long)atomic64_read(&endpoint->unexp_eventq_counter) 
		* OMX_EVENTQ_ENTRY_SIZE) % OMX_UNEXP_EVENTQ_SIZE);

	/* get the next reserved slot in the queue */
	slot = endpoint->unexp_eventq + reserved_offset;

	/* fill an event to be ignored by user-space */
	((struct omx_evt_generic *) slot)->type = OMX_EVT_IGNORE;

	/* no need to wakeup people */
}

/***********
 * Sleeping
 */

/* FIXME: this is for when the application waits, not when the progression thread does */
int
omx_ioctl_wait_event(struct omx_endpoint * endpoint, void __user * uparam)
{
	struct omx_cmd_wait_event cmd;
	struct omx_event_waiter waiter;
	struct timer_list timer;
	int err = 0;

	/* lib-progression-requested timeout */
	uint64_t wakeup_jiffies = endpoint->userdesc->wakeup_jiffies;

	/* timer, either from the ioctl or from the lib-progression-requested timeout */
	void (*timer_handler)(unsigned long) = NULL;
	uint64_t timer_jiffies = 0;

	/* cache current jiffies */
	u64 current_jiffies;

	err = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read wait event cmd hdr\n");
		err = -EFAULT;
		goto out;
	}

	/* FIXME: wait on some event type only */

	spin_lock_bh(&endpoint->event_lock);
	unsigned long nxt_exp = (atomic64_read(&endpoint->exp_eventq_counter) * OMX_EVENTQ_ENTRY_SIZE) % OMX_EXP_EVENTQ_SIZE;
	unsigned long nxt_reserved = (atomic64_read(&endpoint->unexp_reserved_counter) * OMX_EVENTQ_ENTRY_SIZE) % OMX_UNEXP_EVENTQ_SIZE;;
	/* check for race conditions */
	if ((cmd.next_exp_event_offset != nxt_exp)
	    || (cmd.next_unexp_event_offset != nxt_reserved)
	    || (cmd.user_event_index != endpoint->userdesc->user_event_index)) {
		dprintk(EVENT, "wait event race (%ld,%ld,%ld) != (%ld,%ld,%ld)\n",
			(unsigned long) cmd.next_exp_event_offset,
			(unsigned long) cmd.next_unexp_event_offset,
			(unsigned long) cmd.user_event_index,
			nxt_exp,
			nxt_reserved,
			(unsigned long) endpoint->userdesc->user_event_index);
		spin_unlock_bh(&endpoint->event_lock);
		cmd.status = OMX_CMD_WAIT_EVENT_STATUS_RACE;
		goto race;
	}

	/* queue ourself on the wait queue */
	list_add_tail(&waiter.list_elt, &endpoint->waiters);
	waiter.status = OMX_CMD_WAIT_EVENT_STATUS_NONE;
	waiter.task = current;
	set_current_state(TASK_INTERRUPTIBLE);

	spin_unlock_bh(&endpoint->event_lock);

	/* setup the timer if needed by an application timeout */
	if (cmd.jiffies_expire != OMX_CMD_WAIT_EVENT_TIMEOUT_INFINITE) {
		timer_handler = omx_wakeup_on_timeout_handler;
		timer_jiffies = cmd.jiffies_expire;
	}
	/* setup the timer if needed by a progress timeout */
	if (wakeup_jiffies != OMX_NO_WAKEUP_JIFFIES
	    && (!timer_handler || time_before64(wakeup_jiffies, timer_jiffies))) {
		timer_handler = omx_wakeup_on_progress_timeout_handler;
		timer_jiffies = wakeup_jiffies;
	}

	/* cache jiffies for multiple later use */
	current_jiffies = get_jiffies_64();

	/* setup the timer for real now */
	if (timer_handler) {
		/* check timer races */
		if (time_after_eq64(current_jiffies, timer_jiffies)) {
			dprintk(EVENT, "wait event expire %lld has passed (now is %lld), not sleeping\n",
				(unsigned long long) cmd.jiffies_expire, (unsigned long long) current_jiffies);
			waiter.status = OMX_CMD_WAIT_EVENT_STATUS_RACE;
			goto wakeup;
		}
		setup_timer(&timer, timer_handler, (unsigned long) &waiter);
		/* timer not pending yet, use the regular mod_timer() */
		mod_timer(&timer, timer_jiffies);
		dprintk(EVENT, "wait event timer setup at %lld (now is %lld)\n",
			(unsigned long long) timer_jiffies, (unsigned long long) current_jiffies);
	}

	if (waiter.status == OMX_CMD_WAIT_EVENT_STATUS_NONE
	    && !signal_pending(current)) {
		/* if nothing happened, let's go to sleep */
		dprintk(EVENT, "going to sleep at %lld\n", (unsigned long long) current_jiffies);
		schedule();
		dprintk(EVENT, "waking up from sleep at %lld\n", (unsigned long long) current_jiffies);

	} else {
		/* already "woken-up", no need to sleep */
		dprintk(EVENT, "not going to sleep, status is already %d\n", (unsigned) waiter.status);
	}

	/* remove the timer */
	if (timer_handler)
		del_singleshot_timer_sync(&timer);

 wakeup:
	__set_current_state(TASK_RUNNING); /* no need to serialize with below, __set is enough */

	spin_lock_bh(&endpoint->event_lock);
	list_del(&waiter.list_elt);
	spin_unlock_bh(&endpoint->event_lock);

	if (waiter.status == OMX_CMD_WAIT_EVENT_STATUS_NONE) {
		/* status didn't changed, we have been interrupted */
		waiter.status = OMX_CMD_WAIT_EVENT_STATUS_INTR;
	}

	cmd.status = waiter.status;

 race:
	err = copy_to_user(uparam, &cmd, sizeof(cmd));
	if (unlikely(err != 0)) {
		err = -EFAULT;
		printk(KERN_ERR "Open-MX: Failed to write wait event cmd result\n");
	}

	return 0;

 out:
	return err;
}

int
omx_ioctl_release_exp_chunk(struct omx_endpoint *endpoint, void __user *uparam)
{
	unsigned long next_exp_eventq_offset;

	spin_lock(&endpoint->release_exp_lock);

	next_exp_eventq_offset = ((unsigned long)atomic64_read(&endpoint->exp_eventq_counter) * OMX_EVENTQ_ENTRY_SIZE) % OMX_EXP_EVENTQ_SIZE;

	endpoint->last_free_exp_eventq_offset += OMX_EXP_CHUNK_SIZE;
	endpoint->last_free_exp_eventq_offset %= OMX_EXP_EVENTQ_SIZE;

	if (unlikely(endpoint->last_free_exp_eventq_offset == next_exp_eventq_offset))
		endpoint->last_free_exp_eventq_offset -= OMX_EVENTQ_ENTRY_SIZE;

	spin_unlock(&endpoint->release_exp_lock);

	return 0;
}

int
omx_ioctl_release_unexp_chunk(struct omx_endpoint *endpoint, void __user *uparam)
{
	unsigned long next_free_unexp_eventq_offset;

	spin_lock(&endpoint->release_unexp_lock);
	
	next_free_unexp_eventq_offset = ((unsigned long)atomic64_read(&endpoint->unexp_eventq_counter) * OMX_EVENTQ_ENTRY_SIZE) 
		% OMX_UNEXP_EVENTQ_SIZE;

	endpoint->last_free_unexp_eventq_offset += OMX_UNEXP_CHUNK_SIZE;
	endpoint->last_free_unexp_eventq_offset %= OMX_UNEXP_EVENTQ_SIZE;

	if (unlikely(endpoint->last_free_unexp_eventq_offset == next_free_unexp_eventq_offset))
		endpoint->last_free_unexp_eventq_offset -= OMX_EVENTQ_ENTRY_SIZE;

	spin_unlock(&endpoint->release_unexp_lock);
	
	return 0;
}

int
omx_ioctl_test(struct omx_endpoint * endpoint, void __user *uparam)
{
	union omx_evt evt;
	int err, num, i;

	err = copy_from_user(&num, uparam, sizeof(int));

	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read counter from userspace\n");
		err = -EFAULT;
		goto out;
	}
	evt.generic.type = OMX_EVT_TEST;
	for (i = 0; i < num; i++) {

		err = omx_notify_unexp_event(endpoint, &evt.test, sizeof evt.test);
		if (unlikely(err != 0))
			goto out;
	}
	return 0;
 out:
	return err;
}

int
omx_ioctl_wakeup(struct omx_endpoint * endpoint, void __user * uparam)
{
	struct omx_cmd_wakeup cmd;
	int err;

	err = copy_from_user(&cmd, uparam, sizeof(cmd));
	if (unlikely(err != 0)) {
		printk(KERN_ERR "Open-MX: Failed to read wakeup cmd hdr\n");
		err = -EFAULT;
		goto out;
	}

	spin_lock_bh(&endpoint->event_lock);
	omx_wakeup_waiter_list(endpoint, cmd.status);
	spin_unlock_bh(&endpoint->event_lock);

	return 0;

 out:
	return err;
}

void
omx_wakeup_endpoint_on_close(struct omx_endpoint * endpoint)
{
	omx_wakeup_waiter_list(endpoint, OMX_CMD_WAIT_EVENT_STATUS_WAKEUP);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
