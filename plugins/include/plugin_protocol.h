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
 * The protocol between dspserver and protocol plugins (M17 and the like).
 *
 * A plugin is a separate program.  It connects to dspserver on TCP port
 * PLUGIN_PORT and everything, control and samples, goes over that one
 * connection as frames:
 *
 *     uint32  length    big endian: bytes that follow (type + stream + payload)
 *     uint8   type      PLUGIN_MESSAGE
 *     int8    stream    dspserver rf stream, -1 for "not stream related"
 *     ...     payload   length - 2 bytes
 *
 * Samples are float32, little endian.  I/Q is interleaved I,Q (not WDSP's
 * Q,I), centred on the dial frequency.  Audio is mono.
 *
 * Start-up:
 *   plugin -> PLG_REGISTER   XML description (see below)
 *   dspserver -> PLG_ACCEPT  "id=<n>" and one "mode=<name>:<number>" per mode
 *
 * A stream switched to one of the plugin's modes gets PLG_START ("rx <mode>"
 * or "tx <mode>"), then PLG_RXIQ blocks (receive) or PLG_MIC blocks while
 * transmitting.  The plugin answers with PLG_AUDIO / PLG_TXIQ.  Output must
 * follow input sample for sample (N input samples -> N * out_rate / in_rate
 * output samples, after a constant delay); dspserver buffers and paces.
 *
 * Transmit: PLG_MOX 1 when keyed.  On PLG_MOX 0 the plugin finishes its
 * transmission (end of stream marker ...), keeps sending PLG_TXIQ while
 * dspserver keeps sending (silent) PLG_MIC blocks, then sends PLG_TXDONE.
 * dspserver unkeys the radio after PLG_TXDONE, or after tx_tail_ms.
 *
 * Panel: settings the user makes arrive as PLG_SETTING "key=value"
 * (also sent once for every value when a QtRadio connects); PLG_STATUS
 * "key=value" lines go to the panel: a status or meter shows the value, a
 * log adds it as a line ("<log key>.clear=1" empties the log).
 *
 * Registration XML:
 *
 *   <plugin name="m17" version="1" title="M17 digital voice">
 *     <mode name="M17" rx_rate="48000" audio_rate="8000" tx_rate="48000"
 *           mic_rate="8000" filter_low="-4500" filter_high="4500"
 *           tx_tail_ms="500"/>
 *     <panel>
 *       <text   key="src"  label="My callsign" max="9" persist="1"/>
 *       <text   key="dst"  label="Destination" default="@ALL" persist="1"/>
 *       <spin   key="can"  label="CAN" min="0" max="15" default="0" persist="1"/>
 *       <combo  key="rate" label="Voice" items="3200,1600" default="3200"/>
 *       <check  key="x"    label="Something" default="0"/>
 *       <button key="clr"  label="Clear"/>
 *       <status key="heard" label="Heard"/>
 *       <meter  key="ber"  label="BER" min="0" max="20" unit="%"/>
 *       <log    key="msg"  label="Messages" lines="5"/>
 *     </panel>
 *   </plugin>
 *
 * Up to PLUGIN_MAX_MODES modes per plugin.  Mode numbers are given out by
 * dspserver from PLUGIN_FIRST_MODE up; QtRadio sends them with SETMODE.
 */

#ifndef PLUGIN_PROTOCOL_H
#define PLUGIN_PROTOCOL_H

#define PLUGIN_PORT          10160
#define PLUGIN_MAX           8
#define PLUGIN_MAX_MODES     4
#define PLUGIN_FIRST_MODE    32       // mode numbers 32 .. 32 + 8 * 4 - 1
#define PLUGIN_MAX_FRAME     (1 << 20)
#define PLUGIN_HEADER        6        // length + type + stream

enum PLUGIN_MESSAGE
{
    // plugin -> dspserver
    PLG_REGISTER = 1,       // XML
    PLG_AUDIO,              // float32 mono, audio_rate
    PLG_TXIQ,               // float32 I,Q, tx_rate
    PLG_STATUS,             // text, "key=value" lines
    PLG_TXDONE,             // transmission finished after PLG_MOX 0

    // dspserver -> plugin
    PLG_ACCEPT = 64,        // text: "id=<n>\nmode=<name>:<number>\n..."
    PLG_RXIQ,               // float32 I,Q, rx_rate
    PLG_MIC,                // float32 mono, mic_rate
    PLG_SETTING,            // text: "key=value"
    PLG_START,              // text: "rx <mode>" or "tx <mode>"
    PLG_STOP,               // stream leaves the plugin's modes
    PLG_MOX                 // 1 byte: 1 keyed, 0 unkeyed
};

/* dspserver -> QtRadio, on the command connection (4 byte header
 * [length hi][length lo][type][arg] + payload), to clients that asked with
 * QUESTION QPLUGINS: */
#define CLIENT_PLUGIN_LIST    78      // arg: number of plugins; XML <plugins>...</plugins>
#define CLIENT_PLUGIN_STATUS  79      // arg: plugin id; text, "stream=<n>" then key=value lines

#endif // PLUGIN_PROTOCOL_H
