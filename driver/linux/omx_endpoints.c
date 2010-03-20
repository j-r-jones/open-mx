/*
 * Open-MX
 * Copyright © INRIA 2007-2010 (see AUTHORS file)
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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include "omx_endpoints.h"
#include "omx_hal.h"
#include "omx_io.h"

struct mutex omx_mutex;
struct omx_iface **omx_ifaces = NULL; /* initialized to NULL to properly handle omx_module_load_ifnames */
unsigned omx_ifaces_count = 0;
static char *omx_module_load_ifnames = NULL;

/******************************
 * Actual freeing of resources
 */

static void
__omx_iface_last_put(struct kref *kref)
{
	struct omx_iface * iface = container_of(kref, struct omx_iface, refcount);
printk("releasing last ref on iface %d %s\n", iface->iface_index, iface->netdev->name);
	dev_put(iface->netdev);
	kfree(iface);
}

static void
__omx_endpoint_last_put(struct kref *kref)
{
	struct omx_endpoint * endpoint = container_of(kref, struct omx_endpoint, refcount);
printk("releasing last ref on endpoint %d %d\n", endpoint->endpoint_index, endpoint->iface_index);
	kref_put(&endpoint->iface->refcount, __omx_iface_last_put);
	kfree(endpoint);
}

/********************************
 * Attaching/Detaching endpoints
 */

static struct omx_endpoint *
omx_endpoint_attach(struct file *file, u32 iface_index, u32 endpoint_index)
{
	struct omx_iface *iface;
	struct omx_endpoint *endpoint;
	int err;

	/* FIXME: support wildcards */

	err = -EINVAL;
	if (iface_index >= omx_ifaces_max || endpoint_index >= omx_endpoints_max)
		goto out;

	endpoint = kzalloc(sizeof(*endpoint), GFP_KERNEL);
	err = -ENOMEM;
	if (!endpoint)
		goto out;
	kref_init(&endpoint->refcount);
	endpoint->iface_index = iface_index;
	endpoint->endpoint_index = endpoint_index;

	mutex_lock(&omx_mutex);

	err = -EBUSY;
	if (file->private_data)
		goto out_with_mutex;

	iface = omx_ifaces[iface_index];
	err = -EINVAL;
	if (!iface)
		goto out_with_mutex;

	err = -EBUSY;
	if (iface->endpoints[endpoint_index])
		goto out_with_mutex;

	/* link to the file and iface */
	rcu_assign_pointer(endpoint->file, file);
	rcu_assign_pointer(file->private_data, endpoint);
	rcu_assign_pointer(endpoint->iface, iface);
	rcu_assign_pointer(iface->endpoints[endpoint_index], endpoint);
	/* increase iface use counts */
	iface->endpoints_count++;
	kref_get(&iface->refcount);

	mutex_unlock(&omx_mutex);

	return endpoint;

 out_with_mutex:
	mutex_unlock(&omx_mutex);
	kfree(endpoint);
 out:
	return ERR_PTR(err);
}

static void
omx_endpoint_detach_locked(struct omx_endpoint *endpoint)
{
	struct omx_iface *iface = endpoint->iface;
	struct file *file = endpoint->file;

	/* sanity checks, all these links must be coherent */
	BUG_ON(!endpoint);
	BUG_ON(iface && !file);
	BUG_ON(file && !iface);

	/* unlink FROM the iface */
	rcu_assign_pointer(iface->endpoints[endpoint->endpoint_index], NULL);
	iface->endpoints_count--;
	/* keep the link TO the iface until the last reference is released */

	/* unlink FROM the file */
	rcu_assign_pointer(file->private_data, NULL);
	kref_put(&endpoint->refcount, __omx_endpoint_last_put);
	/* unlink TO the file, it's only used to unlink FROM (above) anyway */
	rcu_assign_pointer(endpoint->file, NULL);
}

static int
omx_endpoint_detach_by_file(struct file *file)
{
	struct omx_endpoint *endpoint;
	int err = 0;

	mutex_lock(&omx_mutex);
	endpoint = file->private_data;
	if (endpoint)
		omx_endpoint_detach_locked(endpoint);
	else
		err = -EINVAL;
	mutex_unlock(&omx_mutex);

	return err;
}

/******************************
 * Attaching/Detaching interfaces
 */

/* Attach the given netdev to a new iface.
 * A reference to the netdev is already hold, and the omx_mutex is hold.
 */
static int
omx_iface_attach_locked(struct net_device * netdev)
{
	struct omx_iface * iface;
	int ret;
	int i;

	if (omx_ifaces_count == omx_ifaces_max) {
		printk(KERN_ERR "Open-MX: Too many interfaces already attached\n");
		ret = -EBUSY;
		goto out;
	}

	for(i=0; i<omx_ifaces_max; i++)
		if (omx_ifaces[i] && omx_ifaces[i]->netdev == netdev) {
			printk(KERN_ERR "Open-MX: Interface '%s' already attached\n", netdev->name);
			ret = -EBUSY;
			goto out;
		}

	for(i=0; i<omx_ifaces_max; i++)
		if (omx_ifaces[i] == NULL)
			break;

	iface = kzalloc(sizeof(struct omx_iface), GFP_KERNEL);
	if (!iface) {
		printk(KERN_ERR "Open-MX: Failed to allocate interface as board %d\n", i);
		ret = -ENOMEM;
		goto out;
	}

	/* TODO reverse index table */

//	printk(KERN_INFO "Open-MX: Attaching %sEthernet interface '%s' as #%i, MTU=%d\n",
//		(netdev->type == ARPHRD_ETHER ? "" : "non-"), netdev->name, i, mtu);

	/* TODO pci driver */

	/* TODO check up, mtu, coalesce */

	/* TODO hostname */

	kref_init(&iface->refcount);
	iface->netdev = netdev;
	iface->endpoints_count = 0;
	iface->endpoints = kzalloc(omx_endpoints_max * sizeof(struct omx_endpoint *), GFP_KERNEL);
	if (!iface->endpoints) {
		printk(KERN_ERR "Open-MX:   Failed to allocate interface endpoint pointers\n");
		ret = -ENOMEM;
		goto out_with_iface;
	}

	/* TODO raw */

	/* TODO peer table */

	/* link the iface to the iface array */
	iface->iface_index = i;
	rcu_assign_pointer(omx_ifaces[i], iface);
	omx_ifaces_count++;

	return 0;

 out_with_iface:
	kfree(iface);
 out:
	return ret;
}

static inline int
omx_iface_attach(struct net_device * netdev)
{
	int err;
	mutex_lock(&omx_mutex);
	err = omx_iface_attach_locked(netdev);
	mutex_unlock(&omx_mutex);
	return err;
}

/* Detach the given iface from the array.
 * The omx_mutex must be hold.
 * A reference to the netdev is still hold, it will be released with the last iface reference.
 */
static int
omx_iface_detach_locked(struct omx_iface *iface, int force)
{
	int i;

	if (!force && iface->endpoints_count) {
		printk(KERN_INFO "Open-MX: cannot detach interface '%s' (#%d), still %d endpoints open\n",
		       iface->netdev->name, iface->iface_index, iface->endpoints_count);
		return -EBUSY;
	}

	/* Detach currently open endpoints */
	for(i=0; i<omx_endpoints_max && iface->endpoints_count>0; i++)
		omx_endpoint_detach_locked(iface->endpoints[i]);

	/* Unlink from the iface array and let the last reference release things */
	rcu_assign_pointer(omx_ifaces[iface->iface_index], NULL);
	kref_put(&iface->refcount, __omx_iface_last_put);

	return 0;
}

/****************************************************
 * Attribute-based attaching/detaching of interfaces
 */

/*
 * Format a buffer containing the list of attached ifaces.
 */
int
omx_ifnames_get(char *buf, size_t buflen, char sep)
{
	int total = 0;
	int i;

	rcu_read_lock();

	for (i=0; i<omx_ifaces_max; i++) {
		struct omx_iface * iface = rcu_dereference(omx_ifaces[i]);
		if (iface) {
			char * ifname = iface->netdev->name;
			int length = strlen(ifname);
			if (total + length + 2 > buflen) {
				printk(KERN_ERR "Open-MX: Failed to get all interface names within %ld bytes, ignoring the last ones\n",
				       (unsigned long) buflen);
				break;
			}
			strcpy(buf, ifname);
			buf += length;
			*(buf++) = sep;
			*buf = '\0';
			total += length+1;
		}
	}

	rcu_read_unlock();

	return total;
}

int
omx_ifnames_get_kp(char *buf, struct kernel_param *kp)
{
	return omx_ifnames_get(buf, PAGE_SIZE, '\n') + 1 /* ending '\0' */;
}

/*
 * Attach/Detach one iface depending on the given string.
 *
 * name or +name adds an iface
 * -name removes one
 * --name removes one by force, even if some endpoints are open
 */
static int
omx_ifaces_store_one(const char *buf)
{
	int ret = 0;

	if (buf[0] == '-') {
		const char *ifname;
		int force = 0;
		int i;

		/* if none matches, we return -EINVAL.
		 * if one matches, it sets ret accordingly.
		 */
		ret = -EINVAL;

		/* remove the first '-' and possibly another one for force */
		ifname = buf+1;
		if (ifname[0] == '-') {
			ifname++;
			force = 1;
		}

		mutex_lock(&omx_mutex);
		for(i=0; i<omx_ifaces_max; i++) {
			struct omx_iface * iface = omx_ifaces[i];
			struct net_device * netdev;

			if (iface == NULL)
				continue;

			netdev = iface->netdev;
			if (strcmp(netdev->name, ifname))
				continue;

			ret = omx_iface_detach_locked(iface, force);
			break;
		}
		mutex_unlock(&omx_mutex);

		if (ret == -EINVAL)
			printk(KERN_ERR "Open-MX: Cannot find any attached interface '%s' to detach\n",
			       ifname);

	} else {
		const char *ifname = buf;
		struct net_device * netdev;

		/* remove the first optional '+' */
		if (buf[0] == '+')
			ifname++;

		netdev = omx_dev_get_by_name(ifname);
		if (netdev) {
			ret = omx_iface_attach(netdev);
			if (ret < 0)
				dev_put(netdev);
		} else {
			printk(KERN_ERR "Open-MX: Cannot find interface '%s' to attach\n",
				ifname);
		}
	}

	return ret;
}

/*
 * Attach/Detach one or multiple ifaces depending on the given string,
 * comma- or \n-separated, and \0-terminated.
 */
static void
omx_ifaces_store(const char *buf)
{
	const char *ptr = buf;

	while (1) {
		char tmp[OMX_IF_NAMESIZE+2];
		size_t len;

		len = strcspn(ptr, ",\n\0");
		if (!len)
			goto next;
		if (len >= sizeof(tmp))
			goto next;

		/* copy the word in tmp, and keep one byte to add the ending \0 */
		strncpy(tmp, ptr, len);
		tmp[len] = '\0';
		omx_ifaces_store_one(tmp);

	next:
		ptr += len;
		if (*ptr == '\0')
			break;
		ptr++;
	}
}

int
omx_ifnames_set_kp(const char *buf, struct kernel_param *kp)
{
	if (omx_ifaces) {
		/* module parameter values are guaranteed to be \0-terminated */
		omx_ifaces_store(buf);
	} else {
		/* the module init isn't done yet, let the string be parsed later */
		omx_module_load_ifnames = kstrdup(buf, GFP_KERNEL);
	}
	return 0;
}

/****************************************************************
 * Endpoints and ifaces resources initialization and termination
 */

int omx_endpoints_init(void)
{
	if (!omx_ifaces_max || !omx_endpoints_max)
		return -EINVAL;

	omx_ifaces = kzalloc(omx_ifaces_max * sizeof(*omx_ifaces), GFP_KERNEL);
	if (!omx_ifaces)
		return -ENOMEM;
	omx_ifaces_count = 0;

	mutex_init(&omx_mutex);

	if (omx_module_load_ifnames && strcmp(omx_module_load_ifnames, "all")) {
		/* attach ifaces whose name are in ifnames (limited to omx_iface_max) */
		printk(KERN_INFO "Open-MX: attaching interfaces listed in '%s'...\n", omx_module_load_ifnames);

		/* module parameter values are guaranteed to be \0-terminated */
		omx_ifaces_store(omx_module_load_ifnames);
		kfree(omx_module_load_ifnames);

	} else {
		/* attach everything ethernet/up/large-mtu (limited to omx_iface_max) */
		struct net_device * netdev;

		printk(KERN_INFO "Open-MX: attaching all valid interfaces...\n");

		read_lock(&dev_base_lock);
		omx_for_each_netdev(netdev) {
			/* TODO check that it is an Ethernet device, that it is up, and that the MTU is large enough */

			dev_hold(netdev);
			if (omx_iface_attach(netdev) < 0) {
				dev_put(netdev);
				break;
			}
		}
		read_unlock(&dev_base_lock);
	}

	return 0;
}

void omx_endpoints_exit(void)
{
	int err;
	int i;

	mutex_lock(&omx_mutex);
	/* there cannot be any endpoint anymore since the file is closed when the module is unloaded */
	for (i=0; i<omx_ifaces_max && omx_ifaces_count>0; i++)
		if (omx_ifaces[i]) {
			err = omx_iface_detach_locked(omx_ifaces[i], 0);
			BUG_ON(err == -EBUSY);
		}
	mutex_unlock(&omx_mutex);

	kfree(omx_ifaces);

	/* wait for last references on endpoints and ifaces to be released */
	synchronize_rcu();
}

/**************
 * Misc Device
 */

static int
omx_miscdev_open(struct inode * inode, struct file * file)
{
	rcu_assign_pointer(file->private_data, NULL);
	return 0;
}

static int
omx_miscdev_release(struct inode * inode, struct file * file)
{
	int err;
	err = omx_endpoint_detach_by_file(file);
	/* ignore failure, the endpoint might have been detached earlier */
	return 0;
}

static long
omx_miscdev_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	int err = 0;

	switch (cmd) {
	case OMX_CMD_SET_ENDPOINT: {
		struct omx_cmd_set_endpoint set;
		struct omx_endpoint *endpoint;

		err = copy_from_user(&set, (const void __user *) arg, sizeof(set));
		if (err != 0) {
			err = -EFAULT;
			goto out;
		}

		endpoint = omx_endpoint_attach(file, set.iface_index, set.endpoint_index);
		if (IS_ERR(endpoint))
			err = PTR_ERR(endpoint);

		break;
	}
	case OMX_CMD_UNSET_ENDPOINT: {
		err = omx_endpoint_detach_by_file(file);
		break;
	}
	default:
		err = -ENOSYS;
		break;
	}

 out:
	return err;
}

static struct file_operations
omx_miscdev_fops = {
	.owner = THIS_MODULE,
	.open = omx_miscdev_open,
	.release = omx_miscdev_release,
	.unlocked_ioctl = omx_miscdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = omx_miscdev_ioctl,
#endif
};

static struct miscdevice
omx_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "open-mx",
	.fops = &omx_miscdev_fops,
};

int omx_device_init(void)
{
	int err;

	err = misc_register(&omx_miscdev);
	if (err < 0) {
		printk(KERN_ERR "Open-MX: Failed to register misc device, error %d\n", err);
		goto out;
	}

	return 0;

 out:
	return err;
}

void omx_device_exit(void)
{
	misc_deregister(&omx_miscdev);
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
