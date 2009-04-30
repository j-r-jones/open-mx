/*
 * Open-MX
 * Copyright © INRIA, CNRS 2007-2009 (see AUTHORS file)
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
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "omx_config.h"
#include "omx_lib.h"

#define BID 0
#define DID 0
#define NB  1000000

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, " -b <n>\tchange board id [%d]\n", BID);
  fprintf(stderr, " -d <n>\tchange destination id [%d]\n", DID);
  fprintf(stderr, " -n <n>\tchange number of sent messages [%d]\n", NB);
}

static inline int
do_spam(int fd, int peer_index, int board_index, int nb)
{
  struct omx_cmd_send_spam spam;
  spam.peer_index = peer_index;
  spam.board_index = board_index;
  spam.nb = nb;
  return ioctl(fd, OMX_CMD_SEND_SPAM, &spam);
}

int main(int argc, char *argv[])
{
  int fd, c;
  int bid = BID;
  int did = DID;
  int nb = NB;

  fd = open("/dev/" OMX_MAIN_DEVICE_NAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    return -1;
  }

  while ((c = getopt(argc, argv, "d:b:n:h")) != -1)
    switch (c) {
    case 'b':
      bid = atoi(optarg);
      break;
    case 'd':
      did = atoi(optarg);
      break;
    case 'n':
      nb = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  do_spam(fd, did, bid, nb);
  return 0;
}

