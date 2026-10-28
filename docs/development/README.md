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
  finished step: criterion checked, documentation up to date, one commit; one per item when a step gathers
  independent items (agreed with the user on 2026-10-27).
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
| 0.8 | what the real machine shows; a lower rate asked of the server; automatic server discovery; hardening; the writing rules applied to the documentation | player-9, player-10 (discovery), player-12, player-13, player-14, player-15, tools-11, publish-6 | ⬜ |
| 0.9 | internet radios' metadata (ICY) | player-10 (end) | ⬜ |
| 1.0 | setup guides, a full audit, the upload to Aminet | publish-4, publish-5, tools-10 (Music Assistant), publish-2 (Aminet) | ⬜ |
| none yet | ideas: direct Paula output, source-level debugging | player-11, tools-8 | ⬜ |
| none | reference documents on AmigaOS for contributors, committed when ready (agreed with the user on 2026-10-24) | tools-12 | ⬜ |

The remote (remote-0 to remote-12) will have versions of its own, its name chosen by its study (remote-0). It
comes after these releases, or alongside if the user wants it.

### 0.8

Agreed with the user on 2026-10-26.

- **The real machine** (player-9): the step's whole protocol on the A1200, with LMS, then with Music Assistant.
  Only if it shows gaps: more AHI blocks, or a higher task priority. The same session also checks:
  - Paula's LED filter (protocol step 3): the user guide says that `paula.audio` turns it off, once the real
    machine confirms it (seen in WinUAE on 2026-10-23; agreed with the user on 2026-10-23). Done on 2026-10-28:
    on the A1200 it stays off as long as amisqueeze runs, not only while playing (agreed with the user on
    2026-10-28);
  - Ctrl-C with a real TCP/IP stack (protocol step 6; see [Open points](#open-points));
  - the `aac` row of the user guide, with Music Assistant;
  - the free memory the user guide asks for, from `Avail` (protocol step 1);
  - the automatic server discovery, with LMS, Music Assistant and no server (protocol step 8). Done on
    2026-10-27.
- **A lower rate asked of the server** (player-13): a `MAXRATE` option, with a conversion rule on LMS.
- **Automatic server discovery** (player-10): moved from 0.9, since its last check needs the same session on the
  real machine (agreed with the user on 2026-10-27). Done on 2026-10-27, checked on the host, in WinUAE and on the
  A1200 with AmiNetXDuo, Miami and Roadshow.
- **Paula's swapped sides**: a note in the user guide for `paula.audio`, done on 2026-10-27. No `SWAP` by
  default: the sides are left to the direct Paula output (player-11).
- **The messages Music Assistant sends and amisqueeze ignores** (`vers`, `grfb`, `grfe`, `codc`, `cont`): shown
  with `VERBOSE` only, since they read as errors. An unknown message is still shown. Done on 2026-10-27. `cont`
  itself is handled in 0.9 (ICY).
- **Hardening** (player-12): the 50 list of the [0.7 review](reviews/0.7.md#5-the-50-list), except the items of
  0.9 below. Done on 2026-10-27.
- **The buffer running dry in the middle of a frame** (player-14): no `STMo` when the stream stopped there, found
  on the A1200 with the network cable pulled (agreed with the user on 2026-10-27). Done on 2026-10-27.
- **The task priority** (player-15): a `PRI` option, 0 by default, and a line in the user guide's
  troubleshooting, since the A1200 plays with gaps while it works at priority 0 (agreed with the user on
  2026-10-28). Coded and tested on the host on 2026-10-28; the A1200 next.
- **The writing rules** (publish-6): the plan and the user guide done on 2026-10-24; `docs/development/` next,
  where the user finds it worth it.

### 0.9

- **Internet radios' metadata** (ICY, player-10), designed with the user on 2026-10-27 (see
  [player-10](phase-1-amisqueeze.md)). For a radio played directly by `amisqueeze.fpu`, LMS asks the radio for
  `Icy-MetaData: 1`, then sends the blocks' interval in `cont`. amisqueeze will wait for `cont`, remove the blocks
  and send their titles back to LMS. Coded in a branch, rebased on `main` once 0.8 is out.
- **From the 0.7 review's 50 list** (agreed with the user on 2026-10-23), in the same branch:
  - the HTTP status line is never checked (with ICY);
  - the body of a direct stream plays before LMS's `cont`, so a redirect or an error page plays as a track (with
    ICY);
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
| player-9 Real machine | 🔶 2026-10-27: the protocol with LMS on the A1200, but `SYS:WBStartup`; Music Assistant next |
| player-10 Finishing touches | 🔶 the parts of 0.4 and 0.5 done; server discovery 2026-10-27 (0.8), on the A1200 with three stacks; ICY designed 2026-10-27 (0.9) |
| player-11 Direct Paula output, without AHI | ⬜ idea (CPU load on a slow 68020; crackles with a PCMCIA network card, player-9) |
| player-12 Hardening (the 0.7 review's 50 list) | ✅ 2026-10-27 (version 0.8) |
| player-13 A lower rate asked of the server | ✅ 2026-10-26: `MAXRATE` and the LMS rules; on the A1200, 62 % of the CPU at 32 kHz against 77 %; Music Assistant upstream: the user's decision |
| player-14 The buffer running dry in the middle of a frame | ✅ 2026-10-27: on the host and the A1200 |
| player-15 The task priority (`PRI`) | 🔶 2026-10-28: coded, tested on the host; the A1200 next |
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
  break mask. On the A1200 (player-9, 2026-10-27), "stopped" alone with Miami and Roadshow, and with AmiNetXDuo
  but once: with half-open connections, "recv: error 4" (EINTR) and a retry came before "stopped". Leads:
  `SBTC_BREAKMASK`, or EINTR taken as a stop.
- From the outside review of 2026-10-20, no known failure: `stream.c` is the first module to split when it grows.
  ICY adds a small module of its own and leaves the rest in place (agreed with the user on 2026-10-27). Its other
  points went to player-12 (agreed with the user on 2026-10-27).
- **LMS's `body` frame**: for a playlist address played directly, LMS asks the player to send the body back.
  amisqueeze shows it as "not handled" (agreed with the user on 2026-10-27).

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
