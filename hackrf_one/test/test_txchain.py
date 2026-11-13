#!/usr/bin/env python3
"""Checks the files written by test_txchain: the tone must be at
LO + IF offset + 1 kHz, with images, the opposite sideband and LO leakage
far below it."""
import sys
import numpy as np

IF_OFFSET = 500000
fail = 0
for hw in (8000000, 10000000, 12500000, 16000000, 20000000):
    raw = np.fromfile(f"/tmp/txchain_{hw}.raw", dtype=np.int8).astype(float)
    x = (raw[0::2] + 1j * raw[1::2]) / 127.0
    x = x[len(x) // 5:]                               # skip start-up
    n = 1 << int(np.log2(len(x)))
    x = x[:n]
    t = np.arange(n) / n                              # flat top window: accurate level
    w = (0.21557895 - 0.41663158 * np.cos(2 * np.pi * t) + 0.277263158 * np.cos(4 * np.pi * t)
         - 0.083578947 * np.cos(6 * np.pi * t) + 0.006947368 * np.cos(8 * np.pi * t))
    spec = np.abs(np.fft.fft(x * w)) / np.sum(w)
    f = np.fft.fftfreq(n, 1.0 / hw)
    db = 20 * np.log10(spec + 1e-12)
    k = int(np.argmax(spec))
    tone_f, tone_db = f[k], db[k]
    binw = hw / n
    def level(fr):
        i = int(round(fr / binw)) % n
        return db[max(i - 6, 0):i + 7].max() - tone_db
    mirror = level(IF_OFFSET - 1000)
    lo = level(0.0)
    away = np.abs(f - tone_f) > 20000                 # everything 20 kHz or more from the tone
    spur = db[away].max() - tone_db
    spur_f = f[away][np.argmax(db[away])]
    ok = abs(tone_f - (IF_OFFSET + 1000)) < 2 * binw and abs(tone_db - 20 * np.log10(0.5)) < 0.5 \
        and mirror < -60 and lo < -60 and spur < -45
    print(f"{hw:9d}: tone {tone_f:+.0f} Hz {tone_db:.1f} dBFS, opposite sideband {mirror:.1f} dBc, "
          f"LO {lo:.1f} dBc, worst spur {spur:.1f} dBc at {spur_f:+.0f} Hz {'ok' if ok else '** FAIL'}")
    fail += not ok
print("PASSED" if not fail else f"FAILED ({fail})")
sys.exit(1 if fail else 0)
