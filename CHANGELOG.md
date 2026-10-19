# Changelog

What changed in each release of amisqueeze, the newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are those of the
[releases](https://github.com/shadow38/amiga-network-music/releases) (tags `amisqueeze-<version>`). All of them
are previews, tested in WinUAE only, not yet on a real Amiga.

## [Unreleased]

## [0.3] - 2026-10-20

### Added

- **MP3 decoded on the Amiga** by a second executable, **`amisqueeze.fpu`**, for Amigas with an FPU (68881/68882,
  or a 68040/68060 with its FPU, such as a PiStorm with Emu68). As with FLAC, it measures the speed at start-up
  (`MP3=AUTO`, the default; `MP3=ON`/`OFF` force it); LMS then sends MP3 files as they are, with no conversion rule
  needed on the server. `amisqueeze.fpu` refuses to start without an FPU.
- `BENCH` prints the MP3 speed too.
- The archive holds both executables, `amisqueeze` (any 68020 or better) and `amisqueeze.fpu`, and the user
  guide's images.

### Changed

- User guide: how to install and set up AHI (where to get it, which mode to choose, with a capture of its
  preferences), the server setup in a section of its own, shorter parts on decoding, `OUTPUT` and volume.
- README shortened for users, with a quick start; building and testing moved to `CONTRIBUTING.md`.
- The `mp3 pcm` rule for LMS given in the documentation no longer forces 44100 Hz (48 kHz MP3 files played 9 %
  too fast with it), and allows seeking.

### Known limits

- MP3 tracks are not yet joined without gaps: about 50 ms of silence at the start of each MP3 file.

## [0.2] - 2026-10-19

### Added

- **FLAC decoded on the Amiga** when the CPU is fast enough (PiStorm-class). amisqueeze measures it at start-up
  (`FLAC=AUTO`, the default; `FLAC=ON`/`OFF` force it) and then tells LMS to send FLAC as is, files above 48 kHz
  included. On slower Amigas LMS keeps converting FLAC to PCM.
- `BENCH`: prints the decoding speed and quits, without a server.
- **8, 24 and 32-bit PCM** (e.g. 24-bit FLAC converted by LMS), mono.

### Fixed

- `Version amisqueeze` found no version string (dropped at link time); it is also printed with `VERBOSE`.

## [0.1] - 2026-10-19

First release: a slimproto network music player for classic Amigas (AmigaOS 3.x, 68020 or better). It shows up in
Lyrion Music Server (LMS) as a player and plays through AHI.

### Added

- Raw 16-bit PCM streams sent by LMS. FLAC and WAV work with LMS's default conversion rules; MP3 needs an extra
  `mp3 pcm` rule on the server.
- Play, pause, seek, stop, gapless next track, volume and mute from LMS. Reconnects by itself.
- Options `SERVER`, `PORT`, `NAME`, `MAC`, `UNIT` (AHI unit), `SWAP`, `NOAUDIO`, `OUTPUT`, `VERBOSE`; default
  options in `ENV:amisqueeze.prefs`.

[Unreleased]: https://github.com/shadow38/amiga-network-music/compare/amisqueeze-0.3...HEAD
[0.3]: https://github.com/shadow38/amiga-network-music/compare/amisqueeze-0.2...amisqueeze-0.3
[0.2]: https://github.com/shadow38/amiga-network-music/compare/amisqueeze-0.1...amisqueeze-0.2
[0.1]: https://github.com/shadow38/amiga-network-music/releases/tag/amisqueeze-0.1
