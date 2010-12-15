/*
 * Open-MX
 * Copyright © INRIA 2007-2010 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#ifndef __omx_lib_h__
#define __omx_lib_h__

#if OMX_LIB_DLMALLOC
#define USE_DL_PREFIX 1
#include "dlmalloc.h"
#define omx_malloc dlmalloc
#define omx_calloc dlcalloc
#define omx_free   dlfree
#define omx_malloc_ep(ep,size) dlmalloc(size)
#define omx_calloc_ep(ep,nb_elt,size_elt) dlcalloc(nb_elt,size_elt)
#define omx_free_ep(ep,ptr) dlfree(ptr)
#else /* !OMX_LIB_DLMALLOC */
#define omx_malloc malloc
#define omx_calloc calloc
#define omx_free   free
#define omx_malloc_ep(ep,size) malloc(size)
#define omx_calloc_ep(ep,nb_elt,size_elt) calloc(nb_elt,size_elt)
#define omx_free_ep(ep,ptr) free(ptr)
#endif /* !OMX_LIB_DLMALLOC */

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "open-mx.h"
#include "omx_types.h"
#include "omx_io.h"
#include "omx_valgrind.h"
#include "omx_debug.h"
#include "omx_list.h"

/*************
 * Optimizing
 */

#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && (__GNUC__ > 2 || __GNUC__ == 2 && __GNUC_MINOR__ >= 96)
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#else
#define likely(x)	(x)
#define unlikely(x)	(x)
#endif

/******************
 * Various globals
 */

extern struct omx__globals omx__globals;
extern volatile struct omx_driver_desc * omx__driver_desc;

/******************
 * Timing routines
 */

#define ACK_PER_SECOND 64 /* simplifies divisions too */
#define omx__ack_delay_jiffies() ((omx__driver_desc->hz + ACK_PER_SECOND) / ACK_PER_SECOND)

#define RESEND_PER_SECOND 2
#define omx__resend_delay_jiffies() ((omx__driver_desc->hz + RESEND_PER_SECOND) / RESEND_PER_SECOND)

/* assume 1s = 1024ms, to simplify divisions */

#define omx__timeout_ms_to_resends(ms) ((ms * RESEND_PER_SECOND + 1023) / 1024)

static inline __pure uint64_t
omx__timeout_ms_to_relative_jiffies(uint32_t ms)
{
	uint32_t hz = omx__driver_desc->hz;
	return (ms == OMX_TIMEOUT_INFINITE)
		? OMX_CMD_WAIT_EVENT_TIMEOUT_INFINITE
		: (ms * hz + 1023)/1024;
}

static inline __pure uint64_t
omx__timeout_ms_to_absolute_jiffies(uint32_t ms)
{
	uint32_t hz = omx__driver_desc->hz;
	uint64_t now = omx__driver_desc->jiffies;
	return (ms == OMX_TIMEOUT_INFINITE)
		? OMX_CMD_WAIT_EVENT_TIMEOUT_INFINITE
		: now + (ms * hz + 1023)/1024;
}

/**************************
 * Partner-related helpers
 */

static inline __pure struct omx__partner *
omx__partner_from_addr(const omx_endpoint_addr_t * addr)
{
  BUILD_BUG_ON(sizeof(struct omx__endpoint_addr) != sizeof(struct omx_endpoint_addr));
  return ((struct omx__endpoint_addr *) addr)->partner;
}

static inline void
omx__partner_session_to_addr(const struct omx__partner * partner, uint32_t session_id,
			     omx_endpoint_addr_t * addr)
{
  ((struct omx__endpoint_addr *) addr)->partner = (struct omx__partner *) partner;
  ((struct omx__endpoint_addr *) addr)->session_id = session_id;
  OMX_VALGRIND_MEMORY_MAKE_READABLE(addr, sizeof(*addr));
}

static inline void
omx__partner_recv_to_addr(const struct omx__partner * partner, omx_endpoint_addr_t * addr)
{
  omx__partner_session_to_addr(partner, partner->back_session_id, addr);
}

static inline __pure int
omx__partner_localization_shared(const struct omx__partner *partner)
{
  omx__debug_assert(partner->localization != OMX__PARTNER_LOCALIZATION_UNKNOWN);
  return (partner->localization == OMX__PARTNER_LOCALIZATION_LOCAL);
}

static inline void
omx__partner_recv_lookup(const struct omx_endpoint *ep,
			 uint16_t peer_index, uint8_t endpoint_index,
			 struct omx__partner ** partnerp)
{
  uint32_t partner_index = ((uint32_t) endpoint_index)
    + ((uint32_t) peer_index) * omx__driver_desc->endpoint_max;
  *partnerp = ep->partners[partner_index];
}

static inline void
omx__mark_partner_need_ack_delayed(struct omx_endpoint *ep,
				   struct omx__partner *partner)
{
  /* nothing to do if already NEED_ACK_DELAYED or NEED_ACK_IMMEDIATE */

  if (partner->need_ack == OMX__PARTNER_NEED_NO_ACK) {
    partner->need_ack = OMX__PARTNER_NEED_ACK_DELAYED;
    partner->oldest_recv_time_not_acked = omx__driver_desc->jiffies;
    list_add_tail(&partner->endpoint_partners_to_ack_elt, &ep->partners_to_ack_delayed_list);
  }
}

static inline void
omx__mark_partner_need_ack_immediate(struct omx_endpoint *ep,
				     struct omx__partner *partner)
{
  /* nothing to do if already NEED_ACK_IMMEDIATE */

  if (partner->need_ack != OMX__PARTNER_NEED_ACK_IMMEDIATE) {
    /* queue a new immediate ack, after removing the delayed one if needed */
    if (partner->need_ack == OMX__PARTNER_NEED_ACK_DELAYED) {
      list_move(&partner->endpoint_partners_to_ack_elt, &ep->partners_to_ack_immediate_list);
    } else {
      list_add_tail(&partner->endpoint_partners_to_ack_elt, &ep->partners_to_ack_immediate_list);
    }

    partner->need_ack = OMX__PARTNER_NEED_ACK_IMMEDIATE;
  }
}

static inline omx__seqnum_t
omx__get_partner_needed_ack(const struct omx_endpoint *ep,
			    const struct omx__partner *partner)
{
  return partner->next_frag_recv_seq;
}

static inline void
omx__mark_partner_ack_sent(struct omx_endpoint *ep,
			   struct omx__partner *partner)
{
  /* drop the previous DELAYED or IMMEDIATE ack */
  if (partner->need_ack != OMX__PARTNER_NEED_NO_ACK) {
    partner->need_ack = OMX__PARTNER_NEED_NO_ACK;
    list_del(&partner->endpoint_partners_to_ack_elt);
  }

  /* update the last acked seqnum */
  partner->last_acked_recv_seq = partner->next_frag_recv_seq;
}

static inline void
omx__mark_partner_throttling(struct omx_endpoint *ep,
			     struct omx__partner *partner)
{
  if (!partner->throttling_sends_nr++)
    list_add_tail(&partner->endpoint_throttling_partners_elt, &ep->throttling_partners_list);
}

static inline void
omx__update_partner_throttling(struct omx_endpoint *ep,
			       struct omx__partner *partner,
			       int nr)
{
  if (nr && !(partner->throttling_sends_nr -= nr))
    list_del(&partner->endpoint_throttling_partners_elt);
}

static inline int
omx__board_addr_sprintf(char * buffer, uint64_t addr)
{
	return sprintf(buffer, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		       (uint8_t)(addr >> 40),
		       (uint8_t)(addr >> 32),
		       (uint8_t)(addr >> 24),
		       (uint8_t)(addr >> 16),
		       (uint8_t)(addr >> 8),
		       (uint8_t)(addr >> 0));
}

static inline int
omx__board_addr_sscanf(const char * buffer, uint64_t * addr)
{
	uint8_t bytes[6];
	int err;

        err = sscanf(buffer, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		     &bytes[0], &bytes[1], &bytes[2],
		     &bytes[3], &bytes[4], &bytes[5]);

	if (err == 6)
		*addr = (((uint64_t) bytes[0]) << 40)
		      + (((uint64_t) bytes[1]) << 32)
		      + (((uint64_t) bytes[2]) << 24)
		      + (((uint64_t) bytes[3]) << 16)
		      + (((uint64_t) bytes[4]) << 8)
		      + (((uint64_t) bytes[5]) << 0);

	return err;
}

/*************************************
 * Enpoint sendq map helpers
 */

static inline int
omx__endpoint_sendq_map_get(struct omx_endpoint * ep,
			    int nr, const void * user, omx_sendq_map_index_t * founds)
{
  struct omx__sendq_entry * array = ep->sendq_map.array;
  int index, i;

  /* the sendq_map API manipulates omx_sendq_map_index_t to reduce memory consumption in requests
   * (even if it may not be uint16_t internally),
   * make sure it's enough for the actual offset
   */
  BUILD_BUG_ON(1ULL << (8*sizeof(omx_sendq_map_index_t)) < OMX_SENDQ_ENTRY_NR);

  omx__debug_assert((ep->sendq_map.first_free == -1) == (ep->sendq_map.nr_free == 0));

  if (unlikely(ep->sendq_map.nr_free < nr))
    return -1;

  index = ep->sendq_map.first_free;
  for(i=0; i<nr; i++) {
    int next_free;

    omx__debug_assert(index >= 0);

    next_free = array[index].next_free;

    omx__debug_assert(array[index].user == NULL);
    omx__debug_instr(array[index].next_free = -1);

    array[index].user = (void *) user;
    founds[i] = index;
    index = next_free;
  }
  ep->sendq_map.first_free = index;
  ep->sendq_map.nr_free -= nr;

  return 0;
}

static inline void
omx__endpoint_sendq_map_put(struct omx_endpoint * ep,
			    int nr, const omx_sendq_map_index_t * indexes)
{
  struct omx__sendq_entry * array = ep->sendq_map.array;
  void * user;
  int i;

  user = array[indexes[0]].user;
#ifdef OMX_LIB_DEBUG
  for(i=1; i<nr; i++)
    if (user != array[indexes[i]].user)
      omx__abort(ep, "Tried to put some unrelated sendq map entries\n");
#endif

  omx__debug_assert(user != NULL);

  for(i=0; i<nr; i++) {
    omx__debug_assert(array[indexes[i]].next_free == -1);

    array[indexes[i]].user = NULL;
    array[indexes[i]].next_free = i ? indexes[i-1] : ep->sendq_map.first_free;
  }

  ep->sendq_map.first_free = indexes[nr-1];
  ep->sendq_map.nr_free += nr;
}

static inline void *
omx__endpoint_sendq_map_user(const struct omx_endpoint * ep,
			     omx_sendq_map_index_t index)
{
  struct omx__sendq_entry * array = ep->sendq_map.array;
  void * user = array[index].user;

  omx__debug_assert(user != NULL);
  omx__debug_assert(array[index].next_free == -1);

  return user;
}

/******************************
 * Various internal prototypes
 */

/* sending messages */

extern void
omx__submit_notify(struct omx_endpoint *ep,
		   union omx_request *req,
		   int delayed);

extern omx_return_t
omx__submit_discarded_notify(struct omx_endpoint *ep, const struct omx__partner * partner,
			     const struct omx_evt_recv_msg *msg);

extern omx_return_t
omx__alloc_setup_pull(struct omx_endpoint * ep,
		      union omx_request * req);

extern void
omx__submit_pull(struct omx_endpoint * ep,
		 union omx_request * req);

extern void
omx__send_complete(struct omx_endpoint *ep, union omx_request *req,
		   omx_return_t status);

/* receiving messages */

extern void
omx__process_self_send(struct omx_endpoint *ep,
		       union omx_request *sreq);

extern void
omx__recv_complete(struct omx_endpoint *ep, union omx_request *req,
		   omx_return_t status);

extern void
omx__process_recv(struct omx_endpoint *ep,
		  const struct omx_evt_recv_msg *msg, const void *data, uint32_t msg_length,
		  omx__process_recv_func_t recv_func);

extern void
omx__process_recv_tiny(struct omx_endpoint *ep, struct omx__partner *partner,
		       union omx_request *req,
		       const struct omx_evt_recv_msg *msg,
		       const void *data /* unused */, uint32_t xfer_length);

extern void
omx__process_recv_small(struct omx_endpoint *ep, struct omx__partner *partner,
			union omx_request *req,
			const struct omx_evt_recv_msg *msg,
			const void *data, uint32_t xfer_length);

extern void
omx__process_recv_medium_frag(struct omx_endpoint *ep, struct omx__partner *partner,
			      union omx_request *req,
			      const struct omx_evt_recv_msg *msg,
			      const void *data, uint32_t xfer_length);

extern void
omx__process_recv_rndv(struct omx_endpoint *ep, struct omx__partner *partner,
		       union omx_request *req,
		       const struct omx_evt_recv_msg *msg,
		       const void *data /* unused */, uint32_t xfer_length);

extern void
omx__process_recv_notify(struct omx_endpoint *ep, struct omx__partner *partner,
			 union omx_request *req,
			 const struct omx_evt_recv_msg *msg,
			 const void *data /* unused */, uint32_t xfer_length);

extern void
omx__process_pull_done(struct omx_endpoint * ep,
		       const struct omx_evt_pull_done * event);

extern void
omx__process_recv_liback(struct omx_endpoint *ep,
			 const struct omx_evt_recv_liback *liback);

extern void
omx__process_recv_nack_lib(struct omx_endpoint *ep,
			   const struct omx_evt_recv_nack_lib *nack_lib);

/* progression */

extern omx_return_t
omx__progress(struct omx_endpoint * ep);

extern void
omx__notify_user_event(struct omx_endpoint *ep);

extern void
omx__forget(struct omx_endpoint *ep, union omx_request *req);

/* connect management */

extern omx_return_t
omx__connect_myself(struct omx_endpoint *ep);

extern void
omx__post_connect_request(const struct omx_endpoint *ep,
			  const struct omx__partner *partner,
			  union omx_request * req);

extern void
omx__process_recv_connect_request(struct omx_endpoint *ep,
				  const struct omx_evt_recv_connect_request *event);

extern void
omx__process_recv_connect_reply(struct omx_endpoint *ep,
				const struct omx_evt_recv_connect_reply *event);

extern void
omx__connect_complete(struct omx_endpoint *ep, union omx_request *req,
		      omx_return_t status, uint32_t session_id);

omx_return_t
omx__connect_wait(omx_endpoint_t ep, union omx_request * req,
		  uint32_t ms_timeout);

/* retransmission */

extern void
omx__handle_ack(struct omx_endpoint *ep,
		struct omx__partner *partner, omx__seqnum_t ack);

extern void
omx__handle_liback(struct omx_endpoint *ep,
		   struct omx__partner *partner,
		   const struct omx_evt_recv_liback *liback);

extern void
omx__handle_nack(struct omx_endpoint *ep,
                 struct omx__partner *partner, omx__seqnum_t seqnum,
                 omx_return_t status);

extern void
omx__mark_request_acked(struct omx_endpoint *ep,
			union omx_request *req,
			omx_return_t status);

extern void
omx__process_resend_requests(struct omx_endpoint *ep);

extern void
omx__process_delayed_requests(struct omx_endpoint *ep);

extern void
omx__process_throttling_requests(struct omx_endpoint *ep,
				 struct omx__partner *partner,
				 int nr);

extern void
omx__complete_unsent_send_request(struct omx_endpoint *ep,
				  union omx_request *req);

extern void
omx__process_partners_to_ack(struct omx_endpoint *ep);

extern void
omx__flush_partners_to_ack(struct omx_endpoint *ep);

extern void
omx__prepare_progress_wakeup(struct omx_endpoint *ep);

extern void
omx__partner_cleanup(struct omx_endpoint *ep,
		     struct omx__partner *partner, int disconnect);

/* large region management */

extern omx_return_t
omx__endpoint_large_region_map_init(struct omx_endpoint * ep);

extern void
omx__endpoint_large_region_map_exit(struct omx_endpoint * ep);

extern omx_return_t
omx__get_region(struct omx_endpoint *ep,
		const struct omx__req_segs *segs,
		struct omx__large_region **regionp,
		const void * reserver);

extern omx_return_t
omx__put_region(struct omx_endpoint *ep,
		struct omx__large_region *region,
		const void * reserver);

extern void
omx__regcache_clean(void *ptr, size_t size);

/* board management */

extern omx_return_t
omx__get_board_count(uint32_t * count);

extern omx_return_t
omx__get_board_info(const struct omx_endpoint * ep, uint32_t index, struct omx_board_info * info);

extern omx_return_t
omx__get_board_index_by_name(const char * name, uint32_t * index);

/* hostname management */

omx_return_t
omx__driver_set_hostname(uint32_t board_index, const char *hostname);

omx_return_t
omx__driver_clear_peer_names(void);

omx_return_t
omx__driver_get_peer_table_state(uint32_t *status, uint32_t *version,
				 uint32_t *size, uint64_t *mapper_id);

omx_return_t
omx__driver_set_peer_table_state(uint32_t configured, uint32_t version,
				 uint32_t size, uint64_t mapper_id);

/* peer management */

extern omx_return_t
omx__driver_peer_add(uint64_t board_addr, const char *hostname);

extern omx_return_t
omx__driver_peers_clear(void);

extern omx_return_t
omx__peers_dump(const char * format);

extern omx_return_t
omx__peer_addr_to_index(uint64_t board_addr, uint16_t *index);

extern omx_return_t
omx__peer_index_to_addr(uint16_t index, uint64_t *board_addrp);

/* error management */

extern void
omx__init_error_handler(void);

extern omx_return_t
omx__error(omx_return_t ret, const char *fmt, ...);

extern omx_return_t
omx__error_with_ep(const struct omx_endpoint *ep,
		   omx_return_t ret, const char *fmt, ...);

extern omx_return_t
omx__error_with_req(const struct omx_endpoint *ep, const union omx_request *req,
		    omx_return_t code, const char *fmt, ...);

/* misc helpers */

extern void
omx__init_comms(void);

extern __pure omx_return_t
omx__errno_to_return(void);

extern omx_return_t
omx__ioctl_errno_to_return_checked(omx_return_t ok, ...);

extern void
omx__check_driver_pinning_error(const struct omx_endpoint *ep, omx_return_t ret);

extern __pure const char *
omx__strreqtype(enum omx__request_type type);

extern void
omx__sprintf_reqstate(uint16_t state, char *str);

extern __malloc char *
omx__create_message_prefix(const struct omx_endpoint *ep);

extern void
omx__foreach_endpoint(void (*func)(struct omx_endpoint *, void *), void *);

#define OMX_PROCESS_BINDING_FILE "/tmp/open-mx.bindings.dat"
#define OMX_PROCESS_BINDING_LENGTH_MAX 128

#endif /* __omx_lib_h__ */
