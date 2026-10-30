# Releasing

For the maintainer: how a version is numbered, checked and published. Building the archives is in
[building.md](building.md#the-archives-make-dist).

## In short

1. Run the checks before the tag, on the build to be tagged ([below](#before-the-tag)).
2. Push the tag `amisqueeze-<version>`: GitHub makes a draft release with the archives.
3. Once the CI is green on the tag and the notes are written, publish the draft by hand
   ([publishing the draft](#publishing-the-draft)).
4. Move `main` forward to the tag.

## Versions

- 0.x until the main feature works. 1.0 is the first really usable version, tried on the real machine.
- The version changes only for a published release. Between releases the number reads `<next>-dev`, so that a
  build in between is not taken for the last release.
- The number appears in:
  - the two `$VER:` strings of `amiga.c` (`amisqueeze` and `amisqueeze.fpu`);
  - `Firmware=` of the `HELO` capabilities (`slimproto.c`);
  - the user guide.

  `tools/check-version.py` checks that they agree (see [testing.md](testing.md#host-build-and-tests)).
- The `$VER:` date is the release date.

**`CHANGELOG.md`** follows [Keep a Changelog](https://keepachangelog.com/). Each user-visible change goes under
"Unreleased" as it is made. At the release, that part gets the version number and the date.

## What a release is

- a Git tag `amisqueeze-<version>`;
- a GitHub Release with:
  - the zip and the `.lha`, which hold the same files ([the archives](building.md#the-archives-make-dist));
  - the `.sha256` of each, and the Aminet `.readme`;
  - short notes holding each archive's SHA-256.

## Before the tag

On the build to be tagged:

1. **A review of the release** with Claude Code (plan step publish-5, the tools in `.claude/`):
   - by default, a light review of the range since the previous tag: `/release-review <previous tag>` (three
     agents, limited to the diff and what it reaches);
   - at milestones only, a full audit: `/release-review <previous tag> full` (four agents, the whole repository).
     The milestones: before 1.0, after a big change of architecture such as Music Assistant or splitting
     `stream.c`.

   Its report goes in `docs/development/reviews/`. Then:
   - findings scored 75 or more are fixed, or accepted by the maintainer with the reason in the report;
   - an urgent fix may ship after a review of its own diff;
   - remarks that come after a version's review (outside readings, new ideas) go to the next version, unless they
     show a real defect or the maintainer chooses otherwise.
2. **The scenarios in WinUAE**: `python3 tools/winuae/run-scenarios.py` all OK (JIT, 68882 with `compfpu`). See
   [winuae-scenarios.md](winuae-scenarios.md).
3. **`Version <exe> FULL`** for both executables in WinUAE (see `$VER:` in [pitfalls.md](pitfalls.md)).
4. **The stack** measured again, when [Stack and memory](code-rules.md#stack-and-memory) says so.
5. **The version**: the "Unreleased" part of `CHANGELOG.md` given the version and the date, the version number set
   everywhere. `make dist` refuses anything else.

## The draft made by GitHub

The archives are made by GitHub from the tag (`.github/workflows/release.yml`, since 0.7). Pushing a tag
`amisqueeze-<version>`:

- runs `make dist` through the Docker way of `tools/amiga-make.sh`, with the pinned image;
- checks that the archives bear the tag's version;
- makes a **draft** pre-release with the archives, their `.sha256` and the readme. Its notes hold the SHA-256
  lines and a place for the rest.

**The notes** describe this version only, its known limits as plain facts. Nothing about later versions (agreed
with the maintainer on 2026-10-23; the roadmap stays in the plan).

**The executables' SHA-256** are in the job's summary. The build is reproducible: they must be those of the build
tried in WinUAE before the tag.

## Publishing the draft

The draft is published by hand, once:

- `tests.yml` is green on the tag (`release.yml` does not wait for it);
- its notes are written and approved;
- when the making of the archive changed since its last trial on the Amiga, the draft's own `.lha` has been
  tried in WinUAE (below).

**When the making of the archive changed**: a new image or version of `lha`, new options of `make-dist.py`, a new
kind of file in it. Then extract the draft's `.lha` with LhA in WinUAE, and check:

- the files there, with their protection bits;
- the executables' SHA-256: those of the build tried before the tag;
- `Version <exe> FULL` for both.

The scenarios need not run again: the executables are the same. Otherwise, the checks of `make dist` are enough.

**A trial of the workflow**: run by hand (Actions, "release", "Run workflow"), it makes the development archives
in a draft marked never to publish. Delete it afterwards. A draft's tag is created only when it is published.

## Branches

- The tag is made on `dev`.
- Once the release is published, `main` moves forward to it, without a merge commit:
  `git push origin amisqueeze-<version>^{commit}:main`. So `main`, the repository's page and its README always
  describe the version that can be downloaded.
- A ruleset of the repository refuses on `main` a force push, a deletion, and a commit whose CI (the `host` and
  `amiga` jobs) has not passed: wait for the tag's CI first. A job renamed in `tests.yml` must be renamed in the
  ruleset too (Settings, Rules).
- A fix to a published version starts from its tag, in a branch of its own. Once released, it is merged into
  `dev` too.

## Aminet

From 1.0:

- the files are renamed without the version (Aminet's rule);
- the readme's `Uploader:` gets an e-mail address;
- the `.lha` to be uploaded is first extracted by LhA in WinUAE, and the scenarios run on what it gives.
