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

/* Modifications made by Rick Schnicker KD0OSS 2020,2021,2022  */

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
#include "dsp.h"
#include "hardware.h"
#include "audiostream.h"
#include "main.h"
#include "wdsp.h"
#include "buffer.h"
#include "G711A.h"
#include "util.h"
#include "web.h"

#define true  1
#define false 0

#define BASE_PORT 8000
#define BASE_PORT_SSL 9000

#define SAMPLE_BUFFER_SIZE 4096

#define MSG_SIZE 64
#define MIC_MSG_SIZE 518 //64
#define MIC_ALAW_BUFFER_SIZE 512 //58

#define SPECTRUM_TIMER_NS (20*1000000L)


extern int8_t active_receivers;
extern int8_t active_transmitters;

//extern RADIO *radio[5];
CHANNEL channels[MAX_CHANNELS];

short int connected_radios;

long sample_rate = 48000L;
//int  TX_BUFFER_SIZE = 512;

static pthread_t mic_audio_client_thread_id,
                 client_thread_id,
                 audio_client_thread_id,
                 spectrum_client_thread_id,
                 wideband_client_thread_id,    
                 rx_audio_thread_id,
                 tx_thread_id[MAX_CHANNELS];

static int port = BASE_PORT;
static int port_ssl = BASE_PORT_SSL;

struct        sockaddr_in radio_audio_addr[MAX_CHANNELS];
socklen_t     radio_audio_length[MAX_CHANNELS];
int           radio_audio_socket[MAX_CHANNELS];

static bool rx_audio_enabled[MAX_CHANNELS];  //FIXME:This doesn't need to be MAX_CHANNELS quantity.

int zoom = 0;
int low, high;            // filter low/high

/*
// bits_per_frame is now a variable
#undef BITS_SIZE
#define BITS_SIZE   ((bits_per_frame + 7) / 8)

#define MIC_NO_OF_FRAMES 4
#define MIC_BUFFER_SIZE  (BITS_SIZE*MIC_NO_OF_FRAMES)
#define MIC_ALAW_BUFFER_SIZE 512 //58

#if MIC_BUFFER_SIZE > MIC_ALAW_BUFFER_SIZE
static unsigned char mic_buffer[MIC_BUFFER_SIZE];
#else
static unsigned char mic_buffer[MIC_ALAW_BUFFER_SIZE];
#endif
*/


// For timer based spectrum data (instead of sending one spectrum frame per getspectrum command from clients)
timer_t spectrum_timerid[MAX_CHANNELS];

static timer_t wideband_timerid[5];
float *widebandBuffer[5];

int data_in_counter = 0;
int iq_buffer_counter = 0;

int encoding = 0;

int send_audio = 0;

sem_t audio_bufevent_semaphore,
      spec_bufevent_semaphore,
      wb_bufevent_semaphore,
      wideband_semaphore,
      bufferevent_semaphore,
      mic_semaphore,
      rx_audio_semaphore,
      spectrum_semaphore,
      wb_iq_semaphore,
      iq_semaphore;


// Mic_audio_stream is the HEAD of a queue for encoded Mic audio samples from QtRadio
TAILQ_HEAD(, audio_entry) Mic_audio;
      
// Mic_audio client list
TAILQ_HEAD(, _client_entry) Mic_audio_client_list;

// rx_audio client list
TAILQ_HEAD(, _memory_entry) rx_audio_client_list;

// Client_list is the HEAD of a queue of connected clients
TAILQ_HEAD(, _client_entry) Client_list;

// Spectrum_client_list is the HEAD of a queue of connected spectrum clients
TAILQ_HEAD(, _client_entry) Spectrum_client_list;

// Wideband_client_list is the HEAD of a queue of connected spectrum clients
TAILQ_HEAD(, _client_entry) Wideband_client_list;

// Audio_client_list is the HEAD of a queue of connected receive audio clients
TAILQ_HEAD(, _client_entry) Audio_client_list;

void* client_thread(void* arg);
void* spectrum_client_thread(void* arg);
void* audio_client_thread(void* arg);
void* tx_thread(void* arg);
void* rx_audio_thread(void* arg);
void* mic_audio_client_thread(void* arg);
void* wideband_client_thread(void* arg);

void client_set_samples(CHANNEL *channel, char *client_samples, float* samples, int size);
void client_set_wb_samples(CHANNEL *channel, char *client_samples, float* samples, int size);

void do_accept_command(evutil_socket_t listener, short event, void *arg);
void command_readcb(struct bufferevent *bev, void *ctx);
void command_writecb(struct bufferevent *bev, void *ctx);

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




void printversion()
{
    fprintf(stderr,"dspserver string: %s\n", version);
} // end printversion


void audio_stream_init(int receiver)
{
    sem_init(&audiostream_sem, 0, 1);
    init_alaw_tables();
} //end audio_stream_init


void audio_stream_queue_add(unsigned char *buffer, int length)
{
    client_entry *client_item;
    memory_entry *memory_item;

    sem_wait(&audio_bufevent_semaphore);
    //    if (send_audio)
    {
        TAILQ_FOREACH(client_item, &Audio_client_list, entries)
        {
            bufferevent_write(client_item->bev, buffer, length);
        }
        memory_item = malloc(sizeof(*memory_item));
        memory_item->memory = malloc(512);
        memcpy(memory_item->memory, (char*)buffer, 512);
        TAILQ_INSERT_TAIL(&rx_audio_client_list, memory_item, entries);
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
void Mic_stream_queue_add(int8_t channel, unsigned char *mic_buffer)
{
    unsigned char *bits;
    struct audio_entry *item;

    if (audiostream_conf.micEncoding == MIC_ENCODING_ALAW)
    {
        bits = malloc(MIC_ALAW_BUFFER_SIZE);
        memcpy(bits, mic_buffer, MIC_ALAW_BUFFER_SIZE);
        item = malloc(sizeof(*item));
        item->buf = bits;
        item->channel = channel;
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


void server_init(int receiver)
{
    int rc;

    panadapterMode = PANADAPTER;  // KD0OSS
    bUseNB = false;
    bUseNB2 = false;
    rxMeterMode = RXA_S_AV; // KD0OSS
    txMeterMode = TXA_OUT_AV; // KD0OSS
    LO_offset = 0.0f;
    active_channels = 0;
    connected_radios = 0;
    active_receivers = 0;
    active_transmitters = 0;

    memset(widebandBuffer, 0, sizeof(widebandBuffer));

    // initialize channel structure
    for (int i=0;i<MAX_CHANNELS;i++)
    {
        channels[i].id = i;
        channels[i].radio.radio_id = -1;
        channels[i].dsp_channel = -1;
	    channels[i].frequency = 0;
        channels[i].index = -1;
        channels[i].radio.bandscope_capable = 0;
        channels[i].isTX = false;
        channels[i].radio.mox = false;
        channels[i].enabled = false;
        channels[i].spectrum.samples = NULL;
        rx_audio_enabled[i] = false;
    }

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
    TAILQ_INIT(&Wideband_client_list);
    TAILQ_INIT(&Mic_audio_client_list);
    TAILQ_INIT(&Mic_audio);
    TAILQ_INIT(&rx_audio_client_list);
    
    sem_init(&mic_semaphore, 0, 1);
    sem_init(&bufferevent_semaphore, 0, 1);
    sem_init(&spec_bufevent_semaphore, 0, 1);
    sem_init(&audio_bufevent_semaphore, 0, 1);
    sem_init(&spectrum_semaphore, 0, 1);
    sem_init(&wideband_semaphore, 0, 1);
    sem_init(&wb_bufevent_semaphore, 0, 1);
    sem_init(&iq_semaphore, 0, 1);
    sem_init(&wb_iq_semaphore, 0, 1);
    sem_init(&rx_audio_semaphore, 0, 1);
    
    signal(SIGPIPE, SIG_IGN);

//    spectrum_timer_init();
//    wideband_timer_init();

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

    rc = pthread_create(&mic_audio_client_thread_id, NULL, mic_audio_client_thread, NULL);
    if (rc != 0)
        fprintf(stderr, "pthread_create failed on mic_audio_client_thread: rc=%d\n", rc);
    else
        rc = pthread_detach(mic_audio_client_thread_id);

    rc = pthread_create(&spectrum_client_thread_id, NULL, spectrum_client_thread, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create failed on spectrum_client_thread: rc=%d\n", rc);
    }
    else
        rc = pthread_detach(spectrum_client_thread_id);

    rc = pthread_create(&wideband_client_thread_id, NULL, wideband_client_thread, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create failed on wideband_client_thread: rc=%d\n", rc);
    }
    else
        rc = pthread_detach(wideband_client_thread_id);
    
    //   init_http_server();
} // end server_init


void tx_init(void)
{
    int rc;

    for (int i=0;i<MAX_CHANNELS;i++)
    {
        if (!channels[i].enabled || channels[i].dsp_channel < 0 || !channels[i].isTX)
            continue;
        rc = pthread_create(&tx_thread_id[i], NULL, tx_thread,  (void*)(intptr_t)i);
        if (rc != 0)
            fprintf(stderr, "pthread_create failed on tx_thread: %d  rc=%d\n", i, rc);
        else
            rc = pthread_detach(tx_thread_id[i]);
    }
} // end tx_init


void *tx_thread(void *arg)
{
    struct        audio_entry *item = NULL;
    int8_t        ch = (intptr_t)arg;
    int           tx_buffer_counter = 0;
    int           j;
    unsigned char abuf[MIC_ALAW_BUFFER_SIZE];
    struct        sockaddr_in radio_mic_addr;
    socklen_t     radio_mic_length = sizeof(radio_mic_addr);
    int           radio_mic_socket = -1;
    MIC_BUFFER    radio_mic_buffer;
    float        *data_out;
    int           TX_BUFFER_SIZE = 512;

    if (channels[ch].protocol == 1)
        TX_BUFFER_SIZE = 1024;

    double        tx_IQ[TX_BUFFER_SIZE*8];
    double        mic_buf[TX_BUFFER_SIZE*2];


    data_out = (float *)malloc(sizeof(float) * MIC_ALAW_BUFFER_SIZE * 2);

    sdr_log(SDR_LOG_INFO, "tx_thread STARTED\n");

    // create a socket to get mic audio from radio.
    radio_mic_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (radio_mic_socket < 0)
    {
        perror("tx_thread: create radio mic audio socket failed");
        exit(1);
    }

    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = 10;
    setsockopt(radio_mic_socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);

    memset(&radio_mic_addr, 0, radio_mic_length);
    radio_mic_addr.sin_family = AF_INET;
    radio_mic_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    radio_mic_addr.sin_port = htons(MIC_AUDIO_PORT); // + channels[ch].radio.radio_id);

    if (bind(radio_mic_socket, (struct sockaddr*)&radio_mic_addr, radio_mic_length) < 0)
    {
        perror("tx_init: bind socket failed for radio mic socket");
        exit(1);
    }

    fprintf(stderr, "tx_init: radio mic audio bound to port %d socket %d   Use radio mic: %d\n", ntohs(radio_mic_addr.sin_port), radio_mic_socket, radioMic);

    while (!getStopIQIssued())
    {
        sem_wait(&iq_semaphore);
        if (active_transmitters == 0)
        {
            sem_post(&iq_semaphore);
            break;
        }
        if (radio_mic_socket > -1)
        {
            int bytes_read = recvfrom(radio_mic_socket, (char*)&radio_mic_buffer, sizeof(radio_mic_buffer), 0, (struct sockaddr*)&radio_mic_addr, &radio_mic_length);
            if (bytes_read < 0)
            {
           //     perror("recvfrom socket failed for mic buffer");
             //   exit(1);
            }
            if (bytes_read <= 0)
            {
                sem_post(&iq_semaphore);
                usleep(100);
                continue;
            }
        //    fprintf(stderr, "F: %2.2f  R: %2.2f\n", radio_mic_buffer.fwd_pwr, radio_mic_buffer.rev_pwr);
            channels[ch].spectrum.fwd_pwr = radio_mic_buffer.fwd_pwr;
            channels[ch].spectrum.rev_pwr = radio_mic_buffer.rev_pwr;
        }
        sem_post(&iq_semaphore);

        sem_wait(&mic_semaphore);
        item = TAILQ_FIRST(&Mic_audio);
        sem_post(&mic_semaphore);
        if (item == NULL || item->channel != ch)
        {
            usleep(1000);
            continue;
        }
        else
        {
            if (channels[ch].radio.mox)
            {// fprintf(stderr, "Current TX: ch=%d  dsp=%d\n", ch, channels[ch].dsp_channel);
                memcpy((unsigned char*)abuf, (unsigned char*)&item->buf[0], MIC_ALAW_BUFFER_SIZE);
                for (j=0; j < MIC_ALAW_BUFFER_SIZE; j++)
                {
                    //  convert ALAW to PCM audio
                    data_out[j*2] = data_out[j*2+1] = (float)G711A_decode(abuf[j])/32767.0;

                    mic_buf[tx_buffer_counter*2] = (double)data_out[2*j];
                    mic_buf[tx_buffer_counter*2+1] = (double)data_out[2*j+1];
                    tx_buffer_counter++;
                    if (tx_buffer_counter >= TX_BUFFER_SIZE)
                    {
                        memset(tx_IQ, 0, sizeof(double)*(TX_BUFFER_SIZE*8));
                        if (!radioMic)
                        {
                            process_tx_iq_data(ch, mic_buf, tx_IQ);
                        }
                        else
                            if (radioMic)
                            {
                                process_tx_iq_data(ch, (double*)&radio_mic_buffer.data[0], tx_IQ);
                            }
                        // send Tx IQ to radio, buffer is interleaved.
                        hw_send((unsigned char*)tx_IQ, sizeof(tx_IQ), ch);
                        tx_buffer_counter = 0;
                    }
                }
            } // end if mox
            sem_wait(&mic_semaphore);
            TAILQ_REMOVE(&Mic_audio, item, entries);
            sem_post(&mic_semaphore);
            free(item->buf);
            free(item);
        } // end else item
        sem_post(&iq_semaphore);
    } // end while
    close(radio_mic_socket);
    free(data_out);
    fprintf(stderr, "tx_thread stopped.\n");
    return NULL;
} // end tx_thread


void* rx_audio_thread(void* arg)
{
    struct        _memory_entry *item = NULL;
    int8_t        ch = (intptr_t)arg;
    unsigned char abuf[512];
    short         samples[512];
    
    sdr_log(SDR_LOG_INFO, "rx_auido_thread STARTED\n");
    
    // create a socket to send radio audio from radio.
    radio_audio_socket[ch] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (radio_audio_socket[ch] < 0)
    {
        perror("rx_audio_thread: create radio audio socket failed");
        exit(1);
    }
    
    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = 10;
    setsockopt(radio_audio_socket[ch], SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);
    
    radio_audio_length[ch] = sizeof(radio_audio_addr[ch]);
    memset(&radio_audio_addr[ch], 0, radio_audio_length[ch]);
    radio_audio_addr[ch].sin_family = AF_INET;
    radio_audio_addr[ch].sin_addr.s_addr = htonl(INADDR_ANY);
    radio_audio_addr[ch].sin_port = htons(RX_AUDIO_PORT + channels[ch].radio.radio_id);
    
    if (bind(radio_audio_socket[ch], (struct sockaddr*)&radio_audio_addr[ch], radio_audio_length[ch]) < 0)
    {
        perror("rx_audio: bind socket failed for radio audio socket");
        exit(1);
    }
    
    fprintf(stderr, "rx_audio: radio audio bound to port %d socket %d   Use radio speaker: %d\n", ntohs(radio_audio_addr[ch].sin_port), radio_audio_socket[ch], radioMic);
    
    while (!getStopIQIssued())
    {
        sem_wait(&rx_audio_semaphore);
        item = TAILQ_FIRST(&rx_audio_client_list);
        sem_post(&rx_audio_semaphore);
        if (item == NULL)
        {
            usleep(1000);
            continue;
        }
        else
        {
            if (rx_audio_enabled[ch])
            {
                memcpy((unsigned char*)abuf, (unsigned char*)&item->memory[0], 512);
                for (int j=0; j < 256; j++)
                {
                    samples[j*2] = (short)abuf[j*2];
                    samples[j*2+1] = (short)abuf[j*2+1];
                }
                int bytes_written = sendto(radio_audio_socket[ch], (char*)&samples, sizeof(samples), 0, (struct sockaddr *)&radio_audio_addr[ch], radio_audio_length[ch]);
            }
            sem_wait(&rx_audio_semaphore);
            TAILQ_REMOVE(&rx_audio_client_list, item, entries);
            sem_post(&rx_audio_semaphore);
            free(item->memory);
            free(item);
        }
    } // end while
    close(radio_audio_socket[ch]);
    rx_audio_enabled[channels[ch].radio.radio_id] = false;
    fprintf(stderr, "rx_audio_thread stopped.\n");
    return NULL;
} // end rx_audio_thread


void start_rx_audio(int8_t channel)
{
    if (rx_audio_enabled[channels[channel].radio.radio_id]) return;

    int rc = pthread_create(&rx_audio_thread_id, NULL, rx_audio_thread, (void*)(intptr_t)channel);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create failed on rx_audio_thread: rc=%d\n", rc);
    }
    else
    {
        rc = pthread_detach(rx_audio_thread_id);
        rx_audio_enabled[channels[channel].radio.radio_id] = true;
    }
} // end start_rx_audio


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
    server.sin_port = htons(BASE_PORT);

    if (bind(serverSocket,(struct sockaddr *)&server,sizeof(server)) < 0)
    {
        perror("client bind");
        fprintf(stderr, "port = %d\n", BASE_PORT);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "client_thread: listening on TCP port %d\n", BASE_PORT);

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
    listener_event = event_new(base, serverSocket, EV_READ|EV_PERSIST, do_accept_command, (void*)base);
    event_add(listener_event, NULL);
#ifdef USE_SSL
    // add the ssl listener to event base
    listener = evconnlistener_new_bind(
                base, do_accept_command_ssl, (void *)ctx,
                LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE |
                LEV_OPT_THREADSAFE, 1024,
                (struct sockaddr *)&server_ssl, sizeof(server_ssl));

    sdr_log(SDR_LOG_INFO, "client_thread: listening on TCP port %d for ssl connection\n", port_ssl);
#endif
    // this will be an endless loop to service all the network events
    event_base_loop(base, 0);
#ifdef USE_SSL
    // if for whatever reason the Event loop terminates, cleanup
    evconnlistener_free(listener);
    thread_cleanup();
    SSL_CTX_free(ctx);
#endif
    close(serverSocket);
    fprintf(stderr, "command client thread stopped\n");
    return NULL;
} // end client_thread


void command_writecb(struct bufferevent *bev, void *ctx)
{/*
    struct audio_entry *item;
    client_entry *client_item;
    
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
} // end command_writecb


void command_readcb(struct bufferevent *bev, void *ctx)
{
    struct evbuffer *inbuf;
    client_entry    *current_item = NULL;
    unsigned char    message[MSG_SIZE];
    int              bytesRead = 0;
    bool             foundClient = false;

    
    client_entry *tmp_item;
    /* Only allow CONTROL client type to command dspserver.
     * If this client is not CONTROL type, we
     * will first determine whether it is allowed to execute the
     * command it is executing, and abort if it is not. */

    // locate the current_item for this client
    sem_wait(&bufferevent_semaphore);
    for (current_item = TAILQ_FIRST(&Client_list); current_item != NULL; current_item = tmp_item)
    {
        tmp_item = TAILQ_NEXT(current_item, entries);
        if (current_item->bev == bev)
        {
            foundClient = true;
            break;
        }
    }
    sem_post(&bufferevent_semaphore);
    if (current_item == NULL || !foundClient)
    {
        sdr_log(SDR_LOG_ERROR, "This client was not located");
        return;
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

        dsp_command(current_item, message);
    }  // end main WHILE loop
} // end command_readcb


void command_errorcb(struct bufferevent *bev, short error, void *ctx)
{
    client_entry *item;
    int client_count = 0;

    if ((error & BEV_EVENT_EOF) || (error & BEV_EVENT_ERROR))
    {
        /* connection has been closed, or error has occured, do any clean up here */
        /* ... */
     //   while (!getStopIQIssued()) usleep(1000);
        sem_wait(&bufferevent_semaphore);
        for (item = TAILQ_FIRST(&Client_list); item != NULL; item = TAILQ_NEXT(item, entries))
        {
            if (item->bev == bev)
            {
                char ipstr[16];
                inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
                sdr_log(SDR_LOG_INFO, "Client disconnection from %s:%d\n",
                        ipstr, ntohs(item->client.sin_port));
                shutdown_client_channels(item);
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
            for (int i=0;i<MAX_CHANNELS;i++)
            {
                if (channels[i].enabled)
                {
                    DestroyAnalyzer(channels[i].dsp_channel);
                    CloseChannel(channels[i].dsp_channel);
                    channels[i].enabled = false;
                }
            }
//            send_audio = 0;
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
} // end command_errorcb


void do_accept_command(evutil_socket_t listener, short event, void *arg)
{
    client_entry *item;
    struct event_base *base = arg;
    struct sockaddr_in ss;
    socklen_t slen = sizeof(ss);

    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0)
    {
        sdr_log(SDR_LOG_WARNING, "accept failed for command socket\n");
        return;
    }
    char ipstr[16];
    // add newly connected client to Client_list
    item = malloc(sizeof(*item));
    memset(item, 0, sizeof(*item));
    memcpy(&item->client, &ss, sizeof(ss));

    inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
    sdr_log(SDR_LOG_INFO, "Client command connection from %s:%d\n",
            ipstr, ntohs(item->client.sin_port));

    struct bufferevent *bev;
    evutil_make_socket_nonblocking(fd);
    evutil_make_socket_closeonexec(fd);
    bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
    bufferevent_setcb(bev, command_readcb, command_writecb, command_errorcb, NULL);
    bufferevent_setwatermark(bev, EV_READ, MSG_SIZE, 0);
    bufferevent_setwatermark(bev, EV_WRITE, 4096, 0);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    item->bev = bev;
    for (int i=0;i<MAX_CHANNELS;i++)
        item->client_type[i] = CONTROL;
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
} // end do_accept_command

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

    bufferevent_setcb(bev, command_readcb, command_writecb, command_errorcb, NULL);
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
} // end do_accept_command_ssl


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


/*****************************************************************************************************************
******************************************************************************************************************
*****************************************************************************************************************/

void spectrum_timer_init(int channel)
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
    sev.sigev_value.sival_int = channel;

    timer_create(CLOCK_REALTIME, &sev, &spectrum_timerid[channel]);
    timer_settime(spectrum_timerid[channel], 0, &value, NULL);
    fprintf(stderr, "spectrum timer for channel %d initialized\n", channel);
} // end spectrum_timer_init


void spectrum_timer_handler(union sigval usv)
{  // this is called every 20 ms
    int           flag = 0;
    client_entry *item, *last_item = NULL;
    int8_t i = usv.sival_int;

    // This must match the size declared in WDSP
    float spectrumBuffer[SAMPLE_BUFFER_SIZE];

    if (active_channels <= 0) return;

    sem_wait(&spec_bufevent_semaphore);
    item = TAILQ_FIRST(&Spectrum_client_list);
    last_item = item;
    sem_post(&spec_bufevent_semaphore);
    sem_wait(&spectrum_semaphore);
    if (last_item == NULL)
    {
        if (channels[i].enabled)
            flag = runGetPixels(i, spectrumBuffer);
        sem_post(&spectrum_semaphore);
        return;               // no clients so throw away pixels
    }

    if (!channels[i].enabled || (!channels[i].radio.mox && channels[i].isTX))
    {
        //   fprintf(stderr, "CH: %d  MOX: %d  isTX: %d  Ie: %d  Ce: %d\n", i, (int)channels[i].radio.mox, (int)channels[i].isTX, (int)last_item->channel_enabled[i], (int)channels[i].enabled);
        sem_post(&spectrum_semaphore);
        usleep(5000);
        return;
    }

    if (channels[i].radio.mox && channels[i].isTX)
    {
        flag = runGetPixels(i, spectrumBuffer);
        channels[i].spectrum.meter = (float)GetTXAMeter(channels[i].dsp_channel, txMeterMode);
    }
    else
    {
        switch (panadapterMode) // KD0OSS
        {
        case PANADAPTER:
        case SPECT:
            flag = runGetPixels(i, spectrumBuffer);
            break;

        case SCOPE2:
        case SCOPE:
        case PHASE:
            runRXAGetaSipF1(i, spectrumBuffer, channels[i].spectrum.nsamples);
            break;

        default:
            flag = runGetPixels(i, spectrumBuffer);
        }
        channels[i].spectrum.meter = (float)GetRXAMeter(channels[i].dsp_channel, rxMeterMode); // + multimeterCalibrationOffset + getFilterSizeCalibrationOffset();
    }
    sem_post(&spectrum_semaphore);
    if (!flag) return;

    sem_wait(&spec_bufevent_semaphore);
    TAILQ_FOREACH(item, &Spectrum_client_list, entries)
    {
        sem_post(&spec_bufevent_semaphore);
        if (channels[i].spectrum.fps > 0)
        {
            if (channels[i].spectrum.frame_counter-- <= 1)
            {
                char *client_samples = malloc(sizeof(CHANNEL)+channels[i].spectrum.nsamples);
                sem_wait(&spectrum_semaphore);
                if (!channels[i].enabled)
                {
                    sem_post(&spectrum_semaphore);
                    return;
                }
                client_set_samples(&channels[i], client_samples, spectrumBuffer, channels[i].spectrum.nsamples);
                bufferevent_write(item->bev, client_samples, sizeof(CHANNEL)+channels[i].spectrum.nsamples);
                sem_post(&spectrum_semaphore);
                free(client_samples);
                channels[i].spectrum.frame_counter = (channels[i].spectrum.fps == 0) ? 50 : 50 / channels[i].spectrum.fps;
            }
        }
        sem_wait(&spec_bufevent_semaphore);
    }
    sem_post(&spec_bufevent_semaphore);
} // end spectrum_timer_handler


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
    server.sin_port = htons(BASE_PORT+1);     // Need to had channel info to allow for more than one sprectrum stream.

    if (bind(spectrumSocket, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("spectrum client bind");
        fprintf(stderr,"port = %d\n", BASE_PORT+1);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "spectrum_client_thread: listening on TCP port %d\n", BASE_PORT+1);

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

    close(spectrumSocket);
    fprintf(stderr, "spectrum client thread stopped\n");
    return NULL;
} // end spectrum_client_thread


void spectrum_writecb(struct bufferevent *bev, void *ctx)
{/*
    struct audio_entry *item;
    client_entry *client_item;
    
    while ((item = audio_stream_queue_remove()) != NULL)
    {
        sem_wait(&spec_bufevent_semaphore);
        TAILQ_FOREACH(client_item, &Spectrum_client_list, entries)
        {
s            sem_post(&spec_bufevent_semaphore);
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
    client_entry    *current_item = NULL;
    unsigned char    message[MSG_SIZE];
    int              bytesRead = 0;

    client_entry *tmp_item;

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
        sdr_log(SDR_LOG_ERROR, "This client was not located");
        return;
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
        message[bytesRead-1] = 0;    // for Linux strings terminating in NULL
        if (channels[message[0]].enabled)
        {
            fprintf(stderr, "Spectrum message: [%u] %s\n", (unsigned char)message[1], (const char*)(message+2));
            current_item->channel_enabled[message[0]] = true;
            dsp_command(current_item, message);
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
  //      while (!getStopIQIssued()) usleep(1000);
        sem_wait(&spec_bufevent_semaphore);
        for (item = TAILQ_FIRST(&Spectrum_client_list); item != NULL; item = TAILQ_NEXT(item, entries))
        {
            if (item->bev == bev)
            {
                char ipstr[16];
                inet_ntop(AF_INET, (void *)&item->client.sin_addr, ipstr, sizeof(ipstr));
                sdr_log(SDR_LOG_INFO, "Spectrum client disconnection from %s:%d\n",
                        ipstr, ntohs(item->client.sin_port));
                sem_wait(&spectrum_semaphore);
                for (int i=0;i<MAX_CHANNELS;i++)
                    item->channel_enabled[i] = false;
                TAILQ_REMOVE(&Spectrum_client_list, item, entries);
             //   hw_stopIQ();
                sem_post(&spectrum_semaphore);
                usleep(5000);
                sem_wait(&spectrum_semaphore);
                free(item);
                sem_post(&spectrum_semaphore);
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
 
void wideband_timer_init(int channel)
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
    sev.sigev_value.sival_int = channel;

    timer_create(CLOCK_REALTIME, &sev, &wideband_timerid[channel]);
    timer_settime(wideband_timerid[channel], 0, &value, NULL);
} // end wideband_timer_init


void wideband_timer_handler(union sigval usv)
{   // this is called every 20 ms
    int flag = 0;
    client_entry *item, *last_item = NULL;
    int8_t i = usv.sival_int;

    if (active_channels <= 0) return;

    sem_wait(&wb_bufevent_semaphore);
    item = TAILQ_FIRST(&Wideband_client_list);
    last_item = item;
    sem_post(&wb_bufevent_semaphore);
//    for (int i=MAX_CHANNELS-1;i>MAX_CHANNELS-5;i--) // last 5 channels are for wideband
//    {
        if (channels[i].dsp_channel < 0) return;
   //     fprintf(stderr,"here: %d\n", channels[i].dsp_channel);
        sem_wait(&wideband_semaphore);
        if (last_item == NULL)
        {
            if (channels[i].enabled && channels[i].spectrum.type == BS)
                runGetPixels(i, widebandBuffer[channels[i].radio.radio_id]);
             //   GetPixels(channels[i].dsp_channel, 0, widebandBuffer[channels[i].radio.radio_id], &flag);
            sem_post(&wideband_semaphore);
            usleep(5000);
            return;               // no clients so throw away pixels
        }

   //     fprintf(stderr,"here 2: %d\n", channels[i].dsp_channel);
        if (!item->channel_enabled[i] || !channels[i].enabled || !channels[i].radio.bandscope_capable || channels[i].spectrum.type != BS)
        {
            sem_post(&wideband_semaphore);
            usleep(5000);
            return;
        }

   //     fprintf(stderr,"here: %d\n", channels[i].dsp_channel);
 //       sem_wait(&wideband_semaphore);
        flag = runGetPixels(i, widebandBuffer[channels[i].radio.radio_id]);
      //  GetPixels(channels[i].dsp_channel, 0, widebandBuffer[channels[i].radio.radio_id], &flag);
        sem_post(&wideband_semaphore);
        if (!flag) return;
  //      fprintf(stderr, "here 3\n");

        sem_wait(&wb_bufevent_semaphore);
        TAILQ_FOREACH(item, &Wideband_client_list, entries)
        {
  //          fprintf(stderr, "here 3\n");
            sem_post(&wb_bufevent_semaphore);
            if (channels[i].spectrum.fps > 0)
            {
    //            fprintf(stderr, "here 4\n");
                if (channels[i].spectrum.frame_counter-- <= 1)
                {
             //       fprintf(stderr, "here 5\n");
                    char *client_samples = malloc(sizeof(CHANNEL)+channels[i].spectrum.nsamples);
                    sem_wait(&wideband_semaphore);
                    if (!channels[i].enabled)
                    {
                        sem_post(&wideband_semaphore);
                        return;
                    }
                    client_set_wb_samples(&channels[i], client_samples, widebandBuffer[channels[i].radio.radio_id], channels[i].spectrum.nsamples);
                    sem_post(&wideband_semaphore);
                    bufferevent_write(item->bev, client_samples, sizeof(CHANNEL)+channels[i].spectrum.nsamples);
                    free(client_samples);
                    channels[i].spectrum.frame_counter = (channels[i].spectrum.fps == 0) ? 50 : 50 / channels[i].spectrum.fps;
                }
            }
            sem_wait(&wb_bufevent_semaphore);
        }
        sem_post(&wb_bufevent_semaphore);
//    }
} // end wideband_timer_handler


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
    server.sin_port = htons(BASE_PORT+30);       // FIXME: Need to add channel info to allow for bandscope stream per radio.

    if (bind(widebandSocket, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("wideband client bind");
        fprintf(stderr,"port = %d\n", BASE_PORT+30);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "wideband_client_thread: listening on TCP port %d\n", BASE_PORT+30);

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

    close(widebandSocket);
    fprintf(stderr, "wideband client thread stopped\n");
    return NULL;
} // end wideband_client_thread


void wideband_writecb(struct bufferevent *bev, void *ctx)
{/*
    struct audio_entry *item;
    client_entry *client_item;
    
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
    int8_t          ch = -1;

    sem_wait(&wb_bufevent_semaphore);
    item = TAILQ_FIRST(&Wideband_client_list);
    sem_post(&wb_bufevent_semaphore);
    if (item == NULL)
    {
        sdr_log(SDR_LOG_ERROR, "readcb called with no wideband clients");
        return;
    }

    current_item = item;

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

        message[bytesRead-1] = 0;    // for Linux strings terminating in NULL
        fprintf(stderr, "Wideband message: [%u] [%u] [%u]\n", (unsigned char)message[0], (unsigned char)message[1], (unsigned char)message[2]);
        ch = message[0];
        switch ((unsigned char)message[1])
        {
        case STARTBANDSCOPE:
        {
            int width = 0;
            sscanf((const char*)(message+3), "%d", &width);
            if (width < 512) break;
       //     sem_wait(&wb_bufevent_semaphore);
            channels[ch].radio.radio_id = message[2];
            channels[ch].spectrum.nsamples = (int)width;
            channels[ch].spectrum.fps = 10;
            widebandBuffer[channels[ch].radio.radio_id] = malloc((channels[ch].spectrum.nsamples * 2) * sizeof(float));
            if (widebandBuffer[channels[ch].radio.radio_id] == NULL) fprintf(stderr, "Allocation error!\n");
            enable_wideband(ch, true);
            current_item->channel_enabled[ch] = true;
     //       sem_post(&wb_bufevent_semaphore);
            wideband_timer_init(ch);
            sdr_log(SDR_LOG_INFO, "Bandscope channel %d started with width: %u  DSP: %d  Radio Id: %d\n", ch, width, channels[ch].dsp_channel, channels[ch].radio.radio_id);
        }
            break;

        case STOPBANDSCOPE:
        {
            channels[ch].radio.radio_id = message[2];
            current_item->channel_enabled[ch] = false;
            timer_delete(wideband_timerid[ch]);
            usleep(5000);
            enable_wideband(ch, false);
            sem_wait(&wb_bufevent_semaphore);
            if (widebandBuffer[channels[ch].radio.radio_id] != NULL)
                free(widebandBuffer[channels[ch].radio.radio_id]);
            widebandBuffer[channels[ch].radio.radio_id] = NULL;
            sem_post(&wb_bufevent_semaphore);
            sdr_log(SDR_LOG_INFO, "Bandscope channel %d stopped.\n", ch);
        }
            break;

        case UPDATEBANDSCOPE:
        {
            int width = 0;
            channels[ch].radio.radio_id = message[2];
            sscanf((const char*)(message+3), "%d", &width);
            if (width < 512) break;
            if (channels[ch].enabled)
            {
                sem_wait(&wb_bufevent_semaphore);
                channels[ch].spectrum.nsamples = (int)width;
                if (widebandBuffer[channels[ch].radio.radio_id] != NULL) free(widebandBuffer[channels[ch].radio.radio_id]);
                widebandBuffer[channels[ch].radio.radio_id] = malloc((channels[ch].spectrum.nsamples * 2) * sizeof(float));
                widebandInitAnalyzer(ch, width);
                sem_post(&wb_bufevent_semaphore);
                sdr_log(SDR_LOG_INFO, "Bandscope channel %d width: %u\n", ch, width);
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
                shutdown_wideband_channels(item);
                TAILQ_REMOVE(&Wideband_client_list, item, entries);
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
    server.sin_port = htons(BASE_PORT+10);

    if (bind(audioSocket, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("audio client bind");
        fprintf(stderr,"port=%d\n", BASE_PORT+10);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "audio_client_thread: listening on TCP port %d\n", BASE_PORT+10);

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

    close(audioSocket);
    fprintf(stderr, "audio client thread stopped\n");
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
   //     while (!getStopIQIssued()) usleep(1000);
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
        //    sem_wait(&audio_bufevent_semaphore);
        //    sem_post(&audio_bufevent_semaphore);
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
    // add newly connected client to 
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

    fprintf(stderr,"mic audio client thread\n");

    // setting up non-ssl open serverSocket
    micSocket = socket(AF_INET, SOCK_STREAM, 0);
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
    server.sin_port = htons(BASE_PORT+20);

    if (bind(micSocket, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("mic audio client bind");
        fprintf(stderr,"port=%d\n", BASE_PORT+20);
        return NULL;
    }

    sdr_log(SDR_LOG_INFO, "mic_audio_client_thread: listening on TCP port %d\n", BASE_PORT+20);

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

    close(micSocket);
    fprintf(stderr, "mic audio client thread stopped\n");
    return NULL;
} // end mic_audio_client_thread


void mic_audio_writecb(struct bufferevent *bev, void *ctx)
{/*
    struct audio_entry *item;
    client_entry *client_item;
    
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
    unsigned char    mic_buffer[MIC_ALAW_BUFFER_SIZE];
    struct evbuffer *inbuf;
    client_entry    *current_item = NULL;
    unsigned char    message[MIC_MSG_SIZE];
    int              bytesRead = 0;


    client_entry *tmp_item;
    /* Only allow the first client on Client_list to command
         * dspserver as primary.  If this client is not the primary, we
         * will first determine whether it is allowed to execute the
         * command it is executing, and abort if it is not. */

    // locate the current_item for this slave client
    sem_wait(&mic_semaphore);
    for (current_item = TAILQ_FIRST(&Mic_audio_client_list); current_item != NULL; current_item = tmp_item)
    {
        tmp_item = TAILQ_NEXT(current_item, entries);
        if (current_item->bev == bev)
        {
            break;
        }
    }
    sem_post(&mic_semaphore);
    if (current_item == NULL) // || current_item->client_type != CONTROL)
    {
        sdr_log(SDR_LOG_ERROR, "This client does not have transmit rights.\n");
        return;
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
        switch ((unsigned char)message[1])
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
            Mic_stream_queue_add(message[0], mic_buffer);
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
  //      while (!getStopIQIssued()) usleep(1000);
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
     //       sem_wait(&mic_semaphore);
     //       sem_post(&mic_semaphore);
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
    fprintf(stderr, "mic accept********\n");
} // emd do_accept_mic_audio


/******************************************************************************************************************/

void client_set_samples(CHANNEL *channel, char* client_samples, float* samples, int size)
{
    int   i, j = 0;
    float extras = 0.0f;
    int   offset;
    float rotated_samples[size];

    if (size != 2000) {printf("Spectrum len: %d\n", size); fflush(stdout);}
    channel->spectrum.length = size;

    offset = (float)channel->spectrum.lo_offset * (float)size / (float)sampleRate;
    if (channel->spectrum.lo_offset == 0.0)
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
    }

    if (channel->radio.mox)
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
            client_samples[i+sizeof(CHANNEL)] = (char)-(rotated_samples[i]+extras);
        }
    }
    memcpy(client_samples, (char*)channel, sizeof(CHANNEL));
} // end client_set_samples


void client_set_wb_samples(CHANNEL *channel, char* client_samples, float* samples, int size)
{
    int i = 0;
    float extras = 0.0f;

 //   if (size != 512) {printf("Wideband spectrum len: %d\n", size); fflush(stdout);}
    channel->spectrum.length = (unsigned short)size;

#pragma omp parallel shared(size, samples, client_samples) private(i)
    {
#pragma omp for schedule(static)
        for (i=0;i<size;i++)
        {
            client_samples[i+sizeof(CHANNEL)] = (char)-(samples[i]+extras);
        }
    }
    memcpy(client_samples, (char*)channel, sizeof(CHANNEL));
} // end client_set_wb_samples


void enable_wideband(int8_t channel, bool enable)
{
    int pixels = 512;
    uint8_t ch = MAX_WDSP_CHANNELS - (MAX_CHANNELS - channel);
    int result;


    sem_wait(&wb_bufevent_semaphore);
    if (enable)
    {
        result = widebandInitAnalyzer(ch, pixels);
        if (result != 0)
        {
            sem_post(&wb_bufevent_semaphore);
            return;
        }
        channels[channel].dsp_channel = ch;
        channels[channel].spectrum.type = BS;
        channels[channel].radio.bandscope_capable = true;
        channels[channel].enabled = true;
        fprintf(stderr, "Wideband DSP channel: %u started.\n", ch);
    }
    else
    {
   //     sem_wait(&wb_iq_semaphore);
        channels[channel].enabled = false;
   //     sem_post(&wb_iq_semaphore);
        usleep(10000);
        channels[channel].radio.bandscope_capable = false;
        wb_destroy_analyzer(ch);
    }
    sem_post(&wb_bufevent_semaphore);
} // end enable_wideband

