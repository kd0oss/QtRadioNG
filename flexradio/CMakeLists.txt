# flexradio - FLEX-6000 / FLEX-8000 / Aurora interface program for dspserver
#
#   cmake -B build
#   cmake --build build
#   ctest --test-dir build         # against the simulated radio (python3 + numpy)
#
# No extra libraries are needed.

cmake_minimum_required(VERSION 3.16)
project(flexradio LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

find_package(Threads REQUIRED)

add_executable(flexradio
    main.c
    server.c
    radio.c
    api.c
    vita.c
    tx.c
    txaudio.c)

target_compile_options(flexradio PRIVATE -Wall -Wextra)
target_link_libraries(flexradio PRIVATE Threads::Threads m)

install(TARGETS flexradio RUNTIME DESTINATION bin)

if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        # uses the fixed SmartSDR and dspserver ports: run alone
        add_test(NAME flexradio_simulator
                 COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/test/test_flexradio.py" $<TARGET_FILE:flexradio>)
        set_tests_properties(flexradio_simulator PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
    endif()
endif()
