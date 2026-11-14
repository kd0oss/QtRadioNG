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
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>

#include "qtr_net.h"

#include "server.h"
#include "device.h"
#include "rxchain.h"
#include "hackrf.h"

#define READ_BYTES   262144          // one libhackrf transfer
#define WB_SAMPLES   16384           // complex samples per bandscope frame
#define WB_PACKETS   2

static SERVER_OPTIONS opts;
static int        port_offset = 0;    // block * PORT_BLOCK, from the manifest request
static struct in_addr dsp_addr;
static atomic_bool    stop_requested = false;

// receive chain, receiver and bandscope state: all guarded by chain_lock
static pthread_mutex_t chain_lock = PTHREAD_MUTEX_INITIALIZER;
static RXCHAIN *chain = NULL;
static long     rx_rate = HACKRF_RX_RATE_DEFAULT;
static bool     rx_attached = false;
static int8_t   rx_index = 0;
static int      iq_socket = -1;
static struct sockaddr_in iq_dest;
static atomic_bool wb_requested = false;
static int      wb_socket = -1;
static int      wb_fill = 0;
static long     wb_skip = 0;
static int16_t  wb_frame[WB_SAMPLES * 2];

static bool      radio_started = false;
static pthread_t dsp_thread_id;
static bool      dsp_thread_running = false;
static atomic_bool dsp_thread_stop = false;
static pthread_t audio_thread_id;
static bool      audio_thread_running = false;
static int       audio_socket = -1;

static bool send_manifest = false;
static char response[96];


static int udp_socket(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int size = 1 << 20;

    if (s < 0)
    {
        sock_perror("hackrf_one: socket");
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


/* rxchain output: one block of receiver IQ to dspserver.  Called with chain_lock held. */
static void send_iq_block(void *user, const double *iq, int n)
{
    static BUFFERL buffer;     // zeroed once; only 'length' doubles are meaningful

    (void)user;
    if (!rx_attached || iq_socket < 0)
        return;

    buffer.chunk = 0;
    buffer.radio_id = (int8_t)opts.radio_id;
    buffer.receiver = rx_index;
    buffer.length = (unsigned short)(n * 2);
    // WDSP (and so dspserver) takes interleaved samples as Q,I: a carrier
    // above the receiver frequency must arrive that way to demodulate as USB.
    for (int k = 0; k < n; k++)
    {
        buffer.data[k * 2] = iq[k * 2 + 1];
        buffer.data[k * 2 + 1] = iq[k * 2];
    }
    if (sendto(iq_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&iq_dest, sizeof(iq_dest)) < 0)
        sock_perror("hackrf_one: sendto rx iq");
} // end send_iq_block


/* Called with chain_lock held. */
static void send_wb_frame(void)
{
    static BUFFERWB buffer;
    struct sockaddr_in dest = dsp_endpoint(BANDSCOPE_PORT + opts.radio_id + port_offset);

    if (wb_socket < 0)
        wb_socket = udp_socket();
    if (wb_socket < 0)
        return;

    for (int p = 0; p < WB_PACKETS; p++)
    {
        buffer.chunk = (unsigned short)(WB_CHUNK_COMPLEX | p);
        buffer.radio_id = (int8_t)opts.radio_id;
        buffer.receiver = rx_index;
        buffer.length = 16384;          // int16 values: 8192 complex samples
        memcpy(buffer.data, wb_frame + p * 16384, sizeof(buffer.data));
        if (sendto(wb_socket, (char*)&buffer, sizeof(buffer), 0, (struct sockaddr*)&dest, sizeof(dest)) < 0)
            sock_perror("hackrf_one: sendto bandscope");
    }
} // end send_wb_frame


/* Take a WB_SAMPLES snapshot of the full-rate stream wb_fps times a second.
 * Called with chain_lock held. */
static void bandscope_feed(const int8_t *iq, size_t n)
{
    size_t k = 0;

    if (!atomic_load(&wb_requested) || !rx_attached)
    {
        wb_fill = 0;
        wb_skip = 0;
        return;
    }

    while (k < n)
    {
        if (wb_fill == 0 && wb_skip > 0)
        {
            size_t skip = ((size_t)wb_skip < n - k) ? (size_t)wb_skip : n - k;
            wb_skip -= (long)skip;
            k += skip;
            continue;
        }

        size_t m = (size_t)(WB_SAMPLES - wb_fill);
        if (m > n - k)
            m = n - k;
        for (size_t j = 0; j < m; j++)
        {
            wb_frame[(wb_fill + j) * 2] = (int16_t)(iq[(k + j) * 2] * 256);
            wb_frame[(wb_fill + j) * 2 + 1] = (int16_t)(iq[(k + j) * 2 + 1] * 256);
        }
        wb_fill += (int)m;
        k += m;

        if (wb_fill == WB_SAMPLES)
        {
            send_wb_frame();
            wb_fill = 0;
            wb_skip = device_get_hw_rate() / (opts.wb_fps > 0 ? opts.wb_fps : 15) - WB_SAMPLES;
            if (wb_skip < 0)
                wb_skip = 0;
        }
    }
} // end bandscope_feed


/* Whole-band input level (DC removed: the HackRF's DC offset would otherwise
 * dominate a quiet band) and ADC clipping, for judging gain settings. */
typedef struct
{
    double sum_i, sum_q, power;
    unsigned long clipped;
    unsigned long long samples;      // complex samples
} INPUT_LEVEL;


static void measure_input(const int8_t *buf, size_t n_complex, INPUT_LEVEL *m)
{
    for (size_t k = 0; k < n_complex; k++)
    {
        int i = buf[k * 2], q = buf[k * 2 + 1];
        m->sum_i += i;
        m->sum_q += q;
        m->power += (double)(i * i + q * q);
        if (i >= 127 || i <= -127 || q >= 127 || q <= -127)
            m->clipped++;
    }
    m->samples += n_complex;
} // end measure_input


static void *dsp_thread(void *arg)
{
    int8_t *buf = malloc(READ_BYTES);
    unsigned long reported = 0;
    time_t last = time(NULL);
    time_t last_level = time(NULL);
    INPUT_LEVEL m;
    double last_db = 999.0;

    (void)arg;
    if (buf == NULL)
        return NULL;
    memset(&m, 0, sizeof(m));

    while (!atomic_load(&dsp_thread_stop))
    {
        size_t n = device_read(buf, READ_BYTES, 100);

        if (n > 0)
        {
            pthread_mutex_lock(&chain_lock);
            if (chain != NULL)
                rxchain_process(chain, buf, n / 2);
            bandscope_feed(buf, n / 2);
            pthread_mutex_unlock(&chain_lock);
            // a sample of the stream is enough for the level report
            if (m.samples < 2000000ULL)
                measure_input(buf, n / 2, &m);
        }

        if (time(NULL) - last >= 1)
        {
            unsigned long o = device_overruns();
            if (o != reported)
                fprintf(stderr, "hackrf_one: %lu sample buffer overruns (DSP too slow)\n", o - reported);
            reported = o;

            if (m.samples > 0)
            {
                double ns = (double)m.samples;
                double mi = m.sum_i / ns, mq = m.sum_q / ns;
                double ac = m.power / ns - (mi * mi + mq * mq);
                double db = 10.0 * log10(ac / (127.0 * 127.0) + 1e-12);
                double dc = 10.0 * log10((mi * mi + mq * mq) / (127.0 * 127.0) + 1e-12);
                double clip_pct = 100.0 * (double)m.clipped / ns;
                bool due = opts.level_interval > 0 && time(NULL) - last_level >= opts.level_interval;
                if (due || fabs(db - last_db) >= 3.0 || clip_pct >= 0.01)
                {
                    fprintf(stderr, "hackrf_one: ADC input %.1f dBFS rms (DC %.1f dBFS), %.2f%% clipped%s\n", db, dc, clip_pct,
                            clip_pct >= 0.01 ? "  (overload: lower VGA/LNA gain or turn the RF amp off)" : "");
                    last_db = db;
                    last_level = time(NULL);
                }
            }
            memset(&m, 0, sizeof(m));
            last = time(NULL);
        }
    }
    free(buf);
    return NULL;
} // end dsp_thread


/* dspserver sends receiver audio to the radio for its speaker; the HackRF has
 * none, so tell dspserver where to send it and throw it away. */
static void *audio_drain_thread(void *arg)
{
    char buf[2048];

    (void)arg;
    while (!atomic_load(&stop_requested))
    {
        if (recv(audio_socket, buf, sizeof(buf), 0) < 0 && sock_error() != SOCK_EAGAIN && sock_error() != SOCK_EWOULDBLOCK
                && sock_error() != SOCK_EINTR && sock_error() != SOCK_ETIMEDOUT)
            usleep(100000);
    }
    return NULL;
} // end audio_drain_thread


static void start_audio_drain(void)
{
    struct sockaddr_in dest = dsp_endpoint(RX_AUDIO_PORT + opts.radio_id + port_offset);
    char sync[2] = {0, 0};

    if (audio_socket < 0)
    {
        audio_socket = udp_socket();
        if (audio_socket < 0)
            return;
        sock_set_rcvtimeo(audio_socket, 200);
    }
    // dspserver learns our address from this datagram (see make_connection)
    sendto(audio_socket, sync, sizeof(sync), 0, (struct sockaddr*)&dest, sizeof(dest));

    if (!audio_thread_running)
    {
        if (pthread_create(&audio_thread_id, NULL, audio_drain_thread, NULL) == 0)
            audio_thread_running = true;
    }
} // end start_audio_drain


/* Called with chain_lock held. */
static bool rebuild_chain(void)
{
    RXCHAIN *c = rxchain_create(device_get_hw_rate(), rx_rate, HACKRF_IF_OFFSET, send_iq_block, NULL);

    if (c == NULL)
        return false;
    rxchain_destroy(chain);
    chain = c;
    wb_fill = 0;
    wb_skip = 0;
    return true;
} // end rebuild_chain


static bool start_radio(void)
{
    bool ok = true;

    if (radio_started)
        return true;

    if (!device_is_open() && device_open(opts.serial) != 0)
        return false;

    pthread_mutex_lock(&chain_lock);
    if (chain == NULL)
        ok = rebuild_chain();
    pthread_mutex_unlock(&chain_lock);
    if (!ok)
        return false;

    if (!dsp_thread_running)
    {
        atomic_store(&dsp_thread_stop, false);
        if (pthread_create(&dsp_thread_id, NULL, dsp_thread, NULL) != 0)
            return false;
        dsp_thread_running = true;
    }

    if (device_start() != 0)
        return false;
    radio_started = true;
    fprintf(stderr, "hackrf_one: radio started  hw rate %ld  rx rate %ld  LO %lld\n", device_get_hw_rate(), rx_rate, device_get_lo());
    return true;
} // end start_radio


static void stop_radio(void)
{
    if (!radio_started)
        return;
    tx_set_mox(false);
    device_stop();
    radio_started = false;
    fprintf(stderr, "hackrf_one: radio stopped\n");
} // end stop_radio


static const char *attach_receiver(int8_t index)
{
    if (index != 0)
        return RECEIVER_INVALID;
    if (rx_attached)
        return RECEIVER_IN_USE;

    if (!start_radio())
        return DEVICE_FAILED;

    pthread_mutex_lock(&chain_lock);
    if (iq_socket < 0)
        iq_socket = udp_socket();
    iq_dest = dsp_endpoint(RX_IQ_PORT_0 + index + port_offset);
    rx_index = index;
    rx_attached = (iq_socket >= 0);
    pthread_mutex_unlock(&chain_lock);
    if (!rx_attached)
        return DEVICE_FAILED;

    start_audio_drain();
    snprintf(response, sizeof(response), "%s %ld", OK, rx_rate);
    fprintf(stderr, "hackrf_one: receiver %d attached\n", index);
    return response;
} // end attach_receiver


static const char *detach_receiver(int8_t index)
{
    if (tx_is_attached() && index == tx_index())
        return tx_detach();
    if (!rx_attached || index != rx_index)
        return RECEIVER_NOT_ATTACHED;
    pthread_mutex_lock(&chain_lock);
    rx_attached = false;
    pthread_mutex_unlock(&chain_lock);
    atomic_store(&wb_requested, false);
    fprintf(stderr, "hackrf_one: receiver %d detached\n", index);
    return OK;
} // end detach_receiver


static const char *set_rx_rate(const char *arg)
{
    long r = strtol(arg, NULL, 10);
    long old;
    bool ok;

    if (r < HACKRF_RX_RATE_MIN || r > HACKRF_RX_RATE_MAX)
        return INVALID_COMMAND;
    pthread_mutex_lock(&chain_lock);
    old = rx_rate;
    rx_rate = r;
    ok = rebuild_chain();
    if (!ok)
        rx_rate = old;
    pthread_mutex_unlock(&chain_lock);
    return ok ? OK : INVALID_COMMAND;
} // end set_rx_rate


static const char *set_hw_rate(const char *arg)
{
    long r = strtol(arg, NULL, 10);
    long old = device_get_hw_rate();
    bool ok;

    if (r < HACKRF_HW_RATE_MIN || r > HACKRF_HW_RATE_MAX)
        return INVALID_COMMAND;
    if (r == old)
        return OK;
    if (device_is_transmitting())
        return INVALID_COMMAND;
    pthread_mutex_lock(&chain_lock);
    ok = (device_set_hw_rate(r) == 0) && rebuild_chain();
    if (!ok)
    {
        device_set_hw_rate(old);
        rebuild_chain();
    }
    pthread_mutex_unlock(&chain_lock);
    fprintf(stderr, "hackrf_one: hardware rate %ld\n", device_get_hw_rate());
    return ok ? OK : INVALID_COMMAND;
} // end set_hw_rate


static char *manifest(void)
{
    static char xml[2048];

    snprintf(xml, sizeof(xml),
             "<radios=1>\n"
             "<radio=%d>\n"
             "<radio_type>hackrf_one</radio_type>\n"
             "<protocol>3</protocol>\n"
             "<device>%d</device>\n"
             "<use_tcp>0</use_tcp>\n"
             "<name>HackRF_One</name>\n"
             "<software_version>0</software_version>\n"
             "<status>2</status>\n"
             "<bandscope>1</bandscope>\n"
             "<supported_receivers>1</supported_receivers>\n"
             "<supported_transmitters>1</supported_transmitters>\n"
             "<adcs>1</adcs>\n"
             "<dacs>1</dacs>\n"
             "<frequency_min>%f</frequency_min>\n"
             "<frequency_max>%f</frequency_max>\n"
             "<mac_address>NA</mac_address>\n"
             "<ip_address>USB</ip_address>\n"
             "<local_audio>0</local_audio>\n"
             "<local_mic>0</local_mic>\n"
             "<ant_switch>0</ant_switch>\n"
             "</radio>\n"
             "</radios>\n",
             opts.radio_id, device_board_id(), 1000000.0, 6000000000.0);
    return xml;
} // end manifest


static const char *parse_command(const unsigned char *cmd, int len)
{
    int8_t index = (int8_t)cmd[0];
    const char *text = (const char*)cmd + 2;
    bool flag = (len > 2) && (cmd[2] != 0);

    if (len < 2)
        return INVALID_COMMAND;

    fprintf(stderr, "hackrf_one: command [%d] %u %u\n", index, cmd[1], len > 2 ? cmd[2] : 0);

    switch (cmd[1])
    {
    case STARTRADIO:
        return start_radio() ? OK : DEVICE_FAILED;

    case STOPRADIO:
        stop_radio();
        return OK;

    case HQHARDWARE:
        port_offset = len > 2 ? cmd[2] * PORT_BLOCK : 0;
        if (port_offset != 0)
            fprintf(stderr, "hackrf_one: dspserver port block %d (ports +%d)\n", cmd[2], port_offset);
        send_manifest = true;
        return OK;

    case HATTACHRX:
        return attach_receiver(index);

    case HDETACH:
        return detach_receiver(index);

    case HATTACHTX:
        if (!start_radio())
            return DEVICE_FAILED;
        return tx_attach(index, dsp_addr, opts.radio_id);

    case HMOX:
        if (!tx_is_attached())
            return TRANSMITTER_NONE;
        if (len < 3 || (cmd[2] != 0 && cmd[2] != 1))
            return INVALID_COMMAND;
        return tx_set_mox(cmd[2] == 1);

    case HSETSAMPLERATE:
        if (tx_is_attached() && index == tx_index())
            return OK;               // the TX input rate is fixed at 192 kHz
        return set_rx_rate(text);

    case HSETFREQ:
    {
        long long f = strtoll(text, NULL, 10);

        if (tx_is_attached() && index == tx_index())
            return device_set_tx_frequency(f) == 0 ? OK : DEVICE_FAILED;
        // As in hpsdr, the transmitter follows the receiver: QtRadio only
        // sends the TX stream a frequency when it connects.
        device_set_tx_frequency(f);
        return device_set_frequency(f) == 0 ? OK : DEVICE_FAILED;
    }

    case HSTARTBANDSCOPE:
        atomic_store(&wb_requested, true);
        return OK;

    case HSTOPBANDSCOPE:
        atomic_store(&wb_requested, false);
        return OK;

    case HSTARGETSERIAL:
        snprintf(response, sizeof(response), "%s %s firmware %s", OK, device_serial(), device_firmware());
        return response;

    case HUPDATEBANDSCOPE:
    case HSTARTXCVR:
    case HSTOPXCVR:
    case HSTARTIQ:
    case HSTOPIQ:
    case HSETRECORD:
        return OK;

    case SETLNAGAIN:
        return device_set_lna_gain(atoi(text)) == 0 ? OK : DEVICE_FAILED;

    case SETVGAGAIN:
        return device_set_vga_gain(atoi(text)) == 0 ? OK : DEVICE_FAILED;

    case SETRFAMP:
        return device_set_rf_amp(flag) == 0 ? OK : DEVICE_FAILED;

    case SETANTPOWER:
        return device_set_antenna_power(flag) == 0 ? OK : DEVICE_FAILED;

    case SETHWSAMPLERATE:
        return set_hw_rate(text);

    case SETTXVGAGAIN:
        return device_set_tx_gain(atoi(text)) == 0 ? OK : DEVICE_FAILED;

    case SETTXRFAMP:
        return device_set_tx_amp(flag) == 0 ? OK : DEVICE_FAILED;

    case SETFREQCORR:
        return device_set_ppm(strtod(text, NULL)) == 0 ? OK : INVALID_COMMAND;

    default:
        break;
    }
    return INVALID_COMMAND;
} // end parse_command


/* Read exactly n bytes; returns false on disconnect or stop. */
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
            int e = sock_error();
            if (e == SOCK_EAGAIN || e == SOCK_EWOULDBLOCK || e == SOCK_EINTR || e == SOCK_ETIMEDOUT)
            {
                if (atomic_load(&stop_requested))
                    return false;
                continue;
            }
            sock_perror("hackrf_one: command channel");
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

    opts = *options;
    if (inet_pton(AF_INET, opts.dsp_server_address, &dsp_addr) != 1)
    {
        fprintf(stderr, "hackrf_one: bad dspserver address %s\n", opts.dsp_server_address);
        return 1;
    }

    if (device_open(opts.serial) != 0)
    {
        fprintf(stderr, "hackrf_one: no HackRF found%s%s\n", opts.serial ? " with serial " : "", opts.serial ? opts.serial : "");
        return 1;
    }

    while (!atomic_load(&stop_requested))
    {
        struct sockaddr_in a = dsp_endpoint(COMMAND_PORT);
        int on = 1;
        int s = socket(AF_INET, SOCK_STREAM, 0);

        if (s < 0)
        {
            sock_perror("hackrf_one: socket");
            break;
        }
        if (connect(s, (struct sockaddr*)&a, sizeof(a)) < 0)
        {
            if (!waiting_reported)
                fprintf(stderr, "hackrf_one: waiting for dspserver at %s:%d\n", opts.dsp_server_address, COMMAND_PORT);
            waiting_reported = true;
            sock_close(s);
            sleep(1);
            continue;
        }
        waiting_reported = false;
        sock_set_rcvtimeo(s, 500);
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        fprintf(stderr, "hackrf_one: connected to dspserver %s:%d\n", opts.dsp_server_address, COMMAND_PORT);

        serve_connection(s);
        sock_close(s);

        // dspserver went away: stop streaming and wait for it to come back
        if (tx_is_attached())
            tx_detach();
        detach_receiver(rx_index);
        stop_radio();
        if (!atomic_load(&stop_requested))
        {
            fprintf(stderr, "hackrf_one: dspserver disconnected\n");
            sleep(1);
        }
    }

    stop_radio();
    if (dsp_thread_running)
    {
        atomic_store(&dsp_thread_stop, true);
        pthread_join(dsp_thread_id, NULL);
        dsp_thread_running = false;
    }
    if (audio_thread_running)
    {
        pthread_join(audio_thread_id, NULL);
        audio_thread_running = false;
    }
    device_close();

    pthread_mutex_lock(&chain_lock);
    rxchain_destroy(chain);
    chain = NULL;
    pthread_mutex_unlock(&chain_lock);
    if (iq_socket >= 0)
        sock_close(iq_socket);
    if (wb_socket >= 0)
        sock_close(wb_socket);
    if (audio_socket >= 0)
        sock_close(audio_socket);
    fprintf(stderr, "hackrf_one: exit\n");
    return 0;
} // end server_run
