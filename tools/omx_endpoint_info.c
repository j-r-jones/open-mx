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
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <getopt.h>

#include "omx_lib.h"

#define BID 0

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, " -b <n>\tchange board id [%d]\n", BID);
}

int main(int argc, char *argv[])
{
  struct omx_board_info board_info;
  struct omx_cmd_get_endpoint_info get_endpoint_info;
  char board_addr_str[OMX_BOARD_ADDR_STRLEN];
  uint32_t board_index = BID;
  omx_return_t ret;
  uint32_t emax;
  int i, err;
  int c;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  /* get endpoint max */
  emax = omx__driver_desc->endpoint_max;

  while ((c = getopt(argc, argv, "b:h")) != -1)
    switch (c) {
    case 'b':
      board_index = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  /* get the board id */
  ret = omx__get_board_info(NULL, board_index, &board_info);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to read board #%d id, %s\n", board_index, omx_strerror(ret));
    goto out;
  }
  omx__board_addr_sprintf(board_addr_str, board_info.addr);
  printf("%s (board #%d name %s addr %s)\n",
	 board_info.hostname, board_index, board_info.ifacename, board_addr_str);
  printf("==============================================\n");

  get_endpoint_info.board_index = board_index;
  get_endpoint_info.endpoint_index = OMX_RAW_ENDPOINT_INDEX;

  err = ioctl(omx__globals.control_fd, OMX_CMD_GET_ENDPOINT_INFO, &get_endpoint_info);
  if (err < 0)
    goto out;
  OMX_VALGRIND_MEMORY_MAKE_READABLE(&get_endpoint_info, sizeof(get_endpoint_info));

  if (get_endpoint_info.info.closed)
    printf("  raw\tnot open\n");
  else
    printf("  raw\topen by pid %ld (%s)\n",
	   (unsigned long) get_endpoint_info.info.pid, get_endpoint_info.info.command);

  for(i=0; i<emax; i++) {
    get_endpoint_info.board_index = board_index;
    get_endpoint_info.endpoint_index = i;

    err = ioctl(omx__globals.control_fd, OMX_CMD_GET_ENDPOINT_INFO, &get_endpoint_info);
    if (err < 0)
      goto out;
    OMX_VALGRIND_MEMORY_MAKE_READABLE(&get_endpoint_info, sizeof(get_endpoint_info));

    if (get_endpoint_info.info.closed)
      printf("  %d\tnot open\n", i);
    else
      printf("  %d\topen by pid %ld (%s)\n", i,
	     (unsigned long) get_endpoint_info.info.pid, get_endpoint_info.info.command);
  }

  return 0;

 out:
  return -1;
}
