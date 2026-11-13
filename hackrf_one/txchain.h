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
 * Transmit interpolation chain, the mirror of rxchain:
 *
 *   TX IQ from dspserver (192 ksps) --> libsamplerate --> FIR x4 --> CIC order 5, xN1 --> NCO (+if_offset) --> int8 IQ
 *                                      fs2 >= 2*in      fs1 = 4*fs2     hw_rate
 *
 * The chain pulls its input through a source callback, so it is driven by
 * the HackRF sample clock.  rate_adjust lets the caller trim the resampling
 * ratio to keep its input FIFO level steady (the TX audio comes from a
 * different clock).
 */

#ifndef TXCHAIN_H
#define TXCHAIN_H

#include <stddef.h>
#include <stdint.h>

/* Fill iq (I,Q interleaved, full scale 1.0) with up to max_frames samples at
 * the input rate; return the number written (> 0, pad with zeros if needed). */
typedef long (*txchain_source_fn)(void *user, float *iq, long max_frames);

typedef struct _txchain TXCHAIN;

TXCHAIN *txchain_create(long hw_rate, long in_rate, long if_offset, txchain_source_fn source, void *user);
void     txchain_destroy(TXCHAIN *chain);

/* Produce n_complex int8 IQ samples at the hardware rate.  rate_adjust is a
 * small fraction (e.g. +0.0005) by which to consume input faster. */
void     txchain_run(TXCHAIN *chain, int8_t *out, size_t n_complex, double rate_adjust);

int      txchain_cic_interpolation(const TXCHAIN *chain);
int      txchain_fir_taps(const TXCHAIN *chain);

#endif // TXCHAIN_H
