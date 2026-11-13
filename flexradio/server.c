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
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "server.h"
#include "radio.h"
#include "flex.h"
#include "tx.h"

#define BLOCK 512                    // complex samples per packet to dspserver

static SERVER_OPTIONS opts;
static int        port_offset = 0;    // block * PORT_BLOCK, from the manifest request
static struct in_addr dsp_addr;
static atomic_bool    stop_requested = false;

// receiver state, guarded by rx_lock (the radio's UDP thread sends IQ)
static pthread_mutex_t rx_lock = PTHREAD_MUTEX_INITIALIZER;
static bool     rx_attached = false;
static int8_t   rx_index = 0;
static int      iq_socket = -1;
static struct sockaddr_in iq_dest;
static double   block[BLOCK * 2];
static int      block_fill = 0;

static pthread_t audio_thread_id;
static bool      audio_thread_running = false;
static int       audio_socket = -1;

static bool send_manifest = false;
static char response[128];


static int udp_socket(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int size = 1 << 20;

    if (s < 0)
    {
        perror("flexradio: socket");
        return -1;
    }
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    return s;
} // end udp_socket


int server_port_offset(void)
{
    return port_offset;
} // end server_port_offset


static struct sockaddr_in dsp_endpoint(int port)
{
    struct sockaddr_in a;

    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr = dsp_addr;
    a.sin_port = htons((uint16_t)port);
    return a;
} // end dsp_endpoint


/* Radio IQ (I,Q) -> dspserver packets of BLOCK samples in WDSP's Q,I order. */
static void on_iq(void *user, const double *iq, int n, int rate)
{
    static BUFFERL buffer;

    (void)user;
    (void)rate;
    pthread_mutex_lock(&rx_lock);
    if (!rx_attached || iq_socket < 0)
    {
        block_fill = 0;
        pthread_mutex_unlock(&rx_lock);
        return;
    }
    for (int k = 0; k < n; k++)
    {
        block[block_fill * 2] = iq[k * 2 + 1];
        block[block_fill * 2 + 1] = iq[k * 2];
        if (++block_fill == BLOCK)
        {
            buffer.chunk = 0;
            buffer.radio_id = (int8_t)opts.radio_id;
            buffer.receiver = rx_index;
            buffer.length = BLOCK * 2;
            memcpy(buffer.data, block, sizeof(block));
            if (sendto(iq_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&iq_dest, sizeof(iq_dest)) < 0)
                perror("flexradio: sendto rx iq");
            block_fill = 0;
        }
    }
    pthread_mutex_unlock(&rx_lock);
} // end on_iq


/* dspserver sends receiver audio to the radio; tell it where and drop it. */
static void *audio_drain_thread(void *arg)
{
    char buf[2048];

    (void)arg;
    while (!atomic_load(&stop_requested))
    {
        if (recv(audio_socket, buf, sizeof(buf), 0) < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            usleep(100000);
    }
    return NULL;
} // end audio_drain_thread


static void start_audio_drain(void)
{
    struct timeval tv = {0, 200000};
    struct sockaddr_in dest = dsp_endpoint(RX_AUDIO_PORT + opts.radio_id + port_offset);
    char sync[2] = {0, 0};

    if (audio_socket < 0)
    {
        audio_socket = udp_socket();
        if (audio_socket < 0)
            return;
        setsockopt(audio_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    sendto(audio_socket, sync, sizeof(sync), 0, (struct sockaddr*)&dest, sizeof(dest));
    if (!audio_thread_running && pthread_create(&audio_thread_id, NULL, audio_drain_thread, NULL) == 0)
        audio_thread_running = true;
} // end start_audio_drain


static const char *attach_receiver(int8_t index)
{
    if (index != 0)
        return RECEIVER_INVALID;
    if (rx_attached)
        return RECEIVER_IN_USE;
    if (!radio_is_open())
        return RADIO_FAILED;

    pthread_mutex_lock(&rx_lock);
    if (iq_socket < 0)
        iq_socket = udp_socket();
    iq_dest = dsp_endpoint(RX_IQ_PORT_0 + index + port_offset);
    rx_index = index;
    block_fill = 0;
    rx_attached = (iq_socket >= 0);
    pthread_mutex_unlock(&rx_lock);
    if (!rx_attached)
        return RADIO_FAILED;

    start_audio_drain();
    snprintf(response, sizeof(response), "%s %d", OK, radio_get_rate());
    fprintf(stderr, "flexradio: receiver %d attached\n", index);
    return response;
} // end attach_receiver


static const char *detach_receiver(int8_t index)
{
    if (tx_is_attached() && index == tx_index())
        return tx_detach();
    if (!rx_attached || index != rx_index)
        return RECEIVER_NOT_ATTACHED;
    pthread_mutex_lock(&rx_lock);
    rx_attached = false;
    pthread_mutex_unlock(&rx_lock);
    fprintf(stderr, "flexradio: receiver %d detached\n", index);
    return OK;
} // end detach_receiver


static const char *set_rx_rate(const char *arg)
{
    int r = atoi(arg);

    // dspserver needs 48 kHz or more; the radio offers 24/48/96/192 kHz
    if (r != 48000 && r != 96000 && r != 192000)
        return INVALID_COMMAND;
    if (radio_set_rate(r) != 0)
        return RADIO_FAILED;
    pthread_mutex_lock(&rx_lock);
    block_fill = 0;
    pthread_mutex_unlock(&rx_lock);
    return OK;
} // end set_rx_rate


/* The radio's address as given in the manifest; dspserver also checks it
 * against the ipaddr of our MIC_BUFFER packets (16 bytes). */
static const char *manifest_ip(void)
{
    static char ip[16];
    const FLEX_RADIO_INFO *ri = radio_info();

    const char *src = ri->ip[0] ? ri->ip : "NA";
    size_t n = strnlen(src, sizeof(ip) - 1);    // an IPv4 address fits

    memcpy(ip, src, n);
    ip[n] = '\0';
    return ip;
} // end manifest_ip


static char *manifest(void)
{
    static char xml[2048];
    const FLEX_RADIO_INFO *ri = radio_info();
    char name[40];
    double fmax = 54000000.0;

    // no spaces: QtRadio and dspserver read the value as one word
    snprintf(name, sizeof(name), "%s", ri->model[0] ? ri->model : "FLEX");
    for (char *c = name; *c; c++)
    {
        if (*c == ' ')
            *c = '_';
    }
    if (strstr(ri->model, "6700") != NULL)
        fmax = 165000000.0;          // the 6700 also covers 2 m

    snprintf(xml, sizeof(xml),
             "<radios=1>\n"
             "<radio=%d>\n"
             "<radio_type>flex6000</radio_type>\n"
             "<protocol>4</protocol>\n"
             "<device>0</device>\n"
             "<use_tcp>0</use_tcp>\n"
             "<name>%s</name>\n"
             "<software_version>0</software_version>\n"
             "<status>2</status>\n"
             "<bandscope>0</bandscope>\n"
             "<supported_receivers>1</supported_receivers>\n"
             "<supported_transmitters>1</supported_transmitters>\n"
             "<adcs>1</adcs>\n"
             "<dacs>1</dacs>\n"
             "<frequency_min>%f</frequency_min>\n"
             "<frequency_max>%f</frequency_max>\n"
             "<mac_address>NA</mac_address>\n"
             "<ip_address>%s</ip_address>\n"
             "<local_audio>0</local_audio>\n"
             "<local_mic>0</local_mic>\n"
             "<ant_switch>0</ant_switch>\n"
             "</radio>\n"
             "</radios>\n",
             opts.radio_id, name, 10000.0, fmax, manifest_ip());
    return xml;
} // end manifest


static const char *parse_command(const unsigned char *cmd, int len)
{
    int8_t index = (int8_t)cmd[0];
    const char *text = (const char*)cmd + 2;

    if (len < 2)
        return INVALID_COMMAND;
    fprintf(stderr, "flexradio: command [%d] %u %u\n", index, cmd[1], len > 2 ? cmd[2] : 0);

    switch (cmd[1])
    {
    case FLEXSTARTRADIO:
        return radio_is_open() ? OK : RADIO_FAILED;

    case FLEXSTOPRADIO:
        return OK;

    case FLEXHQHARDWARE:
        port_offset = len > 2 ? cmd[2] * PORT_BLOCK : 0;
        if (port_offset != 0)
            fprintf(stderr, "flexradio: dspserver port block %d (ports +%d)\n", cmd[2], port_offset);
        send_manifest = true;
        return OK;

    case FLEXHATTACHRX:
        return attach_receiver(index);

    case FLEXHDETACH:
        return detach_receiver(index);

    case FLEXHATTACHTX:
        if (!radio_is_open())
            return RADIO_FAILED;
        return tx_attach(index, dsp_addr, opts.radio_id, manifest_ip());

    case FLEXHMOX:
        if (!tx_is_attached())
            return TRANSMITTER_NONE;
        if (len < 3 || (cmd[2] != 0 && cmd[2] != 1))
            return INVALID_COMMAND;
        return tx_set_mox(cmd[2] == 1);

    case FLEXHSETSAMPLERATE:
        if (tx_is_attached() && index == tx_index())
            return OK;               // the TX input rate is fixed at 192 kHz
        return set_rx_rate(text);

    case FLEXHSETFREQ:
        // The radio transmits on its slice, which follows the receiver.
        if (tx_is_attached() && index == tx_index())
            return OK;
        return radio_set_frequency(strtoll(text, NULL, 10)) == 0 ? OK : RADIO_FAILED;

    case FLEXSETMODE:
    {
        char mode[8];
        if (sscanf(text, "%7[A-Za-z]", mode) != 1)
            return INVALID_COMMAND;
        return tx_set_mode(mode);
    }

    case FLEXSETRFPOWER:
        return radio_set_rfpower(atoi(text)) == 0 ? OK : RADIO_FAILED;

    case FLEXHSTARGETSERIAL:
        snprintf(response, sizeof(response), "%s %s", OK, radio_info()->serial);
        return response;

    case FLEXHSTARTBANDSCOPE:
    case FLEXHSTOPBANDSCOPE:
        return INVALID_COMMAND;      // no bandscope yet

    case FLEXHUPDATEBANDSCOPE:
    case FLEXHSTARTXCVR:
    case FLEXHSTOPXCVR:
    case FLEXHSTARTIQ:
    case FLEXHSTOPIQ:
    case FLEXHSETRECORD:
        return OK;

    case FLEXSETRXANT:
    {
        char ant[16];
        if (sscanf(text, "%15[A-Za-z0-9_]", ant) != 1)
            return INVALID_COMMAND;
        return radio_set_antenna(ant) == 0 ? OK : RADIO_FAILED;
    }

    case FLEXSETRFGAIN:
        return radio_set_rfgain(atoi(text)) == 0 ? OK : RADIO_FAILED;

    default:
        break;
    }
    return INVALID_COMMAND;
} // end parse_command


static bool read_full(int s, unsigned char *buf, int n)
{
    int got = 0;

    while (got < n)
    {
        ssize_t r = recv(s, buf + got, (size_t)(n - got), 0);
        if (r == 0)
            return false;
        if (r < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            {
                if (atomic_load(&stop_requested) || !radio_is_open())
                    return false;
                continue;
            }
            perror("flexradio: command channel");
            return false;
        }
        got += (int)r;
    }
    return true;
} // end read_full


static void serve_connection(int s)
{
    unsigned char cmd[260];
    unsigned char byte;

    while (!atomic_load(&stop_requested))
    {
        if (!read_full(s, &byte, 1))
            break;
        if (byte != 0x7f)
            continue;
        if (!read_full(s, &byte, 1))
            break;
        int len = byte;
        memset(cmd, 0, sizeof(cmd));
        if (len > 0 && !read_full(s, cmd, len))
            break;

        const char *resp = parse_command(cmd, len);
        if (send_manifest)
        {
            const char *xml = manifest();
            int n = (int)strlen(xml) + 1;
            send(s, (char*)&n, sizeof(n), MSG_NOSIGNAL);
            send(s, xml, (size_t)n, MSG_NOSIGNAL);
            send_manifest = false;
        }
        else
        {
            send(s, resp, strlen(resp), MSG_NOSIGNAL);
        }
    }
} // end serve_connection


void server_request_stop(void)
{
    atomic_store(&stop_requested, true);
} // end server_request_stop


int server_run(const SERVER_OPTIONS *options)
{
    bool waiting_reported = false;
    time_t last_stats = time(NULL);
    unsigned long last_lost = 0;

    opts = *options;
    if (inet_pton(AF_INET, opts.dsp_server_address, &dsp_addr) != 1)
    {
        fprintf(stderr, "flexradio: bad dspserver address %s\n", opts.dsp_server_address);
        return 1;
    }
    if (!radio_open(&opts.radio, on_iq, NULL))
        return 1;

    while (!atomic_load(&stop_requested) && radio_is_open())
    {
        struct sockaddr_in a = dsp_endpoint(COMMAND_PORT);
        struct timeval tv = {0, 500000};
        int on = 1;
        int s = socket(AF_INET, SOCK_STREAM, 0);

        if (s < 0)
            break;
        if (connect(s, (struct sockaddr*)&a, sizeof(a)) < 0)
        {
            if (!waiting_reported)
                fprintf(stderr, "flexradio: waiting for dspserver at %s:%d\n", opts.dsp_server_address, COMMAND_PORT);
            waiting_reported = true;
            close(s);
            sleep(1);
            continue;
        }
        waiting_reported = false;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        fprintf(stderr, "flexradio: connected to dspserver %s:%d\n", opts.dsp_server_address, COMMAND_PORT);

        serve_connection(s);
        close(s);
        if (tx_is_attached())
            tx_detach();
        detach_receiver(rx_index);

        if (time(NULL) - last_stats > 0)
        {
            unsigned long p, l;
            radio_stats(&p, &l);
            if (l != last_lost)
                fprintf(stderr, "flexradio: %lu IQ packets, %lu lost\n", p, l);
            last_lost = l;
            last_stats = time(NULL);
        }
        if (!atomic_load(&stop_requested))
        {
            fprintf(stderr, "flexradio: dspserver disconnected\n");
            sleep(1);
        }
    }
    if (!radio_is_open() && !atomic_load(&stop_requested))
        fprintf(stderr, "flexradio: lost the radio\n");

    if (tx_is_attached())
        tx_detach();
    radio_close();
    atomic_store(&stop_requested, true);       // ends the audio drain thread
    if (audio_thread_running)
    {
        pthread_join(audio_thread_id, NULL);
        audio_thread_running = false;
    }
    if (iq_socket >= 0)
        close(iq_socket);
    if (audio_socket >= 0)
        close(audio_socket);
    fprintf(stderr, "flexradio: exit\n");
    return 0;
} // end server_run
