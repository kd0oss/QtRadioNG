/*
 * File:   ozy.c
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

#include "hardware.h"
#include "wdsp.h"
#include "audiostream.h"
#include "server.h"
#include "util.h"


static sem_t hw_send_semaphore;
static sem_t hw_cmd_semaphore;

// Added by Alex lee 18 Aug 2010
double LO_offset = 0; // 9000;  // LO offset 9khz

short int connected_radios = 0;


short int active_channels;
static pthread_t iq_thread_id;

static int hw_debug = 0;

/** Response buffers passed to hw_send must be this size */
#define HW_RESPONSE_SIZE 64

#define BUFFER_SIZE 1024

#define COMMAND_PORT 10100 //12000
#define IQ_PORT      10000 // increment 1 per recv channel
#define AUDIO_PORT   15000

RADIO *radio[5];
CHANNEL channels[35];
int iq_socket = -1;

int buffer_size = BUFFER_SIZE;

float input_buffer[BUFFER_SIZE*2]; // I,Q
float output_buffer[BUFFER_SIZE*2];

float mic_left_buffer[BUFFER_SIZE];
float mic_right_buffer[BUFFER_SIZE];

int samples = 0;

int left_sample;
int right_sample;
int mic_sample;


float left_sample_float;
float right_sample_float;
float mic_sample_float;

short left_rx_sample;
short right_rx_sample;
short left_tx_sample;
short right_tx_sample;

int frames = 0;

int show_software_serial_numbers = 1;

unsigned char iq_samples[SPECTRUM_BUFFER_SIZE];

int sampleRate = 48000;  // default 48k
int mox = 0;             // default not transmitting

int command_socket;
int command_port;
static struct sockaddr_in command_addr;
static socklen_t command_length = sizeof(command_addr);

int audio_socket;
int audio_port;
struct sockaddr_in audio_addr;
socklen_t audio_length = sizeof(audio_addr);

struct sockaddr_in server_audio_addr;
socklen_t server_audio_length = sizeof(server_audio_addr);

static struct sockaddr_in server_addr;
static socklen_t server_length = sizeof(server_addr);

short server_port;

int session;

int rxOnly = 1;
int hardware_control = 0;

char *manifest_xml[4];

//static int local_audio = 0;
//static int port_audio = 0;

//
// samplerate library data structures
//
SRC_STATE *sr_state;
double src_ratio;

void dump_udp_buffer(unsigned char* buffer);
double swap_Endians(double value);
void hw_get_manifest();

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
} // end setStopIQIssued


void* iq_thread(void* channel)
{
    int ch = (int)channel;
    struct sockaddr_in iq_addr;
    int iq_length = sizeof(iq_addr);
    BUFFERL buffer;
    int on = 1;
    static float data_in[2048];
    static float data_out[2048];


    // create a socket to receive iq from the server
    iq_socket = socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (iq_socket < 0)
    {
        perror("iq_thread: create iq socket failed");
        exit(1);
    }

    setsockopt(iq_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&iq_addr, 0, iq_length);
    iq_addr.sin_family = AF_INET;
    iq_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    iq_addr.sin_port = htons(IQ_PORT+channels[ch].receiver);

    if (bind(iq_socket, (struct sockaddr*)&iq_addr, iq_length) < 0)
    {
        perror("iq_thread: bind socket failed for iq socket");
        exit(1);
    }

    fprintf(stderr, "iq_thread: iq bound to port %d socket=%d\n", htons(iq_addr.sin_port), iq_socket);
    fprintf(stderr, "audiostream_conf.samplerate=%d\n", audiostream_conf.samplerate);

    while (!getStopIQIssued())
    {
        int bytes_read;
        if (mox) continue;

        bytes_read = recvfrom(iq_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&iq_addr, (socklen_t *)&iq_length);
        if (bytes_read < 0)
        {
            perror("recvfrom socket failed for iq buffer");
            exit(1);
        }

        int rid = buffer.radio_id;
        int rcvr = buffer.receiver;

        int j, i;
        int scale=sampleRate/48000;
        //i=((buffer.length/2)/scale)*2;
        i = 1024;
        double dataout[2048];

        if (bUseNB)
            xanbEXT(0, buffer.data, buffer.data);
        if (bUseNB2)
            xnobEXT(0, buffer.data, buffer.data);

        int error = 0;
        fexchange0(0, buffer.data, dataout, &error);
        if (error != 0 && error != -2) printf("fexchange error: %d\n", error);

        // process the output with resampler
        int rc;
#pragma omp parallel for schedule(static) private(j)
        for (j=0; j < (i/2); j++)
        {
            data_in[j*2]   = (float)dataout[j*2];    // left_samples[i];
            data_in[j*2+1] = (float)dataout[j*2+1];  // right_samples[i];
        }

        SRC_DATA data;
        data.data_in = data_in;
        data.input_frames = i/2; //buffer_size;

        data.data_out = data_out;
        data.output_frames = i/2; //buffer_size;
        data.src_ratio = src_ratio;
        data.end_of_input = 0;

        rc = src_process(sr_state, &data);
        if (rc)
        {
            fprintf(stderr,"SRATE: error: %s (rc=%d)\n", src_strerror (rc), rc);
            fprintf(stderr, "i: %d   Frames: %ld   SR: %f\n", i, data.output_frames_gen, src_ratio);
            exit(1);
        }
        else
        {
            for (int i=0; i < data.output_frames_gen; i++)
            {
                left_rx_sample = (short)(data.data_out[i*2]*32767.0);
                right_rx_sample = (short)(data.data_out[i*2+1]*32767.0);
                audio_stream_put_samples(left_rx_sample, right_rx_sample);
            }
        } // if (rc)

        Spectrum0(1, 0, 0, 0, buffer.data);

    } // end while
    setStopIQIssued(false);
    close(iq_socket);
    fprintf(stderr, "IQ thread closed.\n");
    return 0;
} // end iq_thread


void iq_monitor(double *iqdata, int length)
{
    static float data_out[2048];
    static float data_in[2048];

    int j, i;
    i = 1024;
    double dataout[2048];

    int error = 0;
  //  SetDSPSamplerate(0, 96000);
//  SetInputSamplerate(0, 192000);
    SetChannelState(0, 1, 1);

    fexchange0(0, iqdata, dataout, &error);

    SetChannelState(0, 0, 0);
//    SetDSPSamplerate(0, sampleRate);
  //  SetInputSamplerate(0, 48000);

    if (error != 0 && error != -2)
        printf("fexchange error: %d\n", error);fflush(stdout);

    // process the output with resampler
    int rc;
#pragma omp parallel for schedule(static) private(j)
    for (j=0; j < 512; j++)
    {
        data_in[j*2]   = (float)dataout[j*2];    // left_samples[i];
        data_in[j*2+1] = (float)dataout[j*2+1];  // right_samples[i];
    }

    SRC_DATA data;
    data.data_in = data_in;
    data.input_frames = 512; //buffer_size;

    data.data_out = data_out;
    data.output_frames = 512; //buffer_size;
    data.src_ratio = src_ratio;
    data.end_of_input = 0;

    rc = src_process(sr_state, &data);
    if (rc)
    {
        fprintf(stderr,"SRATE: error: %s (rc=%d)\n", src_strerror (rc), rc);
        fprintf(stderr, "i: %d   Frames: %ld   SR: %f\n", i, data.output_frames_gen, src_ratio);
        exit(1);
    }
    else
    {
        for (int i=0; i < data.output_frames_gen; i++)
        {
            left_rx_sample = (short)(data.data_out[i*2]*32767.0);
            right_rx_sample = (short)(data.data_out[i*2+1]*32767.0);
            audio_stream_put_samples(left_rx_sample, right_rx_sample);
        }
    } // if (rc)
//    fprintf(stderr, "-");fflush(stderr);
} // end iq_monitor


void hw_send(unsigned char* data, int length, int ch)
{
    BUFFER  buffer;

//    iq_monitor(&buffer.data[0], length/8);
    //fprintf(stderr,"hw_send: %s\n",who);
    sem_wait(&hw_send_semaphore);
    for (int i=0;i<length/512;i++)
    {
        buffer.chunk = i;
        buffer.radio_id = channels[ch].radio_id;
        buffer.receiver = channels[ch].receiver;
        buffer.length = length;
        memcpy((char*)&buffer.data[0], (char*)&data[512*i], 512);

        int bytes_written = sendto(audio_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr *)&audio_addr, audio_length);
        if (bytes_written < 0)
        {
            perror("socket error: ");
            fprintf(stderr, "sendto audio failed: %d length=%d\n", bytes_written, length);
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
void send_command(char* command, int len, char *response)
{
    int rc;

    //fprintf(stderr, "send_command: command='%s'\n", command);

    sem_wait(&hw_cmd_semaphore);
    rc = send(command_socket, command, len, 0);
    if (rc < 0)
    {
        sem_post(&hw_cmd_semaphore);
        fprintf(stderr, "send command failed: %d: %s\n", rc, command);
        exit(1);
    }

    /* FIXME: This is broken.  It will probably work as long as
     * responses are very small and everything proceeds in lockstep. */
    rc = recv(command_socket, response, HW_RESPONSE_SIZE, 0);
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

    //fprintf(stderr, "send_command: response='%s'\n", response);
} // end send_command


void* keepalive_thread(void* arg)
{
    char command[128], response[HW_RESPONSE_SIZE];
    sprintf(command, "keepalive 0");
    while (1)
    {
        fprintf(stderr, "keepalive\n");
        sleep(5);
        send_command(command, strlen(command), response);
    }
} // end keepalive_thread


int make_connection(short int radio_id, short int receiver)
{
    char *token, *saveptr;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;
    char buffer[64];


    result = 1;
    sprintf(command, "%c%c%c", ATTACH, (char)receiver, (char)radio_id);
    //sprintf(command, "connect %d %d", receiver, IQ_PORT+(receiver*2));
    send_command(command, 4, response);
    fprintf(stderr, "Resp: %s\n", response);
    token = strtok_r(response, " ", &saveptr);
    if (token != NULL)
    {
        if (strcmp(token, "OK") == 0)
        {
            channels[active_channels].radio_id = radio_id;
            channels[active_channels++].receiver = receiver;

            token=strtok_r(NULL, " ", &saveptr);
            if (token != NULL)
            {
                result = 1;
                sampleRate = 48000; //////////atoi(token);
                fprintf(stderr, "connect: sampleRate=%d\n", sampleRate);
                setSpeed(sampleRate);

                sprintf(command, "%c%c", ATTACH, TX);
                send_command(command, 3, response);
// FIXME:  Not a good place for this.
                recvfrom(audio_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&audio_addr, (socklen_t *)&audio_length);
            }
            else
            {
                fprintf(stderr, "invalid response to connect: %s\n", response);
                result = 0;
            }
        }
        else
            if (strcmp(token, "ERROR") == 0)
            {
                result = 0;
            }
            else
            {
                fprintf(stderr, "invalid response to connect: %s\n", response);
                result = 0;
            }
    }

    return result;
} // end make_connection


void hwDisconnect(short int radio_id, short int receiver)
{
    char command[128], response[HW_RESPONSE_SIZE];

    sprintf(command, "%c%c%c", DETACH, (char)receiver, (char)radio_id);
    send_command(command, 3, response);

    close(radio[radio_id]->socket);
    close(audio_socket);
} // end hwDisconnect


int hwSetSampleRate(long rate)
{
    char *token;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;
    char *saveptr;

    result = 0;
    sprintf(command, "%c %ld", SETSAMPLERATE, rate);
    send_command(command, 64, response);
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


int hwSetFrequency(int ch, long long ddsAFrequency)
{
    char *token;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;
    char *saveptr;

    result = 0;
    sprintf(command, "%c%c%lld", SETFREQ, channels[ch].receiver, (ddsAFrequency - (long long)LO_offset));
    send_command(command, 64, response);
    token = strtok_r(response, " ", &saveptr);
    if (token != NULL)
    {
        if (strcmp(token, "OK") == 0)
        {
            result = 0;
            lastFreq = ddsAFrequency;
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


int hwSetRecord(char* state)
{
    char *token, *saveptr;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;

    result=0;
    sprintf(command,"%c%c", SETRECORD, state[0]);
    send_command(command, 64, response);
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


int hwSetMox(int state)
{
    char *token, *saveptr;
    char command[64], response[HW_RESPONSE_SIZE];
    int result;

    result = 0;
    mox = state;
    command[2] = 0;
    sprintf(command, "%c%c", MOX, state);
    send_command(command, 3, response);
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


int hwSendStarCommand(char *command)
{
    int result;
    char buf[256];
    char response[HW_RESPONSE_SIZE];

    result = 0;
    send_command(command+1, strlen(command+1), response);  // SKIP the leading '*'
    fprintf(stderr, "response to STAR message: [%s]\n", response);
    if (command[1] == 1) command[1] = STARHARDWARE;
    snprintf(buf, sizeof(buf), "%s %s", command, response ); // insert a leading '*' in answer
    strcpy(command, buf);                                    // attach the answer
    result = 0;

    return result;
} // end hwSendStarCommand


/* --------------------------------------------------------------------------*/
/** 
* @brief Process the hw input buffer
* 
* @param buffer
*/
void process_hw_input_buffer(char* buffer)
{
} // end process_hw_input_buffer


/* --------------------------------------------------------------------------*/
/** 
* @brief Get the iq samples
* 
* @param samples
*/
void getSpectrumSamples(char *samples) 
{
    memcpy(samples, iq_samples, SPECTRUM_BUFFER_SIZE);
} // end getSpectrumSamples


/* --------------------------------------------------------------------------*/
/** 
* @brief Set the speed
* 
* @param speed
*/
void setSpeed(int s) 
{
    fprintf(stderr, "setSpeed %d\n", s);
    fprintf(stderr, "LO_offset %f\n", LO_offset);

    sampleRate = s;

    SetDSPSamplerate(0, sampleRate);

    // SetRXAShiftFreq(0, -LO_offset);
    //SetRXAShiftFreq(1, -LO_offset);

    fprintf(stderr, "%s: %f\n", __FUNCTION__, (double)sampleRate);
    hw_set_src_ratio();
} // end setSpeed


void hw_startIQ()
{
    int rc = pthread_create(&iq_thread_id, NULL, iq_thread, NULL);
    if (rc != 0)
    {
         fprintf(stderr,"pthread_create failed on iq_thread: rc=%d\n", rc);
    }
} // end hw_startIQ


void hw_stopIQ()
{
    setStopIQIssued(true);
} // end hw_stopeIQ


void* command_thread(void* arg)
{
    radio[connected_radios] = (RADIO*)arg;
    command_socket = radio[connected_radios]->socket;
    if (connected_radios == 4)
    {
        close(radio[connected_radios]->socket);
        free(radio[connected_radios]);
        connected_radios--;
        return 0;
    }
    hw_get_manifest();

    while (1)
    {
        usleep(1000);
    }
    close(radio[connected_radios]->socket);
    free(radio[connected_radios]);
    connected_radios--;
    return 0;
} // end command_thread


void* hw_command_listener_thread(void* arg)
{
    struct sockaddr_in address;
    RADIO* radio;
    int rc;
    int on=1;

    // create a TCP socket to send commands to the server
    command_socket = socket(AF_INET,SOCK_STREAM, 0);
    if (command_socket < 0)
    {
        perror("hw_init: create command socket failed");
        exit(1);
    }

    setsockopt(command_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&command_addr, 0, command_length);

    command_addr.sin_family = AF_INET;
    command_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    command_addr.sin_port = htons(COMMAND_PORT); //+(receiver*2));

    if (bind(command_socket, (struct sockaddr*)&command_addr, command_length) < 0)
    {
        perror("hw_init: bind socket failed for command socket");
        exit(1);
    }

    fprintf(stderr, "hw_init: listening to port %d socket %d\n", ntohs(command_addr.sin_port), command_socket);

    while (1)
    {
        if (listen(command_socket, 6) < 0)
        {
            perror("Command listen failed");
            exit(1);
        }

        radio = malloc(sizeof(RADIO));
        radio->iq_length = sizeof(radio->iq_addr);
        radio->iq_port[0]=-1;

        if ((radio->socket=accept(command_socket, (struct sockaddr*)&radio->iq_addr, &radio->iq_length)) < 0)
        {
            perror("Command accept failed");
            exit(1);
        }

        fprintf(stderr,"command socket %d\n", radio->socket);
        fprintf(stderr,"radio connecting: %s:%d\n", inet_ntoa(radio->iq_addr.sin_addr), ntohs(radio->iq_addr.sin_port));

        //     client[0].iq_addr = rcr->iq_addr;
        rc = pthread_create(&radio->thread_id, NULL, command_thread, (void *)radio);
        if (rc < 0)
        {
            perror("radio command channel pthread_create failed");
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
    int on = 1;
    char server_address[] = "127.0.0.1";

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

    // create a socket to send audio to the server
    audio_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (audio_socket < 0)
    {
        perror("hw_init: create audio socket failed");
        exit(1);
    }
    setsockopt(audio_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&audio_addr, 0, audio_length);
    audio_addr.sin_family = AF_INET;
    audio_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    audio_addr.sin_port = htons(IQ_PORT+30);

    if (bind(audio_socket, (struct sockaddr*)&audio_addr, audio_length) < 0)
    {
        perror("hw_init: bind socket failed for audio socket");
        exit(1);
    }

    fprintf(stderr, "hw_init: audio bound to port %d socket %d\n", ntohs(audio_addr.sin_port), audio_socket);

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
    rxOnly = !state;
} // end hw_set_canTx


void hw_set_harware_control(char enable)
{
    hardware_control = enable;
} // end hw_set_hardware_control


void createChannels(char *manifest)
{
    char line[80];
    int  index = 0;
    int  radio_id = 0;
    int  last_tx_ch = -1;
    char radio_type[25];
    int  num_chs = 0;
    int  num_rcvrs = 0;

    for (int i=0;i<strlen(manifest);i++)
    {
        if (manifest[i] != 10)
        {
    //        fprintf(stderr, "%02X", manifest[i]);
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
                sscanf(line, "%*s %d", &radio_id);
                channels[active_channels].receiver = -1;
                channels[active_channels].transmitter = -1;
                channels[active_channels].enabled = false;
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
                        if (strstr(line, "supported_transmitters"))
                        {
                            int r = 0;
                            sscanf(line, "%*s %d %*s", &r);
                            for (int x=0;x<num_rcvrs;x++)
                            {
                                channels[active_channels].receiver = num_chs++;
                                if (r-- > 0)
                                {
                                    channels[active_channels].transmitter = num_chs++;
                                    last_tx_ch = num_chs-1;
                                }
                                else
                                    channels[active_channels].transmitter = last_tx_ch;
                                channels[active_channels].radio_id = radio_id;
                                strcpy(channels[active_channels].radio_type, radio_type);
                                channels[active_channels].enabled = false;
                                active_channels++;
                            }
                        }
        }
    }
    fprintf(stderr, "Active channels = %d\n", active_channels);
    fprintf(stderr, "RX = %d  TX = %d  Type = %s\n", channels[0].receiver, channels[0].transmitter, channels[0].radio_type);
} // end createChannels


void hw_get_manifest()
{
    char command[64], response[HW_RESPONSE_SIZE];
    int result;

    command[0] = 255; // QHARDWARE
    sem_wait(&hw_cmd_semaphore);
    int rc = send(command_socket, command, 1, 0);
    if (rc < 0)
    {
        sem_post(&hw_cmd_semaphore);
        fprintf(stderr, "send command failed: %d: %s\n", rc, command);
        exit(1);
    }

    /* FIXME: This is broken.  It will probably work as long as
     * responses are very small and everything proceeds in lockstep. */
    rc = recv(command_socket, response, sizeof(int), 0);
//    sem_post(&hw_cmd_semaphore);
    if (rc < 0)
    {
        fprintf(stderr, "read response failed: %d\n", rc);
    }
    int len;
    memcpy((int*)&len, (int*)&response, sizeof(int));
    manifest_xml[connected_radios] = (char*)malloc(len);
    rc = recv(command_socket, manifest_xml[connected_radios++], len, 0);
    if (rc < 0)
    {
        fprintf(stderr, "read response failed: %d\n", rc);
    }
    sem_post(&hw_cmd_semaphore);
    createChannels(manifest_xml[connected_radios-1]);
    fprintf(stderr, "XML: %s", manifest_xml[connected_radios-1]);
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
