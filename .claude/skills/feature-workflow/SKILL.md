---
name: feature-workflow
description: The end-to-end workflow for a Nutshell feature or fix, how work is allocated to sub-agents, and how a pull request is driven through the Version bump gate to a merge, including Dependabot bumps. Use when starting a feature branch, when asked "what is the process" or "who does what", before the first push of any change, and when watching, fixing or merging a PR in umbongo/nutshell. Restates the rules on main; it decides nothing new.
---

# Feature workflow and bot allocation

Last updated: 2026-11-28 (merge when ready, and the Windows test log; the same day, a count corrected at the retrospective). Changes are recorded in `.claude/CHANGELOG.md`.
The rules live in CLAUDE.md ("Software Development Rules", "Branches, pull
requests and the merge gate"); this skill walks through them in order, and
CLAUDE.md wins where they disagree. History and rationale: the retrospectives.

## Allocation

**One rule:** hand off to sub-agents wherever possible, without letting
the quality of the result suffer. The purpose is to curate what enters
the main session's context. A survey returns conclusions; an
implementation returns its commits, a diff stat and a test summary; a review returns ranked
findings. Raw files, transcripts and intermediate detail stay in the
sub-agent. The main session keeps the decisions, the diffs it reviews,
the reports and the key learnings.

**Model choice is guidance, not a chain of command:**

| Job | Model |
|---|---|
| The main session: the maintainer's conversation, briefs, rulings | The session's own model, named by its id (`claude-fable-5-2` in PR #99, `claude-opus-5-5` in PR #101) |
| Judgement: reviews, critiques, a per-task orchestrator when one is used ("Running a plan") | Opus (`claude-opus-5-5`) |
| Code and tests | Sonnet (`claude-sonnet-5-5`) |
| Mechanical, single-pattern work: renames, fixtures, doc tables | Haiku (`claude-haiku-5-5`) |
| Surveys of the tree that should return conclusions, not files | Explore agent |

A sub-agent may spawn its own.

**Direct work** is allowed only when a handoff would curate nothing: the
main session already holds everything the task needs and a brief would
be longer than the change. Say so in one line.

**Quality checks are sub-agents too.** A second opinion on a decision is
the `critique` skill. A diff review is an Opus or Fable agent, never the
model that wrote the code, given the diff, never a summary. Never poll a running agent; its completion notification is the
signal. **Every Opus or Fable review's findings reach the maintainer
before any fix** (CLAUDE.md). `subagent-driven-development` does not stop
there, so the main session, as its controller, shows the findings and its
rulings before it dispatches a fixer; unless told otherwise, the fixes then
go ahead ("Running a plan" covers a per-task orchestrator). Every review is
run by a model other than the one that wrote the code, Opus or Fable
(maintainer, 2026-11-25). **A branch gets one final whole-branch review,
by whichever of the two did not review its tasks** (maintainer,
2026-11-26): Opus per task means a Fable final, Fable per task an Opus
final. Never a second final review when every task was reviewed. A
re-review of a fix wave's diff is scoped to that diff and is not a second
final. Finish means the pull request merged (step 9), once it is ready.

## Running a plan

**The main session runs the plan itself** (maintainer, 2026-11-26). It is
the controller for `superpowers:subagent-driven-development`: it dispatches
every agent the plan needs as a background agent and takes their
completion notifications. That is each task's implementer, reviewer and
fixer, and the work after the last task: the version bump, build and
verification (a Sonnet agent), the final review, the fix wave and its
re-review. No sub-agent spans two of these jobs.

- **Why** (measured over 2026-11-21 to 26, `docs/token-usage/`): the
  long-lived Opus orchestrators were about 30% of the cost index. Their
  contexts grew to 300k-600k and every request re-read them; and a
  sub-agent's prompt cache lives 5 minutes, so it expired while they
  waited on an implementer and the next request rewrote it in full (68
  of the 106 idle rewrites). The main session has the 1-hour cache on the subscription. It
  takes on part of the controller's reading, so the expected saving is
  smaller: the critique estimated 14-17% of the index.
- **The ledger** is the plan-execution skill's own,
  `.superpowers/sdd/<plan>/progress.md` in the worktree, not the plan's
  checkboxes. Write a line when an agent is dispatched (task, role, model,
  worktree) and when it returns (commits, test line, verdict), before
  anything else, so a compaction or a new session resumes from the file
  and never re-dispatches a task that is running or done. The maintainer's
  instructions given mid-run go in it too; a compaction can lose them.
- **Keep the main session lean.** It does not run `make`, `git diff` or
  broad Reads itself: the implementer runs the tests, the reviewer reads
  the diff, and a question about the code goes to a short sub-agent. To
  rule on a finding it may read the few lines the finding cites. Per task
  it receives the report (commits, a diff stat, the test summary line,
  open questions) and the reviewer's ranked findings.
- **The check before the merge** (step 5) comes last, after the fix wave,
  when nothing else is pending. On a branch that had its final review
  it is the diff stat and the cited hunks, not the whole diff
  (PR 3's is about 15,000 lines, most of a compaction).
- **Briefs carry the task, not the documents:** the task's section of the
  plan and the rulings it cites, pasted or by path and line range, never
  "read the spec and the plan". A spec's review notes and revision history
  go in a file implementers are not pointed at.
- **Fix rounds** follow the plan-execution skill (resume the implementer
  with SendMessage for rounds 1 to 3) only when the implementer finished a
  few minutes ago. Otherwise, and overriding that skill, a fresh fixer gets
  the findings and the files: a cold resume rewrites the implementer's
  whole context.
- **A per-task orchestrator** (Opus) only for a task whose own loop the
  main session should not hold. It is spawned for that task and ends with
  it. After a review it returns the findings and its rulings and stops; the
  main session shows them, then resumes it for the fixes (a fresh one gets
  the ledger path and the findings).
- **Not used:** `subagentPromptCacheTtl: "1h"` (a settings key in Claude
  Code 2.1.332). It makes every sub-agent cache write dearer to spare the
  waits of a few; revisit only with a retrospective's measurement.

**Attribution:** every commit and pull request carries a `Models:` line
naming each model and what it did, then the co-author trailer (CLAUDE.md,
"Git commits"). Subject and body are written as the maintainer would:
imperative, version in parentheses when bumped, e.g.
`fix(tabs): Ctrl+W reattaches the surviving tab (v1.1.13)`. Pull requests
open as drafts, ending with the `Models:` line and the Claude Code footer.

## The workflow, in order

1. **Branch.** Cut from the latest `main`, push with `-u`. A branch with no
   commits cannot have a pull request; the draft PR opens with the first
   push that carries a commit.
2. **Brief and spec.** The maintainer says what the feature is; an Explore
   agent surveys the code; the spec goes in
   `docs/superpowers/specs/YYYY-MM-DD-<name>-design.md` (spec first,
   mockups before choosing a UI direction). Usual route:
   `superpowers:brainstorming`, `superpowers:writing-plans` (plans in
   `docs/superpowers/plans/`), `superpowers:subagent-driven-development`.
3. **Second opinion, if warranted.** For a decision that would be costly
   to migrate, run the `critique` skill: an Opus critic gets the spec and
   the files, never the author's reasoning. Each finding is accepted or
   rejected in writing.
4. **Implementation.** Sub-agents work on the branch, dispatched task by
   task by the main session ("Running a plan"): Sonnet for code, Haiku for
   the mechanical parts. Tests first (CLAUDE.md, "Test-Driven
   Development"). A test that tells two states apart needs a fixture in
   which they differ (PR #101's click-cycle wintest could not fail: at 14
   lines the extended and open heights were equal). `make test` before
   every push. What returns to the main session is each task's report
   (commits, a diff stat, the test summary line) and the reviews' findings
   (Allocation, "Quality checks").
5. **Draft pull request.** The first push opens it. Before touching it,
   classify the diff:

   ```
   git diff --name-only origin/main...HEAD | grep -E '^(src/|Makefile$|nutshell\.rc$)'
   ```

   - **Empty**: the gate only checks internal consistency. Docs, CI,
     `tests/`, `docs/` and `.claude/` changes can be finished from any
     host. Never bump the version for such a change; the gate does not
     require it and the committed exe would then disagree.
   - **Non-empty**: step 6 applies in full.

   Before the merge, the main session checks the diff
   (maintainer, 2026-11-26). A branch that had its final review
   has been read in full by it, so the main session reads
   `git diff --stat origin/main...HEAD`, compares it with the files the
   plan's tasks named, reads in full any file no task named, and reads the
   hunks the review's findings cite and the fixes made to them. A branch
   with no final review (a small fix, docs, a config change) is still read
   in full. Stage new files by path and read every `??` line first (CLAUDE.md, "Git
   commits"). A PR stacked on an unmerged branch is retargeted to `main`
   when its base is deleted (else `gh api -X PATCH
   repos/umbongo/nutshell/pulls/N -f base=main`; `gh pr edit` fails here);
   it then needs `gh api -X PUT repos/umbongo/nutshell/pulls/N/update-branch`
   and about 2.5 minutes for the checks to rerun.
6. **The gate.** Any change under `src/`, the Makefile or `nutshell.rc`
   needs `APP_VERSION` and `APP_VERSION_BINARY` raised in
   `src/ui/resource.h`, README's `**Version**:` line to match, and a
   rebuilt `build/win/nutshell.exe` committed, built on the Windows box
   (`mingw32-make clean && mingw32-make release`) or on `automaton`
   (CLAUDE.md, "Building on Linux"). A spec or plan names the new version
   as "one above both `main`'s and the branch's last build, at build time"
   (patch +1; after X.Y.99 comes X.(Y+1).0), never a literal number. From
   a cloud session, which has neither toolchain, stop at the pushed branch
   and say so. Docs, CI, `tests/` and `.claude/` changes need none of this,
   but `Native tests` still compiles the tree, so a test-only change is
   gated too.
7. **Checking the pull request.** `gh pr checks N`, or
   `mcp__github__pull_request_read` (`get_check_runs`) without `gh`. Expect
   `Version bump`, `Native tests`, `analyze`, `CodeQL`, `Sanitizers`, `Lint`
   and `Fuzz`; all seven must be `success`, even on a docs-only PR (the
   quality jobs skip their work but report green). Only the first two are
   required, but none is merged red. `mergeable_state` should be `clean`
   and the branch up to date with `main`. Checks run on `pull_request`
   only, so a red gate is always the PR's. **The only safe wait is a
   background command and its completion notification (or Monitor)**:
   `gh pr checks N --watch --interval 30; id=$(gh api repos/umbongo/nutshell/commits/<sha>/check-runs -q '.check_runs[] | select(.name=="Native tests") | .id'); gh run view --job "$id" --log | grep "Tests Run"; gh pr view N --json mergeStateStatus,isDraft,headRefOid`
   Chain with `;`, not `&&`, so a red check still reads the log and the
   state. Never wait on a PID (`wait` works only for a child of the same
   shell, `kill -0` loops risk PID reuse) or with `pgrep` (`-x` matches the
   process name, not its arguments; `-f` matches its own command line).
8. **Windows checks and the test log.** Wine is not Windows (PR #90), and
   nothing built on `automaton` has run there. Every pull request that
   changes `src/` adds `docs/test-log/vX.Y.Z-<topic>.md` in the build
   commit: each check it leaves unrun, as Do / Expect / If it fails /
   Result (`docs/test-log/README.md` has the format). The notes' Open list
   keeps the item and points at the file; it does not repeat the steps.
   Never add it as a late push to a PR whose checks are green (that reruns
   all seven and moves the head). The integration tier
   (`Run-Integration.ps1 -Tier gate`) is the maintainer's to run on
   Windows and is not a gate. When the maintainer says they are ready to
   test, guide them from the open files, one group at a time, newest build
   first; record each result in the file and move a file whose checks all
   passed to `docs/test-log/done/` (a docs-only PR).
9. **Merge when ready** (maintainer, 2026-11-28: "Always merge when
   ready"; it replaces the per-merge go-ahead of 2026-11-12). Ready means
   all seven checks green on the latest commit, the branch up to date with
   `main`, the review done and its findings fixed after the maintainer has
   seen them, and the main session's check of the diff (step 5) done. Then,
   on `automaton` or the Windows box: `gh pr ready N`, read the checks once
   more (`gh pr view N --json statusCheckRollup,mergeStateStatus`), then
   `gh pr merge N --merge --auto` (it merges at once when mergeable, and
   CodeQL is not required, so never run it on a head whose checks are not
   all green). It prints nothing on success; read the PR state afterwards
   and tell the maintainer what was merged. In auto mode the classifier
   has refused `gh pr ready` without a go-ahead in the session (MEMORY.md);
   if it does, report the green checks and leave `ready` and `merge` to the
   maintainer, and do not use the MCP calls instead. **In a cloud (Linux)
   session never merge:** warn that the pipeline requires a successful
   `make test` and compiled code, which a cloud instance cannot do
   (CLAUDE.md), and do not mark ready or enable auto-merge. Never merge a
   red check; never push to `main`; never rewrite someone else's branch.
   The rule covers merging only: a feature's design still needs the
   maintainer's approval before it is built, and a release tag their word.
   Once the merge is confirmed and the branch gone, ask about a
   retrospective and read the new alerts (both in Standing rules).
10. **Release**, separately: tag `vX.Y.Z` matching `APP_VERSION` and push
    the tag; the release workflow re-verifies and publishes the committed
    exe. Nothing is built at release time.

## Failures that are not flakes

- A `Version bump` failure is deterministic; its log names the mismatch.
- A CodeQL alert is fixed or dismissed with a reason. Read the SARIF first
  (command in MEMORY.md, "GitHub API quirks"). It reads sources, not the
  exe, so iterate source-only fixes on a draft until `analyze` is clean,
  then bump and rebuild once. A file move renumbers old alerts.
- A CodeQL `analyze` failure in `apt-get` is the hosted image's stale
  third-party repositories; extend the workflow's strip list, do not ignore
  the exit code.

## Standing rules around it

- **After every merge-and-delete, or a pull request closed unmerged, ask
  the maintainer whether to run a retrospective** (the `checkpoint`
  skill), in the same session, right after confirming the branch is gone;
  run it only on a yes. The ask states the merge count since the last
  retrospective (maintainer, 2026-11-21; the command and what counts are in
  `checkpoint`, "When it runs"). The count informs the decision and never
  triggers one: no merge counter, no scheduled Routine (maintainer,
  2026-11-04).
- **New CodeQL alerts after any merge to `main`** (the session's, the
  maintainer's or Dependabot's): a pull request's run reports only its
  changed lines, so a new alert on old code shows only in the push run.
  As a background command:
  `id=$(gh run list --workflow codeql.yml --commit <merge-sha> --json databaseId -q '.[0].databaseId'); gh run watch "$id" >/dev/null; gh api 'repos/umbongo/nutshell/code-scanning/alerts?state=open' --jq '.[]|select(.created_at > "<merged_at>")|"#\(.number) \(.rule.id) \(.most_recent_instance.location.path)"'`
  (an empty `id` means the run has not started yet). Report each alert it
  prints; without `gh`, say it was not read.
- **Record of change.** Every edit to memory or a skill gets a dated
  entry in `.claude/CHANGELOG.md` in the same commit.
- **Dependabot.** Weekly, grouped, one open at a time. For an actions-only
  bump: the diff is only `.github/workflows/*.yml` pins with updated
  `# vX.Y.Z` comments; read the upstream changelog (the PR body quotes it);
  all seven checks green on the head; merge with a merge commit. Only the
  CodeQL push run follows (read its alerts, above); no release, no bump.
  A semver-major bump of `actions/checkout` or `upload-artifact` changes
  the runner's Node runtime; read its release notes first.
- **Branch survey.** "What are my branches" goes through `repo-status`;
  a cloud clone is shallow, and two names are both a branch and a tag.
