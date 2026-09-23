---
name: repo-status
description: Survey the state of umbongo/nutshell — branches, tags, open pull requests, check results and how each branch relates to main. Use when asked "what are my branches", "what is open", "did CI pass", or before choosing where to start work. Handles the shallow clone and the branch/tag name collisions correctly.
---

# Repo status — branches, PRs and checks

Last updated: 2026-09-21. Changes are recorded in `.claude/CHANGELOG.md`.

## Why the naive commands mislead here

- The session clone is shallow and fetches only `main` and the session
  branch. `git branch -a` shows two branches; the remote has more.
- `v1.0.76` and `v1.1.21` are both a branch and a tag. A plain
  `git fetch origin v1.1.21` resolves to the tag.
- Ahead/behind counts without a merge base are wrong. Unshallow first
  if the question is about divergence.

## Procedure

1. **Remote truth first**:
   `git ls-remote --heads origin` and `git ls-remote --tags origin`.
2. **Fetch what you need by explicit refspec**:
   `git fetch origin refs/heads/<b>:refs/remotes/origin/<b>`.
3. **If divergence matters**: `git fetch --unshallow origin main`
   (about a minute, roughly 610 MiB), then
   `git rev-list --left-right --count origin/main...origin/<b>` and check
   `git merge-base` succeeds.
4. **Open pull requests**: `mcp__github__list_pull_requests` with
   `state: open` and a `fields` subset. For "merged or not" use
   `merged_at`, not `merged` (it reads false for merged PRs when a fields
   subset is requested).
5. **Checks on a PR**: `mcp__github__pull_request_read`,
   `method: get_check_runs`; `method: get` for `mergeable_state`.
6. **Session branch**: `claude/*` branches created by the harness exist
   locally with a remote-tracking ref but are not on the remote until
   pushed. Check `ls-remote` before claiming a branch is published.

## Report shape

A short table: branch, tip subject, relation to `main` (ahead/behind or
"contained"), open PR if any. Then notes: which branches duplicate a tag,
which have an open PR, which are stale. Say explicitly when a number could
not be measured (shallow history) rather than reporting a wrong one.

## Known state on 2026-09-24

Remote branches: `main` (v1.2.5, PR #45), `todo-local-shell-followups`
(PR #42, open draft, superseded by the 2026-09-24 checkpoint), `v1.0.76`
(pre-redesign main, also a tag), `v1.1.21` (contained in main, also a
tag). Merged feature branches are deleted by GitHub on merge. Tags:
`v1.0.76`, `v1.1.21`; no tag has been cut since v1.1.21.
