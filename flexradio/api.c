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
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>

#include "qtr_net.h"

#include "api.h"
#include "vita.h"

#define MAX_PENDING   32
#define REPLY_SIZE    1024
#define TIMEOUT_S     5

typedef struct
{
    bool     used;
    bool     done;
    unsigned seq;
    long     code;
    char     message[REPLY_SIZE];
} PENDING;

static int             sock = -1;
static atomic_bool     connected = false;
static pthread_t       reader_id;
static bool            reader_running = false;
static pthread_t       ping_id;
static bool            ping_running = false;
static atomic_bool     ping_stop = false;
static flex_status_fn  status_cb = NULL;
static uint32_t        handle = 0;
static char            version[32] = "";
static unsigned        next_seq = 1;
static PENDING         pending[MAX_PENDING];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t send_lock = PTHREAD_MUTEX_INITIALIZER;


bool flex_discover(const char *want_serial, int timeout_s, FLEX_RADIO_INFO *info)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int on = 1;
    struct sockaddr_in a;
    time_t end = time(NULL) + timeout_s;
    bool found = false;

    if (s < 0)
        return false;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    sock_set_rcvtimeo(s, 1000);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(FLEX_DISCOVERY_PORT);
    if (bind(s, (struct sockaddr*)&a, sizeof(a)) < 0)
    {
        sock_perror("flexradio: discovery bind");
        sock_close(s);
        return false;
    }

    while (!found && time(NULL) < end)
    {
        uint8_t buf[2048];
        char text[1024];
        VITA_PACKET p;
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fl);

        if (n <= 0)
            continue;
        if (!vita_parse(buf, (size_t)n, &p) || p.packet_class != VITA_PCC_DISCOVERY)
            continue;
        size_t len = p.payload_bytes < sizeof(text) - 1 ? p.payload_bytes : sizeof(text) - 1;
        memcpy(text, p.payload, len);
        text[len] = '\0';

        memset(info, 0, sizeof(*info));
        vita_kv(text, "model", info->model, sizeof(info->model));
        vita_kv(text, "serial", info->serial, sizeof(info->serial));
        vita_kv(text, "version", info->version, sizeof(info->version));
        if (!vita_kv(text, "nickname", info->name, sizeof(info->name)))
            vita_kv(text, "name", info->name, sizeof(info->name));
        if (!vita_kv(text, "ip", info->ip, sizeof(info->ip)))
            inet_ntop(AF_INET, &from.sin_addr, info->ip, sizeof(info->ip));
        char port[16];
        info->port = vita_kv(text, "port", port, sizeof(port)) ? atoi(port) : FLEX_API_PORT;

        if (want_serial == NULL || want_serial[0] == '\0' || strstr(info->serial, want_serial) != NULL)
            found = true;
    }
    sock_close(s);
    return found;
} // end flex_discover


static void handle_line(char *line)
{
    size_t n = strlen(line);

    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n'))
        line[--n] = '\0';
    if (n == 0)
        return;

    switch (line[0])
    {
    case 'V':
        snprintf(version, sizeof(version), "%s", line + 1);
        break;

    case 'H':
        handle = (uint32_t)strtoul(line + 1, NULL, 16);
        pthread_mutex_lock(&lock);
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&lock);
        break;

    case 'R':
    {
        // R<seq>|<hex code>|<message>[|debug]
        char *bar1 = strchr(line, '|');
        if (bar1 == NULL)
            break;
        unsigned seq = (unsigned)strtoul(line + 1, NULL, 10);
        long code = (long)strtoul(bar1 + 1, NULL, 16);
        char *bar2 = strchr(bar1 + 1, '|');
        const char *msg = bar2 ? bar2 + 1 : "";
        pthread_mutex_lock(&lock);
        for (int i = 0; i < MAX_PENDING; i++)
        {
            if (pending[i].used && !pending[i].done && pending[i].seq == seq)
            {
                pending[i].code = code;
                snprintf(pending[i].message, REPLY_SIZE, "%s", msg);
                pending[i].done = true;
                pthread_cond_broadcast(&cond);
                break;
            }
        }
        pthread_mutex_unlock(&lock);
        break;
    }

    case 'S':
    {
        char *bar = strchr(line, '|');
        if (bar != NULL && status_cb != NULL)
            status_cb(bar + 1);
        break;
    }

    case 'M':
        fprintf(stderr, "flexradio: radio message %s\n", line + 1);
        break;

    default:
        break;
    }
} // end handle_line


static void *reader_thread(void *arg)
{
    char buf[65536];
    size_t used = 0;

    (void)arg;
    while (atomic_load(&connected))
    {
        ssize_t n = recv(sock, buf + used, sizeof(buf) - 1 - used, 0);
        if (n == 0)
            break;
        if (n < 0)
        {
            int e = sock_error();
            if (e == SOCK_EAGAIN || e == SOCK_EWOULDBLOCK || e == SOCK_EINTR || e == SOCK_ETIMEDOUT)
                continue;
            break;
        }
        used += (size_t)n;
        buf[used] = '\0';

        char *start = buf;
        char *nl;
        while ((nl = strpbrk(start, "\r\n")) != NULL)
        {
            *nl = '\0';
            handle_line(start);
            start = nl + 1;
        }
        used = strlen(start);
        memmove(buf, start, used);
        if (used >= sizeof(buf) - 1)
            used = 0;              // over-long line: drop it
    }

    atomic_store(&connected, false);
    pthread_mutex_lock(&lock);
    pthread_cond_broadcast(&cond);        // fail any waiting commands
    pthread_mutex_unlock(&lock);
    fprintf(stderr, "flexradio: radio connection closed\n");
    return NULL;
} // end reader_thread


bool flex_connect(const char *ip, int port, flex_status_fn status)
{
    struct sockaddr_in a;
    struct timespec deadline;
    int on = 1;

    status_cb = status;
    handle = 0;
    memset(pending, 0, sizeof(pending));

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1 || connect(sock, (struct sockaddr*)&a, sizeof(a)) < 0)
    {
        fprintf(stderr, "flexradio: cannot connect to %s:%d: %s\n", ip, port, sock_strerror(sock_error()));
        sock_close(sock);
        sock = -1;
        return false;
    }
    sock_set_rcvtimeo(sock, 500);
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

    atomic_store(&connected, true);
    if (pthread_create(&reader_id, NULL, reader_thread, NULL) != 0)
    {
        atomic_store(&connected, false);
        sock_close(sock);
        sock = -1;
        return false;
    }
    reader_running = true;

    // wait for the H<handle> line
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += TIMEOUT_S;
    pthread_mutex_lock(&lock);
    while (handle == 0 && atomic_load(&connected))
    {
        if (pthread_cond_timedwait(&cond, &lock, &deadline) == ETIMEDOUT)
            break;
    }
    pthread_mutex_unlock(&lock);
    if (handle == 0)
    {
        fprintf(stderr, "flexradio: no client handle from the radio\n");
        flex_disconnect();
        return false;
    }
    fprintf(stderr, "flexradio: connected to %s, API version %s, handle 0x%08X\n", ip, version, handle);
    return true;
} // end flex_connect


static void *ping_thread(void *arg)
{
    (void)arg;
    while (!atomic_load(&ping_stop) && atomic_load(&connected))
    {
        flex_command(NULL, 0, "ping");
        for (int i = 0; i < 10 && !atomic_load(&ping_stop); i++)
            usleep(100000);
    }
    return NULL;
} // end ping_thread


void flex_start_keepalive(void)
{
    if (ping_running)
        return;
    atomic_store(&ping_stop, false);
    if (pthread_create(&ping_id, NULL, ping_thread, NULL) == 0)
        ping_running = true;
} // end flex_start_keepalive


void flex_disconnect(void)
{
    if (ping_running)
    {
        atomic_store(&ping_stop, true);
        pthread_join(ping_id, NULL);
        ping_running = false;
    }
    atomic_store(&connected, false);
    if (sock >= 0)
        shutdown(sock, SHUT_RDWR);
    if (reader_running)
    {
        pthread_join(reader_id, NULL);
        reader_running = false;
    }
    if (sock >= 0)
    {
        sock_close(sock);
        sock = -1;
    }
} // end flex_disconnect


bool flex_connected(void)
{
    return atomic_load(&connected);
} // end flex_connected


long flex_command(char *reply, size_t reply_size, const char *fmt, ...)
{
    char cmd[1024];
    char line[1100];
    va_list ap;
    int slot = -1;
    unsigned seq;
    long code = -1;
    struct timespec deadline;

    if (reply != NULL && reply_size > 0)
        reply[0] = '\0';
    if (!atomic_load(&connected))
        return -1;

    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_PENDING; i++)
    {
        if (!pending[i].used)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    seq = next_seq++;
    pending[slot].used = true;
    pending[slot].done = false;
    pending[slot].seq = seq;
    pthread_mutex_unlock(&lock);

    int n = snprintf(line, sizeof(line), "C%u|%s\n", seq, cmd);
    pthread_mutex_lock(&send_lock);
    bool sent = send(sock, line, (size_t)n, MSG_NOSIGNAL) == n;
    pthread_mutex_unlock(&send_lock);
    if (strcmp(cmd, "ping") != 0)
        fprintf(stderr, "flexradio: > %s", line);

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += TIMEOUT_S;
    pthread_mutex_lock(&lock);
    while (sent && !pending[slot].done && atomic_load(&connected))
    {
        if (pthread_cond_timedwait(&cond, &lock, &deadline) == ETIMEDOUT)
            break;
    }
    if (pending[slot].done)
    {
        code = pending[slot].code;
        if (reply != NULL && reply_size > 0)
            snprintf(reply, reply_size, "%s", pending[slot].message);
        if (code != 0)
            fprintf(stderr, "flexradio: < R%u|%08lX|%s\n", seq, code, pending[slot].message);
    }
    else
    {
        fprintf(stderr, "flexradio: no answer to \"%s\"\n", cmd);
    }
    pending[slot].used = false;
    pthread_mutex_unlock(&lock);
    return code;
} // end flex_command
