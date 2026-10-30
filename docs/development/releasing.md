# Releasing

For the maintainer: how a version is numbered, checked and published. Building the archives is in
[building.md](building.md#building).

**Versions**: 0.x until the main feature works; 1.0 is the first really usable version, tried on the real machine.
The version changes only for a published release. Between releases the number reads `<next>-dev`, so that a build
in between is not taken for the last release. The number appears in the two `$VER:` strings of `amiga.c`
(`amisqueeze` and `amisqueeze.fpu`), in `Firmware=` of the `HELO` capabilities (`slimproto.c`) and in the user
guide; `tools/check-version.py` checks that they agree (see [testing.md](testing.md#host-build-and-tests)). The
`$VER:` date is the release date.

**`CHANGELOG.md`** follows [Keep a Changelog](https://keepachangelog.com/): each user-visible change goes under
"Unreleased" as it is made; at the release, that part gets the version number and the date.

**A release** is a Git tag `amisqueeze-<version>` and a GitHub Release with the zip and the `.lha` (the same
files: see `make dist` in [building.md](building.md#building)), the `.sha256` of each, the Aminet `.readme`, and
short notes holding each archive's SHA-256.

**Before the tag**, on the build to be tagged:

1. a review of the release with Claude Code (plan step publish-5, the tools in `.claude/`): a light review of the
   range since the previous tag, `/release-review <previous tag>` (three agents, limited to the diff and what it
   reaches); at milestones only (before 1.0, after a big change of architecture such as Music Assistant or
   splitting `stream.c`), a full audit, `/release-review <previous tag> full` (four agents, the whole repository).
   Its report goes in `docs/development/reviews/`. Findings scored 75 or more are fixed, or accepted by the
   maintainer with the reason in the report; an urgent fix may ship after a review of its own diff. Remarks that
   come after a version's review (outside readings, new ideas) go to the next version, unless they show a real
   defect or the maintainer chooses otherwise;
2. `python3 tools/winuae/run-scenarios.py` all OK in WinUAE (JIT, 68882 with `compfpu`):
   [winuae-scenarios.md](winuae-scenarios.md);
3. `Version <exe> FULL` for both executables in WinUAE (see `$VER:` in [pitfalls.md](pitfalls.md));
4. the stack measured again, when [Stack and memory](code-rules.md#stack-and-memory) says so;
5. the "Unreleased" part of `CHANGELOG.md` given the version and the date, the version number set everywhere
   (`make dist` refuses anything else).

**The archives are made by GitHub from the tag** (`.github/workflows/release.yml`, since 0.7): pushing a tag
`amisqueeze-<version>` runs `make dist` through the Docker way of `tools/amiga-make.sh` (the pinned image), checks
that the archives bear the tag's version, and makes a **draft** pre-release with the archives, their `.sha256` and
the readme, its notes holding the SHA-256 lines and a place for the rest. The notes describe this version only,
its known limits as plain facts: nothing about later versions (agreed with the maintainer on 2026-10-23; the
roadmap stays in the plan). The executables' SHA-256 are in the job's
summary: the build is reproducible, so they must be those of the build tried in WinUAE before the tag. The draft is
published by hand once `tests.yml` is green on the tag (`release.yml` does not wait for it), once its notes are
written and approved, and, when the making of the archive changed since its
last trial on the Amiga (the image or the version of `lha`, the options of `make-dist.py`, a new kind of file in
it), once the draft's own `.lha` has been extracted by LhA in WinUAE: the files there with their protection bits,
the executables' SHA-256 those of the build tried before the tag, `Version <exe> FULL` for both (the scenarios
need not run again: the executables are the same); otherwise the checks of `make dist` are enough. Run by hand
(Actions, "release", "Run workflow"), the workflow is a trial: the development archives in a draft marked never to
publish, to delete afterwards (a draft's tag is created only when it is published).

**Branches**: the tag is made on `dev`. Once the release is published, `main` moves forward to it, without a merge
commit: `git push origin amisqueeze-<version>^{commit}:main`. So `main`, the repository's page and its README
always describe the version that can be downloaded. A ruleset of the repository refuses on `main` a force push, a
deletion, and a commit whose CI (the `host` and `amiga` jobs) has not passed: wait for the tag's CI first. A job
renamed in `tests.yml` must be renamed in the ruleset too (Settings, Rules). A fix to a published version starts from its tag, in a branch
of its own; once released, it is merged into `dev` too.

**Aminet** (from 1.0): the files are renamed without the version (Aminet's rule), and the readme's `Uploader:`
gets an e-mail address; the `.lha` to be uploaded is first extracted by LhA in WinUAE, the scenarios run on what
it gives.
