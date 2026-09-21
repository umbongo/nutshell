---
name: critique
description: Get an adversarial Opus critique of a Fable decision before it is committed — a spec, a design choice, a change to what the merge gate or CI enforces, a process rule, a skill. Use whenever Fable is about to write a decision into the repo. Encodes how to brief the critic so it argues instead of agreeing, and how every finding is answered.
---

# Critique — Opus argues, Fable decides

Last updated: 2026-09-21 (after its own first critique). Changes are
recorded in `.claude/CHANGELOG.md`.

The rule in CLAUDE.md: before a spec or a design decision is committed,
Fable hands it to an Opus agent to critique. This skill is how, because
a critic that is briefed carelessly returns the author's view with a
confident tone and nothing is gained. Only Fable runs it; an orchestrator
that hits a design choice mid-package escalates to Fable rather than
critiquing its own decision.

## When

- A new or changed spec under `docs/superpowers/specs/`.
- A design decision that fixes a module boundary, a data format, a config
  key, a policy default, or anything a later change would have to migrate.
- A change to what the merge gate or CI enforces (`check-version.sh`, a
  required check, a workflow's triggers or steps). Not a pin bump: the
  `steward` skill's Dependabot procedure covers those.
- A change to the process sections of CLAUDE.md, or a new or materially
  changed skill under `.claude/skills/`.

Not for: implementation diffs (Opus reviews those as orchestrator), docs
that record what already happened, or a fix with no decision in it. The
triage rule in CLAUDE.md decides the route; this skill runs on every
"yes" to its first question, whatever the size.

## How to brief the critic

Spawn one agent, `model: opus`, and wait for its report before doing
anything that depends on it. The brief contains, in this order:

1. **The artefact itself**, verbatim or by path: the spec, the diff of the
   decision, the skill text. Never a summary of it.
2. **The constraints it must satisfy**, as facts: point at CLAUDE.md and
   MEMORY.md rather than restating them, and name any constraint that
   lives elsewhere (a gate script, a host limit).
3. **The files it touches or depends on**, by path, so the critic reads
   the real code, not the author's description of it.
4. **Options already considered**, listed without a preference and
   without reasons, so the critic does not spend its findings re-proposing
   them. Which one Fable prefers stays out.
5. **What is being asked**, this list:
   - assumptions the artefact makes that are not stated or not true;
   - cases it does not handle, with a concrete input or sequence for each;
   - contradictions with CLAUDE.md, MEMORY.md, the other skills, or the
     code it touches;
   - a cheaper or simpler way to get the same outcome, if one exists;
   - for a code artefact, what the tests it names would not catch;
   - what would make a careful reviewer reject it as written.
6. **The output format**: findings ranked by cost of ignoring them, each
   with a one-line claim, a concrete failure scenario, and the file or
   line it anchors to; then a separate list headed "not verified" for
   anything the critic could not check. Empty lists are valid answers.

The brief never contains:

- Fable's reasoning or which option Fable prefers. The critic anchors on
  whatever it is told, and an anchored critic confirms.
- A previous critic's findings. A second round goes to a fresh agent with
  the revised artefact only.
- "Please confirm" or "check that this is right". The brief asks for
  what is wrong.

## How Fable answers

For every finding: accept, and change the artefact; or reject, with one
sentence saying why. The dispositions are recorded where the decision
lives: a "Critique" section at the end of a spec; the changelog entry for
a `.claude/` artefact (CLAUDE.md names that file the record of change);
the commit body for anything else. A finding is never dropped silently.
"Not verified" items that matter get verified by Fable or by an Explore
agent before the decision is committed. If the critic returns findings
without a ranking, Fable ranks them from their failure scenarios; there
is no channel to ask the critic again.

If the findings change the decision materially, run one more round with
a fresh critic on the revised artefact. Two rounds is the ceiling. A
decision still moving after two stops there: it stays in the draft pull
request with the open findings listed in the body, for the maintainer to
settle. That applies in a fresh session too, where nobody can be asked.

## What good looks like

A critique that returns one or two findings Fable had not considered and
a short "not verified" list is working. An empty findings list is a valid
result; an empty findings list together with an empty "not verified"
list, on a non-trivial artefact, is the signal to re-read the "never
contains" rules, because the critic was probably handed the conclusion.
First run, 2026-09-21, on this skill and the triage rule: fifteen
findings, thirteen accepted; that is the baseline for whether the
mechanism earns its cost.
