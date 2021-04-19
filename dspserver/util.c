/* Copyright 2012 Ethan Blanton */
/**
 * @file util.c
 * @brief Utility functions for dspserver.
 * @author Ethan Blanton, KB8OJH
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#include "util.h"


/**
 * @brief Log a message to stderr with timestamp.
 *
 * Log a message to stderr with timestamp.  The debug level argument is
 * currently ignored, but will be used to control verbosity in the
 * future.
 *
 * @param level severity of the message
 * @param fmt printf-style format string
 */
void sdr_log(enum SDRLogLevel level, char *fmt, ...)
{
    va_list args;
    time_t now;
    struct tm tm;
    char ts[24];
    
    now = time(NULL);
    localtime_r(&now, &tm);
    strftime(ts, sizeof(ts), "[%Y-%m-%d %H:%M:%S] ", &tm);

    /* Ideally we'd do this in one write, but for now two will do. */
    fprintf(stderr, "%s", ts);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
