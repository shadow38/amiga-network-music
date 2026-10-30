# Writing documentation

For anyone who writes in this repository: which document answers which question, and how to write it.

Each document has one reader and answers one question. Write for that reader; anything else goes where it
belongs, with a link.

| Document | Reader | Question it answers |
|---|---|---|
| `README.md` | someone discovering the project | What is it, what does it need, how do I start? |
| `docs/user-guide.md` | a user | How do I set it up and use it? Then, for reference: every option, message and limit. |
| `CHANGELOG.md` | a user upgrading | What changed in this version? |
| `CONTRIBUTING.md` | a new contributor | Which page answers my question? How do I start, and propose a change? |
| `docs/development/building.md` | a contributor | What does the build need, and how do I build and package it? |
| `docs/development/code-rules.md` | a contributor changing the code | Which rules does the code follow, within which limits? |
| `docs/development/testing.md` | a contributor testing a change | Which test covers what, and how do I run it, on the host and in WinUAE? |
| `docs/development/winuae-scenarios.md` | the same | What do the automated scenarios in WinUAE check, and how do I run them? |
| `docs/development/architecture.md` | a contributor | How does amisqueeze work, and why this way? |
| `docs/development/pitfalls.md` | a contributor who is stuck | What has already gone wrong, and how do I debug? |
| `docs/development/releasing.md` | the maintainer | How is a version checked and published? |
| `docs/development/documentation-rules.md` | anyone writing documentation | Which document answers which question, and how do I write it? |
| `docs/development/README.md` | the maintainer, a contributor | What is left to do, in which order? Where does each step stand? |
| `docs/development/phase-*.md` | the same | What is each step: its goal, its success criterion, how to check it? |
| `docs/development/reviews/` | the maintainer | What did a review find, and what became of each finding? |
| commit messages | anyone reading the history | What was done, how was it checked, and why? |

**Sentences**

- One idea per sentence, ideally under 25 words. More than two items make a list.
- One level of parentheses at most. No chain of colons and semicolons: split the sentence.
- Start a section with what the reader needs most: what to do, or the conclusion. Details and reasons come after.
- In user documents, name things as the user sees them: a menu, a message, an option, not the code behind them.

**Content**

- The common case first. Exceptions and error cases go to their own place; in the user guide: Messages,
  Troubleshooting or Known limits.
- Each fact in one place, a link elsewhere. The README sums up and links; it does not repeat the user guide.
- What was tried (machines, versions, servers) goes in one table, not into the running text.
- Why something behaves as it does (a protocol detail, a server's quirk) goes in `architecture.md`, unless the
  user needs it to act.
- Before adding a paragraph, ask whether the reader needs it. If not, link to where it is, or leave it out.

**History**

- A document describes the present. The past lives in the CHANGELOG, the review reports and the commits.
- A finished step shrinks in the plan to its status, its date and one line. How it was checked goes in the commit
  message.
- A published version is one row of the roadmap, with a link to the CHANGELOG.
- A decision is written once, briefly, with its date: "(agreed with the user on 2026-10-23)". Not how it was
  reached.

Text that ships in the archives (the README, the user guide) also follows
[Text for the Amiga](code-rules.md#the-rules).
