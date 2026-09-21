# Change log — project memory and skills

Every change to `.claude/memory/` or `.claude/skills/` gets a dated entry
here in the same commit. Newest first. Git history has the diffs; this file
has the why.

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
