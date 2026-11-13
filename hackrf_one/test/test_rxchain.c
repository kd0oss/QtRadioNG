/*
 * Unit test for the hackrf_one receive decimation chain.
 *
 * For every hardware rate / receiver rate pair it checks:
 *   - output sample rate equals the receiver rate
 *   - a tone at LO + IF offset + f appears at +f with the right amplitude
 *   - pass band flatness at 0.4 * rx_rate
 *   - alias rejection for tones that fold onto the pass band at the
 *     CIC and FIR decimation stages, and for the DC spike at the LO
 *   - throughput (Msps processed per second of CPU)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <complex.h>

#include "../rxchain.h"
#include "../hackrf.h"

typedef struct
{
    double *buf;
    size_t  n;
    size_t  cap;
} CAPTURE;


static void capture_cb(void *user, const double *iq, int n)
{
    CAPTURE *c = user;

    if (c->n + (size_t)n > c->cap)
    {
        c->cap = (c->n + (size_t)n) * 2;
        c->buf = realloc(c->buf, c->cap * 2 * sizeof(double));
    }
    memcpy(c->buf + c->n * 2, iq, (size_t)n * 2 * sizeof(double));
    c->n += (size_t)n;
} // end capture_cb


/* Run the chain on int8 input made of tones (frequencies relative to the LO). */
static void run_tones(long hw, long rx, const double *freqs, const double *amps, int ntones,
                      double seconds, CAPTURE *cap, double *cpu_seconds)
{
    RXCHAIN *chain = rxchain_create(hw, rx, HACKRF_IF_OFFSET, capture_cb, cap);
    size_t total = (size_t)(hw * seconds);
    size_t block = 131072;
    int8_t *buf = malloc(block * 2);
    double ph[8] = {0};
    clock_t cpu = 0;

    if (chain == NULL)
    {
        fprintf(stderr, "create failed for %ld/%ld\n", hw, rx);
        exit(1);
    }

    for (size_t done = 0; done < total; done += block)
    {
        size_t n = (total - done < block) ? total - done : block;
        for (size_t k = 0; k < n; k++)
        {
            double i = 0.0, q = 0.0;
            for (int t = 0; t < ntones; t++)
            {
                i += amps[t] * cos(ph[t]);
                q += amps[t] * sin(ph[t]);
                ph[t] += 2.0 * M_PI * freqs[t] / (double)hw;
                if (ph[t] > M_PI)
                    ph[t] -= 2.0 * M_PI;
            }
            buf[k * 2] = (int8_t)lround(fmax(-127.0, fmin(127.0, i)));
            buf[k * 2 + 1] = (int8_t)lround(fmax(-127.0, fmin(127.0, q)));
        }
        clock_t t0 = clock();
        rxchain_process(chain, buf, n);
        cpu += clock() - t0;
    }
    *cpu_seconds = (double)cpu / CLOCKS_PER_SEC;
    free(buf);
    rxchain_destroy(chain);
} // end run_tones


/* Power (dB re full scale) of the component at frequency f, Blackman-Harris windowed. */
static double tone_db(const CAPTURE *c, long rx, double f, size_t skip)
{
    size_t n = c->n - skip;
    double complex acc = 0.0;
    double wsum = 0.0;

    for (size_t k = 0; k < n; k++)
    {
        double x = (double)k / (double)(n - 1);
        double w = 0.35875 - 0.48829 * cos(2 * M_PI * x) + 0.14128 * cos(4 * M_PI * x) - 0.01168 * cos(6 * M_PI * x);
        double complex s = c->buf[(skip + k) * 2] + I * c->buf[(skip + k) * 2 + 1];
        acc += w * s * cexp(-I * 2.0 * M_PI * f * (double)k / (double)rx);
        wsum += w;
    }
    return 20.0 * log10(cabs(acc) / wsum + 1e-300);
} // end tone_db


static double wrap(double f, double fs)
{
    double r = fmod(f, fs);
    if (r > fs / 2)
        r -= fs;
    if (r < -fs / 2)
        r += fs;
    return r;
} // end wrap


int main(void)
{
    static const long hw_rates[] = {8000000, 10000000, 12500000, 16000000, 20000000};
    static const long rx_rates[] = {48000, 96000, 192000, 384000};
    int failures = 0;
    double worst_alias = -999.0;
    double worst_flat = 0.0;
    double worst_rate = 0.0;    // samples
    double min_speed = 1e9;

    printf("%-9s %-7s %4s %5s  %8s %8s %8s %8s %8s %8s %7s\n",
           "hw", "rx", "cic", "taps", "cnt_err", "level", "flat", "cicalias", "firalias", "dc", "Msps");

    for (size_t h = 0; h < sizeof(hw_rates) / sizeof(hw_rates[0]); h++)
    {
        for (size_t r = 0; r < sizeof(rx_rates) / sizeof(rx_rates[0]); r++)
        {
            long hw = hw_rates[h];
            long rx = rx_rates[r];
            double seconds = 0.25;
            double cpu;
            CAPTURE cap = {0};
            RXCHAIN *probe = rxchain_create(hw, rx, HACKRF_IF_OFFSET, capture_cb, &cap);
            int cic = rxchain_cic_decimation(probe);
            int taps = rxchain_fir_taps(probe);
            rxchain_destroy(probe);
            double fs1 = (double)hw / cic;
            double fs2 = fs1 / 4.0;
            double ftest = 0.1 * rx;
            size_t skip = (size_t)(0.02 * rx);

            // reference tone at +0.1*rx, amplitude 100
            double f1[] = {HACKRF_IF_OFFSET + ftest};
            double a1[] = {100.0};
            run_tones(hw, rx, f1, a1, 1, seconds, &cap, &cpu);
            double expect_n = seconds * rx;
            double rate_err = ((double)cap.n - expect_n) / expect_n;
            double ref = tone_db(&cap, rx, ftest, skip);
            double level = ref - 20.0 * log10(100.0 / 128.0);
            double speed = (double)hw * seconds / cpu / 1e6;
            cap.n = 0;

            // flatness at 0.4 * rx
            double f2[] = {HACKRF_IF_OFFSET + 0.4 * rx};
            run_tones(hw, rx, f2, a1, 1, seconds, &cap, &cpu);
            double flat = tone_db(&cap, rx, 0.4 * rx, skip) - ref;
            cap.n = 0;

            // tone that the CIC decimation folds onto +0.1*rx (only if it is inside the hw band)
            double cic_alias = -200.0;
            double fc = HACKRF_IF_OFFSET + fs1 + ftest;
            if (cic > 1 && fabs(fc) < 0.45 * hw)
            {
                double f3[] = {fc};
                run_tones(hw, rx, f3, a1, 1, seconds, &cap, &cpu);
                cic_alias = tone_db(&cap, rx, ftest, skip) - ref;
                cap.n = 0;
            }

            // tone that the FIR decimation folds onto +0.1*rx
            double ff = HACKRF_IF_OFFSET + fs2 + ftest;
            double f4[] = {ff};
            run_tones(hw, rx, f4, a1, 1, seconds, &cap, &cpu);
            double fir_alias = tone_db(&cap, rx, ftest, skip) - ref;
            cap.n = 0;

            // DC spike at the LO: where would it land after all folding?
            double dc_at = wrap(wrap(-HACKRF_IF_OFFSET, fs1), fs2);
            double f5[] = {0.0};
            run_tones(hw, rx, f5, a1, 1, seconds, &cap, &cpu);
            double dc = (fabs(dc_at) < 0.5 * rx) ? tone_db(&cap, rx, dc_at, skip) - ref : -200.0;
            free(cap.buf);

            printf("%-9ld %-7ld %4d %5d  %8.0f %8.2f %8.2f %8.1f %8.1f %8.1f %7.1f\n",
                   hw, rx, cic, taps, rate_err * expect_n, level, flat, cic_alias, fir_alias, dc, speed);

            // output comes in RXCHAIN_BLOCK blocks after a short filter delay
            if (fabs(rate_err * expect_n) > RXCHAIN_BLOCK + 200 || fabs(level) > 0.5 || fabs(flat) > 0.5 ||
                cic_alias > -80.0 || fir_alias > -80.0 || dc > -80.0)
            {
                printf("  ** FAIL\n");
                failures++;
            }
            worst_alias = fmax(worst_alias, fmax(cic_alias, fmax(fir_alias, dc)));
            worst_flat = fmax(worst_flat, fabs(flat));
            worst_rate = fmax(worst_rate, fabs(rate_err * expect_n));
            min_speed = fmin(min_speed, speed);
            if (speed < (double)hw / 1e6 * 1.5)
                printf("  ** warning: less than 1.5x real time\n");
        }
    }

    printf("\nworst alias %.1f dB, worst flatness %.2f dB, worst count error %.0f samples, slowest %.1f Msps\n",
           worst_alias, worst_flat, worst_rate, min_speed);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
} // end main
