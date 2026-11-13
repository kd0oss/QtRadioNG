/* Copyright (C)
* 2026 - Rick Schnicker KD0OSS
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

/*
 * Transmit side of hackrf_one.
 *
 *  - dspserver sends TX IQ (192 ksps, WDSP Q,I order) as BUFFER packets to
 *    the socket that sent it the sync datagram at attach time.  They go into
 *    a FIFO.
 *  - dspserver's TX thread only runs when "radio mic" packets arrive (hpsdr
 *    sends them with the Hermes mic samples), so a pacing thread sends empty
 *    MIC_BUFFER packets every 5 ms while the transmitter is attached.
 *  - On MOX the HackRF switches to transmit; its USB callback pulls samples
 *    through txchain from the FIFO.  The resampling ratio is trimmed to hold
 *    the FIFO at TX_FIFO_TARGET, since the audio and HackRF clocks differ.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "server.h"
#include "device.h"
#include "txchain.h"
#include "hackrf.h"

#define TX_IN_RATE      192000
#define TX_FIFO_SIZE    TX_IN_RATE           // 1 s
#define TX_FIFO_TARGET  (TX_IN_RATE / 10)    // 100 ms of latency absorbs packet bursts
#define TX_ADJUST_GAIN  0.002                // ratio trim per unit of relative FIFO error
#define MIC_PACE_NS     5000000L             // 200 pacing packets a second

static pthread_mutex_t tx_lock = PTHREAD_MUTEX_INITIALIZER;   // FIFO and chain
static bool        attached = false;
static int8_t      index_tx = -1;
static int         radio = 0;
static struct in_addr dsp_addr;
static int         sock = -1;
static pthread_t   rx_thread_id;
static pthread_t   pace_thread_id;
static atomic_bool threads_stop = false;
static atomic_bool mox = false;

static float      *fifo = NULL;              // complex, I,Q
static long        fifo_head = 0;            // frames written
static long        fifo_tail = 0;            // frames read
static bool        primed = false;
static unsigned long underruns = 0;
static unsigned long overflows = 0;
static unsigned long lost = 0;              // TX IQ packets missing (chunk numbers)
static int          last_chunk = -1;
static TXCHAIN    *chain = NULL;
static long        chain_rate = 0;


/* txchain source: called from the USB callback with tx_lock held. */
static long fifo_source(void *user, float *iq, long max_frames)
{
    long level = fifo_head - fifo_tail;
    long n;

    (void)user;
    if (!primed && level >= TX_FIFO_TARGET)
        primed = true;
    n = primed ? (level < max_frames ? level : max_frames) : 0;
    for (long k = 0; k < n; k++)
    {
        long p = (fifo_tail + k) % TX_FIFO_SIZE;
        iq[k * 2] = fifo[p * 2];
        iq[k * 2 + 1] = fifo[p * 2 + 1];
    }
    fifo_tail += n;
    if (n < max_frames)
    {
        memset(iq + n * 2, 0, sizeof(float) * (size_t)(max_frames - n) * 2);
        if (primed)
        {
            underruns++;
            primed = false;          // refill to the target before sending again
        }
    }
    return max_frames;
} // end fifo_source


/* device TX callback (libusb thread) */
static void tx_fill(void *user, int8_t *buf, size_t n)
{
    (void)user;
    pthread_mutex_lock(&tx_lock);
    if (chain == NULL)
        memset(buf, 0, n * 2);
    else
    {
        double level = (double)(fifo_head - fifo_tail);
        double adjust = primed ? TX_ADJUST_GAIN * (level - TX_FIFO_TARGET) / TX_FIFO_TARGET : 0.0;
        if (adjust > TX_ADJUST_GAIN)
            adjust = TX_ADJUST_GAIN;
        if (adjust < -TX_ADJUST_GAIN)
            adjust = -TX_ADJUST_GAIN;
        txchain_run(chain, buf, n, adjust);
    }
    pthread_mutex_unlock(&tx_lock);
} // end tx_fill


static void *tx_iq_thread(void *arg)
{
    BUFFER buffer;
    struct sockaddr_in dest;
    char sync[2] = {0, 0};
    time_t resync = time(NULL) + 1;

    (void)arg;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr = dsp_addr;
    dest.sin_port = htons(TX_IQ_PORT_0 + server_port_offset());

    while (!atomic_load(&threads_stop))
    {
        // a second sync datagram, as hpsdr does, in case the first was early
        if (resync != 0 && time(NULL) >= resync)
        {
            sendto(sock, sync, sizeof(sync), 0, (struct sockaddr*)&dest, sizeof(dest));
            resync = 0;
        }

        ssize_t n = recv(sock, (char*)&buffer, sizeof(buffer), 0);
        if (n < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                usleep(10000);
            continue;
        }
        if (n < (ssize_t)(8 + 512) || !atomic_load(&mox))
            continue;

        // dspserver numbers the packets of each block 0 .. length/512 - 1
        int chunks = buffer.length >= 512 ? buffer.length / 512 : 1;
        if (last_chunk >= 0 && buffer.chunk != (last_chunk + 1) % chunks)
            lost += (unsigned long)((buffer.chunk - last_chunk - 1 + chunks) % chunks);
        last_chunk = buffer.chunk;

        pthread_mutex_lock(&tx_lock);
        if (fifo_head - fifo_tail > TX_FIFO_SIZE - 32)
            overflows++;
        else
        {
            for (int k = 0; k < 32; k++)          // 512 bytes = 32 complex per packet
            {
                long p = (fifo_head + k) % TX_FIFO_SIZE;
                fifo[p * 2] = (float)buffer.data[k * 2 + 1];      // WDSP order is Q,I
                fifo[p * 2 + 1] = (float)buffer.data[k * 2];
            }
            fifo_head += 32;
        }
        pthread_mutex_unlock(&tx_lock);
    }
    return NULL;
} // end tx_iq_thread


static void *mic_pace_thread(void *arg)
{
    MIC_BUFFER buffer;
    struct sockaddr_in dest;
    struct timespec next;
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    (void)arg;
    if (s < 0)
        return NULL;
    memset(&buffer, 0, sizeof(buffer));
    buffer.radio_id = (int8_t)radio;
    strcpy(buffer.ipaddr, "USB");        // must match <ip_address> in the manifest
    buffer.tx = 0;
    buffer.length = 512 * sizeof(float);
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr = dsp_addr;
    dest.sin_port = htons(MIC_AUDIO_PORT + server_port_offset());

    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!atomic_load(&threads_stop))
    {
        sendto(s, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&dest, sizeof(dest));
        next.tv_nsec += MIC_PACE_NS;
        if (next.tv_nsec >= 1000000000L)
        {
            next.tv_sec++;
            next.tv_nsec -= 1000000000L;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    close(s);
    return NULL;
} // end mic_pace_thread


const char *tx_attach(int8_t index, struct in_addr dsp_address, int radio_id)
{
    struct sockaddr_in dest;
    struct timeval tv = {0, 200000};
    char sync[2] = {0, 0};

    if (attached)
        return OK;
    if (fifo == NULL)
    {
        fifo = malloc(sizeof(float) * TX_FIFO_SIZE * 2);
        if (fifo == NULL)
            return DEVICE_FAILED;
    }
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return DEVICE_FAILED;
    int size = 1 << 21;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    dsp_addr = dsp_address;
    radio = radio_id;
    index_tx = index;

    // dspserver learns where to send TX IQ from this datagram (make_connection)
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr = dsp_addr;
    dest.sin_port = htons(TX_IQ_PORT_0 + server_port_offset());
    sendto(sock, sync, sizeof(sync), 0, (struct sockaddr*)&dest, sizeof(dest));

    atomic_store(&threads_stop, false);
    if (pthread_create(&rx_thread_id, NULL, tx_iq_thread, NULL) != 0)
    {
        close(sock);
        sock = -1;
        return DEVICE_FAILED;
    }
    if (pthread_create(&pace_thread_id, NULL, mic_pace_thread, NULL) != 0)
    {
        atomic_store(&threads_stop, true);
        pthread_join(rx_thread_id, NULL);
        close(sock);
        sock = -1;
        return DEVICE_FAILED;
    }
    attached = true;
    fprintf(stderr, "hackrf_one: transmitter %d attached\n", index);
    return OK;
} // end tx_attach


const char *tx_set_mox(bool on)
{
    if (!attached)
        return TRANSMITTER_NONE;
    if (on == atomic_load(&mox))
        return OK;

    if (on)
    {
        long rate = device_get_hw_rate();

        pthread_mutex_lock(&tx_lock);
        if (chain == NULL || chain_rate != rate)
        {
            txchain_destroy(chain);
            chain = txchain_create(rate, TX_IN_RATE, HACKRF_IF_OFFSET, fifo_source, NULL);
            chain_rate = rate;
        }
        fifo_head = fifo_tail = 0;
        primed = false;
        pthread_mutex_unlock(&tx_lock);
        if (chain == NULL)
            return DEVICE_FAILED;

        atomic_store(&mox, true);
        if (device_set_mox(true, tx_fill, NULL) != 0)
        {
            atomic_store(&mox, false);
            return DEVICE_FAILED;
        }
    }
    else
    {
        atomic_store(&mox, false);
        device_set_mox(false, NULL, NULL);
        if (underruns || overflows || lost)
            fprintf(stderr, "hackrf_one: TX FIFO underruns %lu overflows %lu, I/Q packets lost %lu\n", underruns, overflows, lost);
        underruns = overflows = lost = 0;
        last_chunk = -1;
    }
    return OK;
} // end tx_set_mox


const char *tx_detach(void)
{
    if (!attached)
        return TRANSMITTER_NONE;
    tx_set_mox(false);
    atomic_store(&threads_stop, true);
    pthread_join(rx_thread_id, NULL);
    pthread_join(pace_thread_id, NULL);
    close(sock);
    sock = -1;
    attached = false;
    pthread_mutex_lock(&tx_lock);
    txchain_destroy(chain);
    chain = NULL;
    chain_rate = 0;
    pthread_mutex_unlock(&tx_lock);
    fprintf(stderr, "hackrf_one: transmitter %d detached\n", index_tx);
    index_tx = -1;
    return OK;
} // end tx_detach


bool tx_is_attached(void)
{
    return attached;
} // end tx_is_attached


int8_t tx_index(void)
{
    return index_tx;
} // end tx_index
