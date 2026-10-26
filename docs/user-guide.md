# amisqueeze - User guide

This guide tells how to set up and use amisqueeze, then lists every option, message and known limit. What
amisqueeze is and what the Amiga needs: the [README](../README.md).

Version 0.8 in development. The latest release, 0.7, is a preview: tried in WinUAE only, not yet on a real Amiga
(see [what has been tried](#what-has-been-tried)).

**Using amisqueeze**: [installing](#installing) | [sound (AHI)](#sound-ahi) | [server setup](#server-setup) |
[starting](#starting-amisqueeze) | [stopping](#stopping-amisqueeze) | [volume](#volume).
**Reference**: [options](#options) | [ToolTypes](#tooltypes) | [default options](#default-options) |
[messages](#messages) | [troubleshooting](#troubleshooting) | [known limits](#known-limits) |
[what has been tried](#what-has-been-tried) | [removing amisqueeze](#removing-amisqueeze).

## Using amisqueeze

In order: install amisqueeze, set up AHI, set up the server, then start amisqueeze.

### Installing

Download the archive from the project's [releases](https://github.com/shadow38/amiga-network-music/releases). It
comes in two forms holding the same files:

- the `.lha`, to unpack on the Amiga itself with LhA;
- the zip, to unpack on a PC.

AmigaOS 3.2 has no LhA. Aminet's `util/arc/lha.run` provides it: run it, and it unpacks itself into the current
directory. Copy the one for your CPU (`lha_68k`, `lha_68020` or `lha_68040`) to `C:LhA`, then unpack amisqueeze:

```
LhA x amisqueeze-<version>.lha RAM:
```

Each release gives the SHA-256 of both archives, in its notes and in `.sha256` files. To check an archive before
unpacking it: `sha256sum` on Linux, `Get-FileHash` in PowerShell.

The archive holds two executables of the same program. Take the one that suits the Amiga:

| Executable | For | Decodes itself |
|---|---|---|
| `amisqueeze` | any Amiga with a 68020 or better | raw PCM; FLAC when the CPU is fast enough |
| `amisqueeze.fpu` | an Amiga with an FPU: 68881 or 68882, or a 68040/68060 with its FPU (a PiStorm with Emu68, for instance) | the same, and MP3 when the CPU is fast enough |

Copy it with its icon (`amisqueeze.info` or `amisqueeze.fpu.info`):

- into `C:`, or another directory of the Shell's command path, to run it by name from a Shell;
- into any drawer, to start it from the Workbench.

`amisqueeze.fpu` may be renamed `amisqueeze`; rename its icon the same way.

### Sound (AHI)

amisqueeze plays through **AHI**, the standard audio system of the Amiga. Programs play through AHI, and an AHI
driver drives the sound hardware: Paula (the Amiga's own sound chip) or a sound card. AHI is free, on Aminet, but
**not part of AmigaOS 3.x**: an Amiga that only ever played games and demos usually does not have it.

#### Installing AHI

If there is no `DEVS:ahi.device` and no `SYS:Prefs/AHI`, AHI is not installed. Get it from
[Aminet](https://aminet.net):

| Archive | What it holds |
|---|---|
| `driver/audio/m68k-amigaos-ahiusr.lha` | AHI 6.0 itself (`ahi.device`, the preferences); needs a 68020 or better |
| `driver/audio/m68k-amigaos-ahidrv.lha` | the drivers, among them `paula.audio` (Paula) |
| `util/libs/mui38usr.lha` | MUI, which the AHI 6 preferences need (skip it if MUI is installed) |

AHI 6 has no installer: copy its files as its readme (`ahiusr.readme`) describes. A sound card usually comes with
its own AHI driver: follow its documentation.

#### Setting up the unit

amisqueeze plays on AHI unit 0 (another one with [`UNIT`](#options)). In the AHI preferences (`SYS:Prefs/AHI`),
choose for that unit:

- **a "stereo++" mode**: `Paula:14 bit stereo++` with Paula (the name may end with "calibrated"), or your card's
  "16 bit stereo++" mode. In a plain "stereo" mode, the music plays on one side only;
- **the highest frequency**: 44100 Hz if the driver allows it. Paula's maximum is about 28 kHz with a 15 kHz screen
  mode (27,429 Hz in PAL); AHI converts the rate;
- **1 channel**: amisqueeze plays one sound at a time. More channels let other programs play at the same time.

Then save.

![The AHI preferences: unit 0 chosen at the top left, the mode Paula:14 bit stereo++ selected in the list, the
frequency at 27429 Hz and 1 channel on the right](images/ahi-prefs-unit0.png)

*The AHI preferences (AHI 6, with MUI) set for amisqueeze with Paula: unit 0, `Paula:14 bit stereo++`, the
highest frequency, 1 channel.*

With Paula, AHI converts and mixes the sound with the CPU. `Paula:8 bit stereo++` takes a little less CPU than the
14-bit mode, at a lower quality; the "HiFi" modes take more. On a slow Amiga, see [volume](#volume) too.

At start-up, amisqueeze checks the unit's mode and warns about a stereo mode without "++" or a mono mode.

### Server setup

amisqueeze plays the music of a Lyrion Music Server (LMS) or of a Music Assistant server. The server runs on
another computer of the local network; it is set up once.

#### Lyrion Music Server

amisqueeze needs three things from LMS:

- **Its address**, given with `SERVER`. It is the one in the browser's address bar when LMS's web page is open on
  another computer: `http://<address>:9000`. Give the server a fixed address in the router, so that it does not
  change. A host name works too, if the Amiga's TCP/IP stack can resolve it.
- **Two ports** open to the Amiga in the server's firewall: 3483/tcp for the players, and the web port (9000/tcp
  by default), which also carries the music. LMS tells amisqueeze which web port it uses. In Docker, publish both;
  the image's `HTTP_PORT` setting moves the web port.
- **A rule for MP3**, only when amisqueeze leaves MP3 to the server: with `amisqueeze`, or with `amisqueeze.fpu`
  on a slow Amiga (see [`FLAC`, `MP3` and `BENCH`](#flac-mp3-and-bench)). Add these lines to a file
  `custom-convert.conf`, in LMS's root directory or at the root of a plugins directory, then restart LMS:

  ```
  mp3 pcm * *
  	# FT:{START=trim %s}
  	[sox] -q -t mp3 $FILE$ -t raw -c 2 -2 -s - $START$
  ```

  FLAC, WAV and AIFF need no rule: LMS converts them by default.
- **Rules for `MAXRATE`**, only with that option: see [`MAXRATE`](#maxrate).

#### Music Assistant

Music Assistant drives amisqueeze through its Squeezelite provider. It needs:

- **The provider**: in Settings > Player providers, add "Squeezelite". Its "Slimproto port" is 3483 by default.
- **Its address**, given with `SERVER`: the address of Music Assistant's computer, found as for LMS above.
- **Two ports** open to the Amiga in the server's firewall: 3483/tcp for the players, and 8097/tcp, where players
  fetch the music (the "TCP Port" of Settings > System > Streams).
- **The right address for the streams**, when Music Assistant runs in a container or its computer has several
  networks. Music Assistant tells the player where to fetch each track, at the address it detects for itself. If
  the Amiga cannot reach that address, amisqueeze connects but no track plays. Then give the address the Amiga
  reaches the computer on in "Published IP address", an advanced setting of Settings > System > Streams. It must be
  an IP address, not a host name.
- **A codec amisqueeze plays.** Music Assistant converts every track to the player's output codec, set in the
  player's settings:

  | Output codec | With amisqueeze |
  |---|---|
  | `flac` (the default) | the best choice: lossless, without gaps between tracks; needs a fast Amiga (see [`FLAC`, `MP3` and `BENCH`](#flac-mp3-and-bench)) |
  | `wav` | for a slow Amiga (a 68020 without FPU): no decoding, but about 2 Mbit/s on the network |
  | `mp3` | only with `amisqueeze.fpu`; not recommended (see [known limits](#known-limits)) |
  | `aac` | not played |

  With `wav` on a slow Amiga, turn off what makes Music Assistant work on the sound in 32 bits. Otherwise it sends
  24-bit samples: half as much again as 16 bits, which amisqueeze plays anyway.

  - Settings > System > Player Queues > Audio: "Volume normalization (EBU-R128 based)" to "Disabled";
  - Settings > System > Player Queues > Crossfade: "Enable crossfade by default" unchecked;
  - the player's DSP left off (Settings > Player > DSP), as it is by default.

  These queue settings apply to every player. A 24-bit file still comes as 24 bits.

Music Assistant gives a new player a volume of 20, almost silent: turn it up before judging the sound. Leave
[`MAXRATE`](#maxrate) out with Music Assistant.

### Starting amisqueeze

#### From a Shell

Give the address of the server:

```
amisqueeze SERVER=192.168.1.10
```

amisqueeze prints the player's name and the server, then `connected to the server`. A player called "Amiga"
shows up in the server's web interface: choose it there or in an app, and play. No `Stack` command is needed:
amisqueeze sets up the stack it needs. If the connection drops or the server restarts, amisqueeze reconnects by
itself.

amisqueeze keeps the Shell until it stops. To get the Shell back, start it in the background:

```
Run >NIL: amisqueeze SERVER=192.168.1.10
```

To keep its messages in a file, add [`LOG`](#log).

#### Keeping the options

To avoid typing the address every time, save the options in the variable `amisqueeze.prefs`:

```
Echo "SERVER=192.168.1.10 NAME=Amiga" >ENV:amisqueeze.prefs
Copy ENV:amisqueeze.prefs ENVARC:
```

The copy in `ENVARC:` keeps them after a reboot. Then just type `amisqueeze`. Options given on the command line
win over the saved ones: see [default options](#default-options).

#### From the Workbench

Give the server's address in the icon first:

1. Select the icon of `amisqueeze` (or `amisqueeze.fpu`), then *Icons > Information...* (Amiga-I).
2. In its **ToolTypes**, turn `(SERVER=192.168.1.10)` into your server's address, without the parentheses. A
   ToolType in parentheses is disabled.
3. Save.

Then double-click the icon. A window titled "amisqueeze" shows its messages, and the player shows up in the
server's web interface. The ToolTypes take every [option](#options), one per line; see
[ToolTypes](#tooltypes) for those of the Workbench only.

![A Workbench drawer holding the two programs, amisqueeze.fpu and amisqueeze, each with a loudspeaker
icon](images/workbench-icons.png)

*The two programs in a drawer, with their icons.*

![The amisqueeze window: the FLAC speed measured at start-up, the player's name and server, then "connected to
the server"](images/workbench-window.png)

*Started from its icon: the message window, here with the options of `ENV:amisqueeze.prefs` (`SERVER=127.0.0.1
NAME=Amiga`, a test setup). Without `VERBOSE`, nothing more is printed while it plays.*

#### With the Amiga

- **From the Workbench**: copy the program and its icon into `SYS:WBStartup`. The Workbench starts it at every
  boot. With the ToolTypes `QUIET` and `LOG`, it plays in the background without a window.
- **From a script**: add `Run >NIL: amisqueeze` to `S:User-Startup`, after the line that starts the TCP/IP stack.

### Stopping amisqueeze

amisqueeze shows in **Exchange** (`SYS:Tools/Commodities/Exchange`), the system's list of background programs, as
"amisqueeze", "Squeezebox network music player". To stop it, however it was started:

- in **Exchange**: select "amisqueeze", then **Remove**. This is the way when it runs without a window (`QUIET`);
- in its **window** or its **Shell**: **Ctrl-C**;
- from another Shell: `Status` gives its process number (when started from a Shell), then `Break <number>`.

![Exchange: amisqueeze selected in the list of commodities, its description on the right, the Remove button
below](images/exchange.png)

*Exchange with amisqueeze selected: **Remove** stops it.*

Once Exchange runs, Ctrl-Alt-Help brings its window back.

amisqueeze always stops cleanly: the sound stops, the connection is closed, and it prints `stopped`. Its window
stays open until it is closed, so that the last messages can be read.

### Volume

amisqueeze follows the volume set on the server: slider, apps, mute. A change is heard 0.2 to 0.4 s later; mute
and unmute fade in about a second. Pause, stop and seek act at once.

**With Paula, or on a slow Amiga, fix the volume at 100 and set it on the amplifier.** Below 100, amisqueeze
scales the samples itself, which:

- costs CPU time: on a 68020 at about 28 MHz with Paula, about a quarter of the CPU, on top of AHI's two thirds;
- brings out Paula's own noise: a crackle can be heard at 50, gone at 100.

To fix it in LMS: in the player's settings (*Settings*, the player, then *Audio*), set **Volume Control** to
**Output level is fixed at 100%**. LMS then greys out or hides the slider, and keeps the choice for the player.
On the same page, leave **Audio Startup Time** at 0: it is meant for receivers fed by a digital output.

LMS's volume scale is logarithmic: 50 is already quiet (about -25 dB, 6 % of full amplitude), 25 very quiet
(about -37 dB, 1.4 %).

## Reference

### Options

The command line's template:

```
SERVER,PORT/N/K,NAME/K,MAC/K,OUTPUT/K,LOG/K,UNIT/N/K,NOAUDIO/S,SWAP/S,FLAC/K,MP3/K,MAXRATE/K,BENCH/S,VERBOSE/S
```

| Option | Meaning | Default |
|---|---|---|
| `SERVER` | the server: IP address or host name; the `SERVER=` keyword may be left out | none: required, on the command line or in the [default options](#default-options) |
| `PORT` | the server's slimproto port | `3483` |
| `NAME` | the player's name shown by the server (see [`NAME` and `MAC`](#name-and-mac)) | `Amiga` |
| `MAC` | the player's identifier, `xx:xx:xx:xx:xx:xx` | derived from `NAME` |
| `UNIT` | the AHI unit to play through, 0 to 3 (see [setting up the unit](#setting-up-the-unit)) | `0` |
| `SWAP` | swaps left and right, for an AHI driver that swaps them | off |
| `FLAC` | `AUTO`, `ON` or `OFF`: where FLAC is decoded (see [`FLAC`, `MP3` and `BENCH`](#flac-mp3-and-bench)) | `AUTO` |
| `MP3` | the same for MP3, with `amisqueeze.fpu` only | `AUTO` |
| `MAXRATE` | `AHI` or a rate in Hz, 8000 to 48000: the highest sample rate asked of the server, to lighten a slow Amiga; LMS then needs a rule (see [`MAXRATE`](#maxrate)) | none: the files' own rates, up to 48 kHz |
| `BENCH` | measures how fast this Amiga decodes FLAC and MP3, prints it and quits; no server needed | off |
| `LOG` | a file that keeps a copy of every message (see [`LOG`](#log)) | none |
| `VERBOSE` | prints the messages exchanged with the server and the stream's events | off |
| `NOAUDIO` | no sound: the music is received and consumed at the playing pace, as if played | off |
| `OUTPUT` | a file that receives the audio, for checking; no sound with it (see [`OUTPUT`](#output)) | none |

#### `LOG`

`LOG=<file>` keeps a copy of every message in a file, while they still appear in the window. A redirection
(`>file`) sends them to the file only.

- Each run is appended to the file, after a line with the date and time, for instance
  `--- amisqueeze 0.5 (...), started 20-Oct-26 14:32:05`.
- The file is written as the messages come: `Type` reads it while amisqueeze runs.
- It starts when the file is open: an error in the options shows in the window only.
- In the default options, it keeps a lasting trace of what happened, handy when something goes wrong.
- The file only grows. Without `VERBOSE`, a run adds a few lines; do not leave `VERBOSE` on with `LOG` for good,
  and delete the file now and then.

To keep the messages of a Shell run in a file without the window, redirect them instead. The redirection goes
right after the command's name:

```
Run amisqueeze >RAM:amisqueeze.log VERBOSE
```

#### `NAME` and `MAC`

The server identifies players by their MAC address. Without `MAC`, amisqueeze derives a stable one from the name:

- the same name always gives the same player on the server;
- two Amigas with different names show up as two players.

The player can also be renamed from the server's web interface: amisqueeze takes the new name while it runs.

#### `FLAC`, `MP3` and `BENCH`

When it connects, amisqueeze tells the server which formats it decodes itself. The server sends those files as
they are, and converts the others to raw PCM. Decoding on the Amiga:

- saves network bandwidth: FLAC takes about half of raw PCM, MP3 a fifth or less;
- plays FLAC files above 48 kHz: LMS then lowers their rate in FLAC;
- plays MP3 without a conversion rule on the server.

It needs a fast CPU. MP3 also needs an FPU: only `amisqueeze.fpu` decodes it.

**`AUTO`**, the default, measures at start-up. amisqueeze decodes a built-in sample of each format in its slowest
case (FLAC level 8, MP3 at 320 kbit/s), for at most a second each. It takes a format only if it decodes it at
least twice as fast as real time, and prints the result:

```
FLAC: level 8 decoded at 77.0x real time: FLAC decoded here
MP3: 320 kbit/s decoded at 74.2x real time: MP3 decoded here
```

The measure is a little pessimistic on a CPU that compiles code on the fly (WinUAE's JIT, the PiStorm's Emu68):
its first pass also compiles.

**`ON`** always decodes the format on the Amiga, whatever the speed. **`OFF`** always leaves it to the server.
`MP3=ON` given to `amisqueeze` leaves MP3 to the server, with a message.

**`BENCH`** prints the measures and quits, without connecting. On a 68020 without FPU, with `amisqueeze`:

```
FLAC level 8 (the slowest) decoded at 0.2x real time: FLAC=AUTO leaves FLAC to the server (converted to PCM)
MP3: not decoded by this build (amisqueeze.fpu decodes it, on an Amiga with an FPU)
```

On a fast Amiga, with `amisqueeze.fpu`:

```
FLAC level 8 (the slowest) decoded at 77.0x real time: FLAC=AUTO lets this machine decode FLAC
MP3 at 320 kbit/s (the slowest) decoded at 74.2x real time: MP3=AUTO lets this machine decode MP3
```

#### `MAXRATE`

`MAXRATE` asks the server for music at a lower sample rate, to lighten a slow Amiga: less to receive from the
network, less for AHI to mix. Without it, the server sends each file at its own rate, up to 48 kHz.

- **`MAXRATE=AHI`** takes the rate of the AHI unit, rounded up to a standard rate: Paula at 27429 Hz gives
  32000 Hz. Paula plays nothing above about 13.7 kHz, so nothing that can be heard is lost.
- **`MAXRATE=<Hz>`** gives the rate, from 8000 to 48000. `MAXRATE=22050` halves the network traffic of CD-quality
  music, with duller treble.

At start-up, amisqueeze says what it asks for:

```
MAXRATE: at most 32000 Hz asked of the server (AHI unit 0 at 27429 Hz)
```

**With LMS, `MAXRATE` needs rules on the server.** LMS cannot lower the rate of raw PCM by itself: without the
rules, it refuses every file above the rate asked. Add these lines to `custom-convert.conf` (see
[Lyrion Music Server](#lyrion-music-server)), then restart LMS:

```
flc pcm * 02:41:4d:d4:ca:84
	# FT:{START=--skip=%t}U:{END=--until=%v}D:{RESAMPLE=-r %d}
	[flac] -dcs $START$ $END$ --force-raw-format --endian=little --sign=signed -- $FILE$ | [sox] -q -t raw --endian little --encoding signed-integer -b $SAMPLESIZE$ -r $SAMPLERATE$ -c $CHANNELS$ - -t wav $RESAMPLE$ -b 16 -

mp3 pcm * 02:41:4d:d4:ca:84
	# FT:{START=trim %s}D:{RESAMPLE=-r %d}
	[sox] -q -t mp3 $FILE$ -t wav $RESAMPLE$ -b 16 -c 2 - $START$
```

- **Put the player's MAC address**, in lowercase, in place of `02:41:4d:d4:ca:84`, that of the default name
  `Amiga`. amisqueeze prints it at start-up: `player "Amiga" (02:41:4D:D4:CA:84), ...`.
- **The rules are for that player only.** They send WAV, whose header tells amisqueeze the rate and sample size;
  another player would play the header as music.
- **They send 16-bit samples**, even without `MAXRATE`: a 24-bit file needs a third less traffic.
- **Other formats** (WAV, AIFF, Ogg...) need a rule of the same kind. Without one, LMS refuses those files above
  the rate asked.

With `amisqueeze.fpu` decoding FLAC on the Amiga, LMS lowers the rate of FLAC files itself, without a rule. It
lowers the rate of MP3 decoded on the Amiga by encoding it again, which loses a little quality: `MP3=OFF` and the
rules above avoid it.

**With Music Assistant, leave `MAXRATE` out.** Music Assistant 2.10 offers no rate below 44.1 kHz to a player that
asks for less, and the music does not play (tried with 2.10.7: the stream fails with a server error).

#### `OUTPUT`

`OUTPUT=<file>` writes the audio to a file as it is played. It is meant for checking playback, not for listening:
**there is no sound with it**, as with `NOAUDIO`.

- The file is created, or replaced, when amisqueeze starts, and closed when it stops.
- Everything played in between follows, track after track; nothing while paused.
- Raw PCM is written as the server sent it: from LMS, 16-bit **little-endian** samples (44.1 kHz stereo: about
  10 MB a minute).
- FLAC and MP3 decoded on the Amiga are written as 16-bit samples in the Amiga's byte order (big-endian).

To check the file, compare it with the expected PCM, for instance with `tools/pcm-compare.py` from the
repository. In an emulator, write it to a volume shared with the host. Do not put `OUTPUT` in the default
options: every run would fill the disk.

### ToolTypes

Started from the Workbench, amisqueeze reads its options from the icon's ToolTypes, one per line:
`SERVER=192.168.1.10`, `NAME=Kitchen`, `UNIT=0`, `LOG=...`, `VERBOSE`...

- A ToolType in parentheses is disabled. The icon comes with examples written that way, such as
  `(SERVER=192.168.1.10)`.
- The ToolTypes win over the [default options](#default-options), which still apply to what they leave out.
- Other ToolTypes, such as the Workbench's own `DONOTWAIT`, are left alone. The icon carries `DONOTWAIT`, so that
  the Workbench does not wait for amisqueeze at boot.

Two ToolTypes are for the Workbench only:

| ToolType | Meaning |
|---|---|
| `WINDOW=CON:...` | the message window: position, size, title. The default is `CON:0/20/640/120/amisqueeze/AUTO/CLOSE/WAIT/SMART`; the window opens at the first message |
| `QUIET` | no window: amisqueeze runs silently; add `LOG=<file>` to keep its messages. An error at start-up still opens the window (see [known limits](#known-limits)) |

### Default options

The variable **`amisqueeze.prefs`** holds options with the same syntax as the command line. amisqueeze reads it at
start-up from `ENV:amisqueeze.prefs`. AmigaOS copies `ENVARC:` to `ENV:` when it starts, so a copy in `ENVARC:`
survives a reboot. Both executables read it. To create it: [keeping the options](#keeping-the-options).

- **The command line wins**: `amisqueeze NAME=Kitchen` uses the server of the default options and the name
  `Kitchen`.
- **`VERBOSE`** set in the default options cannot be turned off from the command line. Leave it out of the file,
  and add it on the command line when needed.
- **To change them**: edit the file (`Ed ENVARC:amisqueeze.prefs`, then copy it again to `ENV:`), or create it
  again with `Echo`.
- **To remove them**: `Delete ENV:amisqueeze.prefs ENVARC:amisqueeze.prefs`.
- An unknown option or a syntax error in the file stops amisqueeze, with a message naming `ENV:amisqueeze.prefs`.

### Messages

Without `VERBOSE`, amisqueeze prints a few lines at start-up, then only what goes wrong. Error messages start
with `amisqueeze: `, left out below. `N` stands for a number, `...` for a name or a text.

#### Normal messages

| Message | Meaning |
|---|---|
| `player "..." (xx:xx:xx:xx:xx:xx), server ... port ...` | at every start: the player's name and MAC address as the server sees them, and the server it connects to |
| `FLAC: level 8 decoded at ...x real time: FLAC decoded here` (or `converted to PCM by the server`) | the start-up measure of `FLAC=AUTO` |
| `MP3: 320 kbit/s decoded at ...x real time: ...` | the same for MP3 (`MP3=AUTO`, `amisqueeze.fpu`) |
| `MAXRATE: at most N Hz asked of the server` | the rate [`MAXRATE`](#maxrate) asks for; with `MAXRATE=AHI`, followed by the unit and its rate |
| `connected to the server` | the connection is up; the player should show up on the server |
| `player name: ...` | the player was renamed on the server: amisqueeze takes the new name |
| `stopped` | amisqueeze has stopped cleanly (Ctrl-C, `Break`, Remove in Exchange) |

#### Warnings at start-up

amisqueeze plays anyway.

| Message | Meaning |
|---|---|
| `AHI unit N uses "...", a stereo mode without "++": the music will play on one side only; ...` | choose a "stereo++" mode (see [setting up the unit](#setting-up-the-unit)) |
| `AHI unit N uses "...", a mono mode: the music will play in mono` | choose a "stereo++" mode for stereo |
| `MP3=ON: this build does not decode MP3 ...` | `MP3=ON` given to `amisqueeze`: MP3 stays with the server |
| `MAXRATE=AHI: the AHI unit's rate is not known (...): at most 48000 Hz asked of the server` | with `NOAUDIO` or `OUTPUT`, or when the AHI preferences cannot be read: give the rate in Hz instead |

#### amisqueeze does not start

amisqueeze stops at once.

| Message | Meaning |
|---|---|
| `no server: give SERVER=<ip\|name>, or set it in ENV:amisqueeze.prefs` | no server on the command line nor in the default options |
| `amisqueeze: <AmigaDOS error>`, e.g. `amisqueeze: bad number` | the command line does not fit the template: a bad number for `PORT` or `UNIT`, an unknown keyword...; the text is AmigaDOS's own |
| `ENV:amisqueeze.prefs: ...` | the default options cannot be read: an unknown option, a syntax error |
| `the icon's ToolTypes: ...` | a ToolType cannot be read: an unknown value, a bad number...; shown in the window, which stays open |
| `the icon's ToolTypes are too long` | the ToolTypes together exceed what amisqueeze reads, about 500 characters |
| `invalid MAC (expected xx:xx:xx:xx:xx:xx)` | `MAC` is not six hexadecimal pairs |
| `FLAC=...: expected AUTO, ON or OFF` (or `MP3=...`) | a wrong value for that option |
| `MAXRATE=...: expected AHI, or a rate from 8000 to 48000 Hz` | a wrong value for `MAXRATE` |
| `amisqueeze.fpu needs an FPU (...): use amisqueeze instead` | `amisqueeze.fpu` started on an Amiga without an FPU |
| `amisqueeze is already running (Exchange lists it and can stop it)` | started from a Shell while another amisqueeze runs; from the Workbench, the new one quits without a word |
| `bsdsocket.library not found (is a TCP/IP stack running?)` | no TCP/IP stack started |
| `server ... not found` | the server's host name cannot be resolved |
| `not enough memory for the 1024 KB stream buffer` | not enough free memory |
| `not enough memory for the audio blocks` (or `for the audio output`) | the 3 audio blocks (96 KB) or the AHI requests cannot be allocated |
| `cannot open ahi.device unit N (is AHI installed? NOAUDIO plays without sound)` | AHI missing, or that unit cannot be opened |
| `timer.device unavailable` | the system timer cannot be opened; should not happen |

#### Connection

amisqueeze reconnects by itself.

| Message | Meaning |
|---|---|
| `cannot connect: error N` | nothing answers at that address and port |
| `retrying in 5 s` | after a failed connection, or a connection lost less than 10 s after it was made; after a longer session, amisqueeze reconnects at once |
| `socket: error N` | the TCP/IP stack cannot create a connection (out of resources?) |
| `send: error N`, `recv: error N` | the connection to the server broke |
| `WaitSelect: error N` | waiting on the network failed: a TCP/IP stack error |
| `no news from the server, reconnecting` | the server stayed silent for 35 s |
| `connection closed by the server` | the server closed the connection |

#### Tracks not played

amisqueeze does not play the track. After a message ending with `skipped`, the next track follows; the other
rows say what comes next.

| Message | Meaning |
|---|---|
| `stream format X not supported` | a format other than raw PCM, FLAC, or MP3 with `amisqueeze.fpu`: the server has no rule to convert that file; LMS moves on to the next track |
| `stream: cannot connect: error N` | the server's stream port cannot be reached: 9000 by default with LMS, 8097 with Music Assistant |
| `stream: connection lost before the response headers`, `stream: send: error N` | the stream broke before any music came |
| `stream: recv: error N` | the stream broke during a track: what was received plays, then the next track follows |
| `stream: request too long (N bytes)` | the stream's request exceeds 1 KB; LMS moves on to the next track |
| `stream: response headers too long` | the server's answer exceeds 2 KB |
| `flac: not a FLAC stream, track skipped` | the stream is not FLAC |
| `flac: no STREAMINFO block, track skipped`, `flac: cannot open the stream, track skipped` | a damaged FLAC header |
| `flac: sample rate 0, track skipped` | a FLAC file with a sample rate of 0 |
| `flac: more than 2 channels, track skipped` | not mono or stereo |
| `flac: cannot start from the middle of this file, track skipped` | after a seek, in a rare file whose frames leave their format to its header |
| `mp3: no MP3 frame found, track skipped` | the stream holds no MP3 at all |
| `wav: not integer PCM, track skipped` | the WAV file holds floating-point or compressed audio |
| `wav: number of channels not supported, track skipped` | not mono or stereo |
| `wav: sample size not supported, track skipped` | not 8, 16, 24 or 32 bits |
| `wav: sample rate not supported, track skipped` | 0, or above 384 kHz |
| `wav: no format chunk, track skipped`, `wav: format chunk too short, track skipped`, `wav: header too long, track skipped`, `wav: the track ends in its header, track skipped` | a damaged WAV header, or one longer than 4 KB |
| `flac: not enough memory` (or `mp3: not enough memory`), then `flac: cannot decode this track, skipped` (or `mp3: ...`) | not enough free memory for the decoder |

#### Messages from the server not understood

| Message | Meaning |
|---|---|
| `strm X not handled`, `strm too short (N)`, `XXXX not handled (N bytes)`, `packet too short (N)` | the server sent a message amisqueeze does not know, or a malformed one: it is ignored. Worth reporting, with the server's name and version. `cont not handled` is expected with some internet radios (see [known limits](#known-limits)) |
| `invalid packet size (N)` | the same, but amisqueeze closes the connection and reconnects |

#### Files

| Message | Meaning |
|---|---|
| `cannot open the log ...: error N, going on without it` | the `LOG` file cannot be opened: a missing directory, a write-protected disk |
| `cannot write to the log ...: error N, log closed` | writing the `LOG` file failed (disk full...); playback goes on without it |
| `cannot create ...: error N` | the `OUTPUT` file cannot be created: a missing directory, a write-protected disk; amisqueeze stops. On a volume that is not mounted, AmigaDOS first asks for it; this message follows a Cancel |
| `cannot write to ...: error N, file closed` | writing the `OUTPUT` file failed (disk full...); playback goes on without the file |

#### `BENCH`

| Message | Meaning |
|---|---|
| `FLAC level 8 (the slowest) decoded at ...x real time: FLAC=AUTO lets this machine decode FLAC` (or `leaves FLAC to the server (converted to PCM)`) | the FLAC measure |
| `MP3 at 320 kbit/s (the slowest) decoded at ...x real time: MP3=AUTO ...` | the same for MP3 |
| `MP3: not decoded by this build (...)` | `BENCH` run with `amisqueeze`, which has no MP3 decoder |
| `the FLAC decoding speed cannot be measured` (or `MP3`) | not enough memory for the measure |

#### With `VERBOSE`

| Message | Meaning |
|---|---|
| `amisqueeze 0.7 (...)` | the version, at start-up |
| `FLAC: decoded here (FLAC=ON)` (or `converted to PCM by the server (FLAC=OFF)`), the same for MP3 | where the format is decoded, as the option sets it |
| `output: AHI unit N (..., N Hz)` | the AHI mode and frequency played with, for instance `output: AHI unit 0 (Paula:14 bit stereo++ calibrated, 27429 Hz)`; `output: virtual (no sound)` with `NOAUDIO` or `OUTPUT` |
| `volume: 5.9 % of full amplitude`, `volume: fixed at 100 % by LMS (samples untouched)` | a change of volume |
| `<- strm ...`, `-> STAT ...`, `-> DSCO ...`, `<- audg ...`, `<- aude ...` | the messages exchanged with the server |
| other `stream: ...` messages | the stream's events: parameters, connection, headers received, WAV header, end of the track's data |
| other `output: ...` messages | the output's events: track started, buffer empty, end of playback, `OUTPUT` file opened and bytes written to it |

### Troubleshooting

- **The player does not show up on the server.** Start amisqueeze from a Shell, or add `LOG`, and read the
  messages:
  - `bsdsocket.library not found`: no TCP/IP stack is running;
  - `cannot connect`: nothing answers at that address. Check `SERVER`, that the server runs, and that its
    firewall lets 3483/tcp in (see [server setup](#server-setup));
  - `connected to the server`: the connection is up.
- **It connects, but a track does not play, or is skipped at once.**
  - `stream: cannot connect`: the server's stream port is blocked (LMS: its web port, 9000/tcp; Music Assistant:
    8097/tcp), or Music Assistant's "Published IP address" is wrong (see [server setup](#server-setup)).
  - `stream format X not supported`, or nothing at all, with "Couldn't create command line" in LMS's `server.log`:
    LMS has no rule to convert that file. Typically MP3 with `amisqueeze`: add the
    [MP3 rule](#lyrion-music-server), or use `amisqueeze.fpu` on an Amiga with an FPU. Or a file above 48 kHz
    when FLAC is not decoded on the Amiga (see [known limits](#known-limits)). With `MAXRATE`: the rules of
    [`MAXRATE`](#maxrate) are missing, or carry another player's MAC address.
- **The music plays on one side only.** The AHI unit uses a plain "stereo" mode: choose a "stereo++" one (see
  [setting up the unit](#setting-up-the-unit)).
- **Left and right are swapped.** Add `SWAP`. Seen in WinUAE with `paula.audio` 4.30, in 8 and 14 bits, while
  Paula itself is right; not checked on a real Amiga yet.
- **The sound stutters or stops now and then.** The Amiga does not keep up:
  - run `amisqueeze BENCH`: if FLAC or MP3 is decoded at less than a few times real time, give `FLAC=OFF` or
    `MP3=OFF` so that the server converts them;
  - with Paula, fix the volume at 100 (see [volume](#volume)), and an 8-bit mode costs less CPU;
  - close other busy programs;
  - raw PCM needs about 1.4 Mbit/s for CD quality: a slow network can starve it. With LMS,
    [`MAXRATE`](#maxrate) asks for less.
- **Crackles at low volume with Paula.** Fix the volume at 100 and turn the amplifier down instead (see
  [volume](#volume)).
- **Nothing happens from the Workbench.** With `QUIET`, add `LOG=<file>` to see the messages. If amisqueeze already
  runs (Exchange lists it), a second start does nothing.
- **`amisqueeze.fpu needs an FPU`**: use `amisqueeze`.

### Known limits

- Tried in WinUAE only, not yet on a real Amiga (see [what has been tried](#what-has-been-tried)).
- The server's address must be given (`SERVER`): amisqueeze does not find the server by itself yet.
- MP3 is decoded only by `amisqueeze.fpu`. With `amisqueeze`, LMS needs a [rule](#lyrion-music-server) to convert
  it. Other formats play only through the server's conversion to raw PCM.
- Files above 48 kHz (88.2, 96, 192 kHz) play only when FLAC is decoded on the Amiga. Otherwise LMS cannot lower
  their rate as it makes raw PCM: its log says "Couldn't create command line for flc playback".
- Some internet radios mix their metadata (the title playing) into the MP3 stream. amisqueeze does not remove it
  yet. When LMS has `amisqueeze.fpu` fetch such a radio itself, the sound may cut regularly, with the message
  `cont not handled`. Not checked with a real radio yet.
- MP3 tracks follow each other without a gap only when the file has the header that LAME, and the encoders that
  copy its format, write in the first frame. Without it, a few hundredths of a second of silence remain between
  tracks.
- With Music Assistant:
  - `aac` does not play: amisqueeze refuses the stream, but Music Assistant still shows the track as playing;
  - `mp3` plays only with `amisqueeze.fpu`. Music Assistant encodes it on the fly without the LAME header: the
    change of track can be heard, and some sounds get small artefacts from the second lossy encoding.
- With `QUIET`, an invalid `MAC` or an `OUTPUT` file that cannot be created is reported in the log only. The
  other errors at start-up open the window, which stays until it is closed: in the ToolTypes or the default
  options, a log that cannot be opened, no TCP/IP stack.
- Synchronised players (LMS's player groups) are not supported, nor tried.
- Exchange's Active/Inactive switch is shown, but does not affect the music.
- Occasional crackles with `paula.audio` in WinUAE also occur without amisqueeze, with AHI's own test tone.

### What has been tried

Everything so far was run in WinUAE. What the README names but this table does not is expected to work, not
checked.

| | Tried | Expected, not tried yet |
|---|---|---|
| Machine | WinUAE 6.0 (an A1200 setup) | any real Amiga |
| CPU | 68020, at about 28 MHz and with the JIT (the speed of a PiStorm, roughly); with and without a 68882 | 68030, 68040, 68060, a PiStorm with Emu68 |
| AmigaOS | 3.2.3; 3.0 and 3.1 from a Shell without AHI; 2.04, the oldest version it asks for, starts | 3.1.4, 3.5, 3.9 |
| Memory | about 1.1 MB free: the 1 MB stream buffer (about 6 s of CD-quality audio), and about 60 KB more for `amisqueeze.fpu` decoding MP3 | - |
| TCP/IP | WinUAE's emulation (`bsdsocket_emu=true`) | Roadshow, AmiTCP, AmiNetXDuo |
| Sound | AHI 6.6 with `paula.audio` and a Toccata card, checked by ear | AHI 4.x, other cards, a PiStorm's own outputs |
| Server | Lyrion Music Server 9.1.1, Music Assistant 2.10.7 | other versions |

Measured in WinUAE:

| Measure | Emulated CPU | Result |
|---|---|---|
| `BENCH` | 68020 without JIT (about 45 MHz) | FLAC 0.2x: left to the server, converted by its default rule (nothing to set up) |
| `BENCH` | 68020 with JIT and 68882 (the order of a PiStorm) | FLAC 77x, MP3 74x (`amisqueeze.fpu`): both decoded on the Amiga |
| CPU taken by playback with Paula | 68020 at about 28 MHz | about two thirds for AHI, 14-bit modes a little more than 8-bit ones; a few percent for amisqueeze at volume 100, about a quarter more below 100 |

### Removing amisqueeze

Delete the executable and its icon, from `SYS:WBStartup` too if it was put there. Delete the default options, if
any:

```
Delete ENV:amisqueeze.prefs ENVARC:amisqueeze.prefs
```

amisqueeze writes nothing else, apart from the `LOG` and `OUTPUT` files you named.
