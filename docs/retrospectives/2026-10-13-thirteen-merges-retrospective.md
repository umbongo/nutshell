# Retrospective, 2026-10-13: thirteen merges, a security sweep, a dispatch saga

**Status**: DRAFT until the maintainer reviews.
**Covers**: merges into `main` after the 2026-09-24 marker `21e4d5d` (PR #45) up to
`c593796` (PR #59): #47-#59, 2026-09-23 to 2026-09-25 (13 merges).
**Method**: unshallowed clone, `git log --merges --first-parent`, the PR list with
`merged_at`, `make test` on Linux, a read of both workflows, both scripts, the
process sections of CLAUDE.md and the notes document's Open list. Run unattended by the
retrospective Routine; no Opus critique was run because no process rule or gate
enforcement was changed (see section 4).

## 1. Numbers

| Measure | 2026-09-24 | 2026-10-13 |
|---|---|---|
| Version on `main` | 1.2.5 | 1.2.27 |
| Merges in the period | 11 | 13 (#47-#59) |
| Native tests (Linux `make test`) | 1,960 (09-21 baseline) | 2,485, 0 failed |
| `src/ui/window.c` / `ai_chat.c` lines | 4,224 / n/a | 5,124 / 5,455 |
| Integration case files | 36 cases | 9 files in `tests/integration/cases` (cases per file not recounted) |
| Open pull requests | 1 | 2 (#60 fix v1.2.21, #61 Dependabot actions bump, since 09-30) |

Of the 13 merges, 7 changed `src/` (#50, #54-#59), 5 were docs/CI/tests (#47, #48,
#49, #51, #53) and 1 was a Dependabot bump (#52). Several merges were fast (#56 four minutes
after opening, #53 seven) show the gate is now quick when it is green.

## 2. Findings, ranked by the cost of not acting

**F1. Two pull requests sit open and unattended.** #60 (AI line-clear prefix over SSH,
opened 09-24, v1.2.21) has been open 19 days and its version (1.2.21) is now below
`main`'s 1.2.27, so it cannot pass `Version bump` without a rebase and rebuild on the
Windows box. #61 (Dependabot actions bump) has waited 13 days; its pins are the ones
in `checks.yml`, `codeql.yml` and `release.yml`. *Recommended*: the maintainer decides
whether #60 is superseded by #59, and merges or closes #61 after its checks are green.

**F2. Release-number churn from parallel branches.** Versions in the period are not
monotonic with merge order (PR titles say v1.2.8, 1.2.11, 1.2.15, 1.2.17, 1.2.19,
1.2.22, 1.2.27 for the pull requests; several branches carry intermediate
`release:` commits). The gate only needs "greater than main", so this is cosmetic,
but #60 shows the cost: a parallel branch is left stale. *Recommended*: when two
`src/` branches are in flight, the second one rebases and bumps after the first merges.

**F3. File size keeps growing.** `window.c` went from 4,224 to 5,124 lines and
`ai_chat.c` is 5,455. The Open list already carries "code-health critique"; the
numbers argue for scheduling it. *Recommended*, not applied.

**F4. The F1 gate gap from the last retrospective is closed.** `Native tests` has
been required since #47 (09-23); every merge since carries it. No merge in this cycle
went in with a red check that this retrospective could find from the PR list alone.
CodeQL now installs libssh2, so the SSH files are scanned.

**F5. The notes document and memory were behind.** Memory still said "main at v1.2.5",
"PR #42 open", the manual checks pending and the gate compiling nothing. All stale;
fixed here. The cadence marker also had a quirk: PR #46 (the previous retrospective)
merged after its own marker, so the count for this cycle is 13 rather than 12.

## 3. Workflow review

- `checks.yml`: accurate against what ran (two jobs, both required, concurrency
  cancels superseded runs). Pins are full SHAs with version comments; #61 bumps them.
- `codeql.yml`: installs libssh2, concurrency keyed per the #49 change. Accurate.
- `release.yml`, `check-version.sh`, `exe-version.sh`: unchanged this cycle, no
  discrepancy found.
- CLAUDE.md process sections: consistent with the gate as it runs, including the
  cloud-session rule (stop and warn, never merge). No edit.

## 4. Changes made (memory and record only)

`.claude/memory/MEMORY.md` (marker, test baseline, open items, last-updated) and
`.claude/CHANGELOG.md`. No skill, workflow or CLAUDE.md change, so no critique round:
the checkpoint skill requires one only for those.

## 5. Recommendations, not applied

1. Decide #60 and #61 (F1).
2. Rebase-and-bump rule for parallel `src/` branches (F2), if the maintainer wants it
   in `feature-workflow`.
3. Schedule the `window.c`/`ai_chat.c` split (F3).
4. Run `make release` and the integration gate tier on the Windows box for 1.2.27;
   this session could do neither.
