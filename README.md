# amiga-network-music

Network music for classic Amiga (AmigaOS 3.x, 68020+), built around Lyrion Music Server (LMS):

- **amisqueeze**: turns the Amiga into a Squeezebox player. Like squeezelite on a PC, it speaks slimproto, the
  protocol of Logitech's Squeezebox players, so LMS sees it as one more player, controlled from its web interface,
  its apps or any Squeezebox remote; the sound goes through AHI. A software player without display or infrared
  remote. It works with LMS (Music Assistant speaks slimproto too, but is not tested yet);
- **amilms**: a remote control, using the LMS text CLI (TCP 9090).

Target machines: an Amiga with a 68020 or better (from a plain 68020 accelerator to a PiStorm), sound through AHI
(Paula or a sound card), plus a TCP/IP stack providing `bsdsocket.library` (Roadshow, AmiTCP, AmiNetXDuo, or
WinUAE's emulation). Cross-compiled C, tested in WinUAE.

**Download:** [amisqueeze releases](https://github.com/shadow38/amiga-network-music/releases): a zip with the
Amiga executable and the [user guide](docs/user-guide.md). The latest one, 0.1, is a preview tested in WinUAE
only, not yet on a real Amiga.

**Status:** under development, in testable steps described in the
[development plan](docs/development/README.md). amisqueeze: shows up as a player in LMS and plays through AHI
(play, pause, seek, stop, gapless next track, volume; validated in WinUAE with emulated networking and LMS 9.1.1);
raw PCM (8 to 32 bits, mono or stereo, up to 48 kHz), and FLAC decoded on the Amiga when it is fast enough (a
PiStorm-class CPU; measured at start-up), otherwise converted by the server; MP3 still needs a conversion rule on
the server until local MP3 decoding comes. amilms: not started.

## Requirements

### Build

- the **`m68k-amigaos-gcc`** cross-compiler (AmigaPorts, GCC 16.2 or newer), with NDK 3.2 (v47 headers, including
  `bsdsocket` and AHI) and libnix;
- `make` and `python3` (for the `tools/check-saves.py` safeguard).

Two ways to get the toolchain:

- **native install** (Linux, macOS, WSL on Windows): `m68k-amigaos-gcc` in the `PATH`;
- **Docker**: the [`amigadev/crosstools`](https://hub.docker.com/r/amigadev/crosstools) image, tag
  `m68k-amigaos-gcc10` (a historical name: it follows the AmigaPorts toolchain, now GCC 16). It also ships gdb,
  vasm and vamos.

### Host-side tests (`make host`, `make test`)

- a **native C compiler** for the build machine (`$(HOSTCC)`, default `cc`) with AddressSanitizer and
  UndefinedBehaviorSanitizer support, POSIX sockets;
- `python3` (fake server).

Tested with gcc 14 on Linux. clang, macOS and other POSIX systems should work but have not been tried.

Note that in the `amigadev/crosstools` image, `gcc` is the Amiga cross-compiler: use another environment, or set
`HOSTCC` to a native compiler.

### LMS server

- Ports: 3483/tcp (players), 9000/tcp (web UI and audio streams; set `HTTP_PORT` if you move it), 9090/tcp (CLI).
- **MP3 to PCM conversion.** amisqueeze currently only advertises raw PCM, so the server must convert. LMS 9.1
  has default rules for FLAC and WAV, but **none for MP3** (regular players decode MP3 themselves). Without it,
  `server.log` says "Couldn't create command line for mp3 playback" and the player receives nothing. Add this rule
  to a `custom-convert.conf` file (in the LMS root directory or at the root of a plugins directory), modelled on the
  built-in `ogg pcm` rule and using the `sox` binary shipped with LMS, then restart LMS:

  ```
  mp3 pcm * *
  	[sox] -q -t mp3 $FILE$ -t raw -r 44100 -c 2 -2 -s -
  ```

- Music Assistant: not verified yet. It offers no raw PCM output (per-player output codecs: `flac` by default,
  `mp3`, `aac`, `wav`); `wav` is the likely candidate.

### Testing

- WinUAE 6.0 (or a real machine), with a TCP/IP stack (see [Testing in WinUAE](#testing-in-winuae));
- an LMS server reachable from the Amiga; squeezelite on the PC is handy as a reference player.

## Repository layout

```
src/<prog>/           C sources, one directory per program -> out/<prog>
src/amisqueeze/       portable core (player.c, slimproto.c, player.h), system services behind platform.h:
                      amiga.c (AmigaOS, Amiga build only), posix.c (Linux/POSIX, host build only)
tests/                unit tests of the core (test_core.c on stub_platform.c), fake server (fake_server.py)
obj/, out/            Amiga objects and executables (generated)
obj-host/, out-host/  host objects and executables (generated)
.sdk/                 copy of the toolchain headers, for IntelliSense (generated)
.vscode/              tasks (build, clean, headers) and IntelliSense configuration
docs/user-guide.md    user guide
docs/development/     development plan
Makefile              build (m68k-amigaos-gcc must be in the PATH; HOSTCC for the host build)
tools/amiga-make.sh   runs make with whichever toolchain is available (see "Building")
tools/check-saves.py  checks the generated assembly (safeguard against a gcc bug, see "Building")
tools/pcm-compare.py  compares the PCM written by amisqueeze (OUTPUT option) with a reference, byte by byte
```

`obj/`, `out/`, `obj-host/`, `out-host/` and `.sdk/` are generated (ignored by `.gitignore`).

## Building

With the toolchain in the `PATH`, `make` is enough (`make all`, `make clean`, `make sdk`, or `make <prog>`).

`tools/amiga-make.sh` (same arguments as `make`) picks the available toolchain, in this order:

1. **`$AMIGA_MAKE`**, if set: a local script run instead (it receives the `make` arguments);
2. **`m68k-amigaos-gcc` in the `PATH`**: plain `make`;
3. **`../amiga-make.local.sh`** (next to the repository, outside Git), if it exists: same role as `$AMIGA_MAKE`,
   for a setup specific to one machine (container, remote host…);
4. **Docker**: `docker run` of the `$AMIGA_IMAGE` image (default `amigadev/crosstools:m68k-amigaos-gcc10`), with
   the repository mounted on `/work`. *Not tested yet.*

```sh
sh tools/amiga-make.sh            # all
sh tools/amiga-make.sh clean
```

On Windows, go through WSL (default distribution), from the repository directory in PowerShell:

```powershell
wsl --cd $PWD --exec sh tools/amiga-make.sh
```

Run `make sdk` (task **Amiga: sync headers (.sdk)**) after installing or updating the toolchain, for IntelliSense.

`make dist` (`sh tools/amiga-make.sh dist`) builds the release archive `out/amisqueeze-<version>.zip`: the Amiga
executable, `LICENSE`, this README and the user guide, the version read from the program's `$VER:` string.

Per-program options live in the `Makefile` (`<prog>_CFLAGS`): network programs target the 68020 (`-m68020`).
Header dependencies are tracked (`-MMD -MP`, `obj/<prog>/*.d` files).

**Safeguard:** each `.c` file is first compiled to assembly (`obj/<prog>/*.s`), which `tools/check-saves.py`
checks before assembling. The build fails if a function modifies a register the caller expects to be preserved
(d2-d7, a2-a6) without saving it. This was a bug in `m68k-amigaos-gcc` 16.1.1b (image
`wischner/gcc-m68k-amiga:1.1.0`): programs crashed with an *address error* `#80000003`. It is fixed in 16.2, but
the GCC 16 port is recent.

### Host build and tests

amisqueeze is split into a **portable core** in standard C (no Amiga headers) and a thin **platform layer**
(`platform.h`): `amiga.c` on AmigaOS, `posix.c` on Linux and other POSIX systems. The same core therefore also
runs on the build machine, where it is fast to test:

```sh
make host                         # out-host/amisqueeze, with AddressSanitizer and UBSan
out-host/amisqueeze SERVER=192.168.1.10 NAME=Test VERBOSE     # same arguments as on the Amiga; Ctrl-C stops it
make test                         # unit tests of the core, then scenarios against out-host/amisqueeze
make test HOSTCC=clang            # another native compiler (untested)
```

`make test` needs no LMS server: the unit tests run the core on a fake platform (`tests/stub_platform.c`), and
`tests/fake_server.py` plays a slimproto server on localhost (handshake, `strm t`, renaming, disconnection and
reconnection, Ctrl-C, argument errors). The Amiga build must still be checked in WinUAE: the platform layers
differ.

## VS Code

Recommended extensions: `ms-vscode.cpptools` (C IntelliSense, using the headers in `.sdk/`),
`prb28.amiga-assembly` (68k assembly highlighting) and `spencerwmiles.vscode-task-buttons` (task buttons).
Behind a corporate proxy that inspects TLS, `code --install-extension` may need `NODE_EXTRA_CA_CERTS` pointing to
the proxy's root certificate (exported as PEM).

Tasks (`.vscode/tasks.json`, calling `tools/amiga-make.sh`, through WSL on Windows): **Amiga: build**
(`Ctrl+Shift+B`), **Amiga: clean**, **Amiga: sync headers (.sdk)**. Status bar buttons are set in
`.vscode/settings.json` (Task Buttons extension). Keyboard shortcuts can target tasks by name
(`workbench.action.tasks.runTask`).

## amisqueeze

```
amisqueeze SERVER=192.168.1.10 NAME=Amiga VERBOSE
```

Options (`SERVER`, `PORT`, `NAME`, `MAC`, `OUTPUT`, `UNIT`, `NOAUDIO`, `SWAP`, `FLAC`, `BENCH`, `VERBOSE`) and default options
stored in `ENV:amisqueeze.prefs` are
described in the **[user guide](docs/user-guide.md)**.

Current state (steps 1.1 to 1.5 of the plan): control connection, player name, default options, and 16-bit PCM
streams received into a 1 MB buffer and played through AHI (progress reported to LMS, gapless next track, pause,
seek, stop, volume and mute from LMS), or consumed by a virtual output (`NOAUDIO`, host build) and optionally written to a file (`OUTPUT`) to
check it byte by byte. Listening tests done in WinUAE (1.4c); on a 28 MHz 68020, sound through AHI and Paula takes
about two thirds of the CPU (AHI's mixing, amisqueeze itself a few percent). What comes next is in the
[amisqueeze plan](docs/development/phase-1-amisqueeze.md).

Protocol references: squeezelite (`slimproto.c`, `slimproto.h`, `stream.c`, `pcm.c`,
https://github.com/ralph-irving/squeezelite) and LMS (`Slim/Networking/Slimproto.pm`).

## Testing in WinUAE

- Enable WinUAE's networking emulation: `bsdsocket_emu=true`. WinUAE then provides `bsdsocket.library` itself,
  through the host's network stack: no TCP/IP stack to install in the Amiga, and a server reachable from the host
  is reachable from the Amiga (`127.0.0.1` is the host's loopback).
- Mount `out/` as an Amiga volume (`filesystem2=rw,<DEVICE>:<Volume>:<path to out>,-128`), then run the program
  from a Shell, e.g. `NM:amisqueeze SERVER=127.0.0.1 VERBOSE` with the volume mounted as `NM:`.
- Dedicated test configurations (a fast machine like a PiStorm, a slow 68020, AHI, automated runs) are planned:
  steps 0.4 to 0.7 of the [tooling plan](docs/development/phase-0-tooling.md).

## Debugging

No source-level debugging yet. Meanwhile, WinUAE's built-in debugger (`Shift+F12`) follows the program at the
assembly level. Planned (step 0.8): `bgdbserver` (Aminet `dev/debug/bgdbserver`) in the emulated Amiga, with
WinUAE's `bsdsocket.library` emulation, and `m68k-amigaos-gdb` (`target remote …:2345`) driven by cpptools; build
with `-g`.

## Notes

- `vamos` (shipped with the `amigadev/crosstools` image) runs command-line tools that only use dos.library, but
  not windowed or network programs, and crashes on `printf`: test in WinUAE. Run 68020 programs with `vamos -C
  68020`. Its `GetVar` ignores global variables (`ENV:`), so default options cannot be tested with it.
- Strings shown on the Amiga: stick to ASCII (the Amiga uses ISO-8859-1, the sources are UTF-8).

## Licence

MIT, see [LICENSE](LICENSE). amisqueeze is written from scratch from the protocol description; squeezelite and
LMS (GPL) are used as references for the protocol's behaviour, not as a source of code. Third-party code, in
`third_party/` (each with a README giving its origin and licence): dr_flac (FLAC decoding), public domain or MIT No
Attribution.
