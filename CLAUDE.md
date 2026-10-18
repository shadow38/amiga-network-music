# CLAUDE.md — amiga-network-music

Personal project: network music for classic Amiga (AmigaOS 3.x), in C, tested in WinUAE. Day-to-day usage
(layout, building, tasks) is in `README.md`; this file keeps context, decisions and pitfalls. Public GitHub
repository `shadow38/amiga-network-music`: **nothing specific to the developer's workstation goes here** (local
paths, corporate proxy, personal settings, neighbouring folders); those live in local files outside the
repository. Documentation, code comments and program messages are in English.

**Versioning** (decided 2026-10-18): 0.x until the main feature works; the version changes only for a
**published** release (0.1 = first public push, then 0.2, 0.3…); 1.0 = first really usable version (AHI audio,
basic commands, tested on the real machine). Never bump it on our own: ask the user. The number appears in the
`$VER:` string (`main.c`), in `Firmware=` of the `HELO` capabilities (`slimproto.c`) and in the user guide.

**Licence: MIT** (`LICENSE`, "Copyright (c) 2026 shadow38"), decided 2026-10-18. squeezelite and LMS are GPL:
read them to **understand** the protocol and its expected behaviour (message layouts, when to send which state),
then write our own code; **never copy blocks of their code**. Third-party libraries must have an MIT-compatible
licence (public domain, MIT, BSD, zlib…; e.g. dr_flac, minimp3), not GPL (no libmad).

## Project: amisqueeze and amilms

Target: a classic Amiga with a **PiStorm** and an **AHI** sound card; the WinUAE test configuration must
reproduce that machine.

Decided (2026-10-18): two programs in this repository, one `src/<prog>/` directory each, shared code in
`src/common/`:

- **amisqueeze**: slimproto player with AHI output (works with LMS and Music Assistant, which both speak
  slimproto);
- **amilms**: remote control through the LMS text CLI (TCP 9090); LMS only for now (the Music Assistant API, JSON +
  token + WebSocket, is out of scope for now).

Work proceeds in testable steps, each validated with the user **before** writing code: the plan is in
`docs/development/` (keep the statuses up to date). User-facing behaviour (options, messages) is documented in
`docs/user-guide.md`: update it with every change visible to users. Step 1.1b (default options in
`ENV:amisqueeze.prefs`) validated on 2026-10-18. Current priority: amisqueeze step 1.2 (receiving the stream).

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
