cmake_minimum_required(VERSION 3.10)
project(DummyApp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── 交叉编译选项 (TBox: ARM Cortex-A53, Linux) ──
# 用法: cmake .. -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake
# 或设置: cmake .. -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER gcc)
endif()

# ── 源文件 ──
add_executable(dummy_app dummy_app.c)

# ── 编译选项 ──
target_compile_options(dummy_app PRIVATE
    -Wall -Wextra -O2
    -D_GNU_SOURCE
    -D_POSIX_C_SOURCE=200809L
)

# ── 链接选项 ──
target_link_libraries(dummy_app PRIVATE
    pthread     # POSIX 线程
    m           # 数学库 (sin/cos)
)

# ── 安装 (可选) ──
install(TARGETS dummy_app RUNTIME DESTINATION /opt/tbox/bin)

# ── 编译信息 ──
message(STATUS "Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Target:     ARM Cortex-A53 (TBox)")
