# Retrospective, 2026-09-24: eleven merges, three features, one broken main

**Status**: DRAFT until the maintainer reviews.
**Covers**: merges into `main` after the 2026-09-21 retrospective marker `3f0f540`
(PR #34) up to `21e4d5d` (PR #45): 2026-09-21 to 2026-09-23.
**Method**: a read-only measurement pass (Sonnet agent: `git log --merges
--first-parent`, `gh pr view` per merge, the native suite, the harness case count, the
workflows and their run outcomes, every Open item of the notes document checked against
the log); this document written from its report; the process edits it led to critiqued
by an Opus agent per the `critique` skill, which corrected the central finding (section
5) before anything was recorded.

---

## 1. Numbers

| Measure | 2026-09-21 | 2026-09-24 |
|---|---|---|
| Version on `main` | 1.1.23 | 1.2.5 |
| Merges in the period | (first cycle) | 11 (#33, #35-#41, #43-#45) |
| Native tests (`make test`, Windows host) | 1,960 | 2,157 |
| Windows harness (`make wintest`) | 2 icon tests | 2 icon tests + 7 pseudo-console cases |
| Integration cases (`Invoke-Case` + `Invoke-AiCase`) | 30 per the previous retrospective; 26 by this count's method | 36 (PRs #40 and #44 added 10) |
| `src/ui/window.c` lines | 3,386 | 4,224 |
| CodeQL runs on `main`, last 12 | all green | 10 green, 1 red (14dcf6c), 1 cancelled (bc78b5c) |
| `Version bump`, last 15 runs (about 9 pull requests) | green | green |
| Open pull requests | 0 | 1 (#42, folded into this checkpoint and closed) |

Merge latency, created to merged: from 74 seconds (#45) to 46 hours (#38, waiting for
the maintainer); the Dependabot bump #33 waited 4.7 days. Change size: #40 (the local
shell) +4,751 lines, #41 +1,851, #44 +1,379, #45 +1,256, #39 +752.

Process events: three specs, each with one Opus critique round (16, 12 and 13 findings,
all accepted, dispositions in the specs); four Opus diff reviews (12, 8, 8 and 8
findings; all but one applied); one gate-tier harness run by hand (34/34) for the
refactor; one sub-agent lost to an API outage and resumed.

## 2. Findings, ranked by the cost of not acting

**F1. Two pull requests were merged without looking at the checks, and the gate let
them through because it compiles nothing.** `Version bump`, the only required check,
verifies three version strings and the committed exe. `analyze` (CodeQL) is the only
job that runs `make test`; it is not required and finishes about two minutes after
`Version bump`. On PR #44, `analyze` went red at 15:50:07 (four new files had never
been staged, see F2) and the merge was issued at 15:50:25: `gh pr merge --auto` merges
at once when a pull request is already mergeable, and it was. `main` did not build
for 3 minutes 48 seconds until #45 added the files; #45 itself was merged 74 seconds
after it was opened, with its own `analyze` still pending. The session that wrote the
lesson broke it twice within six minutes. *Diagnosis, corrected by the critique*: not a
timing race in auto-merge but a merge issued with a red check on screen; discipline
alone has already failed once. *Changed*: CLAUDE.md now says to run
`gh pr checks N --watch` until every check is green before merging, and why; the
`feature-workflow` skill and MEMORY.md point at it. *Recommended, not applied* (it
changes what the gate enforces): require `analyze` in the ruleset
(`Protect-Main.ps1`), or add a hosted `make test` job to `checks.yml` and require
that; the second also builds `src/core` and `src/term` on Linux for every pull request.
Note that after an `update-branch` the checks rerun on the new head and `Version bump`
is green two minutes before `analyze`, so an auto-merge enabled early always fires on
an untested tree; only a required compiling check closes that gap.

**F2. New files never reached the commit.** The implementer reported "17 files,
2,514 insertions" for #44; `git diff --stat` showed 13 and 1,301, the difference being
four untracked files; the commit was staged with `git add -u`, which skips them. The
Windows build passed locally because the files were on disk. *Changed*: CLAUDE.md's
Git commits section says to stage new files by path and read every `??` line first;
`.gitignore` now names the busybox sidecar copies (anchored to the two paths they
live at) so untracked files in the tree are only ever real omissions. *Recommended*:
implementers return `git status --short` verbatim, not a count.

**F3. The critique and review rounds paid for themselves every time.** The local shell
critique removed an irreversible licence decision (embedding a GPLv2 binary) from a
change that did not need it, and found a reconnect path that would have copied another
profile's password into a local session. The dispatch critique removed a card type
that would have broken the approve buttons' index mapping and an automatic Ctrl+C that
would have killed working commands. The special-keys critique found the design assumed
consuming a key-down stops its character, which Win32 does not do. The diff reviews
found a close path that could hang the UI thread, a lost paste line-end, a PATH
replaced instead of prepended, and Alt tracking that would have swallowed the next
lone Alt tap. This retrospective's own critique corrected F1. None of these were in the
tests the same agents wrote. *No change*: keep both rounds for anything under `src/ui`
and for every process edit.

**F4. Stacked pull requests need one API call nobody had written down.** #40 on #39,
#44 on #43: GitHub retargets the pull request to `main` itself when the base branch is
deleted on merge, but the ruleset's "up to date" condition then blocks it until
`gh api -X PUT repos/umbongo/nutshell/pulls/N/update-branch` pushes a new head, after
which the checks rerun (about 2.5 minutes). *Changed*: recorded in the skill and
MEMORY.md, with the F1 caveat.

**F5. The manual layer is accumulating.** Two checks only the maintainer can run are
open: the dispatch fix's live scenario (a model turn) and the special-keys checklist
(real keystrokes: lone Alt, AltGr, Alt+numpad, Backspace in Edit). The harness cannot
post them; the AI tier costs credits. *Changed*: both are the first Open item in the
notes document. *Recommended*: run them before cutting a tag; there has been no tag
since v1.1.21 and `main` has moved three minor features.

**F6. `src/ui/window.c` grew by 840 lines in three days** and now holds the local
shell start path, the keyboard block and the paste engine beside everything it already
had; `ai_chat.c` is at 4,973. The dispatcher and the key mapping are testable logic
living in the one directory the native suite cannot build. *Recommended*: the Open
list carries "move the dispatcher out of `src/ui`"; the key mapping's Win32 half is
small and could follow.

**F7. `codeql.yml` cancels its own baseline.** Its concurrency group is keyed on
`github.ref` with `cancel-in-progress: true`, so two merges in quick succession cancel
the first `main` push run: `bc78b5c` (the merge of #43) was never analysed on `main`.
Found by the critique, not the workflow review. *Recommended, not applied*: key the
group on `github.sha` for push events, or set `cancel-in-progress` false for `main`.

**F8. Dependabot and the libssh2 gap, unchanged.** No Dependabot PR is open; pins are
current (checkout and upload-artifact 7.0.1, codeql-action 4.38.0). CodeQL still
installs no libssh2, so `src/ui` and the SSH files are not scanned; it stays on the
Open list behind F1, which it partly overlaps.

## 3. What was changed in this checkpoint

- Notes document: Done entries for #35 to #45; the stale "#33 unmerged" line; three new
  Open items at the top (the manual checks; the gate that compiles nothing; the local
  shell follow-ups, absorbing draft PR #42, which is closed); the process paragraph.
- MEMORY.md: marker to `21e4d5d`, count 0; the facts behind F1, F2 and F4 (the rules
  themselves live in CLAUDE.md); the test baseline; the open-items line; a section of
  local-shell and keyboard facts that outlive the session.
- Skills: `feature-workflow` steps 5, 7 and 9; `repo-status` known state; `checkpoint`
  marker rule. The marker rule is a **change**, not a clarification: the marker is now
  the head of `main` when the retrospective is written and the count restarts at 0, so
  the retrospective's own pull request is merge 1 of the next cycle. The daily Routine
  counts from the marker line and needs no change.
- CLAUDE.md: the merge sentence and the code block comment in the gate section; the
  staging rule under Git commits.
- `.gitignore`: `/busybox64.exe` and `/build/win/busybox64.exe`, anchored so a future
  `third_party/busybox/` payload is not ignored.
- Cleanup: the `agent-a0200b588fb9ea20d` worktree (33 MB) and eight merged local
  branches removed; the session scratchpad emptied. The two busybox copies are the
  maintainer's (placed on request for sidecar testing) and stay, ignored.

## 4. Recommendations, for the maintainer

1. Decide F1: require `analyze`, or add and require a `make test` job. Until then the
   rule in CLAUDE.md depends on discipline that has already lapsed twice.
2. Fix F7's concurrency key; it is a two-line workflow change but it alters what runs.
3. Run the two manual checks (F5) before the next tag.
4. Decide on embedding busybox (GPLv2 source with each release) so the single-file
   portable exe can be finished or closed as a sidecar-only feature.
5. Ask implementers for `git status --short`, not a file count (F2).

## 5. Critique of the process edits

One Opus round on the CLAUDE.md, skill, memory, changelog, notes and `.gitignore`
edits and on this document: twelve findings. Dispositions:

1. The F1 causal story did not match the record (`analyze` was already red when the
   merge was issued; no auto-merge event exists). **Accepted**: F1, MEMORY.md, CLAUDE.md
   and the skill rewritten around "watch the checks, then merge".
2. The fix PR #45 broke the new rule minutes later. **Accepted**: in F1.
3. "Enable auto-merge only when all three are green" cannot hold after an
   `update-branch`. **Accepted**: the rule is now `gh pr checks --watch` then merge, and
   F1 says why only a required compiling check closes the gap.
4. CLAUDE.md's code block comment, the notes paragraph and skill step 9 contradicted
   the new sentence. **Accepted**: all three reworded.
5. The `.gitignore` change made the "`git add -A` is unsafe" example false.
   **Accepted**: the rule now rests on `git add -u` skipping new files only.
6. The ignore pattern was unanchored and would hide a future committed payload.
   **Accepted**: anchored to the two paths.
7. Wrong numbers (78 seconds, latency ranges, wintest split, integration counts, the
   run count). **Accepted**: section 1 corrected; the two integration-count methods
   are both shown.
8. `gh pr edit --base` is redundant when GitHub retargets on base deletion.
   **Accepted**: F4 and the skill say so; the command stays as the fallback.
9. Steps skipped: the workflow review missed the concurrency cancellation; section 5
   was pending; the baseline sat under the cloud-session heading; #42 not closed.
   **Accepted**: F7 added; this section written; baseline line moved; #42 closed with
   a comment when this pull request opened.
10. The same rule copied into three files. **Accepted**: rules in CLAUDE.md, facts in
    MEMORY.md, the skill points at CLAUDE.md.
11. The marker rule changed silently. **Accepted**: labelled a change in section 3 and
    in the changelog.
12. "Three new Open items" listed four. **Accepted**: corrected.

Not verified by the critic, and left as stated: that `gh pr merge --auto` prints
nothing on success (observed four times this period); the 2,157 count on Linux; the
finding counts of the earlier critiques and reviews.
