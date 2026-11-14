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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>

#include "qtr_net.h"

#include "plugin_client.h"

struct _plugin_conn
{
    int             fd;
    pthread_mutex_t wlock;
};


static bool write_all(int fd, const void *buf, size_t n)
{
    size_t off = 0;
    while (off < n)
    {
        ssize_t w = send(fd, (const char*)buf + off, n - off, MSG_NOSIGNAL);
        if (w <= 0)
        {
            if (w < 0 && sock_error() == SOCK_EINTR)
                continue;
            return false;
        }
        off += (size_t)w;
    }
    return true;
} // end write_all


static bool read_all(int fd, void *buf, size_t n)
{
    size_t off = 0;
    while (off < n)
    {
        ssize_t r = recv(fd, (char*)buf + off, n - off, 0);
        if (r <= 0)
        {
            if (r < 0 && sock_error() == SOCK_EINTR)
                continue;
            return false;
        }
        off += (size_t)r;
    }
    return true;
} // end read_all


bool plugin_send(PLUGIN_CONN *c, int type, int stream, const void *payload, size_t len)
{
    unsigned char hdr[PLUGIN_HEADER];
    uint32_t l = (uint32_t)(len + 2);
    bool ok;

    if (c == NULL || len + 2 > PLUGIN_MAX_FRAME)
        return false;
    hdr[0] = (unsigned char)(l >> 24);
    hdr[1] = (unsigned char)(l >> 16);
    hdr[2] = (unsigned char)(l >> 8);
    hdr[3] = (unsigned char)l;
    hdr[4] = (unsigned char)type;
    hdr[5] = (unsigned char)(int8_t)stream;
    pthread_mutex_lock(&c->wlock);
    ok = write_all(c->fd, hdr, sizeof(hdr)) && (len == 0 || write_all(c->fd, payload, len));
    pthread_mutex_unlock(&c->wlock);
    return ok;
} // end plugin_send


bool plugin_send_audio(PLUGIN_CONN *c, int stream, const float *audio, int n)
{
    return n <= 0 || plugin_send(c, PLG_AUDIO, stream, audio, sizeof(float) * (size_t)n);
} // end plugin_send_audio


bool plugin_send_txiq(PLUGIN_CONN *c, int stream, const float *iq, int n)
{
    return n <= 0 || plugin_send(c, PLG_TXIQ, stream, iq, sizeof(float) * 2 * (size_t)n);
} // end plugin_send_txiq


bool plugin_send_txdone(PLUGIN_CONN *c, int stream)
{
    return plugin_send(c, PLG_TXDONE, stream, NULL, 0);
} // end plugin_send_txdone


bool plugin_status(PLUGIN_CONN *c, int stream, const char *fmt, ...)
{
    char text[4096];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (n < 0)
        return false;
    if (n >= (int)sizeof(text))
        n = sizeof(text) - 1;
    return plugin_send(c, PLG_STATUS, stream, text, (size_t)n);
} // end plugin_status


PLUGIN_CONN *plugin_open(const char *host, int port, const char *xml)
{
    struct addrinfo hints, *res, *ai;
    char service[16];
    int fd = -1, on = 1;
    PLUGIN_CONN *c;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%d", port);
    qtr_net_init();
    if (getaddrinfo(host, service, &hints, &res) != 0)
    {
        fprintf(stderr, "plugin: cannot resolve %s\n", host);
        return NULL;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        sock_close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
    {
        fprintf(stderr, "plugin: cannot connect to dspserver at %s:%d (is it running?)\n", host, port);
        return NULL;
    }
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

    c = (PLUGIN_CONN*)calloc(1, sizeof(*c));
    c->fd = fd;
    pthread_mutex_init(&c->wlock, NULL);
    if (!plugin_send(c, PLG_REGISTER, -1, xml, strlen(xml)))
    {
        plugin_close(c);
        return NULL;
    }
    return c;
} // end plugin_open


void plugin_close(PLUGIN_CONN *c)
{
    if (c == NULL)
        return;
    sock_close(c->fd);
    pthread_mutex_destroy(&c->wlock);
    free(c);
} // end plugin_close


bool plugin_run(PLUGIN_CONN *c, const PLUGIN_CALLBACKS *cb, void *user, volatile bool *stop)
{
    unsigned char hdr[PLUGIN_HEADER];
    unsigned char *payload = NULL;
    size_t cap = 0;

    for (;;)
    {
        fd_set rd;
        struct timeval tv = {0, 200000};

        if (stop != NULL && *stop)
        {
            free(payload);
            return true;
        }
        FD_ZERO(&rd);
        FD_SET(c->fd, &rd);
        if (select(c->fd + 1, &rd, NULL, NULL, &tv) <= 0)
            continue;

        if (!read_all(c->fd, hdr, sizeof(hdr)))
            break;
        uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
        int type = hdr[4];
        int stream = (int8_t)hdr[5];
        if (len < 2 || len > PLUGIN_MAX_FRAME)
            break;
        size_t n = len - 2;
        if (n + 1 > cap)
        {
            cap = n + 1024;
            payload = (unsigned char*)realloc(payload, cap);
        }
        if (!read_all(c->fd, payload, n))
            break;
        payload[n] = '\0';

        switch (type)
        {
        case PLG_ACCEPT:
            if (cb->accept)
                cb->accept(user, (const char*)payload);
            break;
        case PLG_START:
            if (cb->start)
                cb->start(user, stream, strncmp((const char*)payload, "tx", 2) == 0,
                          n > 3 ? (const char*)payload + 3 : "");
            break;
        case PLG_STOP:
            if (cb->stop)
                cb->stop(user, stream);
            break;
        case PLG_RXIQ:
            if (cb->rxiq)
                cb->rxiq(user, stream, (const float*)payload, (int)(n / (2 * sizeof(float))));
            break;
        case PLG_MIC:
            if (cb->mic)
                cb->mic(user, stream, (const float*)payload, (int)(n / sizeof(float)));
            break;
        case PLG_SETTING:
        {
            char *eq = strchr((char*)payload, '=');
            if (eq != NULL && cb->setting)
            {
                *eq = '\0';
                cb->setting(user, stream, (const char*)payload, eq + 1);
            }
            break;
        }
        case PLG_MOX:
            if (cb->mox && n >= 1)
                cb->mox(user, stream, payload[0] != 0);
            break;
        default:
            break;
        }
    }
    free(payload);
    return false;
} // end plugin_run
