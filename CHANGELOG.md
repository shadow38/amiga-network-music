# Changelog

What changed in each release of amisqueeze, the newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are those of the
[releases](https://github.com/shadow38/amiga-network-music/releases) (tags `amisqueeze-<version>`). All of them
are previews, tested in WinUAE only, not yet on a real Amiga.

## [Unreleased]

### Changed

- User guide: the AmigaOS versions tried. Besides 3.2.3, amisqueeze was run with AmigaOS 3.0 and 3.1 ROMs from a
  Shell, without AHI (in WinUAE): options, speed measure, connection, FLAC and MP3 with a seek, `LOG`. It even
  starts with 2.04.
- User guide: every message shown without `VERBOSE` is now in the table of messages (start-up line, `retrying in
  5 s`, network and stream errors, a command line error, the output of `BENCH`), with an example of `BENCH`.
- User guide: a known limit, internet radios. Their metadata is not handled yet: an MP3 radio fetched by
  `amisqueeze.fpu` itself may cut regularly, with the message `cont not handled`.

### Fixed

- The example `WINDOW` ToolType of the icons now ends in `/SMART`, like the default window: without it, a window
  made from the example lost its text whenever it was uncovered.

- A track whose decoder cannot start (not enough memory) is skipped, as the user guide says: until now, more of
  that track arriving made the decoder run without its state (a crash or a memory corruption on the Amiga).

## [0.5] - 2026-10-20

### Added

- **`LOG=<file>`**: a copy of every message in a file while they still appear in the window; each run is appended
  after a line with its date and time. A log that cannot be opened or written does not stop the music.
- **Start from the Workbench**: an icon for each executable. Double-clicked, amisqueeze reads its options in the
  icon's ToolTypes (the same as the command line; `WINDOW` sets the message window, `QUIET` opens none) and shows
  its messages in a window.
- **A commodity**: amisqueeze shows in Exchange, whose "Remove" stops it, with or without a window. One runs at a
  time: started again, the running one goes on and the new one quits.

### Fixed

- **`amisqueeze` overflowed its stack when started from a Shell** with the usual 4 KB stack: the FLAC decoder
  needs about 5.5 KB, already at start-up (speed measure). AmigaOS does not detect it: a crash or a hang could
  follow at any later time. Both executables now set up the stack they need themselves (16 KB for `amisqueeze`,
  32 KB for `amisqueeze.fpu`, which already did). Broken since 0.2.
- **Seeking in a FLAC track decoded on the Amiga** skipped the track (`flac: not a FLAC stream, track skipped`),
  and so did LMS resuming a FLAC track when the player reconnects: LMS then sends the file from the middle,
  without its header. Broken since FLAC decoding came in (0.2).
- An MP3 file whose LAME header counts 0 frames (a broken header) would have been decoded and then dropped whole,
  in silence: it now plays in full. Broken since 0.4.

## [0.4] - 2026-10-20

### Added

- **MP3 tracks follow each other without a gap** (`amisqueeze.fpu`): the silence the encoder adds at the start and
  the end of each file is dropped, as told by the header it writes in the first frame (LAME and compatible
  encoders). Files without such a header play as before.
- **Fixed volume**: LMS now offers its "Volume Control" setting for amisqueeze (Settings, the player, Audio):
  "Output level is fixed at 100%" plays at full volume whatever the slider, which LMS's interfaces grey out; the
  samples are not scaled, saving CPU time on a slow Amiga and avoiding Paula's noise at low volumes. amisqueeze
  now advertises `HasDigitalOut=1`, as squeezelite does.
- **A warning when the AHI unit is set to a mode that would not play the music right**: a stereo mode without
  "++" (one side only) or a mono mode. With `VERBOSE`, the unit's mode and frequency are printed at start-up.

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

[Unreleased]: https://github.com/shadow38/amiga-network-music/compare/amisqueeze-0.5...HEAD
[0.5]: https://github.com/shadow38/amiga-network-music/compare/amisqueeze-0.4...amisqueeze-0.5
[0.4]: https://github.com/shadow38/amiga-network-music/compare/amisqueeze-0.3...amisqueeze-0.4
[0.3]: https://github.com/shadow38/amiga-network-music/compare/amisqueeze-0.2...amisqueeze-0.3
[0.2]: https://github.com/shadow38/amiga-network-music/compare/amisqueeze-0.1...amisqueeze-0.2
[0.1]: https://github.com/shadow38/amiga-network-music/releases/tag/amisqueeze-0.1
