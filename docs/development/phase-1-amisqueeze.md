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

## 1.4 AHI audio output ⬜ (next step)

- **Goal**: hear the sound, and hear it right.
- **Content**: `ahi.device`, double-buffered `CMD_WRITE`, `AHIST_S16S`; byte swapping when the stream is
  little-endian; honour the sample size, rate and channel count announced by `strm`.
- **Prerequisite**: 0.5 (AHI in the emulated Amiga).
- **Criterion**: correct test tones (440 Hz at the right pitch, left/right in the right place, no noise); a whole
  track without crackling.
- **Test**: WinUAE, then the real machine.

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
  minimp3), tested on the host first; advertised in `HELO` only once validated; build options for the PiStorm (68040/FPU) if needed.
- **Criterion**: direct playback of a FLAC and an MP3 file without conversion; acceptable CPU load on the target.

## 1.9 Real machine ⬜

- **Content**: PiStorm + AHI sound card + TCP/IP stack (AmiNetXDuo or Roadshow); LMS server reachable from the
  local network.
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
