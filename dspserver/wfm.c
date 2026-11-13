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
 * Wide band (broadcast) FM demodulator, mono.
 *
 * WDSP's FM demodulator is a narrow PLL design for voice FM; it cannot follow
 * 75 kHz deviation.  For FMW the receiver IQ (Q,I order, as WDSP takes it)
 * goes through a quadrature discriminator, 75 us de-emphasis and a 15 kHz
 * low pass that also removes the 19 kHz stereo pilot.  Output is at the
 * receiver rate, so the normal dspserver audio resampler follows.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "wfm.h"

#define WFM_DEVIATION   75000.0
#define WFM_DEEMPH_TAU  75e-6       // North America / Korea; 50e-6 elsewhere
#define WFM_PASS        15000.0
#define WFM_STOP        19000.0     // stereo pilot
#define WFM_ATTEN_DB    60.0

struct _wfm
{
    int     rate;
    double  prev_i;
    double  prev_q;
    double  deemph_a;
    double  deemph_y;
    int     ntaps;
    double *taps;
    double *hist;        // two copies for a linear read
    int     pos;
};


static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0, y = x * x / 4.0;

    for (int k = 1; k < 64; k++)
    {
        term *= y / ((double)k * (double)k);
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
} // end bessel_i0


WFM *wfm_create(int rate)
{
    WFM *w = calloc(1, sizeof(WFM));
    double delta, fc, beta, mid, i0b, sum = 0.0;
    int n;

    if (w == NULL || rate < 48000)
    {
        free(w);
        return NULL;
    }
    w->rate = rate;
    w->deemph_a = 1.0 - exp(-1.0 / ((double)rate * WFM_DEEMPH_TAU));

    // Kaiser windowed sinc low pass, unity gain at DC
    delta = (WFM_STOP - WFM_PASS) / (double)rate;
    fc = (WFM_PASS + WFM_STOP) / 2.0 / (double)rate;
    beta = 0.1102 * (WFM_ATTEN_DB - 8.7);
    n = (int)ceil((WFM_ATTEN_DB - 7.95) / (2.285 * 2.0 * M_PI * delta)) + 1;
    if ((n & 1) == 0)
        n++;
    w->ntaps = n;
    w->taps = malloc(sizeof(double) * (size_t)n);
    w->hist = calloc((size_t)n * 2, sizeof(double));
    if (w->taps == NULL || w->hist == NULL)
    {
        wfm_destroy(w);
        return NULL;
    }
    mid = (n - 1) / 2.0;
    i0b = bessel_i0(beta);
    for (int i = 0; i < n; i++)
    {
        double t = (double)i - mid;
        double x = 2.0 * fc * t;
        double sinc = (fabs(x) < 1e-12) ? 1.0 : sin(M_PI * x) / (M_PI * x);
        double r = t / mid;
        w->taps[i] = 2.0 * fc * sinc * bessel_i0(beta * sqrt(fmax(0.0, 1.0 - r * r))) / i0b;
        sum += w->taps[i];
    }
    for (int i = 0; i < n; i++)
        w->taps[i] /= sum;
    return w;
} // end wfm_create


void wfm_destroy(WFM *w)
{
    if (w == NULL)
        return;
    free(w->taps);
    free(w->hist);
    free(w);
} // end wfm_destroy


int wfm_rate(const WFM *w)
{
    return w->rate;
} // end wfm_rate


void wfm_process(WFM *w, const double *in_qi, double *out_lr, int n, double gain)
{
    const double scale = (double)w->rate / (2.0 * M_PI * WFM_DEVIATION);
    int nt = w->ntaps;

    for (int k = 0; k < n; k++)
    {
        double q = in_qi[k * 2];
        double i = in_qi[k * 2 + 1];

        // phase step between samples: arg(x[n] * conj(x[n-1]))
        double re = i * w->prev_i + q * w->prev_q;
        double im = q * w->prev_i - i * w->prev_q;
        double d = (re == 0.0 && im == 0.0) ? 0.0 : atan2(im, re) * scale;
        w->prev_i = i;
        w->prev_q = q;

        // de-emphasis
        w->deemph_y += w->deemph_a * (d - w->deemph_y);

        // 15 kHz low pass
        w->pos = (w->pos == 0) ? nt - 1 : w->pos - 1;
        w->hist[w->pos] = w->deemph_y;
        w->hist[w->pos + nt] = w->deemph_y;
        const double *x = w->hist + w->pos;
        double y = 0.0;
        for (int t = 0; t < nt; t++)
            y += w->taps[t] * x[t];

        y *= gain;
        if (y > 1.0)
            y = 1.0;
        if (y < -1.0)
            y = -1.0;
        out_lr[k * 2] = y;
        out_lr[k * 2 + 1] = y;
    }
} // end wfm_process
