RM_F		= rm -f

OPEN_MX_DIR	= $(abs_builddir)

installmoddir = $(prefix)/modules/$(OMX_LINUX_RELEASE)

nodist_installmod_DATA = open-mx.ko


noinst_HEADERS	= omx_endpoints.h omx_hal.h

EXTRA_DIST	= check_kernel_headers.sh				\
		  omx_main.c omx_endpoints.c

# Mark open-mx.ko as .PHONY so that the rule is always re-executed
# and let Kbuild handle dependencies.
.PHONY: open-mx.ko

# Kbuild may fail during make install as a different user since it tries to create
# some files in .tmp_versions even when there is nothing to do. This is especially
# a problem when doing make install as root over NFS without root squash.
# Ignore these errors when the current user is not the one that ran configure.
# So make install as root does not fail, and we still do not ignore build failures
# as the normal user.
open-mx.ko:
	+$(MAKE) -C $(OMX_LINUX_BUILD) M=$(OPEN_MX_DIR) $(OMX_KBUILD_ARGS) || test x`id -u` != x`stat -c %u Kbuild`

clean-local:
	+$(MAKE) -C $(OMX_LINUX_BUILD) M=$(OPEN_MX_DIR) clean

distclean-local:
	$(RM_F) omx_checks.h
	$(RM_F) Module.markers
	$(RM_F) modules.order
