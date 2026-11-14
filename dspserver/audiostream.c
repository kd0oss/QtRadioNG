/**
* @file audiostream.c
* @brief audio out put stream (for iPhone)
* @author John Melton, G0ORX/N6LYT, Doxygen Comments Dave Larsen, KV0S
* @version 0.1
* @date 2009-03-10
*/

/* Copyright (C)
* 2009 - John Melton, G0ORX/N6LYT, Doxygen Comments Dave Larsen, KV0S
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/

/* Copyright (C) - modifications of the original program by John Melton
* 2011 - Alex Lee, 9V1Al
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - S
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "qtr_net.h"
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#ifdef _OPENMP
#include <omp.h>
#endif


#include "audiostream.h"
#include "server.h"     // MAX_RFSTREAMS
#include "buffer.h"
#include "util.h"



/**
 * @brief Client audio stream configuration.
 *
 * This structure is configured by the client to declare the audio
 * stream characteristics it wants to receive.  The configuration is
 * protected by audiostream_sem.  Each time an audio buffer is
 * allocated, the iq_thread consults this configuration and makes a
 * copy.  The audio stream configuration in iq_thread is then unchanged
 * until the buffer is disposed (by either passing it to the client, or
 * discarding it), at which point a new configuration is read.  This
 * prevents iq_thread from having to take the lock at each sample.
 *
 * If, at disposal, the audio stream configuration has changed since the
 * current buffer was allocated, the current buffer id discarded rather
 * than giving the client audio it does not expect. The default
 * configuration is for a buffer containing 50ms of audio, so this means
 * that audio stream configuration changes will cause 50ms of audio to
 * be lost.  If the client makes its buffers deeper or changes the
 * sample rate, this buffer may represent a longer or shorter period of
 * time.  If there is a desire to make it substantially longer, this
 * scheme will have to be reconsidered.
 *
 * The specific members of this structure are discussed in the
 * definition of struct audiostream_config.
 *
 * @see iq_thread
 * @see audiostream_sem
 * @see struct audiostream_config
 */
struct audiostream_config audiostream_conf = { AUDIO_BUFFER_SIZE, 48000, 1, 0 };

/**
 * @brief Protects audiostream_conf.
 *
 * @see audiostream_conf
 */
sem_t audiostream_sem;

/**
 * @brief Audio stream configuration used during sample processing.
 *
 * This instance must be accessed only by iq_thread.  The initial
 * definition must match audiostream_conf exactly!
 */
/* Audio being put together for the clients, one buffer per receive stream:
 * each stream's IQ thread fills its own (several run at once with several
 * receivers or radios).  conf is the stream's copy of audiostream_conf,
 * refreshed when a new buffer is started and the configuration changed. */
typedef struct _stream_audio
{
    unsigned char             *buf;
    int                        insert;
    struct audiostream_config  conf;
    unsigned long              gen;
} STREAM_AUDIO;

static STREAM_AUDIO   stream_audio[MAX_RFSTREAMS];
static unsigned long  conf_gen = 1;      // changes of audiostream_conf seen so far

static unsigned char encodetable[65536];

unsigned char alaw(short sample);

void init_alaw_tables();


/* --------------------------------------------------------------------------*/
/**
* @brief put samples to the audio stream
*
* @return
*/

static void allocate_audio_buffer(int8_t index)
{
    STREAM_AUDIO *sa = &stream_audio[index];
    int samplesize;

    sem_wait(&audiostream_sem);
    if (audiostream_conf.age > 0)
    {
        /* The stream configuration has changed: every stream takes the new
         * one with its next buffer. */
        conf_gen++;
        audiostream_conf.age = 0;
    }
    if (sa->gen != conf_gen)
    {
        memcpy(&sa->conf, &audiostream_conf, sizeof(sa->conf));
        sa->gen = conf_gen;
    }
    sem_post(&audiostream_sem);

    /* From this point until the buffer allocated here is handed to the
     * client or discarded, this stream works on sa->conf, NOT
     * audiostream_conf. */
    switch (sa->conf.encoding)
    {
    case ENCODING_ALAW:
        /* 8 bits per sample */
        samplesize = sa->conf.bufsize * sa->conf.channels;
        break;
    case ENCODING_PCM:
        /* 16 bit per sample */
        samplesize = sa->conf.bufsize * sa->conf.channels * 2;
        break;
    default:
        /* No ENCODING_ALAW2! */
        samplesize = 0;
    }
    sa->buf = malloc(samplesize + AUDIO_BUFFER_HEADER_SIZE);
    sa->insert = 0;
} // end allocate_audio_buffer


void audio_stream_reset()
{
    /* FIXME: This isn't atomic */
    //audio_stream_queue_free();
    Mic_stream_queue_free();
} // end audio_stream_reset


/**
 * @brief Process a single stereo audio sample of a receive stream.
 *
 * Called only by that stream's IQ thread: each stream has its own buffer.
 */
void audio_stream_put_samples(int8_t index, short left_sample, short right_sample)
{
    STREAM_AUDIO *sa;
    unsigned char *audio_buffer;
    int audio_buffer_length;

    if (index < 0 || index >= MAX_RFSTREAMS)
        return;
    sa = &stream_audio[index];
    if (sa->buf == NULL)
    {
        allocate_audio_buffer(index);
        if (sa->buf == NULL)
            return;
    }
    audio_buffer = sa->buf;

    // samples are delivered at 48K or 8K depending on audiostream_conf.samplerate

    int offset;
    // use this sample and convert to a-law or PCM
    if (sa->conf.channels == 1)
    {
        switch (sa->conf.encoding)
        {
        default: /* ALAW */
        case ENCODING_ALAW:
            offset = sa->insert + AUDIO_BUFFER_HEADER_SIZE;
            audio_buffer[offset] = alaw((left_sample + right_sample) / 2);
            break;
        case ENCODING_PCM:
            offset = sa->insert * 2 + AUDIO_BUFFER_HEADER_SIZE;
            /* PCM on the wire is always LE? */
            audio_buffer[offset] = (left_sample/2 + right_sample/2) & 0xff;
            audio_buffer[offset + 1] = (left_sample/2 + right_sample/2) >> 8;
            break;
        }
    } else {
        switch (sa->conf.encoding)
        {
        default: /* ALAW */
        case ENCODING_ALAW:
            offset = sa->insert * 2 + AUDIO_BUFFER_HEADER_SIZE;
            audio_buffer[offset] = alaw(left_sample);
            audio_buffer[offset + 1] = alaw(right_sample);
            break;
        case ENCODING_PCM:
            offset = sa->insert * 4 + AUDIO_BUFFER_HEADER_SIZE;
            audio_buffer[offset] = left_sample & 0x00ff;
            audio_buffer[offset + 1] = left_sample >> 8;
            audio_buffer[offset + 2] = right_sample & 0x00ff;
            audio_buffer[offset + 3] = right_sample >> 8;
            break;
        }
    }

    sa->insert++;

    if (sa->insert == sa->conf.bufsize)
    {
        audio_buffer[0] = AUDIO_BUFFER;
        audio_buffer_length = sa->conf.bufsize * sa->conf.channels;
        if (sa->conf.encoding == ENCODING_PCM)
            audio_buffer_length *= 2;
        audio_buffer[1] = HEADER_VERSION;
        audio_buffer[2] = index;
        /* FIXME: htons */
   //     fprintf(stderr, "Audio len: %d\n", audio_buffer_length);
        audio_buffer[3] = (audio_buffer_length >> 8) & 0xFF;
        audio_buffer[4] = audio_buffer_length & 0xFF;
        audio_stream_queue_add(audio_buffer, audio_buffer_length + AUDIO_BUFFER_HEADER_SIZE);
        free(audio_buffer);    // audio_stream_queue_add copies it
        sa->buf = NULL;
        sa->insert = 0;
    }
} // end audio_stream_put_samples


unsigned char alaw(short sample)
{
    return encodetable[sample & 0xFFFF];
} // end alaw


void init_alaw_tables()
{
    int i;

#pragma omp parallel for schedule(static) private(i)
    for (i=0; i<65536; i++)
    {
        short sample = (short)i;

        int sign = (sample & 0x8000) >> 8;
        if (sign != 0)
        {
            sample = (short)-sample;
            sign = 0x80;
        }

        if (sample > 32635) sample = 32635;

        int exp = 7;
        int expMask;
        for (expMask = 0x4000; (sample & expMask) == 0 && exp > 0; exp--, expMask >>= 1)
        {
        }
        int mantis = (sample >> ((exp == 0) ? 4 : (exp + 3))) & 0x0f;
        unsigned char alaw = (unsigned char)(sign | exp << 4 | mantis);
        encodetable[i] = (unsigned char)(alaw ^ 0xD5);
    }
} // end init_alaw_tables
