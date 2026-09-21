# Change log — project memory and skills

Every change to `.claude/memory/` or `.claude/skills/` gets a dated entry
here in the same commit. Newest first. Git history has the diffs; this file
has the why.

## 2026-09-21 — model roles restated by the maintainer

Session: https://claude.ai/code/session_01JJ2vavMqdPNaq745qRsW4w
Model: claude-fable-5-1.

- **CLAUDE.md, "Software Development Rules for Claude".** Rewritten as
  "who does what": Fable is the session the maintainer talks to and owns
  architectural and design decisions; Opus orchestrates each work package
  and peer-reviews Fable's decisions before they are committed; Sonnet and
  Haiku implement under Opus's review; sub-agents are the default so
  Fable's context holds only decisions, reports and learnings. Same
  escalation rule as before; attribution line added.
- **Why.** The maintainer restated the model after a session in which
  Fable did every task directly, including reading the workflows, gate
  scripts and notes into its own context. The old text allowed that; the
  new text does not.

## 2026-09-21 — retrospective cadence: every 10 merges

Session: https://claude.ai/code/session_01JJ2vavMqdPNaq745qRsW4w
Model: claude-fable-5-1. Standing rule from the maintainer.

- **`memory/MEMORY.md`.** New "Retrospective cadence" section: the rule,
  how merges are counted, the last-retrospective marker (`3f0f540`, PR
  #34) and the count since (1). Open-items line refreshed: #33 and #34
  merged, `terminal1` pushed.
- **`skills/checkpoint/SKILL.md`.** "When it runs" section with the
  10-merge trigger and the counting command; a workflows-review step
  (`.github/workflows`, `.github/scripts`, the process sections of
  CLAUDE.md); the marker reset folded into the record-the-change step.
- **CLAUDE.md.** One sentence stating the cadence in "Memory and skills".
- **Outside the repo.** A daily Routine in the maintainer's Claude
  account counts merges since the marker and starts a fresh session with
  the `checkpoint` skill at 10. It is not a repo file; this entry is its
  record.

## 2026-09-21 — first checkpoint from a cloud session

Session: https://claude.ai/code/session_01JJ2vavMqdPNaq745qRsW4w
Model: claude-fable-5-1.

- **Added `memory/MEMORY.md`.** Host limits of a Linux cloud session (no
  cross-compiler, no libssh2, cannot pass the gate for `src/` changes),
  the shallow-clone and branch/tag-collision traps, the merge gate spelled
  out precisely, Dependabot behaviour, GitHub API quirks (`merged` vs
  `merged_at`), and the open items as of today. Imported from CLAUDE.md so
  every session loads it.
- **Added `skills/steward/SKILL.md`.** How to drive a PR here: the
  build-input test that decides whether a version bump and Windows rebuild
  are needed, check names to expect, the Dependabot merge procedure, and
  the commit and PR body conventions.
- **Added `skills/repo-status/SKILL.md`.** How to answer "what are my
  branches" correctly from this clone: `ls-remote` first, fetch by
  explicit refspec, unshallow before ahead/behind, `merged_at` for PR
  state.
- **Added `skills/checkpoint/SKILL.md`.** The checkpoint ritual from the
  notes document, extended with the memory, skill and changelog updates
  and the retrospective format.
- **Why now.** A branch survey and a Dependabot PR review in this session
  hit every one of the traps above; writing them down cost less than
  rediscovering them next time.
