/*
 * Unit test for the hackrf_one transmit interpolation chain.
 * Feeds a 192 ksps tone (+1 kHz, amplitude 0.5) and writes the int8 output
 * for each hardware rate to /tmp/txchain_<rate>.raw for test_txchain.py,
 * and reports the speed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "../txchain.h"
#include "../hackrf.h"

typedef struct
{
    double phase;
    double freq;
    long   rate;
} TONE;

static long tone_source(void *user, float *iq, long max_frames)
{
    TONE *t = user;
    for (long k = 0; k < max_frames; k++)
    {
        iq[k * 2] = (float)(0.5 * cos(t->phase));
        iq[k * 2 + 1] = (float)(0.5 * sin(t->phase));
        t->phase += 2.0 * M_PI * t->freq / (double)t->rate;
        if (t->phase > M_PI)
            t->phase -= 2.0 * M_PI;
    }
    return max_frames;
}

int main(void)
{
    static const long hw_rates[] = {8000000, 10000000, 12500000, 16000000, 20000000};

    for (size_t h = 0; h < sizeof(hw_rates) / sizeof(hw_rates[0]); h++)
    {
        long hw = hw_rates[h];
        TONE tone = {0.0, 1000.0, 192000};
        TXCHAIN *c = txchain_create(hw, 192000, HACKRF_IF_OFFSET, tone_source, &tone);
        size_t n = (size_t)(hw / 4);             // 0.25 s
        int8_t *out = malloc(n * 2);
        clock_t t0 = clock();
        for (size_t done = 0; done < n; done += 131072)
            txchain_run(c, out + done * 2, (n - done < 131072) ? n - done : 131072, 0.0);
        double cpu = (double)(clock() - t0) / CLOCKS_PER_SEC;
        char fn[64];
        snprintf(fn, sizeof(fn), "/tmp/txchain_%ld.raw", hw);
        FILE *f = fopen(fn, "wb");
        fwrite(out, 1, n * 2, f);
        fclose(f);
        printf("%ld: cic x%d fir %d taps, %.1f Msps (%.1fx real time)\n", hw, txchain_cic_interpolation(c),
               txchain_fir_taps(c), (double)n / cpu / 1e6, (double)n / cpu / (double)hw);
        free(out);
        txchain_destroy(c);
    }
    return 0;
}
