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
    core_sampler.c
    net_sampler.c
    report.c
    sign.c
)

target_include_directories(app_profile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/ThirdPartyLib/openssl-1.1.1d/include
)
target_link_directories(app_profile PRIVATE
    "${CMAKE_SOURCE_DIR}/ThirdPartyLib/openssl-1.1.1d/lib"
)
target_compile_options(app_profile PRIVATE -Wall -Wextra -O2 -D_GNU_SOURCE)
target_link_libraries(app_profile PRIVATE pthread crypto)
install(TARGETS app_profile RUNTIME DESTINATION /opt/tbox/test)

add_executable(csv_verify csv_verify.c sign.c)
target_include_directories(csv_verify PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ThirdPartyLib/openssl-1.1.1d/include
)
target_link_directories(csv_verify PRIVATE
    "${CMAKE_SOURCE_DIR}/ThirdPartyLib/openssl-1.1.1d/lib"
)
target_compile_options(csv_verify PRIVATE -Wall -Wextra -O2 -D_GNU_SOURCE)
target_link_libraries(csv_verify PRIVATE crypto)
install(TARGETS csv_verify RUNTIME DESTINATION /opt/tbox/test)
