# Protocol plugins

A protocol plugin is a separate program that handles one or more modes in
place of WDSP: it gets I/Q from dspserver and returns audio, and gets mic
audio while transmitting and returns I/Q. It also describes a control panel,
which QtRadio builds as a tab next to the hardware panel. Plugins work with
every radio program (hpsdr, hackrf_one, flexradio), like those programs work
with dspserver.

Included:

* **m17_plugin**: M17 digital voice (4FSK, Codec 2 3200), receive and transmit.
* **example_plugin**: narrow band FM outside WDSP, as a template for new plugins.

## Building

The plugins build with the rest of the terminal programs (top-level
`cmake -B build ...`, see ../README.md), or on their own:

    cmake -S plugins -B build-plugins
    cmake --build build-plugins -j

`-DBUILD_PLUGINS=OFF` at the top level leaves them out.

m17_plugin needs two libraries. Without them it is skipped with a warning.

* Codec 2: `sudo apt install libcodec2-dev codec2` (the `codec2` package has
  the `c2enc`/`c2dec` tools used by the tests).
* libm17 from the M17 Project:

      git clone https://github.com/M17-Project/libm17
      cmake -S libm17 -B libm17/build
      cmake --build libm17/build -j
      sudo cmake --install libm17/build && sudo ldconfig

## Running

Start dspserver and the radio program as usual, then the plugins, on the
machine with dspserver or any other:

    m17_plugin [-s dspserver_host] [-p port] [-v]
    example_plugin [-s dspserver_host] [-p port]

The default is localhost, port 10160. `-v` makes m17_plugin log every
transmission it hears to stderr.

A plugin can be started or stopped at any time. Its modes appear in QtRadio's
Mode menu, and its panel appears as a tab. If a plugin stops while a receiver
is in one of its modes, that receiver goes quiet, and it picks up again when
the plugin comes back. If a plugin stops while transmitting, dspserver unkeys
the radio.

At the moment QtRadio receives the plugin list only after you press Start in
the radio dialog it shows on connecting.

### M17

Select mode M17 and tune to the channel's centre frequency (for example
433.475 MHz or 144.390 MHz in your area). On the M17 tab:

* **My callsign**: sent as the source. If it is empty, N0CALL is sent.
* **Destination**: `@ALL` (broadcast) or a callsign or reflector address.
* **Channel access (CAN)**: 0 to 15, sent with every transmission.
* **Receive only my CAN**: mutes transmissions on other CANs.
* **My text (sent with voice)**: up to 52 bytes of text (UTF-8, so fewer
  characters if you use accented letters or symbols), sent in the META
  field with every transmission. Longer text is cut at 52 bytes.
* **Audio AGC**: on by default. Codec 2 gives back whatever level the
  sender's microphone had, which is often far below WDSP's FM. The AGC
  brings voice peaks to about the level of FM at full deviation, quiet and
  loud stations alike (up to 30 dB of gain).
* **Audio gain, AGC off (dB)**: a fixed gain used when the AGC is off.
* **Heard / Stream / Text / Signal / Bit errors**: who is transmitting, the
  stream type, the station's text, and the decoder's lock and bit error
  rate. The text stays until another station is heard.
* **Log**: one line per transmission heard, and one per text received.

Text uses the "Text Data" META format of the current M17 specification
(v2.0): up to four blocks of 13 bytes, one block per superframe (240 ms),
so a full 52 byte message takes about a second to arrive. Text that has not
fully arrived when a transmission ends is logged with "..." for the
missing blocks. The 3.0 draft specification changes the TYPE field and the
text format; it is not supported, as the M17 Project has not adopted it.

Transmissions are stream mode voice at Codec 2 3200. Encrypted streams are
muted, and packet mode (SMS and so on) is not decoded yet. Settings are
saved by QtRadio and sent to the plugin when QtRadio connects.

With flexradio, M17 transmits through the radio's DFM mode (direct FM).
This has not been tested on a real radio yet.

## Tests

`ctest` in the build directory runs:

* **m17_plugin_reference** (test/test_m17.py): m17_plugin against the M17
  Project's reference `m17-coder-sym`/`m17-decoder-sym` (from
  https://github.com/M17-Project/M17_Implementations, SP5WWP). It checks
  receive bit-exact against `c2dec` (AGC off), the AGC and the fixed gain
  on a quiet sender, late joining via the LICH, silence on
  noise, META text in both directions (including joining late), and that
  the reference decoder decodes what the plugin transmits.
* **plugins_e2e_example** and **plugins_e2e_m17** (test/e2e_*.py): a whole
  system of dspserver, hackrf_one_mock and the plugin, run by a Python
  client in place of QtRadio. These need the top-level build with
  `-DHACKRF_ONE_MOCK=ON`. They use dspserver's usual ports, so nothing else
  may be running dspserver or a radio program. The first run spends a few minutes creating
  WDSP's wisdom file in `test-work/` in the build directory.

The M17 tests are registered only when `m17-coder-sym`, `m17-decoder-sym`,
`c2enc` and `c2dec` are found. If the reference tools are not on the PATH,
pass `-DM17_CODER=/path/m17-coder-sym -DM17_DECODER=/path/m17-decoder-sym`.

## Writing a plugin

The protocol is documented in include/plugin_protocol.h. lib/plugin_client.h
does the connection part, and example/example.c is a complete plugin to start
from.

1. Write the registration XML: one `<mode>` per mode (up to 4) and a
   `<panel>`. Each mode gives the sample rates the plugin wants: `rx_rate`
   (I/Q in), `audio_rate` (audio out), `mic_rate` (mic in), `tx_rate` (I/Q
   out). It also gives the passband (`filter_low`, `filter_high`, used for
   the display and WDSP's front end) and `tx_tail_ms`, the longest time
   dspserver waits for the plugin to finish after unkeying.
2. `plugin_open(host, port, xml)`, then `plugin_run()` with a
   `PLUGIN_CALLBACKS` table:
   * `accept`: registered; the text gives the plugin id and mode numbers.
   * `start` / `stop`: a stream switched into or out of one of your modes.
   * `rxiq`: I/Q at `rx_rate`, interleaved I,Q, centred on the dial
     frequency. Answer with `plugin_send_audio()` at `audio_rate`.
   * `mic`: mic audio at `mic_rate` while keyed. Answer with
     `plugin_send_txiq()` at `tx_rate`.
   * `mox`: keyed / unkeyed. After unkeying, keep answering `mic` blocks
     (they are silent) until your transmission is finished, then call
     `plugin_send_txdone()`.
   * `setting`: a panel value changed (`key`, `value`). All values are also
     sent once when QtRadio connects.
3. Output must keep pace with input: N input samples give
   N × out_rate / in_rate output samples, after a fixed delay. dspserver
   buffers and paces, but cannot make up samples you do not send.
4. Use `plugin_status(conn, stream, "key=%s", ...)` to show values on the
   panel. A `status` or `meter` element shows the value, and a `log`
   element adds a line (`<logkey>.clear=1` empties it).

Panel elements:

| element  | attributes                          | sends / shows                |
|----------|-------------------------------------|------------------------------|
| `text`   | key, label, max, default, persist   | sends the text when edited   |
| `spin`   | key, label, min, max, default, persist | sends the number          |
| `combo`  | key, label, items (a,b,c), default, persist | sends the item       |
| `check`  | key, label, default, persist        | sends 0 / 1                  |
| `button` | key, label                          | sends `key=1`                |
| `status` | key, label                          | shows the value              |
| `meter`  | key, label, min, max, unit          | shows the value as a bar     |
| `log`    | key, label, lines                   | adds each value as a line    |

`persist="1"` makes QtRadio remember the value between sessions.

Add the plugin to CMakeLists.txt next to example_plugin, linking
`plugin_client`.
