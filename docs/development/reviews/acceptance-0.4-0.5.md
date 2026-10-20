# Review tooling acceptance test: amisqueeze-0.4..amisqueeze-0.5 (plan step 3.5b)

## 1. Header

- **Purpose**: acceptance test of the review tooling (plan step 3.5b) on the release after the one used to tune it
  (`0.3..0.4`, [report](acceptance-0.3-0.4.md)): the plan judges the tooling also on what it finds beyond the
  outside review of 0.4, which the development session knows. The agents were given none of its findings, none of
  the earlier passes' findings and nothing of what the commits of 0.5 fix.
- **Commit**: `6b5a861b6b577f426463f93aa5a9a70a8747eb88` (tag `amisqueeze-0.5`), reviewed in a throwaway worktree
  checked out at that commit, removed afterwards.
- **Range**: `amisqueeze-0.4..6b5a861` (12 commits, 25 files, +1133/-116).
- **Date**: 2026-10-20.
- **Agents**: `review-protocol-security`, `review-platform`, `review-tests-docs`, `review-plan-compliance`, in
  parallel, fresh contexts, effort `high`; definitions of `fe65eb5` (loaded at the session's start), method of
  `08f9557` (one score, never a range). Same prompt as the replays of sections 11 and 12 of the 0.3..0.4 report,
  only the SHA, the range, the worktree names, "no tag later than amisqueeze-0.5" and "three other reviewers"
  changed. One minute after the launch each transcript held its questions (S1-S7, P1-P6 with the new P3 wording,
  T1-T6 with the new T5 wording, C1-C4) and the method's "never a range".
- **Run**: review-plan-compliance 14 minutes, review-platform 20, review-protocol-security 24, review-tests-docs
  31; none near the 80-minute stop. 39 to 75 turns, 3.3 to 11.1 million tokens read from the cache each; about
  9 dollars for the four by the logged usage, a lower bound (the logged output tokens look too low). Every agent
  answered every one of its questions.
- **`make test`**: through `tools/amiga-make.sh` with a local build setup (a Linux container); green at the commit
  before the launch (test_core 391 checks, 0 failed; fake_server 0 failed), and the Amiga build (`all`) too. Each
  agent saw its own baseline green before planting anything, in its own worktree, removed afterwards.
- **Blindness check** (the tool calls of the four transcripts scanned for paths outside the tree, later refs and
  tags, web access):
  - `review-tests-docs` read the first lines of `../amiga-make.local.sh` (the local build launcher, outside the
    root) to find out why `gcov` was missing, and its `git worktree list` showed the main checkout's commit
    (`08f9557`, after 0.5); it declared both, and neither is used in a finding. The same launcher read happened in
    the earlier runs.
  - `review-plan-compliance` ran git with `HEAD` inside the reviewed worktree, where it is `6b5a861`: harmless,
    but `release-review` forbids `HEAD` in the agents' git commands.
  - The agents wrote their logs into this session's scratch directory, where the verification notes of this
    session were kept while three of them still ran; no agent read or named a file but its own (checked file by
    file).
  - No web access this time (the earlier runs fetched LMS and squeezelite sources), no `main`, `--all` or later
    tag, no fact that exists only after 0.5 in a finding.

## 2. Synthesis (findings at 75 or more, after verification)

| Dimension | Findings | Assessment |
|---|---|---|
| Code / Security (core) | R1 | a decoder that cannot start (no memory) leaves a NULL state that the next pass dereferences |
| Code (safety nets, release script) | R2, R3 | `check-saves.py` misses three kinds of register write; `make dist` packages mismatched versions |
| Documentation | R4, R5, R10 | QUIET + LOG loses the start-up errors; network messages missing; direct radios not mentioned |
| Tests | R6 | the 0.5 seek path's rejection side and most header branches never run |
| Compliance | R7, R8, R9 | workstation references added; the icon's `WINDOW` example without `SMART`; a direct `printf` |
| Plan | R11 | step 1.2 still claims `cont` is handled |

Repeats: R2, R3, R5 and R11 were already found by the tooling on `0.3..0.4` (R2 and R3 in its second replay, R5
and R11 as its R13 and R11) and are still there at 0.5; R3 is also an open point of the plan
(`docs/development/README.md:140`). R10 continues the 0.3..0.4 R15 (ICY metadata reaches the decoder) from the
documentation side. New at 0.5 or first seen: R1, R4, R6, R7, R8, R9.

## 3. Findings at 75 or more

Every cited line re-opened at `6b5a861`. "Found by" names the agents and their score; the score here is the one
after verification. All at 75; the proven ones first.

**R1. A decoder start failure leaves `dec.ops` set with a NULL state; the next pass runs the decoder on it**
(score 75, before 0.5)
- dimension: Code / Security
- file / line: `src/amisqueeze/stream.c:276-294` (`prepare_decoder`), `:443-445` (`feed_audio`: `continue` after a
  failure); `src/amisqueeze/flac.c:283-291` (`flac_start` returns 0 without a state), `:318` (`st->r = r`);
  claim at `docs/user-guide.md:402`
- evidence: `prepare_decoder` sets `o->dec.ops = t->ops; o->dec.track = t->start; o->dec.state = NULL;`, and on a
  failed `start()` only prints "cannot decode this track, skipped" and consumes what the ring holds of the track.
  The next call takes the early exit (`o->dec.ops == t->ops && o->dec.track == t->start`) and returns 1, so
  `buffered()` and `run()` get the NULL state. The guide: "`flac: not enough memory` … then `…: cannot decode this
  track, skipped` | … the next track follows".
- failure scenario: an Amiga short of memory; the decoder's allocation fails when the output starts, while the
  track is still downloading; with more of the track in the ring, `flac_run` writes through NULL (the exception
  vectors on a 68000-style memory map without MMU): a crash or corruption instead of "track skipped".
- rule: A2 (the user guide's claim), A7
- proven or inferred: proven on the host by the agent (a test with a decoder whose start fails, then more data of
  the same track: UBSan "member access within null pointer" at `flac.c:318`, `make test` red); the path read
  again in verification; the Amiga consequence is inferred.
- suggestion: on failure, mark the track as skipped so that it is not taken again (a no-op decoder that consumes
  it to its end, or a flag on the track), with a unit test.
- found by: review-protocol-security (75).

**R2. `tools/check-saves.py` misses callee-saved registers written by `(an)+`, `-(an)` and `dbra`** (score 75,
before 0.5)
- dimension: Code (safety net)
- file / line: `tools/check-saves.py:80-85`; claim at `CONTRIBUTING.md:118-120`, `CLAUDE.md:190-191`
- evidence: `dests = ops if op == "exg" else [ops[-1]]` then `re.fullmatch(REG, d)`: only a bare register as the
  last operand counts as a write.
- failure scenario: the GCC reload defect the script guards against picks `a2` as a post-incremented pointer, `a3`
  for a pre-decrement, or `d3` as a `dbra` counter, without saving it: planted `move.l (a2)+,d0`, `clr.l -(a3)`,
  `dbra d3,…` in `amiga.c`, the Amiga build passed and linked both executables (`moveq #0,d2` went red).
- rule: CONTRIBUTING.md:118-120; A4
- proven or inferred: proven (planted defects).
- suggestion: count `(aN)+`/`-(aN)` in any operand and the first operand of `db*` as writes; consider fp2-fp7
  against `fmovem`; a self-test `.s` file run by `make test`.
- found by: review-platform (75). Already found on 0.3..0.4 (50 in the first pass, 75 in the second replay).

**R3. `make dist` packages a release whose executables disagree on the version** (score 75, before 0.5)
- dimension: Code (release script)
- file / line: `tools/make-dist.py:4-5` (claim), `:18-22`, `:34`
- evidence: docstring "The version is read from the $VER: string of src/<prog>/amiga.c, so the archive always
  matches the executable"; the regex finds the first `$VER: amisqueeze <v> (` only; `out/amisqueeze.fpu` is added
  unchecked.
- failure scenario: at a release only one of the two `$VER:` lines is bumped: planted `.fpu` at 0.4 with
  `Firmware=0.4` gave `amisqueeze-0.5.zip` without a word; a `0.6-dev` version without a `[0.5]` CHANGELOG section
  gave `amisqueeze-0.6-dev.zip`.
- rule: CLAUDE.md:12-25; A2, A5
- proven or inferred: proven (planted).
- suggestion: check both `$VER:` strings and `Firmware=` agree, refuse `-dev`, require the CHANGELOG section.
- found by: review-platform (75). Already found on 0.3..0.4 (second replay, 75); an open point of the plan
  (`docs/development/README.md:140`).

**R4. With `QUIET` and `LOG`, start-up errors go nowhere, against the user guide** (score 75)
- dimension: Documentation
- file / line: `src/amisqueeze/amiga.c:680-711` (order of `main`), `:192` (`msg()` writes only if `Output()`),
  `:647-653` (no console with QUIET); `docs/user-guide.md:125`, `:129`, `:164`, `:166-167`
- evidence: `wb_start`, the FPU check, `read_prefs`, `parse_line` (ToolTypes errors) and `cx_open` all run before
  `log_open` (`:711`); with QUIET no console is opened, so `msg()` drops the text. The guide: "`LOG=<file>` keeps a
  copy of every message"; "`QUIET` | … add `LOG=<file>` to keep its messages"; "An error in the ToolTypes … is
  shown in the window"; "A log that cannot be opened … is said".
- failure scenario: an icon in WBStartup with `QUIET`, `LOG=RAM:a.log` and `PORT=abc`: no window, nothing in the
  log, the program quits without a trace; `LOG=NODIR:x` with QUIET: no log and no message.
- rule: user-documentation statements proven false (method, scoring); CLAUDE.md:56-57
- proven or inferred: proven by reading (verified); seeing it needs WinUAE.
- suggestion: open a window for start-up errors even with QUIET, or say in the guide that they are not logged.
- found by: review-tests-docs (75).

**R5. Messages printed without `VERBOSE` are missing from the user guide's table** (score 75, before 0.5)
- dimension: Documentation
- file / line: `docs/user-guide.md:382-419`; `src/amisqueeze/slimproto.c:49`, `:328`, `:393`;
  `src/amisqueeze/player.c:214`, `:229`, `:323`; `src/amisqueeze/stream.c:620`, `:665`, `:692`; `amiga.c:563`
- evidence: no row for `send: error`, `socket: error`, `recv: error`, `retrying in N s`, the start line
  `player "…" (…), server … port …`, `WaitSelect: error`; the row `stream: …, output: … (with VERBOSE)` (`:419`)
  covers `stream: connection lost before the response headers`, `stream: recv: error` and `stream: send: error`,
  which print without `VERBOSE`. `docs/development/README.md:122`: "Done before 0.5: … the user guide's messages".
- failure scenario: the TCP/IP stack fails a `recv`: the user reads `amisqueeze: recv: error 54` and finds no row;
  or reads a stream error the guide says needs `VERBOSE`.
- rule: CLAUDE.md:56-57; A2 (the plan's "done" claim)
- proven or inferred: proven (read; each message grepped in the guide).
- suggestion: add the rows; split the `VERBOSE` row from the `amisqueeze: stream:` errors.
- found by: review-tests-docs (75). Already found on 0.3..0.4 (its R13); the 0.5 fix left these out.

**R6. The 0.5 FLAC seek fix: its rejection side and most header branches are never run** (score 75)
- dimension: Tests
- file / line: `src/amisqueeze/flac.c:127-162` (`frame_streaminfo`), `:214-215`; `tests/fake_server.py:477-522`
- evidence: removed together, the result check at `:214`, the validity check at `:161`, the multi-byte frame
  number step at `:132` and the variable-block branch at `:163`: `make test` green. gcov: `:128`, `:130-132`,
  `:141-144`, `:150-155`, `:215` never executed; `flac_frame()` in the fake server takes frames below 128, rates
  44.1/48 kHz, no block code 6/7. The documented message "cannot start from the middle of this file"
  (`user-guide.md:400`) never fires in a test.
- failure scenario: a change breaks the frame number skip for 2-byte numbers: every seek past frame 127 (about
  12 s) in a file with a header-coded rate or block size builds a wrong STREAMINFO; `make test` stays green.
- rule: CONTRIBUTING.md:11; A4
- proven or inferred: proven (removals, gcov).
- suggestion: unit-test `frame_streaminfo` with crafted headers (frame numbers of 2 to 7 bytes, codes 6/7/12-14,
  variable blocking, a rejected header with its message).
- found by: review-tests-docs (75; "50 if the existing seek scenario counts as the tests the rule asks for").

**R7. The release adds references to a workstation tool ("amiagent") and to "the local notes"** (score 75)
- dimension: Compliance
- file / line: `docs/development/phase-1-amisqueeze.md:526`, `:530`; `CLAUDE.md:201`, `:203`
- evidence: "Clicks in MUI or Workbench windows through amiagent: see the local notes."; "WinUAE, driven through
  amiagent"; "amiagent's Shell has a 256 KB stack"; "(never from amiagent's own stack)". Nothing named amiagent is
  in the repository; `git log -S` gives `4f0ab39` and `a2e132a`, both in the range.
- failure scenario: a reader of the public repository is sent to notes that do not exist for them; the stack
  pitfall's explanation depends on a tool they cannot identify.
- rule: CLAUDE.md:8-10
- proven or inferred: proven.
- suggestion: describe the condition generically ("a Shell with a large stack, such as a remote-control shell") and
  keep amiagent in the local notes.
- found by: review-plan-compliance (75). Same class as the 0.3..0.4 R12 (a local launcher name), new instance.

**R8. The icon's example `WINDOW` ToolType lacks `SMART`** (score 75)
- dimension: Compliance / Documentation
- file / line: `tools/make-icon.py:26`, built into both `.info` files of the zip; `amiga.c:112`;
  `docs/user-guide.md:157-158`, `:163`
- evidence: `"(WINDOW=CON:0/20/640/120/amisqueeze/AUTO/CLOSE/WAIT)"`; the default (`amiga.c:112`) and the guide
  (`:163`) end in `/SMART`; `CLAUDE.md:215`: "A `CON:` window needs `SMART`, or it loses its text when uncovered".
- failure scenario: a user removes the parentheses, as the guide says, to move the window: the window loses its
  text whenever it is uncovered.
- rule: CLAUDE.md:215; A2
- proven or inferred: proven (the string); the visible effect is the project's own WinUAE observation
  (`phase-1-amisqueeze.md:516-517`).
- suggestion: add `/SMART`, ideally from the same string as `DEFAULT_WINDOW`.
- found by: review-plan-compliance (75), review-platform (75), review-tests-docs (75).

**R9. `posix.c` calls `printf` directly** (score 75, an interpretation of the rule: see below)
- dimension: Compliance
- file / line: `src/amisqueeze/posix.c:59` (added in `f183538`)
- evidence: `printf("amisqueeze: cannot write to the log %s: error %d, log closed\n", log_name, errno);` in
  `log_check`; `CLAUDE.md:156-157`: "Never `Printf`/`PutStr`/`printf` directly".
- failure scenario: the one line that bypasses `msg()`, so a change to `msg()`'s output does not reach it; no test
  runs it (R6's gcov: `posix.c:59-62` never executed). No functional failure.
- rule: CLAUDE.md:154-157
- proven or inferred: proven (read).
- verification note: `log_check` is part of `msg()`'s own host implementation (called from `msg()`, which itself
  writes with `vfprintf(stdout, …)`); review-plan-compliance read the rule as met for that reason. The rule states
  no such exception, and the method puts a break of an explicit rule at 75: kept at 75; 50 once the rule says that
  the platform's own output functions are exempt, or 0 if the line goes through a console-only writer.
- suggestion: format into a buffer and write it through `msg()`'s console path (as `amiga.c:141` does), or write
  the exception into the rule.
- found by: review-platform (75), review-tests-docs (75); review-plan-compliance: COVERED.

**R10. Direct internet radios: the user guide and the 0.5 notes do not say that their metadata reaches the
decoder** (score 75, before 0.5, to confirm)
- dimension: Documentation
- file / line: `src/amisqueeze/slimproto.c:216-222` (autostart 2/3 played as 0/1, "would wait for "cont" (radio
  metadata): not supported yet"), `:318` (`cont` "not handled"); `docs/user-guide.md:19-20`, `:409`;
  `CHANGELOG.md` 0.5; the plan at `docs/development/README.md:125-129`
- evidence: no `cont`, `metaint` or ICY handling in the core; no mention of radios, ICY or metadata in the user
  guide (grep); its row for "`… not handled (N bytes)`" says "Worth reporting". The plan: "amisqueeze plays at once
  and does not remove the blocks: minimp3 would lose its sync at each one (cuts every second or so)".
- failure scenario: an MP3 web radio played by LMS directly to `amisqueeze.fpu`: "cont not handled", cuts about
  every second, nothing in the guide, a report filed for a known limit.
- rule: A7; CLAUDE.md:56-57
- proven or inferred: the core side and the documentation gap proven; that LMS sends autostart 3 with
  `Icy-MetaData` for such radios, and the audible effect, **to confirm** with LMS and a radio station.
- suggestion: a known limit in the user guide (and the next CHANGELOG): direct MP3 radios cut until 0.6; "cont not
  handled" expected.
- found by: review-protocol-security (75).

**R11. Step 1.2 (done) says "`cont` and `autostart` handled as in squeezelite"** (score 75, before 0.5)
- dimension: Plan
- file / line: `docs/development/phase-1-amisqueeze.md:53`; `src/amisqueeze/slimproto.c:218-222`, `:302-319`
- evidence: no `cont` branch (it falls into "not handled"), autostart 2/3 turned into 0/1; the plan's own
  `README.md:125-129` says the opposite.
- failure scenario: a later step relies on 1.2's done content and assumes radio metadata is handled.
- rule: CLAUDE.md:55-56; A2
- proven or inferred: proven.
- suggestion: "autostart 2/3 played as 0/1; `cont` ignored, see ICY in 0.6".
- found by: review-protocol-security (75). Already found on 0.3..0.4 (its R11), not corrected.

## 4. Compliance table (merged)

| Unit | Status | Proof |
|---|---|---|
| 0.5 `LOG` (host) | PARTIAL | `scenario_log` (`fake_server.py:742`), its open-failure case; missing: write error and `msg_raw` to the log untested (works with `/dev/full` by experiment); Amiga side OUTSIDE REPO |
| 0.5 Workbench start, `tooltypes_line` | PARTIAL | `test_tooltypes` (`test_core.c:1745`), planted overflow red under ASan; missing: no criterion in the plan, empty value and final size check untested; `wb_start`, icons, QUIET, WINDOW OUTSIDE REPO; R4, R8 |
| 0.5 commodity | OUTSIDE REPO | code read (`amiga.c:464-524`); Exchange in WinUAE |
| 0.5 stack of `amisqueeze` (16 KB) | PARTIAL | `amiga.c:63-67`, `Makefile:32,37` (`___stkinit` in both links); `-fstack-usage` frames match 5.5 / 17.5 KB; missing: the Workbench start (`.fpu` icon at 16384, relies on `__stkinit`) never measured; the probe OUTSIDE REPO |
| 0.5 FLAC seek fixed | PARTIAL | `scenario_flac_seek` (`fake_server.py:499`), red on three plantings; missing: R6 |
| 0.5 LAME frame count 0 | COVERED | `mp3.c:113-115`, `test_lame_header` (`test_core.c:1692`), red when removed |
| 0.5 UBSan stops the tests | COVERED | `Makefile:50`; planted UB red in three agents (signed overflow, negative shift, NULL member access) |
| 0.5 AHI mode at start-up, prefs cut short | COVERED | `test_start_mode`; `test_core.c:1598-1610` red under ASan when the guards are removed |
| Release 0.5: version strings, `$VER:` date, `-dev` sequence, CHANGELOG, tag | COVERED | `amiga.c:47,49`, `slimproto.c:24`, `user-guide.md:15`, `CHANGELOG.md:10`; both binaries; tag on `6b5a861`; `dfbc00a` 0.5-dev |
| Release 0.5: zip contents | COVERED | `make dist` listing: both executables, both icons, LICENSE, README, CHANGELOG, guide, 4 images; R3 for versions |
| Release 0.5: GitHub Release, notes, CI run, `Version FULL` | OUTSIDE REPO | - |
| CLAUDE.md nothing workstation-specific | NOT COVERED | R7 |
| CLAUDE.md `msg()` only | PARTIAL | core: no `printf` (grep); R9 |
| CLAUDE.md a `CON:` window needs `SMART` | PARTIAL | default `amiga.c:112`; R8 |
| CLAUDE.md user guide documents messages | PARTIAL | the 0.5 messages documented; R4, R5, R10 |
| CLAUDE.md statuses kept up to date | PARTIAL | 0.5 rows and 1.10 statuses right; R11 and the 50 list (stale names, CI open point) |
| CLAUDE.md check-saves safeguard | PARTIAL | `d2` plant red; R2 |
| CLAUDE.md stack rule, measurement redone | PARTIAL | measured at `a2e132a`, no later change in depth (frames read); Workbench start not measured |
| CLAUDE.md AHI rules (link, abort order, full volume) | COVERED | `audio.c:167-213`, `amiga.c:434`, `stub_audio_bad` |
| CLAUDE.md portable core, byte order, framing, formats advertised | COVERED | include and call greps; `slimproto.c:29-154, 357-387`; `test_fragmented`, `test_bad_length` |
| CLAUDE.md third-party macros, licences, English, ASCII, LF | COVERED | `flac.c:17-21` = `dr_flac.c:7-11`, `mp3.c:20-21` = `minimp3.c:9-10`; `third_party/` untouched; greps |
| CLAUDE.md open points never implemented unasked | COVERED | none of `README.md:130-145` in the source diff |
| CONTRIBUTING.md tests for core changes | PARTIAL | tests added for each 0.5 item; R1 and R6 paths untested |

## 5. The 50 list

- Plan: the "fixes" of 0.5 (FLAC seek, the outside review's points, the frame count of 0, ICY
  added to 0.6) are recorded and dated but not marked as agreed with the user (`README.md:17-18`).
- 1.10's Workbench start and commodity have no stated criterion (as noted on 0.3..0.4).
- Stale text: `read_lame_header` in `CLAUDE.md:84` and `phase-1-amisqueeze.md:467` (renamed `mp3_lame_header` in
  `03d0a52`), 1.8d not updated for the frame count of 0; `consume()` named in `CLAUDE.md:110` does not exist;
  `CLAUDE.md:60` "2 KB stack OK" superseded; the CI open point and `phase-1-amisqueeze.md:29` ("ToolTypes … come
  later") not marked done; "Tests: 375 unit checks" (now 391); `CLAUDE.md:14-15` and `make-dist.py:2-5` omit the
  `.info` icons.
- `check-saves.py` ignores fp2-fp7 (planted `fmove.x fp0,fp2`: green).
- After a seek into a variable-blocksize FLAC, the made-up STREAMINFO sets `max_block = 65535`
  (`flac.c:163-165`): dr_flac then allocates about 512 KB for stereo (inferred effect on a small Amiga).
- CPU load figure (`CLAUDE.md:61-62`) predates the volume tables and sample conversions (before 0.5).
- Track queue (`MAX_TRACKS` 4) overflow reached in the core with six short tracks (proven: 4 STMs for 5
  boundaries); whether LMS streams that far ahead is not verified.
- HTTP status ignored: a 404 page written as PCM (proven).
- Misleading "stream format p not supported" for rate codes above 48 kHz and `?` fields; ReplayGain, crossfade,
  `serv` and sync ignored, not in the guide.
- Stale socket readiness after a `strm s` reusing the same socket number (inferred); a zero-byte queue entry kept
  (inferred).
- FLAC STREAMINFO rate 0 accepted (`flac.c:90-113`), the output stalls: not run this time (see limits).
- Already in the plan's open points (`README.md:130-145`), met again: `gethostbyname()` result unchecked, `PORT` and
  `UNIT` not range-checked, the 510-character prefs line, Ctrl-C and the stack's break mask, a request over 1 KB
  refused without `STMn`, the no-MP3 and Amiga builds not compiled in CI.
- Exchange "Remove" during a blocking connect or send waits for the call (inferred); `AHIDB_Name` not forcibly
  terminated (inferred); `IoctlSocket(FIONBIO)` unchecked; `PrintFault` and a raw `Write` bypass `msg()`.
- Only one amisqueeze at a time has exceptions (`BENCH`, no commodities.library); a misspelled ToolType or a value
  with `"` is dropped silently.
- `LOG` write-error path and `msg_raw` to the log untested; the 35 s receive timeout untested; malformed server
  input untested (already in the plan, `README.md:143`); two fake-server scenarios ignore the exit code
  (`fake_server.py:291, 313`).
- `dist` does not depend on `test`; `actions/checkout@v5` pinned by tag; the CI's gcc 13 against "tested with gcc
  14"; no `permissions:` (already in the plan).
- `decoder.h:64-67` `*left` wording loose; `flac.c:161` `rate >= 1UL << 20` can never be true; the `'('` guard of
  `player.c:88` redundant.

## 6. Checked and found solid

- Control framing and every opcode's length check; `frame_streaminfo` stays in bounds for every frame number
  coding; ring arithmetic and 64-bit positions; clock-wrap-safe intervals; MP3 staging cannot loop forever.
- The AHI request life cycle (no double `WaitIO`, the playing request aborted first, a reset before close), the
  commodity close order, `main()`'s cleanup on every exit path, the LOG name's lifetime.
- Safety nets seen red on planted defects: ASan, UBSan (now fatal), LSan through the fake server's exit codes,
  `-Werror=format` on both Amiga variants, `make dist` on a missing file or `$VER:`, `scenario_flac_seek`,
  `scenario_log`, `test_lame_header`, the prefs tests.
- Version strings and dates in all their places, the zip's contents, the CHANGELOG line of each user-visible
  commit, links and anchors of the documentation, the option table against the template.
- The stack measurement still holds for the Shell start: no later commit of the range changes call depth.

## 7. Limits

- No WinUAE, real Amiga, LMS or Music Assistant: the Workbench start, the commodity, QUIET, the Amiga `LOG`, the
  stack probe, `Version FULL` and every AHI or bsdsocket behaviour are read only or OUTSIDE REPO.
- review-protocol-security consulted no LMS or squeezelite source this time (the earlier runs did): which opcodes
  LMS sends, how far ahead it streams, its ReplayGain fields and its handling of direct radios stay unverified.
- A method rule not applied: the FLAC STREAMINFO rate of 0 is an inferred finding the host build can settle (a
  crafted file); it was left unrun and unscored instead of settled (it was R14 of 0.3..0.4).
- Not settled by a run: the 24-bit entry of `bits_table` (failed only together with another removal), the fit
  check and case folding of `tooltypes_line` (expected red, not run).
- CI on GitHub not run; `amiga.c` is never built on the host, so none of its paths ran.
- The verification pass was done by the development session, which knows the outside review and the earlier
  passes: repeats are marked as such, scores are the agents' except where section 3 says otherwise.

## 8. Decisions

Acceptance test on a past release: these findings are carried to the review of HEAD (step 3.5e), where each is
fixed through a plan step or accepted by the user.

Decided with the user on 2026-10-20, after section 9: the criterion of 3.5b is met for `0.4..0.5`; the method and
the agents stay as they are (the FLAC rate of 0 left unrun is run-to-run variance against a rule the method already
has: to watch on HEAD); the launch prompt of the review of HEAD adds "no `HEAD` in git commands" and "logs in your
own worktree, not in the session's scratch directory".

| Finding | Decision |
|---|---|
| R1 | |
| R2 | |
| R3 | |
| R4 | |
| R5 | |
| R6 | |
| R7 | |
| R8 | |
| R9 | accepted by the user: the line is part of `msg()`'s own host implementation; the rule now says so (`CLAUDE.md`, "Output goes through `msg()`") |
| R10 | |
| R11 | |

## 9. Comparison with the outside review of 0.4 (written after sections 1 to 8)

Sources: "Outside review" in `docs/development/README.md:119-145` at `6b5a861`, and the commits of 0.5 that fixed
its points (`a2e132a`, `c360f37`, `905d70f`, `03d0a52`, `16ade65`). The question: are the points that 0.5 lists as
"done before 0.5" correctly fixed, according to the agents' evidence?

| Outside review point (fixed in 0.5) | What the agents show | Result |
|---|---|---|
| `amisqueeze` (68020) ran on the Shell's 4 KB stack | `__stack` 16384 (`amiga.c:63-67`) and `___stkinit` in both links (`Makefile:32,37`), seen in the link lines; `-fstack-usage` frames match the recorded 5.5 / 17.5 KB | fixed for a Shell start; the Workbench start (both icons at 16384, `.fpu` relying on `__stkinit` for 32 KB) never measured (50) |
| Stack measurement made before dr_flac, never redone | redone at `a2e132a`; no later commit of the range changes call depth (review-platform, review-plan-compliance) | fixed; `CLAUDE.md:60` still carries the old "2 KB stack OK" (50) |
| UBSan reports without failing | `-fno-sanitize-recover=all` (`Makefile:50`); three agents planted UB, each run red | fixed, proven |
| AHI mode wiring at start-up never run by the tests | `test_start_mode` (`test_core.c:1702-1740`) | fixed |
| "File cut short" test stops before its guard | exact-size buffer; the guard removed: ASan red (`test_core.c:1600`) | fixed, proven |
| LAME header fallbacks untested | `test_lame_header`; the frame-count-0 guard removed: red. The bounds checks `size < off + 12`, `size < off + 1` and `total >= delay` removed: green (a zeroed 400-byte buffer hides an over-read) | partly: the fallbacks' results are pinned, their bounds checks are not |
| A frame count of 0 dropped the whole track | `mp3.c:113-115`, red when removed; `st->left = NO_LIMIT` (`mp3.c:138`) | fixed, proven; the plan's 1.8d text not updated (50) |
| Messages printed without `VERBOSE` missing from the guide | R5: the network errors, `retrying in N s`, the start line and three `stream:` errors still missing or under the `VERBOSE` row | **not fully fixed** |
| Plan statuses (1.10, the open points, 3.3) | review-plan-compliance C2: those rows right | **partly**: the rows named are right; step 1.2's `cont` claim (R11), `read_lame_header`, the CI open point and `phase-1:29` left stale |
| A local launcher name in the plan | gone from the tree (only in `16ade65`'s message) | fixed; the same release adds "amiagent" and "the local notes" (R7) |
| "Building is in the README" cross-reference | review-tests-docs T6: every cross-reference resolves to the right document | fixed |
| Comments: `AUDIO_BLOCK_SIZE` and where the AHI preferences are read | review-protocol-security S7: `player.h:119-120` and `audio.c:7-9` match the code | fixed |

**The points still open** (README:125-145) were all confirmed still open by review-plan-compliance, and each was
met again by an agent: ICY (R10, from the documentation side), the queue overflow (now proven reachable in the
core), the request over 1 KB, the break mask, `gethostbyname()`, `PORT`/`UNIT`, the 510-character line, what
`make dist` checks (R3, proven by planted mismatches), the CI's builds and `permissions:`, the malformed-input
tests, the split of `stream.c`. Most of them came back as items already in the plan (review-platform's P5 and P6,
the 50 list); `gethostbyname()` and `permissions:`, missed by the first pass on 0.3..0.4 and reached by its second
replay, were seen again.

**Found beyond the outside review** (not in it, nor in the earlier passes):

- at 75: R1 (a decoder that cannot start leaves a NULL state the next pass uses; proven on the host), R4 (QUIET +
  LOG loses the start-up errors), R6 (the 0.5 seek fix's rejection side never runs), R7 (workstation references
  added in 0.5), R8 (the icon's `WINDOW` example without `SMART`), R9 (a direct `printf`, an interpretation of the
  rule);
- at 50, all inferred: the 512 KB dr_flac allocation after a seek into a variable-blocksize FLAC (new in 0.5), the
  stale socket readiness, Exchange "Remove" during a blocking call;
- already found by the tooling on 0.3..0.4 and still present: R2 (`check-saves.py`), R11 (step 1.2's `cont`
  claim); at 50, now proven: an HTTP error page played as PCM (0.3..0.4 50 list), `check-saves.py` blind to fp2-fp7
  (0.3..0.4 50 list), the track queue overflow reached in the core (also in the outside review).

**Against the plan's criterion** (the tooling "judged on what it finds beyond" the outside review): six findings
at 75 that neither the outside review nor the earlier passes had, five of them about code or files changed in 0.5;
two of the outside review's "done" points found incomplete (the guide's messages, the plan's statuses) and one
partly (the LAME bounds checks). Not found here: the FLAC rate of 0 (R14 of 0.3..0.4) was seen but left unrun
(section 7). Whether this meets the criterion is for the user to decide.
