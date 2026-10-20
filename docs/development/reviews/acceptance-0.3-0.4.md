# Review tooling acceptance test: amisqueeze-0.3..amisqueeze-0.4 (plan step 3.5b)

## 1. Header

- **Purpose**: acceptance test of the review tooling (plan step 3.5b) on a past release, to compare with the
  outside review of 0.4 afterwards (section 9). The development session knows that review's findings: the agents
  were given none of them.
- **Commit**: `85ed34eb2114a8361016b84cbeaf1416617fa776` (tag `amisqueeze-0.4`), reviewed in a throwaway worktree
  checked out at that commit; nothing later was given to the agents.
- **Range**: `amisqueeze-0.3..85ed34e` (6 commits, 23 files, +517/-56).
- **Date**: 2026-10-20.
- **Agents**: `review-protocol-security`, `review-platform`, `review-tests-docs`, `review-plan-compliance`, in
  parallel, fresh contexts, effort `high`, the method of commit `c6413cf` (`.claude/skills/review-method`). Wall
  time 50 to 64 minutes each.
- **`make test`**: through `tools/amiga-make.sh` with a local build setup (a Linux container); green at the commit
  before the agents started (test_core 371 checks, 0 failed; fake_server 0 failed), and the Amiga build (`all`)
  too. Each agent planted its defects in its own worktree, removed afterwards.
- **Blindness check**: the agents' reports were searched for facts that only exist after 0.4 (the outside review,
  its stack figures): none found. Two agents went outside the tree they were given: `review-tests-docs` listed the
  parent directory and read the local build script (to route its own make targets; it said so);
  `review-protocol-security` read LMS 9.1 sources on GitHub as a catalogue of what a server sends.

## 2. Synthesis (findings at 75 or more, after verification)

| Dimension | Findings | Assessment |
|---|---|---|
| Architecture / Code (stack) | R1, R8 | plain `amisqueeze` runs dr_flac on the Shell's stack; the stack measurement is stale |
| Tests (safety nets) | R2, R3, R6, R7 | UBSan never fails a run; several 0.4 branches are never executed by `make test` |
| Security / Code (server input) | R14, R15 | a FLAC rate of 0 stalls a track; ICY metadata of direct streams reaches the decoder |
| Plan | R4, R5, R9, R10, R11 | 0.4's deliveries not reflected in the statuses; a done step claims what the code does not do |
| Compliance | R12 | a local launcher name in the public plan |
| Documentation | R13 | messages printed without `VERBOSE` missing from the user guide |

## 3. Findings at 75 or more

Ranked by score. "Found by" names the agents and the score each gave; the score here is the one after
verification.

**R1. Plain `amisqueeze` runs dr_flac on the user's Shell stack, at every start-up** (score 90, before 0.4)
- dimension: Architecture / Code
- file / line: `Makefile:28`, `:33-34` (`__stkinit` only for `.fpu`); `src/amisqueeze/amiga.c:53-57` (`__stack`
  under `#ifdef AMISQUEEZE_MP3`); `src/amisqueeze/player.c:241` (`decide("FLAC", ..., flac_speed, ...)` at every
  start); `third_party/dr_flac/dr_flac.h:8053` (`drflac_init_info init;` on the stack), `:6312` (it holds a
  `drflac_bs` with a 4 KB cache, `DR_FLAC_BUFFER_SIZE` 4096 at `:233`)
- evidence: `-fstack-usage` at the release flags: `drflac_open_with_metadata_private` 4,608 bytes, 8,992 bytes
  below `drflac_open`; the plain link has no `__stkinit` nor `__stack` (`nm`). `decoder.h:10`: "State is allocated,
  never on the stack (the Shell's 4 KB stack)"; the user guide asks for no `Stack` setting.
- failure scenario: `amisqueeze SERVER=...` from a default 4 KB Shell -> the FLAC speed measure (default
  `FLAC=AUTO`) opens dr_flac -> more than 4 KB below the stack base -> silent memory corruption (CLAUDE.md:191).
- rule: CLAUDE.md:191-194; A3
- proven or inferred: frame sizes proven (static, release flags); the corruption is **to confirm** in WinUAE
  (`Stack 4096` and `Stack 2048` Shell, plain build, `Avail` before and after).
- suggestion: give the plain build `__stack` and `__stkinit` too, then redo the stack check on both executables.
- found by: review-platform (90), review-protocol-security (75, inferred), review-plan-compliance (75, inferred,
  0.4's added frame only).

**R2. UBSan reports but never fails `make test` or the CI; the sanitizers miss overflows between struct fields**
(score 90)
- dimension: Tests
- file / line: `Makefile:42-43` (claim), `:46-48` (flags); `src/amisqueeze/player.h:64, 66, 162, 172`
- evidence: "AddressSanitizer and UndefinedBehaviorSanitizer stop the program at the first memory error or
  undefined behaviour"; flags `-fsanitize=address,undefined` without `-fno-sanitize-recover`. A planted signed
  overflow in `ring_commit` printed "runtime error" and `make test` exited 0. A planted overrun of
  `stream.request[1024]` by `memcpy` into the next fields of `struct player` gave no sanitizer report at all.
- failure scenario: undefined behaviour (a shift or overflow in sample conversion or protocol parsing) prints one
  line and the CI is green; the m68k build may then miscompile it.
- rule: A2, A4
- proven or inferred: proven (planted defects)
- suggestion: `-fno-sanitize-recover=all` on the host build; length tests at each network-filled buffer's limit.
- found by: review-platform (90), review-protocol-security (75)

**R3. The AHI mode check wired in `player_main` is never executed by `make test`** (score 100)
- dimension: Tests
- file / line: `src/amisqueeze/player.c:231-235`; `tests/stub_platform.h:42-43`, `tests/stub_platform.c:258-265`
- evidence: gcov `#####` on 231-235; `stub_audio_mode` appears 0 times in `tests/test_core.c`; replacing the branch
  with `if (0 && ...)`, or removing `audio_check_mode(...)`, keeps `make test` green.
- failure scenario: the warning call dropped or the `VERBOSE` line broken -> the user loses the warning, the tests
  stay green.
- rule: CONTRIBUTING.md ("add tests for what you change in the portable core"); CLAUDE.md:170-171
- proven or inferred: proven
- suggestion: a test_core test of the audio-open part of `player_main` with `stub_audio_mode_known` set, `VERBOSE`
  on and off.
- found by: review-tests-docs (100), review-plan-compliance (50)

**R4. Step 1.10 marked "to do" although two of its parts shipped in 0.4** (score 100)
- dimension: Plan
- file / line: `docs/development/README.md:66`; `docs/development/phase-1-amisqueeze.md:479`
- evidence: "| 1.10 amisqueeze: finishing touches | ⬜ versions 0.4 to 0.6 |"; the same plan says FIXED volume
  and the AHI warning are done (phase-1:502, :515).
- failure scenario: the status table shows 1.10 untouched after 0.4.
- rule: CLAUDE.md:54-55 (keep the statuses up to date)
- proven or inferred: proven
- suggestion: 🔶 with what is done.
- found by: review-plan-compliance (100)

**R5. The open point "AHI mode warning" is still open after its delivery** (score 100)
- dimension: Plan
- file / line: `docs/development/README.md:103-104`
- evidence: "**Moved to 1.10**, after version 0.3"; the MP3 gapless open point next to it was marked done.
- failure scenario: the user, who decides the open points, sees it pending.
- rule: CLAUDE.md:54-55, :72-73
- proven or inferred: proven
- suggestion: append "done on 2026-10-20 (1.10, version 0.4)".
- found by: review-plan-compliance (100)

**R6. The bounds checks and special cases of `read_lame_header` (new in 0.4) are never exercised** (score 90)
- dimension: Tests
- file / line: `src/amisqueeze/mp3.c:80`, `:85-105`, `:109`, `:111`; also `src/amisqueeze/stream.c:91-94`
- evidence: gcov: lines 89, 91, 101, 103, 105 never run; the CRC and MPEG-2 mono offsets never taken; all guards
  removed together, `make test` green; the request-length guard removed, green too. The plan claims these cases
  (phase-1:457, :460-461).
- failure scenario: an inverted CRC term -> every CRC-protected MP3 plays its Info frame and is not trimmed, the
  tests (no CRC file) still pass.
- rule: CONTRIBUTING.md (tests for core changes); A2
- proven or inferred: proven
- suggestion: unit tests on synthetic first frames (CRC, MPEG-2 mono, no count, no extension, truncated,
  frames 0) and a `strm s` with a 1025-byte request.
- found by: review-tests-docs (90), review-protocol-security (75)

**R7. The "file cut short" test of `ahi_prefs_unit` stops on a chunk boundary** (score 85)
- dimension: Tests
- file / line: `tests/test_core.c:1589`, `:1597-1598`; `src/amisqueeze/audio.c:357`, `:365-367`, `:372`
- evidence: `cut[100]` ends where a chunk ends, so the loop stops on `pos + 8 <= end`; the "chunk cut short" break,
  `size < 12`, `len >= 12` and the odd padding are never taken (gcov); all removed, green.
- failure scenario: a prefs file cut inside a chunk with the guard broken -> reads past the data, wrong mode.
- rule: CONTRIBUTING.md; A2 (plan phase-1:517 "host-tested ... on a file cut short")
- proven or inferred: proven
- suggestion: cut inside a chunk, a huge and an odd chunk length, an 8-byte file.
- found by: review-tests-docs (85)

**R8. The stack measurement is stale for both executables** (score 80)
- dimension: Compliance
- file / line: CLAUDE.md:59 ("2 KB stack OK", step 1.4c), :191-194; `phase-1-amisqueeze.md:171-172`, :422-423
- evidence: `git log -S"Stack 2048"`: 8ecb2cf (1.4c, before any decoder) and 072d62f (`.fpu` `BENCH` only); the
  commits since that deepen the paths: 912e19a, 47fb560 (dr_flac, start-up bench), 148ba47, 6977609, 4ea7d70
  (`struct audio_mode` in `player_main`, `AHI_GetAudioAttrsA` on our stack). No later measurement recorded.
- failure scenario: see R1; any stack figure quoted for 0.4 does not describe the shipped executables.
- rule: CLAUDE.md:193-194 ("Redo it whenever call depth or local variables grow"); A3
- proven or inferred: proven (history); the measurement itself is outside the repository
- suggestion: `Stack 2048` and `Stack 4096` runs of both executables (start-up, FLAC with a seek, MP3), recorded
  with date and variant.
- found by: review-platform (80), review-plan-compliance (75), review-protocol-security (75)

**R9. Steps 1.4 and 1.5 keep statements 0.4 made false** (score 75)
- dimension: Plan
- file / line: `phase-1-amisqueeze.md:186-187`, `:214-216`
- evidence: 1.4 "Not done: the start-up warning for a mode that cannot pan"; 1.5 "fixed volume ... is refused for
  a player without `HasDigitalOut` ..., so LMS never sends `adjust` 0" (0.4 advertises `HasDigitalOut=1`).
- failure scenario: a reader treats the `adjust` 0 path as dead code, or the warning as missing.
- rule: CLAUDE.md:54-55
- proven or inferred: proven
- suggestion: a forward reference to 1.10 in both places.
- found by: review-plan-compliance (75)

**R10. Step 3.3's Done list stops at 0.3** (score 75)
- dimension: Plan
- file / line: `phase-3-release.md:24-29` against `docs/development/README.md:75` ("0.1 to 0.4 published")
- failure scenario: the release history in the plan omits 0.4; the two documents disagree.
- rule: CLAUDE.md:54-55
- proven or inferred: proven
- found by: review-plan-compliance (75)

**R11. Step 1.2, marked done, claims `cont` and autostart "handled as in squeezelite"** (score 75, before 0.4)
- dimension: Plan
- file / line: `phase-1-amisqueeze.md:53`; `src/amisqueeze/slimproto.c:216-222`, `:317-318`
- evidence: plan "`cont` and `autostart` handled as in squeezelite"; code "would wait for "cont" (radio metadata):
  not supported yet", autostart 2/3 lowered by 2, `cont` falls to "not handled".
- failure scenario: the plan hides the radio limit (R15) from whoever plans the next steps.
- rule: A2; CLAUDE.md:54-55
- proven or inferred: proven
- found by: review-protocol-security (75)

**R12. The public plan names a local launcher script** (score 75, before 0.4)
- dimension: Compliance
- file / line: `phase-1-amisqueeze.md:405` (since ad74a1b)
- rule: CLAUDE.md:8-10 (nothing specific to the developer's workstation; English)
- proven or inferred: proven
- suggestion: "a WinUAE profile with the JIT (32-bit addressing, no cycle-exact)".
- found by: review-plan-compliance (75)

**R13. Messages printed without `VERBOSE` are missing from the user guide** (score 75, before 0.4)
- dimension: Documentation
- file / line: `docs/user-guide.md` Messages table, against `amiga.c:255, 271, 408, 509`, `player.c:88, 249, 258`,
  `slimproto.c:49, 231, 263, 305, 318, 328, 370, 393`, `stream.c:92, 614, 620, 665, 692`
- evidence: "not enough memory for the audio output", "timer.device", "invalid packet size", "headers too long",
  "not handled": 0 occurrences in the guide.
- failure scenario: a user who sees one of them finds no explanation.
- rule: CLAUDE.md:55-56 (messages documented in the user guide)
- proven or inferred: proven (grep)
- found by: review-tests-docs (75), review-protocol-security (50)

**R14. A FLAC whose STREAMINFO rate is 0 stalls the track for good** (score 75)
- dimension: Security / Code
- file / line: `src/amisqueeze/flac.c:89`, `:99`, `:175`; `src/amisqueeze/stream.c:148-165`
- evidence: the rate is taken without a check (dr_flac does not reject 0); `sink_space` then gives 0 bytes per
  tick. Planted file in the fake server: STMs, DSCO, STMd, then STMt with elapsed 0 and no STMu after 8 s for a
  1.4 s track.
- failure scenario: a damaged FLAC leaves the player "playing" at 0:00; LMS never moves to the next track.
- rule: A6
- proven or inferred: proven on the virtual output; the AHI path (rate 0 to the device) is to confirm
- suggestion: skip the track when the rate is 0.
- found by: review-protocol-security (75)

**R15. On a direct stream with ICY metadata, `cont` is ignored and the metadata goes to the decoder** (score 75)
- dimension: Security / Code
- file / line: `src/amisqueeze/slimproto.c:216-222`, `:317-318`; `src/amisqueeze/stream.c:642-670`
- evidence: LMS asks radios for `Icy-MetaData` and adds 2 to autostart on direct streams, then sends `cont` with
  `metaint`; amisqueeze lowers autostart, logs "not handled" and stores every byte in the ring.
- failure scenario: `amisqueeze.fpu` plays an HTTP MP3 radio that LMS streams directly -> a metadata block every
  `metaint` bytes between MP3 frames -> periodic glitches, no title; the user guide does not say so.
- rule: A6, A7
- proven or inferred: **to confirm**: depends on LMS choosing direct streaming; a `VERBOSE` log with an HTTP MP3
  radio showing "autostart 3 (cont) handled as 1" then "cont not handled".
- found by: review-protocol-security (75)

**Lowered in verification**: review-plan-compliance's "1.8d content never recorded as agreed, and the ~25 ms
dropped after a seek not marked as agreed" (75 -> 50): the rule asks for validation with the user, not for a
written record, and the Done text states the seek behaviour itself; what remains is a Content line the Done text
contradicts (50 list).

## 4. Compliance table (merged)

| Unit | Status | Proof |
|---|---|---|
| 1.8d MP3 gapless | PARTIAL | `scenario_mp3`, `scenario_mp3_gapless`, `scenario_mp3_continuous`, `scenario_mp3_seek_lms`; missing: R6 paths; listening OUTSIDE REPO |
| 1.10 fixed volume (`HasDigitalOut=1`) | COVERED | `slimproto.c:24`, `audio.c:331-344`, `test_audio_volume`, fake_server HELO check; LMS UI OUTSIDE REPO |
| 1.10 AHI mode warning | PARTIAL | `test_ahi_mode`; missing: R3, R7; `plat_audio_mode` OUTSIDE REPO |
| 1.2 `cont` / autostart | PARTIAL | autostart 0/1 (`test_autostart_0`); missing: R11 |
| Release 0.4: version strings, `$VER:` date, `-dev` sequence, zip, tag, CHANGELOG | COVERED | `amiga.c:40, 42`, `slimproto.c:24`, `user-guide.md:15`; `make dist` contents; CHANGELOG lines per commit |
| Release 0.4: GitHub Release, notes, CI run, `Version FULL` in WinUAE | OUTSIDE REPO | - |
| CLAUDE.md statuses kept up to date | NOT COVERED | R4, R5, R9, R10, R11 |
| CLAUDE.md stack rule and its re-measurement | NOT COVERED | R1, R8 |
| CLAUDE.md `.fpu` `__stack` + `__stkinit`, `__fpucheck` | COVERED | `amiga.c:45-57`, `Makefile:34`, link map |
| CLAUDE.md AHI rules (link, abort order, full volume) | COVERED | `audio.c:166-208`, `amiga.c:345`; abort order caught by `stub_audio_bad` (planted) |
| CLAUDE.md portable core, `msg()` only, byte order, HELO formats | COVERED | include and call greps; `scenario_flac_off`; no script checks them |
| CLAUDE.md ASCII strings | PARTIAL | sources ASCII; a server-given player name is printed as received |
| CLAUDE.md nothing workstation-specific | PARTIAL | R12 |
| CLAUDE.md `make test` with ASan and UBSan | PARTIAL | ASan fails a run (planted); UBSan does not (R2) |
| CLAUDE.md check-saves safeguard | PARTIAL | positive control red; misses `dbra`, `(an)+`/`-(an)`, fp2-fp7 (50 list) |
| CLAUDE.md user guide updated with each change | COVERED for 0.4 | gapless, warning, fixed volume; older messages R13 |

## 5. The 50 list

- `check-saves.py` misses writes through `dbra`, `(an)+`/`-(an)` and to fp2-fp7 (planted, green).
- CI and `make test` never compile `amiga.c` nor the non-MP3 configuration shipped as `amisqueeze` (documented).
- `PORT` truncated silently (65536 -> default port), `UNIT` not range-checked, prefs cut at 510 characters, `NAME`
  at 64.
- `plat_audio_done` ignores `io_Error` (inferred).
- Start-up speed measures cannot be interrupted (about 1 s each).
- "About 1.1 MB" of memory in the user guide is probably low (inferred).
- A request over 1 KB is dropped silently after STMf; the HTTP status is never checked; headers over 2047 bytes
  fail the stream; chunked transfer unsupported (LMS uses HTTP/1.0).
- A FLAC file with a leading ID3v2 tag is skipped.
- `body`, `serv`, `setd` ids other than 0 get no reply; `strm p` with an interval, crossfade, the flags and replay
  gain are ignored, none of it in the user guide.
- `MAX_TRACKS` overflow unreachable with LMS; Music Assistant unchecked.
- `stream.c` carries HTTP, queue, dispatch, sinks and output: split it before ICY or Music Assistant work.
- `memset(&info)` and `!st->first_seen` in `mp3.c` not isolated by any test; a Xing frame count of 0 plays nothing.
- 1.8d: the Content line ("nothing dropped" on a seek) contradicts the Done text (about 25 ms dropped); the 25 ms
  are not in the user guide.
- Only lame 3.100 files tested for "LAME and compatible encoders".
- CHANGELOG 0.4 omits the `VERBOSE` line "volume: fixed at 100 % by LMS" and the "Audio Startup Time" side effect.
- CLAUDE.md:131 says the `mp3 pcm` rule is in the README (it is in the user guide); user-guide.md:3-4 says the
  README covers building (CONTRIBUTING does).
- The zip's README links do not resolve inside the zip (since 0.3); the user guide in the zip holds non-ASCII
  characters.
- No script checks that the version agrees in its four places.
- Workstation hints in CONTRIBUTING (proxy CA variable) and the plan (container proxy), borderline.
- 1.10's delivered parts have no stated criterion.

## 6. Checked and found solid

- Framing and length checks of every opcode; the `setd` clamp backed by a working sanitizer (planted, red).
- Ring arithmetic, 64-bit positions, the ring / output position split; no reachable division by zero.
- MP3 staging, ID3 handling, LAME parsing reads bounded; FLAC header parser; `ahi_prefs_unit` bounds (its
  `end > size` clamp caught by ASan when removed).
- Gapless trimming: start drop, end clamp, `NO_LIMIT`, header skip each caught when removed; fixed volume and both
  AHI warnings caught when removed.
- AHI request life cycle (`CheckIO`/`WaitIO`, `AbortIO`/`WaitIO`), error paths of `main` and `plat_audio_open`,
  sockets closed on every failure path.
- `.fpu` stack: `__stkinit` linked; minimp3's deepest chain about 17 KB (static) under 32 KB.
- Version strings, CHANGELOG, zip contents, licences (LAME parsing written from a CC0 reference).

## 7. Limits

- No WinUAE, real Amiga, LMS or Music Assistant: every runtime stack, memory, AHI and bsdsocket behaviour is
  inferred or OUTSIDE REPO; stack figures are static (`-fstack-usage`), without indirect calls, libnix, AHI or TCP
  code.
- CI on GitHub not run; Docker path of `amiga-make.sh` not run.
- Some single mutations ran on subsets of the suite (review-tests-docs); LMS's `MP3.pm` and Music Assistant not
  read (review-protocol-security); `amiga.c` / `posix.c` beyond the range read only by review-platform.
- The verification pass was done by the development session, which knows the outside review: scores are given as
  the agents stated them.
- All agents answered all their questions.

## 8. Decisions

Acceptance test on a past release: these findings are carried to the review of HEAD (step 3.5e), where each is
fixed through a plan step or accepted by the user.

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
| R9 | |
| R10 | |
| R11 | |
| R12 | |
| R13 | |
| R14 | |
| R15 | |

## 9. Comparison with the outside review of 0.4 (written after sections 1 to 8)

Sources: "Outside review" in `docs/development/README.md`, and the commits of 0.5 that fixed its points
(`a2e132a`, `c360f37`, `905d70f`, `03d0a52`, `16ade65`). "Found" counts only when an agent itself scored the point
75 or more.

| Outside review point | Here | Result |
|---|---|---|
| `amisqueeze` (68020) on the Shell's 4 KB stack since 0.2 | R1 (90) | found |
| Stack measurement made before dr_flac, never redone | R8 (80) | found |
| UBSan reports without failing | R2 (90) | found |
| AHI mode wiring at start-up never run by the tests | R3 (100) | found |
| "File cut short" test stops before its guard | R7 (85) | found |
| LAME header fallbacks (no count, no extension, CRC, MPEG-2 mono, cut short) untested | R6 (90) | found |
| A frame count of 0 dropped the whole track | 50 list (inferred, not run) | under-scored |
| Messages printed without `VERBOSE` missing from the user guide | R13 (75) | found |
| Plan statuses: 1.10, the open points, 3.3 | R4, R5, R9, R10 (75-100) | found |
| A local launcher script named in the plan | R12 (75) | found |
| User guide: "building is in the README" | 50 list | found (at 50) |
| ICY metadata of direct streams (planned for 0.6) | R15 (75, to confirm) | found |
| Track queue forgets the oldest entry when full | 50 list | found (at 50, as the outside review) |
| Request over 1 KB refused without telling LMS | 50 list | found (at 50) |
| Ctrl-C and the TCP/IP stack's break mask | 50 list, limits | found (at 50) |
| `PORT` / `UNIT` not range-checked; prefs line cut at 510 | 50 list | found (at 50) |
| CI builds neither the Amiga executables nor the non-MP3 configuration | 50 list | found (at 50) |
| `stream.c` to split first | 50 list | found (at 50) |
| `gethostbyname()`: empty address list, length other than 4 not checked | - | **missed** |
| `make dist` checks the first `$VER:` only | 50 list, only as "no script checks the four version places" | partly |
| CI workflow without a `permissions:` block | - | **missed** |
| Malformed server input (cut frames, `strm` fields out of range, long headers) not tested | test gaps listed for the LAME header and AHI prefs only | partly |
| Comment: `make dist` contents | 50 list | found (at 50) |
| Comment: `AUDIO_BLOCK_SIZE` "a multiple of every frame size" (false for 24-bit frames) | - | **missed** |
| Comment: where the AHI preferences are read | - | **missed** (minor) |

**Found here, not in the outside review**: R11 (step 1.2 claims `cont` and autostart are handled), R14 (a FLAC
rate of 0 stalls the track, proven); in the 50 list: `check-saves.py` misses `dbra`, `(an)+`/`-(an)` and fp2-fp7
(proven), sanitizers blind to overflows between fields of `struct player` (proven), FLAC with a leading ID3v2 tag
skipped, HTTP status never checked, `io_Error` ignored, replay gain and other ignored `strm` fields undocumented,
the 1.8d Content / Done contradiction, CHANGELOG omissions, wrong cross-references (CLAUDE.md:131), README links
inside the zip, non-ASCII characters in the shipped guide.

**Against the criterion of 3.5b** ("finds what the outside review found at 75 or more"): of the points the
outside review section lists as done before 0.5, all were found at 75 or more except the frame count of 0 (seen,
scored 50, not run). Among the other fixes of `16ade65`, the "building is in the README" cross-reference was
found at 50 and two comments were missed. Of the points still open, ICY was found at 75; `gethostbyname()` and the
CI's `permissions:` were missed; what `make dist` checks and the malformed-input tests were found only in part.
Whether the criterion is met is for the user to decide.

## 10. Method questions raised by the points missed (proposals, nothing changed)

1. `gethostbyname()` (missed): questions P4/P5 of review-platform cover options and environment only. Should the
   values returned by library calls (resolver fields, `GetVar`, `Read`, AHI queries) be reviewed as untrusted
   input, field by field, before use?
2. CI `permissions:` (missed): P6 asks what the CI compiles and runs. Should it also ask with which permissions and
   secrets it runs?
3. `make dist` checks the first `$VER:` only (partly): should the release scripts count as safety nets under P3,
   proven by a planted version mismatch?
4. False comments, e.g. `AUDIO_BLOCK_SIZE` (missed): no agent owns the factual claims of code comments, although
   A2 covers claims. Should one agent check the claims with "every", "always", "a multiple of", "never" in the
   comments of the files the range touches?
5. Frame count of 0 (under-scored): the agent left it inferred at 50 although a host test could settle it. Should
   the method say that an inferred finding a host test can settle is run before it is scored?
6. Malformed-input tests (partly): the agents run in parallel, so review-tests-docs cannot use the server-input
   guards review-protocol-security lists. Should review-tests-docs list those guards itself and ask, for each,
   which test feeds the failing value?
7. Wrong cross-references in the user documentation (scored 50, fixed by the outside review): are they 50, or 75
   under CLAUDE.md:55-56?
