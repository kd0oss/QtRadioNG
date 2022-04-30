/**
* @file client.h
* @brief Handle client connection
* @author John Melton, G0ORX/N6LYT
* @version 0.1
* @date 2009-10-13
*/

/* Copyright (C)
* 2009 - John Melton, G0ORX/N6LYT
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

#ifndef SERVER_H
#define SERVER_H

#ifndef __linux__
#include <winsock.h>
#include "pthread.h"
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/queue.h>
#include <stdbool.h>

#define AUDIO_PORT 15000

#define true 1
#define false 0

#define BANDSCOPE_IN_USE "Error: bandscope in use"
#define BANDSCOPE_NOT_OWNER "Error: Not owner of bandscope"

#define RECEIVER_INVALID "Error: Invalid Receiver"
#define RECEIVER_IN_USE "Error: Receiver in use"
#define RECEIVER_NOT_OWNER "Error: Not owner of receiver"

#define CLIENT_ATTACHED "Error: Client is already attached to receiver"
#define CLIENT_DETACHED "Error: Client is not attached to receiver"

#define INVALID_COMMAND "Error: Invalid Command"
#define FAILED "Error: FAILED"

#define RECEIVER_NOT_ATTACHED "Error: Client does not have a receiver attached"
#define RECEIVER_NOT_ZERO "Error: Client not attached to receiver 0"

#define TRANSMITTER_NOT_ATTACHED "Error: Client does not have a transmitter attached"

#define OK "OK"

typedef enum {
    RECEIVER_DETACHED, RECEIVER_ATTACHED
} RECEIVER_STATE;

typedef enum {
    TRANSMITTER_DETACHED, TRANSMITTER_ATTACHED
} TRANSMITTER_STATE;

void* client_thread(void* arg);

#define RX_IQ_PORT_0     10000
#define TX_IQ_PORT_0     10020
#define COMMAND_PORT     10100
#define RX_AUDIO_PORT    10120
#define MIC_AUDIO_PORT   10130
#define BANDSCOPE_PORT   10140

typedef struct _client {
    int                socket;
    unsigned int       iq_length;
    struct sockaddr_in iq_addr;
    pthread_t          thread_id;
} CLIENT;

typedef struct _rfunit
{
    int8_t  radio_id;
    CLIENT  client;
    bool    attached;
    bool    isTx;
    bool    mox;
    int     port;
} RFUNIT;

typedef struct _buffer {
    unsigned short chunk;
    int8_t         radio_id;
    int8_t         receiver;
    unsigned short length;
    double         data[512];
} BUFFER;

typedef struct _bufferl {
    unsigned short chunk;
    int8_t         radio_id;
    int8_t         receiver;
    unsigned short length;
    double         data[2048];
} BUFFERL;

typedef struct _bufferwb {
    unsigned short chunk;
    int8_t         radio_id;
    int8_t         receiver;
    unsigned short length;
    int16_t        data[16384];
} BUFFERWB;

typedef struct _mic_buffer
{
    int8_t    radio_id;
    int8_t    tx;
    short int length;
    float     fwd_pwr;
    float     rev_pwr;
    float     data[512];
} MIC_BUFFER;

typedef struct _txiq_entry {
    char* buffer;
    TAILQ_ENTRY(_txiq_entry) entries;
} txiq_entry;

typedef struct _rxaudio_entry {
    char* buffer;
    TAILQ_ENTRY(_rxaudio_entry) entries;
} rxaudio_entry;

extern void create_client_thread(char*);
extern void init_receiver(int8_t);
extern void send_IQ_buffer(int);
extern void send_WB_IQ_buffer(int);
extern void send_Mic_buffer(float sample);

#endif // SERVER_H
