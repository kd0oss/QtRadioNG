#!/usr/bin/env python3
"""End-to-end test of protocol plugins with the example plugin.

client (this) -> dspserver -> hackrf_one_mock, with example_plugin connected
to dspserver.  The mock sends an FM station (144.39 MHz, 2.5 kHz deviation,
1 kHz tone) and captures what is transmitted.

Checks: plugin list on QPLUGINS; plugin mode NFMX receives the 1 kHz tone;
settings reach the plugin and status comes back; transmit in NFMX (test
tone) is FM at 2.5 kHz deviation; unkeying waits for the plugin's tail;
receive works again afterwards; the list updates when the plugin leaves.

Usage: e2e_plugin.py <dspserver> <hackrf_one_mock> <example_plugin>

dspserver runs in the current directory (or $E2E_WORKDIR), where it keeps
its WDSP wisdom file: the first run there spends a few minutes creating it.
Logs and the transmit capture are written there too.  Uses dspserver's
usual ports: nothing else may be running dspserver or a radio program.
"""
import os
import re
import signal
import socket
import subprocess
import sys
import threading
import time

import numpy as np


QUESTION, STARCOMMAND, SETCLIENT, SETMODE, SETFILTER = 1, 9, 13, 16, 17
SETENCODING, SETRXOUTGAIN, STARTAUDIO, ENABLEAUDIO = 18, 19, 21, 143
QPLUGINS, SETPLUGIN = 145, 146
STARHARDWARE, STARTXCVR, SETSR, SETFREQ, ATTACHRX, ATTACHTX, STARTIQ, MOX = 255, 245, 246, 248, 249, 250, 252, 254
STARTRADIO, SETTXVGAGAIN = 13, 7
WORK = os.path.abspath(os.environ.get("E2E_WORKDIR") or os.getcwd())
CAPTURE = os.path.join(WORK, "tx_capture_plugin.raw")
FREQ = 144390000
HW_RATE = 10000000
failures = 0


MSG = 64
ISMIC_PCM = 144


def msg(stream, cmd, payload=b""):
    m = bytes([stream, cmd]) + payload
    return m + b"\0" * (MSG - len(m))


def read_exact(s, n):
    buf = b""
    while len(buf) < n:
        part = s.recv(n - len(buf))
        if not part:
            raise EOFError
        buf += part
    return buf


def mic_sender(sock, stop):
    """512 PCM samples per message at 48 kHz, paced in real time."""
    ph = 0.0
    t_next = time.monotonic()
    while not stop.is_set():
        n = np.arange(512)
        s = (0.3 * 32767 * np.sin(ph + 2 * np.pi * 1000 * n / 48000)).astype("<i2")
        ph += 2 * np.pi * 1000 * 512 / 48000
        sock.sendall(bytes([1, ISMIC_PCM, 0, 0, 0, 0]) + s.tobytes())
        t_next += 512 / 48000
        time.sleep(max(0.0, t_next - time.monotonic()))


def check(cond, what):
    global failures
    print(("PASS " if cond else "FAIL ") + what, flush=True)
    if not cond:
        failures += 1


class Command:
    """The command connection: 4 byte header [len hi][len lo][type][arg] + payload."""

    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", 8000))
        self.lock = threading.Lock()
        self.msgs = []
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        try:
            while True:
                hdr = read_exact(self.s, 4)
                n = (hdr[0] << 8) | hdr[1]
                body = read_exact(self.s, n) if n else b""
                with self.lock:
                    self.msgs.append((hdr[2], hdr[3], body.rstrip(b"\0").decode(errors="replace"), time.time()))
        except (EOFError, OSError):
            pass

    def send(self, stream, cmd, payload=b""):
        self.s.sendall(msg(stream, cmd, payload))

    def wait(self, kind, pred=lambda a, t: True, timeout=3.0, since=0.0):
        t_end = time.time() + timeout
        while time.time() < t_end:
            with self.lock:
                for k, a, t, ts in self.msgs:
                    if k == kind and ts >= since and pred(a, t):
                        return a, t, ts
            time.sleep(0.05)
        return None


def read_audio(s, seconds):
    s.settimeout(3)
    out = []
    t_end = time.time() + seconds
    while time.time() < t_end:
        hdr = read_exact(s, 5)
        n = (hdr[3] << 8) | hdr[4]
        body = read_exact(s, n)
        if hdr[0] == 1:
            out.append(np.frombuffer(body, dtype="<i2").astype(float))
    return np.concatenate(out) if out else np.zeros(0)


def tone(x, f, rate=48000):
    t = np.arange(len(x)) / rate
    a = 2 * np.mean(x * np.cos(2 * np.pi * f * t))
    b = 2 * np.mean(x * np.sin(2 * np.pi * f * t))
    fit = a * np.cos(2 * np.pi * f * t) + b * np.sin(2 * np.pi * f * t)
    res = x - fit
    return np.hypot(a, b), 10 * np.log10(np.sum(fit ** 2) / (np.sum(res ** 2) + 1e-30))


def tone_search(x, f0, rate, span=6.0):
    """tone() at the best frequency within f0 +- span: the HackRF trims its
    sample clock (up to 0.2 %) to hold its FIFO level, which moves tones."""
    best = None
    for f in np.linspace(f0 - span, f0 + span, 121):
        amp, sinad = tone(x, f, rate)
        if best is None or sinad > best[2]:
            best = (amp, f, sinad)
    return best


def fm_demod_capture(path, offset_hz):
    """HackRF capture (int8 I/Q at 10 MHz) -> FM audio at 50 kHz, in Hz."""
    raw = np.fromfile(path, dtype=np.int8).astype(float)
    x = (raw[0::2] + 1j * raw[1::2]) / 127.0
    n = np.arange(len(x))
    x = x * np.exp(-2j * np.pi * offset_hz * n / HW_RATE)
    # 200:1 by block averaging twice (10 MHz -> 500 kHz -> 50 kHz)
    x = x[: len(x) // 20 * 20].reshape(-1, 20).mean(axis=1)
    x = x[: len(x) // 10 * 10].reshape(-1, 10).mean(axis=1)
    d = np.angle(x[1:] * np.conj(x[:-1])) * 50000 / (2 * np.pi)
    return d, np.abs(x)


def main():
    dsp_exe, mock_exe, plugin_exe = sys.argv[1:4]
    for f in (CAPTURE,):
        if os.path.exists(f):
            os.remove(f)
    logs = {k: open(os.path.join(WORK, f"e2e_plugin_{k}.log"), "w") for k in ("dsp", "mock", "plugin")}
    dsp = subprocess.Popen([dsp_exe], cwd=WORK, stdout=logs["dsp"], stderr=logs["dsp"])
    for _ in range(100):
        try:
            socket.create_connection(("127.0.0.1", 8000)).close()
            break
        except OSError:
            time.sleep(0.1)
    env = dict(os.environ, MOCK_HACKRF_FM=f"{FREQ}:50:2500:1000", MOCK_HACKRF_TX_CAPTURE=CAPTURE)
    mock = subprocess.Popen([mock_exe, "127.0.0.1"], env=env, stdout=logs["mock"], stderr=logs["mock"])
    procs = {}

    def start_plugin():
        procs["plugin"] = subprocess.Popen([plugin_exe], stdout=logs["plugin"], stderr=logs["plugin"])

    def kill_plugin():
        procs["plugin"].kill()
        procs["plugin"].wait()

    start_plugin()
    time.sleep(2.0)
    try:
        run(start_plugin, kill_plugin)
    finally:
        for p in (procs["plugin"], mock):
            if p.poll() is None:
                p.send_signal(signal.SIGINT)
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()
        dsp.kill()
        dsp.wait()
    print("FAILED" if failures else "ALL PASSED", f"({failures} failures)")
    sys.exit(1 if failures else 0)


def run(start_plugin, kill_plugin):
    c = Command()
    c.send(0, QUESTION, bytes([STARHARDWARE]))
    check(c.wait(77) is not None, "manifest")
    c.send(0, QUESTION, bytes([QPLUGINS]))
    r = c.wait(78)
    check(r is not None and r[0] == 1 and 'name="example"' in r[1] and 'first_mode="32"' in r[1],
          "plugin list: example plugin, first mode 32")

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
    c.send(0, SETENCODING, bytes([1, 1]))                 # PCM audio and mic
    c.send(0, STARTAUDIO, b"800,48000,1,0,0")
    c.send(0, ENABLEAUDIO, bytes([1]))
    c.send(0, SETRXOUTGAIN, b"50")
    a = socket.create_connection(("127.0.0.1", 8010))
    time.sleep(0.5)

    # receive in the plugin mode
    t0 = time.time()
    c.send(0, SETMODE, bytes([32]))
    c.send(1, SETMODE, bytes([32]))
    c.send(0, SETFILTER, b"-6000,6000")
    time.sleep(1.0)
    read_audio(a, 0.5)
    x = read_audio(a, 2.0)
    amp, sinad = tone(x[len(x) // 4:], 1000.0)
    print(f"  NFMX receive: {len(x)} samples, 1 kHz amplitude {amp:.0f}, SINAD {sinad:.1f} dB")
    check(abs(len(x) - 96000) < 9600, "plugin audio arrives at the client rate")
    # 2.5 kHz deviation -> 0.5 of full scale in the plugin, volume 50 % -> x1
    check(abs(amp / (0.5 * 32767) - 1) < 0.2, f"plugin audio level {amp:.0f} (expect about {0.5 * 32767:.0f})")
    check(sinad > 30, f"plugin audio SINAD {sinad:.1f} dB")
    r = c.wait(79, lambda a_, t: "level=" in t, since=t0)
    check(r is not None and "stream=0" in r[1], f"status from the plugin: {r[1].splitlines()[1] if r else None}")
    r = c.wait(79, lambda a_, t: "log=stream 1: transmit NFMX" in t, since=t0)
    check(r is not None, "the plugin knows stream 1 transmits in NFMX")

    # panel settings
    t1 = time.time()
    c.send(0, SETPLUGIN, bytes([0]) + b"note=hello there")
    r = c.wait(79, lambda a_, t: "echo=hello there" in t, since=t1)
    check(r is not None, "setting reaches the plugin and its status comes back")
    c.send(0, SETPLUGIN, bytes([0]) + b"ping=1")
    check(c.wait(79, lambda a_, t: "log=pong" in t, since=t1) is not None, "button press reaches the plugin")

    # transmit the test tone
    c.send(0, SETPLUGIN, bytes([0]) + b"tone=1")
    mic = socket.create_connection(("127.0.0.1", 8020))
    stop = threading.Event()
    th = threading.Thread(target=mic_sender, args=(mic, stop))
    th.start()
    time.sleep(0.3)
    c.send(1, MOX, b"1")
    for _ in range(50):
        if os.path.exists(CAPTURE) and os.path.getsize(CAPTURE) == 2 * (1 << 21):
            break
        time.sleep(0.1)
    time.sleep(0.2)
    t_off = time.time()
    c.send(1, MOX, b"0")
    stop.set()                       # PTT released: the mic stream stops too
    th.join()
    r = c.wait(79, lambda a_, t: "log=transmission ended" in t, since=t_off, timeout=3)
    check(r is not None, "the plugin finished its tail without mic audio")
    if r is not None:
        print(f"  tail ended {1000 * (r[2] - t_off):.0f} ms after MOX off")
        check(0.1 < r[2] - t_off < 1.0, "tail length about 200 ms plus buffering")
    time.sleep(1.0)
    log = open(os.path.join(WORK, "e2e_plugin_dsp.log")).read()
    check("Mox off deferred" in log, "dspserver deferred unkeying for the tail")
    mlog = open(os.path.join(WORK, "e2e_plugin_mock.log")).read()
    check(re.search(r"(rx|receive|RX)", mlog) is not None, "radio back on receive")

    check(os.path.exists(CAPTURE), "transmit samples captured")
    if os.path.exists(CAPTURE):
        # The HackRF transmits at FREQ with its LO 500 kHz below (IF offset)
        d, env = fm_demod_capture(CAPTURE, 500000)
        d = d[len(d) // 10:]
        dev, f_tone, sinad = tone_search(d - np.mean(d), 1000.0, 50000)
        print(f"  NFMX transmit: {f_tone:.1f} Hz tone at {dev:.0f} Hz deviation, SINAD {sinad:.1f} dB, "
              f"carrier offset {np.mean(d):.0f} Hz, envelope {np.std(env) / np.mean(env):.3f}")
        check(abs(dev / 1250 - 1) < 0.15, f"FM deviation {dev:.0f} Hz (test tone 0.5 x 2500 Hz)")
        check(sinad > 25, f"transmit SINAD {sinad:.1f} dB")
        check(abs(np.mean(d)) < 100, "on frequency")

    # receive again
    time.sleep(0.5)
    read_audio(a, 0.5)
    x = read_audio(a, 1.5)
    amp2, sinad2 = tone(x[len(x) // 4:], 1000.0)
    check(sinad2 > 30, f"receive in the plugin mode again after transmit (SINAD {sinad2:.1f} dB)")

    # back to a WDSP mode: plugin told to stop
    c.send(0, SETMODE, bytes([5]))
    c.send(1, SETMODE, bytes([5]))
    time.sleep(0.5)
    plog = open(os.path.join(WORK, "e2e_plugin_plugin.log")).read()
    check("stream 0 stopped" in plog and "stream 1 stopped" in plog, "leaving the plugin mode stops the plugin's streams")

    # the plugin goes away while in use, and comes back
    c.send(0, SETMODE, bytes([32]))
    c.send(1, SETMODE, bytes([32]))
    time.sleep(1.0)
    t2 = time.time()
    kill_plugin()
    r = c.wait(78, lambda n, t: n == 0, since=t2)
    check(r is not None and "<entry" not in r[1], "clients get the empty plugin list when the plugin leaves")
    time.sleep(0.5)
    read_audio(a, 0.3)
    x = read_audio(a, 1.0)
    check(len(x) > 40000 and np.sqrt(np.mean(x ** 2)) < 50, "plugin mode without its plugin is silent (and audio keeps flowing)")
    t3 = time.time()
    start_plugin()
    r = c.wait(78, lambda n, t: n == 1, since=t3, timeout=5)
    check(r is not None, "clients get the list again when the plugin comes back")
    time.sleep(1.5)
    read_audio(a, 0.3)
    x = read_audio(a, 1.5)
    _, sinad3 = tone(x[len(x) // 4:], 1000.0)
    check(sinad3 > 30, f"streams already in its mode carry on with the restarted plugin (SINAD {sinad3:.1f} dB)")

    # the plugin dies while transmitting: unkeying must not wait for it
    mic = socket.create_connection(("127.0.0.1", 8020))
    stop = threading.Event()
    th = threading.Thread(target=mic_sender, args=(mic, stop))
    th.start()
    c.send(1, MOX, b"1")
    time.sleep(1.0)
    kill_plugin()
    time.sleep(0.3)
    n_before = open(os.path.join(WORK, "e2e_plugin_dsp.log")).read().count("Unkeyed tx channel")
    c.send(1, MOX, b"0")
    time.sleep(0.5)
    stop.set()
    th.join()
    n_after = open(os.path.join(WORK, "e2e_plugin_dsp.log")).read().count("Unkeyed tx channel")
    check(n_after == n_before + 1, "unkeyed at once when the plugin is gone")
    start_plugin()
    a.close()
    return c


if __name__ == "__main__":
    main()
