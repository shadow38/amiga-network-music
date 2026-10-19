# Development plan

Roadmap of amiga-network-music: the phases, split into testable steps. Each step has a goal, an **observable**
success criterion and a way to check it.

| Phase | Content | Document |
|---|---|---|
| 0 | Tooling and test environment | [phase-0-tooling.md](phase-0-tooling.md) |
| 1 | amisqueeze: slimproto player with AHI output | [phase-1-amisqueeze.md](phase-1-amisqueeze.md) |
| 2 | amilms: remote control through the LMS CLI | [phase-2-amilms.md](phase-2-amilms.md) |
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
| **1.6 to 1.10** | ⬜ **next: 1.6 (robustness)** |
| 2.1 to 2.7 amilms | ⬜ |
| 3.1 Licence and user documentation | 🔶 licence: MIT |
| 3.2 to 3.3 release | ⬜ |

Phase 0 moves forward as needed: a tooling step is done when a step of phase 1 or 2 needs it. Phases 1 and 2 can
progress in parallel; the current priority is amisqueeze (1.4, AHI audio output).
