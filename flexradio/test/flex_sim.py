#!/usr/bin/env python3
"""Simulated FLEX-6000 for testing flexradio without a radio.

Implements the parts of the SmartSDR API flexradio uses:
  - discovery: VITA-49 extension packets (class 0xFFFF) to UDP 4992
  - TCP 4992: V/H prologue, C<seq>|cmd -> R<seq>|code|msg, S status lines
  - DAX IQ: VITA-49 IF data packets, float32 little endian I/Q, carriers at
    absolute frequencies relative to the panadapter centre, real-time paced
  - transmit: dax_tx stream, "transmit set", "xmit"; DAX TX packets sent to
    UDP 4991 are checked and their audio kept in .tx_audio; while keyed the
    FWDPWR/REFPWR meters report .tx_watts

Usage as a module: FlexSim(carriers=[(14.2001e6, 0.5)]).start()
Run alone: python3 flex_sim.py   (Ctrl-C to stop)
"""
import socket
import struct
import threading
import time

import numpy as np

PCC_IQ = {24000: 0x02E3, 48000: 0x02E4, 96000: 0x02E5, 192000: 0x02E6}
OUI = 0x001C2D


def vita_header(ptype, has_class, trailer, count, words):
    return ((ptype & 0xF) << 28) | (0x08000000 if has_class else 0) | (0x04000000 if trailer else 0) \
        | (0x1 << 22) | (0x1 << 20) | ((count & 0xF) << 16) | (words & 0xFFFF)


class FlexSim:
    def __init__(self, carriers=None, model="FLEX-6600", serial="1234-5678-6600-0001",
                 discovery_to=("127.0.0.1", 4992), rfgain=(0, 20, 10), scale=1.0, big_endian=False,
                 pan_reply="0x40000000,0x42000000"):
        self.carriers = carriers or [(14.2001e6, 0.5)]
        self.model, self.serial = model, serial
        self.discovery_to = discovery_to
        self.rfgain_info = rfgain
        self.scale = scale
        self.big_endian = big_endian
        self.pan_reply = pan_reply
        self.handle = 0x1234ABCD
        self.udp_port = None
        self.client_ip = None
        self.pan = {}              # pan settings
        self.stream = None         # {"id", "channel", "rate"}
        self.slice = {"freq": 14.1, "mute": 0}
        self.commands = []         # every command received (for tests)
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.packets_sent = 0
        self.tx_stream = None      # dax_tx stream id
        self.transmit = {}         # transmit set key=value
        self.xmit = 0
        self.slice_tx = {}         # slice set tx/mode
        self.tx_audio = []         # left channel of every DAX TX packet, in order
        self.tx_packets = 0
        self.tx_bad = []           # reasons for packets that were wrong
        self.tx_src_ports = set()
        self.tx_watts = (50.0, 1.0)

    # ---------------------------------------------------------------- discovery
    def _discovery(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        count = 0
        while not self.stop.is_set():
            text = (f"discovery_protocol_version=3.0.0.2 model={self.model} serial={self.serial} "
                    f"version=3.9.19.0 nickname=SimFlex callsign=N0CALL ip=127.0.0.1 port=4992 "
                    f"status=Available").encode()
            text += b"\0" * (4 - len(text) % 4)
            words = 7 + len(text) // 4
            hdr = struct.pack(">IIIIIII", vita_header(3, True, False, count, words), 0x800,
                              OUI, (0x534C << 16) | 0xFFFF, 0, 0, 0)
            s.sendto(hdr + text, self.discovery_to)
            count += 1
            self.stop.wait(1.0)

    # --------------------------------------------------------------------- API
    def _status(self, conn, text):
        conn.sendall(f"S{self.handle:08X}|{text}\n".encode())

    def _pan_status(self, conn):
        p = self.pan
        self._status(conn, f"display pan 0x40000000 center={p['center']:.6f} bandwidth=0.200000 "
                           f"daxiq_channel={p.get('daxiq_channel', 0)} rxant={p.get('rxant', 'ANT1')} "
                           f"rfgain={p.get('rfgain', 0)}")

    def _stream_status(self, conn):
        st = self.stream
        self._status(conn, f"stream 0x{st['id']:08X} type=dax_iq daxiq_channel={st['channel']} pan=0x40000000 "
                           f"daxiq_rate={st['rate']} client_handle=0x{self.handle:08X} active=1 ip=127.0.0.1")

    def _command(self, conn, seq, cmd):
        with self.lock:
            self.commands.append(cmd)
        words = cmd.split()
        code, msg = 0, ""
        if cmd == "sub meter all":
            # an unrelated meter, then the TX power meters split over two lines
            self._status(conn, "meter 1.src=SLC#1.num=0#1.nam=LEVEL#1.low=-150.0#1.hi=20.0#1.unit=dBm#1.fps=10#")
            self._status(conn, "meter 9.src=TX-#9.num=1#9.nam=FWDPWR#9.low=0.0#9.hi=60.0#9.desc=RF Power Forward#"
                               "9.unit=dBm#9.fps=20#10.src=TX-#10.num=1#10.nam=REFPWR#10.low=0.0#10.hi=60.0#")
            self._status(conn, "meter 10.unit=dBm#10.fps=20#11.src=TX-#11.num=1#11.nam=SWR#11.unit=SWR#")
        elif cmd == "ping" or cmd.startswith(("client program", "client station", "keepalive", "sub ")):
            pass
        elif cmd == "stream create type=dax_tx":
            self.tx_stream = 0x84000000
            self._status(conn, f"stream 0x{self.tx_stream:08X} type=dax_tx client_handle=0x{self.handle:08X} tx=0")
            msg = f"{self.tx_stream:08X}"
        elif cmd.startswith("transmit set"):
            for kv in words[2:]:
                k, _, v = kv.partition("=")
                self.transmit[k] = v
        elif cmd.startswith("xmit"):
            if self.tx_stream is None:
                code = 0x50000016
            else:
                self.xmit = int(words[1])
        elif cmd.startswith("client udpport"):
            self.udp_port = int(words[2])
        elif cmd == "client gui" or cmd.startswith("client gui "):
            msg = "7A2E5C5E-0000-4000-8000-000000000001"
        elif cmd.startswith("display panafall create") or cmd.startswith("display pan create"):
            self.pan = {"center": 14.1, "daxiq_channel": 0}
            # status for the new slice and pan arrive before the reply, as on a radio
            self._status(conn, "slice 0 in_use=1 RF_frequency=14.100000 pan=0x40000000 mode=USB audio_mute=0")
            self._pan_status(conn)
            msg = self.pan_reply
        elif cmd.startswith("display pan rfgain_info"):
            lo, hi, step = self.rfgain_info
            msg = f"{lo},{hi},{step},0"
        elif cmd.startswith("display pan set") or cmd.startswith("display pan s "):
            if words[3].lower() != "0x40000000":
                code = 0x50000059
            else:
                for kv in words[4:]:
                    k, _, v = kv.partition("=")
                    if k == "center":
                        self.pan["center"] = float(v)
                    elif k in ("daxiq_channel", "rfgain"):
                        if k == "rfgain" and (int(v) - self.rfgain_info[0]) % self.rfgain_info[2] != 0:
                            code = 0x5000002D
                        self.pan[k] = int(v)
                    elif k == "rxant":
                        if v not in ("ANT1", "ANT2", "RX_A", "RX_B", "XVTR"):
                            code = 0x5000002D
                        self.pan[k] = v
                    elif k == "bandwidth":
                        self.pan[k] = float(v)
                    else:
                        code = 0x5000002D
                if code == 0:
                    self._pan_status(conn)
        elif cmd.startswith("stream create type=dax_iq"):
            ch = int(cmd.split("daxiq_channel=")[1])
            if self.pan.get("daxiq_channel") != ch:
                code, msg = 0x50000025, "pan has no such DAX IQ channel"
            else:
                self.stream = {"id": 0x20000000, "channel": ch, "rate": 48000}
                self._stream_status(conn)
                msg = "20000000"
        elif cmd.startswith("stream set"):
            rate = int(cmd.split("daxiq_rate=")[1])
            if rate < 1000:
                rate *= 1000
            if self.stream is None or rate not in PCC_IQ:
                code = 0x50000016
            else:
                self.stream["rate"] = rate
                self._stream_status(conn)
        elif cmd.startswith("stream remove"):
            if self.tx_stream is not None and int(words[2], 16) == self.tx_stream:
                self.tx_stream = None
            else:
                self.stream = None
        elif cmd.startswith("display pan remove"):
            self.pan = {}
        elif cmd.startswith("slice set"):
            if "audio_mute=1" in cmd:
                self.slice["mute"] = 1
            for kv in words[3:]:
                k, _, v = kv.partition("=")
                if k in ("tx", "mode"):
                    self.slice_tx[k] = v
                    if k == "mode" and v not in ("LSB", "USB", "AM", "SAM", "CW", "DIGL", "DIGU", "FM", "NFM", "DFM", "RTTY"):
                        code = 0x5000002D
        elif cmd.startswith("slice tune"):
            self.slice["freq"] = float(words[3])
        else:
            code, msg = 0x50000016, "unknown command"
        conn.sendall(f"R{seq}|{code:X}|{msg}\n".encode())

    def _api(self):
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", 4992))
        srv.listen(1)
        srv.settimeout(0.5)
        while not self.stop.is_set():
            try:
                conn, addr = srv.accept()
            except socket.timeout:
                continue
            self.client_ip = addr[0]
            conn.sendall(f"V3.9.19.0\nH{self.handle:08X}\n".encode())
            buf = b""
            conn.settimeout(0.5)
            while not self.stop.is_set():
                try:
                    data = conn.recv(4096)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.decode().strip()
                    if line.startswith(("C", "c")) and "|" in line:
                        seq, cmd = line[1:].split("|", 1)
                        self._command(conn, seq.lstrip("D"), cmd)
            conn.close()
            self.stream = None
            self.tx_stream = None
            self.xmit = 0
        srv.close()

    # ---------------------------------------------------------------- DAX IQ
    def _iq(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        n = 256                      # complex samples per packet
        count = 0
        phase = np.zeros(len(self.carriers))
        t_next = time.monotonic()
        while not self.stop.is_set():
            st, port = self.stream, self.udp_port
            if st is None or port is None or not self.pan:
                time.sleep(0.01)
                t_next = time.monotonic()
                continue
            rate = st["rate"]
            centre = self.pan["center"] * 1e6
            k = np.arange(n)
            x = np.zeros(n, dtype=np.complex128)
            for c, (freq, amp) in enumerate(self.carriers):
                w = 2 * np.pi * (freq - centre) / rate
                x += amp * np.exp(1j * (phase[c] + w * k))
                phase[c] = (phase[c] + w * n) % (2 * np.pi)
            x += (np.random.randn(n) + 1j * np.random.randn(n)) * 1e-4
            iq = np.empty(2 * n, dtype=np.float32)
            iq[0::2] = x.real * self.scale
            iq[1::2] = x.imag * self.scale
            payload = iq.astype(">f4" if self.big_endian else "<f4").tobytes()
            words = 7 + len(payload) // 4 + 1          # with a trailer
            hdr = struct.pack(">IIIIIII", vita_header(1, True, True, count, words), st["id"],
                              OUI, (0x534C << 16) | PCC_IQ[rate], 0, 0, count)
            s.sendto(hdr + payload + b"\0\0\0\0", ("127.0.0.1", port))
            count += 1
            self.packets_sent += 1
            if count % 10 == 0:
                # meters: SLC level, forward and reflected power (dBm * 128)
                fwd, ref = self.tx_watts if self.xmit else (1e-9, 1e-9)
                dbm = lambda w: int(round(10 * np.log10(w * 1000) * 128))
                pairs = struct.pack(">HhHhHh", 1, -100 * 128, 9, dbm(fwd), 10, dbm(ref))
                meter = struct.pack(">IIIIIII", vita_header(3, True, False, count, 7 + len(pairs) // 4), 0x00000700,
                                    OUI, (0x534C << 16) | 0x8002, 0, 0, 0) + pairs
                s.sendto(meter, ("127.0.0.1", port))
            t_next += n / rate
            delay = t_next - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            elif delay < -0.5:
                t_next = time.monotonic()

    # ---------------------------------------------------------------- DAX TX
    def _dax_tx(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", 4991))
        s.settimeout(0.2)
        expect = None
        while not self.stop.is_set():
            try:
                data, src = s.recvfrom(65536)
            except socket.timeout:
                continue
            self.tx_src_ports.add(src[1])
            bad = None
            if len(data) < 28:
                bad = "short"
            else:
                h, sid, oui, cls, tsi, tsf1, tsf2 = struct.unpack(">IIIIIII", data[:28])
                words, cnt = h & 0xFFFF, (h >> 16) & 0xF
                if h >> 28 != 1 or not h & 0x08000000 or h & 0x04000000:
                    bad = f"header type/flags 0x{h:08X}"
                elif (h >> 22) & 3 != 3 or (h >> 20) & 3 != 1:
                    bad = f"timestamp kinds 0x{h:08X}"
                elif words * 4 != len(data):
                    bad = f"size {words} words for {len(data)} bytes"
                elif sid != self.tx_stream:
                    bad = f"stream 0x{sid:08X}"
                elif oui != OUI or cls != (0x534C << 16) | 0x03E3:
                    bad = f"class 0x{oui:08X} 0x{cls:08X}"
                elif len(data) - 28 != 128 * 8:
                    bad = f"payload {len(data) - 28} bytes"
                elif expect is not None and cnt != expect:
                    bad = f"count {cnt} expected {expect}"
                expect = (cnt + 1) & 0xF
            if bad is not None:
                self.tx_bad.append(bad)
                continue
            st = np.frombuffer(data[28:], dtype=">f4")
            if not np.array_equal(st[0::2], st[1::2]):
                self.tx_bad.append("left and right differ")
            self.tx_packets += 1
            self.tx_audio.append(st[0::2].astype(np.float64))
        s.close()

    def start(self):
        for fn in (self._discovery, self._api, self._iq, self._dax_tx):
            threading.Thread(target=fn, daemon=True).start()
        return self

    def close(self):
        self.stop.set()


if __name__ == "__main__":
    sim = FlexSim().start()
    print("simulated FLEX running; Ctrl-C to stop")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        sim.close()
