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
 * HackRF One device layer (libhackrf).  Samples from the USB callback go
 * into a ring buffer that the DSP thread empties with device_read().
 * Settings are cached so they can be given before the radio is started.
 */

#ifndef DEVICE_H
#define DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int         device_open(const char *serial);
void        device_close(void);
bool        device_is_open(void);
const char *device_serial(void);
const char *device_board_name(void);
const char *device_firmware(void);
int         device_board_id(void);

int         device_start(void);
void        device_stop(void);

size_t      device_read(int8_t *buf, size_t max_bytes, int timeout_ms);
void        device_flush(void);
unsigned long device_overruns(void);

int         device_set_hw_rate(long rate);
long        device_get_hw_rate(void);
int         device_set_frequency(long long rx_frequency);
long long   device_get_lo(void);
int         device_set_lna_gain(int db);
int         device_set_vga_gain(int db);
int         device_set_rf_amp(bool on);
int         device_set_antenna_power(bool on);
int         device_set_ppm(double ppm);

/* Transmit.  The HackRF is half duplex: device_set_mox(true) stops receive,
 * retunes to the transmit frequency and starts transmitting, pulling
 * samples through fill(); device_set_mox(false) goes back to receive. */
typedef void (*device_tx_fill_fn)(void *user, int8_t *buf, size_t n_complex);

int         device_set_mox(bool on, device_tx_fill_fn fill, void *user);
bool        device_is_transmitting(void);
int         device_set_tx_frequency(long long tx_frequency);
int         device_set_tx_gain(int db);
int         device_set_tx_amp(bool on);

#endif // DEVICE_H
