# Agents Guide — Lessons Learned and Tips

This file captures hard-won lessons from developing this codebase. If you are an AI assistant (or human contributor), read this before making changes.

## Build System

- **Two compilers**: `x86_64-w64-mingw32-gcc` for the Windows cross-compile (`make`), native `gcc` for tests (`make test`). Code must compile clean under both with `-Werror`.
- **vcpkg include order matters**: `VCPKG_INC` must come before `-Isrc/term` so the real `libssh2.h` (with macros) overrides the local test stub.
- **Linker order matters**: vcpkg libs (`-lssh2 -lssl -lcrypto -lzlib`) must come before Windows system libs (`-lws2_32 -lgdi32 ...`).
- **`NON_TEST_SRCS`**: Files in `src/ui/` are excluded from test builds. If you add portable logic that needs testing, put it in `src/core/`, not `src/ui/`.

## Two config.h Files

There are two copies of `config.h`: `include/config.h` (used by Windows cross-compile) and `src/config/config.h` (used by native test builds). When modifying the `Settings` struct, **both must be updated in sync** or one build will fail with missing-member errors.

## Common Cross-Compile Pitfalls

- **winsock2.h before windows.h**: Any file that transitively includes `windows.h` (e.g., via `ssh_session.h`) and also needs winsock must `#include <winsock2.h>` first, or you get redefinition errors.
- **`_snwprintf` is MSVC-only**: Use `swprintf` (ISO C) instead for MinGW compatibility.
- **`-Wshadow` is strict**: Local variables must not shadow function parameters. Common trap: naming a local `msg` inside a WndProc that has `UINT msg` as a parameter.
- **`-Wconversion` catches size_t/int mismatches**: When calling functions that take `size_t`, cast explicitly (e.g., `(size_t)strlen(cmd)` not `(int)strlen(cmd)`).
- **Missing `#include <stdio.h>`**: If a `.c` file uses `snprintf` but only includes domain headers, MinGW will error on implicit declaration. Always include `<stdio.h>` explicitly.

## Terminal Buffer

- `TermRow.len` tracks actual written content width. `TermRow.width` is the allocated capacity but is **not initialized** when rows are allocated with `xmalloc`. Always use `row->len` for content boundaries.
- When extracting terminal text, skip trailing empty rows to avoid spurious blank lines. Use a two-pass approach: first find last non-empty row, then extract.

## AI Integration Architecture

The AI feature is split into testable core and Win32-only UI:

| Layer | Files | Testable on Linux? |
|-------|-------|--------------------|
| Terminal text extraction | `src/core/term_extract.{c,h}` | Yes |
| Prompt building / JSON | `src/core/ai_prompt.{c,h}` | Yes |
| HTTP response cleanup | `src/core/ai_http.{c,h}` | Yes |
| WinHTTP client | `src/ui/ai_http_win.c` | No (Win32) |
| Chat window UI | `src/ui/ai_chat.{c,h}` | No (Win32) |
| Settings UI fields | `src/ui/settings.c` | No (Win32) |
| Tab bar AI button | `src/ui/tabs.{c,h}` | No (Win32) |

- The AI uses OpenAI-compatible chat completion API. DeepSeek is the default provider.
- Conversations are managed via `AiConversation` struct (fixed-size message array).
- Terminal context (last 50 rows) is injected into the system prompt on each user message.
- Commands are marked with `[EXEC]cmd[/EXEC]` in AI responses and require user confirmation via MessageBox before execution.
- API keys are encrypted at rest using the same AES-256-GCM scheme as SSH passwords.

## UI Theming (Onyx Synapse)

- All UI chrome is themed via `ThemeColors` from `src/core/ui_theme.{c,h}`. 4 themes: Onyx Synapse (dark), Onyx Light, Sage & Sand (dark), Moss & Mist (light).
- Theme is selected by name in config (`colour_scheme` field), looked up via `ui_theme_find()` + `ui_theme_get()`.
- Use `WM_CTLCOLORDLG`, `WM_CTLCOLORSTATIC`, `WM_CTLCOLOREDIT`, `WM_CTLCOLORLISTBOX` to paint dialog/control backgrounds and text. Each dialog stores its own `HBRUSH` handles, freed in `WM_DESTROY`.
- Buttons use `BS_OWNERDRAW` + `WM_DRAWITEM` with shared `draw_themed_button()` from `src/ui/themed_button.h`. Primary buttons (Save, Connect, Send) get accent colour; secondary buttons get bg_secondary.
- For resource-based dialogs (session manager), set `BS_OWNERDRAW` at runtime in `WM_INITDIALOG` by modifying the button's `GWL_STYLE`.
- Tab strip theming: `tabs_set_theme()` stores a `ThemeColors*` and uses it in WM_PAINT. Active tab gets a 3px accent bar at the bottom.
- Dialog background brush: set `wc.hbrBackground = NULL` in WNDCLASS for custom-painted windows; resource dialogs use `WM_CTLCOLORDLG` to return a custom brush.

## Testing Conventions

- TDD: write tests first, then implement.
- Test framework: custom `test_framework.h` with `TEST_BEGIN()`/`TEST_END()`, `ASSERT_EQ`, `ASSERT_STR_EQ`, `ASSERT_TRUE`, `ASSERT_NULL`, `ASSERT_NOT_NULL`.
- All test functions are declared and called in `tests/runner.c`.
- Tests run natively on Linux — Win32 API calls are stubbed or excluded.

## JSON Handling

- Use `json_parse()` + `json_obj_get()`/`json_obj_str()` for reading. Always `json_free()` the root.
- For building JSON output (e.g., AI request bodies), use `snprintf` with manual escaping via a `json_escape_str()` helper. There is no JSON builder library.
- Config fields are fixed-size `char[256]` arrays — no heap allocation per field.

## Secrets

- `config_profile_free()` zeroes passwords with `memset` before `free`. Follow the same pattern for API keys and any credential fields.
- API keys are stored encrypted in `nutshell.config` using `crypto_encrypt()`/`crypto_decrypt()`, same as profile passwords.
