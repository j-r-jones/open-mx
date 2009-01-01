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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>

#include "open-mx.h"

#define BID 0
#define EID OMX_ANY_ENDPOINT
#define ITER 10
#define LEN 4000000
#define NSEG_MAX 10

static int verbose = 0;

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, " -s\tdo not disable shared communications\n");
  fprintf(stderr, " -S\tdo not disable self communications\n");
  fprintf(stderr, " -v\tverbose\n");
}

#define DEFAULT_SEND_CHAR 'a'
#define DEFAULT_RECV_CHAR 'b'
#define REAL_SEND_FIRST_CHAR 'm'
#define REAL_SEND_LAST_CHAR 'z'

static void
vect_send_to_contig_recv(omx_endpoint_t ep, omx_endpoint_addr_t addr,
			 omx_seg_t *segs, int nseg,
			 char *sbuf, char *rbuf)
{
  omx_request_t sreq, rreq;
  omx_return_t ret;
  omx_status_t status;
  uint32_t result;
  uint32_t len;
  int i,j;
  char c;

  /* initialize the whole send buffer to DEFAULT_SEND_CHAR */
  memset(sbuf, DEFAULT_SEND_CHAR, LEN);

  /* initialize send segments to chars between REAL_SEND_FIRST_CHAR and REAL_SEND_LAST_CHAR */
  c = REAL_SEND_FIRST_CHAR;
  for(i=0, len=0; i<nseg; i++) {
    char *buf = segs[i].ptr;
    for(j=0; j<segs[i].len; j++) {
      buf[j] = c;
      if (c++ == REAL_SEND_LAST_CHAR) c = REAL_SEND_FIRST_CHAR;
    }

    len += segs[i].len;
  }

  /* initialize the whole recv buffer to DEFAULT_RECV_CHAR */
  memset(rbuf, DEFAULT_RECV_CHAR, LEN);

  if (verbose)
    printf("  sending %ld as %d segments\n", (unsigned long) len, (unsigned) nseg);

  ret = omx_irecv(ep, rbuf, len, 0, 0, NULL, &rreq);
  assert(ret == OMX_SUCCESS);
  ret = omx_isendv(ep, segs, nseg, addr, 0, NULL, &sreq);
  assert(ret == OMX_SUCCESS);
  ret = omx_wait(ep, &sreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_SUCCESS);
  ret = omx_wait(ep, &rreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_SUCCESS);

  /* check recv segment are chars between REAL_SEND_FIRST_CHAR and REAL_SEND_LAST_CHAR */
  c = REAL_SEND_FIRST_CHAR;
  for(j=0; j<len; j++) {
    if (rbuf[j] != c) {
      fprintf(stderr, "byte %d changed from %c to %c instead of %c\n",
	      j, DEFAULT_RECV_CHAR, rbuf[j], c);
      assert(0);
    }
    if (c++ == REAL_SEND_LAST_CHAR) c = REAL_SEND_FIRST_CHAR;
  }
  if (verbose)
    printf("  rbuf buffer modified as expected\n");

  /* reset the recv segment to DEFAULT_RECV_CHAR */
  memset(rbuf, DEFAULT_RECV_CHAR, len);

  /* check that the whole recv buffer is DEFAULT_RECV_CHAR now */
  for(j=0; j<LEN; j++)
    if (rbuf[j] != DEFAULT_RECV_CHAR) {
      fprintf(stderr, "byte %d should not have changed from %c to %c\n",
	      j, DEFAULT_RECV_CHAR, rbuf[j]);
      assert(0);
    }
  if (verbose)
    printf("  rbuf outside-of-buffer not modified, as expected\n");
}

static void
contig_send_to_vect_recv(omx_endpoint_t ep, omx_endpoint_addr_t addr,
			 char *sbuf,
			 omx_seg_t *segs, int nseg, char *rbuf)
{
  omx_request_t sreq, rreq;
  omx_return_t ret;
  omx_status_t status;
  uint32_t result;
  uint32_t len;
  int i,j;
  char c;

  for(i=0, len=0; i<nseg; i++) {
    len += segs[i].len;
  }

  if (verbose)
    printf("  receiving %ld as %d segments\n", (unsigned long) len, (unsigned) nseg);

  /* initialize the whole send buffer to DEFAULT_SEND_CHAR */
  memset(sbuf, DEFAULT_SEND_CHAR, LEN);

  /* initialize the send segment to chars between REAL_SEND_FIRST_CHAR and REAL_SEND_LAST_CHAR */
  c = REAL_SEND_FIRST_CHAR;
  for(j=0; j<len; j++) {
    sbuf[j] = c;
    if (c++ > REAL_SEND_LAST_CHAR) c = REAL_SEND_FIRST_CHAR;
  }

  /* initialize the whole recv buffer to DEFAULT_RECV_CHAR */
  memset(rbuf, DEFAULT_RECV_CHAR, LEN);

  ret = omx_irecvv(ep, segs, nseg, 0, 0, NULL, &rreq);
  assert(ret == OMX_SUCCESS);
  ret = omx_isend(ep, sbuf, len, addr, 0, NULL, &sreq);
  assert(ret == OMX_SUCCESS);
  ret = omx_wait(ep, &sreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_SUCCESS);
  ret = omx_wait(ep, &rreq, &status, &result, OMX_TIMEOUT_INFINITE);
  assert(ret == OMX_SUCCESS);
  assert(result);
  assert(status.code == OMX_SUCCESS);

  /* check recv segments are chars between REAL_SEND_FIRST_CHAR and REAL_SEND_LAST_CHAR */
  c = REAL_SEND_FIRST_CHAR;
  for(i=0; i<nseg; i++) {
    char *buf = segs[i].ptr;
    for(j=0; j<segs[i].len; j++) {
      if (buf[j] != c) {
        fprintf(stderr, "byte %d in seg %d changed from %c to %c instead of %c\n",
		j, i, DEFAULT_RECV_CHAR, buf[j], c);
	assert(0);
      }
      if (c++ > REAL_SEND_LAST_CHAR) c = REAL_SEND_FIRST_CHAR;
    }
  }
  if (verbose)
    printf("  rbuf segments modified as expected\n");

  /* reset the recv segment to DEFAULT_RECV_CHAR */
  for(i=0; i<nseg; i++)
    memset(segs[i].ptr, DEFAULT_RECV_CHAR, segs[i].len);

  /* check that the whole recv buffer is DEFAULT_RECV_CHAR now */
  for(j=0; j<LEN; j++)
    if (rbuf[j] != DEFAULT_RECV_CHAR) {
      fprintf(stderr, "byte %d should not have changed from %c to %c\n",
	      j, DEFAULT_RECV_CHAR, rbuf[j]);
      assert(0);
    }
  if (verbose)
    printf("  rbuf outside-of-segments not modified, as expected\n");
}

int main(int argc, char *argv[])
{
  omx_endpoint_t ep;
  uint64_t dest_board_addr;
  int board_index = BID;
  int endpoint_index = EID;
  char hostname[OMX_HOSTNAMELEN_MAX];
  char ifacename[16];
  omx_endpoint_addr_t addr;
  int self = 0;
  int shared = 0;
  int c;
  int i, j;
  omx_seg_t seg[NSEG_MAX];
  void * buffer1, * buffer2;
  omx_return_t ret;
  int nseg = 0;

  while ((c = getopt(argc, argv, "e:b:sShv")) != -1)
    switch (c) {
    case 'b':
      board_index = atoi(optarg);
      break;
    case 'e':
      endpoint_index = atoi(optarg);
      break;
    case 's':
      shared = 1;
      break;
    case 'S':
      self = 1;
      break;
    case 'v':
      verbose = 1;
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  if (!self && !getenv("OMX_DISABLE_SELF"))
    putenv("OMX_DISABLE_SELF=1");

  if (!shared && !getenv("OMX_DISABLE_SHARED"))
    putenv("OMX_DISABLE_SHARED=1");

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  ret = omx_board_number_to_nic_id(board_index, &dest_board_addr);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to find board %d nic id (%s)\n",
	    board_index, omx_strerror(ret));
    goto out;
  }

  ret = omx_open_endpoint(board_index, endpoint_index, 0x12345678, NULL, 0, &ep);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  ret = omx_get_info(ep, OMX_INFO_BOARD_HOSTNAME, NULL, 0,
		     hostname, sizeof(hostname));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to find board hostname (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  ret = omx_get_info(ep, OMX_INFO_BOARD_IFACENAME, NULL, 0,
		     ifacename, sizeof(ifacename));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to find board iface name (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  printf("Using board #%d name '%s' hostname '%s'\n", board_index, ifacename, hostname);

  ret = omx_get_endpoint_addr(ep, &addr);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get local endpoint address (%s)\n",
	    omx_strerror(ret));
    goto out_with_ep;
  }

  buffer1 = malloc(LEN);
  buffer2 = malloc(LEN);
  if (buffer1 == NULL || buffer2 == NULL) {
    fprintf(stderr, "failed to allocate buffers\n");
    goto out_with_ep;
  }

  seg[0].ptr = buffer1 + 53;
  seg[0].len = 7; /* total  7, tiny */
  seg[1].ptr = buffer1 + 5672;
  seg[1].len = 23; /* total 30, tiny */
  seg[2].ptr = buffer1 + 8191;
  seg[2].len = 61; /* total 91, small */
  seg[3].ptr = buffer1 + 10001;
  seg[3].len = 26; /* total 117, small */
  seg[4].ptr = NULL;
  seg[4].len = 0; /* total 117, small */
  seg[5].ptr = buffer1 + 11111;
  seg[5].len = 13456; /* total 13573, medium */
  seg[6].ptr = NULL;
  seg[6].len = 0; /* total 13573, medium */
  seg[7].ptr = buffer1 + 50000;
  seg[7].len = 11111; /* total 24684, medium */
  seg[8].ptr = buffer1 + 100000;
  seg[8].len = 333333; /* total 357814, large */
  seg[9].ptr = buffer1 + 1000000;
  seg[9].len = 3000000; /* total 3357814, large */
  nseg = 10;
  assert(nseg <= NSEG_MAX);

  for(i=0; i<=nseg; i++)
    for(j=0; j<=nseg-i; j++) {
      printf("sending %d segments starting as #%d\n", j, i);
      vect_send_to_contig_recv(ep, addr, seg+i, j, buffer1, buffer2);
    }

  for(i=0; i<=nseg; i++)
    for(j=0; j<=nseg-i; j++) {
      printf("receiving %d segments starting as #%d\n", j, i);
      contig_send_to_vect_recv(ep, addr, buffer2, seg+i, j, buffer1);
    }

  omx_close_endpoint(ep);
  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  return -1;
}
