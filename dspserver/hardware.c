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
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <samplerate.h>
#include <stdbool.h>

#include "server.h"
#include "hardware.h"
#include "wdsp.h"
#include "audiostream.h"
#include "server.h"
#include "util.h"
#include "dsp.h"


static sem_t hw_send_semaphore;
static sem_t hw_cmd_semaphore;
extern sem_t iq_semaphore;
extern sem_t wb_iq_semaphore;

// Added by Alex lee 18 Aug 2010
double LO_offset = 0; // 9000;  // LO offset 9khz

//short int connected_radios = 0;
short int active_channels = 0;
extern int8_t active_receivers;
extern int8_t active_transmitters;
static pthread_t iq_thread_id[MAX_CHANNELS];
static pthread_t wb_iq_thread_id[5];

static int hw_debug = 0;

RADIO radio[5];
//int iq_socket = -1;

//int buffer_size = BUFFER_SIZE;
//float input_buffer[BUFFER_SIZE*2]; // I,Q
//float output_buffer[BUFFER_SIZE*2];

//float mic_left_buffer[BUFFER_SIZE];
//float mic_right_buffer[BUFFER_SIZE];

int samples = 0;

short left_rx_sample;
short right_rx_sample;
short left_tx_sample;
short right_tx_sample;

//int frames = 0;

int show_software_serial_numbers = 1;

unsigned char iq_samples[SPECTRUM_BUFFER_SIZE];

int sampleRate = 48000;  // default 48k
//int mox = 0;             // default not transmitting

//int command_socket;
//int command_port;
//static struct sockaddr_in command_addr;
//static socklen_t command_length = sizeof(command_addr);

int tx_iq_socket[MAX_CHANNELS];
struct sockaddr_in tx_iq_addr[MAX_CHANNELS];
socklen_t tx_iq_length[MAX_CHANNELS];

struct sockaddr_in server_audio_addr;
socklen_t server_audio_length = sizeof(server_audio_addr);

int session;

int radioMic = 0;

char *manifest_xml[5];

//static int local_audio = 0;
//static int port_audio = 0;

//
// samplerate library data structures
//
SRC_STATE *sr_state;
double src_ratio;

void dump_udp_buffer(unsigned char* buffer);
double swap_Endians(double value);
void hw_get_manifest(int);

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


void* iq_thread(void* channel)
{
    int ch = (intptr_t)channel;
    struct sockaddr_in iq_addr;
    socklen_t iq_length = sizeof(iq_addr);
    BUFFERL buffer;
    int on = 1;
    static float data_in[2048];
    static float data_out[2048];

    // create a socket to receive iq from the server
    int iq_socket = socket(PF_INET,SOCK_DGRAM, IPPROTO_UDP);
    if (iq_socket < 0)
    {
        perror("iq_thread: create iq socket failed");
        exit(1);
    }

    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = 10;
    setsockopt(iq_socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);
    setsockopt(iq_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&iq_addr, 0, iq_length);
    iq_addr.sin_family = AF_INET;
    iq_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    iq_addr.sin_port = htons(RX_IQ_PORT_0 + channels[ch].index);

    if (bind(iq_socket, (struct sockaddr*)&iq_addr, iq_length) < 0)
    {
        perror("iq_thread: bind socket failed for iq socket");
        exit(1);
    }

    fprintf(stderr, "iq_thread: iq bound to port %d socket=%d  Ch: %d\n", htons(iq_addr.sin_port), iq_socket, ch);
    fprintf(stderr, "audiostream_conf.samplerate=%d\n", audiostream_conf.samplerate);

    while (!getStopIQIssued())
    {
        int bytes_read;
        if (channels[ch].radio.mox) continue;

        bytes_read = recvfrom(iq_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&iq_addr, &iq_length);
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
        double dataout[2048];

        if (bUseNB && channels[ch].enabled)
            runXanbEXT(ch, buffer.data);
        if (bUseNB2 && channels[ch].enabled)
            runXnobEXT(ch, buffer.data);

        int error = 0;
        if (channels[ch].enabled)
            error = runFexchange0(ch, buffer.data, dataout);
        if (error != 0 && error != -2)
            printf("fexchange error: %d\n", error);

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

            rc = src_process(sr_state, &data);
            if (rc)
            {
                fprintf(stderr,"SRATE: error: %s (rc=%d)\n", src_strerror (rc), rc);
                fprintf(stderr, "i: %d   Frames: %ld   SR: %f\n", i, data.output_frames_gen, src_ratio);
                fprintf(stderr, "Bytes read: %d\n", bytes_read);
                //     exit(1);
            }
            else
            {
                for (int i=0; i < data.output_frames_gen; i++)
                {
                    left_rx_sample = (short)(data.data_out[i*2]*32767.0);
                    right_rx_sample = (short)(data.data_out[i*2+1]*32767.0);
                    // FIXME: need option to send audio to radio speaker if hardware capable.
                    audio_stream_put_samples(ch, left_rx_sample, right_rx_sample);  // send audio to client computer
                    //fprintf(stderr, "audio ch: %d\n", ch);
                }
            } // if (rc)
        } // audio_enabled

        if (channels[ch].enabled)
            runSpectrum0(ch, buffer.data);

        sem_post(&iq_semaphore);
    } // end while
    close(iq_socket);
    fprintf(stderr, "******************** IQ thread closed.\n");
    return 0;
} // end iq_thread


void* wb_iq_thread(void* channel)
{
    int ch = (intptr_t)channel;
    struct sockaddr_in iq_addr;
    int iq_socket = -1;
    socklen_t iq_length = sizeof(iq_addr);
    BUFFERWB buffer;
    int on = 1;
    int offset = 0;
    int16_t *data = NULL;
    double *samples = NULL;

    // create a socket to receive iq from the server
    iq_socket = socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (iq_socket < 0)
    {
        perror("wb_q_thread: create iq socket failed");
        exit(1);
    }

    setsockopt(iq_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&iq_addr, 0, iq_length);
    iq_addr.sin_family = AF_INET;
    iq_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    iq_addr.sin_port = htons(BANDSCOPE_PORT + channels[ch].radio.radio_id);   //  Fix channels

    if (bind(iq_socket, (struct sockaddr*)&iq_addr, iq_length) < 0)
    {
        perror("wb_iq_thread: bind socket failed for iq socket");
        exit(1);
    }

    fprintf(stderr, "wb_iq_thread: iq bound to port %d socket=%d  Ch: %d\n", htons(iq_addr.sin_port), iq_socket, ch);

    data = malloc(sizeof(int16_t)*16384);
    samples = malloc(sizeof(double)*(16384*2));

    channels[ch].dsp_channel = ch;

    while (!getStopIQIssued())
    {
        int bytes_read;
        if (channels[ch].radio.mox || channels[ch].spectrum.type != BS) continue;
        bytes_read = recvfrom(iq_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&iq_addr, &iq_length);
        if (bytes_read < 0)
        {
            perror("recvfrom socket failed for wideband iq buffer");
            exit(1);
        }

  //      sem_wait(&wb_iq_semaphore);
        if (channels[ch].enabled)
        {
   //         sem_post(&wb_iq_semaphore);
     //       fprintf(stderr, "Br: %d\n", bytes_read);
            memcpy((char*)data, (char*)buffer.data, buffer.length * 2);
            offset = offset + buffer.length;
            if (offset >= 16384)
            {
                int i = 0;
#pragma omp parallel for schedule(static) private(i)
                for (i=0;i<16384;i++)
                {
                    samples[i*2] = data[i]/32768.0f;
                    samples[(i*2)+1] = 0.0f;
                }
            //    fprintf(stderr, "DSP: %u\n", channels[ch].dsp_channel);
                sem_wait(&iq_semaphore);
                runSpectrum0(ch, samples);
             //   Spectrum0(1, channels[ch].dsp_channel, 0, 0, samples);
                sem_post(&iq_semaphore);
                offset = 0;
            }
        }
    } // end while
 //   setStopIQIssued(false);
    channels[ch].enabled = false;
    channels[ch].dsp_channel = -1;
    close(iq_socket);
    free(data);
    free(samples);
    fprintf(stderr, "****** Wideband IQ thread for ch: %d closed.\n", ch);
    return 0;
} // end wb_iq_thread


void hw_send(unsigned char* data, int length, int ch)
{
    BUFFER  buffer;

//    iq_monitor(&buffer.data[0], length/8);
    //fprintf(stderr,"hw_send: %s\n",who);
    sem_wait(&hw_send_semaphore);
    for (int i=0;i<length/512;i++)
    {
        buffer.chunk = i;
        buffer.radio_id = channels[ch].radio.radio_id;
        buffer.receiver = channels[ch].index;
        buffer.length = length;
        memcpy((char*)&buffer.data[0], (char*)&data[512*i], 512);

        int bytes_written = sendto(tx_iq_socket[ch], (char*)&buffer, sizeof(buffer), 0, (struct sockaddr *)&tx_iq_addr[ch], tx_iq_length[ch]);
        if (bytes_written < 0)
        {
            perror("socket error: ");
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
        exit(1);
    }

    /* FIXME: This is broken.  It will probably work as long as
     * responses are very small and everything proceeds in lockstep. */
    rc = recv(radio[connection].socket, response, HW_RESPONSE_SIZE, 0);
    sem_post(&hw_cmd_semaphore);
    if (rc < 0)
    {
        fprintf(stderr, "read response failed: %d\n", rc);
    }
    /* FIXME: This is broken, too.  If the response is exactly
     * HW_RESPONSE_SIZE, we have to truncate it by one byte. */
    if (rc == HW_RESPONSE_SIZE)
        rc--;
    response[rc] = 0;

    fprintf(stderr, "send_command [%u]: response='%s'\n", (uint8_t)output[3], response);
} // end send_command


void* keepalive_thread(void* arg)
{
    char command[128], response[HW_RESPONSE_SIZE];
    sprintf(command, "keepalive 0");
    while (1)
    {
        fprintf(stderr, "keepalive\n");
        sleep(5);
  //      send_command(command, strlen(command), response);
    }
} // end keepalive_thread


int make_connection(short int channel)
{
    char *token, *saveptr;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;

    setStopIQIssued(false);
    result = 1;
    if (!channels[channel].isTX)
    {
        fprintf(stderr, "ATTACH RX\n");
        command[0] = (char)channels[channel].index;
        command[1] = ATTACHRX;
        command[2] = (char)channels[channel].radio.radio_id;
        //sprintf(command, "connect %d %d", receiver, IQ_PORT+(receiver*2));
        send_command(channels[channel].radio.connection, command, 3, response);
        fprintf(stderr, "Attach RX resp: %s\n", response);
        token = strtok_r(response, " ", &saveptr);
        if (token != NULL)
        {
            if (strcmp(token, "OK") == 0)
            {
                token=strtok_r(NULL, " ", &saveptr);
                if (token != NULL)
                {
                    result = 1;
                    sampleRate = 48000; //////////atoi(token);
                    fprintf(stderr, "connect: sampleRate=%d\n", sampleRate);
                    setSpeed(channel, sampleRate);
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
        tx_init();
        fprintf(stderr, "ATTACH TX\n");
        command[0] = (char)channels[channel].index;
        command[1] = (char)ATTACHTX;
        command[2] = (char)channels[channel].radio.radio_id;
        send_command(channels[channel].radio.connection, command, 3, response);
        fprintf(stderr, "Attach TX resp: %s\n", response);
        token = strtok_r(response, " ", &saveptr);
        if (token != NULL)
        {
            if (strcmp(token, "OK") == 0)
            {
                result = 1;
                tx_iq_addr[channel] = radio[channels[channel].radio.connection].iq_addr;
                tx_iq_length[channel] = radio[channels[channel].radio.connection].iq_length;
                recvfrom(tx_iq_socket[channel], (char*)&command, 2, 0, (struct sockaddr*)&tx_iq_addr[channel], (socklen_t *)&tx_iq_length[channel]);
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


void hwDisconnect(int8_t channel) // FIXME: This function is not used because I'm not sure if it's needed yet.
{
    char command[64], response[HW_RESPONSE_SIZE];

    sprintf(command, "%c%c%c", (char)channels[channel].index, DETACH, (char)channels[channel].radio.radio_id);
 //   send_command(command, 3, response);

    close(radio[channels[channel].radio.radio_id].socket);
    close(tx_iq_socket[channel]);
} // end hwDisconnect


int hwSetSampleRate(int channel, long rate)
{
    char *token;
    char srate[10];
    char command[64], response[HW_RESPONSE_SIZE];
    int result;
    char *saveptr;

    result = 0;
    command[0] = (char)channels[channel].index;
    command[1] = (char)SETSAMPLERATE;
    sprintf(srate, "%ld", rate);
    memcpy(command+2, srate, strlen(srate)+1);
    send_command(channels[channel].radio.connection, command, strlen(srate)+3, response);
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
    sprintf(command, "%c%c%lld", channels[ch].index, SETFREQ, (frequency - (long long)LO_offset));
    send_command(channels[ch].radio.connection, command, 64, response);
    token = strtok_r(response, " ", &saveptr);
    if (token != NULL)
    {
        if (strcmp(token, "OK") == 0)
        {
            result = 0;
            lastFreq = frequency;
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


int hwSetRecord(int8_t ch, char* state)
{
    char *token, *saveptr;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;

    result=0;
    sprintf(command,"%c%c%c", channels[ch].index, SETRECORD, state[0]);
    send_command(channels[ch].radio.connection, command, 64, response);
    token=strtok_r(response, " ", &saveptr);
    if (token != NULL)
    {
        if (strcmp(token, "OK") == 0)
        {
            result=0;
        }
        else
            if (strcmp(token, "ERROR") == 0)
            {
                result=1;
            }
            else
            {
                fprintf(stderr,"invalid response to record: %s\n",response);
                result=1;
            }
    }

    return result;
} // end hwSetRecord


int hwSetMox(int8_t ch, int state)
{
    char *token, *saveptr;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;

    result = 0;
//    mox = state;
    command[0] = (char)channels[ch].index;
    command[1] = (char)MOX;
    command[2] = (char)state;
    send_command(channels[ch].radio.connection, command, 3, response);
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
    buf[0] = (char)channels[ch].index;
    memcpy(buf+1, command, len);
    send_command(channels[ch].radio.connection, (char*)buf, len+1, response);
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
void setSpeed(int channel, int s)
{
    fprintf(stderr, "setSpeed %d on rx: %d\n", s, channels[channel].dsp_channel);
    fprintf(stderr, "LO_offset %f\n", LO_offset);

    sampleRate = s;

    SetDSPSamplerate(channels[channel].dsp_channel, sampleRate);

    // SetRXAShiftFreq(0, -LO_offset);
    //SetRXAShiftFreq(1, -LO_offset);

    fprintf(stderr, "%s:  rx: %d  %f\n", __FUNCTION__, channels[channel].dsp_channel, (double)sampleRate);
    hw_set_src_ratio();
} // end setSpeed


void hw_startIQ(int channel)
{
    int ch = channel;
    int on = 1;

    if (channels[ch].isTX)
    {
        // create a socket to send TX IQ to the server
        tx_iq_socket[ch] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (tx_iq_socket[ch] < 0)
        {
            perror("hw_init: create tx_iq socket failed");
            exit(1);
        }
        setsockopt(tx_iq_socket[ch], SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        memset(&tx_iq_addr[ch], 0, tx_iq_length[ch]);
        tx_iq_addr[ch].sin_family = AF_INET;
        tx_iq_addr[ch].sin_addr.s_addr = htonl(INADDR_ANY);
        tx_iq_addr[ch].sin_port = htons(TX_IQ_PORT_0); // + channels[ch].index);

        if (bind(tx_iq_socket[ch], (struct sockaddr*)&tx_iq_addr[ch], tx_iq_length[ch]) < 0)
        {
            perror("hw_init: bind socket failed for tx_iq socket");
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

    if (channels[WIDEBAND_CHANNEL - channels[ch].radio.radio_id].dsp_channel < 0)
    {
        channels[WIDEBAND_CHANNEL - channels[ch].radio.radio_id].radio.radio_id = channels[ch].radio.radio_id;
        rc = pthread_create(&wb_iq_thread_id[WIDEBAND_CHANNEL - channels[ch].radio.radio_id], NULL, wb_iq_thread, (void*)(intptr_t)(WIDEBAND_CHANNEL - channels[ch].radio.radio_id));
        if (rc != 0)
        {
            fprintf(stderr,"pthread_create failed on wb_iq_thread: rc=%d\n", rc);
        }
    }
} // end hw_startIQ


void hw_stopIQ()
{
    setStopIQIssued(true);
} // end hw_stopeIQ


void* command_thread(void* arg)
{
    char buffer[13];
    int bytes_read = 0;
    int connected = connected_radios;

    fprintf(stderr, "radio connecting: %s:%d  socket: %d\n", inet_ntoa(radio[connected_radios].iq_addr.sin_addr), ntohs(radio[connected_radios].iq_addr.sin_port), radio[connected_radios].socket);
    if (connected_radios == 4)
    {
        close(radio[connected_radios].socket);
    //    free(radio[connected_radios]);
    //    radio[connected_radios] = NULL;
        connected_radios--;
        return 0;
    }
    hw_get_manifest(connected_radios);
    connected_radios++;

    while (1)
    {
/*        bytes_read = recv(radio[connected].socket, buffer, 13, 0);
        if (bytes_read < 0)
        {  // recv error, exit program.
            if (errno != EWOULDBLOCK)
            {
                fprintf(stderr, "command channel error: %s  (%d)\n", strerror(errno), errno);
            }
        }
        if (bytes_read > 0)
        {
            if (strcmp(buffer, "keepalive 0") == 0)
            {
                strcpy(buffer, "OK");
                send(radio[connected].socket, buffer, strlen(buffer)+1, 0);
            }
        } */
        usleep(5000);
    }

    fprintf(stderr, "radio command thread exited\n");
    close(radio[connected_radios-1].socket);
  //  free(radio[connected_radios-1]);
  //  radio[connected_radios-1] = NULL;
    connected_radios--;
    return 0;
} // end command_thread


void* hw_command_listener_thread(void* arg)
{
    int csocket=0;
    struct sockaddr_in csocket_addr;
    socklen_t csocket_length = sizeof(csocket_addr);
    int rc;
    int on=1;
    
    fprintf(stderr, "Starting radio command port.\n");
    // create a TCP socket to send commands to the server
    csocket = socket(AF_INET,SOCK_STREAM, 0);
    if (csocket < 0)
    {
        perror("hw_init: create radio command socket failed");
        exit(1);
    }

    setsockopt(csocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&csocket_addr, 0, csocket_length);

    csocket_addr.sin_family = AF_INET;
    csocket_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    csocket_addr.sin_port = htons(COMMAND_PORT);

    if (bind(csocket, (struct sockaddr*)&csocket_addr, csocket_length) < 0)
    {
        perror("hw_init: bind socket failed for radio command socket");
        exit(1);
    }

    fprintf(stderr, "hw_init: radio command socket listening to port %d socket %d\n", ntohs(csocket_addr.sin_port), csocket);

    while (1) // shall - we - play - a - game?
    {
        if (listen(csocket, 12) < 0)
        {
            perror("Radio command socket listen failed");
            exit(1);
        }

        if ((radio[connected_radios].socket = accept(csocket, (struct sockaddr*)&radio[connected_radios].iq_addr, &radio[connected_radios].iq_length)) < 0)
        {
            perror("Radio command socket accept failed");
       //     free(radio[connected_radios]);
            exit(1);
        }

        rc = pthread_create(&radio[connected_radios].thread_id, NULL, command_thread, NULL);
        if (rc < 0)
        {
            perror("radio command socket pthread_create failed");
       //     free(radio[connected_radios]);
            exit(1);
        }
    }
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

    for (int i=0;i<MAX_CHANNELS;i++)
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


/* --------------------------------------------------------------------------*/
/** 
* @brief Close hw
* 
* @return 
*/
void hwClose()
{
    src_delete(sr_state);
} // end hwClose


void hw_set_local_audio(int state)
{
    //    local_audio = state;
} // end hw_set_local_audio


void hw_set_debug(int state)
{
    hw_debug = state;
} // end hw_set_debug


void dump_udp_buffer(unsigned char* buffer) 
{
    int i;
    fprintf(stderr, "udp ...\n");
    for(i=0; i<1024; i+=16)
    {
        fprintf(stderr, "  [%04X] %02X%02X%02X%02X%02X%02X%02X%02X %02X%02X%02X%02X%02X%02X%02X%02X\n",
                i,
                buffer[i],buffer[i+1],buffer[i+2],buffer[i+3],buffer[i+4],buffer[i+5],buffer[i+6],buffer[i+7],
                buffer[i+8],buffer[i+9],buffer[i+10],buffer[i+11],buffer[i+12],buffer[i+13],buffer[i+14],buffer[i+15]
                );
    }
    fprintf(stderr, "\n");
} // end dump_upd_buffer


void hw_set_canTx(char state)
{
    radioMic = !state;
} // end hw_set_canTx


void createChannels(int connecton, char *manifest)
{
    char line[80];
    int  index = 0;
    int  radio_id = 0;
    char radio_type[25];
    int  num_chs = 0;
    int  num_rcvrs = 0;
    bool bs_capable = false;


    for (int i=0;i<strlen(manifest);i++)
    {
        if (manifest[i] != 10)
        {
            if (manifest[i] == '=' || manifest[i] == '<' || manifest[i] == '>')
            {
                if (index != 0 && index < 79)
                    line[index++] = ' ';
            }
            else
            {
                if (manifest[i] == ' ' && index == 0)
                    ; // do nothing
                else
                    if (index < 79)
                        line[index++] = manifest[i];
            }
        }
        else
        {
            line[index] = 0;
            index = 0;
            fprintf(stderr, "%s\n", line);
            if (strstr(line, "radio "))
            {
        //        fprintf(stderr, "%s\n", line);
                if (active_channels >= MAX_CHANNELS) break;
                sscanf(line, "%*s %d", &radio_id);
            }
            else
                if (strstr(line, "radio_type"))
                {
                    sscanf(line, "%*s %s %*s", radio_type);
                }
                else
                    if (strstr(line, "supported_receivers"))
                    {
                        sscanf(line, "%*s %d %*s", &num_rcvrs);
                    }
                    else
                        if (strstr(line, "bandscope"))
                        {
                            int tmp=0;
                            sscanf(line, "%*s %d %*s", &tmp);
                            if (tmp == 1)
                                bs_capable = true;
                            else
                                bs_capable = false;
                        }
                        else
                            if (strstr(line, "supported_transmitters"))
                            {
                                int x = 0, t = 0;
                                sscanf(line, "%*s %d %*s", &t);
                                for (;x<num_rcvrs;x++)
                                {   // setup receive channels
                                    if (active_channels >= MAX_CHANNELS) break;
                                    channels[active_channels].id = active_channels;
                                    channels[active_channels].radio.radio_id = radio_id;
                                    channels[active_channels].dsp_channel = num_chs++;
                                    channels[active_channels].index = x;
                                    channels[active_channels].isTX = false;
                                    channels[active_channels].radio.bandscope_capable = bs_capable;
                                    strcpy(channels[active_channels].radio.radio_type, radio_type);
                                    channels[active_channels].radio.connection = connecton;
                                    channels[active_channels].enabled = false;
                                    channels[active_channels].spectrum.samples = NULL;
                                    active_channels++;
                                }
                                for (;x<t+num_rcvrs;x++)
                                {   // setup transmit channels
                                    if (active_channels >= MAX_CHANNELS) break;
                                    channels[active_channels].id = active_channels;
                                    channels[active_channels].radio.radio_id = radio_id;
                                    channels[active_channels].dsp_channel = num_chs++;
                                    channels[active_channels].index = x;
                                    channels[active_channels].radio.bandscope_capable = bs_capable;
                                    strcpy(channels[active_channels].radio.radio_type, radio_type);
                                    channels[active_channels].radio.connection = connecton;
                                    channels[active_channels].isTX = true;
                                    channels[active_channels].enabled = false;
                                    channels[active_channels].spectrum.samples = NULL;
                                    active_channels++;
                                }
                            }
        } // end if
    } // end for

    fprintf(stderr, "Active DSP channels = %d\n", active_channels);
    for (int i=0;i<active_channels;i++)
        fprintf(stderr, "DSP Channel = %d  isTX = %d  Radio Slot Index = %d  Type = %s\n", channels[i].dsp_channel, (int)channels[i].isTX, channels[i].index, channels[i].radio.radio_type);
} // end createChannels


void hw_get_manifest(int connection)
{
    char command[4], response[HW_RESPONSE_SIZE];

    fprintf(stderr, "requesting manifest....");
    command[0] = (char)0x7f;
    command[1] = (char)2;
    command[2] = (char)0;
    command[3] = (char)QHARDWARE;
    sem_wait(&hw_cmd_semaphore);
    int rc = send(radio[connection].socket, command, 4, 0);
    if (rc < 0)
    {
        sem_post(&hw_cmd_semaphore);
        fprintf(stderr, "manifest request send command failed: Result: %d: Socket: %d  Command: %u\n", rc, radio[connection].socket, (uint8_t)command[2]);
        exit(1);
    }
    fprintf(stderr, "sent\n");

    rc = recv(radio[connection].socket, response, sizeof(int), 0);
    if (rc < 0)
    {
        fprintf(stderr, "manifest request read response failed: %d\n", rc);
    }
    int len;
    memcpy((int*)&len, (int*)&response, sizeof(int));
    fprintf(stderr, "XML len: %u\n", (uint8_t)len);
    manifest_xml[connection] = (char*)malloc(len+1);
    rc = recv(radio[connection].socket, manifest_xml[connection], len, 0);
    if (rc < 0)
    {
        fprintf(stderr, "manifest request read response failed: %d\n", rc);
    }
    sem_post(&hw_cmd_semaphore);
    manifest_xml[connection][len] = 0;
    createChannels(connection, manifest_xml[connection]);
    fprintf(stderr, "\nXML: %s", manifest_xml[connection]);
} // end hw_get_manifest


// Function to swap a value from 
// big Endian to little Endian and 
// vice versa. 

double swap_Endians(double dvalue)
{
    long leftmost_byte;
    long left_middle_byte;
    long right_middle_byte;
    long rightmost_byte;

    long tleftmost_byte;
    long tleft_middle_byte;
    long tright_middle_byte;
    long trightmost_byte;

    long value;
    memcpy((char*)&value,(char*)&dvalue, sizeof(double));

    long result;

    tleftmost_byte = (value & 0x00000000000000FF) >> 0;

    tleft_middle_byte = (value & 0x000000000000FF00) >> 8;

    tright_middle_byte = (value & 0x0000000000FF0000) >> 16;

    trightmost_byte = (value & 0x00000000FF000000) >> 24;

    leftmost_byte = (value & 0x000000FF00000000) >> 32;

    left_middle_byte = (value & 0x0000FF0000000000) >> 40;

    right_middle_byte = (value & 0x00FF000000000000) >> 48;

    rightmost_byte = (value & 0xFF00000000000000) >> 56;


    tleftmost_byte <<= 56;

    tleft_middle_byte <<= 48;

    tright_middle_byte <<= 40;

    trightmost_byte <<= 32;

    leftmost_byte <<= 24;

    left_middle_byte <<= 16;

    right_middle_byte <<= 8;

    rightmost_byte <<= 0;

    // Result is the concatenation of all these values.

    result = (tleftmost_byte | tleft_middle_byte | tright_middle_byte | trightmost_byte | leftmost_byte | left_middle_byte | right_middle_byte | rightmost_byte);
    double r=0.0;
    memcpy((char*)&r,(char*)&result, sizeof(long));
    //fprintf(stderr, "%f\n", dvalue);
    return r;
} // end swap_Endians
