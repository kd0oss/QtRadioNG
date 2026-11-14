# dspserver - the DSP server (WDSP) at the centre of QtRadioNG
#
#   cmake -B build [-DWDSP_DIR=/path/to/wdsp]
#   cmake --build build
#
# WDSP is looked for in WDSP_DIR (default: ./wdsp, where README.txt says to
# put it), then in the system library directories (after "make install" in
# WDSP).  The executable is linked with an rpath to the WDSP it was built
# against, so LD_LIBRARY_PATH is not needed.
#
# Run dspserver from this directory: it keeps its wisdom file and settings in
# the current directory.

cmake_minimum_required(VERSION 3.16)
project(dspserver LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(WDSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/wdsp" CACHE PATH "WDSP source/build directory")

find_library(WDSP_LIBRARY NAMES wdsp HINTS "${WDSP_DIR}" "${WDSP_DIR}/lib")
if(NOT WDSP_LIBRARY)
    message(FATAL_ERROR "libwdsp not found. Build WDSP (see README.txt) and point -DWDSP_DIR at it, or install it.")
endif()
message(STATUS "WDSP: ${WDSP_LIBRARY}")

find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED COMPONENTS C)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DSPSERVER_DEPS REQUIRED IMPORTED_TARGET
    fftw3 samplerate openssl libevent libevent_openssl)

add_executable(dspserver
    audiostream.c
    G711A.c
    util.c
    hardware.c
    server.c
    dsp.c
    wfm.c
    plugin.c
    main.c)

target_include_directories(dspserver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(EXISTS "${WDSP_DIR}")
    target_include_directories(dspserver PRIVATE "${WDSP_DIR}")
endif()
target_compile_options(dspserver PRIVATE -Wall -Wno-unused-function)
if(USE_SSL)
    target_compile_definitions(dspserver PRIVATE USE_SSL)
endif()
target_link_libraries(dspserver PRIVATE
    "${WDSP_LIBRARY}"
    PkgConfig::DSPSERVER_DEPS
    OpenMP::OpenMP_C
    qtr_platform
    Threads::Threads
    avrt
    m)
if(NOT WIN32)
    target_link_libraries(dspserver PRIVATE rt)     # timer_create (Windows: compat/win/qtr_timer.h)
endif()
set_target_properties(dspserver PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)

install(TARGETS dspserver RUNTIME DESTINATION bin)

if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    enable_testing()
    add_executable(test_wfm test/test_wfm.c wfm.c)
    target_include_directories(test_wfm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_wfm PRIVATE m)
    add_test(NAME dspserver_wfm COMMAND test_wfm)
endif()
