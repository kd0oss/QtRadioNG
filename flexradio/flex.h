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
 * Command set and constants shared by the flexradio interface program and
 * the QtRadio Flex panel.  Plain C so it can be included from C++.
 *
 * Hardware commands reach flexradio as [index][command][argument...].
 */

#ifndef FLEX_H
#define FLEX_H

enum FLEX_COMMAND_SET {
    FLEXSETRXANT = 1,      // text: ANT1, ANT2, RX_A, RX_B, XVTR
    FLEXSETRFGAIN,         // text, dB (rounded to the radio's rfgain steps)
    FLEXSETMODE,           // text, QtRadio mode name (LSB, USB, AM, FMN, DIGU ...)
    FLEXSETRFPOWER,        // text, 0..100 %
    FLEXSTARTRADIO = 13,   // byte, radio id (same value as hermes.h)
    FLEXSTOPRADIO = 14,    // byte, radio id (same value as hermes.h)
    // Below commands shared in common.h. Do not change without changing there as well.
    FLEXHQHARDWARE = 239,
    FLEXHSTARGETSERIAL = 240,
    FLEXHSTARTBANDSCOPE = 241,
    FLEXHSTOPBANDSCOPE = 242,
    FLEXHUPDATEBANDSCOPE = 243,
    FLEXHSTOPXCVR = 244,
    FLEXHSTARTXCVR = 245,
    FLEXHSETSAMPLERATE = 246,
    FLEXHSETRECORD = 247,
    FLEXHSETFREQ = 248,
    FLEXHATTACHRX = 249,
    FLEXHATTACHTX = 250,
    FLEXHDETACH = 251,
    FLEXHSTARTIQ = 252,
    FLEXHSTOPIQ = 253,
    FLEXHMOX = 254
};

/* DAX IQ rates the radio offers that dspserver can use. */
#define FLEX_RX_RATE_DEFAULT 192000

#endif // FLEX_H
