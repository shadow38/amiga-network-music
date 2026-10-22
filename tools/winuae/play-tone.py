#!/usr/bin/env python3
"""Scenario in WinUAE: amisqueeze plays a test tone through AHI, driven by LMS, then is stopped cleanly.

Usage:
  play-tone.py [--unit N] [--extra "OPTIONS"] [--track FILE] [--exe NAME] [--name NAME] [--server HOST]
               [--lms HOST:PORT] [--serial HOST:PORT] [--nm-dir DIR] [--timeout SECONDS]
               [--winuae EXE --config FILE [--winuae-arg ARG...]] [-v]

What it does and checks, step by step: docs/development/winuae-scenarios.md. WinUAE is started only when its
serial shell does not answer, WinUAE is not running and --winuae and --config (or WINUAE and WINUAE_CONFIG in the
environment) are given; a running WinUAE is never stopped.

The last line is OK or FAIL: <reason>. Exit code: 0 OK, 1 a check failed, 2 environment (no serial shell, no LMS,
track not found, amisqueeze already running, WinUAE not started), 3 usage.
"""
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True          # no __pycache__ next to the tools
sys.path.insert(0, str(Path(__file__).resolve().parent))
from amiga_shell import AmigaShell, ShellError, UsageParser  # noqa: E402
from lms_cli import LmsCli, LmsError  # noqa: E402

LOG_NAME = "play-tone.log"


class Fail(Exception):
    def __init__(self, code, reason):
        super().__init__(reason)
        self.code = code


def check(condition, reason):
    if not condition:
        raise Fail(1, reason)


def wait_for(test, seconds, step=0.5):
    """Calls test() until it returns a true value, for `seconds` at most; returns that value, or None"""
    deadline = time.monotonic() + seconds
    while True:
        value = test()
        if value or time.monotonic() >= deadline:
            return value
        time.sleep(step)


def host_port(text, option):
    host, _, port = text.rpartition(":")
    if not host or not port.isdigit():
        raise Fail(3, f"{option}: expected HOST:PORT, got {text}")
    return host, int(port)


def winuae_running():
    try:
        if os.name == "nt":
            out = subprocess.run(["tasklist", "/NH"], capture_output=True, text=True).stdout
            return re.search(r"^winuae", out, re.I | re.M) is not None
        return subprocess.run(["pgrep", "-i", "winuae"], capture_output=True).returncode == 0
    except OSError:
        return False


def open_shell(args, serial):
    """The serial shell; WinUAE started first if it is not running and --winuae and --config are given"""
    try:
        shell = AmigaShell(*serial, args.verbose)
        if shell.prompt():
            return shell
        shell.close()
    except ShellError:
        pass
    if winuae_running():
        raise Fail(2, f"WinUAE is running but its serial shell does not answer on {args.serial}: restart it")
    if not args.winuae or not args.config:
        raise Fail(2, f"no serial shell on {args.serial}: start WinUAE with the test configuration, "
                      "or give --winuae and --config")
    command = [args.winuae, "-f", args.config] + [a for arg in args.winuae_arg for a in shlex.split(arg)]
    print(f"starting WinUAE: {' '.join(command)}")
    try:
        subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError as e:
        raise Fail(2, f"WinUAE not started: {e}")
    # One connection kept while the Amiga boots: Return every few seconds until a prompt answers (a connection
    # closed before the Shell runs could stay attached in WinUAE and block the next one); none during WinUAE's
    # own start
    t0 = time.monotonic()
    time.sleep(10)
    shell = None
    while time.monotonic() - t0 < 240:
        if shell is None:
            try:
                shell = AmigaShell(*serial, args.verbose)
            except ShellError:
                time.sleep(2)
                continue
        try:
            if shell.prompt(5):
                print(f"WinUAE started, serial shell ready after {time.monotonic() - t0:.0f} s")
                return shell
        except ShellError:
            shell = None
    raise Fail(2, f"WinUAE started, but no serial shell on {args.serial} after 240 s")


def amisqueeze_process(shell, exe):
    """The process number of amisqueeze in the Amiga (Status), or None"""
    for line in shell.run("Status"):
        m = re.search(r"Process\s+(\d+):.*\b" + re.escape(exe) + r"\b", line, re.I)
        if m:
            return m.group(1)
    return None


def stop_amisqueeze(shell, exe):
    """Break, tried again: during playback the serial port loses more characters. True once it is gone."""
    for _ in range(3):
        try:
            n = amisqueeze_process(shell, exe)
            if n is None:
                return True
            shell.run(f"Break {n}")
        except ShellError:
            pass
        time.sleep(2)
    try:
        return amisqueeze_process(shell, exe) is None
    except ShellError:
        return False


def reset_player(lms, name):
    """Stops the player and clears its playlist: LMS would resume a playback left running by an earlier run"""
    player = lms.player_id(name)
    if player:
        lms.player(player, "stop")
        lms.player(player, "playlist", "clear")


def main():
    parser = UsageParser(description="Plays a test tone with amisqueeze in WinUAE, driven by LMS; prints OK or "
                                     "FAIL: <reason>. See docs/development/winuae-scenarios.md.")
    parser.add_argument("--exe", default="amisqueeze", help="the program in NM: (default amisqueeze)")
    parser.add_argument("--unit", type=int, default=0, help="AHI unit (default 0)")
    parser.add_argument("--extra", default="", help='more amisqueeze options, e.g. "SWAP FLAC=OFF"')
    parser.add_argument("--track", default="tone-440.flac", help="file in the server's library")
    parser.add_argument("--name", default="Amiga", help="player name (default Amiga)")
    parser.add_argument("--server", default="127.0.0.1", help="the server as the Amiga reaches it")
    parser.add_argument("--lms", default="127.0.0.1:9090", help="LMS's CLI as this host reaches it")
    parser.add_argument("--serial", default="127.0.0.1:1234", help="the serial shell")
    parser.add_argument("--nm-dir", type=Path, default=Path(__file__).resolve().parents[2] / "out",
                        help="the host folder mounted as NM: (default the repository's out/)")
    parser.add_argument("--timeout", type=float, help="seconds to wait for the end of playback "
                                                      "(default the track's length + 30)")
    parser.add_argument("--winuae", default=os.environ.get("WINUAE"), help="WinUAE's executable (WINUAE)")
    parser.add_argument("--config", default=os.environ.get("WINUAE_CONFIG"),
                        help="the test configuration (WINUAE_CONFIG)")
    parser.add_argument("--winuae-arg", action="append", default=[],
                        help='one more WinUAE argument, repeatable, e.g. "-s cpu_speed=real"')
    parser.add_argument("-v", "--verbose", action="store_true", help="print every exchange")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)     # progress shown as it comes, also into a file or a pipe

    log = args.nm_dir / LOG_NAME

    def log_text():
        try:
            return log.read_text(encoding="latin-1")
        except OSError:
            return ""

    shell = lms = player = None
    started = False
    failure = None
    try:
        serial = host_port(args.serial, "--serial")
        lms_address = host_port(args.lms, "--lms")
        if not args.nm_dir.is_dir():
            raise Fail(3, f"--nm-dir: no folder {args.nm_dir}")

        # 1. Preconditions
        shell = open_shell(args, serial)
        try:
            lms = LmsCli(*lms_address, args.verbose)
        except LmsError as e:
            raise Fail(2, str(e))
        track = lms.find_track(args.track)
        if not track:
            raise Fail(2, f"{args.track} is not in the server's library (test sounds: tools/make-test-audio.sh, "
                          "then a rescan)")
        track_id, length = track
        print(f"LMS: {args.track}, track {track_id}, {length:.1f} s")
        if amisqueeze_process(shell, args.exe):
            raise Fail(2, f"{args.exe} is already running in the Amiga: stop it first")

        # 2. Preparation
        reset_player(lms, args.name)
        if log.exists():
            log.unlink()

        # 3. Start: the redirection goes after the program's name; Run can take about 16 s to give the prompt back
        shell.run(f"Run NM:{args.exe} >NM:{LOG_NAME} SERVER={args.server} NAME={args.name} UNIT={args.unit} "
                  f"{args.extra} VERBOSE".replace("  ", " "), timeout=30)
        started = True
        t0 = time.monotonic()
        player = wait_for(lambda: lms.player_id(args.name, connected=True), 20, 1)
        check(player, f"player {args.name} not connected to LMS within 20 s")
        print(f"{args.exe}: started, player {args.name} ({player}) connected after {time.monotonic() - t0:.1f} s")

        output = wait_for(lambda: re.search(r"^output: (AHI unit|virtual).*$", log_text(), re.M), 10)
        check(output, "the log does not say which output is used")
        print(output.group(0))
        check(output.group(0).startswith(f"output: AHI unit {args.unit}"), f"not AHI unit {args.unit}: "
              f"{output.group(0)}")

        # 4. Playback
        lms.player(player, "playlistcontrol", "cmd:load", f"track_id:{track_id}")
        t_play = time.monotonic()
        check(wait_for(lambda: lms.player(player, "mode", "?") == "play", 10), "LMS's mode is not play")

        # Elapsed time: intervals only. WinUAE's clock falls behind real time and LMS's absolute elapsed time is
        # wrong for about 50 s after a start (testing.md, "The server side"): compare how much it grows with the
        # host's clock, with a wide tolerance (a still or twice too fast clock is what this catches).
        def elapsed():
            try:
                return float(lms.player(player, "time", "?"))
            except ValueError:
                return 0.0

        check(wait_for(lambda: elapsed() > 0, 15), "the elapsed time stays at 0")
        interval = min(3.0, length / 3)
        h1, e1 = time.monotonic(), elapsed()
        time.sleep(interval)
        h2, e2 = time.monotonic(), elapsed()
        ratio = (e2 - e1) / (h2 - h1)
        print(f"playing: elapsed time +{e2 - e1:.1f} s in {h2 - h1:.1f} s")
        check(0.6 <= ratio <= 1.4, f"the elapsed time grew by {e2 - e1:.1f} s in {h2 - h1:.1f} s")

        timeout = args.timeout or length + 30
        ended = wait_for(lambda: "output: end of playback" in log_text(), timeout - (time.monotonic() - t_play),
                         0.25)
        check(ended, f"no end of playback {timeout:.0f} s after play")
        took = time.monotonic() - t_play
        print(f"end of playback after {took:.1f} s (track {length:.1f} s)")
        check(length - 1 <= took <= length + 10, f"end of playback after {took:.1f} s for a {length:.1f} s track")
        check(wait_for(lambda: lms.player(player, "mode", "?") == "stop", 5), "LMS's mode is not stop at the end")
        underruns = log_text().count("output: buffer empty")
        if underruns:
            print(f"warning: {underruns} underrun(s) (output: buffer empty)")
    except Fail as e:
        failure = e
    except (ShellError, LmsError) as e:
        failure = Fail(1 if started else 2, str(e))
    finally:
        # 5. Clean-up, whatever happened
        if started:
            gone = stop_amisqueeze(shell, args.exe)
            stopped = wait_for(lambda: re.search(r"^stopped", log_text(), re.M), 15)
            print(f"{args.exe}: {'stopped' if gone and stopped else 'NOT stopped'}")
            if not (gone and stopped) and failure is None:
                failure = Fail(1, f"{args.exe} did not stop cleanly (Break)")
        if lms:
            try:
                reset_player(lms, args.name)
            except LmsError:
                pass
            lms.close()
        if shell:
            shell.close()

    if failure:
        if failure.code == 1 and log.exists():
            print(f"--- end of {log}")
            for line in log.read_text(encoding="latin-1").splitlines()[-15:]:
                print(line)
            print("---")
        print(f"FAIL: {failure}")
        return failure.code
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
