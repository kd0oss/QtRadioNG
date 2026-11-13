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
 * The FLEX radio as a DAX IQ source: one panadapter whose DAX IQ channel
 * streams to us; tuning moves the panadapter centre.
 */

#ifndef RADIO_H
#define RADIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "api.h"

typedef struct _radio_options
{
    const char *address;       // radio IP, NULL to use discovery
    const char *serial;        // pick this radio when discovering
    int         daxiq_channel; // 1..4 (8000 series: more)
    int         udp_port;      // local UDP port for VITA-49, 0 = any
    bool        swap_iq;       // the radio's I/Q are the other way round
} RADIO_OPTIONS;

/* Called from the UDP thread with n complex samples, I,Q interleaved, full scale 1.0. */
typedef void (*radio_iq_fn)(void *user, const double *iq, int n, int rate);

bool radio_open(const RADIO_OPTIONS *options, radio_iq_fn iq, void *user);
void radio_close(void);
bool radio_is_open(void);
const FLEX_RADIO_INFO *radio_info(void);

int  radio_set_frequency(long long hz);
int  radio_set_rate(int hz);
int  radio_get_rate(void);
int  radio_set_antenna(const char *ant);
int  radio_set_rfgain(int db);
void radio_stats(unsigned long *packets, unsigned long *lost);

/* Transmit: the radio takes 24 kHz audio on a DAX TX stream ("transmit
 * set dax=1"); our slice is the TX slice. */
int      radio_tx_open(void);
void     radio_tx_close(void);
uint32_t radio_tx_stream(void);
int      radio_set_mox(bool on);
int      radio_set_mode(const char *mode);      // radio mode name: USB, LSB, AM, FM, DIGU, DIGL
int      radio_set_rfpower(int percent);
int      radio_send_dax_tx(const uint8_t *packet, size_t len);
void     radio_power(float *fwd_watts, float *ref_watts);

#endif // RADIO_H
