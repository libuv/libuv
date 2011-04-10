all: oio.a

test/echo-demo: test/echo-demo.c test/echo.o oio.a
	$(CC) -ansi -g -o test/echo-demo test/echo-demo.c test/echo.o oio.a -lm

test/test-ping-pong: test/test-ping-pong.c test/echo.o oio.a
	$(CC) -ansi -g -o test/test-ping-pong test/test-ping-pong.c test/echo.o oio.a -lm

oio.a: oio-unix.o ev/ev.o
	$(AR) rcs oio.a oio-unix.o ev/ev.o

oio-unix.o: oio-unix.c oio.h oio-unix.h
	$(CC) -ansi -g -c oio-unix.c -o oio-unix.o

test/echo.o: test/echo.c test/echo.h
	$(CC) -ansi -g -c test/echo.c -o test/echo.o

ev/ev.o: ev/config.h ev/ev.c
	$(MAKE) -C ev

ev/config.h:
	cd ev && ./configure


.PHONY: clean distclean

clean:
	$(RM) -f *.o *.a
	$(MAKE) -C ev clean

distclean:
	$(RM) -f *.o *.a
	$(MAKE) -C ev clean
