/*
 * Unit test for the FMW demodulator (wfm.c).
 * Broadcast-style FM at 192 and 384 kHz: tone level against the 75 us
 * de-emphasis curve, distortion, 19 kHz pilot rejection, CPU cost.
 *
 *   gcc -O3 -I.. -o test_wfm test_wfm.c ../wfm.c -lm && ./test_wfm
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "wfm.h"

static double measure(int rate, double ftone, double tone_dev, double pilot_dev,
                      double *sinad, double *pilot_db, double *cpu)
{
    int n = rate * 2;
    double *in = malloc(sizeof(double) * n * 2);
    double *out = malloc(sizeof(double) * n * 2);
    double ph = 0.0;
    WFM *w = wfm_create(rate);

    for (int k = 0; k < n; k++)
    {
        double t = (double)k / rate;
        double inst = tone_dev * sin(2 * M_PI * ftone * t) + pilot_dev * sin(2 * M_PI * 19000.0 * t);
        ph += 2 * M_PI * inst / rate;
        in[k * 2] = 0.5 * sin(ph);        // Q
        in[k * 2 + 1] = 0.5 * cos(ph);    // I
    }
    clock_t c0 = clock();
    for (int k = 0; k < n; k += 512)
        wfm_process(w, in + k * 2, out + k * 2, (n - k < 512) ? n - k : 512, 1.0);
    *cpu = (double)(clock() - c0) / CLOCKS_PER_SEC / 2.0;     // per second of signal

    // fit tone and pilot on the second half
    int s0 = n / 2, m = n / 2;
    double a = 0, b = 0, pa = 0, pb = 0;
    for (int k = 0; k < m; k++)
    {
        double t = (double)(s0 + k) / rate, y = out[(s0 + k) * 2];
        a += y * cos(2 * M_PI * ftone * t);
        b += y * sin(2 * M_PI * ftone * t);
        pa += y * cos(2 * M_PI * 19000.0 * t);
        pb += y * sin(2 * M_PI * 19000.0 * t);
    }
    a *= 2.0 / m; b *= 2.0 / m; pa *= 2.0 / m; pb *= 2.0 / m;
    double sig = 0, res = 0;
    for (int k = 0; k < m; k++)
    {
        double t = (double)(s0 + k) / rate, y = out[(s0 + k) * 2];
        double fit = a * cos(2 * M_PI * ftone * t) + b * sin(2 * M_PI * ftone * t);
        sig += fit * fit;
        res += (y - fit) * (y - fit);
    }
    double amp = sqrt(a * a + b * b);
    *sinad = 10 * log10(sig / (res + 1e-30));
    *pilot_db = 20 * log10(sqrt(pa * pa + pb * pb) / amp + 1e-30);
    wfm_destroy(w);
    free(in);
    free(out);
    return amp;
}

int main(void)
{
    static const int rates[] = {192000, 384000};
    static const double tones[] = {100, 1000, 5000, 10000};
    int fail = 0;
    double ref = 0.0;

    for (int r = 0; r < 2; r++)
    {
        for (int t = 0; t < 4; t++)
        {
            double sinad, pilot, cpu;
            // 68 kHz of programme + 7 kHz pilot = 75 kHz peak deviation
            double amp = measure(rates[r], tones[t], 68000.0, 7000.0, &sinad, &pilot, &cpu);
            double expect = (68000.0 / 75000.0) / sqrt(1.0 + pow(2 * M_PI * tones[t] * 75e-6, 2));
            double err_db = 20 * log10(amp / expect);
            printf("rate %6d tone %5.0f Hz: level %.3f (de-emphasis error %+.2f dB) SINAD %.1f dB pilot %.1f dB  cpu %.1f%%\n",
                   rates[r], tones[t], amp, err_db, sinad, pilot, cpu * 100);
            if (fabs(err_db) > 0.5 || sinad < 40.0 || pilot > -60.0)
            {
                printf("  ** FAIL\n");
                fail++;
            }
            ref = amp;
        }
    }
    (void)ref;
    printf("%s (%d failures)\n", fail ? "FAILED" : "PASSED", fail);
    return fail ? 1 : 0;
}
