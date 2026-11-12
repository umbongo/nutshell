---
name: feature-workflow
description: The end-to-end workflow for a Nutshell feature or fix, how work is allocated to sub-agents, and how a pull request is driven through the Version bump gate to a merge, including Dependabot bumps. Use when starting a feature branch, when asked "what is the process" or "who does what", before the first push of any change, and when watching, fixing or merging a PR in umbongo/nutshell. Restates the rules on main; it decides nothing new.
---

# Feature workflow and bot allocation

Last updated: 2026-11-12 (retrospective: the check wait, Windows checks into the Open list, merging from `automaton`). Changes are recorded in `.claude/CHANGELOG.md`.
The rules themselves live in CLAUDE.md ("Software Development Rules",
"Branches, pull requests and the merge gate"); this skill is the walk
through them in order. Where they disagree, CLAUDE.md wins. It absorbed
the former `steward` skill on 2026-09-21; the pull-request steps below
are that skill's content.

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
the `critique` skill, used when a wrong decision would be expensive to
migrate. A diff review is an Opus agent given the diff, never a summary
of it. Never poll a running agent; its completion notification is the
signal. **Every Opus review's findings reach the maintainer before any
fix** (CLAUDE.md). `subagent-driven-development` does not stop there by
itself, so the orchestrator's brief overrides its stop rules in so many
words: after any review run on Opus (the final whole-branch review, an Opus
re-review, a per-task review dispatched on Opus), return the ranked
findings and your rulings, and stop. The main session shows them to the
maintainer and, unless told otherwise in that reply, resumes that
orchestrator (SendMessage) for the fixes; if it cannot be resumed, a fresh
one gets the ledger path and the findings and starts at the fix. Sonnet
per-task reviews keep looping inside the run. Finish means push and open
or update the draft PR (step 5), never merge.

**Attribution:** every commit and pull request carries a `Models:` line
naming each model that touched the change and what it did, then the
co-author trailer (CLAUDE.md, "Git commits"). Subject and body are
written as the maintainer would write them: imperative, with the version
in parentheses when one was bumped, e.g.
`fix(tabs): Ctrl+W reattaches the surviving tab (v1.1.13)`. Pull requests
open as drafts and their body ends with the same `Models:` line and the
`🤖 Generated with [Claude Code](https://claude.com/claude-code)` footer.

## The workflow, in order

1. **Branch.** Cut from the latest `main`, push with `-u`. A branch with no
   commits cannot have a pull request; the draft PR opens with the first
   push that carries a commit.
2. **Brief and spec.** The maintainer says what the feature is. The
   survey of the relevant code goes to an Explore agent; the spec is
   written into `docs/superpowers/specs/YYYY-MM-DD-<name>-design.md`
   from its conclusions. The notes document's convention: spec first,
   mockups before choosing a direction for UI work. The usual route since
   PR #80: `superpowers:brainstorming` (spec), `superpowers:writing-plans`
   (plan in `docs/superpowers/plans/`), `superpowers:subagent-driven-development`
   (step 4).
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

   The main session reads the diff in full before the maintainer merges;
   that is the one place its context is spent on raw material. Stage new
   files by path and read every `??` line first (CLAUDE.md, "Git commits";
   PR #44 shipped without four files). A pull request stacked on an
   unmerged branch is retargeted to `main` by GitHub when its base is
   deleted on merge (else `gh api -X PATCH repos/umbongo/nutshell/pulls/N
   -f base=main`; `gh pr edit` fails here); it then needs
   `gh api -X PUT repos/umbongo/nutshell/pulls/N/update-branch`, and about
   2.5 minutes for the checks to rerun on the new head.
6. **The gate.** Any change under `src/`, the Makefile or `nutshell.rc`
   needs `APP_VERSION` and `APP_VERSION_BINARY` raised in
   `src/ui/resource.h`, README's `**Version**:` line to match, and a
   rebuilt `build/win/nutshell.exe` committed. The build happens on one
   of the maintainer's own machines: the Windows box (`mingw32-make clean
   && mingw32-make release`) or the Linux box `automaton`, whose
   `x86_64-w64-mingw32-gcc` plus vcpkg cross-compile builds the same exe
   (CLAUDE.md, "Building on Linux"). A spec or plan names the new version
   as "one above both `main`'s version and the branch's last build, at
   build time" (CLAUDE.md's rule: patch +1, and after X.Y.99 comes
   X.(Y+1).0), never a literal number:
   a plan written against a stale base named a version `main` had already
   passed. From
   a cloud session, which has neither, the deliverable is a pushed branch;
   the maintainer builds, commits the exe, and the `Version bump` check
   goes green. Say so and stop at the pushed branch with everything else
   done. Docs, CI, `tests/` and `.claude/` changes need none of this, but
   `Native tests` still has to compile the tree, so a test-only change is
   gated too.
7. **Checking the pull request.** Checks: `gh pr checks N`, or
   `mcp__github__pull_request_read` with `get_check_runs` when `gh` is
   absent. Expect `Version bump`, `Native tests`, `analyze`, `CodeQL`,
   `Sanitizers`, `Lint` and `Fuzz`; all seven must be `success`. The first
   two are required (`Native tests` compiles the committed sources with the
   real libssh2 and runs the suite; `Version bump` compiles nothing); the
   other five are not, so a red one does not block auto-merge, but none is
   merged red. A docs-only pull request still shows all seven: the quality
   jobs report green with their work skipped. The weekly `Fuzz (scheduled)`
   run (`fuzz-weekly.yml`) is not a pull-request check. Mergeability:
   `pull_request_read` with `get`; `mergeable_state` should be `clean`,
   and the ruleset also requires the branch be up to date with `main`.
   The checks run on `pull_request` only, so there is no base-branch run
   to compare against; a red gate is always the PR's. A wait that worked on
   every push of PRs #85-#94: one background command running
   `gh pr checks N --watch --interval 30`, then the `Tests Run` line of the
   `Native tests` job log (checkpoint step 1 has the command), then
   `gh pr view N --json mergeStateStatus,isDraft,headRefOid`; act on its
   completion notification. Never a `pgrep -f` loop (MEMORY.md).
8. **Integration tier and Windows checks.** Before marking ready, the
   maintainer runs the integration tier the change touches by hand on
   Windows (`tests/integration/Run-Integration.ps1 -Tier gate`); it is a
   manual tool, not a gate. Wine is not Windows (PR #90). A pull request
   merged with its Windows checks not run (an unattended merge, say) lists
   them in its body, and the same list goes into the notes' Open list in
   the same pull request.
9. **Merge.** First, where are you: **in a cloud (Linux) session, stop
   here and warn the maintainer** that the pipeline requires a successful
   `make test` and compiled code before it can merge and that this cannot
   be done from a cloud instance (CLAUDE.md, the gate section); do not
   mark ready, do not enable auto-merge. On `automaton` or the Windows box: `gh pr ready N`, then `gh pr checks N --watch` until every
   check is green, then `gh pr merge N --merge --auto`, in that order. The
   reason is in CLAUDE.md ("Branches, pull requests and the merge gate"):
   auto-merge merges at once when the PR is mergeable, and CodeQL is not
   required. It prints nothing on success; read the PR state afterwards.
   In Claude Code's auto permission mode `gh pr ready` can be refused as a
   merge without review: without the maintainer's go-ahead for this pull
   request in the session, report the green checks and leave `ready` and
   `merge` to the maintainer, and do not use the MCP calls below instead.
   With it (as for PR #94), run them. If the head moved after the
   maintainer last tested a build, say what was not seen and offer to hold.
   Without `gh`, in a session allowed to merge,
   `mcp__github__update_pull_request` to leave draft, then
   `mcp__github__enable_pr_auto_merge` with merge method `merge`. Never
   merge a red check; never push to `main`; never rewrite someone else's
   branch. Once the merge is confirmed and the branch gone (GitHub deletes
   it on merge), ask the maintainer whether to run a retrospective now.
10. **Release**, separately: tag `vX.Y.Z` matching `APP_VERSION` and push
    the tag; the release workflow re-verifies and publishes the committed
    exe. Nothing is built at release time.

## Failures that are not flakes

- A `Version bump` failure is deterministic: read its log, it names the
  exact mismatch.
- A CodeQL alert on the pull request is fixed or dismissed with a reason.
  Read the SARIF path before changing code (the command is in MEMORY.md,
  "GitHub API quirks"). CodeQL reads the sources, not the exe, so on a
  draft iterate source-only fixes until `analyze` is clean, then bump and
  rebuild once (`Version bump` only has to pass on the final head). A file
  move renumbers its old alerts; check an alert's history before treating
  it as new.
- A CodeQL `analyze` failure in `apt-get` is the hosted image's stale
  third-party repositories; the workflow already strips them by host. If
  it recurs, extend that list, do not ignore the exit code.

## Standing rules around it

- **After every merge-and-delete, or a pull request closed unmerged, ask
  the maintainer whether to run a retrospective** (the `checkpoint`
  skill), in the same session, right after confirming the branch is gone;
  run it only on a yes. There is no merge counter and no scheduled
  Routine (maintainer, 2026-11-04, replacing "every 10 merges"); the
  last-retrospective marker in `MEMORY.md` is only the scope of the next
  one.
- **Record of change.** Every edit to memory or a skill gets a dated
  entry in `.claude/CHANGELOG.md` in the same commit.
- **Dependabot.** Weekly, grouped, one open at a time. For an
  actions-only bump: confirm the diff is only `.github/workflows/*.yml`
  pins with updated `# vX.Y.Z` comments; read the upstream changelog for
  the new version (the PR body quotes it); confirm all seven checks are
  green on the head commit; merge with a merge commit. Dependabot deletes
  its branch. Only the CodeQL push run fires afterwards; no release, no
  version bump. Semver-major bumps of `actions/checkout` or
  `upload-artifact` change the Node runtime the runner needs; check the
  release notes before merging.
- **Branch survey.** "What are my branches" goes through `repo-status`;
  a cloud clone is shallow, and two names are both a branch and a tag.
