#!/usr/bin/env python3
"""Scenario in WinUAE: Music Assistant's commands on amisqueeze playing through AHI, sent through its API: play,
volume 50, pause and resume, a seek near the end of a track (the next one follows on its own), next, stop.

Usage:
  ma-commands.py [--codec flac|wav] [--unit N] [--extra "OPTIONS"] [--exe NAME] [common options]

--codec flac (default): the player's output codec in Music Assistant, FLAC decoded by amisqueeze; --codec wav: a
WAV stream, its header read by amisqueeze (the codec the user guide gives a slow Amiga). The codec is set back
afterwards. Each track waits for Music Assistant's cont before it plays.
Common options (scenario.py): --name, --server, --ma, --ma-port, --ma-token-file (or MA_TOKEN), --serial,
--nm-dir, --memory-slack, --winuae EXE --config FILE [--winuae-arg ARG...], -v.

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
    parser = UsageParser(description="Music Assistant's commands on amisqueeze in WinUAE, through AHI; prints OK "
                                     "or FAIL: <reason>. See docs/development/winuae-scenarios.md.")
    parser.add_argument("--codec", choices=["flac", "wav"], default="flac",
                        help="the player's output codec in Music Assistant (default flac)")
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

    def started(s):
        """How many times amisqueeze started a track (output: track started), a gapless one included"""
        return s.log_text().count("output: track started")

    def set_volume(s, level):
        s.server.call("players/cmd/volume_set", player_id=s.player, volume_level=level)

    def steps(s):
        ma, player = s.server, s.player

        # The codec under test, set back afterwards (Music Assistant keeps it only while the player is connected:
        # the clean-up sets it back before stopping amisqueeze)
        codec = ma.call("config/players/get_value", player_id=player, key="output_codec")
        if codec != args.codec:
            ma.call("config/players/save", player_id=player, values={"output_codec": args.codec})
            s.on_exit(lambda: ma.call("config/players/save", player_id=player, values={"output_codec": codec}))
            check(wait_for(lambda: ma.player_id(args.name, connected=True), 10, 1),
                  f"player {args.name} gone after its codec was set")
        print(f"codec: {args.codec} (was {codec})")
        volume = (ma.call("players/get", player_id=player) or {}).get("volume_level")
        if volume is not None:
            s.on_exit(lambda: set_volume(s, volume))
        set_volume(s, 100)
        s.call("player_queues/clear")
        s.call("player_queues/play_media", media=[s.tracks[name][0] for name in TRACKS], option="replace")

        # 1. Play
        t_play = time.monotonic()
        s.check_playing("1. play")

        # 2. Volume 50, then pause and resume (with the samples scaled: see lms-commands)
        before = len(volume_lines(s))
        set_volume(s, 50)
        v = wait_volume(s, before, lambda pc: pc < 100)
        check(v, "2. volume 50: no volume: line below 100 % in the log")
        print(f"2. volume 50: {v} % of full amplitude")
        t_pause = time.monotonic()
        s.call("player_queues/pause")
        s.check_paused("3. pause")
        paused = time.monotonic() - t_pause
        s.call("player_queues/play")
        s.check_playing("4. resume", settle=1.0)
        before = len(volume_lines(s))
        set_volume(s, 100)
        check(wait_volume(s, before, lambda pc: pc == 100), "4. volume 100: no volume: 100.0 % line in the log")

        # 5. Seek 2 s before the end of the sweep (whole seconds only): the next track must follow on its own,
        # long before the sweep would have ended. Judged by the host's clock and the queue's index.
        sweep = s.tracks["sweep.flac"][1]
        check(s.index() == 0, f"5. seek: playing track {s.index()}, expected 0")
        left = sweep - (time.monotonic() - t_play - paused)
        check(left >= 9, f"5. seek: only about {left:.0f} s of the sweep left, too few to tell a seek")
        tracks = started(s)
        s.call("player_queues/seek", position=int(sweep) - 2)
        t_seek = time.monotonic()
        check(wait_for(lambda: s.index() == 1, 8, 0.25),
              f"5. seek: the next track did not start within 8 s (about {left:.0f} s were left without it)")
        print(f"5. seek near the end: next track after {time.monotonic() - t_seek:.1f} s "
              f"(about {left:.0f} s without the seek)")
        # The sweep from the seek's position, then the next track: two starts in amisqueeze, not only in the queue
        check(wait_for(lambda: started(s) >= tracks + 2, 8, 0.25),
              f"5. seek: {started(s) - tracks} track start(s) in the log, expected 2")
        s.check_playing("5. next track")

        # 6. Next: the last track, its rate changing from 44.1 to 48 kHz
        tracks = started(s)
        s.call("player_queues/next")
        t_next = time.monotonic()
        check(wait_for(lambda: s.index() == 2 and started(s) > tracks, 8, 0.25),
              "6. next: the last track did not start within 8 s")
        print(f"6. next: last track started after {time.monotonic() - t_next:.1f} s")
        s.check_playing("6. next", settle=1.0)

        # 7. Stop
        s.call("player_queues/stop")
        check(wait_for(lambda: s.mode() == "stop", 5), "7. stop: Music Assistant's mode is not stop")
        print("7. stop: mode stop")

        # Each track waited for cont, and a WAV stream's header was read
        log = s.log_text()
        check("no cont from the server" not in log, "a stream waited more than 10 s for cont")
        conts = len(re.findall(r"^<- cont", log, re.M))
        print(f"cont: {conts}")
        check(conts >= 3, f"only {conts} cont for three tracks and a seek")
        if args.codec == "wav":
            check(not re.search(r"wav: .*track skipped", log), "a WAV header was refused (wav: ... track skipped)")
            headers = re.findall(r"^stream: WAV header: (.*)$", log, re.M)
            print(f"WAV headers: {len(headers)}, {' / '.join(sorted(set(headers)))}")
            check(len(headers) >= 3, f"only {len(headers)} WAV header(s) read for three tracks and a seek")

    scenario = Scenario(args, args.exe, f"ma-commands-{args.codec}.log", TRACKS,
                        f"UNIT={args.unit} {args.extra} FLAC={'ON' if args.codec == 'flac' else 'OFF'}",
                        output=f"AHI unit {args.unit}", stream_format="f" if args.codec == "flac" else "p",
                        server="ma")
    return scenario.run(steps)


if __name__ == "__main__":
    sys.exit(main())
