# CLAUDE.md — Nutshell Project Instructions

## MANDATORY: Version Bump Before Every Build

**Before running `make` (any target that produces a binary), increment the patch version in BOTH files:**

1. `src/ui/resource.h` — update `APP_VERSION` string and `APP_VERSION_BINARY` macro (e.g., `"1.0.30"` -> `"1.0.31"` and `1,0,30,0` -> `1,0,31,0`)
2. `README.md` — update the `**Version**:` line

`nutshell.rc` reads `APP_VERSION` and `APP_VERSION_BINARY` from `resource.h` via `#include`, so it updates automatically. No exceptions. Every build gets a unique version number.

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

- **Planning, architecture, and troubleshooting**: use Opus.
- **Implementation**: use Sonnet sub-agents.
- **Context discipline**: only preserve sub-agent results and key learnings in the Opus context — discard intermediate details. The goal is to keep the Opus context small and efficient.
- Once implementation is complete, have Opus review Sonnet's work.

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

- **Never `RGB(` in `src/ui`, outside `ns_draw.c`.** Pull the colour from
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

## Terminal Buffer

- `TermRow.len` tracks actual written content width. Always use `row->len` for content boundaries, not `term->cols`.
- When extracting terminal text, skip trailing empty rows to avoid spurious blank lines. Use a two-pass approach: first find last non-empty row, then extract.

## JSON Handling

- Use `json_parse()` + `json_obj_get()`/`json_obj_str()` for reading. Always `json_free()` the root.
- For building JSON output (e.g., AI request bodies), use `snprintf` with manual escaping via a `json_escape_str()` helper. There is no JSON builder library.
- Config fields are fixed-size `char[256]` arrays — no heap allocation per field.

## Secrets

- Use `secure_zero()` (from `src/core/secure_zero.h`) to wipe passwords and keys — never plain `memset`, which the compiler can optimize away.
- API keys are stored encrypted in `nutshell.config` using `crypto_encrypt()`/`crypto_decrypt()`, same as profile passwords.

## Git commits

- Do not add "🤖 Generated with Claude Code" footer
- Do not add "Co-Authored-By: Claude <noreply@anthropic.com>" trailer
- Write commit messages as if I authored them