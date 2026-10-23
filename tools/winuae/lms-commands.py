#!/usr/bin/env python3
"""Scenario in WinUAE: LMS's commands on amisqueeze playing through AHI: play, volume 50, pause and resume, a seek
near the end of a track, stop, then two tracks in a row (44.1 then 48 kHz) to the end of the playlist.

Usage:
  lms-commands.py [--flac off|on] [--unit N] [--extra "OPTIONS"] [--exe NAME] [common options]

--flac off (default): the server decodes FLAC and sends raw PCM, as to an Amiga without a fast CPU; --flac on:
the FLAC files are sent as they are and decoded by amisqueeze (a seek then comes without the FLAC header).
Common options (scenario.py): --name, --server, --lms, --serial, --nm-dir, --memory-slack,
--winuae EXE --config FILE [--winuae-arg ARG...], -v.

What it does and checks: docs/development/winuae-scenarios.md. The last line is OK or FAIL: <reason>. Exit code:
0 OK, 1 a check failed, 2 environment, 3 usage.
"""
import re
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True          # no __pycache__ next to the tools
sys.path.insert(0, str(Path(__file__).resolve().parent))
from scenario import Scenario, UsageParser, add_options, check, wait_for  # noqa: E402

TRACKS = ["sweep.flac", "tone-440.flac", "tone-440-48k.flac"]


def main():
    parser = UsageParser(description="LMS's commands on amisqueeze in WinUAE, through AHI; prints OK or "
                                     "FAIL: <reason>. See docs/development/winuae-scenarios.md.")
    parser.add_argument("--flac", choices=["off", "on"], default="off",
                        help="FLAC decoded by the server (off, raw PCM) or by amisqueeze (on)")
    parser.add_argument("--exe", default="amisqueeze", help="the program in NM: (default amisqueeze)")
    parser.add_argument("--unit", type=int, default=0, help="AHI unit (default 0)")
    parser.add_argument("--extra", default="SWAP", help='more amisqueeze options (default "SWAP")')
    add_options(parser)
    args = parser.parse_args()

    def volume_lines(s):
        return re.findall(r"^volume: ([\d.]+) %", s.log_text(), re.M)

    def wait_volume(s, before, test, seconds=5):
        """The first "volume:" line after the `before` first ones that passes test(percent), or None"""
        def found():
            return next((v for v in volume_lines(s)[before:] if test(float(v))), None)
        return wait_for(found, seconds)

    def steps(s):
        sweep = s.tracks["sweep.flac"][1]
        volume = s.query("mixer", "volume", "?")
        s.on_exit(lambda: s.query("mixer", "volume", volume))
        s.query("mixer", "volume", 100)
        s.query("playlist", "clear")
        for name in TRACKS:
            s.query("playlistcontrol", "cmd:add", f"track_id:{s.tracks[name][0]}")

        # 1. Play
        s.query("play")
        t_play = time.monotonic()
        s.check_playing("1. play")

        # 2. Volume 50, then pause and resume: with a low volume given to AHI, the requests sent again after the
        # abort of a pause corrupted the free memory list (step player-6); the memory check after the stop catches it
        before = len(volume_lines(s))
        s.query("mixer", "volume", 50)
        v = wait_volume(s, before, lambda pc: pc < 100)
        check(v, "2. volume 50: no volume: line below 100 % in the log")
        print(f"2. volume 50: {v} % of full amplitude")
        t_pause = time.monotonic()
        s.query("pause", 1)
        s.check_paused("3. pause")
        paused = time.monotonic() - t_pause
        s.query("pause", 0)
        s.check_playing("4. resume", settle=1.0)
        before = len(volume_lines(s))
        s.query("mixer", "volume", 100)
        check(wait_volume(s, before, lambda pc: pc == 100), "4. volume 100: no volume: 100.0 % line in the log")

        # 5. Seek 1.5 s before the end of the sweep: the next track must start long before the sweep would have
        # ended. Judged by the host's clock and the playlist index, not by LMS's elapsed time (wrong for about 50 s
        # after a start or a seek)
        check(s.index() == 0, f"5. seek: playing track {s.index()}, expected 0")
        left = sweep - (time.monotonic() - t_play - paused)
        check(left >= 9, f"5. seek: only about {left:.0f} s of the sweep left, too few to tell a seek")
        s.query("time", f"{sweep - 1.5:.1f}")
        t_seek = time.monotonic()
        check(wait_for(lambda: s.index() == 1, 6, 0.25),
              f"5. seek: the next track did not start within 6 s (about {left:.0f} s were left without it)")
        print(f"5. seek near the end: next track after {time.monotonic() - t_seek:.1f} s "
              f"(about {left:.0f} s without the seek)")

        # 6. Stop, then play to the end: tone-440 and tone-440-48k in a row, the rate changing between them
        s.query("stop")
        check(wait_for(lambda: s.mode() == "stop", 5), "6. stop: LMS's mode is not stop")
        print("6. stop: mode stop")
        ends = s.log_text().count("output: end of playback")
        length = s.tracks["tone-440.flac"][1] + s.tracks["tone-440-48k.flac"][1]
        s.query("play")
        t_play = time.monotonic()
        check(wait_for(lambda: s.mode() == "play", 10), "7. play: LMS's mode is not play")
        check(wait_for(lambda: s.index() == 2, length + 5), "7. two tracks: the last track never started")
        print(f"7. two tracks: the second one started after {time.monotonic() - t_play:.1f} s")
        ended = wait_for(lambda: s.log_text().count("output: end of playback") > ends,
                         length + 30 - (time.monotonic() - t_play), 0.25)
        check(ended, f"7. two tracks: no end of playback {length + 30:.0f} s after play")
        took = time.monotonic() - t_play
        print(f"7. two tracks: end of playback after {took:.1f} s ({length:.1f} s of tracks)")
        check(length - 1 <= took <= length + 10, f"7. two tracks: end of playback after {took:.1f} s for "
                                                 f"{length:.1f} s of tracks")
        check(wait_for(lambda: s.mode() == "stop", 5), "7. two tracks: LMS's mode is not stop at the end")

    scenario = Scenario(args, args.exe, f"lms-commands-{'flac' if args.flac == 'on' else 'pcm'}.log", TRACKS,
                        f"UNIT={args.unit} {args.extra} FLAC={args.flac.upper()}", output=f"AHI unit {args.unit}",
                        stream_format="f" if args.flac == "on" else "p")
    return scenario.run(steps)


if __name__ == "__main__":
    sys.exit(main())
