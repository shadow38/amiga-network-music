# amiga-network-music

Network music for classic Amiga (AmigaOS 3.x, 68020+), built around Lyrion Music Server (LMS):

- **amisqueeze**: a network player speaking slimproto (the Squeezebox protocol), with audio output through AHI.
  It works with LMS and with Music Assistant, which both speak slimproto;
- **amilms**: a remote control, using the LMS text CLI (TCP 9090).

Target machine: an Amiga with a PiStorm and an AHI sound card, plus a TCP/IP stack providing `bsdsocket.library`
(Roadshow, AmiTCP, AmiNetXDuo, or WinUAE's emulation). Cross-compiled C, tested in WinUAE.

**Status:** under development, in testable steps described in the
[development plan](docs/development/README.md). amisqueeze: the control connection works (validated in WinUAE with
emulated networking and LMS 9.1.1: the player shows up in LMS), default options in `ENV:amisqueeze.prefs`; no
streaming or audio yet. amilms: not started.

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

Options (`SERVER`, `PORT`, `NAME`, `MAC`, `VERBOSE`) and default options stored in `ENV:amisqueeze.prefs` are
described in the **[user guide](docs/user-guide.md)**.

Current state (steps 1.1 and 1.1b of the plan): control connection, `HELO`, `STAT` replies (`STMt`…), player
name, default options; streams are refused (`STMn`) and their parameters printed. What comes next is in the
[amisqueeze plan](docs/development/phase-1-amisqueeze.md).

Protocol references: squeezelite (`slimproto.c`, `slimproto.h`, `stream.c`, `pcm.c`,
https://github.com/ralph-irving/squeezelite) and LMS (`Slim/Networking/Slimproto.pm`).

## Testing in WinUAE

- Enable WinUAE's networking emulation: `bsdsocket_emu=true`. WinUAE then provides `bsdsocket.library` itself,
  through the host's network stack: no TCP/IP stack to install in the Amiga, and a server reachable from the host
  is reachable from the Amiga (`127.0.0.1` is the host's loopback).
- Mount `out/` as an Amiga volume (`filesystem2=rw,<DEVICE>:<Volume>:<path to out>,-128`), then run the program
  from a Shell, e.g. `NM:amisqueeze SERVER=127.0.0.1 VERBOSE` with the volume mounted as `NM:`.
- A dedicated test configuration (a machine close to the PiStorm, AHI, automated runs) is planned: steps 0.4 to
  0.7 of the [tooling plan](docs/development/phase-0-tooling.md).

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
LMS (GPL) are used as references for the protocol's behaviour, not as a source of code.
