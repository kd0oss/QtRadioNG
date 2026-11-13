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
 * Protocol plugins: separate programs (M17 ...) that demodulate and
 * modulate a stream's signal in place of WDSP.  See
 * plugins/include/plugin_protocol.h for the protocol.
 */

#ifndef DSPSERVER_PLUGIN_H
#define DSPSERVER_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "../plugins/include/plugin_protocol.h"

void plugin_init(void);                    // starts listening on PLUGIN_PORT

/* SETMODE.  Returns true when mode is a plugin mode (known or not); then
 * *filter_low / *filter_high are the mode's filter (0 if the plugin is
 * gone).  Called for every mode change, so it also takes a stream out of
 * plugin mode. */
bool plugin_set_mode(int8_t stream, int mode, bool tx, int *filter_low, int *filter_high);
bool plugin_is_mode(int mode);

/* Receive: in the IQ thread after fexchange0.  iq_qi is the receiver's
 * I/Q in WDSP order (Q,I), n complex samples at rate; out_lr gets n stereo
 * audio samples at rate (same layout as WDSP's output). */
bool plugin_rx_active(int8_t stream);
void plugin_rx_process(int8_t stream, const double *iq_qi, int n, int rate, double *out_lr);
void plugin_set_rx_gain(int8_t stream, double gain);

/* Transmit: in the TX thread.  mic_lr: n stereo mic samples at 48 kHz;
 * tx_qi gets nout complex samples (WDSP order) at 192 kHz. */
bool plugin_tx_active(int8_t stream);
void plugin_tx_process(int8_t stream, const double *mic_lr, int n, double *tx_qi, int nout);
void plugin_set_mic_gain(int8_t stream, double gain);

/* MOX on a stream in a plugin mode.  Returns true when unkeying is
 * deferred: the plugin is sending its tail; the TX thread then calls
 * plugin_tail_tick() while no mic audio arrives and plugin_tail_done()
 * after each block. */
bool plugin_mox(int8_t stream, bool on);
bool plugin_tail_pending(int8_t stream);
bool plugin_tail_tick(int8_t stream, int n, double *tx_qi, int nout);
bool plugin_tail_done(int8_t stream);

/* QtRadio side: QUESTION QPLUGINS subscribes a client and sends it the
 * plugin list; SETPLUGIN forwards "key=value" to a plugin. */
struct bufferevent;
void plugin_send_list(struct bufferevent *bev);
void plugin_client_setting(int8_t stream, int plugin_id, const char *text);

#endif // DSPSERVER_PLUGIN_H
