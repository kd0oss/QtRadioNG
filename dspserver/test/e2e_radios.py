#!/usr/bin/env python3
"""
Two radios on one dspserver, as with hpsdr and hackrf_one at the same time.

Two hackrf_one_mock programs connect, both calling their radio 0 (as hpsdr's
first board and hackrf_one do).  Each sends a different FM tone on a
different frequency.  A client (in place of QtRadio) must see two radios,
start each by its own id, hear each tone on its own stream only, and transmit
on the second radio without the first one transmitting.  Then the second
radio program is restarted: it must come back as the same radio.

Usage: e2e_radios.py <dspserver> <hackrf_one_mock>

dspserver runs in the current directory (or $E2E_WORKDIR), where it keeps
its WDSP wisdom file: the first run there spends a few minutes creating it.
Uses dspserver's usual ports: nothing else may be running dspserver or a
radio program.
"""

import os
import socket
import subprocess
import sys
import threading
import time

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "plugins", "test"))
from e2e_plugin import (Command, check, read_exact, tone_search, fm_demod_capture,  # noqa: E402
                        QUESTION, STARCOMMAND, SETCLIENT, SETMODE, SETFILTER, SETENCODING, SETRXOUTGAIN,
                        STARTAUDIO, ENABLEAUDIO, STARHARDWARE, STARTXCVR, SETSR, SETFREQ, ATTACHRX, ATTACHTX,
                        STARTIQ, MOX, STARTRADIO, SETTXVGAGAIN)
import e2e_plugin  # noqa: E402

WORK = os.path.abspath(os.environ.get("E2E_WORKDIR") or os.getcwd())
RADIOS = [  # frequency, FM tone, transmit capture
    (144390000, 1000.0, os.path.join(WORK, "tx_capture_radio0.raw")),
    (50313000, 700.0, os.path.join(WORK, "tx_capture_radio1.raw")),
]
FM = 5
USB = 1


def read_audio_by_stream(s, seconds):
    """audio packets carry their stream in byte 2"""
    s.settimeout(3)
    out = {}
    t_end = time.time() + seconds
    while time.time() < t_end:
        hdr = read_exact(s, 5)
        n = (hdr[3] << 8) | hdr[4]
        body = read_exact(s, n)
        if hdr[0] == 1:
            out.setdefault(hdr[2], []).append(np.frombuffer(body, dtype="<i2").astype(float))
    return {k: np.concatenate(v) for k, v in out.items()}


def mic_sender_to(sock, stop, stream):
    """as e2e_plugin.mic_sender, to a given transmit stream (QtRadio sends to its current one)"""
    ph = 0.0
    t_next = time.monotonic()
    while not stop.is_set():
        n = np.arange(512)
        x = (0.3 * 32767 * np.sin(ph + 2 * np.pi * 1000 * n / 48000)).astype("<i2")
        ph += 2 * np.pi * 1000 * 512 / 48000
        sock.sendall(bytes([stream, e2e_plugin.ISMIC_PCM, 0, 0, 0, 0]) + x.tobytes())
        t_next += 512 / 48000
        time.sleep(max(0.0, t_next - time.monotonic()))


def start_mock(mock_exe, k, logs):
    freq, tone, capture = RADIOS[k]
    env = dict(os.environ, MOCK_HACKRF_FM=f"{freq}:50:2500:{tone}", MOCK_HACKRF_TX_CAPTURE=capture + ":0.5")   # from 0.5 s after keying
    # both call their radio 0, like hpsdr's first board next to hackrf_one
    return subprocess.Popen([mock_exe, "-r", "0", "127.0.0.1"], env=env, stdout=logs[k], stderr=logs[k])


def main():
    dsp_exe, mock_exe = sys.argv[1:3]
    for _, _, cap in RADIOS:
        if os.path.exists(cap):
            os.remove(cap)
    logs = [open(os.path.join(WORK, f"e2e_radios_mock{k}.log"), "w") for k in range(2)]
    dlog = open(os.path.join(WORK, "e2e_radios_dsp.log"), "w")
    dsp = subprocess.Popen([dsp_exe], cwd=WORK, stdout=dlog, stderr=dlog)
    for _ in range(100):
        try:
            socket.create_connection(("127.0.0.1", 8000)).close()
            break
        except OSError:
            time.sleep(0.1)
    mocks = [start_mock(mock_exe, 0, logs)]
    time.sleep(1.5)
    mocks.append(start_mock(mock_exe, 1, logs))
    time.sleep(2.0)
    try:
        run(mock_exe, mocks, logs)
    finally:
        for p in mocks:
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()
        dsp.kill()
        dsp.wait()
    failures = e2e_plugin.failures
    print("FAILED" if failures else "ALL PASSED", f"({failures} failures)")
    sys.exit(1 if failures else 0)


def dsp_log():
    return open(os.path.join(WORK, "e2e_radios_dsp.log")).read()


def run(mock_exe, mocks, logs):
    check(all(p.poll() is None for p in mocks), "both radio programs still connected")
    c = Command()
    c.send(0, QUESTION, bytes([STARHARDWARE]))
    time.sleep(1.0)
    with c.lock:
        manifests = [t for k, a, t, _ in c.msgs if k == 77]
    ids = [int(part.split(">")[0]) for t in manifests for part in t.split("<radio=")[1:]]
    check(len(manifests) == 1 and manifests[0].startswith("<radios=2>") and ids == [0, 1],
          f"client gets one manifest with two radios, ids {ids}")

    for rid in (0, 1):
        c.send(0, STARCOMMAND, bytes([STARTRADIO, rid]))
        time.sleep(0.5)
    log = dsp_log()
    check(log.count("send_command [13]") >= 2 or log.count("response to STAR message") >= 2, "start radio 0 and 1")
    c.send(0, SETCLIENT, b"QtRadio")
    # streams: radio 0 = 0 (rx), 1 (tx); radio 1 = 2 (rx), 3 (tx)
    for stream, attach in ((0, ATTACHRX), (1, ATTACHTX), (2, ATTACHRX), (3, ATTACHTX)):
        c.send(stream, STARTXCVR)
        c.send(stream, STARTIQ)
        c.send(stream, STARCOMMAND, bytes([attach]))
        time.sleep(0.5)
    for k, (rx, tx) in enumerate(((0, 1), (2, 3))):
        f = str(RADIOS[k][0]).encode()
        c.send(rx, SETFREQ, f)
        c.send(rx, SETSR, b"192000")
        c.send(tx, SETFREQ, f)
        c.send(rx, SETMODE, bytes([FM]))
        c.send(tx, SETMODE, bytes([FM]))              # QtRadio sets the paired transmitter's mode too
        c.send(rx, SETFILTER, b"-6000,6000")
        c.send(rx, STARCOMMAND, bytes([SETTXVGAGAIN]) + b"20")
    c.send(0, STARTAUDIO, b"800,48000,1,0,0")
    c.send(0, SETENCODING, bytes([1, 1]))               # PCM audio and mic (after STARTAUDIO, which sets them too)
    c.send(0, ENABLEAUDIO, bytes([1]))
    c.send(0, SETRXOUTGAIN, b"50")
    c.send(2, SETRXOUTGAIN, b"50")
    a = socket.create_connection(("127.0.0.1", 8010))
    time.sleep(1.5)
    # clients hear one receiver at a time (ENABLEAUDIO on it, as QtRadio's
    # receiver selection does): each must carry its own radio's tone only
    for k, rx in enumerate((0, 2)):
        c.send(rx, ENABLEAUDIO, bytes([1]))
        time.sleep(1.0)
        read_audio_by_stream(a, 0.5)
        audio = read_audio_by_stream(a, 2.0)
        x = audio.get(rx, np.zeros(0))
        x = x[len(x) // 4:]
        others = sorted(set(audio) - {rx})
        own = tone_search(x, RADIOS[k][1], 48000) if len(x) > 4800 else (0, 0, -99)
        other = tone_search(x, RADIOS[1 - k][1], 48000) if len(x) > 4800 else (0, 0, -99)
        print(f"  stream {rx}: own tone {RADIOS[k][1]:.0f} Hz amplitude {own[0]:.0f}, "
              f"other radio's tone {RADIOS[1 - k][1]:.0f} Hz amplitude {other[0]:.0f}, other streams {others}")
        # WDSP FM of the mock's signal at 2.5 kHz deviation: about 0.48 of full scale
        # (with one radio the absent tone measures 1 to 2 % here: the FM signal's noise)
        check(own[0] > 10000 and other[0] < 0.05 * own[0] and not others, f"radio {k} heard on its own stream {rx}")

    # a second client listens to radio 0 while the first listens to radio 1:
    # both streams' audio must come, each with its own tone
    cb = Command()
    cb.send(0, SETCLIENT, b"QtRadio")
    cb.send(0, ENABLEAUDIO, bytes([1]))
    time.sleep(1.0)
    read_audio_by_stream(a, 0.5)
    audio = read_audio_by_stream(a, 2.0)
    got = {}
    for k, rx in enumerate((0, 2)):
        x = audio.get(rx, np.zeros(0))[4800:]
        got[rx] = tone_search(x, RADIOS[k][1], 48000)[0] if len(x) > 4800 else 0
    print(f"  two clients: stream 0 tone {got[0]:.0f}, stream 2 tone {got[2]:.0f}")
    check(got[0] > 10000 and got[2] > 10000, "two clients listen to different radios at once")
    cb.s.shutdown(socket.SHUT_RDWR)        # (close alone may wait for the reader thread)
    cb.s.close()
    time.sleep(1.0)
    read_audio_by_stream(a, 0.5)
    audio = read_audio_by_stream(a, 1.0)
    check(sorted(audio) == [2], f"when the second client leaves, its stream stops ({sorted(audio)})")

    # transmit on radio 1 only, in USB: a 1 kHz mic tone must come out 1 kHz above its frequency
    c.send(3, SETMODE, bytes([USB]))
    c.send(3, SETFILTER, b"150,2850")
    mic = socket.create_connection(("127.0.0.1", 8020))
    stop = threading.Event()
    th = threading.Thread(target=mic_sender_to, args=(mic, stop, 3))
    th.start()
    time.sleep(0.3)
    c.send(3, MOX, b"1")
    for _ in range(60):
        if os.path.exists(RADIOS[1][2]) and os.path.getsize(RADIOS[1][2]) == 2 * (1 << 21):
            break
        time.sleep(0.1)
    c.send(3, MOX, b"0")
    time.sleep(0.5)
    stop.set()
    th.join()
    mic.close()
    check(os.path.exists(RADIOS[1][2]), "radio 1 transmitted")
    check(not os.path.exists(RADIOS[0][2]), "radio 0 did not transmit")
    if os.path.exists(RADIOS[1][2]):
        # the capture is int8 I/Q at 10 MHz with the HackRF LO 500 kHz below the frequency
        raw = np.fromfile(RADIOS[1][2], dtype=np.int8).astype(float)
        x = raw[0::2] + 1j * raw[1::2]
        spec = np.abs(np.fft.fft(x * np.hanning(len(x)))) ** 2
        fr = np.fft.fftfreq(len(x), 1.0 / 10e6)
        k = int(np.argmax(spec))
        db = 10 * np.log10(spec / spec[k] + 1e-30)
        carrier = db[np.argmin(np.abs(fr - 500000))]
        print(f"  radio 1 transmit: strongest at {RADIOS[1][0] - 500000 + fr[k]:.0f} Hz, carrier {carrier:.1f} dBc")
        check(abs(fr[k] - 501000) < 50, "radio 1 transmits the mic tone (USB, 1 kHz above its frequency)")

    # radio 1's program restarts: same radio, same streams
    mocks[1].terminate()
    mocks[1].wait(5)
    time.sleep(1.0)
    check("radio 1 (hackrf_one) offline" in dsp_log(), "dspserver notices radio 1 has gone")
    mocks[1] = start_mock(mock_exe, 1, logs)
    time.sleep(2.0)
    check("radio 1: hackrf_one back, streams 2..3" in dsp_log(), "radio 1 back with streams 2 and 3")
    c2 = Command()
    c2.send(0, QUESTION, bytes([STARHARDWARE]))
    time.sleep(1.0)
    with c2.lock:
        m = [t for k, _, t, _ in c2.msgs if k == 77]
    check(len(m) == 1 and m[0].startswith("<radios=2>") and "(offline)" not in m[0],
          "a new client still sees both radios, both online")
    check(mocks[0].poll() is None, "radio 0's program unaffected")

    # the first client goes away; the new one uses the restarted radio 1
    a.close()
    c.s.shutdown(socket.SHUT_RDWR)
    c.s.close()
    time.sleep(1.5)
    c2.send(0, STARCOMMAND, bytes([STARTRADIO, 1]))
    time.sleep(0.5)
    c2.send(0, SETCLIENT, b"QtRadio")
    c2.send(2, STARTXCVR)
    c2.send(2, STARTIQ)
    c2.send(2, STARCOMMAND, bytes([ATTACHRX]))
    time.sleep(0.5)
    c2.send(2, SETFREQ, str(RADIOS[1][0]).encode())
    c2.send(2, SETSR, b"192000")
    c2.send(2, SETMODE, bytes([FM]))
    c2.send(2, SETFILTER, b"-6000,6000")
    c2.send(2, STARTAUDIO, b"800,48000,1,0,0")
    c2.send(2, SETENCODING, bytes([1, 1]))
    c2.send(2, SETRXOUTGAIN, b"50")
    c2.send(2, ENABLEAUDIO, bytes([1]))
    a2 = socket.create_connection(("127.0.0.1", 8010))
    time.sleep(1.5)
    try:
        read_audio_by_stream(a2, 0.5)
        x = read_audio_by_stream(a2, 2.0).get(2, np.zeros(0))
    except (EOFError, OSError):
        x = np.zeros(0)
    x = x[len(x) // 4:]
    own = tone_search(x, RADIOS[1][1], 48000) if len(x) > 4800 else (0, 0, -99)
    print(f"  after the first client left: stream 2 tone amplitude {own[0]:.0f}")
    check(own[0] > 10000, "a new client hears radio 1 after the first client disconnected")
    a2.close()


if __name__ == "__main__":
    main()
