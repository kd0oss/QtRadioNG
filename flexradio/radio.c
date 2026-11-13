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
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "radio.h"
#include "vita.h"

#define MAX_SAMPLES 4096

static RADIO_OPTIONS   opts;
static FLEX_RADIO_INFO info;
static bool            is_open = false;
static radio_iq_fn     iq_cb = NULL;
static void           *iq_user = NULL;

static uint32_t pan_id = 0;
static atomic_uint iq_stream_id = 0;
static atomic_int  iq_rate = 48000;
static atomic_bool payload_big_endian = false;
#define MAX_SLICES 16
static uint32_t slice_pan[MAX_SLICES];      // panadapter of each slice, from status
static int      slice_muted = -1;           // slice we have muted
static long long frequency = 14200000;
static int      rfgain_low = -10, rfgain_high = 30, rfgain_step = 10;

static int         udp_socket = -1;
static pthread_t   udp_id;
static bool        udp_running = false;
static atomic_bool udp_stop = false;
static atomic_ulong packets = 0;
static atomic_ulong lost = 0;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

// transmit
static uint32_t    tx_stream_id = 0;          // dax_tx stream we feed
static int         tx_slice = -1;             // slice we made the TX slice
static char        tx_mode[8] = "USB";
static int         fwd_meter = -1, ref_meter = -1;   // meter ids, from status
static char        fwd_unit[8] = "dBm", ref_unit[8] = "dBm";
static float       fwd_watts = 0.0f, ref_watts = 0.0f;
static struct sockaddr_in tx_dest;            // radio:4991


static float le_float(const uint8_t *p, bool big)
{
    uint32_t u;
    float f;

    if (big)
        u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    else
        u = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
    memcpy(&f, &u, sizeof(f));
    return f;
} // end le_float


/* Meter value to watts.  Meter packets carry int16 values scaled by unit:
 * dBm/dB/dBFS/SWR are value/128. */
static float meter_watts(int raw, const char *unit)
{
    if (strcmp(unit, "Watts") == 0)
        return (float)raw / 128.0f;
    return (float)(pow(10.0, ((double)raw / 128.0) / 10.0) / 1000.0);
} // end meter_watts


/* Remember the TX forward/reflected power meters of a meter definition. */
static void define_meter(int idx, const char *src, const char *nam, const char *unit)
{
    if (idx < 0 || strncmp(src, "TX", 2) != 0)
        return;
    pthread_mutex_lock(&state_lock);
    if (strcmp(nam, "FWDPWR") == 0)
    {
        fwd_meter = idx;
        snprintf(fwd_unit, sizeof(fwd_unit), "%s", unit[0] ? unit : "dBm");
    }
    else if (strcmp(nam, "REFPWR") == 0)
    {
        ref_meter = idx;
        snprintf(ref_unit, sizeof(ref_unit), "%s", unit[0] ? unit : "dBm");
    }
    pthread_mutex_unlock(&state_lock);
} // end define_meter


/* "meter 7.src=TX-#7.num=1#7.nam=FWDPWR#7.unit=dBm#8.src=..." (one or more
 * meters, '#' separated tokens "id.key=value") or "meter 7 removed". */
static void on_meter_status(const char *body)
{
    char tok[128];
    char src[16] = "", nam[16] = "", unit[8] = "";
    int cur = -1;
    const char *p = body;

    if (strstr(body, "removed") != NULL)
    {
        int idx = atoi(body);
        pthread_mutex_lock(&state_lock);
        if (idx == fwd_meter)
            fwd_meter = -1;
        if (idx == ref_meter)
            ref_meter = -1;
        pthread_mutex_unlock(&state_lock);
        return;
    }
    while (*p != '\0' && *p != '\r' && *p != '\n')
    {
        size_t n = strcspn(p, "#\r\n");
        char *dot, *eq;

        if (n > 0 && n < sizeof(tok))
        {
            memcpy(tok, p, n);
            tok[n] = '\0';
            dot = strchr(tok, '.');
            eq = dot != NULL ? strchr(dot, '=') : NULL;
            if (eq != NULL)
            {
                int idx = atoi(tok);
                *eq = '\0';
                if (idx != cur)
                {
                    define_meter(cur, src, nam, unit);
                    src[0] = nam[0] = unit[0] = '\0';
                    cur = idx;
                }
                if (strcmp(dot + 1, "src") == 0)
                    snprintf(src, sizeof(src), "%s", eq + 1);
                else if (strcmp(dot + 1, "nam") == 0)
                    snprintf(nam, sizeof(nam), "%s", eq + 1);
                else if (strcmp(dot + 1, "unit") == 0)
                    snprintf(unit, sizeof(unit), "%s", eq + 1);
            }
        }
        p += n;
        if (*p == '#')
            p++;
    }
    define_meter(cur, src, nam, unit);
} // end on_meter_status


/* Meter packet payload: pairs of big endian uint16 id, int16 value. */
static void on_meter_packet(const uint8_t *d, size_t len)
{
    pthread_mutex_lock(&state_lock);
    for (size_t k = 0; k + 4 <= len; k += 4)
    {
        int id = (d[k] << 8) | d[k + 1];
        int v = (int16_t)((d[k + 2] << 8) | d[k + 3]);
        if (id == fwd_meter)
            fwd_watts = meter_watts(v, fwd_unit);
        else if (id == ref_meter)
            ref_watts = meter_watts(v, ref_unit);
    }
    pthread_mutex_unlock(&state_lock);
} // end on_meter_packet


/* Status lines we care about:
 *   display pan 0x40000000 center=14.100000 ... daxiq_channel=1 rxant=ANT1 rfgain=0
 *   stream 0x20000000 type=dax_iq daxiq_channel=1 pan=0x40000000 daxiq_rate=48000 ...
 *   slice 0 RF_frequency=14.100000 ... pan=0x40000000 ... in_use=1 */
static void on_status(const char *s)
{
    char v[64];

    if (strncmp(s, "stream ", 7) == 0)
    {
        uint32_t id = (uint32_t)strtoul(s + 7, NULL, 16);
        if (strstr(s, "removed") != NULL)
            return;
        if (vita_kv(s, "type", v, sizeof(v)) && strcmp(v, "dax_iq") == 0 &&
            vita_kv(s, "daxiq_channel", v, sizeof(v)) && atoi(v) == opts.daxiq_channel)
        {
            if (atomic_load(&iq_stream_id) == 0)
                atomic_store(&iq_stream_id, id);
            if (id == atomic_load(&iq_stream_id))
            {
                if (vita_kv(s, "daxiq_rate", v, sizeof(v)) && atoi(v) > 0)
                    atomic_store(&iq_rate, atoi(v));
                if (vita_kv(s, "payload_endian", v, sizeof(v)))
                    atomic_store(&payload_big_endian, strcmp(v, "big") == 0);
            }
        }
        return;
    }

    if (strncmp(s, "meter ", 6) == 0)
    {
        on_meter_status(s + 6);
        return;
    }

    if (strncmp(s, "slice ", 6) == 0)
    {
        // A panafall create makes a slice too; status for it can arrive
        // before the create reply, so remember every slice's panadapter.
        int idx = atoi(s + 6);
        if (idx < 0 || idx >= MAX_SLICES)
            return;
        pthread_mutex_lock(&state_lock);
        if (vita_kv(s, "pan", v, sizeof(v)))
            slice_pan[idx] = (uint32_t)strtoul(v, NULL, 16);
        if (vita_kv(s, "in_use", v, sizeof(v)) && atoi(v) == 0)
        {
            slice_pan[idx] = 0;
            if (slice_muted == idx)
                slice_muted = -1;
        }
        pthread_mutex_unlock(&state_lock);
        return;
    }
} // end on_status


static void *udp_thread(void *arg)
{
    static uint8_t buf[65536];
    static double iq[MAX_SAMPLES * 2];
    int last_count = -1;
    double scale = 1.0;

    (void)arg;
    while (!atomic_load(&udp_stop))
    {
        ssize_t n = recv(udp_socket, buf, sizeof(buf), 0);
        VITA_PACKET p;

        if (n <= 0)
            continue;
        if (!vita_parse(buf, (size_t)n, &p) || !p.has_stream_id || !p.has_class_id)
            continue;
        if (p.packet_class == VITA_PCC_METER)
        {
            on_meter_packet(p.payload, p.payload_bytes);
            continue;
        }
        int rate = vita_iq_rate(p.packet_class);
        if (rate == 0)
            continue;                   // FFT, audio ...
        uint32_t want = atomic_load(&iq_stream_id);
        if (want == 0)
            atomic_store(&iq_stream_id, p.stream_id);
        else
            if (p.stream_id != want)
                continue;

        atomic_fetch_add(&packets, 1);
        if (last_count >= 0 && p.count != ((last_count + 1) & 0xF))
            atomic_fetch_add(&lost, (unsigned long)((p.count - last_count - 1) & 0xF));
        last_count = p.count;

        int ns = (int)(p.payload_bytes / 8);
        if (ns > MAX_SAMPLES)
            ns = MAX_SAMPLES;
        bool big = atomic_load(&payload_big_endian);
        for (int k = 0; k < ns; k++)
        {
            double i = le_float(p.payload + k * 8, big);
            double q = le_float(p.payload + k * 8 + 4, big);
            if (!isfinite(i) || !isfinite(q))
                i = q = 0.0;
            if (opts.swap_iq)
            {
                double t = i;
                i = q;
                q = t;
            }
            // Samples are normalised to +/-1 (a complex sample cannot exceed
            // about 1.42).  Older firmware is reported to scale to +/-32768:
            // the first sample beyond 2 switches to that scale for good.
            if (scale == 1.0 && (fabs(i) > 2.0 || fabs(q) > 2.0))
            {
                scale = 1.0 / 32768.0;
                fprintf(stderr, "flexradio: IQ samples are scaled to 32768, rescaling\n");
                for (int j = 0; j < k * 2; j++)
                    iq[j] *= scale;
            }
            iq[k * 2] = i * scale;
            iq[k * 2 + 1] = q * scale;
        }
        if (rate != atomic_load(&iq_rate))
            atomic_store(&iq_rate, rate);
        if (iq_cb != NULL && ns > 0)
            iq_cb(iq_user, iq, ns, rate);
    }
    return NULL;
} // end udp_thread


static bool open_udp(void)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    struct timeval tv = {0, 200000};
    int size = 4 << 20;

    udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket < 0)
        return false;
    setsockopt(udp_socket, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)opts.udp_port);
    if (bind(udp_socket, (struct sockaddr*)&a, sizeof(a)) < 0)
    {
        perror("flexradio: UDP bind");
        close(udp_socket);
        udp_socket = -1;
        return false;
    }
    getsockname(udp_socket, (struct sockaddr*)&a, &al);
    opts.udp_port = ntohs(a.sin_port);
    return true;
} // end open_udp


static uint32_t parse_id(const char *reply)
{
    const char *p = strstr(reply, "pan=");

    if (p != NULL)
        reply = p + 4;
    return (uint32_t)strtoul(reply, NULL, 16);     // accepts "0x40000000,0x42000000" too
} // end parse_id


bool radio_open(const RADIO_OPTIONS *options, radio_iq_fn iq, void *user)
{
    char reply[1024];

    opts = *options;
    iq_cb = iq;
    iq_user = user;
    memset(&info, 0, sizeof(info));

    if (opts.address != NULL && opts.address[0] != '\0')
    {
        snprintf(info.ip, sizeof(info.ip), "%s", opts.address);
        info.port = FLEX_API_PORT;
        snprintf(info.model, sizeof(info.model), "FLEX");
    }
    else
    {
        fprintf(stderr, "flexradio: looking for a radio%s%s ...\n", opts.serial ? " with serial " : "", opts.serial ? opts.serial : "");
        if (!flex_discover(opts.serial, 10, &info))
        {
            fprintf(stderr, "flexradio: no radio found (use -a <address> if discovery is blocked)\n");
            return false;
        }
        fprintf(stderr, "flexradio: found %s \"%s\" serial %s version %s at %s\n",
                info.model, info.name, info.serial, info.version, info.ip);
    }

    if (!open_udp())
        return false;
    if (!flex_connect(info.ip, info.port, on_status))
    {
        close(udp_socket);
        udp_socket = -1;
        return false;
    }

    atomic_store(&udp_stop, false);
    if (pthread_create(&udp_id, NULL, udp_thread, NULL) == 0)
        udp_running = true;

    // UDP first (it is shared by all our streams), then become a GUI client:
    // only GUI clients may create panadapters.
    if (flex_command(NULL, 0, "client udpport %d", opts.udp_port) != 0)
        fprintf(stderr, "flexradio: radio refused UDP port %d\n", opts.udp_port);
    flex_command(NULL, 0, "client program QtRadio");
    if (flex_command(reply, sizeof(reply), "client gui") != 0)
    {
        fprintf(stderr, "flexradio: radio refused \"client gui\" (too many GUI clients?)\n");
        radio_close();
        return false;
    }
    flex_command(NULL, 0, "client station QtRadio");
    if (flex_command(NULL, 0, "keepalive enable") == 0)
        flex_start_keepalive();
    flex_command(NULL, 0, "sub slice all");
    flex_command(NULL, 0, "sub pan all");
    flex_command(NULL, 0, "sub daxiq all");

    if (flex_command(reply, sizeof(reply), "display panafall create x=100 y=100") != 0 &&
        flex_command(reply, sizeof(reply), "display pan create x=100 y=100") != 0)
    {
        fprintf(stderr, "flexradio: could not create a panadapter\n");
        radio_close();
        return false;
    }
    pan_id = parse_id(reply);
    fprintf(stderr, "flexradio: panadapter 0x%08X\n", pan_id);

    if (flex_command(reply, sizeof(reply), "display pan rfgain_info 0x%08X", pan_id) == 0)
    {
        int lo, hi, step;
        if (sscanf(reply, "%d,%d,%d", &lo, &hi, &step) == 3 && step > 0 && hi >= lo)
        {
            rfgain_low = lo;
            rfgain_high = hi;
            rfgain_step = step;
        }
    }

    flex_command(NULL, 0, "display pan set 0x%08X center=%.6f bandwidth=%.6f",
                 pan_id, (double)frequency / 1e6, 0.2);
    flex_command(NULL, 0, "display pan set 0x%08X daxiq_channel=%d", pan_id, opts.daxiq_channel);

    if (flex_command(reply, sizeof(reply), "stream create type=dax_iq daxiq_channel=%d", opts.daxiq_channel) != 0 &&
        flex_command(reply, sizeof(reply), "stream create daxiq=%d", opts.daxiq_channel) != 0)
    {
        fprintf(stderr, "flexradio: could not create the DAX IQ stream on channel %d\n", opts.daxiq_channel);
        radio_close();
        return false;
    }
    if (strtoul(reply, NULL, 16) != 0)
        atomic_store(&iq_stream_id, (unsigned)strtoul(reply, NULL, 16));
    fprintf(stderr, "flexradio: DAX IQ stream 0x%08X on channel %d\n", atomic_load(&iq_stream_id), opts.daxiq_channel);

    memset(&tx_dest, 0, sizeof(tx_dest));
    tx_dest.sin_family = AF_INET;
    tx_dest.sin_port = htons(FLEX_DAX_TX_PORT);
    inet_pton(AF_INET, info.ip, &tx_dest.sin_addr);

    is_open = true;
    return true;
} // end radio_open


void radio_close(void)
{
    radio_tx_close();
    if (flex_connected())
    {
        if (atomic_load(&iq_stream_id) != 0)
            flex_command(NULL, 0, "stream remove 0x%08X", atomic_load(&iq_stream_id));
        if (pan_id != 0)
            flex_command(NULL, 0, "display pan remove 0x%08X", pan_id);
    }
    flex_disconnect();
    if (udp_running)
    {
        atomic_store(&udp_stop, true);
        pthread_join(udp_id, NULL);
        udp_running = false;
    }
    if (udp_socket >= 0)
    {
        close(udp_socket);
        udp_socket = -1;
    }
    atomic_store(&iq_stream_id, 0);
    pan_id = 0;
    memset(slice_pan, 0, sizeof(slice_pan));
    slice_muted = -1;
    is_open = false;
} // end radio_close


bool radio_is_open(void)
{
    return is_open && flex_connected();
} // end radio_is_open


const FLEX_RADIO_INFO *radio_info(void)
{
    return &info;
} // end radio_info


/* The radio's slice on our panadapter, or -1.  Call with state_lock held. */
static int our_slice(void)
{
    for (int k = 0; k < MAX_SLICES && pan_id != 0; k++)
    {
        if (slice_pan[k] == pan_id)
            return k;
    }
    return -1;
} // end our_slice


int radio_set_frequency(long long hz)
{
    int idx;
    bool mute;
    long rc;

    frequency = hz;
    if (!radio_is_open())
        return 0;

    // Keep the radio's own slice with the panadapter (and silent: QtRadio
    // does the demodulating), then move the panadapter, which is where the
    // DAX IQ is centred.  The slice is also the one that transmits.
    pthread_mutex_lock(&state_lock);
    idx = our_slice();
    mute = idx >= 0 && slice_muted != idx;
    if (mute)
        slice_muted = idx;
    pthread_mutex_unlock(&state_lock);
    if (mute)
        flex_command(NULL, 0, "slice set %d audio_mute=1", idx);
    if (idx >= 0)
        flex_command(NULL, 0, "slice tune %d %.6f autopan=0", idx, (double)hz / 1e6);
    rc = flex_command(NULL, 0, "display pan set 0x%08X center=%.6f", pan_id, (double)hz / 1e6);
    return rc == 0 ? 0 : -1;
} // end radio_set_frequency


int radio_set_rate(int hz)
{
    uint32_t id = atomic_load(&iq_stream_id);

    if (hz != 24000 && hz != 48000 && hz != 96000 && hz != 192000)
        return -1;
    if (!radio_is_open() || id == 0)
        return -1;
    if (flex_command(NULL, 0, "stream set 0x%08X daxiq_rate=%d", id, hz) != 0 &&
        flex_command(NULL, 0, "dax iq set %d rate=%d", opts.daxiq_channel, hz / 1000) != 0)
        return -1;
    atomic_store(&iq_rate, hz);
    return 0;
} // end radio_set_rate


int radio_get_rate(void)
{
    return atomic_load(&iq_rate);
} // end radio_get_rate


int radio_set_antenna(const char *ant)
{
    if (!radio_is_open())
        return -1;
    return flex_command(NULL, 0, "display pan set 0x%08X rxant=%s", pan_id, ant) == 0 ? 0 : -1;
} // end radio_set_antenna


int radio_set_rfgain(int db)
{
    if (!radio_is_open())
        return -1;
    // round to the radio's steps and range
    if (db < rfgain_low)
        db = rfgain_low;
    if (db > rfgain_high)
        db = rfgain_high;
    db = rfgain_low + ((db - rfgain_low + rfgain_step / 2) / rfgain_step) * rfgain_step;
    return flex_command(NULL, 0, "display pan set 0x%08X rfgain=%d", pan_id, db) == 0 ? 0 : -1;
} // end radio_set_rfgain


void radio_stats(unsigned long *p, unsigned long *l)
{
    *p = atomic_load(&packets);
    *l = atomic_load(&lost);
} // end radio_stats


/* ---------------------------------------------------------------- transmit */

int radio_tx_open(void)
{
    char reply[256];
    int idx;

    if (!radio_is_open())
        return -1;
    if (tx_stream_id != 0)
        return 0;

    flex_command(NULL, 0, "sub tx all");
    flex_command(NULL, 0, "sub meter all");
    if (flex_command(reply, sizeof(reply), "stream create type=dax_tx") != 0 ||
        strtoul(reply, NULL, 16) == 0)
    {
        fprintf(stderr, "flexradio: could not create a DAX TX stream\n");
        return -1;
    }
    tx_stream_id = (uint32_t)strtoul(reply, NULL, 16);
    fprintf(stderr, "flexradio: DAX TX stream 0x%08X\n", tx_stream_id);

    // Our audio replaces the radio's microphone in every mode.
    if (flex_command(NULL, 0, "transmit set dax=1") != 0)
        fprintf(stderr, "flexradio: radio refused \"transmit set dax=1\"\n");

    pthread_mutex_lock(&state_lock);
    idx = our_slice();
    pthread_mutex_unlock(&state_lock);
    if (idx < 0)
        fprintf(stderr, "flexradio: no slice on our panadapter to transmit with\n");
    else
    {
        tx_slice = idx;
        flex_command(NULL, 0, "slice set %d tx=1", idx);
        flex_command(NULL, 0, "slice set %d mode=%s", idx, tx_mode);
    }
    return 0;
} // end radio_tx_open


void radio_tx_close(void)
{
    if (tx_stream_id == 0)
        return;
    if (flex_connected())
    {
        flex_command(NULL, 0, "xmit 0");
        flex_command(NULL, 0, "transmit set dax=0");
        flex_command(NULL, 0, "stream remove 0x%08X", tx_stream_id);
    }
    tx_stream_id = 0;
    tx_slice = -1;
} // end radio_tx_close


uint32_t radio_tx_stream(void)
{
    return tx_stream_id;
} // end radio_tx_stream


int radio_set_mox(bool on)
{
    if (!radio_is_open() || tx_stream_id == 0)
        return -1;
    if (on)
    {
        int idx;
        // the slice may have been closed and re-made since attach
        pthread_mutex_lock(&state_lock);
        idx = our_slice();
        pthread_mutex_unlock(&state_lock);
        if (idx >= 0 && idx != tx_slice)
        {
            tx_slice = idx;
            flex_command(NULL, 0, "slice set %d tx=1", idx);
            flex_command(NULL, 0, "slice set %d mode=%s", idx, tx_mode);
        }
    }
    else
    {
        pthread_mutex_lock(&state_lock);
        fwd_watts = ref_watts = 0.0f;
        pthread_mutex_unlock(&state_lock);
    }
    return flex_command(NULL, 0, "xmit %d", on ? 1 : 0) == 0 ? 0 : -1;
} // end radio_set_mox


int radio_set_mode(const char *mode)
{
    snprintf(tx_mode, sizeof(tx_mode), "%s", mode);
    if (!radio_is_open() || tx_slice < 0)
        return 0;                    // applied when the transmitter attaches
    return flex_command(NULL, 0, "slice set %d mode=%s", tx_slice, tx_mode) == 0 ? 0 : -1;
} // end radio_set_mode


int radio_set_rfpower(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    if (!radio_is_open())
        return -1;
    return flex_command(NULL, 0, "transmit set rfpower=%d", percent) == 0 ? 0 : -1;
} // end radio_set_rfpower


int radio_send_dax_tx(const uint8_t *packet, size_t len)
{
    if (udp_socket < 0 || tx_stream_id == 0)
        return -1;
    // from the socket the radio already knows (client udpport)
    if (sendto(udp_socket, packet, len, 0, (struct sockaddr*)&tx_dest, sizeof(tx_dest)) < 0)
        return -1;
    return 0;
} // end radio_send_dax_tx


void radio_power(float *fwd, float *ref)
{
    pthread_mutex_lock(&state_lock);
    *fwd = fwd_watts;
    *ref = ref_watts;
    pthread_mutex_unlock(&state_lock);
} // end radio_power
