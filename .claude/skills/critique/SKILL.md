---
name: critique
description: Get an adversarial Opus critique of a Fable decision before it is committed — a spec, a design choice, a change to the merge gate or the process. Use whenever Fable is about to write a decision into the repo. Encodes how to brief the critic so it argues instead of agreeing.
---

# Critique — Opus argues, Fable decides

Last updated: 2026-09-21. Changes are recorded in `.claude/CHANGELOG.md`.

The rule in CLAUDE.md: before a spec or a design decision is committed,
Fable hands it to an Opus agent to critique. This skill is how, because
a critic that is briefed carelessly returns the author's view with a
confident tone and nothing is gained.

## When

- A new or changed spec under `docs/superpowers/specs/`.
- A design decision that fixes a module boundary, a data format, a config
  key, a policy default, or anything a later change would have to migrate.
- Any change to what the merge gate enforces, to a workflow under
  `.github/`, or to the process sections of CLAUDE.md, whatever its size.
- A new or materially changed skill under `.claude/skills/`.

Not for: implementation diffs (Opus reviews those as orchestrator), docs
that record what already happened, or a one-line fix with no decision in
it. The triage rule in CLAUDE.md decides the size class; this skill runs
on every decision in the "package" class and on every gate or process
change in any class.

## How to brief the critic

Spawn one agent, `model: opus`, `run_in_background: false` (the next step
depends on its answer). The brief contains, in this order:

1. **The artefact itself**, verbatim or by path: the spec, the diff of the
   decision, the skill text. Never a summary of it.
2. **The constraints it must satisfy**, as facts: the gate rules, the
   host limits (no Windows build from a Linux session), the design-system
   gates, the repo conventions. Point at CLAUDE.md and MEMORY.md rather
   than restating them.
3. **The files it touches or depends on**, by path, so the critic reads
   the real code, not the author's description of it.
4. **What is being asked**, exactly this list:
   - assumptions the artefact makes that are not stated or not true;
   - cases it does not handle, with a concrete input or sequence for each;
   - a cheaper or simpler way to get the same outcome, if one exists;
   - what the tests it names would not catch;
   - what would make a careful reviewer reject it as written.
5. **The output format**: findings ranked by cost of ignoring them, each
   with a one-line claim, a concrete failure scenario, and the file or
   line it anchors to; then a separate list headed "not verified" for
   anything the critic could not check. Empty lists are valid answers.

The brief never contains:

- Fable's reasoning, the options Fable rejected, or which option Fable
  prefers. The critic anchors on whatever it is told, and an anchored
  critic confirms.
- A previous critic's findings. A second round goes to a fresh agent with
  the revised artefact only.
- "Please confirm" or "check that this is right". The brief asks for
  what is wrong.

## How Fable answers

For every finding: accept, and change the artefact; or reject, with one
sentence saying why, recorded where the decision lives (a "Critique"
section at the end of the spec, or the commit body for a smaller
decision). A finding is never dropped silently. "Not verified" items that
matter get verified by Fable or by a follow-up Explore agent before the
decision is committed.

If the critic's findings change the decision materially, run one more
round with a fresh critic on the revised artefact. Two rounds is the
ceiling; a decision that is still moving after two needs the maintainer,
not a third critic.

## What good looks like

A critique that returns one or two findings Fable had not considered and
a short "not verified" list is working. A critique that returns nothing,
twice in a row, means the brief is leaking Fable's conclusion; re-read
the "never contains" list. A critique that returns ten findings of equal
weight has not ranked them; ask once for the ranking, do not re-brief.
