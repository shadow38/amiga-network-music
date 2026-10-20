# Rules inventory (plan step 3.5c)

Every rule, limit and measurement of the repository's `CLAUDE.md` and `CONTRIBUTING.md`: how it is enforced
today, how it should be, and the review findings that touch it. The candidates for the mechanical checks of step
3.5d come out of it (see [Candidates for 3.5d](#candidates-for-35d)). Nothing here changes a rule: what looks
wrong or stale is in the remarks, for the user to decide (listed again in [Remarks to decide](#remarks-to-decide)).

Written on 2026-10-20. Line numbers are those of commit `2802634`. **Frozen** (decided with the user on
2026-10-20, with the two review levels): kept as a reference for the full audit; its rows and remarks are no
longer kept up to date.

## How to read it

- **Sources**: `CLAUDE.md` and `CONTRIBUTING.md` only. The method's principles (A1 to A7, in
  `.claude/skills/review-method`) are referred to, not listed. One entry per rule: a rule stated in both files is
  merged, their differences in the remarks; a paragraph whose parts are enforced differently is split.
- **IDs** are stable: a review finding can name its rule as `rules.md C12` instead of a line number. Groups: `G`
  repository, licence and language; `V` versions and releases; `P` plan, documentation and test method; `C` code
  design; `H` AHI; `S` stack and memory; `B` build and safety nets; `T` tests and CI; `E` tools and servers;
  `F` speed and sound. Rules and limits come first in each group, then its measurements.
- **Kind**: a *rule* can be broken (never, must, only, always), the development process included; a *limit* is a
  fact about something outside the code (AHI, libnix, LMS, WinUAE, a tool) that the code must respect, valid for a
  version, so it can expire too; a *measurement* is a dated figure. Left out: history and status sentences
  ("validated on…"), plain descriptions (layout, build order, VS Code setup, the debugging approach, the remote's
  design, which are decisions of the plan rather than rules).
- **Enforced today** and **should be**: `build` (a compiler or linker flag, a Makefile rule), `script` (in
  `tools/`), `test` (run by `make test`: `tests/test_core.c`, `tests/fake_server.py`), `CI`, `review` (the release
  review agents, `.claude/agents/`), `manual` (a person does it, often in WinUAE), `nothing`. **3.5d** marks a
  candidate for a mechanical check. "Seen failing" means caught a planted defect (principle A4).
- **Findings**: `0.3..0.4 R12` is finding R12 of [acceptance-0.3-0.4.md](reviews/acceptance-0.3-0.4.md),
  `0.4..0.5 R7` finding R7 of [acceptance-0.4-0.5.md](reviews/acceptance-0.4-0.5.md); `§4` is a report's
  compliance table, `§5` its 50 list (no IDs there: the gist follows).
- **Measurements**: the commit is the one `git log -S` gives for the figure's text, the one that wrote it down, not
  necessarily the build that was measured. "Depends on" names paths and symbols, so that a later check can tell
  when they changed after that commit (principle A3).

## G. Repository, licence, language

| ID | Statement (source) | Kind | Enforced today | Should be | Findings | Remarks |
|---|---|---|---|---|---|---|
| G1 | Nothing specific to the developer's workstation (local paths, proxy, personal settings, neighbouring folders); how the toolchain is installed on one machine is not described (CLAUDE.md:7-10, :168-169) | rule | review (review-plan-compliance) | script **3.5d**, its word list outside the repository (as `../amiga-make.local.sh`), and review | 0.3..0.4 R12; 0.4..0.5 R7 | the word list itself is workstation-specific: it cannot be public. CLAUDE.md:202 and :204 still name a workstation tool (0.4..0.5 R7); fixed in 0.6 |
| G2 | `README.md` is for users, kept short (CLAUDE.md:3-4) | rule | nothing | review | – | CLAUDE.md:136 says the `mp3 pcm` rule is documented in the README: it is in the user guide (`user-guide.md:69`) since the README was shortened; fixed in 0.6 |
| G3 | Documentation, code comments and program messages in English (CLAUDE.md:10; CONTRIBUTING.md:13) | rule | review | review | – | – |
| G4 | Strings shown on the Amiga and files read by the Amiga: ASCII only, LF line endings (CLAUDE.md:223; CONTRIBUTING.md:13, :187) | rule | LF: `.gitattributes` for `*.sh` and `Makefile` only; ASCII: review | script **3.5d** (ASCII and LF in `src/`, and in the text files of the zip) | 0.3..0.4 §4 (a player name sent by the server printed as received) | `README.md` (2 lines) and `docs/user-guide.md` (29 lines) hold UTF-8 (`—`, `…`, `·`) and both go into the zip: are they "files read by the Amiga"? No CR in the tree today |
| G5 | MIT licence; squeezelite and LMS (GPL) are read to understand the protocol, never copied (CLAUDE.md:27-29; CONTRIBUTING.md:14-15, :181-183) | rule | review | review (no script can tell) | – | – |
| G6 | Third-party code: MIT-compatible licence (no GPL), in `third_party/`, unmodified, version, commit and licence in a README, built in its own file without our warnings (CLAUDE.md:29-30, :96-97; CONTRIBUTING.md:15-16, :61) | rule | build: `THIRD_PARTY` (Makefile:40-43, `-w`); the rest: review | script **3.5d**: each header against the SHA-256 its `README.txt` records ("unmodified") | – | "unmodified" is in CONTRIBUTING.md only; CLAUDE.md says "a README", the files are `README.txt` |
| G7 | A file that includes a third-party header and reads its structures uses the same configuration macros as the file that builds it (CLAUDE.md:97-98) | rule | review (0.4..0.5 §4: `flac.c` = `dr_flac.c`, `mp3.c` = `minimp3.c`) | build: one header of macros per library, included by both files | – | a mismatch changes structure layouts without a warning |
| G8 | Kickstart ROMs are not versioned (licensed) (CLAUDE.md:184) | rule | nothing (no `.gitignore` entry) | script **3.5d** or `.gitignore` | – | low risk |

## V. Versions and releases

| ID | Statement (source) | Kind | Enforced today | Should be | Findings | Remarks |
|---|---|---|---|---|---|---|
| V1 | 0.x until the main feature works; 1.0 = first really usable version, tested on the real machine (CLAUDE.md:12-13) | rule | review | review | – | – |
| V2 | The version changes only for a published release; never bumped without asking the user (CLAUDE.md:13-14) | rule | review | review | – | – |
| V3 | A release = a tag `amisqueeze-<version>` + a GitHub Release with the zip and short notes, made with the GitHub CLI once the user has approved the notes (CLAUDE.md:14-16) | rule | manual; the zip: `make dist` (`tools/make-dist.py` checks that each file exists) | manual, by the release rule of 3.5e | 0.4..0.5 §5 (`dist` does not depend on `test`) | the zip's list (CLAUDE.md:14-15, `make-dist.py:2-5`) omits the `.info` icons (0.4..0.5 §5); fixed in 0.6 |
| V4 | `CHANGELOG.md` (Keep a Changelog): each user-visible change under "Unreleased" as it is made; the version and date given at the release (CLAUDE.md:16-17; CONTRIBUTING.md:113-114) | rule | the release part: script (`tools/check-version.py`: `make test` refuses a CHANGELOG not starting with "Unreleased", a heading not of the form `## [x.y] - yyyy-mm-dd`, a release version that is not its newest section; `make dist` also refuses a non-empty "Unreleased"; seen failing 2026-10-20); the rest: review | review; the release part: script | 0.4..0.5 R3 | – |
| V5 | The `$VER:` date is the release date; between releases the number reads `<next>-dev` (CLAUDE.md:17-18) | rule | script (`tools/check-version.py`: `make dist` refuses `-dev`; a release version dated otherwise than its CHANGELOG section, and an `x.y-dev` not after the latest release, refused by `make test` too; seen failing 2026-10-20) | script | 0.4..0.5 R3 | – |
| V6 | The number appears in both `$VER:` strings (`amiga.c`), in `Firmware=` (`slimproto.c`) and in the user guide (CLAUDE.md:23-25, :85) | rule | script (`tools/check-version.py`, run by `make test` and, through `make-dist.py`, by `make dist`: both `$VER:` strings, `Firmware=`, the guide's state line agree; the latest release named by the guide and by `README.md` is the CHANGELOG's newest section, an extension decided with the user on 2026-10-20; seen failing 2026-10-20) | script | 0.3..0.4 §12; 0.4..0.5 R3; open point (`docs/development/README.md:140`) | first candidate named by the plan |
| V7 | The `$VER:` string is not const, so it sits in the data hunk (CLAUDE.md:210-211) | rule | script (`tools/check-version.py`: each `$VER:` string declared exactly as `static char <name>[] = "$VER: …";`, on a line of its own; seen failing 2026-10-20) | script | – | – |
| V8 | Before each release, `Version <exe> FULL` for both executables in WinUAE (CLAUDE.md:212-213) | rule | manual | manual, in the release rule of 3.5e | 0.3..0.4 §4, 0.4..0.5 §4: outside the repository | – |
| V9 | The commodity's title is the `$VER:` string without `$VER: `: it must fit `CBD_TITLELEN`, 40 bytes with the NUL (`amiga.c:475`, NDK `libraries/commodities.h`; not in `CLAUDE.md`, added with its check, decided with the user on 2026-10-20) | limit (commodities.library) | script (`tools/check-version.py`; seen failing 2026-10-20) | script | – | what commodities.library does with a longer title was not tried |

## P. Plan, documentation, test method

| ID | Statement (source) | Kind | Enforced today | Should be | Findings | Remarks |
|---|---|---|---|---|---|---|
| P1 | Work proceeds in testable steps, each validated with the user before writing code; contributors open an issue first (CLAUDE.md:55; CONTRIBUTING.md:9-10) | rule | review (review-plan-compliance) | review | 0.4..0.5 §5 (the 0.5 fixes recorded but not marked agreed; 1.10 without a criterion) | – |
| P2 | Plan statuses kept up to date (CLAUDE.md:56) | rule | review | review; script **3.5d** perhaps: the table of `docs/development/README.md` against each step's heading | 0.3..0.4 R4, R5, R9, R10, R11; 0.4..0.5 R11, §5 (stale names, the CI open point, test counts) | the most frequent class of finding |
| P3 | User-visible behaviour (options, messages) documented in `docs/user-guide.md`, updated with every change (CLAUDE.md:56-57) | rule | review (review-tests-docs) | script **3.5d** (every message the program prints has a row in the guide), and review | 0.3..0.4 R13; 0.4..0.5 R4, R5, R8, R10 | named by the plan |
| P4 | Open points listed in `docs/development/README.md`, never implemented unasked (CLAUDE.md:76-77) | rule | review | review | – | – |
| P5 | Study 2.0 before any amilms code (CLAUDE.md:50-51) | rule | nothing | review | – | – |
| P6 | Criteria measured on the server through its CLI, never on the web page (CLAUDE.md:107-108) | rule (test method) | manual | manual | – | – |
| P7 | `OUTPUT` comparisons: a single `play`, compare after the output has drained and amisqueeze has stopped; test MP3s without ID3v1 (CLAUDE.md:112-115) | rule (test method), limit (LMS strips ID3v1, then `sox` drops the last frame) | manual | manual | – | – |
| P8 | Judge Paula's sound at LMS volume 100 (CLAUDE.md:221-222) | rule (test method) | manual | manual | – | rests on F4 |
| P9 | Check memory with the `Largest` column of `Avail` (CLAUDE.md:126-127) | rule (test method) | manual | manual; automated WinUAE runs (plan step 0.7) | – | – |
| P10 | Paula is the main audio test, Toccata a 16-bit reference (CLAUDE.md:38-39) | rule (test method) | manual | manual | – | – |

## C. Code design (player)

| ID | Statement (source) | Kind | Enforced today | Should be | Findings | Remarks |
|---|---|---|---|---|---|---|
| C1 | The core is standard C (no Amiga or POSIX header); everything system-related through `platform.h`; the platform layers thin (CLAUDE.md:141-145; CONTRIBUTING.md:56-58, :129-130) | rule | review (include and call greps, 0.4..0.5 §4); the host build would fail on an Amiga header, not on a POSIX one | script **3.5d** (allowed `#include`s in the core's files) | – | – |
| C2 | The main loop (timeouts, reconnection) in the core; one task, one loop around `plat_wait()` (CLAUDE.md:145-147) | rule; limit (a `SocketBase` belongs to the task that opened it) | review | review | – | – |
| C3 | Addresses and ports in host byte order in the core; `htonl`/`ntohl` only in the platform layers (CLAUDE.md:148-149) | rule | review (no call in the core today) | script **3.5d** (grep) | – | – |
| C4 | Fields serialised byte by byte, no packed structures; 32-bit values are `uint32_t`, the MAC hash too (CLAUDE.md:150-151) | rule | review; test: the fake server checks `HELO` and `STAT` field by field (`fake_server.py:100-109`) | review | – | – |
| C5 | `HELO` advertises only the formats actually decoded (CLAUDE.md:152) | rule | test: `fake_server.py:101-103`, `scenario_flac_off` | as is | – | – |
| C6 | Framing: server → client 2-byte length + opcode; client → server opcode + 4-byte length (CLAUDE.md:153) | limit (slimproto) | test: `test_fragmented`, `test_bad_length` | as is | – | – |
| C7 | Ring positions (stream bytes) and output positions (decoded PCM) never mixed (CLAUDE.md:88-91) | rule | test: the "widening" decoder (`tests/test_core.c`) | as is | – | seen failing: not recorded (A4) |
| C8 | minimp3 is called only with a frame and the next header in its 16 KB staging buffer, or at the end of the track (CLAUDE.md:81-83) | limit (minimp3 resets itself otherwise) | test: the MP3 scenarios of `fake_server.py` | as is | – | seen failing: not recorded (A4) |
| C9 | dr_flac: a frame decoded only when the buffer holds a whole one; a track ends for the output only when the decoder says `finished` (CLAUDE.md:93-95) | limit (dr_flac reads ahead, 4 KB) | test: FLAC scenarios, sent in pieces of 3000 bytes | as is | 0.4..0.5 R1 (a decoder that cannot start) | fixed in 0.6 (`test_decoder_no_start`) |
| C10 | After a seek or a resume, LMS sends a FLAC file without its header: a STREAMINFO is made up from the first frame's header (CLAUDE.md:95-96) | limit (LMS) | test: `scenario_flac_seek` | test: unit tests of `frame_streaminfo` | 0.4..0.5 R6, §5 (`max_block` 65535 after a seek into a variable-blocksize file) | – |
| C11 | MP3 gapless from the LAME header of the first frame; after a seek LMS resends that frame first (CLAUDE.md:83-84) | limit (LMS) | test: `scenario_mp3_gapless`, `scenario_mp3_seek_lms`, `test_lame_header` | as is | 0.3..0.4 R6 | CLAUDE.md:84 names `read_lame_header`, renamed `mp3_lame_header` (0.4..0.5 §5); fixed in 0.6 |
| C12 | Output through `msg()` (printf format) and `msg_raw()`; never `printf`/`Printf`/`PutStr` directly, except in the platform layers' own `msg()`, `msg_raw()` and `LOG` code; 32-bit values cast to the specifier's type, `%c` for chars (CLAUDE.md:154-159) | rule | build: `format(printf)` and `-Werror=format` (Makefile:31, :51) catch format and type mismatches, for each build's own types; direct calls: review | script **3.5d** (no direct call outside the allowed functions) | 0.4..0.5 R9 (accepted, the exception written into the rule), §5 (`PrintFault`, a raw `Write`) | does the exception cover `PrintFault` and the raw `Write`? |
| C13 | Big buffers static or allocated, never on the stack (CLAUDE.md:197-198) | rule | review | build **3.5d**: `-Wframe-larger-than=<n>` on our files (third-party code apart, see S3) | 0.3..0.4 R1 | – |
| C14 | A `CON:` window needs `SMART`, or it loses its text when uncovered (CLAUDE.md:216) | limit (console handler) | review; the default at `amiga.c:112` | one string for the default and the icon's example (`tools/make-icon.py`) | 0.4..0.5 R8 | fixed in 0.6 (`make-icon.py`) |
| C15 | With `QUIET` there is no output: `msg()` checks `Output()`, except during start-up (since 0.6) (CLAUDE.md:226-229) | rule | review | review | 0.4..0.5 R4 (start-up errors lost with `QUIET` and `LOG`); 0.6 R1 (the same without `LOG`, for the errors found after the log is opened) | fixed in 0.6: a message during start-up (up to `bsdsocket.library`) opens the window (WinUAE) |
| C16 | libnix opens the console named by `__stdiowin` before `main()` from the Workbench: set to NULL, own console opened after reading the icon (CLAUDE.md:214-216) | limit (libnix) | manual (WinUAE) | manual | – | – |
| C17 | One amisqueeze at a time (`CBERR_DUP`); a second start does nothing to the running one (CLAUDE.md:217-218) | rule (decided with the user) | manual | manual | 0.4..0.5 §5 (exceptions: `BENCH`, no `commodities.library`) | – |
| C18 | Wait 5 s before reconnecting after a session shorter than 10 s (CLAUDE.md:63) | rule (design) | test: `fake_server.py:126`, `:136` | as is | – | stated only inside a status sentence |
| C19 | `STMt` (one a second) is not printed with `VERBOSE` (CLAUDE.md:106-107) | rule | review | review | – | – |
| C20 | One place only where data leaves the ring buffer, the one `OUTPUT` writes from (CLAUDE.md:110-111) | rule | review | review | – | names `consume()` in `stream.c`, which does not exist (0.4..0.5 §5); fixed in 0.6 (`hand_over()`) |
| C21 | On POSIX, SIGINT and SIGTERM without `SA_RESTART` (Ctrl-C stops the wait) (CLAUDE.md:147) | rule | test: the Ctrl-C scenario (`fake_server.py:81`, `:131`) | as is | – | – |

## H. AHI

| ID | Statement (source) | Kind | Enforced today | Should be | Findings | Remarks |
|---|---|---|---|---|---|---|
| H1 | Code only `ahi.device` features of AHI 4 (CLAUDE.md:37) | rule | review | review; a run on AHI 4.x | – | checked with AHI 6.6 only (H8) |
| H2 | Chain a request (`ahir_Link`) only to one already playing: at most two requests with the device (CLAUDE.md:119-121) | limit (AHI 6.0 source) | test: `stub_audio_bad`, checked 17 times in `tests/test_core.c` | as is | – | seen failing: not recorded (A4) |
| H3 | Abort the playing request first, it takes the linked one along (CLAUDE.md:120-122) | limit (AHI 6.0 source) | test: `stub_audio_bad`; seen failing (0.3..0.4 §4, planted) | as is | – | – |
| H4 | `CMD_STOP` does not pause what is playing: pause = abort, then send the rest again (CLAUDE.md:122) | limit (AHI) | review (`amiga.c` is not in the host tests) | review | – | – |
| H5 | A non-real-time mode ("Filesave") on a device unit writes only silence (CLAUDE.md:122-123) | limit (AHI) | test: `test_ahi_mode`, `test_start_mode` (the warning) | as is | 0.3..0.4 R3 | – |
| H6 | Never send `ahir_Volume` below full: the core scales the samples (CLAUDE.md:124-126) | rule | review (`amiga.c:434`, a constant) | script **3.5d** (the only assignment of `ahir_Volume` is `0x10000`) | – | – |
| H7 | Paula needs `SWAP` in WinUAE (CLAUDE.md:60) | limit (WinUAE) | manual | manual | – | stated only inside a status sentence |

| ID | Measurement | Date | Commit | How measured | Depends on | Remarks |
|---|---|---|---|---|---|---|
| H8 | tested with AHI 6.6 (CLAUDE.md:37) | 2026-10-18 | `e67cf69` | WinUAE, `paula.audio` and Toccata | `amiga.c` AHI calls, `audio.c` | – |

## S. Stack and memory

| ID | Statement (source) | Kind | Enforced today | Should be | Findings | Remarks |
|---|---|---|---|---|---|---|
| S1 | The default Shell stack is 4 KB and AmigaOS does not detect overflows (CLAUDE.md:197) | limit (AmigaOS) | – | – | 0.3..0.4 R1 | – |
| S2 | Both executables set `__stack` (16 KB `amisqueeze`, 32 KB `amisqueeze.fpu`) and link libnix's `__stkinit` (CLAUDE.md:198-199) | rule | build: `-Wl,-u,___stkinit` (Makefile:32, :37); the values at `amiga.c:64-66` | as is; the link map checked **3.5d** | 0.3..0.4 R1, R8 | Makefile:29 still says "dr_flac about 5 KB, minimp3 about 17 KB" |
| S3 | Redo the stack measurement whenever call depth or local variables grow; measure frames with `-fstack-usage`; the probe runs from a script with `Stack 4096`, never from a Shell with a large stack (CLAUDE.md:202-206) | rule | nothing (a temporary probe, removed afterwards) | script **3.5d**: `-fstack-usage` along the main call paths against a recorded budget; the probe kept behind a build option | 0.3..0.4 R8; 0.4..0.5 §4 (a Workbench start never measured) | CLAUDE.md:202 and :204 name a workstation tool (G1) |
| S4 | libnix's `__fpucheck` is replaced by an empty one, `amiga.c` checks `AttnFlags` itself (CLAUDE.md:207-208) | limit (libnix), rule | review (0.3..0.4 §4: link map) | script **3.5d** (link map) | – | – |

| ID | Measurement | Date | Commit | How measured | Depends on | Remarks |
|---|---|---|---|---|---|---|
| S5 | "2 KB stack OK" (CLAUDE.md:60) | 2026-10-18/19 | `3ebcca3` (step 1.2), `8ecb2cf` (1.4c) | WinUAE, a 2 KB Shell stack, play and stop | `src/amisqueeze/*` before the decoders | superseded since dr_flac (0.2) and the 16 KB stack (0.5); still stated (0.3..0.4 R8, 0.4..0.5 §5); fixed in 0.6 (marked as superseded) |
| S6 | dr_flac about 5.5 KB, 4.6 KB of it in `drflac_open` (CLAUDE.md:199-200) | 2026-10-20 | `a2e132a` | `-fstack-usage` frames | `third_party/dr_flac/`, `flac.c`, `CFLAGS`, gcc version | – |
| S7 | minimp3 about 17.5 KB (CLAUDE.md:200); "about 17 KB" in `mp3dec_decode_frame` (CLAUDE.md:209) | 2026-10-20; 2026-10-19 | `a2e132a`; `072d62f` | `-fstack-usage` frames | `third_party/minimp3/`, `mp3.c`, `CFLAGS`, gcc version | two figures for the same thing, the older at :209 |
| S8 | 5.6 KB of 16 KB used (`amisqueeze`, FLAC with a seek); 17.8 KB of 32 KB (`amisqueeze.fpu`, FLAC and MP3) (CLAUDE.md:204-205) | 2026-10-20 | `a2e132a` | a probe in `main()`, from a script with `Stack 4096`, in WinUAE | call depth and locals of `src/amisqueeze/*`, `third_party/*`, `CFLAGS`, gcc version, `__stack` | not measured from the Workbench (0.4..0.5 §4) |
| S9 | no memory leak: same free memory after three play/stop cycles (CLAUDE.md:60) | 2026-10-19 | `8ecb2cf` | WinUAE, `Avail` | allocations in `amiga.c`, `audio.c`, `stream.c`, the decoders | predates the decoders, `LOG` and the commodity |

## B. Build and safety nets

| ID | Statement (source) | Kind | Enforced today | Should be | Findings | Remarks |
|---|---|---|---|---|---|---|
| B1 | The build fails if a function modifies d2-d7 or a2-a6 without saving it (CLAUDE.md:189-192; CONTRIBUTING.md:121-125) | rule | script: `tools/check-saves.py` on both variants (Makefile:85, :116); seen failing on a planted `d2` | script, with a self-test `.s` file run by `make test` | 0.3..0.4 §4, §5; 0.4..0.5 R2, §5 (fp2-fp7) | a script is not a sound check: `(an)+`, `-(an)`, `dbra` and fp2-fp7 slipped through; fixed in 0.6 but for fp2-fp7, with a self-test (`tests/check-saves.s`, `make test`) |
| B2 | Toolchain: `m68k-amigaos-gcc` 16.2 or newer (AmigaPorts), NDK 3.2, libnix; the GCC 16 port is at RC stage, check the assembly (CLAUDE.md:166-168, :192; CONTRIBUTING.md:22-24) | limit | nothing | build **3.5d**: refuse an older compiler | – | "check the assembly" has no check beyond B1 |
| B3 | Network programs target the 68020 (CONTRIBUTING.md:116) | rule | build: `-m68020` (Makefile:31) | as is | – | – |
| B4 | The host build runs with ASan and UBSan, and they stop the tests (CLAUDE.md:174; CONTRIBUTING.md:35-36, :134) | rule | build: `-fno-sanitize-recover=all` (Makefile:50); seen failing (0.4..0.5 §4, planted) | as is | 0.3..0.4 R2 | – |
| B5 | In the `amigadev/crosstools` image, `gcc` is the Amiga cross-compiler: set `HOSTCC` to a native one (CONTRIBUTING.md:41-42) | limit (the image) | – | – | – | – |
| B6 | Run `make sdk` after installing or updating the toolchain (CONTRIBUTING.md:109) | rule | manual | manual | – | IntelliSense only: no effect on the build |

| ID | Measurement | Date | Commit | How measured | Depends on | Remarks |
|---|---|---|---|---|---|---|
| B7 | the wrong-code bug of gcc 16.1.1b fixed in 16.2-rc16 (CLAUDE.md:189-191) | 2026-10-18 | `fe65f56` | not recorded | gcc version | – |

## T. Tests and CI

| ID | Statement (source) | Kind | Enforced today | Should be | Findings | Remarks |
|---|---|---|---|---|---|---|
| T1 | `make test` must pass; run it after every change to the core (CLAUDE.md:176-177; CONTRIBUTING.md:11) | rule | CI on each push and pull request (`.github/workflows/tests.yml`); locally: nothing | as is | 0.3..0.4 §12; 0.4..0.5 §5 (no `permissions:`, `actions/checkout@v5` pinned by tag) | the CI runs after the push: a red run does not undo it |
| T2 | Extend the tests with each step; add tests for what changes in the core (CLAUDE.md:177; CONTRIBUTING.md:11) | rule | review (review-tests-docs) | review; a coverage report (gcov) from `make test` | 0.3..0.4 R3, R6, R7; 0.4..0.5 R1, R6, §5 (untested paths, malformed server input) | – |
| T3 | The Amiga build is still tried in WinUAE: the tests run the core on the build machine only (CLAUDE.md:177-178; CONTRIBUTING.md:12, :145, :148) | rule | manual | manual; the Amiga build in CI (planned before 1.0); automated WinUAE runs (step 0.7) | 0.4..0.5 §5 (the no-MP3 and Amiga builds not compiled in CI) | – |
| T4 | After a push, check the CI run; the tests use real time, so a slow runner may fail one: rerun before suspecting the code, and say so (CLAUDE.md:179-181) | rule | manual | manual | – | – |

| ID | Measurement | Date | Commit | How measured | Depends on | Remarks |
|---|---|---|---|---|---|---|
| T5 | host tests "tested with gcc 14 on Linux" (CONTRIBUTING.md:39) | 2026-10-19 | `e7cad38` | `make test` | `HOSTCC` version | the CI runs Ubuntu 24.04, whose default gcc is 13 (0.4..0.5 §5) |

## E. Tools and servers

| ID | Statement (source) | Kind | Enforced today | Should be | Findings | Remarks |
|---|---|---|---|---|---|---|
| E1 | vamos runs only pure dos.library tools: crashes on `printf`, no windows, no real bsdsocket; `-C 68020` for 68020 builds; its `GetVar` ignores `ENV:` (CLAUDE.md:193-196; CONTRIBUTING.md:184-186) | limit (vamos) | – | – | – | – |
| E2 | WinUAE: without JIT even "max" speed is a 68020 at about 45 MHz; a PiStorm-like test needs the JIT (68020, 32-bit addressing, no cycle-exact), and `compfpu=true` for FPU code (CLAUDE.md:98-100, :219-220) | limit (WinUAE 6.0) | – | – | – | – |
| E3 | A 68040 needs its CPU library (missing from the test HDF); the `-m68881` code's `fintrz` and `fmovecr` are emulated slowly on a 68040/68060 (CLAUDE.md:100, :209) | limit (68040/68060) | – | – | – | – |
| E4 | WinUAE configuration syntax: `filesystem2=…`, `chipmem_size` in 512 KB units, `fastmem_size` in MB, `bsdsocket_emu=true` (CLAUDE.md:224-225; CONTRIBUTING.md:152-155) | limit (WinUAE 6.0, `cfgfile.cpp`) | – | – | – | – |
| E5 | The Docker way of building is untested (CLAUDE.md:171; CONTRIBUTING.md:96) | limit | – | a CI job using the image | – | – |
| E6 | LMS 9.1 has no default `mp3 pcm` rule: hence local MP3 decoding; the rule documented for the builds that leave MP3 to the server (CLAUDE.md:134-136) | limit (LMS 9.1.1) | – | – | – | see G2 (the README) |
| E7 | LMS's `strm s`: PCM little-endian (swapped for AHI), WAV/AIFF headers stripped, FLAC and MP3 sent as they are when `flc` and `mp3` are advertised; files above 48 kHz playable only once decoded locally (CLAUDE.md:65-66, :131-137) | limit (LMS 9.1.1, observed) | test: little-endian PCM, FLAC and MP3 files (`fake_server.py`); WAV headers and rates above 48 kHz untested | – | – | – |
| E8 | Music Assistant: per-player output codecs `flac`, `mp3`, `aac`, `wav`; the solo playback path and the `strm` format for `wav` not verified (CLAUDE.md:160-162) | limit (not verified) | nothing | verify at step 1.7b | – | – |

## F. Speed and sound (measurements)

| ID | Measurement | Date | Commit | How measured | Depends on | Remarks |
|---|---|---|---|---|---|---|
| F1 | CPU load on the slow 68020 profile: 61-68 % with Paula, almost all of it AHI; 2-5 % with `NOAUDIO` (CLAUDE.md:60-61) | 2026-10-19 | `8ecb2cf` | WinUAE slow profile (68020, 28.4 MHz), free CPU counted by a priority -128 loop, 10 s windows (`phase-1-amisqueeze.md:176-180`) | `audio.c` (conversion, volume tables), `amiga.c` AHI calls, AHI version and mode | predates the volume tables and the sample conversions (0.4..0.5 §5) |
| F2 | FLAC level 8 at 0.2x real time on a 68020 without JIT, about 45 MHz (CLAUDE.md:98-99) | 2026-10-19 | `ad74a1b` | `BENCH` in WinUAE | `third_party/dr_flac/`, `flac.c`, `bench.c`, `CFLAGS` | also in `user-guide.md:243` |
| F3 | MP3 at 74x real time with the JIT and `compfpu=true`, 1.8x without (CLAUDE.md:219-220) | 2026-10-19 | `072d62f` | `BENCH`, WinUAE 68020 + 68882 with the JIT | `third_party/minimp3/`, `mp3.c`, `-m68881`, WinUAE settings | – |
| F4 | Paula through AHI (14 bits): a crackle at LMS volume 50, gone at 100 (CLAUDE.md:221-222) | 2026-10-19 | `072d62f` | listening in WinUAE | `audio.c` volume tables, AHI mode | left to the real machine (step 1.9) |

## Findings with no rule

Findings that touch only the method's principles, no rule of `CLAUDE.md` or `CONTRIBUTING.md`:

- A6 (everything the server sends is untrusted): 0.3..0.4 R14 (a FLAC STREAMINFO rate of 0 stalls the track,
  seen again in 0.4..0.5 §5; fixed in 0.6, `scenario_flac_rate_0`); 0.4..0.5 §5: the HTTP status ignored (a 404 page played as PCM), the track queue
  overflow, a stale socket readiness after a `strm s`, a zero-byte queue entry, and the plan's open points met
  again (`gethostbyname()` unchecked, `PORT` and `UNIT` not range-checked, a request over 1 KB refused without
  `STMn`).
- A7 ("not supported yet" has consequences): 0.3..0.4 R15 (ICY metadata on a direct stream goes to the decoder;
  its twin 0.4..0.5 R10 is linked to P3 for the user guide); 0.4..0.5 §5: "stream format p not supported" for
  rates above 48 kHz, ReplayGain, crossfade, `serv` and sync ignored without a word in the guide.
- Platform robustness, no principle in particular: 0.4..0.5 §5 (Exchange "Remove" waits for a blocking call,
  `AHIDB_Name` not forcibly terminated, `IoctlSocket(FIONBIO)` unchecked).

## Candidates for 3.5d

In the plan's order first, then the others. Step 3.5d stopped after the first one (decided with the user on
2026-10-20, to avoid over-quality): the others are ideas, not planned; one is taken up only when a review finds the
defect it would have caught again.

1. Version strings: both `$VER:`, `Firmware=`, the user guide agree; `make dist` refuses `-dev`, a missing
   CHANGELOG section, a date other than the CHANGELOG's; no `const` on `$VER:` (V4 to V7). Done on 2026-10-20:
   `tools/check-version.py`, with the latest release named by the guide and the README and the commodity's title
   length (V9) added.
2. Every message the program prints has a row in the user guide (P3).
3. ASCII only and LF line endings (G4, once its scope is decided).
4. Nothing specific to the developer's workstation, the word list kept outside the repository (G1).
5. No direct `printf`/`Printf`/`PutStr` outside the allowed functions (C12).
6. The core's `#include`s and no `htonl`/`ntohl` in the core (C1, C3).
7. `ahir_Volume` set only to full (H6).
8. A self-test of `check-saves.py` on planted cases (B1).
9. Stack: `-Wframe-larger-than` on our files, and `-fstack-usage` totals against a recorded budget (C13, S3); the
   link map for `__stkinit` and `__fpucheck` (S2, S4).
10. Third-party headers against their recorded SHA-256 (G6).
11. A measurement whose "depends on" paths changed after its commit is reported as stale (all measurements, A3).
12. Lower value: the plan's status table against its step headings (P2), a minimum compiler version (B2), no ROM
    files (G8).

## Remarks to decide

**Closed on 2026-10-21** (decided with the user, against over-quality): not to be processed, unless a review finds
the defect again; G4 was decided the same day (the README and the user guide are files read by the Amiga, made
ASCII in 0.6).

What looks wrong or stale, left as it is (fixed in 0.6, the release of the review fixes: the stack figure, the
stale names, the README for the `mp3 pcm` rule, the workstation tool, the zip's list; the line numbers are
still those of `2802634`):

- `CLAUDE.md:60` "2 KB stack OK" superseded (S5); `:61` the CPU load predates later changes (F1).
- `CLAUDE.md:84` `read_lame_header` is now `mp3_lame_header` (C11); `:110` names `consume()` in `stream.c`, which
  does not exist (C20, 0.4..0.5 §5).
- `CLAUDE.md:136` says the `mp3 pcm` rule is in the README; it is in the user guide (G2).
- `CLAUDE.md:200` and `:209` give 17.5 and 17 KB for minimp3; `Makefile:29` says 5 and 17 KB (S2, S7).
- `CLAUDE.md:202`, `:204` name a workstation tool (G1, 0.4..0.5 R7).
- `CLAUDE.md:14-15` and `make-dist.py:2-5`: the zip's list omits the `.info` icons (V3).
- `README.md` and `docs/user-guide.md` are UTF-8 and ship in the zip: in or out of the ASCII rule (G4)?
- `CONTRIBUTING.md:39` "tested with gcc 14" while the CI uses gcc 13 (T5).
- The `msg()` exception: does it cover `PrintFault` and a raw `Write` (C12)?
- Two rules live only inside status sentences of `CLAUDE.md` (C18 the reconnection delay, H7 `SWAP`): move them
  next to the other rules?
- "Unmodified" third-party code is stated only in `CONTRIBUTING.md`; `CLAUDE.md` says "a README" for `README.txt`
  (G6).
- No written rule for what the server sends (A6) nor for "not supported yet" (A7), though most findings without a
  rule come from there (see [Findings with no rule](#findings-with-no-rule)): should `CLAUDE.md` have one?
