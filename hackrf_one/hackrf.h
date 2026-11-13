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
 * Command set and constants shared by the hackrf_one interface program
 * and the QtRadio HackRF One panel.  Plain C so it can be included from C++.
 *
 * Hardware commands reach hackrf_one as [index][command][argument...].
 * Text arguments are ASCII numbers; on/off arguments are a single byte 0/1.
 */

#ifndef HACKRF_H
#define HACKRF_H

enum HACKRF_COMMAND_SET {
    SETLNAGAIN = 1,       // text, IF (LNA) gain dB: 0..40, step 8
    SETVGAGAIN,           // text, baseband (VGA) gain dB: 0..62, step 2
    SETRFAMP,             // byte, RF amplifier (~11 dB) 0 = off, 1 = on
    SETANTPOWER,          // byte, antenna port bias power 0 = off, 1 = on
    SETHWSAMPLERATE,      // text, HackRF ADC sample rate Hz (HACKRF_HW_RATE_MIN..MAX)
    SETFREQCORR,          // text, reference frequency correction in ppm (e.g. "-3.5")
    SETTXVGAGAIN,         // text, transmit IF (VGA) gain dB: 0..47, step 1
    SETTXRFAMP,           // byte, transmit RF amplifier (~11 dB) 0 = off, 1 = on
    STARTRADIO = 13,      // byte, radio id (same value as hermes.h)
    STOPRADIO = 14,       // byte, radio id (same value as hermes.h)
    // Below commands shared in common.h. Do not change without changing there as well.
    HQHARDWARE = 239,
    HSTARGETSERIAL = 240,
    HSTARTBANDSCOPE = 241,
    HSTOPBANDSCOPE = 242,
    HUPDATEBANDSCOPE = 243,
    HSTOPXCVR = 244,
    HSTARTXCVR = 245,
    HSETSAMPLERATE = 246,
    HSETRECORD = 247,
    HSETFREQ = 248,
    HATTACHRX = 249,
    HATTACHTX = 250,
    HDETACH = 251,
    HSTARTIQ = 252,
    HSTOPIQ = 253,
    HMOX = 254,
    HSTARHARDWARE = 255
};

/* The receiver is tuned this far above the HackRF LO so it stays clear of
 * the DC spike.  The bandscope is centred on the LO, i.e. on
 * receiver frequency - HACKRF_IF_OFFSET. */
#define HACKRF_IF_OFFSET        500000

#define HACKRF_HW_RATE_MIN      8000000
#define HACKRF_HW_RATE_MAX      20000000
#define HACKRF_HW_RATE_DEFAULT  10000000

#define HACKRF_RX_RATE_MIN      48000
#define HACKRF_RX_RATE_MAX      384000
#define HACKRF_RX_RATE_DEFAULT  192000

#define HACKRF_LNA_GAIN_DEFAULT 16
#define HACKRF_VGA_GAIN_DEFAULT 20
#define HACKRF_TX_GAIN_DEFAULT  0     // start at minimum output

/* BUFFERWB.chunk flags used by hackrf_one for its complex bandscope frames.
 * A frame is 16384 complex int16 samples sent as two packets of 8192
 * (I,Q interleaved).  dspserver/hardware.c defines the same values. */
#define WB_CHUNK_COMPLEX        0x8000
#define WB_CHUNK_INDEX_MASK     0x00ff

#endif // HACKRF_H
