AM_CPPFLAGS = -I$(top_srcdir)/../include -I$(top_builddir)/../include -I$(srcdir)/util -I$(srcdir)/stack -I$(srcdir)/hash -I$(srcdir)/../pcl-1.10/include

lib_LTLIBRARIES = libfgmpi.la
libfgmpi_la_SOURCES = util/clock.c util/config.c util/debug.c util/linked_list.c \
		      util/object_pool.c util/plhash.c util/timing.c \
		      util/debug.h util/linked_list.h util/object_pool.h  util/plhash.h  \
	              util/timing.h util/util.h util/clock.h util/utilconfig.h \
                      stack/stack.c stack/stacklink.h \
                      hash/cokusmt.c  hash/hashlib.c  hash/hashmap.c \
		      hash/cokusmt.h  hash/hashlib.h  hash/hashmap.h \
                      threadlib.c sched_global_rr.c


libfgmpi_la_LIBADD = ../pcl-1.10/pcl/libpcl.la
libfgmpi_la_LDFLAGS = -version-info 1:0:0 -L../pcl-1.10/pcl/
AM_LIBS = -lpcl

include_HEADERS =  threadlib_internal.h fgmpi.h $(srcdir)/util/util.h $(srcdir)/hash/hashlib.h  $(srcdir)/hash/hashmap.h
