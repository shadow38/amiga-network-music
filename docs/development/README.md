# Development plan

The plan of Amiga Network Music: the phases, split into testable steps, and the [roadmap](#roadmap) of the
versions that bring them. Each step has a goal, an **observable** success criterion and a way to check it.

| Phase | Content | Document |
|---|---|---|
| 0 | Tooling and test environment | [phase-0-tooling.md](phase-0-tooling.md) |
| 1 | amisqueeze: slimproto player with AHI output | [phase-1-amisqueeze.md](phase-1-amisqueeze.md) |
| 2 | amilms (working name, amiremote proposed): remote control for LMS, then Music Assistant (MPD: an idea) | [phase-2-amilms.md](phase-2-amilms.md) |
| 3 | Release | [phase-3-release.md](phase-3-release.md) |

Steps are named after their phase: `tools-<n>` (phase 0), `player-<n>` (1), `remote-<n>` (2), `publish-<n>` (3),
with a letter for a part (`player-7b`). A step name never reads as a version: 0.7 is a version, `tools-7` a step.
Review reports and commits before 2026-10-23 use the old numbers, `<phase>.<n>` (`1.7b` for `player-7b`).

How to build and test: [CONTRIBUTING.md](../../CONTRIBUTING.md); testing in WinUAE: [testing.md](testing.md).

## Method

- **One step at a time.** The content and success criterion of a step are agreed **before** writing code. A
  finished step: criterion checked, documentation up to date, one commit.
- **Observable criteria.** "It compiles" is not a criterion. The player shows up in LMS, the progress bar moves,
  the test tone sounds right: these are.
- **Three test levels**, from the most automated to the most manual:
  1. **on the host**: the portable code compiled natively and tested against examples from squeezelite and LMS; a
     fake server replays scenarios;
  2. **in WinUAE**: emulated networking, a test LMS server, the program's log readable from the host;
  3. **on the real machines** (PiStorm, 68020 accelerator): at the end of each phase only (listening, stability,
     performance).
- **Reference.** squeezelite, the reference player for the protocol, runs alongside on the PC. It shows how the
  server and the messages are expected to behave.
- **Only promise what works.** The player advertises to the server (`HELO`) only the formats it can play.

## Statuses

| Symbol | Meaning |
|---|---|
| ✅ | done and validated (date) |
| 🔶 | partly done, or done provisionally |
| ⬜ | to do |
| ⏸ | out of scope for now |

## Roadmap

Which version of amisqueeze brings which steps. What can be tested from the development machine comes first; what
needs the real Amiga and a home network comes last (agreed with the user on 2026-10-20). Each version's content is
agreed with the user.

| Version | Brings | Steps | Status |
|---|---|---|---|
| 0.1 to 0.7 | the player for LMS and Music Assistant: PCM, FLAC, MP3, start from the Workbench, the `.lha`, releases built by the CI | player-1 to player-8d, player-7b, part of player-10, tools-10 (LMS), publish-2 (`.lha`), publish-3 | ✅ 2026-10-19 to 2026-10-23, see the [changelog](../../CHANGELOG.md) |
| 0.8 | what the real machine shows; the writing rules applied to the documentation | player-9, tools-11, publish-6 | ⬜ |
| 0.9 | internet radios' metadata (ICY), automatic server discovery | player-10 (end) | ⬜ |
| 1.0 | setup guides, a full audit, the upload to Aminet | publish-4, publish-5, tools-10 (Music Assistant), publish-2 (Aminet) | ⬜ |
| none yet | ideas: direct Paula output, source-level debugging | player-11, tools-8 | ⬜ |
| none | reference documents on AmigaOS for contributors, committed when ready (agreed with the user on 2026-10-24) | tools-12 | ⬜ |

The remote (remote-0 to remote-12) will have versions of its own, its name chosen by its study (remote-0). It
comes after these releases, or alongside if the user wants it.

### 0.8

- **The real machine** (player-9): 0.7 tried with LMS and Music Assistant, following the step's protocol. Only if
  it shows gaps: more AHI blocks, or a higher task priority.
- **Paula's LED filter**: the user guide says that `paula.audio` turns it off while playing, once the real machine
  confirms it (seen in WinUAE on 2026-10-23; agreed with the user on 2026-10-23).
- **The messages Music Assistant sends and amisqueeze ignores** (`vers`, `grfb`, `grfe`, `codc`, `cont`): they
  read as errors, and the user guide calls `... not handled` worth reporting (seen on 2026-10-23).
- **The 50 list of the [0.7 review](reviews/0.7.md#5-the-50-list)**, except the items of 0.9 below: what to do
  with each item is agreed with the user when 0.8 is planned.
- **The writing rules** (publish-6): the plan, then the user guide, rewritten to
  [Writing documentation](../../CONTRIBUTING.md#writing-documentation) (agreed with the user on 2026-10-24).

### 0.9

- **Internet radios' metadata** (ICY, player-10). For a radio played directly by `amisqueeze.fpu`, LMS adds 2 to
  autostart, asks the radio for `Icy-MetaData: 1`, then sends the blocks' interval in `cont`. amisqueeze does not
  remove the blocks yet, and minimp3 loses its sync at each one. To do: remove them (the interval from `cont` or
  the `icy-metaint` header), a fake server scenario, a check with a real radio.
- **Automatic server discovery** (player-10), after the real network: the test LMS runs in a container that the
  emulated Amiga's UDP broadcast does not reach.
- **From the 0.7 review's 50 list** (agreed with the user on 2026-10-23):
  - the HTTP status line is never checked;
  - the body of a direct stream plays before LMS's `cont`, so a redirect or an error page plays as a track;
  - the 88.2 kHz rate code is refused as an unsupported format;
  - the polarity and output channel flags, transitions and replay gain are ignored, without a line in the guide.

### 1.0

- **Before it**: the setup guides (publish-4), a full audit (publish-5), the Music Assistant scenario in WinUAE
  (tools-10).
- **Then**: the upload to Aminet (publish-2), once the `Author:` and `Uploader:` of the `.readme` are corrected.
- **Perhaps**: an attestation of the archives' provenance (`actions/attest-build-provenance`). Left for 1.0 by the
  user on 2026-10-23: of little use to Amiga users, since the reproducible build already lets anyone check.

## Where we are

| Step | Status |
|---|---|
| tools-1 Toolchain | ✅ 2026-10-18 |
| tools-2 Test LMS server | ✅ 2026-10-18 |
| tools-3 Reference player | ✅ 2026-10-18 |
| tools-4 WinUAE test configuration | 🔶 2026-10-22: the template and its slow and JIT profiles ([testing.md](testing.md)); a 68040 profile still open |
| tools-5 AHI in the emulated Amiga (Paula, Toccata) | ✅ 2026-10-18 |
| tools-6 Host-side tests (`make test`) | ✅ 2026-10-18 |
| tools-7 Automated runs in WinUAE | ✅ 2026-10-22 |
| tools-8 Source-level debugging (optional) | ⬜ |
| tools-9 Test Music Assistant server | 🔶 2026-10-21: the players side; the API token with remote-8 |
| tools-10 Regression scenarios in WinUAE | 🔶 2026-10-23: the LMS scenarios ([winuae-scenarios.md](winuae-scenarios.md)); Music Assistant before 1.0 |
| tools-11 A real TCP/IP stack in WinUAE (AmiNetXDuo, PCMCIA card) | ✅ 2026-10-24 |
| tools-12 Amiga reference documents (`docs/amiga/`) | ⬜ agreed with the user on 2026-10-24; tools-12a (AHI, network) first |
| player-1 Control connection | ✅ 2026-10-18 |
| player-1b Default options (`ENV:amisqueeze.prefs`) | ✅ 2026-10-18 |
| player-2 Receiving the stream (virtual output) | ✅ 2026-10-18 |
| player-3 Output to a file | ✅ 2026-10-18 |
| player-4 AHI audio output | ✅ 2026-10-19 |
| player-5 Commands and volume | ✅ 2026-10-19 |
| player-6 Robustness | ✅ 2026-10-19 |
| player-7a Other PCM formats (LMS) | ✅ 2026-10-19 |
| player-7b Music Assistant | ✅ 2026-10-23 |
| player-8 Local decoding (FLAC, then MP3) | ✅ 2026-10-19 |
| player-8d MP3 without gaps between tracks | ✅ 2026-10-20 |
| player-9 Real machine | ⬜ with 0.7, for 0.8 |
| player-10 Finishing touches | 🔶 the parts of 0.4 and 0.5 done; ICY and server discovery in 0.9 |
| player-11 Direct Paula output, without AHI | ⬜ idea (CPU load on a slow 68020) |
| remote-0 Study (common interface, Music Assistant feasibility, name) | ⬜ before any amilms code |
| remote-1 to remote-7 LMS | ⬜ |
| remote-8 to remote-11 Music Assistant | ⬜ |
| remote-12 MPD | ⬜ idea, after Music Assistant |
| publish-1 Licence and user documentation | 🔶 MIT licence; the user guide kept up to date |
| publish-2 Aminet archive | 🔶 2026-10-23: the `.lha` and its Aminet `.readme`; the upload with 1.0 |
| publish-3 Public versions | 🔶 0.1 to 0.7 published; the CI tests, builds and drafts the release from a tag |
| publish-4 Setup guides: minimal server, the Amiga on the network | ⬜ before 1.0 |
| publish-5 Review before each release | ✅ 2026-10-20; reviews since: [0.6](reviews/0.6.md), [0.7](reviews/0.7.md) |
| publish-6 Writing rules for the documentation | 🔶 2026-10-24: the rules, the plan and the user guide; `docs/development/` next, where the user finds it worth it |

Phase 0 moves forward as needed: a tooling step is done when a step of phase 1 or 2 needs it. Phases 1 and 2 can
progress in parallel.

## Open points

Neither planned nor dropped for good. Each is taken up only by the user's decision.

- **`BUFFER` option**: the 1 MB stream buffer is fixed. Only if the real machines show a need (player-9: memory on
  a small Amiga, Wi-Fi), then in player-10 (agreed with the user on 2026-10-19).
- **64-bit multiplies in dr_flac**: slow on a 68060, whose CPU library emulates them. Dropped (agreed with the user
  on 2026-10-19): no 68060 among the target machines, and `FLAC=AUTO` measures the speed. To look at again if a
  68060 user reports a problem.
- **Ctrl-C with a real TCP/IP stack**: `WaitSelect()` may take the signal for itself, depending on the stack's
  break mask. To check on the real machine (player-9): `SBTC_BREAKMASK`, or EINTR taken as a stop.
- From the outside review of 2026-10-20, no known failure:
  - the track queue (4 entries) forgets the oldest one when full; a refusal (`STMn`) would be safer;
  - `gethostbyname()`: no check of an empty address list, or of a length other than 4;
  - `PORT` and `UNIT` have no range check (`PORT=70000`, `UNIT=255`);
  - the default options are read on one line of at most 510 characters; the rest is dropped silently;
  - malformed server input (cut frames, `strm` fields out of range, headers too long) is not tested yet;
  - `stream.c` is the first module to split when it grows (for ICY, for instance).

## Ideas outside the phases

Related projects, not planned (noted on 2026-10-19). Each would be studied first, starting with whether it already
exists.

- **Amiga modules in the server**: tracker music (MOD, MED) played through LMS or Music Assistant on every player
  in the house, with a conversion rule (`openmpt123`) or a server plugin.
- **A network AHI driver**: an AHI audio mode that sends the Amiga's sound to the network (Snapcast, or an HTTP
  stream for LMS), so that every AHI program plays on the house's speakers. Games and demos would not: they drive
  Paula directly.
- **A GitHub wiki for users' contributions**: a compatibility list, `BENCH` figures, a FAQ. The documentation
  itself stays in `docs/` (publish-4). To decide after 1.0, from the feedback.
- **amisqueeze on AmigaOS 4, MorphOS and AROS**: none of them has a Squeezebox player (checked on 2026-10-19). They
  share `ahi.device` and `bsdsocket.library`, and the core is standard C. Cheapest first try: AROS 68k, which runs
  the current executables. A little-endian AROS needs the byte swaps checked. A player for PCs is ruled out:
  squeezelite exists everywhere.
