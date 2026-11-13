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

#include <string.h>
#include <strings.h>
#include <math.h>

#include "txaudio.h"

#define CUTOFF_HZ   10000.0      // passes all transmit audio, stops 14 kHz and up
#define AM_DC_HZ    20.0


void txaudio_set_kind(TXAUDIO *t, TXA_KIND kind)
{
    t->kind = kind;
    t->last_i = 1.0;
    t->last_q = 0.0;
    t->dc = 0.0;
} // end txaudio_set_kind


void txaudio_init(TXAUDIO *t, TXA_KIND kind)
{
    double sum = 0.0;

    memset(t, 0, sizeof(*t));
    // windowed sinc low pass (Blackman), unity gain at DC
    for (int k = 0; k < TXA_TAPS; k++)
    {
        double m = k - (TXA_TAPS - 1) / 2.0;
        double x = 2.0 * CUTOFF_HZ / TXA_IN_RATE;
        double h = (m == 0.0) ? x : sin(M_PI * x * m) / (M_PI * m);
        double w = 0.42 - 0.5 * cos(2.0 * M_PI * k / (TXA_TAPS - 1)) + 0.08 * cos(4.0 * M_PI * k / (TXA_TAPS - 1));
        t->taps[k] = (float)(h * w);
        sum += h * w;
    }
    for (int k = 0; k < TXA_TAPS; k++)
        t->taps[k] = (float)(t->taps[k] / sum);
    txaudio_set_kind(t, kind);
} // end txaudio_init


int txaudio_run(TXAUDIO *t, const float *iq, int n, float *audio)
{
    int out = 0;
    const double dc_a = 2.0 * M_PI * AM_DC_HZ / TXA_IN_RATE;

    for (int k = 0; k < n; k++)
    {
        double i = iq[k * 2];
        double q = iq[k * 2 + 1];
        double x;

        switch (t->kind)
        {
        case TXA_FM:
        {
            // angle of s[n] * conj(s[n-1]) is the phase step
            double re = i * t->last_i + q * t->last_q;
            double im = q * t->last_i - i * t->last_q;
            x = (i == 0.0 && q == 0.0) ? 0.0 : atan2(im, re) * TXA_IN_RATE / (2.0 * M_PI) / TXA_FM_DEV * 0.9;
            if (i != 0.0 || q != 0.0)
            {
                t->last_i = i;
                t->last_q = q;
            }
            break;
        }
        case TXA_AM:
            t->dc += dc_a * (i - t->dc);
            x = i - t->dc;
            break;
        case TXA_OFF:
            x = 0.0;
            break;
        case TXA_SSB:
        default:
            x = i;
            break;
        }

        t->hist[t->pos] = t->hist[t->pos + TXA_TAPS] = (float)x;
        t->pos = (t->pos + 1) % TXA_TAPS;
        if (++t->phase == TXA_DECIM)
        {
            // hist[pos .. pos+TAPS-1] is oldest .. newest
            const float *h = t->hist + t->pos;
            double acc = 0.0;
            t->phase = 0;
            for (int j = 0; j < TXA_TAPS; j++)
                acc += t->taps[j] * h[j];
            if (acc > 1.0)
                acc = 1.0;
            if (acc < -1.0)
                acc = -1.0;
            audio[out++] = (float)acc;
        }
    }
    return out;
} // end txaudio_run


int txaudio_mode(const char *m, const char **flex, TXA_KIND *kind)
{
    static const struct
    {
        const char *qtradio;
        const char *flex;
        TXA_KIND    kind;
    } map[] = {
        {"LSB",  "LSB",  TXA_SSB},
        {"USB",  "USB",  TXA_SSB},
        {"DSB",  "USB",  TXA_SSB},    // no DSB on the radio: the upper side
        {"CWL",  "DIGL", TXA_SSB},    // the radio's CW mode will not take audio:
        {"CWU",  "DIGU", TXA_SSB},    //   the CW tone through DIGL/DIGU
        {"DIGL", "DIGL", TXA_SSB},
        {"DIGU", "DIGU", TXA_SSB},
        {"AM",   "AM",   TXA_AM},
        {"SAM",  "AM",   TXA_AM},
        {"FMN",  "FM",   TXA_FM},
        {"FM",   "FM",   TXA_FM},
        {"FMW",  "FM",   TXA_FM},
        {"M17",  "DFM",  TXA_FM},     // protocol plugin: 4FSK through the radio's digital FM (experimental)
    };

    for (unsigned k = 0; k < sizeof(map) / sizeof(map[0]); k++)
    {
        if (strcasecmp(m, map[k].qtradio) == 0)
        {
            *flex = map[k].flex;
            *kind = map[k].kind;
            return 0;
        }
    }
    *flex = "USB";
    *kind = TXA_OFF;                  // SPEC, DRM, other plugin modes ...
    return -1;
} // end txaudio_mode
