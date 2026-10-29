# Phase 1 — amisqueeze, slimproto player

Back to the [plan](README.md).

A network player for LMS (Music Assistant later, after version 0.2): slimproto control connection (TCP 3483), HTTP
audio stream, audio output through AHI. Protocol references: squeezelite (`slimproto.c`, `slimproto.h`, `stream.c`, `pcm.c`) and LMS
(`Slim/Networking/Slimproto.pm`, `Slim/Player/Squeezebox2.pm`).

Design choices: one task and one loop around `WaitSelect()` (sockets, AHI signals, Ctrl-C), because a `SocketBase`
belongs to the task that opened it; packets serialised field by field; buffers allocated (not on the stack).

## player-1 Control connection ✅ (2026-10-18)

- **Content**: TCP connection to the server, `HELO` (capabilities: `pcm`, 48 kHz max), `STAT` replies (`STMt`
  every 5 s, `STMf`…), player name (`setd`/`SETD`), automatic reconnection; streams refused (`STMn`) but their
  parameters printed.
- **Criterion**: the player shows up in LMS; it reconnects by itself after a server restart; renaming it from
  LMS reaches the Amiga.
- **Validated**: WinUAE (`bsdsocket_emu`), LMS 9.1.1; renaming from the LMS web UI is applied and confirmed. `strm s` received for an MP3: PCM 16 bits, 44.1 kHz,
  stereo, **little-endian**, stream at the control connection's address,
  `GET /stream.mp3?player=<mac> HTTP/1.0`.

## player-1b Default options ✅ (2026-10-18)

- **Goal**: run amisqueeze without retyping its options.
- **Content**: default options in the global variable `ENV:amisqueeze.prefs` (kept across reboots by a copy in
  `ENVARC:`), parsed with the same `ReadArgs` template as the command line; the command line wins; `SERVER` is no
  longer mandatory on the command line; clear error if no server is given anywhere or if the variable cannot be
  parsed. ToolTypes and Workbench start came later (player-10, version 0.5). Documented in the
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

## player-2 Receiving the stream, without playing it ✅ (2026-10-18)

- **Goal**: receive the stream and keep the server informed like a real player, without producing sound yet.
- **Content**:
  - on `strm s`: HTTP connection (address 0 = the control connection's), send the provided request, read the
    response headers and pass them to the server (`RESP`);
  - receive the data into a ring buffer (1 MB, about 6 s of 44.1 kHz 16-bit stereo PCM; size defined in one
    place, a `BUFFER` option may come with step player-6), consumed at real-time speed (PCM rate) by a virtual output
    that later steps replace with a file (player-3) and AHI (player-4); byte counters;
  - states: `STMc` (connected), `STMs` (playback started), `STMd` (end of data), `STMu` (buffer empty), buffer
    fullness and elapsed time in `STAT`;
  - `strm q`: stop; `strm f`: flush (0.7, 2026-10-21: drops only the track being streamed, the playing one goes
    on; until then a stop); autostart 2 and 3 (wait for `cont`, radio metadata) played as 0 and 1; `cont`
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
- The `BUFFER` option mentioned in Content did not come with player-6: an open point of the
  [plan](README.md#open-points).

## player-3 Output to a file ✅ (2026-10-18)

- **Goal**: check the received data byte by byte before listening to anything.
- **Content**: an `OUTPUT=<file>` option (command line and default options) that writes what the virtual output
  plays, exactly as received (no conversion yet: the byte swap for the 68k belongs to AHI, player-4): nothing while
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

## player-4 AHI audio output ✅ (2026-10-19)

- **Goal**: hear the sound, and hear it right.
- **Content**: `ahi.device`, double-buffered `CMD_WRITE`, `AHIST_S16S`; byte swapping when the stream is
  little-endian; honour the sample size, rate and channel count announced by `strm`. Only `ahi.device` features
  of AHI 4 (tested with AHI 6.6; 4.x expected to work, not checked). User guide: a **stereo++** mode (a plain
  "stereo" mode plays a stereo stream on one side only, seen in tools-5), at the stream's rate when the driver allows
  it (44.1 kHz for LMS) to avoid resampling; a warning at start-up if the unit's mode cannot pan (to check:
  reading the unit's mode through the device API).
- **Prerequisite**: tools-5 (AHI in the emulated Amiga) ✅.
- **Design** (agreed 2026-10-18):
  - an output ("sink") interface in the core: how many bytes it can take now, take these bytes, how many bytes
    were really played. The virtual output (clock) stays for the host build, the tests and `NOAUDIO`; AHI is the
    other sink. Elapsed time, `STMs` at a track boundary and `STMu` follow the bytes **played**, not the bytes
    handed over (AHI is about 0.6 s ahead);
  - AHI: three blocks of about 0.2 s (32 KB, size defined in one place), each carrying its rate and sample
    type, never spanning two tracks; the conversion (byte swap of little-endian streams, `SWAP`) done by the core
    while copying into the block (unit tested); the AHI reply port's signal added to the main wait; stop, flush
    and seek abort the blocks (sound cut at once); 16-bit PCM (other sizes refused for now, player-7);
  - options: `UNIT=n` (AHI unit, 0 by default), `NOAUDIO` (virtual output), `SWAP` (swaps left and right, off by
    default: for an AHI driver that swaps them, as `paula.audio` does in WinUAE, see tools-5); AHI missing: error
    message, return code 20.
- **Sub-steps**: player-4a the sink interface with the virtual output only, no change of behaviour (`make test` and the
  `OUTPUT` comparisons unchanged); player-4b the AHI sink, unit tests with a fake AHI (blocks, conversion, track
  boundary, played time, `STMs`/`STMu`, stop, pause) and a command scenario against LMS; player-4c listening
  (Toccata, then Paula; test tones from LMS, a whole track), LMS timing criteria of player-2, no leak, 2 KB stack, and
  CPU load with Paula on a slow 68020 profile of WinUAE (created then, see tools-4).
- **player-4a done** (2026-10-18): sink interface, played positions, track format kept from `strm s`; no change of
  behaviour (`make test`, `OUTPUT` comparisons against LMS on both builds).
- **player-4b done** (2026-10-18): `audio.c` (portable: blocks, conversion, played bytes, pause), `ahi.device` calls in
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
- **Validated (player-4b)**: `make test` 245 unit checks (the AHI 4 pause path included, by the fake device); WinUAE
  against LMS 9.1.1, Toccata (unit 1) and Paula (unit 0): play (`time ?` +3.00 s in 3 s), pause (time frozen),
  resume (+3.01 s in 3 s), stop, a 10 s track played to its end in 10.3 s, clean exit; `OUTPUT` on the Amiga
  build keeps the virtual output and its file stays identical to the counter pattern (twice, 7,056,000 bytes).
- **player-4c, test tones heard** (2026-10-19, WinUAE, LMS 9.1.1, `tone-440`, `tone-left`, `tone-right` in one
  playlist, 20.5 s for 20 s of audio, no `STMo`): Toccata (unit 1) right, sides in place; Paula 14-bit (unit 0)
  sides swapped without `SWAP`, in place with `SWAP`. Occasional crackle with Paula, also heard with the AHI test
  tone played without amisqueeze: it comes from the emulation or `paula.audio`, not from amisqueeze (to check on
  the real machine, player-9). A whole track (a 156 s, 48 kHz song, as MP3 then as FLAC) on Paula with `SWAP`: right
  speed and pitch, clean MP3 to FLAC boundary, 312.5 s for 312 s, no `STMo`. A seek (`time 141` from LMS) restarts
  the stream at the right offset (exactly 15.0 s of data left). Learnt on the server side: LMS announces in `strm`
  the rate of the **source file**, even when its conversion rule resamples; LMS's own `ogg pcm` rule (`sox -r
  44100`) therefore makes a 48 kHz source play 9 % too fast on any PCM-only player (our test `mp3 pcm` rule had
  copied it, fixed).
- **player-4c, the rest** (2026-10-19, measured without the user):
  - LMS timing criteria of player-2 with the AHI output (Paula, `SWAP`): time +60.01 s in 60.02 s, pause frozen,
    resume, seek, next track, end of the playlist, stop: all as with the virtual output;
  - stack: `Stack 2048` (checked with `Status FULL`: amisqueeze gets 2048), three rounds of the command scenario
    plus a seek and a track boundary: same results as with 4 KB;
  - memory: `Avail FLUSH` around each round, chip unchanged; fast drops by 1,064 bytes now and then, also when
    only `C:Wait` is started the same way (`Run` with output to a WinUAE folder), and not on every amisqueeze run:
    the system's, not amisqueeze's;
  - CPU load on the slow profile (68020 without FPU, WinUAE approximate mode at 4 x 7.09 = 28.4 MHz, as a
    typical A1200 accelerator; free CPU counted by a priority -128 loop, load = 1 - count while playing / count
    when idle, 10 s windows): through AHI and Paula, **66-68 % in 14 bits** (unit 0) and **61-65 % in 8 bits** (unit 2), for
    44.1 and 48 kHz streams, no `STMo`, LMS time exact; with `NOAUDIO`, 2-5 % (true until step player-8a, see player-9,
    2026-10-22). So nearly all of it is the sound
    output: AHI resampling and mixing for Paula (27,429 Hz), plus the byte swap into the AHI blocks (not measured
    apart; estimated at a few percent). Limits of the figure: WinUAE's networking runs TCP/IP on the host (a real
    stack, Roadshow, adds its own load) and its Zorro II fast RAM is not the accelerator's own RAM. About a third
    of the CPU is left on such a machine: to keep in mind for local FLAC decoding (player-8).
- **Validated with the user** (2026-10-19): Paula's crackles left to the real machine (player-9); the CPU load accepted
  for now (lighter AHI settings to look at in player-8 if decoding does not fit); `SWAP` stays a manual option until
  the real machine shows whether its channels are swapped too (player-9). Not done then: the start-up warning for a mode
  that cannot pan (Content above), done
  in player-10 (version 0.4).
- **Criterion**: correct test tones (440 Hz at the right pitch, left/right in the right place, no noise); a whole
  track without crackling; CPU load measured with Paula on the slow profile (0.4).
- **Test**: WinUAE with both AHI drivers of step tools-5, Paula first (Amiga chipset, the output of Amigas without a
  sound card) and Toccata (16-bit reference), then the real machines (player-9).

## player-5 Commands and volume ✅ (2026-10-19)

- **Content**: pause and resume (`strm p`/`u`, `STMp`/`STMr`), stop, flush, skip; volume (`audg`, 16.16 gain to
  AHI volume); exact elapsed time in `STAT` (based on what was actually played).
- **Criterion**: every command from the LMS UI acts on the Amiga; the displayed progress stays right after pause
  and resume.
- **Already done in player-4**: pause, resume, stop, flush, seek and next track, elapsed time from the bytes played.
- **Design** (agreed 2026-10-19; the AHI volume replaced by scaling the samples in player-6, see there): the 16.16 gain
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
  `adjust` 0 to amisqueeze then; since player-10 (version 0.4) amisqueeze advertises `HasDigitalOut=1` and handles
  it (fixed volume).

## player-6 Robustness ✅ (2026-10-19)

- **Content**: buffer underrun (slow network) then recovery; server dropping during playback; Ctrl-C at any time
  (AHI requests aborted, sockets closed); long playback.
- **Criterion**: one hour of playback without crash or memory leak (`Avail` before/after); Ctrl-C always returns
  cleanly.
- **Plan** (agreed 2026-10-19): player-6a scenarios of the fake server (`make test`); player-6b the same against the real LMS
  in WinUAE, through a local TCP relay between the emulated Amiga and LMS that can slow the stream down, hold it
  or reset it, and a server restart; player-6c Ctrl-C at every moment; player-6d one hour on the slow profile (Paula). When
  the server goes, playback stops at once (kept: the server decides what follows when it is back).
- **player-6a done** (2026-10-19): the fake server's HTTP side can hold the stream (`stall_at`, `stall`) or reset it
  (`cut_at`, RST). Scenarios: stream held 3 s after 1.5 s of audio (`STMo` once, playback resumes, 4 s played in
  5.4 s, `OUTPUT` file identical to the data sent); stream reset after 1.5 s (`DSCO 2`, what arrived is played to
  the end, byte for byte); control connection lost during playback (stream closed, reconnection, nothing playing
  afterwards). Unit test of a buffer running dry with the AHI output (`STMo` once, nothing lost). 
- **player-6b, against LMS 9.1.1** (2026-10-19, WinUAE, Paula):
  - stream held 10 s (more than the ~6 s buffer): `STMo` once, playback resumes, LMS time right before and after;
  - stream slowed to 68 % of the CD rate for 40 s: LMS handles it itself: on `STMo` it pauses the player (`strm
    p`), waits for the buffer to fill again, then resumes (`strm u`); so playback goes on in long stretches with
    pauses, not in tiny pieces; normal again as soon as the rate is;
  - stream reset: WinUAE's networking reports it as a normal end (`DSCO 0`); what arrived is played, then LMS
    goes on with the next track;
  - LMS restarted during playback: playback stops, the player reconnects, LMS keeps its playlist but stays
    stopped (its choice), a new playback works. **Found**: while LMS was down, amisqueeze connected 121 times in
    4 s: the relay in front of the test server (as any relay) accepts the connection and closes it at once, and
    amisqueeze reconnected without waiting. **Fixed**: a connection lost less than 10 s after it was made is no real session: wait 5 s
    before the next attempt, as after a refused connection ("retrying in 5 s"); after a real session, reconnect at
    once (fake server: 5.0 s, then 0.0 s).
- **player-6c** (2026-10-19, WinUAE): `Break` while waiting to reconnect, connected and idle, waiting for the stream's
  headers, playing, paused, with an empty buffer, after the end of playback, and twice in a row: always a clean
  stop (`stopped`), AHI opens again on the next run; memory: the 1,064-byte system steps of player-4c only.
- **player-6d** (2026-10-19, slow profile, Paula, two tracks of 44.1 and 48 kHz in a loop): one hour without any `STMo`,
  still playing, pause and resume still working at the end. LMS time lagged behind the clock by 16 s over the
  hour (-0.44 %). Measured minute by minute: the lag comes in jumps (1 s once, 6.5 s once), with the music
  following the clock in between and even catching up a little afterwards (+0.15 %); the protocol is clean (every
  track received whole, no stop nor flush); track boundaries, both ways and back to the first track, are exact to
  0.04 s; on the fast profile, -1.0 s in 20 min. So the slow profile's emulation (approximate CPU at real speed)
  falls behind now and then and catches up; amisqueeze counts the bytes actually played. To check on the real
  machine (player-9).
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
  volume (as in player-4, where pause and resume were tested many times); unit tests check the samples bit for bit,
  pause included. Sequence replayed twice: memory intact, clean stop. Cost on the slow profile: none at volume
  100; at 50, Paula 14-bit 65 % to 88 % (8-bit 60 % to 84 %); a first version with one 32-bit multiplication per
  sample (about 43 cycles on a 68020) reached 100 %, two 256-entry tables (sample = high byte * 256 + low byte)
  brought it down. User guide: on a slow Amiga, leave LMS at 100 (agreed 2026-10-19); later possibilities: a
  `FIXED` volume mode (player-10), a faster loop. Final build, one hour on the slow profile at volume 50 (88 % load):
  no `STMo`, 20 tracks, pause, resume and stop clean, largest free block unchanged. While WinUAE's window was
  minimised, the emulated machine fell behind real time (2.5 min in 10 min), the music following the Amiga's own
  clock exactly: time measurements belong to the real machine (player-9), WinUAE only checks crashes, underruns and
  memory. Test configuration: `win32.iconified_pause=false`, `win32.inactive_pause=false` (an emulation paused
  by a stray key had stopped an earlier run).

## player-7 Other PCM formats and Music Assistant 🔶 (player-7a done, player-7b first test, WAV header validated, `mp3` and `aac` tried 2026-10-21)

- **Content**: WAV and AIFF headers (format `?` + header parsing, or explicit values); 48 kHz; mono; 8 and 24 bits
  if the server sends them. Music Assistant: the player's `wav` output codec (MA offers no raw PCM); check what MA
  sends in `strm`.
- **Criterion**: test tones pass in each format; the player works with Music Assistant.
- **Split** (decided 2026-10-19): **player-7a** the formats, tested with LMS (now); **player-7b** Music Assistant, after
  version 0.2, with the test server of step tools-9.
- **player-7a, what LMS 9.1.1 sends** (observed 2026-10-19 with the host build, test tones in each format): always
  format `p` with explicit values, the samples only: **LMS strips the WAV and AIFF headers itself**, so no header
  parsing is needed with LMS (maybe with Music Assistant, player-7b). 16-bit FLAC/WAV: size `1`, little-endian; 24-bit
  FLAC/WAV: size `2` (3 bytes per sample), little-endian; 8-bit WAV: size `0`, **unsigned** (centred on 0x80);
  AIFF: endianness `0` (big-endian); mono: channels `1`. A 96 kHz FLAC is not played at all: amisqueeze
  advertises 48 kHz at most and LMS cannot lower the rate when it makes PCM ("Couldn't create command line for
  flc playback"); left as it is (decided 2026-10-19): with FLAC decoding (player-8), LMS can send a FLAC resampled to
  48 kHz (its `flc flc` rule resamples).
- **player-7a done** (2026-10-19): the audio output takes 8, 16, 24 and 32-bit samples and converts them to 16-bit
  big-endian when a block is sent (24 and 32 bits: the two most significant bytes; 8 bits: unsigned to signed,
  converted from the end of the block since they double in size, so an 8-bit block is filled to half); AHI always
  gets 16 bits (AHI 4). The bytes played are counted in bytes of the stream (each block knows its sample size), so
  the elapsed time, the track boundaries and pauses inside a converted block stay right. The check that refused
  non-16-bit streams (`STMn`) is gone. Unit tests: each conversion bit for bit against an independent one (8-bit
  stereo, 24-bit little-endian stereo, 24-bit big-endian mono, 32-bit, 16-bit big-endian), the time after the
  first block, a pause in a 24-bit block, a gapless 16-bit to 24-bit boundary (328 checks). WinUAE (Paula):
  seven test tones (WAV 16, FLAC 24, FLAC mono, WAV 24, WAV 8, AIFF, FLAC 16) in one playlist, 70 s in 70.4 s,
  no `STMo`; heard by the user: same pitch and level, boundaries not noticeable.
- **player-7b, first test** (2026-10-21, amisqueeze 0.6, Music Assistant 2.10.7 with its Squeezelite provider, test
  server of tools-9; WinUAE JIT + FPU, `amisqueeze.fpu`, Toccata; playback started from the web UI; read alongside its
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
    the "header parsing" foreseen in the content of player-7; the 24-bit conversion already exists (player-7a). In version
    0.7 (decided with the user on 2026-10-21), designed with the user first.
  - Not tried: output codecs `mp3` and `aac`, radios, groups of players, an MA server elsewhere than the test
    one.
- **player-7b, the WAV header** (design agreed with the user on 2026-10-21, version 0.7). Read alongside: aioslimproto
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
    10 s against 8204 idle; 61-68 % in player-4c, 16 bits at 44.1 kHz). Not tried: `mp3`, `aac` codecs (next item of
    0.7).
- **player-7b, the `mp3` and `aac` codecs** (2026-10-21, amisqueeze 0.7-dev, MA 2.10.7; WinUAE JIT + FPU,
  `amisqueeze.fpu`, Toccata). No code changed; the user guide gained a table of MA's output codecs.
  - **MA always transcodes** (read in `controllers/streams/controller.py`, `serve_queue_item_stream`): each track is
    decoded to PCM, then a last ffmpeg encodes it to the player's codec (`mp3` at 320 kbit/s), even an MP3 file.
  - **`mp3` works**: `strm s` with format `m` and no parameters, autostart 2 (3 for the next track), then `codc`
    and `cont` (ignored, as with FLAC). Heard by the user: playback, pause and resume, seek (a new stream from the
    position), the next track appended after `STMd`; no `STMo`. But **not gapless**: ffmpeg writing to a pipe
    leaves out the `Info`/LAME header (checked in the container: the same pipeline gives 165137 bytes for
    `gapless-1.mp3`, the size amisqueeze received, without the header; to a file it writes one), so the encoder
    delay and padding are played. Heard on the 12 s glide of player-8d: the boundaries can be heard, with small
    crackles at times (the second lossy encoding of a pure tone); the same three tracks with the `flac` codec: no
    crackle, seamless. Nothing amisqueeze can do reliably without the header: the user guide recommends `flac`.
  - **`aac`**: `strm s` with format `a`; amisqueeze answers `STMf`, `stream format a not supported`, `STMn`, then
    nothing more (no noise, no hang, no retry from MA). MA knew (its log: "Player did not report support for
    content_type audio/aac, playback might fail") and sends it anyway; its Squeezelite provider ignores the decoder
    error event of `STMn`, so its web UI shows the track playing, the time running then back to 0, until the user
    stops it. LMS, by contrast, moves on to the next track on `STMn` (`playerStreamingFailed`), but only after
    `STMc`: it drops any status between a `strm s` and `STMc`, and amisqueeze sent `STMn` before it, so LMS
    ignored it too (found later the same day in LMS's sources; fixed in 0.7: `STMc`, then `STMn`, agreed with the
    user on 2026-10-27). Not changed
    (decided with the user on 2026-10-21, when LMS was thought to move on already): sending `STMu` after `STMn`
    would make MA move on, but LMS could then skip twice; an idea for later, per server at best. `amisqueeze` (no
    FPU) gets MA's `mp3` the same way (format `m` refused).
  - **Seen once, MA's side**: two requests for the same track were answered with 44 bytes (an ID3 tag, no audio);
    amisqueeze skipped the track (`no MP3 frame found`, `STMu`); the third try played. Nothing in MA's log.
- **player-7b, the user guide part** (2026-10-21): in "Server setup", what Music Assistant needs for amisqueeze: the
  Squeezelite provider, ports 3483 and 8097, the "Published IP address" when MA's own address cannot be reached
  from the Amiga, the volume of 20 given to a new player; setting labels read in MA 2.10.7 (`translations/en.json`,
  frontend). Left out (decided with the user): installing the servers, LMS and MA on the same computer (a
  development setup).

## player-8 Local decoding: FLAC, then MP3 ✅

- **Goal**: stop depending on server-side conversion (a regular LMS has no MP3 → PCM rule).
- **Content**: FLAC then MP3 decoders in portable C (lightweight, MIT-compatible libraries, e.g. dr_flac and
  minimp3), tested on the host first; advertised in `HELO` only once validated. (Written before the design
  further down; where they differ, the design and what was done prevail: no `mpega.library` in `amisqueeze`, which
  leaves MP3 to the server; no `CPU` variable in the Makefile, but a second set of flags for `amisqueeze.fpu`; the
  64-bit `muls.l` check below was not written, see the open points of the [plan](README.md#open-points).)
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
  - **Sub-steps**: **player-8a** the new data path with a pass-through "PCM decoder", nothing visible changes (all
    current tests pass as they are); **player-8b** FLAC: dr_flac, host tests byte for byte against `flac`, Amiga build,
    benchmark in WinUAE (68020 cycle-exact for faithful timings, then the fast profile), start-up measurement and
    `HELO`, listening; **player-8c** MP3: `amisqueeze.fpu` with minimp3, tests, benchmark, listening. Then the real
    machine (player-9) and possibly release 0.2.
- **player-8a done** (2026-10-19, branch `decoding`): `decoder.h` (operations `start`, `run`, `input_for`,
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
- **player-8b done** (2026-10-19, branch `decoding`): dr_flac 0.13.4 in `third_party/dr_flac` (commit `dfe8377`,
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
  `STMo`; heard by the user: a 48 kHz song and the 96 kHz tone, right. Executable: 114 KB. Next: player-8c (MP3).
  **Fixed on 2026-10-20** (found while taking the user guide's captures, never tested before): a seek in a FLAC
  decoded here skipped the track (`not a FLAC stream`). For a seek, and when it resumes a track after the player
  reconnects, LMS sends the file from a frame boundary (`findFrameBoundaries`) without its header (unlike MP3, no
  `getInitialAudioBlock` for FLAC). `flac.c` now makes up a STREAMINFO from that first frame's header (rate,
  channels, bits, block size; fails if the frame leaves its rate or bits to STREAMINFO) and opens dr_flac in strict
  mode as before. `drflac_open_relaxed()` was ruled out: it takes the first 8 bytes as a metadata block header, then
  looks for the next sync code (the first frame lost), and without CRC checks (`DR_FLAC_NO_CRC`) a sync inside the
  audio data could be taken for a frame. Tests: five files sent from a frame in the middle, the rest of the track
  byte for byte; in WinUAE, `time 60` and `time 150` play on, and so does a resume after a restart of amisqueeze.
- **player-8c done** (2026-10-19, branch `decoding`), in three parts:
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
    player-8d (version 0.4).
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
    with Paula (see the volume modes of player-10). Executables: `amisqueeze` 114 KB, `amisqueeze.fpu` 150 KB.
  - **For player-9**: the synthesis uses `fintrz` and `fmovecr` (from `-m68881`), emulated by the CPU library on a real
    68040 or 68060 (one trap per sample: slow); fine on a 68881/68882, Emu68 to check. `MP3=AUTO` would measure it.

## player-8d MP3 without gaps between tracks ✅ (2026-10-20, version 0.4)

- **Why**: an MP3 file begins with the encoder's delay and, from LAME, a header frame (Xing/Info) decoded as
  silence; it ends with padding. Played as they are (player-8c), two tracks meant to follow each other without a pause
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

## player-9 Real machine 🔶 (2026-10-27, release 0.8: Music Assistant next)

- **Content**: both kinds of target with a TCP/IP stack (AmiNetXDuo or Roadshow) and an LMS server reachable
  from the local network, then a Music Assistant server (decided 2026-10-21: one session for both servers): a PiStorm with Emu68, sound through its own AHI outputs (I2S DAC of the PiStorm32-lite
  FPGA firmware, announced, to check; HDMI with `emu68-hdmi-audio` on a Pi 4) or Paula; a 68020 accelerator
  without FPU, sound through Paula.
- **Criterion**: the criteria of player-4 to player-6 met on the real machine.
- **First machine** (2026-10-20): an A1200 with an ACA1221lc (68EC020 at 26.67 MHz, no FPU, 32-bit fast memory on
  the card), a PCMCIA Ethernet card, Paula. The slow WinUAE profile (68020 at about 28 MHz) is close to it, but
  WinUAE's network costs the Amiga nothing: the real TCP/IP stack and the PCMCIA card (the CPU copies every byte
  received) are the unknown. Without an FPU it runs `amisqueeze`, FLAC is left to the server: everything comes as
  raw PCM, about 176 KB/s at CD quality, the hardest case for that card. A first estimate in WinUAE (tools-11,
  2026-10-24, AmiNetXDuo, the stack planned for this machine, on an emulated NE2000 card; this machine's card is a
  Xircom, `xircomce2.device`): on the slow profile, raw PCM at CD quality costs about 45 % of the CPU with
  `NOAUDIO` instead of 4.5 % with WinUAE's emulated network, and 92 % through Paula instead of 53 %. An order of
  magnitude only (another driver and card, PCMCIA accesses not cycle-exact, 4 MB of Zorro II memory, some of the
  playback in chip memory): what to watch here, with the memory the stack takes (about 1.3 MB of fast memory in
  WinUAE).
- **First playback on that machine** (2026-10-24, version 0.7, before the protocol below): Kickstart 3.1, Miami
  3.2b then AmiNetXDuo 1.0.0-rc2, `xircomce2.device`, AHI 4.18 with `paula.audio`, LMS on the local network.
  amisqueeze plays.
  - **Left and right swapped** in `Paula:14 bit stereo++` at 27,429 Hz: the left tone comes out on the right, the
    right tone on the left; in place with `SWAP`. Amiga Test Kit's sound test, which drives Paula directly, puts
    each side in place with the same cables. So `paula.audio` swaps on a real Amiga too, as tools-5 found in
    WinUAE. What became of it: player-11 (its 14 bits).
  - `Paula:14 bit stereo` without "++" plays one channel of the stream only, as tools-5 found. With `SWAP`, the
    left tone is silent and the right tone comes out on the right only; without `SWAP`, only the left tone was
    heard.
  - **CPU load**, method of player-4c (`cpucount`, three 10 s windows), `song-44k.flac` sent as raw PCM, LMS
    volume 100, `SWAP`. Idle, the machine counts about 13,600: 1.6 times the slow WinUAE profile (8,288), so the
    WinUAE estimates were pessimistic. No `STMo` in any run.

    | Stack | Idle | `NOAUDIO` | Paula Fast 14 bit stereo++ | Paula 14 bit stereo++ |
    |---|---|---|---|---|
    | Miami 3.2b | 13,613 | 2,145: 84 % | about 100 %, gaps heard | about 100 %, crackles and gaps heard |
    | AmiNetXDuo | 13,574 | 7,246: 47 % | 4,155: 69 % | 3,729: 73 % |

    The stack makes the difference: AmiNetXDuo costs about half as much as Miami for the same reception.
  - **Occasional crackles** remain with AmiNetXDuo. They do not change with amisqueeze at priority 5, with AHI's
    echo and surround off, or with the mode (Fast or not). A 20 s raw PCM excerpt played through `AUDIO:`, without
    amisqueeze, is clean on its own. It crackles the same while amisqueeze receives a stream with `NOAUDIO`. So the
    network reception disturbs Paula's output, not amisqueeze: probably the card's driver delaying the interrupt
    with which Paula asks for its next block, AHI's Paula buffer being short. The `xircomce2.device` that
    AmiNetXDuo ships is the same file (1.1, 2024-12-22).
  - **Strategy against the crackles** (agreed with the user on 2026-10-24), in this order:
    1. confirm the mechanism, without code: the same `AUDIO:` excerpt while AmiNetXDuo's `iperf` receives at
       several rates; do the crackles follow the rate, and from which rate are they heard?
    2. the driver: read its code; does AmiNetXDuo's own `anxnet.device` take this card (`CheckNetDevice`)?
    3. less to receive: a lower rate asked of the server (planned for 0.8, see step 4 of the protocol);
    4. AHI 6 with `paula.audio` 4.30 instead of 4.18;
    5. the direct Paula output (player-11): large buffers in chip RAM, read by Paula alone, would not mind a late
       interrupt, and AHI's mixing would go too.
    Then the choice between 3 and 5, by what 1 shows.
  - **Step 1 done** (2026-10-24): the `AUDIO:` excerpt while the PC sends a steady stream to `nc` on the Amiga
    stays clean at 44 and 88 KB/s, crackles once at 176 KB/s and all the time at 330 KB/s. So the bursts count,
    not the mean rate: LMS sends at full speed whenever amisqueeze frees room. A remedy without code: AmiNetXDuo's
    `RXBUFFER`, set in the interface file, caps the TCP window, so the bursts. With amisqueeze, Paula 14 bit
    stereo++:

    | `RXBUFFER` | Load | Crackles |
    |---|---|---|
    | none | 73 % | occasional |
    | 8192 | 90 % | none, heard as perfect |
    | 12288 | 81 % | some |
    | 16384 | 76 % | all the time |

    8192 leaves about 10 % of the CPU. Why a small window costs 17 points is open: more acknowledgments, or
    amisqueeze receiving in smaller pieces, which amisqueeze itself could fix. Steps 3 and 5 would win the margin
    back; at 88 KB/s (step 3) nothing crackled even without `RXBUFFER`.
  - **Step 2, the driver read** (2026-10-24, `xircomce2.device` 1.1, GPL, sources on Aminet; the card is a Xircom
    CE3-10/100). The receive work runs in a software interrupt, `RXInt`, at priority 0. It loops until the card
    holds no frame, so a large burst keeps it busy long, and a software interrupt of the same priority waits. That
    fits the measures: `paula.audio` fills its next block in a software interrupt at priority 0 (checked on
    2026-10-25 in 4.23, 4.30 and 4.33). Each
    frame is read 2 bytes at a time, each read preceded by a write of the position: a workaround in the driver.
    A faster path reads CE3 cards through their shared memory, but only when no system memory covers the PCMCIA
    range $600000-$9FFFFF. On this machine the accelerator's memory does, so the slow path is used. Asking the
    accelerator's tool to keep that range free did not move its memory: the system then declared the range a
    second time, as free memory. Never leave a machine like that. Leads in the driver: handle a few frames per
    pass in `RXInt` and cause it again, so that AHI gets its turn and a larger window becomes possible; then the
    copy itself. `anxnet.device` remains to be tried.
  - **The same driver rebuilt** (2026-10-24): `xircomce2.device` 1.1 cross-compiled with GCC 16, sources
    unchanged, kept apart from this project (GPL). It costs about 12 points less at `RXBUFFER=8192` and plays
    clean at 10240. Same protocol as the table above (Paula 14 bit stereo++, `SWAP`, LMS volume 100, three
    10 s windows), both drivers swapped without a reboot; idle count 13,508 to 13,539 before and after:

    | Driver | `RXBUFFER` | Load | Crackles |
    |---|---|---|---|
    | Aminet's | 8192 | 92 % | none |
    | Aminet's | 12288 | 81 % | quite a few |
    | rebuilt | 8192 | 80 % | none |
    | rebuilt | 10240 | 79 % | none heard |
    | rebuilt | 12288 | 79 % | three very small ones |

    No `STMo` in any run. A larger window brings the rebuilt driver almost nothing (80 to 79 %), unlike Aminet's
    (92 to 81 %).
  - **The driver fixed** (2026-10-25): `RXInt` handles at most 4 frames per pass, then causes itself again (2
    gave the same results). Same protocol, one session: clean without `RXBUFFER` at 77 %, and at 16384 at 79 %,
    where the rebuilt 1.1 driver crackles a lot. File transfers keep their speed. `RXBUFFER` is no longer needed
    on this machine.
  - **Roadshow** (2026-10-25): its 1.15 demo, run without installing it, with the fixed driver, same protocol and
    same day. 86 % while playing, against 77 % with AmiNetXDuo; transfers about a fifth slower in, a quarter
    slower out. No `STMo`. The demo ships only the 68000 build of its stack; the commercial build, tuned for 68020
    and better, was not measured. AmiNetXDuo stays the stack for this machine.
  - **Optimization roadmap for this machine** (agreed with the user on 2026-10-24): the most modest machine first,
    so that the others benefit. Today: clean playback at about 77 % of the CPU, with the fixed driver and no
    `RXBUFFER` (above). In this order:
    1. the cheap measures: a lighter system (a trimmed install, MapROM), AHI 6 (step 4 of the strategy); a few
       points each, estimated. Why a small TCP window costs 17 points no longer matters: the window need not be
       capped any more;
    2. less to receive: a lower rate asked of the server (player-13). Done on 2026-10-26: 62 % at
       32 kHz, 50 % at 22.05 kHz (below);
    3. the driver: a few frames per pass in `RXInt`. Done on 2026-10-25 (above): 77 %, against 73 % estimated;
    4. the direct Paula output (player-11), if the margin is still short after 1 to 3: AHI's mixing gone (a good
       share of the 25 to 30 points playback costs, estimated), and large buffers that do not mind a late
       interrupt.

    A 68030 card (twice the CPU, and the PCMCIA range free for the driver's faster path) is the user's own choice,
    outside this plan.
  - **Later: a stock A1200** (68EC020 at 14 MHz) with the user's trapdoor fast memory card instead of the
    accelerator (noted with the user on 2026-10-26, when is the user's decision). Its jumper gives 8 MB, which
    should cover the PCMCIA range (the driver's slower path, if the card is still seen), or 4 MB, which leaves it
    free (its shared-memory path, the driver's issue #2). In each position: the memory list and `card.resource`,
    the idle count, 2 MB transfers both ways, then amisqueeze with LMS and its rules, the AHI unit at 22050 Hz and
    `MAXRATE=AHI`, lower if needed (16000, 11025, Paula's 8-bit mode): load, `STMo`, crackles. Then how responsive
    the Amiga stays while it plays (added with the user on 2026-10-27: it may play, yet be too slow to use): the
    time of a known Shell command (`List` of a drawer), a Workbench window opened, a window moved; with amisqueeze
    at its usual priority, then a higher one. Estimated from the measures above, not measured: about twice the
    load at 14 MHz, less the gain of the shared-memory path, so 60 to 85 % at 22.05 kHz; the direct Paula output
    (player-11) would matter most there.
- **Protocol** (agreed 2026-10-20), with an LMS server on the local network (ports 3483 and its web port), and
  a computer reaching its CLI to read the elapsed time:
  1. the machine: `Version FULL`, `Avail` (with its `Largest` column), `BENCH`; the CPU left when idle;
  2. connection (player-1): the player shows up, renaming it from LMS, a server restart;
  3. sound (player-4): the 440 Hz tone, left then right (with `SWAP` if needed: WinUAE swapped them), a whole track
     without crackles; with Paula, the power LED dims while playing (`paula.audio` turns the LED filter off, bit 1
     of CIA-A `$BFE001` at 1, and back on afterwards: seen in WinUAE on 2026-10-23);
  4. load, the key point: LMS's fixed volume, a 44.1 kHz FLAC for 5 minutes with `VERBOSE` and `LOG`, the CPU
     left while playing (an idle-loop counter at the lowest priority, as in player-4c), the `STMo` of the log (the
     buffer ran empty: a gap) and the gaps heard. If there are some, again with, in this order: Paula's "Fast"
     14-bit stereo++ mode (lighter mixing, added with the user on 2026-10-24), Paula's 8-bit mode, a lower AHI
     frequency (22 kHz: duller treble, to judge by ear). Then the same while the Amiga works: files
     copied on the disk, Workbench windows moved: AHI holds only about 0.6 s of audio, so amisqueeze kept from the
     CPU longer than that by busier tasks would make a gap;
  5. commands (player-5): pause, resume, seek, next track, stop; the elapsed time read through the CLI after a pause;
     once at volume 50 without the fixed volume (load, crackle);
  6. robustness (player-6): the Ethernet cable pulled for 10 s and LMS restarted while playing; Ctrl-C at the
     connection, while playing, in pause; one hour of playback with `Avail` before and after;
  7. the Workbench: the icon, `QUIET` with `LOG` (no window), a wrong ToolType (the window opens), Exchange's
     Remove, `SYS:WBStartup`;
  8. server discovery (player-10, added with the user on 2026-10-27): without `SERVER`, LMS found, then Music
     Assistant, then no server (the message, Ctrl-C while it waits); with each TCP/IP stack at hand, since their
     support of broadcasts varies, each followed by about 30 s of playback through that stack (agreed with the
     user on 2026-10-27; a lower `MAXRATE` where the stack leaves too little CPU); and a start from the icon as
     shipped (its `SERVER` disabled), without `ENV:amisqueeze.prefs` giving one: the server found, its messages in
     the window.

  Steps 1 and 4 (and the hour of step 6) are scripted in AmigaDOS: versions, memory before, while playing and
  after the stop, `BENCH`, the CPU left when idle and while playing, amisqueeze started from a 4 KB stack with its
  log, then the `STMo` lines and the clean stop; the logs on a disk, not in `RAM:`. The rest is by hand. If the
  load does not fit: in 0.8, a lower rate asked of the server (`MaxSampleRate` in `HELO`, if LMS resamples for
  it: to check), or the direct Paula output (player-11).
- **The protocol on the A1200, with LMS** (2026-10-27, 0.8-dev at `860eda6`): AmiNetXDuo with the fixed
  `xircomce2.device`, AHI 4.18 with `paula.audio` 4.23, Paula 14 bit stereo++ at 27,429 Hz with `SWAP`. Every
  step is done but `SYS:WBStartup` (step 7). Two defects found there are fixed: the missing `STMo` (player-14) and
  the gaps while the Amiga works (player-15, to check here).

  | Step | Result |
  |---|---|
  | 1 machine | Kickstart 40.68, Workbench 47.2: AmigaOS 3.1, whose `version.library` comes from 3.2 (`workbench.library` 40.5). Free with the stack up: chip 1,974,000, fast 7,781,872 (largest 4,672,784). `BENCH`: FLAC 8 at 0.2x, so `FLAC=AUTO` leaves FLAC to the server. Idle count 13,521 with the corrected `cpucount`, 13,528 with the old one. Out-of-range `PORT` and `UNIT`, and an `ENV:amisqueeze.prefs` of 510 characters, refused with their messages |
  | 2 connection | the player shows up, renamed from LMS; after a server restart, it reconnects by itself |
  | 3 sound | the left and right tones in place with `SWAP`. The LED filter stays off as long as amisqueeze runs, playing or not, and comes back on when it ends |
  | 4 load | raw PCM at 44.1 kHz for 5 minutes, volume 100: 77 % of the CPU (counts 3,087 to 3,119), no `STMo`, clean. Free while playing: fast 6,091,032 (largest 2,810,472). While the Amiga works: gaps at priority 0 without `STMo`, clean at 1 (player-15) |
  | 5 commands | pause (time frozen), resume, seek to 100 s (102.26 s three seconds later), next track, stop: right, no `STMo`. At volume 50: about 100 % of the CPU, crackles and gaps, as the user guide says |
  | 6 robustness | the cable pulled: player-14. Then the test setup's relay dropped both connections, and amisqueeze never said "no news from the server" (open). LMS restarted: reconnected by itself. Ctrl-C connected, playing, in pause and while retrying: "stopped" alone, in under 2 s. In the half-open state, AmiNetXDuo first shows "recv: error 4" and a retry (see Open points, Ctrl-C) |
  | 6 one hour | 24 tracks, no `STMo`; `Avail` the same to the byte before and after. LMS's time: +60.00 s a minute; -0.48 s over 59 minutes (-0.014 %, within the rounding of the tracks' lengths), against -0.44 % in WinUAE |
  | 7 Workbench | the icon as shipped; `QUIET` with `LOG` (no window, all in the log); `UNIT=9` (the window says why); Exchange's Remove (sound stopped, "stopped"). The icon as shipped takes unit 0, Paula Fast 8 bit mono here: amisqueeze warns it plays in mono |
  | 8 discovery | LMS, Music Assistant (its own `PORT`) and no server, with each stack below. Ctrl-C while looking: "stopped" alone. AmiNetXDuo also from the icon as shipped |

  About 30 s of playback through each stack, the same day:

  | Stack | Rate | Load | Sound |
  |---|---|---|---|
  | AmiNetXDuo 1.0.0-rc2 | 44.1 kHz | 77 % | clean |
  | Miami 3.2b | 22.05 kHz | 71 % (AmiNetXDuo: 50 %) | many crackles; probably Aminet's driver, without the fix (not checked) |
  | Roadshow 1.15 demo | 44.1 kHz | about 95 % (one early window; 86 % on 2026-10-25) | clean |

  Left, split with the user on 2026-10-28 while the A1200 is away:
  - in WinUAE, not checked again on the A1200, since the result does not depend on the machine. Done on
    2026-10-28 (below);
  - on the A1200: `PRI=1` against the gaps (player-15); the cable pulled, Ctrl-C with AmiNetXDuo and the missing
    "no news from the server" (player-16). The load with Music Assistant only if it sent another format than
    LMS's raw PCM: it does not (below).
- **The rest in WinUAE** (2026-10-28, `main` at `1b2d040`, Music Assistant 2.10.7 without the local fix, volume
  normalization off, codec `wav`):

  | Check | Result |
  |---|---|
  | Music Assistant | a 16-bit 44.1 kHz WAV, the rate of LMS's raw PCM. Play, pause (time frozen), resume, seek to 100 s (103.0 s three seconds later), next track, stop: right, no `STMo`. Server restarted: reconnected by itself. Ctrl-C playing, in pause, idle: "stopped" alone. A rename stays on Music Assistant's side: it sends nothing to the player |
  | `aac` | "stream format a not supported", then `STMc` and `STMn`; Music Assistant shows the track playing at 0 s, as the user guide says |
  | Load, Music Assistant against LMS | slow profile with AmiNetXDuo, the same session. Through Paula: about 99 % with both, saturated. With `NOAUDIO`: 42.6 % with LMS, 44.0 % with Music Assistant. So about 78 to 79 % expected on the A1200 instead of 77 % |
  | Memory | same profile, fast memory taken while playing through AHI: 1.72 MB at the first start, AHI loaded with it (1.69 MB on the A1200); 1.52 MB at the second; 1.27 MB with `NOAUDIO`. AHI keeps about 0.2 MB once loaded; nothing else stays. The user guide said 1.1 MB: now about 1.7 MB |
  | `SYS:WBStartup` | slow profile, AmiNetXDuo started from `S:User-Startup`, an icon with `SERVER` and `DONOTWAIT`: connected at the first try, plays, "stopped" on Ctrl-C |
- **Load of the lighter AHI settings, measured in WinUAE** (2026-10-22, 0.7-dev at `60a14fa`), to know which one
  to try first in step 4: slow profile (68020, about 28 MHz, no FPU, as the ACA1221lc), LMS 9.1.1, a 156 s song
  resampled to a 44.1 kHz 16-bit FLAC, sent as raw PCM by the server (`FLAC=AUTO` leaves FLAC to it on this
  profile: `stream: format p, size 1, rate 3`), `amisqueeze` with `SWAP` and `VERBOSE`, LMS volume 100. Method of
  player-4c: free CPU counted by `cpucount` (in the repository since 2026-10-22, `make cpucount`, never released;
  agreed with the user on 2026-10-27) at priority -128, load = 1 - count while playing / count when idle, three
  10 s windows per setting, the first 15 s after `play`; idle measured before and after the series (8,258 to
  8,334, mean 8,288). Mode and frequency as read from `ENV:Sys/ahi.prefs` and printed by `VERBOSE`
  (`output: AHI unit ...`); the 22 kHz rows coming out lower confirm AHI used them. No `STMo` in any run.

  | Setting (Paula, stereo++) | Counts (10 s) | Load per window | Load |
  |---|---|---|---|
  | 14 bits, 27,429 Hz (reference) | 2867, 2870, 2891 | 65.4, 65.4, 65.1 % | 65 % |
  | 14 bits, 22,050 Hz | 3402, 3398, 3400 | 59.0, 59.0, 59.0 % | 59 % |
  | 8 bits, 27,429 Hz | 3275, 3278, 3271 | 60.5, 60.5, 60.5 % | 60.5 % |
  | 8 bits, 22,050 Hz | 3732, 3719, 3732 | 55.0, 55.1, 55.0 % | 55 % |
  | `NOAUDIO` (two runs) | 5118, 4860, 4797; 4906, 5361, 4662 | 35.3 to 43.8 % | 40 % |
  | `NOAUDIO`, with the fix below (idle 8,309 that session) | 7937, 7925, 7917 | 4.5, 4.6, 4.7 % | 4.6 % |

  Each lighter setting saves about 5 to 6 points of CPU, both together about 10: none changes the picture (more
  than half of the CPU still goes to playback). The windows repeat within 0.1 point, but the slow profile is
  WinUAE's approximate mode, not cycle-exact (chip RAM writes, which grow with the bit depth and the rate, may
  weigh differently on the real A1200): which of the two saves more is for the real machine to tell. The reference
  matches player-4c (66-68 %); `NOAUDIO` does not (2-5 % in player-4c, 40 % now, its windows spread over several points).
  Found the same day by bisection (`NOAUDIO`, two windows per build): 3.7 % at `b4a1459` (player-7a), 35 % at
  `148ba47` (player-8a, the decoder stage: the virtual output now copies the PCM out of the ring instead of dropping it
  in place), 31 % for the current code with that copy left out (so the copy is about 10 points, the rest not
  located yet). The AHI path is not affected: `8ecb2cf` (player-4c) against `60a14fa` in the same session, unit 0,
  three windows each: 66.6 % and 65.3 %. Throwaway counters in the main loop (turns, stream reads, bytes in, per
  10 s) showed why: with `NOAUDIO`, about 47 turns and 38 reads of 4.7 KB a second at `b4a1459`, 470 to 875 turns
  and as many reads of about 200 bytes from `148ba47` on; through AHI, 12 turns and 5.4 reads of 32 KB (a whole
  block) before and after. The stream was read as soon as one byte of the ring was free, and the virtual output
  frees a few hundred bytes at each turn, so the loop kept reading what it had just freed; each turn does more
  since player-8a (the copy, the decoder calls), and every turn reads the clock (`plat_now_ms()`, `ReadEClock` and a
  64-bit division, already so before player-8a): a counter build with one more such call per turn went up to 95 %, a
  hint that it is costly, not a measure of it. Why player-8a tipped the loop into this rhythm is not understood; the
  cause common to both is removed instead. **Fixed** (2026-10-22, decided with the user for 0.7, so that `NOAUDIO` can
  isolate the network's cost on the real machine): during the body, the stream is read only once 8 KB of the ring
  are free (at most a quarter of it; `READ_MIN_SPACE` in `stream.c`, unit test `test_read_min_space`). Measured in
  the same way: `NOAUDIO` 4.6 % (three windows), 18 turns and 9 reads of about 20 KB a second; AHI unit 0
  65.3 %, its counters unchanged. Not counted, as in player-4c: the real network (TCP/IP stack such as Roadshow, the
  PCMCIA card whose bytes the CPU copies), which WinUAE's emulated `bsdsocket.library` runs on the host; and
  WinUAE's fast RAM is not the accelerator's own.
- **Elapsed time after a seek, in WinUAE** (2026-10-22, while checking the fix above): on the slow profile, LMS's
  `time ?` is wrong for a while after a seek or a new start: 156 s (the track's length) 3 s after a seek to 146 s,
  about 20 s instead of 5 s just after a new start; the same with `8ecb2cf` (player-4c), with AHI and `NOAUDIO`, with
  an MP3 and a FLAC. Not amisqueeze's: its elapsed time is right (a throwaway build printing every `STAT`: 0 at
  the new `STMs`, 1,009 ms a second later, the next track's `STMs` 10.15 s after the seek), and the host build gets
  the right times from the same LMS. The emulated Amiga's clock falls behind real time (about 28 s
  lost during a two-minute run, where exactly not known), and LMS adds to the last elapsed time
  the delay since that `STAT`, measured with the player's clock, following a clock that falls behind only after
  about 50 messages (`trackJiffiesEpoch` in `Slim/Player/Player.pm`). Intervals stay right (+30.03 s in 30.02 s).
  So the elapsed time after a command is judged on the real machine (step 5 of the protocol above); the player-4c
  statement "seek ... all as with the virtual output" was true of both outputs alike.

## player-10 Finishing touches 🔶 (versions 0.4 to 0.9: the parts of 0.4 and 0.5 done)

- **Split** (decided 2026-10-20, see the [plan](README.md)): **0.4** the volume modes (`LMS` and `FIXED`, done
  as LMS's own setting, see below; `MIXER` left for later) and the AHI mode warning; **0.5** `LOG=<file>` and
  the start from the Workbench; **0.8** the automatic server discovery (agreed with the user on 2026-10-27: its
  last check needs the real machine, in the same session as player-9); **0.9** the internet radios' metadata.
  The `BUFFER` option only if player-9 shows a need. Each part is designed with the user when it starts.
- **Automatic server discovery** (designed with the user on 2026-10-27, version 0.8). Done on 2026-10-27, checked
  on the host and in WinUAE (LMS and Music Assistant found); the A1200 next, step 8 of the player-9 protocol:
  - Only when no `SERVER` is given: not on the command line, in `ENV:amisqueeze.prefs` or in the ToolTypes. No
    new option.
  - A discovery request is broadcast on UDP: `e`, then the field `NAME` with no value, to 255.255.255.255 on
    `PORT` (3483 by default). The same `PORT` then serves the TCP connection. Both servers answer this request on
    their slimproto port: LMS (`Slim/Networking/Discovery.pm`) and Music Assistant (aioslimproto's
    `discovery.py`).
  - Every reply that starts with `E` within 2 s counts. The server's address is the reply's source address,
    since neither server sends its slimproto port. `NAME` is read within the packet's bounds and shown in ASCII:
    Music Assistant counts its length in characters, then encodes the packet in UTF-8. Each server found is
    shown with its name; amisqueeze connects to the first. To choose another, give `SERVER`.
  - No reply: a message once, then a new request every 5 s, Ctrl-C at any time.
  - A lost connection is retried at the same address first. If that connection fails, the discovery starts
    again, the server's address may have changed. Never with `SERVER`.
  - **Criterion**: on the host, the reply's parser in unit tests (malformed and truncated replies, Music
    Assistant's lengths), and fake server scenarios (one server, two, none, a malformed reply); in WinUAE, a
    server found through a UDP relay on the PC, if the broadcast gets there; on the A1200, step 8 of the
    player-9 protocol. A test server the broadcast does not reach directly (in a container, for instance) needs a
    UDP relay.
- **Internet radios' metadata** (ICY; designed with the user on 2026-10-27, version 0.9). Coded in a branch,
  rebased on `dev` once 0.8 is out (agreed with the user on 2026-10-29). The rebase conflicts with player-16 in
  `stream.c`: `PLAT_AGAIN` (`read_again`) is to be handled in both of the branch's reads, the sound's and the
  block's, with a test. Read in LMS 9.1.1 and aioslimproto 3.2.3 (Music Assistant 2.10.7):
  - LMS sends `cont` for a direct stream only, once it has read the `RESP`. It holds the blocks' interval
    (32 bits), a `loop` byte, a count of GUIDs (16 bits), then the GUIDs. On a 3xx, LMS sends a new `strm s`
    itself. Outside 200 to 399, the stream fails (`Squeezebox2.pm`, `directHeaders`).
  - Music Assistant sends autostart 2 or 3 for every track. After each `RESP` it sends `codc`, then `cont` with an
    interval of 0, even after a status over 300 (`client.py`, `_process_resp`).
  - LMS learns a direct stream's titles only from the player: `META` messages, from which it reads `StreamTitle`
    (`Slimproto.pm`, `Protocols/HTTP.pm`, `parseMetadata`).

  The design:
  - **Autostart 2 and 3 wait for `cont`.** After the response headers, `RESP` is sent and the stream is no longer
    read. Body bytes that came with the headers wait in the headers' buffer. At `cont`, the body is read:
    autostart 3 starts at the threshold, 2 sends `STMl` and waits for `strm u`. No time limit.
  - **The interval comes from `cont` only**: LMS asked for `Icy-MetaData` and read `icy-metaint` itself. An
    interval of 0 means no blocks. The `loop` byte is ignored, and so is a `cont` with no stream waiting, both
    said with `VERBOSE`.
  - **The status line**: its code is the three digits after the first space, whatever the protocol (`ICY 200 OK`
    is a 200). Outside 2xx, the body never enters the buffer and a message is always shown.
    - Autostart 0 or 1: the stream closes with `DSCO` 2.
    - Autostart 2 or 3: `RESP` only, and the server decides. A `DSCO` would count as a second failure, or against
      the next track. A `cont` that comes anyway (Music Assistant) closes the stream with `DSCO` 2, nothing
      played.
    - amisqueeze follows no redirect itself: LMS does.
  - **The titles**: each non-empty block goes back to the server in `META`, as received. LMS then shows the title
    in its interfaces and gives it to remotes (amilms). On the console, with `VERBOSE` only, characters outside
    ASCII as `?`.
  - **The CPU**: each `recv` stops before the next block, so the sound goes into the ring without a copy. The
    length byte and the title (4080 bytes at most) go to a buffer in `struct stream`, not on the stack. The
    threshold and `STAT` count the sound's bytes only.
  - **The code**: the blocks' state machine and the status line's parser in a new small module, unit-tested. The
    rest of `stream.c` stays where it is, so that the rebase after 0.8 does not conflict.
  - **Also in the branch**, one commit each: the 88.2 kHz rate code (`:`); a line in the user guide's Known limits
    for what `strm s` asks and amisqueeze ignores (polarity, one side only, transitions, replay gain).
  - **Not done**: LMS's `body` frame for playlist addresses (see the plan's
    [Open points](README.md#open-points)), HTTPS, `codc`.
  - **Criterion**:
    - on the host, unit tests at every cut: a block across two reads, the length byte last in a read, an empty
      block, the longest one, intervals of 1 and 0;
    - fake server scenarios: a direct PCM stream with blocks, written byte for byte to `OUTPUT`, its titles in
      `META`; nothing played before a late `cont`; `ICY 200 OK`; a 404 direct then `strm q`, a 404 then `cont`,
      a 404 with autostart 1; `cont` with an interval of 0;
    - in WinUAE (`amisqueeze.fpu`, JIT and FPU), a real `http://` radio played directly with LMS: no cut, its
      title in LMS's CLI;
    - with Music Assistant, ordinary playback unchanged (FLAC, MP3, gapless, `strm f` then `strm q`), since each
      of its tracks now waits for `cont`.
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
  (player-6). `FIXED`: `audg` ignored, full output, no scaling; advertise `HasDigitalOut` in `HELO` so that LMS accepts
  its "fixed volume" setting and greys its slider instead of showing one that does nothing; the volume is set on
  the amplifier (the simple answer for slow machines). `MIXER`, an idea to study: the volume set on the AHI output
  itself (`AHIC_OutputVolume`), at no CPU cost; to check first: which drivers support it (`paula.audio`?
  Toccata, the PiStorm's DAC), and who drives it: from LMS, it would be `LMS` without the cost, but only if AHI
  does not fall into the fault seen with `ahir_Volume` (player-6); from the Amiga, amilms would have to talk to
  amisqueeze itself (an ARexx port, for instance), not only to LMS. Also heard in player-8c: with Paula through AHI
  (14 bits), samples scaled down to a low volume bring out Paula's noise (a crackle at LMS volume 50, gone at 100);
  a volume set after the conversion (the output's own, or Paula's 0 to 64) would keep the full resolution.
  **Done for `FIXED`, without an option** (2026-10-20, design agreed with the user): LMS's code (`Player.pm`,
  `Settings/Player/Audio.pm`, `Squeezebox2.pm`, `Queries.pm`) shows its per-player "Volume Control" setting only to
  a player advertising `HasDigitalOut=1`; "Output level is fixed at 100%" (`digitalVolumeControl` 0) then sends
  `audg` with `adjust` 0, which amisqueeze already took as full volume (player-5), and reports `use_volume_control 0`,
  so its interfaces grey the slider out. So amisqueeze only advertises `HasDigitalOut=1` (as squeezelite does) and
  the user chooses in LMS, where the choice is kept per player; no `VOLUME=` option. `VERBOSE` prints the switch
  (`volume: fixed at 100 % by LMS`, also from 100 %). Side effect: LMS also shows "Audio Startup Time" (silence
  before MP3s, for digital receivers; default 0). Tests: 361 unit checks (fixed, back to the slider at 100, fixed
  again), `HasDigitalOut=1` in the fake server's `HELO` check. WinUAE with LMS 9.1.1, through its CLI: volume 40
  applied (3.1 %), fixed (`use_volume_control:0`, "fixed at 100 %"), volume 20 asked while fixed (nothing changes),
  adjustable again (0.6 %); the player's Audio page shows the setting. `MIXER` stays an idea for later.
- **AHI mode warning** (from player-4, decided 2026-10-19): a warning at start-up when the unit's mode cannot pan (a
  plain "stereo" mode plays a stereo stream on one side only); first check what AHI tells about a unit's mode.
  **Done** (2026-10-20, design agreed with the user): `ahi.device` reads its units' modes from `ENV:Sys/ahi.prefs`
  (IFF FORM PREF, one `AHIU` chunk per unit: `struct AHIUnitPrefs`, mode ID and frequency; unit 255 is the
  "Music" unit). `ahi_prefs_unit()` in the core finds the unit's chunk (host-tested on the real file of the test
  Amiga, and on a file cut short); `plat_audio_mode()` in `amiga.c` reads the file and asks AHI about the mode
  (`AHI_GetAudioAttrsA`, AHI 4: name, `AHIDB_Stereo`, `AHIDB_Panning`); `audio_check_mode()` warns for a stereo
  mode that cannot pan or a mono mode, and amisqueeze plays anyway; `VERBOSE` prints the mode and frequency. No
  preferences for the unit: nothing said, as before. WinUAE: unit 3 (Toccata:16 bit stereo) warns, unit 0
  (Paula:14 bit stereo++ calibrated, 27429 Hz) does not. 371 unit checks.
- **`BUFFER` option** (from player-2, decided 2026-10-19): only if the real machines (player-9) show a need for another
  size than 1 MB.
- **Criterion**: started without arguments, the player finds the server and shows up under the chosen name; with
  `LOG=<file>`, the messages appear in the Shell window and, at the same time, in the file.

## player-11 Direct Paula output ⬜ (study agreed with the user on 2026-10-29)

- **Why**: on the slow profile, playing through AHI and `paula.audio` costs 61-68 % of the CPU, almost all of it in
  AHI (player-4c). On the A1200, a volume below 100 brings it to about 100 %, with crackles (player-9). The
  network reception through a PCMCIA card also makes AHI's Paula output crackle: large buffers read by Paula alone
  would not mind it. Prior art: MintAMP (Aminet `mus/play`, 68030+) plays internet radio on Paula this way,
  without AHI; read its documentation only, its licence is not checked.
- **The design** (agreed with the user on 2026-10-29):
  - **Through `audio.device`**, not the chip registers. Large blocks in chip RAM, read by Paula alone; Paula holds
    the next block's address while one plays, so a late interrupt makes no gap. Requests sent with `BeginIO`,
    never `DoIO`: it clears `io_Flags`, so `ADIOF_PERVOL` is lost and the volume is 0 (tools-5).
  - **14 bits**: two channels per side, the 8 high bits at volume 64 and the next 6 at volume 1. Left: channels 0
    (high) and 3 (low); right: 1 and 2 (`WT:paula-lr`, tools-5). All four channels are taken. A calibration
    table, as in `paula.audio`'s "calibrated" modes: later, if at all.
  - **The volume is Paula's, never the samples'.** At 100, or fixed by LMS: 64 and 1, so 14 bits. Below 100: the
    high channel at the server's volume (1 to 64; 0 only for 0), the low one at 0. That is 8 bits lowered by
    Paula: no CPU, and Paula's noise goes down with the music. Changed at once with `ADCMD_PERVOL`.
  - **8 bits on request** (`AUDIO=PAULA8`): the high channels only, at Paula's volume even at 100. One channel per
    side stays free, for the system's beep or another program. With `AUTO`, the four channels are nearly always
    free at start-up, so every other sound is silent while amisqueeze runs: `PAULA8` is for an Amiga used for
    work. The user guide's Sound section gets a table, one row per output: `AUTO` or `PAULA` (14 bits, the least
    CPU, other sounds silent), `PAULA8` (8 bits, one channel per side free), `AHI` (14 bits, more CPU, AHI
    programs play along since AHI mixes them).
  - **A channel taken by another program**: the channels are allocated at a precedence the system's beep cannot
    take them from. If one is taken all the same: a message and a clean stop. Playing 8 bits while the low
    channels are taken, then taking them back: later, if at all.
  - **The rate**: above Paula's maximum (about 28.6 kHz with a 15 kHz screen mode, a period of 124: to check), it
    is divided by the smallest integer that brings it under. So 48 kHz plays at 24, 44.1 at 22.05, 32 at 16, 88.2
    and 96 kHz at a quarter. Lower rates play as they come (27,429, 24, 22.05 kHz). The grouped samples are
    averaged, a cheap filter judged by ear with the sweep (player-13); a better one only if the folding is heard.
  - **`MAXRATE` defaults to Paula's maximum** with this output: the server's resampling sounds better and halves
    what the Amiga receives. The division serves a server without the conversion rule, and Music Assistant.
  - **One pass in the portable core**: the 16 bits split into 8 + 6, left and right apart (`SWAP` swaps the
    destinations), the rate divided. Written straight into chip RAM buffers handed out by the platform layer,
    without a copy; tested on the host bit for bit.
  - **Positions**: the bytes played are still counted in bytes of the stream, the elapsed time at the decoded
    rate (`architecture.md`, Two kinds of position). The clock's estimate uses the rate of the rounded period. A
    host test in the spirit of the "widening" decoder.
  - **Paula's LED filter** off while playing, as `paula.audio` does, and its state restored at every exit, Ctrl-C
    included.
  - **An option `AUDIO=AUTO|AHI|PAULA|PAULA8`**, `AUTO` by default, also as a ToolType and in
    `ENV:amisqueeze.prefs`; accepted and ignored by the host build. `UNIT` is unused with Paula.
  - **`AUTO`**, decided at start-up, before `HELO` (its `MAXRATE` depends on the output):
    - AHI opens and the unit's driver is not Paula (`AHIDB_Driver`): AHI;
    - AHI opens and the unit plays through Paula, or AHI is missing: the direct Paula output;
    - there, the four channels (14 bits), else one per side (8 bits), else a message and a stop.

    An AHI mode "Paula 8 bit" still gives 14 bits: in this output, 8 bits saves little CPU.
  - **`SWAP` keeps its meaning everywhere.** Until this output, the user guide has Paula users add it (agreed with
    the user on 2026-10-27); with the direct Paula output, it would swap sides that are right. So `SWAP` with this
    output gives a message saying it is no longer needed, and the CHANGELOG says so.
- **Sub-steps** (agreed with the user on 2026-10-29), one commit each. `stream.c` keeps the `audio_*` interface;
  `audio.c`'s blocks stay in fast RAM, and the conversion writes from them into the four channels' chip buffers.
  - **player-11a, on the host only**: the conversion, the divisor, the server's volume turned into 0 to 64, the
    bytes played, `AUTO`'s rule; a fake Paula device in `tests/stub_platform.c`.
  - **player-11b**: `plat_paula_*` in `platform.h`, `audio.device` in `amiga.c`, the `AUDIO` option, the AHI
    unit's driver, the fall back from 14 to 8 bits; the checks below first, then WinUAE.
  - **player-11c**: the A1200.
- **To check before coding**, in the autodocs or in WinUAE (player-11b):
  - `CMD_STOP` and `CMD_START` stop and restart a write at once: the pause would then send nothing again;
  - the four channels started in step;
  - the longest `ioa_Length`;
  - how a channel taken by another program is reported (`ADIOERR_CHANNELSTOLEN`), and at which precedence the
    Sound preferences play the beep;
  - whether AHI, set to a Paula 8-bit mode, plays on the two channels `PAULA8` leaves free (to know, for the
    user guide; not part of the criterion);
  - the period from the machine's own clock (3,546,895 Hz PAL, 3,579,545 Hz NTSC; `REALLY_PAL` in
    `GfxBase->DisplayFlags`, to check), never from the screen mode.
- **Criterion** (agreed with the user on 2026-10-29):
  - in WinUAE, slow profile: player-4 to player-6's LMS criteria (test tones, pause, seek, volume, tracks without
    gaps), and the load measured with `cpucount`, as in player-4c; then WinUAE set to NTSC: the same pitch as in
    PAL, and the same elapsed time in LMS; with `PAULA8`, the system's beep heard while the music plays on
    unbroken; `AUTO` keeps AHI with Toccata and goes direct with Paula; `SWAP` with Paula direct gives its message;
  - on the A1200 with LMS, at 22.05 kHz sent by the server: a load clearly below AHI's 50 % (30-40 %
    estimated), the same at volume 50 with a clean sound;
  - on the A1200, 44.1 kHz divided by the core: a clean playback, its load measured;
  - in both cases: no `STMo`, no crackle while the network receives, the sides in place without `SWAP`, and a
    sound at least as good as AHI's 14 bits on the same song, by ear. Also by ear: no click from volume 100 to 99,
    a quiet passage clean at volume 30, mute fading smoothly.

## player-12 Hardening ✅ (2026-10-27, version 0.8)

- **Done**: the 50 list of the [0.7 review](reviews/0.7.md#5-the-50-list), but the items of 0.9 and those of the
  real machine (the `aac` row, the free memory: player-9), each item agreed with the user on 2026-10-27, one
  commit each. Safety nets: every exit code in `fake_server.py`, fp2-fp7 in `check-saves.py`, the texts and
  sources ASCII, `check-stack.py`, a test for each check of the server's input, `strm f` and a refused stream,
  the build without MP3, the scripts' syntax, `cpucount` in the CI. Code: `PORT` and `UNIT` ranges, the `setd`
  name shown in ASCII, the stream's readiness after a new socket, `cpucount`'s clock, `gethostbyname()` without
  an address, an `ENV:amisqueeze.prefs` too long. The plan's statuses and decisions brought up to date.
- **Kept as they are** (agreed with the user on 2026-10-27): the track queue that forgets the oldest track when
  full (neither LMS 9.1 nor Music Assistant sends a third track ahead); `release.yml` without the tests (the
  release is a draft published by hand, and `tests.yml` runs on the tag); the advice on `NODE_EXTRA_CA_CERTS` in
  `CONTRIBUTING.md` (generic, no machine named).
- **Left**: the `stop_decoder()` of `strm f` once the decoding has reached the next track is caught by a state
  check only; the sound would show it only between two tracks of the same decoder (FLAC, MP3) on the audio
  output, which the host tests do not have.

## player-13 A lower rate asked of the server ✅ (2026-10-26, version 0.8)

- **On the A1200** (2026-10-26, 0.8-dev, AmiNetXDuo with the fixed `xircomce2.device`, Paula 14 bit stereo++
  at 27,429 Hz, `SWAP`, volume 100, `song-44k.flac`; method of player-9). LMS with the guide's rules for its MAC;
  Music Assistant 2.10.7 with the local fix below, output codec `wav`:

  | Server | Option | Stream | Load |
  |---|---|---|---|
  | LMS | none | WAV, 16 bits, 44.1 kHz, 176 KB/s | 77 % |
  | LMS | `MAXRATE=32000` (then `AHI`) | WAV, 16 bits, 32 kHz, 128 KB/s | 62 % |
  | LMS | `MAXRATE=22050` | WAV, 16 bits, 22.05 kHz, 88 KB/s | 50 % |
  | Music Assistant | `MAXRATE=32000` (then `AHI`) | WAV, 24 bits, 32 kHz, 192 KB/s | 77 % |
  | Music Assistant | `MAXRATE=22050` | WAV, 24 bits, 22.05 kHz, 132 KB/s | 60 % |

  No `STMo`, no crackle heard. The load follows the bytes received, whatever the server. The user heard no
  difference between 22.05 kHz and above with that song; to confirm with others. `MAXRATE=AHI` then rounded the
  unit's rate up to a standard rate: 32000 here.
- **A sweep on the A1200** (`music/sweep.flac`, linear, about 1 kHz a second, LMS; when it fades, read on the
  server's elapsed time, to a second or two): at 22.05 kHz it fades at 10-11 kHz, as the rate allows. Asked for
  32 kHz, past Paula's 13.7 kHz it falls then rises again before fading at about 15 kHz: AHI's conversion folds
  what Paula cannot play back into the treble. Asked for the unit's own 27429 Hz, it fades without that. Asked
  for 24000 Hz, LMS sends a 44.1 kHz file at 22050 Hz (it keeps a file's family of rates), and AHI's conversion
  up to 27429 Hz adds artefacts. So the stream is cleanest at the unit's own rate.
- **Music Assistant sent 24 bits** for a 16-bit file: its volume normalization, on by default, works in 32-bit
  floats (so do crossfade and DSP), and the output takes the deepest the player accepts, 24 bits
  (`get_output_format`). Half of `MAXRATE`'s gain is lost, since amisqueeze keeps 16 bits for AHI. With
  normalization disabled, a 16-bit file comes as 16 bits: on the A1200 with `MAXRATE=AHI`, WAV 16 bits at
  32 kHz, 62 % as with LMS (2026-10-26); the user guide says so. A 24-bit file stays 24 bits, and
  slimproto has no way to ask for 16. A lead, the user's decision (noted on 2026-10-26): Music Assistant honouring
  the player's own "sample rates" setting for Squeezelite players, as it does for Sonos and Raumfeld.

- **Why**: amisqueeze always declares `MaxSampleRate=48000` in `HELO`, so the server keeps the source's rate. On
  the real A1200, the network reception is the largest cost and makes Paula crackle in bursts (player-9). Load
  measured in WinUAE (slow profile, real network stack, 16-bit WAV from a locally patched Music Assistant, Paula
  14-bit at 27429 Hz): 99 % at 44.1 kHz, 86.5 % at 32 kHz, 74 % at 22.05 kHz. A 24-bit source also arrives as
  24-bit PCM, 50 % more traffic than 16 bits. With Paula, nothing above about 13.7 kHz comes out: 32 kHz loses
  nothing audible.
- **What the servers do** (checked on 2026-10-26, LMS 9.1.1 and Music Assistant 2.10.7):
  - LMS keeps a `MaxSampleRate` below the source's rate only through a conversion rule with the `D` (resample)
    capability. None of its rules to `pcm` has it, so it refuses the track of a player that takes PCM only, as
    the A1200 does. Its rules to `flc` have it: a FLAC-decoding player gets a resampled FLAC;
  - for PCM, LMS's `strm` gives the source file's rate and sample size, whatever the rule sends. A rule that
    sends a WAV works, since amisqueeze plays the format of the WAV header;
  - no capability asks for 16 bits: the rule does it;
  - Music Assistant offers 44.1 kHz and up only: below, its list of rates is empty and the stream fails with a
    server error (tried with 2.10.7, `MAXRATE=32000`).
- **Content** (agreed with the user on 2026-10-26):
  - a `MAXRATE` option, `AHI`, `AHISTD` or a rate in Hz, also as a ToolType and in the default options. `AHI`
    takes the AHI unit's rate as it is, `AHISTD` the standard slimproto rate at or below it, for a server that
    makes only those, as LMS re-encoding MP3 (27429 Hz gives 24000; both agreed with the user on 2026-10-26,
    after the sweep, instead of rounding up). Without it, 48000 as before. The user guide advises the unit at
    22050 Hz for any server, or Paula at 27429 Hz with `MAXRATE=AHI` and LMS;
  - the user guide: the option; for LMS, a rule per source format in `custom-convert.conf`, for the player's MAC,
    with `D` and a WAV output in 16 bits (`flc` and `mp3`, the others on the same model); without the rule LMS
    refuses the tracks, and Music Assistant fails to play;
  - tests: `AHI` and `AHISTD`, `HELO` with and without the option, a WAV stream whose header differs from `strm`.
- **Criterion**: `make test` green; on the test LMS with the rule, the host build with `MAXRATE=22050` then
  `32000`: the WAV header at that rate in 16 bits, the CLI's elapsed time right at 30 s and after a seek; without
  the option, `HELO` unchanged. Then `MAXRATE=AHI` on the A1200, load and listening (player-9).
- **After it**: Music Assistant, an issue and a fix upstream for rates below 44.1 kHz. LMS upstream would need `D`
  on its rules to `pcm` and `strm` giving the resampled rate (agreed with the user on 2026-10-26).
- **The Music Assistant fix, tried locally** (2026-10-26, 2.10.7): every slimproto rate up to the player's
  `MaxSampleRate` in its Squeezelite provider, an empty list of rates no longer fatal, and aioslimproto's rate table
  fixed (11025, 22050, 32000). Then amisqueeze plays every rate from 8000 to 32000 Hz, as `wav` and `flac`. Without
  the table fix, a WAV at 11025, 22050 or 32000 Hz stops aioslimproto before `codc` and `cont`: amisqueeze plays
  anyway, a real squeezelite waits forever. Publishing it upstream is the user's decision. Once it is in Music
  Assistant, the user guide can advise the AHI unit at 24000 Hz with `MAXRATE=AHI` for it: it sends the highest
  offered rate below the source's, 24000 Hz for a 44.1 kHz file, where LMS keeps the file's family (22050 Hz).
  Not measured on the A1200 (agreed with the user on 2026-10-26).

## player-14 The buffer running dry in the middle of a frame ✅ (2026-10-27, version 0.8)

- **Found on the A1200** (2026-10-27, player-9 step 6, LMS, PCM): the network cable pulled for 10 s while
  playing. Each time the sound stopped about 6 s later, when the buffer ran dry. Sometimes amisqueeze sent `STMo`,
  LMS paused the player (`strm p`), then resumed it once the data came back (`strm u`), as in player-6b.
  Sometimes no `STMo` came: the sound came back without that pause, or never when the stream's connection was lost
  meanwhile (the test setup's relay on the development machine dropped it). amisqueeze kept running, sending its
  `STMt` with an elapsed time that no longer moved.
- **Cause**: the output sent `STMo` only when the ring was exactly empty. A stream that stops in the middle of a
  frame (a sample of 4 bytes, or a FLAC or MP3 frame of several KB) leaves the start of that frame in the ring.
  The decoder cannot use it, so the output has nothing to play, yet the ring is not empty. With `amisqueeze.fpu`
  decoding FLAC or MP3, a stall almost always falls in the middle of a frame.
- **Fixed** (agreed with the user on 2026-10-27): when the decoder produces nothing and takes nothing, the output
  is empty, whatever the ring still holds (`stream.c`, both outputs). A partial frame at the end of a track was
  already dropped: `STMu` was not affected.
- **Tests**: the fake server's stall scenario stops the PCM stream after a whole frame, then 2 bytes into one; a
  new scenario stalls a FLAC stream 100 bytes into a frame; the unit test of the AHI output runs dry after 40000
  bytes, then after 40002. Each new case fails without the fix (no `STMo`), and passes with it. With the FLAC
  scenario's first threshold of 16 KB, below its largest frame (about 15 KB after the metadata), the output started
  with nothing to play and now says so (`STMo` at once); the servers ask for 200 KB or more.
- **On the A1200** (2026-10-27, LMS, PCM): the cable pulled twice with the fix. Both times `STMo` came when the
  sound stopped. Put back after 8 s: LMS paused the player, then resumed it about 1 s after the cable was back.
  Put back after 13 s: the test setup's relay had dropped both connections meanwhile, so the player stayed
  stopped. Without `STMo`, amisqueeze did not say "no news from the server" either, over more than 3 minutes:
  see player-16.

## player-15 The task priority 🔶 (2026-10-28, version 0.8: the A1200 next)

- **Found on the A1200** (2026-10-27, player-9 step 4, LMS, raw PCM at 44.1 kHz, about 77 % of the CPU): the
  sound has gaps as soon as the Amiga works, at amisqueeze's priority 0, without any `STMo` (the buffer never runs
  dry: AHI is not fed in time). Raised with `ChangeTaskPri`:

  | Priority | Copy only | Windows moved only | Both | Workbench | Copying 2.6 MB |
  |---|---|---|---|---|---|
  | 0 | gaps | gaps | gaps | smooth | 8 s |
  | 0, `MAXRATE=32000` | | | gaps | smooth | 8 to 14 s |
  | 1 | | | clean | slow but usable | 65 s |
  | 5 | clean | clean | | very slow | 17 s |
  | 5, `MAXRATE=32000` | | | clean | slow but usable | 11 to 27 s |

  More AHI blocks would only help with short bursts: a long copy at the same priority leaves amisqueeze about
  half of the CPU, when it needs three quarters.
- **Agreed with the user on 2026-10-28**: a `PRI` option (command line, ToolTypes, `ENV:amisqueeze.prefs`),
  -10 to 10, 0 by default: on a fast Amiga amisqueeze takes little CPU, and a higher default would slow the
  Workbench of a slow one without an obvious reason, where a gap is plain to hear. The user guide's
  troubleshooting says when to give `PRI=1`.
- **Done**: `PRI` checked by `player_main` (refused out of range), the priority set only when the option is
  given (`plat_set_priority`), and set back at the end on the Amiga: started from a Shell, amisqueeze runs in the
  Shell's own process. Tests: `test_priority` (not given, -10, 0, 10, then 11 and -11 refused), the fake server's
  arguments scenario (`PRI=11`, `PRI=-11`).
- **In WinUAE**, not checked again on the A1200 (agreed with the user on 2026-10-28): `PRI` read from the
  command line, the ToolTypes and `ENV:amisqueeze.prefs`; the priority unchanged without it; the Shell's priority
  back after amisqueeze ends. Done on 2026-10-28: 5 from the command line and from a ToolType, -3 from
  `ENV:amisqueeze.prefs`; a Shell at 2 stays at 2 without `PRI` and comes back to 2 after; `PRI=11` refused from
  `ENV:` and from a ToolType, with its message.
- **On the A1200**: `PRI=1` with a copy and windows moved: clean. Without `PRI`: gaps, as before.

## player-16 A receive that never blocks 🔶 (2026-10-28, version 0.8: the A1200 next)

- **Found on the A1200** (2026-10-27, player-9 step 6, AmiNetXDuo 1.0.0-rc2): the cable pulled for 13 s left
  half-open connections. amisqueeze never said "no news from the server", over more than 3 minutes. Ctrl-C then
  showed "recv: error 4" (EINTR) and "cannot connect: error 4", then a retry, before "stopped".
- **Cause** (read in AmiNetXDuo's source, 2026-10-28): amisqueeze was blocked in a `recv()` on the control
  connection, which `WaitSelect()` had reported readable.
  - AmiNetXDuo reports a TCP socket readable as soon as its receive queue holds a segment, out-of-order ones
    included. `recv()` returns none of them until the gap is filled.
  - So a blocking `recv()` waits for the missing segment, forever when the peer is gone. The "no news" check
    comes after it in the main loop.
  - Ctrl-C interrupts the call with EINTR and leaves the signal set: hence the messages.

  The most likely cause, not proven: the `VERBOSE` line below will tell.
- **Content** (agreed with the user on 2026-10-28):
  1. both sockets non-blocking once connected. Today the control one is blocking, and the stream one goes back to
     blocking once connected. A receive with nothing to read means "nothing yet", not an error: a portable code
     in `platform.h`, mapped in `amiga.c` and `posix.c`;
  2. a socket reported readable with nothing to read is left out of the next wait, which lasts 100 ms at most.
     No busy loop, and "no news" comes at 35 s. A `VERBOSE` line says so, once per connection;
  3. EINTR taken as a stop request, without a message: the control connection's `connect()` is the one blocking
     call left. The EINTR itself counts, not the signal, which another stack may have cleared.

  A send whose buffer is full fails, and amisqueeze reconnects. That happens only when the server has been gone
  for long, after "no news" as a rule.
- **Criterion**:
  - `make test` green, with three new cases: a receive with nothing to read; an EINTR with a stop ("stopped"
    alone); the fake server's control going silent without closing, while playing after `STMo` ("no news" at
    35 s);
  - in WinUAE with AmiNetXDuo (tools-11): Ctrl-C while connecting to an address that does not answer, idle,
    playing and in pause: "stopped" alone. The load the same before and after the change;
  - on the A1200: the cable pulled for 13 s with the test setup's relay. "no news" at about 35 s, then a
    reconnection; Ctrl-C and Exchange's Remove: "stopped" alone; the Workbench usable meanwhile. The load as in
    player-9 (77 %).
- **Upstream**: AmiNetXDuo's readiness is not ours to fix. Reporting it is the user's decision.
- **Done** (2026-10-28): `PLAT_AGAIN` and `plat_interrupted()` in `platform.h`; the sockets non-blocking once
  connected in `amiga.c` and `posix.c`, EINTR recorded as the stop request (`WaitSelect()`'s too: AmiNetXDuo
  returns it for a Ctrl-C pressed before the wait, without the signal); `AGAIN_WAIT_MS` in `run()` for the
  control connection and in `stream_events()` for the stream. Tests: `test_run_again`, `test_run_interrupted`,
  `test_stream_again`, the fake server's `silent_server` scenario.
- **In WinUAE** (2026-10-28, slow profile with AmiNetXDuo 1.0.0-rc2 on the emulated NE2000 card, LMS):
  - a connection to an address that does not answer blocks for over a minute. Ctrl-C then gives "stopped" alone;
    the build before this step gave "cannot connect: error 4" and "retrying in 5 s" first;
  - Ctrl-C idle, three times while playing through AHI, and in pause: "stopped" alone each time;
  - the load with `NOAUDIO`, raw PCM at 44.1 kHz, the builds before and after in turn (six 10 s windows each):
    42.4 % before, 42.8 % after, within the spread of the windows (about 1 %). Unchanged.
