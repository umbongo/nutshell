<img src="images/nutshell_acorn_transparent.png" alt="Nutshell" width="80">

# Nutshell SSH

**Version**: v1.0.11 \
**Build Date**: 2026-03-28 \
**Author**: Thomas Sulkiewicz

## Overview

Nutshell is a lightweight, AI-enabled, native C SSH client for Windows. It pairs a full-featured terminal with a built-in AI assistant that can read your terminal output, suggest commands, and execute them over SSH — with your approval at every step.

Built entirely with the Win32 API, no external UI frameworks. Cross-compiled from Linux with MinGW-w64, the release binary is ~2.1 MB (UPX compressed).

## AI Chat Assistant

The standout feature: an integrated AI panel that sits alongside your terminal session. It sees what you see — the last 50 lines of terminal output — and can act on it.

- **Context-aware** — the AI reads the last 150 lines of your live terminal output and tailors responses to what's happening on screen
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
- 1,062 unit tests, zero lint warnings

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

Click the **AI** button in the tab strip to open the chat panel. The button is green when an API key is configured, grey otherwise.

The AI assistant can see the last 150 lines of your terminal output and execute commands over SSH. Each tab maintains its own independent conversation history.

#### Chat Window Controls

| Control | Function |
|---------|----------|
| **New Chat** | Clear the conversation and start fresh |
| **Permit Write** | Toggle read/write mode. **Green** = AI can execute any command. **Grey** = AI restricted to read-only commands (ls, cat, pwd, etc.) |
| **Auto Approve** | When enabled, automatically approves safe commands without prompting |
| **Show Thinking** | Toggle display of AI reasoning/chain-of-thought |
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
│   ├── core/       # Portable logic (testable on Linux)
│   ├── config/     # JSON tokenizer, parser, config loader
│   ├── crypto/     # AES-256-GCM encryption (OpenSSL)
│   ├── term/       # Terminal emulator + SSH (libssh2)
│   ├── ui/         # Win32 UI (excluded from test builds)
│   └── main.c
├── tests/          # Unit tests (TDD)
├── build/          # Build artefacts
├── images/         # Application icon and assets
├── LICENSE
├── CLAUDE.md       # AI assistant instructions and project conventions
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

Always clean before building to ensure a full rebuild:

```bash
make clean && make release
```

Requires `upx` (`sudo apt install upx`). Use plain `make` only if you need an uncompressed binary for debugging.

| Command | Purpose |
|---------|---------|
| `make clean && make release` | **Recommended** — optimised + UPX compressed (~2.1 MB) |
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

1.  **No External UI Libs**: Raw Win32 API only.
2.  **Memory Management**: Strict `malloc`/`free` discipline. Use `xmalloc` from `src/core/` — it aborts on OOM so callers treat the return as unconditionally valid.
3.  **TDD**: Create a test file in `tests/` before writing code in `src/`.
4.  **Static Analysis**: Run `make lint` before committing. Zero warnings required. cppcheck enforces `constVariablePointer` — declare pointers as `const T *` whenever the pointee is not mutated.
5.  **No format-string vulnerabilities**: `log_write()` takes a pre-formatted `const char *`. Use `snprintf` at the call site.
6.  **String copying**: Always use `snprintf(dst, sizeof(dst), "%s", src)`. Never use `strcpy` or `strncpy`.
7.  **Secrets**: Use `secure_zero()` from `src/core/secure_zero.h` to wipe passwords and keys. Never use plain `memset` for sensitive data.
