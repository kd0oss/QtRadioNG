/*
 * qtr_timer.h - periodic timers calling a function on another thread
 *
 * Linux: timer_create() with SIGEV_THREAD.  Windows: a timer-queue timer
 * (CreateTimerQueueTimer), which also calls back on a worker thread.
 *
 *   static QTR_TIMER t;
 *   qtr_timer_start(&t, 20, fn, arg);    // fn(arg) every 20 ms
 *   qtr_timer_stop(&t);                  // as timer_delete(): a call that
 *                                        // has already begun may still finish
 *
 * Starting a running timer restarts it (rather than adding a second one).
 * A QTR_TIMER must stay in place while it runs (a static or global).
 */

#ifndef QTR_TIMER_H
#define QTR_TIMER_H

#include <stdbool.h>
#include <string.h>

typedef void (*qtr_timer_fn)(int arg);

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef struct
{
    HANDLE        h;
    qtr_timer_fn  fn;
    int           arg;
    bool          running;
} QTR_TIMER;

static VOID CALLBACK qtr_timer_tramp(PVOID p, BOOLEAN fired)
{
    QTR_TIMER *t = (QTR_TIMER *)p;
    (void)fired;
    t->fn(t->arg);
}

static inline void qtr_timer_stop(QTR_TIMER *t)
{
    if (!t->running)
        return;
    t->running = false;
    DeleteTimerQueueTimer(NULL, t->h, NULL);
}

static inline int qtr_timer_start(QTR_TIMER *t, int period_ms, qtr_timer_fn fn, int arg)
{
    qtr_timer_stop(t);
    t->fn = fn;
    t->arg = arg;
    if (!CreateTimerQueueTimer(&t->h, NULL, qtr_timer_tramp, t, (DWORD)period_ms, (DWORD)period_ms,
                               WT_EXECUTEDEFAULT))
        return -1;
    t->running = true;
    return 0;
}

#else   // POSIX

#include <signal.h>
#include <time.h>

typedef struct
{
    timer_t       id;
    qtr_timer_fn  fn;
    int           arg;
    bool          running;
} QTR_TIMER;

static void qtr_timer_tramp(union sigval v)
{
    QTR_TIMER *t = (QTR_TIMER *)v.sival_ptr;
    t->fn(t->arg);
}

static inline void qtr_timer_stop(QTR_TIMER *t)
{
    if (!t->running)
        return;
    t->running = false;
    timer_delete(t->id);
}

static inline int qtr_timer_start(QTR_TIMER *t, int period_ms, qtr_timer_fn fn, int arg)
{
    struct sigevent sev;
    struct itimerspec value;

    qtr_timer_stop(t);
    t->fn = fn;
    t->arg = arg;
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = qtr_timer_tramp;
    sev.sigev_value.sival_ptr = t;
    if (timer_create(CLOCK_MONOTONIC, &sev, &t->id) != 0)
        return -1;
    value.it_value.tv_sec = period_ms / 1000;
    value.it_value.tv_nsec = (long)(period_ms % 1000) * 1000000L;
    value.it_interval = value.it_value;
    timer_settime(t->id, 0, &value, NULL);
    t->running = true;
    return 0;
}

#endif

#endif // QTR_TIMER_H
