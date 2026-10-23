# Development plan

Roadmap of Amiga Network Music: the phases, split into testable steps. Each step has a goal, an **observable**
success criterion and a way to check it.

| Phase | Content | Document |
|---|---|---|
| 0 | Tooling and test environment | [phase-0-tooling.md](phase-0-tooling.md) |
| 1 | amisqueeze: slimproto player with AHI output | [phase-1-amisqueeze.md](phase-1-amisqueeze.md) |
| 2 | amilms (working name, amiremote proposed): remote control for LMS, then Music Assistant (MPD: an idea) | [phase-2-amilms.md](phase-2-amilms.md) |
| 3 | Release | [phase-3-release.md](phase-3-release.md) |

How to build and test: [CONTRIBUTING.md](../../CONTRIBUTING.md); testing in WinUAE: [testing.md](testing.md).

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
| 0.7 Automated runs in WinUAE | ✅ 2026-10-22: `tools/winuae/`, one scenario (a test tone through AHI with LMS) |
| 0.8 Source-level debugging (optional) | ⬜ |
| 1.1 amisqueeze: control connection | ✅ 2026-10-18 |
| 1.1b amisqueeze: default options (`ENV:amisqueeze.prefs`) | ✅ 2026-10-18 |
| 1.2 amisqueeze: receiving the stream (virtual output) | ✅ 2026-10-18 |
| 1.3 amisqueeze: output to a file | ✅ 2026-10-18 |
| 1.4 amisqueeze: AHI audio output | ✅ 2026-10-19 |
| 1.5 amisqueeze: commands and volume | ✅ 2026-10-19 |
| 1.6 amisqueeze: robustness | ✅ 2026-10-19 |
| 1.7a amisqueeze: other PCM formats (LMS) | ✅ 2026-10-19 |
| 1.7b amisqueeze: Music Assistant | 🔶 first test 2026-10-21: FLAC works; `wav` sends a 24-bit WAV announced as 16 bits: its header read, validated in WinUAE on 2026-10-21 (slow 68020, Paula); `mp3` works with `amisqueeze.fpu` but not gapless (MA encodes it without a LAME header), `aac` refused with `STMn` (MA ignores it), tried on 2026-10-21; Music Assistant part of the user guide written on 2026-10-21 |
| 1.8 amisqueeze: local decoding (FLAC, then MP3) | ✅ 2026-10-19 (FLAC; MP3 in `amisqueeze.fpu`) |
| 1.8d amisqueeze: MP3 without gaps between tracks | ✅ 2026-10-20 (version 0.4) |
| 1.9 amisqueeze: real machine | ⬜ tried with 0.7, release 0.8 |
| 1.10 amisqueeze: finishing touches | 🔶 the parts of 0.4 and 0.5 done (fixed volume, AHI mode warning, `LOG`, Workbench start); server discovery in 0.9 |
| 1.11 amisqueeze: direct Paula output, without AHI | ⬜ idea (CPU load on a slow 68020) |
| 0.9 Test Music Assistant server | 🔶 2026-10-21: players side done (Music Assistant 2.10.7); API token not yet (2.8) |
| 0.10 Regression scenarios in WinUAE | 🔶 LMS part done 2026-10-23: `tools/winuae/run-scenarios.py`, six scenarios (a test tone, LMS's commands through AHI with FLAC decoded by the server or on the Amiga, `OUTPUT` of PCM, FLAC and MP3), about 4 minutes; Music Assistant before 1.0 |
| 2.0 remote: study (common interface, Music Assistant feasibility, name) | ⬜ before any amilms code |
| 2.1 to 2.7 remote: LMS | ⬜ |
| 2.8 to 2.11 remote: Music Assistant | ⬜ after version 0.2 |
| 2.12 remote: MPD | ⬜ idea, after Music Assistant |
| 3.1 Licence and user documentation | 🔶 licence: MIT; user guide kept up to date |
| 3.2 Aminet archive | 🔶 2026-10-23: `make dist` builds the `.lha` and its Aminet `.readme` (for the 0.7 GitHub Release), tried in WinUAE (LhA 2.15) and on the host (`lhasa`); the upload to Aminet with 1.0 |
| 3.3 Public versions | 🔶 0.1 to 0.6 published (tags, GitHub releases, changelog); CI: host tests, and the Amiga build with `make dist DEV=1`, its archives kept as the run's artifact; a draft release made from a tag (2026-10-23) |
| 3.4 Setup guides: minimal server, the Amiga on the network | ⬜ before 1.0 |
| 3.5 Review before each release (method, review agents, rules inventory, checks) | ✅ 2026-10-20: 3.5a to 3.5c done; 3.5d stopped after the version strings check; 3.5e the release rule in `CLAUDE.md`, no review of HEAD (the next review is the one before 0.6); the tooling changes only when a review misses a real defect |

Phase 0 moves forward as needed: a tooling step is done when a step of phase 1 or 2 needs it. Phases 1 and 2 can
progress in parallel. Release 0.6 was published on 2026-10-21 (the review fixes, the user guide's troubleshooting,
known limits and what has been tried, the zip's SHA-256); 0.5 on 2026-10-20 (`LOG`, start from the Workbench, a
commodity, the stack of `amisqueeze` and seeking in a FLAC fixed; 0.4 the same day: MP3 without gaps, fixed volume
through LMS, AHI mode warning; 0.3 too: MP3 decoded by `amisqueeze.fpu`; 0.2 on 2026-10-19: FLAC, 8/24/32-bit
PCM). Next (decided with the user on 2026-10-21): 0.7 for Music Assistant, then the real machine (1.9) in 0.8,
then 0.9.

**Next releases** (decided with the user on 2026-10-20): what can be tested to the end from the development machine
(host tests, WinUAE) comes first, what needs the real Amiga and a home network last.

| Version | Content | Tested |
|---|---|---|
| 0.4 | MP3 without gaps (1.8d), fixed volume through LMS's own setting, the AHI mode warning (1.10): all done | host and WinUAE |
| 0.5 | `LOG=<file>`, start from the Workbench with an icon and ToolTypes, a commodity stopped from Exchange (1.10); seeking in a FLAC decoded on the Amiga fixed (1.8b); the outside review's fixes (stack of `amisqueeze`, tests): all done | host and WinUAE |
| 0.6 | the fixes from the review reports (decided with the user on 2026-10-20): the open findings of the two acceptance reviews (documentation, the icon's `WINDOW` with `SMART`, a decoder that cannot start, a FLAC rate of 0, gaps in `check-saves.py`, guards without a test), the ICY limitation written in the user guide; then its release review (light, the first in real use) | host and WinUAE |
| 0.7 | Music Assistant (1.7b, decided with the user on 2026-10-21): the WAV header read at the start of a PCM stream (MA's `wav` codec, 24 bits announced as 16), the other output codecs tried (`mp3` with `amisqueeze.fpu`; a clear failure for a codec the build cannot play), a short Music Assistant part in the user guide; two findings moved from the 0.6 review: `strm f` stops the playing track (its R4; ✅ confirmed against LMS and fixed on 2026-10-21), a test of the rejection side of the FLAC seek fix (its R3; ✅ done on 2026-10-21); from an outside reading of the code (2026-10-21): a stream request too long for amisqueeze reported to LMS as a failure (✅ done on 2026-10-21: `STMc` then `STMn`, since LMS drops any status before `STMc`; the refusal of a format had the same defect, fixed with it), `make dist` writing the zip's SHA-256 (done by hand at 0.6; ✅ done on 2026-10-21: `out/<zip>.sha256`, checked with `sha256sum -c`, and the line for the notes); for developers (decided with the user on 2026-10-20, from an outside reading of the documentation), `docs/development/architecture.md`: the portable core and the platform layer, the data path from the network to AHI with a diagram, what of slimproto is handled and what is ignored, taken out of `CLAUDE.md` (✅ done on 2026-10-21, with the two kinds of position and the statuses; linked from `CONTRIBUTING.md`, read by the review agents `review-protocol-security` and `review-platform`); from the CPU measures of 2026-10-22 (decided with the user for 0.7, so that `NOAUDIO` can isolate the network's cost on the real machine): the stream read only once 8 KB of the buffer are free (✅ done on 2026-10-22: `NOAUDIO` from 40 % to 4.6 % of the slow profile, AHI unchanged); for developers (decided with the user on 2026-10-22), `docs/development/testing.md`, the WinUAE template `winuae/amisqueeze-test.uae` and the test sounds' generator `tools/make-test-audio.sh` (✅ done on 2026-10-22); before its release (decided with the user on 2026-10-22), regression scenarios in WinUAE (step 0.10: LMS commands through AHI, `OUTPUT` comparison; ✅ done on 2026-10-23: `tools/winuae/run-scenarios.py`, six scenarios in about 4 minutes) and the `.lha` archive with its Aminet `.readme`, attached to the GitHub Release (step 3.2, first part; ✅ done on 2026-10-23: `make dist`, the same files as the zip, extracted by LhA in WinUAE and by `lhasa`, the scenarios passed on the extracted executables); brought forward from 1.0 (decided with the user on 2026-10-23, after an outside reading of how to set up the development environment), the Amiga build in CI (3.3; ✅ done on 2026-10-23: `make dist DEV=1` through the Docker way of `tools/amiga-make.sh`, the image pinned by its digest, the local container recreated from it: the same executables, byte for byte; decided with the user the same day, from an outside reading, its development `.lha` and `.zip` kept 14 days as the run's artifacts, each on its own and not wrapped in a zip, ✅ done on 2026-10-23; brought forward from 1.0 the same day, the release made by the CI from a tag: a draft with the archives, published by the user, ✅ done on 2026-10-23); to check before its tag (decided with the user on 2026-10-23), since the executables now come from the pinned image (same gcc 16.2.1b, but a rebuilt image: libnix, NDK or binutils may differ; about 1.1 KB more each) while the WinUAE checks of 2026-10-23 ran on the previous image's: `run-scenarios.py` all OK on the build to be tagged, the stack probe redone for both executables (from a script with `Stack 4096`, `CONTRIBUTING.md`, "Checks the host tests cannot make"), `Version <exe> FULL` for both | host and WinUAE, with LMS and Music Assistant |
| 0.8 | the real machine (1.9, tried with 0.7, LMS and Music Assistant): what it shows; from the outside reading of the code, only if the real machine shows gaps while it works, more AHI blocks or a higher task priority | on the real machine and network |
| 0.9 | internet radios' metadata (ICY, see below) and automatic server discovery (1.10), after the real network | host for the code; validated on the real machine and network |
| 1.0 | before it: the setup guides (3.4), a full audit (3.5), the Music Assistant scenario in WinUAE (0.10); then the upload to Aminet (3.2; `Author:` and `Uploader:` of the `.readme` corrected first) | the real machine |

**0.6, the fixes** (open findings of [acceptance-0.3-0.4](reviews/acceptance-0.3-0.4.md) and
[acceptance-0.4-0.5](reviews/acceptance-0.4-0.5.md), still there on 2026-10-20; one batch, each fix with its test):

- ✅ documentation: workstation references (0.4..0.5 R7); step 1.5 still says LMS never sends `adjust` 0 (0.3..0.4
  R9); step 1.2 says `cont` and autostart are handled as in squeezelite (R11 of both); direct radios and their
  metadata not mentioned (0.4..0.5 R10, a known limit in the user guide); the cheap items scored 50: stale names
  (`read_lame_header`, `consume()`), "2 KB stack OK", the `mp3 pcm` rule said to be in the README, "ToolTypes come
  later" in step 1.1, the unit check count, the zip's list without the `.info` icons, the figures in the comments
  of `audio.c:80` and `mp3.c:27` (done 2026-10-20, also marked in `rules.md`);
- ✅ the guide promises that `LOG` keeps the start-up errors with `QUIET` (0.4..0.5 R4): fixed in the code (decided
  with the user on 2026-10-20): under `QUIET`, a message before the log is open opens the window; checked in
  WinUAE;
- ✅ the icon's `WINDOW` ToolType without `SMART` (0.4..0.5 R8, user-visible);
- ✅ a decoder that cannot start leaves a NULL state used later (0.4..0.5 R1: an internal "skip" decoder takes the
  track, `test_decoder_no_start`); a FLAC rate of 0 stalls the track (0.3..0.4 R14, with the dead test
  `flac.c:161`: `scenario_flac_rate_0`); `check-saves.py` misses `(an)+`, `-(an)`, `dbra` (0.4..0.5 R2: self-test
  `tests/check-saves.s` in `make test`); guards no test sees removed: the stream request over 1 KB, the LAME
  header's bounds (0.3..0.4 R2, R6: `test_request_too_long`, `test_lame_header` cut at every length). Each test
  seen failing without its fix (planted in a throwaway copy), then passing;
- moved to 0.7 (decided with the user at the [0.6 review](reviews/0.6.md), 2026-10-20): `strm f` stops the
  playing track (0.3..0.4, first replay; ✅ fixed on 2026-10-21, see below), the rejection side of the FLAC seek
  fix untested (0.4..0.5 R6; ✅ tested on 2026-10-21, see below);
- ✅ `strm f` (0.6 review R4), confirmed and fixed on 2026-10-21: LMS sends it only when the track being streamed
  is no longer the next one (next track removed, moved or replaced, reshuffle; `Playlist.pm`,
  `StreamingController.pm` `_FlushGetNext`) and expects the playing track to go on; amisqueeze stopped it, a
  loss of up to the 1 MB buffer of the playing track (about 6 s of PCM, 9 s of FLAC), since `STMd` is sent at
  the end of the data received, not of the decoding as in squeezelite. Seen with the host build and LMS 9.1
  (three 10 s FLAC tracks, the next one deleted at 3 s through the CLI): before, the first track cut at 3 s;
  after, played to its end, then the new next track from 0. Now `strm f` drops only the last queued track (its
  stream closed without `DSCO`, its data taken back from the ring buffer, `ring_truncate`); with the audio
  output, a block of it already filled plays as the end of the playing track (at most 3 blocks, about 0.6 s);
  with nothing playing, a stop as before. Music Assistant sends `f` then `q` before each new track: unchanged.
  Tests `test_flush*`, `test_ring_truncate`, seen failing before the fix;
- ✅ the rejection side of the FLAC seek fix (0.6 review R3), tested on 2026-10-21: `scenario_flac_seek_rejected`
  sends, as after a seek, a frame whose header leaves its rate, then its sample size, to STREAMINFO (code 0, CRC-8
  recomputed); expected: the "cannot start from the middle of this file" message, nothing played, `STMu`. Seen
  failing in a throwaway copy with the check of `flac.c:214` removed (both cases), and with `rate == 0` or
  `bits == 0` taken out of `flac.c:161` (the matching case);
- ✅ found by the 0.6 review and fixed before the release: `QUIET` without `LOG` still lost the errors found after
  the log was opened (R1, checked in WinUAE); a line of the skip decoder without a test (R2,
  `test_decoder_no_start_audio`).

The real machine is tried with 0.7 (changed on 2026-10-21: until then with 0.6, before Music Assistant), so that
one session covers both servers; it needs no discovery (`SERVER=` works on a real network): what it shows makes
0.8. Server discovery waits for the real network because the test
setup cannot check it: the test LMS runs in a container behind forwarded TCP ports, which a UDP broadcast from the
emulated Amiga does not cross. Each step keeps the method:
design agreed with the user, then code, tests, a line in `CHANGELOG.md`. The remote's study (2.0) comes after these
releases, or alongside if the user wants it.
Music Assistant (0.9, 1.7b, 2.8 to 2.11) was put after 0.2 (decided 2026-10-19): it is no longer waiting on a
release, when to start it is still to be decided. First tried on 2026-10-21 (0.9 and a first test of 1.7b, no
code changed): FLAC works; the `wav` output codec needs amisqueeze to read the WAV header (in 0.7).

## Open points (review of 2026-10-19)

Announced in the plan, then neither done nor dropped; decided with the user on 2026-10-19:

- **AHI mode warning** (1.4): a warning at start-up when the unit's mode cannot pan (a plain "stereo" mode plays
  one side only). **Moved to 1.10**, after version 0.3: first check what AHI tells about a unit's mode. Done on
  2026-10-20 (version 0.4).
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
  build and the release archive before 1.0 (both ✅ done on 2026-10-23, brought forward to 0.7).

## Outside review (2026-10-20)

A code and plan review of version 0.4, made from outside (read only, nothing run). Done before 0.5: the stack of
`amisqueeze` (16 KB, it overflowed the Shell's 4 KB since 0.2), UBSan stops the tests, tests of the AHI mode at
start-up, of a preferences file cut short and of the LAME header's fallbacks (a frame count of 0 no longer drops
the track), the user guide's messages, the plan's statuses. Still open:

- **Internet radios' metadata (ICY)**, checked in LMS's sources: for a radio played directly by the player
  (`amisqueeze.fpu` with an `http` MP3 radio), LMS adds 2 to autostart, asks the radio for `Icy-MetaData: 1`, then
  sends `cont` with the interval of the metadata blocks. amisqueeze plays at once and does not remove the blocks:
  minimp3 would lose its sync at each one (cuts every second or so). **Planned for 0.9** (0.6 states the
  limitation in the user guide): remove the blocks (the interval from `cont` or the `icy-metaint` header), a fake
  server scenario, a check with a real radio.
- **Noted, for the user to decide** (no known failure):
  - the track queue (4 entries) forgets the oldest one when full, its bytes still in the buffer: a clean refusal
    (`STMn`) would be safer; LMS has never been seen sending that far ahead;
  - an HTTP request over 1 KB (very long signed radio URLs) is refused without telling LMS (`STMn`), which keeps
    waiting (✅ done in 0.7, 2026-10-21);
  - Ctrl-C with a real TCP/IP stack: depending on the stack's break mask, `WaitSelect()` may take the signal for
    itself (to check on the real machine, 1.9: `SBTC_BREAKMASK` or EINTR taken as a stop);
  - `gethostbyname()`: no check of an empty address list or of a length other than 4;
  - `PORT` and `UNIT` not checked for range (`PORT=70000`, `UNIT=255`);
  - the default options are read on one line of at most 510 characters, the rest dropped silently;
  - the CI builds neither the Amiga executables nor the configuration without MP3 actually shipped (decided: before
    1.0), and its workflow has no `permissions:` block (✅ done in 0.7, 2026-10-23: both executables built, a
    read-only `permissions:` block);
  - malformed server input (cut frames, `strm` fields out of range, headers too long) is not tested yet;
  - `stream.c` (HTTP client, track queue, output scheduling, virtual output) is the first module to split when it
    grows (for ICY, for instance).

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
