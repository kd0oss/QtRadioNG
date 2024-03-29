/** 
* @file hardware.h
* @brief Header files for the Ozy interface functions 
* @author John Melton, G0ORX/N6LYT, Doxygen Comments Dave Larsen, KV0S
* @version 0.1
* @date 2009-03-10
*/

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
* Updated by Rick Schnicker KD0OSS  -- 2021,2022
*/


#ifndef _HARDWARE_H
#define	_HARDWARE_H

#ifdef	__cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "server.h"

#define WIDEBAND_CHANNEL MAX_CHANNELS-1

// Response buffers passed to hw_send must be this size
#define HW_RESPONSE_SIZE          64
#define BUFFER_SIZE               1024
#define SYNC                      0x7F
#define HW_BUFFER_SIZE            512

// HW command and control
#define MOX_DISABLED              0x00
#define MOX_ENABLED               0x01

#define MIC_SOURCE_JANUS          0x00
#define MIC_SOURCE_PENELOPE       0x80
#define CONFIG_NONE               0x00
#define CONFIG_PENELOPE           0x20
#define CONFIG_MERCURY            0x40
#define CONFIG_BOTH               0x60
#define PENELOPE_122_88MHZ_SOURCE 0x00
#define MERCURY_122_88MHZ_SOURCE  0x10
#define ATLAS_10MHZ_SOURCE        0x00
#define PENELOPE_10MHZ_SOURCE     0x04
#define MERCURY_10MHZ_SOURCE      0x08

#define MODE_CLASS_E              0x01
#define MODE_OTHERS               0x00

#define ALEX_ATTENUATION_0DB      0x00
#define ALEX_ATTENUATION_10DB     0x01
#define ALEX_ATTENUATION_20DB     0x02
#define ALEX_ATTENUATION_30DB     0x03
#define LT2208_GAIN_OFF           0x00
#define LT2208_GAIN_ON            0x04
#define LT2208_DITHER_OFF         0x00
#define LT2208_DITHER_ON          0x08
#define LT2208_RANDOM_OFF         0x00
#define LT2208_RANDOM_ON          0x10

typedef struct _radio {
    int socket;
    unsigned int iq_length;
    struct sockaddr_in iq_addr;
    pthread_t thread_id;
    bool connected;
} RADIO;

typedef struct _buffer {
    unsigned short chunk;
    int8_t         radio_id;
    int8_t         receiver;
    unsigned short length;
    double         data[64];
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

extern CHANNEL channels[MAX_CHANNELS];
//extern int iq_socket;
extern short int connected_radios;
extern int sampleRate;
extern int mox;
extern int receiver;
extern double LO_offset;
extern int rxOnly;
extern char *manifest_xml[5];

// values saved from last change to send to slaves
long long lastFreq;
int lastMode;
bool audio_enabled[MAX_CHANNELS];


/* --------------------------------------------------------------------------*/
/** 
* @brief Initialize the HW interface
* 
* @return 
*/
extern int hw_init(void);


/* --------------------------------------------------------------------------*/
/** 
* @brief Close the HW interface
* 
* @return 
*/
extern void hwClose();

/* --------------------------------------------------------------------------*/
/** 
* @brief Disconnect the hw interface
* 
* @return 
*/
extern void hwDisconnect(int8_t);

/* --------------------------------------------------------------------------*/
/** 
* @brief set frequency
* 
* @return 
*/
extern int hwSetFrequency(int, long long f);

/* --------------------------------------------------------------------------*/
/** 
* @brief set rx frequency
* 
* @return 
*/
extern void setRxFrequency(long f);

/* --------------------------------------------------------------------------*/
/** 
* @brief set tx frequency
* 
* @return 
*/
extern void setTxFrequency(long f);

/* --------------------------------------------------------------------------*/
/** 
* @brief set duplex
* 
* @return 
*/
extern void setDuplex(int d);

/* --------------------------------------------------------------------------*/
/**
* @brief Get hw software version
*
* @return
*/
int get_hw_software_version();

/* --------------------------------------------------------------------------*/
/**
* @brief save hw state
*
* @return
*/
void hwSaveState();

/* --------------------------------------------------------------------------*/
/**
* @brief restore hw state
*
* @return
*/
void hwRestoreState();

/* --------------------------------------------------------------------------*/
/**
* @brief set speed
*
* @return
*/
int make_connection(short int);
void setSpeed(int channel, int s);

extern int hwSetSampleRate(int, long);

int hwSetRecord(int8_t, char* state);

void hw_set_canTx(char);

#ifdef	__cplusplus
}
#endif


int set_frequency();

void setStopIQIssued(int val);
int  getStopIQIssued(void);
void hw_set_local_audio(int state);
void hw_set_port_audio(int state);
void hw_set_debug(int state);
int  hwSetTxMode(int mode);
int  hwSetMox(int8_t ch, int state);
void hw_send(unsigned char* data, int length, int rd);
int  hwSendStarCommand(int8_t, unsigned char *command, int);
void hw_set_src_ratio(void);
void hw_startIQ(int);
void hw_stopIQ(void);

extern int audio_socket;
extern int radioMic;
extern struct sockaddr_in audio_addr, server_audio_addr;
extern socklen_t audio_length, server_audio_length;

#endif	/* _HARDWARE_H */

