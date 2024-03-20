#!/bin/bash -eu
# Supply build instructions
# Use the following environment variables to build the code
# $CXX:               c++ compiler
# $CC:                c compiler
# CFLAGS:             compiler flags for C files
# CXXFLAGS:           compiler flags for CPP files
# LIB_FUZZING_ENGINE: linker flag for fuzzing harnesses

# When run multiple times locally leftover files may be present in shared
# OUT folder. Clear these in case.
rm -f $OUT/test_file*

sh autogen.sh
./configure
make
make install
find . -name "*.a"

# Copy all fuzzer executables to $OUT/
$CC $CFLAGS $LIB_FUZZING_ENGINE \
  $SRC/libuv/.clusterfuzzlite/libuv_fuzzer.c \
  -o $OUT/libuv_fuzzer \
  -I$SRC/libuv/include \
  $SRC/libuv/.libs/libuv.a
