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

- **Goal**: an emulated machine that reproduces the target (PiStorm, AHI sound card) with emulated networking.
- **Done (provisional)**: a local configuration, 68020 running AmigaOS 3.2, CPU at maximum speed,
  `bsdsocket_emu=true`, build output mounted as an Amiga volume. Enough for networking (step 1.1).
- **To do**:
  - a machine close to the PiStorm: 68040 with FPU, 32-bit addressing, Zorro III memory (tens of MB);
  - a versioned configuration template in `winuae/`, with machine-specific paths kept in a local, unversioned file.
- **Criterion**: the configuration boots, `bsdsocket.library` is available, amisqueeze connects to the server.

## 0.5 AHI in the emulated Amiga ⬜

- **Goal**: be able to test audio output (step 1.4) in WinUAE.
- **Content**: the AHI package (Aminet) installed in the emulated Amiga, WinUAE's audio driver, AHI mode set in the
  preferences; notes for the real sound card.
- **Criterion**: an AHI test program (or `Play16` on a WAV file) plays a correct sound.
- **Prerequisite of**: 1.4.

## 0.6 Host-side tests ⬜

- **Goal**: test without an emulator whatever can be tested that way.
- **Content**:
  - portable code isolated in files without Amiga dependencies (slimproto serialisation, HTTP and WAV parsing, PCM
    conversion);
  - a test program compiled natively (host or container gcc), with examples taken from squeezelite and LMS;
  - a fake slimproto server in Python that replays scenarios and checks the player's replies.
- **Criterion**: `make test` passes; a known regression (swapped bytes, wrong `STAT` field) makes it fail.
- **Recommended before**: 1.2.

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
