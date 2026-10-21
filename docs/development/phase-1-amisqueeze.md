# Phase 1 — amisqueeze, slimproto player

Back to the [plan](README.md).

A network player for LMS (Music Assistant later, after version 0.2): slimproto control connection (TCP 3483), HTTP
audio stream, audio output through AHI. Protocol references: squeezelite (`slimproto.c`, `slimproto.h`, `stream.c`, `pcm.c`) and LMS
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
  parsed. ToolTypes and Workbench start came later (1.10, version 0.5). Documented in the
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
  - `strm q`/`f`: stop and flush; autostart 2 and 3 (wait for `cont`, radio metadata) played as 0 and 1; `cont`
    itself ignored ("not handled"): internet radios' metadata (ICY) is planned for 0.9.
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
- The `BUFFER` option mentioned in Content did not come with 1.6: an open point of the
  [plan](README.md#open-points-review-of-2026-10-19).

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

## 1.4 AHI audio output ✅ (2026-10-19)

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
- **1.4c, test tones heard** (2026-10-19, WinUAE, LMS 9.1.1, `tone-440`, `tone-left`, `tone-right` in one
  playlist, 20.5 s for 20 s of audio, no `STMo`): Toccata (unit 1) right, sides in place; Paula 14-bit (unit 0)
  sides swapped without `SWAP`, in place with `SWAP`. Occasional crackle with Paula, also heard with the AHI test
  tone played without amisqueeze: it comes from the emulation or `paula.audio`, not from amisqueeze (to check on
  the real machine, 1.9). A whole track (a 156 s, 48 kHz song, as MP3 then as FLAC) on Paula with `SWAP`: right
  speed and pitch, clean MP3 to FLAC boundary, 312.5 s for 312 s, no `STMo`. A seek (`time 141` from LMS) restarts
  the stream at the right offset (exactly 15.0 s of data left). Learnt on the server side: LMS announces in `strm`
  the rate of the **source file**, even when its conversion rule resamples; LMS's own `ogg pcm` rule (`sox -r
  44100`) therefore makes a 48 kHz source play 9 % too fast on any PCM-only player (our test `mp3 pcm` rule had
  copied it, fixed).
- **1.4c, the rest** (2026-10-19, measured without the user):
  - LMS timing criteria of 1.2 with the AHI output (Paula, `SWAP`): time +60.01 s in 60.02 s, pause frozen,
    resume, seek, next track, end of the playlist, stop: all as with the virtual output;
  - stack: `Stack 2048` (checked with `Status FULL`: amisqueeze gets 2048), three rounds of the command scenario
    plus a seek and a track boundary: same results as with 4 KB;
  - memory: `Avail FLUSH` around each round, chip unchanged; fast drops by 1,064 bytes now and then, also when
    only `C:Wait` is started the same way (`Run` with output to a WinUAE folder), and not on every amisqueeze run:
    the system's, not amisqueeze's;
  - CPU load on the slow profile (68020 without FPU, WinUAE approximate mode at 4 x 7.09 = 28.4 MHz, as a
    typical A1200 accelerator; free CPU counted by a priority -128 loop, load = 1 - count while playing / count
    when idle, 10 s windows): through AHI and Paula, **66-68 % in 14 bits** (unit 0) and **61-65 % in 8 bits** (unit 2), for
    44.1 and 48 kHz streams, no `STMo`, LMS time exact; with `NOAUDIO`, 2-5 %. So nearly all of it is the sound
    output: AHI resampling and mixing for Paula (27,429 Hz), plus the byte swap into the AHI blocks (not measured
    apart; estimated at a few percent). Limits of the figure: WinUAE's networking runs TCP/IP on the host (a real
    stack, Roadshow, adds its own load) and its Zorro II fast RAM is not the accelerator's own RAM. About a third
    of the CPU is left on such a machine: to keep in mind for local FLAC decoding (1.8).
- **Validated with the user** (2026-10-19): Paula's crackles left to the real machine (1.9); the CPU load accepted
  for now (lighter AHI settings to look at in 1.8 if decoding does not fit); `SWAP` stays a manual option until
  the real machine shows whether its channels are swapped too (1.9). Not done then: the start-up warning for a mode
  that cannot pan (Content above); an open point of the [plan](README.md#open-points-review-of-2026-10-19), done
  in 1.10 (version 0.4).
- **Criterion**: correct test tones (440 Hz at the right pitch, left/right in the right place, no noise); a whole
  track without crackling; CPU load measured with Paula on the slow profile (0.4).
- **Test**: WinUAE with both AHI drivers of step 0.5, Paula first (Amiga chipset, the output of Amigas without a
  sound card) and Toccata (16-bit reference), then the real machines (1.9).

## 1.5 Commands and volume ✅ (2026-10-19)

- **Content**: pause and resume (`strm p`/`u`, `STMp`/`STMr`), stop, flush, skip; volume (`audg`, 16.16 gain to
  AHI volume); exact elapsed time in `STAT` (based on what was actually played).
- **Criterion**: every command from the LMS UI acts on the Amiga; the displayed progress stays right after pause
  and resume.
- **Already done in 1.4**: pause, resume, stop, flush, seek and next track, elapsed time from the bytes played.
- **Design** (agreed 2026-10-19; the AHI volume replaced by scaling the samples in 1.6, see there): the 16.16 gain
  of `audg` is kept by the audio output and sent with each block
  (`ahir_Volume`): blocks already with the device keep theirs, so a change is heard 0.2 to 0.4 s later, at no CPU
  cost (scaling the samples ourselves would act at once but cost about a tenth of a 28 MHz 68020); the louder of
  left and right (LMS sends the same value, or a balance: no balance through AHI for now), never above 1.0 (AHI
  does not amplify); `adjust` 0 (fixed volume) = full; ignored without the audio output (`OUTPUT` stays
  byte-exact); with `VERBOSE`, `volume: n.n % of full amplitude` when it changes. Replay gain (`strm s`) left out.
- **Done** (2026-10-19): `audio_set_volume()` (`audio.c`), `plat_audio_send()` takes the volume; unit tests (262
  checks: volume per block, the next block only, louder side, at most full, mute, fixed volume, kept across a
  pause) and the fake server (`audg` in the session scenario).
- **Validated** (2026-10-19, WinUAE, LMS 9.1.1, Paula): every command sent through LMS's CLI (volume 100, 25, 50,
  +10, mute and unmute, pause and resume, next, seek, previous, track chosen in the playlist, stop) acts as
  expected, no `STMo`; heard by the user (100, 25, mute, unmute, 50): each change clear, no click. Learnt from LMS
  (`Slim/Player/Squeezebox2.pm`, `Player.pm`): its volume curve gives 50 = 0x0F00 (about -25 dB), 25 = 0x039E;
  mute, unmute and pause fade through several `audg` in about a second; "fixed volume" (`digitalVolumeControl`
  0, sent as `adjust` 0) is refused for a player without `HasDigitalOut` in its capabilities, so LMS never sent
  `adjust` 0 to amisqueeze then; since 1.10 (version 0.4) amisqueeze advertises `HasDigitalOut=1` and handles
  it (fixed volume).

## 1.6 Robustness ✅ (2026-10-19)

- **Content**: buffer underrun (slow network) then recovery; server dropping during playback; Ctrl-C at any time
  (AHI requests aborted, sockets closed); long playback.
- **Criterion**: one hour of playback without crash or memory leak (`Avail` before/after); Ctrl-C always returns
  cleanly.
- **Plan** (agreed 2026-10-19): 1.6a scenarios of the fake server (`make test`); 1.6b the same against the real LMS
  in WinUAE, through a local TCP relay between the emulated Amiga and LMS that can slow the stream down, hold it
  or reset it, and a server restart; 1.6c Ctrl-C at every moment; 1.6d one hour on the slow profile (Paula). When
  the server goes, playback stops at once (kept: the server decides what follows when it is back).
- **1.6a done** (2026-10-19): the fake server's HTTP side can hold the stream (`stall_at`, `stall`) or reset it
  (`cut_at`, RST). Scenarios: stream held 3 s after 1.5 s of audio (`STMo` once, playback resumes, 4 s played in
  5.4 s, `OUTPUT` file identical to the data sent); stream reset after 1.5 s (`DSCO 2`, what arrived is played to
  the end, byte for byte); control connection lost during playback (stream closed, reconnection, nothing playing
  afterwards). Unit test of a buffer running dry with the AHI output (`STMo` once, nothing lost). 
- **1.6b, against LMS 9.1.1** (2026-10-19, WinUAE, Paula):
  - stream held 10 s (more than the ~6 s buffer): `STMo` once, playback resumes, LMS time right before and after;
  - stream slowed to 68 % of the CD rate for 40 s: LMS handles it itself: on `STMo` it pauses the player (`strm
    p`), waits for the buffer to fill again, then resumes (`strm u`); so playback goes on in long stretches with
    pauses, not in tiny pieces; normal again as soon as the rate is;
  - stream reset: WinUAE's networking reports it as a normal end (`DSCO 0`); what arrived is played, then LMS
    goes on with the next track;
  - LMS restarted during playback: playback stops, the player reconnects, LMS keeps its playlist but stays
    stopped (its choice), a new playback works. **Found**: while LMS was down, amisqueeze connected 121 times in
    4 s: the Incus proxy (as any relay) accepts the connection and closes it at once, and amisqueeze reconnected
    without waiting. **Fixed**: a connection lost less than 10 s after it was made is no real session: wait 5 s
    before the next attempt, as after a refused connection ("retrying in 5 s"); after a real session, reconnect at
    once (fake server: 5.0 s, then 0.0 s).
- **1.6c** (2026-10-19, WinUAE): `Break` while waiting to reconnect, connected and idle, waiting for the stream's
  headers, playing, paused, with an empty buffer, after the end of playback, and twice in a row: always a clean
  stop (`stopped`), AHI opens again on the next run; memory: the 1,064-byte system steps of 1.4c only.
- **1.6d** (2026-10-19, slow profile, Paula, two tracks of 44.1 and 48 kHz in a loop): one hour without any `STMo`,
  still playing, pause and resume still working at the end. LMS time lagged behind the clock by 16 s over the
  hour (-0.44 %). Measured minute by minute: the lag comes in jumps (1 s once, 6.5 s once), with the music
  following the clock in between and even catching up a little afterwards (+0.15 %); the protocol is clean (every
  track received whole, no stop nor flush); track boundaries, both ways and back to the first track, are exact to
  0.04 s; on the fast profile, -1.0 s in 20 min. So the slow profile's emulation (approximate CPU at real speed)
  falls behind now and then and catches up; amisqueeze counts the bytes actually played. To check on the real
  machine (1.9).
- **Found at the end of the hour: AHI corrupts memory** (2026-10-19). Twice amisqueeze vanished after a stop
  without printing `stopped`, the system slowed down for seconds, and about 7 KB were lost (AHI and the sockets
  not closed). Reproduced on a freshly started WinUAE with: playback, pause, resume, stop. The largest free block
  (`Avail`) shows it: intact while paused, broken right after the resume (5.7 MB down to 0.5 MB, or a bogus 537
  MB), before any stop; the hang or crash comes later, depending on what was hit. Cause: LMS fades the volume
  down to 0 before a pause, so the blocks sent again on resume carried `ahir_Volume` 0 (or nearly). With the
  volume at full: no corruption; with a minimum of 1: corruption and a complete hang; low volumes without a pause:
  none. So ahi.device 6.6 with paula.audio corrupts the free memory list when requests sent after an abort carry
  a low volume (its source, AROS copy, does not show why). **Fixed**: the core scales the samples itself when a
  block is first sent (the rest of a paused block goes again as it was converted), the device always gets full
  volume (as in 1.4, where pause and resume were tested many times); unit tests check the samples bit for bit,
  pause included. Sequence replayed twice: memory intact, clean stop. Cost on the slow profile: none at volume
  100; at 50, Paula 14-bit 65 % to 88 % (8-bit 60 % to 84 %); a first version with one 32-bit multiplication per
  sample (about 43 cycles on a 68020) reached 100 %, two 256-entry tables (sample = high byte * 256 + low byte)
  brought it down. User guide: on a slow Amiga, leave LMS at 100 (agreed 2026-10-19); later possibilities: a
  `FIXED` volume mode (1.10), a faster loop. Final build, one hour on the slow profile at volume 50 (88 % load):
  no `STMo`, 20 tracks, pause, resume and stop clean, largest free block unchanged. While WinUAE's window was
  minimised, the emulated machine fell behind real time (2.5 min in 10 min), the music following the Amiga's own
  clock exactly: time measurements belong to the real machine (1.9), WinUAE only checks crashes, underruns and
  memory. Test configuration: `win32.iconified_pause=false`, `win32.inactive_pause=false` (an emulation paused
  by a stray key had stopped an earlier run).

## 1.7 Other PCM formats and Music Assistant 🔶 (1.7a done, 1.7b first test and WAV header validated 2026-10-21)

- **Content**: WAV and AIFF headers (format `?` + header parsing, or explicit values); 48 kHz; mono; 8 and 24 bits
  if the server sends them. Music Assistant: the player's `wav` output codec (MA offers no raw PCM); check what MA
  sends in `strm`.
- **Criterion**: test tones pass in each format; the player works with Music Assistant.
- **Split** (decided 2026-10-19): **1.7a** the formats, tested with LMS (now); **1.7b** Music Assistant, after
  version 0.2, with the test server of step 0.9.
- **1.7a, what LMS 9.1.1 sends** (observed 2026-10-19 with the host build, test tones in each format): always
  format `p` with explicit values, the samples only: **LMS strips the WAV and AIFF headers itself**, so no header
  parsing is needed with LMS (maybe with Music Assistant, 1.7b). 16-bit FLAC/WAV: size `1`, little-endian; 24-bit
  FLAC/WAV: size `2` (3 bytes per sample), little-endian; 8-bit WAV: size `0`, **unsigned** (centred on 0x80);
  AIFF: endianness `0` (big-endian); mono: channels `1`. A 96 kHz FLAC is not played at all: amisqueeze
  advertises 48 kHz at most and LMS cannot lower the rate when it makes PCM ("Couldn't create command line for
  flc playback"); left as it is (decided 2026-10-19): with FLAC decoding (1.8), LMS can send a FLAC resampled to
  48 kHz (its `flc flc` rule resamples).
- **1.7a done** (2026-10-19): the audio output takes 8, 16, 24 and 32-bit samples and converts them to 16-bit
  big-endian when a block is sent (24 and 32 bits: the two most significant bytes; 8 bits: unsigned to signed,
  converted from the end of the block since they double in size, so an 8-bit block is filled to half); AHI always
  gets 16 bits (AHI 4). The bytes played are counted in bytes of the stream (each block knows its sample size), so
  the elapsed time, the track boundaries and pauses inside a converted block stay right. The check that refused
  non-16-bit streams (`STMn`) is gone. Unit tests: each conversion bit for bit against an independent one (8-bit
  stereo, 24-bit little-endian stereo, 24-bit big-endian mono, 32-bit, 16-bit big-endian), the time after the
  first block, a pause in a 24-bit block, a gapless 16-bit to 24-bit boundary (328 checks). WinUAE (Paula):
  seven test tones (WAV 16, FLAC 24, FLAC mono, WAV 24, WAV 8, AIFF, FLAC 16) in one playlist, 70 s in 70.4 s,
  no `STMo`; heard by the user: same pitch and level, boundaries not noticeable.
- **1.7b, first test** (2026-10-21, amisqueeze 0.6, Music Assistant 2.10.7 with its Squeezelite provider, test
  server of 0.9; WinUAE JIT + FPU, `amisqueeze.fpu`, Toccata; playback started from the web UI; read alongside its
  slimproto library, aioslimproto). No code changed.
  - **Output codec `flac` (the default): works.** The player shows up (model `squeezeplay`, model name
    AmiSqueeze, its name asked with `setd`). A library MP3 arrives as FLAC (MA transcodes everything to the
    player's output codec). Heard by the user: playback, pause and resume (`strm p`, `strm u`), seek (`strm f`,
    `strm q`, then a new `strm s` whose stream starts at the position: a whole FLAC with its header), three
    gapless MP3s (`STMd`, then the next `strm s` with autostart 3, appended to the buffer: no gap), end of the
    queue (`STMu`).
  - **How MA starts a stream, unlike LMS**: `strm s` without the stream's parameters (`f????`) and autostart 2 or
    3 (wait for `cont`); after the HTTP response headers (`RESP`), it sends `codc` (the codec, from the
    response's content type) and then `cont` (the player holds the body back until then). amisqueeze ignores
    both (`codc`/`cont not handled`), reads the body at once and treats autostart 2/3 as 0/1; MA then sends
    `strm u`, and playback starts. Harmless with FLAC (the decoder reads everything from the stream). The request:
    `GET /single/<session>/<mac>/<id>/<mac>.flac`, with `Range: bytes=0-` and a VLC user agent. Also received
    and ignored: `vers`, `aude`, `grfb`, `grfe`.
  - **Volume**: the same SqueezePlay curve as LMS for a software player (-74 dB); at each connection `audg` 50
    (aioslimproto's default), then the volume MA keeps for the player: 20 for a new player (-44.4 dB, 0.6 % of
    full amplitude, which the player reports as such), then whatever the user set. Not a defect.
  - **Output codec `wav`** (the slow Amiga's case: no FLAC fast enough, no MP3 without FPU): **loud noise**.
    `strm s` (and `codc`) announce PCM 16 bits, 44.1 kHz, stereo, little-endian, but the stream is a **24-bit
    WAV**: `WAVE_FORMAT_EXTENSIBLE`, 2 channels, 44100 Hz, 6 bytes per frame, a 102-byte header (`fmt ` of 40
    bytes, a `LIST` chunk, `data` of size `FFFFFFFF`). amisqueeze plays it as 16-bit samples, header included.
    No setting forces 16 bits: the provider declares 16 and 24 bits for every slimproto player (up to its
    `MaxSampleRate`), and `codc` comes from the content type, without parameters, so its 16 bits / 44.1 kHz are
    defaults. Hence: **amisqueeze must read a WAV header at the start of a PCM stream** (as squeezelite does),
    the "header parsing" foreseen in the content of 1.7; the 24-bit conversion already exists (1.7a). In version
    0.7 (decided with the user on 2026-10-21), designed with the user first.
  - Not tried: output codecs `mp3` and `aac`, radios, groups of players, an MA server elsewhere than the test
    one.
- **1.7b, the WAV header** (design agreed with the user on 2026-10-21, version 0.7). Read alongside: aioslimproto
  (for `wav`, `strm s` gets format `p` with the defaults of a content type without parameters: 16 bits, 44.1 kHz,
  stereo, little-endian; each stream, after a seek and for the next track too, begins with its header) and
  squeezelite's `pcm.c` (it reads the header only with its `-W` option, off by default; the header then wins).
  - In the raw PCM decoder, at the start of **each** PCM track, whatever the server: `RIFF` ... `WAVE` in the first
    12 bytes means a header, anything else raw PCM as before (LMS strips the headers: nothing changes with LMS).
  - The header is read again from the start at each call until its `data` chunk is in the buffer (MA's is 102
    bytes), at most 4 KB of header; a header not all there yet is waited for, nothing consumed; a track that ends
    before its `data` chunk, or a longer header, is skipped with a message. `ring_peek` reads without consuming,
    across the end of the ring.
  - The header wins over `strm`. Accepted: format 1 (PCM) and `WAVE_FORMAT_EXTENSIBLE` with the PCM sub-format;
    8, 16, 24 or 32 bits (the container's size), 1 or 2 channels, a rate other than 0. Anything else (floating
    point, compressed) skips the track with a message rather than play noise. VERBOSE prints the format found.
  - A `data` size of `FFFFFFFF` or `7FFFEFFC` (squeezelite's two "unknown" values; MA sends the first) plays to the
    end of the stream; any other is counted down, and what follows the data (a `LIST` chunk at the end of a file) is
    dropped. The pad byte after a chunk of odd size is skipped (squeezelite does not).
  - The call that reads the header consumes it, sets the new format and returns no PCM: the output computes its
    block from the format before calling the decoder, so a header saying 8 bits where `strm` said 16 would
    otherwise overrun the AHI block when converted.
  - Not done: AIFF (MA does not send it), `codc` and `cont` (still ignored: the header is enough).
  - Criterion: host tests (the header cut at every length, across the end of the ring, MA's 24-bit extensible
    header with a `LIST` chunk, odd chunk, floating point refused, a limited `data` size, 16 bits announced and 8
    in the header on the audio output; a fake server scenario like MA's, `OUTPUT` compared byte for byte, elapsed
    time at 6 bytes per frame), each seen failing without its code; in WinUAE, the slow 68020 profile without JIT,
    `amisqueeze` (not `.fpu`), Paula, MA's `wav` codec: heard by the user (playback, pause, seek, two gapless
    tracks), CPU load measured, no `STMo`.
  - Code and host tests done on 2026-10-21 (`decoder.c`, `ring_peek`; `test_wav_header`, `test_wav_limited`,
    `test_wav_refused`, `test_wav_audio_8bit`, `scenario_wav`), each test seen failing without its code in a
    throwaway copy (nine changes planted: no header read, header and data in one call, which ASan reports as an
    AHI block overrun, no pad byte, no data limit, no wait for a header cut short, two `ring_peek` faults, no check
    of a rate of 0, none of a missing format chunk, which UBSan reports as a division by zero). The
    header captured from MA on 2026-10-21 has the layout the tests build.
  - **Validated in WinUAE on 2026-10-21**: slow 68020 profile without JIT, `amisqueeze`, Paula (unit 0, `SWAP`),
    MA 2.10.7 with the `wav` codec. 32 streams in the session (tracks started from the web UI, pauses, seeks, each
    a new stream with its header), all read: 24 bits at 44.1 or 48 kHz; no `STMo`, no `wav:` message. Two gapless
    boundaries (MA appends the next track after `STMd` with autostart 3; the second one after a seek 15 s before
    the end), one of them from 48 to 44.1 kHz. Heard by the user: right pitch, no crackle at volume 100, fluid and
    without a gap at the boundaries. CPU load while playing 24 bits at 48 kHz: 75 % (`cpucount`, 2046 rounds in
    10 s against 8204 idle; 61-68 % in 1.4c, 16 bits at 44.1 kHz). Not tried: `mp3`, `aac` codecs (next item of
    0.7).

## 1.8 Local decoding: FLAC, then MP3 ✅

- **Goal**: stop depending on server-side conversion (a regular LMS has no MP3 → PCM rule).
- **Content**: FLAC then MP3 decoders in portable C (lightweight, MIT-compatible libraries, e.g. dr_flac and
  minimp3), tested on the host first; advertised in `HELO` only once validated. (Written before the design
  further down; where they differ, the design and what was done prevail: no `mpega.library` in `amisqueeze`, which
  leaves MP3 to the server; no `CPU` variable in the Makefile, but a second set of flags for `amisqueeze.fpu`; the
  64-bit `muls.l` check below was not written, see the open points of the [plan](README.md#open-points-review-of-2026-10-19).)
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
- **Design** (agreed 2026-10-19):
  - **How LMS chooses**: the player lists in `HELO` the formats it decodes; for each track LMS sends the file as
    is if its format is listed, otherwise converts it with a rule of `convert.conf` (default rules: `flc pcm`,
    `wav pcm`, `aif pcm`; no `mp3 pcm`), otherwise refuses it. So amisqueeze decides once, at connection, what
    the server sends: advertising `flc` means every FLAC comes as is, whatever its compression level.
  - **Libraries**: dr_flac (public domain or MIT-0, integer only) and minimp3 (CC0, floating point: needs an
    FPU), copied into `third_party/` with their version and licence.
  - **Data path**: the ring buffer holds the compressed stream; the decoder writes 16-bit PCM straight into the
    AHI blocks; time is counted in decoded samples, not in bytes received. `OUTPUT` writes the decoded PCM (what
    would be played), so FLAC can be compared byte for byte with the reference decoder.
  - **Two executables**: `amisqueeze` (68020, no FPU: FLAC) and `amisqueeze.fpu` (68020 + 68881: FLAC and MP3,
    refuses to start without an FPU). What is advertised depends on the machine, decided at start-up:
    **a short built-in FLAC level 8 sample (the worst case) is decoded for about 0.3 s**, and FLAC is advertised
    only if it decodes at least twice real time (same later for MP3 in `amisqueeze.fpu`); an option forces it
    either way. The CPU model alone is not enough (a 25 MHz and a 40 MHz 68040 look the same to AmigaOS). The
    same code is the benchmark tool, shipped in the archive. Expected result: 68020/030 keep PCM (LMS converts
    FLAC by its default rule; MP3 needs the server rule), a 68040/060 without FPU gets FLAC, with FPU (PiStorm)
    FLAC and MP3. Ruled out: switching to PCM during playback when decoding falls behind (the track in progress
    would be lost anyway, much more code).
  - **Sub-steps**: **1.8a** the new data path with a pass-through "PCM decoder", nothing visible changes (all
    current tests pass as they are); **1.8b** FLAC: dr_flac, host tests byte for byte against `flac`, Amiga build,
    benchmark in WinUAE (68020 cycle-exact for faithful timings, then the fast profile), start-up measurement and
    `HELO`, listening; **1.8c** MP3: `amisqueeze.fpu` with minimp3, tests, benchmark, listening. Then the real
    machine (1.9) and possibly release 0.2.
- **1.8a done** (2026-10-19, branch `decoding`): `decoder.h` (operations `start`, `run`, `input_for`,
  `buffered`; state in the output, never on the stack) and `decoder.c` (raw PCM, copied as is: one copy, ring to
  block, as before). Each track knows its decoder, the format it produces and where its decoded PCM starts in the
  output (`out_start`, known once the reading reaches the track and the decoder holds nothing more of the
  previous one); the output counts what it hands over (`handed`). Elapsed time, `STMs` at a boundary and `STMu`
  follow the decoded PCM; the ring positions only tell the decoder where each track's data begins; `OUTPUT`
  writes the decoded PCM. Tests: the existing ones pass unchanged (they only use raw PCM, where both kinds of
  position are equal, so they cannot see a mix-up); a test-only "widening" decoder (each byte becomes a 16-bit
  sample) checks the time after a block, a gapless boundary to a raw track, a pause inside a block and the
  virtual output with `OUTPUT` (359 checks); using a ring position on the output side on purpose makes three of
  them fail. WinUAE: command scenario, five formats, `stop-repro` (memory intact), CPU load on the slow profile
  unchanged (65 % at volume 100).
- **1.8b done** (2026-10-19, branch `decoding`): dr_flac 0.13.4 in `third_party/dr_flac` (commit `dfe8377`,
  public domain or MIT-0), built by `dr_flac.c` without our warnings and at `-O2` (no gain measured over `-Os`);
  the Amiga link drops unused sections (`-ffunction-sections`, `--gc-sections`): dr_flac's float output needs
  soft-float helpers libnix does not provide. `flac.c` reads the FLAC header as it arrives (STREAMINFO kept, every
  other block skipped: a picture may be bigger than the ring buffer), opens dr_flac on a minimal header, decodes
  only whole frames in the buffer (bound from STREAMINFO plus dr_flac's 4 KB cache) and at most one frame per
  request; 16-bit native output. The decoder interface gained `stop` (frees the state) and `finished` (dr_flac
  reads ahead: the next track only begins once it has handed over everything it took); the decoder owns its
  output format, known from the stream (`strm` 'f' guesses 44.1 kHz until then, the virtual output paces itself
  on it). Options `FLAC=AUTO|ON|OFF` and `BENCH`: a built-in sample (`bench_flac.c` from
  `tests/data/bench-l8.flac`, 0.3 s at level 8, 35 KB) decoded at start-up for at most 1 s; `flc` advertised in
  `HELO` if it runs at least twice real time. Tests: `tests/flac_data.py` (deterministic signal) and
  `tests/data/*.flac` (levels 0, 5, 8, mono, 24 bits, 48 kHz, 1.2 MB of metadata), sent by the fake server in
  pieces of 3000 bytes, `OUTPUT` identical byte for byte; FLAC, FLAC, PCM back to back; `FLAC=OFF`, `BENCH`.
  Measured in WinUAE (`BENCH`): 68020 without JIT, about 45 MHz (1.6 times the slow profile): level 8 at 0.2x real
  time, so FLAC stays with the server; 68020 with JIT (the fast profile: 32-bit addressing, no
  cycle-exact; about 230 times the slow profile, the order of a PiStorm): 77x (21x when the JIT has not compiled
  the code yet: the start-up measure is pessimistic on such CPUs, never optimistic). There, LMS sends FLAC as is
  and five test tones (16 and 24 bits, mono, 48 and 96 kHz: LMS lowers 96 kHz to 48 in FLAC) play without
  `STMo`; heard by the user: a 48 kHz song and the 96 kHz tone, right. Executable: 114 KB. Next: 1.8c (MP3).
  **Fixed on 2026-10-20** (found while taking the user guide's captures, never tested before): a seek in a FLAC
  decoded here skipped the track (`not a FLAC stream`). For a seek, and when it resumes a track after the player
  reconnects, LMS sends the file from a frame boundary (`findFrameBoundaries`) without its header (unlike MP3, no
  `getInitialAudioBlock` for FLAC). `flac.c` now makes up a STREAMINFO from that first frame's header (rate,
  channels, bits, block size; fails if the frame leaves its rate or bits to STREAMINFO) and opens dr_flac in strict
  mode as before. `drflac_open_relaxed()` was ruled out: it takes the first 8 bytes as a metadata block header, then
  looks for the next sync code (the first frame lost), and without CRC checks (`DR_FLAC_NO_CRC`) a sync inside the
  audio data could be taken for a frame. Tests: five files sent from a frame in the middle, the rest of the track
  byte for byte; in WinUAE, `time 60` and `time 150` play on, and so does a resume after a restart of amisqueeze.
- **1.8c done** (2026-10-19, branch `decoding`), in three parts:
  - **`amisqueeze.fpu`**: the same sources built with `-m68881` (`AMISQUEEZE_FPU`, objects in
    `obj/amisqueeze-fpu/`), both executables in the release archive; `amisqueeze` is unchanged. It checks
    `AttnFlags` and explains that it needs an FPU; libnix's own check, linked in by `-m68881`, showed an alert and
    quit before `main()` without a word: replaced by an empty `__fpucheck`.
  - **MP3 decoder** (`mp3.c`, only with `AMISQUEEZE_MP3`: `amisqueeze.fpu` and the host build): minimp3 (CC0,
    commit `ea99364`) in `third_party/minimp3`, built by `minimp3.c` without SIMD (the host tests run the Amiga's
    code) and layer III only. ID3v2 tags skipped as they arrive (several, with footers, bigger than the ring
    buffer); an ID3v1 tag at the end not taken for a frame. Frames go through a 16 KB staging buffer: minimp3 is
    called with a full buffer to find the first frame (it wants several frames in a row), then with the biggest
    frame and the next header (otherwise it starts over and loses the bit reservoir), or at the end of the track;
    a decoded frame is handed over as the output asks; rate and channels come from the frames (a format change
    starts a new block). minimp3 keeps about 17 KB on the stack (`-fstack-usage`): `amisqueeze.fpu` asks libnix for
    32 KB at start-up (`__stack`, `__stkinit`); `BENCH` from a `Stack 2048` Shell passes. Not done then: gapless
    trimming (encoder delay, LAME header frame decoded as silence: about 50 ms at the start of each file): done in
    1.8d (version 0.4).
  - **`MP3=AUTO|ON|OFF`**: like FLAC, a built-in sample (`bench_mp3.c` from `tests/data/bench-320.mp3`, 0.3 s at
    320 kbit/s, 13.6 KB) measured at start-up; `mp3` advertised last, as squeezelite does: LMS 9.1.1 still sends an
    MP3 as is (and keeps FLAC as FLAC). In `amisqueeze`, `MP3=ON` prints why it is ignored (both executables read
    the same prefs). `BENCH` prints both speeds.
  - **Tests**: MP3 files made with `lame` from the signals of `flac_data.py` (128 kbit/s CBR, VBR, 320 kbit/s
    without header nor tag, mono, 48 kHz, 22.05 kHz MPEG-2), decoded against their signal after the delay found by
    search: SNR 23.6 to 40.9 dB, the same values as `lame --decode`; the same bytes sent in pieces of 3000 bytes or
    at once; rate checked by the elapsed time; extra tags; a seek in the middle of a frame (the end identical to
    the whole file's); MP3, MP3, PCM back to back; a stream without MP3 frames.
  - **Measured in WinUAE**, 68020 + 68882 with the JIT: FLAC 78x, MP3 74x real time, but only with the JIT's FPU
    compiler on (`compfpu=true`: without it, 1.8x). Two MP3s through LMS (215 s) played to the end, the output
    identical to the host build's but for 0.03 % of the samples, by 1. Heard by the user: the user's song decoded
    on the Amiga, right on the Toccata; on Paula a crackle at LMS volume 50 that went away at 100: the samples
    scaled to 6 % leave Paula's noise (14 bits through AHI) 25 dB closer to the music; the user guide advises 100
    with Paula (see the volume modes of 1.10). Executables: `amisqueeze` 114 KB, `amisqueeze.fpu` 150 KB.
  - **For 1.9**: the synthesis uses `fintrz` and `fmovecr` (from `-m68881`), emulated by the CPU library on a real
    68040 or 68060 (one trap per sample: slow); fine on a 68881/68882, Emu68 to check. `MP3=AUTO` would measure it.

## 1.8d MP3 without gaps between tracks ✅ (2026-10-20, version 0.4)

- **Why**: an MP3 file begins with the encoder's delay and, from LAME, a header frame (Xing/Info) decoded as
  silence; it ends with padding. Played as they are (1.8c), two tracks meant to follow each other without a pause
  (a live album, a mix) have about 50 ms of silence between them. FLAC has no such gap.
- **Content** (to agree when the step starts): read the LAME header of the first frame (encoder delay and padding,
  in samples), drop the header frame and that many samples at the start, and the padding at the end, plus the
  decoder's own delay (minimp3's `minimp3_ex.h` does the same for whole files; here, on a stream). A stream that
  starts in the middle of a file (a seek: LMS sends the file from an offset, without the header) is played as it
  comes, nothing dropped. Files without a LAME header are left as they are.
- **Criterion**: on the host, the decoded test files line up with their signal from the first sample (delay 0
  instead of 1105 or 2257 frames) and keep its length; two MP3 tracks back to back without gap, byte for byte;
  heard by the user on a track that flows into the next one.
- **Done** (2026-10-20): `read_lame_header()` (since renamed `mp3_lame_header()`) in `mp3.c` reads the first frame
  found: "Xing" or "Info" after the side information (17 or 32 bytes for MPEG-1, 9 or 17 for MPEG-2, plus 2 with a
  CRC), the frame count, then the encoder's extension (delay and padding, 12 bits each, 21 bytes into it), the
  layout minimp3's `minimp3_ex.h` reads (read to check, not copied). That frame is not played; the delay plus the
  decoder's 529 frames are dropped at the start, and the output stops at frames × 1152 (576 for MPEG-2) minus delay
  and padding. A header without frame count or extension: the frame is skipped, nothing trimmed; no header: played
  as before. Seeking, checked in LMS's source (`Slim/Formats/MP3.pm`, `getInitialAudioBlock`): LMS sends the file's
  first frame again before the data from the new position, so the start trim drops about 25 ms after the seek point
  and the end limit is never reached (nothing wrongly cut). Tests: the five test files with a header come out with
  delay 0 and their signal's exact length (22.05 and 48 kHz, mono included), the one without a header unchanged
  (1105); a continuous signal cut off a frame boundary into two files (`cont-a`, `cont-b`) comes back whole, the
  same SNR around the cut as elsewhere (24.8 dB); a seek as LMS sends it plays the end of the track in full. Heard
  by the user in WinUAE (68020 + 68882, JIT, Toccata): a 12 s glide cut into three MP3s, with mini gaps from 0.3
  (12.9 s) and seamless now (12.3 s).

## 1.9 Real machine ⬜ (tried with version 0.7, release 0.8)

- **Content**: both kinds of target with a TCP/IP stack (AmiNetXDuo or Roadshow) and an LMS server reachable
  from the local network, then a Music Assistant server (decided 2026-10-21: one session for both servers): a PiStorm with Emu68, sound through its own AHI outputs (I2S DAC of the PiStorm32-lite
  FPGA firmware, announced, to check; HDMI with `emu68-hdmi-audio` on a Pi 4) or Paula; a 68020 accelerator
  without FPU, sound through Paula.
- **Criterion**: the criteria of 1.4 to 1.6 met on the real machine.
- **First machine** (2026-10-20): an A1200 with an ACA1221lc (68EC020 at 26.67 MHz, no FPU, 32-bit fast memory on
  the card), a PCMCIA Ethernet card, Paula. The slow WinUAE profile (68020 at about 28 MHz) is close to it, but
  WinUAE's network costs the Amiga nothing: the real TCP/IP stack and the PCMCIA card (the CPU copies every byte
  received) are the unknown. Without an FPU it runs `amisqueeze`, FLAC is left to the server: everything comes as
  raw PCM, about 176 KB/s at CD quality, the hardest case for that card.
- **Protocol** (agreed 2026-10-20), with an LMS server on the local network (ports 3483 and its web port), and
  a computer reaching its CLI to read the elapsed time:
  1. the machine: `Version FULL`, `Avail` (with its `Largest` column), `BENCH`; the CPU left when idle;
  2. connection (1.1): the player shows up, renaming it from LMS, a server restart;
  3. sound (1.4): the 440 Hz tone, left then right (with `SWAP` if needed: WinUAE swapped them), a whole track
     without crackles;
  4. load, the key point: LMS's fixed volume, a 44.1 kHz FLAC for 5 minutes with `VERBOSE` and `LOG`, the CPU
     left while playing (an idle-loop counter at the lowest priority, as in 1.4c), the `STMo` of the log (the
     buffer ran empty: a gap) and the gaps heard. If there are some, again with, in this order: Paula's 8-bit mode,
     a lower AHI frequency (22 kHz: duller treble, to judge by ear). Then the same while the Amiga works: files
     copied on the disk, Workbench windows moved: AHI holds only about 0.6 s of audio, so amisqueeze kept from the
     CPU longer than that by busier tasks would make a gap;
  5. commands (1.5): pause, resume, seek, next track, stop; the elapsed time read through the CLI after a pause;
     once at volume 50 without the fixed volume (load, crackle);
  6. robustness (1.6): the Ethernet cable pulled for 10 s and LMS restarted while playing; Ctrl-C at the
     connection, while playing, in pause; one hour of playback with `Avail` before and after;
  7. the Workbench: the icon, `QUIET` with `LOG` (no window), a wrong ToolType (the window opens), Exchange's
     Remove, `SYS:WBStartup`.

  Steps 1 and 4 (and the hour of step 6) are scripted in AmigaDOS: versions, memory before, while playing and
  after the stop, `BENCH`, the CPU left when idle and while playing, amisqueeze started from a 4 KB stack with its
  log, then the `STMo` lines and the clean stop; the logs on a disk, not in `RAM:`. The rest is by hand. If the
  load does not fit: in 0.8, a lower rate asked of the server (`MaxSampleRate` in `HELO`, if LMS resamples for
  it: to check), or the direct Paula output (1.11).

## 1.10 Finishing touches 🔶 (versions 0.4 to 0.9: the parts of 0.4 and 0.5 done)

- **Split** (decided 2026-10-20, see the [plan](README.md)): **0.4** the volume modes (`LMS` and `FIXED`, done
  as LMS's own setting, see below; `MIXER` left for later) and the AHI mode warning; **0.5** `LOG=<file>` and
  the start from the Workbench; **0.9** (moved from 0.6 on 2026-10-20, 0.6 being the review fixes, then from 0.7
  on 2026-10-21, 0.7 being Music Assistant and 0.8 the real machine) the automatic server discovery, after the
  real machine (1.9), since the test setup cannot check a UDP broadcast. The `BUFFER` option only if 1.9 shows a
  need. Each part is designed with the user when it starts.
- **Content**: automatic server discovery (UDP broadcast on port 3483); start from the Workbench with an icon and
  ToolTypes (read with the same template; precedence: built-in defaults, `ENV:amisqueeze.prefs`, ToolTypes,
  command line), with a console window or quiet mode; running in the background (`Run`, or at startup);
  `LOG=<file>` option: every message written both to the console and to the file (AmigaDOS has no `tee`; today a
  redirection `>file` sends the messages to the file only), appended and flushed after each message like the
  console, usable from the default options too.
- **`LOG=<file>` done** (2026-10-20, version 0.5, design agreed with the user): `LOG/K` in both platform layers;
  `msg()` and `msg_raw()` also write to the file. On the Amiga: `MODE_READWRITE` then `Seek` to the end (created if
  missing, a shared lock: `Type` reads it while amisqueeze runs), a start line with `DateStamp` and `DateToStr`
  (`--- amisqueeze 0.5-dev (20.10.2026), started 20-Oct-26 01:24:25`), the file closed at the end. A log that cannot
  be opened is said and amisqueeze goes on; a write error closes it, said in the window only. Fake server: two runs
  appended to one file, the console without the start line, a log in a missing directory. WinUAE: two runs into
  `RAM:amisqueeze.log`, read with `Type` while running, `RAM:nodir/...` (error 205) without stopping.
- **Workbench start done** (2026-10-20, version 0.5, design agreed with the user; the user chose a commodity, the
  standard way, over a double-click toggle first proposed here, and that a second start does nothing):
  - libnix's start-up code sets `_WBenchMsg` and would open the console named by `__stdiowin` before `main()`;
    `amiga.c` sets it to NULL and opens its own once the icon is read (`icon.library` `GetDiskObject`). The
    ToolTypes become a command line (`tooltypes_line()` in the core, host-tested: keywords of the template only, so
    `DONOTWAIT` and the like are left alone; disabled ones in parentheses skipped; a value with a space quoted, not
    a number, which a `/N` option refuses quoted) parsed with the same template; they win over `ENV:` prefs.
    `WINDOW` (default `CON:0/20/640/120/amisqueeze/AUTO/CLOSE/WAIT/SMART`) and `QUIET` (no window, no output:
    `msg()` writes only to the log; since 0.6, a message during start-up opens the window) are read with
    `FindToolType`. `SMART` is needed: without it the console window lost its text when uncovered.
  - Commodity (`commodities.library` v37): broker "amisqueeze", `NBU_UNIQUE | NBU_NOTIFY`, no Show/Hide; its port's
    signal is in the `WaitSelect` mask; `CXCMD_KILL` ("Remove") stops it as Ctrl-C does, `CXCMD_UNIQUE` (started
    again) is ignored; a second start gets `CBERR_DUP` and quits (with a message from a Shell, silently from the
    Workbench). Not for `BENCH`.
  - Icons: `tools/make-icon.py` draws a classic tool icon (DiskObject, a 2-bitplane image: a speaker with three
    waves; stack 16384; example ToolTypes disabled, and `DONOTWAIT`), built into `out/` with the executables and put
    in the archive.
  - Tests: 391 unit checks at the 0.5 release. WinUAE, driven from a remote-control Shell: double-click (window,
    connected), Exchange lists it with its description and stops it ("Remove"), Ctrl-C in the window, `QUIET` with
    `LOG` (no window, the log fills, player named by the icon), a second double-click while running (nothing
    changes), a bad ToolType (`PORT=abc`: the error in a window that stays), `WINDOW` and `UNIT=1` taken, a second
    start from a Shell refused.
- **Volume modes** (noted 2026-10-19, to decide when the step starts): an option such as `VOLUME=LMS|FIXED|MIXER`.
  `LMS` (default, as now): the gain sent by the server (`audg`) scales the samples, at a CPU cost on a slow 68020
  (1.6). `FIXED`: `audg` ignored, full output, no scaling; advertise `HasDigitalOut` in `HELO` so that LMS accepts
  its "fixed volume" setting and greys its slider instead of showing one that does nothing; the volume is set on
  the amplifier (the simple answer for slow machines). `MIXER`, an idea to study: the volume set on the AHI output
  itself (`AHIC_OutputVolume`), at no CPU cost; to check first: which drivers support it (`paula.audio`?
  Toccata, the PiStorm's DAC), and who drives it: from LMS, it would be `LMS` without the cost, but only if AHI
  does not fall into the fault seen with `ahir_Volume` (1.6); from the Amiga, amilms would have to talk to
  amisqueeze itself (an ARexx port, for instance), not only to LMS. Also heard in 1.8c: with Paula through AHI
  (14 bits), samples scaled down to a low volume bring out Paula's noise (a crackle at LMS volume 50, gone at 100);
  a volume set after the conversion (the output's own, or Paula's 0 to 64) would keep the full resolution.
  **Done for `FIXED`, without an option** (2026-10-20, design agreed with the user): LMS's code (`Player.pm`,
  `Settings/Player/Audio.pm`, `Squeezebox2.pm`, `Queries.pm`) shows its per-player "Volume Control" setting only to
  a player advertising `HasDigitalOut=1`; "Output level is fixed at 100%" (`digitalVolumeControl` 0) then sends
  `audg` with `adjust` 0, which amisqueeze already took as full volume (1.5), and reports `use_volume_control 0`,
  so its interfaces grey the slider out. So amisqueeze only advertises `HasDigitalOut=1` (as squeezelite does) and
  the user chooses in LMS, where the choice is kept per player; no `VOLUME=` option. `VERBOSE` prints the switch
  (`volume: fixed at 100 % by LMS`, also from 100 %). Side effect: LMS also shows "Audio Startup Time" (silence
  before MP3s, for digital receivers; default 0). Tests: 361 unit checks (fixed, back to the slider at 100, fixed
  again), `HasDigitalOut=1` in the fake server's `HELO` check. WinUAE with LMS 9.1.1, through its CLI: volume 40
  applied (3.1 %), fixed (`use_volume_control:0`, "fixed at 100 %"), volume 20 asked while fixed (nothing changes),
  adjustable again (0.6 %); the player's Audio page shows the setting. `MIXER` stays an idea for later.
- **AHI mode warning** (from 1.4, decided 2026-10-19): a warning at start-up when the unit's mode cannot pan (a
  plain "stereo" mode plays a stereo stream on one side only); first check what AHI tells about a unit's mode.
  **Done** (2026-10-20, design agreed with the user): `ahi.device` reads its units' modes from `ENV:Sys/ahi.prefs`
  (IFF FORM PREF, one `AHIU` chunk per unit: `struct AHIUnitPrefs`, mode ID and frequency; unit 255 is the
  "Music" unit). `ahi_prefs_unit()` in the core finds the unit's chunk (host-tested on the real file of the test
  Amiga, and on a file cut short); `plat_audio_mode()` in `amiga.c` reads the file and asks AHI about the mode
  (`AHI_GetAudioAttrsA`, AHI 4: name, `AHIDB_Stereo`, `AHIDB_Panning`); `audio_check_mode()` warns for a stereo
  mode that cannot pan or a mono mode, and amisqueeze plays anyway; `VERBOSE` prints the mode and frequency. No
  preferences for the unit: nothing said, as before. WinUAE: unit 3 (Toccata:16 bit stereo) warns, unit 0
  (Paula:14 bit stereo++ calibrated, 27429 Hz) does not. 371 unit checks.
- **`BUFFER` option** (from 1.2, decided 2026-10-19): only if the real machines (1.9) show a need for another
  size than 1 MB.
- **Criterion**: started without arguments, the player finds the server and shows up under the chosen name; with
  `LOG=<file>`, the messages appear in the Shell window and, at the same time, in the file.

## 1.11 Direct Paula output (idea, noted 2026-10-19) ⬜

- **Why**: on the slow profile, playing through AHI and `paula.audio` costs 61-68 % of the CPU, almost all of it in
  AHI (1.4c), and up to 88 % with the volume scaled in the core (1.6). A second audio sink that drives Paula through
  `audio.device` itself, without AHI, could leave much more room on a 68020 without FPU.
  Prior art: MintAMP (Aminet `mus/play`, 68030+) plays internet radio on Paula this way, without AHI; read its
  documentation only, its licence is not checked.
- **To study first**: the work AHI does that would come back to us: the rate (Paula's maximum is about 28 kHz in
  a PAL or NTSC screen mode, so 44.1 and 48 kHz must be lowered: a cheap integer step, or a simple filter), 8 bits per
  channel (or 14 bits with two channels per side and a calibration table: twice the DMA and more CPU), blocks in
  chip RAM, double buffering with `BeginIO` (never `DoIO`: it clears `io_Flags`, `ADIOF_PERVOL` is lost, volume 0,
  see 0.5). Volume: Paula's own (0 to 64 per channel) costs no CPU, which also fits the volume modes of 1.10. Sides:
  channels 0 and 3 left, 1 and 2 right (`WT:paula-lr`, correct in WinUAE, unlike Paula through AHI, see 0.5).
- **Shape**: an option such as `AUDIO=AHI|PAULA`, behind the same sink interface as `audio.c` (blocks, played
  bytes, pause), the Amiga calls in `amiga.c`; the conversion and the lower rate in the portable core, tested on the
  host bit for bit like the rest.
- **Criterion** (to agree when the step starts): on the slow profile, the same music and the same LMS criteria as
  1.4 to 1.6, sides in place without `SWAP`, at a CPU load clearly below AHI's (measured with `cpucount`, as in
  1.4c); then heard on the real 68020 machine (1.9).
