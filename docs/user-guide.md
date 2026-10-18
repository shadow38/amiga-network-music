# amiga-network-music — User guide

This guide is for people who **use** the programs. Building them is covered in the [README](../README.md); the
development roadmap is in [docs/development](development/README.md).

## amisqueeze

amisqueeze turns the Amiga into a network music player for Lyrion Music Server (LMS). It speaks slimproto, the
protocol of Logitech's Squeezebox players, so LMS (and its web interface or apps) sees and controls it like any
other player.

**Current state (version 0.1):** amisqueeze connects to the server, shows up as a player and follows its
commands, but **does not play audio yet**: streams are refused. Audio output through AHI is the next part of the
work.

### Requirements

On the Amiga:

- AmigaOS 3.x, 68020 or better (a PiStorm is the intended target);
- a TCP/IP stack providing `bsdsocket.library`: Roadshow, AmiTCP, AmiNetXDuo, or WinUAE's emulation
  (`bsdsocket_emu=true`) in an emulator;
- later, for audio: AHI and a driver for your sound card.

On the server:

- Lyrion Music Server (tested with 9.1.1), reachable from the Amiga on TCP ports 3483 (players) and 9000 (audio
  streams; or whatever its HTTP port is);
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

Template: `SERVER,PORT/N/K,NAME/K,MAC/K,VERBOSE/S`

| Option | Meaning | Default |
|---|---|---|
| `SERVER` | LMS server: IP address or host name (the `SERVER=` keyword may be omitted) | none: required, on the command line or in the default options |
| `PORT` | slimproto port of the server | `3483` |
| `NAME` | player name shown in LMS | `Amiga` |
| `MAC` | player identifier, `xx:xx:xx:xx:xx:xx` | derived from `NAME` |
| `VERBOSE` | print the messages exchanged with the server | off |

`Ctrl-C` stops amisqueeze. To keep the Shell free, run it in the background: `Run >NIL: amisqueeze`, or keep its
messages in a file, written as they come: `Run amisqueeze >RAM:amisqueeze.log VERBOSE` (the redirection goes
right after the command name). `Status` then shows its process number, and `Break <number>` stops it.

LMS identifies players by their MAC address. Without `MAC`, amisqueeze derives a stable one from the name: the same
name always gives the same player in LMS, and two Amigas with different names show up as two players. The player
can also be renamed from the LMS web interface; amisqueeze takes the new name while it runs.

If the connection drops or the server restarts, amisqueeze reconnects by itself (it retries every 5 seconds).

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
| `stream: …` (with `VERBOSE`) | the server asked to play something; the parameters are printed, the stream is refused for now |

## amilms

The remote control is not available yet; see [its plan](development/phase-2-amilms.md).
