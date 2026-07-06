cmake_minimum_required(VERSION 3.10)

set(TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/toolchain/arm-linux-toolchain-linux.cmake")
include(${TOOLCHAIN_FILE})

project(TBox_ResourceTest C)
add_subdirectory(app_profile)
add_subdirectory(stability_monitor)
add_subdirectory(system_stress)
