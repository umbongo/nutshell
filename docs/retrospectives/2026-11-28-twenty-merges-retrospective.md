# Retrospective, 2026-11-28: twenty merges, a voice feature nobody has heard, and a cheaper loop

**Status**: DRAFT until its pull request is merged; the merge is the review.
**Covers**: merges into `main` after the 2026-11-21 marker `be6599f` (PR #97) up to
`4897425` (PR #122): #102 to #122, 2026-11-21 to 2026-11-28 (20 merges; there is no #114).
**Method**: full clone on `automaton`, in a worktree;
`git log --merges --first-parent be6599f..origin/main`; `gh pr view` for each pull request
(dates, commits, size, body); the plan ledgers still on disk under
`~/nutshell-wt/*/.superpowers/sdd/`; `.claude/CHANGELOG.md` from 2026-11-21; every file under
`.github/workflows/` and `.github/scripts/`; the process sections of CLAUDE.md, the four
skills and `MEMORY.md`; the open code-scanning alerts; `gh release list`; `repo-status`
step 7; `token-usage.py` over the window since the 2026-11-26 baseline. `make test` was
skipped (docs-only change); `check-version.sh origin/main` passes. The main session
(claude-fable-5-2) wrote this from three sub-agents' reports: a Sonnet agent measured token
use, a Sonnet agent compacted the notes, an Opus agent reviewed the process and the
documents; an Opus critic reviewed the process edits (`.claude/CHANGELOG.md`, 2026-11-28).
The maintainer asked for it at 20 merges, after "No retro yet" at 17.

## 1. Numbers

| Measure | 2026-11-21 | 2026-11-28 |
|---|---|---|
| Version on `main` | 1.2.86 | 1.3.2 |
| Merges in the period | 7 | 20: 11 code (#103, #104, #106 to #110, #112, #117, #120, #122), 8 docs or process (#102, #105, #111, #113, #115, #116, #119, #121), 1 Dependabot (#118) |
| Native tests, hosted (real libssh2) | 4,133 | 4,673 run, 0 failed, 3 skipped (#122 head) |
| Native tests, stub on `automaton` | 4,092 | 4,632 run, 0 failed, 3 skipped |
| `wintest` (Wine) | 58 | 257 run, 0 failed, 6 skipped |
| `window.c` / `chat_listview.c` lines | 6,330 / 4,893 | 6,698 / 4,913 |
| `ai_chat.c` / the `ai_chat*.c` files | 3,134 / 7,388 (seven files) | 3,174 / 8,268 (eight files) |
| `settings.c` lines | 2,428 | 4,164 |
| Commits per code PR | 1 to 7 | 3 to 52 (#117: 52, #120: 37, #112: 31) |
| Lines added, largest PR | +1,356 (#96) | +13,792 (#117); #120 +12,947; #112 +11,697; #108 +8,235 |
| Version steps / exe commits | 4 / 4 | 16 / 23 |
| Opened to merged, code PRs | 6 min to 4.3 h | 18 min (#103) to 2 h 57 min (#120); nine of eleven under 1 h 45 min |
| Code PRs merged with Windows checks unrun | 3 of 4 | 11 of 11 at merge. The maintainer ran v1.2.93 on Windows after #109; #110 is the result. Nothing after #110 is recorded as run |
| Open CodeQL alerts on `main` | 1 | 0 |
| Last release | v1.1.21 (2026-09-14) | unchanged |
| Open pull requests | 0 | 0 at `4897425` (#123 opened while this was written) |
| Exe commits on `main`, all time | 201 | 224 |
| CRLF files in the index | 93 | 93 |
| Never-pushed branches on `automaton` | 0 | 4 live (`fix/terminal-escapes` until #123, `fix/session-io`, `feat/voice-read-aloud`, `docs/voice-realtime-speech-spec`) and 2 merged leftovers |
| Notes document, lines | 698 | 1,208; 65 Open items, 66 after this compaction |
| `fuzz-weekly.yml` | 2026-11-16 green | 2026-11-23 green |
| Root filesystem on `automaton` | 79 % of 76 GB | 25 % of 249 GB |

### Token use, before and after the loop change of 2026-11-26

`docs/token-usage/2026-11-28-first-window-after-loop-change.json` against
`2026-11-26-baseline.json`. The window is 2026-11-26T00:45 to 2026-11-28T07:25 UTC: 2.3
days, 5 merges, 26 agents, 821 requests. The baseline was 5 days and 17 merges.

| Measure | Baseline | This window |
|---|---|---|
| Controller cost per implementer agent | 1.25M (77 implementers) | 3.18M (2 implementers) |
| Controller share of the cost index | 40 % | 34 % |
| Idle cache rewrites | 106 (28.0M tokens, 15 % of the index) | 3 (0.32M, 3 %) |
| Compactions | 4 | 1 |
| Sub-agent start-up, median | 56k | 54k |
| By model | Opus 56 %, Sonnet 38 %, Fable 6 % | Fable 44 %, Opus 32 %, Sonnet 24 %, Haiku 1 % |
| Cost index, total / per merge | 242.5M / 14.3M | 18.6M / 3.7M |

The measurement cannot yet say whether the expected 14 to 17 % saving held. Two
implementer agents are too few for the per-implementer figure to mean anything, and the
per-merge drop is not a loop effect: voice PR 3 was built before the window and merged
inside it, and the other four merges are small. What does point the right way is the idle
rewrites, 106 to 3: the long-lived orchestrators that caused 68 of them are gone. The index
does not weigh Opus against Sonnet or Fable, and the mix moved to Fable because the main
session now does the controller's reading. The next retrospective's window (from
2026-11-28T07:25 UTC) will hold the todo batches and the read-aloud feature, which run
many implementers under the new loop; that is the window to judge by.

## 2. Findings, ranked by the cost of not acting

**F1. Eleven code merges, none run on Windows at merge, and a feature series stacked three
deep on parts nobody had run.** `main` went from v1.2.86 to v1.3.2 and the only release is
still v1.1.21. The one Windows run in the period, the maintainer's test of v1.2.93, produced
PR #110: 21 commits, +4,887 lines, the largest rework of the week. That is the exchange
rate: one Windows session finds about one pull request of faults. After it, #112 (live
dictation), #117 (conversation mode) and #120 (AI speech) were each built on the one before
and merged with "Nothing has run on Windows" in the body. #120 was merged with its Mistral
capture gate waived by the maintainer, which is why three native tests skip. The harness
cases written for the series (`97-voice.ps1`, `settings_help_tool_count`) have never been
parsed, because `automaton` has no `pwsh`. In the notes, "not run" and "hand check" went
from 8 mentions to 29. *Changed*: nothing; the hand checks are all in the Open list, and
the maintainer has the v1.3.1 steps. *Recommended*: R1 and R2.

**F2. Review waves that arrive after the build cost a rebuild each.** #117 committed four
exes in about 70 minutes (v1.2.96 to v1.2.99), one per wave: a Fable final, its re-review,
then two Opus finals that found new defects. #120 built v1.3.0 before its final review, so
its fix wave needed v1.3.1. The maintainer's one-final-review rule (2026-11-26, PR #121)
ended the pile of finals: #120 had one Fable final and a scoped re-review. The order is the
part still open. Batches 1 and 2 of the todo list were reviewed and fixed before their one
build, and each built once. *Recommended*: R3.

**F3. The reviews earn their cost, and keep finding the same four kinds of fault.** In
#120's ledger 8 of 14 per-task Opus reviews found an Important or Critical defect. Across
the period: lifecycle and teardown faults (#117 C1; #112 C1, a recording lost silently); a
Critical in batch 2 (a resize on the alternate screen left a stale scroll region); a
regression the branch itself introduced (#122, `foo__bar__` losing its trailing
underscores); and tests that cannot fail (#108's queue wintest, #110's "passes vacuously
under Wine", #120's M2 and M10), which the 2026-11-21 retrospective had already made a
rule. Batch 3's implementer reported that it did not see its tests fail first. *Changed*:
reviewer briefs for the todo batches now ask, per test file, whether the tests could fail
if the fix were wrong. No rule added.

**F4. Four process rules arrived in two days while two large branches were running, and the
documents did not all follow.** The maintainer's rule of 2026-11-25, that a review is run
by a model other than the one that wrote the code, Opus or Fable, was only in
`feature-workflow`; it had no changelog entry, and CLAUDE.md still said "a review of a diff
is an Opus agent". CLAUDE.md said every branch gets one final whole-branch review while
`feature-workflow` step 5 already exempted a small branch, and #122 was merged under that
exemption (one Opus review, the main session reading the whole diff). The `critique` skill
said `model: opus` though four critiques in the period were Fable at high effort.
*Changed*: CLAUDE.md and `feature-workflow` state the reviewer rule of 2026-11-25;
`critique` names Opus or Fable, never the author's model, with the effort set; the
changelog records the rule. The small-branch case is not in CLAUDE.md, because the
maintainer has not ruled on it: *recommended*, R7.

**F5. The Windows-host `make test` is very likely broken again, the same way as in PR #93.**
`tests/test_ai_netcancel_win.c` (PR #106) is compiled into `wintest` but is missing from
`WIN_ONLY_TESTS` (Makefile, lines 154 to 160; every other `tests/*_win.c` file is listed).
Its body is under `#ifdef _WIN32` and uses `thread_join` from `src/ui` and WinHTTP, so the
native runner built on Windows should fail to link. Not run: only the Windows box can show
it. This is the 2026-11-21 retrospective's recommendation 3, not acted on, with a new
instance five days later. *Changed*: nothing here (a Makefile change needs a build); the fix
is queued on `fix/session-io`. *Recommended*: R4.

**F6. Fixes to a review's findings are sometimes checked only by the main session.** Batch
2's Critical and Important fixes were read by the main session and not by a second
sub-agent. That is what the skill allows for a small branch, and the pull request says so.
It is recorded here because the fix to a Critical is exactly where a second fault hides
(#122's regression came from a fix). No rule proposed; the cost of a scoped re-review is
one short Opus agent, and the main session should ask for it when the fix wave touches
code the review called Critical.

**F7. The token trim does not reach the sessions it was meant for.**
`~/nutshell/.claude/settings.local.json` turns off the synced engineering, design and
cowork plugins and playwright. This session, started in `~/nutshell`, still lists their
skills and tools, a sub-agent started from it reported the same, and the start-up median
moved only from 56k to 54k against the 45.6k a headless start measured on 2026-11-26. Why
is not known: the user-level `~/.claude/settings.json` enables playwright, and the synced
plugins may be keyed differently from `engineering@synced`. *Recommended*: R6.

**F8. Local-only work.** `docs/voice-realtime-speech-spec` holds one commit (`f79e110`,
2026-11-25, +534/-75 to the live-voice spec: the Realtime API alternative, which the
maintainer rejected). It was never pushed and was not in the notes. The worktrees of two
merged branches (`feat/voice-ai-speech`, `fix/core-quick-wins`) were still on disk.
*Changed*: `repo-status` names the local-only spec branch and no longer carries a dated
list of branches in flight; the two leftover worktrees are removed at the end of this
session, their ledgers kept.

## 3. What went well

- One final review. #120: one Fable final, one scoped Opus re-review, two builds. #117
  before the rule: three finals, four builds.
- Findings before fixes. Every ledger shows the stop after a review and the ruling on each
  finding before a fixer was dispatched.
- The ledgers. A compaction in the middle of #120 and of the todo batches lost nothing:
  the run resumed from `progress.md`, and no task was dispatched twice.
- `make selftest-wine`: the off-means-not-loaded promise is tested against the real exe,
  with positive controls, on every code branch since #106.
- Small fixes folded into one pull request (#103), and the todo list worked in batches of
  one pull request each (#122, #123): four to five items per build instead of one.
- Dependabot's #118 merged within ten hours; #97 had waited nine days.
- No failed check on any merged head, and no open CodeQL alert.

## 4. Workflows

Read: `checks.yml`, `quality.yml`, `codeql.yml`, `fuzz-weekly.yml`, `release.yml`,
`dependabot.yml`, `check-version.sh`. They describe what happens. Nothing was changed.

- `github/codeql-action` is at v4.39.4 (#118); 4.39.5 exists and the weekly Dependabot run
  should bring it. Whether `actions/checkout` and `upload-artifact` are at their latest was
  not checked.
- Said in CLAUDE.md and not enforced by CI: `wintest` and the not-loaded-while-off test
  run only on `automaton`, by hand before a push. There is no Wine job.
- `release.yml` has not run since 2026-09-14, so its `.sha256` step is still untried.
- `Native tests` takes about 36 seconds of its ten-minute limit.

## 5. Recommendations, apart from the changes made

- **R1. One Windows session before more voice work, and before a tag.** The v1.3.1 hand
  test (Mistral first) is the cheapest next step in the whole project: it either proves
  three merged pull requests or produces the next one. Then tag a release.
- **R2. In a series, run part N on Windows before part N+1 is stacked on it.** Costs the
  maintainer's time; saves a rework the size of #110. A cheaper partial step: install
  `pwsh` on `automaton` so harness case files can at least be parsed there.
- **R3. Bump and build once, after the final review's fix wave.** The review reads sources,
  and every check but `Version bump` runs without the exe. Cost: the draft's `Version bump`
  stays red until the build. The todo batches already work this way; putting it in
  `feature-workflow` is the maintainer's call.
- **R4. A native test that every `tests/*_win.c` file is in `WIN_ONLY_TESTS`** (carried
  from 2026-11-21, recommendation 3). One small test; it would have caught F5 in PR #106.
- **R5. A hosted Windows `make test` job** remains the maintainer's call (2026-11-21,
  recommendation 1). It would turn F5 from a guess into a red check.
- **R6. Find out why the settings trim is not applied** (`/plugin` in a `~/nutshell`
  session shows what is enabled and from where), then re-measure a sub-agent's start-up.
- **R7. Say in CLAUDE.md what review a branch with no plan gets.** Proposed wording, for
  the maintainer to accept or change: "A branch with no plan (a fix, a batch of small
  fixes) gets one review of its whole diff by a model that did not write it, Opus or
  Fable, and the main session reads that diff in full before the merge." It is what #122
  and #123 did. Today CLAUDE.md speaks only of the model "that did not review its tasks",
  and such a branch has no tasks.

Status of the 2026-11-21 recommendations: 2 (alert #30), 5 (queued fixes, #103), 7
(Dependabot within a day, in effect) and 8 were acted on; 1 (a Windows pass and a tag) and
3 (the `WIN_ONLY_TESTS` test) were not; 4 and 6 were not checked.

## 6. Not measured

- Anything on Windows: no build after v1.2.93 is recorded as run there.
- The Windows-host `make test` (F5) and the harness tiers.
- Failed check runs on unmerged heads of each branch (`gh run list` per branch was not
  repeated this time); no merged head had a red check.
- Whether the four suspected-stale Open items are done: the voice follow-ups M3 and M5
  (commits `60e3f3d`, `f9a44ff`), the "Older review findings" stream leak, the harness
  count ("84 cases" against 78 `Invoke-Case` lines), and the local-shell prompt-detection
  note. They are left in Open.
- Token use for the main session's last minutes: the script counts requests up to
  `--until` and sessions were still running.
