# Testing

For contributors testing a change: which test covers what, the host build and its tests, the tests in WinUAE, and
the checks only the Amiga can make. Building is in [building.md](building.md).

## What tests what

From the most automated to the most manual. A change is tested at every level it reaches: a change to the
portable core by `make test`, a change to the Amiga side in WinUAE too.

| Level | What runs | What it covers | What it does not |
|---|---|---|---|
| Unit tests (`make test`) | `tests/test_core.c` on `tests/stub_platform.c`: the core with a simulated clock, sockets and audio device | the packets of slimproto, the stream and its states, the queue of tracks, the output and its statuses, the decoders on the files of `tests/data/`, the audio blocks and the rules of AHI (`stub_audio_bad`), ICY, the discovery's replies, the Workbench's helpers (ToolTypes, window, message history) | the system: sockets, time, AHI, AmigaOS |
| Scenarios against a fake server (`make test`) | `tests/fake_server.py` against `out-host/amisqueeze` (`posix.c`), on localhost | the program from end to end, with real sockets and real time: see [Host build and tests](#host-build-and-tests) | AHI and AmigaOS; the servers' real behaviour |
| Checks of `make test` | `tools/check-version.py`, `check-core.py`, `check-quality.py`, the self-test of `check-saves.py`, the scripts' syntax, the Aminet readme, the archives' texts in ASCII | the rules a script can hold | |
| Checks of the Amiga build | `tools/check-saves.py` on the assembly, `tools/check-stack.py` on the linker map | the compiler's register bug; the stack's set-up | how much stack is used |
| Scenarios in WinUAE | `python3 tools/winuae/run-scenarios.py`, with LMS ([winuae-scenarios.md](winuae-scenarios.md)) | the Amiga build with a real LMS: a test tone through AHI, LMS's commands, the `OUTPUT` file compared with the source | Music Assistant; a real TCP/IP stack (unless the profile [Real network](#real-network)); listening |
| By hand in WinUAE | the [checks the host tests cannot make](#checks-the-host-tests-cannot-make), and what no scenario covers yet | the stack, the speed, the memory, the sound; Music Assistant, the window and Exchange | the speed of a real machine |
| On the real machines | listening and measures, at the end of a phase and when a change needs it (the plan's [method](README.md#method)) | the sound, the CPU load, a real network card and TCP/IP stack | |

## Host build and tests

### Requirements

- a **native C compiler** for the build machine (`$(HOSTCC)`, default `cc`) with AddressSanitizer and
  UndefinedBehaviorSanitizer support, POSIX sockets;
- `python3` (fake server);
- [lizard](https://github.com/terryyin/lizard) and what it needs, at the versions of `tools/requirements.txt`
  (the size and complexity check): `pip install --require-hashes -r tools/requirements.txt`, in a virtual
  environment if the system's Python refuses it.

Tested with gcc 14 on Linux. The CI uses gcc 14 too (`HOSTCC=gcc-14` on GitHub's `ubuntu-24.04` runner, whose
default `cc` is gcc 13), and prints its version. clang, macOS
and other POSIX systems should work but have not been tried.

Note that in the `amigadev/crosstools` image, `gcc` is the Amiga cross-compiler: use another environment, or set
`HOSTCC` to a native compiler.

### Running them

amisqueeze is split into a **portable core** in standard C (no Amiga headers) and a thin **platform layer**
(`platform.h`): `amiga.c` and `amiga_ahi.c` on AmigaOS, `posix.c` on Linux and other POSIX systems. The same core
therefore also runs on the build machine, where it is fast to test (how the core and the platform layer fit
together: [architecture](architecture.md)):

```sh
make host                         # out-host/amisqueeze, with AddressSanitizer and UBSan
out-host/amisqueeze SERVER=192.168.1.10 NAME=Test VERBOSE     # same arguments as on the Amiga; Ctrl-C stops it
make test                         # unit tests of the core, then scenarios against out-host/amisqueeze
make test HOSTCC=clang            # another native compiler (untested)
```

Before the tests, `make test` runs `tools/check-version.py`: the version number must be the same in both `$VER:` strings
(declared as `static char <name>[] = "$VER: ...";`, never const), `Firmware=` and the user guide's state line
(`Version x.y in development` for `x.y-dev`, `Version x.y,` for a release); the latest release named by the guide
must be the newest section of `CHANGELOG.md`; the commodity's title must fit 39 characters. The README names no
version: its badge shows the latest release published on GitHub.
`make test` needs no LMS server: the unit tests run the core on a fake platform (`tests/stub_platform.c`), and
`tests/fake_server.py` plays a slimproto server and its HTTP streams on localhost (handshake, renaming,
disconnection and reconnection, a server silent for 35 s, Ctrl-C, argument errors; server discovery: one server, two, none, malformed
replies, found again after a failed connection; streams played, paused, stopped, flushed, refused, held,
cut, answered with an HTTP error; an internet radio's metadata blocks taken out of the sound and sent back; the
`OUTPUT` file compared with what was sent; FLAC and MP3 test files decoded from `tests/data/`, whole or
from the middle as after a seek, back to back with raw PCM). It also builds amisqueeze without the MP3 decoder
(`out-host/amisqueeze-nomp3`), as `out/amisqueeze` is, and checks what that build advertises and refuses. The
syntax of every Python and shell script of `tools/` and `tests/` is checked too, those run only by hand included.
`tools/check-quality.py` holds the C code to the size and complexity limits of [code-rules.md](code-rules.md), and
keeps step names out of the code.
The Amiga build must still be checked in WinUAE: the platform layers differ.

### On GitHub

GitHub runs `make test` on each push and pull request (`.github/workflows/tests.yml`, GitHub Actions, Ubuntu); the
badge at the top of the README shows the result of the latest run. A second job builds the Amiga programs and
their archives: [Builds made by GitHub](building.md#builds-made-by-github).

The tests use real time (playback pacing, reconnection delays), so a slow runner can make one fail: rerun it
before suspecting the code. The reverse happens too: a race can pass on a fast machine and fail on the runner. After
changing when the output starts, ends or runs dry (the statuses at a track's end, `STMo`, `STMu`), run the
scenarios concerned several times, not once.

## Testing in WinUAE

How amisqueeze is tested in an emulated Amiga: the machine, its profiles, AHI, how to drive it from the host, the
server side, the CPU load, and the test sounds. The plan's steps behind this setup: tools-4, tools-5 and tools-7
of [phase-0-tooling.md](phase-0-tooling.md).

### What you need

- **WinUAE** 6.0 (Windows), or a real machine, with a TCP/IP stack: WinUAE's own emulation (`bsdsocket_emu`) is
  enough.
- **A Kickstart ROM and AmigaOS**, 3.1 or later (tested with 3.2): both are licensed, never in the repository.
  AmigaOS installed on a hard file (`.hdf`).
- **AHI 6 and MUI** in that system, from Aminet, as the [user guide](../user-guide.md#installing-ahi) describes;
  WinUAE's Toccata card, the 16-bit reference, also needs `toccata.library` (Aminet).
- **A server** reachable from the host: Lyrion Music Server or Music Assistant, set up as the
  [user guide](../user-guide.md) says, with the [test sounds](#test-sounds) in its music folder. The
  [automated scenarios](#automated-scenarios) need LMS, running, on the host or forwarded to it
  ([what they expect of it](winuae-scenarios.md#running-them)). With LMS, add the user guide's `mp3 pcm` rule
  ([server setup](../user-guide.md#server-setup)), to test the builds that leave MP3 to the server. Its CLI
  (9090/tcp) is handy to drive it from scripts ([the server side](#the-server-side)), and squeezelite on the PC as
  a reference player (steps tools-2 and tools-3 of the [tooling plan](phase-0-tooling.md)).

### The test machine

[`winuae/amisqueeze-test.uae`](../../winuae/amisqueeze-test.uae) is a template: copy it outside the repository,
put your paths in place of its `<...>`, and start it with `winuae64.exe -f <your copy>`. The settings that matter:

| Setting | Why |
|---|---|
| A1200, AGA, 68020, 2 MB chip, 8 MB fast | the classic target; Zorro II fast RAM (a 16-bit bus, slower than an accelerator's own) |
| `bsdsocket_emu=true` | WinUAE provides `bsdsocket.library` through the host's network: no TCP/IP stack in the Amiga, and a server on the host is `127.0.0.1` |
| `filesystem2=rw,NM:...:<repository>/out,-128` | the programs under test as `NM:` (see [the two volumes](#the-two-volumes)); boot priority -128, never booted from |
| `filesystem2=ro,WT:...` | your Amiga-side test tools as `WT:` (optional) |
| `sound_stereo_separation=10` | full separation (the default 7 mixes the sides: left would be heard on the right) |
| `toccata=true` | an emulated 16-bit sound card, the reference for Paula |
| `win32.serial_port=TCP://127.0.0.1:1234` | the [serial shell](#serial-shell); the `win32.` prefix is required (otherwise "unknown config entry" in `winuaebootlog.txt`) |
| `win32.iconified_pause=false`, `win32.inactive_pause=false` | long unattended runs: a paused emulation once stopped an hour of playback after 5 minutes |

**Never start two configurations on the same hard file at once**: it would be corrupted.

In a WinUAE configuration file (syntax checked in WinUAE's `cfgfile.cpp`), `chipmem_size` counts in 512 KB units
and `fastmem_size` in MB.

#### The two volumes

Host folders mounted as Amiga volumes, so that nothing is copied onto the system hard file:

- **`NM:`** ("NetMusic", read-write): the repository's `out/` folder. What `make` writes there runs at once
  (`NM:amisqueeze SERVER=127.0.0.1 VERBOSE`), and what amisqueeze writes there is read on the host as it runs: its
  `LOG` and `OUTPUT` files (`LOG=NM:amisqueeze.log`), or a redirected output (`Run NM:amisqueeze >NM:out.log ...`).
- **`WT:`** ("WinUAE Test", read-only): your own Amiga-side tools, kept outside the repository. For instance:
  - AmigaDOS scripts run with `Execute WT:<script>`: a test sequence (versions, `Avail`, a playback with its log),
    the installation of AHI, MUI or the serial shell from their archives, a test tone through AHI;
  - amiagent and its start-up script;
  - the test machine's sign: `S:User-Startup` starts test-only programs (amiagent) only when `WT:` is mounted, so
    that the same hard file started from another configuration runs none of them (see
    [amiagent](#amiagent-optional)).

  Read-only, so that a test cannot change its own tools. amisqueeze itself needs only `NM:`: without `WT:`, test
  from a Shell.

### Profiles

The template is the **default** profile: a 68020 as fast as the host allows (about 45 MHz). The others override
the CPU lines on the command line (`-s name=value`):

| Profile | Overrides | For |
|---|---|---|
| **slow** | `-s cpu_compatible=true -s cpu_speed=real -s cpu_multiplier=4` | a 68020 at 4 x 7.09 = 28.4 MHz, as a typical A1200 accelerator: CPU load, Paula through AHI |
| **JIT** | `-s cpu_24bit_addressing=false -s cachesize=16384 -s cycle_exact=false -s cpu_cycle_exact=false -s cpu_memory_cycle_exact=false -s cpu_speed=max -s cpu_compatible=false` | about 230 times the slow profile, the order of a PiStorm: decoding on the Amiga (FLAC, MP3) |
| **+ FPU** | `-s fpu_model=68882`, with the JIT also `-s compfpu=true` | `amisqueeze.fpu`; without `compfpu` the JIT interprets the FPU (MP3 at 1.8x real time instead of 74x) |

Not a 68040: it needs its CPU library (`68040.library`) in the system. Speeds measured in WinUAE are not those
of a real machine: the plan leaves timings to the real machines (step player-9).

#### Real network

With `bsdsocket_emu`, the TCP/IP stack runs on the host and costs the Amiga nothing. To count it (step tools-11),
add to a profile (usually **slow**):

```
-s bsdsocket_emu=false -s ne2000pcmcia_rom_file=:ENABLED -s ne2000pcmcia_rom_options=inserted=true
-s slirp_redir=tcp:7846:7846:10.0.2.15 -s fastmem_size=4
```

- **The card**: WinUAE's NE2000-compatible PCMCIA card (a CNet CN40BC to the Amiga), on slirp, WinUAE's own NAT:
  its DHCP gives the Amiga 10.0.2.15, and the host is 10.0.2.2 (`SERVER=10.0.2.2`). The older
  `ne2000_pcmcia=slirp` is read only from configurations older than WinUAE 3.4, and is ignored otherwise.
- **4 MB of fast memory at most**: beyond, the Zorro II memory covers the PCMCIA space (`$600000`) and the
  Kickstart's `card.resource` does not start ("no PCMCIA slot"), as on a real A1200.
- **In the Amiga**: a TCP/IP stack and a SANA-II driver for that card. Tried: AmiNetXDuo 1.0.0-rc2 ("fat"
  variant) with `cnet.device` 1.9 (Aminet `driver/net/cnetdevice`, 68020 version); AmiNetXDuo's own
  `anxnet.device` does not open on the emulated card. Start the stack from `S:User-Startup` only when no
  `bsdsocket.library` answers (`FailAt 21`, `Version >NIL: bsdsocket.library`, `If WARN`), so that the same hard
  file still boots the other profiles on WinUAE's emulation.
- **amiagent** through `slirp_redir` (port 7846 of the host to the Amiga): slirp accepts the connection on the
  host even when nothing listens in the Amiga, then closes it, so a successful connect does not mean amiagent is
  up. The redirected port listens on all the host's interfaces, as with `bsdsocket_emu`: the firewall rule of
  [amiagent](#amiagent-optional) still applies.
- **The serial shell** sometimes loses characters in this profile (a command is retyped, or not sent): keep its
  command lines short.

### AHI

Set the units in `SYS:Prefs/AHI` (the [user guide](../user-guide.md#setting-up-the-unit) explains the modes). The
tests use:

| Unit | Mode | For |
|---|---|---|
| 0 | `Paula:14 bit stereo++`, 27,429 Hz | the main test: Paula through AHI, as on an Amiga without a sound card |
| 1 | `Toccata:16 bit stereo++`, 44,100 Hz | the 16-bit reference |
| 2 | `Paula:8 bit stereo++`, 27,429 Hz | the lighter Paula mode |
| 3 | `Toccata:16 bit stereo` (without "++") | the start-up warning of a mode that plays one side only |

- With Paula in WinUAE, left and right come out swapped: use `SWAP`.
- Judge Paula at volume 100 on the server: a lower volume scales the samples down and brings out Paula's noise
  (a crackle at 50). Occasional crackles also occur with AHI's own test tone: WinUAE's, not amisqueeze's.
- A "Filesave" mode on a unit plays nothing (it is not real time): the bytes amisqueeze plays are checked with
  its `OUTPUT` option instead.
- The AHI preferences are read from `ENV:Sys/ahi.prefs`: a test can change a unit for the session by writing that
  file (lost at the next start, `ENV:` being copied from `ENVARC:`), then `Avail FLUSH` (as the tests of
  2026-10-22 did). With `VERBOSE`, amisqueeze prints the mode and frequency it reads there.

### Driving the Amiga from the host

#### Serial shell

An AmigaDOS Shell on the serial port, reached through the TCP socket of `win32.serial_port`. In
`S:User-Startup`:

```
Mount AUX:
Run >NIL: NewShell AUX:
```

A host script connects to `127.0.0.1:1234`, sends a command line and reads the output up to the next prompt
(`<n>.<dir>> `). What it must handle:

- **Send one character at a time**, about 10 ms apart: the emulated port has no flow control and loses characters
  sent in a block. Compare the echo with the command before pressing Return, and type it again if it differs
  (backspaces): a command received wrong is then never run.
- **Filter the console's control sequences**: `CSI` (`0x9B` or `ESC [`) sequences around the prompt, and a lone
  `0x0F` before it. Text is ISO-8859-1.
- **Closing**: WinUAE sees the end of a connection only when the Amiga writes to the port; otherwise the closed
  connection stays attached and blocks the next one. Send `Wait 1` and Return just before closing: the prompt
  printed a second later frees it.
- **Programs in the background**: the redirection goes after the program's name
  (`Run NM:amisqueeze >NM:amisqueeze.log VERBOSE`), then `Status` gives its number and `Break <n>` stops it.
  Never leave a long program in the foreground of this Shell.
- If the Shell hangs: from another Shell in the Amiga, `Status`, then `Break <n>` twice on the serial Shell's
  process; otherwise restart WinUAE.

#### amiagent (optional)

[amiagent](https://aminet.net/search?query=amiagent) (Aminet, `comm/net`, Apache 2.0) is a small
server in the Amiga: commands, files, screen captures, mouse clicks and keys, ARexx. Its host half,
[amimcp](https://github.com/thomas-luebker/amimcp), is an MCP server (Python, standard library only) that lets an
AI assistant such as Claude Code drive the Amiga through it; it can also be used as a plain client.

- Start it with a secret token, and only on the test machine, for instance at the end of `S:User-Startup` when
  the scripts' volume is mounted:

  ```
  Assign >NIL: WT: EXISTS
  If NOT WARN
    Run >NIL: <path>/amiagent TOKEN=<secret> QUIET
  EndIf
  ```

  Keep the token out of the repository (amimcp reads it from its environment, `AMIGA_TOKEN`, with `AMIGA_HOST`).
- **Security**: with `bsdsocket_emu`, a program listening in the Amiga listens on the host's network interfaces
  (amiagent: port 7846 on all of them), and amiagent runs any command it is sent. Keep the host's firewall closed
  to WinUAE (refuse when Windows asks to allow it): the client is `127.0.0.1`.
- One command at a time: a command that does not return blocks the next ones until it is interrupted.
- Its Shell has a **256 KB stack**, which hides stack overflows: a stack test runs from a script that starts with
  `Stack 4096` (see [Checks the host tests cannot make](#checks-the-host-tests-cannot-make)).
- Clicks work on the Workbench, icons and GadTools gadgets; MUI gadgets need the mouse moved, then the button
  pressed and released. The screen is 640 x 256: pixels twice as high as wide (double the height of a capture
  meant for the documentation).

### The server side

Measure on the server through its command line interface, never on its web page (whose progress bar moves by
itself). With LMS, the CLI on TCP port 9090, one command per line:

- `players 0 50`: the players, their ids (MAC addresses) and whether they are connected;
- `<id> time ?`, `<id> mode ?`, `<id> status - 1`: elapsed time, play/pause/stop, the playlist index;
- `<id> playlist add /music/<file>`, `<id> play`, `<id> pause 1`, `<id> time 185`, `<id> stop`: the commands;
- `<id> mixer volume 100`: full volume (Paula, CPU load);
- `rescan`: after adding files to the music folder.

Stop the player and clear its playlist before starting amisqueeze: LMS resumes a playback left running by an
interrupted test.

**WinUAE's clock falls behind real time** (seen on the slow profile: about 28 s lost during a two-minute run;
where exactly is not known). LMS adds to the player's last elapsed time the delay since that status
message, measured with the player's clock, and follows a clock that falls behind only after about 50 messages.
So after a seek or a new start, LMS's absolute time is wrong for about 50 s (for instance the track's length 3 s
after a seek near its end), while intervals stay right. Compare intervals in WinUAE; judge absolute times on a
real machine or with the host build.

### CPU load

Count the CPU left over with `cpucount` ([`tools/amiga/cpucount.c`](../../tools/amiga/cpucount.c), built by
`make cpucount` into `out/`, so `NM:cpucount` in WinUAE; plain 68000 code, copy it to a real machine as it is): at
the lowest priority (-128), it counts loop rounds for a number of seconds of the Amiga's clock and prints the
count. Run it on its own, never in the foreground of a Shell driven from the host:

```
Run >NIL: NM:cpucount >NM:cpu-idle.log 10
```

Once with nothing playing and once while amisqueeze plays, in the same session and for the same time (10 s
windows, several of them); load = 1 - count while playing / count when idle (on the slow profile, about 8,300
rounds in 10 s when idle). Start the measuring window a few seconds after playback starts (the buffer fills
first), and send nothing to the Amiga during a window (the serial shell and amiagent cost CPU too). The results
so far: steps player-4c and player-9 of the [plan](phase-1-amisqueeze.md).

### Test sounds

`sh tools/make-test-audio.sh [DIR]` makes them (default `out/test-audio`), with `sox`, `flac`, `lame` and
`python3`; put them in the server's music folder and rescan. The tones are at half of full scale. Bring your own
music for the rest: the test sounds check the player, not the listening pleasure.

| File | What it is | What to check |
|---|---|---|
| `tone-440.flac` | 440 Hz, 10 s, 44.1 kHz, 16 bits | the pitch (an A, as a tuning fork), the speed (10 s), no crackle |
| `tone-440-48k.flac` | the same at 48 kHz | the same pitch: after `tone-440.flac`, a change of rate between tracks |
| `tone-440-24bit.flac`, `tone-440-mono.flac` | 24 bits; mono | the same tone (24 bits converted to 16; mono on both sides) |
| `tone-440-96k.flac` | 96 kHz, 24 bits | played only when amisqueeze decodes FLAC itself (the server lowers the rate); refused otherwise |
| `tone-440.wav`, `tone-440-24bit.wav`, `tone-440-8bit.wav`, `tone-440.aiff` | the tone in WAV (16, 24, 8 bits) and AIFF | the same tone: the server strips the headers and sends PCM |
| `tone-left.flac`, `tone-right.flac` | 440 Hz on one side only, 5 s each | the sides in place (`SWAP` with Paula in WinUAE), nothing on the other side |
| `sweep.flac` | 20 Hz to 20 kHz in 20 s | a clean rise, without crackles; above half of the AHI unit's rate (about 13.7 kHz at 27,429 Hz) Paula cannot play it, and what is heard there depends on AHI's conversion |
| `test-20s.mp3` | 440 Hz left, 660 Hz right, 20 s, MP3 | MP3 decoding (`amisqueeze.fpu`, or the server), the sides |
| `gapless-1.mp3` to `gapless-3.mp3` | a 12 s glide from 220 to 880 Hz cut in three MP3s | in one playlist, a continuous glide: no click or gap at the two boundaries (gapless MP3) |
| `test-pattern.flac` | the counter pattern of `tools/pcm-compare.py`, 20 s | not to listen to: the `OUTPUT` file compared byte by byte (`pcm-compare.py --counter`, see [Checks the host tests cannot make](#checks-the-host-tests-cannot-make)) |

### Automated scenarios

[winuae-scenarios.md](winuae-scenarios.md): `tools/winuae/` runs tests from the host (starting WinUAE if needed)
and prints `OK` or the reason of the failure, with an exit code: a test tone, LMS's commands through AHI and
`OUTPUT` comparisons (PCM, FLAC and MP3 decoded on the Amiga), all of them with
`python3 tools/winuae/run-scenarios.py`, in the JIT profile with a 68882. Its serial shell client also runs one
command: `python3 tools/winuae/amiga_shell.py "Status"`.

## Checks the host tests cannot make

- **Stack**: AmigaOS does not detect a stack overflow (silent corruption), and a Shell with a large stack hides
  one: from 0.2 to 0.4, `amisqueeze` overflowed the default 4 KB at every start, unseen by tests run from a Shell
  with 256 KB. To measure what is used, add a temporary probe in `main()` (fill the stack from `tc_SPLower` up to
  below the current pointer with a pattern, find at the end how much of it was overwritten) and run the program
  from a script that starts with `Stack 4096`. `-fstack-usage` gives the size of each function's frame.
- **Speed**: without its JIT, WinUAE at its fastest is about a 68020 at 45 MHz, a slow Amiga (FLAC level 8 decodes
  at 0.2x real time). For a fast one such as a PiStorm, enable the JIT (68020, 32-bit addressing, not cycle-exact)
  and, for `amisqueeze.fpu`, `compfpu=true`: without it the JIT interprets the FPU instructions (MP3 at 1.8x real
  time instead of 74x). A 68040 needs its CPU library in the emulated system: without one, emulate a 68020 with
  the JIT.
- **Memory**: `Avail` before and after a few play and stop cycles; watch its `Largest` column too, where a
  corrupted free memory list shows first.
- **Sound**: Paula through AHI (`paula.audio`) is the main test, an AHI sound card (Toccata in WinUAE) the 16-bit
  reference. With Paula in WinUAE, use `SWAP` (left and right come out swapped). Judge Paula's sound at volume 100
  on the server: a lower volume scales the samples down and brings out Paula's 14-bit noise (a crackle at 50).
- **On the server**: measure through its CLI (`<player> time ?`, `<player> mode ?`, the playlist index), never on
  its web page, whose progress bar moves by itself.
- **`OUTPUT=<file>`** writes the audio to a file as it is played, without sound (as with `NOAUDIO`). The file is
  created, or replaced, at the start and closed at the end; everything played follows, track after track, nothing
  while paused. Raw PCM is written as the server sent it (from LMS, 16-bit little-endian; 44.1 kHz stereo is about
  10 MB a minute); FLAC and MP3 decoded on the Amiga as 16-bit big-endian. In an emulator, write it to a volume
  shared with the host; never put it in the default options, every run would fill the disk.
- **`OUTPUT` comparisons** (`tools/pcm-compare.py`, byte by byte): start playback with a single `play`, and compare
  only once the output has drained (`output: end of playback` with `VERBOSE`) and amisqueeze has stopped. Good
  references: a counter pattern in FLAC (decoded losslessly by LMS), MP3s decoded by the server's own `sox`. Make
  test MP3s without an ID3v1 tag (`lame --id3v2-only`): LMS strips it before converting, and `sox` then drops the
  last frame.
