# Building

For contributors: what the build needs, how to build amisqueeze and its archives, how the repository is laid out,
and the editor's set-up. The tests are in [testing.md](testing.md), the code rules in [code-rules.md](code-rules.md).

## Requirements

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

The host build and its tests need a native compiler and lizard too: [testing.md](testing.md#requirements).

## Building

With the toolchain in the `PATH`, `make` is enough (`make all`, `make clean`, `make sdk`, or `make <prog>`).

`tools/amiga-make.sh` (same arguments as `make`) picks the available toolchain, in this order:

1. **`$AMIGA_MAKE`**, if set: a local script run instead (it receives the `make` arguments);
2. **`m68k-amigaos-gcc` in the `PATH`**: plain `make`;
3. **`../amiga-make.local.sh`** (next to the repository, outside Git), if it exists: same role as `$AMIGA_MAKE`,
   for a setup specific to one machine (container, remote host…);
4. **Docker**: `docker run` of the `$AMIGA_IMAGE` image (default: the reference image, pinned by digest), with
   the repository mounted on `/work`. The CI builds this way on each push (see
   [Builds made by GitHub](#builds-made-by-github)).

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
`-dev` one, the same in both `$VER:` strings, `Firmware=` and the user guide, with its section at the top of the
changelog, dated as the `$VER:` strings, and nothing left under "Unreleased". It also refuses a text
file of the archive (`LICENSE`, the README, the changelog, the user guide), a source of amisqueeze or of
`tools/amiga/` that is not ASCII with LF line endings ([Text for the Amiga](code-rules.md#the-rules)); `make test`
checks them too.

Both workflows pin their actions by commit SHA, the version in a comment (as `tools/amiga-make.sh` pins the image
by its digest; to update one, take the commit of its new tag), and their checkout keeps no token
(`persist-credentials: false`), since the build's container mounts the whole repository. The published archives
are made by GitHub from the tag (`.github/workflows/release.yml`, see [releasing.md](releasing.md)).

Per-program options live in the `Makefile` (`<prog>_CFLAGS`): network programs target the 68020 (`-m68020`).
amisqueeze is built twice: `out/amisqueeze` (68020, no FPU) and `out/amisqueeze.fpu` (68020 + 68881, with the MP3
decoder, `AMISQUEEZE_FPU` and `AMISQUEEZE_MP3`); the host build has the MP3 decoder too.
Header dependencies are tracked (`-MMD -MP`, `obj/<prog>/*.d` files).

**Safeguard:** each `.c` file is first compiled to assembly (`obj/<prog>/*.s`), which `tools/check-saves.py`
checks before assembling. The build fails if a function modifies a register the caller expects to be preserved
(d2-d7, a2-a6, and fp2-fp7 in `amisqueeze.fpu`) without saving it. This was a bug in `m68k-amigaos-gcc`
16.1.1b (image `wischner/gcc-m68k-amiga:1.1.0`): programs crashed with an *address error* `#80000003`. It is fixed
in 16.2, but the GCC 16 port is recent. `make test` checks the safeguard itself on `tests/check-saves.s`: a few
functions with such a fault (a plain write, `(an)+`, `-(an)`, `dbra`, an FPU register) and a few without. It
takes every `movem`/`fmovem` other than a push, and every `(sp)+` pop, for a restore. A register loaded that way
without having been saved is not seen; the compiler loads them only to restore what it saved. No
compiler option avoided the bug (`-O1`, `-Os`, `-O2`, `-mlra`, `-mno-lra`); it was fixed in 16.2-rc16. The GCC 16 port is still young: when a crash
makes no sense, look at the generated assembly.

## Builds made by GitHub

On each push and pull request, beside `make test` ([testing.md](testing.md#on-github)), a second job of
`.github/workflows/tests.yml` builds both Amiga executables, `cpucount` and the archives in their development
form (`sh tools/amiga-make.sh all cpucount dist DEV=1`, which takes the Docker way on GitHub's machine), then
prints the executables' SHA-256: a local build from the same image gives the same. Its `.lha` and `.zip` are kept
for 14 days as the run's artifacts, each under its own name and not wrapped in another zip (on the run's page, for
a signed-in GitHub user): a development build to try without building it, its version `<next>-dev` (on a release
commit, the release's own number). The latest build of the version in development: the latest run of
[`tests` on `dev`](https://github.com/shadow38/amiga-network-music/actions/workflows/tests.yml?query=branch%3Adev). With the GitHub CLI, not `gh run download`, which
takes every artifact for a zip to unpack (it fails on the `.lha`, gh 2.102), but
`gh api repos/shadow38/amiga-network-music/actions/artifacts/<id>/zip > <file>`,
the ids given by `gh api repos/shadow38/amiga-network-music/actions/runs/<run id>/artifacts`.

## Repository layout

```
src/<prog>/           C sources, one directory per program -> out/<prog>; code shared between programs will go
                      in src/common/ (with amilms, none yet)
src/amisqueeze/       portable core (player, slimproto, stream, icy, ring, audio, decoders: decoder, flac, mp3,
                      bench), system services behind platform.h: amiga.c and amiga_ahi.c (AmigaOS, Amiga build
                      only), posix.c (Linux/POSIX, host build only)
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
CLAUDE.md             for Claude Code: how to work with the maintainer, pointers to the documents
docs/user-guide.md    user guide
docs/development/     the documents for contributors (see the map in CONTRIBUTING.md) and the development plan
                      (README.md: roadmap, steps, design decisions; phase-*.md; reviews/: the review reports)
winuae/               WinUAE test machine (a template: amisqueeze-test.uae)
CONTRIBUTING.md       for contributors: the map of the documents, a quick start, proposing a change
CHANGELOG.md          what changed in each release (updated with each one)
Makefile              build (m68k-amigaos-gcc must be in the PATH; HOSTCC for the host build)
tools/amiga-make.sh   runs make with whichever toolchain is available (see "Building")
tools/check-saves.py  checks the generated assembly (safeguard against a gcc bug, see "Building")
tools/check-stack.py  checks the linker map: libnix's stack swap linked, the stack's size (see code-rules.md)
tools/check-core.py   checks that the portable core includes no header of the system (make test, code-rules.md)
tools/check-quality.py checks the C code's size and complexity with lizard, and that no step name is left in the
                      code (make test, see code-rules.md)
tools/requirements.txt the Python packages of make test (lizard), pinned with their hashes
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

`obj/`, `out/`, `obj-host/`, `out-host/` and `.sdk/` are generated (ignored by `.gitignore`). The plan,
`docs/development/README.md` and its `phase-*.md`, is the project's working plan (steps, rules, review reports,
which commit messages refer to): not needed to use or build the programs.

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
