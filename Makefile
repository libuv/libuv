all: test/echo-demo test/test-ping-pong

test/echo-demo: test/echo-demo.c test/echo.o ol.a
	$(CC) -ansi -g -o test/echo-demo test/echo-demo.c test/echo.o ol.a -lm

test/test-ping-pong: test/test-ping-pong.c test/echo.o ol.a
	$(CC) -ansi -g -o test/test-ping-pong test/test-ping-pong.c test/echo.o ol.a -lm

ol.a: ol-unix.o ev/ev.o
	$(AR) rcs ol.a ol-unix.o ev/ev.o

ol-unix.o: ol-unix.c ol.h ol-unix.h
	$(CC) -ansi -g -c ol-unix.c -o ol-unix.o

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
