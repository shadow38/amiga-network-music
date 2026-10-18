#!/usr/bin/env python3
"""Scenarios against the host build of amisqueeze: a fake slimproto server on localhost drives the real program
and checks what it sends (see "make test").

Usage: fake_server.py <path to the host build of amisqueeze>
"""
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


def start(port, *args):
    return subprocess.Popen([sys.argv[1], "SERVER=127.0.0.1", "PORT=%d" % port] + list(args),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def stop(proc):
    proc.send_signal(signal.SIGINT)
    out, _ = proc.communicate(timeout=TIMEOUT)
    return proc.returncode, out


def scenario_session():
    print("-- session: HELO, strm t, setd, server disconnect and reconnection, Ctrl-C")
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
        check(body[36:].startswith(b"Model=squeezelite,") and body.endswith(b",pcm"), "HELO capabilities")

        send_packet(conn, b"strm", strm("t", replay_gain=0x12345678))
        opcode, body = read_packet(conn)
        check(opcode == b"STAT" and len(body) == 53, "STAT of 53 bytes")
        check(body[0:4] == b"STMt", "strm t answered with STMt")
        check(struct.unpack(">I", body[47:51])[0] == 0x12345678, "STMt echoes the server timestamp")

        send_packet(conn, b"setd", b"\0Renamed")
        opcode, body = read_packet(conn)
        check(opcode == b"SETD" and body == b"\0Renamed\0", "setd answered with SETD and the new name")

        conn.close()
        conn = accept(server)
        opcode, body = read_packet(conn)
        check(opcode == b"HELO" and struct.unpack(">H", body[24:26])[0] == 0x4000,
              "reconnection after a disconnect, with the reconnection flag")
        conn.close()
    finally:
        rc, out = stop(proc)
        server.close()
    check(rc == 0 and "stopped" in out, "Ctrl-C stops the player cleanly (exit code 0, \"stopped\")")
    check("player name: Renamed" in out, "new name printed")
    check("connection closed by the server" in out, "disconnect reported")


PCM_BPS = 176400                        # 44.1 kHz, 16 bits, stereo
HTTP_HEADERS = b"HTTP/1.0 200 OK\r\nContent-Type: audio/L16;rate=44100;channels=2\r\n\r\n"
HTTP_REQUEST = b"GET /test.pcm?player=test HTTP/1.0\r\n\r\n"


class HttpServer(threading.Thread):
    """Serves one PCM track: `seconds` of audio, sent in chunks of `chunk` bytes every `pause` seconds. The bytes
    are pseudo-random (fixed `seed`): a byte lost, duplicated or moved shows in a comparison."""

    def __init__(self, seconds, chunk=65536, pause=0.0, seed=1):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.data = random.Random(seed).randbytes(int(PCM_BPS * seconds))
        self.chunk, self.pause = chunk, pause
        self.request = b""
        self.closed_by_player = False

    def run(self):
        self.sock.settimeout(TIMEOUT)
        conn, _ = self.sock.accept()
        conn.settimeout(TIMEOUT)
        while not self.request.endswith(b"\r\n\r\n"):
            self.request += conn.recv(1)
        try:
            conn.sendall(HTTP_HEADERS)
            for i in range(0, len(self.data), self.chunk):
                conn.sendall(self.data[i:i + self.chunk])
                time.sleep(self.pause)
        except OSError:
            self.closed_by_player = True
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


def scenario_arguments():
    print("-- arguments")
    proc = subprocess.run([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          timeout=TIMEOUT)
    check(proc.returncode == 20 and "no server" in proc.stdout, "no server: error, exit code 20")
    proc = subprocess.run([sys.argv[1], "SERVER=127.0.0.1", "MAC=00:11"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, timeout=TIMEOUT)
    check(proc.returncode == 20 and "invalid MAC" in proc.stdout, "invalid MAC: error, exit code 20")
    proc = subprocess.run([sys.argv[1], "SERVER=127.0.0.1", "OUTPUT=/nonexistent/play.pcm"], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, timeout=TIMEOUT)
    check(proc.returncode == 20 and "cannot create /nonexistent/play.pcm" in proc.stdout,
          "OUTPUT file that cannot be created: error, exit code 20")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    scenario_session()
    scenario_stream()
    scenario_pause()
    scenario_stop()
    scenario_output()
    scenario_no_server()
    scenario_arguments()
    print("fake_server: %d failed" % failures)
    sys.exit(1 if failures else 0)
