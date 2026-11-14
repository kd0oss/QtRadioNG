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
 * Protocol plugins.
 *
 *  - A listener thread accepts plugins on PLUGIN_PORT.  Each plugin gets a
 *    reader thread (its frames) and a writer thread (a queue, so the IQ and
 *    TX threads never block on a slow plugin; sample frames are dropped
 *    when the queue is full).
 *  - Receive: the receiver's I/Q is shifted to the dial frequency (LO
 *    offset), resampled to the plugin's rx_rate and sent.  The plugin's
 *    audio is resampled to the receiver rate into a FIFO, which replaces
 *    WDSP's audio block for block.
 *  - Transmit: mic audio (48 kHz) is resampled to the plugin's mic_rate and
 *    sent; the plugin's I/Q is resampled to 192 kHz into a FIFO, which
 *    replaces WDSP's transmit I/Q.
 *
 * The FIFOs are primed (PRIME_MS) before they are used and kept from
 * growing without bound; the plugin's output follows its input sample for
 * sample, so they stay level.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "qtr_net.h"
#include <samplerate.h>
#include <event2/bufferevent.h>

#include "plugin.h"
#include "server.h"

/* strndup is POSIX 2008 and missing from mingw-w64 */
static char *plugin_strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *p = malloc(len + 1);

    if (p != NULL)
    {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}

#define PRIME_MS        120
#define FIFO_SECONDS    2
#define QUEUE_LIMIT     (4 << 20)        // bytes of queued sample frames per plugin
#define TX_RATE         192000
#define MIC_RATE        48000

extern double LO_offset;
extern void server_broadcast_plugin(const unsigned char *data, size_t len);

typedef struct
{
    char name[16];
    int  number;
    int  rx_rate;
    int  audio_rate;
    int  tx_rate;
    int  mic_rate;
    int  filter_low;
    int  filter_high;
    int  tx_tail_ms;
} PLUGIN_MODE;

typedef struct _qitem
{
    struct _qitem *next;
    size_t         len;
    bool           samples;
    unsigned char  data[];
} QITEM;

typedef struct
{
    bool            used;
    int             id;
    int             fd;
    char            name[32];
    char           *xml;
    int             nmodes;
    PLUGIN_MODE     mode[PLUGIN_MAX_MODES];
    pthread_t       reader;
    pthread_t       writer;
    pthread_mutex_t qlock;
    pthread_cond_t  qcond;
    QITEM          *qhead, *qtail;
    size_t          qbytes;
    bool            closing;
} PLUGIN;

typedef struct
{
    float *buf;
    long   cap;                 // floats
    long   head, tail;          // floats written / read
    bool   primed;
    long   underruns, trims;
} FIFO;

typedef struct
{
    pthread_mutex_t lock;
    bool            in_mode;     // the stream's mode is a plugin mode ...
    int             mode_number; // ... this one
    PLUGIN         *plugin;      // its plugin; NULL if not connected (silence)
    int             mode;        // index in plugin->mode
    bool            tx;
    double          rx_gain;
    double          mic_gain;
    // receive
    SRC_STATE      *rx_src;      // receiver rate -> rx_rate, 2 channels
    int             rx_in_rate;
    double          lo_phase;
    SRC_STATE      *audio_src;   // audio_rate -> receiver rate, 1 channel
    int             audio_out_rate;
    FIFO            audio;       // mono, receiver rate
    // transmit
    SRC_STATE      *mic_src;     // 48 kHz -> mic_rate
    SRC_STATE      *txiq_src;    // tx_rate -> 192 kHz, 2 channels
    FIFO            txiq;        // I,Q at 192 kHz
    bool            keyed;
    bool            tail;
    bool            txdone;
    struct timespec tail_start;
    struct timespec last_tick;
} PSTREAM;

static PLUGIN          plugins[PLUGIN_MAX];
static PSTREAM         pstream[MAX_RFSTREAMS];
static pthread_mutex_t plugins_lock = PTHREAD_MUTEX_INITIALIZER;   // plugins[] membership


/* ------------------------------------------------------------------ utils */

static double now_s(const struct timespec *t)
{
    return t->tv_sec + t->tv_nsec * 1e-9;
} // end now_s


static double elapsed(const struct timespec *since)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return now_s(&t) - now_s(since);
} // end elapsed


static void fifo_init(FIFO *f, long cap)
{
    if (f->buf == NULL || f->cap != cap)
    {
        free(f->buf);
        f->buf = (float*)calloc(cap, sizeof(float));
        f->cap = cap;
    }
    f->head = f->tail = 0;
    f->primed = false;
    f->underruns = f->trims = 0;
} // end fifo_init


static void fifo_free(FIFO *f)
{
    free(f->buf);
    f->buf = NULL;
    f->cap = 0;
} // end fifo_free


static void fifo_put(FIFO *f, const float *d, long n)
{
    if (f->buf == NULL)
        return;
    for (long k = 0; k < n; k++)
    {
        f->buf[f->head % f->cap] = d[k];
        f->head++;
    }
    if (f->head - f->tail > f->cap)
        f->tail = f->head - f->cap;          // overrun: lose the oldest
} // end fifo_put


/* n floats out; zeros until primed (prime floats) or after an underrun. */
static void fifo_get(FIFO *f, float *d, long n, long prime, long high)
{
    long level = f->head - f->tail;

    if (f->buf == NULL)
    {
        memset(d, 0, n * sizeof(float));
        return;
    }
    if (!f->primed && level >= prime)
        f->primed = true;
    if (level > high)
    {
        f->tail = f->head - prime;           // fell behind: drop back to the target
        level = prime;
        f->trims++;
    }
    if (!f->primed || level < n)
    {
        memset(d, 0, n * sizeof(float));
        if (f->primed && level < n)
        {
            f->primed = false;               // ran dry: refill first
            f->underruns++;
        }
        return;
    }
    for (long k = 0; k < n; k++)
        d[k] = f->buf[(f->tail + k) % f->cap];
    f->tail += n;
} // end fifo_get


/* XML attribute value of tag (the text from "<mode" to ">") */
static bool xml_attr(const char *tag, const char *name, char *out, size_t size)
{
    char pat[48];
    const char *p;

    snprintf(pat, sizeof(pat), " %s=\"", name);
    p = strstr(tag, pat);
    if (p == NULL)
    {
        snprintf(pat, sizeof(pat), "\t%s=\"", name);
        p = strstr(tag, pat);
        if (p == NULL)
        {
            snprintf(pat, sizeof(pat), "\n%s=\"", name);
            p = strstr(tag, pat);
            if (p == NULL)
                return false;
        }
    }
    p += strlen(pat);
    size_t n = strcspn(p, "\"");
    if (n >= size)
        n = size - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
} // end xml_attr


static int xml_int(const char *tag, const char *name, int def)
{
    char v[32];
    return xml_attr(tag, name, v, sizeof(v)) ? atoi(v) : def;
} // end xml_int


/* ------------------------------------------------------------- the queue */

static void queue_frame(PLUGIN *p, uint8_t type, int8_t stream, const void *payload, size_t len, bool samples)
{
    QITEM *q;

    pthread_mutex_lock(&p->qlock);
    if (p->closing || (samples && p->qbytes > QUEUE_LIMIT))
    {
        pthread_mutex_unlock(&p->qlock);
        return;
    }
    pthread_mutex_unlock(&p->qlock);

    q = (QITEM*)malloc(sizeof(QITEM) + PLUGIN_HEADER + len);
    if (q == NULL)
        return;
    uint32_t l = (uint32_t)(len + 2);
    q->data[0] = (uint8_t)(l >> 24);
    q->data[1] = (uint8_t)(l >> 16);
    q->data[2] = (uint8_t)(l >> 8);
    q->data[3] = (uint8_t)l;
    q->data[4] = type;
    q->data[5] = (uint8_t)stream;
    if (len > 0)
        memcpy(q->data + PLUGIN_HEADER, payload, len);
    q->len = PLUGIN_HEADER + len;
    q->samples = samples;
    q->next = NULL;

    pthread_mutex_lock(&p->qlock);
    if (p->qtail != NULL)
        p->qtail->next = q;
    else
        p->qhead = q;
    p->qtail = q;
    p->qbytes += q->len;
    pthread_cond_signal(&p->qcond);
    pthread_mutex_unlock(&p->qlock);
} // end queue_frame


static void queue_text(PLUGIN *p, uint8_t type, int8_t stream, const char *text)
{
    queue_frame(p, type, stream, text, strlen(text), false);
} // end queue_text


static void *writer_thread(void *arg)
{
    PLUGIN *p = (PLUGIN*)arg;

    for (;;)
    {
        QITEM *q;

        pthread_mutex_lock(&p->qlock);
        while (p->qhead == NULL && !p->closing)
            pthread_cond_wait(&p->qcond, &p->qlock);
        if (p->closing)
        {
            pthread_mutex_unlock(&p->qlock);
            break;
        }
        q = p->qhead;
        p->qhead = q->next;
        if (p->qhead == NULL)
            p->qtail = NULL;
        p->qbytes -= q->len;
        pthread_mutex_unlock(&p->qlock);

        size_t off = 0, len = q->len;
        while (off < len)
        {
            ssize_t w = send(p->fd, q->data + off, len - off, MSG_NOSIGNAL);
            if (w <= 0)
            {
                if (w < 0 && sock_error() == SOCK_EINTR)
                    continue;
                break;
            }
            off += (size_t)w;
        }
        free(q);
        if (off < len)
        {
            shutdown(p->fd, SHUT_RDWR);      // the reader thread cleans up
            break;
        }
    }
    return NULL;
} // end writer_thread


/* -------------------------------------------------------------- clients */

static size_t build_list(unsigned char **out)
{
    size_t cap = 256, len = 0;
    char  *s;
    int    count = 0;

    pthread_mutex_lock(&plugins_lock);
    for (int k = 0; k < PLUGIN_MAX; k++)
    {
        if (plugins[k].used && plugins[k].xml != NULL)
            cap += strlen(plugins[k].xml) + 64;
    }
    s = (char*)malloc(4 + cap);
    len = (size_t)snprintf(s + 4, cap, "<plugins>\n");
    for (int k = 0; k < PLUGIN_MAX; k++)
    {
        PLUGIN *p = &plugins[k];
        if (!p->used || p->xml == NULL)
            continue;
        len += (size_t)snprintf(s + 4 + len, cap - len, "<entry id=\"%d\" first_mode=\"%d\">\n%s\n</entry>\n",
                                p->id, PLUGIN_FIRST_MODE + p->id * PLUGIN_MAX_MODES, p->xml);
        count++;
    }
    len += (size_t)snprintf(s + 4 + len, cap - len, "</plugins>");
    pthread_mutex_unlock(&plugins_lock);
    len++;                                    // the terminating 0
    if (len > 0xFFFF)
    {
        fprintf(stderr, "plugin: plugin list too long for a client message\n");
        len = 0xFFFF;
        s[4 + len - 1] = '\0';
    }
    s[0] = (char)((len >> 8) & 0xFF);
    s[1] = (char)(len & 0xFF);
    s[2] = CLIENT_PLUGIN_LIST;
    s[3] = (char)count;
    *out = (unsigned char*)s;
    return 4 + len;
} // end build_list


void plugin_send_list(struct bufferevent *bev)
{
    unsigned char *msg;
    size_t len = build_list(&msg);

    bufferevent_write(bev, msg, len);
    free(msg);
} // end plugin_send_list


static void broadcast_list(void)
{
    unsigned char *msg;
    size_t len = build_list(&msg);

    server_broadcast_plugin(msg, len);
    free(msg);
} // end broadcast_list


static void broadcast_status(PLUGIN *p, int8_t stream, const unsigned char *text, size_t n)
{
    size_t len;
    unsigned char *msg;

    if (n > 0xFFFF - 32)
        n = 0xFFFF - 32;
    msg = (unsigned char*)malloc(4 + 32 + n + 1);
    len = (size_t)snprintf((char*)msg + 4, 32, "stream=%d\n", stream);
    memcpy(msg + 4 + len, text, n);
    len += n;
    msg[4 + len] = '\0';
    len++;
    msg[0] = (unsigned char)((len >> 8) & 0xFF);
    msg[1] = (unsigned char)(len & 0xFF);
    msg[2] = CLIENT_PLUGIN_STATUS;
    msg[3] = (unsigned char)p->id;
    server_broadcast_plugin(msg, 4 + len);
    free(msg);
} // end broadcast_status


void plugin_client_setting(int8_t stream, int plugin_id, const char *text)
{
    pthread_mutex_lock(&plugins_lock);
    if (plugin_id >= 0 && plugin_id < PLUGIN_MAX && plugins[plugin_id].used)
        queue_text(&plugins[plugin_id], PLG_SETTING, stream, text);
    pthread_mutex_unlock(&plugins_lock);
} // end plugin_client_setting


/* ---------------------------------------------------- stream processing */

static void stream_reset(PSTREAM *s)
{
    if (s->rx_src) s->rx_src = src_delete(s->rx_src);
    if (s->audio_src) s->audio_src = src_delete(s->audio_src);
    if (s->mic_src) s->mic_src = src_delete(s->mic_src);
    if (s->txiq_src) s->txiq_src = src_delete(s->txiq_src);
    fifo_free(&s->audio);
    fifo_free(&s->txiq);
    s->rx_in_rate = 0;
    s->audio_out_rate = 0;
    s->lo_phase = 0.0;
    s->keyed = s->tail = s->txdone = false;
} // end stream_reset


/* resample with src_process; returns frames written */
static long resample(SRC_STATE *st, const float *in, long n_in, float *out, long n_out_max, double ratio)
{
    SRC_DATA d;

    memset(&d, 0, sizeof(d));
    d.data_in = in;
    d.input_frames = n_in;
    d.data_out = out;
    d.output_frames = n_out_max;
    d.src_ratio = ratio;
    if (src_process(st, &d) != 0)
        return 0;
    return d.output_frames_gen;
} // end resample


bool plugin_is_mode(int mode)
{
    return mode >= PLUGIN_FIRST_MODE && mode < PLUGIN_FIRST_MODE + PLUGIN_MAX * PLUGIN_MAX_MODES;
} // end plugin_is_mode


/* Connect a stream to (plugin, mode index); call with s->lock held. */
static void attach(PSTREAM *s, int8_t stream, PLUGIN *p, int idx)
{
    s->plugin = p;
    s->mode = idx;
    if (p != NULL)
    {
        char text[48];
        snprintf(text, sizeof(text), "%s %s", s->tx ? "tx" : "rx", p->mode[idx].name);
        queue_text(p, PLG_START, stream, text);
        fprintf(stderr, "plugin: stream %d -> %s mode %s\n", stream, p->name, p->mode[idx].name);
    }
} // end attach


bool plugin_set_mode(int8_t stream, int mode, bool tx, int *filter_low, int *filter_high)
{
    PSTREAM *s;
    PLUGIN *old, *p = NULL;
    int idx = -1;

    if (stream < 0 || stream >= MAX_RFSTREAMS)
        return false;
    s = &pstream[stream];
    *filter_low = *filter_high = 0;

    pthread_mutex_lock(&plugins_lock);
    if (plugin_is_mode(mode))
    {
        int id = (mode - PLUGIN_FIRST_MODE) / PLUGIN_MAX_MODES;
        idx = (mode - PLUGIN_FIRST_MODE) % PLUGIN_MAX_MODES;
        if (plugins[id].used && idx < plugins[id].nmodes)
        {
            p = &plugins[id];
            *filter_low = p->mode[idx].filter_low;
            *filter_high = p->mode[idx].filter_high;
        }
        else
            fprintf(stderr, "plugin: mode %d: no plugin has it (not started?)\n", mode);
    }

    pthread_mutex_lock(&s->lock);
    old = s->plugin;
    if (old != NULL && (old != p || s->mode != idx))
        queue_frame(old, PLG_STOP, stream, NULL, 0, false);
    if (old != p || s->mode != idx || s->in_mode != plugin_is_mode(mode))
    {
        stream_reset(s);
        s->tx = tx;
        attach(s, stream, p, idx);
    }
    s->in_mode = plugin_is_mode(mode);
    s->mode_number = mode;
    pthread_mutex_unlock(&s->lock);
    pthread_mutex_unlock(&plugins_lock);
    return plugin_is_mode(mode);
} // end plugin_set_mode


bool plugin_rx_active(int8_t stream)
{
    return stream >= 0 && stream < MAX_RFSTREAMS && pstream[stream].in_mode && !pstream[stream].tx;
} // end plugin_rx_active


bool plugin_tx_active(int8_t stream)
{
    return stream >= 0 && stream < MAX_RFSTREAMS && pstream[stream].in_mode && pstream[stream].tx;
} // end plugin_tx_active


void plugin_set_rx_gain(int8_t stream, double gain)
{
    if (stream >= 0 && stream < MAX_RFSTREAMS)
        pstream[stream].rx_gain = gain;
} // end plugin_set_rx_gain


void plugin_set_mic_gain(int8_t stream, double gain)
{
    if (stream >= 0 && stream < MAX_RFSTREAMS)
        pstream[stream].mic_gain = gain;
} // end plugin_set_mic_gain


void plugin_rx_process(int8_t stream, const double *iq_qi, int n, int rate, double *out_lr)
{
    PSTREAM *s = &pstream[stream];
    float *iq, *rs, *audio;
    long nrs = 0;

    pthread_mutex_lock(&s->lock);
    if (s->plugin == NULL || s->tx || rate <= 0)
    {
        // plugin mode but no plugin connected: no audio
        pthread_mutex_unlock(&s->lock);
        memset(out_lr, 0, sizeof(double) * 2 * n);
        return;
    }
    PLUGIN_MODE *m = &s->plugin->mode[s->mode];

    if (s->rx_in_rate != rate)
    {
        int err;
        if (s->rx_src)
            s->rx_src = src_delete(s->rx_src);
        s->rx_src = src_new(SRC_SINC_FASTEST, 2, &err);
        s->rx_in_rate = rate;
        fifo_init(&s->audio, (long)rate * FIFO_SECONDS);
        if (s->audio_src)
            s->audio_src = src_delete(s->audio_src);
        s->audio_out_rate = rate;
    }

    iq = (float*)malloc(sizeof(float) * 2 * n);
    rs = (float*)malloc(sizeof(float) * 2 * ((long)n * m->rx_rate / rate + 64));
    audio = (float*)malloc(sizeof(float) * n);

    // WDSP order Q,I -> I,Q, and move the dial frequency to 0 Hz
    double w = -2.0 * M_PI * LO_offset / rate;
    for (int k = 0; k < n; k++)
    {
        double q = iq_qi[2 * k];
        double i = iq_qi[2 * k + 1];
        if (LO_offset != 0.0)
        {
            double c = cos(s->lo_phase), sn = sin(s->lo_phase);
            double ii = i * c - q * sn;
            q = i * sn + q * c;
            i = ii;
            s->lo_phase = fmod(s->lo_phase + w, 2.0 * M_PI);
        }
        iq[2 * k] = (float)i;
        iq[2 * k + 1] = (float)q;
    }
    if (rate == m->rx_rate)
    {
        memcpy(rs, iq, sizeof(float) * 2 * n);
        nrs = n;
    }
    else if (s->rx_src != NULL)
        nrs = resample(s->rx_src, iq, n, rs, (long)n * m->rx_rate / rate + 64, (double)m->rx_rate / rate);
    if (nrs > 0)
        queue_frame(s->plugin, PLG_RXIQ, stream, rs, sizeof(float) * 2 * nrs, true);

    // plugin audio for this block
    fifo_get(&s->audio, audio, n, (long)rate * PRIME_MS / 1000, (long)rate * 3 / 4);
    for (int k = 0; k < n; k++)
    {
        double a = audio[k] * s->rx_gain * 2.0;
        if (a > 1.0) a = 1.0;
        if (a < -1.0) a = -1.0;
        out_lr[2 * k] = out_lr[2 * k + 1] = a;
    }
    pthread_mutex_unlock(&s->lock);
    free(iq);
    free(rs);
    free(audio);
} // end plugin_rx_process


/* PLG_AUDIO from the plugin (reader thread) */
static void on_audio(PLUGIN *p, int8_t stream, const float *a, long n)
{
    PSTREAM *s;

    if (stream < 0 || stream >= MAX_RFSTREAMS)
        return;
    s = &pstream[stream];
    pthread_mutex_lock(&s->lock);
    if (s->plugin == p && !s->tx && s->audio_out_rate > 0)
    {
        PLUGIN_MODE *m = &p->mode[s->mode];
        double ratio = (double)s->audio_out_rate / m->audio_rate;
        long cap = (long)(n * ratio) + 64;
        float *out = (float*)malloc(sizeof(float) * cap);
        long nout;

        if (s->audio_src == NULL)
        {
            int err;
            s->audio_src = src_new(SRC_SINC_FASTEST, 1, &err);
        }
        nout = s->audio_src != NULL ? resample(s->audio_src, a, n, out, cap, ratio) : 0;
        fifo_put(&s->audio, out, nout);
        free(out);
    }
    pthread_mutex_unlock(&s->lock);
} // end on_audio


static void tx_output(PSTREAM *s, double *tx_qi, int nout)
{
    float *iq = (float*)malloc(sizeof(float) * 2 * nout);

    fifo_get(&s->txiq, iq, 2L * nout, 2L * TX_RATE * PRIME_MS / 1000, 2L * TX_RATE);
    for (int k = 0; k < nout; k++)
    {
        tx_qi[2 * k] = iq[2 * k + 1];         // WDSP order Q,I
        tx_qi[2 * k + 1] = iq[2 * k];
    }
    free(iq);
} // end tx_output


static void send_mic(PSTREAM *s, int8_t stream, const float *mono, int n)
{
    PLUGIN_MODE *m = &s->plugin->mode[s->mode];
    double ratio = (double)m->mic_rate / MIC_RATE;
    long cap = (long)(n * ratio) + 64;
    float *out = (float*)malloc(sizeof(float) * cap);
    long nout;

    if (m->mic_rate == MIC_RATE)
    {
        memcpy(out, mono, sizeof(float) * n);
        nout = n;
    }
    else
    {
        if (s->mic_src == NULL)
        {
            int err;
            s->mic_src = src_new(SRC_SINC_FASTEST, 1, &err);
        }
        nout = s->mic_src != NULL ? resample(s->mic_src, mono, n, out, cap, ratio) : 0;
    }
    if (nout > 0)
        queue_frame(s->plugin, PLG_MIC, stream, out, sizeof(float) * nout, true);
    free(out);
} // end send_mic


void plugin_tx_process(int8_t stream, const double *mic_lr, int n, double *tx_qi, int nout)
{
    PSTREAM *s = &pstream[stream];
    float *mono;

    pthread_mutex_lock(&s->lock);
    if (s->plugin == NULL || !s->tx)
    {
        pthread_mutex_unlock(&s->lock);
        memset(tx_qi, 0, sizeof(double) * 2 * nout);
        return;
    }
    mono = (float*)malloc(sizeof(float) * n);
    for (int k = 0; k < n; k++)
    {
        double v = s->tail ? 0.0 : mic_lr[2 * k] * s->mic_gain;
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        mono[k] = (float)v;
    }
    send_mic(s, stream, mono, n);
    tx_output(s, tx_qi, nout);
    clock_gettime(CLOCK_MONOTONIC, &s->last_tick);
    pthread_mutex_unlock(&s->lock);
    free(mono);
} // end plugin_tx_process


/* PLG_TXIQ from the plugin (reader thread) */
static void on_txiq(PLUGIN *p, int8_t stream, const float *iq, long n)
{
    PSTREAM *s;

    if (stream < 0 || stream >= MAX_RFSTREAMS)
        return;
    s = &pstream[stream];
    pthread_mutex_lock(&s->lock);
    if (s->plugin == p && s->tx && (s->keyed || s->tail))
    {
        PLUGIN_MODE *m = &p->mode[s->mode];
        double ratio = (double)TX_RATE / m->tx_rate;
        long cap = (long)(n * ratio) + 64;
        float *out = (float*)malloc(sizeof(float) * 2 * cap);
        long nout;

        if (s->txiq_src == NULL)
        {
            int err;
            s->txiq_src = src_new(SRC_SINC_FASTEST, 2, &err);
        }
        nout = s->txiq_src != NULL ? resample(s->txiq_src, iq, n, out, cap, ratio) : 0;
        fifo_put(&s->txiq, out, 2 * nout);
        free(out);
    }
    pthread_mutex_unlock(&s->lock);
} // end on_txiq


bool plugin_mox(int8_t stream, bool on)
{
    PSTREAM *s;
    bool deferred = false;

    if (!plugin_tx_active(stream))
        return false;
    s = &pstream[stream];
    pthread_mutex_lock(&s->lock);
    if (s->plugin != NULL)
    {
        uint8_t b = on ? 1 : 0;
        if (on)
        {
            if (!s->tail)
            {
                fifo_init(&s->txiq, 2L * TX_RATE * FIFO_SECONDS);
                if (s->mic_src) src_reset(s->mic_src);
                if (s->txiq_src) src_reset(s->txiq_src);
            }
            s->keyed = true;
            s->tail = false;
            s->txdone = false;
        }
        else if (s->keyed)
        {
            s->keyed = false;
            s->tail = true;
            s->txdone = false;
            clock_gettime(CLOCK_MONOTONIC, &s->tail_start);
            clock_gettime(CLOCK_MONOTONIC, &s->last_tick);
            deferred = true;
        }
        queue_frame(s->plugin, PLG_MOX, stream, &b, 1, false);
    }
    pthread_mutex_unlock(&s->lock);
    return deferred;
} // end plugin_mox


bool plugin_tail_pending(int8_t stream)
{
    return stream >= 0 && stream < MAX_RFSTREAMS && pstream[stream].tail;
} // end plugin_tail_pending


/* No mic audio is coming (PTT released): make a silent block every block
 * time so the plugin can finish.  Returns true when tx_qi was filled. */
bool plugin_tail_tick(int8_t stream, int n, double *tx_qi, int nout)
{
    PSTREAM *s = &pstream[stream];
    double block = (double)n / MIC_RATE;
    double *silence;

    if (!s->tail || elapsed(&s->last_tick) < block)
        return false;
    silence = (double*)calloc(2 * n, sizeof(double));
    plugin_tx_process(stream, silence, n, tx_qi, nout);
    free(silence);
    return true;
} // end plugin_tail_tick


bool plugin_tail_done(int8_t stream)
{
    PSTREAM *s = &pstream[stream];
    bool done = false;

    pthread_mutex_lock(&s->lock);
    if (s->tail)
    {
        int tail_ms = s->plugin != NULL ? s->plugin->mode[s->mode].tx_tail_ms : 0;
        long left = s->txiq.head - s->txiq.tail;
        if ((s->txdone && (left <= 0 || !s->txiq.primed)) || s->plugin == NULL ||
            elapsed(&s->tail_start) * 1000.0 > tail_ms + PRIME_MS + 100)
        {
            s->tail = false;
            done = true;
            if (s->txiq.underruns || s->txiq.trims)
                fprintf(stderr, "plugin: stream %d transmit I/Q underruns %ld trims %ld\n", stream, s->txiq.underruns, s->txiq.trims);
        }
    }
    pthread_mutex_unlock(&s->lock);
    return done;
} // end plugin_tail_done


/* --------------------------------------------------------------- plugins */

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


static bool on_register(PLUGIN *p, const char *xml)
{
    const char *t = strstr(xml, "<plugin");
    char reply[512];
    size_t len;

    if (t == NULL)
        return false;
    size_t tl = strcspn(t, ">");
    char *tag = plugin_strndup(t, tl);
    if (!xml_attr(tag, "name", p->name, sizeof(p->name)))
        snprintf(p->name, sizeof(p->name), "plugin%d", p->id);
    free(tag);

    p->nmodes = 0;
    for (t = strstr(xml, "<mode"); t != NULL && p->nmodes < PLUGIN_MAX_MODES; t = strstr(t + 5, "<mode"))
    {
        PLUGIN_MODE *m = &p->mode[p->nmodes];
        tag = plugin_strndup(t, strcspn(t, ">"));
        if (!xml_attr(tag, "name", m->name, sizeof(m->name)))
        {
            free(tag);
            continue;
        }
        m->number = PLUGIN_FIRST_MODE + p->id * PLUGIN_MAX_MODES + p->nmodes;
        m->rx_rate = xml_int(tag, "rx_rate", 48000);
        m->audio_rate = xml_int(tag, "audio_rate", 8000);
        m->tx_rate = xml_int(tag, "tx_rate", 48000);
        m->mic_rate = xml_int(tag, "mic_rate", 8000);
        m->filter_low = xml_int(tag, "filter_low", -5000);
        m->filter_high = xml_int(tag, "filter_high", 5000);
        m->tx_tail_ms = xml_int(tag, "tx_tail_ms", 300);
        free(tag);
        if (m->rx_rate < 8000 || m->rx_rate > 384000 || m->audio_rate < 8000 || m->audio_rate > 48000 ||
            m->tx_rate < 8000 || m->tx_rate > 192000 || m->mic_rate < 8000 || m->mic_rate > 48000)
        {
            fprintf(stderr, "plugin %s: mode %s has unsupported rates\n", p->name, m->name);
            continue;
        }
        p->nmodes++;
    }
    if (p->nmodes == 0)
        return false;

    p->xml = strdup(xml);
    len = (size_t)snprintf(reply, sizeof(reply), "id=%d\n", p->id);
    for (int k = 0; k < p->nmodes; k++)
        len += (size_t)snprintf(reply + len, sizeof(reply) - len, "mode=%s:%d\n", p->mode[k].name, p->mode[k].number);
    queue_text(p, PLG_ACCEPT, -1, reply);
    fprintf(stderr, "plugin: %s registered (id %d, %d mode%s)\n", p->name, p->id, p->nmodes, p->nmodes == 1 ? "" : "s");

    // streams left in one of these modes (the plugin was restarted) carry on
    for (int k = 0; k < MAX_RFSTREAMS; k++)
    {
        PSTREAM *s = &pstream[k];
        pthread_mutex_lock(&s->lock);
        int idx = s->mode_number - (PLUGIN_FIRST_MODE + p->id * PLUGIN_MAX_MODES);
        if (s->in_mode && s->plugin == NULL && idx >= 0 && idx < p->nmodes)
        {
            stream_reset(s);
            attach(s, (int8_t)k, p, idx);
        }
        pthread_mutex_unlock(&s->lock);
    }
    return true;
} // end on_register


static void plugin_close(PLUGIN *p)
{
    // streams in its modes go quiet (and finish a pending tail)
    for (int k = 0; k < MAX_RFSTREAMS; k++)
    {
        pthread_mutex_lock(&pstream[k].lock);
        if (pstream[k].plugin == p)
        {
            bool tail = pstream[k].tail;
            pstream[k].plugin = NULL;
            stream_reset(&pstream[k]);
            pstream[k].tail = tail;          // plugin_tail_done() then unkeys at once
        }
        pthread_mutex_unlock(&pstream[k].lock);
    }

    pthread_mutex_lock(&p->qlock);
    p->closing = true;
    pthread_cond_signal(&p->qcond);
    pthread_mutex_unlock(&p->qlock);
    pthread_join(p->writer, NULL);

    pthread_mutex_lock(&plugins_lock);
    while (p->qhead != NULL)
    {
        QITEM *q = p->qhead;
        p->qhead = q->next;
        free(q);
    }
    p->qtail = NULL;
    p->qbytes = 0;
    sock_close(p->fd);
    fprintf(stderr, "plugin: %s disconnected\n", p->name);
    free(p->xml);
    p->xml = NULL;
    p->used = false;
    pthread_mutex_unlock(&plugins_lock);
    broadcast_list();
} // end plugin_close


static void *reader_thread(void *arg)
{
    PLUGIN *p = (PLUGIN*)arg;
    unsigned char hdr[PLUGIN_HEADER];
    bool registered = false;

    pthread_detach(pthread_self());
    while (read_all(p->fd, hdr, sizeof(hdr)))
    {
        uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
        uint8_t type = hdr[4];
        int8_t stream = (int8_t)hdr[5];
        size_t n;
        unsigned char *payload;

        if (len < 2 || len > PLUGIN_MAX_FRAME)
        {
            fprintf(stderr, "plugin %s: bad frame length %u\n", p->name, len);
            break;
        }
        n = len - 2;
        payload = (unsigned char*)malloc(n + 1);
        if (payload == NULL || !read_all(p->fd, payload, n))
        {
            free(payload);
            break;
        }
        payload[n] = '\0';

        if (!registered)
        {
            if (type != PLG_REGISTER || !on_register(p, (const char*)payload))
            {
                fprintf(stderr, "plugin: registration refused\n");
                free(payload);
                break;
            }
            registered = true;
            broadcast_list();
            free(payload);
            continue;
        }

        switch (type)
        {
        case PLG_AUDIO:
            on_audio(p, stream, (const float*)payload, (long)(n / sizeof(float)));
            break;
        case PLG_TXIQ:
            on_txiq(p, stream, (const float*)payload, (long)(n / (2 * sizeof(float))));
            break;
        case PLG_STATUS:
            broadcast_status(p, stream, payload, n);
            break;
        case PLG_TXDONE:
            if (stream >= 0 && stream < MAX_RFSTREAMS)
            {
                pthread_mutex_lock(&pstream[stream].lock);
                pstream[stream].txdone = true;
                pthread_mutex_unlock(&pstream[stream].lock);
            }
            break;
        default:
            fprintf(stderr, "plugin %s: unknown message %d\n", p->name, type);
            break;
        }
        free(payload);
    }
    plugin_close(p);
    return NULL;
} // end reader_thread


static void *listen_thread(void *arg)
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    struct sockaddr_in addr;

    (void)arg;
    sock_set_reuseaddr(ls);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PLUGIN_PORT);
    if (bind(ls, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(ls, 4) < 0)
    {
        sock_perror("plugin: cannot listen on the plugin port");
        sock_close(ls);
        return NULL;
    }
    fprintf(stderr, "plugin: listening on port %d\n", PLUGIN_PORT);

    for (;;)
    {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int fd = accept(ls, (struct sockaddr*)&from, &fl);
        PLUGIN *p = NULL;

        if (fd < 0)
        {
            if (sock_error() == SOCK_EINTR)
                continue;
            usleep(100000);
            continue;
        }
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

        pthread_mutex_lock(&plugins_lock);
        for (int k = 0; k < PLUGIN_MAX; k++)
        {
            if (!plugins[k].used)
            {
                p = &plugins[k];
                memset(p->name, 0, sizeof(p->name));
                p->used = true;
                p->id = k;
                p->fd = fd;
                p->xml = NULL;
                p->nmodes = 0;
                p->qhead = p->qtail = NULL;
                p->qbytes = 0;
                p->closing = false;
                break;
            }
        }
        pthread_mutex_unlock(&plugins_lock);
        if (p == NULL)
        {
            fprintf(stderr, "plugin: too many plugins, connection from %s refused\n", inet_ntoa(from.sin_addr));
            sock_close(fd);
            continue;
        }
        fprintf(stderr, "plugin: connection from %s\n", inet_ntoa(from.sin_addr));
        pthread_create(&p->writer, NULL, writer_thread, p);
        pthread_create(&p->reader, NULL, reader_thread, p);
    }
    return NULL;
} // end listen_thread


void plugin_init(void)
{
    pthread_t t;

    for (int k = 0; k < PLUGIN_MAX; k++)
    {
        pthread_mutex_init(&plugins[k].qlock, NULL);
        pthread_cond_init(&plugins[k].qcond, NULL);
    }
    for (int k = 0; k < MAX_RFSTREAMS; k++)
    {
        pthread_mutex_init(&pstream[k].lock, NULL);
        pstream[k].rx_gain = 0.5;
        pstream[k].mic_gain = 1.0;
    }
    pthread_create(&t, NULL, listen_thread, NULL);
    pthread_detach(t);
} // end plugin_init
