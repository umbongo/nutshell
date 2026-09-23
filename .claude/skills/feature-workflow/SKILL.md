---
name: feature-workflow
description: The end-to-end workflow for a Nutshell feature or fix, how work is allocated to sub-agents, and how a pull request is driven through the Version bump gate to a merge, including Dependabot bumps. Use when starting a feature branch, when asked "what is the process" or "who does what", before the first push of any change, and when watching, fixing or merging a PR in umbongo/nutshell. Restates the rules on main; it decides nothing new.
---

# Feature workflow and bot allocation

Last updated: 2026-09-24. Changes are recorded in `.claude/CHANGELOG.md`.
The rules themselves live in CLAUDE.md ("Software Development Rules",
"Branches, pull requests and the merge gate"); this skill is the walk
through them in order. Where they disagree, CLAUDE.md wins. It absorbed
the former `steward` skill on 2026-09-21; the pull-request steps below
are that skill's content.

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
co-author trailer (CLAUDE.md, "Git commits"). Subject and body are
written as the maintainer would write them: imperative, with the version
in parentheses when one was bumped, e.g.
`fix(tabs): Ctrl+W reattaches the surviving tab (v1.1.13)`. Pull requests
open as drafts and their body ends with the same `Models:` line and the
`🤖 Generated with [Claude Code](https://claude.com/claude-code)` footer.

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
5. **Draft pull request.** The first push opens it. Before touching it,
   classify the diff:

   ```
   git diff --name-only origin/main...HEAD | grep -E '^(src/|Makefile$|nutshell\.rc$)'
   ```

   - **Empty**: the gate only checks internal consistency. Docs, CI,
     `tests/`, `docs/` and `.claude/` changes can be finished from any
     host. Never bump the version for such a change; the gate does not
     require it and the committed exe would then disagree.
   - **Non-empty**: step 6 applies in full.

   The main session reads the diff in full before the maintainer merges;
   that is the one place its context is spent on raw material. Stage new
   files by path and read every `??` line first (CLAUDE.md, "Git commits";
   PR #44 shipped without four files). A pull request stacked on an
   unmerged branch is retargeted to `main` by GitHub when its base is
   deleted on merge (else `gh pr edit N --base main`); it then needs
   `gh api -X PUT repos/umbongo/nutshell/pulls/N/update-branch`, and about
   2.5 minutes for the checks to rerun on the new head.
6. **The gate.** Any change under `src/`, the Makefile or `nutshell.rc`
   needs `APP_VERSION` and `APP_VERSION_BINARY` raised in
   `src/ui/resource.h`, README's `**Version**:` line to match, and a
   rebuilt `build/win/nutshell.exe` committed. The build happens only on
   the maintainer's Windows box (`mingw32-make clean && mingw32-make
   release`). From a Linux session the deliverable is a pushed branch;
   the maintainer builds, commits the exe, and the `Version bump` check
   goes green. Say so and stop at the pushed branch with everything else
   done. Docs, CI, `tests/` and `.claude/` changes need none of this, but
   `Native tests` still has to compile the tree, so a test-only change is
   gated too.
7. **Checking the pull request.** Checks: `gh pr checks N`, or
   `mcp__github__pull_request_read` with `get_check_runs` when `gh` is
   absent. Expect `Version bump`, `Native tests`, `analyze` and `CodeQL`;
   all four must be `success`. The first two are required (`Native tests`
   compiles the committed sources with the real libssh2 and runs the
   suite; `Version bump` compiles nothing); `analyze` and `CodeQL` are
   not, so a red one does not block auto-merge. Mergeability:
   `pull_request_read` with `get`; `mergeable_state` should be `clean`,
   and the ruleset also requires the branch be up to date with `main`.
   The checks run on `pull_request` only, so there is no base-branch run
   to compare against; a red gate is always the PR's.
8. **Integration tier.** Before marking ready, the maintainer runs the
   integration tier the change touches by hand on Windows
   (`tests/integration/Run-Integration.ps1 -Tier gate`); it is a manual
   tool, not a gate.
9. **Merge.** First, where are you: **in a cloud (Linux) session, stop
   here and warn the maintainer** that the pipeline requires a successful
   `make test` and compiled code before it can merge and that this cannot
   be done from a cloud instance (CLAUDE.md, the gate section); do not
   mark ready, do not enable auto-merge. On the Windows box:
   `gh pr ready N`, then `gh pr checks N --watch` until every
   check is green, then `gh pr merge N --merge --auto`, in that order. The
   reason is in CLAUDE.md ("Branches, pull requests and the merge gate"):
   auto-merge merges at once when the PR is mergeable and only `Version
   bump`, which compiles nothing, is required. It prints nothing on
   success; read the PR state afterwards. Without `gh`,
   `mcp__github__update_pull_request` to leave draft, then
   `mcp__github__enable_pr_auto_merge` with merge method `merge`. Never
   merge a red check; never push to `main`; never rewrite someone else's
   branch.
10. **Release**, separately: tag `vX.Y.Z` matching `APP_VERSION` and push
    the tag; the release workflow re-verifies and publishes the committed
    exe. Nothing is built at release time.

## Failures that are not flakes

- A `Version bump` failure is deterministic: read its log, it names the
  exact mismatch.
- A CodeQL `analyze` failure in `apt-get` is the hosted image's stale
  third-party repositories; the workflow already strips them by host. If
  it recurs, extend that list, do not ignore the exit code.

## Standing rules around it

- **Every 10 merges** into `main`, the `checkpoint` skill runs a
  retrospective that refreshes memory, skills and workflows. A daily
  Routine counts merges from the marker in `MEMORY.md` and starts a fresh
  session at ten. It never merges.
- **Record of change.** Every edit to memory or a skill gets a dated
  entry in `.claude/CHANGELOG.md` in the same commit.
- **Dependabot.** Weekly, grouped, one open at a time. For an
  actions-only bump: confirm the diff is only `.github/workflows/*.yml`
  pins with updated `# vX.Y.Z` comments; read the upstream changelog for
  the new version (the PR body quotes it); confirm all three checks are
  green on the head commit; merge with a merge commit. Dependabot deletes
  its branch. Only the CodeQL push run fires afterwards; no release, no
  version bump. Semver-major bumps of `actions/checkout` or
  `upload-artifact` change the Node runtime the runner needs; check the
  release notes before merging.
- **Branch survey.** "What are my branches" goes through `repo-status`;
  the clone is shallow and two names are both a branch and a tag.
