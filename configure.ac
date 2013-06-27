# Copyright (c) 2013, Ben Noordhuis <info@bnoordhuis.nl>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

AC_PREREQ(2.57)
AC_INIT([libuv], [0.11.5], [https://github.com/joyent/libuv/issues])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AM_SILENT_RULES([yes])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST
AC_ENABLE_SHARED
AC_ENABLE_STATIC
AC_PROG_CC
AM_PROG_AR
LT_INIT
# TODO(bnoordhuis) Check for -pthread vs. -pthreads
AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_LIB([kstat], [kstat_lookup])
AC_CHECK_LIB([kvm], [kvm_open])
AC_CHECK_LIB([nsl], [gethostbyname])
AC_CHECK_LIB([perfstat], [perfstat_cpu])
AC_CHECK_LIB([pthread], [pthread_mutex_init])
AC_CHECK_LIB([rt], [clock_gettime])
AC_CHECK_LIB([sendfile], [sendfile])
AC_CHECK_LIB([socket], [socket])
AC_SYS_LARGEFILE
AM_CONDITIONAL([AIX],     [AS_CASE([$host_os], [aix*],     [true], [false])])
AM_CONDITIONAL([DARWIN],  [AS_CASE([$host_os], [darwin*],  [true], [false])])
AM_CONDITIONAL([FREEBSD], [AS_CASE([$host_os], [freebsd*], [true], [false])])
AM_CONDITIONAL([LINUX],   [AS_CASE([$host_os], [linux*],   [true], [false])])
AM_CONDITIONAL([NETBSD],  [AS_CASE([$host_os], [netbsd*],  [true], [false])])
AM_CONDITIONAL([OPENBSD], [AS_CASE([$host_os], [openbsd*], [true], [false])])
AM_CONDITIONAL([SUNOS],   [AS_CASE([$host_os], [solaris*], [true], [false])])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
