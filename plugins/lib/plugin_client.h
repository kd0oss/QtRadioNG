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
 * The plugin side of plugin_protocol.h: connect, register, and a read loop
 * that calls back for every message.  Callbacks run in the thread that
 * calls plugin_run(); the send functions may be called from any thread.
 */

#ifndef PLUGIN_CLIENT_H
#define PLUGIN_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#include "../include/plugin_protocol.h"

typedef struct _plugin_callbacks
{
    void (*accept)(void *user, const char *text);                     // "id=..\nmode=NAME:NUM\n"
    void (*start)(void *user, int stream, bool tx, const char *mode);
    void (*stop)(void *user, int stream);
    void (*rxiq)(void *user, int stream, const float *iq, int n);     // n complex, I,Q
    void (*mic)(void *user, int stream, const float *audio, int n);
    void (*setting)(void *user, int stream, const char *key, const char *value);
    void (*mox)(void *user, int stream, bool on);
} PLUGIN_CALLBACKS;

typedef struct _plugin_conn PLUGIN_CONN;

/* Connect to dspserver and register; NULL on failure (message on stderr). */
PLUGIN_CONN *plugin_open(const char *host, int port, const char *xml);
void         plugin_close(PLUGIN_CONN *c);

/* Reads and dispatches until the connection closes or *stop becomes true
 * (checked every 200 ms).  Returns false when the connection was lost. */
bool plugin_run(PLUGIN_CONN *c, const PLUGIN_CALLBACKS *cb, void *user, volatile bool *stop);

bool plugin_send(PLUGIN_CONN *c, int type, int stream, const void *payload, size_t len);
bool plugin_send_audio(PLUGIN_CONN *c, int stream, const float *audio, int n);
bool plugin_send_txiq(PLUGIN_CONN *c, int stream, const float *iq, int n);
bool plugin_send_txdone(PLUGIN_CONN *c, int stream);
/* printf style "key=value" lines for the panel */
bool plugin_status(PLUGIN_CONN *c, int stream, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif // PLUGIN_CLIENT_H
