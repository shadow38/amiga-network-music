# Phase 3 — Release

Back to the [plan](README.md).

## 3.1 Licence and user documentation 🔶

- **Done**: licence chosen, MIT (2026-10-18). Third-party code must be MIT-compatible (no GPL code, e.g. from
  squeezelite or libmad). User guide started: [docs/user-guide.md](../user-guide.md), kept up to date with each
  step.
- **Content**: user guide (Amiga-side requirements: TCP/IP stack, AHI; server side: ports,
  conversion rule if needed; program options).
- **Criterion**: a user who does not know the project installs and runs amisqueeze using the guide alone.

## 3.2 Aminet archive ⬜

- **Content**: `.lha` archive (executables, icons, AmigaGuide or text documentation), Aminet-format `.readme`,
  version numbers (`$VER:`).
- **Criterion**: the archive installs on a clean machine (WinUAE) and the programs work.

## 3.3 Public versions 🔶

- **Content**: Git version tags, release notes, public repository up to date.
- **Criterion**: each published version matches a tag and release notes.
- **Done**: amisqueeze 0.1 and 0.2 (2026-10-19), each a tag `amisqueeze-<version>` and a GitHub release (marked
  as a pre-release) holding the zip of `make dist` (`tools/make-dist.py`: the executables, `LICENSE`, the README
  and the user guide) and short notes approved by the user; between releases the version reads `<next>-dev`.
  From 0.3 on, the zip holds both executables (`amisqueeze`, `amisqueeze.fpu`).
- **To decide**: continuous integration with GitHub Actions (host tests on each push, then the Amiga build for
  releases), discussed before 0.1; see the open points of the [plan](README.md#open-points-review-of-2026-10-19).

## 3.4 Setup guides: server and network (noted 2026-10-19) ⬜

- **Why**: amisqueeze is useless without a server, and many Amiga users have never installed LMS; putting the
  Amiga on the network is often the real obstacle. Without these guides few users will get as far as the player,
  whatever its quality. Before 1.0.
- **Where** (decided 2026-10-19): everything in `docs/`, not in a GitHub wiki: one place that moves with the code
  (versioned, reviewed by commit, in the release archive), and GitHub renders Mermaid diagrams in the repository's
  Markdown files too. A summary page `docs/README.md` links the guides, short pages linked to each other, so that
  `docs/` reads like a small site (GitHub Pages could later publish it as is, without copying anything).
- **Diagrams** (Mermaid): the overall picture (sources, server, network, amisqueeze, AHI, Paula or a DAC, a phone
  driving the server); the path of a track (format decoded by amisqueeze: sent as is; otherwise a conversion rule
  on the server: converted to PCM; otherwise refused), with a table of the formats each executable decodes, on
  which machine, and which rule is still needed (e.g. MP3 to PCM); network and ports (3483, the web port, 9090,
  port mapping with Docker).
- **Content**, two guides in `docs/`, linked from the user guide:
  - **minimal server**: Lyrion Music Server (then Music Assistant, after 1.7b) on a Raspberry Pi, a NAS or in Docker,
    with only what amisqueeze needs: ports (3483, the web port), the conversion rules still needed (e.g. MP3 to PCM
    for an Amiga without FPU, which does not decode MP3 itself), adding music, checking that the Amiga's player shows up;
  - **the Amiga on the network**: a TCP/IP stack (Roadshow, AmiTCP, AmiNetXDuo), the usual hardware (PCMCIA network
    card on an A1200, network on accelerators or a PiStorm), checking the connection (`ping`, server reachable),
    then AHI (Paula or a sound card's driver), ending with amisqueeze started and heard.
- **Criterion**: a user who does not know the project goes, with the guides alone, from a fresh server and a
  networked Amiga without amisqueeze to music playing; checked at least once in WinUAE on a clean installation.
