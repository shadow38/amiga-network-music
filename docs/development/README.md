# Developers' documentation

For contributors: which page answers which question. How to start and how to propose a change are in
[CONTRIBUTING.md](../../CONTRIBUTING.md); using the programs, in the [user guide](../user-guide.md).

## How amisqueeze works

| Your question | The page |
|---|---|
| How does amisqueeze work, and why this way? | [architecture.md](architecture.md) |
| What do the servers send, what does amisqueeze answer, what does it trust? What does `STMs` mean? | [protocol.md](protocol.md) |
| What costs the CPU, what was done to lower it, how is it measured? | [performance.md](performance.md) |

## Changing it

| Your question | The page |
|---|---|
| What does the build need, how do I build and package it? Where is what in the repository? | [building.md](building.md) |
| Which rules does the code follow? How much stack and memory may it use? | [code-rules.md](code-rules.md) |
| Which test covers what? How do I run the tests, on the host and in WinUAE? | [testing.md](testing.md), [winuae-scenarios.md](winuae-scenarios.md) |
| Something makes no sense: what has already gone wrong? How do I debug? | [pitfalls.md](pitfalls.md) |
| How do I write documentation for this project? | [documentation-rules.md](documentation-rules.md) |

## Versions and plan

| Your question | The page |
|---|---|
| How is a version checked and published? | [releasing.md](releasing.md) |
| What is planned, and where does each step stand? | [the plan](plan.md), with one page per phase |
| What did the reviews before each release find? | [reviews/](reviews/), and the frozen [rules inventory](rules.md) they used |
| What changed in each version? | [CHANGELOG.md](../../CHANGELOG.md) |
