# hackrf_one - HackRF One interface program for dspserver
#
#   cmake -B build [-DHACKRF_ONE_MOCK=ON]
#   cmake --build build
#   ctest --test-dir build         # receive/transmit chain tests (python3 + numpy)
#
# Needs libhackrf and libsamplerate.  HACKRF_ONE_MOCK also builds
# hackrf_one_mock, which simulates a HackRF (no hardware needed).

cmake_minimum_required(VERSION 3.16)
project(hackrf_one LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(HACKRF_ONE_MOCK "Also build hackrf_one_mock (simulated HackRF)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

find_package(Threads REQUIRED)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/platform.cmake")
find_package(PkgConfig REQUIRED)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)

# libhackrf: pkg-config only as a hint (some libhackrf.pc files list a libusb
# include directory that is not installed, which an imported target rejects)
pkg_check_modules(PC_LIBHACKRF QUIET libhackrf)
# the sources include <libhackrf/hackrf.h>; pkg-config names .../include/libhackrf
set(HACKRF_HINTS ${PC_LIBHACKRF_INCLUDE_DIRS})
list(TRANSFORM HACKRF_HINTS APPEND "/..")
find_path(HACKRF_INCLUDE_DIR libhackrf/hackrf.h HINTS ${HACKRF_HINTS})
find_library(HACKRF_LIBRARY NAMES hackrf HINTS ${PC_LIBHACKRF_LIBRARY_DIRS})
if(NOT HACKRF_INCLUDE_DIR OR NOT HACKRF_LIBRARY)
    message(FATAL_ERROR "libhackrf not found (install libhackrf-dev)")
endif()

set(HACKRF_ONE_SOURCES main.c server.c tx.c device.c rxchain.c txchain.c)
set(HACKRF_ONE_WARNINGS -Wall -Wextra)

add_executable(hackrf_one ${HACKRF_ONE_SOURCES})
target_compile_options(hackrf_one PRIVATE ${HACKRF_ONE_WARNINGS})
target_include_directories(hackrf_one PRIVATE "${HACKRF_INCLUDE_DIR}")
target_link_libraries(hackrf_one PRIVATE "${HACKRF_LIBRARY}" PkgConfig::SAMPLERATE qtr_platform Threads::Threads m)
install(TARGETS hackrf_one RUNTIME DESTINATION bin)

if(HACKRF_ONE_MOCK)
    # the mock provides the libhackrf API itself: headers only
    add_executable(hackrf_one_mock ${HACKRF_ONE_SOURCES} test/mock_hackrf.c)
    target_compile_options(hackrf_one_mock PRIVATE ${HACKRF_ONE_WARNINGS})
    target_include_directories(hackrf_one_mock PRIVATE "${HACKRF_INCLUDE_DIR}")
    target_link_libraries(hackrf_one_mock PRIVATE PkgConfig::SAMPLERATE qtr_platform Threads::Threads m)
endif()

if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    enable_testing()

    add_executable(test_rxchain test/test_rxchain.c rxchain.c)
    target_include_directories(test_rxchain PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(test_rxchain PRIVATE ${HACKRF_ONE_WARNINGS})
    target_link_libraries(test_rxchain PRIVATE PkgConfig::SAMPLERATE qtr_platform Threads::Threads m)

    add_executable(test_txchain test/test_txchain.c txchain.c rxchain.c)
    target_include_directories(test_txchain PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(test_txchain PRIVATE ${HACKRF_ONE_WARNINGS})
    target_link_libraries(test_txchain PRIVATE PkgConfig::SAMPLERATE qtr_platform Threads::Threads m)

    add_test(NAME hackrf_one_rxchain COMMAND test_rxchain)
    add_test(NAME hackrf_one_txchain COMMAND test_txchain)
    # test_txchain writes /tmp/txchain_*.raw; the Python check analyses them
    set_tests_properties(hackrf_one_txchain PROPERTIES FIXTURES_SETUP hackrf_txchain_files)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME hackrf_one_txchain_spectrum
                 COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/test/test_txchain.py")
        set_tests_properties(hackrf_one_txchain_spectrum PROPERTIES FIXTURES_REQUIRED hackrf_txchain_files)
    endif()
endif()
