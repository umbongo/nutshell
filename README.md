<img src="images/nutshell_acorn_transparent.png" alt="Nutshell" width="80">

# Nutshell SSH

**Version**: v1.0.92 \
**Build Date**: 2026-09-07 \
**Author**: Thomas Sulkiewicz

## Overview

Nutshell is a lightweight, AI-enabled, native C SSH client for Windows. It pairs a full-featured terminal with a built-in AI assistant that can read your terminal output, suggest commands, and execute them over SSH — with your approval at every step.

Built entirely with the Win32 API, no external UI frameworks. Cross-compiled from Linux with MinGW-w64, the release binary is ~5.3 MB (UPX compressed).

## AI Chat Assistant

The standout feature: an integrated AI panel that sits alongside your terminal session. It sees what you see — a configurable window of recent terminal output — and can act on it.

- **Context-aware** — the AI reads a configurable window of your live terminal output (1,000 lines by default, up to 50,000) and tailors responses to what's happening on screen
- **Command execution** — suggests commands that appear inline with Allow/Deny buttons; nothing runs without your approval
- **Streaming responses** — real-time token streaming with chain-of-thought / reasoning display
- **Multi-provider** — Anthropic (default), OpenAI, Gemini, Moonshot, DeepSeek, or any OpenAI-compatible endpoint
- **Per-session context** — attach notes to each server profile (e.g. "production database — read-only") that guide the AI's behaviour
- **Safety controls** — Permit Write toggle restricts the AI to read-only commands; Auto Approve for trusted workflows

## Pre-built Binary

A ready-to-run Windows executable is available at `build/win/nutshell.exe` — no compilation needed.

## Features

- AI chat assistant with terminal context, command execution, streaming, and reasoning display
- Multi-tab SSH sessions with owner-drawn tab strip (status dots, log indicator, close button)
- VT100/ANSI terminal emulator — 256-colour, truecolor, alt screen, scroll regions, app cursor keys, OSC title
- Password and SSH key authentication with passphrase prompt and retry
- AES-256-GCM password encryption at rest (PBKDF2-SHA256 derived key, OpenSSL)
- TOFU host key verification (first-connect dialog, mismatch warning)
- Dynamic PTY resize on window resize and zoom
- Paste confirmation dialog with configurable inter-line delay
- Session file logging with ANSI stripping and configurable strftime filenames
- 4 themed colour schemes with consistently themed tabs, dialogs, and buttons
- DPI-aware layout across all windows and dialogs
- 1,539 unit tests, zero lint warnings

---

## User Guide

### Getting Started

1. Place `nutshell.exe` anywhere on your Windows machine. Configuration is stored in `nutshell.config` in the same directory.
2. Launch the application. The session manager opens on demand via **Ctrl+T** or the **+** area in the tab strip. To have it open automatically at launch, enable **Open Session Manager at startup** in Settings (Connection > Startup) — off by default.
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
- **Page Up / Page Down** scroll a full page (screen height minus one line) at a time
- **Vertical scrollbar** on the right tracks the scrollback position (drag to seek)

#### Text Selection and Clipboard

- **Click and drag** on the terminal to select text
- **Ctrl+C** copies the current selection to the clipboard and clears it; with no selection, Ctrl+C sends SIGINT to the remote shell as usual. **Ctrl+Shift+C** always copies (never sent to the shell)
- **Ctrl+V** or **Shift+Insert** pastes from the clipboard; **Ctrl+Shift+V** always pastes
- **Confirm before pasting** (Settings > Terminal, on by default) shows a preview dialog before every paste, so you can review what's about to be sent
- **Paste delay**: an optional inter-line delay (0 to 5000 ms) can be set in Settings for servers that need time between lines

### Zoom

Zoom the terminal font in discrete steps: 6, 8, 10, 12, 14, 16, 18, 20 pt.

| Action | How |
|--------|-----|
| **Zoom in** | **Ctrl+=** or **Ctrl+Mouse Wheel Up** |
| **Zoom out** | **Ctrl+-** or **Ctrl+Mouse Wheel Down** |

Zoom changes trigger an automatic PTY resize so the remote shell adapts to the new column/row count.

### Settings

Open from **Edit > Settings**. Changes take effect immediately — no restart needed.

The window is laid out like PuTTY: pick a category on the left, and its
settings fill the panel on the right. The window is resizable, and a page
scrolls if you shrink it below the content.

| Group | Pages |
|-------|-------|
| **General** | Appearance, Terminal, Logging |
| **Connection** | SSH, Startup |
| **AI Assistant** | Provider, Behaviour, Web Access |
| | About |

#### General > Appearance
- **Colour scheme** — choose from 4 built-in themes:
  - **Onyx Synapse** (dark, default) — dark background with green accents
  - **Onyx Light** — light background variant
  - **Sage & Sand** — dark earthy tones
  - **Moss & Mist** — light pastel colours
- **Terminal font** — curated list of monospace fonts: Consolas (default), Cascadia Code, Cascadia Mono, Courier New, Inter, Lucida Console, Lucida Sans Typewriter, Fira Code, JetBrains Mono, Source Code Pro, Hack. Only fonts actually installed are offered.
- **Font size** — discrete sizes from 6 to 20 pt
- **AI assist font** — font for the AI chat panel; does not affect the terminal

#### General > Terminal
- **Scrollback lines** — 100 to 50,000 (default: 10,000)
- **Paste delay** — inter-line delay in milliseconds (0 to 5000)
- **Confirm before pasting** — show a preview dialog before every paste (default: on)

#### General > Logging
- **Log directory** — where log files are saved (default: same directory as the executable)
- **Log name format** — strftime format string for log filenames (e.g. `%Y-%m-%d_%H-%M-%S`)
- **Debug terminal log** — capture the raw terminal byte stream for escape-sequence debugging

Logging itself is started and stopped from **File > Start/Stop Logging**, not here.

#### Connection > SSH
- **Idle timeout** — disconnect after this many minutes with no user activity; 0 never disconnects. Keystrokes, mouse-wheel scrolling, tab switches and AI chat input all count as activity.

#### Connection > Startup
- **Open Session Manager at startup** — show the Session Manager as soon as Nutshell launches (default: off). Ignored when auto-connect fires, and suppressed by `-nc`.
- **Auto-connect at startup** — connect to a saved session as soon as Nutshell launches
- **Session** — which saved session to open. Command-line options override this; start with `-nc` to skip auto-connect once.

#### AI Assistant > Provider
- **Provider** — Anthropic (default), OpenAI, Gemini, Moonshot, DeepSeek, or Custom
- **API key** — encrypted at rest with AES-256-GCM (same encryption as saved passwords)
- **Model** — type one, or press the refresh button to fetch the provider's model list
- **Base URL** — shown only for the Custom provider, for self-hosted or alternative endpoints

#### AI Assistant > Behaviour
- **Max terminal lines** — how much of the terminal the assistant reads as context, from 1 to 50,000 lines (default: 1,000). Each line becomes part of the context sent with every message: a larger window gives the assistant more of your session to reason about, at a proportionate cost in tokens.
- **System instructions** — global instructions included in every AI conversation (per-session AI Notes take precedence)
- **Render AI markdown** — format AI replies as markdown; turn off to see raw text
- **Auto Approve also covers write/critical commands** — when on, session Auto Approve approves every command Permit Write allows, not just safe read-only ones (default: off)

#### AI Assistant > Web Access
- **Search engine** — None, DuckDuckGo (API), DuckDuckGo (HTML), or Custom
- **Search URL** — shown only for the Custom search engine
- **Max results** — search results returned to the AI per query (1 to 20)
- **Permit web fetch** — allow the AI to fetch arbitrary URLs as a tool call

### AI Chat Assistant

Click the **AI** button in the tab strip to open the chat panel. The button is green when an API key is configured, grey otherwise.

The AI assistant can see the recent history of your terminal output (1,000 lines by default, configurable up to 50,000) and execute commands over SSH. Each tab maintains its own independent conversation history.

#### Chat Window Controls

| Control | Function |
|---------|----------|
| **New Chat** | Clear the conversation and start fresh |
| **Permit Write** | Toggle read/write mode. **Green** = AI can execute any command. **Grey** = AI restricted to read-only commands (ls, cat, pwd, etc.) |
| **Auto Approve** | Click twice within 3 seconds to confirm activation. Once on, automatically approves safe (read-only) commands without prompting — write/critical commands still require a manual Allow, unless **Auto Approve also covers write/critical commands** is enabled in Settings (AI Assistant > Behaviour) |
| **Show Thinking** | Toggle display of AI reasoning/chain-of-thought |
| **Save** (disk icon) | Save the conversation as a plain text file |
| **Context bar** | Shows approximate token usage as a percentage of the model's context window |

#### Sending Messages

- Type in the input box at the bottom
- **Enter** sends the message
- **Shift+Enter** inserts a newline (for multi-line messages)
- **Ctrl+V** with an image on the clipboard attaches a screenshot (sent as base64 PNG to multimodal-capable providers)
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

- Log files are named `<Log Name Format>_<name>.log` under the configured Log Directory — `<name>` is the session's profile name, falling back to its host. Log Name Format is a strftime format string (default: `%Y-%m-%d_%H-%M-%S`, e.g. `2026-09-07_14-30-00_myserver.log`). An empty or invalid format falls back to the default.
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
| **Ctrl+C** | Copy selection to clipboard and clear it; sends SIGINT if there is no selection |
| **Ctrl+Shift+C** | Copy selection to clipboard (no-op if none); never sent to the shell |
| **Ctrl+V** | Paste from clipboard |
| **Ctrl+Shift+V** | Paste from clipboard (always, same as Ctrl+V) |
| **Shift+Insert** | Paste from clipboard (alternative) |
| **Ctrl+=** | Zoom in |
| **Ctrl+-** | Zoom out |
| **Ctrl+Scroll** | Zoom in/out with mouse wheel |
| **Page Up** | Scroll up a full page through scrollback |
| **Page Down** | Scroll down a full page through scrollback |

---

## Directory Structure

```
.
├── src/
│   ├── main.c                          # Entry point
│   ├── config/                         # Configuration & JSON
│   │   ├── config.h, profile.h         #   Settings and profile structs
│   │   ├── json_parser.c/.h            #   Recursive-descent JSON parser
│   │   ├── json_tokenizer.c/.h         #   JSON lexer with Unicode escapes
│   │   ├── loader.c                    #   Config file read/write (atomic save)
│   │   └── ssh_io.c/.h                 #   SSH I/O helpers
│   ├── core/                           # Portable logic (testable on Linux)
│   │   ├── ai_http.c/.h               #   AI HTTP interface
│   │   ├── ai_prompt.c/.h             #   Conversations, request building, response parsing
│   │   ├── app_font.c/.h              #   Font management and size snapping
│   │   ├── base64.c/.h                #   Base64 encoding (OpenSSL)
│   │   ├── chat_activity.c/.h         #   Activity indicator state machine
│   │   ├── chat_approval.c/.h         #   Command approval queue
│   │   ├── chat_msg.c/.h              #   Chat message list with secure wipe
│   │   ├── chat_thinking.c/.h         #   Thinking/reasoning display state
│   │   ├── cmd_classify.c/.h          #   Command safety classification (Linux, Cisco, Aruba, PAN-OS)
│   │   ├── connect_anim.c/.h          #   Connection animation dots
│   │   ├── display_buffer.c/.h        #   Display invalidation tracking
│   │   ├── edit_scroll.c/.h           #   Scroll math for text editors
│   │   ├── log_format.c/.h            #   Log filename formatting (strftime)
│   │   ├── logger.c/.h                #   File and stderr logging
│   │   ├── ns_hover.c/.h              #   Hover/hot-state tracker for painted controls
│   │   ├── ns_layout.c/.h             #   Pure layout geometry (buttons, cards, approval card + hit-test)
│   │   ├── ns_motion.c/.h             #   Easing curves and animation progress (one timer, all panels)
│   │   ├── ns_scale.c/.h              #   The one DPI-scaling helper (round-half-up, base 96)
│   │   ├── ns_type.c/.h               #   Spacing grid, radii, strokes, type ramp, font-slot key
│   │   ├── paste_preview.c/.h         #   Paste preview with size constraints
│   │   ├── secure_zero.h              #   Volatile memset for secrets
│   │   ├── selection.c/.h             #   Text selection (pixel-to-cell)
│   │   ├── settings_layout.c/.h    #   Settings window layout (pages, panes, rows)
│   │   ├── snap.c/.h                  #   Window grid snapping
│   │   ├── string_utils.c/.h          #   String helpers, ANSI stripping, UTF-8
│   │   ├── tab_manager.c/.h           #   Tab data model
│   │   ├── term_extract.c/.h          #   Terminal text extraction
│   │   ├── theme.c/.h                 #   Color calculations (luminance, bg detection)
│   │   ├── tooltip.c/.h               #   Connection tooltip formatting
│   │   ├── ui_demo.c/.h               #   Deterministic demo-state builder for --ui-demo and the gallery
│   │   ├── ui_theme.c/.h              #   Theme system (4 schemes) + resolved design tokens (ThemeTokens)
│   │   ├── vector.c/.h                #   Dynamic array
│   │   ├── xmalloc.c/.h              #   Aborting allocator wrappers
│   │   └── zoom.c/.h                  #   Zoom level calculations
│   ├── crypto/                         # Cryptography
│   │   └── crypto.c/.h                #   AES-256-GCM encrypt/decrypt (OpenSSL, PBKDF2)
│   ├── term/                           # Terminal emulator & SSH
│   │   ├── buffer.c                   #   Ring buffer management (scrollback, resize, reflow)
│   │   ├── parser.c                   #   VT100/ANSI escape sequence parser
│   │   ├── term.c/.h                  #   Terminal structs, init, dirty tracking
│   │   ├── ssh_session.c/.h           #   SSH session lifecycle (connect, auth, disconnect)
│   │   ├── ssh_channel.c/.h           #   SSH channel I/O with timeout
│   │   ├── ssh_pty.c/.h              #   PTY allocation and resize
│   │   └── knownhosts.c/.h           #   TOFU host key verification
│   └── ui/                             # Win32 UI (excluded from test builds)
│       ├── window.c                   #   Main window, menu, layout (108 KB)
│       ├── ai_chat.c/.h              #   AI chat panel, streaming, image paste (140 KB)
│       ├── ai_http_win.c             #   WinHTTP streaming implementation
│       ├── chat_listview.c/.h        #   Chat message rendering, sticky scroll
│       ├── md_render.c/.h, markdown.h #   Markdown rendering for AI responses
│       ├── session_manager.c/.h       #   Session manager dialog
│       ├── settings.c, settings_dlg.h #   Settings dialog (paged, resizable)
│       ├── tabs.c/.h                  #   Double-buffered tab strip widget
│       ├── renderer.c/.h             #   Terminal cell renderer
│       ├── help_guide.c/.h           #   Help guide dialog
│       ├── paste_dlg.c/.h            #   Paste confirmation dialog
│       ├── icon_font.h               #   Icon font detection with ASCII fallbacks
│       ├── resource.h                 #   Version macros and resource IDs
│       ├── themed_button.h           #   Themed button widget (paints via ns_draw_button)
│       ├── custom_scrollbar.h        #   Custom scrollbar widget
│       ├── dpi_util.h                #   DPI-aware layout helpers
│       ├── ns_draw.c/.h              #   The one drawing module (grown from ui_draw): chips, cards, buttons, focus rings
│       ├── ns_font.c/.h              #   Font cache keyed on (role, dpi, face); replaces per-call CreateFont
│       ├── ns_tokens.h               #   Read-only accessor for window.c's resolved ThemeTokens
│       ├── ns_reduced_motion.h       #   SPI_GETCLIENTAREAANIMATION reduced-motion flag
│       ├── ai_dock.h, ai_chat_testable.h, menubar_line.h, ui.h
│       ├── nutshell.rc, resource.rc   #   Windows resource scripts
│       └── fonts/                     #   Embedded Inter Regular + Bold TTF
├── tests/                              # 1,665 unit tests across 71 test files
│   └── stubs/libssh2.h                 #   libssh2 stub for test builds without the real library
├── build/win/                          # Build output (nutshell.exe)
├── images/                             # Application icon and assets
├── CLAUDE.md                           # AI assistant project conventions
├── LICENSE
└── Makefile                            # Cross-compile (MinGW) + native test builds
```

## Build Instructions

The Makefile supports two build hosts and picks the right one automatically.

### Windows host (MSYS2 MINGW64)

Install the toolchain and libraries once:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-libssh2 \
          mingw-w64-x86_64-openssl mingw-w64-x86_64-zlib mingw-w64-x86_64-upx
```

Then, from a MINGW64 shell (or any shell with `C:\msys64\mingw64\bin` on PATH):

```bash
mingw32-make clean && mingw32-make release
```

Use `mingw32-make`, not the MSYS `make` — the latter breaks gcc's temp-file handling. The build links libssh2, OpenSSL and zlib statically, so the exe has no MSYS2 DLL dependencies.

### Linux host (cross-compile)

-   GCC (MinGW-w64) — `x86_64-w64-mingw32-gcc` for Windows cross-compile, native `gcc` for tests
-   `g++-mingw-w64-x86-64` — required by vcpkg even for C-only builds
-   Make, `upx` (`sudo apt install upx`)
-   cppcheck (static analysis)
-   vcpkg with the custom `x64-mingw-gcc-static` triplet for MinGW-targeted OpenSSL and libssh2 (see `~/vcpkg/custom-triplets/`)

```bash
make clean && make release
```

### Targets

Always clean before building to ensure a full rebuild. Use plain `make` only if you need an uncompressed binary for debugging.

| Command | Purpose |
|---------|---------|
| `make clean && make release` | **Recommended** — optimised + UPX compressed (~5.3 MB) |
| `make` | Uncompressed build (~9.7 MB), useful for debugging |
| `make test` | Run unit tests natively on the build host |
| `make wintest` | Win32 icon-renderer tests (runs directly on Windows, under Wine on Linux) |
| `make lint` | Static analysis with cppcheck |
| `make clean` | Remove all build artefacts |

(On Windows substitute `mingw32-make` for `make`.)

### Memory Audit (Windows)

To run Dr. Memory on the Windows release build:

1.  Build the executable: `make`
2.  Transfer `build/win/nutshell.exe` to a Windows machine.
3.  Run with Dr. Memory:
    ```cmd
    drmemory.exe -- nutshell.exe
    ```

## Development Guidelines

1.  **No External UI Libs**: Raw Win32 API only.
2.  **Memory Management**: Strict `malloc`/`free` discipline. Use `xmalloc` from `src/core/` — it aborts on OOM so callers treat the return as unconditionally valid.
3.  **TDD**: Create a test file in `tests/` before writing code in `src/`.
4.  **Static Analysis**: Run `make lint` before committing. Zero warnings required. cppcheck enforces `constVariablePointer` — declare pointers as `const T *` whenever the pointee is not mutated.
5.  **No format-string vulnerabilities**: `log_write()` takes a pre-formatted `const char *`. Use `snprintf` at the call site.
6.  **String copying**: Always use `snprintf(dst, sizeof(dst), "%s", src)`. Never use `strcpy` or `strncpy`.
7.  **Secrets**: Use `secure_zero()` from `src/core/secure_zero.h` to wipe passwords and keys. Never use plain `memset` for sensitive data.

### Design system

Every colour, size, font, animation and hover state in `src/ui` comes from
one tested source in `src/core`/`src/ui`: `ns_tokens()` (resolved `ThemeTokens`)
for colour, `ns_scale`/`ns_type` for spacing and type, `ns_font` for fonts,
`ns_motion` for animation, `ns_hover` for hover state, and `ns_draw` for the
actual painting. Two native tests (`tests/test_ui_tokens.c`) gate this: a
`RGB(` literal outside `ns_draw.c`, or a local DPI-scale macro anywhere in
`src/ui`, fails `make test`. See
`docs/superpowers/specs/2026-09-07-design-system-foundation-design.md` for
the full design, and run `nutshell.exe --ui-demo=all` (or any of the seven
individual states) to preview every themed panel without a live SSH session
— the same states the integration suite's `ui_gallery` case screenshots
across all four themes.
