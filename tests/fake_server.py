#!/usr/bin/env python3
"""Scenarios against the host build of amisqueeze: a fake slimproto server on localhost drives the real program
and checks what it sends (see "make test").

Usage: fake_server.py <path to the host build of amisqueeze> [<path to its build without MP3>]
"""
import math
import os
import random
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

import flac_data

TIMEOUT = 8
failures = 0


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


def mac_from_name(name):
    """Same derivation as player.c, written independently: 32-bit djb2 hash, 02:41:4D:xx:xx:xx"""
    h = 5381
    for c in name.encode("latin-1"):
        h = (h * 33 + c) & 0xFFFFFFFF
    return bytes([0x02, ord("A"), ord("M"), (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF])


def recv_exact(conn, n):
    data = b""
    conn.settimeout(TIMEOUT)
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            raise ConnectionError("connection closed by the player")
        data += chunk
    return data


def read_packet(conn):
    """Client -> server packet: 4-byte opcode, 4-byte length, data"""
    opcode = recv_exact(conn, 4)
    length = struct.unpack(">I", recv_exact(conn, 4))[0]
    return opcode, recv_exact(conn, length)


def send_packet(conn, opcode, body):
    """Server -> client packet: 2-byte length, then opcode and data"""
    payload = opcode + body
    conn.sendall(struct.pack(">H", len(payload)) + payload)


def strm(cmd, replay_gain=0, port=0, ip=0, fmt7=b"\0" * 7, request=b""):
    """strm body after the opcode: command, 7 format bytes, 6 more bytes, replay_gain, server port and ip"""
    return bytes([ord(cmd)]) + fmt7 + b"\0" * 6 + struct.pack(">IHI", replay_gain, port, ip) + request


def accept(server):
    server.settimeout(TIMEOUT)
    conn, _ = server.accept()
    return conn


def start(port, *args, program=None):
    return subprocess.Popen([program or sys.argv[1], "SERVER=127.0.0.1", "PORT=%d" % port] + list(args),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def stop(proc):
    """Ctrl-C; any exit code but 0 fails here, so that a scenario cannot forget it (a sanitizer's report at exit)"""
    proc.send_signal(signal.SIGINT)
    out, _ = proc.communicate(timeout=TIMEOUT)
    if proc.returncode != 0:
        check(False, "exit code %d after Ctrl-C, output:\n%s" % (proc.returncode, out))
    return proc.returncode, out


def scenario_session():
    print("-- session: HELO, strm t, audg, setd, server disconnect and reconnection, Ctrl-C")
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    port = server.getsockname()[1]
    proc = start(port, "NAME=Test", "VERBOSE")
    try:
        conn = accept(server)
        opcode, body = read_packet(conn)
        check(opcode == b"HELO", "HELO received first")
        check(body[0] == 12, "HELO device id 12 (squeezeplay)")
        check(body[2:8] == mac_from_name("Test"), "HELO MAC derived from the name")
        check(struct.unpack(">H", body[24:26])[0] == 0, "HELO first connection (no reconnection flag)")
        check(body[36:].startswith(b"Model=squeezelite,") and body.endswith(b",flc,pcm,mp3"),
              "HELO capabilities (FLAC=AUTO and MP3=AUTO on a fast machine: flc and mp3 advertised)")
        check(b",HasDigitalOut=1," in body, "HELO HasDigitalOut=1 (LMS then offers its fixed volume setting)")

        send_packet(conn, b"strm", strm("t", replay_gain=0x12345678))
        opcode, body = read_packet(conn)
        check(opcode == b"STAT" and len(body) == 53, "STAT of 53 bytes")
        check(body[0:4] == b"STMt", "strm t answered with STMt")
        check(struct.unpack(">I", body[47:51])[0] == 0x12345678, "STMt echoes the server timestamp")

        # audg: old gains, adjust, preamp, new 16.16 gains (no answer; printed with VERBOSE)
        send_packet(conn, b"audg", struct.pack(">IIBBII", 0, 0, 1, 255, 0x4000, 0x4000))
        send_packet(conn, b"audg", struct.pack(">IIBBII", 0, 0, 0, 255, 0x0F00, 0x0F00))

        send_packet(conn, b"setd", b"\0Renamed")
        opcode, body = read_packet(conn)
        check(opcode == b"SETD" and body == b"\0Renamed\0", "setd answered with SETD and the new name")

        conn.close()                    # after a few seconds: no real session, the player waits before retrying
        t_closed = time.time()
        conn = accept(server)
        waited = time.time() - t_closed
        opcode, body = read_packet(conn)
        check(opcode == b"HELO" and struct.unpack(">H", body[24:26])[0] == 0x4000,
              "reconnection after a disconnect, with the reconnection flag")
        check(4.5 <= waited <= 6.5, "short session: reconnection after the 5 s delay (%.1f s)" % waited)
        conn.close()
    finally:
        rc, out = stop(proc)
        server.close()
    check(rc == 0 and "stopped" in out, "Ctrl-C stops the player cleanly (exit code 0, \"stopped\")")
    check("player name: Renamed" in out, "new name printed")
    check("volume: 25.0 % of full amplitude" in out and "volume: fixed at 100 % by LMS" in out,
          "audg: volume set, then fixed volume (full)")
    check("connection closed by the server" in out, "disconnect reported")
    check("retrying in 5 s" in out, "wait announced")


PCM_BPS = 176400                        # 44.1 kHz, 16 bits, stereo
HTTP_HEADERS = b"HTTP/1.0 200 OK\r\nContent-Type: audio/L16;rate=44100;channels=2\r\n\r\n"
HTTP_REQUEST = b"GET /test.pcm?player=test HTTP/1.0\r\n\r\n"


def frames(seconds):
    """Bytes of `seconds` of audio, whole frames"""
    return int(PCM_BPS * seconds) // 4 * 4


class HttpServer(threading.Thread):
    """Serves one PCM track: `seconds` of audio, sent in chunks of `chunk` bytes every `pause` seconds. The bytes
    are pseudo-random (fixed `seed`): a byte lost, duplicated or moved shows in a comparison. A slow network:
    sending stops for `stall` seconds after `stall_at` bytes. A broken connection: it is reset (RST) after
    `cut_at` bytes."""

    def __init__(self, seconds=0.0, chunk=65536, pause=0.0, seed=1, stall_at=None, stall=0.0, cut_at=None, data=None):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.data = data if data is not None else random.Random(seed).randbytes(frames(seconds))
        self.chunk, self.pause = chunk, pause
        self.stall_at, self.stall, self.cut_at = stall_at, stall, cut_at
        self.request = b""
        self.closed_by_player = False

    def run(self):
        self.sock.settimeout(TIMEOUT)
        conn, _ = self.sock.accept()
        conn.settimeout(TIMEOUT)
        while not self.request.endswith(b"\r\n\r\n"):
            self.request += conn.recv(1)
        end = len(self.data) if self.cut_at is None else self.cut_at
        marks = sorted({end} | ({self.stall_at} if self.stall_at is not None else set()))
        try:
            conn.sendall(HTTP_HEADERS)
            pos = 0
            for mark in marks:
                for i in range(pos, mark, self.chunk):
                    conn.sendall(self.data[i:min(i + self.chunk, mark)])
                    time.sleep(self.pause)
                pos = mark
                if mark == self.stall_at:
                    time.sleep(self.stall)
        except OSError:
            self.closed_by_player = True
        if self.cut_at is not None:     # RST instead of a normal end, once the data sent has arrived
            time.sleep(0.5)
            conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        conn.close()
        self.sock.close()


class Session:
    """Control connection with the player, packets timestamped as they arrive"""

    def __init__(self, conn):
        self.conn, self.t0, self.log = conn, time.time(), []

    def until(self, wanted, timeout=TIMEOUT):
        """Reads packets up to the first one named `wanted` ("STAT:STMs", "RESP", "DSCO"...); returns it"""
        end = time.time() + timeout
        while time.time() < end:
            opcode, body = read_packet(self.conn)
            name = opcode.decode() + (":" + body[0:4].decode() if opcode == b"STAT" else "")
            self.log.append((time.time() - self.t0, name, body))
            if name == wanted:
                return self.log[-1]
        raise TimeoutError(wanted)

    def names(self):
        return [name for _, name, _ in self.log if name != "STAT:STMt"]


def stat_elapsed_ms(body):
    return struct.unpack(">I", body[43:47])[0]


def stat_bytes(body):
    high, low = struct.unpack(">II", body[15:23])
    return high << 32 | low


def start_session(name, *args):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    proc = start(server.getsockname()[1], "NAME=" + name, "VERBOSE", *args)
    conn = accept(server)
    read_packet(conn)                   # HELO
    return server, proc, Session(conn)


def send_strm_s(session, http, threshold_kb=64):
    fmt7 = b"1p1321" + bytes([threshold_kb])   # autostart 1, PCM 16 bits 44.1 kHz stereo little-endian
    send_packet(session.conn, b"strm", strm("s", port=http.port, ip=0, fmt7=fmt7, request=HTTP_REQUEST))


def scenario_stream():
    print("-- stream: 3 s of PCM, states, timing, counters")
    http = HttpServer(3.0)
    http.start()
    server, proc, s = start_session("Stream")
    try:
        send_strm_s(s, http)
        t_start, _, _ = s.until("STAT:STMs")
        _, _, body_d = s.until("STAT:STMd")
        t_end, _, _ = s.until("STAT:STMu", timeout=6)
        names = s.names()
        check(names[:4] == ["STAT:STMf", "STAT:STMc", "RESP", "STAT:STMs"],
              "STMf, STMc, RESP, STMs in this order (%s)" % names[:4])
        dsco = [b for _, n, b in s.log if n == "DSCO"]
        check(len(dsco) == 1 and dsco[0] == b"\0", "DSCO 0 (end of the data)")
        check(names.index("DSCO") < names.index("STAT:STMd") < names.index("STAT:STMu"), "DSCO, STMd, then STMu")
        resp = [b for _, n, b in s.log if n == "RESP"][0]
        check(resp == HTTP_HEADERS, "RESP carries the response headers")
        check(http.request == HTTP_REQUEST, "HTTP request sent as received in strm")
        check(stat_bytes(body_d) == len(http.data), "bytes received counted (%d)" % stat_bytes(body_d))
        check(2.85 <= t_end - t_start <= 3.4, "3 s of audio played in %.2f s" % (t_end - t_start))
        stmt = [(t, b) for t, n, b in s.log if n == "STAT:STMt"]
        check(len(stmt) >= 2, "STMt every second while playing (%d)" % len(stmt))
        drift = max(abs(stat_elapsed_ms(b) / 1000 - (t - t_start)) for t, b in stmt)
        check(drift < 0.25, "elapsed time follows the clock (max drift %.3f s)" % drift)
    finally:
        rc, out = stop(proc)
        server.close()
    check(rc == 0 and "end of playback" in out, "end of playback reported, clean exit")


def scenario_pause():
    print("-- stream: pause 1 s, then resume")
    http = HttpServer(2.0)
    http.start()
    server, proc, s = start_session("Pause")
    try:
        send_strm_s(s, http)
        t_start, _, _ = s.until("STAT:STMs")
        time.sleep(0.5)
        send_packet(s.conn, b"strm", strm("p"))
        t_pause, _, _ = s.until("STAT:STMp")
        time.sleep(1.0)
        send_packet(s.conn, b"strm", strm("u"))
        t_resume, _, _ = s.until("STAT:STMr")
        t_end, _, _ = s.until("STAT:STMu", timeout=6)
        played = (t_end - t_start) - (t_resume - t_pause)
        check(1.85 <= played <= 2.4, "2 s of audio played in %.2f s, plus %.2f s of pause"
              % (played, t_resume - t_pause))
    finally:
        stop(proc)
        server.close()


def scenario_stop():
    print("-- stream: stop while receiving")
    http = HttpServer(10.0, chunk=17640, pause=0.1)     # sent in real time
    http.start()
    server, proc, s = start_session("Stop")
    try:
        send_strm_s(s, http, threshold_kb=16)
        s.until("STAT:STMs")
        time.sleep(1.0)
        send_packet(s.conn, b"strm", strm("q"))
        s.until("STAT:STMf")
        http.join(timeout=TIMEOUT)
        check(http.closed_by_player, "stream connection closed by the player")
        time.sleep(1.5)
        send_packet(s.conn, b"strm", strm("t", replay_gain=1))
        _, _, body = s.until("STAT:STMt")
        check(stat_elapsed_ms(body) == 0 and "STAT:STMu" not in s.names(), "stopped: no more playback")
    finally:
        stop(proc)
        server.close()


def scenario_flush_refused():
    print("-- strm f drops the next track while it streams; a format refused (STMc, STMn); the track after them follows")
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    http_a, http_c = HttpServer(1.5, seed=11), HttpServer(1.0, seed=13)
    http_b = HttpServer(5.0, chunk=17640, pause=0.1, seed=12)  # sent in real time: still streaming at strm f
    for h in (http_a, http_b, http_c):
        h.start()
    server, proc, s = start_session("Flush", "OUTPUT=" + path)
    try:
        send_strm_s(s, http_a)
        s.until("STAT:STMd")
        send_strm_s(s, http_b, threshold_kb=16)
        s.until("RESP")
        send_packet(s.conn, b"strm", strm("f"))
        s.until("STAT:STMf")
        http_b.join(timeout=TIMEOUT)
        check(http_b.closed_by_player, "strm f: the next track's stream closed by the player")
        before = len(s.names())
        fmt7 = b"1a????" + bytes([64])  # AAC: not decoded
        send_packet(s.conn, b"strm", strm("s", port=http_c.port, ip=0, fmt7=fmt7, request=HTTP_REQUEST))
        s.until("STAT:STMn")
        check(s.names()[before:] == ["STAT:STMf", "STAT:STMc", "STAT:STMn"],
              "refused format: STMf, STMc, STMn (%s)" % s.names()[before:])
        send_strm_s(s, http_c)
        s.until("STAT:STMu", timeout=8)
        check(s.names().count("STAT:STMs") == 2 and s.names().count("DSCO") == 2,
              "A then C started (STMs twice), a DSCO for each, none for B")
    finally:
        rc, out = stop(proc)
        server.close()
    data = read_output(path)
    want = http_a.data + http_c.data
    check(data == want, "file: A then C, nothing of B (%d bytes, %d expected, first difference at %d)"
          % (len(data), len(want), first_difference(data, want)))
    check("stream format a not supported" in out, "refused format reported")


def first_difference(a, b):
    n = min(len(a), len(b))
    return next((i for i in range(n) if a[i] != b[i]), n)


def scenario_output():
    print("-- OUTPUT file: two tracks back to back (gapless), file compared with the data sent")
    tmp = tempfile.mkdtemp()
    path = os.path.join(tmp, "play.pcm")
    http1, http2 = HttpServer(1.5, seed=1), HttpServer(1.0, seed=2)
    http1.start()
    http2.start()
    server, proc, s = start_session("Output", "OUTPUT=" + path)
    try:
        send_strm_s(s, http1)
        s.until("STAT:STMd")
        send_strm_s(s, http2)           # sent by LMS after STMd: appended to the buffer
        s.until("STAT:STMd")
        s.until("STAT:STMu", timeout=6)
        check(s.names().count("STAT:STMs") == 2, "STMs for each track")
    finally:
        rc, out = stop(proc)
        server.close()
    with open(path, "rb") as f:
        data = f.read()
    sent = http1.data + http2.data
    check(len(data) == len(sent), "file size %d bytes, %d sent" % (len(data), len(sent)))
    check(data == sent, "file identical to the data sent (first difference at byte %d)"
          % first_difference(data, sent))
    check("output: %d bytes written to %s" % (len(sent), path) in out, "bytes written reported")
    os.remove(path)
    os.rmdir(tmp)


def read_output(path):
    with open(path, "rb") as f:
        data = f.read()
    os.remove(path)
    os.rmdir(os.path.dirname(path))
    return data


def wav_header(bits=24, rate=44100, channels=2):
    """A WAV header like Music Assistant's (seen in WinUAE on 2026-10-21): WAVE_FORMAT_EXTENSIBLE, a LIST chunk of
    26 bytes, the data's size unknown (FFFFFFFF); 102 bytes"""
    frame = channels * bits // 8
    guid = bytes([1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71])
    fmt = struct.pack("<HHIIHHHHI", 0xfffe, channels, rate, rate * frame, frame, bits, 22, bits, 3) + guid
    info = b"INFO" + b"ISFT" + struct.pack("<I", 14) + b"x" * 14
    header = (b"RIFF" + struct.pack("<I", 0xffffffff) + b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
              + b"LIST" + struct.pack("<I", len(info)) + info + b"data" + struct.pack("<I", 0xffffffff))
    assert len(header) == 102
    return header


def sox_wav_header(rate, bits=16, channels=2):
    """A WAV header as sox writes it into a pipe, behind an LMS conversion rule (seen on 2026-10-26): a plain PCM
    format chunk, the sizes 7FFFF000 (data) and 7FFFF024 (RIFF); 44 bytes"""
    frame = channels * bits // 8
    fmt = struct.pack("<HHIIHH", 1, channels, rate, rate * frame, frame, bits)
    return (b"RIFF" + struct.pack("<I", 0x7ffff024) + b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"data" + struct.pack("<I", 0x7ffff000))


def send_strm_wav(session, http):
    fmt7 = b"3p1321" + bytes([64])      # as Music Assistant: autostart 3, PCM announced as 16 bits 44.1 kHz stereo
    send_packet(session.conn, b"strm", strm("s", port=http.port, ip=0, fmt7=fmt7, request=HTTP_REQUEST))


def scenario_wav():
    print("-- WAV header (Music Assistant's wav codec): two 24-bit tracks announced as 16 bits, back to back")
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    rng = random.Random(3)
    pcm1, pcm2 = rng.randbytes(6 * 44100 * 3 // 2), rng.randbytes(6 * 44100)     # 1.5 s and 1 s of 24-bit stereo
    http1, http2 = HttpServer(data=wav_header() + pcm1), HttpServer(data=wav_header() + pcm2)
    http1.start()
    http2.start()
    server, proc, s = start_session("Wav", "OUTPUT=" + path)
    try:
        send_strm_wav(s, http1)
        s.until("STAT:STMd")
        send_strm_wav(s, http2)         # the next track, appended to the buffer with its own header
        t_end, _, _ = s.until("STAT:STMu", timeout=6)
        starts = [t for t, n, _ in s.log if n == "STAT:STMs"]
        check(len(starts) == 2, "STMs for each track")
        check(1.4 <= starts[1] - starts[0] <= 1.7 and 0.9 <= t_end - starts[1] <= 1.25,
              "1.5 s and 1 s of 24-bit audio played in %.2f s and %.2f s"
              % (starts[1] - starts[0], t_end - starts[1]))
        stmt = [(t, b) for t, n, b in s.log if n == "STAT:STMt" and t < starts[1]]
        drift = max(abs(stat_elapsed_ms(b) / 1000 - (t - starts[0])) for t, b in stmt)
        check(drift < 0.25, "elapsed time follows the clock (max drift %.3f s)" % drift)
    finally:
        rc, out = stop(proc)
        server.close()
    data = read_output(path)
    check(data == pcm1 + pcm2, "file identical to the data without the headers (first difference at byte %d)"
          % first_difference(data, pcm1 + pcm2))
    check(out.count("stream: WAV header: 24 bits, 44100 Hz, 2 channel(s), to the end of the stream") == 2,
          "WAV header reported for each track")


def scenario_wav_rate():
    print("-- WAV header at 22050 Hz behind a strm that says 44.1 kHz (LMS with a resampling rule, MAXRATE)")
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    pcm = random.Random(4).randbytes(22050 * 4 * 3 // 2)        # 1.5 s of 16-bit stereo at 22050 Hz
    http = HttpServer(data=sox_wav_header(22050) + pcm)
    http.start()
    server, proc, s = start_session("WavRate", "OUTPUT=" + path)
    try:
        send_strm_s(s, http, threshold_kb=16)
        t_start, _, _ = s.until("STAT:STMs")
        t_end, _, _ = s.until("STAT:STMu", timeout=6)
        check(1.35 <= t_end - t_start <= 1.9, "1.5 s at 22050 Hz played in %.2f s (0.75 s at strm's 44.1 kHz)"
              % (t_end - t_start))
        stmt = [(t, b) for t, n, b in s.log if n == "STAT:STMt"]
        drift = max((abs(stat_elapsed_ms(b) / 1000 - (t - t_start)) for t, b in stmt), default=float("inf"))
        check(drift < 0.25, "elapsed time follows the clock (max drift %.3f s)" % drift)
    finally:
        rc, out = stop(proc)
        server.close()
    check(rc == 0, "stopped cleanly (exit code %d)" % rc)
    data = read_output(path)
    check(data == pcm, "file identical to the data without the header (first difference at byte %d)"
          % first_difference(data, pcm))
    check("stream: WAV header: 16 bits, 22050 Hz, 2 channel(s), 2147479552 bytes" in out, "WAV header reported")


def scenario_stall():
    # In the middle of a frame: the ring keeps the start of a frame the decoder cannot use, and is not empty
    for extra, where in ((0, "after a whole frame"), (2, "in the middle of a frame")):
        print("-- slow network: the stream stops for 3 s %s, the buffer runs dry (STMo), then playback resumes"
              % where)
        path = os.path.join(tempfile.mkdtemp(), "play.pcm")
        http = HttpServer(4.0, stall_at=frames(1.5) + extra, stall=3.0)
        http.start()
        server, proc, s = start_session("Stall", "OUTPUT=" + path)
        try:
            send_strm_s(s, http, threshold_kb=16)
            t_start, _, _ = s.until("STAT:STMs")
            t_dry, _, _ = s.until("STAT:STMo")
            t_end, _, _ = s.until("STAT:STMu", timeout=10)
            check(1.3 <= t_dry - t_start <= 1.8, "buffer empty after the 1.5 s received (%.2f s)" % (t_dry - t_start))
            check(s.names().count("STAT:STMo") == 1, "STMo once")
            # 1.5 s played, a wait until the data comes back (3 s after it stopped), then the 2.5 s left
            check(5.2 <= t_end - t_start <= 6.0, "4 s of audio played in %.2f s, including the wait"
                  % (t_end - t_start))
        finally:
            rc, out = stop(proc)
            server.close()
        data = read_output(path)
        check(data == http.data, "nothing lost nor repeated (%d bytes of %d, first difference at byte %d)"
              % (len(data), len(http.data), first_difference(data, http.data)))
        check("output: buffer empty, waiting for data" in out, "buffer empty reported")


def scenario_cut():
    print("-- broken stream: the connection is reset after 1.5 s of audio")
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    http = HttpServer(4.0, cut_at=frames(1.5))
    http.start()
    server, proc, s = start_session("Cut", "OUTPUT=" + path)
    try:
        send_strm_s(s, http, threshold_kb=16)
        t_start, _, _ = s.until("STAT:STMs")
        _, _, dsco = s.until("DSCO")
        s.until("STAT:STMd")
        t_end, _, _ = s.until("STAT:STMu", timeout=6)
        check(dsco == b"\2", "DSCO 2 (connection lost on the server's side), got %r" % dsco)
        check(1.3 <= t_end - t_start <= 1.9, "what arrived is played to the end (%.2f s)" % (t_end - t_start))
    finally:
        rc, out = stop(proc)
        server.close()
    data = read_output(path)
    check(data == http.data[:frames(1.5)], "the 1.5 s received played as sent (%d bytes, first difference at %d)"
          % (len(data), first_difference(data, http.data)))
    check(rc == 0 and "stream: recv: error" in out, "error reported, clean exit")


def scenario_control_lost():
    print("-- server lost during playback, after more than 10 s: playback stops, the player reconnects at once")
    http = HttpServer(15.0, chunk=17640, pause=0.1)     # sent in real time: still streaming when the server goes
    http.start()
    server, proc, s = start_session("Lost")
    try:
        send_strm_s(s, http, threshold_kb=16)
        s.until("STAT:STMs")
        time.sleep(10.5)
        s.conn.close()
        t_closed = time.time()
        conn = accept(server)
        waited = time.time() - t_closed
        opcode, body = read_packet(conn)
        check(opcode == b"HELO" and struct.unpack(">H", body[24:26])[0] == 0x4000,
              "reconnection with the reconnection flag")
        check(waited < 1.0, "real session: reconnection at once (%.1f s)" % waited)
        http.join(timeout=TIMEOUT)
        check(http.closed_by_player, "stream connection closed by the player")
        s = Session(conn)
        time.sleep(1.0)
        send_packet(conn, b"strm", strm("t", replay_gain=1))
        _, _, body = s.until("STAT:STMt")
        check(stat_elapsed_ms(body) == 0 and "STAT:STMu" not in s.names(), "nothing playing after the reconnection")
        conn.close()
    finally:
        rc, out = stop(proc)
        server.close()
    # Closed with the player's STAT still unread, the connection may end with a reset: "recv: error"
    reported = "connection closed by the server" in out or "amisqueeze: recv: error" in out
    check(rc == 0 and reported, "loss reported, clean exit (exit code %s)" % rc)


def scenario_silent_server():
    # Plan step player-16: on the A1200 the player sat in a blocking receive and never said it (AmiNetXDuo); here
    # the core's side: the 35 s timeout runs while the output waits for data
    print("-- server silent without closing, while playing after STMo: \"no news\" after 35 s, then a reconnection")
    http = HttpServer(4.0, stall_at=frames(1.5), stall=40.0)
    http.start()
    server, proc, s = start_session("Silent")
    try:
        send_strm_s(s, http, threshold_kb=16)
        t_last = time.time()            # the server's last packet; it reads nothing more (the STAT pile up)
        s.until("STAT:STMs")
        s.until("STAT:STMo")
        server.settimeout(45)
        conn, _ = server.accept()
        waited = time.time() - t_last
        opcode, body = read_packet(conn)
        check(opcode == b"HELO" and struct.unpack(">H", body[24:26])[0] == 0x4000,
              "reconnection with the reconnection flag")
        check(34.5 <= waited <= 37.5, "35 s after the server's last packet (%.1f s)" % waited)
        conn.close()
    finally:
        rc, out = stop(proc)
        server.close()
    check(rc == 0 and "no news from the server, reconnecting" in out, "silence reported, clean exit")


FLAC_FILES = [                          # file of tests/data, signal of flac_data.py
    ("s16-l0.flac", "s16"), ("s16-l5.flac", "s16"), ("s16-l8.flac", "s16"), ("m16-l5.flac", "m16"),
    ("s24-l5.flac", "s24"), ("s16-48k-l5.flac", "s16-48k"), ("s16-l5-pad.flac", "s16"),
]


def flac_file(name):
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", name), "rb") as f:
        return f.read()


def send_strm_flac(session, http, threshold_kb=64):
    fmt7 = b"1f????" + bytes([threshold_kb])    # autostart 1, FLAC: the parameters are in the stream
    send_packet(session.conn, b"strm", strm("s", port=http.port, ip=0, fmt7=fmt7, request=HTTP_REQUEST))


def scenario_flac():
    print("-- FLAC: each test file decoded byte for byte, sent in pieces of 3000 bytes (frames arrive cut)")
    for name, signal in FLAC_FILES:
        path = os.path.join(tempfile.mkdtemp(), "play.pcm")
        http = HttpServer(data=flac_file(name), chunk=3000, pause=0.001)
        http.start()
        server, proc, s = start_session("Flac", "OUTPUT=" + path)
        try:
            send_strm_flac(s, http)
            s.until("STAT:STMs")
            s.until("STAT:STMd")
            s.until("STAT:STMu", timeout=10)
        finally:
            rc, out = stop(proc)
            server.close()
        data, want = read_output(path), flac_data.expected(signal)
        check(data == want and rc == 0, "%s: %d bytes, %d expected, first difference at %d"
              % (name, len(data), len(want), first_difference(data, want)))
        if data != want:
            print(out)


def scenario_flac_stall():
    print("-- FLAC, slow network: the stream stops for 3 s in the middle of a frame, STMo, then playback resumes")
    data = flac_file("s16-l5.flac")
    stall_at = data.index(b"\xff\xf8", 60000) + 100     # 100 bytes after a frame's sync code
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    http = HttpServer(data=data, chunk=3000, pause=0.001, stall_at=stall_at, stall=3.0)
    http.start()
    server, proc, s = start_session("FlacStall", "OUTPUT=" + path)
    try:
        # The decoder waits for its largest frame (about 15 KB here) after the metadata: below that threshold, the
        # output would start with nothing to play, and say so (STMo); the servers ask for 200 KB or more
        send_strm_flac(s, http, threshold_kb=32)
        t_start, _, _ = s.until("STAT:STMs")
        s.until("STAT:STMo")
        t_end, _, _ = s.until("STAT:STMu", timeout=10)
        check(s.names().count("STAT:STMo") == 1, "STMo once")
        check(3.0 <= t_end - t_start <= 4.5, "1 s of audio played in %.2f s, including the wait" % (t_end - t_start))
    finally:
        rc, out = stop(proc)
        server.close()
    data, want = read_output(path), flac_data.expected("s16")
    check(data == want and rc == 0, "decoded byte for byte (%d bytes, %d expected, first difference at %d)"
          % (len(data), len(want), first_difference(data, want)))


def scenario_flac_rate_0():
    print("-- FLAC whose STREAMINFO gives a sample rate of 0: track skipped, the output does not stall")
    data = bytearray(flac_file("s16-l5.flac"))
    data[18] = data[19] = 0                 # STREAMINFO body at 8: its rate is the 20 bits at 18
    data[20] &= 0x0F
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    http = HttpServer(data=bytes(data), chunk=3000, pause=0.001)
    http.start()
    server, proc, s = start_session("Flac", "OUTPUT=" + path)
    try:
        send_strm_flac(s, http)
        s.until("STAT:STMd")
        try:
            s.until("STAT:STMu", timeout=5)
            ended = True
        except TimeoutError:
            ended = False
    finally:
        rc, out = stop(proc)
        server.close()
    check(ended and rc == 0 and len(read_output(path)) == 0 and "flac: sample rate 0, track skipped" in out,
          "nothing played, reported, STMu sent")


def crc8(data):
    """CRC-8 of a FLAC frame header (polynomial 0x07)"""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc << 1 ^ 0x07 if crc & 0x80 else crc << 1) & 0xFF
    return crc


def flac_frame(data, n):
    """(offset, PCM frames per FLAC frame) of frame n of a test FLAC file (fixed block size, n < 128): a frame sync
    whose header has the right frame number and CRC-8, past the metadata blocks; its header is 6 bytes long"""
    pos, last = 4, False
    while not last:
        last, size = data[pos] & 0x80, int.from_bytes(data[pos + 1:pos + 4], "big")
        if data[pos] & 0x7f == 0:
            block = int.from_bytes(data[pos + 6:pos + 8], "big")        # STREAMINFO: largest block
        pos += 4 + size
    while True:
        pos = data.index(b"\xff\xf8", pos)
        if data[pos + 4] == n and data[pos + 2] >> 4 not in (0, 6, 7) and data[pos + 2] & 15 in (9, 10) \
                and data[pos + 5] == crc8(data[pos:pos + 4] + bytes([n])):    # header up to the frame number, here
            return pos, block
        pos += 1


def scenario_flac_seek():
    print("-- FLAC seek as LMS sends it: the file from a frame in the middle, without its header: the rest of the track")
    for name, signal, n in [("s16-l5.flac", "s16", 5), ("s16-l0.flac", "s16", 0), ("m16-l5.flac", "m16", 3),
                            ("s24-l5.flac", "s24", 7), ("s16-48k-l5.flac", "s16-48k", 10)]:
        file = flac_file(name)
        offset, block = flac_frame(file, n)
        path = os.path.join(tempfile.mkdtemp(), "play.pcm")
        http = HttpServer(data=file[offset:], chunk=3000, pause=0.001)
        http.start()
        server, proc, s = start_session("FlacSeek", "OUTPUT=" + path)
        try:
            send_strm_flac(s, http)
            s.until("STAT:STMd")
            s.until("STAT:STMu", timeout=10)
        finally:
            rc, out = stop(proc)
            server.close()
        want = flac_data.expected(signal)[n * block * flac_data.SIGNALS[signal][1] * 2:]
        data = read_output(path)
        check(data == want and rc == 0 and "skipped" not in out,
              "%s from frame %d: %d bytes, %d expected, first difference at %d"
              % (name, n, len(data), len(want), first_difference(data, want)))
        if data != want:
            print(out)


def scenario_flac_seek_rejected():
    print("-- FLAC seek whose first frame leaves its rate or sample size to STREAMINFO (not sent): track skipped")
    for what, byte, mask in [("rate", 2, 0x0F), ("sample size", 3, 0x0E)]:
        file = flac_file("s16-l5.flac")
        offset, _ = flac_frame(file, 5)
        data = bytearray(file[offset:])
        data[byte] &= ~mask & 0xFF          # code 0: "get it from STREAMINFO"
        data[5] = crc8(data[:5])            # a valid header, as an encoder would write it
        path = os.path.join(tempfile.mkdtemp(), "play.pcm")
        http = HttpServer(data=bytes(data), chunk=3000, pause=0.001)
        http.start()
        server, proc, s = start_session("FlacSeek", "OUTPUT=" + path)
        try:
            send_strm_flac(s, http)
            s.until("STAT:STMd")
            try:
                s.until("STAT:STMu", timeout=5)
                ended = True
            except TimeoutError:
                ended = False
        finally:
            rc, out = stop(proc)
            server.close()
        check(ended and rc == 0 and len(read_output(path)) == 0
              and "flac: cannot start from the middle of this file, track skipped" in out,
              "%s in STREAMINFO: nothing played, reported, STMu sent" % what)


def scenario_flac_gapless():
    print("-- FLAC, FLAC, then raw PCM, back to back: each track complete and in place (the decoder reads ahead)")
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    tracks = [HttpServer(data=flac_file("s16-l5.flac")), HttpServer(data=flac_file("m16-l5.flac")),
              HttpServer(0.5, seed=7)]
    for h in tracks:
        h.start()
    server, proc, s = start_session("Gapless", "OUTPUT=" + path)
    try:
        send_strm_flac(s, tracks[0])
        s.until("STAT:STMd")
        send_strm_flac(s, tracks[1])
        s.until("STAT:STMd")
        send_strm_s(s, tracks[2])
        s.until("STAT:STMd")
        s.until("STAT:STMu", timeout=10)
        check(s.names().count("STAT:STMs") == 3, "STMs for each track (%d)" % s.names().count("STAT:STMs"))
    finally:
        rc, out = stop(proc)
        server.close()
    data = read_output(path)
    want = flac_data.expected("s16") + flac_data.expected("m16") + tracks[2].data
    check(data == want, "file: %d bytes, %d expected, first difference at %d"
          % (len(data), len(want), first_difference(data, want)))


MP3_FILES = [                           # file of tests/data, signal of flac_data.py
    ("s16-128.mp3", "s16"), ("s16-v2.mp3", "s16"), ("s16-320-raw.mp3", "s16"), ("m16-64.mp3", "m16"),
    ("s16-48k-192.mp3", "s16-48k"), ("s16-22k-64.mp3", "s16-22k"),
]
MP3_NO_HEADER = {"s16-320-raw.mp3"}     # made without a LAME header (lame -t)
MP3_MIN_SNR = 20.0                      # dB, decoded against the signal the file was made from
mp3_decoded = {}                        # file: output of scenario_mp3, reference of the following scenarios


def send_strm_mp3(session, http, threshold_kb=64):
    fmt7 = b"1m????" + bytes([threshold_kb])    # autostart 1, MP3: the parameters are in the stream
    send_packet(session.conn, b"strm", strm("s", port=http.port, ip=0, fmt7=fmt7, request=HTTP_REQUEST))


def play_mp3(data, chunk=3000):
    """Plays one MP3 track sent in pieces of `chunk` bytes: output, elapsed time at the end (ms), exit code, log"""
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    http = HttpServer(data=data, chunk=chunk, pause=0.001)
    http.start()
    server, proc, s = start_session("Mp3", "OUTPUT=" + path)
    try:
        send_strm_mp3(s, http)
        s.until("STAT:STMd")
        _, _, body = s.until("STAT:STMu", timeout=10)
    finally:
        rc, out = stop(proc)
        server.close()
    return read_output(path), stat_elapsed_ms(body), rc, out


def mp3_quality(data, signal):
    """Delay (PCM frames: the encoder's and the decoder's, and the first frame of a LAME header, decoded as silence)
    and signal-to-noise ratio (dB) of a decoded MP3 against the signal it was made from"""
    channels = flac_data.SIGNALS[signal][1]
    got = struct.unpack("<%dh" % (len(data) // 2), data)
    want = struct.unpack("<%dh" % (len(flac_data.expected(signal)) // 2), flac_data.expected(signal))
    probe = range(4000 * channels, 4512 * channels, channels)      # 512 samples of the first channel
    if len(got) < 8600 * channels:
        return None, 0.0
    delay = min(range(4000), key=lambda d: sum((got[i + d * channels] - want[i]) ** 2 for i in probe))
    n = min(len(want), len(got) - delay * channels)
    noise = sum((got[i + delay * channels] - want[i]) ** 2 for i in range(n)) or 1
    return delay, 10 * math.log10(sum(v * v for v in want[:n]) / noise)


def scenario_mp3():
    print("-- MP3: each test file against its signal (with a LAME header: from its first sample, its exact length);"
          " sent in pieces of 3000 bytes or at once, the same bytes")
    for name, signal in MP3_FILES:
        rate, channels = flac_data.SIGNALS[signal][:2]
        data, elapsed, rc, out = play_mp3(flac_file(name))
        delay, snr = mp3_quality(data, signal)
        played_ms = len(data) * 1000 // (rate * channels * 2)
        if name in MP3_NO_HEADER:       # nothing to trim: the encoder's and the decoder's delays stay
            in_place = delay == 576 + 529
        else:
            in_place = delay == 0 and len(data) == len(flac_data.expected(signal))
        check(rc == 0 and in_place and snr >= MP3_MIN_SNR and abs(elapsed - played_ms) < 100,
              "%s: %d bytes (signal %d), delay %s frames, SNR %.1f dB, %d ms played at %d Hz (%d ms reported)"
              % (name, len(data), len(flac_data.expected(signal)), delay, snr, played_ms, rate, elapsed))
        whole, _, _, _ = play_mp3(flac_file(name), chunk=1 << 20)
        check(whole == data, "%s at once: %d bytes, first difference at %d"
              % (name, len(whole), first_difference(whole, data)))
        if rc != 0 or whole != data:
            print(out)
        mp3_decoded[name] = data


def id3v2(size, footer=False):
    """An ID3v2 tag of `size` bytes in all, with a footer if asked (version 2.4)"""
    body = size - 10 - (10 if footer else 0)
    synchsafe = bytes([(body >> 21) & 0x7f, (body >> 14) & 0x7f, (body >> 7) & 0x7f, body & 0x7f])
    head = (b"ID3\x04\x00\x10" if footer else b"ID3\x03\x00\x00") + synchsafe
    return head + bytes(body) + (b"3DI\x04\x00\x10" + synchsafe if footer else b"")


def scenario_mp3_tags():
    print("-- MP3 tags: two more ID3v2 tags first (one bigger than the stream buffer, with a footer), an ID3v1 last")
    data = id3v2(1258291, footer=True) + id3v2(3000) + flac_file("s16-128.mp3") + b"TAG" + bytes(125)
    out, _, rc, log = play_mp3(data, chunk=65536)
    want = mp3_decoded["s16-128.mp3"]
    check(rc == 0 and out == want, "%d bytes, %d expected, first difference at %d"
          % (len(out), len(want), first_difference(out, want)))
    if out != want:
        print(log)


def scenario_mp3_seek():
    print("-- MP3 from the middle of a file (a seek), not on a frame boundary: the same audio once in step")
    file = flac_file("s16-320-raw.mp3")
    out, _, rc, log = play_mp3(file[len(file) // 2 + 7:])
    full = mp3_decoded["s16-320-raw.mp3"]
    n = len(out) // 2
    check(rc == 0 and n > 0 and out[-n:] == full[-n:] and len(out) <= len(full) // 2,
          "%d bytes, the last %d as in the whole file" % (len(out), n))
    if rc != 0:
        print(log)


def mp3_frames(data):
    """(offset, size) of the MPEG-1 layer III frames of an MP3 file, after its ID3v2 tag (the test files only)"""
    bitrates = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
    rates = [44100, 48000, 32000]
    pos = 10 + ((data[6] & 0x7f) << 21 | (data[7] & 0x7f) << 14 | (data[8] & 0x7f) << 7 | data[9] & 0x7f) \
        if data[:3] == b"ID3" else 0
    frames = []
    while pos + 4 <= len(data) and data[pos] == 0xFF and (data[pos + 1] & 0xE0) == 0xE0:
        size = 144000 * bitrates[data[pos + 2] >> 4] // rates[(data[pos + 2] >> 2) & 3] + ((data[pos + 2] >> 1) & 1)
        frames.append((pos, size))
        pos += size
    return frames


def scenario_mp3_seek_lms():
    print("-- MP3 seek as LMS sends it: the file's first frame (LAME header), then the data from a frame in the middle")
    file = flac_file("s16-128.mp3")
    frames = mp3_frames(file)
    first, middle = frames[0], frames[len(frames) // 2]
    out, _, rc, log = play_mp3(file[first[0]:first[0] + first[1]] + file[middle[0]:])
    full = mp3_decoded["s16-128.mp3"]
    n = len(out) // 2
    # The header asks to drop the delays at the start (a few ms after the seek point) and gives the file's length,
    # never reached: the end of the track is played in full, as in the whole file
    check(rc == 0 and n > 0 and full[-n:] in out, "%d bytes, the end of the track as in the whole file" % len(out))
    if rc != 0:
        print(log)


def scenario_mp3_continuous():
    print("-- MP3 gapless: one continuous signal cut in two files, back to back: the signal again, without a gap")
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    tracks = [HttpServer(data=flac_file("cont-a.mp3")), HttpServer(data=flac_file("cont-b.mp3"))]
    for h in tracks:
        h.start()
    server, proc, s = start_session("Gapless", "OUTPUT=" + path)
    try:
        send_strm_mp3(s, tracks[0])
        s.until("STAT:STMd")
        send_strm_mp3(s, tracks[1])
        s.until("STAT:STMd")
        s.until("STAT:STMu", timeout=10)
    finally:
        rc, out = stop(proc)
        server.close()
    data = read_output(path)
    want = flac_data.expected("cont")
    delay, snr = mp3_quality(data, "cont")
    check(rc == 0 and delay == 0 and len(data) == len(want) and snr >= MP3_MIN_SNR,
          "%d bytes (signal %d), delay %s frames, SNR %.1f dB" % (len(data), len(want), delay, snr))
    # Around the cut (1.3 s), 0.1 s on each side: a gap or a click would show here
    cut = flac_data.PARTS["cont-b"][1] * 2
    got = struct.unpack("<%dh" % 17640, data[(cut - 8820) * 2:(cut + 8820) * 2])
    ref = struct.unpack("<%dh" % 17640, want[(cut - 8820) * 2:(cut + 8820) * 2])
    noise = sum((a - b) ** 2 for a, b in zip(got, ref)) or 1
    local = 10 * math.log10(sum(v * v for v in ref) / noise)
    check(local >= MP3_MIN_SNR, "around the cut: SNR %.1f dB" % local)


def scenario_mp3_gapless():
    print("-- MP3, MP3, then raw PCM, back to back: each track complete and in place")
    path = os.path.join(tempfile.mkdtemp(), "play.pcm")
    tracks = [HttpServer(data=flac_file("s16-128.mp3")), HttpServer(data=flac_file("m16-64.mp3")),
              HttpServer(0.5, seed=8)]
    for h in tracks:
        h.start()
    server, proc, s = start_session("Gapless", "OUTPUT=" + path)
    try:
        send_strm_mp3(s, tracks[0])
        s.until("STAT:STMd")
        send_strm_mp3(s, tracks[1])
        s.until("STAT:STMd")
        send_strm_s(s, tracks[2])
        s.until("STAT:STMd")
        s.until("STAT:STMu", timeout=10)
        check(s.names().count("STAT:STMs") == 3, "STMs for each track (%d)" % s.names().count("STAT:STMs"))
    finally:
        rc, out = stop(proc)
        server.close()
    data = read_output(path)
    want = mp3_decoded["s16-128.mp3"] + mp3_decoded["m16-64.mp3"] + tracks[2].data
    check(data == want, "file: %d bytes, %d expected, first difference at %d"
          % (len(data), len(want), first_difference(data, want)))


def scenario_mp3_garbage():
    print("-- MP3 announced, something else sent: track skipped")
    out, _, rc, log = play_mp3(random.Random(9).randbytes(40000))
    check(rc == 0 and len(out) == 0 and "mp3: no MP3 frame found, track skipped" in log,
          "nothing played, reported (%d bytes)" % len(out))


def scenario_log():
    print("-- LOG: two runs appended to the same file, each with its start line; a log that cannot be opened")
    path = os.path.join(tempfile.mkdtemp(), "amisqueeze.log")
    outs = []
    for run in range(2):
        server, proc, s = start_session("Log", "LOG=" + path)
        try:
            send_packet(s.conn, b"setd", b"\0Run%d" % run)
            s.until("SETD")
        finally:
            rc, out = stop(proc)
            server.close()
        outs.append(out)
    with open(path) as f:
        log = f.read()
    os.remove(path)
    os.rmdir(os.path.dirname(path))
    check(log.count("--- amisqueeze (host build), started ") == 2 and log.count("connected to the server") == 2
          and "player name: Run0" in log and "player name: Run1" in log,
          "both runs in the log, one after the other (%d lines)" % len(log.splitlines()))
    check(all("connected to the server" in o and "--- amisqueeze" not in o for o in outs),
          "the console shows the same messages, without the log's start line")
    server, proc, s = start_session("Log", "LOG=/nonexistent/amisqueeze.log")
    try:
        send_packet(s.conn, b"setd", b"\0NoLog")
        s.until("SETD")
    finally:
        rc, out = stop(proc)
        server.close()
    check(rc == 0 and "cannot open the log /nonexistent/amisqueeze.log" in out and "player name: NoLog" in out,
          "a log that cannot be opened: said, the player runs anyway")


def scenario_no_server():
    print("-- no server listening: retries, Ctrl-C during the wait")
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()                       # nothing listens on this port any more
    proc = start(port)
    time.sleep(1.5)
    t0 = time.time()
    rc, out = stop(proc)
    check("cannot connect" in out and "retrying in 5 s" in out, "connection failure reported, retry announced")
    check(rc == 0 and time.time() - t0 < 2, "Ctrl-C during the retry delay stops at once")


class Responder(threading.Thread):
    """Answers discovery requests on UDP: for each one, the packets of replies, each sent from its socket"""

    def __init__(self, sock, replies):
        super().__init__(daemon=True)
        self.sock = sock
        self.replies = replies
        self.requests = []

    def run(self):
        while True:
            try:
                data, addr = self.sock.recvfrom(1500)
            except OSError:
                return                  # closed
            self.requests.append(data)
            for sock, packet in self.replies:
                sock.sendto(packet, addr)


def discovery_ports():
    """A TCP listener on every address (the reply to a broadcast comes from this machine's network address, where
    the player then connects) and a UDP socket on the same port number"""
    while True:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("0.0.0.0", 0))
        port = server.getsockname()[1]
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            udp.bind(("0.0.0.0", port))
        except OSError:
            server.close()
            udp.close()
            continue
        server.listen(1)
        return server, udp, port


def start_discovery(port, *args):
    return subprocess.Popen([sys.argv[1], "PORT=%d" % port] + list(args), stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)


def scenario_discovery():
    print("-- discovery: no SERVER, one server found, found again after a failed connection")
    server, udp, port = discovery_ports()
    responder = Responder(udp, [(udp, b"ENAME\x07FakeLMS")])
    responder.start()
    proc = start_discovery(port, "NAME=Test")
    try:
        conn = accept(server)
        opcode, _ = read_packet(conn)
        check(opcode == b"HELO", "HELO to the server found")
        check(responder.requests == [b"eNAME\0"], "one request: 'e' then NAME without a value")
        server.close()                  # the next connection fails: the player looks for its server again
        conn.close()
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("0.0.0.0", port))
        deadline = time.time() + 3 * TIMEOUT
        while len(responder.requests) < 2 and time.time() < deadline:
            time.sleep(0.1)
        check(len(responder.requests) == 2, "a failed connection to the server found: a new request")
        server.listen(1)
        conn = accept(server)
        opcode, body = read_packet(conn)
        check(opcode == b"HELO" and struct.unpack(">H", body[24:26])[0] == 0x4000,
              "connected again, with the reconnection flag")
        conn.close()
    finally:
        rc, out = stop(proc)
        server.close()
        udp.close()
    check("looking for a server on the local network, port %d" % port in out, "the search announced")
    check(out.count('server "FakeLMS" found at') == 2, "the server shown with its name, at each search")
    check("cannot connect" in out and "retrying in 5 s" in out, "the failed connection reported")

    print("-- discovery: two servers, a malformed reply, a duplicate, a packet that is no reply")
    server, udp, port = discovery_ports()
    second = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    second.bind(("127.0.0.2", 0))
    responder = Responder(udp, [(udp, b"Dnot a reply"), (udp, b"ENAME\x20ab"), (udp, b"ENAME\x04Same"),
                                (second, b"ENAME\x06Second")])
    responder.start()
    proc = start_discovery(port, "NAME=Test", "VERBOSE")
    try:
        conn = accept(server)
        opcode, _ = read_packet(conn)
        check(opcode == b"HELO", "HELO to the first server that answered")
        conn.close()
    finally:
        rc, out = stop(proc)
        server.close()
        udp.close()
        second.close()
    check('server "ab" found at' in out, "a NAME past the packet's end: read up to the end")
    check('server "Second" found at 127.0.0.2' in out, "the second server shown")
    check("Same" not in out, "the same server's second reply ignored")
    check("2 servers answered: connecting to the first" in out, "the choice said")
    check("a packet that is no reply ignored (12 bytes)" in out, "a packet that is no reply ignored (VERBOSE)")

    print("-- discovery: no server answers, Ctrl-C while waiting for replies and between requests")
    tcp, udp, port = discovery_ports()
    tcp.close()
    udp.close()                         # nothing answers on this port
    proc = start_discovery(port)
    time.sleep(1)
    t0 = time.time()
    rc, out = stop(proc)
    check(rc == 0 and time.time() - t0 < 1 and "stopped" in out, "Ctrl-C while waiting for replies stops at once")
    proc = start_discovery(port)
    time.sleep(3)
    t0 = time.time()
    rc, out = stop(proc)
    check("no server answered on port %d: still looking" % port in out, "no reply: said")
    check(rc == 0 and time.time() - t0 < 1, "Ctrl-C between requests stops at once")


def scenario_arguments():
    print("-- arguments")
    proc = subprocess.run([sys.argv[1], "SERVER=127.0.0.1", "MAC=00:11"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, timeout=TIMEOUT)
    check(proc.returncode == 20 and "invalid MAC" in proc.stdout, "invalid MAC: error, exit code 20")
    proc = subprocess.run([sys.argv[1], "SERVER=127.0.0.1", "OUTPUT=/nonexistent/play.pcm"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, timeout=TIMEOUT)
    check(proc.returncode == 20 and "cannot create /nonexistent/play.pcm" in proc.stdout,
          "OUTPUT file that cannot be created: error, exit code 20")
    proc = subprocess.run([sys.argv[1], "SERVER=127.0.0.1", "FLAC=MAYBE"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, timeout=TIMEOUT)
    check(proc.returncode == 20 and "expected AUTO, ON or OFF" in proc.stdout, "FLAC=MAYBE: error, exit code 20")
    proc = subprocess.run([sys.argv[1], "SERVER=127.0.0.1", "MP3=MAYBE"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, timeout=TIMEOUT)
    check(proc.returncode == 20 and "MP3=MAYBE: expected AUTO, ON or OFF" in proc.stdout,
          "MP3=MAYBE: error, exit code 20")
    for value in ["FAST", "96000", "7999", ""]:
        proc = subprocess.run([sys.argv[1], "SERVER=127.0.0.1", "MAXRATE=" + value], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, timeout=TIMEOUT)
        check(proc.returncode == 20 and "MAXRATE=%s: expected AHI, AHISTD, or a rate from 8000 to 48000 Hz" % value
              in proc.stdout, "MAXRATE=%s: error, exit code 20" % value)
    for option, said in [("PORT=70000", "PORT=70000: expected 1 to 65535"), ("PORT=0", "PORT=0: expected 1 to 65535"),
                         ("UNIT=4", "UNIT=4: expected 0 to 3"), ("UNIT=-1", "UNIT=-1: expected 0 to 3"),
                         ("PRI=11", "PRI=11: expected -10 to 10"), ("PRI=-11", "PRI=-11: expected -10 to 10")]:
        try:                            # accepted, the player would try to connect, then retry
            proc = subprocess.run([sys.argv[1], "SERVER=127.0.0.1", option], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, timeout=TIMEOUT)
            refused = proc.returncode == 20 and said in proc.stdout
        except subprocess.TimeoutExpired:
            refused = False
        check(refused, "%s: error, exit code 20" % option)
    proc = subprocess.run([sys.argv[1], "BENCH"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          timeout=TIMEOUT)
    check(proc.returncode == 0 and "FLAC level 8 (the slowest) decoded at" in proc.stdout and
          "MP3 at 320 kbit/s (the slowest) decoded at" in proc.stdout,
          "BENCH without a server: both speeds are printed (%s)" % " / ".join(proc.stdout.strip().split("\n")))


def scenario_flac_off():
    print("-- FLAC=OFF, MP3=OFF: only what is left advertised")
    for args, formats in [(["FLAC=OFF"], b"MaxSampleRate=48000,pcm,mp3"), (["MP3=OFF"], b"MaxSampleRate=48000,flc,pcm"),
                          (["FLAC=OFF", "MP3=OFF"], b"MaxSampleRate=48000,pcm")]:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        proc = start(server.getsockname()[1], *args)
        try:
            conn = accept(server)
            _, body = read_packet(conn)
            check(body.endswith(formats), "%s: HELO capabilities end with %s" % (" ".join(args), formats.decode()))
            conn.close()
        finally:
            stop(proc)
            server.close()


def scenario_no_mp3(program):
    print("-- the build without MP3 (as amisqueeze, no FPU): not advertised, MP3=ON said, an MP3 stream refused")
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    proc = start(server.getsockname()[1], "MP3=ON", program=program)
    try:
        conn = accept(server)
        _, body = read_packet(conn)
        check(body.endswith(b",MaxSampleRate=48000,flc,pcm"), "HELO capabilities end with flc,pcm, no mp3")
        s = Session(conn)
        fmt7 = b"1m????" + bytes([64])  # MP3; refused before any connection, so no HTTP server
        send_packet(conn, b"strm", strm("s", port=9, ip=0, fmt7=fmt7, request=HTTP_REQUEST))
        try:
            s.until("STAT:STMn")
        except TimeoutError:
            pass
        check(s.names() == ["STAT:STMf", "STAT:STMc", "STAT:STMn"], "MP3 stream refused: STMf, STMc, STMn (%s)"
              % s.names())
        conn.close()
    finally:
        rc, out = stop(proc)
        server.close()
    check("MP3=ON: this build does not decode MP3" in out and "stream format m not supported" in out,
          "MP3=ON and the refused stream reported")


def scenario_maxrate():
    print("-- MAXRATE: the MaxSampleRate of HELO, and what is said at start-up")
    for args, caps, said in [([], b",MaxSampleRate=48000,pcm", None),
                             (["MAXRATE=22050"], b",MaxSampleRate=22050,pcm",
                              "MAXRATE: at most 22050 Hz asked of the server"),
                             (["MAXRATE=ahi"], b",MaxSampleRate=48000,pcm",
                              "MAXRATE=AHI: the AHI unit's rate is not known"),
                             (["MAXRATE=AHISTD"], b",MaxSampleRate=48000,pcm",
                              "MAXRATE=AHISTD: the AHI unit's rate is not known")]:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        proc = start(server.getsockname()[1], "FLAC=OFF", "MP3=OFF", *args)
        try:
            conn = accept(server)
            _, body = read_packet(conn)
            check(body.endswith(caps), "%s: HELO capabilities end with %s"
                  % (" ".join(args) or "no MAXRATE", caps.decode()))
            conn.close()
        finally:
            rc, out = stop(proc)
            server.close()
        if said:
            check(rc == 0 and said in out, "%s: said at start-up (%s)" % (" ".join(args), said))
        else:
            check(rc == 0 and "MAXRATE" not in out, "no MAXRATE: nothing said")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    scenario_session()
    scenario_stream()
    scenario_pause()
    scenario_stop()
    scenario_flush_refused()
    scenario_output()
    scenario_wav()
    scenario_wav_rate()
    scenario_stall()
    scenario_cut()
    scenario_control_lost()
    scenario_silent_server()
    scenario_flac()
    scenario_flac_stall()
    scenario_flac_rate_0()
    scenario_flac_seek()
    scenario_flac_seek_rejected()
    scenario_flac_gapless()
    scenario_flac_off()
    scenario_maxrate()
    scenario_mp3()
    scenario_mp3_tags()
    scenario_mp3_seek()
    scenario_mp3_seek_lms()
    scenario_mp3_continuous()
    scenario_mp3_gapless()
    scenario_mp3_garbage()
    scenario_log()
    scenario_no_server()
    scenario_discovery()
    scenario_arguments()
    if len(sys.argv) == 3:
        scenario_no_mp3(sys.argv[2])
    print("fake_server: %d failed" % failures)
    sys.exit(1 if failures else 0)
