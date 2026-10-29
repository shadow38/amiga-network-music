# Amiga Network Music

**Network music for classic Amigas**: the Amiga becomes a Squeezebox player, playing the music of a server on
your local network and driven like any other player in the house.

[![tests](https://github.com/shadow38/amiga-network-music/actions/workflows/tests.yml/badge.svg)](https://github.com/shadow38/amiga-network-music/actions/workflows/tests.yml) |
**a preview until 1.0** (0.7: the latest release) | AmigaOS 3.x | 68020 or better | sound through AHI | MIT licence

## How it works

amisqueeze needs a **music server** on the local network. The server holds the music library, web radios and
streaming services, and streams them to its players. A web browser, a phone or a tablet chooses what plays where.
The server sees the Amiga as one more Squeezebox, through slimproto, the protocol of Squeezebox players.
amisqueeze receives the music, decodes it if needed and plays it through AHI.

```mermaid
flowchart LR
    ctl["Web browser,<br>phone, tablet"] -->|"chooses what plays"| srv["Music server<br>music library"]
    subgraph amiga ["Amiga"]
        tcp["TCP/IP stack<br>(bsdsocket.library)"] --> sq["amisqueeze"]
        sq --> ahi["AHI"]
        ahi --> out["Paula or<br>a sound card"]
    end
    srv -->|"slimproto, TCP 3483:<br>play, pause, volume..."| tcp
    srv -->|"audio stream, HTTP:<br>PCM, FLAC or MP3"| tcp
    sq -.->|"player state, elapsed time"| srv
```

## Supported servers

| Server | | Status |
|---|---|---|
| [Lyrion Music Server](https://lyrion.org) (LMS) | the free Squeezebox server, formerly Logitech Media Server; Windows, macOS, Linux, NAS, Raspberry Pi, Docker | **tested** (9.1.1) |
| [Music Assistant](https://music-assistant.io) | a server close to Home Assistant | **tested** (2.10.7) |

## What the Amiga needs

- **AmigaOS 3.x** and a **68020 or better**: from a plain 68020 accelerator to a PiStorm.
- About **1.7 MB of free memory**.
- A **TCP/IP stack** providing `bsdsocket.library`: Roadshow, Miami, AmiNetXDuo, AmiTCP, or WinUAE's emulation.
- **AHI**, the standard Amiga audio system, with the driver of Paula or of a sound card. It is free, on Aminet,
  but not part of AmigaOS 3.x.

## Quick start

1. **Download** the zip or the `.lha` of the latest
   [release](https://github.com/shadow38/amiga-network-music/releases).
2. **Copy the executable** with its icon (`.info`) into a drawer, or into `C:` to run it from a Shell:
   `amisqueeze` for any Amiga with a 68020 or better, `amisqueeze.fpu` for an Amiga with an FPU, which also decodes
   MP3.
3. **Set up AHI**: unit 0 in a "stereo++" mode in `SYS:Prefs/AHI` (with Paula: `Paula:14 bit stereo++`).
   Without "++", the music plays on one side only. See [sound](docs/user-guide.md#sound-ahi).
4. **Open the server's ports** to the Amiga: 3483 tcp and udp, and 9000/tcp for LMS or 8097/tcp for Music
   Assistant. MP3, or a slow Amiga, may need a setting on the server: see
   [server setup](docs/user-guide.md#server-setup).
5. **Start amisqueeze**, with the TCP/IP stack running: double-click its icon, or type `amisqueeze` in a Shell.
   It finds the server on the local network by itself. A player called "Amiga" then shows up in the server's web
   interface: choose it there or in an app, and play.

If no server is found, give its address: `amisqueeze SERVER=192.168.1.10`, or the ToolType of the same name. To
stop amisqueeze: Ctrl-C in its window, or Remove in Exchange. See
[starting](docs/user-guide.md#starting-amisqueeze) and [stopping](docs/user-guide.md#stopping-amisqueeze).

Everything else (all the options, sound settings, volume, messages) is in the **[user guide](docs/user-guide.md)**.

## Status

**amisqueeze** works:

- plays from LMS and Music Assistant: play, pause, seek, stop, next track without a gap, volume;
- raw PCM up to 48 kHz; FLAC and MP3 decoded on the Amiga when its CPU is fast enough (MP3 with
  `amisqueeze.fpu`), otherwise converted by the server;
- recovers from a slow or broken stream and from a server restart;
- starts from a Shell or from the Workbench, at boot too; Exchange lists it and stops it.

Tested in WinUAE and on a real A1200 with a 26 MHz 68020, where CD-quality playback takes about three quarters of
the CPU. The user guide lists [what has been tried](docs/user-guide.md#what-has-been-tried), the
[known limits](docs/user-guide.md#known-limits) and some [troubleshooting](docs/user-guide.md#troubleshooting).
What changed in each version: the [changelog](CHANGELOG.md).

**Tried it on another Amiga?** Please tell, whether it plays or not, in an
[issue](https://github.com/shadow38/amiga-network-music/issues). Give the CPU, the AmigaOS version, the TCP/IP
stack, the sound output and the server. With a problem, add a log: `amisqueeze VERBOSE LOG=RAM:amisqueeze.log`.

## For developers

Building (cross-compiler, `make`, `make dist`), the host build and its tests (`make test`, also run by GitHub on
each push), a build of the version in development (the `dev` branch), testing in WinUAE, the repository layout:
**[CONTRIBUTING.md](CONTRIBUTING.md)**. The roadmap and the design decisions are in the
[development plan](docs/development/README.md).

## Licence

MIT, see [LICENSE](LICENSE). amisqueeze is written from scratch from the protocol description; squeezelite and
LMS (GPL) are used as references for the protocol's behaviour, not as a source of code. Third-party code, in
`third_party/` (each with a README giving its origin and licence): dr_flac (FLAC decoding), public domain or MIT No
Attribution; minimp3 (MP3 decoding), CC0 1.0 (public domain dedication).
