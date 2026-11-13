#!/usr/bin/env python3
"""M17 through the whole system: client -> dspserver -> hackrf_one_mock, with
m17_plugin connected to dspserver.

Receive: the mock's FM station at 144.39 MHz plays a reference M17
transmission (M17 Project coder) on a loop; the client's audio must carry
the decoded voice and the panel status must show the callsigns.
Transmit: speech from the client's mic in mode M17; the mock captures 3 s
of the transmission, which the reference decoder must decode (callsigns,
CAN, frames, Codec 2 bits).

usage: e2e_m17.py <dspserver> <hackrf_one_mock> <m17_plugin> <m17-coder-sym> <m17-decoder-sym>

Needs c2enc and c2dec (codec2) on the PATH.  Working directory: as
e2e_plugin.py.
"""
import os
import signal
import socket
import subprocess
import sys
import threading
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_m17 as m  # noqa: E402
from e2e_plugin import Command, read_audio, QUESTION, STARCOMMAND, SETCLIENT, SETMODE, SETFILTER, \
    SETENCODING, SETRXOUTGAIN, STARTAUDIO, ENABLEAUDIO, QPLUGINS, SETPLUGIN, STARHARDWARE, STARTXCVR, \
    SETSR, SETFREQ, ATTACHRX, ATTACHTX, STARTIQ, MOX, STARTRADIO, SETTXVGAGAIN, WORK, msg  # noqa: E402

FREQ = 144390000
RX_TEXT = "QtRadio M17 test station, EN41"
TX_TEXT = "73 de KD0OSS via QtRadioNG – ✓"
CAPTURE = os.path.join(WORK, "tx_capture_m17.raw")
FMFILE = os.path.join(WORK, "m17_station.f32")
HW_RATE = 10000000
failures = 0


def check(cond, what):
    global failures
    print(("PASS " if cond else "FAIL ") + what, flush=True)
    if not cond:
        failures += 1


def station_file(coder):
    """the reference transmission as FM deviation in Hz at 48 kHz, with gaps"""
    pcm = m.speech(2.0)
    sym, bits = m.reference_transmission(coder, pcm, "KD0OSS", "@ALL", 3, RX_TEXT.encode())
    up = np.zeros(len(sym) * m.SPS)
    up[::m.SPS] = sym
    f = np.convolve(up, m.rrc_taps() * m.SPS) * 800.0
    f = np.concatenate([np.zeros(48000), f, np.zeros(48000)]).astype("<f4")
    f.tofile(FMFILE)
    ref = np.frombuffer(m.c2("c2dec", bits), dtype="<i2").astype(float) / 32768.0
    return ref, len(f) / 48000.0


def mic_speech(sock, pcm8k, stop):
    """the client mic: PCM 16 bit at 48 kHz, 512 samples per packet, real time"""
    x = np.repeat(pcm8k.astype(np.int16), 6)          # 8 kHz -> 48 kHz, held (Codec 2 band limits it)
    t_next = time.time()
    k = 0
    while not stop.is_set():
        blk = x[k: k + 512] if k < len(x) else np.zeros(512, dtype=np.int16)
        if len(blk) < 512:
            blk = np.concatenate([blk, np.zeros(512 - len(blk), dtype=np.int16)])
        k += 512
        sock.sendall(bytes([1, 144, 0, 0, 0, 0]) + blk.astype("<i2").tobytes())   # [stream 1][ISMIC_PCM] header
        t_next += 512 / 48000
        d = t_next - time.time()
        if d > 0:
            time.sleep(d)


def demod_capture(path):
    raw = np.fromfile(path, dtype=np.int8).astype(float)
    x = (raw[0::2] + 1j * raw[1::2]) / 127.0
    n = np.arange(len(x))
    x = x * np.exp(-2j * np.pi * 500000 * n / HW_RATE)      # TX at FREQ, LO 500 kHz below
    x = x[: len(x) // 50 * 50].reshape(-1, 50).mean(axis=1)  # 200 kHz
    f = np.angle(x[1:] * np.conj(x[:-1])) * 200000 / (2 * np.pi)
    f = np.convolve(f, np.ones(4) / 4, mode="same")
    t48 = np.arange(0, len(f) / 200000, 1 / 48000)
    f48 = np.interp(t48, np.arange(len(f)) / 200000, f)
    iq = np.exp(1j * 2 * np.pi * np.cumsum(f48) / 48000)      # back to 48 kHz I/Q for the same demodulator
    # matched filter, then symbol timing tracked per 40 ms (the HackRF trims
    # its sample clock to hold its FIFO level, so the symbol rate drifts a little)
    f = np.angle(iq[1:] * np.conj(iq[:-1])) * 48000 / (2 * np.pi) / 800.0
    y = np.convolve(f, m.rrc_taps())
    levels = np.array([-3, -1, 1, 3])
    out, pos, win = [], 0.0, 192
    while pos + (win + 2) * m.SPS < len(y):
        best = None
        for d in np.arange(-5, 5.01, 0.5):
            idx = pos + d + m.SPS * np.arange(win)
            s = np.interp(idx, np.arange(len(y)), y)
            e = np.mean(np.min(np.abs(s[:, None] - levels[None, :]), axis=1))
            if best is None or e < best[0]:
                best = (e, d, s)
        out.append(best[2])
        pos += best[1] + m.SPS * win
    s = np.concatenate(out)
    err = np.mean(np.min(np.abs(s[:, None] - levels[None, :]), axis=1))
    return s.astype("<f4"), err


def main():
    dsp_exe, mock_exe, plugin_exe, coder, decoder = sys.argv[1:6]
    ref, period = station_file(coder)
    if os.path.exists(CAPTURE):
        os.remove(CAPTURE)
    logs = {k: open(os.path.join(WORK, f"e2e_m17_{k}.log"), "w") for k in ("dsp", "mock", "plugin")}
    dsp = subprocess.Popen([dsp_exe], cwd=WORK, stdout=logs["dsp"], stderr=logs["dsp"])
    for _ in range(100):
        try:
            socket.create_connection(("127.0.0.1", 8000)).close()
            break
        except OSError:
            time.sleep(0.1)
    env = dict(os.environ, MOCK_HACKRF_FM=f"{FREQ}:60:0:0", MOCK_HACKRF_FMFILE=f"{FMFILE}:48000",
               MOCK_HACKRF_TX_CAPTURE=f"{CAPTURE}:0.6:3.0")
    mock = subprocess.Popen([mock_exe, "127.0.0.1"], env=env, stdout=logs["mock"], stderr=logs["mock"])
    plugin = subprocess.Popen([plugin_exe, "-v"], stdout=logs["plugin"], stderr=logs["plugin"])
    time.sleep(2.0)
    try:
        run(ref, period, decoder)
    finally:
        for p in (plugin, mock):
            p.send_signal(signal.SIGINT)
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
        dsp.kill()
        dsp.wait()
    print("FAILED" if failures else "ALL PASSED", f"({failures} failures)")
    sys.exit(1 if failures else 0)


def run(ref, period, decoder):
    c = Command()
    c.send(0, QUESTION, bytes([STARHARDWARE]))
    c.wait(77)
    c.send(0, QUESTION, bytes([QPLUGINS]))
    r = c.wait(78)
    check(r is not None and 'name="M17"' in r[1] and 'first_mode="32"' in r[1], "dspserver lists the M17 plugin (mode 32)")
    c.send(0, STARCOMMAND, bytes([STARTRADIO, 0]))
    time.sleep(0.5)
    c.send(0, SETCLIENT, b"QtRadio")
    for stream, attach in ((0, ATTACHRX), (1, ATTACHTX)):
        c.send(stream, STARTXCVR)
        c.send(stream, STARTIQ)
        c.send(stream, STARCOMMAND, bytes([attach]))
        time.sleep(0.5)
    c.send(0, SETFREQ, str(FREQ).encode())
    c.send(0, SETSR, b"192000")
    c.send(1, SETFREQ, str(FREQ).encode())
    c.send(0, STARCOMMAND, bytes([SETTXVGAGAIN]) + b"20")
    c.send(0, SETENCODING, bytes([1, 1]))
    c.send(0, STARTAUDIO, b"800,48000,1,0,0")
    c.send(0, ENABLEAUDIO, bytes([1]))
    c.send(0, SETRXOUTGAIN, b"50")
    a = socket.create_connection(("127.0.0.1", 8010))
    t0 = time.time()
    c.send(0, SETMODE, bytes([32]))
    c.send(1, SETMODE, bytes([32]))
    c.send(0, SETFILTER, b"-4500,4500")
    time.sleep(0.5)

    # receive two loops of the station
    x = read_audio(a, 2 * period + 0.5)
    r = c.wait(79, lambda a_, t: "heard=KD0OSS" in t, since=t0)
    check(r is not None, f"panel status: {[l for l in r[1].splitlines() if l.startswith('heard=')] if r else None}")
    r = c.wait(79, lambda a_, t: "text=" + RX_TEXT in t, since=t0)
    check(r is not None, f"panel status: META text \"{RX_TEXT}\"")
    # client audio is 48 kHz: compare 8 kHz versions (Codec 2 is band limited)
    x8 = x[: len(x) // 6 * 6].reshape(-1, 6).mean(axis=1) / 32768.0
    lag, corr = m.best_alignment(x8, ref)
    print(f"  client audio against c2dec of the transmitted bits: correlation {corr:.3f}")
    check(corr > 0.9, "M17 voice reaches the client")
    pk = np.max(np.abs(x)) / 32768.0
    check(0.4 < pk <= 1.0, f"voice level at the client, volume 50 %: peak {pk:.2f} (WDSP FM: about 0.5 at 2.5 kHz deviation)")
    plog = open(os.path.join(WORK, "e2e_m17_plugin.log")).read()
    check(plog.count("KD0OSS -> @ALL CAN 3") >= 2, "each loop of the station decoded")

    # transmit
    for kv in (b"src=KD0OSS", b"dst=W1AW", b"can=5", ("txtext=" + TX_TEXT).encode()):
        c.send(0, SETPLUGIN, bytes([0]) + kv)
    time.sleep(0.3)
    pcm = m.speech(4.0, seed=8)
    mic = socket.create_connection(("127.0.0.1", 8020))
    stop = threading.Event()
    th = threading.Thread(target=mic_speech, args=(mic, pcm, stop))
    th.start()
    c.send(1, MOX, b"1")
    time.sleep(4.0)
    t_off = time.time()
    c.send(1, MOX, b"0")
    stop.set()
    th.join()
    r = c.wait(79, lambda a_, t: "end of transmission" in t, since=t_off, timeout=3)
    check(r is not None, "the plugin ended the transmission (EOS frame, EOT)")
    time.sleep(1.5)
    check("Unkeyed tx channel" in open(os.path.join(WORK, "e2e_m17_dsp.log")).read(), "dspserver unkeyed after the plugin's tail")
    check(os.path.exists(CAPTURE), "transmission captured")
    if os.path.exists(CAPTURE):
        sym, dist = demod_capture(CAPTURE)
        print(f"  captured {os.path.getsize(CAPTURE) // 2 / HW_RATE:.1f} s, symbol error {dist:.3f}")
        out = subprocess.run([decoder, "-c", "-m"], input=sym.tobytes(), capture_output=True, timeout=20).stdout.decode()
        lines = [l for l in out.splitlines() if l.strip()]
        fns = [l for l in lines if l.startswith("FN:")]
        lsf = [l for l in lines if "DST:" in l]
        print(f"  reference decoder: {len(fns)} stream frames; {lsf[0].strip() if lsf else 'no link setup'}")
        check(len(fns) >= 60, "about 3 s of stream frames decoded by the reference decoder")
        check(any("DST: W1AW" in l and "SRC: KD0OSS" in l and "CAN: 5" in l for l in lsf),
              "callsigns and CAN from the panel settings (from the LICH)")
        metas = {bytes.fromhex(l.split("META:")[1].split()[0]) for l in lsf if "META:" in l}
        check(metas == set(m.meta_text_blocks(TX_TEXT.encode())), f"META text blocks on the air: {len(metas)}")
        nums = [int(l[4:8], 16) & 0x7FFF for l in fns]
        check(all(b - a == 1 for a, b in zip(nums, nums[1:])), "frame numbers consecutive (no gaps or repeats)")
    a.close()


if __name__ == "__main__":
    main()
