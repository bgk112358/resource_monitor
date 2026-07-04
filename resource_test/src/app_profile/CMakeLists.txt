cmake_minimum_required(VERSION 3.10)
project(app_profile C)
set(CMAKE_C_STANDARD 11)

add_executable(app_profile
    main.c
    proc_reader.c
    cpu_sampler.c
    mem_sampler.c
    thread_fd_sampler.c
    io_sampler.c
    report.c
)

target_include_directories(app_profile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(app_profile PRIVATE -Wall -Wextra -O2 -D_GNU_SOURCE)
target_link_libraries(app_profile PRIVATE pthread)

install(TARGETS app_profile RUNTIME DESTINATION /opt/tbox/test)
