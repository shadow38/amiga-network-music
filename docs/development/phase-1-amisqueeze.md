# Phase 1 — amisqueeze, slimproto player

Back to the [plan](README.md).

A network player for LMS (and Music Assistant): slimproto control connection (TCP 3483), HTTP audio stream, audio
output through AHI. Protocol references: squeezelite (`slimproto.c`, `slimproto.h`, `stream.c`, `pcm.c`) and LMS
(`Slim/Networking/Slimproto.pm`, `Slim/Player/Squeezebox2.pm`).

Design choices: one task and one loop around `WaitSelect()` (sockets, AHI signals, Ctrl-C), because a `SocketBase`
belongs to the task that opened it; packets serialised field by field; buffers allocated (not on the stack).

## 1.1 Control connection ✅ (2026-10-18)

- **Content**: TCP connection to the server, `HELO` (capabilities: `pcm`, 48 kHz max), `STAT` replies (`STMt`
  every 5 s, `STMf`…), player name (`setd`/`SETD`), automatic reconnection; streams refused (`STMn`) but their
  parameters printed.
- **Criterion**: the player shows up in LMS; it reconnects by itself after a server restart; renaming it from
  LMS reaches the Amiga.
- **Validated**: WinUAE (`bsdsocket_emu`), LMS 9.1.1; renaming from the LMS web UI is applied and confirmed. `strm s` received for an MP3: PCM 16 bits, 44.1 kHz,
  stereo, **little-endian**, stream at the control connection's address,
  `GET /stream.mp3?player=<mac> HTTP/1.0`.

## 1.1b Default options ✅ (2026-10-18)

- **Goal**: run amisqueeze without retyping its options.
- **Content**: default options in the global variable `ENV:amisqueeze.prefs` (kept across reboots by a copy in
  `ENVARC:`), parsed with the same `ReadArgs` template as the command line; the command line wins; `SERVER` is no
  longer mandatory on the command line; clear error if no server is given anywhere or if the variable cannot be
  parsed. ToolTypes and Workbench start come later (1.10). Documented in the
  [user guide](../user-guide.md#default-options).
- **Criterion**: after `Echo "SERVER=127.0.0.1 NAME=Amiga" >ENV:amisqueeze.prefs` (and a copy in `ENVARC:`, then
  a reboot), `amisqueeze` alone connects as "Amiga"; `amisqueeze NAME=Other` connects as "Other"; an unknown option
  in the file gives an error naming `ENV:amisqueeze.prefs`.
- **Test**: WinUAE, with an AmigaDOS script running the cases in sequence. Checked under vamos (`-C 68020`): "no
  server" message, command line alone, unknown argument (vamos ignores global variables, so the prefs themselves
  can only be tested in WinUAE).
- **Validated** in WinUAE: prefs alone → player "Amiga"; `NAME=Other` on the command line → player "Other" with
  the server from the prefs; unknown option in the prefs → `ENV:amisqueeze.prefs: wrong number of arguments`,
  return code 20; no server anywhere → "no server", return code 20; after a reboot, `amisqueeze` alone connects
  as "Amiga" (prefs restored from `ENVARC:`).

## 1.2 Receiving the stream, without playing it ✅ (2026-10-18)

- **Goal**: receive the stream and keep the server informed like a real player, without producing sound yet.
- **Content**:
  - on `strm s`: HTTP connection (address 0 = the control connection's), send the provided request, read the
    response headers and pass them to the server (`RESP`);
  - receive the data into a ring buffer (1 MB, about 6 s of 44.1 kHz 16-bit stereo PCM; size defined in one
    place, a `BUFFER` option may come with step 1.6), consumed at real-time speed (PCM rate) by a virtual output
    that later steps replace with a file (1.3) and AHI (1.4); byte counters;
  - states: `STMc` (connected), `STMs` (playback started), `STMd` (end of data), `STMu` (buffer empty), buffer
    fullness and elapsed time in `STAT`;
  - `strm q`/`f`: stop and flush; `cont` and `autostart` handled as in squeezelite.
- **Criterion**, measured on the server through its CLI, not on the web page (its progress bar animates by itself
  between refreshes, even when nothing plays): `waitingToPlay` goes back to 0 once the player reports the start;
  `<player> time ?` increases by about 60 s in 60 s (less than 1 s off);
  with two tracks in the playlist, LMS moves on to the second one and stops after it, driven by the player's
  `STMd`/`STMu`; stop then play again works; no error in `server.log`; no memory leak (`Avail` before/after).
- **Test**: host first (`make test`: stream scenarios in the fake server; then the host build against a real
  LMS), then WinUAE; state sequence compared with squeezelite's on the same scenario.
- **Done**: `stream.c` (HTTP stream, virtual output, gapless next track, pause), `ring.c` (1 MB ring buffer),
  non-blocking connection in both platform layers; single buffer reported as both stream and output buffer.
- **Validated**:
  - `make test`: 124 unit checks (ring buffer, HTTP headers, whole stream sequence on a simulated clock,
    gapless, pause, stop, connection failure, unsupported format, autostart 0) and fake server scenarios streaming
    real PCM over HTTP (state order, 3 s of audio played in 2.97 s, elapsed time within 0.05 s of the clock,
    counters, pause, stop);
  - host build and Amiga build (WinUAE) against LMS 9.1.1: `time ?` advances 60.0 s in 60.0 s (LMS may partly
    extrapolate it from STMs with its own clock); pause: `mode` pause and time frozen, then advancing again after
    unpause; after a seek to 185 s of a track of about 195 s, LMS moves on to the second track, which is at about
    7 s seventeen seconds after the seek: the player consumed the remaining 10 s of data (about 1.76 MB) at the
    PCM rate before reporting the boundary, the independent evidence of the output pacing on the Amiga (the fake
    server measures it for the host build: under 0.05 s drift); then LMS stops at the end of the playlist; play
    again, stop; no error in `server.log`;
  - WinUAE: no memory leak (same free memory after three play/stop cycles), and still working with a 2 KB stack;
    Amiga executable: 21,488 bytes.

## 1.3 Output to a file ✅ (2026-10-18)

- **Goal**: check the received data byte by byte before listening to anything.
- **Content**: an `OUTPUT=<file>` option (command line and default options) that writes what the virtual output
  plays, exactly as received (no conversion yet: the byte swap for the 68k belongs to AHI, 1.4): nothing while
  paused, nothing of what a stop or a seek flushes, the next track right after the previous one. One file per run:
  created (or replaced) at start-up, closed at the end; a file that cannot be created stops amisqueeze (return
  code 20); a write error is reported once, the file is closed and playback goes on.
- **Criterion**: the file is identical to the expected PCM, with two kinds of references: a counter pattern
  (frame n holds n on the left and its complement on the right) encoded in FLAC, which LMS decodes losslessly, so
  the reference is known in advance; and an MP3 decoded by the same `sox` command as the server's conversion rule.
  Two tracks back to back must give both references one after the other (gapless, nothing lost at the boundary).
- **Test**: unit tests (file captured by the stub platform: pause, gapless, stop, errors); fake server scenario
  (two tracks of pseudo-random bytes, file compared with what was sent; file that cannot be created);
  `tools/pcm-compare.py` (identical / prefix / different, and with the counter pattern the counter values around
  the first difference); then the host build and the Amiga build against LMS.
- **Done**: `consume()` in `stream.c` writes each contiguous part of the ring buffer (`ring_read_space()`) before
  dropping it, the only place where the output consumes data; `plat_file_create/write/close` in both platform
  layers (`Open`/`Write`/`Close` of dos.library on the Amiga).
- **Validated**:
  - `make test`: 156 unit checks, fake server scenarios (file identical to the 2.5 s of data sent over two
    tracks; `OUTPUT` in a missing directory: error, return code 20);
  - host build and Amiga build (WinUAE) against LMS 9.1.1, one `play` per run, file compared once the output has
    drained: counter pattern twice (40 s, 7,056,000 bytes) and a 20 s MP3 twice: identical on both builds; a 195 s
    MP3 on the Amiga with a 2 KB stack: identical (34,403,328 bytes), same free memory before and after; Amiga
    executable: 22,268 bytes.
- **Pitfall found**: LMS strips the ID3v1 tag (last 128 bytes) of an MP3 before passing it to `sox`, whose MP3
  decoder then drops the last frame (1,152 samples): seen with the test MP3, where the tag was the only data after
  that frame. A reference decoded from the whole file is then one frame too long: decode the file without its
  ID3v1 tag, or use test MP3s without one (`lame --id3v2-only`).

## 1.4 AHI audio output 🔶 (1.4a, 1.4b done; 1.4c next)

- **Goal**: hear the sound, and hear it right.
- **Content**: `ahi.device`, double-buffered `CMD_WRITE`, `AHIST_S16S`; byte swapping when the stream is
  little-endian; honour the sample size, rate and channel count announced by `strm`. Only `ahi.device` features
  of AHI 4 (tested with AHI 6.6; 4.x expected to work, not checked). User guide: a **stereo++** mode (a plain
  "stereo" mode plays a stereo stream on one side only, seen in 0.5), at the stream's rate when the driver allows
  it (44.1 kHz for LMS) to avoid resampling; a warning at start-up if the unit's mode cannot pan (to check:
  reading the unit's mode through the device API).
- **Prerequisite**: 0.5 (AHI in the emulated Amiga) ✅.
- **Design** (agreed 2026-10-18):
  - an output ("sink") interface in the core: how many bytes it can take now, take these bytes, how many bytes
    were really played. The virtual output (clock) stays for the host build, the tests and `NOAUDIO`; AHI is the
    other sink. Elapsed time, `STMs` at a track boundary and `STMu` follow the bytes **played**, not the bytes
    handed over (AHI is about 0.6 s ahead);
  - AHI: three blocks of about 0.2 s (32 KB, size defined in one place), each carrying its rate and sample
    type, never spanning two tracks; the conversion (byte swap of little-endian streams, `SWAP`) done by the core
    while copying into the block (unit tested); the AHI reply port's signal added to the main wait; stop, flush
    and seek abort the blocks (sound cut at once); 16-bit PCM (other sizes refused for now, 1.7);
  - options: `UNIT=n` (AHI unit, 0 by default), `NOAUDIO` (virtual output), `SWAP` (swaps left and right, off by
    default: for an AHI driver that swaps them, as `paula.audio` does in WinUAE, see 0.5); AHI missing: error
    message, return code 20.
- **Sub-steps**: 1.4a the sink interface with the virtual output only, no change of behaviour (`make test` and the
  `OUTPUT` comparisons unchanged); 1.4b the AHI sink, unit tests with a fake AHI (blocks, conversion, track
  boundary, played time, `STMs`/`STMu`, stop, pause) and a command scenario against LMS; 1.4c listening
  (Toccata, then Paula; test tones from LMS, a whole track), LMS timing criteria of 1.2, no leak, 2 KB stack, and
  CPU load with Paula on a slow 68020 profile of WinUAE (created then, see 0.4).
- **1.4a done** (2026-10-18): sink interface, played positions, track format kept from `strm s`; no change of
  behaviour (`make test`, `OUTPUT` comparisons against LMS on both builds).
- **1.4b done** (2026-10-18): `audio.c` (portable: blocks, conversion, played bytes, pause), `ahi.device` calls in
  `amiga.c`, options `UNIT`, `NOAUDIO`, `SWAP`; `OUTPUT` keeps the virtual output (decided 2026-10-18: the file
  holds exactly what was played). What `ahi.device` taught (its source, AHI 6.0):
  - a request is chained (`ahir_Link`) only to one **already playing**: a request linked to one that is itself
    waiting is never played nor ended (the first version hung on the third block). So at most two blocks are
    with the device, the playing one and the next one linked to it; the third one is filled in advance and sent
    when the first one ends;
  - aborting the playing request also aborts the one linked to it; aborting the linked one first leaves a stale
    reference in the device, which then hangs (`CloseDevice`) and slows the whole system: abort from the first;
  - `CMD_STOP` only holds back the requests sent after it, for every program: a pause aborts the blocks (AHI 6
    gives the bytes played in `io_Actual`; AHI 4: estimated from the clock) and sends the rest again on resume;
  - a "Filesave" mode set by hand on a device unit writes only silence (not real-time): no automated check of
    the AHI output this way; the byte-exact checks are the unit tests' (fake device enforcing the two rules
    above).
- **Validated (1.4b)**: `make test` 245 unit checks (the AHI 4 pause path included, by the fake device); WinUAE
  against LMS 9.1.1, Toccata (unit 1) and Paula (unit 0): play (`time ?` +3.00 s in 3 s), pause (time frozen),
  resume (+3.01 s in 3 s), stop, a 10 s track played to its end in 10.3 s, clean exit; `OUTPUT` on the Amiga
  build keeps the virtual output and its file stays identical to the counter pattern (twice, 7,056,000 bytes).
- **Criterion**: correct test tones (440 Hz at the right pitch, left/right in the right place, no noise); a whole
  track without crackling; CPU load measured with Paula on the slow profile (0.4).
- **Test**: WinUAE with both AHI drivers of step 0.5, Paula first (Amiga chipset, the output of Amigas without a
  sound card) and Toccata (16-bit reference), then the real machines (1.9).

## 1.5 Commands and volume ⬜

- **Content**: pause and resume (`strm p`/`u`, `STMp`/`STMr`), stop, flush, skip; volume (`audg`, 16.16 gain to
  AHI volume); exact elapsed time in `STAT` (based on what was actually played).
- **Criterion**: every command from the LMS UI acts on the Amiga; the displayed progress stays right after pause
  and resume.

## 1.6 Robustness ⬜

- **Content**: buffer underrun (slow network) then recovery; server dropping during playback; Ctrl-C at any time
  (AHI requests aborted, sockets closed); long playback.
- **Criterion**: one hour of playback without crash or memory leak (`Avail` before/after); Ctrl-C always returns
  cleanly.

## 1.7 Other PCM formats and Music Assistant ⬜

- **Content**: WAV and AIFF headers (format `?` + header parsing, or explicit values); 48 kHz; mono; 8 and 24 bits
  if the server sends them. Music Assistant: the player's `wav` output codec (MA offers no raw PCM); check what MA
  sends in `strm`.
- **Criterion**: test tones pass in each format; the player works with Music Assistant.

## 1.8 Local decoding: FLAC, then MP3 ⬜

- **Goal**: stop depending on server-side conversion (a regular LMS has no MP3 → PCM rule).
- **Content**: FLAC then MP3 decoders in portable C (lightweight, MIT-compatible libraries, e.g. dr_flac and
  minimp3), tested on the host first; advertised in `HELO` only once validated.
  - **Decoder interface** (`decoder.h`): compressed bytes in, 16-bit PCM out; integers and pointers only across
    it, so a decoder can be built with other CPU options than the rest of the program.
  - **FLAC**: one 68020 build for every CPU (integer only, no FPU needed). Watch the 32×32→64 multiply
    (`muls.l dx,dh:dl`): the 68060 has none in hardware and `68060.library` traps it (very slow). The 32-bit
    prediction path is enough for 16-bit audio (to check in dr_flac, which seems to pick it by itself); a check of
    the generated assembly, like `tools/check-saves.py`, fails the build if a 64-bit `muls.l` appears in the hot
    loop.
  - **MP3**: minimp3 computes in floating point, so it needs an FPU. Two executables at most:
    `amisqueeze` (`-m68020`, no FPU: MP3 through `mpega.library` if installed, otherwise not advertised and left
    to the server) and `amisqueeze.fpu` (`-m68020 -m68881`: minimp3; checks `AttnFlags` at start-up and exits
    with a clear message without an FPU). `mpega.library` is opened at run time, never linked or shipped; its
    per-CPU versions are its own business; its licence is to be checked.
  - **Other variants only on measured gain**: `-m68040 -m68881` (emits `fdmul`/`fdadd`, 040/060 only) or
    `-m68060` (`___muldi3` instead of the 64-bit `muls.l`). Facts checked with the toolchain (2026-10-18):
    `-m68030` and `-m68040` generate the same integer code as `-m68020` and link the same `libm020`; `-m68040`
    alone stays **soft-float** (`___muldf3`): the FPU needs `-m68881`; multilibs are 68000, 68020, 68020+68881
    and 68060.
  - **Rejected**: one executable holding several builds of a decoder picked at run time (with `-m68881`, floating
    point values are returned in `fp0` instead of `d0`/`d1`: mixing both ABIs and their libraries invites silent
    bugs); one executable per Amiga model (too many builds to test).
  - **Build and release**: a `CPU` variable in the Makefile, one output directory per variant; both executables in
    the release archive, with a table in the user guide saying which one to pick for which CPU. The host build is
    unchanged.
  - **Server-side conversion stays the fallback** for MP3 when the CPU is too slow. LMS 9.1.1 has no default
    `mp3 flc` nor `mp3 pcm` rule (checked in its `convert.conf`: MP3 is only passed through, or re-encoded by
    `lame` to limit the bitrate), so the server's owner must add one, as on the test server (`mp3 pcm` with
    `sox`); an `mp3 flc` rule would halve the network bandwidth, at a `flac` compression level to choose for the
    CPU. Music Assistant sends FLAC by default.
- **Expected CPU** (estimates, not measured; 44.1 kHz stereo): FLAC levels 5–8 (LPC, about 8–12 multiplies per
  sample) need a 68040 (25 MHz borderline, 40 MHz comfortable) or a 68060; FLAC levels 0–2 (fixed predictors, no
  multiply) are within reach of a 50 MHz 68030; MP3 with minimp3 needs an FPU and roughly a 68040 at 50 MHz or
  better, with little margin. Reference point: the MiSTer Minimig pipelined AP68040 core in development
  (2026-10-17, SysInfo 4.4: 42.04 MIPS, 13.15 MFLOPS; xsysinfo 0.12.0: 45 MIPS, 8.99 MFLOPS; figures from
  different tools are not comparable).
- **Measurement**: a decode-only benchmark (file in, PCM discarded, time and real-time factor printed), run on
  the host, in WinUAE in 68020/68030 cycle-exact mode (faithful timings; 040/060 timings are not), then on real
  hardware or the FPGA core; each variant compared on the same machine. A decoder is kept for a CPU if it runs at
  least twice real time (margin for the network stack and AHI).
- **Criterion**: direct playback of a FLAC and an MP3 file without conversion; acceptable CPU load on the target;
  benchmark figures recorded here for each variant and machine tested.

## 1.9 Real machine ⬜

- **Content**: both kinds of target with a TCP/IP stack (AmiNetXDuo or Roadshow) and an LMS server reachable
  from the local network: a PiStorm with Emu68, sound through its own AHI outputs (I2S DAC of the PiStorm32-lite
  FPGA firmware, announced, to check; HDMI with `emu68-hdmi-audio` on a Pi 4) or Paula; a 68020 accelerator
  without FPU, sound through Paula.
- **Criterion**: the criteria of 1.4 to 1.6 met on the real machine.

## 1.10 Finishing touches ⬜

- **Content**: automatic server discovery (UDP broadcast on port 3483); start from the Workbench with an icon and
  ToolTypes (read with the same template; precedence: built-in defaults, `ENV:amisqueeze.prefs`, ToolTypes,
  command line), with a console window or quiet mode; running in the background (`Run`, or at startup);
  `LOG=<file>` option: every message written both to the console and to the file (AmigaDOS has no `tee`; today a
  redirection `>file` sends the messages to the file only), appended and flushed after each message like the
  console, usable from the default options too.
- **Criterion**: started without arguments, the player finds the server and shows up under the chosen name; with
  `LOG=<file>`, the messages appear in the Shell window and, at the same time, in the file.
