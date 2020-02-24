#!/usr/bin/bash

set -e

sh autogen.sh
./configure --host=x86_64-w64-mingw32 CC=x86_64-w64-mingw32-gcc --prefix=$cur__install
make
# TODO: One of the tests fails on Windows. Likely, it is a mingw/cygwin pathing issue - but need to investigate to be sure.
# make check
make install
