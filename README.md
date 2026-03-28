# Nutshell



**Version**: v1.0.01 \
**Build Date**: 2026-03-28 \
**Author**: Thomas Sulkiewicz 

## Overview
Nutshell is a lightweight, AI enabled, native C SSH client for Windows, focusing on performance, minimal dependencies, and native OS integration. Built entirely with the Win32 API — no external UI frameworks. Cross-compiled from Linux with MinGW-w64, the release binary is ~2.0 MB (UPX compressed).

## Pre-built Binary

A ready-to-run Windows executable is available at `build/win/nutshell.exe` — no compilation needed.



## Features

- Multi-tab SSH sessions with owner-drawn tab strip (status dots, log indicator, close button)
- VT100/ANSI terminal emulator — 256-colour, truecolor, alt screen, scroll regions, app cursor keys, OSC title
- Password and SSH key authentication with passphrase prompt and retry
- AES-256-GCM password encryption at rest (PBKDF2-SHA256 derived key, OpenSSL)
- TOFU host key verification (first-connect dialog, mismatch warning)
- Dynamic PTY resize on window resize and zoom
- Paste confirmation dialog with configurable inter-line delay
- Session file logging with ANSI stripping and configurable strftime filenames
- Onyx Synapse UI — 4 themed colour schemes with consistently themed tabs, dialogs, and buttons
- AI chat assistant — reads terminal context, executes commands, supports streaming and reasoning display
- DPI-aware layout across all windows and dialogs
- 1,026 unit tests, zero lint warnings

---

## User Guide

### Getting Started

1. Place `nutshell.exe` anywhere on your Windows machine. Configuration is stored in `nutshell.config` in the same directory.
2. Launch the application. The session manager opens automatically.
3. Create a session profile by entering a hostname, port, username, and authentication credentials, then click **Connect**.

### Connecting to a Server

Open the **Session Manager** with **Ctrl+T** or by clicking the **+** area in the tab strip.

| Field | Description |
|-------|-------------|
| **Name** | Optional friendly label (shown in the tab and tooltips) |
| **Host** | Hostname or IP address |
| **Port** | SSH port (default: 22) |
| **Username** | Login username |
| **Auth Type** | Password or SSH Key |
| **Password / Key Path** | Password for password auth, or path to private key file for key auth |
| **Passphrase** | Passphrase for encrypted SSH keys (shown when auth type is Key) |
| **AI Notes** | Per-session context notes sent to the AI assistant (optional) |

Saved profiles appear in the list on the left. **Double-click** a profile to connect immediately. Use **Save** to store changes, **Delete** to remove a profile.

On first connection to a new host, a **host key verification** dialog shows the server's fingerprint. Accept to save it; future connections verify against the stored key and warn if it changes.

### Working with Tabs

Each SSH connection runs in its own tab. The tab strip at the top of the window shows all open sessions.

| Action | How |
|--------|-----|
| **New tab** | **Ctrl+T** opens the session manager |
| **Close tab** | **Ctrl+W** or click the **x** button on the tab |
| **Switch tab** | Click the tab |
| **Tab tooltip** | Hover over a tab to see session name, user@host, connection status with elapsed time, and logging status |

Each tab shows a **status dot**:
- Grey = idle/disconnected
- Yellow = connecting
- Green = connected
- Red = connection lost

A **[L]** badge appears when session logging is active for that tab.

### Terminal

The terminal emulates a VT100/ANSI-compatible display. It supports:

- **16, 256, and truecolor** (24-bit RGB) rendering
- **Bold, dim, underline, blink, reverse video** text attributes
- **Alternate screen buffer** (used by vim, nano, less, htop, etc.)
- **Scroll regions** (DECSTBM — used by ncurses applications for smooth scrolling)
- **Application cursor keys** mode (programs like vim switch arrow key sequences)
- **OSC title** — programs can set the window/tab title via escape sequences
- **10,000-line scrollback** by default (configurable 100 to 50,000)

#### Scrolling

- **Mouse wheel** scrolls through scrollback history
- **Page Up / Page Down** scroll one page at a time
- **Vertical scrollbar** on the right tracks the scrollback position (drag to seek)

#### Text Selection and Clipboard

- **Click and drag** on the terminal to select text
- **Ctrl+V** or **Shift+Insert** pastes from the clipboard
- Pasting more than 64 characters triggers a **confirmation dialog** showing a preview. This prevents accidental large pastes.
- **Paste delay**: an optional inter-line delay (0 to 5000 ms) can be set in Settings for servers that need time between lines

### Zoom

Zoom the terminal font in discrete steps: 6, 8, 10, 12, 14, 16, 18, 20 pt.

| Action | How |
|--------|-----|
| **Zoom in** | **Ctrl+=** or **Ctrl+Mouse Wheel Up** |
| **Zoom out** | **Ctrl+-** or **Ctrl+Mouse Wheel Down** |

Zoom changes trigger an automatic PTY resize so the remote shell adapts to the new column/row count.

### Settings

Open from the menu or via the settings button. Changes take effect immediately — no restart needed.

#### Display
- **Font family** — curated list of monospace fonts: Consolas (default), Cascadia Code, Cascadia Mono, Courier New, Inter, Lucida Console, Lucida Sans Typewriter, Fira Code, JetBrains Mono, Source Code Pro, Hack
- **Font size** — discrete sizes from 6 to 20 pt
- **Colour scheme** — choose from 4 built-in themes:
  - **Onyx Synapse** (dark, default) — dark background with green accents
  - **Onyx Light** — light background variant
  - **Sage & Sand** — dark earthy tones
  - **Moss & Mist** — light pastel colours

#### Terminal
- **Scrollback lines** — 100 to 50,000 (default: 10,000)
- **Paste delay** — inter-line delay in milliseconds (0 to 5000)

#### Logging
- **Enable/disable** session file logging
- **Log directory** — where log files are saved (default: same directory as the executable)
- **Log format** — strftime format string for log filenames (e.g. `%Y-%m-%d_%H-%M-%S`)

#### AI
- **Provider** — Anthropic (default), OpenAI, Gemini, Moonshot, DeepSeek, or Custom
- **API key** — encrypted at rest with AES-256-GCM (same encryption as saved passwords)
- **Custom URL / Model** — for self-hosted or alternative API endpoints
- **System notes** — global instructions included in every AI conversation

### AI Chat Assistant

Click the **AI** button in the tab strip to open the chat window. The button is green when an API key is configured, grey otherwise.

The AI assistant can see the last 50 lines of your terminal output and execute commands over SSH. Each tab maintains its own independent conversation history.

#### Chat Window Controls

| Control | Function |
|---------|----------|
| **New Chat** | Clear the conversation and start fresh |
| **Permit Write** | Toggle read/write mode. **Green** = AI can execute any command. **Red** = AI restricted to read-only commands (ls, cat, pwd, etc.) |
| **Show Thinking** | Toggle display of AI reasoning/chain-of-thought (for models like DeepSeek Reasoner that provide it) |
| **Save** (disk icon) | Save the conversation as a plain text file |
| **Context bar** | Shows approximate token usage as a percentage of the model's context window |

#### Sending Messages

- Type in the input box at the bottom
- **Enter** sends the message
- **Shift+Enter** inserts a newline (for multi-line messages)
- Click **Send** (or press Enter) to submit

#### Command Execution

When the AI suggests commands, they appear **inline in the chat window** with Allow/Deny buttons. You can:

- **Allow** — execute all queued commands in sequence
- **Deny** — reject the commands

After commands execute, the AI automatically reads the updated terminal output and continues the conversation, reporting results or running additional commands as needed.

#### Per-Session Notes

Each saved session profile has an **AI Notes** field in the session manager. These notes are included in the AI's system prompt for that session, giving it context about the server (e.g. "This is the production database server, be cautious with write operations").

Global **System Notes** in Settings are included in every conversation across all sessions.

### Session Logging

When enabled in Settings, each connected session writes a log file with ANSI escape codes stripped (plain text only).

- Log files are named using the configured strftime format (default: `%Y-%m-%d_%H-%M-%S_hostname.log`)
- The **[L]** badge on a tab indicates active logging
- Logging status is also shown in tab tooltips

### Security

- **Passwords and API keys** are encrypted at rest in `nutshell.config` using AES-256-GCM with a PBKDF2-SHA256 derived key
- **Host key verification** follows a Trust-On-First-Use (TOFU) model. Known hosts are stored at `%APPDATA%\sshclient\known_hosts`. A mismatch triggers a warning dialog (possible man-in-the-middle)
- **SSH key passphrases** are cached in memory only for the duration of the session and securely zeroed on close

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| **Ctrl+T** | New session (open session manager) |
| **Ctrl+W** | Close active tab |
| **Ctrl+V** | Paste from clipboard |
| **Shift+Insert** | Paste from clipboard (alternative) |
| **Ctrl+=** | Zoom in |
| **Ctrl+-** | Zoom out |
| **Ctrl+Scroll** | Zoom in/out with mouse wheel |
| **Page Up** | Scroll up through scrollback |
| **Page Down** | Scroll down through scrollback |

---

## Directory Structure

```
.
├── src/
│   ├── core/       # Portable logic — xmalloc, vector, string_utils, logger, tab_manager,
│   │               #   theme, ui_theme, tooltip, snap, zoom, connect_anim, log_format,
│   │               #   ai_prompt, ai_http, term_extract, display_buffer, app_font,
│   │               #   selection, paste_preview, edit_scroll, chat_msg, chat_activity,
│   │               #   chat_approval, chat_thinking, cmd_classify
│   ├── config/     # JSON tokenizer, JSON parser, config loader, profile, ssh_io
│   ├── crypto/     # AES-256-GCM password encryption (OpenSSL)
│   ├── term/       # Terminal emulator (buffer, parser) + SSH (session, channel, PTY, knownhosts)
│   ├── ui/         # Win32 UI — renderer, tabs, window, session_manager, settings,
│   │               #   ai_chat, ai_dock, ai_http_win, chat_listview, help_guide,
│   │               #   paste_dlg, markdown, md_render, menubar_line, themed_button,
│   │               #   custom_scrollbar
│   └── main.c
├── tests/          # Unit tests (TDD — tests written before implementation)
├── build/          # Build artefacts (gitignored)
├── images/         # Application icon and assets
├── LICENSE
├── PRD.md              # Product Requirements
├── agents.md           # Lessons learned and tips for contributors
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
Always use `make release` for the distributable build — it compiles with size optimisations and compresses with UPX (~2.0 MB):

```bash
make release
```

Requires `upx` (`sudo apt install upx`). Use plain `make` only if you need an uncompressed binary for debugging.

| Command | Purpose |
|---------|---------|
| `make release` | **Recommended** — optimised + UPX compressed (~2.0 MB) |
| `make` | Uncompressed build (~6.7 MB), useful for debugging |
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
6.  **Cross-Reference**: The original Go reference implementation is no longer available. Rely on the C source and tests as the authoritative reference for intended behaviour.
7.  **Struct Packing**: Be mindful of struct padding and alignment when dealing with network protocols (SSH packets). Use `#pragma pack` if necessary.
8.  **JSON / Config API**: Use `json_parse()` -> `json_obj_get()` / `json_obj_str()` / `json_obj_num()` / `json_obj_bool()` to extract values. Always call `json_free()` on the root when done. Config fields are fixed-size `char[256]` arrays — no heap allocation per field. Load with `config_load()`, save with `config_save()`, and always call `config_free()` on the returned pointer.
9.  **Const correctness**: cppcheck enforces `constVariablePointer`. Declare local pointers as `const T *` if the pointee is not mutated. `json_obj_get()`, `json_obj_str()`, `json_obj_num()`, and `json_obj_bool()` all accept `const JsonNode *`, so callers can pass const pointers directly.
10. **Secrets hygiene**: `config_profile_free()` zeroes `Profile.password` with `memset` before calling `free`. Follow the same pattern for any other field that may hold credentials.
11. **Module layout**: SSH code lives in `src/term/` (ssh_session.c, ssh_channel.c, ssh_pty.c, knownhosts.c), not a separate `src/ssh/` directory. Crypto is in `src/crypto/`. Tab management logic is in `src/core/tab_manager.c`; the owner-drawn tab strip UI is in `src/ui/tabs.c`.
12. **libssh2 macros**: `libssh2_channel_open_session`, `libssh2_session_init`, and `libssh2_channel_write` are **macros** in the real libssh2 header. `src/term/libssh2.h` is a stub used only for test builds — the real header comes from vcpkg at `~/vcpkg/installed/x64-mingw-gcc-static/`.
13. **Resize reflow**: `TermRow.len` tracks actual written content width. When reflowing on resize, always loop to `row->len`, not `term->cols`, to avoid copying trailing empty cells that cause spurious wraps.
14. **Discrete font sizes**: Allowed sizes are `{6, 8, 10, 12, 14, 16, 18, 20}` — centralised in `src/core/app_font.h` (`APP_FONT_SIZES` array, `APP_FONT_NUM_SIZES`). Use `app_font_snap_size()` to snap any out-of-set value to the nearest allowed size. Default is 10 pt.
15. **Colour defaults**: Default terminal colours are `fg=#E0E0E0, bg=#121212` matching the "Onyx Synapse" colour scheme. `COLOR_DEFAULT` fg/bg mode means the renderer substitutes the configured scheme colours; hardcoded colour values in `buffer.c`/`parser.c` are only set for `COLOR_ANSI16`/`COLOR_256`/`COLOR_RGB` cells.
16. **UI theming**: All UI chrome (tabs, settings, session manager, AI chat) is themed via `ThemeColors` from `src/core/ui_theme.{c,h}`. Use `WM_CTLCOLORDLG`, `WM_CTLCOLORSTATIC`, `WM_CTLCOLOREDIT`, `WM_CTLCOLORLISTBOX` to paint dialog backgrounds and control colours. Buttons use `BS_OWNERDRAW` with `draw_themed_button()` from `themed_button.h`. The theme is looked up by name from config (`colour_scheme` field) via `ui_theme_find()` + `ui_theme_get()`.
17. **Scrollbar**: `update_scrollbar()` in `window.c` syncs a Win32 vertical scrollbar to the active terminal. Use `GetScrollInfo(SIF_TRACKPOS)` for `WM_VSCROLL` — never `HIWORD(wParam)`, which silently truncates to 16 bits and breaks scrollback > 65535 lines.
18. **AI integration architecture**: AI code is split into portable core (`src/core/ai_prompt.c`, `src/core/ai_http.c`, `src/core/term_extract.c`, `src/core/chat_msg.c`, `src/core/chat_activity.c`, `src/core/chat_approval.c`, `src/core/chat_thinking.c`, `src/core/cmd_classify.c`) and Win32 UI (`src/ui/ai_chat.c`, `src/ui/ai_http_win.c`, `src/ui/chat_listview.c`, `src/ui/md_render.c`). Core files are testable on Linux; UI files are excluded from test builds via the `NON_TEST_SRCS` pattern in the Makefile. The AI uses an OpenAI-compatible chat completion API (Anthropic default). Conversations use `AiConversation` struct with role-tagged messages. Commands are extracted via `[EXEC]cmd[/EXEC]` markers. The chat listview (`chat_listview.c`) provides owner-drawn virtual-scroll rendering of chat messages with DPI-aware layout and text selection support. The HTTP client uses WinHTTP on Windows.
19. **Config header**: `src/config/config.h` is the single config header used by both Windows cross-compile and native test builds.
20. **Config path caveat**: `nutshell.config` is loaded relative to CWD, which can change after `GetOpenFileNameA` file dialogs. The long-term fix is to resolve to an absolute path at startup using `get_exe_dir()`.
