# Copyright Joyent, Inc. and other Node contributors. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

CC = $(PREFIX)gcc
AR = $(PREFIX)ar
E=
CFLAGS=--std=gnu89 -g
LINKFLAGS=-lm
TESTS=test/echo-server.c test/test-*.c
BENCHMARKS=test/echo-server.c test/benchmark-*.c

RUNNER_CFLAGS=$(CFLAGS) -D_GNU_SOURCE # Need _GNU_SOURCE for strdup?
RUNNER_LINKFLAGS=$(LINKFLAGS) -pthread
RUNNER_SRC=test/runner-unix.c

oio.a: oio-unix.o ev/ev.o
	$(AR) rcs oio.a oio-unix.o ev/ev.o

oio-unix.o: oio-unix.c oio.h oio-unix.h
	$(CC) $(CFLAGS) -c oio-unix.c -o oio-unix.o

ev/ev.o: ev/config.h ev/ev.c
	$(MAKE) -C ev

ev/config.h:
	cd ev && ./configure

clean-platform:
	$(MAKE) -C ev clean

distclean-platform:
	$(MAKE) -C ev distclean
