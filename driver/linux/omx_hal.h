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

#ifndef OMX_HAVE_MUTEX
/* mutex introduced in 2.6.16 */
#include <asm/semaphore.h>
#define mutex semaphore
#define mutex_init(m) sema_init(m, 1)
#define mutex_lock(m) down(m)
#define mutex_unlock(m) up(m)
#endif

#include <linux/utsname.h>
#ifdef OMX_HAVE_TASK_STRUCT_NSPROXY
/* task_struct ns_proxy introduced in 2.6.19 */
#define omx_current_utsname current->nsproxy->uts_ns->name
#else
#define omx_current_utsname system_utsname
#endif

#ifdef OMX_HAVE_FOR_EACH_NETDEV
#define omx_for_each_netdev(_ifp) for_each_netdev(&init_net, _ifp)
#elif defined OMX_HAVE_FOR_EACH_NETDEV_WITHOUT_NS
#define omx_for_each_netdev(_ifp) for_each_netdev(_ifp)
#else /* !OMX_HAVE_FOR_EACH_NETDEV && !OMX_HAVE_FOR_EACH_NETDEV_WITHOUT_NS */
#define omx_for_each_netdev(_ifp) for ((_ifp) = dev_base; (_ifp) != NULL; (_ifp) = (_ifp)->next)
#endif /* !OMX_HAVE_FOR_EACH_NETDEV && !OMX_HAVE_FOR_EACH_NETDEV_WITHOUT_NS */

#ifdef OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS
#define omx_dev_get_by_name dev_get_by_name
#else /* !OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS */
#define omx_dev_get_by_name(name) dev_get_by_name(&init_net, name)
#endif /* !OMX_HAVE_DEV_GET_BY_NAME_WITHOUT_NS */

#endif /* __omx_hal_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
