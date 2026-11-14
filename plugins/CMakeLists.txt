# Protocol plugins: programs that connect to dspserver and demodulate /
# modulate a mode in place of WDSP (see include/plugin_protocol.h).
#
#   cmake -B build && cmake --build build

cmake_minimum_required(VERSION 3.16)
project(qtradio_plugins LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

find_package(Threads REQUIRED)

# the plugin side of the protocol, for every plugin
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/platform.cmake")

add_library(plugin_client STATIC lib/plugin_client.c)
target_include_directories(plugin_client PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib" "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(plugin_client PRIVATE -Wall -Wextra)
target_link_libraries(plugin_client PUBLIC qtr_platform Threads::Threads)

# example: narrow band FM outside WDSP, and a template for new plugins
add_executable(example_plugin example/example.c)
target_compile_options(example_plugin PRIVATE -Wall -Wextra)
target_link_libraries(example_plugin PRIVATE plugin_client m)
install(TARGETS example_plugin RUNTIME DESTINATION bin)

# M17 digital voice: needs libm17 (M17 Project, cmake install) and Codec 2
find_package(libm17 QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CODEC2 QUIET IMPORTED_TARGET codec2)
endif()
if(libm17_FOUND AND CODEC2_FOUND)
    add_executable(m17_plugin m17/m17.c)
    target_compile_options(m17_plugin PRIVATE -Wall -Wextra)
    target_link_libraries(m17_plugin PRIVATE plugin_client libm17::libm17 PkgConfig::CODEC2 m)
    install(TARGETS m17_plugin RUNTIME DESTINATION bin)
else()
    message(WARNING "m17_plugin not built: needs libm17 (https://github.com/M17-Project/libm17) and Codec 2 (libcodec2-dev)")
endif()

# Tests.  test_m17 checks m17_plugin alone against the M17 Project's
# reference m17-coder-sym / m17-decoder-sym (M17_Implementations, SP5WWP)
# and Codec 2's c2enc / c2dec.  The end-to-end tests run a whole system
# (dspserver, hackrf_one_mock, plugin) and so need the top-level build with
# -DHACKRF_ONE_MOCK=ON; they use dspserver's usual ports, and the first
# run spends a few minutes creating WDSP's wisdom file in test-work/.
if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter)
    find_program(M17_CODER m17-coder-sym)
    find_program(M17_DECODER m17-decoder-sym)
    find_program(C2ENC c2enc)
    find_program(C2DEC c2dec)
    set(PLUGIN_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test")
    if(DEFINED E2E_WORK_DIR)
        set(PLUGIN_TEST_WORK "${E2E_WORK_DIR}")          # top-level build: shared with the other end-to-end tests
    else()
        set(PLUGIN_TEST_WORK "${CMAKE_CURRENT_BINARY_DIR}/test-work")
    endif()
    file(MAKE_DIRECTORY "${PLUGIN_TEST_WORK}")

    set(HAVE_M17_TOOLS OFF)
    if(TARGET m17_plugin AND M17_CODER AND M17_DECODER AND C2ENC AND C2DEC)
        set(HAVE_M17_TOOLS ON)
    elseif(TARGET m17_plugin)
        message(STATUS "M17 tests skipped: need m17-coder-sym, m17-decoder-sym, c2enc and c2dec on the PATH")
    endif()

    if(Python3_Interpreter_FOUND AND HAVE_M17_TOOLS)
        add_test(NAME m17_plugin_reference
                 COMMAND Python3::Interpreter "${PLUGIN_TEST_DIR}/test_m17.py"
                         $<TARGET_FILE:m17_plugin> "${M17_CODER}" "${M17_DECODER}")
        set_tests_properties(m17_plugin_reference PROPERTIES TIMEOUT 300)
    endif()

    if(Python3_Interpreter_FOUND AND TARGET dspserver AND TARGET hackrf_one_mock)
        add_test(NAME plugins_e2e_example
                 COMMAND Python3::Interpreter "${PLUGIN_TEST_DIR}/e2e_plugin.py"
                         $<TARGET_FILE:dspserver> $<TARGET_FILE:hackrf_one_mock> $<TARGET_FILE:example_plugin>
                 WORKING_DIRECTORY "${PLUGIN_TEST_WORK}")
        set_tests_properties(plugins_e2e_example PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)
        if(HAVE_M17_TOOLS)
            add_test(NAME plugins_e2e_m17
                     COMMAND Python3::Interpreter "${PLUGIN_TEST_DIR}/e2e_m17.py"
                             $<TARGET_FILE:dspserver> $<TARGET_FILE:hackrf_one_mock> $<TARGET_FILE:m17_plugin>
                             "${M17_CODER}" "${M17_DECODER}"
                     WORKING_DIRECTORY "${PLUGIN_TEST_WORK}")
            set_tests_properties(plugins_e2e_m17 PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)
        endif()
    endif()
endif()
