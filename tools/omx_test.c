#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "omx_io.h"

int main()
{
  struct omx_cmd_set_endpoint set;
  int fd, fd2;
  int err;

  printf("opening the device...\n");
  fd = open("/dev/open-mx", O_RDWR);
  if (fd < 0) {
    perror("open");
    exit(1);
  }

  printf("trying to set endpoint on invalid iface index...\n");
  set.iface_index = 33;
  set.endpoint_index = 0;
  err = ioctl(fd, OMX_CMD_SET_ENDPOINT, &set);
  assert(err == -1);
  assert(errno == EINVAL);

  printf("trying to set endpoint on invalid endpoint index...\n");
  set.iface_index = 0;
  set.endpoint_index = 33;
  err = ioctl(fd, OMX_CMD_SET_ENDPOINT, &set);
  assert(err == -1);
  assert(errno == EINVAL);

  printf("setting first endpoint on first iface...\n");
  set.iface_index = 0;
  set.endpoint_index = 0;
  err = ioctl(fd, OMX_CMD_SET_ENDPOINT, &set);
  assert(!err);

  printf("trying to set endpoint again...\n");
  set.iface_index = 0;
  set.endpoint_index = 1;
  err = ioctl(fd, OMX_CMD_SET_ENDPOINT, &set);
  assert(err == -1);
  assert(errno == EBUSY);

  printf("opening the device again...\n");
  fd2 = open("/dev/open-mx", O_RDWR);
  if (fd2 < 0) {
    perror("open");
    exit(1);
  }

  printf("trying to set busy endpoint...\n");
  set.iface_index = 0;
  set.endpoint_index = 0;
  err = ioctl(fd2, OMX_CMD_SET_ENDPOINT, &set);
  assert(err == -1);
  assert(errno == EBUSY);

  printf("setting second endpoint on first iface...\n");
  set.iface_index = 0;
  set.endpoint_index = 1;
  err = ioctl(fd2, OMX_CMD_SET_ENDPOINT, &set);
  assert(!err);

  printf("closing first endpoint file descriptor...\n");
  err = close(fd);
  assert(!err);

  printf("unsetting second file descriptor...\n");
  err = ioctl(fd2, OMX_CMD_UNSET_ENDPOINT);
  assert(!err);
  printf("trying to unset second file descriptor again...\n");
  err = ioctl(fd2, OMX_CMD_UNSET_ENDPOINT);
  assert(err == -1);
  assert(errno == EINVAL);

  printf("re-setting second file descriptor to first endpoint on first iface...\n");
  set.iface_index = 0;
  set.endpoint_index = 0;
  err = ioctl(fd2, OMX_CMD_SET_ENDPOINT, &set);
  assert(!err);

  printf("waiting a bit to let you detach ifaces underneath...\n");
  sleep(20);

  printf("closing file descriptors...\n");
  close(fd2);

  return 0;
}
