---
name: steward
description: Drive a Nutshell pull request to a mergeable state. Use when watching, fixing or merging a PR in umbongo/nutshell, including Dependabot bumps. Encodes the Version bump gate, what a Linux session can and cannot merge, and the commit and PR body conventions.
---

# Steward — pull requests in umbongo/nutshell

Last updated: 2026-09-21. Changes are recorded in `.claude/CHANGELOG.md`.

## Before touching a PR

1. Read `.claude/memory/MEMORY.md` (loaded via CLAUDE.md) for the gate
   rules and the host limits.
2. Determine what the diff touches. Run
   `git diff --name-only origin/main...HEAD | grep -E '^(src/|Makefile$|nutshell\.rc$)'`.
   - **Empty**: the gate only checks internal consistency. Docs, CI,
     `tests/`, `docs/` and `.claude/` changes can be finished from any host.
   - **Non-empty**: the version must be bumped in `src/ui/resource.h`
     (both macros) and `README.md`, and `build/win/nutshell.exe` must be
     rebuilt with `make clean && make release` **on the Windows host** and
     committed. A Linux session cannot do this; say so and stop at a
     pushed branch with everything else done.
3. Never bump the version for a change that does not touch build inputs;
   the gate does not require it and the exe would then disagree.

## Checking state

- Checks: `mcp__github__pull_request_read` with `get_check_runs`. Expect
  `Version bump`, `analyze`, `CodeQL`. All three must be `success`.
- Mergeability: `pull_request_read` with `get`; `mergeable_state` should be
  `clean`. The ruleset also requires the branch be up to date with `main`.
- Base status: `Version bump` runs on `pull_request` only, so there is no
  base-branch run to compare against; a red gate is always the PR's.

## Dependabot bumps

Weekly, grouped, one open at a time. For an actions-only bump:

1. Confirm the diff is only `.github/workflows/*.yml` pins with updated
   `# vX.Y.Z` comments.
2. Read the upstream changelog for the new version (the PR body quotes it).
3. Confirm all three checks are green on the head commit.
4. Merge with a merge commit. Dependabot deletes its branch. Only the
   CodeQL push run fires afterwards; no release, no version bump.

Semver-major bumps of `actions/checkout` or `upload-artifact` change the
Node runtime the runner needs; check the release notes before merging.

## Merging

Repo convention (CLAUDE.md): `gh pr ready N` then
`gh pr merge N --merge --auto`. Without `gh`, use
`mcp__github__update_pull_request` to leave draft, then
`mcp__github__enable_pr_auto_merge` with merge method `merge`. Never
merge a red check, never push to `main`, never rewrite someone else's
branch.

## Commit and PR body conventions

- Subject and body written as the maintainer would write them; imperative,
  with the version in parentheses when one was bumped, e.g.
  `fix(tabs): Ctrl+W reattaches the surviving tab (v1.1.13)`.
- Every commit ends with a `Models:` line naming each model id and what it
  did, then the `Co-Authored-By` trailer the session provides.
- Every PR body ends with the same `Models:` line and the
  `🤖 Generated with [Claude Code](https://claude.com/claude-code)` footer.
- Open pull requests as drafts.

## Things that are not flakes here

- A `Version bump` failure is deterministic: read its log, it names the
  exact mismatch.
- A CodeQL `analyze` failure in `apt-get` is the hosted image's stale
  third-party repositories; the workflow already strips them by host. If
  it recurs, extend that list, do not ignore the exit code.
