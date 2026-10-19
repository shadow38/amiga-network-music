#!/usr/bin/env python3
"""Test signal of the FLAC tests: a few sines and a little noise, deterministic, so that the PCM a FLAC file must
decode to can be computed again here (FLAC is lossless).

The FLAC files of tests/data/ were made from it once (flac 1.3.4, the tool shipped with LMS):
    tests/flac_data.py raw tests/data      # writes the raw PCM files (little-endian signed)
    flac --force-raw-format --endian=little --sign=signed --channels=C --bps=B --sample-rate=R -<level> ...
(see tests/data/README.txt for each file). The tests call expected() to know what amisqueeze must output.
"""
import math
import os
import struct
import sys

# name: (rate, channels, bits, seconds)
SIGNALS = {
    "s16": (44100, 2, 16, 1.0),
    "m16": (44100, 1, 16, 1.0),
    "s24": (44100, 2, 24, 1.0),
    "s16-48k": (48000, 2, 16, 1.0),
}


def samples(rate, channels, bits, seconds):
    """Signed integer samples, interleaved"""
    full = (1 << (bits - 1)) - 1
    seed = 12345
    out = []
    for i in range(int(rate * seconds)):
        t = i / rate
        for c in range(channels):
            seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
            noise = ((seed >> 8) & 0xFFFF) / 65535.0 - 0.5
            v = (0.30 * math.sin(2 * math.pi * (220 + 110 * c) * t) +
                 0.15 * math.sin(2 * math.pi * 1375.5 * t + c) +
                 0.05 * math.sin(2 * math.pi * 7000 * t) + 0.02 * noise)
            out.append(int(round(v * full)))
    return out


def raw(name):
    """The signal as raw PCM, little-endian signed, as the FLAC files were encoded from"""
    rate, channels, bits, seconds = SIGNALS[name]
    s = samples(rate, channels, bits, seconds)
    if bits == 16:
        return struct.pack("<%dh" % len(s), *s)
    return b"".join(struct.pack("<i", v)[:3] for v in s)


def expected(name):
    """What amisqueeze outputs for that signal: 16-bit native (little-endian on the host), 24 bits keeping their 16
    most significant bits"""
    rate, channels, bits, seconds = SIGNALS[name]
    s = samples(rate, channels, bits, seconds)
    if bits == 24:
        s = [v >> 8 for v in s]
    return struct.pack("<%dh" % len(s), *s)


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "raw":
        sys.exit(__doc__)
    for name in SIGNALS:
        with open(os.path.join(sys.argv[2], name + ".raw"), "wb") as f:
            f.write(raw(name))
