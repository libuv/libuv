# Toolchain file for bare metal Arm Cortex-M targets. Defaults to the Cortex-M3
# in the MPS2 AN385 board, as emulated by `qemu-system-arm -M mps2-an385`.
# Set the UV_FREERTOS_CPU_FLAGS environment variable to target another CPU.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

if(DEFINED ENV{UV_FREERTOS_CPU_FLAGS})
  set(uv__cpu_flags "$ENV{UV_FREERTOS_CPU_FLAGS}")
else()
  set(uv__cpu_flags "-mcpu=cortex-m3 -mthumb")
endif()

set(CMAKE_C_FLAGS_INIT "${uv__cpu_flags} -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${uv__cpu_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--specs=nano.specs --specs=nosys.specs -Wl,--gc-sections")

# No OS to link test programs against.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
