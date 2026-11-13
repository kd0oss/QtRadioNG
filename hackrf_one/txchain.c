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

#include "txchain.h"
#include "rxchain.h"      // rxchain_design_lowpass()

#define CIC_ORDER     5
#define FIR_INTERP    4
#define FIR_ATTEN_DB  90.0
#define SRC_CHUNK     256
#define NCO_MAX_LEN   (1 << 20)
#define CIC_IN_SCALE  1048576.0     // 2^20: fs1 samples are fixed point before the CIC

struct _txchain
{
    long     hw_rate;
    long     in_rate;

    txchain_source_fn source;
    void    *user;

    // libsamplerate in callback mode pulls from the source
    SRC_STATE *src;
    double   src_ratio;
    float    src_in[SRC_CHUNK * 2];
    float    fs2_buf[SRC_CHUNK * 2];
    int      fs2_count;
    int      fs2_pos;

    // FIR interpolator, polyphase: taps[p + FIR_INTERP*m] for phase p
    int      fir_taps;           // multiple of FIR_INTERP
    int      fir_len;            // taps per phase
    float   *fir;
    float   *hist;               // fs2 history, two copies, complex
    int      hist_pos;
    int      fir_phase;

    // CIC interpolator (integer, wraps modulo 2^64 by design)
    int      cic_interp;
    int      cic_phase;
    int64_t  comb_i[CIC_ORDER];
    int64_t  comb_q[CIC_ORDER];
    uint64_t integ_i[CIC_ORDER];
    uint64_t integ_q[CIC_ORDER];
    int64_t  pending_i;          // comb output waiting to enter the integrators
    int64_t  pending_q;
    double   cic_scale;

    // NCO up to +if_offset
    int      nco_len;
    int      nco_pos;
    double  *nco_cos;
    double  *nco_sin;
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


static long src_callback(void *cb_data, float **data)
{
    TXCHAIN *c = cb_data;
    long n = c->source(c->user, c->src_in, SRC_CHUNK);

    if (n <= 0)
    {
        memset(c->src_in, 0, sizeof(c->src_in));
        n = SRC_CHUNK;
    }
    *data = c->src_in;
    return n;
} // end src_callback


TXCHAIN *txchain_create(long hw_rate, long in_rate, long if_offset, txchain_source_fn source, void *user)
{
    TXCHAIN *c;
    int err = 0;

    if (hw_rate <= 0 || in_rate <= 0 || source == NULL || hw_rate < 8 * in_rate)
        return NULL;
    if (labs(if_offset) >= hw_rate / 2)
        return NULL;

    c = calloc(1, sizeof(TXCHAIN));
    if (c == NULL)
        return NULL;
    c->hw_rate = hw_rate;
    c->in_rate = in_rate;
    c->source = source;
    c->user = user;

    c->cic_interp = (int)(hw_rate / (2L * FIR_INTERP * in_rate));
    if (c->cic_interp < 1)
        c->cic_interp = 1;
    double fs1 = (double)hw_rate / c->cic_interp;
    double fs2 = fs1 / FIR_INTERP;
    // interpolating CIC gain is N^(order-1)
    c->cic_scale = 1.0 / (CIC_IN_SCALE * pow((double)c->cic_interp, CIC_ORDER - 1));

    // FIR at fs1: keep the input band, remove the images around multiples of fs2
    int ntaps = 0;
    float *h = rxchain_design_lowpass(0.5 * in_rate / fs1, (fs2 - 0.5 * in_rate) / fs1, FIR_ATTEN_DB, &ntaps);
    if (h == NULL)
    {
        free(c);
        return NULL;
    }
    c->fir_len = (ntaps + FIR_INTERP - 1) / FIR_INTERP;
    c->fir_taps = c->fir_len * FIR_INTERP;
    c->fir = calloc((size_t)c->fir_taps, sizeof(float));
    c->hist = calloc((size_t)c->fir_len * 4, sizeof(float));
    if (c->fir == NULL || c->hist == NULL)
    {
        free(h);
        txchain_destroy(c);
        return NULL;
    }
    for (int i = 0; i < ntaps; i++)
        c->fir[i] = h[i] * FIR_INTERP;          // zero stuffing loses a factor FIR_INTERP
    free(h);

    c->src_ratio = fs2 / (double)in_rate;
    c->src = src_callback_new(src_callback, SRC_SINC_MEDIUM_QUALITY, 2, &err, c);
    if (c->src == NULL)
    {
        fprintf(stderr, "txchain: src_callback_new failed: %s\n", src_strerror(err));
        txchain_destroy(c);
        return NULL;
    }

    if (if_offset == 0)
        c->nco_len = 1;
    else
    {
        long len = hw_rate / gcd_long(hw_rate, labs(if_offset));
        if (len > NCO_MAX_LEN)
        {
            txchain_destroy(c);
            return NULL;
        }
        c->nco_len = (int)len;
    }
    c->nco_cos = malloc(sizeof(double) * (size_t)c->nco_len);
    c->nco_sin = malloc(sizeof(double) * (size_t)c->nco_len);
    if (c->nco_cos == NULL || c->nco_sin == NULL)
    {
        txchain_destroy(c);
        return NULL;
    }
    for (int k = 0; k < c->nco_len; k++)
    {
        double ph = 2.0 * M_PI * (double)if_offset * (double)k / (double)hw_rate;
        c->nco_cos[k] = cos(ph);
        c->nco_sin[k] = sin(ph);
    }

    fprintf(stderr, "txchain: %ld -> SRC x%.4f (%.0f) -> FIR x%d %d taps (%.0f) -> CIC x%d -> %ld\n",
            in_rate, c->src_ratio, fs2, FIR_INTERP, c->fir_taps, fs1, c->cic_interp, hw_rate);
    return c;
} // end txchain_create


void txchain_destroy(TXCHAIN *c)
{
    if (c == NULL)
        return;
    if (c->src != NULL)
        src_delete(c->src);
    free(c->fir);
    free(c->hist);
    free(c->nco_cos);
    free(c->nco_sin);
    free(c);
} // end txchain_destroy


/* Next fs1 sample from the FIR interpolator. */
static void next_fs1(TXCHAIN *c, double rate_adjust, float *oi, float *oq)
{
    int n = c->fir_len;

    if (c->fir_phase == 0)
    {
        if (c->fs2_pos >= c->fs2_count)
        {
            long got = src_callback_read(c->src, c->src_ratio * (1.0 - rate_adjust), SRC_CHUNK, c->fs2_buf);
            if (got <= 0)
            {
                memset(c->fs2_buf, 0, sizeof(c->fs2_buf));
                got = SRC_CHUNK;
            }
            c->fs2_count = (int)got;
            c->fs2_pos = 0;
        }
        float xi = c->fs2_buf[c->fs2_pos * 2];
        float xq = c->fs2_buf[c->fs2_pos * 2 + 1];
        c->fs2_pos++;

        c->hist_pos = (c->hist_pos == 0) ? n - 1 : c->hist_pos - 1;
        c->hist[c->hist_pos * 2] = xi;
        c->hist[c->hist_pos * 2 + 1] = xq;
        c->hist[(c->hist_pos + n) * 2] = xi;
        c->hist[(c->hist_pos + n) * 2 + 1] = xq;
    }

    const float *x = c->hist + c->hist_pos * 2;
    const float *h = c->fir + c->fir_phase;
    float si = 0.0f, sq = 0.0f;
    for (int m = 0; m < n; m++)
    {
        si += h[m * FIR_INTERP] * x[m * 2];
        sq += h[m * FIR_INTERP] * x[m * 2 + 1];
    }
    *oi = si;
    *oq = sq;
    if (++c->fir_phase == FIR_INTERP)
        c->fir_phase = 0;
} // end next_fs1


void txchain_run(TXCHAIN *c, int8_t *out, size_t n_complex, double rate_adjust)
{
    uint64_t i0 = c->integ_i[0], i1 = c->integ_i[1], i2 = c->integ_i[2], i3 = c->integ_i[3], i4 = c->integ_i[4];
    uint64_t q0 = c->integ_q[0], q1 = c->integ_q[1], q2 = c->integ_q[2], q3 = c->integ_q[3], q4 = c->integ_q[4];
    int phase = c->cic_phase;
    int pos = c->nco_pos;

    if (rate_adjust > 0.01)
        rate_adjust = 0.01;
    if (rate_adjust < -0.01)
        rate_adjust = -0.01;

    for (size_t k = 0; k < n_complex; k++)
    {
        int64_t in_i = 0, in_q = 0;

        if (phase == 0)
        {
            float fi, fq;
            next_fs1(c, rate_adjust, &fi, &fq);
            int64_t vi = llround((double)fi * CIC_IN_SCALE);
            int64_t vq = llround((double)fq * CIC_IN_SCALE);
            // comb section at the low rate
            for (int s = 0; s < CIC_ORDER; s++)
            {
                int64_t ti = vi, tq = vq;
                vi -= c->comb_i[s];
                vq -= c->comb_q[s];
                c->comb_i[s] = ti;
                c->comb_q[s] = tq;
            }
            in_i = vi;     // zero stuffing: only the first of cic_interp samples carries it
            in_q = vq;
        }
        if (++phase == c->cic_interp)
            phase = 0;

        i0 += (uint64_t)in_i; i1 += i0; i2 += i1; i3 += i2; i4 += i3;
        q0 += (uint64_t)in_q; q1 += q0; q2 += q1; q3 += q2; q4 += q3;

        double yi = (double)(int64_t)i4 * c->cic_scale;
        double yq = (double)(int64_t)q4 * c->cic_scale;

        // shift up by the IF offset: LO + if_offset is the transmit frequency
        double co = c->nco_cos[pos], sn = c->nco_sin[pos];
        if (++pos == c->nco_len)
            pos = 0;
        double ri = (yi * co - yq * sn) * 127.0;
        double rq = (yi * sn + yq * co) * 127.0;
        if (ri > 127.0) ri = 127.0;
        if (ri < -127.0) ri = -127.0;
        if (rq > 127.0) rq = 127.0;
        if (rq < -127.0) rq = -127.0;
        out[k * 2] = (int8_t)lrint(ri);
        out[k * 2 + 1] = (int8_t)lrint(rq);
    }

    c->integ_i[0] = i0; c->integ_i[1] = i1; c->integ_i[2] = i2; c->integ_i[3] = i3; c->integ_i[4] = i4;
    c->integ_q[0] = q0; c->integ_q[1] = q1; c->integ_q[2] = q2; c->integ_q[3] = q3; c->integ_q[4] = q4;
    c->cic_phase = phase;
    c->nco_pos = pos;
} // end txchain_run


int txchain_cic_interpolation(const TXCHAIN *c)
{
    return c->cic_interp;
} // end txchain_cic_interpolation


int txchain_fir_taps(const TXCHAIN *c)
{
    return c->fir_taps;
} // end txchain_fir_taps
