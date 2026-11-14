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

#ifndef TX_H
#define TX_H

#include <stdbool.h>
#include <stdint.h>
#include "qtr_net.h"

/* ipaddr goes in the MIC_BUFFER packets and must equal the manifest's <ip_address>. */
const char *tx_attach(int8_t index, struct in_addr dsp_address, int radio_id, const char *ipaddr);
const char *tx_set_mox(bool on);
const char *tx_set_mode(const char *qtradio_mode);
const char *tx_detach(void);
bool        tx_is_attached(void);
int8_t      tx_index(void);

#endif // TX_H
