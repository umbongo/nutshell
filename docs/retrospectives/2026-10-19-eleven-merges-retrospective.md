# Retrospective, 2026-10-19: eleven merges, four features, two CodeQL loops

**Status**: DRAFT until the maintainer reviews.
**Covers**: merges into `main` after the 2026-10-13 marker `c593796` (PR #59) up to
`7960bd2` (PR #71): #60-#69 and #71, 2026-10-13 to 2026-10-19 (11 merges).
**Method**: full clone on `automaton`, `git log --merges --first-parent c593796..origin/main`,
`gh pr list --state merged`, per-PR commits and check runs, the code-scanning alert list,
`gh release list`, `make test` on `automaton` (libssh2 stub), every file under
`.github/workflows/` and `.github/scripts/`, the process sections of CLAUDE.md, and each
item in the notes document's Open list checked against the log and the code. Facts that
git does not show came in the session brief and are marked *reported*. Two read-only
survey agents did the measuring. Two fresh Opus critics reviewed the process edits, in two rounds
(15 and 12 findings); every disposition is in `.claude/CHANGELOG.md`.

## 1. Numbers

| Measure | 2026-10-13 | 2026-10-19 |
|---|---|---|
| Version on `main` | 1.2.27 | 1.2.41 |
| Merges in the period | 13 | 11: 7 `src/` (#60, #64, #65, #66, #68, #69, #71), 2 docs/CI (#62, #63), 2 Dependabot (#61, #67) |
| Native tests (Linux `make test`) | 2,485, 0 failed | 3,035, 0 failed (7 compiler warnings in the test build) |
| `window.c` / `ai_chat.c` / `chat_listview.c` lines | 5,124 / 5,455 / n/a | 5,440 / 5,908 / 4,316 |
| Integration suite | 9 case files | 10 case files, 49 case definitions |
| Commits per feature PR | n/a | #64: 9, #68: 20, #69: 24, #71: 31 |
| Exe commits (rebuilds) for 14 version steps | n/a | 22 (#60: 7, #68: 7, #71: 4, four PRs 1 each) |
| PRs with a red check on some head before merge | n/a | 3: #64 (`Version bump` x3), #68 (CodeQL on 7 heads), #71 (CodeQL x3, `Version bump` x1) |
| Open CodeQL alerts on `main` | not measured | 6, none from this cycle's code |
| Last release | v1.1.21 (2026-09-14) | unchanged |
| Open pull requests | 2 | 2: #70 (ConPTY resize anchor, spec and plan), #72 (light-theme ANSI contrast, spec in critique) |

All eleven merged with all four checks green on the final head. #60 was open 451 hours
(the previous retrospective's F1); the feature PRs took 3 to 27 hours. #63's docs-only
skip ran for the first time on #67: `Native tests` reported green in 5 s.

## 2. Findings, ranked by the cost of not acting

**F1. The manual gate has been red for six weeks.** `resize_applies_to_inactive_tab` has
failed on every run since v1.2.13, and `ctrl_w_reaches_shell_and_shift_closes_tab` failed
the v1.2.36 and v1.2.37 runs (*reported*). A tier that is never green teaches its reader to
skim the red, and the maintainer-reported terminal copy and paste breakage (notes, Open)
is the kind of defect a trusted clipboard case would catch. *Recommended*: fix or
quarantine the two tab-switch cases (as known-bug blocks that report XPASS) before
batch C's clipboard cases are written.

**F2. CodeQL findings are the costliest loop.** #68 went red on seven heads and #71 on
three; each `src/` fix is a version bump and a rebuild, so 14 version steps took 22 exe
commits, although CodeQL reads the sources and never the exe. *Changed*:
`feature-workflow` now says to read the SARIF first, iterate source-only fixes on the
draft until `analyze` is clean, and then bump and rebuild once. Memory records the form
`cpp/use-after-free` accepts. *Recommended, not applied*: run the CodeQL CLI on
`automaton` before the first push of a `src/` change. That would change how a change
reaches the gate, so it is the maintainer's decision.

**F3. Nothing has been released for five weeks.** The only release is v1.1.21
(2026-09-14); `main` has moved from 1.1.21 to 1.2.41 since, including the v1.2.14
security fixes. The gate's guarantee (exe built from the committed sources) reaches no
user until a tag is pushed. *Recommended*: tag v1.2.41, or the next version after the
copy-and-paste fix (`feature-workflow` step 10).

**F4. The build host was undocumented.** CLAUDE.md called the Windows box "the current
dev host" while `automaton` has built the gate-passing exes since v1.2.29. Rebuild
commits rarely name the machine: cd8319d and 1d6e37f say "Exe built on automaton";
#68's commits carry only a different timezone offset. *Changed*: CLAUDE.md has a "Building
on Linux (`automaton`)" section (commands, cloud caveat, a guarded `wineserver -k`), and
memory has a section on the two machines, with the `hostname` test that tells
`automaton` from a cloud session. *Recommended*: a one-line "Exe built
on <host>" in every rebuild commit body.

**F5. Version numbers churn.** Seven `src/` merges took fourteen versions; 1.2.28, 1.2.31,
1.2.33, 1.2.35 and 1.2.38-1.2.40 were never the version of a merge into `main`. A plan written against a stale
base hard-coded a version `main` had already passed (*reported*). *Changed*:
`feature-workflow` step 6 says to state the bump as "`main`'s version + 1 patch" at build
time ("one above both `main`'s version and the branch's last build"). The previous
retrospective's rebase-and-bump rule (its F2) was not adopted.

**F6. The large files keep growing.** `window.c` grew by 316 lines and `ai_chat.c` by 453
in six days; `chat_listview.c` is 4,316. This is the third retrospective to recommend the
code-health critique; it is still unscheduled.

**F7. The record had drifted again.** The notes header still said 1,960 tests and
v1.1.23, and its closing paragraph said only `Version bump` is required. Memory said
there is no `.gitattributes` (there is one, with a single binary rule). Alert #27 looked
new, but it is #12 (open since April) renumbered when `markdown.h` moved to `src/core`.
The PowerShell local-shell harness case, the stream-lifetime rework and the duplicated
inactive-tab item were all in the Open list without a note. All fixed here.

## 3. Workflow review

- `checks.yml`: accurate. The `scope` step's skip worked on #67 as designed, and the
  required check still reports.
- `codeql.yml`: accurate. It is pinned to codeql-action 4.38.3, and 4.39.0 was tagged
  2026-10-15. No Dependabot PR is open, so the weekly run should raise it.
- `release.yml`, `check-version.sh`, `exe-version.sh`: unchanged, and no discrepancy
  found. `release.yml` has never run on a v1.2.x tag (F3).
- CLAUDE.md process sections: consistent with the gate. The build section was stale
  (F4) and is fixed. In auto permission mode, `gh pr ready` is refused as a merge without
  review (*reported*); `feature-workflow` step 9 and memory now say so. No change to what
  the gate enforces.

## 4. Changes made

- `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`: header counts, ten Done
  entries (#60-#69, #71), Open items corrected, and two items added (CodeQL alerts,
  release).
- `.claude/memory/MEMORY.md`: marker `7960bd2` and count 0; test baseline; "The
  maintainer's two machines"; four GitHub/CodeQL quirks; `.gitattributes` fact; open items.
- `.claude/skills/feature-workflow/SKILL.md` steps 6 and 9 and its failures list;
  `.claude/skills/checkpoint/SKILL.md` step 1; `.claude/skills/repo-status/SKILL.md`
  (full clone on `automaton`, known state).
- `CLAUDE.md`, Build Commands: the Linux build section, and the stale "current dev host"
  label removed.
- `.claude/CHANGELOG.md`: the entry, with the critique dispositions.

## 5. Recommendations, not applied

1. Fix or quarantine the two failing tab-switch cases (F1).
2. Decide whether to run the CodeQL CLI locally before pushing a `src/` change (F2).
3. Cut a release (F3).
4. Add "Exe built on <host>" to rebuild commit bodies (F4).
5. Schedule the `window.c`/`ai_chat.c` split (F6).
6. Close or dismiss the six CodeQL alerts in the next `src/` PR (notes, Open).

Left unsettled by the critique, for the maintainer:

7. May a session on `automaton` mark ready and merge? CLAUDE.md speaks only of cloud
   sessions; `feature-workflow` step 9 still says "On the Windows box".
8. Should the auto-mode `gh pr ready` refusal become a rule in CLAUDE.md's gate section?
   It is a skill step and a memory fact now.
9. Should each worktree use its own `WINEPREFIX`? `wineserver -k` and the LogPixels value
   act on the shared `~/.wine`.
10. Split `feature-workflow` (184 lines against a guide of about 100), and decide whether
    the model table gets a Fable row and which Haiku id is current.
