# Nutshell repository retrospective — 2026-09-21

> **DRAFT.** Prepared by Claude from the repository state on 2026-09-21.
> Not yet reviewed by the maintainer. Numbers are measured, judgements are
> the author's.

## Scope and method

Examined: the full history of `main` (the session's shallow clone was
unshallowed to 378 commits), all 33 pull requests through the GitHub API,
the three workflows and two gate scripts, the running notes document
(`docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`), the remote
branch and tag refs, and a native `make test` run on this Linux host.

Not examined: the Windows build (this host has no MinGW cross-compiler and
no libssh2), the integration harness (needs a Windows host and a live SSH
target), and the security audit document, which is kept out of the repo.

## Timeline

| Period | What happened |
|---|---|
| 2026-02-24 | Initial commit as "Conga.SSH". |
| March–April | Build-out of the 1.0 line: terminal, tabs, session manager, AI chat, agentic tool loop (PR #9, v1.0.42). 241 commits in two months. |
| May–July | Dormant: one commit in May, one in June, none in July. |
| August | Restart at v1.0.76: CLI parameters, autoconnect, settings redesign specs. |
| 2026-09-06/07 | Redesign branch `ui-polish` becomes `main`; the old main is kept as branch `v1.0.76`. Design-system foundation lands (v1.0.82–92), AI Assist panel (v1.0.93–96), v1.1.0 on 2026-09-07. |
| 2026-09-09 | CI hardened in one day: SHA-pinned actions, Dependabot, the Version bump gate, a release workflow, and a self-hosted desktop integration gate (PRs #10–#13). |
| 2026-09-10/11 | Command safety classification across ten CLI families, per-profile platform, UNKNOWN category, one status-line policy control (PRs #24–#27). |
| 2026-09-14/15 | Desktop integration gate retired; the hosted check now verifies the committed exe; releases publish the committed exe (PRs #29–#30). About window and theme fixes bring `main` to v1.1.23. |
| 2026-09-16 | Dependabot opens PR #33 (CodeQL action 4.38.0); green, unmerged. |

Six of the last seven days of work went through pull requests with a
version bump and a rebuilt binary each. 116 commits landed in September,
more than any month since March.

## The code today

| Area | Files | Lines |
|---|---|---|
| `src/ui` (Win32, untested natively) | 38 | 21,526 |
| `src/core` (portable, tested) | 101 | 16,129 |
| `src/term` | 12 | 2,048 |
| `src/config` | 9 | 1,452 |
| `src/crypto` | 2 | 465 |

Native suite on this Linux host: **1,960 tests, 0 failures** across 79 test
files. The notes document still says 1,804, so 156 tests were added since
the last checkpoint. The integration harness has 30 cases in seven case
files and is now a manual tool only.

Half the code by line count lives in `src/ui`, which the native test build
excludes by design. The design-system gates in `tests/test_ui_tokens.c`
are the one automated check that reaches into `src/ui`, and they are a
source-text gate, not a behaviour test. That is the accepted trade-off of
a raw-Win32 application, but it means the UI's regression coverage is the
manual harness and the `--ui-demo` gallery, nothing that runs on a merge.

## Process and CI

The merge gate is a single hosted check, `Version bump`, that verifies the
version strings agree in three places, that a build-input change raised the
version, and that the committed `build/win/nutshell.exe` carries that
version in its resource. Releases publish that exact exe from a `v*` tag
after re-verifying the same three things. CodeQL runs on every pull
request and on every push to `main`. Dependabot sweeps weekly and groups
every action bump into one pull request.

This is a coherent, well-documented system. The gate script explains its
own reasoning, the workflows explain why each trigger exists, and CLAUDE.md
tells a contributor the exact command sequence. The `make test` run here
was clean on the first try with no setup beyond the stock toolchain.

## What is working well

- **Versioning is unfakeable.** Every binary on `main` is traceable to a
  version string, and the release job cannot ship a binary the gate did
  not check. The rule that after 1.0.99 comes 1.1.0 is enforced, not just
  documented.
- **Design decisions are written down before code.** 24 specs and 11
  plans in `docs/superpowers`, each dated, and the commit history
  references them. The notes document doubles as a checkpoint log.
- **The design system is a gate, not a guideline.** A hardcoded `RGB(` or
  a local scale macro fails the native suite. That has kept the redesign
  honest across two months of UI churn.
- **Recovery from a bad bet was quick.** The self-hosted desktop runner
  lasted six days (2026-09-09 to 2026-09-15). When it proved unreliable,
  it was removed cleanly rather than propped up, and the gate was
  redesigned around the committed exe.
- **Supply-chain hygiene.** Actions are pinned to commit hashes with the
  version in a trailing comment, and Dependabot rewrites both.

## Findings that need attention

Ranked by the cost of leaving them.

1. **The repository weighs 608 MiB packed, almost all of it exe history.**
   The 5.2 MB binary has been committed 104 times. The gate now depends on
   the committed exe, so untracking it is off the table, but the growth is
   linear in builds and every clone pays for it. Git LFS for
   `build/win/nutshell.exe` would keep the gate's contract (the file is
   still in the tree at every commit) while making clones cheap. This is
   a decision for the maintainer, not a change to make in passing.
2. **A code change cannot be merged from a Linux session.** The exe must
   be rebuilt on the Windows host for any change under `src/`, the
   Makefile or `nutshell.rc`. A cloud session like this one can build and
   run the native tests but cannot produce the artefact the gate requires.
   Docs, CI, tests-only and skill changes are fine from here; anything
   else has to finish on the dev box. This is not a defect, but it should
   be stated where contributors and agents will read it. This retrospective
   adds it to the project memory.
3. **CodeQL does not see the SSH code or any of `src/ui`.** The CodeQL job
   installs only `libssl-dev`, so the Makefile's libssh2 probe fails and
   the SSH and known-hosts files compile against the stub. `src/ui` is
   never compiled on Linux at all. The notes document already lists this;
   it has been open since 2026-09-10.
4. **The running notes document has gone stale in two places.** It lists
   the Ctrl+W tab-close bug and Dependabot PRs #14–#17 as open. Ctrl+W was
   fixed in v1.1.13 (PR #20) and both harness known-bug blocks are already
   unwrapped. PRs #14–#17 were closed unmerged and superseded by the
   grouped PR #22. Both items are corrected in this change.
5. **Branch and tag names collide.** `v1.0.76` and `v1.1.21` exist as both
   a branch and a tag at the same commit. `git fetch origin v1.1.21`
   silently resolves to the tag. The `v1.1.21` branch is fully contained
   in `main` and carries nothing the tag does not. `v1.0.76` is the
   pre-redesign main and is worth keeping, but as one ref, not two.
6. **Line endings are mixed.** 89 files are CRLF in the index and there is
   no `.gitattributes`. The notes document asks for a lone renormalising
   PR when nothing else is in flight; that condition holds now.

## Records corrected in this change

- Notes document: the Ctrl+W item and the Dependabot #14–#17 item moved
  from open to done, with the PR and commit that closed them, and the
  native test count updated from 1,804 to 1,960.
- Project memory (`.claude/memory/MEMORY.md`) created with the
  environment facts, gate rules and API quirks learned in this session.
- Three repository skills created under `.claude/skills/`: `steward`
  (driving a pull request through this repo's gate), `repo-status`
  (surveying branches, pull requests and checks correctly from a shallow
  clone) and `checkpoint` (the compact-and-record ritual the notes
  document describes).
- `.claude/CHANGELOG.md` created as the record of change for memory and
  skills, and `.gitignore` adjusted so those paths are tracked while local
  Claude state stays ignored.

## Recommendations

For the maintainer to accept or reject; none are applied here.

- Decide on Git LFS for the exe before the pack size doubles again.
- Merge PR #33; it is green and clean.
- Take the lone `.gitattributes` PR now, while the tree is quiet.
- Add `libssh2-1-dev` to the CodeQL job and fail the job if the Makefile
  probe still reports no libssh2, so the SSH code is scanned.
- Delete the `v1.1.21` branch (the tag remains) and consider the same for
  `v1.0.76` once its tag is confirmed to point at the same commit, which
  it does today.

## Numbers

| Measure | Value |
|---|---|
| Commits on `main` | 378 |
| First commit | 2026-02-24 |
| Pull requests | 33 (24 merged, 9 closed unmerged, 1 open) |
| Commits by month (Feb–Sep 2026) | 1, 149, 92, 1, 1, 0, 18, 116 |
| Source lines (`src/`) | 41,620 in 162 files |
| Native tests | 1,960 passing, 0 failing, 79 files |
| Integration cases | 30, manual only |
| Specs and plans | 24 and 11 |
| Committed exe | 5.25 MB, committed 104 times |
| Pack size | 608 MiB |
| CRLF-indexed files | 89 |
| Current version | 1.1.23 |
