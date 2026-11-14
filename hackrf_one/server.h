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
 * Link to dspserver: the same TCP command channel and UDP streams hpsdr uses.
 */

#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "qtr_net.h"

// ports (same as hpsdr/server.h and dspserver)
#define RX_IQ_PORT_0     10000
#define TX_IQ_PORT_0     10020
#define COMMAND_PORT     10100
#define RX_AUDIO_PORT    10120
#define MIC_AUDIO_PORT   10130
#define BANDSCOPE_PORT   10140

// dspserver gives each radio program connection a block of UDP ports: the
// ports above plus block * PORT_BLOCK.  The block comes with the manifest
// request ([index][QHARDWARE][block]); 0 for older dspservers.
#define PORT_BLOCK       200
int server_port_offset(void);

#define RECEIVER_INVALID      "Error: Invalid Receiver"
#define RECEIVER_IN_USE       "Error: Receiver in use"
#define RECEIVER_NOT_ATTACHED "Error: Client does not have a receiver attached"
#define TRANSMITTER_NONE      "Error: Client does not have a transmitter attached"
#define INVALID_COMMAND       "Error: Invalid Command"
#define DEVICE_FAILED         "Error: HackRF device failure"
#define OK                    "OK"

// UDP packet layouts, identical to hpsdr/server.h
typedef struct _bufferl
{
    unsigned short chunk;
    int8_t         radio_id;
    int8_t         receiver;
    unsigned short length;
    double         data[2048];
} BUFFERL;

typedef struct _buffer          // TX IQ from dspserver: 512 bytes (32 complex) used per packet
{
    unsigned short chunk;
    int8_t         radio_id;
    int8_t         receiver;
    unsigned short length;
    double         data[512];
} BUFFER;

typedef struct _mic_buffer      // "radio mic" packets: they pace dspserver's TX thread
{
    int8_t    radio_id;
    char      ipaddr[16];
    int8_t    tx;
    short int length;
    float     fwd_pwr;
    float     rev_pwr;
    float     data[512];
} MIC_BUFFER;

typedef struct _bufferwb
{
    unsigned short chunk;
    int8_t         radio_id;
    int8_t         receiver;
    unsigned short length;
    int16_t        data[16384];
} BUFFERWB;

typedef struct _server_options
{
    const char *dsp_server_address;
    const char *serial;
    int         radio_id;
    int         wb_fps;
    int         level_interval;     // seconds between input level reports, 0 = only on change
} SERVER_OPTIONS;

int  server_run(const SERVER_OPTIONS *options);

// tx.c
const char *tx_attach(int8_t index, struct in_addr dsp_address, int radio_id);
const char *tx_detach(void);
const char *tx_set_mox(bool on);
bool        tx_is_attached(void);
int8_t      tx_index(void);
void server_request_stop(void);

#endif // SERVER_H
