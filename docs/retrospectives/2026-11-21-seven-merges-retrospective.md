# Retrospective, 2026-11-21: seven merges, a fourth Thinking mode, and a build nobody runs

**Status**: DRAFT until its pull request is merged; the merge is the review.
**Covers**: merges into `main` after the 2026-11-12 marker `d966713` (PR #94) up to
`be6599f` (PR #97): #95, #96, #98, #99, #100, #101 and #97, 2026-11-12 to 2026-11-21 (7
merges, in merge order). #97, Dependabot's CodeQL action bump, was merged on the
maintainer's "Merge 97" while this was being written (*reported*; the merge is in git).
**Method**: full clone on `automaton` (not shallow), in a worktree;
`git log --merges --first-parent d966713..origin/main`; `gh pr view` for each PR (dates,
commits, size, body); `gh run list` for each branch (any failed run on any head); the
hosted `Native tests` job log of each code PR for its `Tests Run` line; `APP_VERSION` at
each merge; exe commits on `main`; the open and recent code-scanning alerts and analyses,
and the SARIF of alert #30; `gh release list`; remote branches and tags; `repo-status`
step 7 for never-pushed branches; the `fuzz-weekly.yml` runs; every file under
`.github/workflows/` and `.github/scripts/`; the process sections of CLAUDE.md; each Open
item in the notes checked against the source. `make test` was skipped (docs-only change);
`check-version.sh origin/main` passes. Facts from the main session of PR #101 that git does
not show are marked *reported*. One Opus session measured and wrote this; an Opus critic
reviewed the process edits (`.claude/CHANGELOG.md`, 2026-11-21).

## 1. Numbers

| Measure | 2026-11-12 | 2026-11-21 |
|---|---|---|
| Version on `main` | 1.2.82 | 1.2.86 |
| Merges in the period | 14 | 7: 4 code (#96, #98, #100, #101), 2 docs (#95, #99), 1 Dependabot (#97) |
| Native tests, hosted (real libssh2) | 4,118 (#94) | 4,133 (#101 head, 0 failed); 4,132 on #96, #98, #99 and #100 |
| Native tests, stub on `automaton` | 4,077 | 4,092 (#101 body) |
| `wintest` (Wine) | 52 | 58 (#101 body; 56 after #96) |
| `window.c` / `chat_listview.c` lines | 6,313 / 4,760 | 6,330 / 4,893 |
| `ai_chat.c` / the seven `ai_chat*.c` | 3,150 / 7,404 | 3,134 / 7,388 |
| `settings.c` lines | 2,394 | 2,428 |
| Commits per code PR | 1 to 93 | #96: 7, #98: 1, #100: 3, #101: 3 |
| Lines added, largest PR | #94: +26,550 | #96: +1,356 / -171 (#101: +395 / -92) |
| Version steps / exe commits | 33 / 34 | 4 / 4 |
| Failed check runs on any head | 0 | 0 (one cancelled run on #100's branch) |
| Opened to merged | not comparable | #96 4.3 h, #98 4.1 h, #99 4.1 h, #100 19 min, #101 6 min |
| Code PRs merged with Windows checks unrun | 8 of 11 | 3 of 4 (#96, reported fine on 2026-11-13; #98, failed on Windows that day; #101); #100 ran on Windows on its branch |
| Open CodeQL alerts on `main` | 0 | 1 (#30, opened after the #101 merge) |
| Last release | v1.1.21 (2026-09-14) | unchanged |
| Open pull requests | 0 | 0 (#97 was open from 2026-11-12 until its merge on 2026-11-21) |
| Exe commits on `main`, all time | 197 | 201 |
| CRLF files in the index | 94 | 93 |
| Never-pushed branches on `automaton` | not counted | 0 (only this one) |
| `fuzz-weekly.yml` | 2026-11-09 green | 2026-11-16 green |
| Root filesystem on `automaton` | not measured | 100 % full at session start (*reported*); 79 %, 16 GB free, after the maintainer's cleanup |

## 2. Findings, ranked by the cost of not acting

**F1. The Windows hand-check backlog still grows, and it is what stands between `main`
and a release.** The 2026-11-12 retrospective made it the first Open item. Since then one
part has a run on record: the maintainer ran #96's modes on Windows on 2026-11-13 and
reported them fine (*reported*, not itemised). Nothing from #85 to #94 is recorded as
run, and PR #101 adds its own (streaming in Extended, the cycle, "Always extended"
persisting, the reader's place on completion). `main` is six versions past the last
review of this list and 65 past the last release. *Changed*: #101's item lists the
reader's place and the demo state too. *Recommended*: one Windows session through the
whole first Open item, then a tag.

**F2. A Windows-host `make test` is a third build that nobody runs, and it broke twice.**
`make test` on MSYS2 builds the native test runner, `tests/` included, with `_WIN32`
defined and with gcc 15.2 (*reported*). The hosted `Native tests` and CodeQL never define
`_WIN32`; the cross-compile on `automaton` does, but with gcc 13 and without `tests/`. `tests/test_settings_label_win.c` (PR #93) was in `wintest` but
missing from `WIN_ONLY_TESTS`, so a Windows-host `make test` did not link from PR #93's
merge (2026-11-11) until PR #100 (2026-11-13). PR #98 then made the build `-Werror`, was
merged without a Windows run, and the first one failed on four `-Wformat-truncation`
warnings in a `_WIN32` branch of `ai_tool_web_search.c` that gcc 13 compiles silently.
Both came to light only because #98 asked for that
run. *Changed*: a MEMORY.md fact names the third build and the `WIN_ONLY_TESTS` rule.
*Recommended, not applied*: a native test that fails when a `tests/*_win.c` file is in
`WIN_TEST_SRCS` but not `WIN_ONLY_TESTS`; a hosted Windows job running `make test` would
close the gap, but it would be a new check in the gate, so it is the maintainer's call.

**F3. A CodeQL alert opened on `main` that no pull request check showed.** Alert #30
(`cpp/cleartext-storage-file`, `src/config/loader.c:127`) appeared in the push run on
`986a7b7`, the #101 merge, at 03:25 on 2026-11-21. PR #101's own analysis, with the same
CodeQL 2.27.5, reported none, and main's previous push run (2.27.4, `ffecaac`) none
either. The flagged flow runs from `password_enc_preserved` (line 975, last changed
2026-09-24) to the writer at line 127 (2026-03); PR #101 touched `loader.c`, but not
those lines. With the same bundle on both runs, the PR run's empty result shows that it
reports only alerts on changed lines (the outcome is measured; whether CodeQL's
diff-informed analysis or GitHub's display does the filtering is not); that 2.27.4 did
not flag the flow suggests the bundle change is why it appeared now. The value is the DPAPI blob a config keeps verbatim when it cannot decrypt it, so
it writes ciphertext by design: a likely false positive. Nobody read the main run, so the
memory's "no open CodeQL alert" was out of date within four minutes of the merge.
*Changed*: a `feature-workflow` standing rule reads the alerts created after any merge
to `main`, Dependabot's included, once the push run ends; MEMORY.md records the PR run's
narrower view; the alert is an Open item. *Recommended*:
the maintainer dismisses #30 as a false positive with that reason, or says why not.

**F4. Both Thinking-row PRs relied on the Opus review for the bugs that matter to a
reader.** #96's review found that a click pushed the row being read off screen and that
the wintest only checked heights increase. #101's found that Automatic collapsing a
25-line row above the viewport moved the text by about 24 lines (fixed by holding the
first visible item in `recalc_layout`, `ba6d7b2`) and that the click-cycle wintest could
not fail: its 14-line fixture made the extended and open heights equal (now 40 lines).
The tests were written to the implementation, and the review caught what TDD did not.
#98 and #100 had no Opus review; both were small, and #98's gap was a Windows build, not
code. *Changed*: `feature-workflow` step 4 says a test that tells two states apart needs a
fixture in which they differ. *Recommended*: any layout change that alters a row's height
gets a wintest that the first visible item holds when not pinned to the bottom; the
`recalc_layout` fix makes it one helper.

**F5. A full disk on `automaton` stopped every Bash call.** At the start of the #101
session the 76 GB root had 0 bytes free and each Bash call failed with ENOSPC, because
Claude Code writes command output under `/tmp/claude-1000`; that tree's
`-home-thomas-nutshell` directory alone held 2.1 GB of earlier sessions' scratch and task
output (*reported*). Desktop Commander's `start_process` still ran and was used to
diagnose. The maintainer freed about 16 GB; what they removed is not recorded. Measured
now: `/tmp/claude-1000` is 2.4 MB, `~/.vscode-server` is gone (6.3 GB, *reported*),
`~/.npm` is 7.9 MB (1.5 GB), `/var/log` 701 MB (2.1 GB), `~/.cache` still 2.2 GB. The
scratch tree grows with every session, so it will fill again. *Changed*: MEMORY.md says
to check `df -h /` when every Bash call fails, names the candidates and requires the
maintainer's go-ahead before deleting. *Recommended*: the maintainer decides whether old
`/tmp/claude-1000` session directories may be pruned on a schedule.

**F6. Two small `src/` fixes waiting for "the next code PR" were passed by twice.** The
Search Engine help text that contradicts the tool-call tips (`settings_help.c:378`) and
the dead `src/core/chat_thinking.[ch]` were filed by PR #99 to ride on the next code
change. #100 and #101 were code PRs and took neither. #101 even edited `settings_help.c`.
*Changed*: the Open item says so. *Recommended*: a code PR's brief lists the Open items
marked for the next code PR.

**F7. The model table assumed the main session was Fable.** PR #101's main session ran
`claude-opus-5-5` (*reported*), while `feature-workflow`'s table named Fable for that row.
The `Models:` lines are for quality control, so the table should not steer them.
*Changed*: the row says the session's own model, by its id.

**F8. Dependabot #97 waited nine days for a go-ahead.** It bumps `github/codeql-action`
from 4.39.1 to 4.39.3 (a fix to diff-informed alert filtering, and 4.39.2's bundle
default) and was opened on 2026-11-12, the day of the last retrospective. It was merged
on 2026-11-21 at `be6599f`, after a branch update and seven green checks (*reported*). Nothing broke while it waited, but a pending CodeQL bump is the kind
that changes what the PR runs report (F3). *Recommended*: the merge question for an open
Dependabot PR goes in the same message as the retrospective question, so it is not
left to the next survey.

## 3. What went well

- PR #101 followed `feature-workflow` end to end (*reported*): an Explore survey, a Sonnet
  implementation in a worktree, an Opus review whose findings the maintainer saw before
  any fix, Sonnet fixes, bump and build on `automaton`, all seven checks green on the
  first push, merged on "Merge it". One version step and one exe commit.
- #100 found, fixed and recorded the Windows `-Werror` failure in under a day, and its
  notes entry says what was run where.
- The weekly fuzz run stayed green and no check failed on any head in the period.

## 4. Workflows

Read: `checks.yml`, `codeql.yml`, `quality.yml`, `fuzz-weekly.yml`, `release.yml`,
`check-version.sh`, `exe-version.sh`, and CLAUDE.md's "Branches, pull requests and the
merge gate" and "Software Development Rules". The only workflow change in the period is
#97's pin bump; every action pin on `main` (`be6599f`) is the latest Dependabot proposed,
and no Dependabot PR is open. The gate behaved as documented on every merge: four version
steps, four exe commits, both required checks green. `codeql.yml`'s comment that the
pull-request run reports alerts on the diff is the one line F3 leans on; nothing here
changes what the gate enforces. `release.yml` has still not run since v1.1.21.

## 5. Recommendations, apart from the changes made

1. One Windows session through the first Open item, then tag a release (F1).
2. Dismiss CodeQL #30 as a false positive with its reason, or fix it (F3).
3. A native test that `WIN_ONLY_TESTS` covers every `tests/*_win.c` in `WIN_TEST_SRCS`;
   a hosted Windows `make test` job only if the maintainer wants a new gate (F2).
4. A wintest helper for "the first visible item holds" on any row-height change (F4).
5. Code PR briefs list the Open items marked for the next code PR (F6).
6. A rule for pruning old `/tmp/claude-1000` session directories, if the maintainer wants
   one (F5).
7. Ask about an open Dependabot PR together with the retrospective (F8).
8. CLAUDE.md's retrospective sentence gains the maintainer's rule of 2026-11-21 (the ask
   states the merge count). This PR does not edit CLAUDE.md; the wording is in its body.

## 6. Not measured

- The session cost of this period.
- What the maintainer deleted to free the disk, and the sizes before the cleanup (all
  *reported*).
- Whether #96's Windows run covered every item in its list.
- Why PR #101's CodeQL run did not report #30: the diff-filtering explanation is an
  inference from `codeql.yml`'s comment and the two analyses, not confirmed with GitHub.
