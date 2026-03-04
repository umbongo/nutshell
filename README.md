# Nutshell v1.3

A lightweight native C SSH client for Windows, focusing on performance, minimal dependencies, and native OS integration.

## Features

- Multi-tab SSH sessions with owner-drawn tab strip (status dots, log indicator, ✕ close, Ctrl+T/Ctrl+W)
- VT100/ANSI terminal emulator — 256-colour, truecolor, alt screen, insert mode, app cursor keys, OSC title
- Password and SSH key authentication with passphrase prompt and retry
- AES-256-GCM password encryption in `config.json` (PBKDF2-SHA256 machine key, OpenSSL)
- TOFU host key verification via libssh2 known_hosts API (first-connect dialog, mismatch warning)
- PTY resize on window resize and zoom (Ctrl+=/-, Ctrl+Scroll; discrete sizes 6/8/10/12/14/16/18/20)
- Paste confirmation dialog with inter-line delay (configurable threshold and delay)
- Session file logging with ANSI strip, configurable directory (default: exe dir) and strftime name format
- Settings dialog: font/size from curated comboboxes, 10 built-in colour schemes with swatch preview, scrollback (10,000 default), paste delay, logging, log format tooltip
- Session manager with SSH key file browse button ("..."), double-click to connect
- Vertical scrollbar tracking scrollback position (Win64-safe, uses `GetScrollInfo` for full 32-bit range)
- Dynamic light/dark title bar (DWM, BT.709 luminance, applied on settings change)
- Hover tooltips on tabs (table format: name, host, user, status/duration, logging; [L] footnote)
- Error dialogs for connection/auth failures
- Foreground/background colours applied from settings immediately without restart

## Directory Structure

```
.
├── src/
│   ├── core/       # xmalloc, vector, string_utils, logger, tab_manager, theme, tooltip, snap, zoom, connect_anim, log_format
│   ├── config/     # JSON tokenizer, JSON parser, config loader, ssh_io
│   ├── crypto/     # AES-256-GCM password encryption (OpenSSL)
│   ├── term/       # Terminal emulator (buffer, parser) + SSH (session, channel, PTY, knownhosts)
│   ├── ui/         # Win32 UI — renderer, tabs, window, session manager, settings dialog
│   └── main.c
├── tests/          # Unit tests (TDD — tests written before implementation)
├── build/          # Build artefacts (gitignored)
├── PRD.md              # Product Requirements
├── TODO.md             # Phase completion log and outstanding items
├── vulnerabilities.md  # Security audit findings and recommended fixes
└── Makefile
```

## Build Instructions

### Prerequisites
-   GCC (MinGW-w64) — `x86_64-w64-mingw32-gcc` for Windows cross-compile, native `gcc` for tests
-   `g++-mingw-w64-x86-64` — required by vcpkg even for C-only builds
-   Make
-   cppcheck (static analysis)
-   vcpkg with the custom `x64-mingw-gcc-static` triplet for MinGW-targeted OpenSSL and libssh2 (see `~/vcpkg/custom-triplets/`)

### Building
Always use `make release` for the distributable build — it compiles with size optimisations and compresses with UPX (~1.5 MB):

```bash
make release
```

Requires `upx` (`sudo apt install upx`). Use plain `make` only if you need an uncompressed binary for debugging.

| Command | Purpose |
|---------|---------|
| `make release` | **Recommended** — optimised + UPX compressed (~1.5 MB) |
| `make` | Uncompressed build (~5.4 MB), useful for debugging |
| `make test` | Run unit tests (native Linux) |
| `make lint` | Static analysis with cppcheck |
| `make debug` | Build with AddressSanitizer + UndefinedBehaviorSanitizer |
| `make clean` | Remove all build artefacts |

### Memory Audit (Windows)

To run Dr. Memory on the Windows release build:

1.  Build the executable: `make`
2.  Transfer `build/win/nutshell.exe` to a Windows machine.
3.  Run with Dr. Memory:
    ```cmd
    drmemory.exe -- nutshell.exe
    ```

## Development Guidelines

1.  **No External UI Libs**: We use raw Win32 API.
2.  **Memory Management**: strict `malloc`/`free` discipline. Use the custom `xmalloc` wrapper in `src/core`. `xmalloc` aborts on OOM — callers treat the return value as unconditionally valid.
3.  **TDD**: Create a test file in `tests/` before writing code in `src/`.
4.  **Static Analysis**: Run `make lint` before committing. All code must pass with zero warnings. cppcheck enforces const-correctness via `constVariablePointer` — declare pointers as `const T *` whenever the pointee is not mutated.
5.  **No format-string vulnerabilities**: `log_write()` and any new logging functions take a pre-formatted `const char *`. Use `snprintf` at the call site, not variadic format arguments inside library functions.
6.  **String copying**: Always use `snprintf(dst, sizeof(dst), "%s", src)` for fixed-size field copies. Never use bare `strcpy` or `strncpy`.

---

## Tips and Tricks for Bots

If you are an AI assistant helping with this codebase, please observe the following:

1.  **Context is King**: Always read the header files (`.h`) first to understand struct definitions before modifying implementation (`.c`) files.
2.  **Memory Safety**: C does not have garbage collection. Every `malloc` must have a corresponding `free`. When suggesting code, always double-check error paths (e.g., if a socket fails, is the buffer freed?). Use `make debug` to compile with AddressSanitizer and catch issues at runtime.
3.  **Win32 API Verbosity**: The Win32 API is verbose. When writing UI code, prefer creating helper functions for repetitive tasks like `CreateWindowEx` or `SendMessage`.
4.  **Test Harness**: We use a custom minimal test runner. It relies on macros like `ASSERT_EQ` and `ASSERT_TRUE`. Do not import `CUnit` or `GoogleTest`; use the provided `tests/test_framework.h`. Each test function uses `TEST_BEGIN()` / `TEST_END()` — failures are reported but do not abort the function, so all assertions in a test always run.
5.  **String Handling**: Do not use `strcat` or `strcpy`. Use `snprintf(dst, sizeof(dst), "%s", src)` for field copies, or the helpers in `src/core/string_utils.h` (`str_dup`, `str_cat`, `str_trim`).
6.  **Cross-Reference**: If you are unsure about logic, check `../golang/` for the reference Go implementation, but translate the *intent*, not the syntax. Go channels usually map to Windows Events or callback queues in C.
7.  **Struct Packing**: Be mindful of struct padding and alignment when dealing with network protocols (SSH packets). Use `#pragma pack` if necessary.
8.  **JSON / Config API**: Use `json_parse()` → `json_obj_get()` / `json_obj_str()` / `json_obj_num()` / `json_obj_bool()` to extract values. Always call `json_free()` on the root when done. Config fields are fixed-size `char[256]` arrays — no heap allocation per field. Load with `config_load()`, save with `config_save()`, and always call `config_free()` on the returned pointer.
9.  **Const correctness**: cppcheck enforces `constVariablePointer`. Declare local pointers as `const T *` if the pointee is not mutated. `json_obj_get()`, `json_obj_str()`, `json_obj_num()`, and `json_obj_bool()` all accept `const JsonNode *`, so callers can pass const pointers directly.
10. **Secrets hygiene**: `config_profile_free()` zeroes `Profile.password` with `memset` before calling `free`. Follow the same pattern for any other field that may hold credentials.
11. **Module layout**: SSH code lives in `src/term/` (ssh_session.c, ssh_channel.c, ssh_pty.c, knownhosts.c), not a separate `src/ssh/` directory. Crypto is in `src/crypto/`. Tab management logic is in `src/core/tab_manager.c`; the owner-drawn tab strip UI is in `src/ui/tabs.c`.
12. **libssh2 macros**: `libssh2_channel_open_session`, `libssh2_session_init`, and `libssh2_channel_write` are **macros** in the real libssh2 header. `src/term/libssh2.h` is a stub used only for test builds — the real header comes from vcpkg at `~/vcpkg/installed/x64-mingw-gcc-static/`.
13. **Resize reflow**: `TermRow.len` tracks actual written content width. When reflowing on resize, always loop to `row->len`, not `term->cols`, to avoid copying trailing empty cells that cause spurious wraps.
16. **Discrete font sizes**: Allowed sizes are `{6, 8, 10, 12, 14, 16, 18, 20}` — defined in three places that must stay in sync: `k_allowed_sizes[]` in `window.c`, `k_font_sizes[]` in `settings.c`, and `k_allowed_sizes[]` in `loader.c`. `settings_validate()` snaps any out-of-set value to the nearest allowed size (not a range clamp).
17. **Colour defaults**: Default terminal colours are white-on-black (`fg=#FFFFFF, bg=#000000`) matching the "Default" colour scheme in `settings.c`. `COLOR_DEFAULT` fg/bg mode means the renderer substitutes the configured scheme colours; hardcoded colour values in `buffer.c`/`parser.c` are only set for `COLOR_ANSI16`/`COLOR_256`/`COLOR_RGB` cells.
18. **Scrollbar**: `update_scrollbar()` in `window.c` syncs a Win32 vertical scrollbar to the active terminal. Use `GetScrollInfo(SIF_TRACKPOS)` for `WM_VSCROLL` — never `HIWORD(wParam)`, which silently truncates to 16 bits and breaks scrollback > 65535 lines.
14. **Config path caveat**: `config.json` is loaded relative to CWD, which can change after `GetOpenFileNameA` file dialogs. See `vulnerabilities.md` M-8 for details. The long-term fix is to resolve to an absolute path at startup using `get_exe_dir()`.
15. **Security**: See `vulnerabilities.md` for known security issues ranked by severity. The most critical are password encryption key derivation (C-1/C-2) and TOFU broken for non-RSA keys (H-4).