# Amiga Network Music - User guide

This guide is for people who **use** the programs. How it works and which servers it supports are in the
[README](../README.md#how-it-works); how to build them is in [CONTRIBUTING.md](../CONTRIBUTING.md), and the
development roadmap in [docs/development](development/README.md).

**amisqueeze**: [current state](#current-state) | [requirements](#requirements) | [server setup](#server-setup) |
[installation](#installation) | [running](#running) | [from the Workbench](#from-the-workbench) |
[stopping](#stopping-amisqueeze) | [decoding on the Amiga](#decoding-on-the-amiga-flac-mp3-bench) |
[writing to a file](#writing-the-audio-to-a-file-output) | [sound (AHI)](#sound-ahi) |
[default options](#default-options) | [messages](#messages) | [troubleshooting](#troubleshooting) |
[known limits](#known-limits). **amilms**: [not available yet](#amilms).

## amisqueeze

amisqueeze turns the Amiga into a network music player for the music servers that speak slimproto, the protocol of
Logitech's Squeezebox players: [Lyrion Music Server](https://lyrion.org) (LMS) and Music Assistant, both tested
(see [server setup](#server-setup)). The server (and its web interface or apps) sees and controls it like any other player.

### Current state

Version 0.7 in development. The latest release, 0.6, is a preview: tested in WinUAE only, not yet on a real Amiga.

What amisqueeze does:

- **connects to the server** and shows up as a player;
- **plays what LMS plays, through AHI**: in LMS, playback progresses, moves on to the next track, pauses, seeks,
  stops and changes volume as with any player;
- **plays raw PCM**, which LMS makes from the files: 8, 16, 24 or 32-bit samples (24-bit "high resolution" FLAC
  included), mono or stereo, up to 48 kHz, converted to 16 bits for AHI;
- **decodes FLAC itself** on a fast Amiga, and **MP3** too with an FPU (see
  [decoding on the Amiga](#decoding-on-the-amiga-flac-mp3-bench)).

The sound has been checked by ear in WinUAE, with Paula and with a 16-bit sound card (Toccata).

### Requirements

On the Amiga:

- AmigaOS 3.x, 68020 or better (from a plain 68020 accelerator to a PiStorm). Tested with 3.2.3; also with 3.0
  and 3.1 (Kickstart 39 and 40) from a Shell, without AHI; it even starts with 2.04 (Kickstart 37), the oldest
  version it asks for;
- about 1.1 MB of free memory (1 MB stream buffer, about 6 s of CD-quality audio), a little more for
  `amisqueeze.fpu` when it decodes MP3 (about 60 KB: its decoder and a larger stack); both set up the stack they
  need themselves, whatever the Shell's `Stack`;
- a TCP/IP stack providing `bsdsocket.library`: Roadshow, AmiTCP, AmiNetXDuo, or WinUAE's emulation
  (`bsdsocket_emu=true`) in an emulator;
- for the sound: AHI (tested with 6.x; 4.x expected to work, not checked yet) and a driver: Paula's
  (`paula.audio`, no sound card needed) or your sound card's; see [sound](#sound-ahi).

On the server:

- Lyrion Music Server (tested with 9.1.1), reachable from the Amiga on TCP ports 3483 (players) and 9000 (audio
  streams; or whatever its HTTP port is: LMS tells the player which one);
- for MP3 files, unless `amisqueeze.fpu` decodes them (see
  [decoding on the Amiga](#decoding-on-the-amiga-flac-mp3-bench)): a conversion rule from MP3 to raw PCM, see
  [server setup](#server-setup). FLAC, WAV and AIFF are converted by LMS's default rules;
- files above 48 kHz (88.2, 96, 192 kHz) play only when amisqueeze decodes FLAC itself (LMS then lowers their rate
  in FLAC); when the Amiga is too slow for that, LMS cannot lower their rate as it makes raw PCM, and reports
  "Couldn't create command line for flc playback" in its log.

**What has been tried.** Everything so far was run in WinUAE; what the list above names but this table does not is
expected to work, not checked:

| | Tried | Expected, not tried yet |
|---|---|---|
| Machine | WinUAE 6.0 (an A1200 setup) | any real Amiga |
| CPU | 68020, at about 28 MHz and with the JIT (the speed of a PiStorm, roughly); with and without a 68882 | 68030, 68040, 68060, a PiStorm with Emu68 |
| AmigaOS | 3.2.3; 3.0 and 3.1 from a Shell without AHI; 2.04 starts | 3.1.4, 3.5, 3.9 |
| TCP/IP | WinUAE's emulation (`bsdsocket_emu`) | Roadshow, AmiTCP, AmiNetXDuo |
| Sound | AHI 6.6 with `paula.audio` and a Toccata card | AHI 4.x, other cards, a PiStorm's own outputs |
| Server | Lyrion Music Server 9.1.1, Music Assistant 2.10.7 | other versions |

### Server setup

Lyrion Music Server needs little for amisqueeze:

- **Its address**, given to amisqueeze with `SERVER` (amisqueeze does not find the server by itself yet): the
  address in the browser's address bar when LMS's web page is open on another computer (`http://<address>:9000`),
  or the computer's own network settings; a fixed address (set in the router) saves changing it later. A host name
  works too, if the Amiga's TCP/IP stack can resolve it.
- **Ports** reachable from the Amiga: 3483/tcp (players) and the web port, 9000/tcp by default, which also carries
  the audio streams (LMS tells the player which one; set `HTTP_PORT` if you move it, e.g. in Docker). Open both
  in the server's firewall; in Docker, publish both.
- **MP3 to PCM conversion**, only when amisqueeze does not decode MP3 itself: with `amisqueeze`, or with
  `amisqueeze.fpu` on a CPU too slow for it (see [decoding on the Amiga](#decoding-on-the-amiga-flac-mp3-bench)).
  LMS 9.1 converts FLAC, WAV and AIFF by its default rules, but has **none for MP3** (regular players decode MP3
  themselves): without one, its `server.log` says "Couldn't create command line for mp3 playback" and the player
  receives nothing. Add this rule to a
  `custom-convert.conf` file (in the LMS root directory or at the root of a plugins directory), then restart LMS:

  ```
  mp3 pcm * *
  	# FT:{START=trim %s}
  	[sox] -q -t mp3 $FILE$ -t raw -c 2 -2 -s - $START$
  ```

  It uses the `sox` program shipped with LMS, as its built-in `ogg pcm` rule does, but without `-r`: LMS tells the
  player the file's own rate (forcing 44100 Hz would play 48 kHz files 9 % too fast). `FT:{START=trim %s}` lets
  LMS start within a track (seeking).
- **Music Assistant** (tried with 2.10.7): it drives amisqueeze through its Squeezelite provider. It transcodes
  every track to the player's output codec (in the player's settings), so choose one that amisqueeze plays:

  | Output codec | With amisqueeze |
  |---|---|
  | `flac` (the default) | the best choice: lossless, gapless; needs a fast Amiga (see [decoding on the Amiga](#decoding-on-the-amiga-flac-mp3-bench)) |
  | `wav` | for a slow Amiga (a 68020 without FPU): no decoding, but about 2 Mbit/s on the network (24 bits) |
  | `mp3` | only with `amisqueeze.fpu`; Music Assistant encodes it on the fly without the information that makes MP3 gapless: the change of track can be heard, and some sounds get small artefacts from the second lossy encoding |
  | `aac` | not played: amisqueeze refuses the stream, but Music Assistant still shows the track as playing |

  With `amisqueeze` (no FPU), `mp3` is refused like `aac`.

### Installation

The archive comes from the project's [releases](https://github.com/shadow38/amiga-network-music/releases); each
release gives its SHA-256 (in its notes and in a `.sha256` file), to check the archive before unpacking it, for
instance with `sha256sum` on Linux or `Get-FileHash` in PowerShell. It holds two executables of the same program;
take the one that suits the Amiga:

| Executable | For | Decodes itself |
|---|---|---|
| `amisqueeze` | any Amiga with a 68020 or better | raw PCM; FLAC when the CPU is fast enough |
| `amisqueeze.fpu` | an Amiga with an FPU: 68881 or 68882, or a 68040/68060 with its FPU (a PiStorm with Emu68, for instance) | the same, and MP3 when the CPU is fast enough |

`amisqueeze.fpu` refuses to start without an FPU (`amisqueeze.fpu needs an FPU`). Copy the chosen one anywhere,
with its icon (`amisqueeze.info` or `amisqueeze.fpu.info`) if it is to be started from the Workbench: ideally into
a directory of the Shell's command path (for instance `C:`) so that it can be run by name, or into a drawer of
your choice. It may be renamed `amisqueeze` (rename its icon the same way). Both read the same default options
(below).

**Removing it**: delete the executable and its icon (from `SYS:WBStartup` too, if it was put there), and the
default options if any: `Delete ENV:amisqueeze.prefs ENVARC:amisqueeze.prefs`. amisqueeze writes nothing else,
apart from the `LOG` and `OUTPUT` files you named.

### Running

From a Shell:

```
amisqueeze SERVER=192.168.1.10
```

Template: `SERVER,PORT/N/K,NAME/K,MAC/K,OUTPUT/K,LOG/K,UNIT/N/K,NOAUDIO/S,SWAP/S,FLAC/K,MP3/K,BENCH/S,VERBOSE/S`

| Option | Meaning | Default |
|---|---|---|
| `SERVER` | LMS server: IP address or host name (the `SERVER=` keyword may be omitted) | none: required, on the command line or in the default options |
| `PORT` | slimproto port of the server | `3483` |
| `NAME` | player name shown in LMS | `Amiga` |
| `MAC` | player identifier, `xx:xx:xx:xx:xx:xx` | derived from `NAME` |
| `OUTPUT` | file receiving the audio as it is played, for checking (see [writing the audio to a file](#writing-the-audio-to-a-file-output)); no sound with it | none |
| `LOG` | file receiving a copy of every message, appended to at each run (see below) | none |
| `UNIT` | AHI unit to play through (0 to 3, set in the AHI preferences) | `0` |
| `NOAUDIO` | no sound: the audio is received and consumed at the playing pace, as if played | off |
| `SWAP` | swaps left and right (for an AHI driver that swaps them, see [sound](#sound-ahi)) | off |
| `FLAC` | `AUTO`: FLAC decoded on the Amiga if it is fast enough (measured at start-up), otherwise converted by the server; `ON`: always decoded here; `OFF`: always converted by the server (see [decoding on the Amiga](#decoding-on-the-amiga-flac-mp3-bench)) | `AUTO` |
| `MP3` | the same for MP3, with `amisqueeze.fpu` only (`amisqueeze` leaves MP3 to the server) | `AUTO` |
| `BENCH` | measures how fast this Amiga decodes FLAC and MP3, prints it and quits (no server needed) | off |
| `VERBOSE` | print the messages exchanged with the server and the stream events | off |

`Ctrl-C` stops amisqueeze. To keep the Shell free, run it in the background: `Run >NIL: amisqueeze`, or keep its
messages in a file, written as they come: `Run amisqueeze >RAM:amisqueeze.log VERBOSE` (the redirection goes
right after the command name). `Status` then shows its process number, and `Break <number>` stops it; Exchange can
stop it too (see [stopping amisqueeze](#stopping-amisqueeze)). Only one amisqueeze runs at a time: a second one
says `amisqueeze is already running` and quits.

**`LOG=<file>`** keeps a copy of every message in a file while they still appear in the window (a redirection
`>file` sends them to the file only), from the moment the file is open: an error found while the options are read (an unknown keyword, a bad number)
shows in the window only. Each run is appended to the file, after a line with the date and time, for
instance `--- amisqueeze 0.5 (...), started 20-Oct-26 14:32:05`; the file is written as the messages come, so it can
be read while amisqueeze runs (`Type`). It can be set in the default options too: a lasting trace of what
happened, handy when something goes wrong. A log that cannot be opened (missing directory...) is said, and amisqueeze
plays without it. The file only grows: without `VERBOSE` a run adds a few lines; avoid leaving `VERBOSE` on with
`LOG` for good, and delete the file now and then.

LMS identifies players by their MAC address. Without `MAC`, amisqueeze derives a stable one from the name: the same
name always gives the same player in LMS, and two Amigas with different names show up as two players. The player
can also be renamed from the LMS web interface; amisqueeze takes the new name while it runs.

If the connection drops or the server restarts, amisqueeze reconnects by itself (it retries every 5 seconds).

### From the Workbench

Double-click the icon of `amisqueeze` (or `amisqueeze.fpu`). A window titled "amisqueeze" opens at its first
message and shows the messages as a Shell would; the player then shows up in LMS.

![A Workbench drawer holding the two programs, amisqueeze.fpu and amisqueeze, each with a loudspeaker
icon](images/workbench-icons.png)

*The two programs in a drawer, with their icons.*

![The amisqueeze window: the FLAC speed measured at start-up, the player's name and server, then "connected to
the server"](images/workbench-window.png)

*Started from its icon: the message window, here with the options of `ENV:amisqueeze.prefs` (`SERVER=127.0.0.1
NAME=Amiga`, a test setup). Without `VERBOSE`, nothing more is printed while it plays.*

**Options in the icon.** Select the icon, then *Icons > Information...* (Amiga-I): its **ToolTypes** take the same
options as the command line (every option of the [table above](#running)), one per line: `SERVER=192.168.1.10`, `NAME=Kitchen`, `UNIT=0`, `LOG=...`, `VERBOSE`... A
ToolType in parentheses is disabled: the icon comes with examples written that way, such as
`(SERVER=192.168.1.10)`; remove the parentheses to use one. The ToolTypes win over the default options of
`ENV:amisqueeze.prefs`, which still apply to what they leave out. Two ToolTypes are for the Workbench only:

| ToolType | Meaning |
|---|---|
| `WINDOW=CON:...` | the message window: position, size, title, e.g. `CON:0/20/640/120/amisqueeze/AUTO/CLOSE/WAIT/SMART` (the default) |
| `QUIET` | no window: amisqueeze runs silently; add `LOG=<file>` to keep its messages. An error at start-up (in the ToolTypes or the default options, a log that cannot be opened, no TCP/IP stack) still opens the window, which stays until it is closed; an invalid `MAC` or an `OUTPUT` file that cannot be created goes to the log only |

Other ToolTypes, such as the Workbench's own `DONOTWAIT`, are left alone. An error in the ToolTypes (an unknown
value, a bad number) is shown in the window, which stays open until it is closed.

**Starting with the Amiga.** Copy the program and its icon into `SYS:WBStartup`: the Workbench starts it at every
boot (the icon carries `DONOTWAIT`, so the Workbench does not wait for it). With `QUIET` and `LOG`, it then plays in
the background without a window. From a Shell script instead, add to `S:User-Startup`, once the TCP/IP stack is up:
`Run >NIL: amisqueeze`.

### Stopping amisqueeze

amisqueeze is a **commodity**: while it runs, it shows in **Exchange** (`SYS:Tools/Commodities/Exchange`; once
Exchange runs, Ctrl-Alt-Help brings its window back), the system's list of background programs, as "amisqueeze",
"Squeezebox network music player". To stop it, however it was started:

- in **Exchange**: select "amisqueeze", then **Remove**; this is the way to stop it when it runs without a window
  (`QUIET`);
- in its **window** (started from the Workbench) or its **Shell**: **Ctrl-C**;
- from another Shell: `Status` gives its process number (when started from a Shell), then `Break <number>`.

![Exchange: amisqueeze selected in the list of commodities, its description on the right, the Remove button
below](images/exchange.png)

*Exchange with amisqueeze selected: **Remove** stops it.*

It always stops cleanly: the sound stops, the connection is closed, `stopped` is printed. Its window then stays
open until it is closed, so that the last messages can be read. Exchange's Active/Inactive switch is shown but does
not affect the music. Starting it again from its icon while it runs does nothing: the running one goes on, and the
new one quits at once.

### Decoding on the Amiga (`FLAC`, `MP3`, `BENCH`)

When it connects, amisqueeze tells LMS which formats it decodes itself. LMS sends those files as they are, and
converts the others to raw PCM.

Decoding on the Amiga:

- saves network bandwidth: FLAC takes about half of raw PCM, MP3 a fifth or less;
- plays FLAC files above 48 kHz (LMS lowers their rate in FLAC);
- plays MP3 with no conversion rule on the server.

But it needs a fast CPU, and MP3 also needs an FPU: only `amisqueeze.fpu` has the MP3 decoder.

**Internet radios**: some MP3 radios mix their metadata (the title playing) into the stream, and amisqueeze
does not handle it yet. When LMS has the player fetch such a radio itself, as it may for a player that decodes
MP3 (`amisqueeze.fpu`), that metadata reaches the decoder and the sound may cut regularly; the message
`cont not handled` is then expected. Not yet checked with a real radio.

**At start-up**, with `FLAC=AUTO` and `MP3=AUTO` (the defaults), amisqueeze decodes a built-in sample of each
format in its slowest case (FLAC level 8, MP3 at 320 kbit/s), for at most a second each. It takes a format only if
it decodes it at least twice as fast as real time, and prints the result:

```
FLAC: level 8 decoded at 77.0x real time: FLAC decoded here
MP3: 320 kbit/s decoded at 74.2x real time: MP3 decoded here
```

To decide yourself:

- `FLAC=ON`, `MP3=ON`: always decoded on the Amiga, whatever the measure;
- `FLAC=OFF`, `MP3=OFF`: always left to the server;
- `amisqueeze BENCH`: prints the measures and quits, without connecting.

For instance `amisqueeze BENCH` on a 68020 without FPU:

```
FLAC level 8 (the slowest) decoded at 0.2x real time: FLAC=AUTO leaves FLAC to the server (converted to PCM)
MP3: not decoded by this build (amisqueeze.fpu decodes it, on an Amiga with an FPU)
```

and `amisqueeze.fpu BENCH` on a fast Amiga:

```
FLAC level 8 (the slowest) decoded at 77.0x real time: FLAC=AUTO lets this machine decode FLAC
MP3 at 320 kbit/s (the slowest) decoded at 74.2x real time: MP3=AUTO lets this machine decode MP3
```

Measured in WinUAE:

| Emulated CPU | FLAC | MP3 (`amisqueeze.fpu`) | Result |
|---|---|---|---|
| 68020 without JIT (about 45 MHz) | 0.2x | - | FLAC left to the server (converted by its default rule, nothing to set up) |
| 68020 with JIT and 68882 (the order of a PiStorm) | 77x | 74x | both decoded on the Amiga |

Good to know:

- the measure is a little pessimistic on a CPU that compiles code on the fly (WinUAE's JIT, the PiStorm's Emu68):
  its first pass also compiles;
- MP3 tracks follow each other without a gap (a live album, a mix): amisqueeze reads the header the encoder
  writes in the first frame (LAME, and the encoders that copy its format) and drops the silence the encoder adds
  at the start and the end of each file. MP3 files without such a header keep these few hundredths of a second.

### Writing the audio to a file (`OUTPUT`)

`OUTPUT=<file>` writes the audio to a file as it is played. It is meant for checking playback, not for listening:
**there is no sound with it**, as with `NOAUDIO`.

What the file holds:

- raw PCM exactly as the server sent it: for LMS, 16-bit **little-endian** samples (44.1 kHz stereo: about 10 MB
  per minute);
- for FLAC and MP3 decoded on the Amiga: 16-bit samples in the Amiga's byte order (big-endian).

How it is written:

- created (or replaced) when amisqueeze starts, closed when it stops;
- everything played in between is appended, the next track right after the previous one; nothing while paused.

Why no sound: the file holds exactly what was played, which AHI could not promise (AHI receives the audio about
0.6 s ahead, and a stop cuts that short).

To check the file, compare it with the expected PCM, for instance with `tools/pcm-compare.py` from the repository.
In an emulator, write it to a volume shared with the host.

Avoid putting `OUTPUT` in the default options: every run would fill the disk.

### Sound (AHI)

amisqueeze plays through **AHI** (Audio Hardware Interface), the standard audio system of the Amiga: programs
play through AHI, and an AHI driver drives the sound hardware, Paula (the Amiga's own sound chip) or a sound card.
AHI is free, on Aminet, but **not part of AmigaOS 3.x**: an Amiga that only ever played games and demos usually
does not have it.

#### Installing AHI

If AHI is not installed yet (no `DEVS:ahi.device`, no `SYS:Prefs/AHI`), from [Aminet](https://aminet.net):

| Archive | What it holds |
|---|---|
| `driver/audio/m68k-amigaos-ahiusr.lha` | AHI 6.0 itself (`ahi.device`, the preferences); needs a 68020 or better |
| `driver/audio/m68k-amigaos-ahidrv.lha` | the drivers, among them `paula.audio` (Paula) |
| `util/libs/mui38usr.lha` | MUI, which the AHI 6 preferences need (skip it if MUI is installed) |

AHI 6 has no installer: copy its files as its readme (`ahiusr.readme`) describes. A sound card usually comes with
its own AHI driver: follow its documentation. amisqueeze was tested with AHI 6 (`ahi.device` 6.6); AHI 4 is
expected to work, not checked yet.

#### Setting up the unit

amisqueeze plays on the AHI unit given by `UNIT` (0 by default). In the AHI preferences (`SYS:Prefs/AHI`), choose
for that unit:

- **a "stereo++" mode**, for instance **`Paula:14 bit stereo++`** with Paula (the name may end with "calibrated"),
  or your card's "16 bit stereo++" mode. In a plain "stereo" mode (without "++"), AHI puts each sound entirely on
  one side, and the music plays on one side only;
- **the highest frequency**: 44100 Hz if the driver allows it (the rate LMS sends; otherwise AHI converts the
  rate). Paula's maximum is about 28 kHz with a 15 kHz screen mode (27,429 Hz in PAL);
- 1 channel is enough (amisqueeze plays one sound at a time); more lets other programs play at the same time.

Then save. With Paula, `Paula:8 bit stereo++` takes a little less CPU than the 14-bit mode, at a lower quality;
the "HiFi" modes take more.

At start-up, amisqueeze reads the unit's mode from these preferences and warns if it would not play the music
right: a stereo mode without "++" (one side only) or a mono mode. With `VERBOSE`, it prints the mode and frequency
it plays with, for instance `output: AHI unit 0 (Paula:14 bit stereo++ calibrated, 27429 Hz)`.

![The AHI preferences: unit 0 chosen at the top left, the mode Paula:14 bit stereo++ selected in the list, the
frequency at 27429 Hz and 1 channel on the right](images/ahi-prefs-unit0.png)

*The AHI preferences (AHI 6, with MUI) set for amisqueeze with Paula: unit 0, `Paula:14 bit stereo++`, the
highest frequency, 1 channel.*

#### How it plays

With Paula, AHI converts and mixes the sound with the CPU: on a 68020 at about 28 MHz (measured in WinUAE),
playback takes about two thirds of the CPU, 14-bit modes a little more than 8-bit ones; amisqueeze itself takes
only a few percent at full volume (see [volume](#volume)).

AHI is given about 0.6 s of audio ahead; pause, stop and seek cut the sound at once, and the playing time shown by
LMS follows what was actually played. If left and right come out swapped (seen in WinUAE with `paula.audio` 4.30,
in 8 and 14 bits, while Paula itself is right: not checked on a real Amiga yet), add `SWAP`. Occasional
crackles with `paula.audio` in WinUAE also occur without amisqueeze (AHI's own test tone).

#### Volume

amisqueeze follows the volume set in LMS (slider, apps, mute). It scales the samples itself, since AHI always plays
them at full volume; a change is heard 0.2 to 0.4 s later.

LMS's volume scale is logarithmic:

| LMS volume | Level |
|---|---|
| 100 | full volume: the samples are not touched |
| 50 | quiet: about -25 dB, 6 % of full amplitude |
| 25 | very quiet: about -37 dB, 1.4 % |

Mute and unmute fade in about a second.

**Fixed volume.** In LMS, the player's settings (*Settings*, the player, then *Audio*) have a **Volume Control**
setting: choose **Output level is fixed at 100%**. amisqueeze then plays at full volume whatever the slider
(which LMS's interfaces grey out or hide), and the volume is set on the amplifier. LMS keeps that choice for the
player. **Use it on a slow Amiga, or with Paula.** Below 100, scaling the samples:

- costs CPU time: on a 68020 at about 28 MHz with Paula, about a quarter of the CPU on top of AHI's two thirds
  (measured in WinUAE);
- brings out Paula's noise: Paula's 14 bits through AHI have their own noise, which a low volume brings forward (a
  crackle heard at 50 in WinUAE, gone at 100).

The same page also shows an **Audio Startup Time** setting, meant for receivers fed by a digital output: leave it
at its default (0).

With `VERBOSE`, each change prints `volume: 5.9 % of full amplitude`, or `volume: fixed at 100 % by LMS (samples
untouched)`.

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
| `player "..." (xx:xx:xx:xx:xx:xx), server ... port ...` | printed at every start: the player's name and MAC address as LMS will see them, and the server it connects to |
| `connected to the server` | the control connection is up; the player should appear in LMS |
| `player name: ...` | the player was renamed on the server (LMS web interface): amisqueeze takes the new name |
| `stopped` | amisqueeze has stopped cleanly (Ctrl-C, `Break`, Remove in Exchange) |
| `no server: give SERVER=<ip\|name>, or set it in ENV:amisqueeze.prefs` | no server on the command line nor in the default options |
| `invalid MAC (expected xx:xx:xx:xx:xx:xx)` | the `MAC` option is not six hexadecimal pairs; amisqueeze stops |
| `FLAC=...: expected AUTO, ON or OFF` (or `MP3=...`) | a wrong value for that option; amisqueeze stops |
| `amisqueeze: <AmigaDOS error>`, e.g. `bad number` | the command line does not fit the template (a bad number for `PORT` or `UNIT`, an unknown keyword...); the text is AmigaDOS's own; amisqueeze stops |
| `ENV:amisqueeze.prefs: ...` | the default options cannot be read (unknown option, syntax error); amisqueeze stops |
| `bsdsocket.library not found (is a TCP/IP stack running?)` | no TCP/IP stack started |
| `server ... not found` | the host name cannot be resolved |
| `cannot connect: error N` | the server does not answer on that address and port; amisqueeze retries every 5 s |
| `retrying in 5 s` | after a failed connection, or a connection lost less than 10 s after it was made: amisqueeze waits 5 s before trying again (after a longer session it reconnects at once) |
| `socket: error N` | the TCP/IP stack cannot create a connection (out of resources?); amisqueeze retries every 5 s |
| `send: error N`, `recv: error N` | the connection to the server broke; amisqueeze reconnects |
| `WaitSelect: error N` | waiting on the network failed (TCP/IP stack error); amisqueeze closes the connection and reconnects |
| `no news from the server, reconnecting` | the server stayed silent for 35 s; amisqueeze reconnects |
| `connection closed by the server` | the server closed the connection; amisqueeze reconnects |
| `not enough memory for the 1024 KB stream buffer` | not enough free memory to start |
| `stream format X not supported` | LMS sent a format other than raw PCM, FLAC or (`amisqueeze.fpu`) MP3 (no conversion rule for that format) |
| `FLAC: level 8 decoded at ...x real time: ...` | the start-up measure (`FLAC=AUTO`): whether FLAC is decoded here or converted by the server |
| `MP3: 320 kbit/s decoded at ...x real time: ...` | the same for MP3 (`MP3=AUTO`, `amisqueeze.fpu`) |
| `FLAC level 8 (the slowest) decoded at ...x real time: FLAC=AUTO lets this machine decode FLAC` (or `leaves FLAC to the server`), the same for MP3 | the output of `BENCH` |
| `MP3: not decoded by this build (...)` | `BENCH` run with `amisqueeze`, which has no MP3 decoder |
| `amisqueeze.fpu needs an FPU (...): use amisqueeze instead` | `amisqueeze.fpu` started on an Amiga without an FPU |
| `MP3=ON: this build does not decode MP3 ...` | `MP3=ON` given to `amisqueeze`: MP3 stays with the server |
| `flac: ... , track skipped` | a FLAC stream that cannot be decoded (not FLAC, no STREAMINFO, a sample rate of 0, more than 2 channels; after a seek, `cannot start from the middle of this file`: a rare file whose frames leave their format to its header): the next track follows |
| `mp3: no MP3 frame found, track skipped` | an MP3 stream without a single MP3 frame: the next track follows |
| `wav: ..., track skipped` | a WAV header that amisqueeze cannot play (`not integer PCM`: floating point or compressed; `more than 2 channels`; `sample size not supported`: other than 8, 16, 24 or 32 bits; `sample rate of 0`; `no format chunk`; a header cut short, too short or longer than 4 KB): the next track follows |
| `flac: not enough memory`, `mp3: not enough memory`, then `...: cannot decode this track, skipped` | not enough free memory for the decoder of that track: the next track follows |
| `the FLAC decoding speed cannot be measured` (or MP3) | `BENCH` could not run the measure (not enough memory) |
| `cannot open ahi.device unit N (is AHI installed? NOAUDIO plays without sound)` | AHI missing, or that unit cannot be opened; amisqueeze stops |
| `AHI unit N uses "...", a stereo mode without "++": the music will play on one side only; ...` | the unit is set to a plain stereo mode: choose a "stereo++" one in `SYS:Prefs/AHI`; amisqueeze plays anyway |
| `AHI unit N uses "...", a mono mode: the music will play in mono` | the unit is set to a mono mode; amisqueeze plays anyway |
| `not enough memory for the audio blocks` (or `for the audio output`) | the 3 audio blocks (96 KB) or the AHI requests cannot be allocated; amisqueeze stops |
| `timer.device unavailable` | the system timer cannot be opened (should not happen); amisqueeze stops |
| `strm ... not handled`, `strm too short`, `... not handled (N bytes)`, `packet too short`, `invalid packet size` | the server sent a message amisqueeze does not know or that is malformed: it is ignored (an invalid size closes the connection, and amisqueeze reconnects). Worth reporting, with the server's name and version; `cont not handled` is expected with some internet radios (see [decoding on the Amiga](#decoding-on-the-amiga-flac-mp3-bench)) |
| `stream: request too long`, `stream: response headers too long` | the audio stream's HTTP request or answer exceeds what amisqueeze handles (1 KB, 2 KB): that track is not played |
| `cannot create <file>: error N` | the `OUTPUT` file cannot be created (missing directory, write-protected disk); amisqueeze stops. On a volume that is not mounted, AmigaDOS first asks for it to be inserted; this message follows a Cancel |
| `cannot write to <file>: error N, file closed` | writing the `OUTPUT` file failed (disk full...); playback goes on without the file |
| `cannot open the log <file>: error N, going on without it` | the `LOG` file cannot be opened (missing directory, write-protected disk); amisqueeze plays without a log |
| `amisqueeze is already running (Exchange lists it and can stop it)` | started from a Shell while another amisqueeze runs: the new one quits (from the Workbench it quits without a word) |
| `the icon's ToolTypes: ...` | started from the Workbench, a ToolType cannot be read (unknown value, bad number...); shown in the window, which stays open |
| `the icon's ToolTypes are too long` | the ToolTypes together exceed what amisqueeze reads (about 500 characters) |
| `cannot write to the log <file>: error N, log closed` | writing the `LOG` file failed (disk full...); playback goes on without the log |
| `stream: cannot connect: error N` | the audio stream port of LMS (HTTP, 9000 by default) cannot be reached |
| `stream: connection lost before the response headers`, `stream: send: error N` | the audio stream broke before any music came: that track is not played |
| `stream: recv: error N` | the audio stream broke during a track: what was received plays, then the next track follows |
| other `stream: ...` and `output: ...` messages (with `VERBOSE` only) | stream events: parameters, connection, headers received, end of the track data; output: AHI unit or virtual output, track started, buffer empty, end of playback, `OUTPUT` file opened and bytes written to it |
| `<- strm ...`, `-> STAT ...`, `-> DSCO ...`, `<- audg ...`, `volume: ...`, the version line (with `VERBOSE` only) | the messages exchanged with the server, the volume it sets |

### Troubleshooting

- **The player does not show up in LMS.** Look at the messages (start amisqueeze from a Shell, or add `LOG`):
  `bsdsocket.library not found` means no TCP/IP stack is running; `cannot connect` means nothing answers at that
  address: check `SERVER` (see [server setup](#server-setup)), that LMS runs, and that the server's firewall lets
  3483/tcp in. `connected to the server` means the connection is up.
- **It connects, but a track does not play or is skipped at once.** `stream: cannot connect` means the server's
  web port (9000/tcp) is blocked. `stream format X not supported`, or nothing at all, with "Couldn't create command
  line" in LMS's `server.log`: LMS has no rule to convert that file to raw PCM, typically MP3 with `amisqueeze`
  (add the rule of the [server setup](#server-setup), or use `amisqueeze.fpu` on an Amiga with an FPU), or a file
  above 48 kHz when FLAC is not decoded here.
- **The music plays on one side only.** The AHI unit uses a plain "stereo" mode: choose a "stereo++" one
  ([setting up the unit](#setting-up-the-unit)); amisqueeze warns about it at start-up. Left and right swapped:
  add `SWAP`.
- **The sound stutters or stops now and then.** The Amiga does not keep up: run `amisqueeze BENCH`, and if FLAC or
  MP3 is decoded here at less than a few times real time, give `FLAC=OFF` or `MP3=OFF` so that the server converts
  them (see [decoding on the Amiga](#decoding-on-the-amiga-flac-mp3-bench)); with Paula, an 8-bit mode costs less
  CPU; close other busy programs. Raw PCM needs about 1.4 Mbit/s for CD quality: a slow network can starve it.
- **Crackles at low volume with Paula.** Paula's noise shows when the samples are scaled down: keep LMS's volume
  high and turn the amplifier down instead (see [volume](#volume)).
- **Nothing happens from the Workbench.** With `QUIET`, add `LOG=<file>` to see the messages. If amisqueeze is
  already running (Exchange lists it), a second start does nothing.
- **`amisqueeze.fpu needs an FPU`**: use `amisqueeze`.

### Known limits

- Tried in WinUAE only, not yet on a real Amiga (see [what has been tried](#requirements)).
- The server's address must be given (`SERVER`): amisqueeze does not find the server by itself yet.
- MP3 is decoded only by `amisqueeze.fpu`; with `amisqueeze`, the server must convert it (a rule to add, see
  [server setup](#server-setup)). Other formats play only through LMS's conversion to raw PCM.
- Files above 48 kHz play only when FLAC is decoded on the Amiga.
- Internet radios that mix their metadata into the stream may cut regularly with `amisqueeze.fpu` (see
  [decoding on the Amiga](#decoding-on-the-amiga-flac-mp3-bench)).
- Synchronising players (LMS's player groups) is not supported, nor tried.
- With Music Assistant, the `aac` output codec does not play, nor `mp3` with `amisqueeze`; its `mp3` is not
  gapless (see [server setup](#server-setup)).
- With `QUIET`, an invalid `MAC` or an `OUTPUT` file that cannot be created is reported in the log only.

## amilms

The remote control is not available yet; see [its plan](development/phase-2-amilms.md).
