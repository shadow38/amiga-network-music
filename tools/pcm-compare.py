#!/usr/bin/env python3
"""Compares raw PCM written by amisqueeze (OUTPUT option) with a reference, byte by byte.

Usage:
  pcm-compare.py [--counter] [--rate BYTES_PER_SEC] FILE REFERENCE [REFERENCE...]
  pcm-compare.py --write-counter SECONDS FILE

The reference is the concatenation of the REFERENCE files (a playlist of two tracks played back to back: give
both). Positions are also given in seconds (default rate: 176400 bytes/s, 44.1 kHz 16-bit stereo).

--counter: the reference is the counter pattern made by --write-counter (16-bit stereo little-endian, frame n
holds n on the left and its bitwise complement on the right, both modulo 65536): the counter values around the
first difference tell a late start, a dropped or duplicated block, and corrupted bytes apart.
--write-counter: writes SECONDS of that pattern at 44.1 kHz (encode it losslessly, e.g. FLAC, for the server).

Exit code: 0 identical, 1 the file is a prefix of the reference (playback stopped early), 2 different,
3 usage or file error.
"""
import struct
import sys

FRAME = 4                               # bytes per frame: 16 bits, stereo
CHUNK = 1 << 16


def counter_frames(seconds):
    out = bytearray()
    for n in range(int(seconds * 44100)):
        left = ((n + 32768) & 0xFFFF) - 32768
        out += struct.pack("<hh", left, ~left)
    return bytes(out)


def counter_at(data, offset):
    """Counter value of the frame holding `offset`, or a note if that frame does not follow the pattern"""
    start = offset - offset % FRAME
    if start + FRAME > len(data):
        return "end of data"
    left, right = struct.unpack_from("<hh", data, start)
    if right != ~left:
        return "not a pattern frame (%d, %d)" % (left, right)
    return "counter %d" % (left & 0xFFFF)


def first_difference(a, b):
    n = min(len(a), len(b))
    for pos in range(0, n, CHUNK):
        end = min(pos + CHUNK, n)
        if a[pos:end] != b[pos:end]:
            return next(i for i in range(pos, end) if a[i] != b[i])
    return n


def main(argv):
    rate, counter = 176400, False
    args = argv[1:]
    if args[:1] == ["--write-counter"] and len(args) == 3:
        with open(args[2], "wb") as f:
            f.write(counter_frames(float(args[1])))
        return 0
    while args and args[0].startswith("--"):
        if args[0] == "--counter":
            counter = True
            args = args[1:]
        elif args[0] == "--rate" and len(args) > 1:
            rate = int(args[1])
            args = args[2:]
        else:
            break
    if len(args) < 2:
        print(__doc__, file=sys.stderr)
        return 3
    try:
        with open(args[0], "rb") as f:
            data = f.read()
        ref = b""
        for name in args[1:]:
            with open(name, "rb") as f:
                ref += f.read()
    except OSError as e:
        print("pcm-compare: %s" % e, file=sys.stderr)
        return 3

    def at(offset):
        return "byte %d (%.3f s, frame %d)" % (offset, offset / rate, offset // FRAME)

    print("file:      %d bytes (%.3f s)" % (len(data), len(data) / rate))
    print("reference: %d bytes (%.3f s)" % (len(ref), len(ref) / rate))
    if counter and data:
        print("start of the file: %s" % counter_at(data, 0))
    diff = first_difference(data, ref)
    if diff == len(data) == len(ref):
        print("identical")
        return 0
    if diff == len(data):
        print("prefix: the file matches the start of the reference, %d bytes (%.3f s) missing at the end"
              % (len(ref) - len(data), (len(ref) - len(data)) / rate))
        return 1
    if diff == len(ref):
        print("different: %d extra bytes after the end of the reference" % (len(data) - len(ref)))
        return 2
    print("different: first difference at %s" % at(diff))
    if counter:
        for name, d in (("file:     ", data), ("reference:", ref)):
            if diff >= FRAME:
                print("  %s %s, then %s" % (name, counter_at(d, diff - FRAME), counter_at(d, diff)))
            else:
                print("  %s %s" % (name, counter_at(d, diff)))
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
