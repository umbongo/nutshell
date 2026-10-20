# Nutshell — project memory

Durable facts learned while working in this repository. Loaded into every
session through the `@.claude/memory/MEMORY.md` import in CLAUDE.md. Keep
entries short and factual; the reasoning lives in the docs they cite.
Every edit here gets a dated entry in `.claude/CHANGELOG.md`.

Last updated: 2026-10-20 (checkpoint after PR #74).

## Retrospective cadence (standing rule from the maintainer, 2026-09-21)

- **Every 10 merges into `main`, run a retrospective** with the `checkpoint`
  skill: refresh this memory file, the skills under `.claude/skills/`, and
  the workflows (`.github/workflows/*.yml` and the process sections of
  CLAUDE.md), record it in `.claude/CHANGELOG.md`, and open a draft PR.
- **Counter.** Merges are pull requests merged into `main` (`merged_at`
  set) after the last retrospective marker below. Count them with
  `list_pull_requests` (state closed, sort updated, desc) or
  `git log --merges --first-parent origin/main <marker>..`.
- **Last retrospective:** 2026-10-19, marker commit `7960bd2` (merge of
  PR #71, the head of `main` when the retrospective was written). Merges
  since: 0 (the retrospective's own PR will be the first). Update this line
  at every retrospective. The 2026-10-19 count from `c593796` was 11
  (#60-#69, #71), including the 2026-10-13 retrospective's own PR #62.
- A daily Routine in the maintainer's Claude account performs the count
  and starts the retrospective in a fresh session when it reaches 10. Any
  session that notices the count is at or past 10 should run it too.

## Working from a cloud (Linux) session

- No `x86_64-w64-mingw32-gcc` and no libssh2 on the host. `make test` works
  (stock gcc, OpenSSL present, SSH code compiles against `tests/stubs`),
  `make release` does not. **Any change under `src/`, the `Makefile` or
  `nutshell.rc` cannot pass the `Version bump` gate from here** because the
  gate requires a rebuilt, committed `build/win/nutshell.exe`. Finish such
  changes on one of the maintainer's machines (below). Docs, CI, `tests/`, `docs/` and
  `.claude/` changes can be pushed from here, but **never merged from
  here**: the maintainer's standing rule (2026-09-24) is that a cloud
  session stops and warns that the pipeline requires a successful
  `make test` and compiled code before it can merge, which cannot be done
  on a cloud instance.
- The clone arrives shallow (depth 50) with only `main` and the session
  branch fetched. `git fetch --unshallow origin main` takes about a minute
  and brings the pack to roughly 610 MiB; do it before any ahead/behind or
  merge-base question, or the answer is wrong.
- No `gh` CLI. Pull requests, check runs and branch listings go through the
  `mcp__github__*` tools. Network is proxied; do not disable TLS checks.

## The maintainer's two machines

- **`automaton`**: the Linux box whose `hostname` prints `automaton`; any
  other Linux host is a cloud session (above). Full clone, `gh`
  authenticated, worktrees under `~/nutshell-wt/`, other Claude sessions
  often running at once. It builds the gate-passing exe; the commands are in
  CLAUDE.md ("Building on Linux"). Wine's DPI for `wintest` is
  `HKCU\Control Panel\Desktop\LogPixels` (REG_DWORD 192 for 200 %; delete
  the value to restore 96) -- reported 2026-10-19, not measured here, and it
  changes the shared `~/.wine` prefix for every session.
- **The Windows box** (MSYS2, `mingw32-make`): runs the integration harness
  and `mingw32-make wintest`, and can build too.
- Native suite baseline on 2026-10-19 (`automaton`, libssh2 stub): 3,035
  tests, 0 failed (2,485 on 2026-10-13, 1,960 on 2026-09-21). The hosted
  `Native tests` job links the real libssh2, so its count may differ by the
  libssh2-gated files.
- After any change to `NutshellIT.psm1`'s `Add-Type` sources, run the
  harness from a fresh PowerShell window; the module stops at load and says
  so (`tests/integration/README.md`).

## Repository facts

- **Branches named with a version number (`v1.0.76`, `v1.1.21`, `v1.2.13`)
  are snapshots and are never deleted** (maintainer, 2026-10-20), even
  though each is contained in `main`. "Clear out merged branches" means
  feature branches only; GitHub deletes those on merge.
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
- 85 files are CRLF in the index; `.gitattributes` holds only
  `tests/fixtures/*.bin binary`. The notes ask for eol rules and a lone
  renormalising pull request.
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
- `Native tests` skips its compile and suite (still reporting green) when
  a pull request changes nothing under `src/`, `tests/`, `build-support/`,
  the Makefile, `nutshell.rc`, `README.md` or `checks.yml`. `README.md`
  joined the list on 2026-10-20: `tests/test_shortcuts.c` compares its
  generated block between `<!-- shortcuts:begin -->` and
  `<!-- shortcuts:end -->` with `src/core/shortcuts.c` byte for byte, and
  fails rather than skips under `CI` when README.md cannot be read. The
  regeneration recipe is in that test file.
- **`gh pr merge --auto` merges at once when the PR is already mergeable**
  and prints nothing on success. CodeQL's `analyze` is not required, so a
  red one does not block. PR #44 was merged with `analyze` red (four new
  files unstaged), #45 with it pending, which is why `Native tests` exists.
  The rule is in CLAUDE.md: `gh pr checks N --watch` until every check is
  green, then merge.
- **A stacked PR**: GitHub retargets it to `main` when its base branch is
  deleted on merge (else `gh api -X PATCH repos/umbongo/nutshell/pulls/N
  -f base=main`); it then needs
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
- `gh pr edit` fails with a projectCards GraphQL error. Update a body with
  `gh api -X PATCH repos/umbongo/nutshell/pulls/N -F body=@file`.
- In Claude Code's auto permission mode the classifier refuses
  `gh pr ready N` as a merge without review (reported 2026-10-19). The
  maintainer runs `ready` and `merge`.
- A CodeQL finding on a pull request: read the SARIF before changing code
  (`gh api -H "Accept: application/sarif+json"
  repos/umbongo/nutshell/code-scanning/analyses/<id>`; ids from
  `.../code-scanning/analyses?ref=refs/pull/N/merge`). CodeQL reads the
  sources, not the exe. `cpp/use-after-free` fires on a growable buffer that frees its
  field and later reads it; the form it accepts swaps the new block into
  the field before freeing the old one through a local.
- Moving a file closes its alerts as "fixed" and opens new numbers for the
  same lines (#12 became #27 when `markdown.h` moved to `src/core`). Check
  an alert's history before calling it new.

## Open items worth knowing before starting work

See the "Open" list in the notes document for the authoritative version.
As of 2026-10-20 (`main` at v1.2.44, PR #74; no open drafts): truncated
extended-colour SGR leak; designed light-theme ANSI palette; selection
follow-ups (Shift+click, Alt+drag, middle paste, the scrollback-size
setting); the #70 ConPTY probe run on Windows; two failing tab-switch
harness cases; old CodeQL alerts; no release since v1.1.21; the whole-repository
security review; finished-batch command details; AI panel selection
follow-ups; local shell follow-ups; paint-path polling; `.gitattributes`
renormalise; sub-project 3 needs a spec first. `src/ui/window.c` is 5,440
lines and `ai_chat.c` 5,908.

## Local shell and keys, facts that outlive the session

- The shell resolver order (`src/core/local_shell.c`, v1.2.14): profile
  command, then PowerShell 7, Windows PowerShell, Git for Windows bash, MSYS2
  bash, cmd -- only shells installed on the PC, absolute paths only. busybox
  was removed as a shell on 2026-09-24 by the maintainer's decision (the
  classifier still knows the `busybox` command on remote hosts). PowerShell
  and cmd local sessions are locked to the unresolved (strictest) ruleset.
- Secrets are per-user DPAPI (`$dpapi$v1$`) since v1.2.14; the config stays
  beside the exe and travels, a blob another PC or user cannot decrypt is kept
  verbatim and written back. Legacy `$aes256gcm$v1$` blobs are read once and
  migrated. No ACL checks on the config (maintainer's decision).
- Platform auto-detection may only ever move a session to a stricter ruleset
  after its first resolution (v1.2.14); host output never loosens it.
- ConPTY, measured by `make wintest`: `0x7F` is Backspace, `0x08` is
  Ctrl+Backspace, `?1049h` is forwarded to the terminal, `?1h` is not, no
  DSR/DA query on open, a lone ESC arrives as Escape. Never answer `?9001h`.
- The message loop runs `TranslateMessage` first, so a handled key-down does
  not stop its `WM_CHAR`; `window.c` removes the queued character by scan
  code. Alt chords are sent and also passed to `DefWindowProc` so a lone Alt
  tap still opens the menu.
