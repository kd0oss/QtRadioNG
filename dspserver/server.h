/** 
* @file client.h
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
*/

#if ! defined __SERVER_H__
#define __SERVER_H__

#include <sys/queue.h>
#include <ortp/ortp.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <event.h>
#include <event2/thread.h>
//#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/bufferevent_ssl.h>

// added by KD0OSS ******
int panadapterMode;
int numSamples;
int rxMeterMode;
int txMeterMode;
// **********************

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
        SPECTRUM,
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

enum COMMAND_SET {
    CSFIRST = 0,
    QUESTION,
    QDSPVERSION,
    QLOFFSET,
    QCOMMPROTOCOL1,
    QSERVER,
    QMAIN,
    QINFO,
    QCANTX,
    STARCOMMAND,
    STARGETSERIAL,
    ISMIC,
    SETMAIN,
    SETFPS,
    SETCLIENT,
    SETSUBFREQ,
    SETSUBRX,
    SETMODE,
    SETFILTER,
    SETENCODING,
    SETRXOUTGAIN,
    SETSUBRXOUTGAIN,
    STARTAUDIO,
    STOPAUDIO,
    SETPAN,
    SETANF,
    SETANFVALS,
    SETNR,
    SETNRVALS,
    SETNB,
    SETNB2,
    SETRXBPASSWIN,
    SETTXBPASSWIN,
    SETWINDOW,
    SETSQUELCHVAL,
    SETSQUELCHSTATE,
    SETTXAMCARLEV,
    GETSPECTRUM,
    SETAGC,
    SETNBVAL,
    SETMICGAIN,
    SETFIXEDAGC,
    ENABLERXEQ,
    ENABLETXEQ,
    SETRXEQPRO,
    SETTXEQPRO,


    STOPXCVR = 242,
    STARTXCVR = 243,
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
    STARHARDWARE = 255
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
    int client_type;
    struct sockaddr_in client;
	struct bufferevent * bev;
	int fps;
	int frame_counter;
	int samples;
	TAILQ_ENTRY(_client_entry) entries;
} client_entry;

typedef struct _memory_entry {
	char* memory;
	TAILQ_ENTRY(_memory_entry) entries;
} memory_entry;

typedef struct _mic_buffer
{
    unsigned short radio_id;
    unsigned short tx;
    unsigned short length;
    float          fwd_pwr;
    float          rev_pwr;
    float          data[512];
} MIC_BUFFER;

typedef struct _spectrum
{
    unsigned short radio_id;
    unsigned short rx;
    unsigned short length;
    float          rx_meter;
    float          fwd_pwr;
    float          rev_pwr;
    unsigned int   sample_rate;
    float          lo_offset;
    char           *samples; // not used here, just a place holder for client side consistancy.
} spectrum;

extern short int active_channels;

char servername[21];
unsigned char bUseNB;
unsigned char bUseNB2;
float multimeterCalibrationOffset;
float displayCalibrationOffset;

void server_init(int receiver);
void tx_init(void);
void spectrum_init(void);
void initAnalyzer(int, int, int, int);
void spectrum_timer_init(void);
void *spectrum_thread(void *);
void *memory_thread(void *);
void client_set_timing(void);
void answer_question(char *message, char *clienttype, struct bufferevent *bev);
void printversion(void);
static SSL_CTX *evssl_init(void);

extern double mic_src_ratio;

#endif
