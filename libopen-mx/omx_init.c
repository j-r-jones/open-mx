/*
 * Open-MX
 * Copyright © INRIA, CNRS 2007-2009 (see AUTHORS file)
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

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <signal.h>

#include "omx_lib.h"
#include "omx_wire.h"

#define OMX_ZOMBIE_MAX_DEFAULT 512

struct omx__globals omx__globals = { 0 };
volatile struct omx_driver_desc * omx__driver_desc = NULL;

static int omx__lib_api = OMX_API;

/* API omx__init_api */
omx_return_t
omx__init_api(int app_api)
{
  omx_return_t ret;
  char *env;
  int err;

  /**************************
   * Messaging configuration
   */
  env = getenv("OMX_VERBOSE_PREFIX");
  if (!env)
    omx__globals.message_prefix_format = "Open-MX: ";
  else if (*env == '1')
    omx__globals.message_prefix_format = "Open-MX:p%P-e%E-b%B: ";
  else
    omx__globals.message_prefix_format = env;
  omx__globals.message_prefix = omx__create_message_prefix(NULL);

  /*******************************************
   * Verbose and debug messages configuration
   */

  /* verbose message configuration */
  omx__globals.verbose = 0;
#ifdef OMX_LIB_DEBUG
  omx__globals.verbose = 1;
#endif
  env = getenv("OMX_VERBOSE");
#ifdef OMX_MX_ABI_COMPAT
  if (!env) {
    env = getenv("MX_VERBOSE");
    if (env) {
      omx__printf(NULL, "Emulating MX_VERBOSE as OMX_VERBOSE\n");
      env = "";
    }
  }
#endif /* OMX_MX_ABI_COMPAT */
  if (env)
    omx__globals.verbose = atoi(env);

  /* verbose debug message configuration */
  omx__globals.verbdebug = 0;
#ifdef OMX_LIB_DEBUG
  env = getenv("OMX_VERBDEBUG");
  if (env) {
    char *next;
    unsigned long val = strtoul(env, &next, 0);
    if (env == next) {
      int i;
      val = omx__globals.verbdebug;
      for(i=0; env[i]!='\0'; i++) {
	switch (env[i]) {
	case 'P': val |= OMX_VERBDEBUG_ENDPOINT; break;
	case 'C': val |= OMX_VERBDEBUG_CONNECT; break;
	case 'S': val |= OMX_VERBDEBUG_SEND; break;
	case 'L': val |= OMX_VERBDEBUG_LARGE; break;
	case 'M': val |= OMX_VERBDEBUG_MEDIUM; break;
	case 'Q': val |= OMX_VERBDEBUG_SEQNUM; break;
	case 'R': val |= OMX_VERBDEBUG_RECV; break;
	case 'U': val |= OMX_VERBDEBUG_UNEXP; break;
	case 'E': val |= OMX_VERBDEBUG_EARLY; break;
	case 'A': val |= OMX_VERBDEBUG_ACK; break;
	case 'T': val |= OMX_VERBDEBUG_EVENT; break;
	case 'W': val |= OMX_VERBDEBUG_WAIT; break;
	case 'V': val |= OMX_VERBDEBUG_VECT; break;
	default: omx__abort(NULL, "Unknown verbose debug character '%c'\n", env[i]);
	}
      }
    }
    omx__globals.verbdebug = val;
  }
#endif /* OMX_LIB_DEBUG */

  omx__globals.abort_sleeps = 0;
  env = getenv("OMX_ABORT_SLEEPS");
  if (env) {
    omx__globals.abort_sleeps = atoi(env);
    if (omx__globals.abort_sleeps)
      omx__verbose_printf(NULL, "Will sleep %d seconds in case of abort\n",
			  omx__globals.abort_sleeps);
  }

  /*******************************
   * Check if already initialized
   */
  if (omx__globals.initialized) {
    ret = omx__error(OMX_ALREADY_INITIALIZED, "Initializing the library");
    goto out_with_message_prefix;
  }

  /***************************************
   * Check the application-build-time API
   */
  if (app_api >> 8 != omx__lib_api >> 8
      /* support app_abi 0x0 for now, will drop in 1.0 */
      || app_api == 0) {
    ret = omx__error(OMX_BAD_LIB_ABI,
		     "Comparing library used at build-time (ABI 0x%x) with currently used library (ABI 0x%x)",
		     omx__lib_api >> 8, app_api >> 8);
    goto out_with_message_prefix;
  }

  /*********************************
   * Open, map and check the driver
   */
  err = open("/dev/" OMX_MAIN_DEVICE_NAME, O_RDONLY);
  if (err < 0) {
    ret = omx__errno_to_return();
    if (ret == OMX_INTERNAL_UNEXPECTED_ERRNO)
      ret = omx__error(OMX_BAD_ERROR, "Opening global control device (%m)");
    else if (ret == OMX_INTERNAL_MISC_ENODEV)
      ret = omx__error(OMX_NO_DRIVER, "Opening endpoint control device");
    else
      ret = omx__error(ret, "Opening global control device");
    goto out_with_message_prefix;
  }
  omx__globals.control_fd = err;

  omx__driver_desc = mmap(NULL, OMX_DRIVER_DESC_SIZE, PROT_READ, MAP_SHARED,
			  omx__globals.control_fd, OMX_DRIVER_DESC_FILE_OFFSET);
  if (omx__driver_desc == MAP_FAILED) {
    ret = omx__errno_to_return();
    if (ret == OMX_INTERNAL_MISC_EINVAL || ret == OMX_INTERNAL_MISC_ENODEV || ret == OMX_INTERNAL_UNEXPECTED_ERRNO)
      ret = omx__error(OMX_BAD_ERROR, "Mapping global control device (%m)");
    else
      ret = omx__error(ret, "Mapping global control device");
    goto out_with_fd;
  }

  if (omx__driver_desc->abi_version != OMX_DRIVER_ABI_VERSION) {
    ret = omx__error(omx__driver_desc->abi_version < OMX_DRIVER_ABI_VERSION ? OMX_BAD_KERNEL_ABI : OMX_BAD_LIB_ABI,
		     "Comparing library (ABI 0x%x) with driver (ABI 0x%x)",
		     OMX_DRIVER_ABI_VERSION, omx__driver_desc->abi_version);
    goto out_with_fd;
  }

  if (omx__driver_desc->abi_config != omx_get_abi_config()) {
    ret = omx__error(OMX_BAD_LIB_ABI,
		     "Comparing library (ABI config 0x%x) with driver (ABI config 0x%x)",
		     omx_get_abi_config(), omx__driver_desc->abi_config);
    goto out_with_fd;
  }

  if (omx__driver_desc->mtu != OMX_MTU) {
    ret = omx__error(OMX_BAD_LIB_ABI,
		     "Comparing library (MTU %d) with driver (MTU %d)",
		     OMX_MTU, omx__driver_desc->mtu);
    goto out_with_fd;
  }
  if (omx__driver_desc->medium_frag_length_max != OMX_MEDIUM_FRAG_LENGTH_MAX) {
    ret = omx__error(OMX_BAD_LIB_ABI,
		     "Comparing library (MediumFragMax %d) with driver (MediumFragMax %d)",
		     OMX_MEDIUM_FRAG_LENGTH_MAX, omx__driver_desc->medium_frag_length_max);
    goto out_with_fd;
  }

  /*************************
   * Error Handler Behavior
   */
  omx__globals.fatal_errors = 1;
  env = getenv("OMX_FATAL_ERRORS");
#ifdef OMX_MX_ABI_COMPAT
  if (!env) {
    env = getenv("MX_ERRORS_ARE_FATAL");
    if (env)
      omx__verbose_printf(NULL, "Emulating MX_ERRORS_ARE_FATAL as OMX_FATAL_ERRORS\n");
  }
#endif /* OMX_MX_ABI_COMPAT */
  if (env) {
    omx__globals.fatal_errors = atoi(env);
    omx__verbose_printf(NULL, "Forcing errors to %s\n",
			omx__globals.fatal_errors ? "be fatal" : "not be fatal");
  }

  /***************************
   * Terminate initialization
   */

  omx__init_error_handler();
  omx__globals.initialized = 1;
  return OMX_SUCCESS;

 out_with_fd:
  close(omx__globals.control_fd);
 out_with_message_prefix:
  free(omx__globals.message_prefix);
  return ret;
}

void
omx__init_comms(void)
{
  int debug_signum = SIGUSR1;
  char *env;

  /***************
   * Misc globals
   */

  omx__globals.rndv_threshold = OMX_MEDIUM_MSG_LENGTH_MAX;
  omx__globals.ack_delay_jiffies = omx__ack_delay_jiffies();
  omx__globals.resend_delay_jiffies = omx__resend_delay_jiffies();

  /********************************
   * Endpoint debug initialization
   */

  omx__globals.debug_signal_level = 0;
#ifdef OMX_LIB_DEBUG
  omx__globals.debug_signal_level = 1;
#endif
  env = getenv("OMX_DEBUG_SIGNAL");
  if (env) {
    omx__globals.debug_signal_level =  atoi(env);
    omx__verbose_printf(NULL, "Forcing debugging signal to %s (level %d)\n",
			omx__globals.debug_signal_level?"enabled":"disabled",
			omx__globals.debug_signal_level);
  }
  env = getenv("OMX_DEBUG_SIGNAL_NUM");
  if (env) {
    debug_signum = atoi(env);
    omx__verbose_printf(NULL, "Forcing debugging signal number to %d\n",
			debug_signum);
  }

  if (omx__globals.debug_signal_level)
    omx__debug_init(debug_signum);

  omx__globals.check_request_alloc = 0;
#ifdef OMX_LIB_DEBUG
#  ifdef OMX_DEBUG_REQUESTS
  omx__globals.check_request_alloc = 1;
  omx__verbose_printf(NULL, "Enabling request allocation check by default to level %d\n", omx__globals.check_request_alloc);
#  endif
  env = getenv("OMX_DEBUG_REQUESTS");
  if (env) {
    omx__globals.check_request_alloc = atoi(env);
    omx__verbose_printf(NULL, "Enabling request allocation check level %d\n", omx__globals.check_request_alloc);
  }
#endif

  /**********************************************
   * Shared and self communication configuration
   */

  /* self comm configuration */
#ifndef OMX_DISABLE_SELF
  omx__globals.selfcomms = 1;
  env = getenv("OMX_DISABLE_SELF");
#ifdef OMX_MX_ABI_COMPAT
  if (!env) {
    env = getenv("MX_DISABLE_SELF");
    if (env)
      omx__verbose_printf(NULL, "Emulating MX_DISABLE_SELF as OMX_DISABLE_SELF\n");
  }
#endif /* OMX_MX_ABI_COMPAT */
  if (env) {
    omx__globals.selfcomms = !atoi(env);
    omx__verbose_printf(NULL, "Forcing self comms to %s\n",
			omx__globals.selfcomms ? "enabled" : "disabled");
  }
#endif /* OMX_DISABLE_SELF */

  /* shared comm configuration */
#ifndef OMX_DISABLE_SHARED
  omx__globals.sharedcomms = (omx__driver_desc->features & OMX_DRIVER_FEATURE_SHARED);
  if (!omx__globals.sharedcomms) {
    omx__verbose_printf(NULL, "Shared comms support disabled in the driver\n");
  } else {
    env = getenv("OMX_DISABLE_SHARED");
#ifdef OMX_MX_ABI_COMPAT
    if (!env) {
      env = getenv("MX_DISABLE_SHMEM");
      if (env)
	omx__verbose_printf(NULL, "Emulating MX_DISABLE_SHMEM as OMX_DISABLE_SHARED\n");
    }
#endif /* OMX_MX_ABI_COMPAT */
    if (env) {
      omx__globals.sharedcomms = !atoi(env);
      omx__verbose_printf(NULL, "Forcing shared comms to %s\n",
			  omx__globals.sharedcomms ? "enabled" : "disabled");
    }
  }
#endif /* OMX_DISABLE_SHARED */

  /******************
   * Rndv thresholds
   */

#ifndef OMX_DISABLE_SHARED
  /* must be AFTER sharedcomms init */
  if (omx__globals.sharedcomms) {
    omx__globals.shared_rndv_threshold = 4096;
    env = getenv("OMX_SHARED_RNDV_THRESHOLD");
    if (env) {
      int val = atoi(env);
      if (val < OMX_SMALL_MSG_LENGTH_MAX) {
	omx__verbose_printf(NULL, "Cannot set a rndv threshold to less than %d\n",
			    OMX_SMALL_MSG_LENGTH_MAX);
	val = OMX_SMALL_MSG_LENGTH_MAX;
      }
      if (val > OMX_MEDIUM_MSG_LENGTH_MAX) {
	omx__verbose_printf(NULL, "Cannot set a rndv threshold to more than %d\n",
			    OMX_MEDIUM_MSG_LENGTH_MAX);
	val = OMX_MEDIUM_MSG_LENGTH_MAX;
      }
      omx__globals.shared_rndv_threshold = val;
      omx__verbose_printf(NULL, "Forcing shared rndv threshold to %d\n",
			  omx__globals.shared_rndv_threshold);
    }
  }
#endif /* OMX_DISABLE_SHARED */

  /*******************************
   * Retransmission configuration
   */

  /* resend configuration */
  omx__globals.req_resends_max = 1000;
  env = getenv("OMX_RESENDS_MAX");
#ifdef OMX_MX_ABI_COMPAT
  if (!env) {
    env = getenv("MX_MAX_RETRIES");
    if (env)
      omx__verbose_printf(NULL, "Emulating MX_MAX_RETRIES as OMX_RESENDS_MAX\n");
  }
#endif /* OMX_MX_ABI_COMPAT */
  if (env) {
    omx__globals.req_resends_max = atoi(env);
    omx__verbose_printf(NULL, "Forcing resends max to %ld\n", (unsigned long) omx__globals.req_resends_max);
  }

  /* zombie send configuration */
  omx__globals.zombie_max = OMX_ZOMBIE_MAX_DEFAULT;
  env = getenv("OMX_ZOMBIE_SEND");
#ifdef OMX_MX_ABI_COMPAT
  if (!env) {
    env = getenv("MX_ZOMBIE_SEND");
    if (env)
      omx__verbose_printf(NULL, "Emulating MX_ZOMBIE_SEND as OMX_ZOMBIE_SEND\n");
  }
#endif /* OMX_MX_ABI_COMPAT */
  if (env) {
    omx__globals.zombie_max = atoi(env);
    omx__verbose_printf(NULL, "Forcing zombie max to %d\n",
			omx__globals.zombie_max);
  }

  /* immediate acking threshold */
  omx__globals.not_acked_max = 4;
  env = getenv("OMX_NOTACKED_MAX");
#ifdef OMX_MX_ABI_COMPAT
  if (!env) {
    env = getenv("MX_IMM_ACK");
    if (env)
      omx__verbose_printf(NULL, "Emulating MX_IMM_ACK as OMX_NOTACKED_MAX\n");
  }
#endif /* OMX_MX_ABI_COMPAT */
  if (env) {
    omx__globals.not_acked_max = atoi(env);
    omx__verbose_printf(NULL, "Forcing immediate acking threshold to %d\n",
			omx__globals.not_acked_max);
  }

  /*************************
   * Sleeping configuration
   */

  /* waitspin configuration */
  omx__globals.waitspin = 0;
  env = getenv("OMX_WAITSPIN");
  /* could be enabled by MX_MONOTHREAD */
  if (env) {
    omx__globals.waitspin = atoi(env);
    omx__verbose_printf(NULL, "Forcing waitspin to %s\n",
			omx__globals.waitspin ? "enabled" : "disabled");
  }

  /* interrupted wait configuration */
  omx__globals.waitintr = 0;
  env = getenv("OMX_WAITINTR");
  if (env) {
    omx__globals.waitintr = atoi(env);
    omx__verbose_printf(NULL, "Forcing interrupted wait to %s\n",
			omx__globals.waitintr ? "exit as timeout" : "go back to sleep");
  }

  /* parallel connect configuration */
  omx__globals.connect_pollall = 0;
  env = getenv("OMX_CONNECT_POLLALL");
  if (env) {
    omx__globals.connect_pollall = atoi(env);
    omx__verbose_printf(NULL, "Forcing connect polling all endpoints to %s\n",
			omx__globals.connect_pollall ? "enabled" : "disabled");
  }

  /*************************
   * Regcache configuration
   */
  omx__globals.parallel_regcache = 0;
  env = getenv("OMX_PRCACHE");
  if (env) {
    omx__globals.regcache = atoi(env);
    omx__verbose_printf(NULL, "Forcing parallel regcache to %s\n",
			omx__globals.regcache ? "enabled" : "disabled");
  }

  omx__globals.regcache = omx__globals.parallel_regcache;
  if (omx__driver_desc->features & OMX_DRIVER_FEATURE_PIN_INVALIDATE) {
    omx__globals.regcache = 1;
    omx__verbose_printf(NULL, "Enabling regcache by default since driver reports pin-invalidate support\n");
  }
  env = getenv("OMX_RCACHE");
#ifdef OMX_MX_ABI_COMPAT
  if (!env) {
    env = getenv("MX_RCACHE");
    if (env)
      omx__verbose_printf(NULL, "Emulating MX_RCACHE as OMX_RCACHE\n");
  }
#endif /* OMX_MX_ABI_COMPAT */
  if (env) {
    omx__globals.regcache = atoi(env);
    omx__verbose_printf(NULL, "Forcing regcache to %s\n",
			omx__globals.regcache ? "enabled" : "disabled");
  }

  /******************
   * Process binding
   */
  omx__globals.process_binding = getenv("OMX_PROCESS_BINDING");

  /********************
   * Tune medium frags
   */
  omx__globals.medium_sendq = 1;
  env = getenv("OMX_MEDIUM_SENDQ");
  if (env) {
    omx__globals.medium_sendq = atoi(env);
    omx__verbose_printf(NULL, "Forcing medium sendq to %s\n",
			omx__globals.medium_sendq ? "enabled" : "disabled");
  }

  /*********
   * Ctxids
   */
  omx__globals.ctxid_bits = 0;
  omx__globals.ctxid_shift = 0;
  env = getenv("OMX_CTXIDS");
  if (env) {
    char *env2;
    env2 = strchr(env, ',');
    if (env2) {
      env2++;
      omx__globals.ctxid_bits = atoi(env);
      omx__globals.ctxid_shift = atoi(env2);
      omx__verbose_printf(NULL, "Forcing ctxid bits %d shift %d\n",
			  omx__globals.ctxid_bits, omx__globals.ctxid_shift);
    }
  }

  /****************************
   * OMX_ANY_ENDPOINT behavior
   */
  omx__globals.any_endpoint_id = OMX_ANY_ENDPOINT;
  env = getenv("OMX_ANY_ENDPOINT");
  if (env) {
    omx__globals.any_endpoint_id = atoi(env);
    omx__verbose_printf(NULL, "Forcing OMX_ANY_ENDPOINT to endpoint #%ld\n",
			(unsigned long) omx__globals.any_endpoint_id);
  }
}

/* API omx_finalize */
omx_return_t
omx_finalize(void)
{
  /* FIXME: check that it is initialized */

  /* FIXME: check that no endpoint is still open */

  close(omx__globals.control_fd);
  free(omx__globals.message_prefix);
  omx__globals.initialized = 0;
  return OMX_SUCCESS;
}
