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

## 3.3 Public versions ⬜

- **Content**: Git version tags, release notes, public repository up to date.
- **Criterion**: each published version matches a tag and release notes.
