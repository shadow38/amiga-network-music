#!/usr/bin/env python3
"""Scenario in WinUAE: amisqueeze plays a test tone through AHI, driven by LMS, then is stopped cleanly.

Usage:
  play-tone.py [--unit N] [--extra "OPTIONS"] [--track FILE] [--exe NAME] [--timeout SECONDS] [common options]

Common options (scenario.py): --name, --server, --lms, --serial, --nm-dir, --memory-slack,
--winuae EXE --config FILE [--winuae-arg ARG...], -v.

What it does and checks, step by step: docs/development/winuae-scenarios.md. WinUAE is started only when its
serial shell does not answer, WinUAE is not running and --winuae and --config (or WINUAE and WINUAE_CONFIG in the
environment) are given; a running WinUAE is never stopped.

The last line is OK or FAIL: <reason>. Exit code: 0 OK, 1 a check failed, 2 environment (no serial shell, no LMS,
track not found, amisqueeze already running, WinUAE not started), 3 usage.
"""
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True          # no __pycache__ next to the tools
sys.path.insert(0, str(Path(__file__).resolve().parent))
from scenario import Scenario, UsageParser, add_options, check, wait_for  # noqa: E402


def main():
    parser = UsageParser(description="Plays a test tone with amisqueeze in WinUAE, driven by LMS; prints OK or "
                                     "FAIL: <reason>. See docs/development/winuae-scenarios.md.")
    parser.add_argument("--exe", default="amisqueeze", help="the program in NM: (default amisqueeze)")
    parser.add_argument("--unit", type=int, default=0, help="AHI unit (default 0)")
    parser.add_argument("--extra", default="", help='more amisqueeze options, e.g. "SWAP FLAC=OFF"')
    parser.add_argument("--track", default="tone-440.flac", help="file in the server's library")
    parser.add_argument("--timeout", type=float, help="seconds to wait for the end of playback "
                                                      "(default the track's length + 30)")
    add_options(parser)
    args = parser.parse_args()

    def steps(s):
        track_id, length = s.tracks[args.track]
        s.query("playlistcontrol", "cmd:load", f"track_id:{track_id}")
        t_play = time.monotonic()
        s.check_playing("playing", min(3.0, length / 3))

        timeout = args.timeout or length + 30
        ended = wait_for(lambda: "output: end of playback" in s.log_text(), timeout - (time.monotonic() - t_play),
                         0.25)
        check(ended, f"no end of playback {timeout:.0f} s after play")
        took = time.monotonic() - t_play
        print(f"end of playback after {took:.1f} s (track {length:.1f} s)")
        check(length - 1 <= took <= length + 10, f"end of playback after {took:.1f} s for a {length:.1f} s track")
        check(wait_for(lambda: s.mode() == "stop", 5), "LMS's mode is not stop at the end")

    scenario = Scenario(args, args.exe, "play-tone.log", [args.track], f"UNIT={args.unit} {args.extra}",
                        output=f"AHI unit {args.unit}")
    return scenario.run(steps)


if __name__ == "__main__":
    sys.exit(main())
