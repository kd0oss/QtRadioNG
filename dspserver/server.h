/** 
* @file server.h
* @brief iPhone network interface
* @author John Melton, G0ORX/N6LYT, Doxygen Comments Dave Larsen, KV0S
* @version 0.1
* @date 2009-04-12
*/
// server.h

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
*
* Updated by Rick Schnicker KD0OSS -- 2021,2022
*/

#if ! defined __SERVER_H__
#define __SERVER_H__

#include "../common.h"
#include <sys/queue.h>
#include <stdbool.h>
#include <ortp/ortp.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <event.h>
#include <event2/thread.h>
//#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/bufferevent_ssl.h>

#define SPECTRUM_BUFFER_SIZE 8192
#define MAX_CHANNELS         35  // Internal stream management not related to WDSP channels.
#define MAX_WDSP_CHANNELS    31  // Max channels defined in WDSP documentation.

#define RX_IQ_PORT_0     10000
#define TX_IQ_PORT_0     10020
#define COMMAND_PORT     10100
#define RX_AUDIO_PORT    10120
#define MIC_AUDIO_PORT   10130
#define BANDSCOPE_PORT   10140

typedef enum _sdrmode
{
  LSB,  //  0
  USB,  //  1
  DSB,  //  2
  CWL,  //  3
  CWU,  //  4
  FM,   //  5
  AM,   //  6
  DIGU, //  7
  SPEC, //  8
  DIGL, //  9
  SAM,  // 10
  DRM   // 11
} SDRMODE;

// RXA
enum rxaMode
{
        RXA_LSB,
        RXA_USB,
        RXA_DSB,
        RXA_CWL,
        RXA_CWU,
        RXA_FM,
        RXA_AM,
        RXA_DIGU,
        RXA_SPEC,
        RXA_DIGL,
        RXA_SAM,
        RXA_DRM
};

// TXA
enum txaMode
{
        TXA_LSB,
        TXA_USB,
        TXA_DSB,
        TXA_CWL,
        TXA_CWU,
        TXA_FM,
        TXA_AM,
        TXA_DIGU,
        TXA_SPEC,
        TXA_DIGL,
        TXA_SAM,
        TXA_DRM,
        TXA_AM_LSB,
        TXA_AM_USB
};

// added by KD0OSS
enum PanadapterMode
{
//        FIRST = -1,
        SPECT,
        PANADAPTER,
        SCOPE,
        SCOPE2,
        PHASE,
        PHASE2,
        WATERFALL,
        HISTOGRAM,
        PANAFALL,
        PANASCOPE,
        SPECTRASCOPE,
        OFF
//        LAST,
};

// added by KD0OSS
enum Window
{
//    FIRST = -1,
    RECTANGULAR,
    HANNING,
    WELCH,
    PARZEN,
    BARTLETT,
    HAMMING,
    BLACKMAN2,
    BLACKMAN3,
    BLACKMAN4,
    EXPONENTIAL,
    RIEMANN,
    BLKHARRIS
//    LAST,
};

enum CLIENT_TYPE {
    CONTROL = 0,
    SERVICE,
    MONITOR
};

enum CLIENT_CONNECTION {
    connection_unknown,
    connection_tcp = 0
} client_connection;

typedef struct _client_entry {
    int                        client_type[MAX_CHANNELS];
    struct sockaddr_in         client;
    struct bufferevent        *bev;
    bool                       channel_enabled[MAX_CHANNELS];
    TAILQ_ENTRY(_client_entry) entries;
} client_entry;

typedef struct _memory_entry {
	unsigned char* memory;
	TAILQ_ENTRY(_memory_entry) entries;
} memory_entry;

typedef struct _mic_buffer
{
    int8_t    radio_id;
    int8_t    tx;
    short int length;
    float     fwd_pwr;
    float     rev_pwr;
    float     data[512];
} MIC_BUFFER;

typedef enum {
    RXS, TXS, BS
} SPECTRUM_TYPE;

typedef struct _xcvr
{
    int8_t    radio_id;
    int8_t    connection;
    char      radio_type[25];
    char      radio_name[25];
    char      ip_address[16];
    char      mac_address[18];
    bool      bandscope_capable;
    bool      mox;
    bool      local_audio;
    bool      local_mic;
    int       ant_switch;
} XCVR;

typedef struct _spectrum
{
    SPECTRUM_TYPE  type;
    unsigned short length;
    float          meter;
    float          fwd_pwr;
    float          rev_pwr;
    unsigned int   sample_rate;
    float          lo_offset;
    short int      fps;
    int            nsamples;
    int            frame_counter;
    char           *samples; // not used here, just a place holder for client side consistancy.
} SPECTRUM;

typedef struct _channel
{
    int8_t    id;
    XCVR      radio;
    SPECTRUM  spectrum;
    int8_t    dsp_channel;
    int8_t    protocol;
    long long frequency;
    int8_t    index;
    bool      isTX;
    bool      enabled;
} CHANNEL;

extern short int active_channels;
extern double mic_src_ratio;
extern bool wideband_enabled;

char servername[21];
int panadapterMode;
int rxMeterMode;
int txMeterMode;
bool bUseNB;
bool bUseNB2;
float multimeterCalibrationOffset;
float displayCalibrationOffset;

void server_init(int receiver);
void tx_init(void);
void spectrum_init(void);
void initAnalyzer(int, int, int, int);
void start_rx_audio(int8_t channel);
void enable_wideband(int8_t, bool);
int widebandInitAnalyzer(int, int);
void spectrum_timer_init(int);
void wideband_timer_init(int);
void *spectrum_thread(void *);
void *memory_thread(void *);
void client_set_timing(void);
void answer_question(char *message, char *clienttype, struct bufferevent *bev);
void printversion(void);
static SSL_CTX *evssl_init(void);
#endif
