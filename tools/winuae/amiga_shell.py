#!/usr/bin/env python3
"""Client of the AmigaDOS Shell on WinUAE's serial port (docs/development/testing.md, "Serial shell").

The test configuration connects the Amiga's serial port to a TCP socket (win32.serial_port=TCP://127.0.0.1:1234)
and S:User-Startup opens a Shell on AUX:. This client types a command line, checks its echo, and reads the output
up to the next prompt. One connection serves many commands; close() frees it in WinUAE.

As a module: AmigaShell(host, port).run("Status") returns the output lines.
As a command:
  amiga_shell.py [--serial HOST:PORT] [--timeout SECONDS] [-v] COMMAND

Exit code: 0 done, 1 no prompt after the command (still running?) or command not received right, 2 no serial
shell, 3 usage.
"""
import argparse
import re
import socket
import sys
import time

# The prompt: "3.System:> " (process number, current directory)
PROMPT = re.compile(r"(^|[\r\n])\d+\.[^\r\n]*> ?$")
# The console's control sequences (CSI = 0x9B or ESC [, e.g. CSI 20h around the prompt) and lone control
# characters (0x0F before the prompt); tab, CR and LF are kept
CSI = re.compile(r"(\x9b|\x1b\[)[0-9;?]*[ -/]*[@-~]")
CTRL = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")


def clean(text):
    return CTRL.sub("", CSI.sub("", text))


class ShellError(Exception):
    pass


class AmigaShell:
    def __init__(self, host="127.0.0.1", port=1234, verbose=False):
        self.verbose = verbose
        self.buf = ""
        try:
            self.sock = socket.create_connection((host, port), timeout=5)
        except OSError as e:
            raise ShellError(f"no serial shell on {host}:{port} ({e})") from e

    def log(self, text):
        if self.verbose:
            for line in text.splitlines():
                print(f"  shell| {line}", file=sys.stderr)

    def send(self, text):
        """One character at a time, 10 ms apart: the emulated port has no flow control and loses characters
        sent in a block"""
        try:
            for byte in text.encode("latin-1"):
                self.sock.sendall(bytes([byte]))
                time.sleep(0.01)
        except OSError as e:
            raise ShellError(f"serial shell: {e}") from e

    def read_until(self, pattern, seconds):
        """Reads until the cleaned text received matches `pattern`; returns (text, matched) and empties the buffer"""
        deadline = time.monotonic() + seconds
        while True:
            text = clean(self.buf)
            if re.search(pattern, text):
                self.buf = ""
                return text, True
            left = deadline - time.monotonic()
            if left <= 0:
                self.buf = ""
                return text, False
            self.sock.settimeout(min(left, 0.2))
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            except OSError as e:
                raise ShellError(f"serial shell: {e}") from e
            if not data:
                raise ShellError("serial shell connection closed")
            self.buf += data.decode("latin-1")

    def prompt(self, seconds=5):
        """A fresh prompt: True if the Shell answers"""
        self.send("\r")
        return self.read_until(PROMPT, seconds)[1]

    def run(self, command, timeout=15):
        """Runs one command line; returns its output lines. The command is typed without Return and its echo
        compared first: if characters were lost, the line is erased (backspaces) and typed again, so a command
        received wrong is never run."""
        if not self.prompt():
            raise ShellError("no prompt from the serial shell")
        self.log(f"> {command}")
        time.sleep(0.3)
        for attempt in range(4):
            self.send(command)
            text, _ = self.read_until(re.escape(command) + "$", 2)
            # The space that ends the prompt can come after the prompt was recognised: it is not part of the echo
            echo = text.replace("\r", "").replace("\n", "").lstrip(" ")
            if echo == command:
                break
            self.log(f"! echo '{echo}', typed again")
            self.send("\b" * (len(echo) + 2))
            self.read_until("x^", 1)            # never matches: lets the backspaces' echo go by
        else:
            self.send("\b" * (len(command) + 2))
            raise ShellError(f"command not received right after 4 attempts: {command}")
        self.send("\r")
        text, done = self.read_until(PROMPT, timeout)
        lines = [line for line in re.split(r"\r\n|\n|\r", text)
                 if line.strip() and line.strip() != command and not PROMPT.search(line)]
        self.log("\n".join(lines))
        if not done:
            raise ShellError(f"no prompt {timeout} s after: {command} (still running?)")
        return lines

    def close(self):
        """WinUAE sees the end of a connection only when the Amiga writes to the port: the prompt that "Wait 1"
        prints a second later frees it for the next one"""
        try:
            self.send("Wait 1\r")
        except ShellError:
            pass
        self.sock.close()


class UsageParser(argparse.ArgumentParser):
    """argparse with exit code 3 for a usage error (argparse's own is 2, an environment error here)"""

    def error(self, message):
        self.print_usage(sys.stderr)
        print(f"{self.prog}: {message}", file=sys.stderr)
        sys.exit(3)


def main():
    parser = UsageParser(description="Runs one command in the AmigaDOS Shell of WinUAE's serial port.")
    parser.add_argument("command")
    parser.add_argument("--serial", default="127.0.0.1:1234", help="host:port of the serial shell")
    parser.add_argument("--timeout", type=int, default=15, help="seconds to wait for the next prompt")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    host, _, port = args.serial.rpartition(":")
    try:
        shell = AmigaShell(host, int(port), args.verbose)
    except (ShellError, ValueError) as e:
        print(e, file=sys.stderr)
        return 2
    try:
        for line in shell.run(args.command, args.timeout):
            print(line)
        return 0
    except ShellError as e:
        print(e, file=sys.stderr)
        return 1
    finally:
        shell.close()


if __name__ == "__main__":
    sys.exit(main())
