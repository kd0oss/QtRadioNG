/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil */
/**
* @file server.c
* @brief client network interface
* @author John Melton, G0ORX/N6LYT, Doxygen Comments Dave Larsen, KV0S
* @version 0.1
* @date 2009-04-12
*/
// server.c

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
* Foundation, Inc., 59 Temple Pl
*/

/* Modifications made by Rick Schnicker KD0OSS 2020  */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <sys/timeb.h>
/* For fcntl */
#include <fcntl.h>

#include <samplerate.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "server.h"
#include "hardware.h"
#include "audiostream.h"
#include "main.h"
#include "wdsp.h"
#include "buffer.h"
#include "G711A.h"
#include "util.h"

RADIO *radio[5];
CHANNEL channels[35];

short int active_channels = 0;
short int current_channel = -1;
short int connected_radios;
bool      wideband_enabled = false;

extern float txfwd;
extern float txref;


static int timing = 0;
static int current_rx = -1;
static int current_tx = -1;

static long sample_rate = 48000L;

static pthread_t mic_audio_client_thread_id,
                 client_thread_id,
                 audio_client_thread_id,
                 spectrum_client_thread_id,
                 wideband_client_thread_id,
                 tx_thread_id;

#define BASE_PORT 8000
static int port = BASE_PORT;

#define BASE_PORT_SSL 9000
static int port_ssl = BASE_PORT_SSL;

// This must match the size declared in WDSP
#define SAMPLE_BUFFER_SIZE 4096
static float spectrumBuffer[SAMPLE_BUFFER_SIZE];
float *widebandBuffer = NULL;
static int zoom = 0;
static int low, high;            // filter low/high

#define TX_BUFFER_SIZE 512 //1024

// bits_per_frame is now a variable
#undef BITS_SIZE
#define BITS_SIZE   ((bits_per_frame + 7) / 8)

#define MIC_NO_OF_FRAMES 4
#define MIC_BUFFER_SIZE  (BITS_SIZE*MIC_NO_OF_FRAMES)
#define MIC_ALAW_BUFFER_SIZE 512 //58

#define true  1
#define false 0

#if MIC_BUFFER_SIZE > MIC_ALAW_BUFFER_SIZE
static unsigned char mic_buffer[MIC_BUFFER_SIZE];
#else
static unsigned char mic_buffer[MIC_ALAW_BUFFER_SIZE];
#endif

// For timer based spectrum data (instead of sending one spectrum frame per getspectrum command from clients)
#define SPECTRUM_TIMER_NS (20*1000000L)
static timer_t spectrum_timerid;
static unsigned long spectrum_timestamp = 0;

static timer_t wideband_timerid;
static unsigned long wideband_timestamp = 0;

int data_in_counter = 0;
int iq_buffer_counter = 0;

#define MSG_SIZE 64
#define MIC_MSG_SIZE 518 //64


// Mic_audio_stream is the HEAD of a queue for encoded Mic audio samples from QtRadio
TAILQ_HEAD(, audio_entry) Mic_audio;

// Mic_audio client list
TAILQ_HEAD(, _client_entry) Mic_audio_client_list;

// Client_list is the HEAD of a queue of connected clients
TAILQ_HEAD(, _client_entry) Client_list;

// Spectrum_client_list is the HEAD of a queue of connected spectrum clients
TAILQ_HEAD(, _client_entry) Spectrum_client_list;

// Wideband_client_list is the HEAD of a queue of connected spectrum clients
TAILQ_HEAD(, _client_entry) Wideband_client_list;

// Audio_client_list is the HEAD of a queue of connected receive audio clients
TAILQ_HEAD(, _client_entry) Audio_client_list;

static float meter;
int encoding = 0;

static int send_audio = 0;

static sem_t audio_bufevent_semaphore,
             spec_bufevent_semaphore,
             wb_bufevent_semaphore,
             wideband_semaphore,
             bufferevent_semaphore,
             mic_semaphore,
             spectrum_semaphore;

void* client_thread(void* arg);
void* spectrum_client_thread(void* arg);
void* audio_client_thread(void* arg);
void* tx_thread(void* arg);
void* mic_audio_client_thread(void* arg);
void* wideband_client_thread(void* arg);

void client_set_samples(char *client_samples, float* samples,int size);
void client_set_wb_samples(char *client_samples, float* samples,int size);

void do_accept(evutil_socket_t listener, short event, void *arg);
void readcb(struct bufferevent *bev, void *ctx);
void writecb(struct bufferevent *bev, void *ctx);

void do_accept_spectrum(evutil_socket_t listener, short event, void *arg);
void spectrum_readcb(struct bufferevent *bev, void *ctx);
void spectrum_writecb(struct bufferevent *bev, void *ctx);

void do_accept_audio(evutil_socket_t listener, short event, void *arg);
void audio_readcb(struct bufferevent *bev, void *ctx);
void audio_writecb(struct bufferevent *bev, void *ctx);

void do_accept_mic_audio(evutil_socket_t listener, short event, void *arg);
void mic_audio_readcb(struct bufferevent *bev, void *ctx);
void mic_audio_writecb(struct bufferevent *bev, void *ctx);

void do_accept_wideband(evutil_socket_t listener, short event, void *arg);
void wideband_readcb(struct bufferevent *bev, void *ctx);
void wideband_writecb(struct bufferevent *bev, void *ctx);

void spectrum_timer_handler(union sigval);
void wideband_timer_handler(union sigval);


float getFilterSizeCalibrationOffset()
{
    int size = 1024; // dspBufferSize
    float i = log10((float)size);
    return 3.0f*(11.0f-i);
} // end getFilterSizeCalibrationOffset


void audio_stream_init(int receiver)
{
    sem_init(&audiostream_sem, 0, 1);
    init_alaw_tables();
} //end audio_stream_init


void init_analyzer(int disp, int length)
{
    multimeterCalibrationOffset = -41.0f;
    displayCalibrationOffset = -48.0f;

    initAnalyzer(disp, 120, 15, length);
} // end init_analyzer


void audio_stream_queue_add(unsigned char *buffer, int length)
{
    client_entry *client_item;

    sem_wait(&audio_bufevent_semaphore);
    //    if (send_audio)
    {
        TAILQ_FOREACH(client_item, &Audio_client_list, entries)
        {
            bufferevent_write(client_item->bev, buffer, length);
        }
    }
    //    else free(buffer);
    sem_post(&audio_bufevent_semaphore);
} // end audio_stream_queue_add


struct _client_entry *audio_stream_queue_remove()
{
    return NULL;

    struct _client_entry *first_item;
    sem_wait(&audio_bufevent_semaphore);
    first_item = TAILQ_FIRST(&Audio_client_list);
    if (first_item != NULL)
        TAILQ_REMOVE(&Audio_client_list, first_item, entries);
    sem_post(&audio_bufevent_semaphore);
    return first_item;
} // end audio_stream_queue_remove


// this is run from the client thread
void Mic_stream_queue_add()
{
    unsigned char *bits;
    struct audio_entry *item;

    if (audiostream_conf.micEncoding == MIC_ENCODING_ALAW)
    {
        bits = malloc(MIC_ALAW_BUFFER_SIZE);
        memcpy(bits, mic_buffer, MIC_ALAW_BUFFER_SIZE);
        item = malloc(sizeof(*item));
        item->buf = bits;
        item->length = MIC_ALAW_BUFFER_SIZE;
        sem_wait(&mic_semaphore);
        TAILQ_INSERT_TAIL(&Mic_audio, item, entries);
        sem_post(&mic_semaphore);
    }
} // end Mic_stream_queue_add


void Mic_stream_queue_free()
{
    struct audio_entry *item;

    sem_wait(&mic_semaphore);
    while ((item = TAILQ_FIRST(&Mic_audio)) != NULL)
    {
        TAILQ_REMOVE(&Mic_audio, item, entries);
        free(item->buf);
        free(item);
    }
    sem_post(&mic_semaphore);
} // end Mic_stream_queue_free


void calculate_display_average(int disp)
{
    double display_avb;
    int display_average;

    double t = 0.001 * 170.0f;

    display_avb = exp(-1.0 / ((double)15 * t));
    display_average = fmax(2, (int)fmin(60, (double)15 * t));
    SetDisplayAvBackmult(disp, 0, display_avb);
    SetDisplayNumAverage(disp, 0, display_average);
    fprintf(stderr, "Disp: %d  avb: %f   avg: %d\n", disp, display_avb, display_average);
} // end calculate_display_average


void server_init(int receiver)
{
    int rc;

    panadapterMode = PANADAPTER;  // KD0OSS
    numSamples = 1000; // KD0OSS
    bUseNB = false;
    bUseNB2 = false;
    //  rxMeterMode = AVG_SIGNAL_STRENGTH; // KD0OSS
    //  txMeterMode = PWR; // KD0OSS

    evthread_use_pthreads();

    // initialize WDSP
    char wisdom_directory[1024];
    char *c = getcwd(wisdom_directory, sizeof(wisdom_directory));
    strcpy(&wisdom_directory[strlen(wisdom_directory)], "/");
    WDSPwisdom(c);
    printf("Wisdom set.\n");
    fprintf(stderr, "WSDP Version: %d\n", GetWDSPVersion());

    TAILQ_INIT(&Client_list);
    TAILQ_INIT(&Spectrum_client_list);
    TAILQ_INIT(&Audio_client_list);
    TAILQ_INIT(&Mic_audio_client_list);
    TAILQ_INIT(&Mic_audio);
    TAILQ_INIT(&Wideband_client_list);

    sem_init(&bufferevent_semaphore, 0, 1);
    sem_init(&spec_bufevent_semaphore, 0, 1);
    sem_init(&audio_bufevent_semaphore, 0, 1);
    sem_init(&mic_semaphore, 0, 1);
    sem_init(&spectrum_semaphore, 0, 1);
    sem_init(&wideband_semaphore, 0, 1);
    sem_init(&wb_bufevent_semaphore, 0, 1);

    signal(SIGPIPE, SIG_IGN);

    spectrum_timer_init();
    wideband_timer_init();

    port = BASE_PORT;
    port_ssl = BASE_PORT_SSL;

    rc = pthread_create(&client_thread_id, NULL, client_thread, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create failed on client_thread: rc=%d\n", rc);
    }
    else
        rc = pthread_detach(client_thread_id);

    rc = pthread_create(&audio_client_thread_id, NULL, audio_client_thread, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create failed on audioclient_thread: rc=%d\n", rc);
    }
    else
        rc = pthread_detach(audio_client_thread_id);

    rc = pthread_create(&spectrum_client_thread_id, NULL, spectrum_client_thread, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create failed on spectrum_client_thread: rc=%d\n", rc);
    }
    else
        rc = pthread_detach(spectrum_client_thread_id);

    rc = pthread_create(&mic_audio_client_thread_id, NULL, mic_audio_client_thread, NULL);
    if (rc != 0)
        fprintf(stderr, "pthread_create failed on mic_audio_client_thread: rc=%d\n", rc);
    else
        rc = pthread_detach(mic_audio_client_thread_id);

    rc = pthread_create(&wideband_client_thread_id, NULL, wideband_client_thread, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create failed on wideband_client_thread: rc=%d\n", rc);
    }
    else
        rc = pthread_detach(wideband_client_thread_id);
} // end server_init


void tx_init(void)
{
    int rc;

    rc = pthread_create(&tx_thread_id, NULL, tx_thread, NULL);
    if (rc != 0)
        fprintf(stderr, "pthread_create failed on tx_thread: rc=%d\n", rc);
    else
        rc = pthread_detach(tx_thread_id);
} // end tx_init


void spectrum_timer_init(void)
{
    struct itimerspec value;
    struct sigevent sev;

    value.it_value.tv_sec = 0;
    value.it_value.tv_nsec = SPECTRUM_TIMER_NS;

    value.it_interval.tv_sec = 0;
    value.it_interval.tv_nsec = SPECTRUM_TIMER_NS;

    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = spectrum_timer_handler;
    sev.sigev_notify_attributes = NULL;

    timer_create(CLOCK_REALTIME, &sev, &spectrum_timerid);
    timer_settime(spectrum_timerid, 0, &value, NULL);
} // end spectrum_timer_init


void spectrum_timer_handler(union sigval usv)
{            // this is called every 20 ms
    int flag = 0;
    client_entry *item;

    if (current_rx == -1) return;

    sem_wait(&spec_bufevent_semaphore);
    item = TAILQ_FIRST(&Spectrum_client_list);
    sem_post(&spec_bufevent_semaphore);
    if (item == NULL)
    {
        GetPixels(current_rx, 0, spectrumBuffer, &flag);
        return;               // no clients
    }
    sem_wait(&spectrum_semaphore);

    if (mox && current_tx > -1)
    {
        GetPixels(current_tx, 0, spectrumBuffer, &flag);
        meter = (float)GetTXAMeter(current_tx, TXA_ALC_PK);
    }
    else
    {
        switch (panadapterMode) // KD0OSS
        {
        case PANADAPTER:
        case SPECTRUM:
            GetPixels(current_rx, 0, spectrumBuffer, &flag);
            break;

        case SCOPE2:
        case SCOPE:
        case PHASE:
            RXAGetaSipF1(current_rx, spectrumBuffer, numSamples);
            break;

        default:
            GetPixels(current_rx, 0, spectrumBuffer, &flag);
        }
        meter = (float)GetRXAMeter(current_rx, RXA_S_AV); // + multimeterCalibrationOffset + getFilterSizeCalibrationOffset();
    }
    sem_post(&spectrum_semaphore);
    if (!flag) return;

    sem_wait(&spec_bufevent_semaphore);
    TAILQ_FOREACH(item, &Spectrum_client_list, entries)
    {
        sem_post(&spec_bufevent_semaphore);
        if (item->fps > 0)
        {
            if (item->frame_counter-- <= 1)
            {
             //   fprintf(stderr, "here\n");
                char *client_samples = malloc(sizeof(spectrum)+item->samples);
                sem_wait(&spectrum_semaphore);
                client_set_samples(client_samples, spectrumBuffer, item->samples);
                sem_post(&spectrum_semaphore);
                bufferevent_write(item->bev, client_samples, sizeof(spectrum)+item->samples);
                free(client_samples);
                item->frame_counter = (item->fps == 0) ? 50 : 50 / item->fps;
            }
        }
        sem_wait(&spec_bufevent_semaphore);
    }
    sem_post(&spec_bufevent_semaphore);

    spectrum_timestamp++;
} // end spectrum_timer_handler


void wideband_timer_init(void)
{
    struct itimerspec value;
    struct sigevent sev;

    value.it_value.tv_sec = 0;
    value.it_value.tv_nsec = SPECTRUM_TIMER_NS;

    value.it_interval.tv_sec = 0;
    value.it_interval.tv_nsec = SPECTRUM_TIMER_NS;

    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = wideband_timer_handler;
    sev.sigev_notify_attributes = NULL;

    timer_create(CLOCK_REALTIME, &sev, &wideband_timerid);
    timer_settime(wideband_timerid, 0, &value, NULL);
} // end wideband_timer_init


void wideband_timer_handler(union sigval usv)
{            // this is called every 20 ms
    int flag = 0;
    client_entry *item;

    if (current_rx == -1 || !wideband_enabled) return;

    sem_wait(&wb_bufevent_semaphore);
    item = TAILQ_FIRST(&Wideband_client_list);
    sem_post(&wb_bufevent_semaphore);
    if (item == NULL)
    {
        GetPixels(WIDEBAND_CHANNEL, 0, widebandBuffer, &flag);
        return;               // no clients
    }

    sem_wait(&wideband_semaphore);
    GetPixels(WIDEBAND_CHANNEL, 0, widebandBuffer, &flag);
    sem_post(&wideband_semaphore);
    if (!flag) return;

    sem_wait(&wb_bufevent_semaphore);
    TAILQ_FOREACH(item, &Wideband_client_list, entries)
    {
        sem_post(&wb_bufevent_semaphore);
        if (item->fps > 0)
        {
            if (item->wb_frame_counter-- <= 1)
            {
                char *client_samples = malloc(sizeof(spectrum)+item->wb_samples);
                sem_wait(&wideband_semaphore);
                client_set_wb_samples(client_samples, widebandBuffer, item->wb_samples);
                sem_post(&wideband_semaphore);
                bufferevent_write(item->bev, client_samples, sizeof(spectrum)+item->wb_samples);
                free(client_samples);
                item->wb_frame_counter = (item->fps == 0) ? 50 : 50 / item->fps;
            }
        }
        sem_wait(&wb_bufevent_semaphore);
    }
    sem_post(&wb_bufevent_semaphore);

    wideband_timestamp++;
} // end wideband_timer_handler


void *tx_thread(void *arg)
{
    struct audio_entry *item = NULL;
    int tx_buffer_counter = 0;
    int j, i;
    unsigned char abuf[MIC_ALAW_BUFFER_SIZE];
    struct sockaddr_in mic_addr;
    socklen_t mic_length = sizeof(mic_addr);
    int mic_socket;
    MIC_BUFFER mic_buffer;
    float *data_out;

    data_out = (float *)malloc(sizeof(float) * MIC_ALAW_BUFFER_SIZE * 2);

    sdr_log(SDR_LOG_INFO, "tx_thread STARTED\n");

    double tx_IQ[TX_BUFFER_SIZE*8];
    double mic_buf[TX_BUFFER_SIZE*2];
    int error;

    // create a socket to get mic audio from radio client.
    mic_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (mic_socket < 0)
    {
        perror("hw_init: create mic audio socket failed");
        exit(1);
    }
    // setsockopt(audio_socket, SOL_SOCKET, SO_REUSEADDR, &audio_on, sizeof(audio_on));

    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = 10;
    setsockopt(mic_socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);

    memset(&mic_addr, 0, mic_length);
    mic_addr.sin_family = AF_INET;
    mic_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    mic_addr.sin_port = htons(10020);

    if (bind(mic_socket, (struct sockaddr*)&mic_addr, mic_length) < 0)
    {
        perror("tx_init: bind socket failed for mic socket");
        exit(1);
    }

    fprintf(stderr, "tx_init: mic audio bound to port %d socket %d\n", ntohs(mic_addr.sin_port), mic_socket);

    while (!getStopIQIssued())
    {
        if (mic_socket > -1)
        {
            int bytes_read = recvfrom(mic_socket, (char*)&mic_buffer, sizeof(mic_buffer), 0, (struct sockaddr*)&mic_addr, &mic_length);
            if (bytes_read < 0)
            {
           //     perror("recvfrom socket failed for mic buffer");
             //   exit(1);
            }
            if (bytes_read <= 0) continue;
         //   fprintf(stderr, "F: %2.2f  R: %2.2f\n", mic_buffer.fwd_pwr, mic_buffer.rev_pwr);
            txfwd = mic_buffer.fwd_pwr;
            txref = mic_buffer.rev_pwr;
        }
        sem_wait(&mic_semaphore);
        item = TAILQ_FIRST(&Mic_audio);
        sem_post(&mic_semaphore);
        if (item == NULL)
        {
            usleep(1000);
            continue;
        }
        else
        {
            memcpy((unsigned char*)abuf, (unsigned char*)&item->buf[0], MIC_ALAW_BUFFER_SIZE);
            if (mox)
            {
                //  convert ALAW to PCM audio
                //#pragma omp parallel for schedule(static) private(j)
                for (j=0; j < MIC_ALAW_BUFFER_SIZE; j++)
                {
                    data_out[j*2] = data_out[j*2+1] = (float)G711A_decode(abuf[j])/32767.0;
//fprintf(stderr, "%2.2f\n", data_out[j*2]);
                    mic_buf[tx_buffer_counter*2] = (double)data_out[2*j];
                    mic_buf[tx_buffer_counter*2+1] = (double)data_out[2*i+1];
                    tx_buffer_counter++;
                    if (tx_buffer_counter >= TX_BUFFER_SIZE)
                    {
                        memset(tx_IQ, 0, sizeof(double)*(TX_BUFFER_SIZE*8));
                        if (!rxOnly && !hardware_control)
                        {
                            fexchange0(current_tx, mic_buf, tx_IQ, &error);
                            if (error != 0)
                                fprintf(stderr, "TX Error (1): %d\n", error);

                            Spectrum0(1, current_tx, 0, 0, tx_IQ);
                        }
                        else
                            if (rxOnly)
                            {
                                fexchange0(current_tx, mic_buf, tx_IQ, &error);
                                if (error != 0)
                                    fprintf(stderr, "TX Error (2): %d\n", error);
                                Spectrum0(1, current_tx, 0, 0, tx_IQ);
                            }
                        // send Tx IQ to server, buffer is interleaved.
                        hw_send((unsigned char *)tx_IQ, sizeof(tx_IQ), 0);
                        tx_buffer_counter = 0;
                    }
                } // end else rc
            } // end if mox
            sem_wait(&mic_semaphore);
            TAILQ_REMOVE(&Mic_audio, item, entries);
            sem_post(&mic_semaphore);
            free(item->buf);
            free(item);
        } // end else item
    } // end while
    close(mic_socket);
    fprintf(stderr, "tx_thread stopped.\n");
    return NULL;
} // end tx_thread


void client_set_timing()
{
    timing = 1;
} // end client_set_timing


void errorcb(struct bufferevent *bev, short error, void *ctx)
{
    client_entry *item;
    int client_count = 0;

    if ((error & BEV_EVENT_EOF) || (error & BEV_EVENT_ERROR))
    {
        /* connection has been closed, or error has occured, do any clean up here */
        /* ... */
        sem_wait(&bufferevent_semaphore);
        for (item = TAILQ_FIRST(&Client_list); item != NULL; item = TAILQ_NEXT(item, entries))
        {
            if (item->bev == bev)
            {
                char ipstr[16];
                inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
                sdr_log(SDR_LOG_INFO, "Client disconnection from %s:%d\n",
                        ipstr, ntohs(item->client.sin_port));
                TAILQ_REMOVE(&Client_list, item, entries);
                free(item);
                break;
            }
        }

        TAILQ_FOREACH(item, &Client_list, entries)
        {
            client_count++;
        }

        sem_post(&bufferevent_semaphore);

        if (client_count <= 0)
        {
            fprintf(stderr, "No more clients so closing all DSP channels.\n");
            sem_wait(&bufferevent_semaphore);
            for (int i=0;i<active_channels;i++)
            {
                if (channels[i].enabled)
                {
                    CloseChannel(channels[i].receiver);
                    if (channels[i].transmitter >= 0)
                        CloseChannel(channels[i].transmitter);
                    channels[i].enabled = false;
                }
            }
            send_audio = 0;
            sem_post(&bufferevent_semaphore);
        }
        bufferevent_free(bev);
    } else if (error & BEV_EVENT_ERROR) {
        /* check errno to see what error occurred */
        /* ... */
        sdr_log(SDR_LOG_INFO, "special EVUTIL_SOCKET_ERROR() %d: %s\n",  EVUTIL_SOCKET_ERROR(), evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    } else if (error & BEV_EVENT_TIMEOUT) {
        /* must be a timeout event handle, handle it */
        /* ... */
    } else if (error & BEV_EVENT_CONNECTED){
        sdr_log(SDR_LOG_INFO, "BEV_EVENT_CONNECTED: completed SSL handshake connection\n");
    }
} // end errorcb


void do_accept(evutil_socket_t listener, short event, void *arg)
{
    client_entry *item;
    struct event_base *base = arg;
    struct sockaddr_in ss;
    socklen_t slen = sizeof(ss);

    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0)
    {
        sdr_log(SDR_LOG_WARNING, "accept failed\n");
        return;
    }
    char ipstr[16];
    // add newly connected client to Client_list
    item = malloc(sizeof(*item));
    memset(item, 0, sizeof(*item));
    memcpy(&item->client, &ss, sizeof(ss));

    inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
    sdr_log(SDR_LOG_INFO, "Client connection from %s:%d\n",
            ipstr, ntohs(item->client.sin_port));

    struct bufferevent *bev;
    evutil_make_socket_nonblocking(fd);
    evutil_make_socket_closeonexec(fd);
    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
    bufferevent_setcb(bev, readcb, writecb, errorcb, NULL);
    bufferevent_setwatermark(bev, EV_READ, MSG_SIZE, 0);
    bufferevent_setwatermark(bev, EV_WRITE, 4096, 0);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    item->bev = bev;
    item->fps = 0;
    item->frame_counter = 0;
    sem_wait(&bufferevent_semaphore);
    TAILQ_INSERT_TAIL(&Client_list, item, entries);
    sem_post(&bufferevent_semaphore);

    int client_count = 0;
    sem_wait(&bufferevent_semaphore);
    /* NB: Clobbers item */
    TAILQ_FOREACH(item, &Client_list, entries)
    {
        client_count++;
    }
    sem_post(&bufferevent_semaphore);

    if (client_count == 0)
    {
        zoom = 0;
    }
} // end do_accept

#ifdef USE_SSL
/*************************************************************************************************
 * ********************************************************************************************
 * ***********************************************************************************************/

// used for testing ssl socket.  This is just an echo server.
static void
ssl_readcb(struct bufferevent * bev, void * arg)
{
    struct evbuffer *in = bufferevent_get_input(bev);

    printf("Received %zu bytes\n", evbuffer_get_length(in));
    printf("----- data ----\n");
    printf("%.*s\n", (int)evbuffer_get_length(in), evbuffer_pullup(in, -1));
    bufferevent_write_buffer(bev, in);
} // end ssl_readcb

/**
   Create a new SSL bufferevent to send its data over an SSL * on a socket.

   @param base An event_base to use to detect reading and writing
   @param fd A socket to use for this SSL
   @param ssl A SSL* object from openssl.
   @param state The current state of the SSL connection
   @param options One or more bufferevent_options
   @return A new bufferevent on success, or NULL on failure.
*/
struct bufferevent *
        bufferevent_openssl_socket_new(struct event_base *base,
                                       evutil_socket_t fd,
                                       struct ssl_st *ssl,
                                       enum bufferevent_ssl_state state,
                                       int options);

static void
do_accept_ssl(struct evconnlistener *serv, int sock, struct sockaddr *sa, int sa_len, void *arg)
{
    struct event_base *evbase;
    struct bufferevent *bev;
    SSL_CTX *server_ctx;
    SSL *client_ctx;

    server_ctx = (SSL_CTX *)arg;
    client_ctx = SSL_new(server_ctx);
    evbase = evconnlistener_get_base(serv);
    evutil_make_socket_nonblocking(sock);
    bev = bufferevent_openssl_socket_new(evbase, sock, client_ctx,
                                         BUFFEREVENT_SSL_ACCEPTING,
                                         BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);

    client_entry *item;

    // add newly connected client to Client_list
    item = malloc(sizeof(*item));
    memset(item, 0, sizeof(*item));

    bufferevent_setcb(bev, readcb, writecb, errorcb, NULL);
    bufferevent_setwatermark(bev, EV_READ, MSG_SIZE, 0);
    bufferevent_setwatermark(bev, EV_WRITE, 4096, 0);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    item->bev = bev;
    item->fps = 0;
    item->frame_counter = 0;
    sem_wait(&bufferevent_semaphore);
    TAILQ_INSERT_TAIL(&Client_list, item, entries);
    sem_post(&bufferevent_semaphore);

    int client_count = 0;
    sem_wait(&bufferevent_semaphore);
    /* NB: Clobbers item */
    TAILQ_FOREACH(item, &Client_list, entries)
    {
        client_count++;
    }
    sem_post(&bufferevent_semaphore);

    if (client_count == 0)
    {
        zoom = 0;
    }

    /*
    bufferevent_enable(bev, EV_READ);
    bufferevent_setcb(bev, ssl_readcb, NULL, NULL, NULL);
*/
} // end do_accept_ssl


SSL_CTX *evssl_init(void)
{
    SSL_CTX  *server_ctx;

    /* Initialize the OpenSSL library */
    SSL_load_error_strings();
    SSL_library_init();
    /* We MUST have entropy, or else there's no point to crypto. */
    if (!RAND_poll())
        return NULL;

    server_ctx = SSL_CTX_new(SSLv23_server_method());

    if (! SSL_CTX_use_certificate_chain_file(server_ctx, "cert") ||
            ! SSL_CTX_use_PrivateKey_file(server_ctx, "pkey", SSL_FILETYPE_PEM))
    {
        puts("Couldn't read 'pkey' or 'cert' file.  To generate a key\n"
             "and self-signed certificate, run:\n"
             "  openssl genrsa -out pkey 2048\n"
             "  openssl req -new -key pkey -out cert.req\n"
             "  openssl x509 -req -days 365 -in cert.req -signkey pkey -out cert");
        return NULL;
    }
    SSL_CTX_set_options(server_ctx, SSL_OP_NO_SSLv2);

    return server_ctx;
} // end evssl_init

static pthread_mutex_t *lock_cs;
static long *lock_count;

void pthreads_locking_callback(int mode, int type, char *file, int line);
unsigned long pthreads_thread_id(void);

void thread_setup(void)
{
    int i;

    lock_cs = OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
    lock_count = OPENSSL_malloc(CRYPTO_num_locks() * sizeof(long));
    for (i=0; i<CRYPTO_num_locks(); i++)
    {
        lock_count[i] = 0;
        pthread_mutex_init(&(lock_cs[i]), NULL);
    }

    CRYPTO_set_id_callback((unsigned long (*)())pthreads_thread_id);
    CRYPTO_set_locking_callback((void (*)())pthreads_locking_callback);
} // end thread_setup


void thread_cleanup(void)
{
    int i;

    CRYPTO_set_locking_callback(NULL);
    fprintf(stderr, "cleanup\n");
    for (i=0; i<CRYPTO_num_locks(); i++)
    {
        pthread_mutex_destroy(&(lock_cs[i]));
        // fprintf(stderr, "%8ld:%s\n", lock_count[i], CRYPTO_get_lock_name(i));
    }
    OPENSSL_free(lock_cs);
    OPENSSL_free(lock_count);

    fprintf(stderr,"done cleanup\n");
} // end thread_cleanup


void pthreads_locking_callback(int mode, int type, char *file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        pthread_mutex_lock(&(lock_cs[type]));
        lock_count[type]++;
    }
    else
    {
        pthread_mutex_unlock(&(lock_cs[type]));
    }
} // end pthreads_locking_callback
#endif

unsigned long pthreads_thread_id(void)
{
    unsigned long ret;

    ret=(unsigned long)pthread_self();
    return(ret);
} // end pthreads_thread_id


void* client_thread(void* arg)
{
    int on = 1;
    struct event_base *base;
    struct event *listener_event;
    struct sockaddr_in server;
    int serverSocket;

#ifdef USE_SSL
    struct evconnlistener *listener;
    SSL_CTX *ctx;
    struct sockaddr_in server_ssl;
#endif
    fprintf(stderr,"client_thread\n");

    // setting up non-ssl open serverSocket
    serverSocket = socket(AF_INET,SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        perror("client socket");
        return NULL;
    }

    evutil_make_socket_nonblocking(serverSocket);
    evutil_make_socket_closeonexec(serverSocket);

#ifndef WIN32
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    if(bind(serverSocket,(struct sockaddr *)&server,sizeof(server))<0)
    {
        perror("client bind");
        fprintf(stderr, "port=%d\n", port);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "client_thread: listening on port %d\n", port);

    if (listen(serverSocket, 5) == -1)
    {
        perror("client listen");
        exit(1);
    }
#ifdef USE_SSL
    // setting up ssl server
    memset(&server_ssl, 0, sizeof(server_ssl));
    server_ssl.sin_family = AF_INET;
    server_ssl.sin_addr.s_addr = htonl(INADDR_ANY);
    server_ssl.sin_port = htons(port_ssl);

    ctx = evssl_init();
    if (ctx == NULL)
    {
        perror("client ctx init failed");
        exit(1);
    }
    // setup openssl thread-safe callbacks
    thread_setup();
#endif
    // this is the Event base for both non-ssl and ssl servers
    base = event_base_new();

    // add the non-ssl listener to event base
    listener_event = event_new(base, serverSocket, EV_READ|EV_PERSIST, do_accept, (void*)base);
    event_add(listener_event, NULL);
#ifdef USE_SSL
    // add the ssl listener to event base
    listener = evconnlistener_new_bind(
                base, do_accept_ssl, (void *)ctx,
                LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE |
                LEV_OPT_THREADSAFE, 1024,
                (struct sockaddr *)&server_ssl, sizeof(server_ssl));

    sdr_log(SDR_LOG_INFO, "client_thread: listening on port %d for ssl connection\n", port_ssl);
#endif
    // this will be an endless loop to service all the network events
    event_base_loop(base, 0);
#ifdef USE_SSL
    // if for whatever reason the Event loop terminates, cleanup
    evconnlistener_free(listener);
    thread_cleanup();
    SSL_CTX_free(ctx);
#endif
    return NULL;
} // end client_thread


/*****************************************************************************************************************
******************************************************************************************************************
*****************************************************************************************************************/

void* spectrum_client_thread(void* arg)
{
    int on = 1;
    struct event_base *base;
    struct event *listener_event;
    struct sockaddr_in server;
    int spectrumSocket;

    fprintf(stderr,"spectrum_client_thread\n");

    // setting up non-ssl open serverSocket
    spectrumSocket = socket(AF_INET,SOCK_STREAM, 0);
    if (spectrumSocket == -1)
    {
        perror("spectrum_client socket");
        return NULL;
    }

    evutil_make_socket_nonblocking(spectrumSocket);
    evutil_make_socket_closeonexec(spectrumSocket);

#ifndef WIN32
    setsockopt(spectrumSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port+1);

    if (bind(spectrumSocket, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("spectrum client bind");
        fprintf(stderr,"port = %d\n", port+1);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "spectrum_client_thread: listening on port %d\n", port+1);

    if (listen(spectrumSocket, 5) == -1)
    {
        perror("spectrum client listen");
        exit(1);
    }

    // this is the Event base for both non-ssl and ssl servers
    base = event_base_new();

    // add the non-ssl listener to event base
    listener_event = event_new(base, spectrumSocket, EV_READ|EV_PERSIST, do_accept_spectrum, (void*)base);
    event_add(listener_event, NULL);

    event_base_loop(base, 0);

    return NULL;
} // end spectrum_client_thread


void spectrum_writecb(struct bufferevent *bev, void *ctx)
{
    struct audio_entry *item;
    client_entry *client_item;
    /*
    while ((item = audio_stream_queue_remove()) != NULL)
    {
        sem_wait(&spec_bufevent_semaphore);
        TAILQ_FOREACH(client_item, &Spectrum_client_list, entries)
        {
            sem_post(&spec_bufevent_semaphore);
            //  if(client_item->rtp == connection_tcp)
            {
                bufferevent_write(client_item->bev, item->buf, item->length);
            }
            sem_wait(&spec_bufevent_semaphore);
        }
        sem_post(&spec_bufevent_semaphore);

        free(item->buf);
        free(item);
    } */
} // end spectrum_writecb


void spectrum_readcb(struct bufferevent *bev, void *ctx)
{
    struct evbuffer *inbuf;
    client_entry    *item, *current_item = NULL;
    unsigned char    message[MSG_SIZE];
    int              bytesRead = 0;

    sem_wait(&spec_bufevent_semaphore);
    item = TAILQ_FIRST(&Spectrum_client_list);
    sem_post(&spec_bufevent_semaphore);
    if (item == NULL)
    {
        sdr_log(SDR_LOG_ERROR, "readcb called with no spectrum clients");
        return;
    }

    if (item->bev != bev)
    {
        client_entry *tmp_item;
        /* Only allow the first client on Client_list to command
         * dspserver as primary.  If this client is not the primary, we
         * will first determine whether it is allowed to execute the
         * command it is executing, and abort if it is not. */

        // locate the current_item for this slave client
        sem_wait(&spec_bufevent_semaphore);
        for (current_item = TAILQ_FIRST(&Spectrum_client_list); current_item != NULL; current_item = tmp_item)
        {
            tmp_item = TAILQ_NEXT(current_item, entries);
            if (current_item->bev == bev)
            {
                break;
            }
        }
        sem_post(&spec_bufevent_semaphore);
        if (current_item == NULL)
        {
            sdr_log(SDR_LOG_ERROR, "This nonprivileged client was not located");
            return;
        }
    }

    inbuf = bufferevent_get_input(bev);
    while (evbuffer_get_length(inbuf) >= MSG_SIZE)
    {
        bytesRead = bufferevent_read(bev, message, MSG_SIZE);
        if (bytesRead != MSG_SIZE)
        {
            sdr_log(SDR_LOG_ERROR, "Short read from spectrum client; shouldn't happen\n");
            return;
        }
        fprintf(stderr, "Spectrum message: %s\n", (const char*)(message+2));
        switch ((unsigned char)message[0])
        {
        case ENABLENOTCHFILTER:
            RXANBPSetNotchesRun(current_rx, message[1]);
            sdr_log(SDR_LOG_INFO, "Notch filter set to: %d\n", message[1]);
            break;

        case SETNOTCHFILTER:
        {
            double fcenter, fwidth;
        //    printf("%s\n", message+2);
            sscanf((const char*)(message+2), "%lf %lf", &fcenter, &fwidth);
            RXANBPAddNotch(current_rx, message[1]-1, fcenter, fwidth, true);
            sdr_log(SDR_LOG_INFO, "Notch filter added: Id: %d  F: %lf   W: %lf'\n", message[1]-1, fcenter, fwidth);
        }
            break;

        case EDITNOTCHFILTER:
        {
            double fcenter, fwidth;
        //    printf("%s\n", message+2);
            sscanf((const char*)(message+2), "%lf %lf", &fcenter, &fwidth);
            RXANBPEditNotch(current_rx, message[1]-1, fcenter, fwidth, true);
            sdr_log(SDR_LOG_INFO, "Notch filter updated: Id: %d  F: %lf   W: %lf'\n", message[1]-1, fcenter, fwidth);
        }
            break;

        case DELNOTCHFILTER:
        {
            RXANBPDeleteNotch(current_rx, message[1]);
            sdr_log(SDR_LOG_INFO, "Notch filter id: %d deleted.\n", message[1]);
        }
            break;

        case SETFPS:
        {
            int samp, fps;

            sscanf((const char*)(message+1), "%d,%d", &samp, &fps);
            sdr_log(SDR_LOG_INFO, "Spectrum fps set to = '%d'  Samples = '%d'\n", fps, samp);
            sem_wait(&spec_bufevent_semaphore);
            item->samples = samp;
            item->fps = fps;
            sem_post(&spec_bufevent_semaphore);
        }
            break;
        }
    }
} // end spectrum_readcb


void spectrum_errorcb(struct bufferevent *bev, short error, void *ctx)
{
    client_entry *item;
    int client_count = 0;

    if ((error & BEV_EVENT_EOF) || (error & BEV_EVENT_ERROR))
    {
        /* connection has been closed, or error has occured, do any clean up here */
        /* ... */
        sem_wait(&spec_bufevent_semaphore);
        for (item = TAILQ_FIRST(&Spectrum_client_list); item != NULL; item = TAILQ_NEXT(item, entries))
        {
            if (item->bev == bev)
            {
                char ipstr[16];
                inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
                sdr_log(SDR_LOG_INFO, "Spectrum client disconnection from %s:%d\n",
                        ipstr, ntohs(item->client.sin_port));
                TAILQ_REMOVE(&Spectrum_client_list, item, entries);
                hw_stopIQ();
                free(item);
                break;
            }
        }

        TAILQ_FOREACH(item, &Spectrum_client_list, entries)
        {
            client_count++;
        }

        sem_post(&spec_bufevent_semaphore);

        bufferevent_free(bev);
    }
    else if (error & BEV_EVENT_ERROR)
    {
        /* check errno to see what error occurred */
        /* ... */
        sdr_log(SDR_LOG_INFO, "special EVUTIL_SOCKET_ERROR() %d: %s\n", EVUTIL_SOCKET_ERROR(), evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    }
    else if (error & BEV_EVENT_TIMEOUT)
    {
        /* must be a timeout event handle, handle it */
        /* ... */
    } else if (error & BEV_EVENT_CONNECTED)
    {
        sdr_log(SDR_LOG_INFO, "BEV_EVENT_CONNECTED: completed SSL handshake connection\n");
    }
} // end spectrum_errorcb


void do_accept_spectrum(evutil_socket_t listener, short event, void *arg)
{
    client_entry *item;
    struct event_base *base = arg;
    struct sockaddr_in ss;
    socklen_t slen = sizeof(ss);

    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0)
    {
        sdr_log(SDR_LOG_WARNING, "spectrum client accept failed\n");
        return;
    }
    char ipstr[16];
    // add newly connected client to Spectrum_client_list
    item = malloc(sizeof(*item));
    memset(item, 0, sizeof(*item));
    memcpy(&item->client, &ss, sizeof(ss));

    inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
    sdr_log(SDR_LOG_INFO, "Spectrum client connection from %s:%d\n",
            ipstr, ntohs(item->client.sin_port));

    struct bufferevent *bev;
    evutil_make_socket_nonblocking(fd);
    evutil_make_socket_closeonexec(fd);
    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
    bufferevent_setcb(bev, spectrum_readcb, spectrum_writecb, spectrum_errorcb, NULL);
    bufferevent_setwatermark(bev, EV_READ, MSG_SIZE, 0);
    bufferevent_setwatermark(bev, EV_WRITE, 4096, 0);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    item->bev = bev;
    item->fps = 0;
    item->frame_counter = 0;
    sem_wait(&spec_bufevent_semaphore);
    TAILQ_INSERT_TAIL(&Spectrum_client_list, item, entries);
    sem_post(&spec_bufevent_semaphore);

    int client_count = 0;
    sem_wait(&spec_bufevent_semaphore);
    /* NB: Clobbers item */
    TAILQ_FOREACH(item, &Spectrum_client_list, entries)
    {
        client_count++;
    }
    sem_post(&spec_bufevent_semaphore);

    if (client_count == 0)
    {
        zoom = 0;
    }
} // emd do_accept_spectrum

/*****************************************************************************************************************
******************************************************************************************************************
*****************************************************************************************************************/
 
void* wideband_client_thread(void* arg)
{
    int on = 1;
    struct event_base *base;
    struct event *listener_event;
    struct sockaddr_in server;
    int    widebandSocket;

    fprintf(stderr,"wideband_client_thread\n");

    // setting up non-ssl open serverSocket
    widebandSocket = socket(AF_INET,SOCK_STREAM, 0);
    if (widebandSocket == -1)
    {
        perror("wideband_client socket");
        return NULL;
    }

    evutil_make_socket_nonblocking(widebandSocket);
    evutil_make_socket_closeonexec(widebandSocket);

#ifndef WIN32
    setsockopt(widebandSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port+2);

    if (bind(widebandSocket, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("wideband client bind");
        fprintf(stderr,"port = %d\n", port+2);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "wideband_client_thread: listening on port %d\n", port+2);

    if (listen(widebandSocket, 5) == -1)
    {
        perror("wideband client listen");
        exit(1);
    }

    // this is the Event base for both non-ssl and ssl servers
    base = event_base_new();

    // add the non-ssl listener to event base
    listener_event = event_new(base, widebandSocket, EV_READ|EV_PERSIST, do_accept_wideband, (void*)base);
    event_add(listener_event, NULL);

    event_base_loop(base, 0);

    return NULL;
} // end wideband_client_thread


void wideband_writecb(struct bufferevent *bev, void *ctx)
{
    struct audio_entry *item;
    client_entry *client_item;
    /*
    while ((item = audio_stream_queue_remove()) != NULL)
    {
        sem_wait(&spec_bufevent_semaphore);
        TAILQ_FOREACH(client_item, &Spectrum_client_list, entries)
        {
            sem_post(&spec_bufevent_semaphore);
            //  if(client_item->rtp == connection_tcp)
            {
                bufferevent_write(client_item->bev, item->buf, item->length);
            }
            sem_wait(&spec_bufevent_semaphore);
        }
        sem_post(&spec_bufevent_semaphore);

        free(item->buf);
        free(item);
    } */
} // end wideband_writecb


void wideband_readcb(struct bufferevent *bev, void *ctx)
{
    struct evbuffer *inbuf;
    client_entry    *item, *current_item = NULL;
    unsigned char    message[MSG_SIZE];
    int              bytesRead = 0;

    sem_wait(&wb_bufevent_semaphore);
    item = TAILQ_FIRST(&Wideband_client_list);
    sem_post(&wb_bufevent_semaphore);
    if (item == NULL)
    {
        sdr_log(SDR_LOG_ERROR, "readcb called with no wideband clients");
        return;
    }

    if (item->bev != bev)
    {
        sdr_log(SDR_LOG_ERROR, "readcb called with wideband non-privileged client");
        client_entry *tmp_item;
        /* Only allow the first client on Client_list to command
         * dspserver as primary.  If this client is not the primary, we
         * will first determine whether it is allowed to execute the
         * command it is executing, and abort if it is not. */

        // locate the current_item for this slave client
        sem_wait(&wb_bufevent_semaphore);
        for (current_item = TAILQ_FIRST(&Wideband_client_list); current_item != NULL; current_item = tmp_item)
        {
            tmp_item = TAILQ_NEXT(current_item, entries);
            if (current_item->bev == bev)
            {
                break;
            }
        }
        sem_post(&wb_bufevent_semaphore);
        if (current_item == NULL)
        {
            sdr_log(SDR_LOG_ERROR, "This non-privileged client was not located");
            return;
        }
    }

    inbuf = bufferevent_get_input(bev);
    while (evbuffer_get_length(inbuf) >= MSG_SIZE)
    {
        bytesRead = bufferevent_read(bev, message, MSG_SIZE);
        if (bytesRead != MSG_SIZE)
        {
            sdr_log(SDR_LOG_ERROR, "Short read from wideband client; shouldn't happen\n");
            return;
        }
        fprintf(stderr, "Wideband message: %s\n", (const char*)(message+1));
        switch ((unsigned char)message[0])
        {
        case STARTBANDSCOPE:
        {
            int width = 0;
            sscanf((const char*)(message+1), "%d", &width);
            sdr_log(SDR_LOG_INFO, "Bandscope width: %u\n", width);
            if (width < 512) break;
            item->wb_samples = (int)width;
            item->fps = 10;
            widebandBuffer = malloc((item->wb_samples * 2) * sizeof(float));
            enable_wideband(true);
        }
            break;

        case STOPBANDSCOPE:
        {
            enable_wideband(false);
            if (widebandBuffer != NULL)
                free(widebandBuffer);
            widebandBuffer = NULL;
        }
            break;

        case UPDATEBANDSCOPE:
        {
            int width = 0;
            sscanf((const char*)(message+1), "%d", &width);
            sdr_log(SDR_LOG_INFO, "Bandscope width: %u\n", width);
            if (width < 512) break;
            if (wideband_enabled)
            {
                sem_wait(&wb_bufevent_semaphore);
                item->wb_samples = (int)width;
                if (widebandBuffer != NULL) free(widebandBuffer);
                widebandBuffer = malloc((item->wb_samples * 2) * sizeof(float));
                widebandInitAnalyzer(WIDEBAND_CHANNEL, width);
                sem_post(&wb_bufevent_semaphore);
            }
        }
            break;
        }
    }
} // end wideband_readcb


void wideband_errorcb(struct bufferevent *bev, short error, void *ctx)
{
    client_entry *item;
    int client_count = 0;

    if ((error & BEV_EVENT_EOF) || (error & BEV_EVENT_ERROR))
    {
        /* connection has been closed, or error has occured, do any clean up here */
        /* ... */
        sem_wait(&wb_bufevent_semaphore);
        for (item = TAILQ_FIRST(&Wideband_client_list); item != NULL; item = TAILQ_NEXT(item, entries))
        {
            if (item->bev == bev)
            {
                char ipstr[16];
                inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
                sdr_log(SDR_LOG_INFO, "Wideband client disconnection from %s:%d\n",
                        ipstr, ntohs(item->client.sin_port));
                TAILQ_REMOVE(&Wideband_client_list, item, entries);
                if (widebandBuffer != NULL)
                {
                    free(widebandBuffer);
                    widebandBuffer = NULL;
                }
                free(item);
                break;
            }
        }

        TAILQ_FOREACH(item, &Wideband_client_list, entries)
        {
            client_count++;
        }

        sem_post(&wb_bufevent_semaphore);

        bufferevent_free(bev);
    }
    else if (error & BEV_EVENT_ERROR)
    {
        /* check errno to see what error occurred */
        /* ... */
        sdr_log(SDR_LOG_INFO, "special EVUTIL_SOCKET_ERROR() %d: %s\n", EVUTIL_SOCKET_ERROR(), evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    }
    else if (error & BEV_EVENT_TIMEOUT)
    {
        /* must be a timeout event handle, handle it */
        /* ... */
    } else if (error & BEV_EVENT_CONNECTED)
    {
        sdr_log(SDR_LOG_INFO, "BEV_EVENT_CONNECTED: completed SSL handshake connection\n");
    }
} // end wideband_errorcb


void do_accept_wideband(evutil_socket_t listener, short event, void *arg)
{
    client_entry *item;
    struct event_base *base = arg;
    struct sockaddr_in ss;
    socklen_t slen = sizeof(ss);

    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0)
    {
        sdr_log(SDR_LOG_WARNING, "wideband client accept failed\n");
        return;
    }
    char ipstr[16];
    // add newly connected client to Wideband_client_list
    item = malloc(sizeof(*item));
    memset(item, 0, sizeof(*item));
    memcpy(&item->client, &ss, sizeof(ss));

    inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
    sdr_log(SDR_LOG_INFO, "Wideband client connection from %s:%d\n",
            ipstr, ntohs(item->client.sin_port));

    struct bufferevent *bev;
    evutil_make_socket_nonblocking(fd);
    evutil_make_socket_closeonexec(fd);
    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
    bufferevent_setcb(bev, wideband_readcb, wideband_writecb, wideband_errorcb, NULL);
    bufferevent_setwatermark(bev, EV_READ, MSG_SIZE, 0);
    bufferevent_setwatermark(bev, EV_WRITE, 4096, 0);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    item->bev = bev;
    item->fps = 0;
    item->wb_frame_counter = 0;
    sem_wait(&wb_bufevent_semaphore);
    TAILQ_INSERT_TAIL(&Wideband_client_list, item, entries);
    sem_post(&wb_bufevent_semaphore);

    int client_count = 0;
    sem_wait(&wb_bufevent_semaphore);
    /* NB: Clobbers item */
    TAILQ_FOREACH(item, &Wideband_client_list, entries)
    {
        client_count++;
    }
    sem_post(&wb_bufevent_semaphore);
} // emd do_accept_wideband


/*****************************************************************************************************************
******************************************************************************************************************
*****************************************************************************************************************/

void* audio_client_thread(void* arg)
{
    int on = 1;
    struct event_base *base;
    struct event *listener_event;
    struct sockaddr_in server;
    int audioSocket;

    fprintf(stderr,"audio_client_thread\n");

    // setting up non-ssl open serverSocket
    audioSocket = socket(AF_INET,SOCK_STREAM, 0);
    if (audioSocket == -1)
    {
        perror("audio_client socket");
        return NULL;
    }

    evutil_make_socket_nonblocking(audioSocket);
    evutil_make_socket_closeonexec(audioSocket);

#ifndef WIN32
    setsockopt(audioSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port+10);

    if (bind(audioSocket, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("audio client bind");
        fprintf(stderr,"port=%d\n", port+10);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "audio_client_thread: listening on port %d\n", port+10);

    if (listen(audioSocket, 5) == -1)
    {
        perror("audio client listen");
        exit(1);
    }

    // this is the Event base for both non-ssl and ssl servers
    base = event_base_new();

    // add the non-ssl listener to event base
    listener_event = event_new(base, audioSocket, EV_READ|EV_PERSIST, do_accept_audio, (void*)base);
    event_add(listener_event, NULL);

    event_base_loop(base, 0);

    return NULL;
} // end audio_client_thread


void audio_writecb(struct bufferevent *bev, void *ctx)
{
    struct audio_entry *item;
    client_entry *client_item;

    while ((client_item = audio_stream_queue_remove()) != NULL)
    {
        sem_wait(&audio_bufevent_semaphore);
        TAILQ_FOREACH(client_item, &Audio_client_list, entries)
        {
            sem_post(&audio_bufevent_semaphore);
            bufferevent_write(client_item->bev, item->buf, item->length);
            sem_wait(&audio_bufevent_semaphore);
        }
        sem_post(&audio_bufevent_semaphore);

        free(item->buf);
        free(item);
    }
} // end audio_writecb


void audio_readcb(struct bufferevent *bev, void *ctx)
{
    struct evbuffer *inbuf;
    client_entry    *item, *current_item = NULL;
    unsigned char    message[MSG_SIZE];
    int              bytesRead = 0;

    sem_wait(&audio_bufevent_semaphore);
    item = TAILQ_FIRST(&Audio_client_list);
    sem_post(&audio_bufevent_semaphore);
    if (item == NULL)
    {
        sdr_log(SDR_LOG_ERROR, "readcb called with no audio clients");
        return;
    }

    if (item->bev != bev)
    {
        client_entry *tmp_item;
        /* Only allow the first client on Client_list to command
         * dspserver as primary.  If this client is not the primary, we
         * will first determine whether it is allowed to execute the
         * command it is executing, and abort if it is not. */

        // locate the current_item for this slave client
        sem_wait(&audio_bufevent_semaphore);
        for (current_item = TAILQ_FIRST(&Audio_client_list); current_item != NULL; current_item = tmp_item)
        {
            tmp_item = TAILQ_NEXT(current_item, entries);
            if (current_item->bev == bev)
            {
                break;
            }
        }
        sem_post(&audio_bufevent_semaphore);
        if (current_item == NULL)
        {
            sdr_log(SDR_LOG_ERROR, "This nonprivileged audio client was not located");
            return;
        }
    }

    inbuf = bufferevent_get_input(bev);
    while (evbuffer_get_length(inbuf) >= MSG_SIZE)
    {
        bytesRead = bufferevent_read(bev, message, MSG_SIZE);
        if (bytesRead != MSG_SIZE)
        {
            sdr_log(SDR_LOG_ERROR, "Short read from audio client; shouldn't happen\n");
            return;
        }
        fprintf(stderr, "Audio message: %s\n", (const char*)(message+1));
        switch ((unsigned char)message[0])
        {

        }
    }
} // end audio_readcb



void audio_errorcb(struct bufferevent *bev, short error, void *ctx)
{
    client_entry *item;
    int client_count = 0;

    if ((error & BEV_EVENT_EOF) || (error & BEV_EVENT_ERROR))
    {
        /* connection has been closed, or error has occured, do any clean up here */
        /* ... */
        sem_wait(&audio_bufevent_semaphore);
        for (item = TAILQ_FIRST(&Audio_client_list); item != NULL; item = TAILQ_NEXT(item, entries))
        {
            if (item->bev == bev)
            {
                char ipstr[16];
                inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
                sdr_log(SDR_LOG_INFO, "Audio client disconnection from %s:%d\n",
                        ipstr, ntohs(item->client.sin_port));
                TAILQ_REMOVE(&Audio_client_list, item, entries);
                free(item);
                break;
            }
        }

        TAILQ_FOREACH(item, &Audio_client_list, entries)
        {
            client_count++;
        }

        sem_post(&audio_bufevent_semaphore);

        if (client_count <= 0)
        {
            sem_wait(&audio_bufevent_semaphore);
            sem_post(&audio_bufevent_semaphore);
        }
        bufferevent_free(bev);
    }
    else if (error & BEV_EVENT_ERROR)
    {
        /* check errno to see what error occurred */
        /* ... */
        sdr_log(SDR_LOG_INFO, "special EVUTIL_SOCKET_ERROR() %d: %s\n", EVUTIL_SOCKET_ERROR(), evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    }
    else if (error & BEV_EVENT_TIMEOUT)
    {
        /* must be a timeout event handle, handle it */
        /* ... */
    } else if (error & BEV_EVENT_CONNECTED)
    {
        sdr_log(SDR_LOG_INFO, "BEV_EVENT_CONNECTED: completed SSL handshake connection\n");
    }
} // end audio_errorcb


void do_accept_audio(evutil_socket_t listener, short event, void *arg)
{
    client_entry *item;
    struct event_base *base = arg;
    struct sockaddr_in ss;
    socklen_t slen = sizeof(ss);

    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0)
    {
        sdr_log(SDR_LOG_WARNING, "audio client accept failed\n");
        return;
    }
    char ipstr[16];
    // add newly connected client to Audio_client_list
    item = malloc(sizeof(*item));
    memset(item, 0, sizeof(*item));
    memcpy(&item->client, &ss, sizeof(ss));

    inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
    sdr_log(SDR_LOG_INFO, "Audio client connection from %s:%d\n",
            ipstr, ntohs(item->client.sin_port));

    struct bufferevent *bev;
    evutil_make_socket_nonblocking(fd);
    evutil_make_socket_closeonexec(fd);
    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
    bufferevent_setcb(bev, audio_readcb, audio_writecb, audio_errorcb, NULL);
    bufferevent_setwatermark(bev, EV_READ, MSG_SIZE, 0);
    bufferevent_setwatermark(bev, EV_WRITE, 4096, 0);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    item->bev = bev;
    item->fps = 0;
    item->frame_counter = 0;
    sem_wait(&audio_bufevent_semaphore);
    TAILQ_INSERT_TAIL(&Audio_client_list, item, entries);
    sem_post(&audio_bufevent_semaphore);

    int client_count = 0;
    sem_wait(&audio_bufevent_semaphore);
    /* NB: Clobbers item */
    TAILQ_FOREACH(item, &Audio_client_list, entries)
    {
        client_count++;
    }
    sem_post(&audio_bufevent_semaphore);

    if (client_count == 0)
    {
        zoom = 0;
    }
} // emd do_accept_audio


/*****************************************************************************************************************
******************************************************************************************************************
*****************************************************************************************************************/

void* mic_audio_client_thread(void* arg)
{
    int on = 1;
    struct event_base *base;
    struct event *listener_event;
    struct sockaddr_in server;
    int micSocket;

    fprintf(stderr,"mic_audio_client_thread\n");

    // setting up non-ssl open serverSocket
    micSocket = socket(AF_INET,SOCK_STREAM, 0);
    if (micSocket == -1)
    {
        perror("mic_audio_client socket");
        return NULL;
    }

    evutil_make_socket_nonblocking(micSocket);
    evutil_make_socket_closeonexec(micSocket);

#ifndef WIN32
    setsockopt(micSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port+20);

    if (bind(micSocket, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("mic audio client bind");
        fprintf(stderr,"port=%d\n", port+20);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "mic_audio_client_thread: listening on port %d\n", port+20);

    if (listen(micSocket, 5) == -1)
    {
        perror("mic audio client listen");
        exit(1);
    }

    // this is the Event base for both non-ssl and ssl servers
    base = event_base_new();

    // add the non-ssl listener to event base
    listener_event = event_new(base, micSocket, EV_READ|EV_PERSIST, do_accept_mic_audio, (void*)base);
    event_add(listener_event, NULL);

    event_base_loop(base, 0);

    return NULL;
} // end mic_audio_client_thread


void mic_audio_writecb(struct bufferevent *bev, void *ctx)
{
    struct audio_entry *item;
    client_entry *client_item;
    /*
    while ((item = mic_stream_queue_remove()) != NULL)
    {
        sem_wait(&spec_bufevent_semaphore);
        TAILQ_FOREACH(client_item, &Spectrum_client_list, entries)
        {
            sem_post(&spec_bufevent_semaphore);
            //  if(client_item->rtp == connection_tcp)
            {
                bufferevent_write(client_item->bev, item->buf, item->length);
            }
            sem_wait(&spec_bufevent_semaphore);
        }
        sem_post(&spec_bufevent_semaphore);

        free(item->buf);
        free(item);
    } */
} // end mic_audio_writecb


void mic_audio_readcb(struct bufferevent *bev, void *ctx)
{
    struct evbuffer *inbuf;
    client_entry    *item, *current_item = NULL;
    unsigned char    message[MIC_MSG_SIZE];
    int              bytesRead = 0;


    sem_wait(&mic_semaphore);
    item = TAILQ_FIRST(&Mic_audio_client_list);
    sem_post(&mic_semaphore);
    if (item == NULL)
    {
        sdr_log(SDR_LOG_ERROR, "readcb called with no mic audio clients");
        return;
    }
    //    printf("mic 1\n");

    if (item->bev != bev)
    {
        client_entry *tmp_item;
        /* Only allow the first client on Client_list to command
         * dspserver as primary.  If this client is not the primary, we
         * will first determine whether it is allowed to execute the
         * command it is executing, and abort if it is not. */
        printf("mic 2\n");
        // locate the current_item for this slave client
        sem_wait(&mic_semaphore);
        for (current_item = TAILQ_FIRST(&Mic_audio_client_list); current_item != NULL; current_item = tmp_item)
        {
            printf("mic 3\n");
            tmp_item = TAILQ_NEXT(current_item, entries);
            if (current_item->bev == bev)
            {
                break;
            }
        }
        sem_post(&mic_semaphore);
        if (current_item == NULL)
        {
            sdr_log(SDR_LOG_ERROR, "This nonprivileged mic audio client was not located");
            return;
        }
    }

    inbuf = bufferevent_get_input(bev);
    while (evbuffer_get_length(inbuf) >= MIC_MSG_SIZE)
    {
        bytesRead = bufferevent_read(bev, message, MIC_MSG_SIZE);
        if (bytesRead != MIC_MSG_SIZE)
        {
            sdr_log(SDR_LOG_ERROR, "Short read from mic audio client; shouldn't happen\n");
            return;
        }
        //        fprintf(stderr, "Mic audio message: %s\n", (const char*)(message+1));
        switch ((unsigned char)message[0])
        {
        case ISMIC:
        {
            //  int samp, fps;
            //    fprintf(stderr, "mic 4\n");
            //  sscanf((const char*)(message+1), "%d,%d", &samp, &fps);
            //  sdr_log(SDR_LOG_INFO, "Spectrum fps set to = '%d'  Samples = '%d'\n", fps, samp);
            sem_wait(&mic_semaphore);
            memcpy(mic_buffer, &message[6], MIC_ALAW_BUFFER_SIZE);
            sem_post(&mic_semaphore);
            Mic_stream_queue_add();
        }
            break;
        }
    }
} // end mic_audio_readcb


void mic_audio_errorcb(struct bufferevent *bev, short error, void *ctx)
{
    client_entry *item;
    int client_count = 0;

    if ((error & BEV_EVENT_EOF) || (error & BEV_EVENT_ERROR))
    {
        /* connection has been closed, or error has occured, do any clean up here */
        /* ... */
        sem_wait(&mic_semaphore);
        for (item = TAILQ_FIRST(&Mic_audio_client_list); item != NULL; item = TAILQ_NEXT(item, entries))
        {
            if (item->bev == bev)
            {
                char ipstr[16];
                inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
                sdr_log(SDR_LOG_INFO, "Mic audio client disconnection from %s:%d\n",
                        ipstr, ntohs(item->client.sin_port));
                TAILQ_REMOVE(&Mic_audio_client_list, item, entries);
                free(item);
                break;
            }
        }

        TAILQ_FOREACH(item, &Mic_audio_client_list, entries)
        {
            client_count++;
        }

        sem_post(&mic_semaphore);

        if (client_count <= 0)
        {
            sem_wait(&mic_semaphore);
            sem_post(&mic_semaphore);
        }
        bufferevent_free(bev);
    }
    else if (error & BEV_EVENT_ERROR)
    {
        /* check errno to see what error occurred */
        /* ... */
        sdr_log(SDR_LOG_INFO, "special EVUTIL_SOCKET_ERROR() %d: %s\n", EVUTIL_SOCKET_ERROR(), evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    }
    else if (error & BEV_EVENT_TIMEOUT)
    {
        /* must be a timeout event handle, handle it */
        /* ... */
    } else if (error & BEV_EVENT_CONNECTED)
    {
        sdr_log(SDR_LOG_INFO, "BEV_EVENT_CONNECTED: completed SSL handshake connection\n");
    }
} // end mic_audio_errorcb


void do_accept_mic_audio(evutil_socket_t listener, short event, void *arg)
{
    client_entry *item;
    struct event_base *base = arg;
    struct sockaddr_in ss;
    socklen_t slen = sizeof(ss);

    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0)
    {
        sdr_log(SDR_LOG_WARNING, "mic audio client accept failed\n");
        return;
    }
    char ipstr[16];
    // add newly connected client to Mic_audio_client_list
    item = malloc(sizeof(*item));
    memset(item, 0, sizeof(*item));
    memcpy(&item->client, &ss, sizeof(ss));

    inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
    sdr_log(SDR_LOG_INFO, "Mic audio client connection from %s:%d\n",
            ipstr, ntohs(item->client.sin_port));

    struct bufferevent *bev;
    evutil_make_socket_nonblocking(fd);
    evutil_make_socket_closeonexec(fd);
    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
    bufferevent_setcb(bev, mic_audio_readcb, mic_audio_writecb, mic_audio_errorcb, NULL);
    bufferevent_setwatermark(bev, EV_READ, MSG_SIZE, 0);
    bufferevent_setwatermark(bev, EV_WRITE, 4096, 0);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    item->bev = bev;
    item->fps = 0;
    item->frame_counter = 0;
    sem_wait(&mic_semaphore);
    TAILQ_INSERT_TAIL(&Mic_audio_client_list, item, entries);
    sem_post(&mic_semaphore);

    int client_count = 0;
    sem_wait(&mic_semaphore);
    /* NB: Clobbers item */
    TAILQ_FOREACH(item, &Mic_audio_client_list, entries)
    {
        client_count++;
    }
    sem_post(&mic_semaphore);

    if (client_count == 0)
    {
        zoom = 0;
    }
    printf("mic accept********\n");
} // emd do_accept_mic_audio


/******************************************************************************************************************/

void writecb(struct bufferevent *bev, void *ctx)
{
    struct audio_entry *item;
    client_entry *client_item;
    /*
    while ((item = audio_stream_queue_remove()) != NULL)
    {
        sem_wait(&bufferevent_semaphore);
        TAILQ_FOREACH(client_item, &Client_list, entries)
        {
            sem_post(&bufferevent_semaphore);
            //  if(client_item->rtp == connection_tcp)
            {
                bufferevent_write(client_item->bev, item->buf, item->length);
            }
            sem_wait(&bufferevent_semaphore);
        }
        sem_post(&bufferevent_semaphore);

        free(item->buf);
        free(item);
    } */
} // end writecb


void readcb(struct bufferevent *bev, void *ctx)
{
    struct evbuffer *inbuf;
    client_entry    *item, *current_item = NULL;
    unsigned char    message[MSG_SIZE];
    char             role[15] = "privileged";
    char             answer[80];
    int              bytesRead = 0;

    sem_wait(&bufferevent_semaphore);
    item = TAILQ_FIRST(&Client_list);
    sem_post(&bufferevent_semaphore);
    if (item == NULL)
    {
        sdr_log(SDR_LOG_ERROR, "readcb called with no clients");
        return;
    }

    if (item->bev != bev)
    {
        client_entry *tmp_item;
        /* Only allow CONTROL client type to command dspserver.
         * If this client is not CONROL type, we
         * will first determine whether it is allowed to execute the
         * command it is executing, and abort if it is not. */

        // locate the current_item for this client
        sem_wait(&bufferevent_semaphore);
        for (current_item = TAILQ_FIRST(&Client_list); current_item != NULL; current_item = tmp_item)
        {
            tmp_item = TAILQ_NEXT(current_item, entries);
            if (current_item->bev == bev)
            {
                break;
            }
        }
        sem_post(&bufferevent_semaphore);
        if (current_item == NULL)
        {
            sdr_log(SDR_LOG_ERROR, "This client was not located");
            return;
        }

        strcpy(role , "non-privileged");
        tmp_item->client_type = MONITOR;
    }

    /* The documentation for evbuffer_get_length is somewhat unclear as
     * to the actual definition of "length".  It appears to be the
     * amount of space *available* in the buffer, not occupied by data;
     * However, the code for reading from an evbuffer will read as many
     * bytes as it would return, so this behavior is not different from
     * what was here before. */
    inbuf = bufferevent_get_input(bev);
    while (evbuffer_get_length(inbuf) >= MSG_SIZE)
    {
        bytesRead = bufferevent_read(bev, message, MSG_SIZE);
        if (bytesRead != MSG_SIZE)
        {
            sdr_log(SDR_LOG_ERROR, "Short read from client; shouldn't happen\n");
            return;
        }

        message[bytesRead-1] = 0;    // for Linux strings terminating in NULL

        if (message[0] == STARCOMMAND)
        {
            fprintf(stderr, "Message: [%X] [%d] [%d]\n", message[0], message[1], message[2]);
            fprintf(stderr,"HARDWARE DIRECTED: message\n");
            //if (message[1] == STARHARDWARE) message[1] = 1;
            if (item->client_type == CONTROL)
            {
                // if privilged client, forward the message to the hardware
                if (message[1] == ATTACH)
                {
//                    if (message[2] == TX)
  //                      tx_init();
                    make_connection(channels[message[2]].radio_id, channels[message[2]].recv_index, channels[message[2]].trans_index);
                }
                else
                {
                    if (message[1] == DETACH)
                    {
                        char command[64];
                        setStopIQIssued(1);
                        sleep(1);
                        command[0] = '*';
                        command[1] = DETACH;
                        command[2] = (char)channels[message[2]].recv_index;
                        command[3] = (char)channels[message[2]].radio_id;
                        hwSendStarCommand(command, 3);
                        DestroyAnalyzer(channels[message[2]].receiver);
                        CloseChannel(channels[message[2]].receiver);
                        if (channels[message[2]].transmitter >= 0)
                        {
                       //     pthread_kill(tx_thread_id, SIGKILL);
                            DestroyAnalyzer(channels[message[2]].transmitter);
                            CloseChannel(channels[message[2]].transmitter);
                        }
                        channels[message[2]].enabled = false;
                        fprintf(stderr, "** Channel detached. **\n");
                    }
                    else
                        hwSendStarCommand(message, strlen(message));
                }
                /////                answer_question(message, role, bev);
            }
            else
            {
                // if non-privileged client don't forward the message
            }
            continue;
        }

        //****** Add client command permission checks here *********/

        if (message[0] == QUESTION)
        {
            //            answer_question((const char*)(message+1), role, bev);
            fprintf(stderr, "Question.....%02X\n", message[1]);

            if (message[1] == STARHARDWARE)
            {
                fprintf(stderr, "Hardware.....\n");

                char hdr[4];
                for (int i=0;i<connected_radios;i++)
                {
                    hdr[2] = 77; //READ_MANIFEST
                    hdr[0] = ((strlen(manifest_xml[i])+1) & 0xff00) >> 8;
                    hdr[1] = (strlen(manifest_xml[i])+1) & 0xff;
                    hdr[3] = i;
                    bufferevent_write(bev, hdr, 4);
                    bufferevent_write(bev, manifest_xml[i], strlen(manifest_xml[i])+1);
                    fprintf(stderr, "Manifest %d sent.\n", i);
                }
            }
            else
                if (message[1] == QINFO)
                {  // FIX_ME: this mess needs to be changed to binary structure.
                    answer[0] = 0;
                    char hdr[4];
                    hdr[2] = QINFO;
                    strcat(answer, version);
                    //                if (strcmp(clienttype, role) == 0)
                    //              {
                    //                  strcat(answer, "^s;0");
                    //              }
                    //                else
                    //                {
                    strcat(answer, "^s;1");
                    //                }
                    strcat(answer, ";f;");
                    char f[50];
                    sprintf(f,"%lld;m;", lastFreq);
                    strcat(answer,f);
                    char m[50];
                    sprintf(m, "%d;z;", lastMode);
                    strcat(answer,m);
                    char z[50];
                    sprintf(z, "%d;l;", zoom);
                    strcat(answer,z);
                    char l[50];
                    sprintf(l, "%d;r;", low);       // Rx filter low
                    strcat(answer,l);              // Rx filter high
                    char h[50];
                    sprintf(h, "%d;", high);
                    strcat(answer, h);
                    char c[50];
                    sprintf(c, "%d;", current_channel);
                    strcat(answer, c);

                    hdr[0] = ((strlen(answer)+1) & 0xff00) >> 8;
                    hdr[1] = (strlen(answer)+1) & 0xff;
                    bufferevent_write(bev, hdr, 4);
                    bufferevent_write(bev, answer, strlen(answer)+1);
                    fprintf(stderr, "QINFO sent.\n");
                }
        }
        else
        {
            fprintf(stderr, "Message: [%X] [%d] [%d]\n", message[0], message[1], message[2]);
            switch ((unsigned char)message[0])
            {
            case SETMAIN:
            {
                if (item->client_type != CONTROL)
                {
                    sdr_log(SDR_LOG_INFO, "Set to CONTROL allowed\n");
                    sem_wait(&bufferevent_semaphore);
                    //TAILQ_REMOVE(&Client_list, current_item, entries);
                    //TAILQ_INSERT_HEAD(&Client_list, current_item, entries);
                    item->client_type = CONTROL;
                    sem_post(&bufferevent_semaphore);
                }
            }
                break;

            case STARTXCVR:
            {
                short int current_channel = (short int)message[1];
                short int rx = channels[current_channel].receiver;
                fprintf(stderr, "Ch = %d  RX = %d  TX = %d\n", current_channel, channels[current_channel].receiver, channels[current_channel].transmitter);

                OpenChannel(rx, 512, 2048, 48000, 48000, 48000, 0, 0, 0.010, 0.025, 0.000, 0.010, 0);
                printf("RX channel %d opened.\n", rx);fflush(stdout);

                create_anbEXT(rx, 0, 512, 48000, 0.0001, 0.0001, 0.0001, 0.05, 20);
                create_nobEXT(rx, 0, 0, 512, 48000, 0.0001, 0.0001, 0.0001, 0.05, 20);
                create_divEXT(rx, 0, 2, 512);

                RXASetNC(rx, 2048);
                RXASetMP(rx, 0);

                SetRXAPanelGain1(rx, 0.05f);
                SetRXAPanelSelect(rx, 3);
                SetRXAPanelPan(rx, 0.5f);
                SetRXAPanelCopy(rx, 0);
                SetRXAPanelBinaural(rx, 0);
                SetRXAPanelRun(rx, true);

                SetRXAEQRun(rx, false);

                SetRXABandpassRun(rx, true);

                SetRXAShiftFreq(rx, LO_offset);

                SetRXAAGCMode(rx, 3); //AGC_MEDIUM
                SetRXAAGCSlope(rx, 35.0f);
                SetRXAAGCTop(rx, 90.0f);
                SetRXAAGCAttack(rx, 2);
                SetRXAAGCHang(rx, 0);
                SetRXAAGCDecay(rx, 250);
                SetRXAAGCHangThreshold(rx, 100);

                SetEXTANBRun(rx, false);
                SetEXTNOBRun(rx, false);

                SetRXAEMNRPosition(rx, 0);
                SetRXAEMNRgainMethod(rx, 2);
                SetRXAEMNRnpeMethod(rx, 0);
                SetRXAEMNRRun(rx, false);
                SetRXAEMNRaeRun(rx, 1);

                SetRXAANRVals(rx, 64, 16, 16e-4, 10e-7); // defaults
                SetRXAANRRun(rx, false);
                SetRXAANFRun(rx, false);
                SetRXASNBARun(rx, false);

                RXANBPSetRun(rx, true);

                int rc = 0;
                XCreateAnalyzer(rx, &rc, 262144, 1, 1, "");
                if (rc < 0) printf("XCreateAnalyzer failed on channel %d.\n", rx);
                initAnalyzer(rx, 128, 15, 512);  // sample (128) will get changed to spectrum display width when client connects.

                SetDisplayDetectorMode(rx, 0, DETECTOR_MODE_PEAK/*display_detector_mode*/);
                SetDisplayAverageMode(rx, 0, AVERAGE_MODE_NONE/*display_average_mode*/);
                calculate_display_average(rx);

                SetChannelState(rx, 1, 1);
                channels[current_channel].enabled = true;
                current_rx = rx;

                active_channels++;

                short int tx = (short int)channels[current_channel].transmitter;
                if (tx < 0) break;

                OpenChannel(tx, 512, 2048, 48000, 96000, 192000, 1, 0, 0.010, 0.025, 0.000, 0.010, 0);
                printf("TX channel %d opened.\n", tx);fflush(stdout);

                rc = 0;
                XCreateAnalyzer(tx, &rc, 262144, 1, 1, "");
                if (rc < 0) printf("XCreateAnalyzer failed on TX channel %d.\n", tx);

                initAnalyzer(tx, 128, 15, 2048);  // sample (128) will get changed to spectrum display width when client connects.

                TXASetNC(tx, 2048);
                TXASetMP(tx, 0);
                SetTXABandpassWindow(tx, 1);
                SetTXABandpassRun(tx, 1);

                SetTXAFMEmphPosition(tx, false);

                SetTXACFIRRun(tx, 0);
                SetTXAEQRun(tx, 0);

                SetTXAAMSQRun(tx, 0);
                SetTXAosctrlRun(tx, 0);

                SetTXAALCAttack(tx, 1);
                SetTXAALCDecay(tx, 10);
                SetTXAALCSt(tx, 1); // turn it on (always on)

                SetTXALevelerAttack(tx, 1);
                SetTXALevelerDecay(tx, 500);
                SetTXALevelerTop(tx, 5.0);
                SetTXALevelerSt(tx, false);

                SetTXAPreGenMode(tx, 0);
                SetTXAPreGenToneMag(tx, 0.0);
                SetTXAPreGenToneFreq(tx, 0.0);
                SetTXAPreGenRun(tx, 0);

                SetTXAPostGenMode(tx, 0);
                SetTXAPostGenToneMag(tx, 0.2);
                SetTXAPostGenTTMag(tx, 0.2, 0.2);
                SetTXAPostGenToneFreq(tx, 0.0);
                SetTXAPostGenRun(tx, 0);

                SetTXAPanelGain1(tx, pow(10.0, 0.0 / 20.0));
                SetTXAPanelRun(tx, 1);

                SetTXAFMDeviation(tx, 2500.0);
                SetTXAAMCarrierLevel(tx, 0.5);

                SetTXACompressorGain(tx, 0.0);
                SetTXACompressorRun(tx, false);

                //  create_eerEXT(0, 0, 1024, 48000, 0.5, 200.0, true, 200/1.e6, 200/1e6, 1);
                //  SetEERRun(2, 1);

                SetTXABandpassFreqs(tx, 150, 2850);
                SetTXAMode(tx, TXA_USB);
                SetChannelState(tx, 0, 0);

                SetDisplayDetectorMode(tx, 0, DETECTOR_MODE_PEAK/*display_detector_mode*/);
                SetDisplayAverageMode(tx, 0, AVERAGE_MODE_NONE/*display_average_mode*/);
                current_tx = tx;
            }
                break;

            case STOPXCVR:
            {
                short int ch = (short int)message[1];
                current_channel = (short int)message[2];
                sem_wait(&bufferevent_semaphore);
                if (channels[ch].enabled)
                {
                    DestroyAnalyzer(channels[ch].receiver);
                    CloseChannel(channels[ch].receiver);
                    if (channels[ch].transmitter >= 0)
                    {
                        DestroyAnalyzer(channels[ch].transmitter);
                        CloseChannel(channels[ch].transmitter);
                    }
                    channels[ch].enabled = false;
                }
                sem_post(&bufferevent_semaphore);
                active_channels--;
            }
                break;

            case STARTIQ:
                hw_startIQ(current_channel);
                fprintf(stderr, "IQ thread started.\n");
                break;

            case SETTXOSCTRLRUN:
            {
                int run = 0;
                run = message[1];
                SetTXACompressorRun(current_tx, run);
                SetTXAosctrlRun(current_tx, run);
            }
                break;

            case SETRXAGCMODE:
            {
                int agc;
                agc = message[1];
                SetRXAAGCMode(current_rx, agc);
            }
                break;

            case SETRXAGCFIXED:
            {
                double agc;
                agc = atof((const char*)(message+1));
                SetRXAAGCFixed(current_rx, agc);
            }
                break;

            case SETRXAGCATTACK:
            {
                int attack = atoi((const char*)(message+1));
                SetRXAAGCAttack(current_rx, attack);
            }
                break;

            case SETRXAGCDECAY:
            {
                int decay = atoi((const char*)(message+1));
                SetRXAAGCDecay(current_rx, decay);
            }
                break;

            case SETRXAGCSLOPE:
            {
                int slope = atoi((const char*)(message+1));
                SetRXAAGCSlope(current_rx, slope);
            }
                break;

            case SETRXAGCHANG:
            {
                int hang = atoi((const char*)(message+1));
                SetRXAAGCHang(current_rx, hang);
            }
                break;

            case SETRXAGCHANGLEVEL:
            {
                double level = atof((const char*)(message+1));
                SetRXAAGCHangLevel(current_rx, level);
            }
                break;

            case SETRXAGCHANGTHRESH:
            {
                int thresh = atoi((const char*)(message+1));
                SetRXAAGCHangThreshold(current_rx, thresh);
            }
                break;

            case SETRXAGCTHRESH:
            {
                double thresh = 0.0f;
                double size = 0.0f;
                double rate = 0.0f;
                sscanf((const char*)(message+1), "%lf %lf %lf", &thresh, &size, &rate);
                SetRXAAGCThresh(current_rx, thresh, size, rate);
            }
                break;

            case SETRXAGCTOP:
            {
                double max_agc = atof((const char*)(message+1));
                SetRXAAGCTop(current_rx, max_agc);
            }
                break;

            case ENABLERXEQ:
                SetRXAEQRun(current_rx, message[1]);
                break;

            case SETRXEQPRO:
            {
                double freq[11] = {0.0, 32.0, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
                double gain[11];

                sscanf((const char*)(message+2), "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                       &gain[0], &gain[1], &gain[2], &gain[3], &gain[4], &gain[5], &gain[6], &gain[7], &gain[8], &gain[9], &gain[10]);
                SetRXAEQProfile(current_rx, message[1], freq, gain);
            }
                break;

            case SETTXEQPRO:
            {
                if (current_tx < 0) break;

                double freq[11] = {0.0, 32.0, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
                double gain[11];

                sscanf((const char*)(message+2), "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                       &gain[0], &gain[1], &gain[2], &gain[3], &gain[4], &gain[5], &gain[6], &gain[7], &gain[8], &gain[9], &gain[10]);
                SetTXAEQProfile(current_tx, message[1], freq, gain);
            }
                break;

            case ENABLETXEQ:
                SetTXAEQRun(current_tx, message[1]);
                break;

            case SETTXALCATTACK:
            {
                int attack = 0;
                sscanf((const char*)(message+1), "%d", &attack);
                SetTXAALCAttack(current_tx, attack);
            }
                break;

            case SETTXALCDECAY:
            {
                int decay = 0;
                sscanf((const char*)(message+1), "%d", &decay);
                SetTXAALCDecay(current_tx, decay);
            }
                break;

            case SETTXALCMAXGAIN:
            {
                double maxgain = 0.0f;
                sscanf((const char*)(message+1), "%lf", &maxgain);
                SetTXAALCMaxGain(current_tx, maxgain);
            }
                break;

            case SETFPS:
            {
                int samp, fps;

                sscanf((const char*)(message+1), "%d,%d", &samp, &fps);
                sem_wait(&bufferevent_semaphore);
                if (item->client_type != CONTROL)
                {
                    current_item->samples = samp;
                    current_item->fps = fps;
                }
                else
                {
                    item->samples = samp;
                    item->fps = fps;
                }
                initAnalyzer(current_rx, item->samples, fps, 512);
                if (current_tx >= 0)
                    initAnalyzer(current_tx, item->samples, fps, 2048);
                sem_post(&bufferevent_semaphore);
                sdr_log(SDR_LOG_INFO, "Spectrum fps set to = '%d'  Samples = '%d'\n", item->fps, item->samples);
            }
                break;

            case SETFREQ:
                hwSetFrequency(message[1], atoll((const char*)(message+2)));
                     fprintf(stderr, "Set frequency: %lld\n", atoll((const char*)(message+2)));
                break;

            case SETMODE:
            {
                int mode;
                current_rx = channels[message[1]].receiver;
                current_tx = channels[message[1]].transmitter;
                mode = message[2];
                lastMode = mode;
                fprintf(stderr, "************** Mode change: %d *******************\n", mode);
                switch (mode)
                {
                case USB:
                    SetRXAMode(current_rx, RXA_USB);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_USB);
                    RXASetPassband(current_rx, 150, 2850);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, 150, 2850);
                    sdr_log(SDR_LOG_INFO, "Mode set to USB\n");
                    break;
                case LSB:
                    SetRXAMode(current_rx, RXA_LSB);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_LSB);
                    RXASetPassband(current_rx, -2850, -150);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, -2850, -150);
                    sdr_log(SDR_LOG_INFO, "Mode set to LSB\n");
                    break;
                case AM:
                    SetRXAMode(current_rx, RXA_AM);
                    fprintf(stderr, "Mode: AM\n");
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_AM);
                    RXASetPassband(current_rx, -2850, 2850);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, -2850, 2850);
                    break;
                case SAM:
                    SetRXAMode(current_rx, RXA_SAM);
                    fprintf(stderr, "Mode: SAM\n");
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_SAM);
                    RXASetPassband(current_rx, -2850, 2850);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, -2850, 2850);
                    break;
                case FM:
                    SetRXAMode(current_rx, RXA_FM);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_FM);
                    RXASetPassband(current_rx, -4800, 4800);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, -4800, 4800);
                    break;
                case DSB:
                    SetRXAMode(current_rx, RXA_DSB);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_DSB);
                    RXASetPassband(current_rx, 150, 2850);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, 150, 2850);
                    break;
                case CWU:
                    SetRXAMode(current_rx, RXA_CWU);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_CWU);
                    RXASetPassband(current_rx, 150, 2850);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, 150, 2850);
                    break;
                case CWL:
                    SetRXAMode(current_rx, RXA_CWL);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_CWL);
                    RXASetPassband(current_rx, -2850, -150);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, -2850, -150);
                    break;
                case DIGU:
                    SetRXAMode(current_rx, RXA_DIGU);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_DIGU);
                    RXASetPassband(current_rx, 150, 2850);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, 150, 2850);
                    break;
                case DIGL:
                    SetRXAMode(current_rx, RXA_DIGL);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_DIGL);
                    RXASetPassband(current_rx, -2850, -150);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, -2850, -150);
                    break;
                case SPEC:
                    SetRXAMode(current_rx, RXA_SPEC);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_SPEC);
                    RXASetPassband(current_rx, 150, 2850);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, 150, 2850);
                    break;
                case DRM:
                    SetRXAMode(current_rx, RXA_DRM);
                    if (current_tx >= 0)
                        SetTXAMode(current_tx, TXA_DRM);
                    RXASetPassband(current_rx, 150, 2850);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, 150, 2850);
                    break;
                default:
                    RXASetPassband(current_rx, -4800, 4800);
                    if (current_tx >= 0)
                        SetTXABandpassFreqs(current_tx, -4800, 4800);
                }
            }
                break;

            case SETFILTER:
            {
                int low, high;
                current_rx = channels[message[1]].receiver;
                sscanf((const char*)(message+2), "%d,%d", &low, &high);
                printf("RX: %d  Low: %d   High: %d\n", current_rx, low, high);
                RXASetPassband(current_rx, (double)low, (double)high);
            }
                break;

            case SETENCODING:
            {
                int enc;
                enc = message[1];
                /* This used to force to 0 on error, now it leaves unchanged */
                if (enc >= 0 && enc <= 2)
                {
                    sem_wait(&audiostream_sem);
                    audiostream_conf.encoding = enc;
                    audiostream_conf.age++;
                    sem_post(&audiostream_sem);
                }
                sdr_log(SDR_LOG_INFO, "encoding changed to %d\n", enc);
            }
                break;

            case STARTAUDIO:
            {
                int ntok, bufsize, rate, channels, micEncoding;
                if (item->client_type != CONTROL)
                {
                    continue;
                }

                audio_stream_init(0);
                /* FIXME: this is super racy */

                bufsize = AUDIO_BUFFER_SIZE;
                rate = 8000;
                channels = 1;
                micEncoding = 0;
                ntok = sscanf((const char*)(message+1), "%d,%d,%d,%d", &bufsize, &rate, &channels, &micEncoding);

                if (ntok >= 1)
                {
                    /* FIXME: validate */
                    /* Do not vary buffer size according to buffer size setting from client
                   as it causes problems when the buffer size set by primary is smaller
                   then slaves */
                    if (bufsize < AUDIO_BUFFER_SIZE)
                        bufsize = AUDIO_BUFFER_SIZE;
                    else
                        if (bufsize > 32000)
                            bufsize = 32000;
                }
                if (ntok >= 2)
                {
                    if (rate != 8000 && rate != 48000)
                    {
                        sdr_log(SDR_LOG_INFO, "Invalid audio sample rate: %d\n", rate);
                        rate = 8000;
                    }
                }
                if (ntok >= 3)
                {
                    if (channels != 1 && channels != 2)
                    {
                        sdr_log(SDR_LOG_INFO, "Invalid audio channels: %d\n", channels);
                        channels = 1;
                    }
                }
                if (ntok >= 4)
                {
                    if (micEncoding != MIC_ENCODING_ALAW)
                    {
                        sdr_log(SDR_LOG_INFO, "Invalid mic encoding: %d\n", micEncoding);
                        micEncoding = MIC_ENCODING_ALAW;
                    }
                }

                sem_wait(&audiostream_sem);
                audiostream_conf.bufsize = bufsize;
                audiostream_conf.samplerate = rate;
                hw_set_src_ratio();
                audiostream_conf.channels = channels;
                audiostream_conf.micEncoding = micEncoding;
                audiostream_conf.age++;
                sem_post(&audiostream_sem);

                sdr_log(SDR_LOG_INFO, "starting audio stream at rate %d channels %d bufsize %d encoding %d micEncoding %d\n",
                        rate, channels, bufsize, encoding, micEncoding);

                //audio_stream_reset();
                sem_wait(&audio_bufevent_semaphore);
                send_audio = 1;
                sem_post(&audio_bufevent_semaphore);
            }
                break;

            case STOPAUDIO:
                break;

            case SETPAN:
                SetRXAPanelPan(current_rx, atof((const char*)(message+1)));
                break;

            case SETANFVALS:
            {
                int taps, delay;
                double gain, leakage;

                if (sscanf((const char*)(message+1), "%d,%d,%f,%f", &taps, &delay, (float*)&gain, (float*)&leakage) != 4)
                    goto badcommand;

                SetRXAANFVals(current_rx, taps, delay, gain, leakage);
            }
                break;

            case SETANF:
                SetRXAANFRun(current_rx, message[1]);
                break;

            case SETNRVALS:
            {
                int taps, delay;
                double gain, leakage;

                if (sscanf((const char*)(message+1), "%d,%d,%f,%f", &taps, &delay, (float*)&gain, (float*)&leakage) != 4)
                    goto badcommand;

                SetRXAANRVals(current_rx, taps, delay, gain, leakage);
            }
                break;

            case SETNR:
                SetRXAANRRun(current_rx, message[1]);
                break;

            case SETNB:
                SetEXTANBRun(current_rx, message[1]);
                //    bUseNB = message[1];
                break;

            case SETNB2:
                SetEXTNOBRun(current_rx, message[1]);
                bUseNB2 = message[1];
                break;

            case SETNBVAL:
            {
                double thresh;
                sscanf((const char*)(message+1), "%lf", &thresh);
                SetEXTNOBThreshold(current_rx, thresh);
            }
                break;

            case SETSQUELCHVAL:
                SetRXAAMSQThreshold(current_rx, atof((const char*)(message+1)));
                break;

            case SETSQUELCHSTATE:
                SetRXAAMSQRun(current_rx, message[1]);
                break;

            case SETWINDOW:
                SetRXABandpassWindow(current_rx, atoi((const char*)(message+1)));
                if (current_tx > -1)
                    SetTXABandpassWindow(current_tx, atoi((const char*)(message+1)));
                break;

            case SETCLIENT:
                sdr_log(SDR_LOG_INFO, "Client is %s\n", (const char*)(message+1));
                break;

            case SETRXOUTGAIN:
                SetRXAPanelGain1(current_rx, (double)atoi((const char*)(message+1))/100.0);
                break;

            case SETMICGAIN:
            {
                double gain;

                if (current_tx == -1) break;

                if (sscanf((const char*)(message+1), "%lf", (double*)&gain) > 1)
                    goto badcommand;

                SetTXAPanelGain1(current_tx, pow(10.0, gain / 20.0));
                fprintf(stderr, "Mic gain: %lf\n", pow(10.0, gain / 20.0));
            }
                break;

            case SETSAMPLERATE:
            {
                if (sscanf((const char*)(message+1), "%ld", (long*)&sample_rate) > 1)
                    goto badcommand;
                SetChannelState(current_rx, 0, 1);
                //    SetInputSampleratecurrent_rx, sample_rate);
                //    SetEXTANBSamplerate current_rx, sample_rate);
                //    SetEXTNOBSamplerate current_rx, sample_rate);
                hwSetSampleRate(sample_rate);
                setSpeed(sample_rate);
                SetChannelState(current_rx, 1, 0);
            }
                break;

            case SETTXAMCARLEV:
            {
                double level;
                char user[20];
                char pass[20];

                if (current_tx == -1) break;

                if (sscanf((const char*)(message+1), "%lf %s %s", (double*)&level, user, pass) > 3)
                    goto badcommand;

                SetTXAAMCarrierLevel(current_tx, level * 10.0);
                fprintf(stderr, "AM carrier level: %lf\n", level * 10.0);
            }
                break;

            case SETRXBPASSWIN:
                SetRXABandpassWindow(current_rx, atoi((const char*)(message+1)));
                break;

            case SETTXBPASSWIN:
                if (current_tx > -1)
                    SetTXABandpassWindow(current_tx, atoi((const char*)(message+1)));
                break;

            case MOX:
            {
                char user[20];
                char pass[20];

                if (current_tx == -1) break;

                if (sscanf((const char*)(message+1), "%d %s %s", &mox, user, pass) > 3)
                    goto badcommand;


                if (mox)
                {
                    SetChannelState(current_rx, 0, 1);
                    SetChannelState(current_tx, 1, 0);
                    hwSetMox(mox);
                }
                else
                {
                    hwSetMox(mox);
                    SetChannelState(current_tx, 0, 1);
                    SetChannelState(current_rx, 1, 0);
                }
                sdr_log(SDR_LOG_INFO, "Mox set to: %d for tx channel %d\n", mox, current_tx);
            }
                break;

            default:
                fprintf(stderr, "READCB: Unknown command. 0x%02X\n", message[0]);
                break;
            }
        }
        continue;
badcommand:
        sdr_log(SDR_LOG_INFO, "Invalid command: %d  '%s'\n", message[0], (const char*)(message+1));
    }  // end main WHILE loop
} // end readcb


void client_set_samples(char* client_samples, float* samples, int size)
{
    spectrum spec;
    int i,j=0;
    float extras = 0.0f;
    int offset;
    float rotated_samples[size];

    if (size != 2000) {printf("Spectrum len: %d\n", size); fflush(stdout);}
    spec.length = size;
    spec.radio_id = channels[current_channel].radio_id;
    spec.rx = current_rx;
    spec.fwd_pwr = txfwd;
    spec.rev_pwr = txref;
    spec.rx_meter = meter;
    spec.sample_rate = sampleRate;
    spec.lo_offset = (float)LO_offset;

    offset = (float)LO_offset * (float)size / (float)sampleRate;
    if (LO_offset == 0.0)
    {
#pragma omp parallel for schedule(static) private(i,j)
        for (i = 0; i < size; i++)
        {
            j = i - offset;
            if (j < 0) j += size;
            if (j > size) j -= size;
            rotated_samples[i] = samples[j];
        }
    }
    else
    {
#pragma omp parallel for schedule(static) private(i)
        for (i = 0; i < size; i++)
        {
            rotated_samples[i] = samples[i];
        }
    };

    if (mox)
    {
        extras = -82.62103F;
    }
    else
    {
        //      extras = displayCalibrationOffset;
    }
#pragma omp parallel shared(size, samples, client_samples) private(i, j)
    {
#pragma omp for schedule(static)
        for (i=0;i<size;i++)
        {
            client_samples[i+sizeof(spec)] = (char)-(rotated_samples[i]+extras);
        }
    }
    memcpy(client_samples, (char*)&spec, sizeof(spec));
} // end client_set_samples


void client_set_wb_samples(char* client_samples, float* samples, int size)
{
    spectrum spec;
    int i = 0;
    float extras = 0.0f;

 //   if (size != 512) {printf("Wideband spectrum len: %d\n", size); fflush(stdout);}
    spec.length = (unsigned short)size;
    spec.radio_id = channels[current_channel].radio_id;
    spec.rx = current_rx;
    spec.sample_rate = sampleRate;


#pragma omp parallel shared(size, samples, client_samples) private(i)
    {
#pragma omp for schedule(static)
        for (i=0;i<size;i++)
        {
            client_samples[i+sizeof(spec)] = (char)-(samples[i]+extras);
        }
    }
    memcpy(client_samples, (char*)&spec, sizeof(spec));
} // end client_set_wb_samples


#ifdef old
void answer_question(char *message, char *clienttype, struct bufferevent *bev)
{
    // reply = 4LLqqq:aaaa LL= length (limit 99 + header 3) followed by question : answer
    char *reply;
    char answer[101] = "xx";
    unsigned short length;
    int txcfg = TXNONE;

    memset(answer, 0, sizeof(answer));
    answer[0] = '4';
    answer[1] = 1;

    if (message[0] == QDSPVERSION)
    {
        answer[2] = QDSPVERSION;
        strcat(answer, version);
    }
    else
        if (message[0] == QSERVER)
        {
            answer[2] = QSERVER;
            strcat(answer, servername);
            if (txcfg == TXNONE)
            {
                strcat(answer, " N");
            }
            else
                if (txcfg == TXPASSWD)
                {
                    strcat(answer, " P");
                }
                else
                {  // must be TXALL
                    strcat(answer, " Y");
                }
        }
        else
            if (message[0] == QMASTER)
            {
                answer[2] = QMASTER;
                strcat(answer, clienttype);
            }
            else
                if (message[0] == QINFO)
                {
                    answer[2] = QINFO;
                    if (strcmp(clienttype, "slave") == 0)
                    {
                        strcat(answer, ";s;0");
                    }
                    else
                    {
                        strcat(answer, ";s;1");
                    }
                    strcat(answer,";f;");
                    char f[50];
                    sprintf(f,"%lld;m;",lastFreq);
                    strcat(answer,f);
                    char m[50];
                    sprintf(m,"%d;z;",lastMode);
                    strcat(answer,m);
                    char z[50];
                    sprintf(z,"%d;l;", zoom);
                    strcat(answer,z);
                    char l[50];
                    sprintf(l,"%d;r;", low);       // Rx filter low
                    strcat(answer,l);              // Rx filter high
                    char h[50];
                    sprintf(h,"%d;", high);
                    strcat(answer,h);
                }
                else
                    if (message[0] == QCANTX)
                    {
                        //   char delims[] = "#";
                        char *result = NULL;
                        //   result = strtok_r( message, delims, &safeptr ); //returns q-cantx
                        //   if ( result != NULL )
                        //       result = strtok_r( NULL, delims, &safeptr ); // this should be call/user
                        if ( result != NULL )
                        {
                            //             if (chkFreq(result,  lastFreq , lastMode) == 0)
                            //             {
                            //                 strcat(answer,"q-cantx:Y");
                            //             }
                            //             else
                            {
                                strcat(answer,"N");
                            }
                        }
                        else
                        {
                            strcat(answer,"N");
                        }
                    }
                    else
                        if (message[0] == QLOFFSET)
                        {
                            answer[2] = QLOFFSET;
                            char p[50];
                            sprintf(p, "%f;", LO_offset);
                            strcat(answer, p);
                            //fprintf(stderr,"q-loffset: %s\n",answer);
                        }
                        else
                            if (message[0] == QCOMMPROTOCOL1)
                            {
                                answer[2] = QCOMMPROTOCOL1;
                                strcat(answer, "Y");
                            }
                            else
                                if (strstr(message, "OK"))
                                {
                                    printf("RET: [%s]\n", message);
                                    strcat(answer, message);
                                }
                                else
                                    if (message[0] == STARHARDWARE)
                                    {
                                        strcat
                                    }
                                    else
                                    {
                                        fprintf(stderr,"Unknown question: %s\n",message);
                                        return;
                                    }

    answer[0] = '4';  //ANSWER_BUFFER 0x52
    length = strlen(answer) - 3;
    if (length > 99)
    {
        fprintf(stderr,"Oversize reply!!: %s = %u\n",message, length);
        return;
    }

    answer[1] = length & 0xFF;

    reply = (char *) malloc(length+3);		// need to include the terminating null
    memcpy(reply, answer, length+3);
    bufferevent_write(bev, reply, strlen(answer));

    free(reply);
} // end answer_question
#endif

void initAnalyzer(int disp, int pixels, int frame_rate, int blocksize)
{
    //maximum number of frames of pixels to average
    //   int frame_rate = 15;
    double tau = 0.12;
    int data_type = 1;
    int n_pixout = 1;
    int overlap = 0;
    int fft_size = 8192; //32768;
    int sample_rate = 48000;
    int clip = 0;
    int stitches = 1;
    double z_slider = 0.0;
    int span_clip_l = 0;
    double pan_slider = 0.5;
    int span_clip_h = 0;
    const double KEEP_TIME = 0.1;
    int max_w;
    //    double freq_offset = 12000.0;
    int spur_eliminationtion_ffts = 1;
    static int h_flip[] = {0};
    //   int blocksize = 512;
    int window_type = 5;
    double kaiser_pi = 14.0;
    int calibration_data_set = 0;
    double span_min_freq = 0.0;
    double span_max_freq = 0.0;

    const int MAX_AV_FRAMES = 60;

    //compute multiplier for weighted averaging
    double avb = exp(-1.0 / (frame_rate * tau));
    //compute number of frames to average for window averaging
    int display_average = MAX(2, (int)MIN(MAX_AV_FRAMES, frame_rate * tau));

    //no spur elimination => only one spur_elim_fft and it's spectrum is flipped
    //    h_flip[0] = 0;

    int low = 0;
    int high = 0;
    double bw_per_subspan = 0.0;

    switch (data_type)
    {
    case 0:     //real fft - in case we want to use for wideband data in the future
    {

        break;
    }
    case 1:     //complex fft
    {
        //fraction of the spectrum to clip off each side of each sub-span
        const double CLIP_FRACTION = 0.017;

        //set overlap as needed to achieve the desired frame_rate
        overlap = (int)MAX(0.0, ceil(fft_size - (double)sample_rate / (double)frame_rate));

        //clip is the number of bins to clip off each side of each sub-span
        clip = (int)floor(CLIP_FRACTION * fft_size);

        //the amount of frequency in each fft bin (for complex samples) is given by:
        double bin_width = (double)sample_rate / (double)fft_size;

        //the number of useable bins per subspan is
        int bins_per_subspan = fft_size - 2 * clip;

        //the amount of useable bandwidth we get from each subspan is:
        bw_per_subspan = bins_per_subspan * bin_width;

        //the total number of bins available to display is:
        int bins = stitches * bins_per_subspan;

        //apply log function to zoom slider value
        double zoom_slider = log10(9.0 * z_slider + 1.0);

        //limits how much you can zoom in; higher value means you zoom more
        const double zoom_limit = 100;

        int width = (int)(bins * (1.0 - (1.0 - 1.0 / zoom_limit) * zoom_slider));

        //FSCLIPL is 0 if pan_slider is 0; it's bins-width if pan_slider is 1
        //FSCLIPH is bins-width if pan_slider is 0; it's 0 if pan_slider is 1
        span_clip_l = (int)floor(pan_slider * (bins - width));
        span_clip_h = bins - width - span_clip_l;
        /*
                        if (Display.RX1DSPMode == DSPMode.DRM)
                        {
                            //Apply any desired frequency offset
                            int bin_offset = (int)(freq_offset / bin_width);
                            if ((span_clip_h -= bin_offset) < 0) span_clip_h = 0;
                            span_clip_l = bins - width - span_clip_h;
                        }
*/
        //As for the low and high frequencies that are being displayed:
        low = -(int)((double)stitches / 2.0 * bw_per_subspan - (double)span_clip_l * bin_width + bin_width / 2.0);
        high = +(int)((double)stitches / 2.0 * bw_per_subspan - (double)span_clip_h * bin_width - bin_width / 2.0);
        //Note that the bin_width/2.0 factors are included because the complex FFT has one more negative output bin
        //  than positive output bin.
        max_w = fft_size + (int)MIN(KEEP_TIME * sample_rate, KEEP_TIME * fft_size * frame_rate);
        break;
    }
    }

    fprintf(stderr, "Disp: %d  avb: %f  davg: %d  low: %d  high: %d  clip: %d  sclip_l: %d  sclip_h: %d  overlap: %d  maxw: %d\n", disp, avb, display_average, low, high, clip, span_clip_l, span_clip_h, overlap, max_w);
    SetAnalyzer(disp,
                n_pixout,
                spur_eliminationtion_ffts,
                data_type,
                h_flip,
                fft_size,
                blocksize,
                window_type,
                kaiser_pi,
                overlap,
                clip,
                span_clip_l,
                span_clip_h,
                pixels,
                stitches,
                calibration_data_set,
                span_min_freq,
                span_max_freq,
                max_w);
    fprintf(stderr, "Analyzer created.\n");
} // end initAnalyzer


void enable_wideband(bool enable)
{
    int pixels = 512;
    int ch = WIDEBAND_CHANNEL;
    int result;


    if (enable)
    {
        XCreateAnalyzer(ch, &result, 262144, 1, 1, "");
        if (result != 0)
        {
            printf("XCreateAnalyzer channel=%d failed: %d\n", ch, result);
        }
        else
        {
            widebandInitAnalyzer(ch, pixels);
        }

        SetDisplayDetectorMode(ch, 0, DETECTOR_MODE_AVERAGE/*display_detector_mode*/);
        SetDisplayAverageMode(ch, 0,  AVERAGE_MODE_LOG_RECURSIVE/*display_average_mode*/);
        wideband_enabled = true;
    }
    else
    {
        wideband_enabled = false;
        DestroyAnalyzer(ch);
    }
} // end enable_wideband


void widebandInitAnalyzer(int disp, int pixels)
{
    //maximum number of frames of pixels to average
    //   int frame_rate = 15;
    int frame_rate = 10;
//    double tau = 0.12;
    int data_type = 1;
    int n_pixout = 1;
    int overlap = 0;
    int fft_size = 16384;
    int blocksize = 16384;
    int sample_rate = 48000;
    int clip = 0;
    int stitches = 1;
    int span_clip_l = 0;
    int span_clip_h = 0;
    const double KEEP_TIME = 0.1;
    int max_w;
    int spur_eliminationtion_ffts = 1;
    static int h_flip[] = {0};
    int window_type = 4;
    double kaiser_pi = 14.0;
    int calibration_data_set = 0;
    double span_min_freq = 0.0;
    double span_max_freq = 0.0;

    max_w = fft_size + (int)MIN(KEEP_TIME * (double)frame_rate, KEEP_TIME * (double)fft_size * (double)frame_rate);

//    fprintf(stderr, "Disp: %d  avb: %f  davg: %d  low: %d  high: %d  clip: %d  sclip_l: %d  sclip_h: %d  overlap: %d  maxw: %d\n", disp, avb, display_average, low, high, clip, span_clip_l, span_clip_h, overlap, max_w);
    SetAnalyzer(disp,
                n_pixout,
                spur_eliminationtion_ffts,
                data_type,
                h_flip,
                fft_size,
                blocksize,
                window_type,
                kaiser_pi,
                overlap,
                clip,
                span_clip_l,
                span_clip_h,
                pixels*2,
                stitches,
                calibration_data_set,
                span_min_freq,
                span_max_freq,
                max_w);
    fprintf(stderr, "Wideband analyzer created.\n");
} // end windbandInitAnalyzer


void printversion()
{
    fprintf(stderr,"dspserver string: %s\n", version);
}
