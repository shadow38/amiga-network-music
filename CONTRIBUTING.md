# Contributing

For developers: how to build Amiga Network Music, test it, find one's way in the repository and propose a change.
Using the programs is covered in the [user guide](docs/user-guide.md); what comes next is in the
[development plan](docs/development/README.md).

## Proposing a change

- For anything more than a small fix, **open an issue first**: the work proceeds in planned steps, each with an
  agreed content and success criterion (see the plan), and a change may already be planned, or ruled out.
- `make test` must pass (GitHub runs it on each pull request); add tests for what you change in the portable core.
  The Amiga build must also be tried in WinUAE (see below): the tests run the core on the build machine only.
- A change users can see (an option, a message, a behaviour) is documented in the
  [user guide](docs/user-guide.md) and noted under "Unreleased" in `CHANGELOG.md` (see [Releasing](#releasing)).
- Code, comments, documentation and program messages are in English; strings shown on the Amiga stay ASCII.
  Documentation follows [Writing documentation](#writing-documentation).
- The licence is MIT: no code copied from GPL projects (squeezelite and LMS are references for the protocol's
  behaviour, never a source of code); third-party code only under an MIT-compatible licence (public domain, MIT,
  BSD, zlib...; not GPL: no libmad), in `third_party/` with its origin and licence (see [Code rules](#code-rules)).

## Requirements

### Build

- the **`m68k-amigaos-gcc`** cross-compiler (AmigaPorts, GCC 16.2 or newer), with NDK 3.2 (v47 headers, including
  `bsdsocket` and AHI) and libnix;
- `make` and `python3` (for the `tools/check-saves.py` safeguard).

Two ways to get the toolchain:

- **native install** (Linux, macOS, WSL on Windows): `m68k-amigaos-gcc` in the `PATH`;
- **Docker**: the [`amigadev/crosstools`](https://hub.docker.com/r/amigadev/crosstools) image, tag
  `m68k-amigaos-gcc10` (a historical name: it follows the AmigaPorts toolchain, now GCC 16; installed in
  `/opt/m68k-amigaos`). It also ships gdb,
  vasm and vamos. The tag moves when the image is rebuilt, so `tools/amiga-make.sh` pins it by its digest
  (`REFERENCE_IMAGE`, the toolchain the releases are built with). To move to a newer image: put its digest there,
  build, run `make test` and the scenarios in WinUAE, and compare the executables' SHA-256 printed by the CI with a
  local build.

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
  reference player. See also steps tools-2 and tools-3 of the [tooling plan](docs/development/phase-0-tooling.md).

## Repository layout

```
src/<prog>/           C sources, one directory per program -> out/<prog>; code shared between programs will go
                      in src/common/ (with amilms, none yet)
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
.github/workflows/    continuous integration: make test and the Amiga build on each push (tests.yml), the draft
                      release made from a tag (release.yml)
.claude/              the review before each release, for Claude Code (plan step publish-5): the method
                      (skills/review-method), four review agents (agents/), the entry point
                      (skills/release-review, /release-review <previous tag> [full])
CLAUDE.md             for Claude Code: how to work with the maintainer, pointers to this page and the others
docs/user-guide.md    user guide
docs/development/     development plan (roadmap, steps, design decisions); architecture.md: how amisqueeze works;
                      testing.md: testing in WinUAE; winuae-scenarios.md: the scenarios of tools/winuae/
winuae/               WinUAE test machine (a template: amisqueeze-test.uae)
CONTRIBUTING.md       this page
CHANGELOG.md          what changed in each release (updated with each one)
Makefile              build (m68k-amigaos-gcc must be in the PATH; HOSTCC for the host build)
tools/amiga-make.sh   runs make with whichever toolchain is available (see "Building")
tools/check-saves.py  checks the generated assembly (safeguard against a gcc bug, see "Building")
tools/check-stack.py  checks the linker map: libnix's stack swap linked (see "Stack and memory")
tools/pcm-compare.py  compares the PCM written by amisqueeze (OUTPUT option) with a reference, byte by byte
tools/make-test-audio.sh makes the test sounds (tones, sides, sweep, gapless MP3s, counter pattern)
tools/winuae/         test scenarios run in WinUAE from the host (run-scenarios.py runs them all; serial shell
                      and LMS CLI clients)
tools/amiga/cpucount.c free CPU meter run on the Amiga for the load measures (make cpucount, not released)
tools/make-dist.py    builds the release archives and the Aminet readme (make dist); checks the texts are ASCII
dist/amisqueeze.readme the Aminet readme, its version filled in by make dist
tools/check-version.py checks that the version number agrees everywhere (make test, make dist)
tools/bin2c.py        turns a file into a C array (the built-in FLAC and MP3 samples of the speed measurement)
tools/make-icon.py    draws the Workbench icons (out/<prog>.info, classic AmigaOS format) built with the programs
```

`obj/`, `out/`, `obj-host/`, `out-host/` and `.sdk/` are generated (ignored by `.gitignore`). `docs/development/`
is the project's working plan (steps, rules, review reports, which commit messages refer to): not needed to use or
build the programs.

## Building

With the toolchain in the `PATH`, `make` is enough (`make all`, `make clean`, `make sdk`, or `make <prog>`).

`tools/amiga-make.sh` (same arguments as `make`) picks the available toolchain, in this order:

1. **`$AMIGA_MAKE`**, if set: a local script run instead (it receives the `make` arguments);
2. **`m68k-amigaos-gcc` in the `PATH`**: plain `make`;
3. **`../amiga-make.local.sh`** (next to the repository, outside Git), if it exists: same role as `$AMIGA_MAKE`,
   for a setup specific to one machine (container, remote host…);
4. **Docker**: `docker run` of the `$AMIGA_IMAGE` image (default: the reference image, pinned by digest), with
   the repository mounted on `/work`. The CI builds this way on each push (see Host build and tests below).

```sh
sh tools/amiga-make.sh            # all
sh tools/amiga-make.sh clean
```

On Windows, go through WSL (default distribution), from the repository directory in PowerShell:

```powershell
wsl --cd $PWD --exec sh tools/amiga-make.sh
```

Run `make sdk` (task **Amiga: sync headers (.sdk)**) after installing or updating the toolchain, for IntelliSense.

`make dist` (`sh tools/amiga-make.sh dist`) builds the release archives `out/amisqueeze-<version>.zip` and
`out/amisqueeze-<version>.lha`, which hold the same files: the Amiga executables and their icons, `LICENSE`, the
README, the changelog and the user guide with its images (`docs/images/`), in an `amisqueeze-<version>/`
directory. Next to each archive, `<archive>.sha256` holds its SHA-256 in the format of `sha256sum` (checked with
`sha256sum -c`), and `out/amisqueeze-<version>.readme` is the Aminet readme, `dist/amisqueeze.readme` with its
version filled in. The `.lha` needs `lha` (LHa for UNIX, [jca02266/lha](https://github.com/jca02266/lha)), which
the image `amigadev/crosstools` has; it is made with method `-lh5-` and header level 1, what LhA 2.15 writes on
the Amiga, then tested (`lha t`) and its files and sizes compared with the zip's. The readme is checked against
Aminet's rules (the fields, `Short:` of 40 characters at most, lines of 78 at most, ASCII, LF; `make test` checks
it too). `make dist` ends with the lines that give the SHA-256 in the release notes. Each run makes new archives
(new dates, new SHA-256): attach the archives, their `.sha256` and the readme of the same run, and copy that run's
lines into the notes. Before a release, the "Unreleased" part of `CHANGELOG.md` gets the version number and the
date. `make dist DEV=1` accepts a development version, to try the archives (`out/amisqueeze-<x.y>-dev.*`, never
published).
`make dist` refuses to build the archive unless `tools/check-version.py --release` accepts the version: not a
`-dev` one, the same in both `$VER:` strings, `Firmware=`, the user guide and the README, with its section at the
top of the changelog, dated as the `$VER:` strings, and nothing left under "Unreleased". It also refuses a text
file of the archive (`LICENSE`, the README, the changelog, the user guide) or a source of amisqueeze that is not
ASCII with LF line endings ([Text for the Amiga](#code-rules)); `make test` checks them too.

Both workflows pin their actions by commit SHA, the version in a comment (as `tools/amiga-make.sh` pins the image
by its digest; to update one, take the commit of its new tag), and their checkout keeps no token
(`persist-credentials: false`), since the build's container mounts the whole repository. The published archives
are made by GitHub from the tag (`.github/workflows/release.yml`, see [Releasing](#releasing)).

Per-program options live in the `Makefile` (`<prog>_CFLAGS`): network programs target the 68020 (`-m68020`).
amisqueeze is built twice: `out/amisqueeze` (68020, no FPU) and `out/amisqueeze.fpu` (68020 + 68881, with the MP3
decoder, `AMISQUEEZE_FPU` and `AMISQUEEZE_MP3`); the host build has the MP3 decoder too.
Header dependencies are tracked (`-MMD -MP`, `obj/<prog>/*.d` files).

**Safeguard:** each `.c` file is first compiled to assembly (`obj/<prog>/*.s`), which `tools/check-saves.py`
checks before assembling. The build fails if a function modifies a register the caller expects to be preserved
(d2-d7, a2-a6, and fp2-fp7 in `amisqueeze.fpu`) without saving it. This was a bug in `m68k-amigaos-gcc`
16.1.1b (image `wischner/gcc-m68k-amiga:1.1.0`): programs crashed with an *address error* `#80000003`. It is fixed
in 16.2, but the GCC 16 port is recent. `make test` checks the safeguard itself on `tests/check-saves.s`: a few
functions with such a fault (a plain write, `(an)+`, `-(an)`, `dbra`, an FPU register) and a few without. No
compiler option avoided the bug (`-O1`, `-Os`, `-O2`, `-mlra`, `-mno-lra`); it was fixed in 16.2-rc16. The GCC 16 port is still young: when a crash
makes no sense, look at the generated assembly.

## Host build and tests

amisqueeze is split into a **portable core** in standard C (no Amiga headers) and a thin **platform layer**
(`platform.h`): `amiga.c` on AmigaOS, `posix.c` on Linux and other POSIX systems. The same core therefore also
runs on the build machine, where it is fast to test (how the core and the platform layer fit together:
[architecture](docs/development/architecture.md)):

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
disconnection and reconnection, Ctrl-C, argument errors; streams played, paused, stopped, flushed, refused, held,
cut; the `OUTPUT` file compared with what was sent; FLAC and MP3 test files decoded from `tests/data/`, whole or
from the middle as after a seek, back to back with raw PCM). It also builds amisqueeze without the MP3 decoder
(`out-host/amisqueeze-nomp3`), as `out/amisqueeze` is, and checks what that build advertises and refuses.
The Amiga build must still be checked in WinUAE: the platform layers differ.

GitHub runs `make test` on each push and pull request (`.github/workflows/tests.yml`, GitHub Actions, Ubuntu); the
badge at the top of the README shows the result of the latest run. A second job builds both Amiga executables and
the archives in their development form (`sh tools/amiga-make.sh all dist DEV=1`, which takes the Docker way on
GitHub's machine), then prints the executables' SHA-256: a local build from the same image gives the same. Its
`.lha` and `.zip` are kept for 14 days as the run's artifacts, each under its own name and not wrapped in another
zip (on the run's page, for a signed-in GitHub user): a development build to try without building it, its version
`<next>-dev` (on a release commit, the release's own number). With the GitHub CLI, not `gh run download`, which
takes every artifact for a zip to unpack (it fails on the `.lha`, gh 2.102), but
`gh api repos/shadow38/amiga-network-music/actions/artifacts/<id>/zip > <file>`,
the ids given by `gh api repos/shadow38/amiga-network-music/actions/runs/<run id>/artifacts`.

The tests use real time (playback pacing, reconnection delays), so a slow runner can make one fail: rerun it
before suspecting the code.

## Code rules

amisqueeze targets classic Amigas with a 68020 or better, of two kinds: **fast** (a PiStorm with Emu68: a 68040
with FPU) and **slow** (a 68020 accelerator without FPU), with sound through AHI: Paula (`paula.audio`, on Amigas
without a sound card) or another AHI driver (a sound card, or the PiStorm's own outputs: the I2S DAC of the
PiStorm32-lite FPGA firmware, HDMI with a Pi 4). The code uses only the `ahi.device` features of AHI 4 (tested
with AHI 6.6; 4.x is expected to work, not checked). How the parts fit together:
[architecture](docs/development/architecture.md).

- **Portable core**: every file of `src/amisqueeze/` but `amiga.c` and `posix.c` is standard C (`<stdint.h>`, no
  Amiga or POSIX header); everything system-related goes through `platform.h`. Keep the core free of system calls
  and the platform layers thin; the main loop (timeouts, reconnection) lives in the core.
- **One task, one loop** around `plat_wait()`: `WaitSelect()` on the Amiga, where a `SocketBase` belongs to the
  task that opened it; `select()` on POSIX, with SIGINT and SIGTERM set without `SA_RESTART`, so that Ctrl-C stops
  the wait.
- **Byte order**: addresses and ports in host byte order in the core; `htonl`/`ntohl` only in the platform layers
  (no-ops on 68k, essential on x86).
- **Serialisation**: fields byte by byte, no packed structures; 32-bit values are `uint32_t` (the MAC hash too, so
  that both builds derive the same MAC).
- **`HELO`** advertises only the formats the build actually decodes.
- **Positions**: the ring's (bytes of the stream) and the output's (bytes of decoded PCM) are never mixed
  ([two kinds of position](docs/development/architecture.md#two-kinds-of-position)). Raw PCM cannot reveal a
  mix-up: the test-only "widening" decoder of `tests/test_core.c` can.
- **`OUTPUT`** writes from `hand_over()` in `stream.c` only, the one place where the output hands PCM over.
- **Messages** go through `msg()`, with the standard printf format (`format(printf)` attribute, `-Werror=format`
  on both builds; on the Amiga, libnix `vsnprintf` into a static buffer, then `Write()` to `Output()`,
  unbuffered, so that a log redirected to a file can be read while the program runs), raw bytes through
  `msg_raw()`. Never `Printf`, `PutStr` or `printf` directly, except inside the platform layers' own
  implementation of `msg()`, `msg_raw()` and the `LOG` file. Cast 32-bit values to the type the specifier names
  (`(unsigned long)` with `%lX`); `%c` for chars (not `%lc`). `STMt` (one a second) is not printed with `VERBOSE`.
- **Text for the Amiga**: strings shown on the Amiga and files it reads are ASCII only (the Amiga uses
  ISO-8859-1, the sources are UTF-8), with LF line endings. The README and the user guide are among them, since
  they ship in the archives: `...`, `-`, `|` rather than their typographic forms.
- **ahi.device**: keep the rules of [AHI](docs/development/architecture.md#ahi) (a request chained only to one
  already playing, the playing one aborted first, `CMD_STOP` is no pause). **Never send `ahir_Volume` below
  full**: the core scales the samples instead.
- **Third-party code** goes in `third_party/`, unmodified, with its version, commit and licence in a `README.txt`,
  each library built in a file of its own without our warnings (`THIRD_PARTY` in the `Makefile`). A file that
  includes a third-party header and reads its structures must use the same configuration macros as the file that
  builds it: otherwise the structures' layout differs, without a warning. What each decoder needs from its
  caller: [decoders](docs/development/architecture.md#decoders).

### Stack and memory

- The default Shell stack is 4 KB, and AmigaOS does not detect an overflow (silent corruption). Big buffers are
  static or allocated, never on the stack.
- Both executables set `__stack` (16 KB `amisqueeze`, 32 KB `amisqueeze.fpu`, in `amiga.c`) and link libnix's
  `__stkinit`, which gives them that stack when the Shell's is smaller: dr_flac needs about 5.5 KB (4.6 KB in
  `drflac_open` alone), minimp3 about 17.5 KB (most of it in `mp3dec_decode_frame`). After each link,
  `tools/check-stack.py` reads the linker map (`obj/<prog>.map`): the build fails if `__stkinit` is not linked or
  `__stack` is not defined in `amiga.c`.
- Measured last on 2026-10-23 on the 0.7 release build, with the probe of
  [Checks the host tests cannot make](#checks-the-host-tests-cannot-make), from a script with `Stack 4096`: 5600 of
  16384 bytes used by `amisqueeze` playing FLAC with a seek, 17544 of 32768 by `amisqueeze.fpu` with FLAC and MP3. Redo it when `-fstack-usage` shows the deepest path growing by more
  than 1 KB, or when a new decoder, library or start-up path is linked; a few bytes more do not call for it.
- Memory: see `Avail` in [Checks the host tests cannot make](#checks-the-host-tests-cannot-make).

## Writing documentation

Each document has one reader and answers one question. Write for that reader; anything else goes where it
belongs, with a link.

| Document | Reader | Question it answers |
|---|---|---|
| `README.md` | someone discovering the project | What is it, what does it need, how do I start? |
| `docs/user-guide.md` | a user | How do I set it up and use it? Then, for reference: every option, message and limit. |
| `CHANGELOG.md` | a user upgrading | What changed in this version? |
| `CONTRIBUTING.md` | a contributor | How do I build, test and change it? Which rules apply? |
| `docs/development/architecture.md` | a contributor | How does amisqueeze work, and why this way? |
| `docs/development/README.md` | the maintainer, a contributor | What is left to do, in which order? Where does each step stand? |
| `docs/development/phase-*.md` | the same | What is each step: its goal, its success criterion, how to check it? |
| `docs/development/reviews/` | the maintainer | What did a review find, and what became of each finding? |
| commit messages | anyone reading the history | What was done, how was it checked, and why? |

**Sentences**

- One idea per sentence, ideally under 25 words. More than two items make a list.
- One level of parentheses at most. No chain of colons and semicolons: split the sentence.
- Start a section with what the reader needs most: what to do, or the conclusion. Details and reasons come after.
- In user documents, name things as the user sees them: a menu, a message, an option, not the code behind them.

**Content**

- The common case first. Exceptions and error cases go to their own place; in the user guide: Messages,
  Troubleshooting or Known limits.
- Each fact in one place, a link elsewhere. The README sums up and links; it does not repeat the user guide.
- What was tried (machines, versions, servers) goes in one table, not into the running text.
- Why something behaves as it does (a protocol detail, a server's quirk) goes in `architecture.md`, unless the
  user needs it to act.
- Before adding a paragraph, ask whether the reader needs it. If not, link to where it is, or leave it out.

**History**

- A document describes the present. The past lives in the CHANGELOG, the review reports and the commits.
- A finished step shrinks in the plan to its status, its date and one line. How it was checked goes in the commit
  message.
- A published version is one row of the roadmap, with a link to the CHANGELOG.
- A decision is written once, briefly, with its date: "(agreed with the user on 2026-10-23)". Not how it was
  reached.

Text that ships in the archives (the README, the user guide) also follows [Text for the Amiga](#code-rules).

## Testing in WinUAE

- Enable WinUAE's networking emulation: `bsdsocket_emu=true`. WinUAE then provides `bsdsocket.library` itself,
  through the host's network stack: no TCP/IP stack to install in the Amiga, and a server reachable from the host
  is reachable from the Amiga (`127.0.0.1` is the host's loopback).
- Mount `out/` as an Amiga volume (`filesystem2=rw,<DEVICE>:<Volume>:<path to out>,-128`), then run the program
  from a Shell, e.g. `NM:amisqueeze SERVER=127.0.0.1 VERBOSE` with the volume mounted as `NM:`. In a WinUAE
  configuration file (syntax checked in WinUAE's `cfgfile.cpp`), `chipmem_size` counts in 512 KB units and
  `fastmem_size` in MB.
- Kickstart ROMs are licensed: never in the repository.
- The test machine (a configuration template in `winuae/`), its profiles (a slow 68020, a fast one like a
  PiStorm, with or without FPU), the AHI units, driving the Amiga from the host (serial shell, amiagent), the
  server side, the CPU load and the test sounds (`tools/make-test-audio.sh`):
  [docs/development/testing.md](docs/development/testing.md); the scenarios run from the host before a release
  (`python3 tools/winuae/run-scenarios.py`), which print OK or the reason of a failure:
  [docs/development/winuae-scenarios.md](docs/development/winuae-scenarios.md).

### Checks the host tests cannot make

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
- **`OUTPUT` comparisons** (`tools/pcm-compare.py`, byte by byte): start playback with a single `play`, and compare
  only once the output has drained (`output: end of playback` with `VERBOSE`) and amisqueeze has stopped. Good
  references: a counter pattern in FLAC (decoded losslessly by LMS), MP3s decoded by the server's own `sox`. Make
  test MP3s without an ID3v1 tag (`lame --id3v2-only`): LMS strips it before converting, and `sox` then drops the
  last frame.

## Releasing

**Versions**: 0.x until the main feature works; 1.0 is the first really usable version, tried on the real machine.
The version changes only for a published release. Between releases the number reads `<next>-dev`, so that a build
in between is not taken for the last release. The number appears in the two `$VER:` strings of `amiga.c`
(`amisqueeze` and `amisqueeze.fpu`), in `Firmware=` of the `HELO` capabilities (`slimproto.c`) and in the user
guide; `tools/check-version.py` checks that they agree (see [Host build and tests](#host-build-and-tests)). The
`$VER:` date is the release date.

**`CHANGELOG.md`** follows [Keep a Changelog](https://keepachangelog.com/): each user-visible change goes under
"Unreleased" as it is made; at the release, that part gets the version number and the date.

**A release** is a Git tag `amisqueeze-<version>` and a GitHub Release with the zip and the `.lha` (the same
files: see `make dist` in [Building](#building)), the `.sha256` of each, the Aminet `.readme`, and short notes
holding each archive's SHA-256.

**Before the tag**, on the build to be tagged:

1. a review of the release with Claude Code (plan step publish-5, the tools in `.claude/`): a light review of the
   range since the previous tag, `/release-review <previous tag>` (three agents, limited to the diff and what it
   reaches); at milestones only (before 1.0, after a big change of architecture such as Music Assistant or
   splitting `stream.c`), a full audit, `/release-review <previous tag> full` (four agents, the whole repository).
   Its report goes in `docs/development/reviews/`. Findings scored 75 or more are fixed, or accepted by the
   maintainer with the reason in the report; an urgent fix may ship after a review of its own diff. Remarks that
   come after a version's review (outside readings, new ideas) go to the next version, unless they show a real
   defect or the maintainer chooses otherwise;
2. `python3 tools/winuae/run-scenarios.py` all OK in WinUAE (JIT, 68882 with `compfpu`):
   [winuae-scenarios.md](docs/development/winuae-scenarios.md);
3. `Version <exe> FULL` for both executables in WinUAE (see `$VER:` in [Pitfalls](#pitfalls));
4. the stack measured again, when [Stack and memory](#stack-and-memory) says so;
5. the "Unreleased" part of `CHANGELOG.md` given the version and the date, the version number set everywhere
   (`make dist` refuses anything else).

**The archives are made by GitHub from the tag** (`.github/workflows/release.yml`, since 0.7): pushing a tag
`amisqueeze-<version>` runs `make dist` through the Docker way of `tools/amiga-make.sh` (the pinned image), checks
that the archives bear the tag's version, and makes a **draft** pre-release with the archives, their `.sha256` and
the readme, its notes holding the SHA-256 lines and a place for the rest. The executables' SHA-256 are in the job's
summary: the build is reproducible, so they must be those of the build tried in WinUAE before the tag. The draft is
published by hand once its notes are written and approved, and, when the making of the archive changed since its
last trial on the Amiga (the image or the version of `lha`, the options of `make-dist.py`, a new kind of file in
it), once the draft's own `.lha` has been extracted by LhA in WinUAE: the files there with their protection bits,
the executables' SHA-256 those of the build tried before the tag, `Version <exe> FULL` for both (the scenarios
need not run again: the executables are the same); otherwise the checks of `make dist` are enough. Run by hand
(Actions, "release", "Run workflow"), the workflow is a trial: the development archives in a draft marked never to
publish, to delete afterwards (a draft's tag is created only when it is published).

**Aminet** (from 1.0): the files are renamed without the version (Aminet's rule), and the readme's `Uploader:`
gets an e-mail address; the `.lha` to be uploaded is first extracted by LhA in WinUAE, the scenarios run on what
it gives.

## Debugging

No source-level debugging yet. Meanwhile, WinUAE's built-in debugger (`Shift+F12`) follows the program at the
assembly level. Planned (step tools-8): `bgdbserver` (bebbo, Aminet `dev/debug/bgdbserver`) in the emulated Amiga,
with WinUAE's `bsdsocket.library` emulation, and `m68k-amigaos-gdb` (`target remote …:2345`) driven by a cpptools
`launch.json`; build with `-g`. To check first: that WinUAE's port 2345 can be reached from wherever gdb runs.
Ruled out: uae-dap (prb28), which needs SAS/C `LINE DEBUG` hunks, and Bartman's amiga-debug (its own toolchain,
NDK 3.9, no libc).

## VS Code

Open each repository on its own: the task names are the same in every repository, so that shortcuts can target
them by name.

Recommended extensions: `ms-vscode.cpptools` (C IntelliSense, using the headers in `.sdk/`),
`prb28.amiga-assembly` (68k assembly highlighting) and `spencerwmiles.vscode-task-buttons` (task buttons).
Behind a corporate proxy that inspects TLS, `code --install-extension` may need `NODE_EXTRA_CA_CERTS` pointing to
the proxy's root certificate (exported as PEM).

Tasks (`.vscode/tasks.json`, calling `tools/amiga-make.sh`, through WSL on Windows): **Amiga: build**
(`Ctrl+Shift+B`), **Amiga: clean**, **Amiga: sync headers (.sdk)**. Status bar buttons are set in
`.vscode/settings.json` (Task Buttons extension). Keyboard shortcuts can target tasks by name
(`workbench.action.tasks.runTask`).

## Pitfalls

- **Protocol references**: squeezelite (`slimproto.c`, `slimproto.h`, `stream.c`, `pcm.c`,
  https://github.com/ralph-irving/squeezelite) and LMS (`Slim/Networking/Slimproto.pm`). They are GPL: read to
  understand the protocol's behaviour (message layouts, when to send which status), never copied (the project is
  MIT).
- **The compiler**: gcc 16.1.1b generated wrong code, hence the safeguard of [Building](#building).
- **vamos** (shipped with the `amigadev/crosstools` image) runs command-line tools that only use dos.library: it
  crashes on `printf` (libnix), has no windows and no real `bsdsocket.library` (its stub makes amisqueeze retry
  forever: kill it). Run 68020 programs with `vamos -C 68020` (otherwise alert `00068020`). Its `GetVar` ignores
  global variables (`ENV:`) and returns 0, so default options cannot be tested with it. Useful for argument
  parsing only: test the rest in WinUAE.
- **`-m68881` and libnix**: libnix links a start-up check (`__fpucheck`) that, without an FPU, shows an alert and
  quits before `main()` without a message: `amiga.c` replaces it with an empty one and checks `AttnFlags` itself.
  The `-m68881` code uses `fintrz` and `fmovecr`, emulated (slowly) by the CPU library of a 68040 or 68060.
- **The `$VER:` string** stays non-const, in the data hunk (`tools/check-version.py` checks it). Const, it sat in
  the code hunk, and AmigaOS 3.2's `Version` printed "amiseeze 0.3" for the 0.3 build although the bytes were
  right (only when reading the file as an executable). Hence `Version <exe> FULL` for both executables before
  each release.
- **Workbench start and libnix**: started from the Workbench, libnix opens the console named by `__stdiowin`
  before `main()`; `amiga.c` sets it to NULL and opens its own once it has read the icon (`WINDOW`, `QUIET`). A
  `CON:` window needs `SMART`, or it loses its text when uncovered. With `QUIET` there is no output (`msg()`
  checks `Output()`), except during start-up (`main()`, up to `bsdsocket.library`): a message then opens the
  window (`MAC` and `OUTPUT`, checked by the core, go to the log only).
- **The commodity**: amisqueeze is a commodity ("Remove" in Exchange = Ctrl-C), one at a time (`CBERR_DUP`); a
  second start does nothing to the running one.
- **LMS drops every status after a `strm s` until `STMc`**: a refusal must come after a `STMc`, or LMS waits for
  that track forever ([architecture](docs/development/architecture.md#a-tracks-life-the-statuses)).
