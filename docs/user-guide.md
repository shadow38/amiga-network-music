# Amiga Network Music — User guide

This guide is for people who **use** the programs. How it works and which servers it supports are in the
[README](../README.md#how-it-works), as well as how to build them; the development roadmap is in
[docs/development](development/README.md).

## amisqueeze

amisqueeze turns the Amiga into a network music player for the music servers that speak slimproto, the protocol of
Logitech's Squeezebox players: [Lyrion Music Server](https://lyrion.org) (LMS), tested, and Music Assistant, not
tested yet. The server (and its web interface or apps) sees and controls it like any other player.

**Current state (version 0.3 in development; 0.2, the latest release, is a preview tested in WinUAE only):** amisqueeze connects to the server, shows up as a player and
**plays what LMS plays through AHI**: in LMS, playback progresses, moves on to the next track, pauses, seeks,
stops and changes volume as with any player. It takes raw PCM streams, which LMS makes from the files (see the
requirements): 8, 16, 24 or 32-bit samples (24-bit "high resolution" FLAC included), mono or stereo, up to
48 kHz, converted to 16 bits for AHI. On a fast Amiga it also decodes **FLAC** itself, and **MP3** with an FPU
(see "FLAC and MP3" below). The sound has been checked by ear in WinUAE (Paula and Toccata), not yet on a real
Amiga.

### Requirements

On the Amiga:

- AmigaOS 3.x, 68020 or better (from a plain 68020 accelerator to a PiStorm);
- about 1.1 MB of free memory (1 MB stream buffer, about 6 s of CD-quality audio), a little more for
  `amisqueeze.fpu` when it decodes MP3 (about 60 KB: its decoder and a larger stack, which it sets up itself);
- a TCP/IP stack providing `bsdsocket.library`: Roadshow, AmiTCP, AmiNetXDuo, or WinUAE's emulation
  (`bsdsocket_emu=true`) in an emulator;
- for the sound: AHI (tested with 6.x; 4.x expected to work, not checked yet) and a driver: Paula's
  (`paula.audio`, no sound card needed) or your sound card's; see "Sound (AHI)" below.

On the server:

- Lyrion Music Server (tested with 9.1.1), reachable from the Amiga on TCP ports 3483 (players) and 9000 (audio
  streams; or whatever its HTTP port is: LMS tells the player which one);
- for MP3 files, unless `amisqueeze.fpu` decodes them (see "FLAC and MP3" below): a conversion rule from MP3 to
  raw PCM, see [server setup](#server-setup). FLAC, WAV and AIFF are converted by LMS's default rules;
- files above 48 kHz (88.2, 96, 192 kHz) play only when amisqueeze decodes FLAC itself (LMS then lowers their rate
  in FLAC); when the Amiga is too slow for that, LMS cannot lower their rate as it makes raw PCM, and reports
  "Couldn't create command line for flc playback" in its log.

### Server setup

Lyrion Music Server needs little for amisqueeze:

- **Ports** reachable from the Amiga: 3483/tcp (players) and the web port, 9000/tcp by default, which also carries
  the audio streams (LMS tells the player which one; set `HTTP_PORT` if you move it, e.g. in Docker).
- **MP3 to PCM conversion**, only when amisqueeze does not decode MP3 itself: with `amisqueeze`, or with
  `amisqueeze.fpu` on a CPU too slow for it (see "FLAC and MP3" below). LMS 9.1 converts FLAC, WAV and AIFF by its
  default rules, but has **none for MP3** (regular players decode MP3 themselves): without one, its `server.log`
  says "Couldn't create command line for mp3 playback" and the player receives nothing. Add this rule to a
  `custom-convert.conf` file (in the LMS root directory or at the root of a plugins directory), then restart LMS:

  ```
  mp3 pcm * *
  	# FT:{START=trim %s}
  	[sox] -q -t mp3 $FILE$ -t raw -c 2 -2 -s - $START$
  ```

  It uses the `sox` program shipped with LMS, as its built-in `ogg pcm` rule does, but without `-r`: LMS tells the
  player the file's own rate (forcing 44100 Hz would play 48 kHz files 9 % too fast). `FT:{START=trim %s}` lets
  LMS start within a track (seeking).
- **Music Assistant**: not tested yet. It offers no raw PCM output (its per-player output codecs are `flac`, the
  default, `mp3`, `aac` and `wav`).

### Installation

The archive holds two executables of the same program; take the one that suits the Amiga:

| Executable | For | Decodes itself |
|---|---|---|
| `amisqueeze` | any Amiga with a 68020 or better | raw PCM; FLAC when the CPU is fast enough |
| `amisqueeze.fpu` | an Amiga with an FPU: 68881 or 68882, or a 68040/68060 with its FPU (a PiStorm with Emu68, for instance) | the same, and MP3 when the CPU is fast enough |

`amisqueeze.fpu` refuses to start without an FPU (`amisqueeze.fpu needs an FPU`). Copy the chosen one anywhere,
ideally into a directory of the Shell's command path (for instance `C:`), so that it can be run by name; it may be
renamed `amisqueeze`. Both read the same default options (below).

### Running

From a Shell:

```
amisqueeze SERVER=192.168.1.10
```

Template: `SERVER,PORT/N/K,NAME/K,MAC/K,OUTPUT/K,UNIT/N/K,NOAUDIO/S,SWAP/S,FLAC/K,MP3/K,BENCH/S,VERBOSE/S`

| Option | Meaning | Default |
|---|---|---|
| `SERVER` | LMS server: IP address or host name (the `SERVER=` keyword may be omitted) | none: required, on the command line or in the default options |
| `PORT` | slimproto port of the server | `3483` |
| `NAME` | player name shown in LMS | `Amiga` |
| `MAC` | player identifier, `xx:xx:xx:xx:xx:xx` | derived from `NAME` |
| `OUTPUT` | file receiving the audio as it is played, for checking (see below); no sound with it | none |
| `UNIT` | AHI unit to play through (0 to 3, set in the AHI preferences) | `0` |
| `NOAUDIO` | no sound: the audio is received and consumed at the playing pace, as if played | off |
| `SWAP` | swaps left and right (for an AHI driver that swaps them, see below) | off |
| `FLAC` | `AUTO`: FLAC decoded on the Amiga if it is fast enough (measured at start-up), otherwise converted by the server; `ON`: always decoded here; `OFF`: always converted by the server (see "FLAC and MP3" below) | `AUTO` |
| `MP3` | the same for MP3, with `amisqueeze.fpu` only (`amisqueeze` leaves MP3 to the server) | `AUTO` |
| `BENCH` | measures how fast this Amiga decodes FLAC and MP3, prints it and quits (no server needed) | off |
| `VERBOSE` | print the messages exchanged with the server and the stream events | off |

`Ctrl-C` stops amisqueeze. To keep the Shell free, run it in the background: `Run >NIL: amisqueeze`, or keep its
messages in a file, written as they come: `Run amisqueeze >RAM:amisqueeze.log VERBOSE` (the redirection goes
right after the command name). `Status` then shows its process number, and `Break <number>` stops it.

LMS identifies players by their MAC address. Without `MAC`, amisqueeze derives a stable one from the name: the same
name always gives the same player in LMS, and two Amigas with different names show up as two players. The player
can also be renamed from the LMS web interface; amisqueeze takes the new name while it runs.

If the connection drops or the server restarts, amisqueeze reconnects by itself (it retries every 5 seconds).

**FLAC and MP3**: the player tells LMS, when it connects, which formats it decodes; LMS sends those files as they
are and converts the others. Decoding on the Amiga saves network bandwidth (FLAC about half of raw PCM, MP3 a fifth
or less), lets FLAC files above 48 kHz play (LMS lowers their rate in FLAC), and plays MP3 without any conversion
rule on the server; but it needs a fast CPU. With `FLAC=AUTO` and `MP3=AUTO` (the defaults), amisqueeze decodes a
built-in sample of each at start-up (the slowest cases: FLAC level 8, MP3 at 320 kbit/s; at most a second each)
and only takes the format if it runs at least twice real time; it prints the result, for instance `FLAC: level 8
decoded at 77.0x real time: FLAC decoded here`, then `MP3: 320 kbit/s decoded at 74.2x real time: MP3 decoded
here`. MP3 is decoded in floating point: only `amisqueeze.fpu` has the MP3 decoder. Measured in WinUAE: a 68020
without its JIT compiler (about 45 MHz) decodes FLAC at 0.2x real time, so FLAC stays with the server (its default
rule converts it to PCM, nothing to set up); with the JIT (the order of a PiStorm) FLAC at 77x, and MP3 at 74x with
a 68882. `amisqueeze BENCH` prints those measures without connecting; `FLAC=ON|OFF` and `MP3=ON|OFF` force the
choice. The measure is a little pessimistic on a CPU that compiles code on the fly (WinUAE's JIT, the PiStorm's
Emu68): the first pass also compiles. MP3 tracks are not yet joined without gaps: an MP3 file begins with a few
hundredths of a second of silence (the encoder's delay), as with a basic player.

**`OUTPUT=<file>`** writes the audio to a file as it is played: raw PCM exactly as the server sent it (for LMS
16-bit **little-endian** samples; 44.1 kHz stereo: about 10 MB per minute), or, for FLAC and MP3 decoded on the
Amiga, 16-bit samples in the Amiga's byte order (big-endian). The file is created (or replaced) when
amisqueeze starts and closed when it stops; everything played in between is appended, the next track right after
the previous one, nothing while paused. It is meant for checking playback: compare it with the expected PCM, e.g.
with `tools/pcm-compare.py` from the repository (in an emulator, write it to a volume shared with the host). With
`OUTPUT` there is no sound (as with `NOAUDIO`): the file holds exactly what was "played", which AHI could not
promise (it is given the audio about 0.6 s ahead, and a stop cuts that short). Avoid putting it in the default
options, or every run fills the disk.

### Sound (AHI)

amisqueeze plays through `ahi.device` (tested with AHI 6.x: `ahi.device` 6.6), on the unit given by `UNIT` (0 by
default). Set that unit in the AHI preferences (`SYS:Prefs/AHI`):

- **a "stereo++" mode**: in a plain "stereo" mode, AHI puts each sound entirely on one side, and a stereo stream
  plays on one side only;
- **at 44100 Hz** if the driver allows it (the rate LMS sends): otherwise AHI converts the rate. Paula is limited
  to about 28 kHz with a 15 kHz screen mode (27,429 Hz in PAL);
- 1 channel is enough (amisqueeze plays one sound at a time); more lets other programs play at the same time.

With Paula, AHI converts and mixes the sound with the CPU: on a 68020 at about 28 MHz (measured in WinUAE),
playback takes about two thirds of the CPU, 14-bit modes a little more than 8-bit ones; amisqueeze itself takes
only a few percent at full volume (see the volume below).

AHI is given about 0.6 s of audio ahead; pause, stop and seek cut the sound at once, and the playing time shown by
LMS follows what was actually played. If left and right come out swapped (seen in WinUAE with `paula.audio` 4.30,
in 8 and 14 bits, while Paula itself is right: not checked on a real Amiga yet), add `SWAP`. Occasional
crackles with `paula.audio` in WinUAE also occur without amisqueeze (AHI's own test tone).

**Volume**: amisqueeze follows the volume set in LMS (slider, apps, mute): it scales the samples itself (AHI always
plays them at full volume) and a change is heard 0.2 to 0.4 s later. LMS's volume scale is logarithmic: 100 is
full volume, 50 is quiet (about -25 dB, 6 % of full amplitude), 25 very quiet; mute and unmute fade in about a
second. LMS offers no "fixed volume" for this kind of player: for full volume, set it to 100. Below 100 the
scaling costs CPU time: on a 68020 at about 28 MHz with Paula, about a quarter of the CPU on top of AHI's two
thirds (measured in WinUAE). **On a slow Amiga, leave LMS at 100 and set the volume on the amplifier**: at 100 the
samples are not touched. **With Paula, the same advice holds for the sound**: Paula's 14 bits through AHI have
their own noise, which a low volume brings forward (a crackle heard at 50 in WinUAE, gone at 100). With `VERBOSE`,
each change prints `volume: 5.9 % of full amplitude`.

### Default options

To avoid typing the options every time, store them in the global variable **`amisqueeze.prefs`**, with exactly the
same syntax as the command line. amisqueeze reads it at startup from `ENV:amisqueeze.prefs`; a copy in `ENVARC:`
makes it survive a reboot (AmigaOS copies `ENVARC:` to `ENV:` when it starts).

Create it from a Shell:

```
Echo "SERVER=192.168.1.10 NAME=Amiga" >ENV:amisqueeze.prefs
Copy ENV:amisqueeze.prefs ENVARC:
```

Then simply run:

```
amisqueeze
```

Rules:

- **The command line wins**: `amisqueeze NAME=Kitchen` uses the server from the default options and the name
  `Kitchen`.
- `VERBOSE` set in the default options cannot be turned off from the command line: leave it out of the file and add
  it on the command line when needed.
- To change the options, edit the file (`Ed ENVARC:amisqueeze.prefs`, then copy it again to `ENV:`), or recreate it
  with `Echo` as above. To remove them: `Delete ENV:amisqueeze.prefs ENVARC:amisqueeze.prefs`.
- If the file contains an unknown option or a syntax error, amisqueeze stops with an error message naming
  `ENV:amisqueeze.prefs`.

### Messages

| Message | Meaning |
|---|---|
| `connected to the server` | the control connection is up; the player should appear in LMS |
| `no server: give SERVER=<ip\|name>, or set it in ENV:amisqueeze.prefs` | no server on the command line nor in the default options |
| `invalid MAC (expected xx:xx:xx:xx:xx:xx)` | the `MAC` option is not six hexadecimal pairs; amisqueeze stops |
| `FLAC=…: expected AUTO, ON or OFF` (or `MP3=…`) | a wrong value for that option; amisqueeze stops |
| `ENV:amisqueeze.prefs: …` | the default options cannot be read (unknown option, syntax error); amisqueeze stops |
| `bsdsocket.library not found (is a TCP/IP stack running?)` | no TCP/IP stack started |
| `server … not found` | the host name cannot be resolved |
| `cannot connect: error N` | the server does not answer on that address and port; amisqueeze retries every 5 s |
| `no news from the server, reconnecting` | the server stayed silent for 35 s; amisqueeze reconnects |
| `connection closed by the server` | the server closed the connection; amisqueeze reconnects |
| `not enough memory for the 1024 KB stream buffer` | not enough free memory to start |
| `stream format X not supported` | LMS sent a format other than raw PCM, FLAC or (`amisqueeze.fpu`) MP3 (no conversion rule for that format) |
| `FLAC: level 8 decoded at …x real time: …` | the start-up measure (`FLAC=AUTO`): whether FLAC is decoded here or converted by the server |
| `MP3: 320 kbit/s decoded at …x real time: …` | the same for MP3 (`MP3=AUTO`, `amisqueeze.fpu`) |
| `amisqueeze.fpu needs an FPU (…): use amisqueeze instead` | `amisqueeze.fpu` started on an Amiga without an FPU |
| `MP3=ON: this build does not decode MP3 …` | `MP3=ON` given to `amisqueeze`: MP3 stays with the server |
| `flac: … , track skipped` | a FLAC stream that cannot be decoded (not FLAC, no STREAMINFO, more than 2 channels): the next track follows |
| `mp3: no MP3 frame found, track skipped` | an MP3 stream without a single MP3 frame: the next track follows |
| `flac: not enough memory`, `mp3: not enough memory`, then `…: cannot decode this track, skipped` | not enough free memory for the decoder of that track: the next track follows |
| `the FLAC decoding speed cannot be measured` (or MP3) | `BENCH` could not run the measure (not enough memory) |
| `cannot open ahi.device unit N (is AHI installed? NOAUDIO plays without sound)` | AHI missing, or that unit cannot be opened; amisqueeze stops |
| `not enough memory for the audio blocks` | the 3 audio blocks (96 KB) cannot be allocated; amisqueeze stops |
| `cannot create <file>: error N` | the `OUTPUT` file cannot be created (missing directory, write-protected disk); amisqueeze stops. On a volume that is not mounted, AmigaDOS first asks for it to be inserted; this message follows a Cancel |
| `cannot write to <file>: error N, file closed` | writing the `OUTPUT` file failed (disk full…); playback goes on without the file |
| `stream: cannot connect: error N` | the audio stream port of LMS (HTTP, 9000 by default) cannot be reached |
| `stream: …`, `output: …` (with `VERBOSE`) | stream events: parameters, connection, headers received, end of the track data; output: AHI unit or virtual output, track started, buffer empty, end of playback, `OUTPUT` file opened and bytes written to it |

## amilms

The remote control is not available yet; see [its plan](development/phase-2-amilms.md).
