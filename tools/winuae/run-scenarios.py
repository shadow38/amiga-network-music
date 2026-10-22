#!/usr/bin/env python3
"""Runs the scenarios in WinUAE one after the other and prints one line for each.

Usage:
  run-scenarios.py [--only NAME...] [common options]

First, once: the serial shell (WinUAE started if needed, as by each scenario), the WinUAE profile (both decoders
on the Amiga at least twice real time: the JIT and a 68882 with compfpu, checked with NM:amisqueeze.fpu BENCH),
and a warm-up (amisqueeze started on AHI unit 0, then stopped: the first AHI run after a boot keeps about 12 KB,
which the memory check would take for a leak). Then each scenario in its own process, with the common options.
Common options (scenario.py): --name, --server, --lms, --serial, --nm-dir, --memory-slack,
--winuae EXE --config FILE [--winuae-arg ARG...], -v.

What it does and checks: docs/development/winuae-scenarios.md. The last line is OK or FAIL: <reasons>. Exit code:
the worst of the scenarios' (0 OK, 1 a check failed, 2 environment: the next scenarios are not run, 3 usage).
"""
import re
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True          # no __pycache__ next to the tools
sys.path.insert(0, str(Path(__file__).resolve().parent))
from scenario import (RUN_DONE, Fail, LmsCli, LmsError, ShellError, UsageParser, add_options,  # noqa: E402
                      amisqueeze_process, host_port, open_shell, reset_player, stop_amisqueeze, wait_for)

HERE = Path(__file__).resolve().parent
SCENARIOS = [
    ("play-tone", ["play-tone.py"]),
    ("commands-pcm", ["lms-commands.py", "--flac", "off"]),
    ("commands-flac", ["lms-commands.py", "--flac", "on"]),
    ("output-pcm", ["output-compare.py", "--case", "pcm"]),
    ("output-flac", ["output-compare.py", "--case", "flac"]),
    ("output-mp3", ["output-compare.py", "--case", "mp3"]),
]
PROFILE = ("the JIT and a 68882 with compfpu: -s fpu_model=68882 -s compfpu=true with the JIT profile's lines "
           "(docs/development/testing.md, Profiles)")


def prepare(args):
    """The serial shell, the profile, the warm-up; raises Fail(2) when the environment is not ready"""
    for exe in ("amisqueeze", "amisqueeze.fpu"):
        if not (args.nm_dir / exe).is_file():
            raise Fail(2, f"no {exe} in {args.nm_dir}: build it first")
    shell = open_shell(args)
    lms = None
    try:
        for exe in ("amisqueeze", "amisqueeze.fpu"):
            if amisqueeze_process(shell, exe):
                raise Fail(2, f"{exe} is already running in the Amiga: stop it first")
        bench = "\n".join(shell.run("NM:amisqueeze.fpu BENCH", timeout=30))
        if "needs an FPU" in bench:
            raise Fail(2, f"no FPU in this Amiga: restart WinUAE with {PROFILE}")
        for line in bench.splitlines():
            print(f"profile: {line}")
        for fmt in ("FLAC", "MP3"):
            if not re.search(f"{fmt}=AUTO lets this machine decode", bench):
                raise Fail(2, f"{fmt} decoded too slowly on this Amiga: restart WinUAE with {PROFILE}")

        try:
            lms = LmsCli(*host_port(args.lms, "--lms"), args.verbose)
        except LmsError as e:
            raise Fail(2, str(e))
        reset_player(lms, args.name)
        log = args.nm_dir / "warm-up.log"
        if log.exists():
            log.unlink()
        shell.run(f"Run NM:amisqueeze >NM:{log.name} SERVER={args.server} NAME={args.name} UNIT=0 VERBOSE",
                  timeout=30, until=RUN_DONE)

        def opened():
            try:
                return re.search(r"^output: AHI unit 0", log.read_text(encoding="latin-1"), re.M)
            except OSError:
                return None

        ok = wait_for(opened, 15)
        if not stop_amisqueeze(shell, "amisqueeze") or not ok:
            raise Fail(2, "warm-up: amisqueeze did not open AHI unit 0, or did not stop")
        reset_player(lms, args.name)
        print("warm-up: AHI unit 0 opened and closed")
    except (ShellError, LmsError) as e:
        raise Fail(2, str(e))
    finally:
        if lms:
            lms.close()
        shell.close()


def main():
    parser = UsageParser(description="Runs the scenarios in WinUAE; prints one line for each, then OK or FAIL. "
                                     "See docs/development/winuae-scenarios.md.")
    names = [name for name, _ in SCENARIOS]
    parser.add_argument("--only", nargs="+", choices=names, metavar="NAME",
                        help=f"only these scenarios: {', '.join(names)}")
    add_options(parser)
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    common = ["--name", args.name, "--server", args.server, "--lms", args.lms, "--serial", args.serial,
              "--nm-dir", str(args.nm_dir), "--memory-slack", str(args.memory_slack)] + (["-v"] if args.verbose else [])

    try:
        if not args.nm_dir.is_dir():
            raise Fail(3, f"--nm-dir: no folder {args.nm_dir}")
        prepare(args)
    except Fail as e:
        print(f"FAIL: {e}")
        return e.code

    results = []
    for name, command in SCENARIOS:
        if args.only and name not in args.only:
            continue
        print(f"=== {name}")
        t0 = time.monotonic()
        proc = subprocess.Popen([sys.executable, str(HERE / command[0])] + command[1:] + common,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="latin-1")
        last = ""
        for line in proc.stdout:
            print(f"  {line.rstrip()}")
            if line.strip():
                last = line.strip()
        code = proc.wait()
        results.append((name, code, last, time.monotonic() - t0))
        if code == 2:
            break

    print("=== results")
    for name, code, last, took in results:
        print(f"{name:<14} {last if code else 'OK':<60} {took:4.0f} s")
    for name, _ in SCENARIOS:
        if (not args.only or name in args.only) and name not in [r[0] for r in results]:
            print(f"{name:<14} not run")
    worst = max((code for _, code, _, _ in results), default=0)
    failed = [name for name, code, _, _ in results if code]
    print(f"FAIL: {', '.join(failed)}" if failed else "OK")
    return worst


if __name__ == "__main__":
    sys.exit(main())
