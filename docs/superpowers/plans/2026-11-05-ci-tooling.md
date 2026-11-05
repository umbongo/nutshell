# CI and Tooling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Nutshell an incremental parallel test build, a sanitizer run, a cppcheck gate and libFuzzer targets, three new pull-request checks plus a weekly fuzz workflow, a release checksum, dependency versions in About and `-v`, an audit-tag index and a stricter `test_dlog_gate.c`, in one pull request with one version bump.

**Architecture:** The Makefile's native `test` becomes a per-object build under `build/test/` (or `build/test-san/` with `SAN=1`) with `-MMD -MP` header tracking and two stamps (flags for the objects, the source list for the link); fuzz targets link a separately compiled `build/fuzz/libnutshell.a`. `quality.yml` runs `Sanitizers`, `Lint` and `Fuzz` on pull requests (deterministic, not required); `fuzz-weekly.yml` runs random fuzzing on a schedule. The one piece of new product logic, `build_info_deps()`, is a portable function in `src/core/` with native tests; `window.c` and `main.c` only draw and print it.

**Tech Stack:** C11, GNU Make 4.3 (recipes run under `/bin/sh`, dash on Ubuntu), gcc 13 (native), MinGW-w64 (cross), clang/libFuzzer (`/usr/lib/llvm-16/bin/clang` on `automaton`, not on PATH), cppcheck 2.13, GitHub Actions on `ubuntu-24.04`.

**Spec:** `docs/superpowers/specs/2026-11-05-ci-tooling-design.md`. Read it with this plan; where a step says "spec §N" the wording and values there are binding. Its two "Critique" sections are already folded into the body.

## Global Constraints

- Worktree `~/nutshell-wt/ci-tooling`, branch `chore/ci-tooling`, cut from `main` at 1f85a3a (v1.2.52) or later. All rules in `CLAUDE.md` apply; `hostname` must print `automaton` for Tasks 3, 5, 6 and 11.
- **Do not touch** `.github/workflows/checks.yml`, `.github/scripts/check-version.sh`, `tests/integration/Protect-Main.ps1`, `tests/test_ui_tokens.c`, `tests/test_json_fuzz.c`, `tests/test_term_fuzz.c` or `.gitattributes`. No `paths:`/`paths-ignore:` filter in any workflow.
- **Baseline** (Task 1 Step 0, before any edit): `make test 2>&1 | tee build/base.log`; note `Tests Run`, `Failed: 0` and `grep -c 'warning:' build/base.log` (W). No task may raise W or lower the test count except by adding tests.
- **Test command.** Task 1 uses `make test`; from Task 2 on, `make -j"$(nproc)" test` (written `make -j test` below).
- **Makefile recipes are POSIX sh** (dash): no `set -o pipefail`, `[[ ]]`, arrays or `$SECONDS`. A `#` inside a Makefile variable value is written `\#`.
- **Cross-compile check** (Tasks 2, 5, and any task that edits a file compiled into the exe; never `make clean` before Task 11): `find src -name '*.o' -delete && make -j"$(nproc)" build/win/nutshell.exe && git restore build/win/nutshell.exe`. Must build clean under `-Werror`.
- **No version bump before Task 11.** Task 11 sets the version to one patch above the greater of `origin/main`'s `APP_VERSION` and the branch's, worked out then (after `.99` comes `X.(Y+1).0`). Never write a literal version anywhere else.
- **Line endings.** `Makefile` and `tests/test_vt_sequences.c` are CRLF and stay CRLF; every other touched or new file is LF. Before every commit: `git diff --cached --stat` and `git diff --cached --stat --ignore-cr-at-eol` show the same counts per file, and `awk '!/\r$/' Makefile | wc -l` prints `0`.
- **Design system** (`src/ui`): no `RGB(`, colours from `ns_tokens()`, fonts from `ns_font()`, sizes via `ns_scale()` and `ns_type.h`. `tests/test_ui_tokens.c` passes unchanged.
- **Sanitizer findings** (spec §2): every non-leak fault, test or product, is fixed in this PR or the PR is split so the fix lands first; a test-code leak is fixed; only an unfixable product leak gets one `leak:<function>` line in `tests/lsan.supp` with a comment naming it, plus a line on the notes' Open list. Never `detect_leaks=0`, `-fno-sanitize=`, or a pattern broader than one function.
- **Crash inputs.** A fuzz or sanitizer reproducer for an unfixed bug is never committed, pasted into a commit message, PR body, issue or log. Inputs stay under `build/fuzz/` (gitignored) or the session scratchpad; reports name entry point and fault class only.
- **Workflows.** Actions pinned by SHA as in `checks.yml` (checkout `3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1`); `permissions: contents: read`; every `run:` step that contains a pipe (`|`, not `||`) declares `shell: bash` and its first command line is `set -o pipefail`.
- **Commits.** One commit per task. Stage new files by path and read every `??` line of `git status --short` first; never `git add -u` alone. Every commit message ends with:

```
Models: claude-opus-5-5 (orchestration, review); claude-sonnet-5-5 (implementation, task review)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg
```

## Review Focus

1. **A deleted or renamed header with stale `.d` files.** After a header is removed and its includer edited, `make -j test` must rebuild, not stop with "No rule to make target 'x.h'". Pinned by Task 2 Step 4 (the `zz_probe` sequence; `-MP` is what makes it pass).
2. **A sanitizer run green locally (libssh2 stub) and red in CI (real libssh2).** `ssh_session.c`, `ssh_channel.c`, `ssh_pty.c`, `ssh_io.c`, `knownhosts.c`, `test_ssh.c`, `session.c`, `test_knownhosts.c`, `test_key_auth.c` never meet ASan on `automaton`. Pinned by Task 3 Step 6 (the excluded list is recorded for the PR body), Task 7's libssh2 probe in `Sanitizers` (a missing libssh2 fails the job instead of falling back to the stub) and the Handoff's first-run rule.
3. **A fuzz seed that trips the 10 s per-input timeout on a slower CI runner under ASan.** Pinned by Task 6 Step 6: no seed over 4 KiB, and each target's whole seed run takes at most 2 s on `automaton` (5x headroom).
4. **A workflow step whose `make … | tee` hides a failing `make`.** Pinned by Task 7 Step 3's checker, which fails on any piped step without `shell: bash` and a leading `set -o pipefail`, and is itself proven on planted bad copies.
5. **The About dependency row clipping at 125 % DPI** (or with a longer future version string). A portable test cannot measure glyphs, so it is pinned by Task 5's full-width assertion for `deps` at 120 DPI, `DT_END_ELLIPSIS` on the row (Task 5 Step 5), and the Handoff's manual check at 125 % and 200 %.

---

### Task 1: `fake_dlog` padding and the content-keyed dlog gate

**Files:**
- Modify: `tests/fake_dlog.c` (`fk_put_file`, lines 66-76), `tests/test_dlog.c`, `tests/test_dlog_gate.c`, `tests/runner.c` (declarations ~274-282, calls ~3952-3959)

**Interfaces:**
- Produces (all `static` in `test_dlog_gate.c`):
  - `typedef struct { int line; char word[64]; } GateHit;` and `#define GATE_MAX_HITS 64`
  - `static int gate_scan(const char *src, GateHit *hits, int max_hits);` returns the total violation count and records every violation's line and word, up to `max_hits` (NULL/0 records none). `SCAN(text)` becomes `gate_scan((text), NULL, 0)`.
  - `typedef struct { const char *path; const char *needle; } GateAllow;` and `k_allow[] = { { "src/ui/window.c", "profile.password_saved" }, { NULL, NULL } }` (keep the reason comment)
  - `static int gate_unexcused(const char *path, const char *text, int report);` violations in `text` not excused; each `k_allow` entry whose `path` equals `path` excuses at most one recorded hit whose source line (in `text`) contains `needle`. Hits beyond `GATE_MAX_HITS` are never excused (fail closed). `report` prints `  path:line: a log call mentions "word"` per unexcused hit.

- [ ] **Step 0: Record the baseline** (Global Constraints) and `time ./build/test_runner >/dev/null` (T0).

- [ ] **Step 1: Write the failing tests.**

```c
/* tests/test_dlog.c */
int test_dlog_fake_put_file_pads_once(void);
/* fk_reset(); fk_put_file(FK_LOG_DIR "\\pad.log", "ab", 10, 5):
     fk_find(..)->len == 10, fk_text(..) == "abxxxxxxxx", ->mtime == 5
   ("abc", pad_to 1) -> len 3, "abc";  (NULL, pad_to 3) -> "xxx"
   ("old\n", pad_to 2 MiB) -> len == 2097152, data[len] == '\0', data[len-1] == 'x'
   fk_free() */
/* tests/test_dlog_gate.c */
int test_dlog_gate_reports_every_violating_line(void);
/* "DLOG(A, B, 0, \"e\", \"password=%s\", p);\nint x;\nDLOG(A, B, 0, \"e\", \"x=%s\", api_key);\n"
   GateHit h[4]: gate_scan(src, h, 4) == 2, h[0].line == 1, h[1].line == 3,
   h[1].word == "api_key";  gate_scan(src, h, 1) == 2 (counts beyond max_hits) */
int test_dlog_gate_allow_entry_excuses_exactly_one_violation(void);
/* two = "void f(void) {\n"
         "  DLOG(A, B, 0, \"profile.password_saved\", \"ok=%d\", ok);\n"
         "  DLOG(A, B, 0, \"e\", \"password=%s\", pw);\n}\n"
   gate_unexcused("src/ui/window.c", two, 0) == 1
   gate_unexcused("src/ui/other.c",  two, 0) == 2
   first DLOG line only -> 0;  the first DLOG line twice -> 1 */
int test_dlog_gate_allow_survives_lines_added_above(void);
/* the one-allowed text with 50 "\n" prepended -> gate_unexcused("src/ui/window.c", t, 0) == 0 */
```

  Update `test_dlog_gate_flags_across_lines_and_nested_parens` to read line 6 and `"pw_secret_copy"` from `hits[0]`.

- [ ] **Step 2: Run `make test`.** Expected: compile failure (`GateHit`, `gate_unexcused` unknown).

- [ ] **Step 3: Implement.** `fk_put_file`: one `realloc` to `pad_to + 1` when `len < pad_to`, `memset` the gap with `'x'`, set `data[pad_to] = '\0'` (keep `mtime` last). `gate_scan` records each hit; `scan_dir` reads each file and adds `gate_unexcused(path, buf, 1)`; `allowed()` is deleted. `test_dlog_gate_allow_list_has_no_stale_entries` asserts, per entry, that a hit in that file has `needle` on its line.

- [ ] **Step 4: Run `make test`.** Expected: `Failed: 0`, `Tests Run` = baseline + 4, warnings W; `time ./build/test_runner` below T0 (note it).

- [ ] **Step 5: Commit** (`test(dlog): pad fake files in one allocation; key the gate's allow-list on content`).

### Task 2: per-object test build with header tracking and two stamps

**Files:**
- Modify: `Makefile` (CRLF; `test` ~224-227, `clean` ~242-244, `.PHONY` ~166, new variables after `TEST_TARGET` ~163), `.gitignore`

**Interfaces:**
- Produces (Makefile names later tasks extend):
  - `TEST_OBJ_DIR = build/test`, `TEST_OBJS = $(TEST_SRCS:%.c=$(TEST_OBJ_DIR)/%.o)`
  - `TEST_FLAGS_STAMP = $(TEST_OBJ_DIR)/flags.stamp` (text: `$(TEST_CC) | $(TEST_CFLAGS) | $(TEST_LDFLAGS) | SAN=$(SAN)`), `TEST_SRCS_STAMP = $(TEST_OBJ_DIR)/srcs.stamp` (text: `$(sort $(TEST_SRCS))`)
  - `SAN ?=` (empty) and `SAN_FLAGS =` (empty here; Task 3 fills both in)
  - `define write-stamp` (args: path, text): with `$(file <...)`/`$(file >...)` at parse time, rewrites the file only when its text differs, so its mtime moves only on a real change. Task 6 reuses it.
  - `STAMP_GOALS = test` (Task 6 adds the fuzz goals): the stamps are written, after `mkdir -p` of their directory, only when `$(filter $(STAMP_GOALS),$(MAKECMDGOALS))` is non-empty.

- [ ] **Step 1: Implement.**
  - `$(TEST_OBJ_DIR)/%.o: %.c $(TEST_FLAGS_STAMP)` → `@mkdir -p $(dir $@)` then `$(TEST_CC) $(TEST_CFLAGS) $(SAN_FLAGS) -MMD -MP -c $< -o $@`.
  - `$(TEST_TARGET): $(TEST_OBJS) $(TEST_SRCS_STAMP)` → `$(TEST_CC) $(TEST_OBJS) -o $@ $(TEST_LDFLAGS) $(SAN_FLAGS)`.
  - `test: $(TEST_TARGET)` → `./$(TEST_TARGET)`; `test` stays in `.PHONY`.
  - `-include $(TEST_OBJS:.o=.d)`, with a comment that `-MMD` leaves system headers out, so a changed system header (an OpenSSL upgrade) needs `make clean`.
  - `clean` also runs `rm -f build/test_runner$(EXE) build/test_runner_san$(EXE)` and `rm -rf build/test build/test-san build/fuzz`.
  - `.gitignore` gains `build/test/`, `build/test-san/`, `build/fuzz/`, `build/test_runner_san` under "Native-host test artefacts".
  - Detection, `NON_TEST_SRCS`, `WIN_ONLY_TESTS`, `-D_POSIX_C_SOURCE` and the cross-build `%.o: %.c` rule are unchanged.

- [ ] **Step 2: Clean build.** `make clean && make -j test 2>&1 | tee build/t.log`. Expected: `Failed: 0`, same `Tests Run` as Task 1 Step 4, warnings W; `build/test/src/core/json.o`, `build/test/tests/runner.o` and both stamps exist.

- [ ] **Step 3: Incremental checks.** Count compiles in each output with `grep -c -- ' -c '`:
  - `make test` again → `0`.
  - `touch src/config/json_parser.h && make -j test` → equals `grep -l 'src/config/json_parser.h' $(find build/test -name '*.d') | wc -l`.
  - `make -j test TEST_CC="gcc -DSTAMP_PROBE"` → equals the number of test sources (`make -n --eval 'n:;@echo $(words $(TEST_SRCS))' n`); then `make -j test` → that full count again; then `make test` → `0`.

- [ ] **Step 4: Added and deleted sources and headers (Review Focus 1).**
  - `printf '#include "zz_probe.h"\n' > tests/zz_probe.c; printf 'int zz_probe_unused;\n' > tests/zz_probe.h; make -j test` → passes, exactly `1` compile (only `srcs.stamp` changed, so nothing else rebuilds).
  - `rm tests/zz_probe.h; printf 'int zz_probe_unused;\n' > tests/zz_probe.c; make -j test` → passes, no "No rule to make target".
  - `rm tests/zz_probe.c; make -j test` → `0` compiles, the runner relinks, passes. `git status --short` shows neither file.

- [ ] **Step 5: `make clean`** → `ls build/test build/test-san build/fuzz 2>&1` reports all three missing; `git status --short` shows ` D build/win/nutshell.exe` only, then `git restore build/win/nutshell.exe`. Rebuild with `make -j test`.

- [ ] **Step 6: Cross-compile check.** Expected: builds (the `-include`d `.d` files and the stamps do not affect it).

- [ ] **Step 7: Commit** (`build: per-object native test build with header and flag tracking`).

### Task 3: `SAN=1`, `tests/lsan.supp` and a local sanitizer run

**Files:**
- Modify: `Makefile` (CRLF)
- Create: `tests/lsan.supp` (LF)
- Modify (only as findings require): files the run names

**Interfaces:**
- Consumes: `TEST_OBJ_DIR`, `SAN`, `SAN_FLAGS`, the stamps (Task 2).
- Produces: `make test SAN=1` → objects in `build/test-san/`, runner `build/test_runner_san$(EXE)`, `SAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -fsanitize-recover=all`; `tests/lsan.supp` (Task 7 passes it in CI).

- [ ] **Step 1: Implement.** `ifeq ($(SAN),1)`: set `SAN_FLAGS` as above, `TEST_OBJ_DIR = build/test-san`, `TEST_TARGET = build/test_runner_san$(EXE)`; inside it, `ifeq ($(HOST_WINDOWS),yes)` → `$(error SAN=1 needs libasan, which MinGW gcc does not ship; run it on Linux)`. `TEST_CFLAGS` is never overridden. `tests/lsan.supp` starts as a comment block only (LSan refuses a missing file): one `leak:<function>` line per unfixable product leak, each with a comment naming it and its Open-list entry; nothing broader.

- [ ] **Step 2: Isolation check.** `make -j test SAN=1` builds; then `make test` → `0` compiles; `nm build/test_runner | grep -c __asan_init` prints `0`; `nm build/test_runner_san | grep -c __asan_init` prints at least `1`.

- [ ] **Step 3: The run.**

```bash
set -o pipefail
CI=true ASAN_OPTIONS=halt_on_error=0:detect_leaks=1 \
LSAN_OPTIONS=suppressions=tests/lsan.supp \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 \
make -j"$(nproc)" test SAN=1 2>&1 | tee build/san.log
grep -nE 'AddressSanitizer|LeakSanitizer|runtime error:' build/san.log
```

  Expected at the end of the task: `make` exits 0, `Failed: 0`, grep prints nothing.

- [ ] **Step 4: Triage every report** by the first stack frame outside the sanitizer runtime, per Global Constraints "Sanitizer findings":
  - in `tests/` → fix it here (free what the test allocated, size the buffer right; a stack overflow moves the struct to the heap or `static`, never a raised `ulimit`);
  - a non-leak fault in `src/` → report `file:function`, fault class and the reaching test to the orchestrator before fixing; it is fixed in this PR with a native regression test, or the orchestrator splits it out to land first;
  - a leak in `src/` → report likewise; a fix, or (orchestrator's ruling that it cannot be fixed here) one `leak:<function>` line in `tests/lsan.supp`.

  Re-run Step 3 after each change.

- [ ] **Step 5: Run `make -j test`.** Expected: `Failed: 0`, warnings W.

- [ ] **Step 6: Record what this run never saw (Review Focus 2).** `make -n --eval 'x:;@echo $(NON_TEST_IMPL) $(filter src/term/ssh% src/config/ssh_io.c src/term/knownhosts.c,$(NON_TEST_SRCS))' x` → keep the list for the PR body as "first meets ASan in CI".

- [ ] **Step 7: Commit** (`build: SAN=1 runs the native suite under ASan, UBSan and LeakSanitizer`), fixes in the same commit, each named in the body; `tests/lsan.supp` staged by path.

### Task 4: `lint-gate`

**Files:**
- Modify: `Makefile` (CRLF; after `lint` ~246; `.PHONY`)

**Interfaces:**
- Produces: `LINT_GATE_SRC ?= src/`, `LINT_JOBS := $(shell nproc 2>/dev/null || echo 1)`, target `lint-gate`: `cppcheck --enable=warning,performance,portability --error-exitcode=1 --inline-suppr --std=c11 -q -j$(LINT_JOBS) $(LINT_GATE_SRC)`. `lint` is unchanged.

- [ ] **Step 1: Implement** the target above.

- [ ] **Step 2: Prove it bites.** `d=$(mktemp -d)`; `printf 'int f(void){int a[2]; return a[2];}\n' > $d/bad.c; make lint-gate LINT_GATE_SRC=$d/` → non-zero exit (arrayIndexOutOfBounds). Replace with `printf 'int f(void){int x = 1; return 0;}\n' > $d/bad.c` (style only) → exit 0.

- [ ] **Step 3: Run `time make lint-gate`.** Expected: exit 0 on the tree, roughly 46 s on `automaton`.

- [ ] **Step 4: Commit** (`build: lint-gate fails on cppcheck errors, warnings, performance and portability findings`).

### Task 5: dependency versions in About and `-v`

**Files:**
- Create: `src/core/build_info.h`, `src/core/build_info.c`, `tests/test_build_info.c`
- Modify: `tests/stubs/libssh2.h`, `src/core/about_layout.h`, `src/core/about_layout.c`, `tests/test_about_layout.c` (all 15 `about_layout_compute` calls), `src/ui/window.c` (~3073-3163), `src/main.c` (~329-331), `tests/runner.c`

**Interfaces:**
- Produces:
  - `size_t build_info_deps(char *out, size_t cap);` writes `OpenSSL <v>, libssh2 <v>, zlib <v>`, ASCII only, always NUL-terminated when `cap > 0`, returns the length it needed (snprintf-style); `out` may be NULL only with `cap == 0`.
  - `#define BUILD_INFO_DEPS_MAX 96` (buffer size callers use)
  - `AboutLayout` gains `AboutRect deps;` after `copyright`.
  - `void about_layout_compute(AboutLayout *out, int dpi, int icon_px, int title_h, int tagline_h, int copyright_h, int deps_h);` the `deps` row sits `ns_scale(SP_XS, dpi)` below `copyright`, full width; the button follows `deps` by `ns_scale(SP_XL, dpi)`.
  - `tests/stubs/libssh2.h`: `LIBSSH2_VERSION_MAJOR 0`, `LIBSSH2_VERSION_MINOR 0`, `LIBSSH2_VERSION_PATCH 0`, with a comment that the stub has no real version.

- [ ] **Step 1: Write the failing tests.**

```c
/* tests/test_build_info.c */
int test_build_info_deps_whole_format(void);
/* n = build_info_deps(buf, sizeof buf): n == strlen(buf); a static matcher
   `const char *ver(const char *s)` accepting [0-9]+(\.[0-9A-Za-z-]+)* (returns the char
   after it, or NULL) proves the whole string is
   "OpenSSL " ver ", libssh2 " ver ", zlib " (ver | "?") and then '\0';
   every byte is 0x20..0x7E */
int test_build_info_deps_openssl_part_matches_header(void);
/* the text between "OpenSSL " and the first ',' equals OPENSSL_VERSION_STR */
int test_build_info_deps_truncates(void);
/* full n; build_info_deps(small, 8) returns n, strlen(small) == 7, prefix equals the full text;
   cap 1 -> "" and returns n; (NULL, 0) returns n; n < BUILD_INFO_DEPS_MAX */
```

  Extend the existing `tests/test_about_layout.c` cases (pass `T_C` as `deps_h` everywhere, `-12` in the clamping case, `0` in the zero case): exact positions (`l.deps.y == l.copyright.y + T_C + SP_XS`, `l.deps.h == T_C`, `l.button.y == l.deps.y + T_C + SP_XL`); 192 = 2x96 for `deps.y`/`deps.h`; ordering (`copyright` bottom ≤ `deps.y`, `deps` bottom ≤ `button.y`); full width (`deps.x == 0`, `deps.w == client_w`, and the same at 120 DPI); zero heights (`deps.h == 0`). No glyph-width assertion (spec §7).

- [ ] **Step 2: Run `make -j test`.** Expected: compile failure (`build_info_deps` undefined, `about_layout_compute` arity).

- [ ] **Step 3: Implement `build_info.c`.** OpenSSL from `OPENSSL_VERSION_STR` (`<openssl/opensslv.h>`); libssh2 from `LIBSSH2_VERSION_MAJOR`/`_MINOR`/`_PATCH` formatted at run time (never `LIBSSH2_VERSION`: vcpkg's is `"1.11.1_DEV"`); zlib from `ZLIB_VERSION` under the nested form `#if defined(__has_include)` / `#if __has_include(<zlib.h>)`, else `"?"`. Then `about_layout.c` per the Interfaces block.

- [ ] **Step 4: Run `make -j test`.** Expected: `Failed: 0`, tests up by 3, warnings W.

- [ ] **Step 5: Wire the UI.** `window.c`: `about_compute_layout` passes `ns_type_line_px(FONT_CAPTION, dpi)` as `deps_h`; `about_draw_row` adds `DT_END_ELLIPSIS` to its flags; `WM_PAINT` draws `lay.deps` with `FONT_CAPTION`, `tk->text_dim` and the text from `build_info_deps()` into a `char[BUILD_INFO_DEPS_MAX]`. `main.c` `CLI_VERSION`: `cli_output` of `"Nutshell " APP_VERSION "\n" <deps> "\n"` built with `snprintf` into a local buffer.

- [ ] **Step 6: Run `make -j test`** (expected: pass, warnings W, `test_ui_tokens` green) **and the cross-compile check.** Confirm the cross build's headers: `grep -E 'LIBSSH2_VERSION_(MAJOR|MINOR|PATCH) ' ~/vcpkg/installed/x64-mingw-gcc-static/include/libssh2.h` shows 1, 11, 1, and `grep 'define ZLIB_VERSION' ~/vcpkg/installed/x64-mingw-gcc-static/include/zlib.h` shows `1.3.1` (so About will not show `zlib ?`).

- [ ] **Step 7: Commit** (`feat(about): show the OpenSSL, libssh2 and zlib versions in About and -v`).

### Task 6: fuzz targets, seeds and `make fuzz`/`fuzz-run`/`fuzz-long`

**Files:**
- Create: `tests/fuzz/fuzz_json.c`, `fuzz_term.c`, `fuzz_exec_markers.c`, `fuzz_cmd_classify.c`, `fuzz_html.c`, `fuzz_url.c`; `tests/fuzz/corpus/<name>/` (small text seeds)
- Modify: `Makefile` (CRLF; `.PHONY`, `STAMP_GOALS`)

**Interfaces:**
- Consumes: `write-stamp`, `STAMP_GOALS`, `TEST_CFLAGS`, `TEST_LDFLAGS`, `APP_SRCS` (Task 2).
- Produces:
  - `FUZZ_CC`: when not set by the caller (`ifeq ($(origin FUZZ_CC),undefined)`), `:=` the first of `clang clang-18 clang-17 clang-16` on PATH via `$(firstword $(foreach c,clang clang-18 clang-17 clang-16,$(shell command -v $(c))))`; empty → the fuzz targets stop with one line naming the four tried and `FUZZ_CC=`.
  - `FUZZ_TIME ?= 60`, `FUZZ_TARGETS = json term exec_markers cmd_classify html url`, `FUZZ_DIR = build/fuzz`, `FUZZ_CFLAGS = $(TEST_CFLAGS) -Wno-ignored-attributes -Wno-unknown-warning-option`
  - `$(FUZZ_DIR)/libnutshell.a` from `$(APP_SRCS)` compiled to `$(FUZZ_DIR)/obj/%.o` with `$(FUZZ_CFLAGS) -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -MMD -MP`. Objects depend on `$(FUZZ_DIR)/obj/flags.stamp` (`FUZZ_CC`, those flags); the archive depends on `$(FUZZ_DIR)/obj/srcs.stamp` (sorted `APP_SRCS`) and is rebuilt from scratch (`rm -f $@` before `ar rcs`) so a dropped source leaves no stale member.
  - static pattern `$(FUZZ_BINS): $(FUZZ_DIR)/%: tests/fuzz/fuzz_%.c $(FUZZ_DIR)/libnutshell.a`, linked with `$(FUZZ_CFLAGS) -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined … $(TEST_LDFLAGS)`
  - `STAMP_GOALS` gains `fuzz fuzz-run fuzz-long`. Targets `fuzz` (build), `fuzz-run`, `fuzz-long`. On `HOST_WINDOWS` all three print `[skip] fuzzing needs clang's libFuzzer; not available under MSYS2` and exit 0.
  - `FUZZ_LOG_FILTER = ^INFO: Seed:|^==[0-9]+==(ERROR|WARNING)|^ *\#[0-9]+ 0x[0-9a-f]+ |^SUMMARY:` (frames printed whether symbolized or not; never `Base64:`, hex dumps or `MS:` lines).

- [ ] **Step 1: Write the six targets** (`int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)`, returns 0; all heap freed; large arrays `static`):
  - `json`: NUL-terminated heap copy → `json_parse()` → `json_free()`.
  - `term`: `term_init(24, 80, 100)`, `term_process(t, (const char *)data, size)`, `term_free()`.
  - `exec_markers`: NUL-terminated copy → `ai_extract_commands_full(s, cmds, notes, quoted, 16, &rej, &rej_long)` with `static char cmds[16][1024]`, `static char notes[16][CMD_NOTE_MAX]`, `int quoted[16]`.
  - `cmd_classify`: `size == 0` returns; `p = data[0] % (CMD_PLATFORM_UNKNOWN + 1)`; the rest NUL-terminated as `c` → `cmd_classify_ex(c, p, reason, sizeof reason)`; then `abort()` unless all hold: `cmd_classify_mask(c, p) & CMD_MASK_OF(cmd_classify(c, p))` is non-zero; `cmd_classify_session(c, p, 1) >= cmd_classify(c, p)`; `cmd_classify_session(c, p, 1) >= cmd_classify(c, CMD_PLATFORM_UNKNOWN)`.
  - `html`: `web_fetch_html_to_text((const char *)data, size, &out, &len, &trunc)` on the raw bytes, `free(out)`.
  - `url`: NUL-terminated copy → `net_url_parse()`, `net_url_display()`, `net_url_cleartext_remote()`, `net_url_resolve(s, "z/../w?k=1", ...)`, `net_url_resolve("https://a.test/x/y?q", s, ...)` into `static char out[NET_URL_MAX]`.

- [ ] **Step 2: Seeds**, plain files, each under 4 KiB. `json` and `term` start from the inputs in `tests/test_json_fuzz.c` and `tests/test_term_fuzz.c` (one file per input, escapes decoded), plus a few well-formed samples. Others: replies with live, fenced and inline `[EXEC]` markers; a platform byte plus `ls -la`/`show run`/`rm -rf /`; HTML with entities, `<script>`, unclosed tags; absolute, relative, `//host`, IPv6 and userinfo URLs.

- [ ] **Step 3: Write the run recipes** (POSIX sh; one `define fuzz-loop` taking the extra flags and the directory list is fine). First `mkdir -p build/fuzz/log build/fuzz/crashes` (and `build/fuzz/corpus/<name>` for `fuzz-long`). Per target: record `date +%s`; run `build/fuzz/<name> -timeout=10 -artifact_prefix=build/fuzz/crashes/<name>- <dirs> >build/fuzz/log/<name>.log 2>&1`, then `rc=$?`; then `grep -E '$(FUZZ_LOG_FILTER)' build/fuzz/log/<name>.log || true`; on `rc != 0` print `fuzz <name>: FAILED (exit <rc>); details in build/fuzz/log/<name>.log` and `exit 1` at once; else print `fuzz <name>: ok in <s> s`.
  - `fuzz-run`: `-runs=0` and `tests/fuzz/corpus/<name>/` only (no scratch directory; `-runs=0` writes nothing there).
  - `fuzz-long`: `-max_total_time=$(FUZZ_TIME)` and `build/fuzz/corpus/<name>/ tests/fuzz/corpus/<name>/` (growth lands in the first).

- [ ] **Step 4: Build and run.** `make fuzz FUZZ_CC=/usr/lib/llvm-16/bin/clang && make fuzz-run FUZZ_CC=/usr/lib/llvm-16/bin/clang`. Expected: six `ok` lines, an `INFO: Seed:` line each, no `SUMMARY:` line. `make fuzz` with `FUZZ_CC` unset prints the one-line "no clang" error (none is on PATH on `automaton`).

- [ ] **Step 5: One long run.** `make fuzz-long FUZZ_CC=/usr/lib/llvm-16/bin/clang` (6 x 60 s). A crash: copy `build/fuzz/crashes/<name>-*` to the scratchpad; if the fix is small, fix it in `src/` with a native regression test that encodes the fault class, not the raw input (in the owning `tests/test_*.c`, declared in `tests/runner.c`), and re-run; otherwise report entry point and fault class to the orchestrator. Never commit the input.

- [ ] **Step 6: Checks (Review Focus 3).**
  - `find tests/fuzz/corpus -type f -size +4k` prints nothing.
  - Every `fuzz <name>: ok in <s> s` line from Step 4 has `s <= 2`.
  - After Steps 4-5, `git status --short tests/fuzz/corpus` lists only the new seeds and nothing modified; nothing new at the repository root.
  - `make -n --eval 'x:;@echo $(TEST_IMPL_SRCS)' x | tr ' ' '\n' | grep -c '^tests/fuzz/'` prints `0`.
  - `make -j test` passes, warnings W; a second `make fuzz FUZZ_CC=...` compiles nothing.

- [ ] **Step 7: Commit** (`test(fuzz): libFuzzer targets for six untrusted-byte parsers`), staging every seed by path.

### Task 7: `quality.yml`, `fuzz-weekly.yml` and the CodeQL comment

**Files:**
- Create: `.github/workflows/quality.yml`, `.github/workflows/fuzz-weekly.yml`
- Modify: `.github/workflows/codeql.yml` (the comment "No pipeline here: the job shell is bash -eo pipefail, ..." only)

**Interfaces:**
- Consumes: `make test SAN=1`, `tests/lsan.supp` (Task 3), `make lint-gate` (Task 4), `make fuzz`/`fuzz-run`/`fuzz-long` (Task 6).
- Produces: check names `Sanitizers`, `Lint`, `Fuzz` (pull requests) and `Fuzz (scheduled)` (weekly).

- [ ] **Step 1: Write `quality.yml`** per spec §5:
  - `on: pull_request: branches: [main]` only; `permissions: contents: read`; `concurrency: group: ${{ github.workflow }}-${{ github.ref }}`, `cancel-in-progress: true`; header comment: none of these is a required check, all are expected green before a merge, why the skip lives in a step.
  - Exactly three jobs, `Sanitizers`, `Lint`, `Fuzz`, each `runs-on: ubuntu-24.04`, `timeout-minutes: 15`, the pinned checkout.
  - Each job's first step `scope` (as `checks.yml`'s, regex `^(src/|tests/|build-support/|Makefile$|nutshell\.rc$|README\.md$|\.github/workflows/quality\.yml$)`, tested with `grep -qE … <<<"$changed"`, not `printf | grep -q`); every later step `if: steps.scope.outputs.code == 'true'`.
  - apt step as `checks.yml` (drop stale Google/Microsoft sources by host, `apt-get update`, `libssl-dev libssh2-1-dev zlib1g-dev`; `Lint` adds `cppcheck`).
  - `Sanitizers`: the libssh2 probe step from `checks.yml`; then `mkdir -p build` and `make -j"$(nproc)" test SAN=1 2>&1 | tee build/san.log` with env `CI: true`, `ASAN_OPTIONS: halt_on_error=0:detect_leaks=1`, `LSAN_OPTIONS: suppressions=tests/lsan.supp`, `UBSAN_OPTIONS: print_stacktrace=1:halt_on_error=0`; then a step that runs `grep -nE 'AddressSanitizer|LeakSanitizer|runtime error:' build/san.log` and exits 1 when it matches (matches printed).
  - `Lint`: `v=$(cppcheck --version)`; fail unless it starts `Cppcheck 2.13` (a `case`, no pipe); `make lint-gate`.
  - `Fuzz`: a step that links a one-line `LLVMFuzzerTestOneInput` with `clang -fsanitize=fuzzer` and, on failure, installs `libclang-rt-$(clang -dumpversion | cut -d. -f1)-dev`; exports `ASAN_SYMBOLIZER_PATH` to `llvm-symbolizer` or `llvm-symbolizer-<N>` when one exists (via `$GITHUB_ENV`); then `make fuzz && make fuzz-run`.
  - No `upload-artifact`, no `paths`, no `continue-on-error`.

- [ ] **Step 2: Write `fuzz-weekly.yml`**: `on: schedule: - cron: '0 3 * * 1'` and `workflow_dispatch:`; same permissions and concurrency; one job `Fuzz (scheduled)` on `ubuntu-24.04`, `timeout-minutes: 15`: checkout, apt step, the clang link/symbolizer step, `make fuzz && make fuzz-long`. Header comment: not a pull-request check; whoever sees it red opens an issue by hand naming target and fault class, never the input.

- [ ] **Step 3: Write and run the checker** `$SCRATCH/check_workflows.py` (scratchpad, not committed; `python3 -I`). It loads each file with `yaml.safe_load` (`on` loads as `True`: read `doc.get('on', doc.get(True))`) and asserts: `quality.yml` triggers only `pull_request` on `main`, exactly the three job names; `fuzz-weekly.yml` triggers `schedule` with cron `0 3 * * 1` plus `workflow_dispatch`, one job `Fuzz (scheduled)`; every job `runs-on: ubuntu-24.04` and `timeout-minutes == 15`; workflow `permissions == {'contents': 'read'}`; every `uses:` pinned to a 40-hex SHA; no `paths`/`paths-ignore`, `upload-artifact` or `detect_leaks=0` anywhere; `Sanitizers`' env has `CI` and `LSAN_OPTIONS=suppressions=tests/lsan.supp`; **every step whose `run` contains a `|` not part of `||` has `shell: bash` and its first non-comment, non-blank line matches `^set -[a-z]*o pipefail`**. Run on both files → exit 0. Prove it: a copy of `quality.yml` without the `Sanitizers` make step's `set -o pipefail`, and one without its `shell: bash`, each → non-zero naming the step. `python3 -I -c 'import sys,yaml; [yaml.safe_load(open(p)) for p in sys.argv[1:]]' .github/workflows/*.yml` → exit 0.

- [ ] **Step 4: Fix `codeql.yml`'s comment** to say the default `run` shell is `bash -e {0}` (pipefail only with an explicit `shell: bash`), so a `grep` that matches nothing inside `$(...)` would still fail the step without the `|| true`. Commands unchanged; `git diff .github/workflows/codeql.yml` touches comment lines only.

- [ ] **Step 5: Commit** (`ci: Sanitizers, Lint and Fuzz checks on pull requests; weekly fuzzing`).

### Task 8: release checksum

**Files:**
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Implement.** After the verify step, an unconditional step `id: sum`: `cd build/win && sha256sum nutshell.exe > nutshell.exe.sha256`, then `echo "hash=$(cut -d' ' -f1 nutshell.exe.sha256)" >> "$GITHUB_OUTPUT"`. `Publish release`: `gh release create "${{ github.ref_name }}" build/win/nutshell.exe build/win/nutshell.exe.sha256 --title "${{ github.ref_name }}" --generate-notes --notes "SHA-256: $HASH"` with `HASH: ${{ steps.sum.outputs.hash }}` in `env`. The upload step's `path:` lists both files. One header-comment sentence: the `.sha256` sits beside GitHub's own digest so `sha256sum -c` works offline.

- [ ] **Step 2: Check locally.** `d=$(mktemp -d) && cp build/win/nutshell.exe $d/ && (cd $d && sha256sum nutshell.exe > nutshell.exe.sha256 && sha256sum -c nutshell.exe.sha256)` → `nutshell.exe: OK`; the hash is 64 hex characters. The YAML load from Task 7 Step 3 passes on `release.yml`.

- [ ] **Step 3: Commit** (`ci(release): publish nutshell.exe.sha256 and state the hash`).

### Task 9: audit-tag index

**Files:**
- Create: `docs/audit-tags.md`
- Modify: `tests/test_ai_prompt.c` (heading ~495-496 above `test_ai_extract_commands_ex_embedded_newline_rejected`), `tests/test_vt_sequences.c` (CRLF; heading ~284)

- [ ] **Step 1: Inventory.** Hyphenated: `grep -rnoE '\b[CHILM]-[0-9]+\b' src tests --include=*.c --include=*.h` (expect exactly the 21 of spec §8). Unhyphenated: `grep -rnE '\b[CHLM][0-9]\b' src tests --include=*.c --include=*.h`, keeping only references that cite an audit or review (spec's M1–M6, H1–H4, H6, H9, L1–L4, C1, C2; decide C3, C4 and L6 by reading them); `C0`/`C1` naming control-character ranges are left out.

- [ ] **Step 2: Write `docs/audit-tags.md`**: a title; the sentence that the 2026-09-09 security-audit spec the tests cite was never committed and that b5d3887 (2026-09-09, "fix(security): reject control characters in AI commands … clamp the VT parser", v1.1.10) added the regressions; table 1 (tag | one-line meaning | `file:function` locations, no line numbers); table 2 (tag | origin: 2026-09-24 review or classifier holes audit | meaning | locations).

- [ ] **Step 3: Cite it.** The `test_ai_prompt.c` heading's `(see docs/superpowers/specs/2026-09-09-security-audit.md)` becomes `(see docs/audit-tags.md)`; `test_vt_sequences.c`'s heading becomes `/* ---- Security audit regressions (2026-09-09, docs/audit-tags.md) ---- */`, CRLF kept. No other comment changes.

- [ ] **Step 4: Verify.** A loop over the Step 1 tag list with `grep -qw "$tag" docs/audit-tags.md` finds every one; `make -j test` passes; the CRLF check passes for `tests/test_vt_sequences.c`.

- [ ] **Step 5: Commit** (`docs: map every cited audit tag in docs/audit-tags.md`).

### Task 10: process texts and README

**Files:**
- Modify: `CLAUDE.md` (merge-gate section, after "Never push to `main` directly; never merge a red check." ~239), `.claude/skills/feature-workflow/SKILL.md` (step 7 ~131-133; Dependabot ~195), `.claude/memory/MEMORY.md` (~154-155), `.claude/CHANGELOG.md` (new entry at the top), `README.md` (Linux host list ~709-717, Targets table ~727-735)

- [ ] **Step 1: Edit.**
  - `CLAUDE.md`: one sentence: `Sanitizers`, `Lint` and `Fuzz` (`.github/workflows/quality.yml`) are deterministic, expected green before a merge and not in the ruleset; the weekly `Fuzz (scheduled)` run (`fuzz-weekly.yml`) is not a pull-request check, and whoever sees it red opens an issue naming target and fault class.
  - `feature-workflow` step 7: expect the seven checks `Version bump`, `Native tests`, `analyze`, `CodeQL`, `Sanitizers`, `Lint`, `Fuzz`, all `success`; the first two required; a docs-only pull request still shows all seven (the quality jobs report green with their work skipped). Dependabot line: "confirm all seven checks are green".
  - `MEMORY.md`: the "three checks on an actions-only change" line names the seven, noting that `Native tests` and the three quality jobs skip their work unless the bump touches their own workflow file.
  - `.claude/CHANGELOG.md`: `## <commit date> — the quality checks in the process texts`, `Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg, on \`automaton\`.`, a `Models:` line, one bullet per file saying what changed and why (spec §5).
  - `README.md` "Linux host (cross-compile)": the shipped vcpkg versions (libssh2 1.11.1, OpenSSL 3.6.1, zlib 1.3.1 at this release; About and `-v` show the built-in ones); optional `libssh2-1-dev` and `zlib1g-dev` for the native build; `clang` for `make fuzz`. Targets table gains `make -j test` (incremental, parallel), `make test SAN=1`, `make lint-gate`, `make fuzz` / `make fuzz-run` / `make fuzz-long`. The shortcuts block is untouched.

- [ ] **Step 2: Run `make -j test`.** Expected: pass (`test_shortcuts` still matches README).

- [ ] **Step 3: Commit** (`docs: the quality checks in CLAUDE.md, the workflow skill, memory and README`), all five files in one commit (the CHANGELOG rule).

### Task 11: version bump, Windows build and gate checks

**Files:**
- Modify: `src/ui/resource.h` (`APP_VERSION`, `APP_VERSION_BINARY`), `README.md` (`**Version**:`), `build/win/nutshell.exe`

- [ ] **Step 1: Work out the version.** `git fetch origin main`; take the higher of `git show origin/main:src/ui/resource.h | grep APP_VERSION` and the branch's; add one patch. Write it into both `resource.h` macros and README's `**Version**:` line. Confirm `hostname` prints `automaton`.
- [ ] **Step 2: Build.** `make clean && make release`. Expected: success, UPX output. On failure, `git restore build/win/nutshell.exe` before anything else.
- [ ] **Step 3: Tests.** `make -j test 2>&1 | tee build/t.log`: `Failed: 0`, warnings W, note the count; `make test` again compiles nothing. `xvfb-run -a make wintest`: 45 run, 0 failed (if Wine reports no driver: `pgrep -a wine` first, `wineserver -k` only if no other session's run shows). `make lint-gate` exit 0. Task 3 Step 3 once more: clean apart from named `tests/lsan.supp` entries. `make fuzz-run FUZZ_CC=/usr/lib/llvm-16/bin/clang`: six `ok`.
- [ ] **Step 4: Gate script.** `bash .github/scripts/check-version.sh origin/main` → passes.
- [ ] **Step 5: Checklist.** `git diff origin/main --stat` equals `git diff origin/main --stat --ignore-cr-at-eol` per file; `git ls-files --eol Makefile tests/test_vt_sequences.c` shows `i/crlf` for both; read every `??` line of `git status --short`; `git diff origin/main --name-only -- .github/workflows/checks.yml .github/scripts tests/test_ui_tokens.c tests/test_json_fuzz.c tests/test_term_fuzz.c .gitattributes` prints nothing; `grep -rn 'RGB(' src/ui --include=*.c | grep -v ns_draw.c | wc -l` unchanged from `main`.
- [ ] **Step 6: Commit** `build: vX.Y.Z` staging `src/ui/resource.h README.md build/win/nutshell.exe` by path. Do not push, mark ready or merge.

---

## Handoff (orchestrator and maintainer, after Task 11)

- Push and open the draft PR; its body carries the `Models:` line, the test count, the Task 3 Step 6 list, every `tests/lsan.supp` entry with its reason, and any finding from Tasks 3 and 6 (entry point and fault class only).
- **First CI run (Review Focus 2):** read `Sanitizers` before anything else. Findings in the libssh2-gated files follow Global Constraints "Sanitizer findings": non-leak faults fixed in a follow-up commit (`tests/` only needs no bump; `src/` needs a re-bump and rebuild) or split into a PR that lands first; an unfixable product leak gets one named `tests/lsan.supp` line. A red `Sanitizers` is never merged. Also confirm `Fuzz` linked without the extra package (or that the fallback ran), stack frames were symbolized, and every job finished inside 15 minutes.
- `workflow_dispatch` on the branch for `fuzz-weekly.yml` and for `release.yml` (the artifact holds both files and `sha256sum -c` passes on them).
- Windows by hand: About's fourth row on the four themes at 100 %, 125 % and 200 % and after a `WM_DPICHANGED` monitor move, no clipping (an ellipsis is acceptable); About shows a real zlib version, not `?`; `nutshell.exe -v` prints both lines.
- `gh pr ready` and the merge stay with the maintainer, after all seven checks are green.
