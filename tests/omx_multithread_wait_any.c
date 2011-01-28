/*
 * Open-MX
 * Copyright © INRIA 2010
 * (see AUTHORS file)
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


#include <sys/types.h>
#include <sys/wait.h>

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "open-mx.h"


#define BID 0
#define EID 0
#define RID 0
#define ITER 1000
#define DELAY 5

typedef struct cl_req
{
    int verbose;
    unsigned bid;
    unsigned eid;
    int rid;
    int iter;

    int sender;

    struct {
	struct {
	    unsigned nb_threads;
	} recv;

	struct {
	    char dest_hostname[OMX_HOSTNAMELEN_MAX];
	    int constant_delay;
	    unsigned delay;
	    unsigned nb_processes;
	} send;
    } side;

} cl_req_t;


typedef struct param_msg {
  uint32_t iter;
  uint32_t nbsender;
} param_msg_t;


typedef struct peer {
    char hostname[OMX_HOSTNAMELEN_MAX];
    uint64_t addr;
    uint32_t rid;
    omx_endpoint_addr_t ep_addr;
} peer_t;


typedef struct thread_param {
    cl_req_t *cl_req;
    omx_endpoint_t ep;
    int nb_thread_iter;
} thread_param_t;


#define check_val(expr, errval, log, retval)		\
    if ((errval) == (expr)) {				\
	(log);						\
	return retval;					\
    }

#define check_val_neg(expr, errval, log, retval)	\
    if ((errval) != (expr)) {				\
	(log);						\
	return retval;					\
    }

#define check_omx(expr, log, retval)			\
    check_val_neg ((expr), OMX_SUCCESS, (log), retval)

#define check(expr, log, retval)		\
    check_val ((expr), -1, (log), retval)




#define try_val(expr, errval, log, label)	\
    if ((errval) == (expr)) {			\
	(log);					\
	goto label;				\
    }						\

#define try_val_neg(expr, errval, log, label)	\
    if ((errval) != (expr)) {			\
	(log);					\
	goto label;				\
    }						\

#define try(expr, log, label)			\
    try_val ((expr), -1, (log), label)

#define try_omx(expr, log, label)			\
    try_val_neg ((expr), OMX_SUCCESS, (log), label)



static void
usage(int argc, char *argv[])
{
    fprintf(stderr, "%s [options]\n", argv[0]);

    fprintf(stderr, "Common options:\n");
    fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
    fprintf(stderr, " -v\tverbose\n");

    fprintf(stderr, "Sender options:\n");
    fprintf(stderr, " -d <hostname>\tset remote peer name and switch to sender mode\n");
    fprintf(stderr, " -D <n>\tset the delay (in microseconds) between message sendings [%d]\n", DELAY);
    fprintf(stderr, " -r <n>\tchange remote endpoint id [%d]\n", RID);
    fprintf(stderr, " -N <n>\tchange number of iterations [%d]\n", ITER);
    fprintf(stderr, " -p <n>\tchange the number of sender processes [nbcores]\n");

    fprintf(stderr, "Receiver options:\n");
    fprintf(stderr, " -t <n>\tchange the number of receiver threads [2*nbprocs]\n");
}


int parse_cl (int argc, char *argv[], cl_req_t *cl_req)
{
    int c;

    while ((c = getopt (argc, argv, "b:d:D:r:t:p:N:hv")) != -1)
	switch (c) {
	case 'b':
	    cl_req->bid = atoi(optarg);
	    break;
	case 'd':
	    cl_req->sender = 1;
	    strncpy (cl_req->side.send.dest_hostname, optarg, OMX_HOSTNAMELEN_MAX);
	    cl_req->eid = OMX_ANY_ENDPOINT;
	    break;
	case 'D':
	    cl_req->side.send.delay	     = atoi(optarg);
	    cl_req->side.send.constant_delay = 1;
	    break;
	case 'v':
	    cl_req->verbose = 1;
	    break;
	case 'r':
	    cl_req->rid = atoi(optarg);
	    break;
	case 't':
	    cl_req->side.recv.nb_threads = atoi(optarg);
	    break;
	case 'p':
	    cl_req->side.send.nb_processes = atoi(optarg);
	    break;
	case 'N':
	    cl_req->iter = atoi (optarg);
	    break;
	default:
	    fprintf (stderr, "Unknown option -%c\n", c);
	case 'h':
	    usage (argc, argv);
	    exit (-1);
	}

    return 0;
}


int init_omx (cl_req_t *cl_req, omx_endpoint_t *ep, char *my_hostname, char *my_ifacename)
{
    omx_return_t ret;

    check_omx (ret = omx_init (),
	       fprintf (stderr, "Failed to initialize (%s)\n", omx_strerror(ret)), -1);
    check_omx (omx_open_endpoint (cl_req->bid, cl_req->eid, 0x12345678, NULL, 0, ep),
	       fprintf (stderr, "Failed to open endpoint (%s)\n", omx_strerror(ret)), -1);

    try_omx (omx_get_info (*ep, OMX_INFO_BOARD_HOSTNAME, NULL, 0, my_hostname, sizeof(my_hostname)),
	     fprintf(stderr, "Failed to get endpoint hostname (%s)\n", omx_strerror(ret)),
	     out_with_ep);
    try_omx (omx_get_info (*ep, OMX_INFO_BOARD_IFACENAME, NULL, 0, my_ifacename, sizeof(my_ifacename)),
	     fprintf (stderr, "Failed to get endpoint iface name (%s)\n", omx_strerror(ret)),
	     out_with_ep);

    return 0;

out_with_ep:
    omx_close_endpoint (*ep);

    return -1;
}


static inline int send_null_message (omx_endpoint_t ep, omx_endpoint_addr_t dest_ep)
{
    omx_request_t req;
    omx_status_t status;
    omx_return_t ret;
    uint32_t result;

    check_omx (ret = omx_isend (ep, NULL, 0, dest_ep, 0x1234567887654321ULL, NULL, &req),
	       fprintf(stderr, "Failed to isend void message (%s)\n", omx_strerror(ret)), -1);

    check_omx (ret = omx_wait (ep, &req, &status, &result, OMX_TIMEOUT_INFINITE),
	       fprintf(stderr, "Failed to wait isend param message (%s)\n", omx_strerror(ret)), -1);

    return 0;
}


void fork_sender (cl_req_t *cl_req)
{
    char my_hostname[OMX_HOSTNAMELEN_MAX];
    char my_ifacename[OMX_BOARD_ADDR_STRLEN];
    uint32_t result;
    omx_endpoint_t ep;
    omx_request_t req;
    omx_status_t status;
    omx_return_t ret;

    param_msg_t param_msg;
    uint64_t dest_addr;
    omx_endpoint_addr_t dest_ep;
    int i, j;

    pid_t cpid = fork();

    if (cpid != 0)
	return;

    try (init_omx (cl_req, &ep, my_hostname, my_ifacename),
	 fprintf (stderr, "Failed to initialize Open-MX subsystem."), out);

    try_omx (omx_hostname_to_nic_id (cl_req->side.send.dest_hostname, &dest_addr),
	     fprintf (stderr, "Cannot find peer name %s\n", cl_req->side.send.dest_hostname), out);

    printf("Starting sender to '%s'...\n", cl_req->side.send.dest_hostname);

    try_omx (ret = omx_connect (ep, dest_addr, cl_req->rid, 0x12345678, OMX_TIMEOUT_INFINITE,
				&dest_ep),
	     fprintf (stderr, "Failed to connect (%s)\n", omx_strerror(ret)), out_with_ep);

    /* Send the param message */
    param_msg.iter = htonl (cl_req->iter);
    param_msg.nbsender = htonl (cl_req->side.send.nb_processes);
    try_omx (ret = omx_issend (ep, &param_msg, sizeof(param_msg), dest_ep, 0x1234567887654321ULL, NULL,
			       &req),
	     fprintf(stderr, "Failed to isend param message (%s)\n", omx_strerror(ret)), out_with_ep);

    try_omx (ret = omx_wait (ep, &req, &status, &result, OMX_TIMEOUT_INFINITE),
	     fprintf (stderr, "Failed to wait isend param message (%s)\n", omx_strerror(ret)),
	     out_with_ep);

    try_omx (status.code,
	     fprintf (stderr, "isend param message failed with status (%s)\n",
		      omx_strerror(status.code)), out_with_ep);

    if (cl_req->verbose)
	printf ("Sent parameters (iter=%d)\n", cl_req->iter);


    /* Wait for the ok message */
    try_omx (ret = omx_irecv (ep, NULL, 0, 0, 0, NULL, &req),
    	     fprintf(stderr, "Failed to irecv param ack message (%s)\n", omx_strerror(ret)),
    	     out_with_ep);

    try_omx (ret = omx_wait (ep, &req, &status, &result, OMX_TIMEOUT_INFINITE),
    	     fprintf(stderr, "Failed to wait param ack message (%s)\n", omx_strerror(ret)),
    	     out_with_ep);

    try_omx (status.code,
    	     fprintf(stderr, "param ack message failed with status (%s)\n", omx_strerror(status.code)),
    	     out_with_ep);


    /* Send the cl_req->iter null messages */

    /* with a constant delay */
    if (cl_req->side.send.constant_delay)
	for (i = 0; i < cl_req->iter; i++) {
	    usleep (cl_req->side.send.delay);
	    try (send_null_message (ep, dest_ep),
		 fprintf (stderr, "failed to send a null message.\n"), out_with_ep)
	}

    /* with a variable delay */
    else {
	div_t slice_spec = div (cl_req->iter, 10);
	unsigned delays[10] = { 0,
				1, 1<<1, 1<<2, 1<<3, 1<<4, 1<<5, 1<<6, 1<<7,
				1000 };

	for (i = 0; i < 9; i++)
	    for (j = 0; j < slice_spec.quot; j++) {
		usleep (delays[i]);
		try (send_null_message (ep, dest_ep),
		     fprintf (stderr, "failed to send a null message.\n"), out_with_ep);
	    }

	for (j = 0; j < (slice_spec.quot + slice_spec.rem); j++) {
	    usleep (delays[9]);
	    try (send_null_message (ep, dest_ep),
		 fprintf (stderr, "failed to send a null message.\n"), out_with_ep);
	}
    }


    /* Wait for the goodbye message */
    try_omx (ret = omx_irecv (ep, NULL, 0, 0, 0, NULL, &req),
    	     fprintf(stderr, "Failed to irecv goodbye message (%s)\n", omx_strerror(ret)),
    	     out_with_ep);

    try_omx (ret = omx_wait (ep, &req, &status, &result, OMX_TIMEOUT_INFINITE),
    	     fprintf(stderr, "Failed to wait goodbye message (%s)\n", omx_strerror(ret)),
    	     out_with_ep);

    try_omx (status.code,
    	     fprintf(stderr, "goodbye message failed with status (%s)\n", omx_strerror(status.code)),
    	     out_with_ep);


    omx_close_endpoint (ep);
    omx_finalize ();

    exit (0);


out_with_ep:
    omx_close_endpoint (ep);
out:
    omx_finalize ();

    exit (1);
}


void * thread_receive (void *uncasted_thread_param)
{
    omx_endpoint_t ep  = ((thread_param_t *)uncasted_thread_param)->ep;
    int nb_thread_iter = ((thread_param_t *)uncasted_thread_param)->nb_thread_iter;

    omx_status_t status;
    uint32_t result;
    omx_return_t ret;
    int i;

    for (i = 0; i < nb_thread_iter; i++) {
	try_omx (ret = omx_wait_any (ep, 0, 0, &status, &result, OMX_TIMEOUT_INFINITE),
		 fprintf (stderr, "Failed to wait null message %d (%s)\n", i, omx_strerror(ret)),
		 out);

	try_omx (status.code,
		 fprintf(stderr, "irecv null message %d failed with status (%s)\n",
			 i, omx_strerror(status.code)), out);
    }

out:
    pthread_exit (NULL);
}

#ifdef OMX_HAVE_HWLOC
#include <hwloc.h>

static hwloc_topology_t topology = NULL;

static void
topology_init(void)
{
  if (!topology) {
    hwloc_topology_init(&topology);
    hwloc_topology_load(topology);
  }
}

static unsigned
get_nbcores(void)
{
  topology_init();
  return hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_CORE);
}

static unsigned
get_nbthreads(void)
{
  topology_init();
  return hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_PU);
}

static void
topology_exit(void)
{
  if (topology)
    hwloc_topology_destroy(topology);
}
#else
#define get_nbcores() sysconf(_SC_NPROCESSORS_ONLN)
#define get_nbthreads() sysconf(_SC_NPROCESSORS_ONLN)
#define topology_exit() /* nothing to do */
#endif

int main (int argc, char *argv[])
{
    int i;
    int child_status;
    unsigned nbcores = get_nbcores();
    unsigned nbthreads = get_nbthreads();

    cl_req_t cl_req = { .verbose	= 0,
			.eid		= EID,
			.bid		= BID,
			.rid		= RID,
			.iter		= ITER,
			.sender		= 0,
			.side.send	= { .delay	    = DELAY,
					    .constant_delay = 0,
					    .nb_processes   = nbcores },
			.side.recv	= { .nb_threads = 2*nbthreads } };

    parse_cl (argc, argv, &cl_req);

    if (cl_req.sender) {
	/* Sender */

	printf("Starting %u sender processes\n", cl_req.side.send.nb_processes);

	for (i = 0; i < cl_req.side.send.nb_processes; i++)
	    fork_sender (&cl_req);

	while (-1 != wait (&child_status)) {
	    if (! WIFEXITED (child_status) || (WEXITSTATUS(child_status) != 0))
		fprintf (stderr, "a child exited anormally\n");
	}
	if (errno != ECHILD)
	    fprintf (stderr, "failed to wait for a child.\n");

    } else {
	/* Receiver */
	char my_hostname[OMX_HOSTNAMELEN_MAX];
	char my_ifacename[OMX_BOARD_ADDR_STRLEN];
	uint32_t result;
	omx_endpoint_t ep;
	omx_request_t req;
	omx_status_t status;
	omx_return_t ret;
	param_msg_t param_msg;
        param_msg.nbsender = 0;
	peer_t *peers;

	pthread_t threads[cl_req.side.recv.nb_threads];

	param_msg.nbsender = 0; /* default value until we receive an actual value from senders */

	try (init_omx (&cl_req, &ep, my_hostname, my_ifacename),
	     fprintf (stderr, "Failed to initialize Open-MX subsystem."), out);

	/* Wait for the param messages */
	for (i = 0; i == 0 /* need one client to initialize param_msg.nbsender */ || i < param_msg.nbsender; i++) {
	    try_omx (ret = omx_irecv (ep, &param_msg, sizeof(param_msg), 0, 0, NULL, &req),
		     fprintf (stderr, "Failed to irecv null message %d (%s)\n", i, omx_strerror(ret)),
		     out);

	    try_omx (ret = omx_wait (ep, &req, &status, &result, OMX_TIMEOUT_INFINITE),
		     fprintf (stderr, "Failed to wait param message (%s)\n", omx_strerror(ret)),
		     out_with_ep);

	    try_omx (status.code,
		     fprintf(stderr, "irecv param message failed with status (%s)\n",
			     omx_strerror(status.code)), out_with_ep);

	    /* Retrieve parameters */
	    param_msg.iter = ntohl (param_msg.iter);
	    param_msg.nbsender = ntohl (param_msg.nbsender);
	    if (i == 0)
		peers = malloc(param_msg.nbsender * sizeof(peer_t));

	    try_omx (ret = omx_decompose_endpoint_addr (status.addr, &peers[i].addr,
							&peers[i].rid),
		     fprintf (stderr, "Failed to decompose sender's address (%s)\n",
			      omx_strerror(ret)),
		     out_with_ep);

	    if (OMX_SUCCESS != omx_nic_id_to_hostname (peers[i].addr, peers[i].hostname))
		strncpy (peers[i].hostname, "<unknown peer>", OMX_HOSTNAMELEN_MAX);

	    if (cl_req.verbose)
		printf("Got parameters (iter=%d) from peer %s\n", param_msg.iter, peers[i].hostname);
	}


	/* Send the param ack messages */
	for (i = 0; i < param_msg.nbsender; i++) {
	    try_omx (ret = omx_connect(ep, peers[i].addr, peers[i].rid, 0x12345678,
				       OMX_TIMEOUT_INFINITE, &peers[i].ep_addr),
		     fprintf (stderr, "Failed to connect back to client (%s)\n", omx_strerror(ret)),
		     out_with_ep);

	    try_omx (ret = omx_issend (ep, NULL, 0, peers[i].ep_addr, 0, NULL, &req),
		     fprintf (stderr, "Failed to isend param ack message (%s)\n", omx_strerror(ret)),
		     out_with_ep);

	    try_omx (ret = omx_wait (ep, &req, &status, &result, OMX_TIMEOUT_INFINITE),
		     fprintf(stderr, "Failed to wait param ack message (%s)\n", omx_strerror(ret)),
		     out_with_ep);

	    try_omx (status.code,
		     fprintf (stderr, "isend param message failed with status (%s)\n",
			      omx_strerror(status.code)), out_with_ep);
	}


	/* Post the (nb_processes * iter) receives */
	for (i = 0; i < param_msg.nbsender * param_msg.iter; i++)
	    try_omx (ret = omx_irecv (ep, NULL, 0, 0, 0, NULL, &req),
		     fprintf (stderr, "Failed to irecv null message %d (%s)\n", i, omx_strerror(ret)),
		     out);

	printf("Starting %u receiver threads\n", cl_req.side.recv.nb_threads);

	div_t nb_thread_iter = div (param_msg.nbsender * param_msg.iter,
				    cl_req.side.recv.nb_threads);
	thread_param_t thread_param = { .ep		= ep,
					.nb_thread_iter = nb_thread_iter.quot };
	for (i = 0; i < cl_req.side.recv.nb_threads; i++)
	    pthread_create (&threads[i], NULL, thread_receive, &thread_param);

	for (i = 0; i < cl_req.side.recv.nb_threads; i++)
	    pthread_join (threads[i], NULL);


	/* Send the 'goodbye' messages */
	for (i = 0; i < param_msg.nbsender; i++) {
	    try_omx (ret = omx_issend (ep, NULL, 0, peers[i].ep_addr, 0, NULL, &req),
		     fprintf (stderr, "Failed to isend goodbye message (%s)\n", omx_strerror(ret)),
		     out_with_ep);

	    try_omx (ret = omx_wait (ep, &req, &status, &result, OMX_TIMEOUT_INFINITE),
		     fprintf(stderr, "Failed to wait goodbye message (%s)\n", omx_strerror(ret)),
		     out_with_ep);

	    try_omx (status.code,
		     fprintf (stderr, "isend goodbye message failed with status (%s)\n",
			      omx_strerror(status.code)), out_with_ep);
	}

        free(peers);

	omx_close_endpoint (ep);
	omx_finalize ();
	topology_exit();
	return 0;

    out_with_ep:
	omx_close_endpoint (ep);
    out:
	omx_finalize ();
	topology_exit();
	return 1;
    }

    topology_exit();
    return 0;
}
