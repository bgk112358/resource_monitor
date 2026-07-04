# TBox ARM Cortex-A53 cross-compilation toolchain
# 用法: cmake .. -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ── 编译器 (根据实际工具链路径调整) ──
set(CROSS_PREFIX arm-linux-gnueabihf-)

set(CMAKE_C_COMPILER   ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ── 目标平台编译标志 ──
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard" CACHE STRING "" FORCE)
