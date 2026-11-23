# Nutshell — project memory

Durable facts learned while working in this repository. Loaded into every
session through the `@.claude/memory/MEMORY.md` import in CLAUDE.md. Keep
entries short and factual; the reasoning lives in the docs they cite.
Every edit here gets a dated entry in `.claude/CHANGELOG.md`.

Last updated: 2026-11-21 (retrospective of PRs #95-#101: the merge count in the ask, baselines, the full-disk symptom, CodeQL's PR-only view, the Windows-host `make test`, open items).

## Retrospectives: ask after each merge (maintainer's rule, 2026-11-04)

- **After a branch is merged and deleted, or its pull request closed
  unmerged, ask the maintainer whether to run a retrospective** with the
  `checkpoint` skill: refresh this memory file, the skills under
  `.claude/skills/`, and the workflows (`.github/workflows/*.yml` and the
  process sections of CLAUDE.md), record it in `.claude/CHANGELOG.md`, and
  open a draft PR. Run it only on a yes.
- **The ask states how many merges `main` has had since the last
  retrospective** (maintainer, 2026-11-21): the command and what counts
  are in the `checkpoint` skill's "When it runs"; `<marker>` is the commit
  below. The count informs the decision and never triggers one; the
  no-counter rule stands.
- This replaced the every-10-merges cadence of 2026-09-21. There is no
  merge counter any more. The daily Routine "Nutshell: retrospective every
  10 merges" in the maintainer's Claude account was disabled on 2026-11-04
  (deleting it is the maintainer's click at claude.ai/code/routines). The
  maintainer declined a retrospective on 2026-11-04 and asked for none
  until the backlog was cleared (*reported*), so the 2026-11-12 one covered
  14 merges.
- **Last retrospective:** 2026-11-21, marker commit `be6599f` (merge of
  PR #97, Dependabot's CodeQL action bump; it covered seven merges, #95 to
  #101). The next one covers the merges since it
  (`git log --merges --first-parent be6599f..origin/main`); move this
  marker at every retrospective.

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
  changes the shared `~/.wine` prefix for every session. No `pwsh` is
  installed, so a harness case file cannot be parsed or run here; only the
  Windows box can check one. Before a push the PRs of 2026-11 ran
  `make -j test`, `make test SAN=1`, `make lint-gate`, `make fuzz-run` and
  `xvfb-run -a make wintest` here.
- **Never wait with `pgrep -f`.** `while pgrep -f 'make fuzz-long'; do
  sleep 5; done` matches its own `bash -c` command line and never exits:
  one such loop ran from 2026-11-05 to 2026-11-12 and kept a sub-agent
  "running" until the maintainer noticed (*reported*). `pgrep -x` matches
  the process name, not its arguments, and a PID wait fails too (`wait`
  works only for a child of the same shell, `kill -0` loops risk PID
  reuse). Run the job as a background command and take its completion
  notification (or Monitor).
- **When every Bash call fails, check the disk first** (`df -h /`):
  Claude Code writes each command's output under `/tmp/claude-1000`, so a
  full root filesystem fails every Bash call with ENOSPC. On 2026-11-21 the
  76 GB root was full and `/tmp/claude-1000/-home-thomas-nutshell` alone held
  2.1 GB of earlier sessions' scratch and task output; Desktop Commander's
  `start_process` still ran and was used to diagnose (*reported*). Old
  session directories there are the first candidate, then `~/.vscode-server`,
  `~/.npm`, `~/.cache` and `/var/log`; ask the maintainer before deleting
  anything, since other sessions may be live.
- **Wine is not Windows.** v1.2.49 to v1.2.59 died at launch on the
  maintainer's Windows 11 24H2 workstation and passed every `wintest`: the
  manifest did not ask for comctl32 v6, and that build's system32 comctl32
  5.82 no longer exports `SetWindowSubclass` and its siblings. Wine's does,
  so it never reproduced (PR #90). A build only counts as started on Windows
  once it has been run there.
- **A Windows-host `make test` is a third build** that neither `automaton`
  nor CI runs. It differs from the two here in two ways: MSYS2's gcc is
  newer (15.2 on 2026-11-13, *reported*; the cross-compile is gcc 13), so it
  warns where they do not, and it builds the native test runner, `tests/`
  included, with `_WIN32` defined. It broke twice unseen:
  `tests/test_settings_label_win.c` (PR #93) was missing from
  `WIN_ONLY_TESTS`, so it did not link from PR #93 to PR #100, and once
  PR #98 made it `-Werror`, four `-Wformat-truncation` warnings in a `_WIN32`
  branch of `ai_tool_web_search.c`, which gcc 13 compiles without a word,
  failed it (fixed in PR #100). A new `tests/*_win.c` file goes into
  `WIN_ONLY_TESTS`; a change to the build flags, or one a newer gcc may warn
  on, asks the maintainer for a `mingw32-make test`.
- **Never-pushed branches** can live in `~/nutshell-wt/*` worktrees, the
  main clone `~/nutshell` or the Windows box; check `automaton`'s
  (`repo-status` step 7) before calling an Open item untouched.
- **The Windows box** (MSYS2, `mingw32-make`): runs the integration harness
  and `mingw32-make wintest`, and can build too. Command blocks written for
  the maintainer there prefix a local file with `.\` and carry no `#`
  comment lines (maintainer, *reported* 2026-11).
- Native suite baseline: 4,133 tests, 0 failed, in the hosted `Native
  tests` run on PR #101's head (real libssh2, 2026-11-21); 4,092 on
  `automaton` with the libssh2 stub. Earlier hosted counts: 4,132 (#96 to
  #100), 4,118 (#94), 3,736 (#93), 3,314 (#81). Earlier stub counts: 4,091
  (#96 to #100), 4,077 (#94), 3,279 (2026-11-03), 3,035
  (2026-10-19), 2,485 (2026-10-13), 1,960 (2026-09-21). The two counts
  differ by the libssh2-gated files, so compare like with like. `wintest`:
  58 tests after PR #101 (56 after PR #96, 52 in PR #94's body, 45 on
  2026-11-03).
- After any change to `NutshellIT.psm1`'s `Add-Type` sources, run the
  harness from a fresh PowerShell window; the module stops at load and says
  so (`tests/integration/README.md`).

## Repository facts

- **Branches named with a version number (`v1.0.76`, `v1.1.21`, `v1.2.13`,
  `v1.2.41`, `v1.2.60`, `v1.2.89`) are snapshots and are never deleted** (maintainer, 2026-10-20),
  even though each is contained in `main`. "Clear out merged branches" means
  feature branches only; GitHub deletes those on merge
  (`delete_branch_on_merge` is on; a merged branch that still shows locally
  needs `git fetch --prune`). `v1.2.41` is a branch only, at `7960bd2`;
  `v1.2.60` is a branch only, at `0f5bf9a` (merge of PR #90, the comctl32
  fix). `v1.2.89` is a branch only, at `4c1a4e7` (merge of PR #104), the
  backup the maintainer asked for on 2026-11-23 before the AI switch and
  voice work: a backup branch is named `v` + the version on `main`.
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
- `build/win/nutshell.exe` (about 5.6 MB, UPX-packed, version resource left
  uncompressed) is committed on every build; 201 commits on `main` touch it
  (2026-11-21). Git LFS
  is an open question, untracking it is not (the gate reads its version).
- 93 files are CRLF in the index (2026-11-21, unchanged since 2026-11-12); `.gitattributes` holds only
  `tests/fixtures/*.bin binary`. Eight of the 93 are fuzz seeds under
  `tests/fuzz/corpus/` whose CR bytes are test input: mark that tree binary
  before any renormalise, or it rewrites them. The notes ask for eol rules
  and a lone renormalising pull request.
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
  publishes the committed exe with `gh release create`, with
  `nutshell.exe.sha256` beside it since PR #87. It has not run since
  v1.1.21, so that addition is untried.
- CodeQL (`codeql.yml`) runs on pull requests and on pushes to `main`,
  builds `make test` on ubuntu; since 2026-09-24 it installs `libssh2-1-dev`
  too, so the SSH files are scanned for real. `src/ui` is still never
  scanned (Win32 only), so neither is the Win32 half of a feature, such as
  the telnet socket backend `src/ui/telnet_io.c`.
- Dependabot (`.github/dependabot.yml`): github-actions only, weekly, all
  actions grouped into one pull request, open-pull-requests-limit 1. A
  merged actions-only bump triggers just the CodeQL push run; no release.
- The desktop integration gate and its self-hosted runner were removed on
  2026-09-15. `tests/integration/` is manual; never wait on a runner.

## GitHub API quirks seen here

- `list_pull_requests` returns `merged: false` even for merged pull
  requests when a `fields` subset is requested; use `merged_at` instead.
- Check runs on a pull request head come from `pull_request_read` with
  `get_check_runs`; the seven checks on any change, an actions-only one
  included, are `Version bump`, `Native tests`, `analyze` (CodeQL build+scan),
  `CodeQL` (upload), `Sanitizers`, `Lint` and `Fuzz`. On an actions-only bump
  `Native tests` and the three quality jobs skip their work (still green)
  unless the bump touches their own workflow file (`checks.yml`,
  `quality.yml`). Only the first two are required.
- `gh pr edit` fails with a projectCards GraphQL error. Update a body with
  `gh api -X PATCH repos/umbongo/nutshell/pulls/N -F body=@file`.
- In Claude Code's auto permission mode the classifier refused
  `gh pr ready N` as a merge without review (reported 2026-10-19); on
  2026-11-12 it accepted it after the maintainer's explicit go-ahead in the
  session (*reported*). Later on 2026-11-12 it refused `gh pr ready` after
  "proceed and merge", given before PR #96 existed, and accepted it after
  "Merge it" with PR #96 in play. Without a go-ahead (CLAUDE.md, "Branches,
  pull requests and the merge gate"), the maintainer runs `ready` and `merge`.
- A CodeQL finding on a pull request: read the SARIF before changing code
  (`gh api -H "Accept: application/sarif+json"
  repos/umbongo/nutshell/code-scanning/analyses/<id>`; ids from
  `.../code-scanning/analyses?ref=refs/pull/N/merge`). CodeQL reads the
  sources, not the exe. `cpp/use-after-free` fires on a growable buffer that frees its
  field and later reads it; the form it accepts swaps the new block into
  the field before freeing the old one through a local.
- **A pull request's CodeQL run reports only alerts on the lines it
  changed.** Alert #30 opened in the push run on PR #101's merge while the
  PR's own analysis, with the same CodeQL 2.27.5, had `results_count` 0
  (code-scanning analyses API); its flow runs through `loader.c` lines last
  changed in 2026-03 and 2026-09, and main's previous push run (2.27.4) did
  not flag it. The PR's SARIF carries the `codeql-action/pr-diff-range`
  extension (diff-informed analysis); #30 came from `codeql/cpp-queries`
  1.10.1 tracking flow through character types, and was dismissed as a false
  positive (it writes the preserved DPAPI blob). So a new alert on old code
  shows only on `main`; the `feature-workflow` standing rule reads it after
  every merge.
- Moving a file closes its alerts as "fixed" and opens new numbers for the
  same lines (#12 became #27 when `markdown.h` moved to `src/core`). Check
  an alert's history before calling it new.
- `cpp/world-writable-file-creation` fires on a plain `fopen(path, "w")` or
  `"w+"`, test files included. A test that creates a file opens it with
  `test_fopen_private()` (`tests/test_framework.h`, 0600 on POSIX, write-only)
  and reopens it with `fopen(path, "r")` to read it back (0499c11, PR #81).
  A test-only fix changes nothing the `Version bump` check looks at, so it
  needs no bump or rebuild.

## Open items worth knowing before starting work

See the "Open" list in the notes document for the authoritative version.
As of 2026-11-21 (`main` at v1.2.87: PR #103 took the two queued `src/`
fixes, the Search Engine help text and the dead `chat_thinking.[ch]`;
PR #101 added the Thinking row's fourth mode, Extended; PR #100 fixed the Windows-host `make test`; PR #97
moved `github/codeql-action` to v4.39.3; open pull requests: `gh pr list`,
none on 2026-11-21; no open CodeQL alert: #30 was dismissed as a false
positive on 2026-11-21): a backlog of Windows hand checks from PRs #85-#101, most of
them never run (five of those merges were unattended; #96's were reported
fine, not itemised); tool-gate follow-ups (PR #91); Settings follow-ups
(PR #93); web_search does not URL-encode
its query; a `vcpkg.json` manifest with a baseline (deferred by PR #87);
`ssh_channel_write` can block the UI thread for up to 5 s per write;
`run_command` live rounds on five of six providers; terminal escapes CSI I,
HTS, TBC and scroll-region gaps; harness cases never run (telnet,
`settings_help_tool_count`, `ui_gallery`); no release since v1.1.21;
web_fetch watchdog and TLS fail-closed; light-theme ANSI palette;
top-anchored ConPTY reflow; sub-project 3 needs a spec first.
`src/ui/window.c` is 6,330 lines, `chat_listview.c` 4,893, `ai_chat.c`
3,134 (the seven `ai_chat*.c` files 7,388 since the PR #86 split).

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
