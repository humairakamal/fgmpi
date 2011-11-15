## -*- Mode: Makefile; -*-
## vim: set ft=automake :
##
## (C) 2011 by Argonne National Laboratory.
##     See COPYRIGHT in top-level directory.
##

include $(top_srcdir)/src/mpi/attr/Makefile.mk
include $(top_srcdir)/src/mpi/coll/Makefile.mk
include $(top_srcdir)/src/mpi/comm/Makefile.mk
include $(top_srcdir)/src/mpi/datatype/Makefile.mk
include $(top_srcdir)/src/mpi/debugger/Makefile.mk
include $(top_srcdir)/src/mpi/errhan/Makefile.mk
include $(top_srcdir)/src/mpi/group/Makefile.mk
include $(top_srcdir)/src/mpi/init/Makefile.mk
include $(top_srcdir)/src/mpi/misc/Makefile.mk
include $(top_srcdir)/src/mpi/pt2pt/Makefile.mk
include $(top_srcdir)/src/mpi/rma/Makefile.mk
include $(top_srcdir)/src/mpi/spawn/Makefile.mk
include $(top_srcdir)/src/mpi/timer/Makefile.mk
include $(top_srcdir)/src/mpi/topo/Makefile.mk

if BUILD_ROMIO
SUBDIRS += src/mpi/romio
DIST_SUBDIRS += src/mpi/romio
# FIXME is this the right place to put this?
external_libs += src/mpi/romio/libromio.la

# libpromio contains the PMPI symbols (unlike libpmpich, which contains MPI
# symbols) and should be added to libmpich as well
if BUILD_PROFILING_LIB
external_libs += src/mpi/romio/libpromio.la
endif BUILD_PROFILING_LIB

# This was previously a hard copy (not a symlink) performed by config.status
# (specified via AC_CONFIG_COMMANDS).  Ideally we would eliminate this "copy"
# altogether and just set -Iromio_include_dir, but MPE2's build system uses
# $(top_builddir)/bin/mpicc that can't handle more than one include dir.
#
# Using a symlink allows us to avoid trying to capture the full dependency chain
# of MPICH2/mpio.h --> ROMIO/mpio.h --> ROMIO/mpio.h.in --> ROMIO/config.status --> ...MORE_AUTOTOOLS...
BUILT_SOURCES += $(top_builddir)/src/include/mpio.h
$(top_builddir)/src/include/mpio.h: $(top_builddir)/src/mpi/romio/include/mpio.h
	(cd $(top_builddir)/src/include &&        \
	    test ! -e mpio.h &&                   \
	    $(LN_S) ../mpi/romio/include/mpio.h )

DISTCLEANFILES += $(top_builddir)/src/include/mpio.h

endif BUILD_ROMIO

# dir is present but currently intentionally unbuilt
#include $(top_srcdir)/src/mpi/io/Makefile.mk