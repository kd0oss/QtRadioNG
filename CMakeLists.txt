# QtRadioNG terminal programs: dspserver and the radio interface programs.
#
#   cmake -B build
#   cmake --build build -j
#   ctest --test-dir build            # unit and simulator tests
#   sudo cmake --install build        # into /usr/local/bin
#
# Each program can also be built on its own from its directory
# (cmake -S dspserver -B build-dspserver).  QtRadio has its own qmake project.

cmake_minimum_required(VERSION 3.16)
project(QtRadioNG_servers LANGUAGES C)

option(BUILD_DSPSERVER  "Build dspserver (needs WDSP)"                        ON)
option(BUILD_HPSDR      "Build hpsdr (Hermes / HPSDR interface)"             ON)
option(BUILD_HACKRF_ONE "Build hackrf_one (skipped if libhackrf is missing)" ON)
option(BUILD_FLEXRADIO  "Build flexradio (FLEX-6000/8000/Aurora interface)"  ON)
option(BUILD_PLUGINS    "Build the protocol plugins (plugins/)"              ON)
option(BUILD_TESTING    "Build the tests and register them with ctest"       ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(BUILD_TESTING)
    enable_testing()
    # where the end-to-end tests run dspserver: it keeps its WDSP wisdom file
    # there, which the first run spends a few minutes creating
    set(E2E_WORK_DIR "${CMAKE_BINARY_DIR}/test-work")
    file(MAKE_DIRECTORY "${E2E_WORK_DIR}")
endif()

if(BUILD_DSPSERVER)
    add_subdirectory(dspserver)
endif()

if(BUILD_HPSDR)
    add_subdirectory(hpsdr)
endif()

if(BUILD_HACKRF_ONE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_LIBHACKRF QUIET libhackrf)
    endif()
    # the sources include <libhackrf/hackrf.h>; pkg-config names .../include/libhackrf
    set(HACKRF_HINTS ${PC_LIBHACKRF_INCLUDE_DIRS})
    list(TRANSFORM HACKRF_HINTS APPEND "/..")
    find_path(HACKRF_INCLUDE_DIR libhackrf/hackrf.h HINTS ${HACKRF_HINTS})
    find_library(HACKRF_LIBRARY NAMES hackrf HINTS ${PC_LIBHACKRF_LIBRARY_DIRS})
    if(HACKRF_INCLUDE_DIR AND HACKRF_LIBRARY)
        add_subdirectory(hackrf_one)
    else()
        message(WARNING "libhackrf not found: hackrf_one is not built (install libhackrf-dev, or -DBUILD_HACKRF_ONE=OFF)")
    endif()
endif()

if(BUILD_FLEXRADIO)
    add_subdirectory(flexradio)
endif()

if(BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

# Two radios on one dspserver (as hpsdr and hackrf_one): needs the HackRF
# simulator (-DHACKRF_ONE_MOCK=ON).  Uses dspserver's usual ports.
if(BUILD_TESTING AND TARGET dspserver AND TARGET hackrf_one_mock)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME dspserver_e2e_two_radios
                 COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/dspserver/test/e2e_radios.py"
                         $<TARGET_FILE:dspserver> $<TARGET_FILE:hackrf_one_mock>
                 WORKING_DIRECTORY "${E2E_WORK_DIR}")
        set_tests_properties(dspserver_e2e_two_radios PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)
    endif()
endif()
