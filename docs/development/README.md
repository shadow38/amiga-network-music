# Development plan

Roadmap of Amiga Network Music: the phases, split into testable steps. Each step has a goal, an **observable**
success criterion and a way to check it.

| Phase | Content | Document |
|---|---|---|
| 0 | Tooling and test environment | [phase-0-tooling.md](phase-0-tooling.md) |
| 1 | amisqueeze: slimproto player with AHI output | [phase-1-amisqueeze.md](phase-1-amisqueeze.md) |
| 2 | amilms (working name, amiremote proposed): remote control for LMS, then Music Assistant (MPD: an idea) | [phase-2-amilms.md](phase-2-amilms.md) |
| 3 | Release | [phase-3-release.md](phase-3-release.md) |

How to build and test: [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Method

- **One step at a time.** The content and success criterion of a step are reviewed and agreed **before** writing
  code. A finished step = criterion checked, documentation up to date, one commit.
- **Observable criteria.** "It compiles" is not a criterion: the player shows up in LMS, the progress bar moves,
  the test tone sounds right, etc.
- **Three test levels**, from the most automated to the most manual:
  1. **on the host**: portable code (packet serialisation, response parsing, conversions) compiled natively and
     tested against examples taken from squeezelite and LMS; a fake server replays scenarios;
  2. **in WinUAE**: emulated networking (`bsdsocket_emu`), a test LMS server, the program's log readable from the
     host;
  3. **on the real machines** (PiStorm, 68020 accelerator; Paula or another AHI output): at the end of each
     phase only (listening, stability, performance).
- **Reference.** squeezelite, the reference player for the protocol, runs alongside on the PC: it shows how the
  server and the messages are expected to behave.
- **Only promise what works.** The player advertises to the server (`HELO`) only the formats it can actually
  play.

## Statuses

| Symbol | Meaning |
|---|---|
| ✅ | done and validated (date) |
| 🔶 | partly done, or done provisionally |
| ⬜ | to do |
| ⏸ | out of scope for now |

## Where we are

| Step | Status |
|---|---|
| 0.1 Toolchain | ✅ 2026-10-18 |
| 0.2 Test LMS server | ✅ 2026-10-18 |
| 0.3 Reference player | ✅ 2026-10-18 |
| 0.4 WinUAE test configuration | 🔶 local profiles (slow 68020, fast JIT), no versioned template |
| 0.5 AHI in the emulated Amiga (Paula, Toccata) | ✅ 2026-10-18 |
| 0.6 Host-side tests (`make test`) | ✅ 2026-10-18 |
| 0.7 Automated runs in WinUAE | 🔶 serial shell and scenarios, local only |
| 0.8 Source-level debugging (optional) | ⬜ |
| 1.1 amisqueeze: control connection | ✅ 2026-10-18 |
| 1.1b amisqueeze: default options (`ENV:amisqueeze.prefs`) | ✅ 2026-10-18 |
| 1.2 amisqueeze: receiving the stream (virtual output) | ✅ 2026-10-18 |
| 1.3 amisqueeze: output to a file | ✅ 2026-10-18 |
| 1.4 amisqueeze: AHI audio output | ✅ 2026-10-19 |
| 1.5 amisqueeze: commands and volume | ✅ 2026-10-19 |
| 1.6 amisqueeze: robustness | ✅ 2026-10-19 |
| 1.7a amisqueeze: other PCM formats (LMS) | ✅ 2026-10-19 |
| 1.7b amisqueeze: Music Assistant | ⬜ after version 0.2 |
| 1.8 amisqueeze: local decoding (FLAC, then MP3) | ✅ 2026-10-19 (FLAC; MP3 in `amisqueeze.fpu`) |
| 1.8d amisqueeze: MP3 without gaps between tracks | ✅ 2026-10-20 (version 0.4) |
| 1.9 amisqueeze: real machine | ⬜ version 0.6 |
| 1.10 amisqueeze: finishing touches | ⬜ versions 0.4 to 0.6 (see below) |
| 1.11 amisqueeze: direct Paula output, without AHI | ⬜ idea (CPU load on a slow 68020) |
| 0.9 Test Music Assistant server | ⬜ after version 0.2 |
| 2.0 remote: study (common interface, Music Assistant feasibility, name) | ⬜ before any amilms code |
| 2.1 to 2.7 remote: LMS | ⬜ |
| 2.8 to 2.11 remote: Music Assistant | ⬜ after version 0.2 |
| 2.12 remote: MPD | ⬜ idea, after Music Assistant |
| 3.1 Licence and user documentation | 🔶 licence: MIT; user guide kept up to date |
| 3.2 Aminet archive | ⬜ |
| 3.3 Public versions | 🔶 0.1, 0.2, 0.3 published (tags, GitHub releases, changelog); CI: host tests |
| 3.4 Setup guides: minimal server, the Amiga on the network | ⬜ before 1.0 |

Phase 0 moves forward as needed: a tooling step is done when a step of phase 1 or 2 needs it. Phases 1 and 2 can
progress in parallel. Release 0.3 was published on 2026-10-20 (MP3 decoded by `amisqueeze.fpu`, two executables,
documentation reworked; 0.2 on 2026-10-19: FLAC, 8/24/32-bit PCM).

**Next releases** (decided with the user on 2026-10-20): what can be tested to the end from the development machine
(host tests, WinUAE) comes first, what needs the real Amiga and a home network last.

| Version | Content | Tested |
|---|---|---|
| 0.4 | MP3 without gaps (1.8d), fixed volume through LMS's own setting, the AHI mode warning (1.10): all done | host and WinUAE |
| 0.5 | `LOG=<file>`, start from the Workbench with an icon and ToolTypes (1.10) | host and WinUAE |
| 0.6 | automatic server discovery (1.10), with the real machine (1.9): the first version tried on a real Amiga | host for the code; validated on the real machine and network |

Server discovery waits for 0.6 because the test setup cannot check it: the test LMS runs in a container behind
forwarded TCP ports, which a UDP broadcast from the emulated Amiga does not cross. Each step keeps the method:
design agreed with the user, then code, tests, a line in `CHANGELOG.md`. The remote's study (2.0) comes after these
releases, or alongside if the user wants it.
Music Assistant (0.9, 1.7b, 2.8 to 2.11) was put after 0.2 (decided 2026-10-19): it is no longer waiting on a
release, when to start it is still to be decided.

## Open points (review of 2026-10-19)

Announced in the plan, then neither done nor dropped; decided with the user on 2026-10-19:

- **AHI mode warning** (1.4): a warning at start-up when the unit's mode cannot pan (a plain "stereo" mode plays
  one side only). **Moved to 1.10**, after version 0.3: first check what AHI tells about a unit's mode.
- **64-bit multiplies in dr_flac** (1.8): the planned check of the generated assembly (a 64-bit `muls.l` in the
  hot loop fails the build) was not written. dr_flac's assembly holds such multiplies (481, most in the decoding
  of the residuals), emulated slowly by the 68060's CPU library. **Dropped**: no 68060 among the target machines
  (a PiStorm with Emu68 is a 68040, the accelerator a 68020), and `FLAC=AUTO` measures the speed anyway, leaving
  FLAC to the server on a CPU too slow for it. To look at again only if a 68060 user reports a problem.
- **`BUFFER` option** (1.2: "may come with step 1.6"): the 1 MB stream buffer is fixed. **Only if the real
  machines show a need** (1.9: memory on a small Amiga, an unsteady network such as Wi-Fi), then in 1.10.
- **MP3 gapless** (1.8c): the encoder delay and the LAME header frame are not trimmed (about 50 ms of silence at
  the start of each MP3 file). **Step 1.8d**, after version 0.3 (which documents the limit): done on 2026-10-20.
- **Continuous integration** (3.3): GitHub Actions. **Host tests on each push before version 0.3**; the Amiga
  build and the release archive before 1.0.

## Ideas outside the phases (noted 2026-10-19)

Projects of their own, related to this one, not planned: to be studied (and their existence checked) if one day
they are taken up.

- **Amiga modules in the server**: play tracker music (MOD, MED, etc.) through LMS or Music Assistant on every
  player in the house, e.g. a conversion rule with `openmpt123` or a server plugin. To check first: what LMS and
  Music Assistant already do with these files.
- **A network AHI driver**: an AHI audio mode that sends the Amiga's sound to the network (Snapcast, an HTTP stream
  that LMS can play) instead of a sound chip, so that every AHI program (players, trackers) plays on the house's
  speakers. Limit: games and demos drive Paula directly, not AHI. To check first: whether such a driver exists.
- **A GitHub wiki for users' contributions**: what users bring and the developer alone cannot produce: a
  compatibility list (machines, network cards, TCP/IP stacks, AHI drivers), `BENCH` figures from machines not at
  hand, a FAQ from real questions. Not for the documentation itself, which stays in `docs/` (3.4). To decide after
  1.0 and a public announcement, depending on the feedback; editing restricted to collaborators.
- **amisqueeze on the other Amiga-family systems** (AmigaOS 4, MorphOS, AROS): checked on 2026-10-19, none of
  them has a Squeezebox player, nor any client of a music server (OS4Depot, MorphOS-Storage, the AROS archives
  through the AROS World mirror, Aminet, the community forums; the only one, AmiPlexAmp for Plex, is a 68k
  program). They have the same `ahi.device` and `bsdsocket.library`, so most of `amiga.c` should carry over, and
  the core is standard C. First and cheapest try: AROS 68k, which runs the current executables as they are (no
  code). Then AmigaOS 4 and MorphOS (PowerPC) and AROS on x86 or ARM: a toolchain each; decoding FLAC and MP3 is
  no CPU issue there. To check for a little-endian AROS: the byte swaps of the platform layer and of the samples
  handed to AHI (written for a big-endian 68k). A player for PCs was ruled out the same day: squeezelite exists
  everywhere; the host build stays a test tool.
