# Retrospective, 2026-11-03: ten merges, telnet, and a branch nobody pushed

**Status**: Reviewed (merged)
**Covers**: merges into `main` after the 2026-10-19 marker `7960bd2` (PR #71) up to
`94d7ae3` (PR #81): #73, #70, #72, #74, #75, #76, #77, #79, #80 and #81, 2026-10-20 to
2026-11-03 (10 merges). No merges between 2026-10-21 and 2026-11-03.
**Method**: full clone on `automaton`, `git log --merges --first-parent 7960bd2..origin/main`,
`gh pr view` and `gh pr list --state merged` for every PR, the check runs on each PR's
heads (`gh run list --branch`, `commits/<sha>/check-runs`), versions and exe commits per
merge from `git log`, the code-scanning alert list, `gh release list`, `git worktree list`,
every file under `.github/workflows/` and `.github/scripts/`, the process sections of
CLAUDE.md, and each item in the notes document's Open list checked against the log.
`make test` was skipped (docs-only change, maintainer's instruction); the suite count
comes from the hosted `Native tests` log on PR #81's head. Facts that git does not show
came in the session brief and are marked *reported*. One session (Opus) measured and wrote
the edits directly, since the brief already held the context a survey agent would have
needed; one fresh Opus critic reviewed the process edits (section 6).

## 1. Numbers

| Measure | 2026-10-19 | 2026-11-03 |
|---|---|---|
| Version on `main` | 1.2.41 | 1.2.49 |
| Merges in the period | 11 | 10: 7 `src/` (#70, #72, #74, #76, #79, #80, #81), 3 docs (#73, #75, #77), 0 Dependabot (#78 open) |
| Native tests | 3,035, 0 failed (stub, `automaton`) | 3,314, 0 failed (hosted, real libssh2, #81 head); 3,279 with the stub (#81 body, *reported*) |
| `wintest` (Wine) | not measured | 45, 0 failed (#81 body, *reported*) |
| `window.c` / `ai_chat.c` / `chat_listview.c` lines | 5,440 / 5,908 / 4,316 | 5,910 / 5,926 / 4,316 |
| Integration suite | 10 case files | 11 case files; README case tables 56 -> 73 rows, plus 5 telnet cases |
| Commits per `src/` PR | #64: 9 ... #71: 31 | #70: 21, #72: 23, #74: 32, #76: 10, #79: 1, #80: 12, #81: 16 |
| Version steps / exe commits | 14 / 22 for 7 `src/` merges | 8 / 9 for 7 `src/` merges (only 1.2.47 never reached `main`) |
| PRs with a red check on some head before merge | 3 | 2: #72 (`Version bump` x2, after merging `main`), #81 (CodeQL x1, test-only fix) |
| Hours open, `src/` PRs | 3 to 27 | 0.2 (#80) to 44 (#70) |
| Rebuild commits that name their build host | 2 of 22 | 0 of 8 |
| Open CodeQL alerts on `main` | 6 | 6, the same six; all fixed on an unpushed branch (F1) |
| Last release | v1.1.21 (2026-09-14) | unchanged |
| Open pull requests | 2 drafts (#70, #72) | 1: Dependabot #78, green since 2026-10-21 |
| Exe commits on `main`, all time | 104 (memory, stale) | 163 |

All ten merged with all four checks green on the final head.

## 2. Findings, ranked by the cost of not acting

**F1. Finished fixes have sat on an unpushed branch for thirteen days.** Worktree
`~/nutshell-wt/tidy` holds `fix/sgr-codeql-tidy`: two commits from 2026-10-21 (e5a1229,
359dbe0; since rebased into PR #83, 723ee51) on top of #77's merge. They fix the truncated-SGR leak and all six open CodeQL
alerts, including #22, `config_save` creating the config's temp file with a plain
`fopen()` while it carries the encrypted secrets, with tests. They were never pushed,
bumped or built, and they merge cleanly with `main` at 94d7ae3. The commits carry
`Claude-Session: ...01Cczi6ipaKBtKnozR7vkgFM`, the session that wrote the 2026-10-19
retrospective and PR #76, so they are not of unknown origin; why they stopped there is not
recorded. Two Open items looked untouched while their fixes existed. One caveat for
review: on Windows the new `_open(..., _S_IREAD | _S_IWRITE)` sets only the read-only
attribute, so it silences alert #22 without narrowing who can read the file there (the
file's ACL comes from its folder). *Changed*: the checkpoint skill's step 1 and
`repo-status` now list worktrees and branches with no upstream, and memory says where
local-only work lives. *Open question for the maintainer*: keep the branch as the next
`src/` pull request (push, bump, rebuild), or drop it. It was not deleted.

**F2. Review findings were fixed before the maintainer saw them.** On PR #81 the
orchestrator applied the final Opus review's fix wave (e710a16: one Critical, one
Important, seven minors) before the findings reached the maintainer (*reported*). The
maintainer's standing preference that review findings are shown before fixing lives
only in the maintainer's personal memory, which a sub-agent never sees, and nothing in the
repository said it. The fixes were sound (an Opus re-review confirmed them), so the cost
was visibility, not quality. *Changed*: see section 6 for the rule and its critique.

**F3. A blocking SSH write sits on the UI thread.** PR #79 found that
`ssh_channel_write` can block the UI thread for up to its 5 s send timeout per write when
the remote window is full, and left it alone. That is the same symptom class #79 was
opened for: keys stop reaching the terminal. Also unexplained from #79 (*reported*): a
"Mark Issue Ctrl+Shift+M" tooltip drawn over terminal row 2 in the maintainer's
screenshot. *Recommended*: an investigation of its own; both are in Open.

**F4. The manual harness keeps falling behind.** The two tab-switch cases from the last
retrospective's F1 are unchanged. #76's four new gate cases were "still to run on
Windows" in its body and nothing records that they ran. #81's
`tests/integration/cases/85-telnet.ps1` (393 lines, five cases) has never been parsed,
let alone run: `automaton` has no `pwsh`. *Recommended*: run `-Only` on the telnet and
selection cases on the Windows box. Installing `pwsh` on `automaton` would allow a parse
check (`[System.Management.Automation.Language.Parser]::ParseFile`) before push, though
pwsh 7's parser accepts syntax that 5.1 does not, so it catches less than a run.

**F5. Nothing has been released for seven weeks.** `main` has gone from 1.1.21 to 1.2.49
since the only release, now including telnet (a new network surface that sends
passwords in clear by design) and the password eye. *Recommended*: tag after the
maintainer's Windows checks of v1.2.49, or after F1's branch lands.

**F6. The superpowers route has replaced the spec critique.** #80 and #81 went through
brainstorming, a plan and subagent-driven development, and the maintainer tested each exe
on Windows before merge (*reported*). It produced the fewest rebuilds of any cycle. But
the telnet spec, which adds a profile kind and config keys a later change would have to
migrate, has no Critique section, where #74's spec had two rounds. The critique is a
judgement call, not a gate, but a skipped one leaves no trace that tells it from a
deliberate one. *Changed*: `feature-workflow` step 2 names the route. *Recommended, not
applied*: a one-line "Critique: run / not run because ..." in every spec (critique round 1,
finding 10); it adds a requirement to the spec format, so it is the maintainer's call.

**F7. The large files keep growing.** `window.c` grew by 470 lines (telnet's status
lines and prompt wiring went there) while `pw_prompt.c` and `pw_reveal.c` showed the
alternative of a new module. Fourth retrospective to recommend the code-health critique.

**F8. What went right.** Version churn fell from 14 version steps and 22 exe commits to 8
and 9 for the same seven `src/` merges, in a cycle that also followed the last
retrospective's F2 and F5 changes (iterate CodeQL fixes source-only, state the bump
relative to `main` at build time); fewer CodeQL findings helped as much. #81's one
CodeQL failure was fixed in the test, needing no bump. Every merge was green on its final
head.

**F9. The record had drifted again.** Memory said 104 exe commits (163), 5.2 MB (5.5),
85 CRLF files (86), and listed open items that #76 had closed. The notes had no Done entry
for #76, still listed five selection follow-ups #76 shipped, and counted 49 harness cases.
Dependabot #78 has been green and unmerged for thirteen days; with one Dependabot PR at a
time, no other bump can arrive. All the record fixes are made here; #78 is in Open.

## 3. The last retrospective's recommendations

| # | Recommendation | Status on 2026-11-03 |
|---|---|---|
| 1 | Fix or quarantine the two tab-switch cases | Not done |
| 2 | Decide on a local CodeQL CLI run | No decision recorded |
| 3 | Cut a release | Not done |
| 4 | "Exe built on <host>" in rebuild commits | Not adopted (0 of 8) |
| 5 | Schedule the `window.c`/`ai_chat.c` split | Not scheduled; `window.c` +470 |
| 6 | Close the six CodeQL alerts | Fixed on the unpushed branch only (F1) |
| 7-10 | Merge authority on `automaton`, the auto-mode refusal as a CLAUDE.md rule, per-worktree `WINEPREFIX`, splitting `feature-workflow` | No decision recorded |

## 4. Workflow review

- `checks.yml`: accurate. `Native tests` ran the suite on every `src/` PR and skipped on
  the docs PRs (#73, #75, #77), still reporting green. The README rule (350912c) works.
- `codeql.yml`: accurate, pinned to codeql-action 4.38.3; Dependabot #78 raises it to
  4.39.0. `src/ui` is still never compiled, which since #81 includes the telnet socket
  backend `src/ui/telnet_io.c`.
- `release.yml`, `check-version.sh`, `exe-version.sh`: unchanged and consistent with
  what happened; `release.yml` has still never run on a v1.2.x tag (F5).
- CLAUDE.md process sections: consistent with the gate. "Software Development Rules"
  said nothing about when review findings reach the maintainer (F2). No change to what
  the gate enforces.

## 5. Changes made

- `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`: header counts and version;
  Done entries for #73/#75, #76, #79, #80, #81 and this retrospective; Open items added
  (the unpushed branch, the blocking SSH write and the tooltip, telnet follow-ups,
  Dependabot #78), trimmed (selection follow-ups #76 shipped) and annotated (SGR, CodeQL,
  harness, release, CI, case and CRLF counts).
- `.claude/memory/MEMORY.md`: marker `94d7ae3`, count 0; test baseline; no `pwsh` on
  `automaton`; local-only branches; snapshot branch `v1.2.41`; exe, CRLF and
  `delete_branch_on_merge` facts; the `test_fopen_private()` rule for test files; telnet
  backend unscanned; open items and line counts.
- `.claude/skills/feature-workflow/SKILL.md` step 2 and Allocation; `.claude/skills/checkpoint/SKILL.md`
  step 1; `.claude/skills/repo-status/SKILL.md` step 7 and known state.
- `CLAUDE.md`, "Software Development Rules": the maintainer sees every Opus review's
  findings before any is fixed, then the fixes go ahead unless the maintainer says
  otherwise; an orchestrator returns them instead of fixing them itself.
- `feature-workflow`'s Allocation paragraph: how a subagent-driven run does that (the
  top-level orchestrator returns findings and rulings, the main session shows them and
  resumes the same orchestrator for the fix wave).
- `.claude/CHANGELOG.md`: the entry, with the critique dispositions.

## 6. Critique of the process edits

Two fresh Opus critics, one per round, each given only the diff, the files and the
constraints (`critique` skill). Round 1 returned 11 findings and round 2, on the revised
diff, another 11; every disposition is in `.claude/CHANGELOG.md`. The edits changed
materially between rounds:

- The rule moved out of `feature-workflow` step 4 into the skill's "Quality checks"
  paragraph, where CLAUDE.md's matching bullet is restated.
- The orchestrator's brief now overrides `subagent-driven-development`'s stop rules in so
  many words; that run has no stop at its final review, which is the likely cause on #81.
- The fix wave resumes the same orchestrator, or a fresh one starts from the ledger.
- "Finish" is a pushed branch and a draft PR, never `finishing-a-development-branch`'s
  local merge.
- The local-branch commands were wrong (an upstream proves nothing; `%(refname:short)`
  misreads a branch that shares a tag's name) and are fixed and verified.
- The test-count command is now `gh api .../actions/jobs/<id>/logs`, after
  `gh run view --log` printed nothing for two runs.

**Left for the maintainer**, since two rounds is the ceiling:

1. After showing a final review's findings, does the main session wait for your reply
   before the fix wave? Showing and resuming in one turn changes nothing in practice;
   waiting stalls an unattended run. CLAUDE.md's sentence leaves this open on purpose.
2. Your preference as written covers any Opus review. The CLAUDE.md sentence covers the
   final review only, so a per-task Opus reviewer's fix still goes in first. Ratify
   "final review", or widen it.

**Decided by the maintainer on 2026-11-04:** (1) show, then proceed: the main session
shows the findings and the fixes go ahead unless the maintainer says otherwise in reply;
(2) widened to every Opus review. CLAUDE.md and `feature-workflow` now say both.

## 7. Recommendations, not applied

1. Decide on `fix/sgr-codeql-tidy` (F1): next `src/` PR or drop.
2. Investigate the blocking `ssh_channel_write` and the stray tooltip (F3).
3. Run the telnet and #76 selection cases on Windows; consider `pwsh` on `automaton`
   for a parse check (F4).
4. Cut a release (F5).
5. Update and merge Dependabot #78 (F9).
6. Schedule the `window.c` split (F7), and the earlier open questions 7-10 (section 3).
7. A one-line "Critique: run / not run because ..." in every spec (F6).
8. Settle the two questions in section 6.
