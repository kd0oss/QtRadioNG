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
 * SmartSDR TCP/IP API client (FLEX-6000 / FLEX-8000 / Aurora).
 *
 *   radio -> "V<version>" and "H<handle>" on connect
 *   client -> "C<seq>|<command>\n"
 *   radio -> "R<seq>|<hex code>|<message>", "S<handle>|<status>", "M<num>|<text>"
 */

#ifndef API_H
#define API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLEX_API_PORT       4992
#define FLEX_DISCOVERY_PORT 4992
#define FLEX_DAX_TX_PORT    4991

typedef struct _flex_radio_info
{
    char model[32];
    char serial[48];
    char version[32];
    char name[64];
    char ip[48];
    int  port;
} FLEX_RADIO_INFO;

typedef void (*flex_status_fn)(const char *status);

/* Listen for discovery broadcasts.  want_serial may be NULL (first radio). */
bool flex_discover(const char *want_serial, int timeout_s, FLEX_RADIO_INFO *info);

bool flex_connect(const char *ip, int port, flex_status_fn status);
void flex_disconnect(void);
bool flex_connected(void);

/* Send a command and wait for its response.  Returns the response code
 * (0 = OK) or -1 if the radio did not answer.  The response message is
 * copied to reply when given. */
long flex_command(char *reply, size_t reply_size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* Start sending "ping" once a second after "keepalive enable". */
void flex_start_keepalive(void);

#endif // API_H
