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

typedef struct _rcvr {
    int socket;
    unsigned int iq_length;
    struct sockaddr_in iq_addr;
    pthread_t thread_id;
    RECEIVER_STATE receiver_state;
    TRANSMITTER_STATE transmitter_state;
    int radio_id;
    int receiver;
    int isTx;
    int iq_port;
    int bs_port;
    int mox;
} CLIENT;

enum COMMAND_SET {
    QUESTION = 0,
    QLOFFSET,
    QCOMMPROTOCOL1,
    QINFO,
    QCANTX,
    STARCOMMAND,
    STARGETSERIAL,
    //ISMIC,
    SETPREAMP,
    SETMICBOOST,
    SETPOWEROUT,
    SETRXANT,
    SETDITHER,
    SETRANDOM,
    SETLINEIN,
    SETLINEINGAIN,
    SETTXRELAY,
    SETOCOUTPUT,
    GETADCOVERFLOW,
    SETATTENUATOR,

    SETSAMPLERATE = 244,
    SETRECORD = 245,
    SETFREQ = 246,
    ATTACH = 247,
    TX = 248,
    DETACH = 249,
    STARTIQ = 250,
    STARTBANDSCOPE = 251,
    STOPIQ = 252,
    STOPBANDSCOPE = 253,
    MOX = 254,
    QHARDWARE = 255
};


void* client_thread(void* arg);

#define COMMAND_PORT 10100
#define RX_IQ_PORT_0 10000
#define RX_IQ_PORT_1 10001
#define RX_IQ_PORT_2 10002
#define RX_IQ_PORT_3 10003
#define RX_IQ_PORT_4 10004
#define RX_IQ_PORT_5 10005
#define RX_IQ_PORT_6 10006
#define RX_IQ_PORT_7 10007
#define TX_IQ_PORT_0 10010
#define TX_IQ_PORT_1 10011
#define RX_AUDIO_PORT 10020
#define MIC_AUDIO_PORT 10030

typedef struct _buffer {
    unsigned short chunk;
    unsigned short radio_id;
    unsigned short receiver;
    unsigned short length;
    double data[512];
} BUFFER;

typedef struct _bufferl {
    unsigned short chunk;
    unsigned short radio_id;
    unsigned short receiver;
    unsigned short length;
    double data[2048];
} BUFFERL;

typedef struct _mic_buffer
{
    unsigned short radio_id;
    unsigned short tx;
    unsigned short length;
    float          fwd_pwr;
    float          rev_pwr;
    float          data[512];
} MIC_BUFFER;

typedef struct _txiq_entry {
        char* buffer;
        TAILQ_ENTRY(_txiq_entry) entries;
} txiq_entry;

extern void create_listener_thread(void);
extern void init_receivers(int, int);
extern void send_IQ_buffer(int);
extern void send_Mic_buffer(float sample);

#endif // SERVER_H
