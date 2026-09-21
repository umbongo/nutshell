# Change log — project memory and skills

Every change to `.claude/memory/` or `.claude/skills/` gets a dated entry
here in the same commit. Newest first. Git history has the diffs; this file
has the why.

## 2026-09-21 — one rule replaces the hierarchy: hand off to curate context

Session: https://claude.ai/code/session_01JJ2vavMqdPNaq745qRsW4w
Model: claude-fable-5-1. Instruction from the maintainer, applied
directly: handing this edit off would have curated nothing.

- **CLAUDE.md, "Software Development Rules for Claude".** The
  Fable/Opus/Sonnet/Haiku hierarchy and the triage rule are replaced by
  one rule: hand off to sub-agents wherever possible without letting
  quality suffer, so that the main session's context holds decisions,
  reviewed diffs, reports and learnings rather than raw material. Model
  choice is guidance (Opus judgement, Sonnet code, Haiku mechanical), not
  a chain of command. Direct work is allowed when a handoff would curate
  nothing, stated in one line. Attribution unchanged.
- **`skills/critique/SKILL.md`.** No longer a mandatory step; a judgement
  call for decisions that would be expensive to migrate. "Fable" replaced
  by "the main session" throughout, since the skill is a handoff any main
  session can make.
- **`skills/checkpoint/SKILL.md`.** Step 6 reworded as a sub-agent second
  opinion on process edits rather than a required critique; kept because
  it is the one check on an unattended retrospective rewriting the rules.
- **Why.** The maintainer's stated goal is curating what is ingested into
  the main session's memory. The hierarchy served a different goal
  (division of labour) and was heavier than that needs.

## 2026-09-21 — critique skill and triage rule, after their own critique

Session: https://claude.ai/code/session_01JJ2vavMqdPNaq745qRsW4w
Models: claude-fable-5-1 (draft, dispositions, revision);
claude-opus-5 (critique of the draft, 15 findings).

- **Added `skills/critique/SKILL.md`.** How to brief an Opus critic on a
  Fable decision so it argues instead of agreeing; what to ask; the output
  shape; how every finding is answered; two rounds at most, ending in the
  draft PR if still unsettled.
- **CLAUDE.md.** "Opus peer-reviews" points at the skill; `critique` added
  to the skill list; bullet 1 softened to "delegated cheaply" with Fable
  reading the diffs it reviews; new "Triage before delegating" built on
  two questions (migration-bearing decision? Windows rebuild?) with the
  three routes direct / small code / package derived from them.
- **`skills/checkpoint/SKILL.md`.** New step 6: critique any skill or
  process edit before recording it; unsettled decisions stay in the draft
  PR. Dispositions go in the changelog entry.
- **Critique dispositions** (Opus, first run of the skill on itself):
  1. small-code class unreachable because the version triplet counts as
     files — accepted, triplet and exe excluded. 2. a new spec classed
     *direct* and skipping critique — accepted, specs are question 1.
     3. every `.github` change critique-worthy, contradicting steward's
     Dependabot procedure — accepted, narrowed to what CI enforces. 4. the
     skill untracked and unlisted — accepted, listed in CLAUDE.md (the
     draft commit had already tracked it with its entry). 5. "ask once
     for the ranking" not executable — accepted, Fable ranks. 6. empty
     critique both valid and a failure — accepted, the failure signal is
     now empty findings plus empty "not verified". 7. withholding rejected
     options makes the critic re-propose them — accepted, options listed
     without preference or reasons. 8. retrospective edits skills with no
     critique and no one to escalate to — accepted, checkpoint step 6 and
     the draft-PR terminal state. 9. mechanical multi-file work classed as
     a package — accepted, file count subordinated to "mechanical".
     10. small code from Linux hits the rebuild wall — accepted, question
     2. 11. the ask-list demanded a tests item for non-code and lacked the
     contradictions item — accepted. 12. dispositions split from the
     record of change — accepted for `.claude/` artefacts. 13.
     `run_in_background: false` rejected by the harness — rejected as
     stated (the parameter is accepted) but the line is removed as
     harness-specific; "wait for the report" says what matters.
     14. class names inconsistent and the first-line rule unscoped —
     accepted. 15. bullet 1 absolute versus triage — accepted.
     Cheaper way (two-question triage) — accepted as the frame; the three
     routes are kept because they name who does the work.
- **Not verified, resolved.** The Agent tool does accept
  `run_in_background`, and a sub-agent with full tools can spawn agents;
  the skill now says only Fable runs it. Whether Opus finds what Fable
  missed: first data point is 13 of 15 accepted. No critique had been run
  on the draft before this one; it was the first.

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
