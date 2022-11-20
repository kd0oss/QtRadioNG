// dsp.h

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
*
*/
#ifndef DSP_H
#define DSP_H

//#include "server.h"

extern float txfwd;
extern float txref;
extern float meter;

extern void runXanbEXT(int8_t ch, double *data);
extern void runXnobEXT(int8_t ch, double *data);
extern int runFexchange0(int8_t ch, double *data, double *dataout);
extern void runSpectrum0(int8_t ch, double *data);
extern int runGetPixels(int8_t ch, float *data);
extern void runRXAGetaSipF1(int8_t ch, float *data, int samples);
extern void process_tx_iq_data(int channel, double *mic_buf, double *tx_IQ);
extern void shutdown_client_rfstreams(struct _client_entry *current_item);
extern void shutdown_wideband_rfstreams(struct _client_entry *item);
extern void wb_destroy_analyzer(int8_t ch);
extern char *dsp_command(struct _client_entry*, unsigned char*);
#endif
