# hackrf_one

HackRF One interface program for dspserver. It plays the same part as `hpsdr`:
it connects to dspserver's hardware port, sends receiver IQ and bandscope
data and takes transmit IQ over the same UDP streams.

## Build

You need CMake 3.16 or later, `libhackrf` (with its headers, e.g.
`libhackrf-dev`) and `libsamplerate`.

    cd hackrf_one
    cmake -B build
    cmake --build build
    sudo cmake --install build     # optional, copies to /usr/local/bin

The program is `build/hackrf_one`. It is also built by the top-level
`CMakeLists.txt`, together with the other programs.

## Run

Start dspserver first, then:

    hackrf_one [-s serial] [-r radio_id] [-f bandscope_fps] [dspserver_address]

* `-s serial`: open a particular HackRF (serial number or a suffix of it).
* `-r radio_id`: the radio's id in its manifest (default 0). dspserver gives
  every radio its own number anyway, so this need not differ from other radio
  programs' ids (see "Several radios" in ../README.md).
* `-f fps`: bandscope snapshots per second (default 15).
* `-l seconds`: log the ADC input level (DC removed) and clipping this often.
  Without it the level is logged only when it moves by 3 dB or clips.
* `dspserver_address`: default 127.0.0.1.

If dspserver is not running yet, hackrf_one waits for it. It also waits again
if dspserver goes away. Ctrl-C stops it cleanly.

In QtRadio the radio shows up as `hackrf_one`, and the HackRF One panel has:

* **Hardware Rate**: the HackRF ADC rate, 8–20 MHz. This is also the bandscope width.
* **Receiver Rate**: the rate dspserver/WDSP gets, 48–384 kHz.
* **Gain**: LNA 0–40 dB in 8 dB steps, VGA 0–62 dB in 2 dB steps, the RF amp
  (+11 dB) and antenna port bias power.
* **Frequency Correction**: reference error in ppm.
* **TX tab**: transmit VGA gain (0–47 dB, starts at 0) and the RF amp while
  transmitting.

The HackRF is half duplex. On MOX hackrf_one stops receiving, retunes to the
transmit frequency and transmits; on MOX off it goes back to receive.

**Before transmitting:** the HackRF has no output filter and its harmonics
are strong. Put a band pass or low pass filter between it and any antenna or
amplifier, and stay within your licence.

Settings are saved in the `HackrfOne` group of the QtRadio settings.

## How it works

    HackRF int8 IQ at 8-20 Msps
      -> NCO mix: the receiver sits HACKRF_IF_OFFSET (500 kHz) above the LO, clear of the DC spike
      -> CIC order 5, decimate by N
      -> FIR (Kaiser, 90 dB) decimate by 4
      -> libsamplerate to the exact receiver rate
      -> UDP to dspserver (BUFFERL, 512 complex samples per packet)

N is chosen so that the FIR output is at least twice the receiver rate. The
unit test (`ctest --test-dir build`) checks every hardware rate against every receiver rate
for these things:

* the output rate;
* the tone position and level;
* flatness to 0.4 × the receiver rate (better than 0.2 dB);
* alias and DC spike rejection (better than 86 dB);
* speed (more than 1.5× real time; at 20 Msps it is usually 5–10×).

Transmit runs the same idea backwards:

    TX IQ from dspserver, 192 ksps
      -> FIFO (100 ms; the resampling ratio is trimmed to hold it there, since
         the mic audio and the HackRF run on different clocks)
      -> libsamplerate -> FIR x4 -> CIC order 5, xN -> NCO up 500 kHz -> int8 IQ at the hardware rate

The LO sits 500 kHz below the transmit frequency, so its leakage is outside
the signal. dspserver's transmit thread only runs when the radio sends "mic"
packets (hpsdr sends them with the Hermes mic samples), so hackrf_one sends
empty ones every 5 ms while the transmitter is attached; the power meter
reads 0 because the HackRF has no power detector.

The bandscope takes 16384 samples from the full-rate stream `fps` times a
second. It sends them as complex frames: two BUFFERWB packets with
`WB_CHUNK_COMPLEX` set in `chunk`. dspserver sees that flag and runs its
analyzer over the whole −fs/2…+fs/2 span. The span is centred on the LO,
which is the receiver frequency minus 500 kHz. QtRadio labels the axis from
that and draws a red line at the receiver frequency.

WDSP takes interleaved samples as Q,I. hackrf_one sends receiver IQ in that
order. The bandscope frames are plain I,Q, and dspserver swaps them.

## Testing without hardware

    cmake -B build -DHACKRF_ONE_MOCK=ON
    cmake --build build
    ./build/hackrf_one_mock 127.0.0.1

`hackrf_one_mock` links a simulated libhackrf (`test/mock_hackrf.c`). It
streams in real time and produces carriers at absolute frequencies
(`MOCK_HACKRF_TONES="14200000:60,14260000:20"`) plus a DC spike
(`MOCK_HACKRF_DC=8`). Tuning therefore behaves like a real radio.

## Limitations (v1)

* One receiver and one transmitter, half duplex.
* No forward/reflected power: the meter shows 0 W and SWR 0.0.
* 8 bit samples: transmit spurs are about 60 dB below full output.
* FMW is receive only; keyed in FMW the transmitter sends narrow FM.
* Every frequency change retunes the HackRF LO, which can cause a short click.
  The bandscope follows the receiver.
* The ppm correction corrects the tuning. The small matching error in the
  sample rate is not corrected.
