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

#include "omx_endpoints.h"

module_param_call(ifnames, omx_ifnames_set_kp, omx_ifnames_get_kp, NULL, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ifnames, "Interfaces to attach on startup, comma-separated");

unsigned omx_ifaces_max = 32;
module_param_named(ifaces, omx_ifaces_max, uint, S_IRUGO);
MODULE_PARM_DESC(ifaces, "Maximum number of attached interfaces");

unsigned omx_endpoints_max = 32;
module_param_named(endpoints, omx_endpoints_max, uint, S_IRUGO);
MODULE_PARM_DESC(endpoints, "Maximum number of endpoints per interface");

/************************
 * Main Module Init/Exit
 */

#ifdef OMX_SRC_VERSION
#define OMX_DRIVER_VERSION PACKAGE_VERSION " (" OMX_SRC_VERSION ")"
#else
#define OMX_DRIVER_VERSION PACKAGE_VERSION
#endif

int omx_init(void)
{
	int err;

	printk(KERN_INFO "Open-MX " OMX_DRIVER_VERSION " initializing...\n");

	err = omx_endpoints_init();
	if (err < 0)
		goto out;

	err = omx_device_init();
	if (err < 0)
		goto out_with_endpoints;

	printk(KERN_INFO "Open-MX initialized\n");
	return 0;

 out_with_endpoints:
	omx_endpoints_exit();
 out:
	return err;
}
module_init(omx_init);

void omx_exit(void)
{
	printk(KERN_INFO "Open-MX terminating...\n");

	/* TODO, force close everything */
	omx_device_exit();
	omx_endpoints_exit();

	printk(KERN_INFO "Open-MX " OMX_DRIVER_VERSION " terminated\n");
}
module_exit(omx_exit);

/***************
 * Module Infos
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brice Goglin <Brice.Goglin@inria.fr>");
MODULE_VERSION(OMX_DRIVER_VERSION);
MODULE_DESCRIPTION(PACKAGE_NAME ": Myrinet Express over generic Ethernet");

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
