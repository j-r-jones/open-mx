#include <errno.h>
#include <sys/ioctl.h>

#include "mpoe_io.h"
#include "mpoe_lib.h"
#include "mpoe_internals.h"

/*
 * Returns the max amount of boards supported by the driver
 */
mpoe_return_t
mpoe__get_board_max(uint32_t * max)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  int err;

  if (!mpoe_globals.initialized) {
    ret = MPOE_NOT_INITIALIZED;
    goto out;
  }

  err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_BOARD_MAX, max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_MAX");
    goto out;
  }

 out:
  return ret;
}

/*
 * Returns the max amount of endpoints per board supported by the driver
 */
mpoe_return_t
mpoe__get_endpoint_max(uint32_t * max)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  int err;

  if (!mpoe_globals.initialized) {
    ret = MPOE_NOT_INITIALIZED;
    goto out;
  }

  err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_ENDPOINT_MAX, max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_ENDPOINT_MAX");
    goto out;
  }

 out:
  return ret;
}

/*
 * Returns the current amount of boards attached to the driver
 */
mpoe_return_t
mpoe__get_board_count(uint32_t * count)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  int err;

  if (!mpoe_globals.initialized) {
    ret = MPOE_NOT_INITIALIZED;
    goto out;
  }

  err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_BOARD_COUNT, count);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_COUNT");
    goto out;
  }

 out:
  return ret;
}

/*
 * Returns the board id of the endpoint is non NULL,
 * or the current board corresponding to the index.
 *
 * index, name and addr pointers may be NULL is unused.
 */
mpoe_return_t
mpoe__get_board_id(struct mpoe_endpoint * ep, uint8_t * index,
		   char * name, uint64_t * addr)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  struct mpoe_cmd_get_board_id board_id;
  int err, fd;

  if (!mpoe_globals.initialized) {
    ret = MPOE_NOT_INITIALIZED;
    goto out;
  }

  if (ep) {
    /* use the endpoint fd */
    fd = ep->fd;
  } else {
    /* use the control fd and the index */
    fd = mpoe_globals.control_fd;
    board_id.board_index = *index;
  }

  err = ioctl(fd, MPOE_CMD_GET_BOARD_ID, &board_id);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_ID");
    goto out;
  }

  if (name)
    strncpy(name, board_id.board_name, MPOE_IF_NAMESIZE);
  if (index)
    *index = board_id.board_index;
  if (addr)
    *addr = board_id.board_addr;

 out:
  return ret;
}

/*
 * Returns the current index of a board given by its name
 */
mpoe_return_t
mpoe__get_board_index_by_name(const char * name, uint8_t * index)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  uint32_t max;
  int err, i;

  if (!mpoe_globals.initialized) {
    ret = MPOE_NOT_INITIALIZED;
    goto out;
  }

  err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_BOARD_MAX, &max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_MAX");
    goto out;
  }

  ret = MPOE_INVALID_PARAMETER;
  for(i=0; i<max; i++) {
    struct mpoe_cmd_get_board_id board_id;

    board_id.board_index = i;
    err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_BOARD_ID, &board_id);
    if (err < 0) {
      ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_ID");
      if (ret != MPOE_INVALID_PARAMETER)
	goto out;
    }

    if (!strncmp(name, board_id.board_name, MPOE_IF_NAMESIZE)) {
      ret = MPOE_SUCCESS;
      *index = i;
      break;
    }
  }

 out:
  return ret;
}

/*
 * Returns the current index of a board given by its addr
 */
mpoe_return_t
mpoe__get_board_index_by_addr(uint64_t addr, uint8_t * index)
{
  mpoe_return_t ret = MPOE_SUCCESS;
  uint32_t max;
  int err, i;

  if (!mpoe_globals.initialized) {
    ret = MPOE_NOT_INITIALIZED;
    goto out;
  }

  err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_BOARD_MAX, &max);
  if (err < 0) {
    ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_MAX");
    goto out;
  }

  ret = MPOE_INVALID_PARAMETER;
  for(i=0; i<max; i++) {
    struct mpoe_cmd_get_board_id board_id;

    board_id.board_index = i;
    err = ioctl(mpoe_globals.control_fd, MPOE_CMD_GET_BOARD_ID, &board_id);
    if (err < 0) {
      ret = mpoe__errno_to_return(errno, "ioctl GET_BOARD_ID");
      if (ret != MPOE_INVALID_PARAMETER)
	goto out;
    }

    if (addr == board_id.board_addr) {
      ret = MPOE_SUCCESS;
      *index = i;
      break;
    }
  }

 out:
  return ret;
}

/*
 * Returns various info
 */
mpoe_return_t
mpoe_get_info(struct mpoe_endpoint * ep, enum mpoe_info_key key,
	      const void * in_val, uint32_t in_len,
	      void * out_val, uint32_t out_len)
{
  switch (key) {
  case MPOE_INFO_BOARD_MAX:
    if (out_len < sizeof(uint32_t))
      return MPOE_INVALID_PARAMETER;
    return mpoe__get_board_max((uint32_t *) out_val);

  case MPOE_INFO_ENDPOINT_MAX:
    if (out_len < sizeof(uint32_t))
      return MPOE_INVALID_PARAMETER;
    return mpoe__get_endpoint_max((uint32_t *) out_val);

  case MPOE_INFO_BOARD_COUNT:
    if (out_len < sizeof(uint32_t))
      return MPOE_INVALID_PARAMETER;
    return mpoe__get_board_count((uint32_t *) out_val);

  case MPOE_INFO_BOARD_NAME:
  case MPOE_INFO_BOARD_ADDR:
    if (ep) {
      /* use the info stored in the endpoint */
      if (key == MPOE_INFO_BOARD_NAME)
	strncpy(out_val, ep->board_name, out_len);
      else
	memcpy(out_val, &ep->board_addr, out_len > sizeof(ep->board_addr) ? sizeof(ep->board_addr) : out_len);
      return MPOE_SUCCESS;

    } else {
      /* if no endpoint given, ask the driver about the index given in in_val */
      uint64_t addr;
      char name[MPOE_IF_NAMESIZE];
      uint8_t index;
      mpoe_return_t ret;

      if (!in_val || !in_len)
	return MPOE_INVALID_PARAMETER;
      index = *(uint8_t*)in_val;

      ret = mpoe__get_board_id(ep, &index, name, &addr);
      if (ret != MPOE_SUCCESS)
	return ret;

      if (key == MPOE_INFO_BOARD_NAME)
	strncpy(out_val, name, out_len);
      else
	memcpy(out_val, &addr, out_len > sizeof(addr) ? sizeof(addr) : out_len);
    }

  case MPOE_INFO_BOARD_INDEX_BY_ADDR:
  case MPOE_INFO_BOARD_INDEX_BY_NAME:
    if (!out_val || !out_len)
      return MPOE_INVALID_PARAMETER;
    if (ep) {
      /* use the info stored in the endpoint */
      *(uint8_t*) out_val = ep->board_index;
      return MPOE_SUCCESS;

    } else {
      if (key == MPOE_INFO_BOARD_INDEX_BY_NAME) {
	return mpoe__get_board_index_by_name(in_val, out_val);
      } else {
	if (in_len < sizeof(uint64_t))
	  return MPOE_INVALID_PARAMETER;
	return mpoe__get_board_index_by_addr(*(uint64_t *) in_val, out_val);
      }
    }

  default:
    return MPOE_INVALID_PARAMETER;
  }

  return MPOE_SUCCESS;
}
