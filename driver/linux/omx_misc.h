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

#ifndef __omx_misc_h__
#define __omx_misc_h__

#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include "omx_io.h"
#include "omx_wire.h"

/* set/get a skb destructor and its data */
static inline void
omx_set_skb_destructor(struct sk_buff *skb, void (*callback)(struct sk_buff *skb), void * data)
{
	skb->destructor = callback;
	skb->sk = data;
}

static inline void *
omx_get_skb_destructor_data(struct sk_buff *skb)
{
	return (void *) skb->sk;
}

/* queue a skb for xmit, account it, and eventually actually drop it for debugging */
#define __omx_queue_xmit(iface, skb, type)	\
do {						\
	omx_counter_inc(iface, SEND_##type);	\
	skb->dev = iface->eth_ifp;		\
	dev_queue_xmit(skb);			\
} while (0)

#ifdef OMX_DRIVER_DEBUG
#define omx_queue_xmit(iface, skb, type)					\
	do {									\
	if (omx_##type##_packet_loss &&						\
	    (++omx_##type##_packet_loss_index >= omx_##type##_packet_loss)) {	\
		kfree_skb(skb);							\
		omx_##type##_packet_loss_index = 0;				\
	} else {								\
		__omx_queue_xmit(iface, skb, type);				\
	}									\
} while (0)
#else /* OMX_DRIVER_DEBUG */
#define omx_queue_xmit __omx_queue_xmit
#endif /* OMX_DRIVER_DEBUG */

/* translate omx_endpoint_acquire_by_iface_index return values into nack type */
static inline enum omx_nack_type
omx_endpoint_acquire_by_iface_index_error_to_nack_type(void * errptr)
{
	switch (PTR_ERR(errptr)) {
	case -EINVAL:
		return OMX_NACK_TYPE_BAD_ENDPT;
	case -ENOENT:
		return OMX_NACK_TYPE_ENDPT_CLOSED;
	}

	BUG();
	return 0; /* shut-up the compiler */
}

/* manage addresses */
static inline uint64_t
omx_board_addr_from_netdevice(struct net_device * ifp)
{
	return (((uint64_t) ifp->dev_addr[0]) << 40)
	     + (((uint64_t) ifp->dev_addr[1]) << 32)
	     + (((uint64_t) ifp->dev_addr[2]) << 24)
	     + (((uint64_t) ifp->dev_addr[3]) << 16)
	     + (((uint64_t) ifp->dev_addr[4]) << 8)
	     + (((uint64_t) ifp->dev_addr[5]) << 0);
}

static inline uint64_t
omx_board_addr_from_ethhdr_src(struct ethhdr * eh)
{
	return (((uint64_t) eh->h_source[0]) << 40)
	     + (((uint64_t) eh->h_source[1]) << 32)
	     + (((uint64_t) eh->h_source[2]) << 24)
	     + (((uint64_t) eh->h_source[3]) << 16)
	     + (((uint64_t) eh->h_source[4]) << 8)
	     + (((uint64_t) eh->h_source[5]) << 0);
}

static inline void
omx_board_addr_to_ethhdr_dst(struct ethhdr * eh, uint64_t board_addr)
{
	eh->h_dest[0] = (uint8_t)((board_addr >> 40 & 0xff));
	eh->h_dest[1] = (uint8_t)((board_addr >> 32 & 0xff));
	eh->h_dest[2] = (uint8_t)((board_addr >> 24 & 0xff));
	eh->h_dest[3] = (uint8_t)((board_addr >> 16 & 0xff));
	eh->h_dest[4] = (uint8_t)((board_addr >> 8 & 0xff));
	eh->h_dest[5] = (uint8_t)((board_addr >> 0 & 0xff));
}

#endif /* __omx_misc_h__ */

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
