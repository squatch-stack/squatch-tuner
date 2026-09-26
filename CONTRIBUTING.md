<!-- SPDX-License-Identifier: MIT -->
# Contributing to squatch-tuner

Thank you for helping. Every change comes in as a pull request and is reviewed before it lands.

## What belongs here

squatch-tuner is a **library**: the Squatch Tuner's core (`include/squatch/tuner`), its tests and
benchmarks, the thin hosts that show it working (the browser demo, the live JACK tuner), and the
published demo in `docs/`. Applications and device firmware do not belong here. If you are unsure
whether something fits, open an issue first.

## A good pull request

- **One change.** A fix, a feature, a refactor or a documentation change, not several.
  Refactors go in their own pull request.
- **Small.** Aim for under 400 changed lines. Over 1000 needs a reason in the description (a
  maintainer then adds the `large-change` label). `docs/`, `results/` and `EXPORTED` are not
  counted.
- **Tested.** A change in what the tuner reads comes with a test or a measured result
  (`make results`) that shows it.
- **Checked locally.** `make lint` and `make test` run what CI runs.
- **Commit subjects** follow [Conventional Commits](https://www.conventionalcommits.org/):
  `type(scope): summary`, at most 72 characters, with type one of `feat fix docs chore ci build
  test refactor perf style revert`. No `fixup!`, `squash!` or `WIP` commits in the final branch.

## What the checks look at

On every pull request (`.github/workflows/quality.yml` and `ci.yml`):

| Check | Limit |
|---|---|
| Cyclomatic complexity per function (`make lint`, lizard) | at most 10 |
| Function length | at most 60 lines |
| Parameters per function | at most 5 |
| Shell scripts (shellcheck) | no findings |
| Commit subjects (`tools/pr-shape`) | the format above |
| Pull request size (`tools/pr-shape`) | notice over 400 lines, fails over 1000 without `large-change` |
| Unit and host tests | pass under ASan + UBSan and TSan |
| The published `docs/tuner.wasm` | reads the test tones exactly |

## Your machine

This repository installs nothing on your machine and changes no configuration: no hooks to
install, no package managers, no downloads at build time. `make lint` needs `lizard`; the rest
needs a C++17 compiler and make.

## How a pull request lands

The tuner is developed inside [squatch-dsp](https://github.com/squatch-stack/squatch-dsp) and
published here. Once a pull request is approved, a maintainer applies it there and it comes back
here in the next publish, credited to you. `EXPORTED` lists every published file; a pull request
may edit them, and CI reports which, but it only enforces `EXPORTED` on `main`.

Contributions are accepted under the MIT licence of this repository.
