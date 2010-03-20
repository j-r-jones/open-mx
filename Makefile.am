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


noinst_HEADERS = common/omx_io.h
