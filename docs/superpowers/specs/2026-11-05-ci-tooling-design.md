# PR 2: CI and tooling — design

Date: 2026-11-05. Status: draft for the maintainer's review, revised after
critique rounds 1 and 2. Decisions taken by the orchestrating session from the CI
and tooling survey of 2026-11-04 (`main` at 1f85a3a, v1.2.52), each checked
against the code. One pull request, one version bump, one Windows build at
the end. Branch `chore/ci-tooling`.

## Why

The tooling that would have found most of what the external review found
is missing. `make test` is one gcc call (no `-j`, full recompiles); the
suite has never run under a sanitizer in CI; nothing runs `make lint`; no
parser of host or network bytes has been fuzzed by a coverage-guided
fuzzer (the hand-written `tests/test_json_fuzz.c` and
`tests/test_term_fuzz.c` exist, stay, and their inputs seed the `json` and
`term` corpora). Releases carry no
checksum, About does not name the OpenSSL or libssh2 inside, and cited
audit tags (`H-7`, `M2`, "audit C2") point at a never-committed document.

## Goals

1. `make -j test` builds the native suite in parallel and rebuilds only what
   changed, including after a header edit or a flag change.
2. `make test SAN=1` runs the suite under ASan, UBSan and LeakSanitizer; a
   hosted job does so on every pull request.
3. `make lint-gate` fails on any cppcheck error, warning, performance or
   portability finding; a hosted job runs it on every pull request.
4. Six untrusted-byte parsers have libFuzzer targets; every pull request
   runs each over its seeds, deterministically, and a weekly workflow
   (`fuzz-weekly.yml`) fuzzes each for 60 s.
5. Every GitHub release carries `nutshell.exe.sha256` and states the hash.
6. About and `-v` name the OpenSSL, libssh2 and zlib versions built in;
   README records them.
7. Every audit tag cited in the code resolves to `docs/audit-tags.md`.
8. `tests/test_dlog_gate.c` survives lines added above its one allow-listed
   call, and no longer lets a second violation through.

## Non-goals

- Making any new job a required check, or changing `Protect-Main.ps1`.
- Building the exe in CI, a reproducible build, or a `vcpkg.json` manifest.
- Running `wintest` or the integration harness in CI.
- Fixing every cppcheck `style` finding; `make lint` keeps reporting them.
- Renumbering or rewording audit tags in code comments.

## Anti-goals (would be a failure even if it technically works)

- **Changing what the two required checks enforce.** `checks.yml`,
  `check-version.sh` and the job names `Native tests` and `Version bump` are
  not touched. No `paths:` filter is added anywhere.
- **A sanitizer job that goes green by silencing.** No blanket suppression:
  `tests/lsan.supp` holds only one `leak:<function>` line per named,
  unfixable product leak; no `detect_leaks=0`; no `-fno-sanitize=` for a
  check that fired. Never merge a red `Sanitizers` check.
- **Committing, uploading or logging a crash input for an unfixed bug.** The
  repository and its Actions logs are public; a reproducer is an exploit.
- **A random-mutation fuzz run on a pull request**, or one that writes into
  the tree (corpus growth and crashes go under `build/`).
- **A test build that reuses objects compiled with other flags** (stub vs
  real libssh2, sanitized vs plain) and links a mismatched runner.
- **`.gitattributes` renormalisation** riding along.

## Constraints

- Every rule in `CLAUDE.md` applies: TDD for the new portable code, clean
  under `-Werror` in both compilers, design-system rules for the About row
  (no `RGB(`, `ns_font`, `ns_scale`, colours as the existing rows take
  them), `tests/test_ui_tokens.c` unchanged and passing.
- **Line endings preserved:** `Makefile` and `tests/test_vt_sequences.c`
  stay CRLF, everything else touched or new is LF. Check
  `git diff --stat --ignore-cr-at-eol` before review.
- CodeQL traces `make test`: one compiler run per source, nothing cached.
- Workflows: actions SHA-pinned as in `checks.yml` (checkout `3d3c42e5…`
  v7.0.1), `permissions: contents: read` unless a job needs more. **Every
  step that pipes `make` into `tee` declares `shell: bash` and starts with
  `set -o pipefail`**: the default `bash -e {0}` lets `tee` hide a failure.
- **Version:** one above the higher of `main`'s and the branch's last
  build, worked out at build time; never a number fixed here.

## What has been decided

### 1. Per-object native test build

- `make test` compiles each file of `$(TEST_SRCS)` to `build/test/<path>.o`
  (path kept, so `src/core/x.c` and `tests/x.c` never collide) with a
  pattern rule `build/test/%.o: %.c`, links `$(TEST_TARGET)`, then runs it.
  `test` stays phony and depends on the runner, so `-j` parallelises.
  Headers: `-MMD -MP` on every compile, `-include $(TEST_OBJS:.o=.d)`.
- **Two stamps.** `.d` files track headers, not flags, and `TEST_CFLAGS`
  changes with the host (`-DNO_SSH_LIBS -Itests/stubs` only without
  libssh2). `build/test/flags.stamp` (in the object directory) holds
  `TEST_CC`, `TEST_CFLAGS`, `TEST_LDFLAGS` and `SAN`; every object depends
  on it. `build/test/srcs.stamp` holds the sorted `TEST_SRCS` list; only the
  runner link depends on it (and on the object list). Each is rewritten
  only when its text differs, so a flag change rebuilds everything and an
  added or dropped source only compiles what is new and relinks.
- `-MMD` leaves system headers out of the `.d` files: a changed system
  header (an OpenSSL upgrade, say) needs `make clean`.
- Detection, `NON_TEST_SRCS`, `WIN_ONLY_TESTS` and `-D_POSIX_C_SOURCE`
  behave as today; the cross-build's in-place `%.o: %.c` rule is untouched.
- `make clean` also removes `build/test/`, `build/test-san/`, `build/fuzz/`
  and `build/test_runner_san`; `.gitignore` gains all four.
- `wildcard tests/*.c` does not recurse, so `tests/fuzz/*.c` never joins
  `TEST_IMPL_SRCS`; the builder confirms that
  `make -n --eval 'x:;@echo $(TEST_IMPL_SRCS)' x | tr ' ' '\n' | grep -c '^tests/fuzz/'`
  prints 0 (`tests/test_json_fuzz.c` and `tests/test_term_fuzz.c` are native
  tests and stay in the list).

### 2. Sanitizer switch and the hosted job

- `SAN=1` sets `SAN_FLAGS = -fsanitize=address,undefined
  -fno-omit-frame-pointer -fsanitize-recover=all`, appended to the compile
  and link lines, never by overriding `TEST_CFLAGS` (that would drop
  `-D_POSIX_C_SOURCE`). Objects go to `build/test-san/`, the runner to
  `build/test_runner_san`. On a Windows host `SAN=1` stops with a one-line
  error (MinGW gcc has no libasan).
- **The blocker, fixed first:** `fk_put_file()` (`tests/fake_dlog.c`) pads
  one `realloc` per byte (about 30 MB across `test_dlog.c` and
  `test_dlog_review.c`); it becomes one `realloc`, a `memset` and the NUL.
- **Job `Sanitizers`** runs `make test SAN=1` with `CI: true`,
  `ASAN_OPTIONS=halt_on_error=0:detect_leaks=1`,
  `LSAN_OPTIONS=suppressions=tests/lsan.supp` and
  `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`, tees stderr to a file
  (`shell: bash`, `set -o pipefail`), and fails if `make` fails or the file
  contains `AddressSanitizer`, `LeakSanitizer` or `runtime error:` (matches
  printed).
- **If the first run is red** (expected in the libssh2-gated files, which
  first meet ASan in this PR's run): every non-leak fault (test or product
  code) is fixed in this PR, or the PR is split so the fix lands first; a
  leak in test code is fixed; a leak in product code that cannot be fixed
  here gets one line in the new `tests/lsan.supp` (`leak:<function>`, with
  a comment naming it and a line on the notes' Open list), passed with
  `LSAN_OPTIONS=suppressions=tests/lsan.supp`. `detect_leaks=0` is not
  used. A stack overflow under ASan moves the struct off the stack, never
  a raised `ulimit`. A red `Sanitizers` check is never merged.

### 3. cppcheck gate

- New target `lint-gate`: `cppcheck --enable=warning,performance,portability
  --error-exitcode=1 --inline-suppr --std=c11 src/`. No `style`, so a style
  finding cannot fail it; `make lint` keeps `style` for humans.
- Measured on `automaton` at 1f85a3a (`-j$(nproc) -q`): exit 0, 46 s.
- **Job `Lint`** runs on `ubuntu-24.04`, not `ubuntu-latest` (which moves to
  a new LTS and cppcheck without notice), installs cppcheck from apt and
  fails unless `cppcheck --version` starts with `Cppcheck 2.13`.

### 4. Fuzz targets

- Recipes run under `/bin/sh` (dash on Ubuntu): no `set -o pipefail` or
  other bash-only syntax in the Makefile.
- `tests/fuzz/fuzz_<name>.c`, one `LLVMFuzzerTestOneInput` each; input
  copied into a NUL-terminated heap buffer where a C string is needed,
  everything allocated freed.
  - `json` — `json_parse()`, `json_free()`. `term` — `term_init(24, 80,
    100)`, `term_process(data, size)`, `term_free()`, per input.
  - `exec_markers` — `ai_extract_commands_full()` with 16-entry arrays.
  - `cmd_classify` — `cmd_classify_ex()`; platform from the first byte
    modulo `CMD_PLATFORM_UNKNOWN + 1` (UNKNOWN is the last enumerator), the
    rest is the command. It also checks three invariants and calls
    `abort()` on a violation: `cmd_classify_mask(c, p) &
    CMD_MASK_OF(cmd_classify(c, p))` is non-zero; `cmd_classify_session(c,
    p, 1) >= cmd_classify(c, p)`; and `cmd_classify_session(c, p, 1) >=
    cmd_classify(c, CMD_PLATFORM_UNKNOWN)`.
  - `html` — `web_fetch_html_to_text()` on the raw bytes, freeing `out`.
  - `url` — `net_url_parse()`, `net_url_display()`,
    `net_url_cleartext_remote()`; `net_url_resolve()` with the input as the
    base (fixed location) and as the location (fixed base).
- `make fuzz` builds `build/fuzz/<name>` with `FUZZ_CC`, which defaults to
  the first of `clang`, `clang-18`, `clang-17`, `clang-16` found on PATH
  (`$(firstword $(foreach c,clang clang-18 clang-17 clang-16,$(shell
  command -v $(c))))`) and can be overridden, with
  `-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined`
  (UB is a crash here), `-Wno-ignored-attributes
  -Wno-unknown-warning-option`, and the include paths, `-D_TEST`,
  `-D_POSIX_C_SOURCE` and stub handling of `make test`.
- **`make fuzz-run` is deterministic** (the PR job): `build/fuzz/<name>
  -runs=0 -timeout=10 -artifact_prefix=build/fuzz/crashes/<name>-
  tests/fuzz/corpus/<name>/` runs each committed seed once, no mutation;
  with `-runs=0` nothing is written into the corpus directory, so there is
  no scratch directory and the seeds are never modified.
- **`make fuzz-long`** (the weekly job; `FUZZ_TIME ?= 60`): the same
  with `-max_total_time=$(FUZZ_TIME)` for `-runs=0` and
  `build/fuzz/corpus/<name>/` as the first directory (growth stays under
  `build/`).
- Both first `mkdir -p` the corpus (for `fuzz-long`), log and artifact
  directories.
- **Output stays out of the log.** Each binary runs with stdout and stderr
  redirected to `build/fuzz/log/<name>.log`; the recipe captures its exit
  status, then filters the file. Printed: the `INFO: Seed:` line, the
  first line of each `==` error header, the stack frames
  (`#N 0x... in <fn> <file>:<line>`) and `SUMMARY:`. Never printed:
  `Base64:`, hex dumps of the input, or `MS:` mutation lines. A crash stops
  the run, exits non-zero and names the target, never the input bytes.
- On a Windows (MSYS2) host the three fuzz targets print a one-line skip
  notice and exit 0, as `wintest` does without wine. Seeds: a few small
  text files per target in `tests/fuzz/corpus/<name>/`; the inputs of the
  hand-written `tests/test_json_fuzz.c` and `tests/test_term_fuzz.c` seed
  the `json` and `term` corpora.
- **Crashes found while building:** fixed here if small, with a native
  regression test once the fix is in; otherwise described in the PR body by
  entry point and fault class only, the input kept in the scratchpad.

### 5. The workflows `quality.yml` and `fuzz-weekly.yml`

- New `.github/workflows/quality.yml`, `on: pull_request: branches:
  [main]` only. Exactly three jobs, all on `ubuntu-24.04`,
  `timeout-minutes: 15`, none a required check: `Sanitizers`, `Lint`,
  `Fuzz` (`make fuzz && make fuzz-run`). Every pull request therefore shows
  seven checks.
- New `.github/workflows/fuzz-weekly.yml`, `on: schedule: cron '0 3 * * 1'`
  (Monday 03:00 UTC) and `workflow_dispatch`; one job `Fuzz (scheduled)`
  on `ubuntu-24.04`, `timeout-minutes: 15` (`make fuzz && make fuzz-long`,
  6 × 60 s). Never on a pull request.
- **Skip pattern:** each PR job decides in a step whether the PR touches
  `src/`, `tests/`, `build-support/`, the Makefile, `nutshell.rc`,
  `README.md` or `quality.yml` (as `Native tests` does); if not, its work
  steps skip and it reports green, so a docs-only PR shows all seven checks.
- Same apt handling as `checks.yml` (drop stale Google/Microsoft sources by
  host, `apt-get update`, install `libssl-dev libssh2-1-dev zlib1g-dev`),
  same libssh2 probe for `Sanitizers` (which also sets `CI: true`), same
  pinned checkout, concurrency
  group `${{ github.workflow }}-${{ github.ref }}`, `cancel-in-progress`.
- `Fuzz` and `Fuzz (scheduled)` use the runner's `clang` (if
  `-fsanitize=fuzzer` cannot link, they install the matching
  `libclang-rt-<N>-dev`). **No job uploads anything.**
- `checks.yml` is not edited. `codeql.yml`'s comment claiming the job shell
  is `bash -eo pipefail` is wrong (true only with `shell: bash`) and is
  corrected here; its commands are unchanged.
- **Rules.** On a pull request every check, the three quality jobs
  included, is deterministic and expected green before a merge; "never
  merge a red check" holds for all of them. A red scheduled fuzz run is not
  a PR check: whoever sees it opens an issue by hand (target, fault class).
- **Process texts, same PR:** `CLAUDE.md`'s merge-gate section gains one
  sentence: `Sanitizers`, `Lint` and `Fuzz` are deterministic, expected
  green before a merge, and not in the ruleset; the weekly fuzz run
  (`fuzz-weekly.yml`) is not a PR check. `feature-workflow` step 7's "Expect `Version bump`, `Native
  tests`, `analyze` and `CodeQL`; all four must be `success`" lists the
  seven (`Version bump`, `Native tests`, `analyze`, `CodeQL`, `Sanitizers`,
  `Lint`, `Fuzz`), all `success`, the first two required, a docs-only PR
  showing all seven (quality jobs green, work skipped); its Dependabot "all
  three checks" line and `MEMORY.md`'s "three checks on an actions-only
  change" line name the new set. Each `.claude/` edit gets a dated
  `.claude/CHANGELOG.md` entry in the same commit.

### 6. Release checksum

- A new step runs `cd build/win && sha256sum nutshell.exe >
  nutshell.exe.sha256` (so `sha256sum -c` works offline in a download
  folder; kept beside GitHub's own asset digest because it is cheap) and
  puts the bare hash in a step output.
- `gh release create … build/win/nutshell.exe build/win/nutshell.exe.sha256
  --title … --generate-notes --notes "SHA-256: <hash>"` (`gh` prepends
  `--notes`). The `workflow_dispatch` artifact uploads both files.

### 7. Dependency versions

- New portable `src/core/build_info.c/.h`:
  `size_t build_info_deps(char *out, size_t cap)` writes
  `OpenSSL <v>, libssh2 <v>, zlib <v>`, always NUL-terminated, returning the
  length it needed (the libssh2 part is formatted at run time).
  - OpenSSL from `OPENSSL_VERSION_STR` (3.x everywhere).
  - libssh2 from `LIBSSH2_VERSION_MAJOR`/`_MINOR`/`_PATCH`, **not**
    `LIBSSH2_VERSION`: vcpkg's says `"1.11.1_DEV"`.
  - zlib from `ZLIB_VERSION` under `#if defined(__has_include) &&
    __has_include(<zlib.h>)`, else `zlib ?`; no new hard requirement.
  - **ASCII only**: About draws with `DrawTextA` and `-v` goes through
    `cli_output`; a UTF-8 `·` would show as `Â·`.
- `tests/stubs/libssh2.h` gains `LIBSSH2_VERSION_MAJOR`, `_MINOR`, `_PATCH`
  (0, 0, 0) with a comment. New `tests/test_build_info.c` (in
  `tests/runner.c`) asserts by a small pattern match that the whole output
  is `OpenSSL <v>, libssh2 <v>, zlib <v|?>`, each `<v>` matching
  `[0-9]+(\.[0-9A-Za-z-]+)*`, and nothing else, that the OpenSSL part equals `OPENSSL_VERSION_STR`, and
  truncation at a small `cap`. Format, not numbers.
- `about_layout_compute()` gains a `deps_h` row height and an `AboutRect
  deps`; `tests/test_about_layout.c` covers the fourth row in its
  exact-position, 2x-at-192-DPI and clamping tests (no width assertion: a
  portable test cannot measure glyphs). `window.c` draws it like the
  copyright row (caption font, `text_dim`) with `DT_END_ELLIPSIS`, so a
  longer string truncates instead of clipping; a Windows manual check at
  125 % and 200 % confirms it.
- `-v` (`src/main.c`, `CLI_VERSION`) prints the dependency line after
  `Nutshell <version>`; a Windows manual check (`main.c` is not native).
- README "Linux host (cross-compile)": the shipped vcpkg versions (libssh2
  1.11.1, OpenSSL 3.6.1, zlib 1.3.1 now, "see About or `-v`"); optional
  `libssh2-1-dev` and `zlib1g-dev`. Targets table: `make -j test`,
  `make test SAN=1`, `make lint-gate`, `make fuzz`/`fuzz-run`/`fuzz-long`.

### 8. Audit-tag mapping

- New `docs/audit-tags.md` with two tables:
  - the hyphenated series (21 tags: C-1, H-2, H-3, H-4, H-6, H-7, I-1–I-3,
    L-1–L-5, M-1–M-5, M-7, M-8): tag, one-line meaning, `file:function`
    locations, no line numbers;
  - the unhyphenated series from the 2026-09-24 review and the classifier
    holes audit (M1–M6, H1–H4, H6, H9, L1–L4, C1, C2, and any more the
    builder finds): tag, origin, meaning, locations. `C0`/`C1` usually mean
    control-character ranges; only references citing an audit go in.
- One sentence: the 2026-09-09 security-audit spec the tests cite was never
  committed; b5d3887 (2026-09-09, "fix(security): reject control characters
  in AI commands … clamp the VT parser", v1.1.10) added the regressions.
- The "C1" comment in `tests/test_ai_prompt.c` (above
  `test_ai_extract_commands_ex_embedded_newline_rejected`) and the "Security
  audit regressions (2026-09-09)" heading in `tests/test_vt_sequences.c`
  cite `docs/audit-tags.md`; other code comments are left alone.

### 9. `test_dlog_gate.c` allow-list by content

- The `k_allow` entry `"src/ui/window.c:1244"` becomes a path plus a
  substring of the call, `"profile.password_saved"` (unique in `window.c`).
  `gate_scan()` reports every violating line (an array or a callback), not
  only the first; the allow check looks at the text of each reported line;
  the stale-entry test checks the substring still occurs on a violating
  line.
- **A gap found while checking:** an allowed file excused all its
  violations; one allow entry now excuses exactly one. `scan_dir()`'s
  allow/count logic moves into a function over a path and its text, called
  per file; the planted tests call it: a two-violation case on an
  in-memory string expects one unexcused violation.

### 10. Version, build and commits

- One bump at the end, `make clean && make release` on `automaton`, exe
  committed; new files staged by path, a `Models:` line and the trailer.

## Left to the builder

- The object layout beyond `build/test/<path>.o`; how the stamps are
  written.
- How fuzz targets get their code. Suggested: `$(APP_SRCS)` compiled once
  with `-fsanitize=fuzzer-no-link,address,undefined` into
  `build/fuzz/libnutshell.a`, each target linked against it, so no list
  goes stale; per-target lists kept in one place are fine.
- Corpus contents; whether `lint-gate` uses `-j$(nproc)`; the About row's
  position and separator; the exact `-v` layout; commit order.

## Verification before the gate

- `make clean && make -j test`: 0 failed, count noted; a second `make test`
  compiles nothing; `touch src/config/json_parser.h && make -j test`
  recompiles only its includers (there is no `src/core/json_parser.h`, and
  one would shadow the real header, `-Isrc/core` preceding `-Isrc/config`).
- `make test SAN=1`: no sanitizer report beyond the named `tests/lsan.supp`
  entries.
  It uses the stub (no libssh2-dev on `automaton`), so `ssh_session.c`,
  `knownhosts.c`, `test_ssh.c` and `test_key_auth.c` first meet ASan in the
  PR's CI run; the "first run red" plan in 2 expects findings there.
  `make lint-gate`: exit 0.
- `make fuzz && make fuzz-run`: every seed runs, no crash, only the filtered
  lines printed; `make fuzz-long` once (no crash, or handled as in 4);
  `git status` shows nothing new under `tests/fuzz/corpus/` or the root.
- `xvfb-run -a make wintest`: 45 run, 0 failed.
- On Windows by hand: About's fourth row on the four themes at 100 %,
  125 % and 200 % and after a `WM_DPICHANGED` monitor move, no clipping;
  About shows a real zlib version, not `?`; `-v` prints both lines.
- `check-version.sh origin/main` PASS; `git diff --stat --ignore-cr-at-eol`
  matches `git diff --stat`.
- Workflows: no `act`; each YAML file is loaded with `yaml.safe_load` and
  read. `quality.yml` first runs on the PR; `workflow_dispatch` runs on the
  branch exercise `fuzz-weekly.yml` and `release.yml`'s checksum step and
  artifact; `gh release create` first runs at the next tag.
- **Not verified until the first CI run shows it:** that `ubuntu-24.04`'s
  clang links `-fsanitize=fuzzer` without an extra package; the ASan suite's
  duration with the real libssh2 against the 15-minute timeout; whether
  LSan reports leaks inside libssh2 or OpenSSL (the leak rule in 2 applies:
  one named `tests/lsan.supp` line each, never a blanket suppression); About's height after `WM_DPICHANGED`.

## Critique (2026-11-05, round 1)

1. Random fuzzing on PRs made a non-deterministic check — ACCEPTED: PR `Fuzz` runs seeds only, 60 s runs weekly; all PR checks expected green; process texts updated (§4, §5).
2. Fuzzer output in public logs could disclose crash inputs — ACCEPTED: logged to `build/fuzz/log/`, filtered lines printed, nothing uploaded (§4, §5).
3. Header-edit check named a non-existent `src/core/json_parser.h` — ACCEPTED: `src/config/json_parser.h` (Verification).
4. `tee` without pipefail hides a failing `make`; codeql.yml's comment is wrong — ACCEPTED: `shell: bash` + `set -o pipefail`, comment corrected (Constraints, §5).
5. `make -pn test | grep fuzz` proves nothing — ACCEPTED: `--eval` echo of `TEST_IMPL_SRCS`, count 0 (§1).
6. Audit-tags doc cited the wrong commit — ACCEPTED: b5d3887 (§8).
7. Flags stamp content and runner relink unspecified — ACCEPTED: fields listed, runner depends on stamp and object list (§1).
8. `.gitignore` missed the sanitized runner — ACCEPTED: `build/test_runner_san` added (§1).
9. zlib became a hard requirement; brittle version test — ACCEPTED: `__has_include` with `zlib ?`, MAJOR/MINOR/PATCH, pattern-match test (§7).
10. Local SAN run never reaches the libssh2-gated files — ACCEPTED: stated; first CI run expected to find issues (§2, Verification).
11. Loose shape test, clipping About row, `-v` not natively testable — ACCEPTED: exact pattern, `DT_END_ELLIPSIS`, width assertion, Windows manual check (§7).
12. Two-violation case needed files — ACCEPTED: allow/count logic as a function over text; one entry excuses one violation (§9).
13. Fuzz on MSYS2, `FUZZ_CC` default, checksum file vs GitHub digest — ACCEPTED: skip notice exit 0, `FUZZ_CC ?= clang`, `.sha256` kept (§4, §6).

## Critique (2026-11-05, round 2)

1. Sanitizer faults: unfixable product leaks go in `tests/lsan.supp` (`leak:<function>`) via `LSAN_OPTIONS=suppressions=tests/lsan.supp`; non-leak faults fixed here or the PR split; `detect_leaks=0` withdrawn; never merge a red `Sanitizers` — accepted (Anti-goals, §2).
2. The `TEST_IMPL_SRCS` check is `... | tr ' ' '\n' | grep -c '^tests/fuzz/'` (0); the hand-written `test_json_fuzz.c`/`test_term_fuzz.c` stay and seed the corpora — accepted (Why, §1, §4).
3. `gate_scan()` reports every violating line; one allow entry excuses exactly one; the planted tests call it — accepted (§9).
4. `Sanitizers`, `Lint` and `Fuzz` all run on `ubuntu-24.04` — accepted (§5).
5. The scheduled fuzz moves to its own `fuzz-weekly.yml`; `quality.yml` has exactly three jobs and every PR shows seven checks — accepted (Goals, §5).
6. The fuzz log filter prints `INFO: Seed:`, the `==` header, stack frames and `SUMMARY:`; never `Base64:`, hex dumps or `MS:` lines — accepted (§4).
7. Makefile recipes run under dash: no `pipefail`; `fuzz-run` redirects to the log, captures the status, then filters; `mkdir -p` first — accepted (§4).
8. `FUZZ_CC` defaults to the first of `clang`, `clang-18`, `clang-17`, `clang-16` on PATH, overridable — accepted (§4).
9. Two stamps: `flags.stamp` for the objects, `srcs.stamp` for the runner link only — accepted (§1).
10. No About width assertion (`DT_END_ELLIPSIS` plus a manual check at 125 % and 200 %); versions match `[0-9]+(\.[0-9A-Za-z-]+)*`, or `?` for zlib — accepted (§7, Verification).
11. The `cmd_classify` target asserts the mask and session invariants and aborts on a violation — accepted (§4).
12. No `seedrun/` directory; `-runs=0` over `tests/fuzz/corpus/<name>/` only — accepted (§4).
13. `Sanitizers` sets `CI: true`; fuzz CFLAGS add `-Wno-ignored-attributes -Wno-unknown-warning-option`; `-MMD` omits system headers; the manual checks include a real zlib version in About — accepted (§1, §4, §5, Verification).
