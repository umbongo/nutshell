---
name: checkpoint
description: Run the Nutshell checkpoint or retrospective ritual — compact the todo in the notes document, update project memory and skills, record the changes in the changelog, write a dated retrospective when asked, and leave the tree ready for a new session. Use when asked to "create a checkpoint", "do a retrospective", or "update your memory".
---

# Checkpoint and retrospective

Last updated: 2026-09-24 (marker rule made concrete). Changes are recorded in `.claude/CHANGELOG.md`.

## When it runs

- **Every 10 merges into `main`** (maintainer's standing rule). The
  counter and the last-retrospective marker live in
  `.claude/memory/MEMORY.md` under "Retrospective cadence". Count with
  `git log --merges --first-parent origin/main <marker>..` on an
  unshallowed clone, or `list_pull_requests` filtered on `merged_at`.
- On request: "create a checkpoint", "do a retrospective".
- A daily Routine performs the count and starts a fresh session with this
  skill when the count reaches 10.

The notes document defines a checkpoint as: compact the todo list, compact
memory, delete temp files and worktrees, leave the tree ready for a new
session. This skill is that, plus the record-keeping added on 2026-09-21.

## Files involved

| File | Role |
|---|---|
| `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md` | Running todo and checkpoint log. Authoritative open list. |
| `.claude/memory/MEMORY.md` | Durable facts for future sessions. Imported by CLAUDE.md. |
| `.claude/skills/*/SKILL.md` | Repo procedures. |
| `.claude/CHANGELOG.md` | Record of every change to memory and skills. |
| `docs/retrospectives/YYYY-MM-DD-*.md` | Dated retrospectives, marked DRAFT until the maintainer reviews. |
| `.github/workflows/*.yml`, `.github/scripts/*` | The workflows. Reviewed at every retrospective (see step 5). |

## Procedure

1. **Measure before writing.** Unshallow `main`, run `make test`, list
   PRs with `merged_at`, count what the notes document claims (test
   totals, open items) and check each open item against the log with
   `git log --grep`. Stale items are the most common finding.
2. **Compact the notes document.** Move finished items to "Done" with the
   PR number and commit, keep "Open" in priority order, fix any counts.
   Do not delete history; the done list is the project's memory of why.
3. **Update `MEMORY.md`.** Add only facts that will still be true next
   session (host limits, gate rules, API quirks, ref layout). Remove
   anything the docs now state better. Bump its "Last updated" line.
4. **Update or add skills** when a procedure was learned the hard way.
   One skill per job; keep each under about 100 lines.
5. **Review the workflows.** Read every file under `.github/workflows/`
   and `.github/scripts/`, and the "Branches, pull requests and the merge
   gate" and "Software Development Rules" sections of CLAUDE.md. For each,
   ask: does it still describe what actually happens (compare against the
   last ten merges), are its pins current (Dependabot may have a PR open),
   and did any finding in this retrospective change it? Fix what is
   stale; propose, do not apply, anything that changes what the gate
   enforces.
6. **Second opinion on the process edits.** A skill added or materially
   changed, or an edit to the process sections of CLAUDE.md or to what a
   workflow enforces, gets a sub-agent review (the `critique` skill)
   before it is recorded; it is the one check on an unattended session
   rewriting the rules. A decision the review leaves unsettled stays in
   the draft pull request with the open findings in its body.
7. **Record the change.** Add a dated entry to `.claude/CHANGELOG.md`
   naming each memory, skill or workflow file touched and why, and the
   critique dispositions, in the same commit. Move the "Last retrospective"
   marker in `MEMORY.md` to the head of `origin/main` at the time of
   writing (the last merge the retrospective covers) and reset the count
   to 0; the retrospective's own pull request is then the first merge of
   the next cycle.
8. **Retrospective (when the cadence fires or when asked).** Write it under
   `docs/retrospectives/`, headed DRAFT, with a scope-and-method section,
   measured numbers in a table, findings ranked by cost of inaction, and
   recommendations clearly separated from changes made.
9. **Verify the gate will pass.** These files are not build inputs, so no
   version bump; but `check-version.sh` still runs its consistency check,
   so never edit the version strings in a checkpoint.
10. **Clean up.** Remove scratch files and worktrees, then commit on the
   session branch as `docs: checkpoint YYYY-MM-DD — <one line>` (or
   `docs: retrospective YYYY-MM-DD`), push, open a draft PR.

## What not to do

- Do not change `src/`, the Makefile or `nutshell.rc` in a checkpoint
  commit; that drags in a version bump and a Windows rebuild.
- Do not mark an item done without the commit or PR that closed it.
- Do not put session-specific detail in memory; that belongs in the
  retrospective or the commit message.
