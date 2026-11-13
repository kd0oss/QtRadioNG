# hpsdr - Hermes / HPSDR (protocol 1 and 2) interface program for dspserver
#
#   cmake -B build
#   cmake --build build

cmake_minimum_required(VERSION 3.16)
project(hpsdr LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

add_executable(hpsdr
    discovered.c
    discovery.c
    new_discovery.c
    new_protocol.c
    old_discovery.c
    old_protocol.c
    radio.c
    receiver.c
    server.c
    transmitter.c
    main.c)

target_compile_options(hpsdr PRIVATE -Wall)
target_link_libraries(hpsdr PRIVATE Threads::Threads m)

install(TARGETS hpsdr RUNTIME DESTINATION bin)
