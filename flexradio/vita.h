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

/* Minimal VITA-49 packet parsing for FlexRadio streams. */

#ifndef VITA_H
#define VITA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define VITA_FLEX_OUI          0x001C2D
#define VITA_PCC_DISCOVERY     0xFFFF
#define VITA_PCC_IQ_24K        0x02E3
#define VITA_PCC_IQ_48K        0x02E4
#define VITA_PCC_IQ_96K        0x02E5
#define VITA_PCC_IQ_192K       0x02E6
#define VITA_PCC_METER         0x8002
#define VITA_PCC_DAX_AUDIO     0x03E3     // 24 kHz float stereo (IF narrow)
#define VITA_INFO_CLASS        0x534C

typedef struct _vita_packet
{
    int            type;          // packet type (header bits 31..28)
    bool           has_stream_id;
    uint32_t       stream_id;
    bool           has_class_id;
    uint32_t       oui;
    uint16_t       info_class;
    uint16_t       packet_class;
    int            count;         // 4 bit packet counter
    const uint8_t *payload;
    size_t         payload_bytes;
} VITA_PACKET;

/* Returns true if the datagram is a well formed VITA-49 packet. */
bool vita_parse(const uint8_t *data, size_t len, VITA_PACKET *p);

/* Sample rate of a DAX IQ packet class, or 0 if it is not one. */
int  vita_iq_rate(uint16_t packet_class);

/* Find "key=value" in a space separated string; copies the value. */
bool vita_kv(const char *text, const char *key, char *value, size_t size);

#endif // VITA_H
