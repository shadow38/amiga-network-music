# Development plan

Roadmap of amiga-network-music: the phases, split into testable steps. Each step has a goal, an **observable**
success criterion and a way to check it.

| Phase | Content | Document |
|---|---|---|
| 0 | Tooling and test environment | [phase-0-tooling.md](phase-0-tooling.md) |
| 1 | amisqueeze: slimproto player with AHI output | [phase-1-amisqueeze.md](phase-1-amisqueeze.md) |
| 2 | amilms (working name, amiremote proposed): remote control for LMS, then Music Assistant (MPD: an idea) | [phase-2-amilms.md](phase-2-amilms.md) |
| 3 | Release | [phase-3-release.md](phase-3-release.md) |

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
| 0.4 WinUAE test configuration | 🔶 provisional (networking, no AHI) |
| 0.5 AHI in the emulated Amiga (Paula, Toccata) | ✅ 2026-10-18 |
| 0.8 | ⬜ |
| 0.6 Host-side tests (`make test`) | ✅ 2026-10-18 |
| 0.7 Automated runs in WinUAE | 🔶 serial shell working |
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
| 1.9 to 1.10 | ⬜ |
| 1.11 amisqueeze: direct Paula output, without AHI | ⬜ idea (CPU load on a slow 68020) |
| 0.9 Test Music Assistant server | ⬜ after version 0.2 |
| 2.0 remote: study (common interface, Music Assistant feasibility, name) | ⬜ before any amilms code |
| 2.1 to 2.7 remote: LMS | ⬜ |
| 2.8 to 2.11 remote: Music Assistant | ⬜ after version 0.2 |
| 2.12 remote: MPD | ⬜ idea, after Music Assistant |
| 3.1 Licence and user documentation | 🔶 licence: MIT |
| 3.2 to 3.3 release | ⬜ |
| 3.4 Setup guides: minimal server, the Amiga on the network | ⬜ before 1.0 |

Phase 0 moves forward as needed: a tooling step is done when a step of phase 1 or 2 needs it. Phases 1 and 2 can
progress in parallel. Release 0.2 was published on 2026-10-19 (FLAC decoded on fast Amigas, 8/24/32-bit PCM); the
current priority is amisqueeze for LMS towards version 0.3: MP3 is done (1.8c, 2026-10-19), next the real machine
(1.9) or the finishing touches (1.10), to agree with the user.
Music Assistant (0.9, 1.7b, 2.8 to 2.11) was put after 0.2 (decided 2026-10-19): it is no longer waiting on a
release, when to start it is still to be decided.

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
