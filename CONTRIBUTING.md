# Contributing

For developers: how to build Amiga Network Music, test it, find one's way in the repository and propose a change.
Using the programs is covered in the [user guide](docs/user-guide.md); what comes next is in the
[development plan](docs/development/README.md).

## Proposing a change

- For anything more than a small fix, **open an issue first**: the work proceeds in planned steps, each with an
  agreed content and success criterion (see the plan), and a change may already be planned, or ruled out.
- `make test` must pass (GitHub runs it on each pull request); add tests for what you change in the portable core.
  The Amiga build must also be tried in WinUAE (see below): the tests run the core on the build machine only.
- Code, comments, documentation and program messages are in English; strings shown on the Amiga stay ASCII.
- The licence is MIT: no code copied from GPL projects (squeezelite and LMS are references for the protocol's
  behaviour, never a source of code); third-party code only under an MIT-compatible licence, in `third_party/`
  with its origin and licence.

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

### Testing

- WinUAE 6.0 (or a real machine), with a TCP/IP stack (see [Testing in WinUAE](#testing-in-winuae));
- an LMS server reachable from the Amiga, set up as in the user guide's
  [server setup](docs/user-guide.md#server-setup), with the `mp3 pcm` rule (to test the builds that leave MP3 to
  the server); the CLI port (9090/tcp) is handy to drive it from scripts; squeezelite on the PC is handy as a
  reference player. See also steps 0.2 and 0.3 of the [tooling plan](docs/development/phase-0-tooling.md).

## Repository layout

```
src/<prog>/           C sources, one directory per program -> out/<prog>
src/amisqueeze/       portable core (player, slimproto, stream, ring, audio, decoders: decoder, flac, mp3, bench),
                      system services behind platform.h: amiga.c (AmigaOS, Amiga build only), posix.c
                      (Linux/POSIX, host build only)
tests/                unit tests of the core (test_core.c on stub_platform.c), fake server (fake_server.py),
                      test files (data/: FLAC and MP3 made from the signals of flac_data.py)
third_party/          decoders by others, unmodified, with their version and licence: dr_flac, minimp3
obj/, out/            Amiga objects and executables (generated; obj/amisqueeze-fpu/: amisqueeze.fpu)
obj-host/, out-host/  host objects and executables (generated)
.sdk/                 copy of the toolchain headers, for IntelliSense (generated)
.vscode/              tasks (build, clean, headers) and IntelliSense configuration
.github/workflows/    continuous integration: make test on GitHub (tests.yml)
.claude/              the review before each release, for Claude Code (plan step 3.5): the method
                      (skills/review-method), four review agents (agents/), the entry point
                      (skills/release-review, /release-review <previous tag>)
docs/user-guide.md    user guide
docs/development/     development plan (roadmap, steps, design decisions)
CONTRIBUTING.md       this page
CHANGELOG.md          what changed in each release (updated with each one)
Makefile              build (m68k-amigaos-gcc must be in the PATH; HOSTCC for the host build)
tools/amiga-make.sh   runs make with whichever toolchain is available (see "Building")
tools/check-saves.py  checks the generated assembly (safeguard against a gcc bug, see "Building")
tools/pcm-compare.py  compares the PCM written by amisqueeze (OUTPUT option) with a reference, byte by byte
tools/make-dist.py    builds the release archive (make dist)
tools/check-version.py checks that the version number agrees everywhere (make test, make dist)
tools/bin2c.py        turns a file into a C array (the built-in FLAC and MP3 samples of the speed measurement)
tools/make-icon.py    draws the Workbench icons (out/<prog>.info, classic AmigaOS format) built with the programs
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
executables and their icons, `LICENSE`, the README, the changelog and the user guide with its images
(`docs/images/`). Before a release, the "Unreleased" part of `CHANGELOG.md` gets the version number and the date.
`make dist` refuses to build the archive unless `tools/check-version.py --release` accepts the version: not a
`-dev` one, the same in both `$VER:` strings, `Firmware=`, the user guide and the README, with its section at the
top of the changelog, dated as the `$VER:` strings, and nothing left under "Unreleased".

Per-program options live in the `Makefile` (`<prog>_CFLAGS`): network programs target the 68020 (`-m68020`).
amisqueeze is built twice: `out/amisqueeze` (68020, no FPU) and `out/amisqueeze.fpu` (68020 + 68881, with the MP3
decoder, `AMISQUEEZE_FPU` and `AMISQUEEZE_MP3`); the host build has the MP3 decoder too.
Header dependencies are tracked (`-MMD -MP`, `obj/<prog>/*.d` files).

**Safeguard:** each `.c` file is first compiled to assembly (`obj/<prog>/*.s`), which `tools/check-saves.py`
checks before assembling. The build fails if a function modifies a register the caller expects to be preserved
(d2-d7, a2-a6) without saving it. This was a bug in `m68k-amigaos-gcc` 16.1.1b (image
`wischner/gcc-m68k-amiga:1.1.0`): programs crashed with an *address error* `#80000003`. It is fixed in 16.2, but
the GCC 16 port is recent.

## Host build and tests

amisqueeze is split into a **portable core** in standard C (no Amiga headers) and a thin **platform layer**
(`platform.h`): `amiga.c` on AmigaOS, `posix.c` on Linux and other POSIX systems. The same core therefore also
runs on the build machine, where it is fast to test:

```sh
make host                         # out-host/amisqueeze, with AddressSanitizer and UBSan
out-host/amisqueeze SERVER=192.168.1.10 NAME=Test VERBOSE     # same arguments as on the Amiga; Ctrl-C stops it
make test                         # unit tests of the core, then scenarios against out-host/amisqueeze
make test HOSTCC=clang            # another native compiler (untested)
```

Before the tests, `make test` runs `tools/check-version.py`: the version number must be the same in both `$VER:` strings
(declared as `static char <name>[] = "$VER: ...";`, never const), `Firmware=` and the user guide's state line
(`Version x.y in development` for `x.y-dev`, `Version x.y,` for a release); the latest release named by the guide
and the README must be the newest section of `CHANGELOG.md`; the commodity's title must fit 39 characters.
`make test` needs no LMS server: the unit tests run the core on a fake platform (`tests/stub_platform.c`), and
`tests/fake_server.py` plays a slimproto server and its HTTP streams on localhost (handshake, renaming,
disconnection and reconnection, Ctrl-C, argument errors; streams played, paused, stopped, held, cut; the `OUTPUT`
file compared with what was sent; FLAC and MP3 test files decoded from `tests/data/`, whole or from the middle as
after a seek, back to back with raw PCM).
The Amiga build must still be checked in WinUAE: the platform layers differ.

GitHub runs `make test` on each push and pull request (`.github/workflows/tests.yml`, GitHub Actions, Ubuntu); the
badge at the top of the README shows the result of the latest run. The Amiga build is not part of it yet.

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

## VS Code

Recommended extensions: `ms-vscode.cpptools` (C IntelliSense, using the headers in `.sdk/`),
`prb28.amiga-assembly` (68k assembly highlighting) and `spencerwmiles.vscode-task-buttons` (task buttons).
Behind a corporate proxy that inspects TLS, `code --install-extension` may need `NODE_EXTRA_CA_CERTS` pointing to
the proxy's root certificate (exported as PEM).

Tasks (`.vscode/tasks.json`, calling `tools/amiga-make.sh`, through WSL on Windows): **Amiga: build**
(`Ctrl+Shift+B`), **Amiga: clean**, **Amiga: sync headers (.sdk)**. Status bar buttons are set in
`.vscode/settings.json` (Task Buttons extension). Keyboard shortcuts can target tasks by name
(`workbench.action.tasks.runTask`).

## Notes

- Protocol references: squeezelite (`slimproto.c`, `slimproto.h`, `stream.c`, `pcm.c`,
  https://github.com/ralph-irving/squeezelite) and LMS (`Slim/Networking/Slimproto.pm`). They are GPL: read to
  understand the protocol's behaviour, never copied (the project is MIT).
- `vamos` (shipped with the `amigadev/crosstools` image) runs command-line tools that only use dos.library, but
  not windowed or network programs, and crashes on `printf`: test in WinUAE. Run 68020 programs with `vamos -C
  68020`. Its `GetVar` ignores global variables (`ENV:`), so default options cannot be tested with it.
- Strings shown on the Amiga: stick to ASCII (the Amiga uses ISO-8859-1, the sources are UTF-8).
