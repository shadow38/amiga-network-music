# Contributing

For developers: where to find what, how to start, and how to propose a change. Using the programs is covered in
the [user guide](docs/user-guide.md).

## The documents

| Your question | The page |
|---|---|
| What does the build need, how do I build and package it? Where is what in the repository? | [building.md](docs/development/building.md) |
| Which rules does the code follow? How much stack and memory may it use? | [code-rules.md](docs/development/code-rules.md) |
| Which test covers what? How do I run the tests, on the host and in WinUAE? | [testing.md](docs/development/testing.md), [winuae-scenarios.md](docs/development/winuae-scenarios.md) |
| How does amisqueeze work, and why this way? | [architecture.md](docs/development/architecture.md) |
| What do the servers send, what does amisqueeze answer, what does it trust? What does `STMs` mean? | [protocol.md](docs/development/protocol.md) |
| Something makes no sense: what has already gone wrong? How do I debug? | [pitfalls.md](docs/development/pitfalls.md) |
| How do I write documentation for this project? | [documentation-rules.md](docs/development/documentation-rules.md) |
| How is a version checked and published? | [releasing.md](docs/development/releasing.md) |
| What is planned, and where does each step stand? | [the plan](docs/development/README.md) |
| What changed in each version? | [CHANGELOG.md](CHANGELOG.md) |

## Quick start

With Docker, or the cross-compiler in the `PATH` ([building.md](docs/development/building.md#requirements)), and
a native C compiler with Python 3 and lizard for the tests
([testing.md](docs/development/testing.md#requirements)):

```sh
sh tools/amiga-make.sh            # out/amisqueeze and out/amisqueeze.fpu
make test                         # the host build, its unit tests and its scenarios
sh tools/amiga-make.sh dist DEV=1 # the archives, as a release would have them
```

Then try the Amiga build in WinUAE: [testing.md](docs/development/testing.md#testing-in-winuae).

## Proposing a change

- For anything more than a small fix, **open an issue first**: the work proceeds in planned steps, each with an
  agreed content and success criterion (see the plan), and a change may already be planned, or ruled out.
- **Branches**: `main` is the last published version; the work goes on in `dev`, and pull requests target `dev`
  (see [releasing.md](docs/development/releasing.md)).
- `make test` must pass (GitHub runs it on each pull request); add tests for what you change in the portable core.
  The Amiga build must also be tried in WinUAE ([testing.md](docs/development/testing.md)): the tests run the core
  on the build machine only.
- A change users can see (an option, a message, a behaviour) is documented in the
  [user guide](docs/user-guide.md) and noted under "Unreleased" in `CHANGELOG.md` (see
  [releasing.md](docs/development/releasing.md)).
- Code, comments, documentation and program messages are in English; strings shown on the Amiga stay ASCII.
  Documentation follows [documentation-rules.md](docs/development/documentation-rules.md).
- The licence is MIT: no code copied from GPL projects (squeezelite and LMS are references for the protocol's
  behaviour, never a source of code); third-party code only under an MIT-compatible licence (public domain, MIT,
  BSD, zlib...; not GPL: no libmad), in `third_party/` with its origin and licence (see
  [code-rules.md](docs/development/code-rules.md)).

## Where the rules are

The project's rules are on this page and on the pages that hold rules: [building.md](docs/development/building.md)
(the toolchain, the archives, the safeguard), [code-rules.md](docs/development/code-rules.md),
[testing.md](docs/development/testing.md) (the tests and the checks on the Amiga),
[releasing.md](docs/development/releasing.md) and [documentation-rules.md](docs/development/documentation-rules.md).
[architecture.md](docs/development/architecture.md) explains the reasons behind several of them: AHI's, the two
kinds of position; [protocol.md](docs/development/protocol.md), those of a track's statuses.
