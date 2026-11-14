/*
 * sys/utsname.h stand-in for Windows (mingw-w64 / MSYS2).
 *
 * uname() fills in the fields hpsdr prints at start-up from the computer
 * name and the processor architecture. The version is whatever
 * GetVersionEx gives it; without an application manifest Windows reports 6.2.
 */
#ifndef QTR_WIN_SYS_UTSNAME_H
#define QTR_WIN_SYS_UTSNAME_H

#include <winsock2.h>   /* must precede windows.h */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define QTR_UTSNAME_LEN 65

struct utsname
{
    char sysname[QTR_UTSNAME_LEN];
    char nodename[QTR_UTSNAME_LEN];
    char release[QTR_UTSNAME_LEN];
    char version[QTR_UTSNAME_LEN];
    char machine[QTR_UTSNAME_LEN];
};

static inline int uname(struct utsname *u)
{
    DWORD n = sizeof(u->nodename);
    SYSTEM_INFO si;
    OSVERSIONINFOA vi;

    if (u == NULL)
        return -1;
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname, "Windows");
    if (!GetComputerNameA(u->nodename, &n))
        strcpy(u->nodename, "localhost");

    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (GetVersionExA(&vi))
#pragma GCC diagnostic pop
    {
        snprintf(u->release, sizeof(u->release), "%lu.%lu", vi.dwMajorVersion, vi.dwMinorVersion);
        snprintf(u->version, sizeof(u->version), "build %lu", vi.dwBuildNumber);
    }

    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:
        strcpy(u->machine, "x86_64");
        break;
    case PROCESSOR_ARCHITECTURE_ARM64:
        strcpy(u->machine, "aarch64");
        break;
    case PROCESSOR_ARCHITECTURE_INTEL:
        strcpy(u->machine, "i686");
        break;
    default:
        strcpy(u->machine, "unknown");
        break;
    }
    return 0;
}

#endif
