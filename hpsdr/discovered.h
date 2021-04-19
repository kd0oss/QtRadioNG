/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
*
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

/* 2021 - Altered for QtRadio NG by Rick Schnicker, KD0OSS */

#ifndef _DISCOVERED_H
#define _DISCOVERED_H

#include <netinet/in.h>

#define MAX_DEVICES 16


// ANAN 7000DLE and 8000DLE uses 10 as the device type in old protocol
// HermesLite V2 uses V1 board ID and software version >= 40
#define DEVICE_METIS           0
#define DEVICE_HERMES          1
#define DEVICE_GRIFFIN         2
#define DEVICE_ANGELIA         4
#define DEVICE_ORION           5
#define DEVICE_HERMES_LITE     6
#define DEVICE_HERMES_LITE2 1006
#define DEVICE_ORION2         10 
#define DEVICE_STEMLAB       100

#ifdef USBOZY
#define DEVICE_OZY 7
#endif

#define NEW_DEVICE_ATLAS           0
#define NEW_DEVICE_HERMES          1
#define NEW_DEVICE_HERMES2         2
#define NEW_DEVICE_ANGELIA         3
#define NEW_DEVICE_ORION           4
#define NEW_DEVICE_ORION2          5
#define NEW_DEVICE_HERMES_LITE     6
#define NEW_DEVICE_HERMES_LITE2 1006

#define STATE_AVAILABLE 2
#define STATE_SENDING 3

#define ORIGINAL_PROTOCOL 0
#define NEW_PROTOCOL 1


struct _DISCOVERED {
    int protocol;
    int device;
    int use_tcp;    // use TCP rather than UDP to connect to radio
    char name[64];
    int software_version;
    int status;
    int supported_receivers;
    int supported_transmitters;
    int adcs;
    int dacs;
    double frequency_min;
    double frequency_max;
    union {
      struct network {
        unsigned char mac_address[6];
        int address_length;
        struct sockaddr_in address;
        int interface_length;
        struct sockaddr_in interface_address;
        struct sockaddr_in interface_netmask;
        char interface_name[64];
      } network;
    } info;
};

typedef struct _DISCOVERED DISCOVERED;

extern int selected_device;
extern int devices;
extern DISCOVERED discovered[MAX_DEVICES];

#endif
