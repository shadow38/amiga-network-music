# Building

For contributors: what the build needs, how to build amisqueeze and its archives, how the repository is laid out,
and the editor's set-up. The tests are in [testing.md](testing.md), the code rules in [code-rules.md](code-rules.md).

## In short

```sh
sh tools/amiga-make.sh              # out/amisqueeze and out/amisqueeze.fpu
sh tools/amiga-make.sh dist DEV=1   # the archives of a development version, to try them
sh tools/amiga-make.sh clean
```

On Windows, go through WSL (default distribution), from the repository directory in PowerShell:

```powershell
wsl --cd $PWD --exec sh tools/amiga-make.sh
```

`tools/amiga-make.sh` takes the same arguments as `make`. With the toolchain in the `PATH`, plain `make` works
too: `make all`, `make clean`, `make sdk`, or `make <prog>`.

## Requirements

- the **`m68k-amigaos-gcc`** cross-compiler (AmigaPorts, GCC 16.2 or newer);
- NDK 3.2 (v47 headers, including `bsdsocket` and AHI) and libnix;
- `make`;
- `python3`, for the `tools/check-saves.py` safeguard.

The host build and its tests need a native compiler and lizard too: [testing.md](testing.md#requirements).

### Getting the toolchain

Two ways:

- **a native install** (Linux, macOS, WSL on Windows): `m68k-amigaos-gcc` in the `PATH`;
- **Docker**: the [`amigadev/crosstools`](https://hub.docker.com/r/amigadev/crosstools) image, tag
  `m68k-amigaos-gcc10`.

About the Docker image:

- The tag's name is historical: it follows the AmigaPorts toolchain, now GCC 16.
- The toolchain is installed in `/opt/m68k-amigaos`. The image also ships gdb, vasm and vamos.
- The tag moves when the image is rebuilt. So `tools/amiga-make.sh` pins it by its digest, in `REFERENCE_IMAGE`:
  the toolchain the releases are built with.

### Moving to a newer image

1. Put its digest in `REFERENCE_IMAGE`.
2. Build, then run `make test` and the scenarios in WinUAE.
3. Compare the executables' SHA-256 printed by the CI with those of a local build.

## How `tools/amiga-make.sh` finds the toolchain

It takes the first of these that is available:

1. **`$AMIGA_MAKE`**, if set: a local script run instead. It receives the `make` arguments.
2. **`m68k-amigaos-gcc` in the `PATH`**: plain `make`.
3. **`../amiga-make.local.sh`**, next to the repository and outside Git, if it exists. Same role as `$AMIGA_MAKE`,
   for a set-up specific to one machine (container, remote host…).
4. **Docker**: `docker run` of the `$AMIGA_IMAGE` image, with the repository mounted on `/work`. By default, the
   reference image, pinned by its digest. The CI builds this way on each push (see
   [Builds made by GitHub](#builds-made-by-github)).

Run `make sdk` (task **Amiga: sync headers (.sdk)**) after installing or updating the toolchain. It copies the
headers for IntelliSense.

## What is built

amisqueeze is built twice:

| Executable | For | With |
|---|---|---|
| `out/amisqueeze` | 68020, no FPU | |
| `out/amisqueeze.fpu` | 68020 and 68881 | the MP3 decoder (`AMISQUEEZE_FPU` and `AMISQUEEZE_MP3`) |

- Each program's options live in the `Makefile` (`<prog>_CFLAGS`). Network programs target the 68020 (`-m68020`).
- The host build has the MP3 decoder too ([testing.md](testing.md#host-build-and-tests)).
- Header dependencies are tracked (`-MMD -MP`, `obj/<prog>/*.d` files).

## The safeguard against a compiler bug

Each `.c` file is first compiled to assembly (`obj/<prog>/*.s`), which `tools/check-saves.py` checks before
assembling. The build fails if a function changes a register the caller expects to be kept without saving it:
d2-d7, a2-a6, and fp2-fp7 in `amisqueeze.fpu`.

**Why**: `m68k-amigaos-gcc` 16.1.1b had that bug (image `wischner/gcc-m68k-amiga:1.1.0`). Programs crashed with an
*address error* `#80000003`.

- No compiler option avoided it: `-O1`, `-Os`, `-O2`, `-mlra`, `-mno-lra`.
- It was fixed in 16.2-rc16. But the GCC 16 port is still young: when a crash makes no sense, look at the
  generated assembly.

**Its own test**: `make test` runs it on `tests/check-saves.s`. That file holds a few functions with such a fault
(a plain write, `(an)+`, `-(an)`, `dbra`, an FPU register) and a few without.

**What it does not see**: it takes every `movem`/`fmovem` other than a push, and every `(sp)+` pop, for a restore.
So a register loaded that way without having been saved is not seen. The compiler loads them only to restore what
it saved.

## The archives (`make dist`)

`make dist` (`sh tools/amiga-make.sh dist`) builds the release archives `out/amisqueeze-<version>.zip` and
`out/amisqueeze-<version>.lha`.

**What they hold**: the same files in both, in an `amisqueeze-<version>/` directory:

- the Amiga executables and their icons;
- `LICENSE`, the README and the changelog;
- the user guide, with its images (`docs/images/`).

**Beside them**:

- `<archive>.sha256`: the archive's SHA-256, in the format of `sha256sum` (checked with `sha256sum -c`);
- `out/amisqueeze-<version>.readme`: the Aminet readme, `dist/amisqueeze.readme` with its version filled in.

**The `.lha`** needs `lha` (LHa for UNIX, [jca02266/lha](https://github.com/jca02266/lha)), which the image
`amigadev/crosstools` has.

- It is made with method `-lh5-` and header level 1: what LhA 2.15 writes on the Amiga.
- It is then tested (`lha t`), and its files and sizes compared with the zip's.

**The readme** is checked against Aminet's rules: the fields, `Short:` of 40 characters at most, lines of 78 at
most, ASCII, LF. `make test` checks it too.

**What `make dist` refuses**: it builds nothing unless `tools/check-version.py --release` accepts the version.

- not a `-dev` version;
- the same version in both `$VER:` strings, `Firmware=` and the user guide;
- its section at the top of the changelog, dated as the `$VER:` strings;
- nothing left under "Unreleased".

It also refuses a file that is not ASCII with LF line endings
([Text for the Amiga](code-rules.md#the-rules)): a text file of the archive (`LICENSE`, the README, the
changelog, the user guide), a source of amisqueeze or of `tools/amiga/`. `make test` checks them too.

**For a release**:

- Before it, the "Unreleased" part of `CHANGELOG.md` gets the version number and the date.
- Each run makes new archives, with new dates and so new SHA-256. Attach the archives, their `.sha256` and the
  readme of the same run.
- `make dist` ends with the lines that give the SHA-256 in the release notes: copy that run's lines.
- The published archives are made by GitHub from the tag (`.github/workflows/release.yml`, see
  [releasing.md](releasing.md)).

**To try them**: `make dist DEV=1` accepts a development version (`out/amisqueeze-<x.y>-dev.*`, never published).

## The workflows

- Both workflows pin their actions by commit SHA, the version in a comment, as `tools/amiga-make.sh` pins the
  image by its digest. To update one, take the commit of its new tag.
- Their checkout keeps no token (`persist-credentials: false`), since the build's container mounts the whole
  repository.

## Builds made by GitHub

On each push and pull request, `.github/workflows/tests.yml` runs two jobs: `make test`
([testing.md](testing.md#on-github)), and the Amiga build.

**The Amiga build** runs `sh tools/amiga-make.sh all cpucount dist DEV=1`, which takes the Docker way on GitHub's
machine. It builds:

- both Amiga executables and `cpucount`;
- the archives in their development form.

It then prints the executables' SHA-256: a local build from the same image gives the same.

**Its archives** are kept for 14 days as the run's artifacts:

- the `.lha` and the `.zip`, each under its own name, not wrapped in another zip;
- on the run's page, for a signed-in GitHub user;
- a development build to try without building it. Its version is `<next>-dev`, or on a release commit, the
  release's own number.

The latest build of the version in development: the latest run of
[`tests` on `dev`](https://github.com/shadow38/amiga-network-music/actions/workflows/tests.yml?query=branch%3Adev).

**With the GitHub CLI**, not `gh run download`: it takes every artifact for a zip to unpack, and fails on the
`.lha` (gh 2.102). Instead:

```sh
gh api repos/shadow38/amiga-network-music/actions/runs/<run id>/artifacts    # the ids
gh api repos/shadow38/amiga-network-music/actions/artifacts/<id>/zip > <file>
```

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
tools/amiga-make.sh   runs make with whichever toolchain is available (see above)
tools/check-saves.py  checks the generated assembly (the safeguard against a compiler bug, see above)
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

- `obj/`, `out/`, `obj-host/`, `out-host/` and `.sdk/` are generated, and ignored by `.gitignore`.
- The plan, `docs/development/README.md` and its `phase-*.md`, is the project's working plan: steps, rules,
  review reports, which commit messages refer to. It is not needed to use or build the programs.

## VS Code

Open each repository on its own: the task names are the same in every repository, so that shortcuts can target
them by name.

**Recommended extensions**:

- `ms-vscode.cpptools`: C IntelliSense, using the headers in `.sdk/`;
- `prb28.amiga-assembly`: 68k assembly highlighting;
- `spencerwmiles.vscode-task-buttons`: task buttons.

Behind a corporate proxy that inspects TLS, `code --install-extension` may need `NODE_EXTRA_CA_CERTS` pointing to
the proxy's root certificate (exported as PEM).

**Tasks** (`.vscode/tasks.json`): they call `tools/amiga-make.sh`, through WSL on Windows.

- **Amiga: build** (`Ctrl+Shift+B`), **Amiga: clean**, **Amiga: sync headers (.sdk)**;
- status bar buttons are set in `.vscode/settings.json` (Task Buttons extension);
- keyboard shortcuts can target tasks by name (`workbench.action.tasks.runTask`).
