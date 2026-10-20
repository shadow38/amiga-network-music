# CLAUDE.md — amiga-network-music

Personal project: network music for classic Amiga (AmigaOS 3.x), in C, tested in WinUAE. The `README.md` is for
users (what it is, quick start; decided 2026-10-19: keep it short); building, tests, layout, tasks and how to
propose a change are in `CONTRIBUTING.md` at the root (moved there from `docs/development/building.md` on
2026-10-19, the GitHub convention); `docs/` holds the user guide and its images, `docs/development/` the plan; the
server setup is in the user guide; this file keeps context, decisions and pitfalls. Public GitHub repository
`shadow38/amiga-network-music`: **nothing specific to the developer's workstation goes here** (local
paths, corporate proxy, personal settings, neighbouring folders); those live in local files outside the
repository. Documentation, code comments and program messages are in English.

**Versioning** (decided 2026-10-18, completed 2026-10-19): 0.x until the main feature works; the version changes
only for a **published** release; 1.0 = first really usable version (tested on the real machine). Never bump it on
our own: ask the user. A release = a Git tag `amisqueeze-<version>` + a GitHub Release with a zip (the Amiga
executables and their `.info` icons, `LICENSE`, `README.md`, `CHANGELOG.md`, the user guide and its images) and
short notes, made with the GitHub CLI once the user has approved the notes; the zip's SHA-256 goes in the notes and
in a `<zip>.sha256` file attached to the release (decided 2026-10-21; by hand until `make dist` writes it, plan 0.7). **Before every release tag** (plan step
3.5, decided 2026-10-20; two levels decided the same day, for cost): a light review of the range since the previous
tag, `/release-review <previous tag>` (three agents, limited to the diff and what it reaches); a full audit,
`/release-review <previous tag> full` (four agents, the whole repository), only at milestones: before 1.0, or after
a big change of architecture (Music Assistant, splitting `stream.c`). Findings scored 75 or more are fixed, or
accepted by the user with the reason in the report (an urgent fix may ship after a review of its own diff); the
review tooling changes only when a review misses a real defect, or when the user decides it. Remarks that come
after a version's review (outside readings, new ideas) go to the next version by default; only a real defect, or
the user's choice, brings one into the current version (decided 2026-10-21, against over-quality). `CHANGELOG.md` (Keep a
Changelog, added 2026-10-20): note each user-visible change under "Unreleased" as it is made, and give that part
the version and date at the release; the `$VER:` date is the release date. Between releases the number reads
`<next>-dev`, so that a build in between is not taken for the last release. 0.1 = first release (2026-10-19: AHI
audio, volume, robustness, tested in WinUAE only); 0.2 = second release (2026-10-19: FLAC decoded on fast Amigas,
8/24/32-bit PCM, tested in WinUAE only); 0.3 = third release (2026-10-20: MP3 decoded by `amisqueeze.fpu`, two
executables, documentation reworked, tested in WinUAE only); 0.4 = fourth release (2026-10-20: MP3 without gaps,
fixed volume through LMS, AHI mode warning); 0.5 = fifth release (2026-10-20: `LOG`, Workbench start, commodity,
stack of `amisqueeze` and FLAC seek fixed); 0.6 = sixth release (2026-10-21: the review fixes, troubleshooting,
known limits and what has been tried in the user guide, ASCII documents in the zip, the zip's SHA-256 with the
release; the first light review), followed by 0.7-dev; the first public push of 2026-10-18 had no tag.
The number appears in the two `$VER:` strings (`amiga.c`: `amisqueeze` and `amisqueeze.fpu`), in `Firmware=` of the
`HELO` capabilities (`slimproto.c`) and in the user guide.

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
`src/common/` (to create with amilms; none yet):

- **amisqueeze**: slimproto player with AHI output, for LMS (Music Assistant speaks slimproto too; decided
  2026-10-19: Music Assistant is tested only after version 0.2; first tried on 2026-10-21, plan step 1.7b: FLAC
  works, `wav` does not yet);
- **amilms** (working name; **amiremote** proposed, to be decided by study step 2.0): remote control, one
  portable core with two server back ends behind a common interface, LMS text CLI (TCP 9090) first, then the
  Music Assistant API (JSON over WebSocket, token), an MPD back end noted as an idea (step 2.12); a Shell front
  end first, then a GUI front end calling the same core in the same program (not a GUI driving a CLI program). Decided 2026-10-19: study 2.0 (common interface, MA
  feasibility, name) before any amilms code. Track search is one of its main goals (step 2.4). For Music
  Assistant, the existing Home Assistant client ami2ha already offers basic control (play/pause, next, volume, no
  search): noted in the phase 2 plan.

Work proceeds in testable steps, each validated with the user **before** writing code: the plan is in
`docs/development/` (keep the statuses up to date). User-facing behaviour (options, messages) is documented in
`docs/user-guide.md`: update it with every change visible to users. Steps 1.1b (default options in
`ENV:amisqueeze.prefs`), 1.2 (stream received, virtual output) and 1.3 (`OUTPUT` file) validated on 2026-10-18.
Step 1.4: 1.4a and 1.4b done (AHI output); 1.4c listening done on 2026-10-19 (test tones, a whole 48 kHz track as
MP3 and FLAC, Paula needs `SWAP` in WinUAE); rest of 1.4c measured the same day (2 KB stack OK then, before the
decoders: see Stack in Pitfalls; no leak, CPU load on the slow 68020 profile: 61-68 % with Paula, almost all of it
AHI; 2-5 % with `NOAUDIO`). Step 1.4 validated on 2026-10-19. Step 1.5 (volume from `audg`) validated on
2026-10-19. Step 1.6 (robustness) on 2026-10-19: reconnection storm fixed (wait 5 s after a session shorter than 10
s), AHI memory corruption avoided (volume applied to the samples, see Pitfalls); validated on 2026-10-19. Release
0.1 published on 2026-10-19. Step 1.7a (8, 24 and 32-bit samples converted to 16 bits for AHI; LMS strips WAV/AIFF
headers, files above 48 kHz not playable until FLAC decoding) done on 2026-10-19. Step 1.8 on the branch `decoding`
(merged into `main` after each validated sub-step): 1.8a done (decoder stage, see below); 1.8b done (FLAC with
dr_flac, `FLAC=AUTO|ON|OFF`, `BENCH`); release 0.2 published on 2026-10-19, then 0.3-dev; 1.8c done on 2026-10-19
(MP3 with minimp3 in `amisqueeze.fpu`, `MP3=AUTO|ON|OFF`); release 0.3 published on 2026-10-20. Release 0.4 the
same day (1.8d MP3 without gaps, the volume modes done as LMS's fixed volume setting with `HasDigitalOut=1`, the
AHI mode warning). Release 0.5 the same day (`LOG`, Workbench start with icons and ToolTypes, a commodity, the
stack of `amisqueeze` and FLAC seek fixed, the outside review's points: see "Outside review" in
`docs/development/README.md`), then 0.6-dev. Next releases (decided 2026-10-20, table in
`docs/development/README.md`; changed the same day, and again on 2026-10-21): 0.6 = the fixes from the review
reports; 0.7 = Music Assistant (1.7b: the WAV header, the other codecs, a user guide part), with the findings
moved from the 0.6 review, the outside reading's remarks and `architecture.md`; 0.8 = the real machine (1.9,
tried with 0.7, LMS and Music Assistant) and what it shows; 0.9 = internet radios' metadata (ICY), server
discovery. Then the remote's study (2.0).
Documentation review of 2026-10-19: points announced in the plan but neither done nor dropped are listed under
"Open points" in `docs/development/README.md`, for the user to decide (never implement them unasked).

Step 1.8c (mp3.c, minimp3.c, `amisqueeze.fpu`): two Amiga executables from the same sources, `amisqueeze`
(68020) and `amisqueeze.fpu` (`-m68881`, `AMISQUEEZE_FPU`; `AMISQUEEZE_MP3` compiles the MP3 decoder, also in the
host build). minimp3 resets itself when the bytes after a frame are not the next header: it is called only with a
frame and the next header in its 16 KB staging buffer (a full buffer for the first frame), or at the end of the
track. MP3 gapless (step 1.8d, 2026-10-20): the LAME header of the first frame gives the delays and padding to
drop (`mp3_lame_header`); after a seek LMS resends that frame first, so the end limit is never reached. In the
zip, both executables; in `$VER:`, `amisqueeze` or `amisqueeze.fpu` (two lines in `amiga.c` to bump).

Step 1.8a (decoder.h, decoder.c): the ring buffer holds the stream as received; the decoder of each track writes
PCM into the AHI blocks or for the virtual output. Two kinds of position, never to be mixed: the ring's (bytes of
the stream: where each track's data begins, `track.start`) and the output's (bytes of decoded PCM: `out.handed`,
`track.out_start`, everything played, elapsed time, boundaries). Raw PCM cannot reveal a mix-up (same size on both
sides): the test-only "widening" decoder in `tests/test_core.c` can.

Step 1.8b (flac.c, dr_flac.c, bench.c): dr_flac takes a short read for the end of the stream and reads ahead (4 KB
cache), so a frame is decoded only when the buffer holds a whole one, and a track ends for the output only when
the decoder says `finished`. After a seek or a resume, LMS sends a FLAC file from a frame boundary without its header
(fixed 2026-10-20: a STREAMINFO is made up from the first frame's header). Third-party code goes in `third_party/` (version, commit, licence in a README), built
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

Step 1.3: `OUTPUT=<file>` writes what the virtual output plays (`hand_over()` in `stream.c`, the only place
where the output hands PCM over; raw PCM as received, the rest as decoded), one file per run. Byte-exact checks
with `tools/pcm-compare.py`: a counter pattern in FLAC (decoded losslessly by LMS) and MP3s decoded by the server's
own `sox` command. Start playback with a single `play`, and compare only after the output has drained (`output: end
of playback`) and amisqueeze has stopped. With AHI, `OUTPUT` keeps the virtual output (no sound). LMS strips an
MP3's ID3v1 tag before conversion, and `sox` then drops the last frame: test MP3s without ID3v1 (`lame
--id3v2-only`).

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
address, request `GET /stream.mp3?player=<mac> HTTP/1.0`. LMS 9.1 has no default `mp3 pcm` rule: a regular
user's server won't have it, hence local MP3 decoding (step 1.8c, `amisqueeze.fpu`); for the builds that leave MP3
to the server, the rule is documented in the user guide (server setup) and in plan step 0.2 (without `-r`, with
`trim` for seeking).
With `flc` and `mp3` advertised, LMS sends FLAC and MP3 files as they are (format `f`, `m`), WAV as PCM.

## Design choices (player)

- **Portable core + platform layer** (step 0.6, 2026-10-18): every file of `src/amisqueeze/` but `amiga.c` and
  `posix.c` (player, slimproto, stream, ring, audio, decoder, flac, mp3, bench) is standard C (`<stdint.h>`, no
  Amiga or POSIX header); everything system-related goes through `platform.h`, implemented by `amiga.c` (Amiga
  build only) and `posix.c` (host build only). Keep the core free of system calls; keep the
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
  `Printf`/`PutStr`/`printf` directly, except inside the platform layers' own implementation of `msg()`,
  `msg_raw()` and the `LOG` file (decided 2026-10-20, review of 0.5). Cast 32-bit values to the type the
  specifier names (`(unsigned long)` with `%lX`); `%c` for chars (not `%lc`).
- Music Assistant: per-player output codecs `flac` (default), `mp3`, `aac`, `wav` (seen in `constants.py`,
  `CONF_ENTRY_OUTPUT_CODEC`); everything transcoded to that codec. Observed with MA 2.10.7 (2026-10-21, step
  1.7b): `strm s` without parameters, autostart 2/3, then `codc` and `cont` after the response headers (ignored
  by amisqueeze; fine with FLAC); `wav` = a WAV with its header, 24 bits while `strm` and `codc` say 16 bits: to
  play it, read the WAV header (not done, when to decide).

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
  checked in WinUAE (different platform layer). GitHub Actions runs `make test` on each push
  (`.github/workflows/tests.yml`, Ubuntu 24.04, decided 2026-10-19): after a push, check the run (`gh run list`,
  `gh run watch`); the tests use real time (playback pacing, reconnection delays), so a slow runner could make one
  fail: rerun before suspecting the code, and say so.
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
  are static or allocated, never on the stack. Both executables set `__stack` (16 KB `amisqueeze`, 32 KB
  `amisqueeze.fpu`) and link libnix's `__stkinit`, which gives that stack when the Shell's is smaller: dr_flac
  needs about 5.5 KB (4.6 KB in `drflac_open` alone), minimp3 about 17.5 KB. **Found by an outside review on
  2026-10-20**: until then `amisqueeze` (68020) ran on the Shell's 4 KB and overflowed at every start (FLAC speed
  measure) since 0.2; the WinUAE tests never saw it because they ran from a remote-control Shell with a 256 KB
  stack. Check: a temporary probe in `main()` (fill the stack below the current pointer with a pattern from
  `tc_SPLower`, scan it at the end) run from a script with `Stack 4096` (never from a Shell with a large stack);
  2026-10-20: 5.6 KB of 16 KB used while playing FLAC with a seek, 17.8 KB of 32 KB for `amisqueeze.fpu` with FLAC
  and MP3. Redo it when `-fstack-usage` shows the deepest path growing by more than 1 KB, or when a new decoder,
  library or start-up path is linked (decided 2026-10-21: a few bytes more do not call for it); measure frames
  with `-fstack-usage`.
- **`-m68881` and libnix**: libnix links a start-up check (`__fpucheck`) that, without an FPU, shows an alert and
  quits before `main()` without a message: `amiga.c` replaces it with an empty one and checks `AttnFlags` itself.
  minimp3 keeps about 17 KB on the stack (`mp3dec_decode_frame`): see Stack above. The `-m68881` code uses `fintrz` and `fmovecr`, emulated (slowly) by the CPU library on a 68040/68060.
- **`$VER:` string** (found at the 0.3 release): kept non-const, in the data hunk. Const, it sat in the code hunk,
  and AmigaOS 3.2's `Version` printed "amiseeze 0.3" for the 0.3 build although the bytes were right (only when
  reading the file as an executable; the 0.2 build was fine). Before each release, check `Version <exe> FULL`
  for both executables in WinUAE.
- **Workbench start and libnix** (step 1.10): libnix opens the console named by `__stdiowin` before `main()` when
  started from the Workbench; `amiga.c` sets it to NULL and opens its own after reading the icon (`WINDOW`,
  `QUIET`). A `CON:` window needs `SMART`, or it loses its text when uncovered. With `QUIET` there is no output
  (`msg()` checks `Output()`), except during start-up (`main()`, up to `bsdsocket.library`): a message then opens
  the window (0.6; `MAC` and `OUTPUT`, checked by the core, go to the log only). amisqueeze is
  a commodity ("Remove" in Exchange = Ctrl-C), one at a time (`CBERR_DUP`); a second start does nothing to the
  running one (decided with the user).
- **WinUAE JIT and FPU**: without `compfpu=true` the JIT interprets FPU instructions (MP3 at 1.8x real time
  instead of 74x): a PiStorm-like test of `amisqueeze.fpu` needs it.
- **Volume with Paula**: samples scaled down (low LMS volume) bring out Paula's noise through AHI (14 bits): a
  crackle at 50, gone at 100. Judge Paula's sound at volume 100.
- Strings shown on the Amiga and files read by the Amiga: ASCII only (ISO-8859-1 on the Amiga), LF line endings.
  The README and the user guide are among them, since they ship in the zip (decided 2026-10-21): `...`, `-`, `|`
  rather than their typographic forms.
- WinUAE syntax checked in `cfgfile.cpp`: `filesystem2=rw,DEVICE:Volume:path,bootpri`; `chipmem_size` in 512 KB
  units, `fastmem_size` in MB; networking emulation `bsdsocket_emu=true`.

## Source-level debugging (step 0.8, optional)

Chosen approach: `bgdbserver` (bebbo, Aminet `dev/debug/bgdbserver`) in the emulated Amiga + WinUAE's
`bsdsocket.library` emulation + `m68k-amigaos-gdb` (`target remote …:2345`) + a cpptools `launch.json`; build with
`-g`. To validate first: reaching WinUAE (port 2345) from wherever gdb runs.
Ruled out: uae-dap/prb28 (requires SAS/C LINE DEBUG hunks), Bartman's amiga-debug (its own toolchain, NDK 3.9, no
libc). WinUAE's built-in debugger (Shift+F12) remains available at the assembly level.
