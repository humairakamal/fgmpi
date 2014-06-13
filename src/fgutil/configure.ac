AC_INIT([fgutil], [1.0], [kamal@cs.ubc.ca])
AC_CONFIG_MACRO_DIR([confdb])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

AC_PROG_CC
AM_PROG_AR

LT_PREREQ([2.2.6])

# Bug in libtool adds -O2 and -g by default
save_cflags=$CFLAGS
LT_INIT()
CFLAGS=$save_cflags

AC_CONFIG_HEADERS([config.h])
#AC_CONFIG_SUBDIRS([pcl-1.10]) 
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
