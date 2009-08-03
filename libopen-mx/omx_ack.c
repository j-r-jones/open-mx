/*
 * Open-MX
 * Copyright © INRIA 2007-2009 (see AUTHORS file)
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

#include <sys/ioctl.h>

#include "omx_lib.h"
#include "omx_request.h"

/*************************************
 * Apply a ack or a nack to a request
 */

void
omx__mark_request_acked(struct omx_endpoint *ep,
			union omx_request *req,
			omx_return_t status)
{
  omx__debug_assert(req->generic.state & OMX_REQUEST_STATE_NEED_ACK);
  req->generic.state &= ~OMX_REQUEST_STATE_NEED_ACK;

  switch (req->generic.type) {

  case OMX_REQUEST_TYPE_SEND_TINY:
  case OMX_REQUEST_TYPE_SEND_SMALL:
  case OMX_REQUEST_TYPE_SEND_MEDIUMVA:
    omx__dequeue_request(&ep->non_acked_req_q, req);
    omx__send_complete(ep, req, status);
    break;

  case OMX_REQUEST_TYPE_SEND_MEDIUMSQ:
    if (unlikely(req->generic.state & OMX_REQUEST_STATE_DRIVER_MEDIUMSQ_SENDING)) {
      /* keep the request in the driver_posted_req_q for now until it returns from the driver */
      if (req->generic.status.code == OMX_SUCCESS)
	/* set the status (success for ack, error for nack) only if there has been no error early */
	req->generic.status.code = omx__error_with_req(ep, req, status, "Send request nacked");
    } else {
      omx__dequeue_request(&ep->non_acked_req_q, req);
      omx__send_complete(ep, req, status);
    }
    break;

  case OMX_REQUEST_TYPE_SEND_LARGE:
    /* if the request was already replied, it would have been acked at the same time */
    omx__dequeue_request(&ep->non_acked_req_q, req);
    if (unlikely(status != OMX_SUCCESS)) {
      /* the request has been nacked, there won't be any reply */
      req->generic.state &= ~OMX_REQUEST_STATE_NEED_REPLY;
      omx__put_region(ep, req->send.specific.large.region, req);
      ep->large_sends_avail_nr++;
      omx__send_complete(ep, req, status);
    } else {
      if (req->generic.state & OMX_REQUEST_STATE_NEED_REPLY)
	omx__enqueue_request(&ep->large_send_need_reply_req_q, req);
      else
	omx__send_complete(ep, req, status);
    }
    break;

  case OMX_REQUEST_TYPE_RECV_LARGE:
    omx__dequeue_request(&ep->non_acked_req_q, req);
    omx__recv_complete(ep, req, status);
    break;

  default:
    omx__abort(ep, "Failed to ack unexpected request type %d\n",
	       req->generic.type);
  }
}

/***********************
 * Handle Received Acks
 */

void
omx__handle_ack(struct omx_endpoint *ep,
		struct omx__partner *partner, omx__seqnum_t ack_before)
{
  /* take care of the seqnum wrap around by casting differences into omx__seqnum_t */
  omx__seqnum_t missing_acks = OMX__SEQNUM(partner->next_send_seq - partner->next_acked_send_seq);
  omx__seqnum_t new_acks = OMX__SEQNUM(ack_before - partner->next_acked_send_seq);

  if (!new_acks || new_acks > missing_acks) {
    omx__debug_printf(ACK, ep, "got obsolete ack up to %d (#%d), %d new for %d missing\n",
		      (unsigned) OMX__SEQNUM(ack_before - 1),
		      (unsigned) OMX__SESNUM_SHIFTED(ack_before - 1),
		      (unsigned) new_acks, (unsigned) missing_acks);

  } else {
    union omx_request *req, *next;

    omx__debug_printf(ACK, ep, "marking seqnums up to %d (#%d) as acked (jiffies %lld)\n",
		      (unsigned) OMX__SEQNUM(ack_before - 1),
		      (unsigned) OMX__SESNUM_SHIFTED(ack_before - 1),
		      (unsigned long long) omx__driver_desc->jiffies);

    omx__foreach_partner_request_safe(&partner->non_acked_req_q, req, next) {
      /* take care of the seqnum wrap around here too */
      omx__seqnum_t req_index = OMX__SEQNUM(req->generic.send_seqnum - partner->next_acked_send_seq);

      /* ack req_index from 0 to new_acks-1 */
      if (req_index >= new_acks) {
	omx__debug_printf(ACK, ep, "stopping marking reqs as acked at seqnum %x (#%d)\n",
			  (unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			  (unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
	break;
      }

      omx__debug_printf(ACK, ep, "marking req with seqnum %x (#%d) as acked\n",
			(unsigned) OMX__SEQNUM(req->generic.send_seqnum),
			(unsigned) OMX__SESNUM_SHIFTED(req->generic.send_seqnum));
      omx___dequeue_partner_request(req);
      omx__mark_request_acked(ep, req, OMX_SUCCESS);
    }

    partner->next_acked_send_seq = ack_before;

    /* there are some new seqnum available, dequeue throttling sends */
    omx__process_throttling_requests(ep, partner, new_acks);
  }
}

void
omx__handle_liback(struct omx_endpoint *ep,
		   struct omx__partner *partner,
		   const struct omx_evt_recv_liback *liback)
{
  omx__seqnum_t ack = liback->lib_seqnum;
  uint32_t acknum = liback->acknum;

  if (unlikely(OMX__SESNUM(ack ^ partner->next_send_seq)) != 0) {
    omx__verbose_printf(ep, "Obsolete session truc ack received (session %d seqnum %d instead of session %d)\n",
                        (unsigned) OMX__SESNUM_SHIFTED(ack), (unsigned) OMX__SEQNUM(ack),
                        (unsigned) OMX__SESNUM_SHIFTED(partner->next_send_seq));
    return;
  }

  if (acknum <= partner->last_recv_acknum) {
    omx__debug_printf(ACK, ep, "got truc ack from partner %016llx ep %d with obsolete acknum %d, expected more than %d\n",
		      (unsigned long long) partner->board_addr, (unsigned) partner->endpoint_index,
		      (unsigned) acknum, (unsigned) partner->last_recv_acknum);
    return;
  }
  partner->last_recv_acknum = acknum;

  omx__debug_printf(ACK, ep, "got a truc ack from partner %016llx ep %d for ack up to %d (#%d)\n",
		    (unsigned long long) partner->board_addr, (unsigned) partner->endpoint_index,
		    (unsigned) OMX__SEQNUM(ack - 1),
		    (unsigned) OMX__SESNUM_SHIFTED(ack - 1));
  omx__handle_ack(ep, partner, ack);
}

/************************
 * Handle Received Nacks
 */

void
omx__handle_nack(struct omx_endpoint *ep,
		 struct omx__partner *partner, omx__seqnum_t seqnum,
		 omx_return_t status)
{
  omx__seqnum_t nack_index = OMX__SEQNUM(seqnum - partner->next_acked_send_seq);
  union omx_request *req;

  omx__debug_printf(ACK, ep, "got nack from partner %016llx ep %d for seqnum %d\n",
		    (unsigned long long) partner->board_addr, (unsigned) partner->endpoint_index,
		    (unsigned) seqnum);

  if (unlikely(OMX__SESNUM(seqnum ^ partner->next_send_seq)) != 0)
    /* This cannot be a real send since the sesnum is wrong, but it can be a connect */
    goto try_connect_req;

  /* look in the list of pending real messages */
  omx__foreach_partner_request(&partner->non_acked_req_q, req) {
    omx__seqnum_t req_index = OMX__SEQNUM(req->generic.send_seqnum - partner->next_acked_send_seq);

    if (nack_index < req_index)
      break;

    if (nack_index == req_index) {
      omx___dequeue_partner_request(req);
      omx__mark_request_acked(ep, req, status);
      return;
    }
  }

 try_connect_req:

  /* look in the list of pending connect requests */
  omx__foreach_partner_request(&partner->connect_req_q, req) {
    /* FIXME: if > then break,
     * but take care of the wrap around using partner->connect_seqnum
     * but this protocol is crap so far since we can't distinguish between nacks for send and connect
     */
    if (req->connect.connect_seqnum == seqnum) {
      omx__connect_complete(ep, req, status, (uint32_t) -1);
      return;
    }
  }

  omx__debug_printf(ACK, ep, "Failed to find request to nack for seqnum %d, could be a duplicate, ignoring\n",
		    seqnum);
}

/**********************
 * Handle Acks to Send
 */

static omx_return_t
omx__submit_send_liback(const struct omx_endpoint *ep,
			struct omx__partner * partner)
{
  struct omx_cmd_send_liback liback_param;
  omx__seqnum_t ack_upto = omx__get_partner_needed_ack(ep, partner);
  int err;

  partner->last_send_acknum++;

  liback_param.peer_index = partner->peer_index;
  liback_param.dest_endpoint = partner->endpoint_index;
  liback_param.shared = omx__partner_localization_shared(partner);
  liback_param.session_id = partner->back_session_id;
  liback_param.acknum = partner->last_send_acknum;
  liback_param.session_id = partner->back_session_id;
  liback_param.lib_seqnum = ack_upto;
  liback_param.send_seq = ack_upto; /* FIXME? partner->send_seq */
  liback_param.resent = 0; /* FIXME? partner->requeued */

  err = ioctl(ep->fd, OMX_CMD_SEND_LIBACK, &liback_param);
  if (unlikely(err < 0)) {
    omx_return_t ret = omx__ioctl_errno_to_return_checked(OMX_NO_SYSTEM_RESOURCES,
							  OMX_SUCCESS,
							  "send truc message");
    /*
     * no need to call the handler here, we can resend later.
     * but notify the caller anyway so that partner's acking status are updated
     */
    return ret;
  }

  return OMX_SUCCESS;
}

void
omx__process_partners_to_ack(struct omx_endpoint *ep)
{
  struct omx__partner *partner, *next;
  uint64_t now = omx__driver_desc->jiffies;

  /* look at the immediate list */
  list_for_each_entry_safe(partner, next,
			   &ep->partners_to_ack_immediate_list, endpoint_partners_to_ack_elt) {
    omx_return_t ret;

    omx__debug_printf(ACK, ep, "acking immediately back to partner %016llx ep %d up to %d (#%d) at jiffies %lld\n",
		      (unsigned long long) partner->board_addr, (unsigned) partner->endpoint_index,
		      (unsigned) OMX__SEQNUM(partner->next_frag_recv_seq - 1),
		      (unsigned) OMX__SESNUM_SHIFTED(partner->next_frag_recv_seq - 1),
		      (unsigned long long) now);

    ret = omx__submit_send_liback(ep, partner);
    if (ret != OMX_SUCCESS)
      /* failed to send one liback, no need to try more */
      break;

    omx__mark_partner_ack_sent(ep, partner);
  }

  /* no need to bother looking at the delayed list if the time didn't change */
  if (now == ep->last_partners_acking_jiffies)
    return;
  ep->last_partners_acking_jiffies = now;

  /* look at the delayed list */
  list_for_each_entry_safe(partner, next,
			   &ep->partners_to_ack_delayed_list, endpoint_partners_to_ack_elt) {
    omx_return_t ret;

    if (now - partner->oldest_recv_time_not_acked < omx__globals.ack_delay_jiffies)
      /* the remaining ones are more recent, no need to ack them yet */
      break;

    omx__debug_printf(ACK, ep, "delayed acking back to partner %016llx ep %d up to %d (#%d), jiffies %lld >> %lld\n",
		      (unsigned long long) partner->board_addr, (unsigned) partner->endpoint_index,
		      (unsigned) OMX__SEQNUM(partner->next_frag_recv_seq - 1),
		      (unsigned) OMX__SESNUM_SHIFTED(partner->next_frag_recv_seq - 1),
		      (unsigned long long) now,
		      (unsigned long long) partner->oldest_recv_time_not_acked);

    ret = omx__submit_send_liback(ep, partner);
    if (ret != OMX_SUCCESS)
      /* failed to send one liback, no need to try more */
      break;

    omx__mark_partner_ack_sent(ep, partner);
  }

  /* no need to notify errors */
}

void
omx__flush_partners_to_ack(struct omx_endpoint *ep)
{
  struct omx__partner *partner, *next;
  /* immediate list should have been emptied at the end of the previous round of progression */
  omx__debug_assert(list_empty(&ep->partners_to_ack_immediate_list));

  /* look at the delayed list */
  list_for_each_entry_safe(partner, next,
			   &ep->partners_to_ack_delayed_list, endpoint_partners_to_ack_elt) {
    omx_return_t ret;

    omx__debug_printf(ACK, ep, "forcing ack back to partner %016llx ep %d up to %d (#%d), jiffies %lld instead of %lld\n",
		      (unsigned long long) partner->board_addr, (unsigned) partner->endpoint_index,
		      (unsigned) OMX__SEQNUM(partner->next_frag_recv_seq - 1),
		      (unsigned) OMX__SESNUM_SHIFTED(partner->next_frag_recv_seq - 1),
		      (unsigned long long) omx__driver_desc->jiffies,
		      (unsigned long long) partner->oldest_recv_time_not_acked);

    ret = omx__submit_send_liback(ep, partner);
    if (ret != OMX_SUCCESS)
      /* failed to send one liback, too bad for this peer */
      continue;

    omx__mark_partner_ack_sent(ep, partner);
  }

  /* no need to notify errors */
}

void
omx__prepare_progress_wakeup(struct omx_endpoint *ep)
{
  union omx_request *req;
  struct omx__partner *partner;
  uint64_t wakeup_jiffies = OMX_NO_WAKEUP_JIFFIES;

  /* any delayed ack to send soon? */
  if (!list_empty(&ep->partners_to_ack_delayed_list)) {
    uint64_t tmp;

    partner = list_first_entry(&ep->partners_to_ack_delayed_list, struct omx__partner, endpoint_partners_to_ack_elt);
    tmp = partner->oldest_recv_time_not_acked + omx__globals.ack_delay_jiffies;

    omx__debug_printf(WAIT, ep, "need to wakeup at %lld jiffies (in %ld) for delayed acks\n",
		      (unsigned long long) tmp, (unsigned long) (tmp - omx__driver_desc->jiffies));

    if (tmp < wakeup_jiffies || wakeup_jiffies == OMX_NO_WAKEUP_JIFFIES)
      wakeup_jiffies = tmp;
  }

  /* any send to resend soon? */
  if (!omx__empty_queue(&ep->non_acked_req_q)) {
    uint64_t tmp;

    req = omx__first_request(&ep->non_acked_req_q);
    tmp = req->generic.last_send_jiffies + omx__globals.resend_delay_jiffies;

    omx__debug_printf(WAIT, ep, "need to wakeup at %lld jiffies (in %ld) for resend\n",
		      (unsigned long long) tmp, (unsigned long) (tmp - omx__driver_desc->jiffies));

    if (tmp < wakeup_jiffies || wakeup_jiffies == OMX_NO_WAKEUP_JIFFIES)
      wakeup_jiffies = tmp;
  }

  /* any connect to resend soon? */
  if (!omx__empty_queue(&ep->connect_req_q)) {
    uint64_t tmp;

    req = omx__first_request(&ep->connect_req_q);
    tmp = req->generic.last_send_jiffies + omx__globals.resend_delay_jiffies;

    omx__debug_printf(WAIT, ep, "need to wakeup at %lld jiffies (in %ld) for resend\n",
		      (unsigned long long) tmp, (unsigned long) (tmp - omx__driver_desc->jiffies));

    if (tmp < wakeup_jiffies || wakeup_jiffies == OMX_NO_WAKEUP_JIFFIES)
      wakeup_jiffies = tmp;
  }

  ep->desc->wakeup_jiffies = wakeup_jiffies;
}

/**********************************
 * Set Request or Endpoint Timeout
 */

/* API omx_set_request_timeout */
omx_return_t
omx_set_request_timeout(struct omx_endpoint *ep,
			union omx_request *request, uint32_t ms)
{
  uint32_t jiffies = omx__timeout_ms_to_relative_jiffies(ms);
  uint32_t resends = omx__timeout_ms_to_resends(ms);

  /* no need to lock here, there's no possible race condition or so */

  if (request) {
    request->generic.resends_max = resends;
  } else {
    ep->pull_resend_timeout_jiffies = jiffies;
    ep->req_resends_max = resends;
  }

  return OMX_SUCCESS;
}
