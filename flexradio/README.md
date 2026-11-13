# flexradio

FLEX-6000 / FLEX-8000 / Aurora interface program for dspserver. It does for
these radios what `hpsdr` does for Hermes. It takes a DAX IQ stream from the
radio over the SmartSDR TCP/IP API and passes it to dspserver, where WDSP
demodulates it. It also transmits: see "Transmit" below.

## Build

You need CMake 3.16 or later. No extra libraries are needed.

    cd flexradio
    cmake -B build
    cmake --build build
    sudo cmake --install build     # optional, copies to /usr/local/bin

The program is `build/flexradio`. It is also built by the top-level
`CMakeLists.txt`, together with the other programs.

## Run

Start dspserver, then:

    flexradio [-a radio_address] [-s serial] [-c daxiq_channel] [-u udp_port] [-r radio_id] [-x] [dspserver_address]

* Without `-a`, it listens for the radio's discovery broadcast (UDP 4992)
  and uses the first radio it hears, or the one whose serial contains `-s`.
* `-c`: the DAX IQ channel to use (default 1). Pick one that SmartSDR/DAX
  isn't using.
* `-u`: the local UDP port the radio streams to (default: any free port). Set
  it if a firewall needs a fixed port.
* `-x`: swap I and Q. Use this if signals appear mirrored around the
  receiver frequency, or USB and LSB come out swapped.

In QtRadio the radio appears as `flex6000`. The panel has these controls:

* **Receiver Rate:** the DAX IQ rate (48/96/192 kHz).
* **Antenna:** ANT1, ANT2, RX_A or RX_B.
* **RF gain:** rounded to the steps your radio reports.
* **RF power:** the radio's power setting, 0–100 %.

## What it does on the radio

1. Connects to TCP 4992 and registers its UDP port (`client udpport`).
2. Becomes a GUI client (`client gui`), because only GUI clients can create
   panadapters. Under multiFLEX, SmartSDR can stay connected. On radios
   limited to one GUI client, close SmartSDR first.
3. Enables keepalive and pings once a second.
4. Creates a panafall and gives it the DAX IQ channel (`display pan set …
   daxiq_channel=N`).
5. Creates the IQ stream (`stream create type=dax_iq`) and sets its rate
   (`stream set … daxiq_rate=`).
6. **Tuning:** moves the panadapter centre, because DAX IQ is centred on it.
   The slice the radio creates with the panadapter is kept on the same
   frequency and muted.
7. **On exit:** removes the stream and the panadapter.

## Transmit

Flex radios can't take I/Q for transmit, only audio. So flexradio turns
dspserver's transmit I/Q (WDSP's output, 192 ksps) back into 24 kHz audio,
and the radio modulates it again in the same mode on its own slice:

| QtRadio mode | Radio mode | Audio recovered from the I/Q |
|---|---|---|
| USB, LSB, DSB | USB, LSB, USB | real part |
| CWU, CWL | DIGU, DIGL | real part (the CW tone) |
| DIGU, DIGL | DIGU, DIGL | real part |
| AM, SAM | AM | real part less the carrier |
| FMN, FMW | FM | frequency discriminator, 5 kHz deviation = 0.9 |

QtRadio sends the mode to flexradio whenever it changes. Other modes (SPEC,
DRM) transmit silence.

When QtRadio attaches the transmitter, flexradio does the following:

1. Creates a DAX TX stream (`stream create type=dax_tx`) and routes it to the
   transmitter (`transmit set dax=1`).
2. Makes its slice the TX slice (`slice set N tx=1 mode=…`).
3. Subscribes to the meters (`sub meter all`).
4. Keys with `xmit 1` / `xmit 0`.
5. While keyed, sends DAX TX packets to the radio's UDP port 4991. Each packet
   holds 128 stereo samples, 32-bit float, big endian.

The radio's forward and reflected power meters go back to QtRadio's meter.
When the transmitter detaches, flexradio sets `transmit set dax=0` and
removes the stream.

Things to know:

* **The radio's own transmit processing still applies.** That includes its
  TX filter, EQ, compressor and FM pre-emphasis. WDSP has already done this
  processing, so turn the radio's versions off or down in SmartSDR. For FM
  in particular, use pre-emphasis in one place only.
* **Drive level** is set by the audio level, the radio's DAX TX gain and RF
  power.
* **Transmit frequency.** The radio transmits on its slice, which follows the
  receiver. A separate frequency for the transmit stream is ignored, so
  there is no split.

DAX IQ samples are 32-bit floats, little endian, I then Q, normalised to ±1.
The radio reports this as `payload_endian=little`, and flexradio switches to
big endian if the radio says so. If a sample beyond ±2 arrives, it assumes the
older ±32768 scaling and rescales from then on.

## Testing without a radio

    ctest --test-dir build

This runs `test/flex_sim.py`, a simulated FLEX-6600. The simulator provides
discovery, the TCP API with status messages, and real-time DAX IQ with
carriers at absolute frequencies. `test/test_flexradio.py` drives flexradio
through it the way dspserver does (it needs Python 3 with numpy). The test
covers receive and transmit. On transmit, it checks the DAX TX packet format,
rate and audio in each mode, and the power meters.

## Not tested on a real radio

This was written from the SmartSDR API documentation and open-source
clients, without hardware. Things to check first on a real radio:

* Signals sit on the right side of the receiver frequency (otherwise use `-x`).
* The rate change takes effect: the log shows `stream … daxiq_rate=`.
* With SmartSDR running, the DAX IQ channel isn't taken by DAX.
* On transmit, the radio accepts the DAX TX packets. The log shows
  `DAX TX stream 0x…`, and after unkeying a count of packets sent. Check
  that SmartSDR's TX meter shows DAX audio while keyed.
* `transmit set dax=1` is accepted in voice modes. If voice modes stay
  silent, try DIGU/DIGL.

The log shows every command sent (`> C…`) and every error reply.

## Limitations

* Transmit goes through the radio's modulator (see "Transmit" above), so
  there is no PureSignal and no split.
* One receiver, up to 192 kHz wide.
* No bandscope yet. The radio's panadapter FFT could feed it later.
