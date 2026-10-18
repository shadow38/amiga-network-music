# amiga-network-music — User guide

This guide is for people who **use** the programs. Building them is covered in the [README](../README.md); the
development roadmap is in [docs/development](development/README.md).

## amisqueeze

amisqueeze turns the Amiga into a network music player for Lyrion Music Server (LMS). It speaks slimproto, the
protocol of Logitech's Squeezebox players, so LMS (and its web interface or apps) sees and controls it like any
other player.

**Current state (version 0.1, in development):** amisqueeze connects to the server, shows up as a player and
**receives the audio stream** of what LMS plays: in LMS, playback progresses, moves on to the next track, pauses,
seeks and stops as with any player. But **there is no sound yet**: the audio is received and discarded at the
playing pace (or written to a file with `OUTPUT`, to check it). Sound through AHI is the next part of the work.
Only raw PCM streams are accepted for now (LMS converts the other formats, see the requirements).

### Requirements

On the Amiga:

- AmigaOS 3.x, 68020 or better (a PiStorm is the intended target);
- about 1.1 MB of free memory (1 MB stream buffer, about 6 s of CD-quality audio);
- a TCP/IP stack providing `bsdsocket.library`: Roadshow, AmiTCP, AmiNetXDuo, or WinUAE's emulation
  (`bsdsocket_emu=true`) in an emulator;
- later, for audio: AHI and a driver for your sound card.

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

Template: `SERVER,PORT/N/K,NAME/K,MAC/K,OUTPUT/K,VERBOSE/S`

| Option | Meaning | Default |
|---|---|---|
| `SERVER` | LMS server: IP address or host name (the `SERVER=` keyword may be omitted) | none: required, on the command line or in the default options |
| `PORT` | slimproto port of the server | `3483` |
| `NAME` | player name shown in LMS | `Amiga` |
| `MAC` | player identifier, `xx:xx:xx:xx:xx:xx` | derived from `NAME` |
| `OUTPUT` | file receiving the audio as it is played, for checking (see below) | none |
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
with `tools/pcm-compare.py` from the repository (in an emulator, write it to a volume shared with the host). Avoid
putting it in the default options, or every run fills the disk.

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
| `cannot create <file>: error N` | the `OUTPUT` file cannot be created (missing directory, write-protected disk); amisqueeze stops. On a volume that is not mounted, AmigaDOS first asks for it to be inserted; this message follows a Cancel |
| `cannot write to <file>: error N, file closed` | writing the `OUTPUT` file failed (disk full…); playback goes on without the file |
| `stream: cannot connect: error N` | the audio stream port of LMS (HTTP, 9000 by default) cannot be reached |
| `stream: …`, `output: …` (with `VERBOSE`) | stream events: parameters, connection, headers received, end of the track data; output: track started, buffer empty, end of playback, `OUTPUT` file opened and bytes written to it |

## amilms

The remote control is not available yet; see [its plan](development/phase-2-amilms.md).
