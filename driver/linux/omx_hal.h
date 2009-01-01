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

#ifndef __omx_hal_h__
#define __omx_hal_h__

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

#ifdef OMX_HAVE_REMAP_VMALLOC_RANGE
#define omx_vmalloc_user vmalloc_user
#define omx_remap_vmalloc_range remap_vmalloc_range
#else /* OMX_HAVE_REMAP_VMALLOC_RANGE */
static inline void *
omx_vmalloc_user(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO, PAGE_KERNEL);
}

static inline int
omx_remap_vmalloc_range(struct vm_area_struct *vma, void *addr, unsigned long pgoff)
{
	unsigned long uaddr = vma->vm_start;
	unsigned long usize = vma->vm_end - vma->vm_start;
	int ret;

	addr += pgoff << PAGE_SHIFT;
	do {
		struct page *page = vmalloc_to_page(addr);
		ret = vm_insert_page(vma, uaddr, page);
		if (ret)
			return ret;

		uaddr += PAGE_SIZE;
		addr += PAGE_SIZE;
		usize -= PAGE_SIZE;
	} while (usize > 0);

	/* Prevent "things" like memory migration? VM_flags need a cleanup... */
	vma->vm_flags |= VM_RESERVED;

	return ret;
}
#endif /* OMX_HAVE_REMAP_VMALLOC_RANGE */

#ifdef OMX_HAVE_FOR_EACH_NETDEV
#define omx_for_each_netdev(_ifp) for_each_netdev(&init_net, _ifp)
#elif defined OMX_HAVE_FOR_EACH_NETDEV_WITHOUT_NS
#define omx_for_each_netdev(_ifp) for_each_netdev(_ifp)
#else /* OMX_HAVE_FOR_EACH_NETDEV */
#define omx_for_each_netdev(_ifp) for ((_ifp) = dev_base; (_ifp) != NULL; (_ifp) = (_ifp)->next)
#endif /* OMX_HAVE_FOR_EACH_NETDEV */

#ifdef OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS
#define omx_dev_get_by_name dev_get_by_name
#else /* OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS */
#define omx_dev_get_by_name(name) dev_get_by_name(&init_net, name)
#endif /* OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS */

#ifdef OMX_HAVE_SKB_HEADERS
#define omx_skb_reset_mac_header skb_reset_mac_header
#define omx_skb_reset_network_header skb_reset_network_header
#define omx_skb_mac_header(skb) ((struct omx_hdr *) skb_mac_header(skb))
#else /* OMX_HAVE_SKB_HEADERS */
#define omx_skb_reset_mac_header(skb) skb->mac.raw = skb->data
#define omx_skb_reset_network_header(skb) skb->nh.raw = skb->mac.raw
#define omx_skb_mac_header(skb) ((struct omx_hdr *) skb->mac.raw)
#endif /* OMX_HAVE_SKB_HEADERS */

#ifdef OMX_HAVE_TASK_STRUCT_NSPROXY
/* task_struct ns_proxy introduced in 2.6.19 */
#define omx_current_utsname current->nsproxy->uts_ns->name
#else
#define omx_current_utsname system_utsname
#endif

#ifndef OMX_HAVE_MUTEX
/* mutex introduced in 2.6.16 */
#include <asm/semaphore.h>
#define mutex semaphore
#define mutex_init(m) sema_init(m, 1)
#define mutex_lock(m) down(m)
#define mutex_unlock(m) up(m)
#endif

/* list_first_entry appeared in 2.6.22 */
#ifndef list_first_entry
#define list_first_entry(ptr, type, member) \
list_entry((ptr)->next, type, member)
#endif

/* net_device switch from class_device to device in 2.6.21 */
#ifdef OMX_HAVE_NETDEVICE_CLASS_DEVICE
#define omx_ifp_to_dev(ifp) (ifp)->class_dev.dev
#else
#define omx_ifp_to_dev(ifp) (ifp)->dev.parent
#endif

/* dev_to_node appeared in 2.6.20 */
#ifdef OMX_HAVE_DEV_TO_NODE
static inline int
omx_ifp_node(struct net_device *ifp)
{
  struct device *dev = omx_ifp_to_dev(ifp);
  return dev ? dev_to_node(dev) : -1;
}
#else
#define omx_ifp_node(ifp) -1
#endif

/* work_struct switch to container_of in 2.6.20 */
#ifdef OMX_HAVE_WORK_STRUCT_DATA
#define OMX_INIT_WORK(_work, _func, _data) INIT_WORK(_work, _func, _data)
#define OMX_WORK_STRUCT_DATA(_data, _type, _field) (_data)
typedef void * omx_work_struct_data_t;
#else
#define OMX_INIT_WORK(_work, _func, _data) INIT_WORK(_work, _func)
#define OMX_WORK_STRUCT_DATA(_data, _type, _field) container_of(_data, _type, _field)
typedef struct work_struct * omx_work_struct_data_t;
#endif

/* 64bits jiffies comparison routines appeared in 2.6.19 */
#include <linux/jiffies.h>
#ifndef time_after64
#define time_after64(a,b)	\
	(typecheck(__u64, a) &&	\
	 typecheck(__u64, b) &&	\
	((__s64)(b) - (__s64)(a) < 0))
#define time_before64(a,b)	time_after64(b,a)

#define time_after_eq64(a,b)	\
	(typecheck(__u64, a) &&	\
	 typecheck(__u64, b) &&	\
	((__s64)(a) - (__s64)(b) >= 0))
#define time_before_eq64(a,b) 	time_after_eq64(b,a)
#endif

#endif /* __omx_hal_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
