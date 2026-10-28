# Architecture of amisqueeze

For developers: how the player is organised, how the audio goes from the network to AHI, and what of slimproto it
handles. Building and testing are in [CONTRIBUTING.md](../../CONTRIBUTING.md); why each step was done the way it
was is in the [plan](README.md). The code comments give the details; this page gives the picture.

## Portable core and platform layer

Every file of `src/amisqueeze/` but `amiga.c` and `posix.c` is the **portable core**: standard C, no Amiga or
POSIX header. Whatever touches the system (sockets, clock, files, the audio device, the wait) goes through
`platform.h`.

| File | Role |
|---|---|
| `player.c` | set-up from the options, the main loop, reconnection, the FLAC and MP3 decision at start-up |
| `slimproto.c` | the control connection: framing, incoming packets, `HELO`, `STAT`, `RESP`, `DSCO`, `SETD` |
| `stream.c` | the HTTP stream, the queue of tracks, the output (which decoder runs, track boundaries, elapsed time, statuses), the virtual output |
| `ring.c` | the 1 MB ring buffer of stream bytes |
| `decoder.c`, `flac.c`, `mp3.c` | the decoders: raw PCM (and a WAV header), FLAC (dr_flac), MP3 (minimp3, `AMISQUEEZE_MP3` only) |
| `audio.c` | the audio output's blocks: order, conversion to 16-bit big-endian, volume, `SWAP`, bytes played, pause |
| `bench.c` | the decoding speed (built-in samples `bench_flac.c`, `bench_mp3.c`), for `AUTO` and `BENCH` |

`platform.h` has three implementations:

- `amiga.c`: bsdsocket.library (`WaitSelect()`), timer.device, dos.library, ahi.device; also the entry point
  (`ReadArgs`, `ENV:amisqueeze.prefs`, Workbench start and ToolTypes, `LOG`, the commodity);
- `posix.c`: the host build, for tests and debugging; no audio device: its entry point never asks for the audio
  output (`UNIT`, `NOAUDIO`, `SWAP` accepted and ignored), so always the virtual output;
- `tests/stub_platform.c`: the unit tests (`tests/test_core.c`), with a simulated clock and sockets, and a fake
  audio device that counts the requests ahi.device would mishandle (`stub_audio_bad`).

One task, one loop (`run()` in `player.c`): `plat_wait()` on the control socket and, while there is one, the
stream socket (writable while connecting, readable once the ring has room enough), with a timeout of 100 ms while
something streams or plays and 1 s otherwise. On the Amiga, the end of an AHI request also wakes it up. Each turn:
the control packet, then the stream socket, then `output_tick()`. The stream socket's readiness is used only if
the control packet closed no stream socket (`stream.closes`): a new socket may take the old one's number. Addresses and ports are in host byte order in the
core; only the platform layers convert them.

Connected sockets are non-blocking. A TCP/IP stack may report a socket readable while nothing can be read yet:
AmiNetXDuo counts the segments that arrived out of order, after a lost one. A blocking receive would then wait for
the missing segment, forever if the server is gone, and the 35 s "no news" check would never come. A read that
finds nothing (`PLAT_AGAIN`) is no error: that socket is left out of the waits for 100 ms (`AGAIN_WAIT_MS`), so the
loop does not spin while the stack keeps reporting it. The control connection's `connect()` is the one blocking
call left. Ctrl-C interrupts it with EINTR, which counts as the stop request itself (`plat_interrupted()`): a stack
may have cleared the signal. The same for `WaitSelect()`: AmiNetXDuo returns EINTR for a Ctrl-C pressed before the
wait started, without the signal in the mask it gives back.

Builds of the same sources: `out/amisqueeze` (68020, FLAC), `out/amisqueeze.fpu` (68020 + 68881, `AMISQUEEZE_FPU`
and `AMISQUEEZE_MP3`: FLAC and MP3), `out-host/amisqueeze` (`posix.c`, with the MP3 decoder, ASan and UBSan).
LMS 9.1 has no default `mp3 pcm` conversion rule, so a regular user's server cannot turn MP3 into PCM: hence the
MP3 decoder of `amisqueeze.fpu`. For the build without it, the rule is in the user guide's server setup. The rule
has no `-r`: LMS tells the player the file's own rate, and forcing 44100 Hz would play 48 kHz files 9 % too fast.
`FT:{START=trim %s}` lets LMS start within a track (a seek).

## Data path

```mermaid
flowchart LR
    server["Server (LMS, Music Assistant)"]
    slim["slimproto.c: control connection (TCP 3483)"]
    http["stream.c: HTTP stream"]
    ring["ring.c: ring buffer (stream bytes)"]
    dec["decoder of the track (pcm, flac, mp3, skip)"]
    out["stream.c: output (boundaries, elapsed time)"]
    virt["virtual output (paced at the PCM rate)"]
    file["OUTPUT file"]
    blocks["audio.c: 3 blocks (16-bit, volume, SWAP)"]
    ahi["amiga.c: ahi.device"]

    server -->|"strm, audg, setd"| slim
    slim -->|"STAT, RESP, DSCO, SETD"| server
    slim -->|"strm s"| http
    server -->|"HTTP body"| http
    http --> ring --> dec --> out
    out -->|"hand_over()"| virt
    virt -.->|"OUTPUT="| file
    out -->|"audio_submit()"| blocks --> ahi
```

- **`strm s`** gives the stream's address (0 = the control connection's), the HTTP request (sent as is), the
  format and a threshold. `stream_start()` queues a track at the ring's write position and connects without
  blocking; the response headers go back to the server in `RESP`.
- **The ring buffer** holds the stream as received, several tracks back to back (gapless). The stream socket is
  read only once 8 KB of it are free (`READ_MIN_SPACE`, a quarter of a smaller ring), 2 KB for the response
  headers: reading the few hundred bytes the output frees at each turn kept the loop awake (40 % of a 68020 at
  28 MHz with `NOAUDIO`, 4.6 % since). When it is full, TCP slows the server down. The single buffer is reported
  in `STAT` as both the stream buffer and the output buffer.
- **The output** starts once `threshold` bytes have arrived (or the whole track). It asks the decoder of the track
  at the ring's read position for PCM; a decoder never reads past its track's end (`until_next_track()`), and a
  track whose decoder cannot start gets the `skip` decoder, which drops its data.
- **The sink** takes the PCM: the virtual output (host build, `NOAUDIO`, `OUTPUT`) consumes it at the PCM rate, as
  a sound card would, through `hand_over()`, the only place where `OUTPUT` writes; the audio output fills
  `audio.c`'s blocks with whole frames of one track, at most two with the device (one playing, one linked to it)
  and one filled in advance, about 0.6 s in all. `OUTPUT` keeps the virtual output, so that the file holds exactly
  what was handed over.

## Two kinds of position

A decoder turns N bytes of the stream into M bytes of PCM, so positions are counted in two units that must never
be mixed:

- **the ring's, in bytes of the stream**: `ring.total_in` and `ring.total_out` (since the last `ring_clear()`,
  whatever the wrap-around), `track.start` (where a track's data begins), `decoder.track` (the track being
  decoded); `strm f` truncates the ring at a `track.start`;
- **the output's, in bytes of decoded PCM**: `out.handed` (handed to the sink since the last reset),
  `track.out_start` (where the track begins in the output), and the bytes played (`sink_played()`: an estimate
  from the AHI blocks, or `handed` for the virtual output).

The link between them: when the reading reaches a queued track and the decoder holds nothing more of the previous
one (`finished`: dr_flac and minimp3 read ahead), that track's `out_start` is set to `handed` (`reach_tracks()`).
The boundary is reported (`STMs`) when the bytes played reach it (`next_track_due()`); elapsed time is
`(played - cur.out_start)` at the rate of the decoded format. Raw PCM has the same size on both sides and hides a
mix-up: the test-only "widening" decoder of `tests/test_core.c` makes the output bigger than the input, and
reveals it.

## Decoders

What each third-party decoder needs from its caller:

- **FLAC** (`flac.c`, dr_flac): dr_flac takes a short read for the end of the stream, and reads ahead (a 4 KB
  cache). So a frame is decoded only when the ring holds a whole one, and a track ends for the output only when
  the decoder says `finished`. After a seek or a resume, LMS sends the file from a frame boundary, without its
  header: `flac.c` makes up a STREAMINFO from the first frame's header, and refuses a frame whose header leaves
  its rate or its sample size to STREAMINFO.
- **MP3** (`mp3.c`, minimp3, `AMISQUEEZE_MP3` only): minimp3 resets itself when the bytes after a frame are not
  the next header, so it is called only with a frame and the next header in its 16 KB staging buffer (a full
  buffer for the first frame), or at the end of the track. Without gaps: the LAME header of the first frame gives
  the encoder's delay and padding to drop (`mp3_lame_header()`); after a seek, LMS sends that frame again first,
  so the end limit is never reached.

## AHI

`audio.c` (portable) fills the blocks; the `ahi.device` calls are in `amiga.c`. Rules learnt from the source of
`ahi.device` (AHI 6.0), which the code keeps:

- chain a request (`ahir_Link`) only to one already **playing**: linked to a waiting one, it never plays nor ends;
  hence at most two requests with the device;
- abort the **playing** one first: it takes the linked one along; the reverse order leaves a stale reference that
  hangs the device;
- `CMD_STOP` does not pause what is playing: a pause aborts the requests, and the rest is sent again on resume;
- a non-real-time mode ("Filesave") on a device unit writes only silence;
- `ahir_Volume` always full: with AHI 6.6 and `paula.audio`, requests sent again after an abort (a resume) with a
  low volume corrupt the free memory list, and the system hangs or amisqueeze dies later. `audio.c` scales the
  samples instead (two 256-entry tables, no multiplication per sample).

The fake audio device of the unit tests counts the requests that break the first two rules (`stub_audio_bad`).

## A track's life: the statuses

What the server expects, as squeezelite behaves (written from the protocol, not from its code):

1. `strm s` -> `STMf`; connected and request sent -> `STMc`; response headers -> `RESP`.
2. Threshold reached: autostart 1 -> `STMs`; autostart 0 -> `STMl`, then `strm u` -> `STMr` (and `STMs`).
3. While playing: `STMt` every second, and in reply to each `strm t` (its timestamp echoed); `strm p` -> `STMp`,
   `strm u` -> `STMr`.
4. End of the HTTP body -> `DSCO` 0 + `STMd`. The server then sends the next track's `strm s`; its data is appended
   to the ring and `STMs` is sent again when the output reaches it.
5. Buffer and sink empty: `STMo` while still streaming (the network is too slow), `STMu` once everything has been
   played. A stream that fails sends `DSCO` with its reason (1 local, 2 remote, 3 unreachable). "Empty" means
   nothing the decoder can use: a stream that stops in the middle of a frame leaves its start in the ring (fixed
   in 0.8; until then no `STMo`, and LMS was not told).

**`STMd` is sent at the end of the data received, not at the end of the decoding** (squeezelite sends it when its
decoder has finished). With a 1 MB ring, the server's next `strm s` therefore comes while up to 1 MB of the
current track (about 6 s of PCM, 9 s of FLAC) is still waiting in the same buffer. When LMS changes its mind about
the next track (removed, moved, reshuffled), it sends `strm f` and expects the playing track to go on: clearing
the ring would cut it. So `strm f` drops only the last queued track: its stream closed without `DSCO`, its data
taken back from the ring (`ring_truncate()` at its `track.start`); with the audio output, a block of it already
filled plays at the end of the current track (at most about 0.6 s). With nothing playing, `strm f` is a stop.
This was the `strm f` defect fixed in 0.7.

**LMS drops every status after a `strm s` until `STMc`** (its stream start). A `strm s` refused at once (a format
this build does not decode, a request over 1 KB) is therefore answered `STMf`, `STMc`, then `STMn`
(`slim_send_refusal()`), as by a player whose decoder fails once the stream has started; LMS then moves on to the
next track. Music Assistant ignores `STMn`.

## What of slimproto is handled

Framing: server to client, a 2-byte length then a 4-byte opcode and the data; client to server, the opcode, a
4-byte length, then the data. Every field is serialised byte by byte, big-endian, without packed structures.

**Sent**:

- `HELO`: device id 12 (SqueezePlay), the MAC (derived from the name unless `MAC`), uuid and byte counts at 0,
  `0x4000` in the channel list on a reconnection; capabilities `Model=squeezelite` (so that LMS recognises the
  player), `ModelName=AmiSqueeze`, `Firmware=<version>`, `AccuratePlayPoints=0`, `HasDigitalOut=1` (LMS then offers
  its fixed volume setting), `MaxSampleRate` (48000, or what `MAXRATE` asks), then the formats decoded here: `pcm`
  always, `flc` and `mp3` as decided at start-up (`FLAC`, `MP3` options; `AUTO` measures the speed). LMS then sends
  FLAC and MP3 files as they are (formats `f`, `m`), WAV and AIFF as raw PCM (headers stripped), and converts the
  rest.
- A `MaxSampleRate` below a file's rate makes LMS 9.1 look for a conversion rule with the `D` (resample)
  capability. None of its rules to `pcm` has one, so it refuses the track of a player that takes PCM only. Its
  rules to `flc` have one. For PCM, `strm s` gives the source file's rate and sample size whatever the rule sends
  (`pcm_sample_rates`, `pcm_sample_sizes`): the rules of the user guide send WAV, whose header `decoder.c` plays
  (plan step player-13).
- `STAT` (the statuses above), `RESP`, `DSCO`, `SETD` (the player name).

**Received and handled**:

- `strm s`: autostart, format (`p`, `f`, `m`), sample size, rate and channel codes, endianness, threshold, port,
  address, request. The other fields (S/PDIF, transition, flags, output threshold, replay gain) are ignored. For
  `f` and `m` the parameters are a guess until the decoder reads the stream. Seen from LMS for an MP3 it converts:
  format `p`, size `1` (16 bits), rate `3` (44.1 kHz), channels `2`, endianness `1` (little-endian: `audio.c`
  swaps it for AHI), autostart `1`, the control connection's address, request
  `GET /stream.mp3?player=<mac> HTTP/1.0`.
- `strm t`, `q`, `f`, `p` (interval 0), `u`.
- `audg`: the 16.16 gains, already on the server's volume curve, applied to the samples by `audio.c`; adjust 0 =
  fixed volume (samples untouched). ahi.device always gets full volume (see [AHI](#ahi)).
- `setd` id 0: the player name, set or queried; answered with `SETD`.

**Received and ignored on purpose**:

- `aude` (S/PDIF and DAC on or off): printed with `VERBOSE` only;
- `vers`, `grfb`, `grfe`, `codc`, `cont`, which Music Assistant sends (`cont` also comes with LMS's internet
  radios): `<- ... ignored` with `VERBOSE` only, since they read as errors;
- `strm a` (skip ahead) and `strm p` with an interval: for synchronised players; `strm u` resumes at once, whatever
  time it gives;
- autostart 2 and 3 (wait for `cont`, for radio metadata): played as 0 and 1.

**Not handled** (message "not handled", always shown, nothing done): `serv`, and any other opcode. Not done at
all: internet radios' metadata (ICY, planned for 0.9, see the plan), synchronisation between players, displays
and IR.

**Internet radios** (read in LMS 9.1.1's and Music Assistant 2.10.7's code): ICY blocks reach amisqueeze only
on a direct stream, where the player fetches the radio itself.

- LMS streams directly when the player decodes the format (MP3 with `amisqueeze.fpu`), the address is `http://`
  (amisqueeze does not announce `CanHTTPS`) and the player is not synchronised. Neither the player's "Streaming
  Method" nor the server's `useEnhancedHTTP` ("Streaming mode for HTTP(S)") may ask for a proxy
  (`canDirectStream` in `Slim/Player/Protocols/HTTP.pm` and `HTTPS.pm`).
- Otherwise LMS fetches the radio, asks for `Icy-MetaData` and removes the blocks itself (`readMetaData`).
- Music Assistant always fetches the radio, removes the blocks and keeps the title
  (`controllers/streams/audio.py`).

**Server discovery** (without `SERVER`): a UDP request broadcast to 255.255.255.255 on `PORT`, `e` then the
field `NAME` with no value (a 4-byte tag, a 1-byte length). LMS (`Slim/Networking/Discovery.pm`) and Music
Assistant (aioslimproto's `discovery.py`) both answer on their slimproto port, with `E` and the fields asked for.
Neither sends its slimproto port, so the reply's source address is the server's, and `PORT` serves both the
request and the connection. Every reply starting with `E` within 2 s counts; the first one is connected to.
`NAME` is only shown, read within the packet's bounds: Music Assistant writes its length in characters, then
encodes the whole packet in UTF-8, so a name outside ASCII gives a wrong length (from 128 characters, the length
byte itself takes two). The older request `d` (the Squeezebox's) is not used: its reply carries only the host
name, 17 bytes. A discovered server is looked for again when a connection to it fails, not after a lost
connection: the same address is tried first. A router does not pass a broadcast on, and some NATs (an Incus
bridge, for instance) rewrite a broadcast's source, so that the replies are lost.

**Music Assistant** (seen with 2.10.7, plan step player-7b) speaks the same protocol, with differences. It
transcodes everything to the output codec set for the player (`flac` by default, `mp3`, `aac`, `wav`; its
`constants.py`, `CONF_ENTRY_OUTPUT_CODEC`):

- `strm s` without the stream's parameters for FLAC and MP3, autostart 2 or 3, then `codc` (the codec) and `cont`
  after the response headers: amisqueeze reads the body at once, and MA sends `strm u`;
- its `wav` output codec sends a WAV header, 24 bits while `strm s` says 16: `decoder.c` reads the header at the
  start of each PCM track and plays its format;
- its `mp3` codec has no LAME header (not gapless); `aac` is refused (`STMn`, which MA ignores);
- `strm f` then `strm q` before each new track; `audg` 50 at each connection, then the player's volume.
