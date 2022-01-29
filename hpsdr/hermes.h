/* Copyright (C)
* 2021 - Rick Schnicker KD0OSS
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

#ifndef HERMES_H
#define HERMES_H

enum HCOMMAND_SET {
    SETPREAMP = 1,
    SETMICBOOST,
    SETPOWEROUT,
    SETRXANT,
    SETDITHER,
    SETRANDOM,
    SETLINEIN,
    SETLINEINGAIN,
    SETTXRELAY,
    SETOCOUTPUT,
    GETADCOVERFLOW,
    SETATTENUATOR,
    STARTRADIO,
    STOPRADIO,

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


#endif
