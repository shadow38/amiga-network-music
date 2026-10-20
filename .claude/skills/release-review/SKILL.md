---
name: release-review
description: Review a release before its tag (plan step 3.5): runs the four review agents in parallel, in fresh contexts, on the range since the previous tag, verifies every finding and writes the report. Fixes nothing.
argument-hint: <previous tag> [commit, default HEAD] [worktree path]
---

# Release review

Arguments: `$ARGUMENTS` = `<previous tag> [commit] [root]`. The commit defaults to `HEAD`; the root (the tree under
review) defaults to the repository. Read `.claude/skills/review-method/SKILL.md` first: it is the method you apply in
the verification pass.

## 1. Pin the review

- Resolve the commit to a full SHA (`git rev-parse`), and check the previous tag exists. The release diff is
  `<previous tag>..<SHA>`.
- If the commit is not the checked-out one, or for an acceptance test on a past release, review a throwaway worktree
  checked out at that commit, next to the repository (`git worktree add ../<repo>-review-<tag> <SHA>`), and give
  that path as the root. Nothing later than the commit may be read: no `HEAD`, `main`, `--all` or later tag in the
  agents' git commands.
- Note how `make test` and the Amiga build run on this machine (`CONTRIBUTING.md`, plus any local notes the user
  keeps outside the repository): the agents need it, and it is not in the repository's generic files.

## 2. Run the four agents in parallel

- In **one** message, launch the agents `review-protocol-security`, `review-platform`, `review-tests-docs` and
  `review-plan-compliance` (`.claude/agents/`). Their definitions set `omitClaudeMd`: they read the `CLAUDE.md` of
  the tree under review themselves, so the rules are those of the reviewed commit.
- Give each one only: the root, the full SHA, the range, the path of the method skill, how to run `make test` (and
  the Amiga build) here, and a worktree name of its own for experiments (`../<repo>-review-<agent>`). Nothing from
  the development conversation: no known defects, no hints, no expected results.
- Use this prompt, the same for the four agents but the worktree name; fill the `<…>` and change nothing else
  (`<build>` is the machine's own text, kept outside the repository; `<later tags>` is "any tag later than
  <tag of the commit>", or "any tag" when the commit has none):

  ```
  Release review of this repository.

  - Root (tree under review, checked out, detached HEAD = the commit): <root>
  - Commit under review: <full SHA>
  - Release range: <previous tag>..<full SHA>
  - Method: <path of review-method/SKILL.md> (the tree under review has no .claude/; this file is the only one
    you may read outside the root).

  How to build and test on this machine:
  <build>
  - The root has already been built (`all`, `test`) to check the environment. Do not modify the root.
  - No Amiga or emulator is available to you: what needs one goes in your limits.

  Experiments: your own throwaway worktree only: `git -C <root> worktree add ../<repo>-review-<name> <full SHA>`.
  Remove it when done (`git -C <root> worktree remove --force <path>`, then `git -C <root> worktree prune`).
  Never commit, never push. Three other reviewers run at the same time in their own worktrees (tests use
  ephemeral ports, builds stay in each tree). Write your logs and scratch files in your worktree, nowhere else.

  Read nothing beyond the commit under review:
  - use absolute paths under the root; run git as `git -C <root>` against the full SHA or the range only;
  - never use `HEAD`, `main`, `origin/*`, `--all`, `--branches`, `--tags`, or <later tags> (a tag listing may
    show later ones: ignore them);
  - read nothing else in the parent directory of the root (no other repository or checkout, no local notes),
    except the method file above.

  Time: if you approach 70 minutes, wrap up and return your report, listing what you could not answer as limits.

  Return your report as the method describes.
  ```

  Before the launch, build the root (`all`, `test`) and see it green. Stop rule: 80 minutes per agent.
- Never use a fork, which inherits the conversation. If the `review-*` agent types are not available (they are
  found when Claude Code starts in the repository; a `.claude/agents/` created or edited during a session needs a
  restart), stop and say so, rather than falling back to an agent that gets this session's context. After an
  edit, check that each agent's transcript holds the new wording a minute after the launch.

## 3. Verify (the method's verification pass)

For every finding the agents return:

- re-open the cited file:line at the SHA (`git show <SHA>:<path>`); wrong line or quote: correct it or drop it;
- try to refute it: a guard elsewhere (callers, initialisation, a later check) means drop it or lower the score;
- keep "proven" only for what was read in the code or shown by a planted defect; mark the rest "to confirm" and
  say what would confirm it;
- merge duplicates found by two agents (keep the best evidence);
- an inferred finding the host build could settle (the method's rule) but that the agent left unrun: send it
  back to that agent (SendMessage, nothing else added) or settle it yourself in a throwaway worktree, then score
  it; say which in the report;
- mark each finding "new" or "seen before", against the previous review report in `docs/development/reviews/`
  (and say whether a finding seen before was marked fixed there).

Check that each agent answered every one of its questions; a question left unanswered is a limit of the review.
Check the agents' blindness: scan their transcripts' tool calls for paths outside the root, refs or tags later
than the commit, and web access; write what you find in the report's header.

## 4. Write the report

`docs/development/reviews/<version or tag>.md` (one per release; for an acceptance test, a name that says so), in
English:

1. Header: SHA, range, date, the agents run, how `make test` was run.
2. Synthesis: findings per dimension at 75 or more, one line of assessment each.
3. Findings at 75 or more, ranked by score, in the method's format, each with "proven" or "to confirm".
4. The compliance table (merged from the agents).
5. The 50 list.
6. What was checked and found solid, with evidence.
7. Limits: what was not built, run or measured, and on which machines; questions not answered.
8. Decisions: one line per finding at 75 or more, left empty: "fixed in <commit or plan step>" or "accepted by the
   user: <reason>". The release is tagged only once every line is filled.

Remove the worktrees left behind (`git worktree list`, `git worktree remove --force`, `git worktree prune`).

## 5. Hand over

Fix nothing. Give the user the report's path and a summary in their language; each finding is then fixed through a
normal plan step (agreed, coded, tested, documented) or accepted by the user, with the reason written in the
report's decisions.
