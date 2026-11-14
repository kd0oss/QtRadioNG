# Platform settings for the terminal programs: include() this from a
# program's CMakeLists.txt and link its targets with qtr_platform.
#
# Linux: the compat directory (qtr_net.h) on the include path.
# Windows (MSYS2 UCRT64 / MINGW64, mingw-w64): also Winsock, the Windows
# version needed for inet_pton() and friends (7 and later), POSIX
# compatibility headers that mingw-w64 does not have (compat/win), and
# winpthreads' thread-safe time functions (localtime_r ...).

if(NOT TARGET qtr_platform)
    get_filename_component(QTR_COMPAT_DIR "${CMAKE_CURRENT_LIST_DIR}/../compat" ABSOLUTE)
    add_library(qtr_platform INTERFACE)
    target_include_directories(qtr_platform INTERFACE "${QTR_COMPAT_DIR}")
    if(WIN32)
        target_include_directories(qtr_platform INTERFACE "${QTR_COMPAT_DIR}/win")
        target_compile_definitions(qtr_platform INTERFACE
            _WIN32_WINNT=0x0601
            WIN32_LEAN_AND_MEAN
            _POSIX_THREAD_SAFE_FUNCTIONS
            _USE_MATH_DEFINES)
        target_link_libraries(qtr_platform INTERFACE ws2_32 iphlpapi winmm)
    endif()
endif()
