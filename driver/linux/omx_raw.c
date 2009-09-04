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
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <asm/uaccess.h>

#include "omx_hal.h"
#include "omx_io.h"
#include "omx_common.h"
#include "omx_misc.h"
#include "omx_iface.h"
#include "omx_endpoint.h"
#include "omx_peer.h"

/*
 * The raw interface exists when the file descriptor gets the OMX_CMD_RAW_OPEN_ENDPOINT
 * ioctl. It links the file private_data onto the iface and the iface raw.opener_file onto
 * the file. Starting from then, the file owns a reference onto the iface.
 * The reference is released when the raw iface is closed, either by closing the file
 * descriptor, or by the underlying interface being removed by force.
 *
 * Attaching or detaching a raw iface is done while holding the main ifaces_mutex which
 * protects against multiple thread trying to attach to the same file.
 *
 * Other accesses to the raw iface (regular ioctls and the poll file operation) are
 * protected by RCU locking. They get a reference onto the iface first so that they
 * can finish proceedings while a closing is pending.
 * Closing waits for RCU grace to end before releasing the main reference onto the iface
 * so that RCU acquires have no chance of acquiring when the refcount is already 0.
 */

#ifdef OMX_DRIVER_DEBUG
/* defined as module parameters */
extern unsigned long omx_RAW_packet_loss;
/* index between 0 and the above limit */
static unsigned long omx_RAW_packet_loss_index = 0;
#endif /* OMX_DRIVER_DEBUG */

struct omx_raw_event {
	int status;
	uint64_t context;
	struct list_head list_elt;
	int data_length;
	char data[0];
};

/**********************************
 * Init/Finish the Raw of an Iface
 */

void
omx_iface_raw_init(struct omx_iface_raw * raw)
{
	raw->opener_file = NULL;
	spin_lock_init(&raw->event_lock);
	INIT_LIST_HEAD(&raw->event_list);
	raw->event_list_length = 0;
	init_waitqueue_head(&raw->event_wq);
}

void
omx_iface_raw_exit(struct omx_iface_raw * raw)
{
	struct omx_raw_event *event, *next;
	spin_lock_bh(&raw->event_lock);
	list_for_each_entry_safe(event, next, &raw->event_list, list_elt) {
		list_del(&event->list_elt);
		kfree(event);
	}
	raw->event_list_length = 0;
	spin_unlock_bh(&raw->event_lock);
}

/*******************
 * Send Raw Packets
 */

static void
omx_raw_wakeup(struct omx_iface *iface)
{
	wake_up_interruptible(&iface->raw.event_wq);
}

static int
omx_raw_send(struct omx_iface *iface, const void __user * uparam)
{
	struct omx_iface_raw *raw = &iface->raw;
	struct omx_cmd_raw_send raw_send;
	struct omx_raw_event *event = NULL;
	struct sk_buff *skb;
	int ret;

	ret = copy_from_user(&raw_send, uparam, sizeof(raw_send));
	if (unlikely(ret != 0)) {
		ret = -EFAULT;
		goto out;
	}

	ret = -ENOMEM;
	skb = omx_new_skb(raw_send.buffer_length);
	if (!skb)
		goto out;

	ret = copy_from_user(omx_skb_mac_header(skb), (void __user *)(unsigned long) raw_send.buffer, raw_send.buffer_length);
	if (unlikely(ret != 0)) {
		ret = -EFAULT;
		goto out_with_skb;
	}

	if (raw_send.need_event) {
		ret = -ENOMEM;
		event = kmalloc(sizeof(*event), GFP_KERNEL);
		if (!event)
			goto out_with_skb;

		event->status = OMX_CMD_RAW_EVENT_SEND_COMPLETE;
		event->data_length = 0;
		event->context = raw_send.context;

		spin_lock_bh(&raw->event_lock);
		list_add_tail(&event->list_elt, &raw->event_list);
		raw->event_list_length++;
		omx_raw_wakeup(iface);
		spin_unlock_bh(&raw->event_lock);
	}

	omx_queue_xmit(iface, skb, RAW);

	return 0;

 out_with_skb:
	kfree_skb(skb);
 out:
	return ret;
}

/**********************
 * Receive Raw Packets
 */

int
omx_recv_raw(struct omx_iface * iface,
	     struct omx_hdr * mh,
	     struct sk_buff * skb)
{
	struct omx_iface_raw *raw = &iface->raw;

	if (raw->event_list_length > OMX_RAW_RECVQ_LEN) {
		dev_kfree_skb(skb);
		omx_counter_inc(iface, DROP_RAW_QUEUE_FULL);
	} else if (skb->len > OMX_RAW_PKT_LEN_MAX) {
		dev_kfree_skb(skb);
		omx_counter_inc(iface, DROP_RAW_TOO_LARGE);
	} else {
		struct omx_raw_event *event;
		int length = min((unsigned) skb->len, (unsigned) OMX_RAW_PKT_LEN_MAX);

		event = kmalloc(sizeof(*event) + length, GFP_ATOMIC);
		if (!event)
			return -ENOMEM;

		event->status = OMX_CMD_RAW_EVENT_RECV_COMPLETE;
		event->data_length = length;
		skb_copy_bits(skb, 0, event->data, length);
		dev_kfree_skb(skb);

		spin_lock(&raw->event_lock);
		list_add_tail(&event->list_elt, &raw->event_list);
		raw->event_list_length++;
		omx_raw_wakeup(iface);
		spin_unlock(&raw->event_lock);

	        omx_counter_inc(iface, RECV_RAW);
	}

	return 0;
}

static int
omx_raw_get_event(struct omx_iface * iface, void __user * uparam)
{
	struct omx_iface_raw * raw = &iface->raw;
	struct omx_cmd_raw_get_event get_event;
	DEFINE_WAIT(__wait);
	unsigned long timeout;
	int err;

	err = copy_from_user(&get_event, uparam, sizeof(get_event));
	if (unlikely(err != 0))
		return -EFAULT;

	timeout = msecs_to_jiffies(get_event.timeout);
	get_event.status = OMX_CMD_RAW_NO_EVENT;

	spin_lock_bh(&raw->event_lock);
	while (timeout > 0) {
		prepare_to_wait(&raw->event_wq, &__wait, TASK_INTERRUPTIBLE);

		if (unlikely(iface->status != OMX_IFACE_STATUS_OK))
			/* getting closed */
			break;

		if (raw->event_list_length)
			/* got an event */
			break;

		if (signal_pending(current))
			/* got interrupted */
			break;

		spin_unlock_bh(&raw->event_lock);
		timeout = schedule_timeout(timeout);
		spin_lock_bh(&raw->event_lock);
	}
	finish_wait(&raw->event_wq, &__wait);

	if (!raw->event_list_length) {
		spin_unlock_bh(&raw->event_lock);
		/* got a timeout or interrupted */

	} else {
		struct omx_raw_event * event;

		event = list_entry(raw->event_list.next, struct omx_raw_event, list_elt);
		list_del(&event->list_elt);
		raw->event_list_length--;
		spin_unlock_bh(&raw->event_lock);

		/* fill the event */
		get_event.status = event->status;
		get_event.context = event->context;
		get_event.buffer_length = event->data_length;

		/* copy into user-space */
		err = copy_to_user((void __user *)(unsigned long) get_event.buffer,
				   event->data, event->data_length);
		if (unlikely(err != 0)) {
			err = -EFAULT;
			kfree(event);
			goto out;
		}

		kfree(event);
	}

	get_event.timeout = jiffies_to_msecs(timeout);

	err = copy_to_user(uparam, &get_event, sizeof(get_event));
	if (unlikely(err != 0)) {
		err = -EFAULT;
		goto out;
	}

	return 0;

 out:
	return err;
}

/****************
 * Raw Interface
 */

static int
omx_raw_attach_iface(uint32_t board_index, struct file * filp)
{
	struct omx_iface * iface;
	int err;

	iface = omx_iface_find_by_index_lock(board_index);
	err = -EINVAL;
	if (!iface)
		goto out;

	err = -EBUSY;
	if (filp->private_data)
		goto out_with_lock;

	if (iface->raw.opener_file)
		goto out_with_lock;

	omx_iface_reacquire(iface);
	rcu_assign_pointer(iface->raw.opener_file, filp);
	rcu_assign_pointer(filp->private_data, iface);
	iface->raw.opener_pid = current->pid;
	strncpy(iface->raw.opener_comm, current->comm, TASK_COMM_LEN);

	omx_ifaces_peers_unlock();
	return 0;

 out_with_lock:
	omx_ifaces_peers_unlock();
 out:
	return err;
}

/* Called with the iface array locked */
void
omx__raw_detach_iface_locked(struct omx_iface *iface)
{
	struct file *filp = iface->raw.opener_file;

	if (filp) {
		filp->private_data = NULL;
		iface->raw.opener_file = NULL;
		omx_raw_wakeup(iface);

		/* wait for other people that dereferenced the raw iface to have acquired it before we release it */
		synchronize_rcu();

		omx_iface_release(iface);
	}
}

static int
omx_raw_detach_iface(struct file *filp)
{
	struct omx_iface * iface;
	int err;

	omx_ifaces_peers_lock();

	err = -EINVAL;
	iface = filp->private_data;
	if (!iface)
		goto out_with_lock;

	BUG_ON(!iface->raw.opener_file);
	omx__raw_detach_iface_locked(iface);

	omx_ifaces_peers_unlock();
	return 0;

 out_with_lock:
	omx_ifaces_peers_unlock();
	return err;
}

/****************************
 * Raw MiscDevice operations
 */

static struct omx_iface *
omx_acquire_iface_from_raw_file(const struct file * file)
{
	struct omx_iface * iface;

	rcu_read_lock();

	iface = rcu_dereference(file->private_data);
	if (unlikely(!iface || iface->status != OMX_IFACE_STATUS_OK)) {
		rcu_read_unlock();
		return NULL;
	}
	omx_iface_reacquire(iface);

	rcu_read_unlock();

	return iface;
}

static int
omx_raw_miscdev_open(struct inode * inode, struct file * file)
{
	file->private_data = NULL;
	return 0;
}

static int
omx_raw_miscdev_release(struct inode * inode, struct file * file)
{
	return omx_raw_detach_iface(file);
}

static long
omx_raw_miscdev_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	struct omx_iface *iface;
	int err = 0;

	switch (cmd) {
	case OMX_CMD_RAW_OPEN_ENDPOINT: {
		struct omx_cmd_raw_open_endpoint raw_open;

		err = copy_from_user(&raw_open, (void __user *) arg, sizeof(raw_open));
		if (unlikely(err != 0)) {
			err = -EFAULT;
			goto out;
		}

		err = omx_raw_attach_iface(raw_open.board_index, file);
		break;
	}

	case OMX_CMD_RAW_SEND: {
		iface = omx_acquire_iface_from_raw_file(file);
		err = -EBADF;
		if (!iface)
			goto out;

		err = omx_raw_send(iface, (void __user *) arg);

		omx_iface_release(iface);
		break;
	}

	case OMX_CMD_RAW_GET_EVENT: {
		iface = omx_acquire_iface_from_raw_file(file);
		err = -EBADF;
		if (!iface)
			goto out;

		err = omx_raw_get_event(iface, (void __user *) arg);

		omx_iface_release(iface);
		break;
	}

	default:
		err = -ENOSYS;
		break;
	}

 out:
	return err;
}

static unsigned int
omx_raw_miscdev_poll(struct file *file, struct poll_table_struct *wait)
{
	struct omx_iface *iface;
	struct omx_iface_raw *raw;
	unsigned int mask = 0;

	iface = omx_acquire_iface_from_raw_file(file);
	if (!iface) {
		mask |= POLLERR;
		goto out;
	}

	raw = &iface->raw;

	poll_wait(file, &raw->event_wq, wait);
	if (raw->event_list_length)
		mask |= POLLIN;
	if (unlikely(iface->status != OMX_IFACE_STATUS_OK))
		mask |= POLLERR;

	omx_iface_release(iface);

 out:
	return mask;
}

static struct file_operations
omx_raw_miscdev_fops = {
	.owner = THIS_MODULE,
	.open = omx_raw_miscdev_open,
	.release = omx_raw_miscdev_release,
	.unlocked_ioctl = omx_raw_miscdev_ioctl,
	.poll = omx_raw_miscdev_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl = omx_raw_miscdev_ioctl,
#endif
};

static struct miscdevice
omx_raw_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "open-mx-raw",
	.fops = &omx_raw_miscdev_fops,
};

/******************************
 * Device registration
 */

int
omx_raw_init(void)
{
	int ret;

	ret = misc_register(&omx_raw_miscdev);
	if (ret < 0) {
		printk(KERN_ERR "Open-MX: Failed to register raw misc device, error %d\n", ret);
		goto out;
	}

	return 0;

 out:
	return ret;
}

void
omx_raw_exit(void)
{
	misc_deregister(&omx_raw_miscdev);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
