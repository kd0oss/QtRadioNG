/*
 * qtr_sleep.h - sleep until an absolute CLOCK_MONOTONIC time, portably.
 *
 *   qtr_sleep_until(&deadline)   deadline from clock_gettime(CLOCK_MONOTONIC)
 *
 * The pacing loops (DAX and HackRF transmit, the mock radio) keep a fixed
 * cadence by sleeping to an absolute deadline. On POSIX that is
 * clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME). winpthreads only accepts
 * CLOCK_REALTIME there and returns EINVAL at once for CLOCK_MONOTONIC, so
 * on Windows the loops would spin. Windows also sleeps in 15.6 ms steps by
 * default, coarser than a 5 ms DAX period: a high-resolution waitable timer
 * (Windows 10 1803 and later) is used, else 1 ms timer resolution is
 * requested once with timeBeginPeriod (link winmm).
 */
#ifndef QTR_SLEEP_H
#define QTR_SLEEP_H

#include <time.h>
#include <errno.h>

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

static inline void qtr_sleep_until(const struct timespec *deadline)
{
    struct timespec now;
    long long ns;
    HANDLE t;

    clock_gettime(CLOCK_MONOTONIC, &now);
    ns = (long long)(deadline->tv_sec - now.tv_sec) * 1000000000LL + (deadline->tv_nsec - now.tv_nsec);
    if (ns <= 0)
        return;

    t = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (t != NULL)
    {
        LARGE_INTEGER due;

        due.QuadPart = -(ns / 100);     // relative, in 100 ns units
        if (due.QuadPart == 0)
            due.QuadPart = -1;
        if (SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE))
            WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
    else
    {
        static volatile LONG period_set = 0;

        if (InterlockedExchange(&period_set, 1) == 0)
            timeBeginPeriod(1);
        Sleep((DWORD)((ns + 999999) / 1000000));
    }
}

#else

static inline void qtr_sleep_until(const struct timespec *deadline)
{
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, deadline, NULL) == EINTR)
        ;
}

#endif

#endif
