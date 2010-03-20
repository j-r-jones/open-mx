/*
 * Open-MX
 * Copyright © INRIA, CNRS 2007-2010 (see AUTHORS file)
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

#ifndef __omx_io_h__
#define __omx_io_h__

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

/************
 * Constants
 */

#define OMX_HOSTNAMELEN_MAX	80
#define OMX_IF_NAMESIZE		16

/************************
 * IOCTL parameter types
 */

struct omx_cmd_set_endpoint {
	uint8_t iface_index;
	uint8_t endpoint_index;
	uint8_t pad[6];
	/* 8 */
};

/************************
 * IOCTL commands
 */

#define OMX_CMD_MAGIC	'O'
#define OMX_CMD_INDEX(x)	_IOC_NR(x)

#define OMX_CMD_SET_ENDPOINT	_IOR(OMX_CMD_MAGIC, 0x70, struct omx_cmd_set_endpoint)
#define OMX_CMD_UNSET_ENDPOINT	_IO(OMX_CMD_MAGIC,0x71)

/* TODO __pure */
static inline const char *
omx_strcmd(unsigned cmd)
{
	switch (cmd) {
	case OMX_CMD_SET_ENDPOINT: return "Set Endpoint";
	case OMX_CMD_UNSET_ENDPOINT: return "Unset Endpoint";
        default: return "** Unknown Command **";
	}
}

#endif /* __omx_io_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */

