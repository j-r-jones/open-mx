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

#ifndef __omx_endpoints_h__
#define __omx_endpoints_h__

#include <linux/kref.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>

struct omx_endpoint {
	struct kref refcount;

	struct omx_iface *iface;
	struct file *file;

	u32 endpoint_index;
	u32 iface_index;
};

struct omx_peer {
	unsigned char eth_hdr[ETH_ALEN]; /* the mac address, as stored in ethernet header */
	char *hostname;
	struct omx_iface *local_iface;
};

struct omx_iface {
	struct kref refcount;

	struct net_device *netdev;

	struct omx_peer peer;

	struct omx_endpoint **endpoints;
	unsigned endpoints_count;

	u32 iface_index;
};

extern struct omx_iface **omx_ifaces;
extern unsigned omx_ifaces_count;

extern unsigned omx_ifaces_max;
extern unsigned omx_endpoints_max;

extern int omx_device_init(void);
extern void omx_device_exit(void);

extern int omx_endpoints_init(void);
extern void omx_endpoints_exit(void);

extern int omx_ifnames_get(char *buf, size_t buflen, char sep);
extern int omx_ifnames_get_kp(char *buf, struct kernel_param *kp);
extern int omx_ifnames_set_kp(const char *buf, struct kernel_param *kp);

#endif /* __omx_endpoints_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
