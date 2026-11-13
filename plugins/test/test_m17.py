#!/usr/bin/env python3
"""Tests m17_plugin against the M17 Project's reference coder and decoder.

This script plays dspserver: it listens on a test port, lets the plugin
register, and speaks the plugin protocol to it.

  receive:  speech -> c2enc 3200 -> m17-coder-sym (reference) -> RRC + FM
            here (with a deviation error, a frequency offset and noise)
            -> plugin -> audio, compared with c2dec of the same bits;
            also a late start (link setup from the LICH)
  transmit: speech -> plugin (MOX on/off) -> I/Q -> FM demodulation + RRC
            here -> m17-decoder-sym (reference): callsigns, CAN, frame
            count, end of stream flag, and the Codec 2 bits against c2enc

usage: test_m17.py <m17_plugin> <m17-coder-sym> <m17-decoder-sym>
needs: python3 numpy, c2enc/c2dec (Codec 2 tools)
"""
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

import numpy as np

PORT = 10161
RATE = 48000
SPS = 10
PLG_REGISTER, PLG_AUDIO, PLG_TXIQ, PLG_STATUS, PLG_TXDONE = 1, 2, 3, 4, 5
PLG_ACCEPT, PLG_RXIQ, PLG_MIC, PLG_SETTING, PLG_START, PLG_STOP, PLG_MOX = 64, 65, 66, 67, 68, 69, 70
failures = 0


def check(cond, what):
    global failures
    print(("PASS " if cond else "FAIL ") + what, flush=True)
    if not cond:
        failures += 1


# ------------------------------------------------------------------ signals

def speech(seconds, seed=1):
    """Voice-like test audio at 8 kHz: a gliding pitch with two formants."""
    rng = np.random.default_rng(seed)
    n = int(seconds * 8000)
    t = np.arange(n) / 8000
    f0 = 140 + 40 * np.sin(2 * np.pi * 0.7 * t)
    ph = 2 * np.pi * np.cumsum(f0) / 8000
    x = np.zeros(n)
    for h in range(1, 25):
        f = h * f0
        a = np.exp(-((f - 700) / 300) ** 2) + 0.6 * np.exp(-((f - 1800) / 400) ** 2) + 0.05
        x += np.where(f < 3800, a, 0) * np.sin(h * ph)
    env = 0.5 + 0.5 * np.sin(2 * np.pi * 2.0 * t) ** 2
    x = x * env / np.max(np.abs(x)) * 0.6 + 0.002 * rng.standard_normal(n)
    return (x * 32767).astype(np.int16)


def c2(tool, data):
    return subprocess.run([tool, "3200", "-", "-"], input=data, capture_output=True, check=True).stdout


def rrc_taps(alpha=0.5, span=8, sps=SPS):
    t = (np.arange(span * sps + 1) - span * sps / 2) / sps
    h = np.zeros_like(t)
    for k, tt in enumerate(t):
        if abs(tt) < 1e-9:
            h[k] = 1 - alpha + 4 * alpha / np.pi
        elif abs(abs(4 * alpha * tt) - 1) < 1e-9:
            h[k] = alpha / np.sqrt(2) * ((1 + 2 / np.pi) * np.sin(np.pi / (4 * alpha)) + (1 - 2 / np.pi) * np.cos(np.pi / (4 * alpha)))
        else:
            h[k] = (np.sin(np.pi * tt * (1 - alpha)) + 4 * alpha * tt * np.cos(np.pi * tt * (1 + alpha))) / (np.pi * tt * (1 - (4 * alpha * tt) ** 2))
    return h / np.sum(h)


def callsign(s):
    """M17 base-40 address (6 bytes); '@ALL' is the broadcast address."""
    if s == "@ALL":
        return b"\xff" * 6
    chars = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/."
    v = 0
    for c in reversed(s):
        v = v * 40 + chars.index(c)
    return v.to_bytes(6, "big")


def modulate(symbols, dev_scale=1.0, offset_hz=0.0, snr_db=None, seed=2):
    up = np.zeros(len(symbols) * SPS)
    up[::SPS] = symbols
    shaped = np.convolve(up, rrc_taps() * SPS)
    f = shaped * 800.0 * dev_scale + offset_hz
    x = np.exp(1j * 2 * np.pi * np.cumsum(f) / RATE) * 0.7
    if snr_db is not None:
        rng = np.random.default_rng(seed)
        p = 0.49 / 10 ** (snr_db / 10)
        x = x + np.sqrt(p / 2) * (rng.standard_normal(len(x)) + 1j * rng.standard_normal(len(x)))
    return x


def noise(seconds, seed=3):
    rng = np.random.default_rng(seed)
    n = int(seconds * RATE)
    return 0.05 * (rng.standard_normal(n) + 1j * rng.standard_normal(n))


# ------------------------------------------------------------- the protocol

class Plugin:
    def __init__(self, exe):
        ls = socket.socket()
        ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ls.bind(("127.0.0.1", PORT))
        ls.listen(1)
        self.proc = subprocess.Popen([exe, "-p", str(PORT), "-v"], stderr=open("/tmp/test_m17_plugin.log", "w"))
        ls.settimeout(5)
        self.s, _ = ls.accept()
        ls.close()
        self.lock = threading.Lock()
        self.audio = {}
        self.txiq = {}
        self.status = []
        self.txdone = set()
        t, _, xml = self.read()
        self.xml = xml.decode()
        self.send(PLG_ACCEPT, -1, b"id=0\nmode=M17:32\n")
        threading.Thread(target=self.reader, daemon=True).start()

    def read(self):
        hdr = self._exact(6)
        n = struct.unpack(">I", hdr[:4])[0] - 2
        return hdr[4], struct.unpack("b", hdr[5:6])[0], self._exact(n)

    def _exact(self, n):
        b = b""
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise EOFError
            b += c
        return b

    def reader(self):
        try:
            while True:
                t, stream, p = self.read()
                with self.lock:
                    if t == PLG_AUDIO:
                        self.audio.setdefault(stream, []).append(np.frombuffer(p, dtype="<f4"))
                    elif t == PLG_TXIQ:
                        self.txiq.setdefault(stream, []).append(np.frombuffer(p, dtype="<f4"))
                    elif t == PLG_STATUS:
                        self.status.append((stream, p.decode(errors="replace")))
                    elif t == PLG_TXDONE:
                        self.txdone.add(stream)
        except (EOFError, OSError):
            pass

    def send(self, t, stream, payload=b""):
        self.s.sendall(struct.pack(">IBb", len(payload) + 2, t, stream) + payload)

    def rx(self, stream, iq, block=960):
        f = np.empty(2 * len(iq), dtype="<f4")
        f[0::2] = iq.real
        f[1::2] = iq.imag
        for k in range(0, len(iq), block):
            self.send(PLG_RXIQ, stream, f[2 * k: 2 * min(k + block, len(iq))].tobytes())
            if (k // block) % 50 == 0:
                time.sleep(0.002)

    def audio_of(self, stream):
        with self.lock:
            return np.concatenate(self.audio.get(stream, [np.zeros(0, dtype="<f4")])).astype(float)

    def text(self, stream=None):
        with self.lock:
            return "\n".join(t for s, t in self.status if stream is None or s == stream)

    def close(self):
        self.s.close()
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()


# ------------------------------------------------------------------- tests

def meta_text_blocks(text):
    """spec v2 META Text Data: control byte (block bit map << 4 | block bit) + 13 bytes"""
    nb = (len(text) + 12) // 13
    return [bytes([(((1 << nb) - 1) << 4) | (1 << b)]) + text[13 * b: 13 * b + 13].ljust(13, b" ")
            for b in range(nb)]


def reference_transmission(coder, pcm, src, dst, can, text=b""):
    """the M17 Project's coder; META text blocks change with every superframe"""
    bits = c2("c2enc", pcm.tobytes())
    frames = len(bits) // 16
    t = 0x0001 | 0x0004 | (can << 7)
    blocks = meta_text_blocks(text) or [b"\0" * 14]
    rec = b"".join(callsign(dst) + callsign(src) + struct.pack(">H", t) + blocks[(k // 6) % len(blocks)]
                   + bits[16 * k: 16 * k + 16] for k in range(frames))
    sym = subprocess.run([coder], input=rec, capture_output=True, check=True).stdout
    return np.frombuffer(sym, dtype="<f4").astype(float), bits[: 16 * frames]


def best_alignment(a, ref):
    """correlation of a with ref at the best lag (a may lag ref)"""
    best = (0, 0.0)
    n = len(ref)
    for lag in range(0, max(1, len(a) - n)):
        seg = a[lag: lag + n]
        if len(seg) < n:
            break
        c = np.dot(seg, ref) / (np.linalg.norm(seg) * np.linalg.norm(ref) + 1e-12)
        if c > best[1]:
            best = (lag, c)
    return best


def test_receive(p, coder):
    pcm = speech(2.0)
    sym, bits = reference_transmission(coder, pcm, "KD0OSS", "@ALL", 3)
    ref = np.frombuffer(c2("c2dec", bits), dtype="<i2").astype(float) / 32768.0
    print(f"  reference: {len(sym)} symbols, {len(bits) // 16} stream frames")

    iq = np.concatenate([noise(0.5), modulate(sym, dev_scale=0.9, offset_hz=300.0, snr_db=20), noise(0.5)])
    for kv in (b"agc=0", b"gain=0"):                # unity: Codec 2's own output
        p.send(PLG_SETTING, -1, kv)
    p.send(PLG_START, 0, b"rx M17")
    p.rx(0, iq)
    time.sleep(1.0)
    a = p.audio_of(0)
    check(abs(len(a) - len(iq) / 6) <= 160, f"audio follows the input: {len(a)} samples for {len(iq)} I/Q (expect {len(iq) // 6})")
    lag, c = best_alignment(a, ref)
    print(f"  decoded voice against c2dec of the same bits: correlation {c:.4f} at {1000 * lag / 8000:.0f} ms")
    check(c > 0.99, "voice decoded bit exact (90 % deviation, 300 Hz off, 20 dB SNR)")
    txt = p.text(0)
    check("heard=KD0OSS → @ALL" in txt, "heard: KD0OSS → @ALL")
    check("info=CAN 3, voice 3200" in txt, "stream info: CAN 3, voice 3200")
    check("end (end of stream" in txt, "end of transmission seen")
    ber = [float(l[4:]) for l in txt.splitlines() if l.startswith("ber=")]
    print(f"  BER readings: {ber}")
    check(len(ber) > 0 and max(ber[:-1] or [0]) < 2.0, "low bit error reading")

    # joining late: no preamble or LSF, link setup from the LICH
    p.send(PLG_START, 2, b"rx M17")
    late = sym[192 * 2 + 192 * 4 + 37:]          # into the 5th stream frame
    p.rx(2, np.concatenate([noise(0.2, 5), modulate(late, offset_hz=-200.0, snr_db=25, seed=7), noise(0.4, 6)]))
    time.sleep(1.0)
    check("heard=KD0OSS → @ALL" in p.text(2), "late start: link setup recovered from the LICH")
    a2 = p.audio_of(2)
    check(np.sqrt(np.mean(a2 ** 2)) > 0.02, "late start: voice decoded")

    # noise only: nothing heard, silence
    p.send(PLG_START, 3, b"rx M17")
    p.rx(3, noise(2.0, 9) * 4)
    time.sleep(0.5)
    a3 = p.audio_of(3)
    check("heard=" not in p.text(3) and np.max(np.abs(a3)) == 0.0, "noise alone: no false decodes")


def test_audio_level(p, coder):
    """a quiet sender (speech peaks 26 dB below full scale)"""
    pcm = (speech(3.0, seed=4).astype(float) * 0.05).astype(np.int16)
    sym, bits = reference_transmission(coder, pcm, "KD0OSS", "@ALL", 0)
    ref = np.frombuffer(c2("c2dec", bits), dtype="<i2").astype(float) / 32768.0
    iq = np.concatenate([noise(0.3, 11), modulate(sym, snr_db=25, seed=12), noise(0.5, 13)])
    print(f"  quiet sender: Codec 2 output peak {np.max(np.abs(ref)):.3f}")

    p.send(PLG_SETTING, -1, b"agc=1")
    p.send(PLG_START, 4, b"rx M17")
    p.rx(4, iq)
    time.sleep(1.0)
    a = p.audio_of(4)
    lag, c = best_alignment(a, ref)
    pk = np.max(np.abs(a))
    later = a[len(a) // 3:]                         # after the AGC has settled
    print(f"  AGC: output peak {pk:.3f}, correlation with c2dec {c:.3f}")
    check(0.4 < np.max(np.abs(later)) < 0.95 and pk <= 1.0, f"AGC brings a quiet sender up (peak {pk:.3f})")
    check(c > 0.9, "AGC keeps the voice intact")

    p.send(PLG_SETTING, -1, b"agc=0")
    p.send(PLG_SETTING, -1, b"gain=20")
    p.send(PLG_START, 5, b"rx M17")
    p.rx(5, iq)
    time.sleep(1.0)
    a = p.audio_of(5)
    lag, c = best_alignment(a, ref)
    seg = a[lag: lag + len(ref)] if lag >= 0 else a[: len(ref)]
    n = min(len(seg), len(ref))
    ratio = np.dot(seg[:n], ref[:n]) / np.dot(ref[:n], ref[:n])
    print(f"  AGC off, 20 dB: gain {ratio:.2f}")
    check(abs(ratio - 10.0) < 1.0, f"fixed gain of 20 dB with the AGC off (x{ratio:.2f})")
    p.send(PLG_SETTING, -1, b"agc=1")


def test_metatext_rx(p, coder):
    pcm = speech(2.0, seed=5)
    cases = [(6, "Hello from QtRadio M17 metatext test", 0),       # 3 blocks
             (7, "73 de KD0OSS", 0),                                # 1 block
             (8, "Grüße aus Iowa – 73 ✓ ¡olé! 13+13+13+13 byte", 0),  # 4 blocks, UTF-8, 52 bytes
             (9, "Hello from QtRadio M17 metatext test", 192 * 2 + 192 * 3)]  # joining late
    for stream, text, skip in cases:
        tb = text.encode()
        assert len(tb) <= 52, len(tb)
        sym, bits = reference_transmission(coder, pcm, "KD0OSS", "@ALL", 0, tb)
        p.send(PLG_START, stream, b"rx M17")
        p.rx(stream, np.concatenate([noise(0.2, 20 + stream), modulate(sym[skip:], snr_db=25, seed=stream),
                                     noise(0.4, 30 + stream)]))
        time.sleep(0.8)
        txt = p.text(stream)
        shown = [l[5:] for l in txt.splitlines() if l.startswith("text=") and len(l) > 5]
        logged = [l for l in txt.splitlines() if l.startswith("log=") and "KD0OSS: " in l]
        what = "late join, " if skip else ""
        check(shown == [text], f"META text received ({what}{len(tb)} bytes): {shown}")
        check(len(logged) == 1 and logged[0].endswith("KD0OSS: " + text), f"META text logged once: {logged}")


def test_metatext_tx(p, decoder):
    text = "Grüße aus Iowa – 73 ✓ ¡olé! 13+13+13+13 byte"
    p.send(PLG_SETTING, -1, ("txtext=" + text).encode())
    pcm = speech(1.6, seed=6)
    p.send(PLG_START, 10, b"tx M17")
    p.send(PLG_MOX, 10, b"\x01")
    mic = (pcm.astype(np.float32) / 32767.0).astype("<f4")
    for k in range(0, len(mic), 85):
        p.send(PLG_MIC, 10, mic[k: k + 85].tobytes())
    p.send(PLG_MOX, 10, b"\x00")
    for _ in range(40):
        p.send(PLG_MIC, 10, np.zeros(85, dtype="<f4").tobytes())
        if 10 in p.txdone:
            break
        time.sleep(0.01)
    time.sleep(0.2)
    p.send(PLG_SETTING, -1, b"txtext=")
    with p.lock:
        iq = np.concatenate(p.txiq.get(10, [np.zeros(0, dtype="<f4")])).astype(float)

    # the reference decoder: META of the LSF and of every superframe (LICH)
    sym, dist = fm_demod_symbols(iq)
    out = subprocess.run([decoder, "-c", "-m"], input=sym.tobytes(), capture_output=True, timeout=20).stdout.decode()
    metas = [bytes.fromhex(l.split("META:")[1].split()[0]) for l in out.splitlines() if "META:" in l]
    types = [l.split("TYPE:")[1].split()[0] for l in out.splitlines() if "TYPE:" in l]
    want = meta_text_blocks(text.encode())
    print(f"  META on the air: {[m_.hex()[:2] for m_ in metas]}")
    check(len(metas) >= 5 and metas[0] == want[0], "LSF carries the first text block")
    check(metas[1: 1 + len(want)] == want and all(m_ in want for m_ in metas),
          "superframes carry the blocks in turn (reference decoder)")
    check(len(types) > 0 and all(int(t_, 16) & 0x78 == 0 for t_ in types),
          "TYPE: no encryption, META type text")
    # and our own receiver puts it together again
    p.send(PLG_START, 11, b"rx M17")
    p.rx(11, np.concatenate([noise(0.2, 40), iq[0::2] + 1j * iq[1::2], noise(0.4, 41)]))
    time.sleep(0.8)
    shown = [l[5:] for l in p.text(11).splitlines() if l.startswith("text=") and len(l) > 5]
    check(shown == [text], f"own transmission received: {shown}")


def fm_demod_symbols(iq):
    z = iq[0::2] + 1j * iq[1::2]
    f = np.angle(z[1:] * np.conj(z[:-1])) * RATE / (2 * np.pi) / 800.0
    y = np.convolve(f, rrc_taps())
    # symbol timing: the phase whose samples sit closest to +-1, +-3
    best = None
    for ph in range(SPS):
        s = y[ph::SPS]
        d = np.mean(np.min(np.abs(s[:, None] - np.array([-3, -1, 1, 3])[None, :]), axis=1))
        if best is None or d < best[0]:
            best = (d, ph)
    return y[best[1]::SPS].astype("<f4"), best[0]


def test_transmit(p, decoder):
    pcm = speech(1.6, seed=4)
    p.send(PLG_START, 1, b"tx M17")
    for kv in (b"src=KD0OSS-T", b"dst=W1AW", b"can=7"):
        p.send(PLG_SETTING, -1, kv)
    p.send(PLG_MOX, 1, b"\x01")
    mic = (pcm.astype(np.float32) / 32767.0).astype("<f4")
    for k in range(0, len(mic), 85):
        p.send(PLG_MIC, 1, mic[k: k + 85].tobytes())
    p.send(PLG_MOX, 1, b"\x00")
    for _ in range(40):
        p.send(PLG_MIC, 1, np.zeros(85, dtype="<f4").tobytes())
        if 1 in p.txdone:
            break
        time.sleep(0.01)
    time.sleep(0.2)
    check(1 in p.txdone, "TXDONE after unkeying")
    with p.lock:
        iq = np.concatenate(p.txiq.get(1, [np.zeros(0, dtype="<f4")])).astype(float)
    n_frames = len(pcm) // 320                       # full frames while keyed
    expect = (2 + n_frames + 1 + 1) * 192 * SPS      # preamble, LSF, voice, last frame, EOT
    print(f"  transmitted {len(iq) // 2} I/Q samples (frames: preamble, LSF, {n_frames}+1 voice, EOT)")
    check(abs(len(iq) // 2 - expect) <= 9 * SPS, "transmission length")
    amp = np.hypot(iq[0::2], iq[1::2])
    check(np.std(amp) < 1e-3, "constant envelope (FM)")

    sym, dist = fm_demod_symbols(iq)
    print(f"  demodulated symbol error from the ideal levels: {dist:.3f}")
    out = subprocess.run([decoder, "-c"], input=sym.tobytes(), capture_output=True, timeout=20).stdout.decode()
    lines = [l for l in out.splitlines() if l.strip()]
    lsf = [l for l in lines if "{LSF}" in l]
    fns = [l for l in lines if l.startswith("FN:")]
    check(len(lsf) == 1 and "DST: W1AW" in lsf[0] and "SRC: KD0OSS-T" in lsf[0] and "CAN: 7" in lsf[0],
          f"reference decoder: LSF {lsf[0].strip() if lsf else None}")
    check(len(fns) == n_frames + 1, f"reference decoder: {len(fns)} stream frames (expect {n_frames + 1})")
    if fns:
        check(int(fns[-1][4:8], 16) & 0x8000 != 0, "last frame carries the end of stream flag")
        got = bytes.fromhex("".join(l.split("PLD:")[1].split()[0] for l in fns[:n_frames]))
        want = c2("c2enc", pcm[: n_frames * 320].tobytes())
        check(got == want, "Codec 2 bits on the air equal c2enc of the mic audio")


def main():
    exe, coder, decoder = sys.argv[1:4]
    p = Plugin(exe)
    check('name="m17"' in p.xml and 'name="M17"' in p.xml, "registration")
    try:
        test_receive(p, coder)
        test_audio_level(p, coder)
        test_metatext_rx(p, coder)
        test_transmit(p, decoder)
        test_metatext_tx(p, decoder)
    finally:
        p.close()
    print("FAILED" if failures else "ALL PASSED", f"({failures} failures)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
