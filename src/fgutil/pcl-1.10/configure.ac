dnl Process this file with autoconf to produce a configure script.
AC_INIT([libpcl], [1.10])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AM_CONFIG_HEADER(config.h)
AC_CONFIG_MACRO_DIR([confdb])

dnl Checks for programs.
AC_PROG_MAKE_SET
AC_PROG_CC
AM_PROG_AR

LT_PREREQ([2.2.6])

# Bug in libtool adds -O2 and -g by default
save_cflags=$CFLAGS
LT_INIT()
CFLAGS=$save_cflags

AC_PROG_INSTALL
AC_PROG_LIBTOOL

dnl Checks for libraries.

dnl Checks for header files.
AC_STDC_HEADERS

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_C_VOLATILE
AC_C_CONST
AC_C_BIGENDIAN

dnl Checks for library functions.
AC_CHECK_FUNCS(memset malloc free)
AC_CHECK_FUNCS(makecontext getcontext swapcontext)
AC_CHECK_FUNCS(sigaction)
AC_CHECK_FUNCS(longjmp setjmp)
AC_CHECK_FUNCS(sigaltstack)


AC_OUTPUT(Makefile pcl/Makefile test/Makefile man/Makefile include/Makefile)

