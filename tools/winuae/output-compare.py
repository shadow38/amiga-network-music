#!/usr/bin/env python3
"""Scenario in WinUAE: amisqueeze plays two tracks back to back into a file (OUTPUT), compared with what they hold.

Usage:
  output-compare.py --case pcm|flac|mp3 [common options]

  pcm   amisqueeze.fpu FLAC=OFF: the server decodes test-pattern.flac and sends raw PCM;
  flac  amisqueeze FLAC=ON: test-pattern.flac sent as it is, decoded by dr_flac on the 68k;
  mp3   amisqueeze.fpu MP3=ON: test-20s.mp3 sent as it is, decoded by minimp3 on the FPU.

pcm and flac are compared byte by byte with the counter pattern (tools/pcm-compare.py, made again at each run).
MP3 decoded on the Amiga cannot be: its floating point gives a few samples off by one from any other decoder. It
must have the exact length of the two tracks, and each side of each track must be the tone it holds (440 Hz on the
left, 660 Hz on the right): the sine that fits best, at about half of full scale, with a minimal signal-to-noise
ratio. Common options (scenario.py): --name, --server, --lms, --serial, --nm-dir, --memory-slack,
--winuae EXE --config FILE [--winuae-arg ARG...], -v.

What it does and checks: docs/development/winuae-scenarios.md. The last line is OK or FAIL: <reason>. Exit code:
0 OK, 1 a check failed, 2 environment, 3 usage.
"""
import math
import subprocess
import sys
import tempfile
import time
from array import array
from pathlib import Path

sys.dont_write_bytecode = True          # no __pycache__ next to the tools
sys.path.insert(0, str(Path(__file__).resolve().parent))
from scenario import REPOSITORY, Scenario, UsageParser, add_options, check, wait_for  # noqa: E402

# case: program, options, track, format of its strm s, decoded on the Amiga (samples in its byte order, big-endian)
CASES = {
    "pcm": ("amisqueeze.fpu", "FLAC=OFF MP3=OFF", "test-pattern.flac", "p", False),
    "flac": ("amisqueeze", "FLAC=ON", "test-pattern.flac", "f", True),
    "mp3": ("amisqueeze.fpu", "MP3=ON", "test-20s.mp3", "m", True),
}
RATE = 44100
FRAME = 4                               # bytes: 16 bits, stereo
TONES = (440, 660)                      # Hz, left and right, in test-20s.mp3
MIN_SNR = 60                            # dB


def tone_fit(samples, first, count, channel, freq):
    """(amplitude, SNR in dB) of the sine at `freq` that fits one channel best, its phase and an offset free. The
    test tracks last a whole number of periods, so sine, cosine and offset are fitted one by one."""
    table = [math.sin(2 * math.pi * k / RATE) for k in range(RATE)]
    xs = xc = x1 = xx = ss = cc = 0.0
    for n in range(count):
        x = samples[2 * (first + n) + channel]
        k = n * freq % RATE
        s, c = table[k], table[(k + RATE // 4) % RATE]
        xs += x * s
        xc += x * c
        x1 += x
        xx += x * x
        ss += s * s
        cc += c * c
    a, b = xs / ss, xc / cc
    fitted = a * a * ss + b * b * cc
    noise = max(xx - x1 * x1 / count - fitted, 1e-9)
    return math.hypot(a, b), 10 * math.log10(fitted / noise)


def main():
    parser = UsageParser(description="Two tracks played by amisqueeze in WinUAE into a file (OUTPUT), compared "
                                     "with what they hold; prints OK or FAIL: <reason>. "
                                     "See docs/development/winuae-scenarios.md.")
    parser.add_argument("--case", choices=sorted(CASES), required=True, help="what decodes, see above")
    add_options(parser)
    args = parser.parse_args()
    exe, options, track, stream_format, decoded = CASES[args.case]
    name = f"output-{args.case}"
    output = args.nm_dir / f"{name}.pcm"

    def steps(s):
        track_id, length = s.tracks[track]
        s.query("playlist", "clear")
        for _ in range(2):
            s.query("playlistcontrol", "cmd:add", f"track_id:{track_id}")
        # A single play (docs/development/testing.md, OUTPUT comparisons): no index then play, which could start the stream
        # twice and leave an extra beginning in the file
        s.query("play")
        t_play = time.monotonic()
        ended = wait_for(lambda: "output: end of playback" in s.log_text(), 2 * length + 30, 0.5)
        check(ended, f"no end of playback {2 * length + 30:.0f} s after play")
        print(f"end of playback after {time.monotonic() - t_play:.1f} s ({2 * length:.1f} s of tracks)")

    def compare(s):
        check(wait_for(lambda: "bytes written to" in s.log_text(), 15),
              "the OUTPUT file was not closed (no 'bytes written' in the log)")
        raw = output.read_bytes()
        data = array("h", raw[:len(raw) // 2 * 2])
        if decoded and sys.byteorder == "little":
            data.byteswap()             # decoded on the Amiga: big-endian; the counter pattern is little-endian
        length = s.tracks[track][1]
        if args.case != "mp3":
            with tempfile.TemporaryDirectory() as tmp:
                ref, played = Path(tmp) / "counter.pcm", Path(tmp) / "played.pcm"
                subprocess.run([sys.executable, str(REPOSITORY / "tools" / "pcm-compare.py"), "--write-counter",
                                f"{length:g}", str(ref)], check=True)
                played.write_bytes(data.tobytes())
                result = subprocess.run([sys.executable, str(REPOSITORY / "tools" / "pcm-compare.py"), "--counter",
                                         str(played), str(ref), str(ref)], capture_output=True, text=True)
            for line in result.stdout.splitlines():
                print(f"pcm-compare: {line}")
            check(result.returncode == 0, f"{output.name} is not the counter pattern twice "
                                          f"(pcm-compare exit code {result.returncode})")
            return
        frames = round(length * RATE)
        print(f"file: {len(data) // 2} frames, expected {2 * frames}")
        check(len(data) == 4 * frames, f"{output.name}: {len(data) // 2} frames, expected {2 * frames} "
                                       f"(two tracks of {frames})")
        for t in range(2):
            for channel, freq in enumerate(TONES):
                amplitude, snr = tone_fit(data, t * frames, frames, channel, freq)
                side = ("left", "right")[channel]
                print(f"track {t + 1}, {side}: {freq} Hz, amplitude {amplitude / 32768:.3f} of full scale, "
                      f"SNR {snr:.1f} dB")
                check(0.4 <= amplitude / 32768 <= 0.6 and snr >= MIN_SNR,
                      f"track {t + 1}, {side}: not a {freq} Hz tone at half of full scale "
                      f"(amplitude {amplitude / 32768:.3f}, SNR {snr:.1f} dB, at least {MIN_SNR} expected)")

    scenario = Scenario(args, exe, f"{name}.log", [track], f"OUTPUT=NM:{output.name} {options}", output="virtual",
                        stream_format=stream_format, files=[output.name])
    return scenario.run(steps, after_stop=compare)


if __name__ == "__main__":
    sys.exit(main())
