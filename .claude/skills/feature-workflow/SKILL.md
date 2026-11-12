---
name: feature-workflow
description: The end-to-end workflow for a Nutshell feature or fix, how work is allocated to sub-agents, and how a pull request is driven through the Version bump gate to a merge, including Dependabot bumps. Use when starting a feature branch, when asked "what is the process" or "who does what", before the first push of any change, and when watching, fixing or merging a PR in umbongo/nutshell. Restates the rules on main; it decides nothing new.
---

# Feature workflow and bot allocation

Last updated: 2026-11-12 (retrospective: the check wait, Windows checks into the Open list, merging from `automaton`). Changes are recorded in `.claude/CHANGELOG.md`.
The rules live in CLAUDE.md ("Software Development Rules", "Branches, pull
requests and the merge gate"); this skill walks through them in order, and
CLAUDE.md wins where they disagree. History and rationale: the retrospectives.

## Allocation

**One rule:** hand off to sub-agents wherever possible, without letting
the quality of the result suffer. The purpose is to curate what enters
the main session's context. A survey returns conclusions; an
implementation returns a diff and a test summary; a review returns ranked
findings. Raw files, transcripts and intermediate detail stay in the
sub-agent. The main session keeps the decisions, the diffs it reviews,
the reports and the key learnings.

**Model choice is guidance, not a chain of command:**

| Job | Model |
|---|---|
| Judgement: reviews, critiques, orchestrating a multi-step package | Opus (`claude-opus-5-5`) |
| Code and tests | Sonnet (`claude-sonnet-5-5`) |
| Mechanical, single-pattern work: renames, fixtures, doc tables | Haiku (`claude-haiku-4-5-20251001`) |
| Surveys of the tree that should return conclusions, not files | Explore agent |

A sub-agent may spawn its own.

**Direct work** is allowed only when a handoff would curate nothing: the
main session already holds everything the task needs and a brief would
be longer than the change. Say so in one line.

**Quality checks are sub-agents too.** A second opinion on a decision is
the `critique` skill. A diff review is an Opus agent given the diff, never a
summary. Never poll a running agent; its completion notification is the
signal. **Every Opus review's findings reach the maintainer before any
fix** (CLAUDE.md). `subagent-driven-development` does not stop there, so the
orchestrator's brief overrides its stop rules: after any Opus review, return
the ranked findings and your rulings, and stop. The main session shows them
and, unless told otherwise, resumes that orchestrator (SendMessage) for the
fixes (a fresh one gets the ledger path and findings if it cannot be
resumed). Sonnet per-task reviews keep looping inside the run. Finish means
push and open or update the draft PR (step 5), never merge.

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
4. **Implementation.** A sub-agent works on the branch: Opus orchestrating
   when the package is multi-step, otherwise Sonnet directly, Haiku for
   the mechanical parts. Tests first (CLAUDE.md, "Test-Driven
   Development"). `make test` before every push. What returns to the main
   session is the diff and the test summary line, and mid-run the final
   review's findings (Allocation, "Quality checks").
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

   The main session reads the diff in full before the maintainer merges.
   Stage new files by path and read every `??` line first (CLAUDE.md, "Git
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
8. **Integration tier and Windows checks.** Before marking ready, the
   maintainer runs the integration tier the change touches by hand on
   Windows (`Run-Integration.ps1 -Tier gate`); it is not a gate. Wine is
   not Windows (PR #90). Any Windows check the PR leaves unrun goes into
   the notes document's Open list in the same PR when it is opened, or in a
   docs-only PR straight after the merge; never as a late push to a PR
   whose checks are green (that reruns all seven and moves the head).
9. **Merge.** **The maintainer's explicit go-ahead is a condition in every
   permission mode, before any command below.** A go-ahead means the
   maintainer, in this session, names `ready` or `merge` and the PR number;
   "go ahead with the fixes", an earlier plan or an unattended-run memory
   is not one. If the head moves after the maintainer's last test or
   go-ahead (a merge of `main`, a review fix, a rebuilt exe), say what they
   have not seen and HOLD until they say yes again. In auto mode the
   classifier may refuse `gh pr ready` (MEMORY.md); report the green checks
   and leave `ready` and `merge` to the maintainer, and do not use the MCP
   calls below instead. **In a cloud (Linux) session never merge:** warn
   that the pipeline requires a successful `make test` and compiled code,
   which a cloud instance cannot do (CLAUDE.md), and do not mark ready or
   enable auto-merge. With the go-ahead, on `automaton` or the Windows box:
   `gh pr ready N`, then `gh pr checks N --watch` until every check is
   green, then `gh pr merge N --merge --auto`, in that order (auto-merge
   merges at once when mergeable, and CodeQL is not required). It prints
   nothing on success; read the PR state afterwards. Without `gh`:
   `mcp__github__update_pull_request` to leave draft, then
   `mcp__github__enable_pr_auto_merge` (method `merge`). Never merge a red
   check; never push to `main`; never rewrite someone else's branch. Once
   the merge is confirmed and the branch gone, ask the maintainer whether
   to run a retrospective now.
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
  run it only on a yes. No merge counter, no scheduled Routine
  (maintainer, 2026-11-04); the marker in `MEMORY.md` is only the scope.
- **Record of change.** Every edit to memory or a skill gets a dated
  entry in `.claude/CHANGELOG.md` in the same commit.
- **Dependabot.** Weekly, grouped, one open at a time. For an actions-only
  bump: the diff is only `.github/workflows/*.yml` pins with updated
  `# vX.Y.Z` comments; read the upstream changelog (the PR body quotes it);
  all seven checks green on the head; merge with a merge commit. Only the
  CodeQL push run follows; no release, no bump. A semver-major bump of
  `actions/checkout` or `upload-artifact` changes the runner's Node
  runtime; read its release notes first.
- **Branch survey.** "What are my branches" goes through `repo-status`;
  a cloud clone is shallow, and two names are both a branch and a tag.
