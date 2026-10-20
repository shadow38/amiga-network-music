---
name: review-method
description: The review method of this project (principles, scoring, finding format, compliance verdicts, what not to report, verification pass). Loaded by every release review agent and by the release-review entry point; the single source of the method.
---

# Review method

You review this repository from the outside: you never saw the conversation that wrote the code, and your only job
is to find what is wrong. A review that finds nothing must show, question by question, the evidence that nothing is
wrong.

## Principles (why defects slip through)

- **A1. The writer must not be the only reviewer.** The development session tests the assumptions it made while
  building; its tests confirm its code. Challenge them.
- **A2. A claim without proof is not a fact.** The plan, the CHANGELOG, the user guide and the code comments make
  claims ("tested on X", "done", "handles Y", "measured Z"). Each one needs a proof you can open: file:line, a test
  function, a fake-server scenario, or a dated measurement. A claim that cannot be traced to a proof is a finding.
  Code comments are claims too: a comment that states a size, a unit, a range, an invariant, an order or which
  part of the program does something must match the code.
- **A3. A measurement expires when what it measures changes.** For each limit or measurement recorded in
  `CLAUDE.md` (stack, memory, CPU load, timings), find the commit that recorded it (`git log -S`), then the later
  commits that changed what it depends on: call depth, local variables, newly linked code (`third_party/`
  included), build variants, start-up paths. If anything changed, the measurement is stale until redone.
- **A4. A safety net is only real once you have seen it catch something.** Sanitizers, `make test`, the CI, the
  `tools/check-*.py` scripts: each is assumed to stop a class of errors. Prove it by planting a defect of that
  class in a throwaway worktree and watching the result turn red.
- **A5. A rule in prose decays; a rule in a script does not.** Each rule of `CLAUDE.md` a program could check will
  one day be forgotten. Say which ones are not checked by a script.
- **A6. Everything the server sends is untrusted.** Review the protocol and stream code against everything a
  server may send (other LMS versions, Music Assistant, radio stations' HTTP servers), not only against what the
  test server sent.
- **A7. "Not supported yet" has consequences for users.** A comment saying "not handled", "should not happen" or
  "not supported yet" marks a place where the program goes on in a degraded way: work out what a user would see,
  which servers or streams trigger it, and whether the docs or the plan say so.

## Authority and scope

- The project's `CLAUDE.md` and `CONTRIBUTING.md` take precedence. A finding cites the rule it relies on
  (file:line), or the principle above that applies.
- The release diff (the range you are given) is reviewed line by line. Architecture, security and tests are
  reviewed on the whole repository at the commit under review.
- `third_party/` and generated files (`bench_*.c`) are not reviewed themselves; the paths through them that the
  project's own code reaches are, including what they cost in stack, memory and time on the Amiga.
- Read only the commit under review and the history up to it. Never look at later commits, other branches or tags
  after the one under review, nor at files outside the tree you are given (except this method).

## Scoring (confidence that the problem is real, not how serious it feels)

| Score | Meaning |
|---|---|
| 0 | false positive, does not survive a second look |
| 25 | maybe a problem |
| 50 | real but minor or rare: goes in the "worth knowing" list |
| 75 | very likely real, direct impact, or breaks an explicit rule |
| 100 | certain and frequent |

Only findings at 75 or more go in the main list. A finding that breaks an explicit rule of `CLAUDE.md` is at least
75 by definition, even when it looks minor.

A code comment proven false is 75 when it states a limit or an invariant that the code, or a later change, would
rely on; 50 otherwise. In the user documentation (README, user guide), a statement proven false at the reviewed
commit (a pointer to the wrong document, an option or a message described wrongly) is 75: a reader who follows it
finds nothing.

An inferred finding that the host build can settle (a unit test, a fake-server scenario, a crafted input file) is
settled before it is scored: run it in your worktree, then report it proven or drop it. A finding stays inferred
only when settling it needs the Amiga, an emulator, a server or hardware.

## Finding format (one block per finding)

```
dimension: Architecture | Code | Security | Tests | Documentation | Plan | Compliance
file / line: where it is, in the reviewed commit
score:
title: one line
evidence: the quoted code or text
failure scenario: concrete input or situation -> wrong behaviour
rule: the CLAUDE.md / CONTRIBUTING.md rule (file:line) or principle (A1-A7)
proven or inferred: proven (read in the code, or shown by a planted defect) / inferred (depends on the server,
  the hardware or a measurement not redone: say what would confirm it)
suggestion: what to change, in one or two lines
```

No finding without a failure scenario; if you cannot write one, the score is 50 at most.

## Compliance table

One line per plan step marked done, per release item, and per `CLAUDE.md` rule in your scope:

```
unit | COVERED | proof (file:lines, test name, scenario)
unit | PARTIAL | proof of the part done ; missing: ...
unit | NOT COVERED | -
unit | OUTSIDE REPO | why (real machine, WinUAE listening, server)
```

No proof that can be opened means no COVERED.

## What not to report

Style preferences no rule supports; speculative future problems; anything a compiler warning already catches.
Problems that predate the release are reported, marked "before <version>", when they break a rule or a claim;
otherwise they go in the 50 list.

## Experiments (planted defects, removed guards)

- Only in a throwaway git worktree **next to the repository** (same parent directory, so that the project's build
  scripts find what they need), with a name of your own: `git worktree add ../<repo>-review-<you> <commit>`.
- In it: run `make test` once **as it is** and see it green; then plant the defect with a file edit (not `sed`),
  rerun, and record red or green. A green result after the planted defect means the net or test does not catch it.
- Use a long timeout for `make test` (several minutes). The tests use real time: if a run fails for no planted
  reason, rerun it before concluding anything.
- Afterwards: `git worktree remove --force ../<repo>-review-<you>` then `git worktree prune`. Never commit, never
  push, never edit the main checkout.
- Do not drive emulators, real machines, servers or remote services: what needs them is "OUTSIDE REPO".

## Your report (what an agent returns)

1. The answers to each of your questions, in order, each with its evidence (even when the answer is "nothing
   found": say what you looked at).
2. The compliance table for your scope.
3. The findings at 75 or more, in the format above, most certain first.
4. The 50 list (one or two lines each).
5. What you checked and found solid, with its evidence.
6. Your limits: what you did not build, run or measure.

## Verification pass (done by the entry point, not by the agents)

- **D1.** Re-open every cited file:line in the reviewed commit. Wrong line or wrong quote: correct it or drop the
  finding.
- **D2.** Try to refute each finding: look for a guard elsewhere (callers, initialisation, a later check). If one
  exists, drop the finding or lower its score.
- **D3.** Separate what was proven from what is inferred; mark inferred findings "to confirm" and say what would
  confirm them.
- **D4.** Write the limits of the review in the report: what was not built, not run, or not measured, and on which
  machines.
