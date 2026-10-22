# Testing in WinUAE

How amisqueeze is tested in an emulated Amiga: the machine, its profiles, AHI, how to drive it from the host, the
server side, the CPU load, and the test sounds. The host build and its tests (`make test`) are in
[CONTRIBUTING.md](../../CONTRIBUTING.md#host-build-and-tests), with the checks only the Amiga can make
([Checks the host tests cannot make](../../CONTRIBUTING.md#checks-the-host-tests-cannot-make)). The plan's steps
behind this setup: 0.4, 0.5 and 0.7 of [phase-0-tooling.md](phase-0-tooling.md).

## What you need

- **WinUAE** 6.0 (Windows).
- **A Kickstart ROM and AmigaOS**, 3.1 or later (tested with 3.2): both are licensed, never in the repository.
  AmigaOS installed on a hard file (`.hdf`).
- **AHI 6 and MUI** in that system, from Aminet, as the [user guide](../user-guide.md#installing-ahi) describes;
  WinUAE's Toccata card, the 16-bit reference, also needs `toccata.library` (Aminet).
- **A server** reachable from the host: Lyrion Music Server or Music Assistant, set up as the
  [user guide](../user-guide.md) says, with the [test sounds](#test-sounds) in its music folder.

## The test machine

[`winuae/amisqueeze-test.uae`](../../winuae/amisqueeze-test.uae) is a template: copy it outside the repository,
put your paths in place of its `<...>`, and start it with `winuae64.exe -f <your copy>`. The settings that matter:

| Setting | Why |
|---|---|
| A1200, AGA, 68020, 2 MB chip, 8 MB fast | the classic target; Zorro II fast RAM (a 16-bit bus, slower than an accelerator's own) |
| `bsdsocket_emu=true` | WinUAE provides `bsdsocket.library` through the host's network: no TCP/IP stack in the Amiga, and a server on the host is `127.0.0.1` |
| `filesystem2=rw,NM:...:<repository>/out` | the programs under test as `NM:`, as soon as `make` has written them |
| `filesystem2=ro,WT:...` | your test scripts as `WT:` (optional) |
| `sound_stereo_separation=10` | full separation (the default 7 mixes the sides: left would be heard on the right) |
| `toccata=true` | an emulated 16-bit sound card, the reference for Paula |
| `win32.serial_port=TCP://127.0.0.1:1234` | the [serial shell](#serial-shell); the `win32.` prefix is required (otherwise "unknown config entry" in `winuaebootlog.txt`) |
| `win32.iconified_pause=false`, `win32.inactive_pause=false` | long unattended runs: a paused emulation once stopped an hour of playback after 5 minutes |

**Never start two configurations on the same hard file at once**: it would be corrupted.

## Profiles

The template is the **default** profile: a 68020 as fast as the host allows (about 45 MHz). The others override
the CPU lines on the command line (`-s name=value`):

| Profile | Overrides | For |
|---|---|---|
| **slow** | `-s cpu_compatible=true -s cpu_speed=real -s cpu_multiplier=4` | a 68020 at 4 x 7.09 = 28.4 MHz, as a typical A1200 accelerator: CPU load, Paula through AHI |
| **JIT** | `-s cpu_24bit_addressing=false -s cachesize=16384 -s cycle_exact=false -s cpu_cycle_exact=false -s cpu_memory_cycle_exact=false -s cpu_speed=max -s cpu_compatible=false` | about 230 times the slow profile, the order of a PiStorm: decoding on the Amiga (FLAC, MP3) |
| **+ FPU** | `-s fpu_model=68882`, with the JIT also `-s compfpu=true` | `amisqueeze.fpu`; without `compfpu` the JIT interprets the FPU (MP3 at 1.8x real time instead of 74x) |

Not a 68040: it needs its CPU library (`68040.library`) in the system. Speeds measured in WinUAE are not those
of a real machine: the plan leaves timings to the real machines (step 1.9).

## AHI

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

## Driving the Amiga from the host

### Serial shell

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

### amiagent (optional)

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
  `Stack 4096` (see [Checks the host tests cannot make](../../CONTRIBUTING.md#checks-the-host-tests-cannot-make)).
- Clicks work on the Workbench, icons and GadTools gadgets; MUI gadgets need the mouse moved, then the button
  pressed and released. The screen is 640 x 256: pixels twice as high as wide (double the height of a capture
  meant for the documentation).

## The server side

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

## CPU load

Count the CPU left over: a small program at the lowest priority (-128) counts loop rounds for a fixed time on the
Amiga's clock (`DateStamp()`) and prints the count. Run it on its own (`Run`), once with nothing playing and once
while amisqueeze plays, in the same session and for the same time (10 s windows, several of them); load = 1 -
count while playing / count when idle. Start the measuring window a few seconds after playback starts (the
buffer fills first), and send nothing to the Amiga during a window (the serial shell and amiagent cost CPU too).
Such a counter is not in the repository yet. The results so far: step 1.4c and 1.9 of the
[plan](phase-1-amisqueeze.md).

## Test sounds

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
| `test-pattern.flac` | the counter pattern of `tools/pcm-compare.py`, 20 s | not to listen to: the `OUTPUT` file compared byte by byte (`pcm-compare.py --counter`, see [CONTRIBUTING.md](../../CONTRIBUTING.md#checks-the-host-tests-cannot-make)) |

## Not in the repository yet

The host scripts that drive the tests (the serial shell client, restarting WinUAE in a profile, the scenarios of
the plan's steps through LMS's CLI, the CPU counter) exist only in a local setup: step 0.7 of
[phase-0-tooling.md](phase-0-tooling.md) plans a generic version.
