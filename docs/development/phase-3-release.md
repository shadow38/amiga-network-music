# Phase 3 — Release

Back to the [plan](README.md).

## publish-1 Licence and user documentation 🔶

- **Done**: licence chosen, MIT (2026-10-18). Third-party code must be MIT-compatible (no GPL code, e.g. from
  squeezelite or libmad). User guide started: [docs/user-guide.md](../user-guide.md), kept up to date with each
  step.
- **Content**: user guide (Amiga-side requirements: TCP/IP stack, AHI; server side: ports,
  conversion rule if needed; program options).
- **Criterion**: a user who does not know the project installs and runs amisqueeze using the guide alone.

## publish-2 Aminet archive 🔶

- **Content**: `.lha` archive (executables, icons, AmigaGuide or text documentation), Aminet-format `.readme`,
  version numbers (`$VER:`).
- **In two parts** (decided with the user on 2026-10-22):
  - **with 0.7**: `make dist` also builds `out/<prog>-<version>.lha` (the same files as the zip) with its
    `.sha256`, and the Aminet `.readme` (`Short:`, `Author:`, `Uploader:`, `Type: mus/play`, `Version:`,
    `Architecture:`, `Requires:`, then the description), all attached to the GitHub Release. `Author:` and
    `Uploader:` read `shadow38` for now: **to correct (name, e-mail address) before the upload to Aminet**. To
    decide when designing it, proposed: the archive made by `lha` (jca02266/lha, built in the build environment;
    `lhasa` only extracts), the user guide shipped as text as it is (AmigaGuide later, if needed);
  - **with 1.0**: the upload to Aminet (`.lha` and `.readme`), by the user or at their explicit request. Before
    it (decided with the user on 2026-10-23): the files renamed without the version, `amisqueeze.lha` and
    `amisqueeze.readme` (Aminet: "Version numbers should be part of the readme file, not part of the filename";
    an update is uploaded again under the same name; the content, so the SHA-256, unchanged); a drawer icon for
    the archive's directory, which the Workbench does not show without one (in the zip too, `tools/make-icon.py`
    to extend); `Uploader:` with an e-mail address, which Aminet requires.
- **Done with 0.7** (2026-10-23, decided with the user that day): `make dist` builds `out/<prog>-<version>.lha`
  with `lha` of the image `amigadev/crosstools` (LHa for UNIX 1.14i, jca02266: nothing to build), method `-lh5-`
  and header level 1 (the default of LhA 2.15 on the Amiga), from the same files as the zip copied with fixed
  modes (755 for the executables, 644 for the rest); it tests it (`lha t`), compares its files and sizes with the
  zip's, and writes its `.sha256`. The readme is `dist/amisqueeze.readme` with its version filled in, checked
  against Aminet's rules ([The Readme file](https://wiki.aminet.net/The_Readme_file): `Short:` first and of 40
  characters at most, the required fields, lines of 78 characters at most, LF; ASCII besides), by `make dist`
  and by `make test`. `make dist DEV=1` accepts a development version, to try them. Tried on the 0.7-dev build:
  extracted by LhA 2.15 (Aminet `util/arc/lha.run`) in RAM: of a 3.2.3 system that had no LhA (all the files
  `----rwed`, both executables started from there: `Version`, `BENCH`), by `lhasa` 0.4.0 on Linux and by
  Windows' `tar`, each time the twelve files identical to the sources; the executables extracted on the Amiga
  put in `NM:`, and the WinUAE scenarios (tools-10) all OK on them.
- **When to try it again on the Amiga** (decided with the user on 2026-10-23): not at every release, whose
  archives `make dist` checks (`lha t`, the files and sizes against the zip's, the readme, the SHA-256) and whose
  executables the scenarios test; only when the making of the archive changes (another image or version of
  `lha`, other options in `make-dist.py`, a new kind of file such as the drawer icon), and once before the Aminet
  upload, on the archive to be uploaded: LhA in a system that has none, extraction into `RAM:`, protection bits,
  `Version` and `BENCH` from there, the files compared with the sources, the scenarios on the extracted
  executables.
- **Criterion**: the archive extracts on a clean system in WinUAE (LhA) and on the host (`lhasa`), and the WinUAE
  scenarios (tools-10) pass on what it holds; at 1.0, the archive is on Aminet.

## publish-3 Public versions 🔶

- **Content**: Git version tags, release notes, public repository up to date.
- **Criterion**: each published version matches a tag and release notes.
- **Done**: amisqueeze 0.1 and 0.2 (2026-10-19), each a tag `amisqueeze-<version>` and a GitHub release (marked
  as a pre-release) holding the zip of `make dist` (`tools/make-dist.py`: the executables, `LICENSE`, the README
  and the user guide) and short notes approved by the user; between releases the version reads `<next>-dev`.
  amisqueeze 0.3 (2026-10-20): the zip holds both executables (`amisqueeze`, `amisqueeze.fpu`), `CHANGELOG.md`
  (Keep a Changelog, at the root, since 0.3) and the user guide's images; the release notes end with a link to the
  changelog. amisqueeze 0.4 and 0.5 (2026-10-20): the same; since 0.5 the zip also holds the Workbench icons.
  amisqueeze 0.6 (2026-10-21): the same, and the zip's SHA-256 in the notes and in a `.sha256` file, computed by
  hand; since 0.7 (2026-10-21) `make dist` writes that file (format of `sha256sum`, checked with `sha256sum -c`)
  and prints the line for the notes: the zip, its `.sha256` and the notes come from the same run.
- **Scenarios in WinUAE** (decided with the user on 2026-10-23, from step tools-10): before each tag, besides the
  review (publish-5), `python3 tools/winuae/run-scenarios.py` all OK on the build to be tagged, in the JIT profile with
  a 68882 and `compfpu` ([winuae-scenarios.md](winuae-scenarios.md)); first for 0.7.
- **Continuous integration** (decided 2026-10-19): GitHub Actions, in two parts. First, done before 0.3: on each
  push, `make test` (host build, unit tests, fake server) on GitHub's Linux machine; the badge or the run's page
  tells whether a commit passes. Then, before 1.0: the Amiga build (the `amigadev/crosstools` image, the Docker mode of
  `tools/amiga-make.sh`; ✅ done on 2026-10-23, brought forward to 0.7 by the user after an outside reading: a second
  job runs `sh tools/amiga-make.sh all dist DEV=1`, with the image pinned by its digest, then prints the
  executables' SHA-256; its `.lha` and `.zip` kept 14 days as the run's artifacts, each on its own and not wrapped in a zip, decided with the user on 2026-10-23 for
  0.7 after an outside reading, so that a development build can be tried without building it) and the release
  archive made by GitHub from a tag (✅ done on 2026-10-23, brought forward to 0.7 by the user: `release.yml`,
  on a tag `amisqueeze-*`, makes a draft pre-release with the archives of `make dist`, their `.sha256` and the
  readme; the user writes the notes, compares the executables' SHA-256 with the build tried in WinUAE and
  publishes; run by hand, a trial in a draft to delete). From an outside reading the same day: the actions pinned
  by commit SHA, the checkout without a token (`persist-credentials: false`, the build's container mounts the
  repository); an attestation of provenance left for 1.0.

## publish-4 Setup guides: server and network (noted 2026-10-19) ⬜

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
  - **minimal server**: Lyrion Music Server (then Music Assistant, after player-7b) on a Raspberry Pi, a NAS or in Docker,
    with only what amisqueeze needs: ports (3483, the web port), the conversion rules still needed (e.g. MP3 to PCM
    for an Amiga without FPU, which does not decode MP3 itself), adding music, checking that the Amiga's player shows up;
  - **the Amiga on the network**: a TCP/IP stack (Roadshow, AmiTCP, AmiNetXDuo), the usual hardware (PCMCIA network
    card on an A1200, network on accelerators or a PiStorm), checking the connection (`ping`, server reachable),
    then AHI (Paula or a sound card's driver), ending with amisqueeze started and heard.
- **Criterion**: a user who does not know the project goes, with the guides alone, from a fresh server and a
  networked Amiga without amisqueeze to music playing; checked at least once in WinUAE on a clean installation.

## publish-5 Review before each release (decided 2026-10-20) ✅

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
  - **publish-5a** the method (`.claude/skills/review-method`), the agents `review-protocol-security`,
    `review-platform`, `review-tests-docs`, `review-plan-compliance` (`.claude/agents/`) and the entry point
    `/release-review <previous tag>`: pins the commit and the range, runs the four agents in parallel, verifies
    every finding (re-opens each cited line, tries to refute it, separates proven from inferred), writes the
    report, fixes nothing;
  - **publish-5b** acceptance test: the review of `amisqueeze-0.3..amisqueeze-0.4` in a worktree, compared with the
    outside review of 0.4; each item missed sharpens a question of the method, and the review is replayed until
    it finds it. Since the development session knows that review's findings, the tooling is also judged on what
    it finds beyond them, on `0.4..0.5` and on HEAD. First run on 2026-10-20
    ([report](reviews/acceptance-0.3-0.4.md)): of the points the outside review lists as done before 0.5, all
    found at 75 or more except a frame count of 0 (seen, scored 50, not run); missed: `gethostbyname()`'s result
    unchecked, the CI workflow's `permissions:`, two false comments; found in part: what `make dist` checks, the
    malformed-input tests; found beyond it: step player-2's claim on `cont`, a FLAC rate of 0 stalling a track, gaps in
    `check-saves.py`. The seven method questions (section 10) decided with the user on 2026-10-20 and the method
    and agents changed; a first replay the same day ran the old agent definitions (loaded when the session starts),
    so it tested the method changes only (section 11): the pointer rule and the false-comment rule worked, the
    frame count of 0 was not found in that run; found beyond: `strm f` stops the playing track. A second replay
    the same day, from a new session, ran the changed questions of `review-platform`, `review-tests-docs` and
    `review-protocol-security` (section 12): `permissions:`, what `make dist` checks (75), the false comments and
    the malformed-input tests found; `gethostbyname()` reached but listed at "25-50", off the scale (counted as
    found by the user; the method now asks for one score, never a range); the frame count of 0 and `strm f` not
    found in that run. Review of `0.4..0.5` on 2026-10-20, the four agents, blind
    ([report](reviews/acceptance-0.4-0.5.md)): eleven findings at 75, six beyond the outside review and the
    earlier passes (a decoder that cannot start leaves a NULL state, proven on the host; QUIET with LOG loses the
    start-up errors; the 0.5 seek fix's rejection side never runs; workstation references; the icon's `WINDOW`
    without `SMART`; a direct `printf`); of the outside review's points fixed in 0.5, the guide's messages and the
    plan's statuses found incomplete, the LAME bounds checks partly tested, the rest confirmed; the FLAC rate of 0
    seen but left unrun. Criterion met for `0.4..0.5` (decided with the user, method unchanged; the direct `printf`
    accepted, the `msg()` rule made explicit). No separate review of HEAD (see publish-5e): the first review in real use,
    `0.5..0.6`, is the tooling's last judge;
  - **publish-5c** the rules inventory: every rule, limit and measurement of `CLAUDE.md` and `CONTRIBUTING.md`, how it
    is enforced today (script, test, review, nothing), how it should be, and for a measurement its date, commit
    and what it depends on. Done on 2026-10-20 ([rules.md](rules.md)): 88 entries in ten groups, with the findings of
    the two acceptance reports linked to them; twelve candidates for publish-5d; twelve remarks (stale figures and
    names, the scope of the ASCII rule, the `msg()` exception) left for the user to decide;
  - **publish-5d** mechanical checks run by `make test`, proposed one by one (first candidates: version strings, every
    message in the user guide, ASCII-only sources, nothing specific to the developer's workstation). Each check:
    proposed, accepted, written, seen failing on planted defects in a throwaway worktree, seen passing on the
    repository. Done: (1) the version strings, `tools/check-version.py` (2026-10-20; `rules.md` V4 to V7, V9):
    run by `make test`, and with `--release` by `make dist` through `make-dist.py`; accepted the 0.3, 0.4 and 0.5
    tags as releases, refused 17 planted defects and HEAD itself with `--release`, also through `make test` and
    `make dist` themselves. Closed there on 2026-10-20 (decided with the user: the step was turning into
    over-quality); the other candidates of `rules.md` stay as ideas, not planned;
  - **publish-5e** the release rule in `CLAUDE.md` (done on 2026-10-20; in `CONTRIBUTING.md`, Releasing, since
    2026-10-23). The agents and the method read
    [rules.md](rules.md) and name a rule by its ID (`rules.md C12`). The review of HEAD planned here was dropped
    (decided with the user on 2026-10-20: since 0.5, HEAD changed only documentation and review tooling): the next
    review is the one before the 0.6 tag, `/release-review amisqueeze-0.5`, whose launch prompt adds "no `HEAD`
    in git commands" and "logs in your own worktree" ([decided](reviews/acceptance-0.4-0.5.md#8-decisions)). The
    remarks of `rules.md` stay written, undecided; that review reports those that matter. The findings of the two
    acceptance reports still open are fixed in one batch, listed with the user.
- **Stop** (decided with the user on 2026-10-20): step publish-5 is over. From now on each release gets its review and
  nothing more; the tooling (method, agents, checks) changes only when a review misses a real defect, never for its
  own sake.
- **Two levels** (decided with the user on 2026-10-20, after the 0.6 fixes: a full review costs about 10-20 $ and
  1 h 30 for each release, and repeats the same whole-repository inventories): a light review for each release
  (`/release-review <previous tag>`: three agents, limited to the diff and what it reaches, the questions marked
  *(full)* skipped, the plan's statuses checked by `review-tests-docs`), a full audit at milestones only (`full`:
  before 1.0, after a big change of architecture). A stale statement in the internal documents scores 50 at most
  unless it would lead a reader to a wrong action; `rules.md` is frozen as a reference.
- **Criterion**: on `0.3..0.4` the review finds what the outside review found at 75 or more (met); on `0.4..0.5`
  it finds defects beyond the outside review (met: six).
