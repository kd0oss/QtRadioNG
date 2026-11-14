/*
 * File:   hardware.c
 * Author: jm57878
 *
 * Created on 10 March 2009, 20:26
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
*
* Updates by Rick Schnicker KD0OSS -- 2021,2022
*/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <time.h>
#include "qtr_net.h"
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include <samplerate.h>
#include <stdbool.h>

#include "server.h"
#include "hardware.h"
#include "plugin.h"
#include "wdsp.h"
#include "audiostream.h"
#include "server.h"
#include "util.h"
#include "dsp.h"
#include "wfm.h"


static sem_t hw_send_semaphore;
static sem_t hw_cmd_semaphore;
extern sem_t iq_semaphore;
extern sem_t wb_iq_semaphore;
extern sem_t wideband_semaphore;

// Added by Alex lee 18 Aug 2010
double LO_offset = 0; // 9000;  // LO offset 9khz

//short int connected_radios = 0;
short int active_rfstreams = 0;
extern int8_t active_receivers;
extern int8_t active_transmitters;
static pthread_t iq_thread_id[MAX_RFSTREAMS];
static pthread_t wb_iq_thread_id[MAX_RFSTREAMS];   // indexed by stream (bandscope = MAX_RFSTREAMS-1)

// wide band FM (mode FMW): replaces the WDSP audio of a receive stream
static WFM   *wfm_state[MAX_RFSTREAMS];
static bool   wfm_on[MAX_RFSTREAMS];
static double wfm_gain[MAX_RFSTREAMS] = {[0 ... MAX_RFSTREAMS-1] = 0.5};

RADIO radio[MAX_RADIO_CONNECTIONS];
int8_t rfstream_local_id[MAX_RFSTREAMS] = {[0 ... MAX_RFSTREAMS-1] = -1};

/* Radios known to dspserver, numbered in the order they first connected.
 * The number is the radio id clients see; a radio keeps it, and its streams,
 * when its radio program reconnects. */
typedef struct _registration
{
    bool    used;
    int8_t  connection;          // slot, -1 while its radio program is not connected
    int8_t  local_id;            // the radio's id inside its radio program
    char    radio_type[25];
    int     receivers;
    int     transmitters;
    int     first_stream;
    int     streams;
    char   *body;                // manifest lines inside <radio=...> ... </radio>
    char   *xml;                 // <radio=id> ... </radio> for the clients' manifest
} REGISTRATION;

static REGISTRATION    reg[MAX_RADIOS];
static int             reg_count = 0;
static pthread_mutex_t reg_lock = PTHREAD_MUTEX_INITIALIZER;
//int iq_socket = -1;

//int buffer_size = BUFFER_SIZE;
//float input_buffer[BUFFER_SIZE*2]; // I,Q
//float output_buffer[BUFFER_SIZE*2];

//float mic_left_buffer[BUFFER_SIZE];
//float mic_right_buffer[BUFFER_SIZE];

long long lastFreq;

short left_rx_sample;
short right_rx_sample;

//int frames = 0;

int sampleRate = 48000;  // default 48k
//int mox = 0;             // default not transmitting

//int command_socket;
//int command_port;
//static struct sockaddr_in command_addr;
//static socklen_t command_length = sizeof(command_addr);

extern struct        sockaddr_in radio_audio_addr[MAX_RFSTREAMS];
extern socklen_t     radio_audio_length[MAX_RFSTREAMS];
extern int           radio_audio_socket[MAX_RFSTREAMS];

int tx_iq_socket[MAX_RFSTREAMS];
struct sockaddr_in tx_iq_addr[MAX_RFSTREAMS];
socklen_t tx_iq_length[MAX_RFSTREAMS];

int radioMic = 0;
int local_audio = 0;


//static int local_audio = 0;
//static int port_audio = 0;

//
// samplerate library data structures
//
SRC_STATE *sr_state;
double src_ratio;

int hw_get_manifest(int);
void hw_radio_disconnected(int slot);

int stopIQIssued = 0;
pthread_mutex_t stopIQMutex;

int getStopIQIssued(void)
{
    int ret = 0;
    pthread_mutex_lock(&stopIQMutex);
    ret = stopIQIssued;
    pthread_mutex_unlock(&stopIQMutex);
    return ret;
} // end getStopIQIssued


void setStopIQIssued(int val)
{
    pthread_mutex_lock(&stopIQMutex);
    stopIQIssued = val;
    pthread_mutex_unlock(&stopIQMutex);
    fprintf(stderr, "StopIQ set to: %d\n", stopIQIssued);
} // end setStopIQIssued


void* iq_thread(void* stream)
{
    int ch = (intptr_t)stream;
    struct sockaddr_in iq_addr;
    socklen_t iq_length = sizeof(iq_addr);
    BUFFERL buffer;
    // this stream's own buffers and resampler (one IQ thread per receive stream)
    float *data_in;
    float *data_out;
    double *dataout;
    SRC_STATE *src;
    int src_error = 0;

    // create a socket to receive iq from the server
    int iq_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (iq_socket < 0)
    {
        sock_perror("iq_thread: create iq socket failed");
        exit(1);
    }

    sock_set_rcvtimeo_us(iq_socket, 10);
    sock_set_reuseaddr(iq_socket);

    memset(&iq_addr, 0, iq_length);
    iq_addr.sin_family = AF_INET;
    iq_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    iq_addr.sin_port = htons(RX_IQ_PORT_0 + rfstream[ch].index + hw_port_offset(ch));

    if (bind(iq_socket, (struct sockaddr*)&iq_addr, iq_length) < 0)
    {
        sock_perror("iq_thread: bind socket failed for iq socket");
        exit(1);
    }

    dataout = (double*)malloc(2048 * sizeof(double));
    if (dataout == NULL) exit(1);

    data_out = (float*)malloc(2048 * sizeof(float));
    if (data_out == NULL)
    {
        free(dataout);
        exit(1);
    }

    data_in = (float*)malloc(2048 * sizeof(float));
    if (data_in == NULL)
    {
        free(dataout);
        free(data_out);
        exit(1);
    }

    src = src_new(SRC_SINC_FASTEST, 2, &src_error);
    if (src == NULL)
        fprintf(stderr, "iq_thread: SR INIT ERROR: %s\n", src_strerror(src_error));

    fprintf(stderr, "iq_thread: iq bound to port %d socket=%d  Rf stream: %d\n", htons(iq_addr.sin_port), iq_socket, ch);
    fprintf(stderr, "audiostream_conf.samplerate=%d\n", audiostream_conf.samplerate);

    while (!getStopIQIssued())
    {
        int bytes_read;
        if (rfstream[ch].radio.mox) continue;

        bytes_read = recvfrom(iq_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&iq_addr, &iq_length);
//if (ch == 1)	fprintf(stderr, "%d \n", bytes_read);
        if (bytes_read < 0)
        {
         //   perror("recvfrom socket failed for iq buffer");
         //   exit(1);
            continue;
        }

        if (bytes_read < sizeof(BUFFERL))
            continue;

        if (bytes_read < buffer.length)
            continue;

        sem_wait(&iq_semaphore);
        if (active_receivers == 0)
        {
            sem_post(&iq_semaphore);
            break;
        }

        int j, i;
        i = buffer.length;

        if (bUseNB && rfstream[ch].enabled)
            runXanbEXT(ch, buffer.data);
        if (bUseNB2 && rfstream[ch].enabled)
            runXnobEXT(ch, buffer.data);

        int error = 0;
        if (rfstream[ch].enabled)
            error = runFexchange0(ch, buffer.data, dataout);
        if (error != 0 && error != -2)
            printf("fexchange error: %d\n", error);

        // FMW: WDSP still runs (meters, spectrum) but the audio comes from wfm.c
        if (wfm_on[ch] && rfstream[ch].enabled)
        {
            int rate = rfstream[ch].spectrum.sample_rate > 0 ? rfstream[ch].spectrum.sample_rate : sampleRate;
            if (wfm_state[ch] == NULL || wfm_rate(wfm_state[ch]) != rate)
            {
                wfm_destroy(wfm_state[ch]);
                wfm_state[ch] = wfm_create(rate);
                if (rate < 192000)
                    fprintf(stderr, "FMW: receiver rate %d is too low for broadcast FM; use 192000 or 384000\n", rate);
            }
            if (wfm_state[ch] != NULL)
                wfm_process(wfm_state[ch], buffer.data, dataout, i / 2, wfm_gain[ch]);
        }

        // protocol plugin mode: the plugin's audio replaces WDSP's
        if (plugin_rx_active(ch) && rfstream[ch].enabled)
        {
            int rate = rfstream[ch].spectrum.sample_rate > 0 ? rfstream[ch].spectrum.sample_rate : sampleRate;
            plugin_rx_process(ch, buffer.data, i / 2, rate, dataout);
        }

        // audio_enabled[ch] = false; //*******************************************
        if (audio_enabled[ch])
        {
            // process the output with resampler
            int rc;
#pragma omp parallel for schedule(static) private(j)
            for (j=0; j < (i/2); j++)
            {
                data_in[j*2]   = (float)dataout[j*2];
                data_in[j*2+1] = (float)dataout[j*2+1];
            }

            SRC_DATA data;
            data.data_in = data_in;
            data.input_frames = i/2;

            data.data_out = data_out;
            data.output_frames = i/2;
            data.src_ratio = src_ratio;
            data.end_of_input = 0;

            rc = src != NULL ? src_process(src, &data) : 1;
            if (rc)
            {
                fprintf(stderr,"SRATE2: error: %s (rc=%d)\n", src_strerror(rc), rc);
                fprintf(stderr, "i: %d   Frames: %ld   SR: %f\n", i, data.output_frames_gen, src_ratio);
                fprintf(stderr, "Bytes read: %d\n", bytes_read);
                //     exit(1);
            }
            else
            {
          //      fprintf(stderr, "Frames 2: %ld\n", data.output_frames_gen);
                for (int i=0; i < data.output_frames_gen; i++)
                {
                    left_rx_sample = (short)(data.data_out[i*2]*32767.0);
                    right_rx_sample = (short)(data.data_out[i*2+1]*32767.0);
                    audio_stream_put_samples(ch, left_rx_sample, right_rx_sample);  // send audio to client computer and radio hardware
                    //fprintf(stderr, "audio ch: %d\n", ch);
                }
            } // if (rc)
        } // audio_enabled

        if (rfstream[ch].enabled)
            runSpectrum0(ch, buffer.data);

        sem_post(&iq_semaphore);
    } // end while
    sock_close(iq_socket);
    if (src != NULL)
        src_delete(src);
    free(data_in);
    free(data_out);
    free(dataout);
    fprintf(stderr, "******************** Rf stream [%d] IQ thread closed.\n", ch);
    return 0;
} // end iq_thread


/*
 * Bandscope samples from the radio.  Two formats arrive on the same port:
 *  - real ADC samples (hpsdr/Hermes): chunk = 0, 16384 int16 per frame;
 *  - complex IQ (hackrf_one): chunk has WB_CHUNK_COMPLEX set and the packet
 *    index in the low bits; a frame is 16384 I/Q pairs in two packets.
 * The analyzer is reconfigured when the format changes.
 */
#define WB_CHUNK_COMPLEX     0x8000     // same values as hackrf_one/hackrf.h
#define WB_CHUNK_INDEX_MASK  0x00ff
#define WB_FFT_SIZE          16384

void* wb_iq_thread(void* channel)
{
    int ch = (intptr_t)channel;
    struct sockaddr_in iq_addr;
    int iq_socket = -1;
    socklen_t iq_length = sizeof(iq_addr);
    BUFFERWB buffer;
    int offset = 0;
    bool frame_complex = false;
    int16_t *data = NULL;
    double *samples = NULL;

    // create a socket to receive iq from the server
    iq_socket = socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (iq_socket < 0)
    {
        sock_perror("wb_q_thread: create iq socket failed");
        exit(1);
    }

    sock_set_reuseaddr(iq_socket);

    memset(&iq_addr, 0, iq_length);
    iq_addr.sin_family = AF_INET;
    iq_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    iq_addr.sin_port = htons(BANDSCOPE_PORT + rfstream_local_id[ch] + hw_port_offset(ch));

    if (bind(iq_socket, (struct sockaddr*)&iq_addr, iq_length) < 0)
    {
        sock_perror("wb_iq_thread: bind socket failed for iq socket");
        exit(1);
    }

    fprintf(stderr, "wb_iq_thread: iq bound to port %d socket=%d  Ch: %d\n", htons(iq_addr.sin_port), iq_socket, ch);

    data = malloc(sizeof(int16_t) * WB_FFT_SIZE * 2);
    samples = malloc(sizeof(double) * WB_FFT_SIZE * 2);

    rfstream[ch].dsp_channel = ch;

    // Block on the socket with a timeout so the thread neither spins while the
    // bandscope is closed nor misses the stop request.
    sock_set_rcvtimeo_us(iq_socket, 100000);

    while (!getStopIQIssued())
    {
        int bytes_read;
        int payload_samples;
        int frame_values;
        int n;
        bool is_complex;

        iq_length = sizeof(iq_addr);
        bytes_read = recvfrom(iq_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&iq_addr, &iq_length);
        if (bytes_read < 0)
        {
            int err = sock_error();
            if (!sock_would_block(err) && err != SOCK_EINTR)
            {
                fprintf(stderr, "recvfrom socket failed for wideband iq buffer: %s (%d)\n", sock_strerror(err), err);
                usleep(10000);
            }
            continue;
        }

        // Always drain the socket; only use the data while a bandscope is open.
        if (rfstream[ch].radio.mox || rfstream[ch].spectrum.type != BS || !rfstream[ch].enabled)
        {
            offset = 0;
            continue;
        }

        if (bytes_read < (int)offsetof(BUFFERWB, data))
            continue;

        is_complex = (buffer.chunk & WB_CHUNK_COMPLEX) != 0;
        if (is_complex != frame_complex || (is_complex && (buffer.chunk & WB_CHUNK_INDEX_MASK) == 0))
            offset = 0;              // format change, or first packet of a complex frame
        frame_complex = is_complex;
        frame_values = is_complex ? WB_FFT_SIZE * 2 : WB_FFT_SIZE;

        payload_samples = (bytes_read - (int)offsetof(BUFFERWB, data)) / (int)sizeof(int16_t);
        n = buffer.length;
        if (n > payload_samples) n = payload_samples;
        if (n > frame_values - offset) n = frame_values - offset;
        if (n <= 0) continue;

        memcpy((char*)&data[offset], (char*)buffer.data, n * sizeof(int16_t));
        offset = offset + n;
        if (offset < frame_values)
            continue;
        offset = 0;

        int i = 0;
        if (frame_complex)
        {
            // frames are I,Q; Spectrum0() takes Q,I (as for the receivers)
#pragma omp parallel for schedule(static) private(i)
            for (i=0;i<WB_FFT_SIZE;i++)
            {
                samples[i*2] = data[i*2+1]/32768.0;
                samples[(i*2)+1] = data[i*2]/32768.0;
            }
        }
        else
        {
#pragma omp parallel for schedule(static) private(i)
            for (i=0;i<WB_FFT_SIZE;i++)
            {
                samples[i*2] = data[i]/32768.0;
                samples[(i*2)+1] = 0.0;
            }
        }

        sem_wait(&iq_semaphore);
        if (rfstream[ch].enabled)      // may have been stopped meanwhile
        {
            if (wb_complex[ch] != frame_complex)
            {
                // analyzer was set up for the other format: switch it over
                sem_wait(&wideband_semaphore);
                wb_complex[ch] = frame_complex;
                widebandConfigAnalyzer(rfstream[ch].dsp_channel, rfstream[ch].spectrum.nsamples, frame_complex);
                sem_post(&wideband_semaphore);
            }
            runSpectrum0(ch, samples);
        }
        sem_post(&iq_semaphore);
    } // end while
 //   setStopIQIssued(false);
    rfstream[ch].enabled = false;
    rfstream[ch].dsp_channel = -1;
    sock_close(iq_socket);
    free(data);
    free(samples);
    fprintf(stderr, "****** Wideband IQ thread for ch: %d closed.\n", ch);
    return 0;
} // end wb_iq_thread


void hw_send(unsigned char* data, int length, int stream)
{
    BUFFER  buffer;

//    iq_monitor(&buffer.data[0], length/8);
    //fprintf(stderr,"hw_send: %s\n",who);
    sem_wait(&hw_send_semaphore);
    for (int i=0;i<length/512;i++)
    {
        buffer.chunk = i;
        buffer.radio_id = rfstream_local_id[stream];
        buffer.receiver = rfstream[stream].index;
        buffer.length = length;
        memcpy((char*)&buffer.data[0], (char*)&data[512*i], 512);

        int bytes_written = sendto(tx_iq_socket[stream], (char*)&buffer, sizeof(buffer), 0, (struct sockaddr *)&tx_iq_addr[stream], tx_iq_length[stream]);
        if (bytes_written < 0)
        {
            sock_perror("socket error");
            fprintf(stderr, "sendto tx_iq failed: %d length=%d\n", bytes_written, length);
            exit(1);
        }
    }
    sem_post(&hw_send_semaphore);
} // end hw_send


/* --------------------------------------------------------------------------*/
/**
* @brief send a command
*
* @param command
* @param response buffer allocated by caller, MUST be hw_RESPONSE_SIZE bytes
*/
void send_command(int connection, char* command, int len, char *response)
{
    int rc = -1;
    char output[67];

    //fprintf(stderr, "send_command: command='%s'\n", command);

    if (connection < 0 || connection >= MAX_RADIO_CONNECTIONS || !radio[connection].connected)
    {
        strcpy(response, "ERROR radio not connected");
        fprintf(stderr, "send_command [%u]: radio not connected\n", (uint8_t)command[1]);
        return;
    }
    output[0] = (char)0x7f;
    output[1] = (char)len;
 //   fprintf(stderr,"SC: %u\n", (uint8_t)command[2]);
    memcpy(output+2, command, len);
    sem_wait(&hw_cmd_semaphore);
    rc = send(radio[connection].socket, output, len+2, 0);
    if (rc < 0)
    {
        sem_post(&hw_cmd_semaphore);
        fprintf(stderr, "send command failed: %d: %s\n", rc, command);
        radio[connection].connected = false;
    }

    /* FIXME: This is broken.  It will probably work as long as
     * responses are very small and everything proceeds in lockstep. */
    rc = recv(radio[connection].socket, response, HW_RESPONSE_SIZE, 0);
    sem_post(&hw_cmd_semaphore);
    if (rc < 1)
    {
        fprintf(stderr, "read response failed: %d\n", rc);
        radio[connection].connected = false;
    }
    /* FIXME: This is broken, too.  If the response is exactly
     * HW_RESPONSE_SIZE, we have to truncate it by one byte. */
    if (rc == HW_RESPONSE_SIZE)
        rc--;
    response[rc] = 0;

    fprintf(stderr, "send_command [%u]: response='%s'\n", (uint8_t)output[3], response);
} // end send_command


int make_connection(short int stream)
{
    char *token, *saveptr;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;

    setStopIQIssued(false);
    result = 1;
    if (!rfstream[stream].isTX)
    {
        start_rx_audio(stream);
        fprintf(stderr, "ATTACH RX\n");
        command[0] = (char)rfstream[stream].index;
        command[1] = ATTACHRX;
        command[2] = (char)rfstream_local_id[stream];
        //sprintf(command, "connect %d %d", receiver, IQ_PORT+(receiver*2));
        send_command(rfstream[stream].radio.connection, command, 3, response);
        fprintf(stderr, "Attach RX resp: %s\n", response);
        token = strtok_r(response, " ", &saveptr);
        if (token != NULL)
        {
            if (strcmp(token, "OK") == 0)
            {
                radio_audio_addr[stream] = radio[rfstream[stream].radio.connection].iq_addr;
                radio_audio_length[stream] = radio[rfstream[stream].radio.connection].iq_length;
                recvfrom(radio_audio_socket[stream], (char*)&command, 2, 0, (struct sockaddr*)&radio_audio_addr[stream], (socklen_t *)&radio_audio_length[stream]);
                fprintf(stderr, "Receiver audio attached.\n");
                token=strtok_r(NULL, " ", &saveptr);
                if (token != NULL)
                {
                    result = 1;
                    sampleRate = 48000; //////////atoi(token);
                    fprintf(stderr, "connect: sampleRate=%d\n", sampleRate);
                    setSpeed(stream, sampleRate);
                }
                else
                {
                    fprintf(stderr, "invalid response to RX attach: %s\n", response);
                    result = 0;
                }
            }
            else
                if (strcmp(token, "ERROR") == 0)
                {
                    fprintf(stderr, "error response to RX attach: %s\n", response);
                    result = 0;
                }
                else
                {
                    fprintf(stderr, "invalid response to RX attach: %s\n", response);
                    result = 0;
                }
        }
    }
    else
    {
        tx_init(stream);
        fprintf(stderr, "ATTACH TX\n");
        command[0] = (char)rfstream[stream].index;
        command[1] = (char)ATTACHTX;
        command[2] = (char)rfstream_local_id[stream];
        send_command(rfstream[stream].radio.connection, command, 3, response);
        fprintf(stderr, "Attach TX resp: %s\n", response);
        token = strtok_r(response, " ", &saveptr);
        if (token != NULL)
        {
            if (strcmp(token, "OK") == 0)
            {
                result = 1;
                tx_iq_addr[stream] = radio[rfstream[stream].radio.connection].iq_addr;
                tx_iq_length[stream] = radio[rfstream[stream].radio.connection].iq_length;
                recvfrom(tx_iq_socket[stream], (char*)&command, 2, 0, (struct sockaddr*)&tx_iq_addr[stream], (socklen_t *)&tx_iq_length[stream]);
                fprintf(stderr, "Transmitter attached.\n");
            }
            else
                if (strcmp(token, "ERROR") == 0)
                {
                    fprintf(stderr, "error response to TX attach: %s\n", response);
                    result = 0;
                }
                else
                {
                    fprintf(stderr, "invalid response to TX attach: %s\n", response);
                    result = 0;
                }
        }
    }
    return result;
} // end make_connection


int hwSetSampleRate(int stream, long rate)
{
    char *token;
    char srate[10];
    char command[64], response[HW_RESPONSE_SIZE];
    int result;
    char *saveptr;

    result = 0;
    command[0] = (char)rfstream[stream].index;
    command[1] = (char)SETSAMPLERATE;
    sprintf(srate, "%ld", rate);
    memcpy(command+2, srate, strlen(srate)+1);
    send_command(rfstream[stream].radio.connection, command, strlen(srate)+3, response);
    token = strtok_r(response, " ", &saveptr);
    if (token != NULL)
    {
        if (strcmp(token, "OK") == 0)
        {
            result = 0;
        }
        else
            if (strcmp(token, "ERROR") == 0)
            {
                result = 1;
            }
            else
            {
                fprintf(stderr, "invalid response to set sample rate: %s\n", response);
                result = 1;
            }
    }

    return result;
} // hwSetSampleRate


int hwSetFrequency(int ch, long long frequency)
{
    char *token;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;
    char *saveptr;

    result = 0;
    memset(command, 0, sizeof(command));
    sprintf(command, "%c%c%lld", rfstream[ch].index, SETFREQ, (frequency - (long long)LO_offset));
    send_command(rfstream[ch].radio.connection, command, 64, response);
    token = strtok_r(response, " ", &saveptr);
    if (token != NULL)
    {
        if (strcmp(token, "OK") == 0)
        {
            result = 0;
            lastFreq = frequency;
            rfstream[ch].frequency = frequency;
            fprintf(stderr, "Freq set to: %lld hz\n", frequency);
        }
        else
            if (strcmp(token, "ERROR") == 0)
            {
                result = 1;
            }
            else
            {
                fprintf(stderr, "invalid response to set frequency: %s\n", response);
                result = 1;
            }
    }

    return result;
} // end hwSetFrequency


int hwSetMox(int8_t ch, int state)
{
    char *token, *saveptr;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;

    result = 0;
//    mox = state;
    command[0] = (char)rfstream[ch].index;
    command[1] = (char)MOX;
    command[2] = (char)state;
    send_command(rfstream[ch].radio.connection, command, 3, response);
    token = strtok_r(response, " ", &saveptr);
    if (token != NULL)
    {
        if (strcmp(token, "OK") == 0)
        {
            result = 0;
        }
        else
            if (strcmp(token, "ERROR") == 0)
            {
                result = 1;
            }
            else
            {
                fprintf(stderr, "Invalid response to set MOX: %s\n", response);
                result = 1;
            }
    }

    return result;
} // end hwSetMox


int hwSendStarCommand(int8_t ch, unsigned char *command, int len)
{
    int result;
    char buf[64];
    char response[HW_RESPONSE_SIZE];

    result = 0;
    buf[0] = (char)rfstream[ch].index;
    memcpy(buf+1, command, len);
    send_command(rfstream[ch].radio.connection, (char*)buf, len+1, response);
    fprintf(stderr, "response to STAR message: [%s]\n", response);
//    if (command[1] == 1) command[1] = STARHARDWARE;
//    snprintf(buf, sizeof(buf), "%s %s", command, response); // insert a leading '*' in answer
//    strcpy((char*)command, buf);                                    // attach the answer

    return result;
} // end hwSendStarCommand


/* --------------------------------------------------------------------------*/
/** 
* @brief Set the speed
* 
* @param speed
*/
void setSpeed(int stream, int s)
{
    fprintf(stderr, "setSpeed %d on rx: %d\n", s, rfstream[stream].dsp_channel);
    fprintf(stderr, "LO_offset %f\n", LO_offset);

    sampleRate = s;

    //SetDSPSamplerate(rfstream[stream].dsp_channel, sampleRate);

    // SetRXAShiftFreq(0, -LO_offset);
    //SetRXAShiftFreq(1, -LO_offset);

    fprintf(stderr, "%s:  rx: %d  %f\n", __FUNCTION__, rfstream[stream].dsp_channel, (double)sampleRate);
    hw_set_src_ratio();
} // end setSpeed


/*
 * Ask hpsdr to start or stop the radio's wideband stream for a radio.
 * Sent through a receive stream of that radio that is connected to hpsdr.
 */
void hw_set_bandscope(int8_t radio_id, bool enable)
{
    char command[64], response[HW_RESPONSE_SIZE];

    for (int i = 0; i < MAX_RFSTREAMS; i++)
    {
        if (rfstream[i].isTX || !rfstream[i].enabled || rfstream[i].index < 0 ||
            rfstream[i].spectrum.type == BS || rfstream[i].radio.radio_id != radio_id)
            continue;
        if (rfstream[i].radio.connection < 0 || !radio[rfstream[i].radio.connection].connected)
            continue;
        command[0] = (char)rfstream[i].index;
        command[1] = (char)(enable ? STARTBANDSCOPE : STOPBANDSCOPE);  // same values as hpsdr's HSTART/HSTOPBANDSCOPE
        command[2] = (char)rfstream_local_id[i];
        send_command(rfstream[i].radio.connection, command, 3, response);
        return;
    }
    fprintf(stderr, "hw_set_bandscope: no connected receive stream for radio %d\n", radio_id);
} // end hw_set_bandscope


/* Called from the command thread; the IQ thread picks the change up on its next buffer. */
void hw_set_wfm(int8_t stream, bool enable)
{
    if (stream < 0 || stream >= MAX_RFSTREAMS)
        return;
    sem_wait(&iq_semaphore);
    wfm_on[stream] = enable;
    if (!enable)
    {
        wfm_destroy(wfm_state[stream]);
        wfm_state[stream] = NULL;
    }
    sem_post(&iq_semaphore);
} // end hw_set_wfm


void hw_set_wfm_gain(int8_t stream, double gain)
{
    if (stream >= 0 && stream < MAX_RFSTREAMS)
        wfm_gain[stream] = gain;
} // end hw_set_wfm_gain


bool hw_wfm_enabled(int8_t stream)
{
    return stream >= 0 && stream < MAX_RFSTREAMS && wfm_on[stream];
} // end hw_wfm_enabled


void hw_startIQ(int stream)
{
    int ch = stream;

    // a client starts a stream: IQ must run, even if the last client to
    // leave stopped it (STARTIQ comes before the attach that also clears this)
    if (getStopIQIssued())
        setStopIQIssued(0);

    if (rfstream[ch].isTX)
    {
        // create a socket to send TX IQ to the server
        tx_iq_socket[ch] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (tx_iq_socket[ch] < 0)
        {
            sock_perror("hw_init: create tx_iq socket failed");
            exit(1);
        }
        sock_set_reuseaddr(tx_iq_socket[ch]);

        memset(&tx_iq_addr[ch], 0, tx_iq_length[ch]);
        tx_iq_addr[ch].sin_family = AF_INET;
        tx_iq_addr[ch].sin_addr.s_addr = htonl(INADDR_ANY);
        tx_iq_addr[ch].sin_port = htons(TX_IQ_PORT_0 + hw_port_offset(ch));

        if (bind(tx_iq_socket[ch], (struct sockaddr*)&tx_iq_addr[ch], tx_iq_length[ch]) < 0)
        {
            sock_perror("hw_init: bind socket failed for tx_iq socket");
            exit(1);
        }

        fprintf(stderr, "hw_init: tx_iq bound to port %d socket %d\n", ntohs(tx_iq_addr[ch].sin_port), tx_iq_socket[ch]);
        return;
    }

    int rc = pthread_create(&iq_thread_id[ch], NULL, iq_thread, (void*)(intptr_t)ch);
    if (rc != 0)
    {
         fprintf(stderr,"pthread_create failed on iq_thread: rc=%d\n", rc);
         exit(1);
    }

    if (rfstream[WIDEBAND_CHANNEL - rfstream[ch].radio.radio_id].dsp_channel < 0)
    {
        rfstream[WIDEBAND_CHANNEL - rfstream[ch].radio.radio_id].radio = rfstream[ch].radio;
        rfstream_local_id[WIDEBAND_CHANNEL - rfstream[ch].radio.radio_id] = rfstream_local_id[ch];
        rc = pthread_create(&wb_iq_thread_id[WIDEBAND_CHANNEL - rfstream[ch].radio.radio_id], NULL, wb_iq_thread, (void*)(intptr_t)(WIDEBAND_CHANNEL - rfstream[ch].radio.radio_id));
        if (rc != 0)
        {
            fprintf(stderr,"pthread_create failed on wb_iq_thread: rc=%d\n", rc);
        }
    }
} // end hw_startIQ


/* A radio program's connection: manifest, then wait until it goes away.
 * Commands are sent from other threads (send_command); this thread only
 * looks for the connection closing while no command is in progress. */
void* command_thread(void* arg)
{
    int slot = (intptr_t)arg;

    fprintf(stderr, "radio connecting: %s:%d  slot %d  ports +%d\n", inet_ntoa(radio[slot].iq_addr.sin_addr),
            ntohs(radio[slot].iq_addr.sin_port), slot, slot * PORT_BLOCK);
    if (hw_get_manifest(slot) == 0)
    {
        radio[slot].connected = true;
        while (radio[slot].connected)
        {
            char peek;
            usleep(200000);
            sem_wait(&hw_cmd_semaphore);
            /* poll without blocking: only peek when select says readable */
            int r = sock_readable(radio[slot].socket, 0);
            ssize_t n = 1;
            int err = 0;
            if (r > 0)
            {
                n = recv(radio[slot].socket, &peek, 1, MSG_PEEK);
                if (n < 0)
                    err = sock_error();
            }
            else if (r < 0)
            {
                n = -1;
                err = sock_error();
            }
            sem_post(&hw_cmd_semaphore);
            if (n == 0 || (n < 0 && err != SOCK_EAGAIN && err != SOCK_EWOULDBLOCK && err != SOCK_EINTR))
                radio[slot].connected = false;
        }
    }
    radio[slot].connected = false;
    hw_radio_disconnected(slot);
    fprintf(stderr, "radio disconnected: slot %d\n", slot);
    sock_close(radio[slot].socket);
    radio[slot].in_use = false;
    return 0;
} // end command_thread


void* hw_command_listener_thread(void* arg)
{
    int csocket=0;
    struct sockaddr_in csocket_addr;
    socklen_t csocket_length = sizeof(csocket_addr);
    
    fprintf(stderr, "Starting radio command port.\n");
    // create a TCP socket to send commands to the server
    csocket = socket(AF_INET,SOCK_STREAM, 0);
    if (csocket < 0)
    {
        sock_perror("hw_init: create radio command socket failed");
        exit(1);
    }

    sock_set_reuseaddr(csocket);

    memset(&csocket_addr, 0, csocket_length);

    csocket_addr.sin_family = AF_INET;
    csocket_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    csocket_addr.sin_port = htons(COMMAND_PORT);

    if (bind(csocket, (struct sockaddr*)&csocket_addr, csocket_length) < 0)
    {
        sock_perror("hw_init: bind socket failed for radio command socket");
        exit(1);
    }
    if (listen(csocket, 12) < 0)
    {
        sock_perror("Radio command socket listen failed");
        exit(1);
    }

    fprintf(stderr, "hw_init: radio command socket listening to port %d socket %d\n", ntohs(csocket_addr.sin_port), csocket);

    while (1) // shall - we - play - a - game?
    {
        struct sockaddr_in addr;
        socklen_t addr_length = sizeof(addr);
        int slot;
        int s = accept(csocket, (struct sockaddr*)&addr, &addr_length);

        if (s < 0)
        {
            if (sock_error() != SOCK_EINTR)
                sock_perror("Radio command socket accept failed");
            continue;
        }
        for (slot = 0; slot < MAX_RADIO_CONNECTIONS && radio[slot].in_use; slot++)
            ;
        if (slot == MAX_RADIO_CONNECTIONS)
        {
            fprintf(stderr, "radio connection from %s refused: already %d radio programs connected\n",
                    inet_ntoa(addr.sin_addr), MAX_RADIO_CONNECTIONS);
            sock_close(s);
            continue;
        }
        radio[slot].socket = s;
        radio[slot].iq_addr = addr;
        radio[slot].iq_length = addr_length;
        radio[slot].connected = false;
        radio[slot].in_use = true;
        if (pthread_create(&radio[slot].thread_id, NULL, command_thread, (void*)(intptr_t)slot) != 0)
        {
            perror("radio command socket pthread_create failed");
            sock_close(s);
            radio[slot].in_use = false;
            continue;
        }
        pthread_detach(radio[slot].thread_id);
    }
    fprintf(stderr, "hw_command_thread stopped.\n");
} // end hw_command_listener_thread


/* --------------------------------------------------------------------------*/
/** 
* @brief Initialize hw Server
* 
* @return 
*/
int hw_init()
{
    pthread_t thread_id;
    int rc;
    struct hostent *h;
    char server_address[] = "127.0.0.1";

    local_audio = 0;
    radioMic = 0;

    for (int i=0;i<MAX_RFSTREAMS;i++)
        tx_iq_length[i] = sizeof(tx_iq_addr[0]);

    rc = sem_init(&hw_send_semaphore, 0, 1);
    if (rc < 0)
    {
        perror("hardware send sem_init failed");
    }

    rc = sem_init(&hw_cmd_semaphore, 0, 1);
    if (rc < 0)
    {
        perror("hw command semaphore init failed");
    }

    // create the thread to listen for TCP connections
    rc = pthread_create(&thread_id, NULL, hw_command_listener_thread, NULL);
    if (rc < 0)
    {
        perror("pthread_create listener_thread failed");
        exit(1);
    }


    h = gethostbyname(server_address);
    if (h == NULL)
    {
        fprintf(stderr, "hw_init: unknown host %s\n", server_address);
        exit(1);
    }

    // create sample rate subobject
    hw_set_src_ratio();
    int sr_error;
    sr_state = src_new (
                //SRC_SINC_BEST_QUALITY,  // NOT USABLE AT ALL on Atom 300 !!!!!!!
                //SRC_SINC_MEDIUM_QUALITY,
                SRC_SINC_FASTEST,
                //SRC_ZERO_ORDER_HOLD,
                //SRC_LINEAR,
                2, &sr_error
                ) ;

    if (sr_state == 0)
    {
        fprintf (stderr, "hw_init: SR INIT ERROR: %s\n", src_strerror (sr_error));
    }
    else
    {
        fprintf (stderr, "hw_init: sample rate init successfully at ratio: %f\n", src_ratio);
    }

    return rc;
} // end hw_init


void hw_set_src_ratio(void)
{
    src_ratio = (double)audiostream_conf.samplerate / ((double)sampleRate);
    fprintf(stderr, "Src_ratio: %f    %d\n", src_ratio, audiostream_conf.samplerate);
} // end hw_set_src_ratio


void hw_set_canTx(char state)
{
    radioMic = !state;
} // end hw_set_canTx


/* A radio's part of the clients' manifest, with dspserver's radio id.  Call
 * with reg_lock held. */
static void reg_make_xml(int g)
{
    REGISTRATION *r = &reg[g];
    size_t size = strlen(r->body) + 128;
    char *xml = malloc(size);
    char *name = strstr(r->body, "<name>");

    if (xml == NULL)
        return;
    if (r->connection < 0 && name != NULL)
    {   // offline: say so in the name, so the numbering of the radios stays the same
        char *end = strstr(name, "</name>");
        int head = (int)(name - r->body) + 6;
        snprintf(xml, size, "<radio=%d>\n%.*s%.*s (offline)%s</radio>\n", g, head, r->body,
                 end != NULL ? (int)(end - name - 6) : 0, name + 6, end != NULL ? end : "</name>\n");
    }
    else
        snprintf(xml, size, "<radio=%d>\n%s</radio>\n", g, r->body);
    free(r->xml);
    r->xml = xml;
} // end reg_make_xml


/* One radio of a manifest: find its registration (a radio that was here
 * before and is offline, of the same kind) or make a new one, and point its
 * streams at this connection. */
static void register_radio(int slot, int local_id, const char *radio_type, int protocol, int num_rcvrs, int t,
                           bool bs_capable, const char *ip_addr, const char *body)
{
    int g;

    pthread_mutex_lock(&reg_lock);
    for (g = 0; g < reg_count; g++)
        if (reg[g].used && reg[g].connection < 0 && reg[g].local_id == local_id && reg[g].receivers == num_rcvrs
                && reg[g].transmitters == t && strcmp(reg[g].radio_type, radio_type) == 0)
            break;
    if (g == reg_count)
    {
        if (reg_count >= MAX_RADIOS || active_rfstreams + num_rcvrs + t > WIDEBAND_CHANNEL - MAX_RADIOS + 1)
        {
            pthread_mutex_unlock(&reg_lock);
            fprintf(stderr, "radio %s (id %d in its program) not added: dspserver has no room for more radios\n", radio_type, local_id);
            return;
        }
        reg[g].used = true;
        reg[g].local_id = (int8_t)local_id;
        snprintf(reg[g].radio_type, sizeof(reg[g].radio_type), "%s", radio_type);
        reg[g].receivers = num_rcvrs;
        reg[g].transmitters = t;
        reg[g].first_stream = active_rfstreams;
        reg[g].streams = num_rcvrs + t;
        reg[g].body = NULL;
        reg[g].xml = NULL;
        for (int x = 0; x < num_rcvrs + t; x++)
        {
            RFSTREAM *rs = &rfstream[active_rfstreams];
            rs->id = active_rfstreams;
            rs->dsp_channel = active_rfstreams;    // WDSP channels are dspserver wide
            rs->index = x;                         // the receiver / transmitter number in the radio
            rs->isTX = x >= num_rcvrs;
            rs->enabled = false;
            rs->spectrum.samples = NULL;
            active_rfstreams++;
        }
        reg_count++;
        fprintf(stderr, "radio %d: %s, id %d in its radio program, streams %d..%d\n", g, radio_type, local_id,
                reg[g].first_stream, reg[g].first_stream + reg[g].streams - 1);
    }
    else
        fprintf(stderr, "radio %d: %s back, streams %d..%d\n", g, radio_type, reg[g].first_stream,
                reg[g].first_stream + reg[g].streams - 1);
    reg[g].connection = (int8_t)slot;
    free(reg[g].body);
    reg[g].body = strdup(body);
    for (int x = 0; x < reg[g].streams; x++)
    {
        RFSTREAM *rs = &rfstream[reg[g].first_stream + x];
        rs->protocol = protocol;
        rs->radio.radio_id = (int8_t)g;
        rfstream_local_id[reg[g].first_stream + x] = (int8_t)local_id;
        rs->radio.connection = (int8_t)slot;
        rs->radio.bandscope_capable = bs_capable;
        snprintf(rs->radio.radio_type, sizeof(rs->radio.radio_type), "%s", radio_type);
        snprintf(rs->radio.ip_address, sizeof(rs->radio.ip_address), "%s", ip_addr);
    }
    reg_make_xml(g);
    pthread_mutex_unlock(&reg_lock);
} // end register_radio


/* A radio program's connection closed: its radios stay registered, offline. */
void hw_radio_disconnected(int slot)
{
    pthread_mutex_lock(&reg_lock);
    for (int g = 0; g < reg_count; g++)
    {
        if (!reg[g].used || reg[g].connection != slot)
            continue;
        reg[g].connection = -1;
        for (int x = 0; x < reg[g].streams; x++)
            rfstream[reg[g].first_stream + x].radio.connection = -1;
        reg_make_xml(g);
        fprintf(stderr, "radio %d (%s) offline\n", g, reg[g].radio_type);
    }
    pthread_mutex_unlock(&reg_lock);
} // end hw_radio_disconnected


int hw_radio_count(void)
{
    return reg_count;
} // end hw_radio_count


/* The manifest for clients: every radio dspserver knows, in radio id order,
 * offline ones too (clients number the radios and their streams by counting
 * them), in one <radios=n> document.  The caller frees it. */
char *hw_clients_manifest(void)
{
    size_t size = 32;
    char *xml;

    pthread_mutex_lock(&reg_lock);
    for (int g = 0; g < reg_count; g++)
        if (reg[g].used && reg[g].xml != NULL)
            size += strlen(reg[g].xml);
    xml = malloc(size);
    if (xml != NULL)
    {
        int n = 0;
        for (int g = 0; g < reg_count; g++)
            if (reg[g].used && reg[g].xml != NULL)
                n++;
        snprintf(xml, size, "<radios=%d>\n", n);
        for (int g = 0; g < reg_count; g++)
            if (reg[g].used && reg[g].xml != NULL)
                strcat(xml, reg[g].xml);
        strcat(xml, "</radios>\n");
    }
    pthread_mutex_unlock(&reg_lock);
    return xml;
} // end hw_clients_manifest


int hw_radio_stream(int8_t radio_id)
{
    if (radio_id < 0 || radio_id >= reg_count || !reg[radio_id].used || reg[radio_id].receivers < 1)
        return -1;
    return reg[radio_id].first_stream;
} // end hw_radio_stream


int hw_port_offset(int stream)
{
    int c;

    if (stream < 0 || stream >= MAX_RFSTREAMS)
        return 0;
    c = rfstream[stream].radio.connection;
    return c < 0 ? 0 : c * PORT_BLOCK;
} // end hw_port_offset


/* The radios of a manifest: <radios=n> <radio=id> ... </radio> ... */
void createChannels(int slot, char *manifest)
{
    char line[80];
    char ip_addr[16] = "";
    int  radio_id = 0;
    char radio_type[25] = "";
    int  num_rcvrs = 0;
    int  protocol = 0;
    int  t = 0;
    bool bs_capable = false;
    bool in_radio = false;
    char *body = NULL;
    size_t body_len = 0;
    char *p = manifest;

    while (*p != '\0')
    {
        char *eol = strchr(p, '\n');
        size_t raw_len = eol != NULL ? (size_t)(eol - p) : strlen(p);
        int index = 0;

        // the line as words: "<radio=0>" -> "radio 0", "<protocol>3</protocol>" -> "protocol 3 /protocol"
        for (size_t i = 0; i < raw_len; i++)
        {
            char c = p[i];
            if (c == '=' || c == '<' || c == '>')
            {
                if (index != 0 && index < 79)
                    line[index++] = ' ';
            }
            else if (!(c == ' ' && index == 0) && index < 79)
                line[index++] = c;
        }
        line[index] = 0;

        if (strncmp(line, "radio ", 6) == 0)
        {
            sscanf(line, "%*s %d", &radio_id);
            in_radio = true;
            body_len = 0;
            free(body);
            body = calloc(1, 1);
            strcpy(radio_type, "unknown");
            ip_addr[0] = '\0';
            num_rcvrs = t = protocol = 0;
            bs_capable = false;
        }
        else if (in_radio && strncmp(line, "/radio", 6) == 0)
        {
            if (body != NULL)
                register_radio(slot, radio_id, radio_type, protocol, num_rcvrs, t, bs_capable, ip_addr, body);
            in_radio = false;
        }
        else if (in_radio)
        {
            char *nb = realloc(body, body_len + raw_len + 2);
            if (nb != NULL)
            {
                body = nb;
                memcpy(body + body_len, p, raw_len);
                body_len += raw_len;
                body[body_len++] = '\n';
                body[body_len] = '\0';
            }
            if (strncmp(line, "radio_type ", 11) == 0)
                sscanf(line, "%*s %24s", radio_type);
            else if (strncmp(line, "protocol ", 9) == 0)
                sscanf(line, "%*s %d", &protocol);
            else if (strncmp(line, "supported_receivers ", 20) == 0)
                sscanf(line, "%*s %d", &num_rcvrs);
            else if (strncmp(line, "bandscope ", 10) == 0)
            {
                int tmp = 0;
                sscanf(line, "%*s %d", &tmp);
                bs_capable = tmp == 1;
            }
            else if (strncmp(line, "supported_transmitters ", 23) == 0)
                sscanf(line, "%*s %d", &t);
            else if (strncmp(line, "ip_address ", 11) == 0)
                sscanf(line, "%*s %15s", ip_addr);
        }
        p += raw_len;
        if (*p == '\n')
            p++;
    }
    free(body);

    fprintf(stderr, "Active DSP channels = %d\n", active_rfstreams);
    for (int i=0;i<active_rfstreams;i++)
        fprintf(stderr, "DSP Channel = %d  isTX = %d  Radio %d (%d in its program)  Slot Index = %d  Connection = %d  Type = %s\n",
                rfstream[i].dsp_channel, (int)rfstream[i].isTX, rfstream[i].radio.radio_id, rfstream_local_id[i],
                rfstream[i].index, rfstream[i].radio.connection, rfstream[i].radio.radio_type);
} // end createChannels


/* Read exactly n bytes from a radio program; false on error or close. */
static bool recv_full(int s, char *buf, int n)
{
    int got = 0;

    while (got < n)
    {
        ssize_t r = recv(s, buf + got, (size_t)(n - got), 0);
        if (r <= 0)
        {
            if (r < 0 && sock_error() == SOCK_EINTR)
                continue;
            return false;
        }
        got += (int)r;
    }
    return true;
} // end recv_full


/* Ask a radio program for its manifest.  The request carries the
 * connection's slot: the radio program adds slot * PORT_BLOCK to its UDP
 * ports.  Returns 0 when the radios are set up, -1 on failure. */
int hw_get_manifest(int connection)
{
    char command[5];
    int len = 0;
    char *xml;

    fprintf(stderr, "requesting manifest....");
    command[0] = (char)0x7f;
    command[1] = (char)3;
    command[2] = (char)0;                // index
    command[3] = (char)QHARDWARE;
    command[4] = (char)connection;       // port block
    sem_wait(&hw_cmd_semaphore);
    if (send(radio[connection].socket, command, 5, MSG_NOSIGNAL) != 5 || !recv_full(radio[connection].socket, (char*)&len, sizeof(int)))
    {
        sem_post(&hw_cmd_semaphore);
        fprintf(stderr, "manifest request failed on slot %d\n", connection);
        return -1;
    }
    if (len <= 0 || len > 65536)
    {
        sem_post(&hw_cmd_semaphore);
        fprintf(stderr, "manifest request: bad length %d on slot %d\n", len, connection);
        return -1;
    }
    xml = (char*)malloc(len + 1);
    if (xml == NULL || !recv_full(radio[connection].socket, xml, len))
    {
        sem_post(&hw_cmd_semaphore);
        free(xml);
        fprintf(stderr, "manifest request: reading the manifest failed on slot %d\n", connection);
        return -1;
    }
    sem_post(&hw_cmd_semaphore);
    xml[len] = 0;
    fprintf(stderr, "XML len: %d\n", len);
    createChannels(connection, xml);
    fprintf(stderr, "\nXML: %s", xml);
    free(xml);
    return 0;
} // end hw_get_manifest


