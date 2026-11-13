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
 * example_plugin - the smallest useful protocol plugin, and a template.
 *
 * Mode "NFMX": narrow band FM done outside WDSP.
 *   receive:  48 kHz I/Q -> discriminator -> 3.5 kHz low pass -> 8 kHz audio
 *   transmit: 8 kHz mic (or a 1 kHz test tone) -> 48 kHz -> FM -> I/Q,
 *             with 200 ms of carrier after PTT is released (a stand-in for
 *             a digital mode's end of transmission)
 *   panel:    deviation, test tone, a note that is echoed back, a ping
 *             button; receive level as text and as a meter; an event log.
 *
 *   example_plugin [-s dspserver_host] [-p port]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include "plugin_client.h"

#define RX_RATE     48000
#define AUDIO_RATE  8000
#define RATIO       (RX_RATE / AUDIO_RATE)
#define TAPS        96
#define MAX_STREAMS 64
#define TAIL_S      0.2

static const char *XML =
    "<plugin name=\"example\" version=\"1\" title=\"Example (NFM outside WDSP)\">\n"
    "  <mode name=\"NFMX\" rx_rate=\"48000\" audio_rate=\"8000\" tx_rate=\"48000\" mic_rate=\"8000\"\n"
    "        filter_low=\"-6000\" filter_high=\"6000\" tx_tail_ms=\"300\"/>\n"
    "  <panel>\n"
    "    <combo  key=\"dev\"  label=\"Deviation\" items=\"2500,5000\" default=\"2500\" persist=\"1\"/>\n"
    "    <check  key=\"tone\" label=\"1 kHz test tone\" default=\"0\"/>\n"
    "    <text   key=\"note\" label=\"Note\" max=\"20\" persist=\"1\"/>\n"
    "    <button key=\"ping\" label=\"Ping\"/>\n"
    "    <status key=\"level\" label=\"RX level\"/>\n"
    "    <meter  key=\"rxlevel\" label=\"Level\" min=\"-100\" max=\"0\" unit=\"dBFS\"/>\n"
    "    <status key=\"echo\" label=\"Echo\"/>\n"
    "    <log    key=\"log\"  label=\"Events\" lines=\"4\"/>\n"
    "  </panel>\n"
    "</plugin>\n";

typedef struct
{
    bool   active;
    bool   tx;
    // receive
    double last_i, last_q;
    float  hist[TAPS * 2];
    int    pos, phase;
    double power;
    long   power_n;
    // transmit
    float  thist[TAPS * 2];
    int    tpos;
    double fm_phase;
    double tone_phase;
    bool   keyed;
    double tail_left;            // seconds of carrier still to send
} STREAM;

static PLUGIN_CONN *conn;
static STREAM       st[MAX_STREAMS];
static float        taps[TAPS];
static double       deviation = 2500.0;
static bool         test_tone = false;
static volatile bool stop = false;


static void design_lowpass(float *h, int n, double fc, double fs, double gain)
{
    double sum = 0.0;
    for (int k = 0; k < n; k++)
    {
        double m = k - (n - 1) / 2.0;
        double x = 2.0 * fc / fs;
        double v = (m == 0.0) ? x : sin(M_PI * x * m) / (M_PI * m);
        v *= 0.42 - 0.5 * cos(2.0 * M_PI * k / (n - 1)) + 0.08 * cos(4.0 * M_PI * k / (n - 1));
        h[k] = (float)v;
        sum += v;
    }
    for (int k = 0; k < n; k++)
        h[k] = (float)(h[k] * gain / sum);
} // end design_lowpass


static float fir(float *hist, int *pos, float x)
{
    double acc = 0.0;
    hist[*pos] = hist[*pos + TAPS] = x;
    *pos = (*pos + 1) % TAPS;
    for (int k = 0; k < TAPS; k++)
        acc += taps[k] * hist[*pos + k];
    return (float)acc;
} // end fir


static void on_accept(void *user, const char *text)
{
    (void)user;
    fprintf(stderr, "example_plugin: accepted by dspserver:\n%s", text);
} // end on_accept


static void on_start(void *user, int stream, bool tx, const char *mode)
{
    (void)user;
    if (stream < 0 || stream >= MAX_STREAMS)
        return;
    memset(&st[stream], 0, sizeof(STREAM));
    st[stream].active = true;
    st[stream].tx = tx;
    st[stream].last_i = 1.0;
    fprintf(stderr, "example_plugin: stream %d %s %s\n", stream, tx ? "transmit" : "receive", mode);
    plugin_status(conn, stream, "log=stream %d: %s %s\n", stream, tx ? "transmit" : "receive", mode);
} // end on_start


static void on_stop(void *user, int stream)
{
    (void)user;
    if (stream >= 0 && stream < MAX_STREAMS)
        st[stream].active = false;
    fprintf(stderr, "example_plugin: stream %d stopped\n", stream);
} // end on_stop


static void on_rxiq(void *user, int stream, const float *iq, int n)
{
    STREAM *s;
    float audio[2048];
    int na = 0;

    (void)user;
    if (stream < 0 || stream >= MAX_STREAMS || !st[stream].active || st[stream].tx)
        return;
    s = &st[stream];
    for (int k = 0; k < n; k++)
    {
        double i = iq[2 * k], q = iq[2 * k + 1];
        double re = i * s->last_i + q * s->last_q;
        double im = q * s->last_i - i * s->last_q;
        double f = (re == 0.0 && im == 0.0) ? 0.0 : atan2(im, re) * RX_RATE / (2.0 * M_PI);
        s->last_i = i;
        s->last_q = q;
        s->power += i * i + q * q;
        s->power_n++;

        float y = fir(s->hist, &s->pos, (float)(f / deviation * 0.5));
        if (++s->phase == RATIO)
        {
            s->phase = 0;
            if (na < (int)(sizeof(audio) / sizeof(audio[0])))
                audio[na++] = y;
        }
    }
    plugin_send_audio(conn, stream, audio, na);

    if (s->power_n >= RX_RATE / 2)       // twice a second
    {
        double db = 10.0 * log10(s->power / s->power_n + 1e-20);
        plugin_status(conn, stream, "level=%.1f dBFS\nrxlevel=%.1f\n", db, db);
        s->power = 0.0;
        s->power_n = 0;
    }
} // end on_rxiq


static void on_mic(void *user, int stream, const float *audio, int n)
{
    STREAM *s;
    float iq[2 * 2048 * RATIO];
    int nq = 0;

    (void)user;
    if (stream < 0 || stream >= MAX_STREAMS || !st[stream].active || !st[stream].tx)
        return;
    s = &st[stream];
    if (n > 2048)
        n = 2048;
    for (int k = 0; k < n; k++)
    {
        float a;
        if (!s->keyed)
            a = 0.0f;                                   // tail: carrier only
        else if (test_tone)
        {
            a = (float)(0.5 * sin(s->tone_phase));
            s->tone_phase = fmod(s->tone_phase + 2.0 * M_PI * 1000.0 / AUDIO_RATE, 2.0 * M_PI);
        }
        else
            a = audio[k];
        for (int j = 0; j < RATIO; j++)
        {
            // zero stuffing, the low pass restores the level (gain RATIO)
            float x = fir(s->thist, &s->tpos, j == 0 ? a * RATIO : 0.0f);
            s->fm_phase = fmod(s->fm_phase + 2.0 * M_PI * deviation * x / RX_RATE, 2.0 * M_PI);
            iq[2 * nq] = (float)(0.8 * cos(s->fm_phase));
            iq[2 * nq + 1] = (float)(0.8 * sin(s->fm_phase));
            nq++;
        }
    }
    plugin_send_txiq(conn, stream, iq, nq);

    if (!s->keyed && s->tail_left > 0.0)
    {
        s->tail_left -= (double)n / AUDIO_RATE;
        if (s->tail_left <= 0.0)
        {
            plugin_send_txdone(conn, stream);
            plugin_status(conn, stream, "log=transmission ended\n");
        }
    }
} // end on_mic


static void on_mox(void *user, int stream, bool on)
{
    (void)user;
    if (stream < 0 || stream >= MAX_STREAMS)
        return;
    st[stream].keyed = on;
    if (on)
    {
        st[stream].tail_left = 0.0;
        plugin_status(conn, stream, "log=transmitting%s\n", test_tone ? " (test tone)" : "");
    }
    else
        st[stream].tail_left = TAIL_S;
} // end on_mox


static void on_setting(void *user, int stream, const char *key, const char *value)
{
    (void)user;
    if (strcmp(key, "dev") == 0 && atof(value) > 0.0)
        deviation = atof(value);
    else if (strcmp(key, "tone") == 0)
        test_tone = atoi(value) != 0;
    else if (strcmp(key, "note") == 0)
        plugin_status(conn, -1, "echo=%s\n", value);
    else if (strcmp(key, "ping") == 0)
        plugin_status(conn, -1, "log=pong\n");
    fprintf(stderr, "example_plugin: setting %s=%s (stream %d)\n", key, value, stream);
} // end on_setting


static void on_signal(int sig)
{
    (void)sig;
    stop = true;
} // end on_signal


int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int port = PLUGIN_PORT;
    int opt;
    PLUGIN_CALLBACKS cb = {on_accept, on_start, on_stop, on_rxiq, on_mic, on_setting, on_mox};

    while ((opt = getopt(argc, argv, "s:p:h")) != -1)
    {
        switch (opt)
        {
        case 's':
            host = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        default:
            fprintf(stderr, "usage: %s [-s dspserver_host] [-p port]\n", argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    design_lowpass(taps, TAPS, 3500.0, RX_RATE, 1.0);

    conn = plugin_open(host, port, XML);
    if (conn == NULL)
        return 1;
    bool ok = plugin_run(conn, &cb, NULL, &stop);
    if (!ok)
        fprintf(stderr, "example_plugin: dspserver closed the connection\n");
    plugin_close(conn);
    return ok ? 0 : 1;
} // end main
