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
 * Receive decimation chain: HackRF int8 IQ at the hardware rate (8-20 Msps)
 * down to the receiver rate (48-384 ksps) as normalised double IQ.
 *
 *   int8 IQ --> NCO mix (-if_offset) --> CIC order 5, /N1 --> FIR /4 --> libsamplerate --> double IQ
 *     hw_rate                              fs1 = hw_rate/N1      fs2 = fs1/4     rx_rate
 *
 * N1 is chosen so fs2 >= 2 * rx_rate; libsamplerate then makes the exact
 * receiver rate.  Output is delivered in blocks of RXCHAIN_BLOCK complex
 * samples, I/Q interleaved, full scale = 1.0.
 */

#ifndef RXCHAIN_H
#define RXCHAIN_H

#include <stddef.h>
#include <stdint.h>

#define RXCHAIN_BLOCK 512

typedef void (*rxchain_output_fn)(void *user, const double *iq, int n_complex);

typedef struct _rxchain RXCHAIN;

RXCHAIN *rxchain_create(long hw_rate, long rx_rate, long if_offset, rxchain_output_fn output, void *user);
void     rxchain_destroy(RXCHAIN *chain);
void     rxchain_process(RXCHAIN *chain, const int8_t *iq, size_t n_complex);

int      rxchain_cic_decimation(const RXCHAIN *chain);
int      rxchain_fir_taps(const RXCHAIN *chain);
double   rxchain_src_ratio(const RXCHAIN *chain);

/* Kaiser windowed sinc low pass, unity DC gain; edges in cycles/sample.
 * Returns malloc'ed taps (also used by txchain.c). */
float   *rxchain_design_lowpass(double pass, double stop, double atten, int *ntaps);

#endif // RXCHAIN_H
