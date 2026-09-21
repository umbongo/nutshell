---
name: feature-workflow
description: The end-to-end workflow for a Nutshell feature or fix, and how work is allocated to sub-agents. Use when starting a feature branch, when asked "what is the process" or "who does what", or before the first push of any change. Restates the rules on main; it decides nothing new.
---

# Feature workflow and bot allocation

Last updated: 2026-09-21. Changes are recorded in `.claude/CHANGELOG.md`.
The rules themselves live in CLAUDE.md ("Software Development Rules",
"Branches, pull requests and the merge gate"); this skill is the walk
through them in order. Where they disagree, CLAUDE.md wins.

## Allocation

**One rule:** hand off to sub-agents wherever possible, without letting
the quality of the result suffer. The purpose is to curate what enters
the main session's context. A survey returns conclusions; an
implementation returns a diff and a test summary; a review returns ranked
findings. Raw files, transcripts and intermediate detail stay in the
sub-agent. The main session keeps the decisions, the diffs it reviews,
the reports and the key learnings.

**Model choice is guidance, not a chain of command:**

| Job | Model |
|---|---|
| Judgement: reviews, critiques, orchestrating a multi-step package | Opus (`claude-opus-5`) |
| Code and tests | Sonnet (`claude-sonnet-5`) |
| Mechanical, single-pattern work: renames, fixtures, doc tables | Haiku (`claude-haiku-4-5-20251001`) |
| Surveys of the tree that should return conclusions, not files | Explore agent |

A sub-agent may spawn its own.

**Direct work** is allowed only when a handoff would curate nothing: the
main session already holds everything the task needs and a brief would
be longer than the change. Say so in one line.

**Quality checks are sub-agents too.** A second opinion on a decision is
the `critique` skill, used when a wrong decision would be expensive to
migrate. A diff review is an Opus agent given the diff, never a summary
of it. Never poll a running agent; its completion notification is the
signal.

**Attribution:** every commit and pull request carries a `Models:` line
naming each model that touched the change and what it did, then the
co-author trailer (CLAUDE.md, "Git commits").

## The workflow, in order

1. **Branch.** Cut from the latest `main`, push with `-u`. A branch with no
   commits cannot have a pull request; the draft PR opens with the first
   push that carries a commit.
2. **Brief and spec.** The maintainer says what the feature is. The
   survey of the relevant code goes to an Explore agent; the spec is
   written into `docs/superpowers/specs/YYYY-MM-DD-<name>-design.md`
   from its conclusions. The notes document's convention: spec first,
   mockups before choosing a direction for UI work.
3. **Second opinion, if warranted.** For a decision that would be costly
   to migrate, run the `critique` skill: an Opus critic gets the spec and
   the files, never the author's reasoning. Each finding is accepted or
   rejected in writing.
4. **Implementation.** A sub-agent works on the branch: Opus orchestrating
   when the package is multi-step, otherwise Sonnet directly, Haiku for
   the mechanical parts. Tests first (CLAUDE.md, "Test-Driven
   Development"). `make test` before every push. What returns to the main
   session is the diff and the test summary line.
5. **Draft pull request.** The first push opens it. Subscribe and drive
   it with the `steward` skill. The main session reads the diff in full
   before the maintainer merges; that is the one place its context is
   spent on raw material.
6. **The gate.** Any change under `src/`, the Makefile or `nutshell.rc`
   needs `APP_VERSION` and `APP_VERSION_BINARY` raised in
   `src/ui/resource.h`, README's `**Version**:` line to match, and a
   rebuilt `build/win/nutshell.exe` committed. The build happens only on
   the maintainer's Windows box (`mingw32-make clean && mingw32-make
   release`). From a Linux session the deliverable is a pushed branch;
   the maintainer builds, commits the exe, and the `Version bump` check
   goes green. Docs, CI, `tests/` and `.claude/` changes need none of
   this.
7. **Integration tier.** Before marking ready, the maintainer runs the
   integration tier the change touches by hand on Windows
   (`tests/integration/Run-Integration.ps1 -Tier gate`); it is a manual
   tool, not a gate.
8. **Merge.** Mark ready, then enable auto-merge with a merge commit. It
   lands when `Version bump` is green and the branch is up to date with
   `main`. Never merge a red check; never push to `main`.
9. **Release**, separately: tag `vX.Y.Z` matching `APP_VERSION` and push
   the tag; the release workflow re-verifies and publishes the committed
   exe. Nothing is built at release time.

## Standing rules around it

- **Every 10 merges** into `main`, the `checkpoint` skill runs a
  retrospective that refreshes memory, skills and workflows. A daily
  Routine counts merges from the marker in `MEMORY.md` and starts a fresh
  session at ten. It never merges.
- **Record of change.** Every edit to memory or a skill gets a dated
  entry in `.claude/CHANGELOG.md` in the same commit.
- **Dependabot.** A grouped actions bump is merged after confirming the
  diff is pins only, reading the upstream changelog, and seeing the
  checks green (`steward`, "Dependabot bumps").
- **Branch survey.** "What are my branches" goes through `repo-status`;
  the clone is shallow and two names are both a branch and a tag.
