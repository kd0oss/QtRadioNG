/* Copyright 2012 Ethan Blanton */
/**
 * @file util.h
 * @brief Definitions of utility functions for dspserver.
 * @author Ethan Blanton, KB8OJH
 */
#ifndef _UTIL_H_
#define _UTIL_H_

#include <pthread.h>

/**
 * @brief Severity of debug message.
 *
 * Values in this enum indicate the severity of a debug message, in
 * increasing order from top (lesser values) to bottom (greater values).
 * When verbosity is implemented, more severe levels will be logged at
 * lower verbosity levels.
 */
enum SDRLogLevel {
    SDR_LOG_INFO,
    SDR_LOG_WARNING,
    SDR_LOG_ERROR
};

/**
 * @defgroup threads Thread debugging facilities
 *
 * This group contains structures and functions useful for debugging
 * thread interactions.
 */

/**
 * @brief Thread tracking structure for synchronization-less structures.
 * @ingroup threads
 *
 * This structure is used to ensure that synchronization-less global
 * data is always accessed by the same thread.  To use the
 * synchronization assertions, globally allocate an instance of this
 * structure with a static initializer of SDR_THREAD_ID, like:
 *
 * struct sdr_thread_id tid = SDR_THREAD_ID;
 *
 * The members of this structure should not be modified by any code
 * other than the sdr thread debugging code.
 *
 * @see SDR_THREAD_ID
 * @see sdr_thread_assert_id()
 */
struct sdr_thread_id {
    volatile int initialized;
    pthread_mutex_t lock;
    pthread_t id;
};

/**
 * @brief Static initializer for sdr_thread_id
 * @ingroup threads
 *
 * @see struct sdr_thread_id
 */
#define SDR_THREAD_ID { 0, PTHREAD_MUTEX_INITIALIZER }

void sdr_log(enum SDRLogLevel level, char *fmt, ...);

void sdr__thread_assert_id(struct sdr_thread_id *tid, char *file, int line);

#define sdr_thread_assert_id(tid)

#endif /* _UTIL_H_ */
