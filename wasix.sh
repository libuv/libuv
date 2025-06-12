#! /bin/bash

set -euxo pipefail

rm -rf build
mkdir build
cd build

export WASIXCC_SYSROOT=/home/arshia/repos/wasmer/wasix-libc/sysroot32-ehpic/
export WASIXCC_WASM_EXCEPTIONS=yes
export WASIXCC_PIC=yes

WASIXCC_RUN_WASM_OPT=no \
  cmake .. -DCMAKE_TOOLCHAIN_FILE=wasix-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \

make uv_a -j16