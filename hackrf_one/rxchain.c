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
#include <samplerate.h>

#include "rxchain.h"

#define CIC_ORDER     5
#define NCO_SCALE     16384          // NCO table amplitude (2^14)
#define NCO_MAX_LEN   (1 << 20)
#define FIR_DECIM     4
#define FIR_ATTEN_DB  90.0
#define SRC_CHUNK     256            // frames handed to libsamplerate at a time

struct _rxchain
{
    long     hw_rate;
    long     rx_rate;
    long     if_offset;

    // NCO: exact, periodic table of e^(j*2*pi*if_offset*k/hw_rate)
    int      nco_len;
    int      nco_pos;
    int32_t *nco_cos;
    int32_t *nco_sin;

    // CIC decimator (integer, wraps modulo 2^64 by design)
    int      cic_dec;
    int      cic_count;
    uint64_t integ_i[CIC_ORDER];
    uint64_t integ_q[CIC_ORDER];
    uint64_t comb_i[CIC_ORDER];
    uint64_t comb_q[CIC_ORDER];
    double   cic_scale;

    // FIR decimate by FIR_DECIM, complex history kept twice for a linear read
    int      fir_taps;
    float   *fir;
    float   *hist;
    int      hist_pos;
    int      fir_phase;

    // fractional resampler to the exact receiver rate
    SRC_STATE *src;
    double   src_ratio;
    float   *src_in;
    int      src_in_frames;
    float   *src_out;
    int      src_out_frames;

    // output block
    double   out[RXCHAIN_BLOCK * 2];
    int      out_count;
    rxchain_output_fn output;
    void    *user;
};


static long gcd_long(long a, long b)
{
    while (b != 0)
    {
        long t = a % b;
        a = b;
        b = t;
    }
    return a < 0 ? -a : a;
} // end gcd_long


static double bessel_i0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    double y = x * x / 4.0;

    for (int k = 1; k < 64; k++)
    {
        term *= y / ((double)k * (double)k);
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
} // end bessel_i0


/* Kaiser windowed sinc low pass.  pass and stop are edges in cycles/sample. */
float *rxchain_design_lowpass(double pass, double stop, double atten, int *ntaps)
{
    double delta = stop - pass;
    double fc = (pass + stop) / 2.0;
    double beta;
    int n;
    float *h;
    double sum = 0.0;

    if (atten > 50.0)
        beta = 0.1102 * (atten - 8.7);
    else
        beta = 0.5842 * pow(atten - 21.0, 0.4) + 0.07886 * (atten - 21.0);

    n = (int)ceil((atten - 7.95) / (2.285 * 2.0 * M_PI * delta)) + 1;
    if (n < 5)
        n = 5;
    if ((n & 1) == 0)
        n++;

    h = malloc(sizeof(float) * (size_t)n);
    if (h == NULL)
        return NULL;

    double *tmp = malloc(sizeof(double) * (size_t)n);
    if (tmp == NULL)
    {
        free(h);
        return NULL;
    }

    double mid = (n - 1) / 2.0;
    double i0b = bessel_i0(beta);
    for (int i = 0; i < n; i++)
    {
        double t = (double)i - mid;
        double x = 2.0 * fc * t;
        double sinc = (fabs(x) < 1e-12) ? 1.0 : sin(M_PI * x) / (M_PI * x);
        double r = t / mid;
        double w = bessel_i0(beta * sqrt(fmax(0.0, 1.0 - r * r))) / i0b;
        tmp[i] = 2.0 * fc * sinc * w;
        sum += tmp[i];
    }
    for (int i = 0; i < n; i++)
        h[i] = (float)(tmp[i] / sum);     // unity gain at DC
    free(tmp);

    *ntaps = n;
    return h;
} // end design_lowpass


RXCHAIN *rxchain_create(long hw_rate, long rx_rate, long if_offset, rxchain_output_fn output, void *user)
{
    RXCHAIN *c;
    int err = 0;

    if (hw_rate <= 0 || rx_rate <= 0 || output == NULL)
        return NULL;
    if (hw_rate < 8 * rx_rate)
    {
        fprintf(stderr, "rxchain: hardware rate %ld too low for receiver rate %ld\n", hw_rate, rx_rate);
        return NULL;
    }
    if (labs(if_offset) >= hw_rate / 2)
        return NULL;

    c = calloc(1, sizeof(RXCHAIN));
    if (c == NULL)
        return NULL;

    c->hw_rate = hw_rate;
    c->rx_rate = rx_rate;
    c->if_offset = if_offset;
    c->output = output;
    c->user = user;

    // NCO table length is the exact period of the offset frequency
    if (if_offset == 0)
        c->nco_len = 1;
    else
    {
        long len = hw_rate / gcd_long(hw_rate, labs(if_offset));
        if (len > NCO_MAX_LEN)
        {
            fprintf(stderr, "rxchain: IF offset %ld does not divide %ld well enough\n", if_offset, hw_rate);
            free(c);
            return NULL;
        }
        c->nco_len = (int)len;
    }
    c->nco_cos = malloc(sizeof(int32_t) * (size_t)c->nco_len);
    c->nco_sin = malloc(sizeof(int32_t) * (size_t)c->nco_len);
    if (c->nco_cos == NULL || c->nco_sin == NULL)
    {
        rxchain_destroy(c);
        return NULL;
    }
    for (int k = 0; k < c->nco_len; k++)
    {
        double ph = 2.0 * M_PI * (double)if_offset * (double)k / (double)hw_rate;
        c->nco_cos[k] = (int32_t)lround(cos(ph) * NCO_SCALE);
        c->nco_sin[k] = (int32_t)lround(sin(ph) * NCO_SCALE);
    }

    // CIC: keep fs2 = hw_rate / (cic_dec * FIR_DECIM) >= 2 * rx_rate
    c->cic_dec = (int)(hw_rate / (2L * FIR_DECIM * rx_rate));
    if (c->cic_dec < 1)
        c->cic_dec = 1;
    // bit growth: 8 bit samples * 2^14 NCO * 2 (sum) * cic_dec^5 must fit in 63 bits
    if (CIC_ORDER * log2((double)c->cic_dec) + 23.0 > 62.0)
    {
        fprintf(stderr, "rxchain: CIC decimation %d too large\n", c->cic_dec);
        rxchain_destroy(c);
        return NULL;
    }
    c->cic_scale = 1.0 / (128.0 * NCO_SCALE * pow((double)c->cic_dec, CIC_ORDER));

    double fs1 = (double)hw_rate / c->cic_dec;
    double fs2 = fs1 / FIR_DECIM;

    // FIR: flat to rx_rate/2, stop where the next decimation would alias in
    c->fir = rxchain_design_lowpass(0.5 * rx_rate / fs1, (fs2 - 0.5 * rx_rate) / fs1, FIR_ATTEN_DB, &c->fir_taps);
    if (c->fir == NULL)
    {
        rxchain_destroy(c);
        return NULL;
    }
    c->hist = calloc((size_t)c->fir_taps * 4, sizeof(float));   // 2 copies * complex
    if (c->hist == NULL)
    {
        rxchain_destroy(c);
        return NULL;
    }

    // fractional resampler fs2 -> rx_rate
    c->src_ratio = (double)rx_rate / fs2;
    c->src = src_new(SRC_SINC_MEDIUM_QUALITY, 2, &err);
    if (c->src == NULL)
    {
        fprintf(stderr, "rxchain: src_new failed: %s\n", src_strerror(err));
        rxchain_destroy(c);
        return NULL;
    }
    c->src_in = malloc(sizeof(float) * SRC_CHUNK * 2 * 2);
    c->src_out_frames = (int)(SRC_CHUNK * 2 * c->src_ratio) + 16;
    c->src_out = malloc(sizeof(float) * (size_t)c->src_out_frames * 2);
    if (c->src_in == NULL || c->src_out == NULL)
    {
        rxchain_destroy(c);
        return NULL;
    }

    fprintf(stderr, "rxchain: %ld -> CIC/%d (%.0f) -> FIR/%d %d taps (%.0f) -> SRC x%.6f -> %ld\n",
            hw_rate, c->cic_dec, fs1, FIR_DECIM, c->fir_taps, fs2, c->src_ratio, rx_rate);
    return c;
} // end rxchain_create


void rxchain_destroy(RXCHAIN *c)
{
    if (c == NULL)
        return;
    if (c->src != NULL)
        src_delete(c->src);
    free(c->nco_cos);
    free(c->nco_sin);
    free(c->fir);
    free(c->hist);
    free(c->src_in);
    free(c->src_out);
    free(c);
} // end rxchain_destroy


static void emit_samples(RXCHAIN *c, const float *iq, int frames)
{
    for (int k = 0; k < frames; k++)
    {
        c->out[c->out_count * 2] = iq[k * 2];
        c->out[c->out_count * 2 + 1] = iq[k * 2 + 1];
        if (++c->out_count == RXCHAIN_BLOCK)
        {
            c->output(c->user, c->out, RXCHAIN_BLOCK);
            c->out_count = 0;
        }
    }
} // end emit_samples


static void run_src(RXCHAIN *c)
{
    SRC_DATA d;
    int used = 0;

    while (used < c->src_in_frames)
    {
        memset(&d, 0, sizeof(d));
        d.data_in = c->src_in + used * 2;
        d.input_frames = c->src_in_frames - used;
        d.data_out = c->src_out;
        d.output_frames = c->src_out_frames;
        d.src_ratio = c->src_ratio;
        d.end_of_input = 0;
        int rc = src_process(c->src, &d);
        if (rc != 0)
        {
            fprintf(stderr, "rxchain: src_process: %s\n", src_strerror(rc));
            break;
        }
        emit_samples(c, c->src_out, (int)d.output_frames_gen);
        used += (int)d.input_frames_used;
        if (d.input_frames_used == 0 && d.output_frames_gen == 0)
            break;
    }
    if (used < c->src_in_frames)
        memmove(c->src_in, c->src_in + used * 2, sizeof(float) * (size_t)(c->src_in_frames - used) * 2);
    c->src_in_frames -= used;
} // end run_src


static void fir_push(RXCHAIN *c, float i, float q)
{
    int n = c->fir_taps;

    c->hist_pos = (c->hist_pos == 0) ? n - 1 : c->hist_pos - 1;
    c->hist[c->hist_pos * 2] = i;
    c->hist[c->hist_pos * 2 + 1] = q;
    c->hist[(c->hist_pos + n) * 2] = i;
    c->hist[(c->hist_pos + n) * 2 + 1] = q;

    if (++c->fir_phase < FIR_DECIM)
        return;
    c->fir_phase = 0;

    const float *x = c->hist + c->hist_pos * 2;
    const float *h = c->fir;
    float si = 0.0f, sq = 0.0f;
    for (int k = 0; k < n; k++)
    {
        si += h[k] * x[k * 2];
        sq += h[k] * x[k * 2 + 1];
    }

    c->src_in[c->src_in_frames * 2] = si;
    c->src_in[c->src_in_frames * 2 + 1] = sq;
    if (++c->src_in_frames >= SRC_CHUNK)
        run_src(c);
} // end fir_push


void rxchain_process(RXCHAIN *c, const int8_t *iq, size_t n_complex)
{
    const int32_t *nc = c->nco_cos;
    const int32_t *ns = c->nco_sin;
    int pos = c->nco_pos;
    int len = c->nco_len;
    int count = c->cic_count;
    int dec = c->cic_dec;
    uint64_t i0 = c->integ_i[0], i1 = c->integ_i[1], i2 = c->integ_i[2], i3 = c->integ_i[3], i4 = c->integ_i[4];
    uint64_t q0 = c->integ_q[0], q1 = c->integ_q[1], q2 = c->integ_q[2], q3 = c->integ_q[3], q4 = c->integ_q[4];

    for (size_t k = 0; k < n_complex; k++)
    {
        int32_t si = iq[k * 2];
        int32_t sq = iq[k * 2 + 1];
        int32_t co = nc[pos];
        int32_t sn = ns[pos];

        if (++pos == len)
            pos = 0;

        // multiply by e^(-j*w*t): shifts +if_offset down to DC
        int32_t mi = si * co + sq * sn;
        int32_t mq = sq * co - si * sn;

        i0 += (uint64_t)(int64_t)mi; i1 += i0; i2 += i1; i3 += i2; i4 += i3;
        q0 += (uint64_t)(int64_t)mq; q1 += q0; q2 += q1; q3 += q2; q4 += q3;

        if (++count < dec)
            continue;
        count = 0;

        uint64_t vi = i4, vq = q4;
        for (int s = 0; s < CIC_ORDER; s++)
        {
            uint64_t ti = vi, tq = vq;
            vi -= c->comb_i[s];
            vq -= c->comb_q[s];
            c->comb_i[s] = ti;
            c->comb_q[s] = tq;
        }
        fir_push(c, (float)((double)(int64_t)vi * c->cic_scale), (float)((double)(int64_t)vq * c->cic_scale));
    }

    c->nco_pos = pos;
    c->cic_count = count;
    c->integ_i[0] = i0; c->integ_i[1] = i1; c->integ_i[2] = i2; c->integ_i[3] = i3; c->integ_i[4] = i4;
    c->integ_q[0] = q0; c->integ_q[1] = q1; c->integ_q[2] = q2; c->integ_q[3] = q3; c->integ_q[4] = q4;
} // end rxchain_process


int rxchain_cic_decimation(const RXCHAIN *c)
{
    return c->cic_dec;
} // end rxchain_cic_decimation


int rxchain_fir_taps(const RXCHAIN *c)
{
    return c->fir_taps;
} // end rxchain_fir_taps


double rxchain_src_ratio(const RXCHAIN *c)
{
    return c->src_ratio;
} // end rxchain_src_ratio
