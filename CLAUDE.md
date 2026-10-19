# CLAUDE.md — amiga-network-music

Personal project: network music for classic Amiga (AmigaOS 3.x), in C, tested in WinUAE. Day-to-day usage
(layout, building, tasks) is in `README.md`; this file keeps context, decisions and pitfalls. Public GitHub
repository `shadow38/amiga-network-music`: **nothing specific to the developer's workstation goes here** (local
paths, corporate proxy, personal settings, neighbouring folders); those live in local files outside the
repository. Documentation, code comments and program messages are in English.

**Versioning** (decided 2026-10-18, completed 2026-10-19): 0.x until the main feature works; the version changes
only for a **published** release; 1.0 = first really usable version (tested on the real machine). Never bump it on
our own: ask the user. A release = a Git tag `amisqueeze-<version>` + a GitHub Release with a zip (the Amiga
executable, `LICENSE`, `README.md`, the user guide) and short notes, made with the GitHub CLI once the user has
approved the notes; the `$VER:` date is the release date. Between releases the number reads `<next>-dev`, so that
a build in between is not taken for the last release. 0.1 = first release (2026-10-19: AHI audio, volume,
robustness, tested in WinUAE only); 0.2 = second release (2026-10-19: FLAC decoded on fast Amigas, 8/24/32-bit
PCM, tested in WinUAE only), followed by 0.3-dev; the first public push of 2026-10-18 had no tag. The number appears in the
`$VER:` string (`amiga.c`), in `Firmware=` of the `HELO` capabilities (`slimproto.c`) and in the user guide.

**Licence: MIT** (`LICENSE`, "Copyright (c) 2026 shadow38"), decided 2026-10-18. squeezelite and LMS are GPL:
read them to **understand** the protocol and its expected behaviour (message layouts, when to send which state),
then write our own code; **never copy blocks of their code**. Third-party libraries must have an MIT-compatible
licence (public domain, MIT, BSD, zlib…; e.g. dr_flac, minimp3), not GPL (no libmad).

## Project: amisqueeze and amilms

Targets (2026-10-18): classic Amigas, 68020 or better, of two kinds: **fast** (PiStorm with Emu68: a 68040 with
FPU) and **slow** (a 68020 accelerator without FPU); sound through **AHI**: Paula (`paula.audio`, Amigas without a
sound card) or an AHI driver (sound card, or the PiStorm's own outputs: I2S DAC of the PiStorm32-lite FPGA
firmware, HDMI with a Pi 4). Code only `ahi.device` features of AHI 4 (tested with AHI 6.6; 4.x expected to
work, not checked). The WinUAE test configurations reproduce both kinds (step 0.4); Paula is the main audio test,
Toccata a 16-bit reference.

Decided (2026-10-18): two programs in this repository, one `src/<prog>/` directory each, shared code in
`src/common/`:

- **amisqueeze**: slimproto player with AHI output, for LMS (Music Assistant speaks slimproto too; decided
  2026-10-19: Music Assistant is tested only after version 0.2; 0.2 is out, when to start it is still to be
  decided, see the plan);
- **amilms** (working name; **amiremote** proposed, to be decided by study step 2.0): remote control, one
  portable core with two server back ends behind a common interface, LMS text CLI (TCP 9090) first, then the
  Music Assistant API (JSON over WebSocket, token), an MPD back end noted as an idea (step 2.12); a Shell front end first, then a GUI front end calling the same
  core in the same program (not a GUI driving a CLI program). Decided 2026-10-19: study 2.0 (common interface, MA
  feasibility, name) before any amilms code. Track search is one of its main goals (step 2.4). For Music
  Assistant, the existing Home Assistant client ami2ha already offers basic control (play/pause, next, volume, no
  search): noted in the phase 2 plan.

Work proceeds in testable steps, each validated with the user **before** writing code: the plan is in
`docs/development/` (keep the statuses up to date). User-facing behaviour (options, messages) is documented in
`docs/user-guide.md`: update it with every change visible to users. Steps 1.1b (default options in
`ENV:amisqueeze.prefs`), 1.2 (stream received, virtual output) and 1.3 (`OUTPUT` file) validated on 2026-10-18.
Step 1.4: 1.4a and 1.4b done (AHI output); 1.4c listening done on 2026-10-19 (test tones, a whole 48 kHz track as
MP3 and FLAC, Paula needs `SWAP` in WinUAE); rest of 1.4c measured the same day (2 KB stack OK, no leak, CPU
load on the slow 68020 profile: 61-68 % with Paula, almost all of it AHI; 2-5 % with `NOAUDIO`). Step 1.4
validated on 2026-10-19. Step 1.5 (volume from `audg`) validated on 2026-10-19. Step 1.6 (robustness) on
2026-10-19: reconnection storm fixed (wait 5 s after a session shorter than 10 s), AHI memory corruption avoided
(volume applied to the samples, see Pitfalls); validated on 2026-10-19. Release 0.1 published on 2026-10-19.
Step 1.7a (8, 24 and 32-bit samples converted to 16 bits for AHI; LMS strips WAV/AIFF headers, files
above 48 kHz not playable until FLAC decoding) done on 2026-10-19. Step 1.8 on the branch `decoding` (merged into
`main` after each validated sub-step): 1.8a done (decoder stage, see below); 1.8b done (FLAC with dr_flac,
`FLAC=AUTO|ON|OFF`, `BENCH`); release 0.2 published on 2026-10-19, then 0.3-dev; 1.8c (MP3: `amisqueeze.fpu`
with minimp3) in progress, then the real machine (1.9).

Step 1.8a (decoder.h, decoder.c): the ring buffer holds the stream as received; the decoder of each track writes
PCM into the AHI blocks or for the virtual output. Two kinds of position, never to be mixed: the ring's (bytes of
the stream: where each track's data begins, `track.start`) and the output's (bytes of decoded PCM: `out.handed`,
`track.out_start`, everything played, elapsed time, boundaries). Raw PCM cannot reveal a mix-up (same size on both
sides): the test-only "widening" decoder in `tests/test_core.c` can.

Step 1.8b (flac.c, dr_flac.c, bench.c): dr_flac takes a short read for the end of the stream and reads ahead (4 KB
cache), so a frame is decoded only when the buffer holds a whole one, and a track ends for the output only when
the decoder says `finished`. Third-party code goes in `third_party/` (version, commit, licence in a README), built
in its own file without our warnings (`THIRD_PARTY` in the Makefile). A file that includes a third-party header and
reads its structures must use the same configuration macros as the file that builds it. FLAC speed in WinUAE:
without JIT even "max" speed is a 68020 at about 45 MHz (level 8 at 0.2x real time); a PiStorm-like test needs the
JIT (68020, 32-bit addressing, no cycle-exact; a 68040 needs its CPU library, missing from the test HDF).

Step 1.2 (stream.c, ring.c): HTTP stream with a non-blocking connection; one 1 MB ring buffer, reported to LMS
as both the stream and the output buffer (raw PCM needs no decoding); a virtual output consumes it at the PCM rate
(the point where the file output, 1.3, and AHI, 1.4, plug in). States as in squeezelite: STMf, STMc, RESP, STMs,
STMt every second while playing, DSCO + STMd at the end of the data, the next track appended (gapless) with STMs at
the boundary, STMo / STMu when the buffer is empty, STMl for autostart 0, pause / unpause. STMt is not printed with
VERBOSE (one per second). Criteria are measured on the server through its CLI (`<player> time ?`, mode, playlist
index), never on the web page, whose progress bar animates by itself.

Step 1.3: `OUTPUT=<file>` writes what the virtual output consumes (`consume()` in `stream.c`, the only place
where data leaves the buffer), as received, one file per run. Byte-exact checks with `tools/pcm-compare.py`:
a counter pattern in FLAC (decoded losslessly by LMS) and MP3s decoded by the server's own `sox` command. Start
playback with a single `play`, and compare only after the output has drained (`output: end of playback`) and
amisqueeze has stopped. With AHI, `OUTPUT` keeps the virtual output (no sound). LMS strips an MP3's ID3v1 tag
before conversion, and `sox` then drops the last frame: test MP3s without ID3v1 (`lame --id3v2-only`).

Step 1.4 (AHI): the output hands bytes to a sink, the virtual output (host build, `NOAUDIO`, `OUTPUT`) or the
audio output (`audio.c`, portable: blocks, conversion, played bytes, pause; `ahi.device` calls in `amiga.c`).
`ahi.device` rules learnt from its source (AHI 6.0), to keep: chain a request (`ahir_Link`) only to one already
**playing** (linked to a waiting one, it never plays nor ends), so at most two requests with the device; abort the
**playing** one first (it takes the linked one along; the reverse order leaves a stale reference that hangs the
device); `CMD_STOP` does not pause what is playing (pause = abort, then send the rest again); a non-real-time mode
("Filesave") on a device unit writes only silence. The test stub enforces the first two rules (`stub_audio_bad`).
**Never send `ahir_Volume` below full** (found in step 1.6): with AHI 6.6 and `paula.audio`, requests sent again
after an abort (a resume) with a low volume corrupt the free memory list, and the system hangs or amisqueeze dies
later; the core scales the samples instead (two 256-entry tables, no multiplication per sample). Check memory with
the `Largest` column of `Avail`: a jump there is the first sign of a corruption.

amisqueeze state: control connection (step 1.1) validated on 2026-10-18 in WinUAE (`bsdsocket_emu`,
`SERVER=127.0.0.1`) with LMS 9.1.1: the player shows up in LMS and reconnects by itself after a server restart.
`HELO` advertises `Model=squeezelite` (so LMS recognises it) and `ModelName=AmiSqueeze`. Observed `strm s` for an
MP3 transcoded by LMS: format `p`, size `1` (16 bits), rate `3` (44.1 kHz), channels `2`, endianness `1`
(**little-endian**: to be swapped for AHI on 68k), autostart `1`, stream address = the control connection's
address, request `GET /stream.mp3?player=<mac> HTTP/1.0`. LMS 9.1 has no default `mp3 pcm` rule: a test server
needs one (documented in the README and in plan step 0.2); a regular user's server won't have it, hence local MP3
decoding later (step 1.8).

## Design choices (player)

- **Portable core + platform layer** (step 0.6, 2026-10-18): `player.c`, `slimproto.c`, `player.h` are standard
  C (`<stdint.h>`, no Amiga or POSIX header); everything system-related goes through `platform.h`, implemented by
  `amiga.c` (Amiga build only) and `posix.c` (host build only). Keep the core free of system calls; keep the
  platform layers thin. The main loop (timeouts, reconnection) lives in the core.
- One task and one loop around `plat_wait()` (`WaitSelect()` on the Amiga: a `SocketBase` belongs to the task
  that opened it; `select()` on POSIX, SIGINT/SIGTERM without `SA_RESTART` = Ctrl-C).
- Addresses and ports in **host byte order** in the core; `htonl`/`ntohl` only in the platform layers (no-ops on
  68k, essential on x86).
- Fields serialised byte by byte, no packed structures; 32-bit values are `uint32_t` (the MAC hash too, so both
  builds derive the same MAC).
- Advertise in `HELO` only the formats actually decoded.
- Framing: server → client = 2-byte length + opcode; client → server = opcode + 4-byte length.
- Output goes through `msg()` with the **standard printf format** (`format(printf)` attribute, `-Werror=format`
  on both builds; on the Amiga: libnix `vsnprintf` into a static buffer, then `Write()` to `Output()`, unbuffered,
  so a log redirected to a file is readable while the program runs); raw bytes through `msg_raw()`. Never
  `Printf`/`PutStr`/`printf` directly. Cast 32-bit values to the type the specifier names (`(unsigned long)` with
  `%lX`); `%c` for chars (not `%lc`).
- Music Assistant: per-player output codecs `flac` (default), `mp3`, `aac`, `wav` (seen in `constants.py`,
  `CONF_ENTRY_OUTPUT_CODEC`); not verified yet: the solo playback path (`play_media`) and the `strm` format sent for
  `wav` (explicit values, or `?` plus header parsing).

## Toolchain

- **Toolchain**: `m68k-amigaos-gcc` 16.2 (AmigaPorts), NDK 3.2 (v47 headers, including bsdsocket/Roadshow and
  AHI), libnix, `make`, `python3`; reference image `amigadev/crosstools:m68k-amigaos-gcc10` (historical tag that
  follows GCC 16; prefix `/opt/m68k-amigaos`, with vasm, gdb, vamos). The repository stays **generic**: how the
  toolchain is installed on a given machine is not described here.
- **Building**: `sh tools/amiga-make.sh [all|clean|sdk|host|test|<prog>]`, which tries in order `$AMIGA_MAKE`,
  the toolchain in the `PATH`, `../amiga-make.local.sh` (outside the repository), then Docker (untested). On
  Windows: through WSL (VS Code tasks: `wsl.exe --cd ${workspaceFolder} --exec sh tools/amiga-make.sh …`, default
  distribution).
- **Host build and tests**: `make host` (core + `posix.c`, `$(HOSTCC)`, default `cc`, with ASan + UBSan) and
  `make test` (`tests/test_core.c` on `tests/stub_platform.c`, then `tests/fake_server.py` against
  `out-host/amisqueeze`; needs a native compiler and python3, no LMS). Run `make test` after every change to the
  core; extend the tests with each step (1.2: stream scenarios in the fake server). The Amiga build is still
  checked in WinUAE (different platform layer).
- **VS Code**: open each repository on its own (task names are the same everywhere, shortcuts can target them by
  name); extensions `prb28.amiga-assembly`, `ms-vscode.cpptools` and `spencerwmiles.vscode-task-buttons`.
- **WinUAE** 6.0; Kickstart ROMs are not versioned (licensed). No test configuration in the repository yet (plan
  steps 0.4 to 0.7).

## Pitfalls

- **gcc 16.1.1b (wischner 1.1.0) generated wrong code**: a register picked by reload (e.g. `d2`) was used without
  being saved in the prologue → corruption in the caller, *address error* `#80000003` in WinUAE. No option avoided
  it (-O1/-Os/-O2, -mlra/-mno-lra, -fbbb=-…); fixed in 16.2-rc16. Safeguard: the Makefile goes through `-S` and
  `tools/check-saves.py` (fails the build). The GCC 16 port is at RC stage: be wary, check the assembly.
- **vamos** only runs pure dos.library tools: it crashes on `printf` (libnix), no windows, no real bsdsocket (its
  stub makes amisqueeze retry forever: kill it). Use `vamos -C 68020` for 68020 builds (otherwise alert
  `00068020`). Its `GetVar` ignores global variables (`ENV:`) and returns 0. Useful for argument parsing only;
  test the rest in WinUAE.
- **Stack**: the default Shell stack is 4 KB and AmigaOS does not detect overflows (silent corruption). Big buffers
  are static or allocated, never on the stack. Margin check: `Stack 2048` in the Shell, then replay the WinUAE
  scenario; passed on 2026-10-18 after the switch to `vsnprintf` (step 0.6). Redo it whenever call depth or
  local variables grow (step 1.2).
- Strings shown on the Amiga and files read by the Amiga: ASCII only (ISO-8859-1 on the Amiga), LF line endings.
- WinUAE syntax checked in `cfgfile.cpp`: `filesystem2=rw,DEVICE:Volume:path,bootpri`; `chipmem_size` in 512 KB
  units, `fastmem_size` in MB; networking emulation `bsdsocket_emu=true`.

## Source-level debugging (step 0.8, optional)

Chosen approach: `bgdbserver` (bebbo, Aminet `dev/debug/bgdbserver`) in the emulated Amiga + WinUAE's
`bsdsocket.library` emulation + `m68k-amigaos-gdb` (`target remote …:2345`) + a cpptools `launch.json`; build with
`-g`. To validate first: reaching WinUAE (port 2345) from wherever gdb runs.
Ruled out: uae-dap/prb28 (requires SAS/C LINE DEBUG hunks), Bartman's amiga-debug (its own toolchain, NDK 3.9, no
libc). WinUAE's built-in debugger (Shift+F12) remains available at the assembly level.
