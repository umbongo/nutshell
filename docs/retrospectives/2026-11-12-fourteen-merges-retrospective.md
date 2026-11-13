# Retrospective, 2026-11-12: fourteen merges, a code review worked through, and a build that never started

**Status**: Reviewed (merged)
**Covers**: merges into `main` after the 2026-11-03 marker `94d7ae3` (PR #81) up to
`d966713` (PR #94): #83, #82, #84, #85, #86, #87, #88, #89, #90, #78, #92, #91, #93 and
#94, 2026-11-04 to 2026-11-12 (14 merges, in merge order).
**Method**: full clone on `automaton`, `git log --merges --first-parent 94d7ae3..origin/main`,
`gh pr view` for every PR (dates, commits, size, body, final-head checks), `gh run list`
for every branch (any failed run on any head), the hosted `Native tests` job log of each
`src/` PR for its `Tests Run` line, the version at each merge from `src/ui/resource.h`,
exe commits per branch, the code-scanning alert list, `gh release list`, the remote
branches and tags, `repo-status` step 7 for never-pushed branches, the runs of
`fuzz-weekly.yml` and `release.yml`, every file under `.github/workflows/` and
`.github/scripts/`, the process sections of CLAUDE.md, and each item in the notes
document's Open list checked against the log. `make test` was skipped (docs-only change,
maintainer's instruction); `check-version.sh origin/main` passes. Facts that git does not
show came from the notes of the session that ran PRs #85 to #94 and are marked
*reported*. One Opus session measured and wrote everything; no critique was run here (the
main session sends the process edits in section 5 for one).

## 1. Numbers

| Measure | 2026-11-03 | 2026-11-12 |
|---|---|---|
| Version on `main` | 1.2.49 | 1.2.82 |
| Merges in the period | 10 | 14: 11 `src/`, 2 docs (#82, #84), 1 Dependabot (#78) |
| Native tests, hosted (real libssh2) | 3,314 (#81) | 4,118 (#94 head, 0 failed); 3,736 on #93 |
| Native tests, stub on `automaton` | 3,279 | 4,077 (#94 body, *reported*) |
| `wintest` (Wine) | 45 | 52 (#94 body, *reported*) |
| `window.c` / `chat_listview.c` lines | 5,910 / 4,316 | 6,313 / 4,760 |
| `ai_chat.c` lines | 5,926 | 3,150; the seven `ai_chat*.c` files 7,404 (split in #86) |
| `settings.c` lines | 2,226 | 2,394 |
| Commits per `src/` PR | 1 to 32 | #83: 4, #85: 36, #86: 12, #87: 14, #88: 19, #89: 18, #90: 1, #91: 52, #92: 13, #93: 17, #94: 93 |
| Lines added, largest PR | not measured | #94: +26,550 / -1,004 (code, tests, spec and plan) |
| Version steps / exe commits | 8 / 9 for 7 `src/` merges | 33 / 34 for 11 `src/` merges (#91: 13 steps, #94: 7) |
| Failed check runs on any head | 2 PRs | 0 (one cancelled run on #91) |
| All seven checks green on the final head | 10 of 10 | 14 of 14 |
| Merges with Windows checks listed as not run | not counted | 8 of 11 `src/` PRs (#85-#89, #92, #93, #94 in part) |
| `src/` PRs that recorded their work in the notes | not counted | 3 of 11 (#85, #89, #94; #94 only the Thinking row item) |
| Open CodeQL alerts on `main` | 6 | 0 |
| Last release | v1.1.21 (2026-09-14) | unchanged; `release.yml` not run since |
| Open pull requests | 1 (Dependabot #78) | 0 |
| Exe commits on `main`, all time | 163 | 197 |
| CRLF files in the index | 86 | 94 (the eight new are fuzz seeds) |
| Snapshot branches | 4 | 5 (`v1.2.60` at `0f5bf9a`) |
| `fuzz-weekly.yml` | did not exist | one scheduled run, 2026-11-09, green |

The eleven `src/` merges are #83, #85, #86, #87, #88, #89, #90, #91, #92, #93 and #94.
Hours from opening to merge say little this period: #85-#90 and #92 were opened minutes
before merging, after the work was done on the branch.

## 2. Findings, ranked by the cost of not acting

**F1. A large backlog of Windows checks has never been run, and the notes did not list
it.** Five merges (#85 to #89) ran unattended under the maintainer's instruction of
2026-11-04 to skip checks that needed the maintainer; #92, #93 and #94 also merged with
parts of their Windows lists unrun. Each PR body lists what was not run: web_fetch's
refusals of loopback and metadata addresses, the 15 s SSH setup timeout, About at 125 and
200 %, the data block at a loopback listener, the keyboard-interactive hand tests, the
four-theme gallery after #92 moved Moss & Mist's accent, #93's 13-item checklist, and
`--ui-demo=all` on the four themes for nearly all of them. Some may be covered by the
maintainer's later tests of v1.2.67-v1.2.69 and v1.2.77-v1.2.81 (the Kimi sessions), but
nothing records which. Until this retrospective none of it was in the notes' Open list, so
the next session would have had to read eight PR bodies to find it. These are the
security-facing changes of the period (address filter, sensitive reads, SSH auth), and
`main` has not been released, so the cost so far is risk rather than harm. *Changed*: the
whole list is the first Open item; `feature-workflow` step 8 now says an unrun Windows
list goes into Open in the same PR; `checkpoint` step 2 reads the PR bodies for it.
*Recommended*: a Windows session working through that item before the next release.

**F2. Eleven versions in a row did not start on the maintainer's workstation, and nothing
automated could see it.** Every build from v1.2.49 to v1.2.59 died at launch with no dialog
on Windows 11 24H2 (build 26100.9711): the manifest never asked for comctl32 v6, and that
build's system32 comctl32 5.82 no longer exports `SetWindowSubclass` and its siblings. Wine
exports them in both versions, so `wintest` passed throughout. Six merges (#83, #85-#89)
shipped on top of the fault before #90 fixed it, and the period's new quality checks
(sanitizers, lint, fuzz) run on Linux and cannot see a loader failure. It was found because
the maintainer ran the exe. *Changed*: memory says Wine is not Windows and a build only
counts as started once it has been run there; `v1.2.60` is recorded as a snapshot.
*Recommended, not applied*: (a) a native test that reads `src/ui/nutshell.manifest` and
fails if the Common-Controls 6.0.0.0 dependency goes missing (cheap, catches this exact
regression); (b) a launch smoke check of the committed exe on real Windows (`--ui-demo`
with a timeout, or `-v`) before a release; whether that is a manual step or a Windows
runner is the maintainer's call, given the runner history of 2026-09-15.

**F3. A wait loop ran for seven and a half days.** `while pgrep -f 'make fuzz-long' ...;
do sleep 5; done` matched its own `bash -c` command line, so it never ended; it ran from
2026-11-05 to 2026-11-12 and kept its sub-agent reported as running, and the maintainer
noticed, not the session (*reported*; git cannot show it). The cost was a week of a
stuck agent and whatever it was meant to report. *Changed*: memory names the trap and the
alternatives; `feature-workflow` step 7 records the background-command wait that worked on
every push of #85-#94 and says never a `pgrep -f` loop.

**F4. The record lagged behind the work.** Eight of eleven `src/` PRs added nothing to the
notes; #87's body says its deferred `vcpkg.json` item is in the Open list, and it was not;
the keyboard-interactive Done entry named v1.2.56 and a branch, not the PR or the merged
v1.2.59. The bodies of #88 and #89 still say "Draft on purpose ... do not merge" until the
startup failure is understood, yet both merged at 04:05 and 04:14 on 2026-11-05, and the fix
(#90) merged at 10:45 the same day; whether the pause had been lifted is not recorded
(*unverified*). *Changed*: every Done entry and the follow-ups of #83 to #94 are now in the
notes; the `checkpoint` step 2 sentence above. *Recommended*: correct the two PR bodies or
add a comment saying when the pause was lifted.

**F5. Version churn rose again.** 33 version steps and 34 exe commits for 11 `src/` merges,
against 8 and 9 for 7 last time. Most of it is #91 (13 steps, v1.2.61-v1.2.74) and #94 (7),
where each round of the maintainer's live testing needed a fresh exe. That is the cost of
testing on Windows, which F1 and F2 argue for, so it is not by itself a problem; but a
93-commit, 26,550-line pull request is hard to review whole, and #94 needed three part
reviews, a fold review and a scoped re-check. *Recommended*: split a feature of #94's size
into two pull requests where the spec allows (the tool path and the command list were
separable).

**F6. `run_command` is on by default for two providers nobody has run it against.**
Automatic means On for Anthropic, OpenAI and Moonshot; only Moonshot (Kimi) had a live
round. DeepSeek, Gemini and Custom were deliberately left Off until tested, so the rule
was applied unevenly. *Recommended*: a live round on Anthropic and OpenAI before the next
release, or Automatic means Off for them until one has run. It is in Open.

**F7. Nothing released for eight and a half weeks.** `main` has gone from 1.1.21 to 1.2.82,
with telnet, keyboard-interactive auth, the tool gate and `run_command` since the only
release. `release.yml` gained the `.sha256` step in #87 and has never run with it.
*Recommended*: tag after F1's checks, and treat the first run as the test of the new step.

**F8. The large files keep growing, with one exception.** `window.c` +403 lines,
`chat_listview.c` +444, `settings.c` +168. `ai_chat.c` was split into seven files in #86
with a line-for-line proof of no behaviour change: the first time a split recommended by
these retrospectives happened. `window.c` is the obvious next one.

**F9. What went right.** No check failed on any run of any branch in the period, against
two PRs last time. All six CodeQL alerts closed in #83, the unpushed branch from the last
retrospective's F1 landed the day after it, and Dependabot #78 merged. The native suite
grew by 804 tests; the new fuzzers found and fixed an OSC overflow in their own PR; the
nano cursor fault was found by replaying a recorded session against tmux chunk by chunk.
Five of the seven specs record critique rounds. The rule that Opus review findings reach
the maintainer before any fix held through #94 (*reported*), and when #94's final head
(v1.2.82) differed from the tested v1.2.81, the session said so and offered to hold
(*reported*).

**F10. Small drift.** Memory said the classifier always refuses `gh pr ready`; on
2026-11-12 it accepted it after the maintainer's explicit go-ahead (*reported*). Memory and
the skills spoke of merging from the Windows box only; the session that ran #85-#94 merged
from `automaton` (*reported*). `quality.yml` said six fuzz targets; there are seven. Eight new fuzz
seeds carry CR bytes as test input, so a future renormalise must mark
`tests/fuzz/corpus/` binary first. All fixed or recorded here.

## 3. The last retrospective's recommendations

| # | Recommendation | Status on 2026-11-12 |
|---|---|---|
| 1 | Decide on `fix/sgr-codeql-tidy` | Done: PR #83, v1.2.50 |
| 2 | Investigate the blocking `ssh_channel_write` and the stray tooltip | Not done |
| 3 | Run the telnet and selection cases on Windows; `pwsh` on `automaton` | Not recorded; no `pwsh` |
| 4 | Cut a release | Not done |
| 5 | Update and merge Dependabot #78 | Done: 7122a29 |
| 6 | Schedule the `window.c` split | Not done; `ai_chat.c` split instead (#86) |
| 7 | A "Critique: run / not run" line in every spec | Not adopted as a rule; 5 of 7 specs record critiques |
| 8 | Settle the two review-findings questions | Decided 2026-11-04 |

## 4. Workflow review

- `checks.yml`: accurate. Both required checks ran on every PR; `Native tests` took under
  a minute on #94 against a 10-minute limit.
- `quality.yml` (new, #87): accurate except the header said "the six libFuzzer targets";
  #94 added `runcmd`, making seven. Fixed to name the Makefile's `FUZZ_TARGETS` instead of
  a count. All three jobs finished in about a minute on #94 against 15-minute limits. This
  edit makes the three jobs do their full work on this docs PR, since the workflow file is
  in their scope.
- `fuzz-weekly.yml` (new, #87): accurate; one scheduled run, green.
- `codeql.yml`: accurate, codeql-action 4.39.1 after #78. `src/ui` still never compiled,
  which now includes the comctl32 manifest's consumers and the whole list view.
- `release.yml`: the `.sha256` step is untried (F7).
- `check-version.sh`, `exe-version.sh`: unchanged and consistent.
- CLAUDE.md process sections: consistent with what happened, including the seven checks.
  Nothing in the gate would have caught F2; the proposals in F2 are for the maintainer.

## 5. Changes made

Process-rule edits (sent to a critique by the main session; none was run here):

- `.claude/skills/feature-workflow/SKILL.md` step 7: the background-command wait that
  worked, and never a `pgrep -f` loop (F3).
- `feature-workflow` step 8: Wine is not Windows; a PR merged with its Windows checks unrun
  lists them in its body and puts the same list in the notes' Open list in the same PR (F1).
- `feature-workflow` step 9: merges run from `automaton` or the Windows box; `gh pr ready`
  can be refused in auto mode, and with the maintainer's go-ahead for that PR it is run; if
  the head moved after the maintainer's last test, say what was not seen and offer to hold
  (F10, F9).
- `.claude/skills/checkpoint/SKILL.md` step 2: read each merged PR body's unrun checks,
  follow-ups, deferrals and known limits and carry them into Open (F1, F4).

Record changes:

- `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`: header counts and version;
  Done entries for #82/#84, #83, #85, #86, #87, #88, #89 (corrected), #90, #78, #92, #91,
  #93, #94 and this retrospective; Open gains the Windows-check backlog, `run_command`,
  tool-gate, terminal-escape, Settings and other follow-ups, and `vcpkg.json`; the Thinking
  row item moved up unchanged; removed as done: the unpushed branch, the SGR leak, the six
  CodeQL alerts, Dependabot #78, `fake_dlog`, the scrollback-size setting; annotated: the
  release, the security review (classifier holes closed by #88, web_fetch filter by #85),
  the AI panel's DPI, the harness and CRLF counts.
- `.claude/memory/MEMORY.md`: marker `d966713`; baselines 4,118 / 4,077 / 52; snapshot
  `v1.2.60`; the `pgrep -f` trap; Wine is not Windows; the Windows command-block style; the
  `gh pr ready` fact; exe, CRLF and release facts; open items and line counts.
- `.claude/skills/repo-status/SKILL.md`: known state on 2026-11-12.
- `.github/workflows/quality.yml`: the header comment's target count (section 4).
- `.claude/CHANGELOG.md`: the entry.

## 6. Recommendations, not applied

1. Work through the Windows-check backlog (first Open item) before tagging (F1).
2. A native test that the manifest keeps the Common-Controls 6.0.0.0 dependency (F2a).
3. A launch smoke check of the committed exe on real Windows before a release (F2b).
4. Correct or annotate #88's and #89's "do not merge" bodies (F4).
5. A live `run_command` round on Anthropic and OpenAI, or Automatic means Off for them (F6).
6. Cut a release after 1 (F7).
7. Split features of #94's size where the spec allows (F5); schedule the `window.c` split (F8).
8. Still open from earlier retrospectives: the blocking SSH write, the tab-switch harness
   cases, a local CodeQL CLI run or a Windows CodeQL run for `src/ui`.

## Session cost

Maintainer's report, 2026-11-12: the 5-hour allowance went in about an hour and two resets were used. Measured from the Claude Code transcripts on `automaton` (token counts only; the allowance accounting is not in them, and the weights below are assumptions: input 1.0, cache read 0.1, output 5.0, cache write 1.25, Sonnet 1.0, Opus and Fable 1.67).

| Window (UTC) | Weighted tokens | Main session | Sub-agents |
|---|---|---|---|
| 2026-11-10 23:00 to 11-11 14:00 (`run_command` build) | 209M, 5,510 calls; worst five hours 77M | 5% | 95% |
| 2026-11-12 12:00 to 16:00 | about 9M | about a third | about two thirds |

Causes on the heavy day, in order:

1. Five long-running Opus orchestrator sub-agents, 28% of the window: 659 calls with contexts of 280k to 600k tokens, each re-read on every call. The longest ran 289 minutes.
2. The number of sub-agents: 162 in 15 hours (41 Sonnet implementers, 79 Sonnet reviewers, 9 Opus reviewers), 5,365 calls at an average 158k context.
3. Cache expiry, about 14%: 45 orchestrator calls came back after more than five minutes idle and re-wrote 300k or more of context each time.

No runaway loops and no oversized tool results were found. The `pgrep -f` wait loop (finding 3) used no tokens. 4 and 5 November had similar five-hour peaks, so 11 November was heavy, not unique. On 12 November the main session's share is larger because each call re-reads about 140k of standing context (nine days of summary, instructions and skills); whether Fable counts for more than Opus against the allowance is not known.

Not measured: the meter is account-wide and the maintainer worked on two repositories at once, the second on another client (no transcripts on `automaton`). A reading of 52% of the 5-hour limit against about 5M weighted tokens used on this machine in the same window shows that this machine was a minority of the bar at that moment (reported, ratio rough). The Claude usage page in account settings is the one place that splits it by client.

Recommendations, not applied (they change CLAUDE.md's delegation rule and the skills): start a fresh session per pull request; do not use long-lived Opus orchestrators (orchestrate from the main session in small hand-offs, or use Sonnet); one task review per task and one Opus review per branch, not one per fix round; keep waits on child agents under five minutes or use the one-hour cache; run the heavy repository's session alone when the bar is high.

## Rules added (moved out of the skills)

- Step 2 of `checkpoint` (read merged PR bodies for unrun checks, follow-ups, deferrals, known limits): on 2026-11-12 eight of eleven `src/` PRs had added nothing to the notes document, and one body said its deferral was in Open when it was not.
- `feature-workflow` step 7: a wait that worked on every push of PRs #85-#94 was one background `gh pr checks N --watch --interval 30` command, then the `Tests Run` line, then the PR state, acted on at its completion notification. The `pgrep -f` loop that never ended is finding 3.
- `feature-workflow` step 9: PR #94 was merged after the maintainer's go-ahead in the session; the rule is now that the go-ahead is named (`ready` or `merge` plus the PR number) and renewed when the head moves.
- Earlier history trimmed from the skills: PR #44 shipped without four unstaged files (hence the `??` rule); PR #80 set the brainstorming, plans, subagent-driven route; a plan written against a stale base named a version `main` had already passed (hence no literal version in plans); stacked-PR retargeting was seen on PRs stacked on unmerged branches.
