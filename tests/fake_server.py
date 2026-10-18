#!/usr/bin/env python3
"""Scenarios against the host build of amisqueeze: a fake slimproto server on localhost drives the real program
and checks what it sends (see "make test").

Usage: fake_server.py <path to the host build of amisqueeze>
"""
import signal
import socket
import struct
import subprocess
import sys
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


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    scenario_session()
    scenario_no_server()
    scenario_arguments()
    print("fake_server: %d failed" % failures)
    sys.exit(1 if failures else 0)
