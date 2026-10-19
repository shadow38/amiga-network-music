# Phase 0 — Tooling and test environment

Back to the [plan](README.md).

## 0.1 Toolchain ✅ (2026-10-18)

- **Goal**: build several programs from one repository, whatever the developer's machine.
- **Content**: multi-program `Makefile` (`src/<prog>/` → `out/<prog>`), header dependencies, the
  `tools/check-saves.py` safeguard (register-saving bug in gcc 16.1), `tools/amiga-make.sh` (`$AMIGA_MAKE`,
  toolchain in the `PATH`, local script, Docker), VS Code tasks.
- **Criterion**: `make clean all` produces `out/amisqueeze`; touching a `.h` rebuilds the `.c` files that include
  it.
- **Remaining**: the Docker mode of `amiga-make.sh` has not been tested yet.

## 0.2 Test LMS server ✅ (2026-10-18)

- **Goal**: a Lyrion Music Server reachable from the emulated Amiga, with a small library.
- **Content**: any LMS will do (Docker image `lmscommunity/lyrionmusicserver`, distribution package, NAS…).
  Ports: 3483/tcp (players), 9000/tcp (web UI + audio streams; set `HTTP_PORT` if the port is taken), 9090/tcp
  (CLI). With `bsdsocket_emu`, the emulated Amiga uses the host's network stack: a server reachable from the host
  is reachable from the Amiga.
- **MP3 to PCM conversion**: LMS 9.1 has no `mp3 pcm` rule (regular players decode MP3 themselves). As long as
  amisqueeze only advertises `pcm`, add one in a `custom-convert.conf` (LMS root directory, or the root of a
  plugins directory), modelled on the `ogg pcm` rule and using the `sox` shipped with LMS:

  ```
  mp3 pcm * *
  	[sox] -q -t mp3 $FILE$ -t raw -r 44100 -c 2 -2 -s -
  ```

  Without it, `server.log` says "Couldn't create command line for mp3 playback" and the player receives nothing.
- **Criterion**: web UI reachable, CLI answering `version ?`, library scanned.
- **Validated with**: LMS 9.1.1.

## 0.3 Reference player ✅ (2026-10-18)

- **Goal**: a known-good player to validate the server and compare amisqueeze's behaviour.
- **Content**: squeezelite (Windows builds on SourceForge, `lmsclients` project):
  `squeezelite -s <server>:3483 -n PC-test`.
- **Criterion**: the player shows up in LMS and plays an MP3 from the library.

## 0.4 WinUAE test configuration 🔶

- **Goal**: emulated machines that reproduce the targets, with emulated networking.
- **Done (provisional)**: a local configuration, 68020 running AmigaOS 3.2, CPU at maximum speed,
  `bsdsocket_emu=true`, build output mounted as an Amiga volume. Enough for networking (step 1.1).
- **To do**:
  - two profiles (decided 2026-10-18): **fast**, close to a PiStorm with Emu68 (68040 with FPU, 32-bit
    addressing, Zorro III memory, tens of MB); **slow**, a 68020 accelerator without FPU at its real speed (not
    "as fast as possible", so that CPU load can be measured: Paula mixing, later the decoders);
  - a versioned configuration template in `winuae/`, with machine-specific paths kept in a local, unversioned file.
- **Slow profile, local** (2026-10-19, for step 1.4c): the provisional configuration started with the CPU
  settings overridden (`-s cpu_compatible=true -s cpu_speed=real -s cpu_multiplier=4`, a 68020 at about
  28 MHz, as a typical A1200 accelerator); no versioned template yet.
- **Criterion**: the configuration boots, `bsdsocket.library` is available, amisqueeze connects to the server.

## 0.5 AHI in the emulated Amiga ✅ (2026-10-18)

- **Goal**: be able to test audio output (step 1.4) in WinUAE.
- **Content**: the AHI package (Aminet) installed in the emulated Amiga, AHI mode set in the preferences; notes for
  the real outputs. Two drivers, both kept for the tests (decided 2026-10-18), Paula first:
  - **Paula** (`paula.audio`, Amiga chipset): what an Amiga without a sound card plays; 8 bits per channel
    (14 bits with AHI's combined modes), about 28 kHz at most with a 15 kHz screen mode (AHI resamples 44.1 kHz
    down), mixing done by the CPU;
  - **Toccata** (Zorro II card emulated by WinUAE, `toccata.audio` from the AHI package): a real 16-bit sound card,
    up to 48 kHz stereo; a 16-bit reference, to tell a fault of the player from Paula's limits (a Zorro II card,
    not available on every Amiga: not a target as such).
  - The PiStorm's own AHI outputs (I2S DAC of the PiStorm32-lite FPGA firmware, HDMI with `emu68-hdmi-audio` on a
    Pi 4) cannot be emulated: tested on the real machine (1.9).
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
    2 right) is correct, so the swap comes from `paula.audio`, not from the emulation: to check on a real Amiga
    (1.9);
  - "stereo" modes without "++" put each sound entirely on one side: a stereo stream sent at the centre plays one
    channel only, on one side. amisqueeze needs a **stereo++** mode (1.4).
  - The `filesave.audio` driver ("Filesave" modes) writes what AHI plays to a file (WAV, AIFF…), but only for
    programs using AHI's library interface: its modes are not real-time, the preferences offer them only for
    the "Music" unit, and an `ahi.device` unit set to one by hand writes silence (tried in 1.4b).
- **Prerequisite of**: 1.4.

## 0.6 Host-side tests ✅ (2026-10-18)

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
  Amiga build replayed the 1.1/1.1b cases in WinUAE with the same messages as before the split, and the host
  build connects to a real LMS 9.1.1 with the same MAC as the Amiga build.

## 0.7 Logging and automated runs in WinUAE 🔶

- **Goal**: run a test in WinUAE and read its result from the host, without manual steps.
- **Done (2026-10-18, local setup)**: a **serial shell**. WinUAE connects the Amiga's serial port to a local TCP
  socket (`win32.serial_port=TCP://127.0.0.1:<port>`; the `win32.` prefix is required, otherwise WinUAE logs
  "unknown config entry"); `S:User-Startup` runs `Mount AUX:` and `Run >NIL: NewShell AUX:`; a host script sends a
  command line and reads the output up to the next prompt. Validated: commands, starting amisqueeze in the
  background (`Run NM:amisqueeze >T:log …`), `Status`, `Break <n>`, reading the log. Pitfalls: send one character at
  a time (no flow control, characters get lost otherwise); the console wraps the prompt in control sequences
  (`CSI 20h`, `SI`) to filter out.
- **To do**: start WinUAE from the host and wait for the shell; a generic version of the host script in
  `tools/`; scripted test scenarios.
- **Criterion**: one command on the host starts WinUAE, waits, runs a scenario and prints its result.

## 0.8 Source-level debugging ⬜ (optional)

- **Goal**: breakpoints and stepping in C.
- **Approach**: `bgdbserver` (Aminet `dev/debug/bgdbserver`) in the emulated Amiga, WinUAE's emulated networking,
  `m68k-amigaos-gdb` (`target remote …:2345`) driven by VS Code (cpptools); build with `-g`.
- **Criterion**: stops on a breakpoint in `main.c`, variables readable.

## 0.9 Test Music Assistant server ⬜ (after version 0.2)

- **Goal**: a Music Assistant server reachable from the emulated Amiga, for the Music Assistant back end of the
  remote (2.8 to 2.11) and for amisqueeze as a Music Assistant player (1.7).
- **Content**: any Music Assistant server will do (Docker image, Home Assistant add-on…), with a small local
  library and an access token for the API. To check: the server's ports (web UI and API, its slimproto port for
  players) and whether they collide with a test LMS on the same machine (both servers use 3483 for players: two
  addresses, or one server at a time).
- **Criterion**: web UI reachable, API answering with the token, squeezelite (0.3) playing a track from it.
- **Prerequisite of**: 1.7 (Music Assistant part), 2.8.
