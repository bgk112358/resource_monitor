# ── GTest unit tests for proc_reader + app_profile samplers ─────────
#
# Build:
#   cmake -B gtest/build -S gtest
#   cmake --build gtest/build
#   ctest --test-dir gtest/build -V
#
cmake_minimum_required(VERSION 3.10)

enable_language(CXX)

# ── GTest from thread_monitor reference ──────────────────────────────
set(GTEST_ROOT "/home/ubuntu/Documents/cursor/thread_monitor/gtest")
list(APPEND CMAKE_PREFIX_PATH "${GTEST_ROOT}/install")
find_package(GTest REQUIRED QUIET)

find_package(Threads REQUIRED)

# ── app_profile source root ──────────────────────────────────────────
set(APP_PROFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../resource_test/src/app_profile")

# -Wno-unused-result: suppress fgets() return-value warnings from C code
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-result")

# ── test_proc_reader ─────────────────────────────────────────────────
add_executable(test_proc_reader
    src/test_proc_reader.cpp
)

target_include_directories(test_proc_reader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GTEST_INCLUDE_DIRS}
)

target_link_libraries(test_proc_reader PRIVATE
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

set_target_properties(test_proc_reader PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
add_test(NAME test_proc_reader COMMAND test_proc_reader)

# ── test_app_profile ─────────────────────────────────────────────────
# Links against the actual app_profile C modules, testing real API functions.
add_executable(test_app_profile
    src/test_app_profile.cpp
    ${APP_PROFILE_DIR}/proc_reader.c
    ${APP_PROFILE_DIR}/cpu_sampler.c
    ${APP_PROFILE_DIR}/mem_sampler.c
    ${APP_PROFILE_DIR}/thread_fd_sampler.c
    ${APP_PROFILE_DIR}/io_sampler.c
    ${APP_PROFILE_DIR}/report.c
)

target_include_directories(test_app_profile PRIVATE
    ${APP_PROFILE_DIR}              # sampler.h
    ${GTEST_INCLUDE_DIRS}
)

target_link_libraries(test_app_profile PRIVATE
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

set_target_properties(test_app_profile PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
add_test(NAME test_app_profile COMMAND test_app_profile)

# ── test_app_profile_v2 — enhanced assertions ────────────────────────
add_executable(test_app_profile_v2
    src/test_app_profile_v2.cpp
    ${APP_PROFILE_DIR}/proc_reader.c
    ${APP_PROFILE_DIR}/cpu_sampler.c
    ${APP_PROFILE_DIR}/mem_sampler.c
    ${APP_PROFILE_DIR}/thread_fd_sampler.c
    ${APP_PROFILE_DIR}/io_sampler.c
    ${APP_PROFILE_DIR}/report.c
)

target_include_directories(test_app_profile_v2 PRIVATE
    ${APP_PROFILE_DIR}
    ${GTEST_INCLUDE_DIRS}
)

target_link_libraries(test_app_profile_v2 PRIVATE
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

target_compile_options(test_app_profile_v2 PRIVATE -Wno-unused-result)

set_target_properties(test_app_profile_v2 PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
add_test(NAME test_app_profile_v2 COMMAND test_app_profile_v2)

# ── Coverage target ───────────────────────────────────────────────────
# usage: cmake --build build --target coverage
add_custom_target(coverage
    COMMAND lcov --directory ${CMAKE_BINARY_DIR} --capture --output-file ${CMAKE_BINARY_DIR}/coverage_raw.info
    COMMAND lcov --remove ${CMAKE_BINARY_DIR}/coverage_raw.info '/usr/*' '*/test_*' '*/gtest/*' --output-file ${CMAKE_BINARY_DIR}/coverage.info
    COMMAND lcov --extract ${CMAKE_BINARY_DIR}/coverage.info '*/resource_test/src/*' --output-file ${CMAKE_BINARY_DIR}/coverage_filtered.info
    COMMAND genhtml ${CMAKE_BINARY_DIR}/coverage_filtered.info --output-directory ${CMAKE_CURRENT_SOURCE_DIR}/report/html --title "TBox Resource Test Coverage"
    COMMAND lcov --list ${CMAKE_BINARY_DIR}/coverage_filtered.info
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating coverage report..."
)
