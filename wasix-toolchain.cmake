# Cmake toolchain description file for the Makefile for WASI
cmake_minimum_required(VERSION 3.5.0)

set(CMAKE_SYSTEM_NAME WASI) # Generic for now, to not trigger a Warning
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(CMAKE_C_COMPILER_ID Clang)
set(triple wasm32-wasmer-wasi)
set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_CXX_COMPILER_TARGET ${triple})
set(CMAKE_ASM_COMPILER_TARGET ${triple})
set(CMAKE_${lang}_COMPILE_OPTIONS_SYSROOT "--sysroot=$ENV{WASIXCC_SYSROOT}")

set(CMAKE_C_COMPILER wasixcc)
set(CMAKE_CXX_COMPILER wasix++)
set(CMAKE_LINKER wasixld)
set(CMAKE_AR wasixar)
set(CMAKE_RANLIB wasixranlib)

# Don't look in the sysroot for executables to run during the build
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Only look in the sysroot (not in the host paths) for the rest
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
