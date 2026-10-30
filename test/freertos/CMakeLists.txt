# Smoke test for the FreeRTOS port. Not part of the regular libuv build, use:
#
#   cmake -S test/freertos -B build -DFREERTOS_KERNEL_PATH=/path/to/kernel
#
# Builds for the POSIX port by default. Add -DCMAKE_TOOLCHAIN_FILE=<path to
# test/freertos/mps2/arm-none-eabi.cmake> to build for the MPS2 AN385 board.
cmake_minimum_required(VERSION 3.15)
project(uv_freertos_smoke C)

set(FREERTOS_KERNEL_PATH "" CACHE PATH "FreeRTOS-Kernel checkout")
if(NOT FREERTOS_KERNEL_PATH)
  message(FATAL_ERROR "FREERTOS_KERNEL_PATH is not set")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
  set(FREERTOS_PORT "GCC_ARM_CM3" CACHE STRING "FreeRTOS port name")
  set(FREERTOS_HEAP "4" CACHE STRING "FreeRTOS heap model")
else()
  find_package(Threads REQUIRED)
  set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "FreeRTOS port name")
  set(FREERTOS_HEAP "3" CACHE STRING "FreeRTOS heap model")
endif()

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Include the kernel first: libuv auto-detects FreeRTOS from its CMake target.
add_subdirectory(${FREERTOS_KERNEL_PATH} kernel)
add_subdirectory(../.. libuv)

add_executable(app main.c)
target_compile_options(app PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(app PRIVATE freertos_kernel uv_a)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
  target_sources(app PRIVATE mps2/startup.c)
  target_link_options(app PRIVATE -T${CMAKE_CURRENT_SOURCE_DIR}/mps2/mps2.ld)
  set_target_properties(app PROPERTIES
      SUFFIX ".elf"
      LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mps2/mps2.ld)
else()
  target_link_libraries(app PRIVATE Threads::Threads rt)
endif()
