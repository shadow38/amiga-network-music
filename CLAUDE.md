# CLAUDE.md — amiga-network-music

For Claude Code. What a human contributor needs is in the repository's own documents, not here: `README.md` (for
users, kept short), `docs/user-guide.md` (options, messages, server setup), `CONTRIBUTING.md` (building, tests,
code rules, testing in WinUAE, releasing, pitfalls), `docs/development/architecture.md` (how amisqueeze works),
`docs/development/testing.md` (the WinUAE test machine), `docs/development/README.md` (the plan: roadmap, where
each step stands, open points). This file keeps only how to work with the maintainer, the rules never to break,
and what to read before touching what. Public GitHub repository `shadow38/amiga-network-music`; documentation,
code comments and program messages in English.

## Working with the maintainer

- Work proceeds in testable steps, named `tools-<n>`, `player-<n>`, `remote-<n>`, `publish-<n>` (never a number
  that reads as a version; until 2026-10-23 `<phase>.<n>`, kept in older review reports and commits). The content
  and success criterion of a step are agreed with the user **before** any code. A finished step: criterion checked,
  documentation up to date, one commit (one per item when a step gathers independent items). Decisions are
  recorded in the plan, once, briefly and dated ("agreed with the user on <date>"); how a step was checked goes in
  its commit message, not in the plan.
- Which version brings which steps, and where each step stands: "Roadmap" and "Where we are" in
  `docs/development/README.md`, the only place they are kept; keep the statuses up to date.
- Each user-visible change: the user guide updated and a line under "Unreleased" in `CHANGELOG.md`, as it is made.
- `make test` after every change to the core; extend the tests with each step.
- Never implement an "Open point" of the plan unasked. No amilms code before its study (step remote-0).
- Always the user's decision, never ours: bumping the version; publishing a release (the CI makes a draft from the
  tag); changing the pinned image (`REFERENCE_IMAGE`); changing the review tooling (`.claude/`), except when a
  review missed a real defect.
- Before a release tag: `CONTRIBUTING.md`, Releasing. Findings scored 75 or more are fixed, or accepted by the user
  with the reason in the report. Remarks that come after a version's review (outside readings, new ideas) go to
  the next version by default; only a real defect, or the user's choice, brings one into the current version
  (against over-quality).
- After a push: watch the CI run in the background, without waiting for it (`gh run list --commit <sha>` for its
  id, then `gh run watch <id> --exit-status`), and speak of it only when it fails. A failure may come from a slow
  runner (the tests use real time): rerun before suspecting the code, and say so.
- Before testing on the Amiga or in WinUAE: `CONTRIBUTING.md`, "Checks the host tests cannot make".

## Never

One line each; the pointer says where it is explained.

- Nothing specific to the developer's workstation in the repository (local paths, corporate proxy, personal
  settings, neighbouring folders, how the toolchain is installed on one machine): those live in local files
  outside it.
- No code copied from squeezelite or LMS (GPL; the project is MIT): read them to understand the protocol, then
  write our own. Third-party code only under an MIT-compatible licence (`CONTRIBUTING.md`, Proposing a change and
  Code rules).
- No `ahir_Volume` below full (`architecture.md`, AHI).
- No mixing of the ring's positions and the output's (`architecture.md`, Two kinds of position).
- No big buffer on the stack, and no stack measurement from a Shell with a large stack (`CONTRIBUTING.md`, Stack
  and memory).
- No `printf`, `Printf` or `PutStr` called directly: `msg()` and `msg_raw()`, except inside the platform layers'
  own implementation of `msg()`, `msg_raw()` and the `LOG` file (`CONTRIBUTING.md`, Code rules).
- No const `$VER:` string (`CONTRIBUTING.md`, Pitfalls).
- Nothing but ASCII and LF line endings in what the Amiga shows or reads, the README and the user guide included
  (`CONTRIBUTING.md`, Code rules).
- No refusal (`STMn`) of a `strm s` before its `STMc`: LMS drops every status after a `strm s` until `STMc`, and
  then waits for that track forever (`architecture.md`, A track's life).
- No measurement on the server's web page: through its CLI (`CONTRIBUTING.md`, Checks the host tests cannot make).
- No Kickstart ROM in the repository (licensed).
- No work committed to `main`: it is the last release, moved forward only at a release; work on `dev`
  (`CONTRIBUTING.md`, Releasing).

## Before touching X, read Y

| Before touching | Read |
|---|---|
| the portable core (`src/amisqueeze/` but `amiga*.c`, `posix.c`) | `architecture.md`; `CONTRIBUTING.md`, Code rules |
| `stream.c`, `slimproto.c` (statuses, `strm`) | `architecture.md`, A track's life and Slimproto: the messages |
| `decoder.c`, `flac.c`, `mp3.c`, `third_party/` | `architecture.md`, Decoders; `CONTRIBUTING.md`, Code rules and Stack and memory |
| `audio.c`, `amiga_ahi.c` | `architecture.md`, AHI |
| the start-up of `amiga.c`, `__stack`, libnix | `architecture.md`, The Amiga side; `CONTRIBUTING.md`, Stack and memory and Pitfalls |
| `Makefile`, `tools/check-saves.py`, `tools/check-stack.py`, `tools/amiga-make.sh` | `CONTRIBUTING.md`, Requirements, Building, and Stack and memory |
| `tests/`, `.github/workflows/` | `CONTRIBUTING.md`, Host build and tests |
| the version number, `CHANGELOG.md`, `make dist`, a release | `CONTRIBUTING.md`, Releasing |
| anything tried in WinUAE | `CONTRIBUTING.md`, Testing in WinUAE; `docs/development/testing.md`, `winuae-scenarios.md` |
| options, messages | `docs/user-guide.md` |
| any documentation (`*.md`, the plan included) | `CONTRIBUTING.md`, Writing documentation |
| `.claude/` (the review tooling) | `.claude/skills/review-method/SKILL.md`; plan step publish-5 (`phase-3-release.md`) |
| amilms | `docs/development/phase-2-amilms.md` |
