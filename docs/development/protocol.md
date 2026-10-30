# The protocol, as amisqueeze speaks it

For contributors, and for anyone writing a Squeezebox player or remote: what LMS and Music Assistant send to
amisqueeze, what it answers, and what it trusts. How the program is built around it is in
[architecture.md](architecture.md).

## Glossary

The words of slimproto, and of this project, used in the code and the documents.

| Word | Meaning |
|---|---|
| slimproto | the control protocol of Squeezebox players: one TCP connection to the server, port 3483 by default |
| LMS | Lyrion Music Server, formerly Logitech Media Server: the reference server |
| Music Assistant, MA | another server that speaks slimproto to its Squeezebox players |
| squeezelite | the reference software player, read to learn how a player behaves (never copied: GPL) |
| packet, opcode | a slimproto message and its 4-letter name: lower case from the server (`strm`), upper case from the player (`STAT`) |
| `HELO` | the player's first packet: its device id, MAC address and capabilities (formats, highest rate) |
| `strm` | the server's stream command; its first byte says which: `s` start a track, `p` pause, `u` resume (or start), `q` stop, `f` flush the track being streamed, `t` ask for a status, `a` skip ahead |
| `STAT`, `STMx` | the player's status packet, and its 4-letter event: `STMf` flushed, `STMc` connected, `STMl` loaded, `STMs` track started, `STMt` timer, `STMp` paused, `STMr` resumed, `STMd` all of the track received, `STMo` output ran dry while streaming, `STMu` everything played, `STMn` track refused |
| autostart | a field of `strm s`: 1 starts at the threshold, 0 waits for `strm u`; 3 and 2 the same, the body read only after `cont` (a direct stream) |
| threshold | how much of a track to receive before it may start, in KB, from `strm s` |
| direct stream | a track the player fetches from its source itself (an internet radio), not through the server |
| `RESP` | the player sends a stream's HTTP response headers to the server |
| `cont` | the server lets a direct stream's body be read, with the interval of its metadata blocks |
| ICY, metadata block | an internet radio's titles, mixed into its sound every `icy-metaint` bytes |
| `META` | the player sends a radio's metadata block back to the server |
| `DSCO` | the player says a stream's connection ended, with its reason: 0 normal, 1 local, 2 remote, 3 unreachable |
| `audg`, `aude`, `setd` | the server's volume (gains), audio outputs on or off, and settings (id 0: the player's name) |
| discovery | a UDP broadcast that finds the servers on the local network |
| ring | amisqueeze's 1 MB stream buffer, where the tracks wait back to back |
| sink | where the output hands the PCM: the AHI blocks, or the virtual output paced by the clock |

## Slimproto: the messages

**Framing**: from server to client, a 2-byte length, then a 4-byte opcode and the data. From client to server, the
opcode, a 4-byte length, then the data. Every field is serialised byte by byte, big-endian, without packed
structures.

**Sent**:

- `HELO`: device id 12 (SqueezePlay), the MAC, uuid and byte counts at 0. The MAC is derived from the name, unless
  `MAC`. On a reconnection, `0x4000` is set in the channel list;
- `HELO`'s capabilities:
  - `Model=squeezelite`, so that LMS recognises the player;
  - `ModelName=AmiSqueeze`, `Firmware=<version>`, `AccuratePlayPoints=0`;
  - `HasDigitalOut=1`, so that LMS offers its fixed volume setting;
  - `MaxSampleRate` and the formats ([Formats and conversions](#formats-and-conversions));
- `STAT` ([the statuses](#a-tracks-life-the-statuses)), `RESP`, `DSCO`, `META` (a radio's metadata block),
  `SETD` (the player name).

**Received and handled**:

- `strm s`: autostart, format (`p`, `f`, `m`), sample size, rate and channel codes, endianness, threshold, port,
  address, request. The other fields are ignored: S/PDIF, transition, flags, output threshold, replay gain;
- `strm t`, `q`, `f`, `p` (interval 0), `u`;
- `cont`: the blocks' interval (32 bits), a `loop` byte, a count of GUIDs (16 bits), the GUIDs. Only the interval
  is used. The `loop` byte, and a `cont` with no stream waiting for it, are said with `VERBOSE`;
- `audg`: the 16.16 gains, already on the server's volume curve, which `audio.c` applies to the samples. Adjust 0
  means a fixed volume: the samples are left untouched. ahi.device always gets full volume
  ([AHI](architecture.md#ahi));
- `setd` id 0: the player name, set or queried. It is answered with `SETD`.

**Received and ignored on purpose**:

- `aude` (S/PDIF and DAC on or off): printed with `VERBOSE` only;
- `vers`, `grfb`, `grfe`, `codc`, which Music Assistant sends: `<- ... ignored` with `VERBOSE` only, since they
  would read as errors;
- `strm a` (skip ahead) and `strm p` with an interval, which serve synchronised players. `strm u` resumes at
  once, whatever time it gives.

**Not handled**: `serv`, LMS's `body`, and any other opcode. The message "not handled" is always shown, and
nothing is done. LMS sends `body` for a playlist address played directly, asking for the body back.

**Not done at all**: synchronisation between players, displays and IR, HTTPS.

## A track's life: the statuses

What the server expects, as squeezelite behaves (written from the protocol, not from its code):

1. `strm s` -> `STMf`; connected and request sent -> `STMc`; response headers -> `RESP`. For a status outside
   2xx, see [internet radios](#internet-radios).
2. Threshold reached: autostart 1 -> `STMs`; autostart 0 -> `STMl`, then `strm u` -> `STMr` (and `STMs`).
   Autostart 3 and 2 (a direct stream) are the same, but nothing of the body is read before `cont`, not even the
   bytes that came with the headers. There is no time limit.
3. While playing: `STMt` every second, and in reply to each `strm t`, its timestamp echoed. `strm p` -> `STMp`,
   `strm u` -> `STMr`.
4. End of the HTTP body -> `DSCO` 0 + `STMd`. The server then sends the next track's `strm s`. Its data is
   appended to the ring, and `STMs` is sent again when the output reaches it.
5. Buffer and sink empty: `STMo` while still streaming (the network is too slow), `STMu` once everything has been
   played. "Empty" means nothing the decoder can use: a stream that stops in the middle of a frame leaves its
   start in the ring.
6. A stream that fails sends `DSCO` with its reason: 1 local, 2 remote, 3 unreachable.

Three rules follow from what the server does.

**`strm f` drops only the last queued track**, since `STMd` comes at the end of the data received, not at the
end of the decoding as with squeezelite:

- its stream is closed without `DSCO`, and its data taken back from the ring (`ring_truncate()` at its
  `track.start`);
- with the audio output, a block of it already filled plays at the end of the current track, at most about 0.6 s;
- with nothing playing, `strm f` is a stop.

Why: with a 1 MB ring, the server's next `strm s` comes while up to 1 MB of the current track is still waiting in
the same buffer. That is about 6 s of PCM, 9 s of FLAC. When LMS changes its mind about the next track (removed,
moved, reshuffled), it sends `strm f` and expects the playing track to go on. Clearing the ring would cut it.

**A `strm s` refused at once is answered `STMf`, `STMc`, then `STMn`** (`slim_send_refusal()`), as by a player
whose decoder fails once the stream has started. LMS then moves on to the next track; Music Assistant ignores
`STMn`. A `strm s` is refused for a format this build does not decode, a request over 1 KB, or 16 tracks already
waiting. Why: LMS drops every status after a `strm s` until `STMc`, its stream start.

**At most 16 tracks wait in the ring** after the one playing (`MAX_TRACKS`), and one more is refused. Why so
many:

- LMS sends its second song only after the first one's `STMs`. But it fetches a radio that drops again at once
  with a new `strm s` each time;
- Music Assistant sends another track each time the one after the track playing changes in its queue, or
  crossfade is switched, once the waiting one has fully arrived.

Why refuse rather than forget the oldest: its data would go to the decoder of the track before it.

## Formats and conversions

amisqueeze tells the server which formats it decodes, and the highest rate it takes. The server then sends some
files as they are and converts the others.

- `HELO` announces `pcm` always, then `flc` and `mp3` as decided at start-up: `FLAC` and `MP3` options, `AUTO`
  measuring the speed. Its `MaxSampleRate` is 48000, or what `MAXRATE` asks.
- LMS then sends FLAC and MP3 files as they are (formats `f`, `m`), and WAV and AIFF as raw PCM with their headers
  stripped. It converts the rest.
- For `f` and `m`, the parameters of `strm s` are a guess until the decoder reads the stream.
- For PCM, `strm s` gives the source file's rate and sample size, whatever the conversion rule sends
  (`pcm_sample_rates`, `pcm_sample_sizes`). So the user guide's rules send WAV, whose header `decoder.c` plays.

**LMS 9.1's conversion rules**:

- a `MaxSampleRate` below a file's rate makes LMS look for a rule with the `D` (resample) capability. None of its
  rules to `pcm` has one, so it refuses the track of a player that takes PCM only. Its rules to `flc` have one;
- LMS has no default `mp3 pcm` rule, so a regular user's server cannot turn MP3 into PCM. Hence the MP3 decoder of
  `amisqueeze.fpu`. For the build without it, the rule is in the user guide's server setup;
- that rule has no `-r`: LMS tells the player the file's own rate, and forcing 44100 Hz would play 48 kHz files
  9 % too fast;
- its `FT:{START=trim %s}` lets LMS start within a track (a seek).

**An example**: an MP3 that LMS converts comes with format `p`, size `1` (16 bits), rate `3` (44.1 kHz) and
channels `2`. Its endianness is `1`, little-endian, which `audio.c` swaps for AHI. Then autostart `1`, the control
connection's address, and the request `GET /stream.mp3?player=<mac> HTTP/1.0`.

## Internet radios

An internet radio mixes metadata blocks (ICY) into its sound. Most often the server removes them: they reach
amisqueeze only on a direct stream, where the player fetches the radio itself. Read in LMS 9.1.1's and Music
Assistant 2.10.7's code:

- LMS streams directly when the player decodes the format (MP3 with `amisqueeze.fpu`), the address is `http://`,
  and the player is not synchronised. amisqueeze does not announce `CanHTTPS`;
- for a direct stream, neither the player's "Streaming Method" nor the server's `useEnhancedHTTP` ("Streaming mode
  for HTTP(S)") may ask for a proxy (`canDirectStream` in `Slim/Player/Protocols/HTTP.pm` and `HTTPS.pm`);
- otherwise LMS fetches the radio, asks for `Icy-MetaData` and removes the blocks itself (`readMetaData`);
- Music Assistant always fetches the radio, removes the blocks and keeps the title (`controllers/streams/audio.py`).

**What the servers send**: a direct stream comes with autostart 3 or 2.

- LMS sends `cont` once it has read `RESP`, with the interval of `icy-metaint`: it asked the radio for
  `Icy-MetaData`. On a 3xx it sends a new `strm s` itself. Outside 200 to 399 the stream fails (`Squeezebox2.pm`,
  `directHeaders`).
- Music Assistant sends autostart 3 or 2 for every track. After each `RESP`, even after an error, it sends `codc`
  and `cont` with an interval of 0 (`client.py`, `_process_resp`).

**The status line**: its code is the three digits after the first space, whatever the protocol (`ICY 200 OK` is a
200). Outside 2xx the body never enters the ring, and a message is always shown. Then:

- autostart 0 or 1: `DSCO` 2;
- autostart 2 or 3: `RESP` only, and the server decides. A `DSCO` would count as a second failure, or against the
  next track. A `cont` that comes anyway gets `DSCO` 2.

amisqueeze follows no redirect itself.

**The blocks**: every `interval` bytes of sound come a length byte (times 16), then the block,
`StreamTitle='...';` padded with NULs.

- Each `recv` stops where the next block begins (`icy_sound()`), so the sound goes straight into the ring.
- The length byte and the block (4080 bytes at most) go to a buffer in `struct stream`.
- The bytes that came with the headers are split the same way at `cont`.
- The threshold and `STAT` count the sound only.
- With an interval of 1000 (some radios), there is a `recv` every 1000 bytes at most.

**The titles**: LMS learns a direct stream's titles only from the player. Each block that is not empty goes back
in `META`, as received. LMS reads `StreamTitle` in it (`Protocols/HTTP.pm`, `parseMetadata`), shows it and gives
it to remotes. On the console, with `VERBOSE`, in ASCII.

## Server discovery

Without `SERVER`, amisqueeze broadcasts a UDP request to 255.255.255.255 on `PORT`. It listens for 2 s, then
connects to the first server that answered; up to 8 are kept and shown.

- The request is `e`, then the field `NAME` with no value: a 4-byte tag, a 1-byte length.
- LMS (`Slim/Networking/Discovery.pm`) and Music Assistant (aioslimproto's `discovery.py`) both answer on their
  slimproto port, with `E` and the fields asked for. Every reply starting with `E` counts.
- Neither sends its slimproto port. So the reply's source address is the server's, and `PORT` serves both the
  request and the connection.
- `NAME` is only shown, read within the packet's bounds. Music Assistant writes its length in characters, then
  encodes the whole packet in UTF-8. A name outside ASCII thus gives a wrong length; from 128 characters, the
  length byte itself takes two.
- The older request `d` (the Squeezebox's) is not used: its reply carries only the host name, 17 bytes.
- A discovered server is looked for again when a connection to it fails, not after a lost connection: the same
  address is tried first.

A router does not pass a broadcast on. Some NATs, the bridge of a container host for instance, rewrite a
broadcast's source, so that the replies are lost.

## Music Assistant's differences

Music Assistant (seen with 2.10.7) speaks the same protocol, with differences. It transcodes everything to the
output codec set for the player: `flac` by default, `mp3`, `aac`, `wav` (its `constants.py`,
`CONF_ENTRY_OUTPUT_CODEC`).

- `strm s` comes without the stream's parameters for FLAC and MP3, with autostart 2 or 3. Then `codc` (the codec)
  and `cont` follow the response headers. amisqueeze reads the body once `cont` has come. With autostart 2, MA
  then sends `strm u`.
- Its `wav` output codec sends a WAV header, 24 bits while `strm s` says 16. `decoder.c` reads the header at the
  start of each PCM track and plays its format.
- Its `mp3` codec has no LAME header, so it is not gapless. `aac` is refused (`STMn`, which MA ignores).
- It sends `strm f` then `strm q` before each new track, and `audg` 50 at each connection, then the player's
  volume.

## What amisqueeze trusts

amisqueeze trusts the local network and the server it connects to. Slimproto has neither authentication nor
encryption, and amisqueeze adds none.

- **The first server that answers** the discovery is the one it connects to. Any host of the local network can
  answer. `SERVER` names one, and skips the discovery.
- **The server chooses where the sound comes from.** `strm s` gives an address and a port, and amisqueeze connects
  there and sends the server's request as is. That is how a direct stream reaches an internet radio.
- **It listens on no port.** It only connects: TCP to the server and to the streams, UDP for the discovery, whose
  replies come back to the port it sent from.
- **Plain HTTP only**: no HTTPS, and it follows no redirect itself. LMS does, with a new `strm s`.
- **What it receives is untrusted all the same.** The rule: every size that comes from the network is checked
  against the buffer it goes to (all the limits: [architecture.md](architecture.md#time-outs-and-limits)). For
  instance:
  - a control packet over 4 KB, or of size 0, ends the connection, which is then made again;
  - a `strm s` whose request is over 1 KB is refused (`STMn`), as is one more track when 16 wait;
  - response headers over 4 KB end the stream (`DSCO` 1);
  - a metadata block is 4,080 bytes at most, by its length byte;
  - a title or a server's name is shown in ASCII, any other byte as `?`.

## What amisqueeze does not handle

Besides the messages left out ([Slimproto: the messages](#slimproto-the-messages)):

- **No time limit on a stream**: its connection, its response headers and `cont` are waited for as long as it
  takes. The server's next command ends the wait (`strm q`, a new `strm s`), and the control connection's own
  check still runs: no news from the server for 35 s, and amisqueeze connects again.
- **The control connection's `connect()` blocks**, as long as the TCP/IP stack tries. Ctrl-C ends it.
- **A body sent in chunks** (`Transfer-Encoding: chunked`) is played as it comes, the chunks' sizes in it.
  amisqueeze sends the server's request as is. Both servers ask for HTTP/1.0, which a web server answers without
  chunks: LMS 9.1.1 (`Slim/Player/Protocols/HTTP.pm`, `requestString`) and Music Assistant (aioslimproto's
  `client.py`).
- **HTTPS, redirects**: see [What amisqueeze trusts](#what-amisqueeze-trusts).
