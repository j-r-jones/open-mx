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
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "omx_config.h"
#include "omx_lib.h"

static inline int
do_spam(int fd, int index)
{
  struct omx_cmd_send_spam spam;
  spam.peer_index = index;
  spam.board_index = 0;
  spam.nb = 1000000;
  return ioctl(fd, OMX_CMD_SEND_SPAM, &spam);
}

int main(int argc, char *argv[])
{
  int fd;

  fd = open("/dev/" OMX_MAIN_DEVICE_NAME, O_RDWR);
  if (fd < 0) {
    perror("open");
    return -1;
  }

  do_spam(fd, 1);
  return 0;
}

