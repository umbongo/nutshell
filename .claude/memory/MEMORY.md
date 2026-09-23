# Nutshell — project memory

Durable facts learned while working in this repository. Loaded into every
session through the `@.claude/memory/MEMORY.md` import in CLAUDE.md. Keep
entries short and factual; the reasoning lives in the docs they cite.
Every edit here gets a dated entry in `.claude/CHANGELOG.md`.

Last updated: 2026-09-24 (retrospective).

## Retrospective cadence (standing rule from the maintainer, 2026-09-21)

- **Every 10 merges into `main`, run a retrospective** with the `checkpoint`
  skill: refresh this memory file, the skills under `.claude/skills/`, and
  the workflows (`.github/workflows/*.yml` and the process sections of
  CLAUDE.md), record it in `.claude/CHANGELOG.md`, and open a draft PR.
- **Counter.** Merges are pull requests merged into `main` (`merged_at`
  set) after the last retrospective marker below. Count them with
  `list_pull_requests` (state closed, sort updated, desc) or
  `git log --merges --first-parent origin/main <marker>..`.
- **Last retrospective:** 2026-09-24, marker commit `21e4d5d` (merge of
  PR #45, the head of `main` when the retrospective was written). Merges
  since: 0 (the retrospective's own PR will be the first). Update this line
  at every
  retrospective.
- A daily Routine in the maintainer's Claude account performs the count
  and starts the retrospective in a fresh session when it reaches 10. Any
  session that notices the count is at or past 10 should run it too.

## Working from a cloud (Linux) session

- No `x86_64-w64-mingw32-gcc` and no libssh2 on the host. `make test` works
  (stock gcc, OpenSSL present, SSH code compiles against `tests/stubs`),
  `make release` does not. **Any change under `src/`, the `Makefile` or
  `nutshell.rc` cannot pass the `Version bump` gate from here** because the
  gate requires a rebuilt, committed `build/win/nutshell.exe`. Finish such
  changes on the Windows dev box. Docs, CI, `tests/`, `docs/` and
  `.claude/` changes merge fine from here.
- The clone arrives shallow (depth 50) with only `main` and the session
  branch fetched. `git fetch --unshallow origin main` takes about a minute
  and brings the pack to roughly 610 MiB; do it before any ahead/behind or
  merge-base question, or the answer is wrong.
- No `gh` CLI. Pull requests, check runs and branch listings go through the
  `mcp__github__*` tools. Network is proxied; do not disable TLS checks.
- Native suite baseline on 2026-09-21 (Linux): 1,960 tests. On 2026-09-24 the
  Windows host ran 2,157; the Linux count may differ by the libssh2-gated files.

## Repository facts

- `main` was the redesign branch `ui-polish`, renamed on 2026-09-07. The
  pre-redesign main lives on as branch **and** tag `v1.0.76`. `v1.1.21` is
  also both a branch and a tag; the branch is fully contained in `main`.
  Because of the collisions, `git fetch origin v1.1.21` resolves to the
  tag: fetch branches by explicit refspec
  (`refs/heads/v1.1.21:refs/remotes/origin/v1.1.21`).
- The running todo and checkpoint log is
  `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`. Checkpoints are
  committed as `docs: checkpoint YYYY-MM-DD — ...`. Retrospectives go in
  `docs/retrospectives/`.
- `build/win/nutshell.exe` (about 5.2 MB, UPX-packed, version resource left
  uncompressed) is committed on every build; 104 commits touch it. Git LFS
  is an open question, untracking it is not (the gate reads its version).
- 89 files are CRLF in the index and there is no `.gitattributes`. The
  notes ask for a lone renormalising pull request.
- `.claude/` is gitignored except `memory/`, `skills/` and `CHANGELOG.md`.

## The merge gate, precisely

- Two required checks, both in `.github/workflows/checks.yml` on
  `pull_request` only: `Native tests` (Ubuntu, real libssh2, `make test`;
  since 2026-09-24) and `Version bump` (`.github/scripts/check-version.sh`).
  The ruleset is applied by `tests/integration/Protect-Main.ps1`, whose
  default `-CheckNames` lists both. `Version bump` always
  checks that `APP_VERSION`, `APP_VERSION_BINARY`, README's `**Version**:`
  line and the exe's FileVersion agree. If `src/`, `Makefile` or
  `nutshell.rc` changed against the base, it also requires the version to
  be strictly greater than the base's and the exe to be in the diff.
- **`gh pr merge --auto` merges at once when the PR is already mergeable**
  and prints nothing on success. CodeQL's `analyze` is not required, so a
  red one does not block. PR #44 was merged with `analyze` red (four new
  files unstaged), #45 with it pending, which is why `Native tests` exists.
  The rule is in CLAUDE.md: `gh pr checks N --watch` until every check is
  green, then merge.
- **A stacked PR**: GitHub retargets it to `main` when its base branch is
  deleted on merge (else `gh pr edit N --base main`); it then needs
  `gh api -X PUT repos/umbongo/nutshell/pulls/N/update-branch` because the
  ruleset requires up-to-date, and the checks rerun on the new head.
- `git add -u` skips new files: that is how PR #44 shipped without four of
  them. The staging rule is in CLAUDE.md ("Git commits").
- Nothing rebuilds anywhere in CI. `release.yml` fires on a `v*` tag (or
  manual dispatch), re-verifies tag == `resource.h` == exe version, and
  publishes the committed exe with `gh release create`.
- CodeQL (`codeql.yml`) runs on pull requests and on pushes to `main`,
  builds `make test` on ubuntu; since 2026-09-24 it installs `libssh2-1-dev`
  too, so the SSH files are scanned for real. `src/ui` is still never
  scanned (Win32 only).
- Dependabot (`.github/dependabot.yml`): github-actions only, weekly, all
  actions grouped into one pull request, open-pull-requests-limit 1. A
  merged actions-only bump triggers just the CodeQL push run; no release.
- The desktop integration gate and its self-hosted runner were removed on
  2026-09-15. `tests/integration/` is manual; never wait on a runner.

## GitHub API quirks seen here

- `list_pull_requests` returns `merged: false` even for merged pull
  requests when a `fields` subset is requested; use `merged_at` instead.
- Check runs on a pull request head come from `pull_request_read` with
  `get_check_runs`; the three checks on an actions-only change are
  `Version bump`, `analyze` (CodeQL build+scan) and `CodeQL` (upload).

## Open items worth knowing before starting work

See the "Open" list in the notes document for the authoritative version.
As of 2026-09-24 (`main` at v1.2.5, PR #45; PR #42 open, superseded by
the 2026-09-24 checkpoint): two manual checks for the maintainer (the
dispatch fix and the special-keys checklist); the merge gate compiles
nothing (require `analyze` or add a `make test` job); local shell
follow-ups (PowerShell as shell, busybox sidecar run, GPL decision on
embedding, dispatcher out of `src/ui`, timeout, WSL, OSC 7, mouse
reporting); then the older items: CodeQL libssh2 gap; `.gitattributes`
renormalise (89 files); harness batches B–D; security audit H3–H8;
AI-stream thread lifetime; Session Manager phantom row; status-line
keyboard path; `md_render.c` DPI; sub-project 3 needs a spec first.

## Local shell and keys, facts that outlive the session

- The shell resolver order (`src/core/local_shell.c`): profile command,
  `busybox64.exe` beside the exe, the same in `%LOCALAPPDATA%\Nutshell\runtime`,
  Git for Windows bash, MSYS2 bash. busybox-w32 is GPLv2; embedding it in the
  MIT exe is deferred until the maintainer decides on shipping its source.
- ConPTY, measured by `make wintest`: `0x7F` is Backspace, `0x08` is
  Ctrl+Backspace, `?1049h` is forwarded to the terminal, `?1h` is not, no
  DSR/DA query on open, a lone ESC arrives as Escape. Never answer `?9001h`.
- The message loop runs `TranslateMessage` first, so a handled key-down does
  not stop its `WM_CHAR`; `window.c` removes the queued character by scan
  code. Alt chords are sent and also passed to `DefWindowProc` so a lone Alt
  tap still opens the menu.
