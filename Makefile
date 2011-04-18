CFLAGS=-ansi -g -Wall
LINKFLAGS=-g -lm
all: oio.a test/runner

TESTS=test/echo-server.c \
			test/test-pass-always.c \
			test/test-fail-always.c \
			test/test-ping-pong.c \
			test/test-callback-stack.c \
			test/test-timeout.c

test/runner: test/*.h test/runner.c test/runner-unix.c $(TESTS) oio.a
	$(CC) $(CFLAGS) $(LINKFLAGS) -o test/runner  test/runner.c test/runner-unix.c $(TESTS) oio.a

oio.a: oio-unix.o ev/ev.o
	$(AR) rcs oio.a oio-unix.o ev/ev.o

oio-unix.o: oio-unix.c oio.h oio-unix.h
	$(CC) $(CFLAGS) -c oio-unix.c -o oio-unix.o

test/echo.o: test/echo.c test/echo.h
	$(CC) $(CFLAGS) -c test/echo.c -o test/echo.o

ev/ev.o: ev/config.h ev/ev.c
	$(MAKE) -C ev

ev/config.h:
	cd ev && ./configure


.PHONY: clean distclean test

test: test/runner
	test/runner

clean:
	$(RM) -f *.o *.a test/runner
	$(MAKE) -C ev clean

distclean:
	$(RM) -f *.o *.a
	$(MAKE) -C ev clean
