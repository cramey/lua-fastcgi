m4_define([LF_MAJOR], 1)
m4_define([LF_MINOR], 0)
m4_define([LF_PATCH], 0)
m4_define([LF_BUGS], [])

AC_PREREQ([2.64])
AC_INIT([lua-fastcgi], [LF_MAJOR.LF_MINOR.LF_PATCH], [LF_BUGS])
AC_CONFIG_SRCDIR([src/lua-fastcgi.c])

AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AM_CONFIG_HEADER([config.h])

AC_LANG([C])
AC_PROG_CC

AC_CHECK_HEADERS([ctype.h errno.h fcgiapp.h fcgi_config.h fcntl.h inttypes.h])
AC_CHECK_HEADERS([lua5.1/lauxlib.h lua5.1/lua.h lua5.1/lualib.h])
AC_CHECK_HEADERS([pthread.h stdint.h stdio.h stdlib.h string.h sys/mman.h])
AC_CHECK_HEADERS([sys/resource.h sys/stat.h sys/time.h time.h unistd.h])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
