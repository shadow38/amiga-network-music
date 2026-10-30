# Phase 0 — Tooling and test environment

Back to the [plan](README.md).

## tools-1 Toolchain ✅ (2026-10-18)

- **Goal**: build several programs from one repository, whatever the developer's machine.
- **Content**: multi-program `Makefile` (`src/<prog>/` → `out/<prog>`), header dependencies, the
  `tools/check-saves.py` safeguard (register-saving bug in gcc 16.1), `tools/amiga-make.sh` (`$AMIGA_MAKE`,
  toolchain in the `PATH`, local script, Docker), VS Code tasks.
- **Criterion**: `make clean all` produces `out/amisqueeze`; touching a `.h` rebuilds the `.c` files that include
  it.
- **Docker mode**: tested since 2026-10-23, the CI's Amiga build takes it (publish-3).

## tools-2 Test LMS server ✅ (2026-10-18)

- **Goal**: a Lyrion Music Server reachable from the emulated Amiga, with a small library.
- **Content**: any LMS will do (Docker image `lmscommunity/lyrionmusicserver`, distribution package, NAS…).
  Ports: 3483/tcp (players), 9000/tcp (web UI + audio streams; set `HTTP_PORT` if the port is taken), 9090/tcp
  (CLI). With `bsdsocket_emu`, the emulated Amiga uses the host's network stack: a server reachable from the host
  is reachable from the Amiga.
- **MP3 to PCM conversion**: LMS 9.1 has no `mp3 pcm` rule (regular players decode MP3 themselves). The test
  server needs one for every build that does not decode MP3 itself (`amisqueeze`, or `amisqueeze.fpu` on a CPU too
  slow for it, player-8c), in a `custom-convert.conf` (LMS root directory, or the root of a plugins directory),
  modelled on the `ogg pcm` rule and using the `sox` shipped with LMS:

  ```
  mp3 pcm * *
  	# FT:{START=trim %s}
  	[sox] -q -t mp3 $FILE$ -t raw -c 2 -2 -s - $START$
  ```

  Without it, `server.log` says "Couldn't create command line for mp3 playback" and the player receives nothing.
  No `-r 44100` (unlike `ogg pcm`): LMS announces the source file's rate in `strm`, so a forced rate played 48 kHz
  files 9 % too fast (found in player-4c); `FT:{START=trim %s}` lets LMS start within a track (seek).
- **Criterion**: web UI reachable, CLI answering `version ?`, library scanned.
- **Validated with**: LMS 9.1.1.

## tools-3 Reference player ✅ (2026-10-18)

- **Goal**: a known-good player to validate the server and compare amisqueeze's behaviour.
- **Content**: squeezelite (Windows builds on SourceForge, `lmsclients` project):
  `squeezelite -s <server>:3483 -n PC-test`.
- **Criterion**: the player shows up in LMS and plays an MP3 from the library.

## tools-4 WinUAE test configuration 🔶

- **Goal**: emulated machines that reproduce the targets, with emulated networking.
- **Done (provisional)**: a local configuration, 68020 running AmigaOS 3.2, CPU at maximum speed,
  `bsdsocket_emu=true`, build output mounted as an Amiga volume. Enough for networking (step player-1).
- **To do**:
  - two profiles (decided 2026-10-18): **fast**, close to a PiStorm with Emu68 (68040 with FPU, 32-bit
    addressing, Zorro III memory, tens of MB); **slow**, a 68020 accelerator without FPU at its real speed (not
    "as fast as possible", so that CPU load can be measured: Paula mixing, later the decoders);
  - a versioned configuration template in `winuae/`, with machine-specific paths kept in a local, unversioned file
    (done on 2026-10-22, see "Template, versioned" below).
- **Slow profile, local** (2026-10-19, for step player-4c): the provisional configuration started with the CPU
  settings overridden (`-s cpu_compatible=true -s cpu_speed=real -s cpu_multiplier=4`, a 68020 at about
  28 MHz, as a typical A1200 accelerator); an override of the versioned template since 2026-10-22.
- **Fast profile, local** (2026-10-19, for steps player-8b and player-8c): not a 68040 (its CPU library is missing from the
  test installation) but a 68020 with WinUAE's JIT compiler, 32-bit addressing
  and no cycle-exact emulation (`cpu_24bit_addressing=false`, `cachesize=16384`, `cycle_exact=false`,
  `cpu_compatible=false`, `cpu_speed=max`): about 230 times the slow profile, the order of a PiStorm. For
  `amisqueeze.fpu`, a 68882 (`fpu_model=68882`) and the JIT's FPU compiler (`compfpu=true`; without it FPU
  instructions are interpreted: MP3 at 1.8x real time instead of 74x). Speeds measured there are not those of a
  real machine (player-9). No versioned template yet.
- **Template, versioned** (2026-10-22, decided with the user): `winuae/amisqueeze-test.uae`, with placeholders
  for the machine-specific paths, and the profiles as command-line overrides, described in
  [testing.md](testing.md) with the rest of the test setup. Still open: a 68040 profile (its CPU library in the
  test system).
- **Criterion**: the configuration boots, `bsdsocket.library` is available, amisqueeze connects to the server.

## tools-5 AHI in the emulated Amiga ✅ (2026-10-18)

- **Goal**: be able to test audio output (step player-4) in WinUAE.
- **Content**: the AHI package (Aminet) installed in the emulated Amiga, AHI mode set in the preferences; notes for
  the real outputs. Two drivers, both kept for the tests (decided 2026-10-18), Paula first:
  - **Paula** (`paula.audio`, Amiga chipset): what an Amiga without a sound card plays; 8 bits per channel
    (14 bits with AHI's combined modes), about 28 kHz at most with a 15 kHz screen mode (AHI resamples 44.1 kHz
    down), mixing done by the CPU;
  - **Toccata** (Zorro II card emulated by WinUAE, `toccata.audio` from the AHI package): a real 16-bit sound card,
    up to 48 kHz stereo; a 16-bit reference, to tell a fault of the player from Paula's limits (a Zorro II card,
    not available on every Amiga: not a target as such).
  - The PiStorm's own AHI outputs (I2S DAC of the PiStorm32-lite FPGA firmware, HDMI with `emu68-hdmi-audio` on a
    Pi 4) cannot be emulated: tested on the real machine (player-9).
- **Criterion**: an AHI test program (or `Play16` on a WAV file) plays a correct sound, with each of the two
  drivers.
- **Done**: AHI 6.0 from Aminet (`m68k-amigaos-ahiusr` and `m68k-amigaos-ahidrv`: `ahi.device` 6.6,
  `paula.audio` 4.30, `toccata.audio` 4.7), copied by hand as its readme describes (AHI 6 has no installer);
  `toccata.audio` needs `toccata.library` (v12, MacroSystem, Aminet `util/libs/toclib12`); the AHI 6 preferences
  need MUI (`muimaster.library` v19, MUI 3.8 from Aminet); the `emu10kx` driver (PCI card) is left out, otherwise
  the preferences complain about `openpci.library`. Toccata emulated by WinUAE (`toccata=true`), Paula stereo
  separation set to 100 % (`sound_stereo_separation=10`; the default mixes the channels). Test sound: 16-bit
  big-endian PCM sent to `AUDIO:` (AHI's handler, `Mount SYS:Storage/DOSDrivers/AUDIO`): 440 Hz on both sides,
  then 1 kHz on the left only, then on the right only.
- **Validated**, by ear, against a left/right reference played by the host:
  - Toccata 16 bit stereo++ at 44.1 kHz: correct;
  - Paula through AHI (14 bit and 8 bit stereo++ at 27,429 Hz, the maximum with a 15 kHz screen mode): correct
    sound, **left and right swapped**; Paula played directly through `audio.device` (channels 0 and 3 left, 1 and
    2 right) is correct, so the swap comes from `paula.audio`, not from the emulation: seen on a real Amiga too
    (player-9, 2026-10-24);
  - "stereo" modes without "++" put each sound entirely on one side: a stereo stream sent at the centre plays one
    channel only, on one side. amisqueeze needs a **stereo++** mode (player-4).
  - The `filesave.audio` driver ("Filesave" modes) writes what AHI plays to a file (WAV, AIFF…), but only for
    programs using AHI's library interface: its modes are not real-time, the preferences offer them only for
    the "Music" unit, and an `ahi.device` unit set to one by hand writes silence (tried in player-4b).
- **Prerequisite of**: player-4.

## tools-6 Host-side tests ✅ (2026-10-18)

Since then, the core grew (stream, ring buffer, audio output, decoders) and the tests with it, at each step of
phase 1; the figures below are those of 0.6.

- **Goal**: test without an emulator whatever can be tested that way.
- **Content**:
  - amisqueeze split into a **portable core** (`player.c`, `slimproto.c`, `player.h`: standard C, no Amiga or
    POSIX header) and a **platform layer** (`platform.h`: clock, output, resolution, sockets, wait):
    `amiga.c` (AmigaOS) and `posix.c` (Linux/POSIX). Messages use the standard printf format on both builds
    (libnix `vsnprintf` on the Amiga, about 8 KB larger);
  - `make host`: the same core on the build machine (`$(HOSTCC)`), with AddressSanitizer and UBSan;
  - `make test`: unit tests of the core on a fake platform (`tests/test_core.c`, `tests/stub_platform.c`: packets
    checked byte by byte, fragmented reads, invalid lengths, MAC derivation against values seen in WinUAE), then a
    fake slimproto server in Python driving the host build (`tests/fake_server.py`: HELO, `strm t`, renaming,
    disconnection and reconnection, Ctrl-C, argument errors). No LMS needed.
- **Criterion**: `make test` passes; a known regression makes it fail. Validated: 51 unit checks and 17 scenario
  checks pass; a `STAT` length of 52 instead of 53, and swapped bytes in 16-bit fields, both make it fail. The
  Amiga build replayed the player-1/player-1b cases in WinUAE with the same messages as before the split, and the host
  build connects to a real LMS 9.1.1 with the same MAC as the Amiga build.

## tools-7 Logging and automated runs in WinUAE ✅ (2026-10-22)

- **Goal**: run a test in WinUAE and read its result from the host, without manual steps.
- **Done (2026-10-18, local setup)**: a **serial shell**. WinUAE connects the Amiga's serial port to a local TCP
  socket (`win32.serial_port=TCP://127.0.0.1:<port>`; the `win32.` prefix is required, otherwise WinUAE logs
  "unknown config entry"); `S:User-Startup` runs `Mount AUX:` and `Run >NIL: NewShell AUX:`; a host script sends a
  command line and reads the output up to the next prompt. Validated: commands, starting amisqueeze in the
  background (`Run NM:amisqueeze >T:log …`), `Status`, `Break <n>`, reading the log. Pitfalls: send one character at
  a time (no flow control, characters get lost otherwise); the console wraps the prompt in control sequences
  (`CSI 20h`, `SI`) to filter out.
- **Done since, local setup only** (2026-10-19): a script that restarts WinUAE in a given profile and waits for the
  shell; scripted scenarios for the steps (commands through LMS's CLI, robustness through a TCP relay, `OUTPUT`
  comparisons, listening runs), all outside the repository.
- **Documented** (2026-10-22, [testing.md](testing.md)): the serial shell and the rules its client must follow,
  amiagent (an optional server in the Amiga, used through its MCP server since 2026-10-19), the measures on the
  server and the CPU load; the test sounds made by `tools/make-test-audio.sh`.
- **Done** (2026-10-22, designed with the user): `tools/winuae/`, Python 3 with the standard library only, described
  in [winuae-scenarios.md](winuae-scenarios.md): a serial shell client following the rules of
  [testing.md](testing.md), an LMS CLI client, and one scenario, `play-tone.py` (amisqueeze on an AHI unit plays
  `tone-440.flac`; checks the output used, LMS's play mode, the elapsed time as an interval, the track's length by
  the host's clock, the end of playback and a clean stop; prints OK or the reason, with an exit code). Decided: it
  starts WinUAE only when its serial shell does not answer and WinUAE is not running, and never stops it; player
  `Amiga` by default; no CHANGELOG entry (a developer tool); no check of these scripts in `make test`. Validated in
  WinUAE with LMS 9.1.1: a run that starts WinUAE itself, a run on the running one, and the failures (no WinUAE:
  exit code 2; a track not in the library: 2; the virtual output instead of AHI: 1, amisqueeze stopped all the
  same). The restart in a profile stays local. Other scenarios later, on the same model, if they are needed.
- **Criterion**: one command on the host starts WinUAE, waits, runs a scenario and prints its result.

## tools-8 Source-level debugging ⬜ (optional)

- **Goal**: breakpoints and stepping in C.
- **Approach**: `bgdbserver` (Aminet `dev/debug/bgdbserver`) in the emulated Amiga, WinUAE's emulated networking,
  `m68k-amigaos-gdb` (`target remote …:2345`) driven by VS Code (cpptools); build with `-g`.
- **Criterion**: stops on a breakpoint in a function of amisqueeze (e.g. in `player.c`), variables readable.

## tools-9 Test Music Assistant server 🔶 (players side done 2026-10-21; API token not yet)

- **Goal**: a Music Assistant server reachable from the emulated Amiga, for the Music Assistant back end of the
  remote (remote-8 to remote-11) and for amisqueeze as a Music Assistant player (player-7b).
- **Content**: any Music Assistant server will do (Docker image, Home Assistant add-on…), with a small local
  library and an access token for the API. To check: the server's ports (web UI and API, its slimproto port for
  players) and whether they collide with a test LMS on the same machine (both servers use 3483 for players: two
  addresses, or one server at a time).
- **Criterion**: web UI reachable, API answering with the token, squeezelite (tools-3) playing a track from it.
- **Prerequisite of**: player-7b, remote-8.
- **Done** (2026-10-21): Music Assistant 2.10.7 (official image `ghcr.io/music-assistant/server`) with the LMS
  test library. Its slimproto port is moved on the host side, so both servers run at once (amisqueeze `PORT=`).
  By default the stream server publishes its own address in `strm`, which the emulated Amiga cannot reach: set
  the published IP (Settings > System > Streams, advanced; `publish_ip`) to the address the Amiga reaches. Players
  need the Squeezelite provider, added by hand; it also serves an LMS-like CLI (9090, no token), too limited to
  start a library track: playback was started from the web UI. amisqueeze was tried instead of squeezelite (player-7b).
  Not done: an API token (needed by remote-8).

## tools-10 Regression scenarios in WinUAE 🔶 (LMS part done 2026-10-23; Music Assistant before 1.0)

- **Goal**: before a release, replay in WinUAE what `make test` cannot check: the Amiga platform layer (AHI,
  bsdsocket, `Break`), the real servers (LMS, Music Assistant) and the 68k build, so that a release is validated
  on a solid base (decided with the user on 2026-10-22, before the release of 0.7).
- **Content**: a few scenarios in `tools/winuae/` on the model of `play-tone.py` (tools-7,
  [winuae-scenarios.md](winuae-scenarios.md)), to design with the user before writing them. Proposed: LMS's
  commands through AHI (pause, resume, seek, stop, two tracks in a row, the `Largest` column of `Avail` before and
  after); a byte-by-byte `OUTPUT` comparison of the Amiga build (FLAC and MP3, `tools/pcm-compare.py`); these two
  before the release of 0.7. Music Assistant before 1.0: its LMS-like CLI cannot start a library track (see tools-9),
  so the scenario goes through its API, which needs a token and a small WebSocket client (reused by amilms, remote-8).
  Not in it: the measures (CPU load, long runs, the slow relay) and the old ROMs, which stay manual.
- **Criterion**: one command runs the scenarios, each in less than 2 minutes, and prints OK or the reason of each
  failure.
- **Design** (agreed with the user on 2026-10-22): one profile for all, the JIT and a 68882 with `compfpu`
  (decoding on the Amiga at least twice real time, `amisqueeze.fpu`), checked by the runner with `BENCH`, which
  then warms AHI up once; what the scenarios share in `scenario.py` (`play-tone.py` moved onto it); every scenario
  checks that the stream came in the format it tests (`stream: format` in the log) and that the fast memory and
  its largest block are given back (`Avail FLUSH` before and after); LMS's commands run twice, FLAC decoded by the
  server (raw PCM, the A1200's path) and on the Amiga (where a seek comes without the FLAC header), the pause and
  resume at volume 50 (meant to catch the AHI defect of step player-6, set off by LMS's fade before a pause, in the
  memory check: see Done, it does not); the counter pattern made again
  at each run, no reference file to keep. **MP3 decoded on the Amiga is not compared byte by byte**, contrary to
  the content proposed above (decided with the user): its floating point gives a few samples off by one from any
  other decoder, so it is checked against the tones it holds (exact length, the sine fitted on each side, at least
  60 dB of signal-to-noise ratio).
- **Done** (2026-10-23): `tools/winuae/run-scenarios.py` and six scenarios, described in
  [winuae-scenarios.md](winuae-scenarios.md): `play-tone`, `commands-pcm`, `commands-flac`, `output-pcm`,
  `output-flac`, `output-mp3`; all OK in about 4 minutes from a fresh boot with LMS 9.1.1 (each from 18 to 50 s),
  failures checked (a track missing: exit code 2; the memory check: 1, amisqueeze stopped all the same; the MP3
  case run with `amisqueeze`, which LMS feeds raw PCM: 1, "stream formats p p, expected only m"; the slow
  profile: the runner stops with exit code 2 and the profile to use). Found on the way: `Run`'s prompt can be
  followed on the same line by the output of the program started, which made the serial shell client wait 30 s
  (`RUN_DONE`); the first AHI run after a boot keeps about 12 KB (AHI's, hence the warm-up), and `Run` itself
  sometimes 1,064 bytes (under the 16 KB of tolerance). The measured values: MP3 at 72 and 77 dB, 0.475 of full
  scale. A mutation test (decided with the user on 2026-10-23): `ahir_Volume` made to follow the volume again, as
  before the fix of step player-6; `commands-pcm` still passed, memory unchanged to the byte although the resume came
  at 0.0 %: the defect does not show in this profile and this short run, so the scenarios do not guard against
  it (the constant full volume in `amiga.c` does); code put back, `git diff src/` empty, checked again. Left: the
  Music Assistant scenario, before 1.0.

## tools-11 A real TCP/IP stack in WinUAE ✅ (2026-10-24)

- **Goal**: an estimate of what the network costs the CPU of the A1200 (player-9) before the real machine: with
  `bsdsocket_emu`, WinUAE runs the TCP/IP stack on the host and the Amiga pays nothing for it (decided with the
  user on 2026-10-24, for 0.8).
- **Content**: a "real network" profile ([testing.md](testing.md), Real network): the slow profile (a 68020 at
  28.4 MHz) without `bsdsocket_emu`, with WinUAE's NE2000-compatible PCMCIA card on its slirp NAT and 4 MB of fast
  memory (more covers the PCMCIA space at `$600000`, and the Kickstart's `card.resource` does not start, as on a
  real A1200); in the Amiga, AmiNetXDuo (MIT, [github.com/tinic/AmiNetXDuo](https://github.com/tinic/AmiNetXDuo)),
  the stack planned for the A1200, release 1.0.0-rc2 (its SHA-256 checked), its "Fat" variant (compiled for a
  68020 or better, the one the A1200 will run), started only when WinUAE provides no `bsdsocket.library`, so the
  other profiles are unchanged. The server on the host is reached at slirp's gateway address (10.0.2.2, LMS
  only); amiagent through a slirp port redirection.
- **Driver** (changed with the user on 2026-10-24): AmiNetXDuo's own `anxnet.device`, first agreed, does not open
  on WinUAE's emulated card ("unit 0 did not open": WinUAE's log shows it never writes the card's configuration
  register, so it refuses the card while reading its CIS, which is minimal there). `cnet.device` 1.9 instead
  (Aminet `driver/net/cnetdevice`, public domain, 68020 version), the driver AmiNetXDuo's own tests use with that
  emulated card.
- **Measure**: the CPU load as in player-4c ([testing.md](testing.md), CPU load), raw PCM at 44.1 kHz and 16 bits
  (FLAC decoded by the server, about 176 KB/s, the A1200's case), with `NOAUDIO` and with Paula, with
  `bsdsocket_emu` and with AmiNetXDuo; the difference is the estimate.
- **Limits**: the A1200's card is a Xircom (`xircomce2.device`), so the stack is the real one but not the driver
  nor the card; the slow profile is not cycle-exact, so the cost of the PCMCIA accesses is approximate; the 4 MB
  of Zorro II fast memory are neither the size nor the speed of the accelerator's own (11.2 MB, kept with a
  PCMCIA card as far as the user knows, to check in player-9); with the stack up (about 1.3 MB of fast memory),
  playback puts about 266 KB in chip memory (4 KB with `bsdsocket_emu`), slower for the CPU, which the A1200's
  11.2 MB would avoid: the figures with AmiNetXDuo may be somewhat high. An order of magnitude, which player-9
  measures for real.
- **Criterion**: amisqueeze plays from LMS through AmiNetXDuo in the "real network" profile; the loads measured
  and written under player-9, with the limits above.
- **Done** (2026-10-24): amisqueeze 0.7 played `song-44k.flac` from LMS 9.1.1 through AmiNetXDuo and
  `cnet.device` (DHCP from slirp, 10.0.2.15), with `NOAUDIO` and through Paula (AHI unit 0), no `STMo`. Rounds of
  `cpucount` in 10 s, three windows each; each column in its own session, with its own idle reference:

  | | `bsdsocket_emu` | AmiNetXDuo, `cnet.device` |
  |---|---|---|
  | idle | 8,294 8,275 8,260 | 8,042 8,015 8,034 (the stack idle: 3 %) |
  | `NOAUDIO` | 7,912 7,896 7,893: **4.5 %** | 4,613 4,500 4,184: **45 %** |
  | Paula (AHI unit 0) | 3,871 3,889 3,880: **53 %** | 745 544 620: **92 %** |

  So the network, which WinUAE's emulation hid, takes about 40 % of a 68020 at 28 MHz for raw PCM at CD quality,
  and playback through Paula leaves about 8 % of it. The `bsdsocket_emu` column was measured with 8 MB of fast
  memory; again with 4 MB, as the other: 8,305 8,280 8,275 idle, 7,906 7,903 7,922 `NOAUDIO`, 3,859 3,863 3,866
  Paula, the same. Paula there is 53 %, where player-4c and 0.7 measured about 65 % in the same mode (14 bits,
  27,429 Hz): not explained, so these figures compare only within a session. Found on the way: WinUAE's newer syntax for the card
  (`ne2000pcmcia_rom_file=:ENABLED`; `ne2000_pcmcia=` is read only from configurations older than 3.4), the 4 MB
  limit above, and slirp accepting a connection on the host even when nothing listens in the Amiga.

## tools-12 Amiga reference documents ⬜ (outside the versions)

- **Goal**: what AmigaOS does on the points amisqueeze depends on, written once with its source instead of
  scattered through the steps' findings; for contributors, and for amilms (decided with the user on 2026-10-24).
- **Where**: `docs/amiga/`, one document per subject and an index (`README.md`). Developer reference: how
  `ahi.device` or `bsdsocket.library` behave, not how to set them up, which is the user's side (publish-4); the
  two link to each other without repeating. Nothing moved out of `architecture.md`, `testing.md` or
  `CONTRIBUTING.md`, which keep amisqueeze's own rules (the review agents read `architecture.md` whole); the
  reference gives the why, with links both ways.
- **Sources**: every statement says where it comes from: a reading of a source (AHI 6.0), a measurement in
  WinUAE (date, profile), the real machine, or a cited third-party document; what was only seen in emulation is
  said so. Nothing copied from the RKMs, the autodocs or AHI's documentation: paraphrased and cited.
- **Parts**, one commit each, in this order:
  - tools-12a: the index, `ahi.md` and `network.md`, the latter with tools-11's measurements (WinUAE), which
    player-9 completes on the real machine;
  - tools-12b: stack and memory (chip and fast, the Shell's stack, silent overflow, libnix's `__stack`);
  - tools-12c: signals and `Wait()` (the main loop, Ctrl-C, the signals of `bsdsocket.library` and AHI);
  - tools-12d: what differs between Kickstart and AmigaOS versions (2.04 to 3.2) for a program like this one;
  - tools-12e: the Workbench start and the console (icon, ToolTypes, `CON:` window, commodity).
- **Criterion** (each part): every rule and finding on its subject in `architecture.md`, `testing.md`,
  `CONTRIBUTING.md` and the steps' findings has its explanation in `docs/amiga/`, sourced, with the links in
  place; `CONTRIBUTING.md` (Repository layout), the "Before touching X, read Y" table of `CLAUDE.md` and "Where
  we are" updated. Not user-visible: no `CHANGELOG.md` line.

## tools-13 Safety nets from the 0.8 review ✅ (2026-10-29, version 0.8)

- **Goal**: rules of `CONTRIBUTING.md` checked by a script rather than by a review, from the
  [0.8 review](reviews/0.8.md) (agreed with the user on 2026-10-29).
- **Content**, one commit each:
  - `make test` fails when the portable core includes a header other than the C library's, `platform.h`, its own
    and the third-party ones;
  - `check-stack.py` checks the value of `__stack` (16 KB, 32 KB), not only that it is defined;
  - `--check-text` also covers `tools/amiga/cpucount.c`, which runs on the Amiga;
  - `release.yml`: the draft's first line without "tested in WinUAE only" (R3);
  - `CONTRIBUTING.md`: what `check-saves.py` sees, and the host compiler as the CI has it;
  - `CONTRIBUTING.md`, Releasing: the draft published only once `tests.yml` is green on the tag.
- **Kept**: `release.yml` does not wait for the tests (decided on 2026-10-27 in player-12, confirmed with the user
  on 2026-10-29).
- **Criterion**: each new check seen failing on a planted defect, then green; `make test` and the CI green. Not
  user-visible but R3: no `CHANGELOG.md` line.
- **Left for 0.9** (agreed with the user on 2026-10-29): the blind spots of `check-saves.py` (a `movem` or
  `fmovem` load from memory, a `(sp)+` pop), which the compiler does not emit today; the CI's compiler, not
  pinned (gcc 14 named in tools-14).

## tools-14 The code's size and complexity ✅ (2026-10-30, version 0.9)

- Limits per function and per file in `CONTRIBUTING.md`, Code rules, checked by `tools/check-quality.py` in
  `make test` and the CI (lizard pinned with its hashes in `tools/requirements.txt`); the 20 functions over them on
  2026-10-30 listed there, never to grow. Comments without pointers to the project's history, step names checked
  by the same script. Question S6 of `review-protocol-security` points to the limits (agreed with the user on
  2026-10-30).
- The CI's host compiler: gcc 14 named, its exact version printed, rather than a container pinned by digest (the
  user's choice, 2026-10-30).
