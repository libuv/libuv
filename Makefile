test/echo-server: test/echo-server.c ol.a
	gcc -o test/echo-server test/echo-server.c ol.a

ol.a: ol-unix.o
	ar rcs ol.a ol-unix.o

ol-unix.o: ol-unix.c ol.h ol-unix.h
	gcc -c ol-unix.c -o ol-unix.o -lm

ev/libev.a: ol-unix.c ol.h ol-unix.h
	gcc -c ol-unix.c -o ol-unix.o -lm

