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

#include <string.h>

#include "vita.h"

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
} // end be32


/*
 * Header word: type(31..28) C(27) T(26) TSI(23..22) TSF(21..20) count(19..16) size in words(15..0).
 * Types 1 and 3 carry a stream id.  The header itself is big endian.
 */
bool vita_parse(const uint8_t *data, size_t len, VITA_PACKET *p)
{
    uint32_t header;
    size_t pos = 4;
    size_t total;
    bool trailer;

    if (len < 4)
        return false;
    memset(p, 0, sizeof(*p));
    header = be32(data);
    p->type = (int)(header >> 28);
    p->has_class_id = (header & 0x08000000u) != 0;
    trailer = (header & 0x04000000u) != 0;
    p->count = (int)((header >> 16) & 0xF);
    total = (size_t)(header & 0xFFFFu) * 4;
    if (total == 0 || total > len)
        total = len;            // trust the datagram length if the size field is odd

    p->has_stream_id = (p->type == 1 || p->type == 3);
    if (p->has_stream_id)
    {
        if (pos + 4 > total)
            return false;
        p->stream_id = be32(data + pos);
        pos += 4;
    }
    if (p->has_class_id)
    {
        if (pos + 8 > total)
            return false;
        p->oui = be32(data + pos) & 0x00FFFFFFu;
        p->info_class = (uint16_t)(be32(data + pos + 4) >> 16);
        p->packet_class = (uint16_t)(be32(data + pos + 4) & 0xFFFFu);
        pos += 8;
    }
    if ((header & 0x00C00000u) != 0)        // integer timestamp
        pos += 4;
    if ((header & 0x00300000u) != 0)        // fractional timestamp
        pos += 8;
    if (trailer)
    {
        if (total < 4)
            return false;
        total -= 4;
    }
    if (pos > total)
        return false;
    p->payload = data + pos;
    p->payload_bytes = total - pos;
    return true;
} // end vita_parse


int vita_iq_rate(uint16_t packet_class)
{
    switch (packet_class)
    {
    case VITA_PCC_IQ_24K:
        return 24000;
    case VITA_PCC_IQ_48K:
        return 48000;
    case VITA_PCC_IQ_96K:
        return 96000;
    case VITA_PCC_IQ_192K:
        return 192000;
    default:
        return 0;
    }
} // end vita_iq_rate


bool vita_kv(const char *text, const char *key, char *value, size_t size)
{
    size_t klen = strlen(key);
    const char *s = text;

    while ((s = strstr(s, key)) != NULL)
    {
        bool at_start = (s == text) || s[-1] == ' ' || s[-1] == '|';
        if (at_start && s[klen] == '=')
        {
            const char *v = s + klen + 1;
            size_t n = strcspn(v, " \r\n");
            if (n >= size)
                n = size - 1;
            memcpy(value, v, n);
            value[n] = '\0';
            return true;
        }
        s += klen;
    }
    return false;
} // end vita_kv
