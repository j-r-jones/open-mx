MAKE = make

ACLOCAL_AMFLAGS = -I ./build-aux

SUBDIRS =

# Driver
if OMX_BUILD_DRIVER
  SUBDIRS += driver/linux
endif

# Library
if OMX_BUILD_LIBRARY
  SUBDIRS += tools
endif


# Public and private API
noinst_HEADERS = common/omx_io.h


# Man pages and other documentations


EXTRA_DIST = AUTHORS ChangeLog COPYING COPYING.GPL COPYING.LGPL \
	     README README.devel REPORTING-BUGS TODO open-mx.spec

