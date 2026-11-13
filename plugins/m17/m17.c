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
 * m17_plugin - M17 digital voice for QtRadioNG, as a dspserver protocol
 * plugin.  libm17 (M17 Project) does the framing and FEC, Codec 2 the voice.
 *
 * Receive (48 kHz I/Q):
 *   FM discriminator -> RRC matched filter (alpha 0.5, 10 samples/symbol)
 *   -> syncword search on every sample phase (normalised correlation with
 *   the LSF/stream/packet/EOT patterns, which also gives the symbol scale
 *   and DC, so deviation and frequency errors do not matter) -> 184 payload
 *   symbols -> libm17 decode -> Codec 2 -> 8 kHz audio.  Each frame's
 *   syncword re-times the next frame.  The link setup comes from the LSF
 *   frame or, for late joiners, from the LICH chunks of six stream frames.
 *
 * Transmit (8 kHz mic -> 48 kHz I/Q):
 *   preamble + LSF, then a stream frame per 40 ms of Codec 2 3200 voice;
 *   on unkey a last frame (end of stream flag) and the EOT marker.  Symbols
 *   -> RRC -> FM, +-3 = +-2.4 kHz.
 *
 *   m17_plugin [-s dspserver_host] [-p port] [-v]
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <codec2/codec2.h>
#include <m17.h>

#include "plugin_client.h"

#define RATE          48000
#define AUDIO_RATE    8000
#define SPS           10                      // samples per symbol at 48 kHz
#define RRC_TAPS      (8 * SPS + 1)
#define DEV_PER_UNIT  800.0                   // Hz per symbol unit (+3 = 2.4 kHz)
#define MAX_STREAMS   64
#define RING          4096                    // filtered samples kept (> 2 frames' sync span)
#define SYNC_SPAN     ((SYM_PER_SWD - 1) * SPS)
#define FRAME_SAMPLES (SYM_PER_FRA * SPS)
#define ACQUIRE       0.92                    // normalised correlation to find a syncword
#define TRACK         0.70                    // ... where the next one is expected
#define GAIN_MIN      0.5                     // plausible symbol scale (deviation 50 .. 200 %)
#define GAIN_MAX      2.0
#define BER_START     8.0                     // % bit errors: a frame this good starts a transmission
#define BER_MUTE      15.0                    // ... worse than this is not played
#define VOICE_FIFO    (AUDIO_RATE * 2)
// voice AGC: Codec 2 gives back the sender's level, often far below what
// WDSP's FM gives; bring speech peaks up to about AGC_TARGET
#define AGC_TARGET    0.6                     // peak level, full scale 1
#define AGC_MAX       31.6                    // most gain: 30 dB
#define AGC_START     10.0                    // gain before the first block: 20 dB
#define AGC_FLOOR     0.01                    // below this the gain is held (pauses, silence)
#define AGC_RAMP      40                      // samples to lower the gain (5 ms)
#define AGC_HANG      (AUDIO_RATE / 2)        // samples the envelope holds after a peak
#define AGC_RELEASE   0.99992                 // then falls per sample (about 1.5 s)
#define AGC_KNEE      0.8                     // soft limiter above this

// META text (spec v2 "Text Data", no encryption, encryption subtype 00):
// META[0] is a control byte, high nibble a bit map of the blocks in the
// message (1, 3, 7 or 15), low nibble the bit of this block; META[1..13]
// are 13 bytes of UTF-8, the last used block padded with spaces
#define TEXT_BLOCK    13
#define TEXT_BLOCKS   4
#define TEXT_MAX      (TEXT_BLOCK * TEXT_BLOCKS)

enum { P_LSF, P_STR, P_PKT, P_EOT, P_COUNT };

typedef struct
{
    // front end
    double last_i, last_q;
    float  rrc_hist[RRC_TAPS * 2];
    int    rrc_pos;
    float  ring[RING];
    long   n;                    // filtered samples so far
    // framing
    bool   searching;
    long   cand_at;              // search: best candidate so far (sample index), -1 none
    double cand_rho;
    int    cand_kind;
    double cand_g, cand_c;
    long   frame_sync;           // last syncword's last symbol (sample index)
    int    frame_kind;
    double g, c;                 // symbol = (sample - c) / g
    bool   in_frame;
    int    missed;
    // link
    bool   active;               // a transmission is being received
    bool   have_lsf;
    lsf_t  lsf;
    uint8_t lich[30];
    uint8_t lich_got;            // bit per received LICH chunk
    uint16_t type;
    time_t started;
    double ber_sum;
    int    ber_n;
    time_t last_status;
    // voice
    struct CODEC2 *c2_3200, *c2_1600;
    float  voice[VOICE_FIFO];
    long   v_head, v_tail;
    long   out_acc;              // input samples not yet matched by audio
    // META text
    uint8_t text[TEXT_BLOCKS][TEXT_BLOCK];
    uint8_t text_have;           // blocks received (low nibble bits)
    uint8_t text_total;          // blocks in the message (bit map), 0 none yet
    bool   text_shown;
    uint8_t text_from[6];        // SRC of the text on the panel
    double agc_env;              // speech peak envelope
    double agc_gain;
    int    agc_hang;
} RX;

typedef struct
{
    bool   keyed;
    struct CODEC2 *c2;
    float  mic[320];
    int    nmic;
    lsf_t  lsf;
    uint16_t fn;
    uint8_t lich_cnt;
    uint8_t meta[TEXT_BLOCKS][14];   // META text blocks, sent in turn
    int    nblk;
    int    blk;
    float  hist[RRC_TAPS * 2];
    int    pos;
    double phase;
} TX;

typedef struct
{
    bool  active;
    bool  tx;
    RX    rx;
    TX    t;
} STREAM;

static const char *XML =
    "<plugin name=\"m17\" version=\"1\" title=\"M17 digital voice\">\n"
    "  <mode name=\"M17\" rx_rate=\"48000\" audio_rate=\"8000\" tx_rate=\"48000\" mic_rate=\"8000\"\n"
    "        filter_low=\"-4500\" filter_high=\"4500\" tx_tail_ms=\"500\"/>\n"
    "  <panel>\n"
    "    <text   key=\"src\"   label=\"My callsign\" max=\"9\" persist=\"1\"/>\n"
    "    <text   key=\"dst\"   label=\"Destination\" max=\"9\" default=\"@ALL\" persist=\"1\"/>\n"
    "    <spin   key=\"can\"   label=\"Channel access (CAN)\" min=\"0\" max=\"15\" default=\"0\" persist=\"1\"/>\n"
    "    <check  key=\"canrx\" label=\"Receive only my CAN\" default=\"0\" persist=\"1\"/>\n"
    "    <text   key=\"txtext\" label=\"My text (sent with voice)\" max=\"52\" persist=\"1\"/>\n"
    "    <check  key=\"agc\"   label=\"Audio AGC\" default=\"1\" persist=\"1\"/>\n"
    "    <spin   key=\"gain\"  label=\"Audio gain, AGC off (dB)\" min=\"0\" max=\"30\" default=\"12\" persist=\"1\"/>\n"
    "    <status key=\"heard\" label=\"Heard\"/>\n"
    "    <status key=\"info\"  label=\"Stream\"/>\n"
    "    <status key=\"text\"  label=\"Text\"/>\n"
    "    <status key=\"signal\" label=\"Signal\"/>\n"
    "    <meter  key=\"ber\"   label=\"Bit errors\" min=\"0\" max=\"10\" unit=\"%\"/>\n"
    "    <button key=\"clear\" label=\"Clear log\"/>\n"
    "    <log    key=\"log\"   label=\"Log\" lines=\"6\"/>\n"
    "  </panel>\n"
    "</plugin>\n";

static PLUGIN_CONN *conn;
static STREAM       st[MAX_STREAMS];
static float        rrc[RRC_TAPS];            // normalised to unity DC gain
static float        rrc_tx[RRC_TAPS];         // impulse every SPS samples -> unity DC
static int8_t       pattern[P_COUNT][SYM_PER_SWD];
static char         my_call[16] = "N0CALL";
static char         dst_call[16] = "@ALL";
static int          my_can = 0;
static bool         can_filter = false;
static char         tx_text[TEXT_MAX + 1] = "";
static bool         agc_on = true;
static double       fixed_gain = 4.0;         // AGC off: 12 dB
static bool         verbose = false;
static volatile bool stop = false;


/* ------------------------------------------------------------- helpers */

static void upper(char *dst, size_t size, const char *src)
{
    size_t k = 0;
    for (; src[k] != '\0' && k + 1 < size; k++)
        dst[k] = (char)toupper((unsigned char)src[k]);
    dst[k] = '\0';
} // end upper


static const char *now_hms(void)
{
    static char buf[16];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
} // end now_hms


static float fir(const float *taps, float *hist, int *pos, float x)
{
    double acc = 0.0;
    hist[*pos] = hist[*pos + RRC_TAPS] = x;
    *pos = (*pos + 1) % RRC_TAPS;
    for (int k = 0; k < RRC_TAPS; k++)
        acc += taps[k] * hist[*pos + k];
    return (float)acc;
} // end fir


/* Least squares fit x = g p + c of 8 samples to a pattern; returns the
 * normalised correlation (1 = perfect). */
static double fit(const float x[SYM_PER_SWD], const int8_t p[SYM_PER_SWD], double *g, double *c)
{
    double mx = 0, mp = 0, sxp = 0, spp = 0, sxx = 0;
    for (int k = 0; k < SYM_PER_SWD; k++)
    {
        mx += x[k];
        mp += p[k];
    }
    mx /= SYM_PER_SWD;
    mp /= SYM_PER_SWD;
    for (int k = 0; k < SYM_PER_SWD; k++)
    {
        double dx = x[k] - mx, dp = p[k] - mp;
        sxp += dx * dp;
        spp += dp * dp;
        sxx += dx * dx;
    }
    if (spp <= 0.0 || sxx <= 1e-12)
        return 0.0;
    *g = sxp / spp;
    *c = mx - *g * mp;
    return sxp / sqrt(spp * sxx);
} // end fit


/* syncword symbols ending at sample index s */
static void sync_samples(const RX *r, long s, float x[SYM_PER_SWD])
{
    for (int k = 0; k < SYM_PER_SWD; k++)
        x[k] = r->ring[(s - (SYM_PER_SWD - 1 - k) * SPS) % RING];
} // end sync_samples


static int best_pattern(const RX *r, long s, double *rho, double *g, double *c)
{
    float x[SYM_PER_SWD];
    int best = -1;

    *rho = -1.0;
    sync_samples(r, s, x);
    for (int p = 0; p < P_COUNT; p++)
    {
        double gg = 0.0, cc = 0.0, q = fit(x, pattern[p], &gg, &cc);
        if (q > *rho && gg > GAIN_MIN && gg < GAIN_MAX)
        {
            *rho = q;
            *g = gg;
            *c = cc;
            best = p;
        }
    }
    return best;
} // end best_pattern


/* ------------------------------------------------------------- receive */

/* Soft limiter: unchanged below AGC_KNEE, never reaches full scale */
static float soft_limit(double y)
{
    double m = fabs(y);

    if (m > AGC_KNEE)
        y = copysign(AGC_KNEE + (1.0 - AGC_KNEE) * tanh((m - AGC_KNEE) / (1.0 - AGC_KNEE)), y);
    return (float)y;
} // end soft_limit


/* One decoded block (a 40 ms frame) into the voice FIFO, through the AGC
 * or the fixed gain.  The AGC sees the block's peak before it applies the
 * gain, so it does not overshoot: a louder block lowers the gain within
 * AGC_RAMP samples, a quieter one raises it gradually across the block. */
static void voice_put(RX *r, const short *pcm, int n)
{
    double peak = 0.0, g0, g1;

    for (int k = 0; k < n; k++)
        peak = fmax(peak, fabs(pcm[k] / 32768.0));
    if (r->agc_gain <= 0.0)                       // first use of this receiver
    {
        r->agc_gain = AGC_START;
        r->agc_env = AGC_TARGET / AGC_START;
    }
    g0 = r->agc_gain;
    if (peak > r->agc_env)
    {
        r->agc_env = peak;
        r->agc_hang = AGC_HANG;
    }
    else if (r->agc_hang > 0)
        r->agc_hang -= n;
    else
        r->agc_env *= pow(AGC_RELEASE, n);
    if (r->agc_env > AGC_FLOOR)                   // pauses and silence hold the gain
        r->agc_gain = fmin(AGC_MAX, AGC_TARGET / r->agc_env);
    g1 = r->agc_gain;

    for (int k = 0; k < n; k++)
    {
        double g;
        if (!agc_on)
            g = fixed_gain;
        else if (g1 < g0)
            g = k < AGC_RAMP ? g0 + (g1 - g0) * (k + 1) / AGC_RAMP : g1;
        else
            g = g0 + (g1 - g0) * (k + 1) / n;
        r->voice[r->v_head % VOICE_FIFO] = soft_limit(g * pcm[k] / 32768.0);
        r->v_head++;
    }
    if (r->v_head - r->v_tail > VOICE_FIFO)
        r->v_tail = r->v_head - VOICE_FIFO;
} // end voice_put


static void status_link(int stream, RX *r)
{
    char s[12], d[12];
    int can = (r->type >> 7) & 0xF;
    int dt = (r->type >> 1) & 3;
    int enc = (r->type >> 3) & 3;
    const char *what = dt == 2 ? "voice 3200" : dt == 3 ? "voice+data 1600" : dt == 1 ? "data" : "?";

    decode_callsign_bytes(d, r->lsf.dst);
    decode_callsign_bytes(s, r->lsf.src);
    plugin_status(conn, stream, "heard=%s \xe2\x86\x92 %s\ninfo=CAN %d, %s%s\nlog=%s  %s \xe2\x86\x92 %s  CAN %d%s\n",
                  s, d, can, what, enc ? ", encrypted (muted)" : "",
                  now_hms(), s, d, can, enc ? " encrypted" : "");
    if (verbose)
        fprintf(stderr, "m17: stream %d: %s -> %s CAN %d type %04X\n", stream, s, d, can, r->type);
} // end status_link


static void text_reset(RX *r)
{
    r->text_have = 0;
    r->text_total = 0;
    r->text_shown = false;
} // end text_reset


/* The received text so far: blocks in order, missing ones as "...",
 * trailing padding removed, control characters (which would break the
 * key=value status lines) as spaces.  Returns false if there is none. */
static bool text_string(const RX *r, char *out, size_t size)
{
    size_t n = 0;

    for (int b = 0; b < TEXT_BLOCKS && (r->text_total >> b) & 1; b++)
    {
        if (!((r->text_have >> b) & 1))
        {
            if (n + 4 < size)
            {
                memcpy(out + n, "...", 3);
                n += 3;
            }
            continue;
        }
        for (int k = 0; k < TEXT_BLOCK && n + 1 < size; k++)
        {
            uint8_t c = r->text[b][k];
            out[n++] = (char)(c < 0x20 || c == 0x7F ? ' ' : c);
        }
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
    return n > 0;
} // end text_string


static void text_show(int stream, RX *r, bool complete)
{
    char txt[TEXT_MAX + 4 * TEXT_BLOCKS + 1], s[12];

    if (!text_string(r, txt, sizeof(txt)))
        return;
    decode_callsign_bytes(s, r->lsf.src);
    memcpy(r->text_from, r->lsf.src, 6);
    plugin_status(conn, stream, "text=%s\nlog=%s  %s: %s%s\n", txt, now_hms(), s, txt,
                  complete ? "" : "  (incomplete)");
    if (verbose)
        fprintf(stderr, "m17: stream %d: text from %s: \"%s\"\n", stream, s, txt);
} // end text_show


/* META of an LSF (the link setup frame, or one put together from the LICH) */
static void got_meta(int stream, RX *r, const lsf_t *l)
{
    uint16_t type = ((uint16_t)l->type[0] << 8) | l->type[1];
    uint8_t ctl = l->meta[0], total = ctl >> 4, blk = ctl & 0xF;
    int b = 0;

    if (((type >> 3) & 3) != 0 || ((type >> 5) & 3) != 0 || ctl == 0)
        return;                                   // encrypted, other META, or no text
    if ((total != 1 && total != 3 && total != 7 && total != 15) || (blk & (blk - 1)) != 0 || !(blk & total))
        return;                                   // not a valid control byte
    while (!((blk >> b) & 1))
        b++;
    if (total != r->text_total)                   // a different message
        text_reset(r);
    if (((r->text_have >> b) & 1) && memcmp(r->text[b], l->meta + 1, TEXT_BLOCK) == 0)
        return;                                   // already have this block
    if ((r->text_have >> b) & 1)
        r->text_shown = false;                    // the text changed
    memcpy(r->text[b], l->meta + 1, TEXT_BLOCK);
    r->text_have |= blk;
    r->text_total = total;
    if (r->text_have == total && !r->text_shown)
    {
        text_show(stream, r, true);
        r->text_shown = true;
    }
} // end got_meta


static void end_of_transmission(int stream, RX *r, const char *why)
{
    if (!r->active)
        return;
    r->active = false;
    if (r->text_total != 0 && !r->text_shown)
        text_show(stream, r, false);
    text_reset(r);
    plugin_status(conn, stream, "signal=idle\nber=0\nlog=%s  end (%s, %ld s)\n", now_hms(), why,
                  (long)(time(NULL) - r->started));
    r->have_lsf = false;
    r->lich_got = 0;
    if (verbose)
        fprintf(stderr, "m17: stream %d: end of transmission (%s)\n", stream, why);
} // end end_of_transmission


static void got_lsf(int stream, RX *r, const lsf_t *l)
{
    uint16_t type = ((uint16_t)l->type[0] << 8) | l->type[1];
    // META can change every superframe (text blocks): the link is DST, SRC, TYPE
    bool same = r->have_lsf && memcmp(&r->lsf, l, offsetof(lsf_t, meta)) == 0;

    r->lsf = *l;
    r->type = type;
    r->have_lsf = true;
    if (!same)
    {
        status_link(stream, r);
        // the panel keeps a station's text until another station is heard
        if (memcmp(l->src, r->text_from, 6) != 0)
        {
            memset(r->text_from, 0, 6);
            plugin_status(conn, stream, "text=\n");
        }
    }
    got_meta(stream, r, l);
} // end got_lsf


static void begin(int stream, RX *r)
{
    if (r->active)
        return;
    r->active = true;
    r->started = time(NULL);
    // Codec 2 decoders keep state from frame to frame: start clean
    codec2_destroy(r->c2_3200);
    codec2_destroy(r->c2_1600);
    r->c2_3200 = codec2_create(CODEC2_MODE_3200);
    r->c2_1600 = codec2_create(CODEC2_MODE_1600);
    r->ber_sum = 0;
    r->ber_n = 0;
    // a little audio cushion: frames arrive every 40 ms in one piece
    r->v_head = r->v_tail;
    short silence[160] = {0};
    voice_put(r, silence, 160);
    text_reset(r);
    plugin_status(conn, stream, "signal=receiving\n");
} // end begin


static void restart_search(RX *r)
{
    r->searching = true;
    r->cand_at = -1;
} // end restart_search


static void decode_frame(int stream, RX *r)
{
    float pld[SYM_PER_PLD];

    for (int k = 0; k < SYM_PER_PLD; k++)
        pld[k] = (float)((r->ring[(r->frame_sync + (k + 1) * SPS) % RING] - r->c) / r->g);

    if (r->frame_kind == P_LSF)
    {
        lsf_t l;
        uint32_t e = decode_LSF(&l, pld);
        if (CRC_M17((const uint8_t*)&l, 30) == 0)
        {
            begin(stream, r);
            got_lsf(stream, r, &l);
        }
        else
        {
            if (verbose && r->active)
                fprintf(stderr, "m17: stream %d: LSF CRC error (e=%.1f)\n", stream, e / 65535.0);
            if (!r->active)
                restart_search(r);             // a false syncword
        }
        return;
    }
    if (r->frame_kind == P_PKT)
    {
        if (!r->active)
            restart_search(r);                 // packet mode: not decoded (yet)
        return;
    }

    // stream frame
    uint8_t data[16], lich[5], lich_cnt = 0;
    uint16_t fn = 0;
    uint32_t e = decode_str_frame(data, lich, &fn, &lich_cnt, pld);
    double ber = (e / 65535.0) / 272.0 * 100.0;       // ~errored bits of the 272 coded bits
    if (!r->active)
    {
        if (ber > BER_START)
        {
            restart_search(r);                 // a false syncword
            return;
        }
        begin(stream, r);
    }
    r->ber_sum += ber;
    r->ber_n++;

    if (lich_cnt < 6)
    {
        // a superframe at a time: META (text blocks) may change between them
        if (lich_cnt == 0)
            r->lich_got = 0;
        memcpy(r->lich + lich_cnt * 5, lich, 5);
        r->lich_got |= (uint8_t)(1 << lich_cnt);
        if (r->lich_got == 0x3F)
        {
            if (CRC_M17(r->lich, 30) == 0)
                got_lsf(stream, r, (const lsf_t*)r->lich);
            r->lich_got = 0;
        }
    }

    // voice: 3200 unless the link setup says otherwise
    int dt = r->have_lsf ? (r->type >> 1) & 3 : 2;
    bool encrypted = r->have_lsf && ((r->type >> 3) & 3) != 0;
    bool wrong_can = can_filter && r->have_lsf && ((r->type >> 7) & 0xF) != my_can;
    short pcm[320];
    if (encrypted || wrong_can || ber > BER_MUTE)
    {
        memset(pcm, 0, sizeof(pcm));
        voice_put(r, pcm, 320);
    }
    else if (dt == 3)
    {
        codec2_decode(r->c2_1600, pcm, data);           // voice in the first 8 bytes
        voice_put(r, pcm, 320);
    }
    else if (dt == 2)
    {
        codec2_decode(r->c2_3200, pcm, data);
        codec2_decode(r->c2_3200, pcm + 160, data + 8);
        voice_put(r, pcm, 320);
    }
    else
    {
        memset(pcm, 0, sizeof(pcm));
        voice_put(r, pcm, 320);
    }

    time_t t = time(NULL);
    if (t != r->last_status && r->ber_n > 0)
    {
        plugin_status(conn, stream, "ber=%.1f\n", r->ber_sum / r->ber_n);
        r->ber_sum = 0;
        r->ber_n = 0;
        r->last_status = t;
    }
    if (fn & 0x8000)
        end_of_transmission(stream, r, "end of stream");
} // end decode_frame


/* one filtered sample, already in the ring at index r->n - 1 */
static void framer(int stream, RX *r)
{
    long s = r->n - 1;
    double rho, g, c;
    int kind;

    if (s < SYNC_SPAN + RRC_TAPS)
        return;

    if (r->in_frame)
    {
        if (s == r->frame_sync + SYM_PER_PLD * SPS)
        {
            decode_frame(stream, r);
            r->in_frame = false;
        }
        return;
    }

    if (!r->searching)
    {
        // the next syncword ends one frame after the last one, give or take
        long expect = r->frame_sync + FRAME_SAMPLES;
        if (s < expect + 3)
            return;
        long best_at = -1;
        double best_rho = -1, bg = 0, bc = 0;
        int best_kind = -1;
        for (long at = expect - 3; at <= expect + 3; at++)
        {
            kind = best_pattern(r, at, &rho, &g, &c);
            if (kind >= 0 && rho > best_rho)
            {
                best_rho = rho;
                best_at = at;
                best_kind = kind;
                bg = g;
                bc = c;
            }
        }
        if (best_kind == P_EOT && best_rho > TRACK)
        {
            end_of_transmission(stream, r, "EOT");
            r->searching = true;
            r->cand_at = -1;
            return;
        }
        if (best_kind >= 0 && best_kind != P_EOT && best_rho > TRACK)
        {
            r->frame_sync = best_at;
            r->frame_kind = best_kind;
            r->g = bg;
            r->c = bc;
            r->in_frame = true;
            r->missed = 0;
            return;
        }
        // lost it: carry on searching; give up the transmission after a few frames
        r->searching = true;
        r->cand_at = -1;
        r->frame_sync = expect;
        r->missed = 1;
        return;
    }

    // searching: find the correlation peak above ACQUIRE
    kind = best_pattern(r, s, &rho, &g, &c);
    if (kind >= 0 && rho > ACQUIRE && (r->cand_at < 0 || rho > r->cand_rho))
    {
        r->cand_at = s;
        r->cand_rho = rho;
        r->cand_kind = kind;
        r->cand_g = g;
        r->cand_c = c;
    }
    if (r->cand_at >= 0 && s - r->cand_at >= SPS / 2)
    {
        if (r->cand_kind == P_EOT)
            end_of_transmission(stream, r, "EOT");
        else
        {
            r->frame_sync = r->cand_at;
            r->frame_kind = r->cand_kind;
            r->g = r->cand_g;
            r->c = r->cand_c;
            r->in_frame = true;
            r->searching = false;
            r->missed = 0;
        }
        r->cand_at = -1;
        return;
    }
    if (r->active && r->missed > 0 && s - r->frame_sync > 3 * FRAME_SAMPLES)
        end_of_transmission(stream, r, "signal lost");
} // end framer


static void on_rxiq(void *user, int stream, const float *iq, int n)
{
    RX *r;
    float audio[4096];
    int na;

    (void)user;
    if (stream < 0 || stream >= MAX_STREAMS || !st[stream].active || st[stream].tx)
        return;
    r = &st[stream].rx;

    for (int k = 0; k < n; k++)
    {
        double i = iq[2 * k], q = iq[2 * k + 1];
        double re = i * r->last_i + q * r->last_q;
        double im = q * r->last_i - i * r->last_q;
        double f = (re == 0.0 && im == 0.0) ? 0.0 : atan2(im, re) * RATE / (2.0 * M_PI);
        r->last_i = i;
        r->last_q = q;
        r->ring[r->n % RING] = fir(rrc, r->rrc_hist, &r->rrc_pos, (float)(f / DEV_PER_UNIT));
        r->n++;
        framer(stream, r);
    }

    // audio follows the input: n / 6 samples per block
    r->out_acc += n;
    na = (int)(r->out_acc / (RATE / AUDIO_RATE));
    r->out_acc -= (long)na * (RATE / AUDIO_RATE);
    if (na > (int)(sizeof(audio) / sizeof(audio[0])))
        na = sizeof(audio) / sizeof(audio[0]);
    for (int k = 0; k < na; k++)
    {
        if (r->v_tail < r->v_head)
            audio[k] = r->voice[r->v_tail++ % VOICE_FIFO];
        else
            audio[k] = 0.0f;
    }
    plugin_send_audio(conn, stream, audio, na);
} // end on_rxiq


/* ------------------------------------------------------------ transmit */

static void send_symbols(int stream, TX *t, const float *sym, int nsym)
{
    float *iq = (float*)malloc(sizeof(float) * 2 * (size_t)nsym * SPS);
    int n = 0;

    for (int k = 0; k < nsym; k++)
    {
        for (int j = 0; j < SPS; j++)
        {
            float x = fir(rrc_tx, t->hist, &t->pos, j == 0 ? sym[k] : 0.0f);
            t->phase = fmod(t->phase + 2.0 * M_PI * DEV_PER_UNIT * x / RATE, 2.0 * M_PI);
            iq[2 * n] = (float)(0.9 * cos(t->phase));
            iq[2 * n + 1] = (float)(0.9 * sin(t->phase));
            n++;
        }
    }
    plugin_send_txiq(conn, stream, iq, n);
    free(iq);
} // end send_symbols


static void tx_start(int stream, TX *t)
{
    float frame[SYM_PER_FRA];
    uint32_t cnt = 0;
    uint16_t type = M17_TYPE_STREAM | M17_TYPE_VOICE | M17_TYPE_ENCR_NONE | M17_TYPE_META_TEXT | M17_TYPE_CAN(my_can);
    size_t len = strlen(tx_text);

    // META text: 13 bytes per block, padded with spaces; no text, META all zero
    memset(t->meta, 0, sizeof(t->meta));
    t->nblk = (int)((len + TEXT_BLOCK - 1) / TEXT_BLOCK);
    for (int b = 0; b < t->nblk; b++)
    {
        t->meta[b][0] = (uint8_t)((((1 << t->nblk) - 1) << 4) | (1 << b));
        memset(t->meta[b] + 1, ' ', TEXT_BLOCK);
        for (size_t k = 0; k < TEXT_BLOCK && b * TEXT_BLOCK + k < len; k++)
            t->meta[b][1 + k] = (uint8_t)tx_text[b * TEXT_BLOCK + k];
    }
    t->blk = 0;
    if (set_LSF(&t->lsf, my_call, dst_call, type, t->meta[0]) != 0)
    {
        plugin_status(conn, stream, "log=%s  callsign \"%s\" or \"%s\" cannot be sent\n", now_hms(), my_call, dst_call);
        set_LSF(&t->lsf, "N0CALL", "@ALL", type, t->meta[0]);
    }
    t->fn = 0;
    t->lich_cnt = 0;
    t->nmic = 0;
    t->keyed = true;
    gen_preamble(frame, &cnt, PREAM_LSF);
    send_symbols(stream, t, frame, SYM_PER_FRA);
    gen_frame(frame, NULL, FRAME_LSF, &t->lsf, 0, 0);
    send_symbols(stream, t, frame, SYM_PER_FRA);
    plugin_status(conn, stream, "log=%s  transmitting %s \xe2\x86\x92 %s  CAN %d\n", now_hms(), my_call, dst_call, my_can);
} // end tx_start


static void tx_voice_frame(int stream, TX *t, bool last)
{
    short pcm[320];
    uint8_t data[16];
    float frame[SYM_PER_FRA];

    for (int k = 0; k < 320; k++)
    {
        float v = k < t->nmic ? t->mic[k] : 0.0f;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[k] = (short)lrintf(v * 32767.0f);
    }
    t->nmic = 0;
    codec2_encode(t->c2, data, pcm);
    codec2_encode(t->c2, data + 8, pcm + 160);
    // the next text block with each superframe (6 frames, 240 ms)
    if (t->lich_cnt == 0 && t->fn != 0 && t->nblk > 1)
    {
        t->blk = (t->blk + 1) % t->nblk;
        memcpy(t->lsf.meta, t->meta[t->blk], 14);
        update_LSF_CRC(&t->lsf);
    }
    gen_frame(frame, data, FRAME_STR, &t->lsf, t->lich_cnt, last ? (uint16_t)(t->fn | 0x8000) : t->fn);
    send_symbols(stream, t, frame, SYM_PER_FRA);
    t->fn = (uint16_t)((t->fn + 1) % 0x8000);
    t->lich_cnt = (uint8_t)((t->lich_cnt + 1) % 6);
} // end tx_voice_frame


static void tx_end(int stream, TX *t)
{
    float frame[SYM_PER_FRA], flush[RRC_TAPS / SPS + 1] = {0};
    uint32_t cnt = 0;

    if (!t->keyed)
        return;
    t->keyed = false;
    tx_voice_frame(stream, t, true);
    gen_eot(frame, &cnt);
    send_symbols(stream, t, frame, SYM_PER_FRA);
    send_symbols(stream, t, flush, (int)(sizeof(flush) / sizeof(flush[0])));   // empty the RRC
    plugin_send_txdone(conn, stream);
    plugin_status(conn, stream, "log=%s  end of transmission (%u frames)\n", now_hms(), (unsigned)t->fn);
} // end tx_end


static void on_mic(void *user, int stream, const float *audio, int n)
{
    TX *t;

    (void)user;
    if (stream < 0 || stream >= MAX_STREAMS || !st[stream].active || !st[stream].tx)
        return;
    t = &st[stream].t;
    if (!t->keyed)
        return;                                   // the tail: already finished
    for (int k = 0; k < n; k++)
    {
        t->mic[t->nmic++] = audio[k];
        if (t->nmic == 320)
            tx_voice_frame(stream, t, false);
    }
} // end on_mic


static void on_mox(void *user, int stream, bool on)
{
    (void)user;
    if (stream < 0 || stream >= MAX_STREAMS || !st[stream].active || !st[stream].tx)
        return;
    if (on)
        tx_start(stream, &st[stream].t);
    else
        tx_end(stream, &st[stream].t);
} // end on_mox


/* ------------------------------------------------------------ control */

static void on_accept(void *user, const char *text)
{
    (void)user;
    fprintf(stderr, "m17_plugin: registered with dspserver:\n%s", text);
} // end on_accept


static void on_start(void *user, int stream, bool tx, const char *mode)
{
    STREAM *s;

    (void)user;
    if (stream < 0 || stream >= MAX_STREAMS)
        return;
    s = &st[stream];
    if (s->rx.c2_3200 == NULL)
    {
        s->rx.c2_3200 = codec2_create(CODEC2_MODE_3200);
        s->rx.c2_1600 = codec2_create(CODEC2_MODE_1600);
        s->t.c2 = codec2_create(CODEC2_MODE_3200);
    }
    struct CODEC2 *a = s->rx.c2_3200, *b = s->rx.c2_1600, *c = s->t.c2;
    memset(&s->rx, 0, sizeof(RX));
    memset(&s->t, 0, sizeof(TX));
    s->rx.c2_3200 = a;
    s->rx.c2_1600 = b;
    s->t.c2 = c;
    s->rx.last_i = 1.0;
    s->rx.searching = true;
    s->rx.cand_at = -1;
    s->active = true;
    s->tx = tx;
    fprintf(stderr, "m17_plugin: stream %d %s %s\n", stream, tx ? "transmit" : "receive", mode);
    if (!tx)
        plugin_status(conn, stream, "signal=idle\n");
} // end on_start


static void on_stop(void *user, int stream)
{
    (void)user;
    if (stream < 0 || stream >= MAX_STREAMS)
        return;
    if (st[stream].active && st[stream].tx && st[stream].t.keyed)
        tx_end(stream, &st[stream].t);
    st[stream].active = false;
    fprintf(stderr, "m17_plugin: stream %d stopped\n", stream);
} // end on_stop


/* At most TEXT_MAX bytes, not cutting a UTF-8 character; control
 * characters and leading/trailing spaces removed. */
static void set_tx_text(const char *value)
{
    size_t n = 0, k = 0;

    while (value[k] == ' ')
        k++;
    for (; value[k] != '\0' && n < TEXT_MAX; k++)
    {
        unsigned char c = (unsigned char)value[k];
        tx_text[n++] = (char)(c < 0x20 || c == 0x7F ? ' ' : c);
    }
    // back off over a character cut in two (continuation bytes 10xxxxxx)
    if (value[k] != '\0')
    {
        size_t lead = n;
        while (lead > 0 && ((unsigned char)tx_text[lead - 1] & 0xC0) == 0x80)
            lead--;
        if (lead > 0 && ((unsigned char)tx_text[lead - 1] & 0xC0) == 0xC0)
        {
            unsigned char c = (unsigned char)tx_text[lead - 1];
            size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
            if (n - (lead - 1) < need)
                n = lead - 1;
        }
    }
    while (n > 0 && tx_text[n - 1] == ' ')
        n--;
    tx_text[n] = '\0';
} // end set_tx_text


static void on_setting(void *user, int stream, const char *key, const char *value)
{
    (void)user;
    (void)stream;
    if (strcmp(key, "src") == 0)
        upper(my_call, sizeof(my_call), value[0] ? value : "N0CALL");
    else if (strcmp(key, "dst") == 0)
        upper(dst_call, sizeof(dst_call), value[0] ? value : "@ALL");
    else if (strcmp(key, "can") == 0)
        my_can = atoi(value) & 0xF;
    else if (strcmp(key, "canrx") == 0)
        can_filter = atoi(value) != 0;
    else if (strcmp(key, "txtext") == 0)
        set_tx_text(value);
    else if (strcmp(key, "agc") == 0)
        agc_on = atoi(value) != 0;
    else if (strcmp(key, "gain") == 0)
        fixed_gain = pow(10.0, atof(value) / 20.0);
    else if (strcmp(key, "clear") == 0)
        plugin_status(conn, -1, "log.clear=1\n");
    if (verbose)
        fprintf(stderr, "m17_plugin: %s=%s\n", key, value);
} // end on_setting


static void on_signal(int sig)
{
    (void)sig;
    stop = true;
} // end on_signal


static void make_filters(void)
{
    double sum = 0.0;

    for (int k = 0; k < RRC_TAPS; k++)
        sum += rrc_taps_10[k];
    for (int k = 0; k < RRC_TAPS; k++)
    {
        rrc[k] = (float)(rrc_taps_10[k] / sum);            // matched filter, unity DC gain
        rrc_tx[k] = (float)(rrc_taps_10[k] * SPS / sum);   // one impulse per SPS samples
    }
    memcpy(pattern[P_LSF], lsf_sync_symbols, SYM_PER_SWD);
    memcpy(pattern[P_STR], str_sync_symbols, SYM_PER_SWD);
    memcpy(pattern[P_PKT], pkt_sync_symbols, SYM_PER_SWD);
    memcpy(pattern[P_EOT], eot_symbols, SYM_PER_SWD);
} // end make_filters


int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int port = PLUGIN_PORT;
    int opt;
    PLUGIN_CALLBACKS cb = {on_accept, on_start, on_stop, on_rxiq, on_mic, on_setting, on_mox};

    while ((opt = getopt(argc, argv, "s:p:vh")) != -1)
    {
        switch (opt)
        {
        case 's':
            host = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 'v':
            verbose = true;
            break;
        default:
            fprintf(stderr, "usage: %s [-s dspserver_host] [-p port] [-v]\n", argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    make_filters();

    conn = plugin_open(host, port, XML);
    if (conn == NULL)
        return 1;
    bool ok = plugin_run(conn, &cb, NULL, &stop);
    if (!ok)
        fprintf(stderr, "m17_plugin: dspserver closed the connection\n");
    plugin_close(conn);
    for (int k = 0; k < MAX_STREAMS; k++)
    {
        if (st[k].rx.c2_3200 != NULL)
        {
            codec2_destroy(st[k].rx.c2_3200);
            codec2_destroy(st[k].rx.c2_1600);
            codec2_destroy(st[k].t.c2);
        }
    }
    return ok ? 0 : 1;
} // end main
