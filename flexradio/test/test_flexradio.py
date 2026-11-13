#!/usr/bin/env python3
"""Test flexradio against the simulated FLEX (flex_sim.py) and a fake dspserver.

usage: test_flexradio.py path/to/flexradio
Runs twice: once as a current radio (samples +/-1, "0x40000000,0x42000000" pan
reply), once with samples scaled to 32768 and a "pan=0x40000000" reply.
"""
import os
import socket
import struct
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from flex_sim import FlexSim  # noqa: E402

QHARDWARE, ATTACHRX, DETACH, SETSR, SETFREQ, MOX, STARTBS = 239, 249, 251, 246, 248, 254, 241
STARTRADIO, SETRXANT, SETRFGAIN, SETMODE, SETRFPOWER = 13, 1, 2, 3, 4
ATTACHTX = 250
BUFFERL = struct.Struct("<HbbH2x2048d")
BUFFER = struct.Struct("<HbbH2x512d")
MIC = struct.Struct("<b16sbhff")
failures = 0


def check(cond, what):
    global failures
    print(("PASS " if cond else "FAIL ") + what, flush=True)
    if not cond:
        failures += 1


def command(conn, idx, code, arg=b""):
    payload = bytes([idx, code]) + arg
    conn.sendall(bytes([0x7F, len(payload)]) + payload)
    return conn.recv(4096)


def recv_iq(sock, seconds):
    sock.settimeout(1.0)
    out = []
    t_end = time.time() + seconds
    while time.time() < t_end:
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            break
        chunk, rid, rx, length, *vals = BUFFERL.unpack(data[:BUFFERL.size])
        v = np.array(vals[:length])
        out.append(v[1::2] + 1j * v[0::2])        # dspserver/WDSP order is Q,I
    return np.concatenate(out) if out else np.zeros(0, dtype=complex)


def drain(sock, seconds=0.3):
    """Throw away what is queued (or arrives in the next few hundred ms)."""
    sock.settimeout(0.2)
    t_end = time.time() + seconds
    while time.time() < t_end:
        try:
            sock.recvfrom(65536)
        except socket.timeout:
            return


def peak(iq, rate):
    n = 1 << int(np.log2(len(iq)))
    x = iq[-n:] * np.blackman(n)
    spec = np.abs(np.fft.fftshift(np.fft.fft(x))) / np.sum(np.blackman(n))
    freqs = np.fft.fftshift(np.fft.fftfreq(n, 1.0 / rate))
    k = int(np.argmax(spec))
    return freqs[k], 20 * np.log10(spec[k])


def tx_packets(kind):
    """One period of a 1 kHz modulating tone as dspserver TX IQ packets
    (32 complex in WDSP's Q,I order, 192 ksps) -> list of bytes."""
    rate, n = 192000, 192 * 5                     # 5 ms: a whole number of tone cycles
    t = np.arange(n) / rate
    if kind == "USB":
        x = 0.5 * np.exp(2j * np.pi * 1000 * t)
    elif kind == "LSB":
        x = 0.5 * np.exp(-2j * np.pi * 1000 * t)
    elif kind == "AM":
        x = 0.5 * (1 + 0.5 * np.cos(2 * np.pi * 1000 * t)) + 0j
    else:                                         # FM, 2.5 kHz deviation
        x = 0.8 * np.exp(1j * 2500 / 1000 * np.sin(2 * np.pi * 1000 * t))
    out = []
    for k in range(0, n, 32):
        d = np.zeros(512)
        d[0:64:2] = x[k:k + 32].imag
        d[1:64:2] = x[k:k + 32].real
        out.append(BUFFER.pack(0, 0, 1, 64, *d))
    return out


def send_tx_iq(sock, dest, packets, seconds):
    """Real-time paced, in 5 ms bursts, as dspserver sends them."""
    t_next = time.monotonic()
    t_end = t_next + seconds
    while time.monotonic() < t_end:
        for p in packets:
            sock.sendto(p, dest)
        t_next += 0.005
        d = t_next - time.monotonic()
        if d > 0:
            time.sleep(d)


def tone(audio, rate=24000):
    """Frequency, amplitude and DC of the steady part of the DAX TX audio."""
    x = np.concatenate(audio)[rate // 4:]
    n = 1 << int(np.log2(len(x)))
    x = x[-n:]
    spec = np.abs(np.fft.rfft(x * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1.0 / rate)
    k = int(np.argmax(spec[1:])) + 1
    return f[k], np.sqrt(2) * np.std(x), np.mean(x)


def test_tx(conn, sim):
    txs = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)       # dspserver TX IQ port
    txs.bind(("127.0.0.1", 10020))
    mic = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)       # dspserver radio mic port
    mic.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    mic.bind(("127.0.0.1", 10130))

    check(command(conn, 1, MOX, b"\1").startswith(b"Error"), "MOX refused before ATTACHTX")
    check(command(conn, 0, SETMODE, b"USB") == b"OK", "SETMODE USB before attach")
    check(command(conn, 0, SETRFPOWER, b"40") == b"OK" and sim.transmit.get("rfpower") == "40", "RF power 40 %")
    check(command(conn, 1, ATTACHTX, b"\0") == b"OK", "ATTACHTX")
    txs.settimeout(2)
    try:
        sync, flexaddr = txs.recvfrom(16)
        check(len(sync) == 2, "TX sync sent to dspserver")
    except socket.timeout:
        check(False, "TX sync sent to dspserver")
        return
    check(sim.tx_stream is not None, "radio: dax_tx stream created")
    check(sim.transmit.get("dax") == "1", "radio: transmit set dax=1")
    check(sim.slice_tx.get("tx") == "1" and sim.slice_tx.get("mode") == "USB", f"radio: our slice is the TX slice, {sim.slice_tx}")
    check(command(conn, 1, SETSR, b"192000") == b"OK", "SETSAMPLERATE on the TX stream accepted")
    check(command(conn, 1, SETFREQ, b"7100000") == b"OK" and abs(sim.pan["center"] - 14.2) < 1e-9,
          "SETFREQ on the TX stream does not move the receiver")

    mic.settimeout(1)
    t0 = time.time()
    pkts = []
    while time.time() - t0 < 1.0:
        try:
            pkts.append(mic.recvfrom(4096)[0])
        except socket.timeout:
            break
    check(170 < len(pkts) < 230, f"radio mic pacing packets: {len(pkts)}/s")
    rid, ip, tx, length, fwd, rev = MIC.unpack(pkts[-1][:MIC.size])
    check(ip.rstrip(b"\0") == b"127.0.0.1", f"MIC_BUFFER ipaddr {ip.rstrip(b'\0')!r} matches the manifest")
    check(fwd < 0.01, f"forward power {fwd:.3f} W before keying")

    for mode, flexmode, kind, amp in (("USB", "USB", "USB", 0.5), ("LSB", "LSB", "LSB", 0.5),
                                      ("AM", "AM", "AM", 0.25), ("FMN", "FM", "FM", 0.45),
                                      ("DIGU", "DIGU", "USB", 0.5)):
        check(command(conn, 0, SETMODE, mode.encode()) == b"OK" and sim.slice_tx.get("mode") == flexmode,
              f"SETMODE {mode} -> radio mode {sim.slice_tx.get('mode')}")
        sim.tx_audio.clear()
        n0 = sim.tx_packets
        check(command(conn, 1, MOX, b"\1") == b"OK" and sim.xmit == 1, f"{mode}: MOX on -> xmit 1")
        t0 = time.time()
        send_tx_iq(txs, flexaddr, tx_packets(kind), 2.0)
        rate = (sim.tx_packets - n0) / (time.time() - t0)
        f, a, dc = tone(sim.tx_audio)
        print(f"  {mode}: {rate:.1f} DAX TX packets/s, audio {f:.1f} Hz amplitude {a:.3f} dc {dc:+.4f}")
        check(abs(rate - 187.5) < 10, f"{mode}: DAX TX packet rate")
        check(abs(f - 1000) < 5 and abs(a - amp) < 0.05 * amp + 0.01 and abs(dc) < 0.01,
              f"{mode}: 1 kHz audio at {amp}")
        drain(mic, 0.3)
        fwd = rev = 0
        for _ in range(20):
            rid, ip, tx, length, fwd, rev = MIC.unpack(mic.recvfrom(4096)[0][:MIC.size])
        check(abs(fwd - 50) < 0.5 and abs(rev - 1) < 0.05, f"{mode}: radio power meters {fwd:.1f} W / {rev:.2f} W reach dspserver")
        check(command(conn, 1, MOX, b"\0") == b"OK" and sim.xmit == 0, f"{mode}: MOX off -> xmit 0")
        time.sleep(0.2)
        n0 = sim.tx_packets
        time.sleep(0.3)
        check(sim.tx_packets == n0, f"{mode}: DAX TX stops with MOX off")

    check(not sim.tx_bad, f"DAX TX packets well formed {sim.tx_bad[:3]}")
    check(sim.tx_src_ports == {sim.udp_port}, f"DAX TX sent from the client UDP port {sim.tx_src_ports} {sim.udp_port}")
    check(command(conn, 1, DETACH, b"\0") == b"OK", "DETACH transmitter")
    time.sleep(0.2)
    check(sim.tx_stream is None and sim.transmit.get("dax") == "0", "radio: dax_tx stream removed, dax=0")
    check(command(conn, 1, MOX, b"\1").startswith(b"Error"), "MOX refused after detach")
    txs.close()
    mic.close()


def run(exe, scale, pan_reply, tx=True):
    print(f"--- simulated radio: sample scale {scale}, pan reply '{pan_reply}'")
    sim = FlexSim(carriers=[(14.2010e6, 0.5), (14.2500e6, 0.05)], scale=scale, pan_reply=pan_reply).start()

    lsock = socket.socket()
    lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    lsock.bind(("127.0.0.1", 10100))
    lsock.listen(1)
    iq = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    iq.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 22)
    iq.bind(("127.0.0.1", 10000))
    audio = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    audio.bind(("127.0.0.1", 10120))

    log = open("/tmp/flexradio_test.log", "w")
    proc = subprocess.Popen([exe, "127.0.0.1"], stderr=log, stdout=log)
    lsock.settimeout(20)
    try:
        conn, _ = lsock.accept()
    except socket.timeout:
        check(False, "flexradio found the simulated radio by discovery and connected to dspserver")
        proc.kill()
        sim.close()
        return
    check(True, "flexradio found the simulated radio by discovery and connected to dspserver")

    conn.sendall(bytes([0x7F, 3, 0, QHARDWARE, 0]))
    n = struct.unpack("<i", conn.recv(4))[0]
    xml = b""
    while len(xml) < n:
        xml += conn.recv(n - len(xml))
    xml = xml.decode().rstrip("\0")
    check("<radio_type>flex6000</radio_type>" in xml and "<name>FLEX-6600</name>" in xml and
          "<supported_transmitters>1</supported_transmitters>" in xml, "manifest")
    check(sim.udp_port is not None and sim.stream is not None, "radio: client udpport set and DAX IQ stream created")
    check(any(c.startswith("client gui") for c in sim.commands), "radio: registered as a GUI client")

    check(command(conn, 0, STARTRADIO, b"\0") == b"OK", "STARTRADIO")
    r = command(conn, 0, ATTACHRX, b"\0")
    check(r.startswith(b"OK"), f"ATTACHRX -> {r!r}")
    audio.settimeout(2)
    try:
        audio.recvfrom(16)
        check(True, "audio sync sent")
    except socket.timeout:
        check(False, "audio sync sent")

    check(command(conn, 0, SETFREQ, b"14200000") == b"OK", "SETFREQ 14.200 MHz")
    check(abs(sim.pan.get("center", 0) - 14.2) < 1e-9, f"radio: panadapter centre {sim.pan.get('center')}")
    check(sim.slice["mute"] == 1 and abs(sim.slice["freq"] - 14.2) < 1e-9, "radio: own slice muted and kept on frequency")

    for rate in (192000, 96000, 48000):
        check(command(conn, 0, SETSR, str(rate).encode()) == b"OK", f"SETSAMPLERATE {rate}")
        check(sim.stream["rate"] == rate, f"radio: DAX IQ rate {sim.stream['rate']}")
        time.sleep(0.3)
        drain(iq)
        t0 = time.time()
        x = recv_iq(iq, 1.5)
        measured = len(x) / (time.time() - t0)
        f, lvl = peak(x, rate)
        print(f"  {rate}: ~{measured:.0f} samples/s, peak {f:+.1f} Hz at {lvl:.1f} dBFS")
        check(abs(measured - rate) / rate < 0.1, "IQ rate")
        check(abs(f - 1000) < 2 * rate / len(x) + 5, "carrier 14.201 MHz at +1 kHz (I/Q order right)")
        check(abs(lvl - 20 * np.log10(0.5)) < 1.0, "carrier level -6 dBFS (scaling right)")
    check(command(conn, 0, SETSR, b"384000").startswith(b"Error"), "SETSAMPLERATE 384000 refused")

    check(command(conn, 0, 9 if False else SETRXANT, b"ANT2") == b"OK" and sim.pan.get("rxant") == "ANT2", "antenna ANT2")
    check(command(conn, 0, SETRXANT, b"FOO").startswith(b"Error"), "bad antenna refused")
    check(command(conn, 0, SETRFGAIN, b"15") == b"OK" and sim.pan.get("rfgain") == 20, f"RF gain 15 -> {sim.pan.get('rfgain')} (radio steps 0,10,20)")
    check(command(conn, 0, SETRFGAIN, b"99") == b"OK" and sim.pan.get("rfgain") == 20, "RF gain clamped to the radio's maximum")
    if tx:
        test_tx(conn, sim)
    check(command(conn, 0, STARTBS, b"\0").startswith(b"Error"), "bandscope refused")

    check(command(conn, 0, DETACH, b"\0") == b"OK", "DETACH")
    time.sleep(0.3)
    drain(iq, 2.0)
    iq.settimeout(0.5)
    try:
        iq.recvfrom(65536)
        check(False, "IQ stops after DETACH")
    except socket.timeout:
        check(True, "IQ stops after DETACH")

    conn.close()
    proc.send_signal(2)
    try:
        proc.wait(10)
        check(proc.returncode == 0, "flexradio exits cleanly on Ctrl-C")
    except subprocess.TimeoutExpired:
        proc.kill()
        check(False, "flexradio exits cleanly on Ctrl-C")
    time.sleep(0.3)
    check(any(c.startswith("stream remove") for c in sim.commands) and
          any(c.startswith("display pan remove") for c in sim.commands), "radio: stream and panadapter removed on exit")
    log.close()
    lost = [l for l in open("/tmp/flexradio_test.log") if "lost" in l]
    check(not lost, "no IQ packets lost")
    sim.close()
    for s in (lsock, iq, audio):
        s.close()
    time.sleep(1.5)


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "./flexradio"
    run(exe, 1.0, "0x40000000,0x42000000")
    run(exe, 32768.0, "pan=0x40000000", tx=False)
    print("FAILED" if failures else "ALL PASSED", f"({failures} failures)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
