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

#ifndef __omx_request_h__
#define __omx_request_h__

#include <stdlib.h>

#include "omx_lib.h"

/*********************
 * Request allocation
 */

static inline void
omx__request_alloc_init(struct omx_endpoint *ep)
{
#ifdef OMX_LIB_DEBUG
  ep->req_alloc_nr = 0;
#endif
}

static inline void
omx__request_alloc_exit(struct omx_endpoint *ep)
{
#ifdef OMX_LIB_DEBUG
  if (ep->req_alloc_nr)
    omx__verbose_printf(ep, "%d requests were not freed on endpoint close\n", ep->req_alloc_nr);
#endif
}

static inline __malloc union omx_request *
omx__request_alloc(struct omx_endpoint *ep)
{
  union omx_request * req;

#ifdef OMX_LIB_DEBUG
  req = omx_calloc_ep(ep, 1, sizeof(*req));
#else
  req = omx_malloc_ep(ep, sizeof(*req));
#endif
  if (unlikely(!req))
    return NULL;

  req->generic.state = 0;
  req->generic.status.code = OMX_SUCCESS;

#ifdef OMX_LIB_DEBUG
  ep->req_alloc_nr++;
#endif
  return req;
}

static inline void
omx__request_free(struct omx_endpoint *ep, union omx_request * req)
{
  omx_free_ep(ep, req);
#ifdef OMX_LIB_DEBUG
  ep->req_alloc_nr--;
#endif
}

extern void
omx__request_alloc_check(const struct omx_endpoint *ep);

/***************************
 * Request queue management
 */

static inline void
omx__enqueue_request(struct omx__req_q *head,
		     union omx_request *req)
{
  TAILQ_INSERT_TAIL(head, &req->generic, queue_elt);
}

static inline void
omx__requeue_request(struct omx__req_q *head,
		     union omx_request *req)
{
  TAILQ_INSERT_HEAD(head, &req->generic, queue_elt);
}

static inline void
omx__dequeue_request(struct omx__req_q *head,
		     union omx_request *req)
{
#ifdef OMX_LIB_DEBUG
  struct omx__generic_request *e;
  TAILQ_FOREACH(e, head, queue_elt)
    if (&req->generic == e)
      goto found;

  omx__abort(NULL, "Failed to find request in queue for dequeueing\n");

 found:
#endif /* OMX_LIB_DEBUG */
  TAILQ_REMOVE(head, &req->generic, queue_elt);
}

static inline union omx_request *
omx__first_request(const struct omx__req_q *head)
{
  return (union omx_request *) TAILQ_FIRST(head);
}

static inline int
omx__empty_queue(const struct omx__req_q *head)
{
  return TAILQ_EMPTY(head);
}

static inline unsigned
omx__queue_count(const struct omx__req_q *head)
{
  struct omx__generic_request *e;
  unsigned i=0;
  TAILQ_FOREACH(e, head, queue_elt)
    i++;
  return i;
}

#define omx__foreach_request(head, req) {    \
  struct omx__generic_request *_elt;         \
  TAILQ_FOREACH(_elt, head, queue_elt) {     \
    req = (union omx_request *) _elt;        \
    {
#define omx__foreach_request_end()           \
    }                                        \
  }                                          \
}

#define omx__foreach_request_safe(head, req) { \
  struct omx__generic_request *_elt, *_next;   \
  _elt = TAILQ_FIRST(head);                    \
  while (_elt) {                               \
  _next = TAILQ_NEXT(_elt, queue_elt);         \
    req =  (union omx_request *) _elt;         \
    {
#define omx__foreach_request_safe_end()        \
    }                                          \
    _elt = _next;                              \
  }                                            \
}

/*********************************
 * Request ctxid queue management
 */

static inline void
omx__enqueue_ctxid_request(struct omx__ctxid_req_q *head,
			   union omx_request *req)
{
  TAILQ_INSERT_TAIL(head, &req->generic, ctxid_elt);
}

static inline void
omx__dequeue_ctxid_request(struct omx__ctxid_req_q *head,
			   union omx_request *req)
{
#ifdef OMX_LIB_DEBUG
  struct omx__generic_request *e;
  TAILQ_FOREACH(e, head, ctxid_elt)
    if (&req->generic == e)
      goto found;

  omx__abort(NULL, "Failed to find request in ctxid queue for dequeueing\n");

 found:
#endif /* OMX_LIB_DEBUG */
  TAILQ_REMOVE(head, &req->generic, ctxid_elt);
}

#define omx__foreach_ctxid_request(head, req) {    \
  struct omx__generic_request *_elt;               \
  TAILQ_FOREACH(_elt, head, ctxid_elt) {           \
    req = (union omx_request *) _elt;              \
    {
#define omx__foreach_ctxid_request_end()           \
    }                                              \
  }                                                \
}

/********************************
 * Done request queue management
 */

/* mark the request as done while it is not done yet */
static inline void
omx__notify_request_done_early(struct omx_endpoint *ep, uint32_t ctxid,
			       union omx_request *req)
{
  if (unlikely(ep->zombies >= ep->zombie_max))
    return;

  omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_INTERNAL));
  omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_DONE));
  omx__debug_assert(req->generic.state);

  req->generic.state |= OMX_REQUEST_STATE_DONE;

  if (likely(!(req->generic.state & OMX_REQUEST_STATE_ZOMBIE))) {
    TAILQ_INSERT_TAIL(&ep->anyctxid.done_req_q, &req->generic, done_elt);
    if (unlikely(HAS_CTXIDS(ep)))
      TAILQ_INSERT_TAIL(&ep->ctxid[ctxid].done_req_q, &req->generic, ctxid_elt);
  }

  /*
   * need to wakeup some possible send-done waiters (or recv-done for notify)
   * since this event does not come from the driver
   */
  omx__notify_user_event(ep);
}

static inline void
omx__notify_request_done(struct omx_endpoint *ep, uint32_t ctxid,
			 union omx_request *req)
{
  if (unlikely(req->generic.state & OMX_REQUEST_STATE_INTERNAL)) {
    /* no need to queue the request, just set the DONE status */
    omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_DONE));
    req->generic.state |= OMX_REQUEST_STATE_DONE;
    omx__debug_assert(!(req->generic.state & OMX_REQUEST_STATE_ZOMBIE));
#ifdef OMX_LIB_DEBUG
    omx__enqueue_request(&ep->internal_done_req_q, req);
#endif

  } else if (likely(req->generic.state & OMX_REQUEST_STATE_ZOMBIE)) {
    /* request already completed by the application, just free it */
    omx__request_free(ep, req);
    ep->zombies--;

  } else if (unlikely(!(req->generic.state & OMX_REQUEST_STATE_DONE))) {
    /* queue the request to the done queue */
    omx__debug_assert(!req->generic.state);
    req->generic.state |= OMX_REQUEST_STATE_DONE;
    TAILQ_INSERT_TAIL(&ep->anyctxid.done_req_q, &req->generic, done_elt);
    if (unlikely(HAS_CTXIDS(ep)))
      TAILQ_INSERT_TAIL(&ep->ctxid[ctxid].done_req_q, &req->generic, ctxid_elt);
#ifdef OMX_LIB_DEBUG
    omx__enqueue_request(&ep->really_done_req_q, req);
#endif
  } else {
    /* request was marked as done early, its done_*_elt are already queued */
    omx__debug_assert(req->generic.state == OMX_REQUEST_STATE_DONE);
#ifdef OMX_LIB_DEBUG
    omx__enqueue_request(&ep->really_done_req_q, req);
#endif
  }
}

static inline void
omx__dequeue_done_request(struct omx_endpoint *ep,
			  union omx_request *req)
{
  uint32_t ctxid = CTXID_FROM_MATCHING(ep, req->generic.status.match_info);
#ifdef OMX_LIB_DEBUG
  struct omx__generic_request *e;

  TAILQ_FOREACH(e, &ep->anyctxid.done_req_q, done_elt)
    if (req == (union omx_request *) e)
      goto found2;
  omx__abort(ep, "Failed to find request in anyctxid done queue for dequeueing\n");
 found2:

  if (unlikely(HAS_CTXIDS(ep))) {
    TAILQ_FOREACH(e, &ep->ctxid[ctxid].done_req_q, ctxid_elt)
      if (req == (union omx_request *) e)
	goto found;
    omx__abort(ep, "Failed to find request in ctxid done queue for dequeueing\n");
  }
 found:

  if (req->generic.state == OMX_REQUEST_STATE_DONE)
    omx__dequeue_request(&ep->really_done_req_q, req);
#endif /* OMX_LIB_DEBUG */
  TAILQ_REMOVE(&ep->anyctxid.done_req_q, &req->generic, done_elt);
  if (unlikely(HAS_CTXIDS(ep)))
    TAILQ_REMOVE(&ep->ctxid[ctxid].done_req_q, &req->generic, ctxid_elt);
}

//#define omx__foreach_done_ctxid_request(ep, _ctxid, req) {	  \
//  struct omx__generic_request *_elt;				  \
//  TAILQ_FOREACH(_elt, &ep->ctxid[_ctxid].done_req_q, ctxid_elt) {	\
//    req = (union omx_request *) _elt;					\
//    {
//#define omx__foreach_done_ctxid_request_end()			  \
//    }								  \
//  }								  \
//}

#define omx__foreach_done_anyctxid_request(head, _req) { \
  struct omx__generic_request *_elt;                     \
  TAILQ_FOREACH(_elt, head, done_elt) {                  \
    req = (union omx_request *) _elt;                    \
    {
#define omx__foreach_done_anyctxid_request_end()         \
    }                                                    \
  }                                                      \
}

//#define omx__foreach_done_anyctxid_request_safe(ep, req, next)	\
//list_for_each_entry_safe(req, next, &ep->anyctxid.done_req_q, generic.done_elt)

static inline union omx_request *
omx__first_done_anyctxid_request(const struct omx_endpoint *ep)
{
  return (union omx_request *) TAILQ_FIRST(&ep->anyctxid.done_req_q);
}

static inline int
omx__empty_done_ctxid_queue(const struct omx_endpoint *ep, uint32_t ctxid)
{
  return TAILQ_EMPTY(&ep->ctxid[ctxid].done_req_q);
}

static inline int
omx__empty_done_anyctxid_queue(const struct omx_endpoint *ep)
{
  return TAILQ_EMPTY(&ep->anyctxid.done_req_q);
}

/****************************
 * Partner queues management
 */

static inline void
omx__enqueue_partner_request(struct omx__partner_req_q *head,
			     union omx_request *req)
{
  TAILQ_INSERT_TAIL(head, &req->generic, partner_elt);
}

static inline void
omx__dequeue_partner_request(struct omx__partner_req_q *head,
			     union omx_request *req)
{
#ifdef OMX_LIB_DEBUG
  struct omx__generic_request *e;
  TAILQ_FOREACH(e, head, partner_elt)
    if (&req->generic == e)
      goto found;

  omx__abort(NULL, "Failed to find request in partner queue for dequeueing\n");

 found:
#endif /* OMX_LIB_DEBUG */
  TAILQ_REMOVE(head, &req->generic, partner_elt);
}

static inline int
omx__empty_partner_queue(const struct omx__partner_req_q *head)
{
  return TAILQ_EMPTY(head);
}

static inline union omx_request *
omx__first_partner_request(const struct omx__partner_req_q *head)
{
  return (union omx_request *) TAILQ_FIRST(head);
}

static inline union omx_request *
omx__dequeue_first_partner_request(struct omx__partner_req_q *head)
 {
  struct omx__generic_request *elt;

  if (TAILQ_EMPTY(head))
    return NULL;

  elt = TAILQ_FIRST(head);
  TAILQ_REMOVE(head, elt, partner_elt);
  return (union omx_request *) elt;
}

#define omx__foreach_partner_request(head, req) {    \
  struct omx__generic_request *_elt;                 \
  TAILQ_FOREACH(_elt, head, partner_elt) {           \
    req = (union omx_request *) _elt;                \
    {
#define omx__foreach_partner_request_end()           \
    }                                                \
  }                                                  \
}

#define omx__foreach_partner_request_safe(head, req) { \
  struct omx__generic_request *_elt, *_next;           \
  _elt = TAILQ_FIRST(head);                            \
  while (_elt) {                                       \
  _next = TAILQ_NEXT(_elt, partner_elt);               \
    req =  (union omx_request *) _elt;                 \
    {
#define omx__foreach_partner_request_safe_end()        \
    }                                                  \
    _elt = _next;                                      \
  }                                                    \
}

#endif /* __omx_request_h__ */
