# CLAUDE.md — Nutshell Project Instructions

## MANDATORY: Version Bump Before Every Build

**Before running `make` (any target that produces a binary), increment the patch version in BOTH files:**

1. `src/ui/resource.h` — update `APP_VERSION` string and `APP_VERSION_BINARY` macro (e.g., `"1.0.30"` -> `"1.0.31"` and `1,0,30,0` -> `1,0,31,0`)
2. `README.md` — update the `**Version**:` line

`nutshell.rc` reads `APP_VERSION` and `APP_VERSION_BINARY` from `resource.h` via `#include`, so it updates automatically. No exceptions. Every build gets a unique version number.

The patch number never goes past 99: after `1.0.99` comes `1.1.0` (`1,1,0,0`), not `1.0.100`.

CI enforces this: the `Version bump` check (`.github/scripts/check-version.sh`) fails any pull request that touches `src/`, the `Makefile` or `nutshell.rc` without raising `APP_VERSION`, or that leaves the three version strings disagreeing.

## Build Commands

- **Always `make clean && make release`** — never `make release` alone.
- **Always run `make test` after changes** to verify nothing is broken.
- **Two compilers**: `x86_64-w64-mingw32-gcc` for the Windows cross-compile (`make`), native `gcc` for tests (`make test`). Code must compile clean under both with `-Werror`.
- **Linker order matters**: dependency libs (`-lssh2 -lssl -lcrypto -lzlib`/`-lz`) must come before Windows system libs (`-lws2_32 -lgdi32 ...`).
- **`NON_TEST_SRCS`**: Files in `src/ui/` are excluded from test builds. If you add portable logic that needs testing, put it in `src/core/`, not `src/ui/`.
- **libssh2 test stub** lives in `tests/stubs/libssh2.h` and is only added to the include path (`-Itests/stubs`) when the test build finds no real libssh2. Never put a stub header in `src/` — it would shadow the real one in the Windows build.

### Building on Windows (MSYS2 MINGW64) — the current dev host

The Makefile detects a Windows host (`OS=Windows_NT`) and switches to the pacman-installed libs, `-lz`, static linking, and `windres`.

- One-time setup: `pacman -S mingw-w64-x86_64-make mingw-w64-x86_64-libssh2 mingw-w64-x86_64-openssl mingw-w64-x86_64-zlib mingw-w64-x86_64-upx mingw-w64-x86_64-gdb`
- PATH must have `C:\msys64\mingw64\bin` before `C:\msys64\usr\bin`.
- **Use `mingw32-make`, never the MSYS `make`** — MSYS make rewrites the temp-dir env for child processes and gcc then fails with "Cannot create temporary file in C:\WINDOWS".
- Commands: `mingw32-make clean && mingw32-make release`, `mingw32-make test`, `mingw32-make wintest` (the GDI+ icon harness, runs directly on Windows).
- Debug a crashing test runner with `gdb -batch -ex run -ex bt ./build/test_runner.exe`.

## Test-Driven Development

Write tests before implementation code. Include corner cases, positive and negative tests.

- Test framework: custom `test_framework.h` with `TEST_BEGIN()`/`TEST_END()`, `ASSERT_EQ`, `ASSERT_STR_EQ`, `ASSERT_TRUE`, `ASSERT_NULL`, `ASSERT_NOT_NULL`.
- All test functions are declared and called in `tests/runner.c`.
- Tests run natively on the build host (Linux or Windows) — Win32 UI code is excluded; Win32-only tests (`test_icons.c`, `win_runner.c`) belong to `make wintest`.
- Tests that write files must build paths from `TEST_TMP_DIR` (in `test_framework.h`), never a literal `/tmp/` — that path does not exist for a MinGW binary.
- Windows threads default to a 1 MB stack (Linux: 8 MB). The Makefile links the Windows test runner with 16 MB; keep very large structs off the stack in product code anyway.

## Software Development Rules for Claude

- **Opus orchestrates each work package.** One Opus agent owns the package
  end to end: it decides which model does each sub-task — Sonnet or Haiku
  for mechanical implementation, Opus itself for review and the tricky
  parts — spawns those agents with narrow briefs (the files and cases they
  touch, not the whole tree; agent cost is driven by what they read and
  re-run, not by the model), reviews their diffs, and opens the pull request.
- **Escalation**: Opus hands a problem to Fable 5.1 (the main session) only
  when it cannot work it out or wants a peer consult, and says so explicitly
  in its report.
- **Fable checks Opus's work**: the main session reviews the orchestrator's
  deliverable at pull-request level before it merges. Fable is not spent on
  mechanical implementation.
- **Context discipline**: only preserve sub-agent results and key learnings in
  the main context — discard intermediate details, and never poll a running
  agent in a loop; its completion notification is the signal.

## Config Header

There is a single `config.h` at `src/config/config.h`, used by both the Windows cross-compile and native test builds. When modifying the `Settings` struct or `Profile` struct, this is the only file that needs updating.

## Cross-Compile Pitfalls

- **winsock2.h before windows.h**: Any file that transitively includes `windows.h` (e.g., via `ssh_session.h`) and also needs winsock must `#include <winsock2.h>` first, or you get redefinition errors.
- **`_snwprintf` is MSVC-only**: Use `swprintf` (ISO C) instead for MinGW compatibility.
- **`-Wshadow` is strict**: Local variables must not shadow function parameters. Common trap: naming a local `msg` inside a WndProc that has `UINT msg` as a parameter.
- **`-Wconversion` catches size_t/int mismatches**: When calling functions that take `size_t`, cast explicitly (e.g., `(size_t)strlen(cmd)` not `(int)strlen(cmd)`).
- **Missing `#include <stdio.h>`**: If a `.c` file uses `snprintf` but only includes domain headers, MinGW will error on implicit declaration. Always include `<stdio.h>` explicitly.

## Design system rules

All of `src/ui`'s colours, sizes, fonts, animation and hover state come from
the six Design-System Foundation modules (see
`docs/superpowers/specs/2026-09-07-design-system-foundation-design.md`).
Two native tests in `tests/test_ui_tokens.c` enforce this as an
exact-allow-list gate, not a ratchet — any violation fails `make test`:

- **Never `RGB(` in `src/ui`, outside `ns_draw.c`** (which is excluded
  entirely) **or the 2 allowed literals in `renderer.c`** (the terminal's
  default fg/bg fallback, used only before a theme is resolved). Pull the
  colour from
  `ns_tokens()` (a `const ThemeTokens *`): a surface's `.base`/`.hover`/
  `.pressed`/`.disabled`/`.label`, a scalar like `text_main`/`text_dim`/
  `text_disabled`/`border`/`focus`, one of the five intents (`success`/
  `warning`/`danger`/`info`/`link`), or the chat block. Need something in
  between two tokens (a dimmed label, a tinted chip background)? Use
  `rgb_alpha()` from `ns_draw.h` to blend them — never a new hardcoded
  literal. The gate classifies each `RGB(` call by its arguments: literal
  numbers (`RGB(255, 255, 255)`) count against the allow-list; an
  expression unpacking an existing packed colour (`RGB((c) >> 16 & 0xFF,
  ...)`) or blending two `COLORREF`s (`GetRValue`/`GetGValue`/`GetBValue`
  arithmetic) does not.
- **Never `MulDiv(x, dpi, 96)` or a local `#define S(px)`/`CLV_SCALE`-style
  scale macro.** Use `ns_scale(px, dpi)` (`src/core/ns_scale.h`) — the one
  DPI-scaling helper for the whole UI. (`MulDiv` for something that
  genuinely isn't a 96-DPI scale — a point-size conversion at `/72`, or
  rescaling between two live DPIs on a monitor move — is fine; it just
  won't match the gate's `MulDiv(..., 96)` pattern.)
- **Never `CreateFont` in `src/ui`.** Use `ns_font(role, dpi)`
  (`src/ui/ns_font.h`), a cache keyed on `(role, dpi, face)`; call
  `ns_font_flush()` after `WM_DPICHANGED` or a font-setting change, not a
  fresh `CreateFont`.
- **Sizes and type from `ns_type.h`**: the `SP_*`/`SZ_*` spacing-and-size
  grid, `R_CTRL`/`R_CARD`/`ns_type_pill()` for radii, `STROKE_*` for line
  widths, and the `NsFontRole` ramp (`FONT_CAPTION`/`FONT_BODY`/
  `FONT_TITLE`/`FONT_HEADING`/`FONT_MONO`) for text size/weight/line-height
  — never a bare pixel constant for something the grid already names.
- **Animation via `ns_motion`** (`src/core/ns_motion.h`): one timer per
  window drives an `NsAnimList`; easing and progress are pure, tested
  functions. Don't add a second `WM_TIMER`-driven animation loop.
- **Hover via `ns_hover`** (`src/core/ns_hover.h`): feed a hit-test id into
  `ns_hover_move()`/`ns_hover_leave()` on `WM_MOUSEMOVE`/`TrackMouseEvent`
  and invalidate only the two elements that changed state, rather than
  tracking a hot-id by hand per widget.
- **Visual sanity check**: `nutshell.exe --ui-demo=all` (or `--ui-demo=<state>`
  with `--theme "<name>"`) opens a live window in every panel state without
  needing an SSH session or an AI key — use it to eyeball a change before
  running the integration suite's `ui_gallery` case, which screenshots the
  same states across all four themes into `tests\integration\artifacts\gallery\`.

## Integration Harness

`tests/integration/Run-Integration.ps1` drives the real `nutshell.exe` against
a live SSH host — see `tests/integration/README.md` for the case list, the
helpers, prerequisites and how to run a subset. Typing and dialog driving go
through posted window messages (`Send-NutshellLine`, `Wait-NutshellDialog`,
`Get-NutshellControl`, …), so **no** case needs a foreground window: the suite
runs on the self-hosted runner, on a locked desktop and on an
RDP-disconnected session alike. Two hard rules:

- **Never stop a `nutshell` process you did not start.** Each case launches
  its own scratch copy of the exe and tears it down itself; killing an
  unrelated running instance (the user's own session, say) is never the fix
  for a stuck or failing case.
- **Modifier chords go through `Send-NutshellChord`** (Ctrl+C/V,
  Ctrl+Shift+C/V, Shift+Insert, Ctrl+= zoom — the ones the app reads with
  `GetKeyState`). It attaches the harness thread's input to the app's UI
  thread (`AttachThreadInput`) so a posted key sees the modifier down; there
  is no real-`SendKeys` helper and nothing may be added that needs the
  foreground. If a case seems to need one, the mechanism is wrong, not the
  desktop.

The tiers: `-Tier gate` is the merge gate — every case that needs no AI key —
`-Tier ai` the key-gated cases (they cost model credits), `-Tier nightly` the
slow ones.

## Branches, pull requests and the integration-test gate

`main` is protected: changes land only through a pull request whose
`Integration tests` status check (`.github/workflows/integration.yml`,
self-hosted runner labelled `nutshell-desktop` on the dev box) has passed. The
`Version bump` check (`.github/workflows/checks.yml`) is required too.

**That check runs once per pull request, when the pull request is marked ready
for review** — not on every push. It occupies the one machine that can drive a
real desktop for 10–15 minutes, so work in a draft and open the gate when the
change is actually done:

```
git switch -c my-change && git push -u origin my-change
gh pr create --draft            # push as often as you like: no desktop runs
gh pr ready N                   # fires the integration tests, once
gh pr merge N --merge --auto    # auto-merge lands it when both checks go green
```

`gh pr ready` before `gh pr merge --auto`, in that order — auto-merge cannot be
enabled while a pull request is still a draft.

Never push to `main` directly; never merge a red gate. Two consequences of
running the gate once, both deliberate:

- **Push after it has gone green and the check goes missing on the new commit,
  which blocks the merge** rather than passing it on a stale result. Re-run it
  with `gh workflow run integration.yml --ref my-change`.
- **A pull request opened non-draft never fires `ready_for_review`** (Dependabot's,
  or one opened by mistake), so it has no gate result at all. Run
  `gh workflow run integration.yml --ref <branch>` for those too.

`tests/integration/Protect-Main.ps1` applies the ruleset and
`tests/integration/Install-IntegrationRunner.ps1` registers the runner (both
need `gh auth login` by a repository administrator).

## Terminal Buffer

- `TermRow.len` tracks actual written content width. Always use `row->len` for content boundaries, not `term->cols`.
- When extracting terminal text, skip trailing empty rows to avoid spurious blank lines. Use a two-pass approach: first find last non-empty row, then extract.

## JSON Handling

- Use `json_parse()` + `json_obj_get()`/`json_obj_str()` for reading. Always `json_free()` the root.
- For building JSON output (e.g., AI request bodies), use `snprintf` with manual escaping via `json_escape_string()` (`src/core/json_validate.h`). There is no JSON builder library.
- Config fields are fixed-size `char[256]` arrays — no heap allocation per field.

## Secrets

- Use `secure_zero()` (from `src/core/secure_zero.h`) to wipe passwords and keys — never plain `memset`, which the compiler can optimize away.
- API keys are stored encrypted in `nutshell.config` using `crypto_encrypt()`/`crypto_decrypt()`, same as profile passwords.

## Git commits

- Write the subject and body as if I authored them.
- Keep the attribution: end every commit with the
  `Co-Authored-By: Claude <model> <noreply@anthropic.com>` trailer the tooling
  adds, and every pull request body with its
  `🤖 Generated with [Claude Code](https://claude.com/claude-code)` footer.
- Name the models with their version, for quality control and performance
  analysis: add a `Models:` line before the trailer listing the full model id
  of every model that touched the change and what it did, e.g.
  `Models: claude-opus-5 (orchestration, review); claude-sonnet-5 (cases 50-70);
  claude-fable-5-1 (PR review)`. Pull request bodies carry the same line.