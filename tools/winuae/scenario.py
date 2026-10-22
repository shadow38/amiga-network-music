"""What the scenarios in WinUAE share (docs/development/winuae-scenarios.md): their common options, the serial shell
and WinUAE's start, the preconditions, amisqueeze started and stopped, the free memory before and after, the
result.

A scenario builds a Scenario and hands run() its own steps, a function of that Scenario; run() does the rest and
returns the exit code: 0 OK, 1 a check failed, 2 environment, 3 usage.
"""
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True          # no __pycache__ next to the tools
from amiga_shell import RUN_DONE, AmigaShell, ShellError, UsageParser  # noqa: E402,F401
from lms_cli import LmsCli, LmsError  # noqa: E402

REPOSITORY = Path(__file__).resolve().parents[2]


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


def add_options(parser):
    """The options every scenario takes, and the runner passes on"""
    parser.add_argument("--name", default="Amiga", help="player name (default Amiga)")
    parser.add_argument("--server", default="127.0.0.1", help="the server as the Amiga reaches it")
    parser.add_argument("--lms", default="127.0.0.1:9090", help="LMS's CLI as this host reaches it")
    parser.add_argument("--serial", default="127.0.0.1:1234", help="the serial shell")
    parser.add_argument("--nm-dir", type=Path, default=REPOSITORY / "out",
                        help="the host folder mounted as NM: (default the repository's out/)")
    parser.add_argument("--memory-slack", type=int, default=16,
                        help="KB of fast memory, free or in the largest block, that may be missing after the stop "
                             "(default 16)")
    parser.add_argument("--winuae", default=os.environ.get("WINUAE"), help="WinUAE's executable (WINUAE)")
    parser.add_argument("--config", default=os.environ.get("WINUAE_CONFIG"),
                        help="the test configuration (WINUAE_CONFIG)")
    parser.add_argument("--winuae-arg", action="append", default=[],
                        help='one more WinUAE argument, repeatable, e.g. "-s cpu_speed=real"')
    parser.add_argument("-v", "--verbose", action="store_true", help="print every exchange")


def winuae_running():
    try:
        if os.name == "nt":
            out = subprocess.run(["tasklist", "/NH"], capture_output=True, text=True).stdout
            return re.search(r"^winuae", out, re.I | re.M) is not None
        return subprocess.run(["pgrep", "-i", "winuae"], capture_output=True).returncode == 0
    except OSError:
        return False


def open_shell(args):
    """The serial shell; WinUAE started first if it is not running and --winuae and --config are given"""
    serial = host_port(args.serial, "--serial")
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


def fast_memory(shell):
    """(free, largest block) of fast memory in bytes, after Avail FLUSH (unused libraries and devices expunged
    first, so that ahi.device loaded by a first run is not taken for a leak)"""
    for line in shell.run("Avail FLUSH"):
        m = re.match(r"\s*fast\s+(\d+)\s+\d+\s+\d+\s+(\d+)", line.replace(",", ""), re.I)
        if m:
            return int(m.group(1)), int(m.group(2))
    raise ShellError("Avail FLUSH: no line for fast memory")


class Scenario:
    """One run of amisqueeze in WinUAE, driven by LMS.

    exe: the program in NM:; log_name: its output, redirected into NM:; tracks: the files it needs in the server's
    library; options: amisqueeze's own options (SERVER, NAME and VERBOSE are added); output: how the log's
    "output:" line must begin ("AHI unit 0", "virtual"); stream_format: the format every strm s must give
    ("p" raw PCM, "f" FLAC, "m" MP3; None: not checked); files: other files of NM: deleted first (OUTPUT)."""

    def __init__(self, args, exe, log_name, tracks, options="", output="AHI unit 0", stream_format=None,
                 files=()):
        self.args = args
        self.exe = exe
        self.log = args.nm_dir / log_name
        self.log_name = log_name
        self.track_names = tracks
        self.options = options
        self.output = output
        self.stream_format = stream_format
        self.files = [args.nm_dir / f for f in files]
        self.shell = self.lms = self.player = None
        self.tracks = {}                    # file -> (track id, length in seconds)
        self.exits = []                     # what the steps want done in the clean-up (a volume set back)

    def log_text(self):
        try:
            return self.log.read_text(encoding="latin-1")
        except OSError:
            return ""

    def query(self, *terms):
        """A command to the player in LMS; returns the last term of the answer"""
        return self.lms.player(self.player, *terms)

    def mode(self):
        return self.query("mode", "?")

    def elapsed(self):
        try:
            return float(self.query("time", "?"))
        except ValueError:
            return 0.0

    def index(self):
        try:
            return int(self.query("playlist", "index", "?"))
        except ValueError:
            return -1

    def on_exit(self, action):
        self.exits.append(action)

    def check_playing(self, what, interval=3.0, settle=0.0):
        """Mode play, and the elapsed time grows with the host's clock. Intervals only: WinUAE's clock falls
        behind real time and LMS's absolute elapsed time is wrong for about 50 s after a start or a seek
        (testing.md, "The server side"); the tolerance is wide (a still or twice too fast clock is what this
        catches). settle: seconds left to LMS first (after a resume, its elapsed time takes a moment to move)."""
        check(wait_for(lambda: self.mode() == "play", 10), f"{what}: LMS's mode is not play")
        check(wait_for(lambda: self.elapsed() > 0, 15), f"{what}: the elapsed time stays at 0")
        time.sleep(settle)
        h1, e1 = time.monotonic(), self.elapsed()
        time.sleep(interval)
        h2, e2 = time.monotonic(), self.elapsed()
        print(f"{what}: elapsed time +{e2 - e1:.1f} s in {h2 - h1:.1f} s")
        check(0.6 <= (e2 - e1) / (h2 - h1) <= 1.4, f"{what}: the elapsed time grew by {e2 - e1:.1f} s "
                                                   f"in {h2 - h1:.1f} s")

    def check_paused(self, what, interval=3.0):
        """Mode pause, and the elapsed time stands still"""
        check(wait_for(lambda: self.mode() == "pause", 5), f"{what}: LMS's mode is not pause")
        e1 = self.elapsed()
        time.sleep(interval)
        e2 = self.elapsed()
        print(f"{what}: elapsed time {e1:.1f} s, then {e2:.1f} s")
        check(abs(e2 - e1) < 0.3, f"{what}: the elapsed time moved from {e1:.1f} s to {e2:.1f} s")

    def run(self, steps, after_stop=None):
        """Preconditions, start, steps(self), clean-up whatever happened, then after_stop(self) once amisqueeze
        stopped cleanly (an OUTPUT file closed); prints the result and returns the exit code"""
        args = self.args
        sys.stdout.reconfigure(line_buffering=True)     # progress shown as it comes, also into a file or a pipe
        started = stopped = False
        memory = None
        failure = None
        try:
            if not args.nm_dir.is_dir():
                raise Fail(3, f"--nm-dir: no folder {args.nm_dir}")
            if not (args.nm_dir / self.exe).is_file():
                raise Fail(2, f"no {self.exe} in {args.nm_dir}: build it first")

            # 1. Preconditions
            self.shell = open_shell(args)
            try:
                self.lms = LmsCli(*host_port(args.lms, "--lms"), args.verbose)
            except LmsError as e:
                raise Fail(2, str(e))
            for name in self.track_names:
                track = self.lms.find_track(name)
                if not track:
                    raise Fail(2, f"{name} is not in the server's library (test sounds: "
                                  "tools/make-test-audio.sh, then a rescan)")
                self.tracks[name] = track
                print(f"LMS: {name}, track {track[0]}, {track[1]:.1f} s")
            if amisqueeze_process(self.shell, self.exe):
                raise Fail(2, f"{self.exe} is already running in the Amiga: stop it first")

            # 2. Preparation
            reset_player(self.lms, args.name)
            for f in [self.log] + self.files:
                if f.exists():
                    f.unlink()
            memory = fast_memory(self.shell)
            print(f"fast memory: {memory[0]} bytes free, largest block {memory[1]}")

            # 3. Start: the redirection goes after the program's name; Run's prompt can come with amisqueeze's
            # output after it (RUN_DONE). From here the clean-up stops amisqueeze whatever happens.
            started = True
            t0 = time.monotonic()
            self.shell.run(f"Run NM:{self.exe} >NM:{self.log_name} SERVER={args.server} NAME={args.name} "
                           f"{self.options} VERBOSE".replace("  ", " "), timeout=30, until=RUN_DONE)
            self.player = wait_for(lambda: self.lms.player_id(args.name, connected=True), 20, 1)
            check(self.player, f"player {args.name} not connected to LMS within 20 s")
            print(f"{self.exe}: started, player {args.name} ({self.player}) connected after "
                  f"{time.monotonic() - t0:.1f} s")
            output = wait_for(lambda: re.search(r"^output: (AHI unit|virtual).*$", self.log_text(), re.M), 10)
            check(output, "the log does not say which output is used")
            print(output.group(0))
            check(output.group(0).startswith(f"output: {self.output}"),
                  f"not output: {self.output}: {output.group(0)}")

            # 4. The scenario's own steps
            steps(self)

            # Every stream in the expected format: the path under test was the one taken
            formats = re.findall(r"^stream: format (.)", self.log_text(), re.M)
            print(f"streams: {len(formats)}, format {' '.join(sorted(set(formats))) or 'none'}")
            if self.stream_format:
                check(formats and set(formats) == {self.stream_format},
                      f"stream formats {' '.join(formats) or 'none'}, expected only {self.stream_format}")
            underruns = self.log_text().count("output: buffer empty")
            if underruns:
                print(f"warning: {underruns} underrun(s) (output: buffer empty)")
        except Fail as e:
            failure = e
        except (ShellError, LmsError) as e:
            failure = Fail(1 if started else 2, str(e))
        finally:
            # 5. Clean-up, whatever happened
            if started:
                gone = stop_amisqueeze(self.shell, self.exe)
                stopped = gone and bool(wait_for(lambda: re.search(r"^stopped", self.log_text(), re.M), 15))
                print(f"{self.exe}: {'stopped' if stopped else 'NOT stopped'}")
                if not stopped and failure is None:
                    failure = Fail(1, f"{self.exe} did not stop cleanly (Break)")
            if self.lms:
                try:
                    for action in self.exits:
                        action()
                    reset_player(self.lms, args.name)
                except LmsError:
                    pass
                self.lms.close()
            # Free memory back where it was: a corrupted free list shows first in the largest block
            if stopped and memory:
                try:
                    after = fast_memory(self.shell)
                    print(f"fast memory after the stop: {after[0]} bytes free, largest block {after[1]}")
                    slack = args.memory_slack * 1024
                    if failure is None and (after[0] < memory[0] - slack or after[1] < memory[1] - slack):
                        failure = Fail(1, f"fast memory not given back: {memory[0] - after[0]} bytes less free, "
                                          f"largest block {memory[1] - after[1]} bytes smaller")
                except ShellError as e:
                    if failure is None:
                        failure = Fail(1, str(e))
            if self.shell:
                self.shell.close()

        if failure is None and after_stop:
            try:
                after_stop(self)
            except Fail as e:
                failure = e

        if failure:
            if failure.code == 1 and self.log.exists():
                print(f"--- end of {self.log}")
                for line in self.log.read_text(encoding="latin-1").splitlines()[-15:]:
                    print(line)
                print("---")
            print(f"FAIL: {failure}")
            return failure.code
        print("OK")
        return 0
