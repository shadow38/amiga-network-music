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
  amisqueeze 0.3 (2026-10-20): the zip holds both executables (`amisqueeze`, `amisqueeze.fpu`), `CHANGELOG.md`
  (Keep a Changelog, at the root, since 0.3) and the user guide's images; the release notes end with a link to the
  changelog. amisqueeze 0.4 and 0.5 (2026-10-20): the same; since 0.5 the zip also holds the Workbench icons.
- **Continuous integration** (decided 2026-10-19): GitHub Actions, in two parts. First, done before 0.3: on each
  push, `make test` (host build, unit tests, fake server) on GitHub's Linux machine; the badge or the run's page
  tells whether a commit passes. Then, before 1.0: the Amiga build (the `amigadev/crosstools` image, the Docker mode of
  `tools/amiga-make.sh`, never tried so far) and the release archive made by GitHub from a tag.

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

## 3.5 Review before each release (decided 2026-10-20) 🔶

- **Why**: an outside review of version 0.4 (a fresh Claude session, reading only) found defects the
  development sessions had not seen: a stack measurement made before dr_flac came in and never redone, a sanitizer
  that reported without failing, tests that passed without the guard they aimed at, plan statuses not updated. None
  was hard to find; they slipped through because the same session wrote the code, wrote its tests and declared
  each step done. The review method comes from that outside review's recommendations.
- **Principles**: the writer is not the only reviewer (reviews run in fresh-context subagents); a claim needs a
  proof that can be opened (file:line, test, scenario, dated measurement); a measurement expires when what it
  measures changes; a safety net is only real once seen failing on a planted defect; a rule a script can check
  becomes a script; everything a server sends is untrusted; "not supported yet" has consequences for users.
- **When** (decided with the user on 2026-10-20): before **every** release tag, a full review of the range since
  the previous tag; findings scored 75 or more are fixed, or accepted by the user with the reason written in the
  report. Releases follow the plan's milestones, not single steps, so that reviews stay few. Exception: an urgent
  fix may ship after a review of its own diff only; the next release then gets a full review from the last fully
  reviewed tag.
- **Where**: the tooling in the repository (`.claude/`: one method skill, four review agents, the
  `release-review` entry point), the reports in `docs/development/reviews/`, the rules inventory in
  `docs/development/rules.md`. Experiments (planted defects, removed guards) only in throwaway git worktrees next
  to the repository, never committed, deleted afterwards.
- **Sub-steps**:
  - **3.5a** the method (`.claude/skills/review-method`), the agents `review-protocol-security`,
    `review-platform`, `review-tests-docs`, `review-plan-compliance` (`.claude/agents/`) and the entry point
    `/release-review <previous tag>`: pins the commit and the range, runs the four agents in parallel, verifies
    every finding (re-opens each cited line, tries to refute it, separates proven from inferred), writes the
    report, fixes nothing;
  - **3.5b** acceptance test: the review of `amisqueeze-0.3..amisqueeze-0.4` in a worktree, compared with the
    outside review of 0.4; each item missed sharpens a question of the method, and the review is replayed until
    it finds it. Since the development session knows that review's findings, the tooling is also judged on what
    it finds beyond them, on `0.4..0.5` and on HEAD;
  - **3.5c** the rules inventory: every rule, limit and measurement of `CLAUDE.md` and `CONTRIBUTING.md`, how it
    is enforced today (script, test, review, nothing), how it should be, and for a measurement its date, commit
    and what it depends on;
  - **3.5d** mechanical checks run by `make test`, proposed one by one (first candidates: version strings, every
    message in the user guide, ASCII-only sources, nothing specific to the developer's workstation);
  - **3.5e** the release rule in `CLAUDE.md`, then a review of HEAD against `amisqueeze-0.5`.
- **Criterion**: on `0.3..0.4` the review finds what the outside review found at 75 or more; its report on HEAD is
  handed to the user, each finding then fixed through a plan step or accepted.
