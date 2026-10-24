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
with a letter for a part (`player-7b`), so that a step is never read as a version: 0.7 is a version of amisqueeze,
`tools-7` a step. Until 2026-10-23 they were numbered `<phase>.<n>` (`1.7b` for `player-7b`, `0.10` for
`tools-10`); the review reports and the commit messages up to then keep those numbers.

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

## Roadmap

Which version of amisqueeze brings which steps. What can be tested to the end from the development machine (host
tests, WinUAE) comes first, what needs the real Amiga and a home network last (decided with the user on
2026-10-20). Each version's content is decided with the user; the details are below the table, the steps' own
statuses in [Where we are](#where-we-are).

| Version | Theme | Steps | Status | Tested on |
|---|---|---|---|---|
| 0.1 to 0.6 | the player for LMS: PCM, FLAC, MP3 (`amisqueeze.fpu`), MP3 without gaps, `LOG`, start from the Workbench, the review fixes | player-1 to player-8d, player-10 (part) | ✅ published from 2026-10-19 to 2026-10-21 ([changelog](../../CHANGELOG.md)) | host and WinUAE |
| 0.7 | Music Assistant; the `.lha`, the Amiga build in CI, the release made from a tag | player-7b, tools-10 (LMS), publish-2 (`.lha`), publish-3 | ✅ published on 2026-10-23 ([changelog](../../CHANGELOG.md)) | host and WinUAE, with LMS and Music Assistant |
| 0.8 | the real machine: what it shows | tools-11, player-9 | ⬜ | the real machine and network |
| 0.9 | internet radios' metadata (ICY), automatic server discovery | player-10 (end) | ⬜ | host for the code; the real machine and network |
| 1.0 | setup guides, a full audit, the Music Assistant scenario, the upload to Aminet | publish-4, publish-5, tools-10 (Music Assistant), publish-2 (Aminet) | ⬜ | the real machine |
| none yet | ideas: direct Paula output, source-level debugging | player-11, tools-8 | ⬜ | – |
| none | reference documents on AmigaOS for contributors, committed when ready, holding back no version (decided with the user on 2026-10-24) | tools-12 | ⬜ | – |

The remote (remote-0 to remote-12) will have versions of its own, its name being chosen by its study (remote-0);
it comes after these releases, or alongside if the user wants it.

**0.7 in detail:**

- Music Assistant (player-7b, decided with the user on 2026-10-21): the WAV header read at the start of a PCM
  stream (MA's `wav` codec, 24 bits announced as 16), the other output codecs tried (`mp3` with `amisqueeze.fpu`;
  a clear failure for a codec the build cannot play), a short Music Assistant part in the user guide.
- Two findings moved from the 0.6 review: `strm f` stops the playing track (its R4; ✅ confirmed against LMS and
  fixed on 2026-10-21), a test of the rejection side of the FLAC seek fix (its R3; ✅ done on 2026-10-21).
- From an outside reading of the code (2026-10-21): a stream request too long for amisqueeze reported to LMS as a
  failure (✅ done on 2026-10-21: `STMc` then `STMn`, since LMS drops any status before `STMc`; the refusal of a
  format had the same defect, fixed with it), `make dist` writing the zip's SHA-256 (done by hand at 0.6; ✅ done
  on 2026-10-21: `out/<zip>.sha256`, checked with `sha256sum -c`, and the line for the notes).
- For developers (decided with the user on 2026-10-20, from an outside reading of the documentation),
  `docs/development/architecture.md`: the portable core and the platform layer, the data path from the network to
  AHI with a diagram, what of slimproto is handled and what is ignored, taken out of `CLAUDE.md` (✅ done on
  2026-10-21, with the two kinds of position and the statuses; linked from `CONTRIBUTING.md`, read by the review
  agents `review-protocol-security` and `review-platform`).
- From the CPU measures of 2026-10-22 (decided with the user for 0.7, so that `NOAUDIO` can isolate the network's
  cost on the real machine): the stream read only once 8 KB of the buffer are free (✅ done on 2026-10-22:
  `NOAUDIO` from 40 % to 4.6 % of the slow profile, AHI unchanged).
- For developers (decided with the user on 2026-10-22): `docs/development/testing.md`, the WinUAE template
  `winuae/amisqueeze-test.uae` and the test sounds' generator `tools/make-test-audio.sh` (✅ done on 2026-10-22).
- Before its release (decided with the user on 2026-10-22): regression scenarios in WinUAE (tools-10: LMS commands
  through AHI, `OUTPUT` comparison; ✅ done on 2026-10-23: `tools/winuae/run-scenarios.py`, six scenarios in about
  4 minutes) and the `.lha` archive with its Aminet `.readme`, attached to the GitHub Release (publish-2, first
  part; ✅ done on 2026-10-23: `make dist`, the same files as the zip, extracted by LhA in WinUAE and by `lhasa`,
  the scenarios passed on the extracted executables).
- Brought forward from 1.0 (decided with the user on 2026-10-23, after an outside reading of how to set up the
  development environment): the Amiga build in CI (publish-3; ✅ done on 2026-10-23: `make dist DEV=1` through the
  Docker way of `tools/amiga-make.sh`, the image pinned by its digest, the local container recreated from it: the
  same executables, byte for byte); decided with the user the same day, from an outside reading, its development
  `.lha` and `.zip` kept 14 days as the run's artifacts, each on its own and not wrapped in a zip (✅ done on
  2026-10-23); brought forward from 1.0 the same day, the release made by the CI from a tag: a draft with the
  archives, published by the user (✅ done on 2026-10-23).
- The steps renamed and this roadmap (decided with the user on 2026-10-23, from an outside reading of the
  documentation: step numbers read as versions; ✅ done on 2026-10-23).
- `CLAUDE.md` kept for what only Claude needs (decided with the user on 2026-10-23, before the full audit so that
  it checks the move): the rules of working with the user, one line for each critical "never" rule with a pointer
  to where it is explained, and a table "before touching X, read Y"; everything a human contributor should know
  (release process, design choices, notes on each module, toolchain and pitfalls) moved to `CONTRIBUTING.md` and
  `architecture.md` only, the documents the review agents already read (no new document, the agents unchanged);
  `rules.md` pointing to sections instead of `CLAUDE.md` line numbers (89 of them, already off); no separate
  "living docs": the executable scenarios, the user guide and `architecture.md` are that. Shown to the user before
  its commit (✅ done on 2026-10-23: `CONTRIBUTING.md` gained Code rules with Stack and memory, Releasing and
  Pitfalls, `architecture.md` Decoders and AHI). Decided with the user the same day, so that the full audit checks
  the rules where they now live: the review tooling names the rules of `CLAUDE.md` or `CONTRIBUTING.md` (method:
  A3, A5, scoring, compliance table; `review-platform` P2; `review-plan-compliance` C4), where it named
  `CLAUDE.md` alone.
- Before its tag, a full audit (publish-5) instead of the light review (decided with the user on 2026-10-23), on
  the state after the renaming of the steps and the `CLAUDE.md` move; each step in a fresh session (the move,
  then the audit, then the release). ✅ done on 2026-10-23: [report](reviews/0.7.md), two findings at 75.
- Its R1, fixed (decided with the user on 2026-10-23): the WAV header's guards no test saw removed, each now
  caught by a test (✅ done on 2026-10-23: in `test_wav_refused`, 0 and 40 bits, 0 channels, a track ending
  between two chunks, a chunk length near 2^32 under an alarm, a whole track shorter than a RIFF header; in
  `test_wav_limited`, a raw PCM track after a WAV with a data size; each seen failing in a throwaway copy with
  its guard removed: a division by zero for 0 bits or 0 channels, the alarm for the chunk length). Seen on the
  way, brought into 0.7 by the user the same day (minor): a header of 0 channels said `more than 2 channels`, now
  `number of channels not supported` (✅ done on 2026-10-23, the user guide and the CHANGELOG with it).
- To decide with the user before the tag (asked on 2026-10-23, answered the same day): released on 2026-10-23,
  after a short Music Assistant check by hand in WinUAE (✅ done on 2026-10-23, Music Assistant 2.10.7, the build
  to be tagged, Paula through AHI, and a slimproto relay printing each status's elapsed time): FLAC (Music
  Assistant sends 24 bits) plays, pauses, resumes, seeks and ends; the PCM written by `OUTPUT` is the music; after
  a seek, the elapsed time sent starts again from 0 (Music Assistant adds the position; its progress bar jumps
  ahead for a moment, its own doing: the `STMt` it reads are right). `aac`: `STMc` then `STMn` each time; Music
  Assistant then sends nothing more and shows the track as playing, its time running to about 4 s and back to 0,
  again and again (once it went on to the next track): the user guide's `aac` row stays as it is. Seen on the way:
  the messages Music Assistant sends that amisqueeze ignores (0.8, below).
- Its R2, fixed (decided with the user on 2026-10-23): the `.lha` tried again on the Amiga is not a question but a
  rule, the image having changed since its trial; and it is now tried on the draft's own `.lha`, before the draft
  is published, rather than on a local build before the tag (`CONTRIBUTING.md`, Releasing; `phase-3-release.md`,
  publish-2). For 0.7: the draft's `.lha` extracted by LhA in WinUAE, its executables' SHA-256 those tried before
  the tag, `Version FULL` for both (✅ done on 2026-10-23: the draft's archives checked against their `.sha256`;
  LhA 2.15 tested and extracted the 14 entries, the 12 files `----rwed`, `Version FULL` 0.7 for both; each file
  the same byte for byte as the tested build or the repository; the CI's summary gave the same SHA-256; the
  release published by the user the same day). On the way, the tests workflow failed on the release commit: its
  artifact steps looked for a `-dev` archive only (fixed before the tag, decided with the user).
- From its 50 list, brought into 0.7 by the user on 2026-10-23 (the rest goes to 0.8 and 0.9, below): the plan's
  stale statuses (tools-1, tools-4, player-7b, publish-5, the "When" and "Stop" of publish-5; ✅ done on
  2026-10-23); a WAV sample rate whose bytes per second wrap 32 bits, which stalled the track: refused above
  384 kHz, with 0, as `sample rate not supported` (✅ done on 2026-10-23: a case in `test_wav_refused`, seen
  failing without the bound); `architecture.md` on when the stream socket is read, stale since `READ_MIN_SPACE`
  (✅ done on 2026-10-23).
- To check before its tag (decided with the user on 2026-10-23), since the executables now come from the pinned
  image (same gcc 16.2.1b, but a rebuilt image: libnix, NDK or binutils may differ; about 1.1 KB more each) while
  the WinUAE checks of 2026-10-23 ran on the previous image's: `run-scenarios.py` all OK on the build to be
  tagged, the stack probe redone for both executables (from a script with `Stack 4096`, `CONTRIBUTING.md`, "Checks
  the host tests cannot make"), `Version <exe> FULL` for both. ✅ done on 2026-10-23 on the release build (version
  0.7, the pinned image; SHA-256 `342d888b68f47c310e6a453439491c09d68bfde21e224b0a45c901bbbed86919` for
  `amisqueeze`, `77caa4a5ab481b5d4230fcd204e5bf4dcbb82d57403618a7abe305a58c006882` for `amisqueeze.fpu`): the six
  scenarios OK (JIT, 68882 with `compfpu`); `Version FULL` gives `amisqueeze 0.7 (10/23/2026)` and
  `amisqueeze.fpu 0.7 (10/23/2026)`; the stack, from a script with `Stack 4096` (a throwaway build with the probe;
  the release build rebuilt afterwards, the same SHA-256), 5600 bytes of 16384 for `amisqueeze` playing two FLAC
  tracks with a seek in each, 17544 of 32768 for `amisqueeze.fpu` with FLAC then MP3, a seek in each (5.6 and
  17.8 KB on 2026-10-20).

**Later versions in detail:**

- 0.8: the real machine (player-9, tried with 0.7, LMS and Music Assistant): what it shows; from the outside
  reading of the code, only if the real machine shows gaps while it works, more AHI blocks or a higher task
  priority; in the user guide's Paula part (decided with the user on 2026-10-23), that the LED filter (a 3.3 kHz
  low-pass) needs no tool to turn it off: `paula.audio` turns it off while playing and back on afterwards (seen in
  WinUAE on 2026-10-23 with AHI 6.6, bit 1 of CIA-A `$BFE001`), once the real machine has confirmed it (player-9,
  step 3 of its protocol). The 50 list of the [0.7 review](reviews/0.7.md) (put there on 2026-10-23, by the
  project's rule; what to do of each item is decided with the user when 0.8 is planned):
  - safety nets with a hole: `fake_server.py` scenarios that ignore the exit code; `check-saves.py` blind to
    fp2-fp7; no script for the ASCII rule but on the Aminet readme; nothing checks that `___stkinit` is linked;
    eight server-input checks no test feeds (`slimproto.c`, `stream.c`); the "nothing playing" branch of
    `stream_flush` untested; no fake-server scenario for `strm f` or a refused stream;
  - absurd input: `PORT` and `UNIT` without range (an open point below);
  - documentation: the "about 1.1 MB" of the user guide against about 1.23 MiB from the constants (to measure on
    the real machine, player-9); the guide's catch-all message row without `stream: WAV header`;
    "(HostTest)" in `phase-1-amisqueeze.md`; three
    decisions not marked as agreed with the user (the format refusal as STMc then STMn, the CI actions pinned by
    SHA, `cpucount`);
  - smaller: `cpucount`'s tick overflow; the release draft not waiting for the tests; the CI not compiling
    `cpucount` nor the Python tools; the no-MP3 branches never compiled by `make test`.

  Seen at the Music Assistant check before the 0.7 tag (2026-10-23, by the user): the messages Music Assistant
  sends and amisqueeze ignores (`vers`, `grfb`, `grfe` at each connection, `codc` and `cont` at each track) read
  as errors, and the user guide's messages table calls `... not handled` worth reporting.
- 0.9: internet radios' metadata (ICY, see [Outside review](#outside-review-2026-10-20)) and automatic server discovery (player-10),
  after the real network. With it, from the 50 list of the 0.7 review: the HTTP status line never checked, the
  body of a direct stream played before LMS's `cont` (a redirect or an error page played as a track, the `body`
  frame of playlist URLs), the 88.2 kHz rate code refused as an unsupported format; the polarity and output
  channel flags, transitions and replay gain ignored without a line in the guide.
- 1.0: before it, the setup guides (publish-4), a full audit (publish-5), perhaps an attestation of the archives'
  provenance (`actions/attest-build-provenance` in `release.yml`, from an outside reading on 2026-10-23, left for
  1.0 by the user: little use to Amiga users, the reproducible build already lets anyone check), the Music
  Assistant scenario in WinUAE (tools-10); then the upload to Aminet (publish-2; `Author:` and `Uploader:` of the
  `.readme` corrected first).

## Where we are

| Step | Status |
|---|---|
| tools-1 Toolchain | ✅ 2026-10-18 |
| tools-2 Test LMS server | ✅ 2026-10-18 |
| tools-3 Reference player | ✅ 2026-10-18 |
| tools-4 WinUAE test configuration | 🔶 the versioned template `winuae/amisqueeze-test.uae` (2026-10-22), the slow 68020 and fast JIT profiles as its overrides ([testing.md](testing.md)); a 68040 profile still open |
| tools-5 AHI in the emulated Amiga (Paula, Toccata) | ✅ 2026-10-18 |
| tools-6 Host-side tests (`make test`) | ✅ 2026-10-18 |
| tools-7 Automated runs in WinUAE | ✅ 2026-10-22: `tools/winuae/`, one scenario (a test tone through AHI with LMS) |
| tools-8 Source-level debugging (optional) | ⬜ |
| tools-9 Test Music Assistant server | 🔶 2026-10-21: players side done (Music Assistant 2.10.7); API token not yet (remote-8) |
| tools-10 Regression scenarios in WinUAE | 🔶 LMS part done 2026-10-23: `tools/winuae/run-scenarios.py`, six scenarios (a test tone, LMS's commands through AHI with FLAC decoded by the server or on the Amiga, `OUTPUT` of PCM, FLAC and MP3), about 4 minutes; Music Assistant before 1.0 |
| tools-11 A real TCP/IP stack in WinUAE (AmiNetXDuo, PCMCIA card) | ✅ 2026-10-24: the "real network" profile; raw PCM costs about 40 % of the slow profile in the network, Paula through AHI 92 % in all |
| tools-12 Amiga reference documents (`docs/amiga/`) | ⬜ agreed on 2026-10-24: tools-12a (AHI, network) first, then stack and memory, signals, Kickstart versions, Workbench start |
| player-1 Control connection | ✅ 2026-10-18 |
| player-1b Default options (`ENV:amisqueeze.prefs`) | ✅ 2026-10-18 |
| player-2 Receiving the stream (virtual output) | ✅ 2026-10-18 |
| player-3 Output to a file | ✅ 2026-10-18 |
| player-4 AHI audio output | ✅ 2026-10-19 |
| player-5 Commands and volume | ✅ 2026-10-19 |
| player-6 Robustness | ✅ 2026-10-19 |
| player-7a Other PCM formats (LMS) | ✅ 2026-10-19 |
| player-7b Music Assistant | ✅ 2026-10-23: first test 2026-10-21: FLAC works; `wav` sends a 24-bit WAV announced as 16 bits: its header read, validated in WinUAE on 2026-10-21 (slow 68020, Paula); `mp3` works with `amisqueeze.fpu` but not gapless (MA encodes it without a LAME header), `aac` refused with `STMn` (MA ignores it), tried on 2026-10-21; Music Assistant part of the user guide written on 2026-10-21; the short check by hand before the 0.7 tag done on 2026-10-23 (FLAC, the `aac` row confirmed; see 0.7 in detail) |
| player-8 Local decoding (FLAC, then MP3) | ✅ 2026-10-19 (FLAC; MP3 in `amisqueeze.fpu`) |
| player-8d MP3 without gaps between tracks | ✅ 2026-10-20 (version 0.4) |
| player-9 Real machine | ⬜ tried with 0.7, release 0.8 |
| player-10 Finishing touches | 🔶 the parts of 0.4 and 0.5 done (fixed volume, AHI mode warning, `LOG`, Workbench start); server discovery in 0.9 |
| player-11 Direct Paula output, without AHI | ⬜ idea (CPU load on a slow 68020) |
| remote-0 Study (common interface, Music Assistant feasibility, name) | ⬜ before any amilms code |
| remote-1 to remote-7 LMS | ⬜ |
| remote-8 to remote-11 Music Assistant | ⬜ after version 0.2 |
| remote-12 MPD | ⬜ idea, after Music Assistant |
| publish-1 Licence and user documentation | 🔶 licence: MIT; user guide kept up to date |
| publish-2 Aminet archive | 🔶 2026-10-23: `make dist` builds the `.lha` and its Aminet `.readme` (for the 0.7 GitHub Release), tried in WinUAE (LhA 2.15) and on the host (`lhasa`); the upload to Aminet with 1.0 |
| publish-3 Public versions | 🔶 0.1 to 0.7 published (tags, GitHub releases, changelog); CI: host tests, and the Amiga build with `make dist DEV=1`, its archives kept as the run's artifact; a draft release made from a tag (2026-10-23) |
| publish-4 Setup guides: minimal server, the Amiga on the network | ⬜ before 1.0 |
| publish-5 Review before each release (method, review agents, rules inventory, checks) | ✅ 2026-10-20: publish-5a to publish-5c done; publish-5d stopped after the version strings check; publish-5e the release rule in `CLAUDE.md` (in `CONTRIBUTING.md`, Releasing, since 2026-10-23), no review of HEAD; the tooling changes only when a review misses a real defect, or by the user's decision. Reviews since: [0.6](reviews/0.6.md) (light, 2026-10-20), [0.7](reviews/0.7.md) (full audit, 2026-10-23) |

Phase 0 moves forward as needed: a tooling step is done when a step of phase 1 or 2 needs it. Phases 1 and 2 can
progress in parallel.

**0.6, the fixes** (open findings of [acceptance-0.3-0.4](reviews/acceptance-0.3-0.4.md) and
[acceptance-0.4-0.5](reviews/acceptance-0.4-0.5.md), still there on 2026-10-20; one batch, each fix with its test):

- ✅ documentation: workstation references (0.4..0.5 R7); step player-5 still says LMS never sends `adjust` 0 (0.3..0.4
  R9); step player-2 says `cont` and autostart are handled as in squeezelite (R11 of both); direct radios and their
  metadata not mentioned (0.4..0.5 R10, a known limit in the user guide); the cheap items scored 50: stale names
  (`read_lame_header`, `consume()`), "2 KB stack OK", the `mp3 pcm` rule said to be in the README, "ToolTypes come
  later" in step player-1, the unit check count, the zip's list without the `.info` icons, the figures in the comments
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
design agreed with the user, then code, tests, a line in `CHANGELOG.md`. The remote's study (remote-0) comes after these
releases, or alongside if the user wants it.
Music Assistant (tools-9, player-7b, remote-8 to remote-11) was put after 0.2 (decided 2026-10-19): it is no longer waiting on a
release, when to start it is still to be decided. First tried on 2026-10-21 (tools-9 and a first test of player-7b, no
code changed): FLAC works; the `wav` output codec needs amisqueeze to read the WAV header (in 0.7).

## Open points (review of 2026-10-19)

Announced in the plan, then neither done nor dropped; decided with the user on 2026-10-19:

- **AHI mode warning** (player-4): a warning at start-up when the unit's mode cannot pan (a plain "stereo" mode plays
  one side only). **Moved to player-10**, after version 0.3: first check what AHI tells about a unit's mode. Done on
  2026-10-20 (version 0.4).
- **64-bit multiplies in dr_flac** (player-8): the planned check of the generated assembly (a 64-bit `muls.l` in the
  hot loop fails the build) was not written. dr_flac's assembly holds such multiplies (481, most in the decoding
  of the residuals), emulated slowly by the 68060's CPU library. **Dropped**: no 68060 among the target machines
  (a PiStorm with Emu68 is a 68040, the accelerator a 68020), and `FLAC=AUTO` measures the speed anyway, leaving
  FLAC to the server on a CPU too slow for it. To look at again only if a 68060 user reports a problem.
- **`BUFFER` option** (player-2: "may come with step player-6"): the 1 MB stream buffer is fixed. **Only if the real
  machines show a need** (player-9: memory on a small Amiga, an unsteady network such as Wi-Fi), then in player-10.
- **MP3 gapless** (player-8c): the encoder delay and the LAME header frame are not trimmed (about 50 ms of silence at
  the start of each MP3 file). **Step player-8d**, after version 0.3 (which documents the limit): done on 2026-10-20.
- **Continuous integration** (publish-3): GitHub Actions. **Host tests on each push before version 0.3**; the Amiga
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
    itself (to check on the real machine, player-9: `SBTC_BREAKMASK` or EINTR taken as a stop);
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
  hand, a FAQ from real questions. Not for the documentation itself, which stays in `docs/` (publish-4). To decide after
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
