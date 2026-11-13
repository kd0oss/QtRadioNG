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
 * FLEX radios transmit audio, not IQ.  This turns dspserver's TX IQ
 * (192 ksps, baseband, I,Q) back into 24 kHz audio for the radio's own
 * modulator:
 *
 *   SSB, CW, digital  the real part.  For a one-sided signal that is the
 *                     original audio; the radio's slice mode picks the
 *                     sideband again.
 *   AM                the real part less its DC (the carrier); the radio
 *                     adds its own carrier.
 *   FM                a frequency discriminator, 5 kHz deviation = 0.9.
 */

#ifndef TXAUDIO_H
#define TXAUDIO_H

#define TXA_IN_RATE   192000
#define TXA_OUT_RATE  24000
#define TXA_DECIM     (TXA_IN_RATE / TXA_OUT_RATE)
#define TXA_TAPS      96
#define TXA_FM_DEV    5000.0

typedef enum
{
    TXA_SSB = 0,
    TXA_AM,
    TXA_FM,
    TXA_OFF                       // a mode we cannot transmit: silence
} TXA_KIND;

typedef struct _txaudio
{
    TXA_KIND kind;
    float    taps[TXA_TAPS];
    float    hist[TXA_TAPS * 2];  // doubled so a window is always contiguous
    int      pos;
    int      phase;               // input samples since the last output
    double   last_i, last_q;      // FM discriminator
    double   dc;                  // AM carrier estimate
} TXAUDIO;

void txaudio_init(TXAUDIO *t, TXA_KIND kind);
void txaudio_set_kind(TXAUDIO *t, TXA_KIND kind);

/* n complex samples, I,Q interleaved, in; up to n / TXA_DECIM + 1 audio
 * samples out.  Returns the number written. */
int  txaudio_run(TXAUDIO *t, const float *iq, int n, float *audio);

/* QtRadio mode name to the radio's mode name and our kind.  Returns 0, or
 * -1 for a mode the radio cannot transmit. */
int  txaudio_mode(const char *qtradio, const char **flex, TXA_KIND *kind);

#endif // TXAUDIO_H
