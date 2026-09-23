# Nutshell — project memory

Durable facts learned while working in this repository. Loaded into every
session through the `@.claude/memory/MEMORY.md` import in CLAUDE.md. Keep
entries short and factual; the reasoning lives in the docs they cite.
Every edit here gets a dated entry in `.claude/CHANGELOG.md`.

Last updated: 2026-09-23 (merge counter).

## Retrospective cadence (standing rule from the maintainer, 2026-09-21)

- **Every 10 merges into `main`, run a retrospective** with the `checkpoint`
  skill: refresh this memory file, the skills under `.claude/skills/`, and
  the workflows (`.github/workflows/*.yml` and the process sections of
  CLAUDE.md), record it in `.claude/CHANGELOG.md`, and open a draft PR.
- **Counter.** Merges are pull requests merged into `main` (`merged_at`
  set) after the last retrospective marker below. Count them with
  `list_pull_requests` (state closed, sort updated, desc) or
  `git log --merges --first-parent origin/main <marker>..`.
- **Last retrospective:** 2026-09-21, marker commit `3f0f540` (merge of
  PR #34). Merges since: 6 (PR #33, #35, #36, #37, #39, #40; counted
  2026-09-23, `main` at `0696257`). Update this line at every
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
- Native suite baseline on 2026-09-21: 1,960 tests, 0 failures.

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

- One required check, `Version bump` (`.github/workflows/checks.yml`,
  `.github/scripts/check-version.sh`), on `pull_request` only. It always
  checks that `APP_VERSION`, `APP_VERSION_BINARY`, README's `**Version**:`
  line and the exe's FileVersion agree. If `src/`, `Makefile` or
  `nutshell.rc` changed against the base, it also requires the version to
  be strictly greater than the base's and the exe to be in the diff.
- Nothing rebuilds anywhere in CI. `release.yml` fires on a `v*` tag (or
  manual dispatch), re-verifies tag == `resource.h` == exe version, and
  publishes the committed exe with `gh release create`.
- CodeQL (`codeql.yml`) runs on pull requests and on pushes to `main`,
  builds `make test` on ubuntu, installs only `libssl-dev`: SSH files
  compile against the stub and `src/ui` is never scanned. Open item since
  2026-09-10.
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
As of 2026-09-21 (PR #33 and #34 merged, `terminal1` branch pushed from
`main` for the next feature, no pull request open): CodeQL libssh2 gap; `.gitattributes` renormalise; harness batches B–D;
security audit H3–H8; AI-stream thread lifetime; Session Manager phantom
row; status-line keyboard path; `md_render.c` DPI; sub-project 3 (main
window chrome) needs a spec first.
