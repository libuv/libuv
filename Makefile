test/echo-server: test/echo-server.c ol.a
	$(CC) -g -o test/echo-server test/echo-server.c ol.a -lm

ol.a: ol-unix.o ev/ev.o
	$(AR) rcs ol.a ol-unix.o ev/ev.o

ol-unix.o: ol-unix.c ol.h ol-unix.h
	$(CC) -g -c ol-unix.c -o ol-unix.o -lm

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
