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
fixed through a plan step or accepted by the user. Their fate is recorded in the plan (`docs/development/README.md`, the
0.6 list: fixed in 0.6, or moved to 0.7) and in the [0.6 review](0.6.md); the table below is left as it was.

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

## 11. First replay, 2026-10-20: the method changes only

### Decisions on the questions of section 10 (taken with the user, 2026-10-20)

All seven accepted, worded as classes of defect rather than as the items missed, so that a replay is not handed
its answer:

| Question | Change | File |
|---|---|---|
| 1. Values handed back by system calls | P5 rewritten: every value a system or library call hands back (structures and each field read, lengths, counts, lists, strings without a terminator) checked before use | `review-platform.md` |
| 2. CI | P6 completed: triggers, the rights its token gets, the secrets it can reach, the third-party code it pulls | `review-platform.md` |
| 3. Release scripts | P3 completed: release scripts are safety nets, one planted mistake of each kind they should stop | `review-platform.md` |
| 4. Comments | A2: code comments are claims; scoring: a false comment is 75 when it states a limit or invariant the code would rely on, 50 otherwise; new S7 (factual claims of the comments in the files the range touches) | `review-method`, `review-protocol-security.md` |
| 5. Inferred but settleable | Scoring: an inferred finding the host build can settle is run before it is scored | `review-method` |
| 6. Malformed input | T5 rewritten: list the core's checks on what a server or a file sends from `src/`, name the test that feeds each one its failing value, remove the untested ones | `review-tests-docs.md` |
| 7. Wrong pointers in the user documentation | Scoring: a statement of the README or user guide proven false at the reviewed commit is 75 (the rule is new: CLAUDE.md:54-55 asks the guide to follow visible changes, not for correct pointers) | `review-method` |

"Found" criteria, fixed before the replay: (1) the resolver's result used unchecked, at 50 or more; (2) the missing
`permissions:` block, at 50 or more; (3) `make dist` checks the first `$VER:` only, at 50 or more (not "no script
checks the four places"); (4) the `AUDIO_BLOCK_SIZE` comment shown false, at 50 or more; (5) the frame count of 0,
proven by a run, at 75 or more; (6) at least two server-input checks shown untested by their removal, at 50 or
more; (7) re-scored, no replay.

### What ran

- Same worktree, commit and prompt as the first pass (one change: "wrap up near 70 minutes" instead of 50; stop
  rule 80 minutes), effort `high`.
- **The agents ran their old definitions**: Claude Code loads `.claude/agents/` when the session starts, and the
  definitions were edited during it; the agents' transcripts hold the old P5, T5 and S6 and no S7. Only the method
  file, read from disk, was new. So this replay tests changes 4 (A2 and scoring part), 5 and 7 only; changes 1,
  2, 3, 6 and S7 were not tested.
- `review-platform` was stopped once this was seen (none of its questions had changed); `review-plan-compliance`
  was not replayed (nothing in its scope changed).
- `review-protocol-security`: 21 minutes. `review-tests-docs`: 35 minutes. Both baselines green (371 checks,
  fake_server 0 failed); worktrees removed.
- Blindness check: the transcripts show no read outside the tree, the method and the agents' own files, and no fact
  that exists only after 0.4; as in the first pass, review-protocol-security fetched LMS 9.1 and squeezelite
  sources from GitHub as a catalogue of what a server sends.

### Results against the criteria

| Question | Result |
|---|---|
| 4. False comment | **Found at 50** by review-protocol-security, from the method alone (S7 was not loaded): `player.h:119` "a multiple of every frame size", false for 24-bit frames (32768 % 6 = 2, % 3 = 2); scored 50 under the new rule, the code rounding the block down to whole frames (`stream.c:446-448`). The comment on where the AHI preferences are read (`audio.c`) was not found. |
| 5. Frame count of 0 | **Not found**, not even at 50 this time: review-tests-docs lists "`left == 0` with frames still arriving" among the branches `make test` never reaches, without the consequence (a track that plays nothing). |
| 7. Wrong pointers | **Met**: review-tests-docs scores `user-guide.md:3-4` ("how to build them" in the README) 75, citing the new rule; `CLAUDE.md:131` stays at 50 (not user documentation, consistent with the rule). |
| 1, 2, 3, 6 | Not tested (old definitions). |

### Findings, after verification (each cited line re-opened at `85ed34e`)

Points of the first pass found again (first pass score -> this replay's score):

- R3 (AHI mode check never run by `make test`): 100 -> 75.
- R6 (`read_lame_header` branches unexercised): 90 -> 75.
- R7 ("file cut short" test stops on a chunk boundary): 85 -> 75.
- R8 (stale stack measurement): 80 -> 75, here as the 2 KB Shell check of step 1.4 not redone.
- R11 and R15 (`cont`, autostart, ICY): 75 and 75 -> one finding at 75.
- R13 (messages missing from the user guide): 75 -> 75.
- R14 (FLAC rate 0 stalls the track): 75 -> **50**, although proven (60 s measured, no STMu): a proven defect that
  stalls a track was scored 50.
- R2, its second half (sanitizers blind to an overrun of `stream.request[1024]` into the next fields of
  `struct player`): 90 merged (75 from this agent) -> 75: the same planted defect, now through its network trigger
  (a `strm s` request over 1024 bytes with the bound at `stream.c:91-94` disabled: `make test` green, no ASan
  report).
- The pointer of `user-guide.md:3-4`: 50 -> 75 (rule 7).

Scores moved down by one step for most points (100/90/85/80 -> 75); one run each way does not tell whether this is
the method or the run.

New at 75:

- **N1. `strm f` stops the track that is playing** (`slimproto.c:242-245`: `case 'q': case 'f': stream_stop(p);`),
  score 75, before 0.4. LMS sends `f` while playing to drop only the track being streamed (removing, inserting or
  moving the next track, reshuffling; `StreamingController.pm`, `_FlushGetNext`); amisqueeze clears the ring and
  stops the output, cutting the current track. Proven on the player side (a unit test in the agent's worktree:
  0.7 s of track A left, `strm f`, output STOPPED, ring empty); the LMS side read in LMS 9.1, to confirm with a
  real server (remove the next track during the last seconds of one). Not in the outside review.

New in the 50 list: `serv` ignored ("switch server" does nothing); synchronisation unsupported and undocumented;
server strings (the `setd` name) printed raw; the AHI preferences format parsed in the portable core; the
"Firmware=" line and the four version places checked by no test (a planted `Firmware=0.3` stayed green);
`scenario_mp3_seek_lms` does not see the header frame played as audio after a seek; the SNR around the MP3 cut
asserted at 20 dB, not "the same" as the plan says.

### Assessment

The method changes worked where they were tested: rule 7 scored as decided, rule 4 found the comment without its
question. The frame count of 0 was not found in this run (the first pass saw it at 50, this one did not see it):
one run each way does not tell whether rule 5 failed or the run varied; R14, proven and scored 50, points the same
way. Changes 1, 2, 3, 6 and S7 need a replay from a session started after the definitions were changed.

Method and agents used: those committed with this section (the first pass used those of `c6413cf`); none of the
agents read the saved diff of the change.

## 12. Second replay, 2026-10-20: the changed questions of three agents

### What ran

- From a session started after the definitions were changed (`fe65eb5`, `6fa3a63`): `review-platform` (P3, P5,
  P6: questions 1 to 3), `review-tests-docs` (T5: question 6), `review-protocol-security` (S7: question 4).
  `review-plan-compliance` not replayed (nothing in its scope changed).
- Same worktree (`85ed34e`, `amisqueeze-0.4`, built `all` and `test` before the launch: green), same prompt as the
  first replay (taken from its transcript), effort `high`; nothing in the prompt about any finding.
- One minute after the launch, each transcript held the new wording: P3 "Release scripts are safety nets too", P5
  "hands back", P6 "the rights its token gets" (review-platform); T5 "reading `src/`, not the tests"
  (review-tests-docs); S7 (review-protocol-security).
- `review-platform`: 21 minutes; `review-protocol-security`: 24 minutes; `review-tests-docs`: 35 minutes; none near
  the 80-minute stop. 47 to 70 turns and 7 to 9.4 million tokens read from the cache each. All three baselines
  green (371 checks, fake_server 0 failed); their worktrees removed. Every question of the three agents answered.
- Blindness check (the tool calls of the three transcripts scanned for paths outside the tree, later refs and
  tags): one breach, declared by the agent itself: `review-tests-docs` read the first five lines of
  `../amiga-make.local.sh` (the local build launcher, outside the root) while finding out how `make` runs; nothing
  from it is used in its findings. Otherwise no read outside the tree, the method and the agents' own scratch
  files, no `main`, `HEAD`, `--all` or later tag; review-protocol-security fetched LMS 9.1 sources (`public/9.1`)
  as a catalogue of what a server sends, as in both earlier runs. The one fact after the range in a report (0.5 =
  `LOG`, Workbench start) is in the plan at `85ed34e` (`docs/development/README.md:89`).

### Results against the criteria (fixed before the first replay, section 11)

| Question | Result |
|---|---|
| 1. Values handed back by system calls | **Found, at a score off the scale**: review-platform (P5) names `amiga.c:134-136`, `gethostbyname()` with `h_addr_list[0]` and `h_length` unchecked, and lists it at "25-50", which is not a score of the method; the criterion (50 or more) is not met as fixed. The new P5 reached the class (it also lists `PORT/N` cut to 16 bits, the AHI mode name not forced to end with a NUL, `io_Actual` clamped). For the user to decide. |
| 2. CI | **Met at 50** (review-platform, P6): no `permissions:` block in `tests.yml`, the token's rights left to a repository setting; also `actions/checkout@v5` pinned by tag, not by SHA. |
| 3. Release scripts | **Met at 75**, above the criterion (review-platform F4): `tools/make-dist.py:20` reads only `$VER: amisqueeze`, and its docstring (lines 4-5) claims "the archive always matches the executable". Proven by six mistakes planted in one run, none stopped: `amisqueeze.fpu` at 0.3, a `-dev` version, no CHANGELOG section, `Firmware=0.2`, the guide at 0.4, an image the guide links deleted (left out of the zip without a word). |
| 4. False comments | **Met at 50** (review-protocol-security, S7): `player.h:119` "a multiple of every frame size" (32768 % 6 = 2), scored 50 since the code rounds down (`stream.c:448`). Also found false at 50: `audio.c:80-81` (43 cycles per multiplication is about 13.5 % of a 28 MHz CPU, not a third) and `mp3.c:27` (16 KB is about 15.7 frames at 320 kbit/s, not 10). The comment on where the AHI preferences are read (`audio.c:7-8`, cited as 4-6) was found at 50 by review-tests-docs, not by S7: the "missed (minor)" row of section 9 is now found. |
| 6. Malformed input | **Met at 50**, with new findings above it (review-tests-docs, T5): the core's checks on what a server or a file sends, listed from `src/`, and the unfed ones removed. About twenty server-input checks are never fed a failing value: `strm`, `audg` or packet too short, PCM fields out of range, `setd` name over 64 bytes, request over 1 KB, headers too long or cut, connection lost, not FLAC, more than 2 channels, the 35 s silence. Removed in **one** batch run (green), not one by one; gcov shows the failing side of each never runs, which makes one run enough. Scored 50. The guards of `read_lame_header` (an MP3 sent by the server) are scored 75, those of the `ahi.prefs` parser (a local file) 100. |
| 5. Frame count of 0 | Not targeted by this replay; **not found** (no report names a Xing frame count of 0 with its flag set; review-tests-docs' "no frame count" is the flag clear, `mp3.c:88-89`). |

### Findings, after verification (each cited line re-opened at `85ed34e`)

All cited lines checked; three corrected: the AHI preferences comment is at `audio.c:7-8`; in review-platform's F1
the Makefile lines are 30-34 (`__stkinit` for `amisqueeze.fpu` only), in its F2 the comment is at `Makefile:42-43`.
Merges: review-protocol-security's F3 (stack not re-checked for 0.4) into review-platform's F1, which has the
evidence; the "no script checks the version places" items of the two other 50 lists into review-platform's F4.

Points of the first pass found again (first pass -> first replay -> this replay):

- R1 / R8 (`amisqueeze` on the Shell's 4 KB stack, stale measurement): 90 / 80 -> 75 -> **100** (review-platform
  F1). No `__stack` or `__stkinit` in the 68020 build (`Makefile:30-34`, `amiga.c:53-57`), and `-fstack-usage` on
  the Amiga build puts the start-up FLAC measurement at about 5.6 KB (`drflac_open_with_metadata_private` 4608
  bytes). The need is proven; the corruption is to confirm (the outside review confirmed it).
- R2, first half (UBSan reports without failing): 90 -> not tested -> **75** (review-platform F2). A left shift of
  a negative value planted at `audio.c:142` printed `runtime error`, and the run stayed green; the comment at
  `Makefile:42-43` ("stop the program at the first ... undefined behaviour") is false.
- R3 (AHI mode check at start-up never run, `player.c:231-237`; stub hook `stub_audio_mode` never set): 100 -> 75
  -> **100**.
- R7 ("file cut short" test ends between chunks, `test_core.c:1589,1597-1598`; `audio.c:365-366` never run): 85 ->
  75 -> **100**. Without the guard the run stays green; a 110-byte cut gives an ASan stack-buffer-overflow.
- R6 (`read_lame_header` variants: CRC, MPEG-2 mono, no count, no extension, `mp3.c:80-112`): 90 -> 75 -> **75**.
- R13 (messages printed without `VERBOSE` missing from the user guide): 75 -> 75 -> **75**. About twenty, plus
  `user-guide.md:341`, which shows `stream: …` as `VERBOSE` only while the stream errors print without it.
- R11 / R15 (`cont`, autostart 2/3, ICY): 75 -> 75 -> **75** (review-protocol-security F1 and F2). The metadata
  bytes going to the MP3 decoder are proven with a fake-server scenario (about 110 ms lost per metadata block); the
  LMS trigger is inferred (direct streaming depends on preferences whose default was not found).
- `check-saves.py` misses `dbra`, `(an)+`, `-(an)`, `movem` loads and pops: 50 list -> not tested -> **75**
  (review-platform F3), proven by planted defects; a stricter checker finds no hit in 0.4, so the gap is latent.
  fp2-fp7 stays at 50.
- R14 (a FLAC rate of 0 stalls the track): 75 -> 50 -> **50**, proven again (176 bytes written, then nothing, no
  STMu in 6 s) and scored 50 again.
- In the 50 list again: the track queue that forgets its oldest entry, a request over 1 KB, the HTTP status never
  checked, replay gain and the other ignored `strm` fields, `serv`, `PORT`/`UNIT`, the prefs line cut at 510, CI
  coverage of the Amiga build, `stream.c` to split, the README links inside the zip, the 1.8d Content / Done
  contradiction, `CLAUDE.md:131`.
- The pointer in `user-guide.md:3-4` ("how to build them" in the README): 50 -> 75 -> **50**: review-tests-docs did
  not apply rule 7 this time. Re-scored **75** at verification under that rule (a statement of the user guide
  proven false at the reviewed commit).
- **Not found this time**: N1 (`strm f` stops the playing track, found by the first replay); review-protocol-security
  lists `q` and `f` as handled the same way, without the consequence. As in section 11, one run each way does not
  tell the method from the run.

New in the 50 list:

- the fake server throws away the ASan report of a crash at start-up (`stop()` in a `finally`), and shows only a
  `TimeoutError`;
- `test_core` never calls `flac_start`;
- removing `memset(&info)` (`mp3.c:250`) or `!st->first_seen` (`mp3.c:257`) stays green, and the sanitizers do not
  see uninitialised reads;
- the length of `s16-320-raw.mp3` is printed but not asserted;
- 8-bit AIFF is read as unsigned (inferred);
- `p` with `?` fields (Music Assistant `wav`) gives "format p not supported", which the user guide does not explain;
- the `setd` name cut at 64 bytes can split a UTF-8 character;
- the Makefile's `dist` comment omits CHANGELOG and images.

### Assessment

The changed questions reached their classes:

- **Questions 2, 3, 4 and 6** were met, question 3 well above its criterion. The new P3 also turned the
  `check-saves.py` gaps from 50 into a proven 75.
- **Question 1** reached `gethostbyname()` through the new P5, but at a score the method does not have ("25-50"):
  against the criterion as fixed, not met.
- **Not seen in this run**: the frame count of 0 (in none of the three runs since the first pass), N1, and rule 7
  (applied in the first replay, not in this one). These vary between runs of the same method; no question changed
  them.

Method and agents used: those of `6fa3a63` (the agents unchanged since `fe65eb5`).

Decided with the user after this replay (2026-10-20): question 1 counts as found (P5 reached the point missed;
only its score was off the scale), and the method now says that a score is one of the five values, never a range,
with one line on what would move it (`review-method`, under the scoring table). Not tested by this replay; the
next review applies it.
