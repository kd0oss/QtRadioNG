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
 * Transmit side of flexradio.
 *
 *  - dspserver sends TX IQ (192 ksps, WDSP Q,I order) as BUFFER packets to
 *    the socket that sent it the sync datagram at attach time.  txaudio turns
 *    them back into 24 kHz audio, which goes into a FIFO.
 *  - dspserver's TX thread only runs when "radio mic" packets arrive, so a
 *    pacing thread sends MIC_BUFFER packets every 5 ms; they also carry the
 *    radio's forward and reflected power for QtRadio's meter.
 *  - While MOX is on, a sender thread takes 128 samples from the FIFO every
 *    5.33 ms and sends them to the radio as a DAX TX packet.  The radio's
 *    clock and QtRadio's microphone clock differ, so the FIFO is held near
 *    TX_FIFO_TARGET by dropping samples when it runs long and sending
 *    silence when it runs dry.
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
#include "radio.h"
#include "vita.h"
#include "tx.h"
#include "txaudio.h"

#define TX_FIFO_SIZE    TXA_OUT_RATE             // 1 s
#define TX_FIFO_TARGET  (TXA_OUT_RATE / 20)      // 50 ms
#define TX_FIFO_HIGH    (TX_FIFO_TARGET * 4)     // trim back to the target above this
#define DAX_SAMPLES     128                      // stereo samples per DAX TX packet
#define DAX_PERIOD_NS   (1000000000L * DAX_SAMPLES / TXA_OUT_RATE)
#define MIC_PACE_NS     5000000L                 // 200 pacing packets a second

static pthread_mutex_t tx_lock = PTHREAD_MUTEX_INITIALIZER;   // FIFO and audio chain
static bool        attached = false;
static int8_t      index_tx = -1;
static int         radio = 0;
static char        mic_ipaddr[16];
static struct in_addr dsp_addr;
static int         sock = -1;
static pthread_t   rx_thread_id;
static pthread_t   pace_thread_id;
static pthread_t   send_thread_id;
static atomic_bool threads_stop = false;
static atomic_bool mox = false;

static TXAUDIO     chain;
static TXA_KIND    kind = TXA_SSB;
static float       fifo[TX_FIFO_SIZE];
static long        fifo_head = 0;            // samples written
static long        fifo_tail = 0;            // samples read
static bool        primed = false;
static unsigned long underruns = 0;
static unsigned long trims = 0;
static unsigned long dax_packets = 0;


static void *tx_iq_thread(void *arg)
{
    BUFFER buffer;
    struct sockaddr_in dest;
    char sync[2] = {0, 0};
    time_t resync = time(NULL) + 1;
    float iq[64];
    float audio[8];

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

        for (int k = 0; k < 32; k++)          // 512 bytes = 32 complex per packet
        {
            iq[k * 2] = (float)buffer.data[k * 2 + 1];      // WDSP order is Q,I
            iq[k * 2 + 1] = (float)buffer.data[k * 2];
        }
        pthread_mutex_lock(&tx_lock);
        int na = txaudio_run(&chain, iq, 32, audio);
        for (int k = 0; k < na; k++)
        {
            if (fifo_head - fifo_tail >= TX_FIFO_SIZE)
                fifo_tail++;                   // full: lose the oldest
            fifo[fifo_head % TX_FIFO_SIZE] = audio[k];
            fifo_head++;
        }
        pthread_mutex_unlock(&tx_lock);
    }
    return NULL;
} // end tx_iq_thread


static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
} // end put_be32


/* VITA-49 IF data packet with stream id and class id, 24 kHz float32 stereo,
 * big endian: what the radio takes on a dax_tx stream. */
static size_t build_dax_packet(uint8_t *pkt, uint32_t stream, int count, const float *audio)
{
    const int words = 7 + DAX_SAMPLES * 2;
    uint32_t header = (1u << 28) | (1u << 27) | (3u << 22) | (1u << 20) |
                      ((uint32_t)(count & 0xF) << 16) | (uint32_t)words;

    put_be32(pkt, header);
    put_be32(pkt + 4, stream);
    put_be32(pkt + 8, VITA_FLEX_OUI);
    put_be32(pkt + 12, ((uint32_t)VITA_INFO_CLASS << 16) | VITA_PCC_DAX_AUDIO);
    put_be32(pkt + 16, 0);                    // integer timestamp
    put_be32(pkt + 20, 0);                    // fractional timestamp
    put_be32(pkt + 24, 0);
    for (int k = 0; k < DAX_SAMPLES; k++)
    {
        uint32_t u;
        memcpy(&u, &audio[k], sizeof(u));
        put_be32(pkt + 28 + k * 8, u);        // left
        put_be32(pkt + 28 + k * 8 + 4, u);    // right
    }
    return (size_t)words * 4;
} // end build_dax_packet


static void *dax_send_thread(void *arg)
{
    uint8_t pkt[28 + DAX_SAMPLES * 8];
    float audio[DAX_SAMPLES];
    struct timespec next;
    int count = 0;
    bool was_mox = false;

    (void)arg;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!atomic_load(&threads_stop))
    {
        bool on = atomic_load(&mox);
        uint32_t stream = radio_tx_stream();

        if (on && !was_mox)
            clock_gettime(CLOCK_MONOTONIC, &next);    // start the cadence now
        was_mox = on;
        if (on && stream != 0)
        {
            pthread_mutex_lock(&tx_lock);
            long level = fifo_head - fifo_tail;
            if (!primed && level >= TX_FIFO_TARGET)
                primed = true;
            if (level > TX_FIFO_HIGH)
            {
                fifo_tail = fifo_head - TX_FIFO_TARGET;
                level = TX_FIFO_TARGET;
                trims++;
            }
            if (primed && level >= DAX_SAMPLES)
            {
                for (int k = 0; k < DAX_SAMPLES; k++)
                    audio[k] = fifo[(fifo_tail + k) % TX_FIFO_SIZE];
                fifo_tail += DAX_SAMPLES;
            }
            else
            {
                memset(audio, 0, sizeof(audio));
                if (primed)
                {
                    underruns++;
                    primed = false;          // refill to the target first
                }
            }
            pthread_mutex_unlock(&tx_lock);

            size_t len = build_dax_packet(pkt, stream, count, audio);
            count = (count + 1) & 0xF;
            if (radio_send_dax_tx(pkt, len) == 0)
                dax_packets++;
        }
        next.tv_nsec += DAX_PERIOD_NS;
        if (next.tv_nsec >= 1000000000L)
        {
            next.tv_sec++;
            next.tv_nsec -= 1000000000L;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
} // end dax_send_thread


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
    memcpy(buffer.ipaddr, mic_ipaddr, sizeof(buffer.ipaddr));   // must match <ip_address> in the manifest
    buffer.tx = 0;
    buffer.length = 512 * sizeof(float);
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr = dsp_addr;
    dest.sin_port = htons(MIC_AUDIO_PORT + server_port_offset());

    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!atomic_load(&threads_stop))
    {
        radio_power(&buffer.fwd_pwr, &buffer.rev_pwr);
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


const char *tx_attach(int8_t index, struct in_addr dsp_address, int radio_id, const char *ipaddr)
{
    struct sockaddr_in dest;
    struct timeval tv = {0, 200000};
    char sync[2] = {0, 0};
    int size = 1 << 21;

    if (attached)
        return OK;
    if (index != 1 && index != 0)
        return INVALID_COMMAND;
    if (radio_tx_open() != 0)
        return RADIO_FAILED;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return RADIO_FAILED;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    dsp_addr = dsp_address;
    radio = radio_id;
    index_tx = index;
    memset(mic_ipaddr, 0, sizeof(mic_ipaddr));
    snprintf(mic_ipaddr, sizeof(mic_ipaddr), "%s", ipaddr);
    pthread_mutex_lock(&tx_lock);
    txaudio_init(&chain, kind);
    fifo_head = fifo_tail = 0;
    primed = false;
    pthread_mutex_unlock(&tx_lock);

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
        return RADIO_FAILED;
    }
    if (pthread_create(&pace_thread_id, NULL, mic_pace_thread, NULL) != 0)
    {
        atomic_store(&threads_stop, true);
        pthread_join(rx_thread_id, NULL);
        close(sock);
        sock = -1;
        return RADIO_FAILED;
    }
    if (pthread_create(&send_thread_id, NULL, dax_send_thread, NULL) != 0)
    {
        atomic_store(&threads_stop, true);
        pthread_join(rx_thread_id, NULL);
        pthread_join(pace_thread_id, NULL);
        close(sock);
        sock = -1;
        return RADIO_FAILED;
    }
    attached = true;
    fprintf(stderr, "flexradio: transmitter %d attached\n", index);
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
        pthread_mutex_lock(&tx_lock);
        txaudio_set_kind(&chain, kind);
        fifo_head = fifo_tail = 0;
        primed = false;
        pthread_mutex_unlock(&tx_lock);
        atomic_store(&mox, true);
        if (radio_set_mox(true) != 0)
        {
            atomic_store(&mox, false);
            return RADIO_FAILED;
        }
    }
    else
    {
        atomic_store(&mox, false);
        radio_set_mox(false);
        if (underruns || trims)
            fprintf(stderr, "flexradio: TX audio underruns %lu trims %lu\n", underruns, trims);
        underruns = trims = 0;
    }
    return OK;
} // end tx_set_mox


const char *tx_set_mode(const char *qtradio_mode)
{
    const char *flex;
    TXA_KIND k;

    if (txaudio_mode(qtradio_mode, &flex, &k) != 0)
        fprintf(stderr, "flexradio: %s cannot be transmitted, sending silence\n", qtradio_mode);
    pthread_mutex_lock(&tx_lock);
    kind = k;
    txaudio_set_kind(&chain, k);
    pthread_mutex_unlock(&tx_lock);
    return radio_set_mode(flex) == 0 ? OK : RADIO_FAILED;
} // end tx_set_mode


const char *tx_detach(void)
{
    if (!attached)
        return TRANSMITTER_NONE;
    tx_set_mox(false);
    atomic_store(&threads_stop, true);
    pthread_join(rx_thread_id, NULL);
    pthread_join(pace_thread_id, NULL);
    pthread_join(send_thread_id, NULL);
    close(sock);
    sock = -1;
    attached = false;
    radio_tx_close();
    fprintf(stderr, "flexradio: transmitter %d detached (%lu DAX TX packets sent)\n", index_tx, dax_packets);
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
