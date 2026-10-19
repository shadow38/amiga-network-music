# amiga-network-music — User guide

This guide is for people who **use** the programs. Building them is covered in the [README](../README.md); the
development roadmap is in [docs/development](development/README.md).

## amisqueeze

amisqueeze turns the Amiga into a network music player for Lyrion Music Server (LMS). It speaks slimproto, the
protocol of Logitech's Squeezebox players, so LMS (and its web interface or apps) sees and controls it like any
other player.

**Current state (version 0.1, in development):** amisqueeze connects to the server, shows up as a player and
**plays what LMS plays through AHI**: in LMS, playback progresses, moves on to the next track, pauses, seeks,
stops and changes volume as with any player. Only raw 16-bit PCM streams are accepted for now (LMS converts the other formats, see
the requirements). The sound has been checked by ear in WinUAE (Paula and Toccata), not yet on a real Amiga.

### Requirements

On the Amiga:

- AmigaOS 3.x, 68020 or better (from a plain 68020 accelerator to a PiStorm);
- about 1.1 MB of free memory (1 MB stream buffer, about 6 s of CD-quality audio);
- a TCP/IP stack providing `bsdsocket.library`: Roadshow, AmiTCP, AmiNetXDuo, or WinUAE's emulation
  (`bsdsocket_emu=true`) in an emulator;
- for the sound: AHI (tested with 6.x; 4.x expected to work, not checked yet) and a driver: Paula's
  (`paula.audio`, no sound card needed) or your sound card's; see "Sound (AHI)" below.

On the server:

- Lyrion Music Server (tested with 9.1.1), reachable from the Amiga on TCP ports 3483 (players) and 9000 (audio
  streams; or whatever its HTTP port is: LMS tells the player which one);
- a conversion rule from MP3 to raw PCM, see the [README](../README.md#lms-server). FLAC and WAV are converted by
  LMS's default rules.

### Installation

Copy `amisqueeze` anywhere, ideally into a directory of the Shell's command path (for instance `C:`), so that it
can be run by name.

### Running

From a Shell:

```
amisqueeze SERVER=192.168.1.10
```

Template: `SERVER,PORT/N/K,NAME/K,MAC/K,OUTPUT/K,UNIT/N/K,NOAUDIO/S,SWAP/S,VERBOSE/S`

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
| `VERBOSE` | print the messages exchanged with the server and the stream events | off |

`Ctrl-C` stops amisqueeze. To keep the Shell free, run it in the background: `Run >NIL: amisqueeze`, or keep its
messages in a file, written as they come: `Run amisqueeze >RAM:amisqueeze.log VERBOSE` (the redirection goes
right after the command name). `Status` then shows its process number, and `Break <number>` stops it.

LMS identifies players by their MAC address. Without `MAC`, amisqueeze derives a stable one from the name: the same
name always gives the same player in LMS, and two Amigas with different names show up as two players. The player
can also be renamed from the LMS web interface; amisqueeze takes the new name while it runs.

If the connection drops or the server restarts, amisqueeze reconnects by itself (it retries every 5 seconds).

**`OUTPUT=<file>`** writes the audio to a file as it is played, exactly as the server sent it: raw PCM, for LMS
16-bit **little-endian** samples (44.1 kHz stereo: about 10 MB per minute). The file is created (or replaced) when
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
samples are not touched. With `VERBOSE`, each change prints `volume: 5.9 % of full amplitude`.

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
| `bsdsocket.library not found (is a TCP/IP stack running?)` | no TCP/IP stack started |
| `server … not found` | the host name cannot be resolved |
| `cannot connect: error N` | the server does not answer on that address and port; amisqueeze retries every 5 s |
| `no news from the server, reconnecting` | the server stayed silent for 35 s; amisqueeze reconnects |
| `connection closed by the server` | the server closed the connection; amisqueeze reconnects |
| `not enough memory for the 1024 KB stream buffer` | not enough free memory to start |
| `stream format X not supported` | LMS sent a format other than raw PCM (no conversion rule for that format) |
| `N-bit samples not supported by the audio output` | the stream is not 16-bit; LMS normally sends 16-bit PCM |
| `cannot open ahi.device unit N (is AHI installed? NOAUDIO plays without sound)` | AHI missing, or that unit cannot be opened; amisqueeze stops |
| `not enough memory for the audio blocks` | the 3 audio blocks (96 KB) cannot be allocated; amisqueeze stops |
| `cannot create <file>: error N` | the `OUTPUT` file cannot be created (missing directory, write-protected disk); amisqueeze stops. On a volume that is not mounted, AmigaDOS first asks for it to be inserted; this message follows a Cancel |
| `cannot write to <file>: error N, file closed` | writing the `OUTPUT` file failed (disk full…); playback goes on without the file |
| `stream: cannot connect: error N` | the audio stream port of LMS (HTTP, 9000 by default) cannot be reached |
| `stream: …`, `output: …` (with `VERBOSE`) | stream events: parameters, connection, headers received, end of the track data; output: AHI unit or virtual output, track started, buffer empty, end of playback, `OUTPUT` file opened and bytes written to it |

## amilms

The remote control is not available yet; see [its plan](development/phase-2-amilms.md).
