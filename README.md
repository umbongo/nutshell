<img src="images/nutshell_acorn_transparent.png" alt="Nutshell" width="80">

# Nutshell SSH

**Version**: v1.1.16 \
**Build Date**: 2026-09-10 \
**Author**: Thomas Sulkiewicz

## Overview

Nutshell is a lightweight, AI-enabled, native C SSH client for Windows. It pairs a full-featured terminal with a built-in AI assistant that can read your terminal output, suggest commands, and execute them over SSH — with your approval at every step.

Built entirely with the Win32 API, no external UI frameworks. Cross-compiled from Linux with MinGW-w64, the release binary is ~5.3 MB (UPX compressed).

## AI Chat Assistant

The standout feature: an integrated AI panel that sits alongside your terminal session. It sees what you see — a configurable window of recent terminal output — and can act on it.

- **Context-aware** — the AI reads a configurable window of your live terminal output (1,000 lines by default, up to 50,000) and tailors responses to what's happening on screen
- **Command execution** — suggested commands land in an approval card (checkboxes, a risk tag per command, `Deny all` / `Run N selected`) that never blocks the input; keep chatting and every reply that suggests commands gets its own pending card, up to eight per session
- **Streaming responses** — real-time token streaming, with a collapsible "Thinking" disclosure for chain-of-thought / reasoning
- **Multi-provider** — Anthropic (default), OpenAI, Gemini, Moonshot, DeepSeek, or any OpenAI-compatible endpoint
- **Per-session context** — attach notes to each server profile (e.g. "production database — read-only") that guide the AI's behaviour
- **Safety controls** — a Read-only / Read + write mode switch and an Auto approve level (off / safe only / safe + unknown / safe + write / safe + unknown + write / all) sit directly above the input, next to a context-usage meter
- **Guided empty states** — the panel always opens: it shows starter suggestions with nothing to ask yet, a button straight to Settings when no API key is set, and a button to the Session Manager when no session is connected

## Pre-built Binary

A ready-to-run Windows executable is available at `build/win/nutshell.exe` — no compilation needed.

## Features

- AI chat assistant with terminal context, pending approval cards for command execution (never block the input, up to eight per session), streaming, and a Thinking disclosure for reasoning
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
- 1,804 unit tests, zero lint warnings

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
| **Platform** | Which CLI this host speaks, used to classify command safety. **Auto-detect** (the default) reads the login banner and prompt; pin it explicitly to Linux/Unix, Cisco IOS/IOS-XE, Cisco NX-OS, Cisco ASA, HP ProCurve, HPE Comware, Aruba OS-CX, ArubaOS, PAN-OS, Junos, FortiOS, VyOS or RouterOS when you'd rather not rely on detection |
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
- **Render AI markdown** — format AI replies as markdown; turn off to see raw text. Headings, list/blockquote markers, and table header text pick up the theme's link hue, and code (inline and fenced) picks up the theme's info hue, so the structure of a reply reads at a glance
- **Auto approve for new sessions** — the starting Auto approve level for each new session: Off / Safe only / Safe + unknown / Safe + write / Safe + unknown + write / All (default: Off). The levels are a *set* of permitted categories rather than a sliding scale — **Safe + write** deliberately excludes unknown commands, so pick **Safe + unknown + write** if you want both. Anything above safe still needs Read + write, and the status line in the AI panel can change the level per session from there.

#### AI Assistant > Web Access
- **Search engine** — None, DuckDuckGo (API), DuckDuckGo (HTML), or Custom
- **Search URL** — shown only for the Custom search engine
- **Max results** — search results returned to the AI per query (1 to 20)
- **Permit web fetch** — allow the AI to fetch arbitrary URLs as a tool call

### AI Chat Assistant

Click the **AI** button in the tab strip (or press **Ctrl+Space**, or use View > AI Assist) to open the panel. The button is green when an API key is configured, grey otherwise — either way the panel opens; it just shows one of the [empty states](#empty-no-key-and-no-session-states) below when there's nothing to display yet.

The AI assistant can see the recent history of your terminal output (1,000 lines by default, configurable up to 50,000) and execute commands over SSH. Each tab maintains its own independent conversation history.

#### Header

- The session name, in the title font
- A **model chip** naming the active model
- Three icon buttons, each with a tooltip: **New chat** (clear the conversation and start fresh), **Save chat** (export the conversation as a plain text file), and **Undock/Dock** (pop the panel into its own window, or dock it back into the main frame)

#### Status Line

A line above the input box, always visible once a session is connected:

- **Mode switch** — a two-segment control, **Read-only** / **Read + write**. Read-only restricts the AI to commands that are *provably* read-only (`ls`, `cat`, `pwd`, etc.) — anything the classifier can't vouch for, including commands it doesn't recognise, is held back; **Read + write** is tinted with a warning colour whenever it's selected, so a permissive session stays visible at a glance
- **Auto approve** — click to cycle **off** → **safe only** → **safe + unknown** → **safe + write** → **safe + unknown + write** → **all** → **off**. A new session starts from the **Auto approve for new sessions** level set in Settings under AI Assistant > Behaviour; clicking here only changes the current session
- **Context meter** — a small bar plus `used / limit` numbers showing how much of the model's context window the conversation is using; hover it for the exact token counts

#### Sending Messages

- Type in the input box at the bottom
- **Enter** sends the message
- **Shift+Enter** inserts a newline (for multi-line messages)
- **Ctrl+V** with an image on the clipboard attaches a screenshot (sent as base64 PNG to multimodal-capable providers)
- Click **Send** (or press Enter) to submit; while a reply is streaming the button becomes **Stop**

#### Command Execution

When the AI suggests commands, they land in an approval card in the chat thread:

- **Header** — "N commands · M held", plus the reason (e.g. "Permit write is off") when any are held back
- **Rows** — a checkbox, the command text, and a risk tag (**SAFE** / **UNKNOWN** / **WRITE** / **CRITICAL**). Held rows show a disabled checkbox and dimmed text. Pending safe commands start checked
- **Actions** — **Deny all** rejects every row; **Run N selected** executes the checked rows (disabled when nothing is checked)

A card never blocks the input — keep chatting, ask something else, or just scroll past it, and it stays right there, pending, for as long as the session lasts. Every reply that suggests commands gets its own card, so more than one can be pending at once; acting on one (or letting its approved commands finish running) never touches any other. Sending is only held up while a reply is actually streaming in or the dispatcher is mid-run sending an already-approved batch.

With session Auto approve on, safe rows are approved and start running the moment they arrive, same as before — the card just shows them as executing instead of waiting for a click. Approved commands are sent one at a time: each one waits for the terminal to return to a shell prompt before the next is typed, so a command that stops at a password or confirmation prompt simply holds the queue instead of racing ahead. Once the last command's prompt has returned, the AI automatically reads the updated terminal output and continues the conversation, reporting results or running additional commands as needed; if another card's commands are waiting to run, they start the moment that continuation finishes.

#### How Commands Are Classified

Every command the AI proposes is classified before it can run, and the result drives both the
risk tag on the card and whether Auto approve may run it unattended.

| Category | Meaning | Examples |
|----------|---------|----------|
| **SAFE** | Provably read-only — it matched an explicit rule saying so | `ls -la`, `show running-config`, `systemctl status nginx`, `apt list`, `kubectl get pods` |
| **UNKNOWN** | No rule claimed it. It might read, it might wipe the disk — the classifier isn't guessing | anything not in the tables: a local script, an unusual tool, a vendor command Nutshell hasn't been taught |
| **WRITE** | Changes state, recoverable with another command | `mv`, `apt install`, `configure terminal`, `docker restart` |
| **CRITICAL** | Outage, data loss or lock-out | `rm -rf`, `reload`, `write erase`, `commit`, `no username admin`, `kubectl drain` |

**UNKNOWN is a real answer, not a fallback.** "I don't recognise this" is a different claim from
"this writes", and it is the honest one for a command no rule matched. It is gated like a write:
under **Read-only** an unrecognised command is held back, because nothing proved it safe.

**Classification is per-device.** A profile's **Platform** decides which ruleset applies, since
the same word means different things on different systems — `reload` reboots a Cisco switch,
`commit` applies a firewall policy on PAN-OS and Junos, `undo` removes configuration on Comware.
Ten CLI families are covered: Linux/Unix, Cisco IOS/IOS-XE, NX-OS and ASA, HP ProCurve and HPE
Comware, Aruba OS-CX and ArubaOS, PAN-OS, Junos, FortiOS, VyOS and RouterOS.

With **Auto-detect** (the default) Nutshell reads the login banner and first prompt. It only
resolves on a positive signal and never guesses between families that share a prompt shape —
`hostname#` belongs to six of them, and `user@host>` to both Junos and PAN-OS. Until it resolves,
and for good if it never does, the session classifies with the Linux rules plus an overlay of
verbs that are destructive on some network CLI and aren't Linux commands at all (`reload`,
`erase`, `commit`, `rollback`, `undo`, `request system …`), so an unrecognised switch can't slip
a reboot through as a safe command. The overlay only ever raises a command's category.

**A pipeline is classified segment by segment**, and Auto approve runs it only when *every*
segment is permitted. `unknown-thing | tee /etc/hosts` is both unknown and write, so a
"safe + write" session leaves it for you to approve by hand, even though the card shows it as
WRITE — the worst single category.

#### Thought Process

When a reply carries reasoning, a **Thinking** disclosure appears above the reply text: a chevron, the word "Thinking", and a dimmed summary (`· 240 words` once finished, `· streaming…` while still arriving). Click anywhere on the row to expand or collapse it. It opens automatically while a reply is streaming so you can watch the reasoning live, then collapses back to the summary once the reply text starts — unless you've expanded it yourself, in which case it stays open for the rest of the session. Replies with no reasoning show no disclosure at all.

#### Empty, No-Key, and No-Session States

The panel always opens, even with nothing to show yet — there's no longer a dialog that blocks it:

| State | When | What it offers |
|-------|------|-----------------|
| **Ask about this session** | A connected session with an empty conversation | Three suggestion chips ("What's using disk?", "Why did that fail?", "Summarise the log") — click one to send it as a prompt |
| **Add an API key to use the assistant** | No AI API key configured | An **Open Settings** button that jumps straight to AI Assistant > Provider |
| **Connect to a session first** | No connected SSH session | An **Open Session Manager** button |

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
│   │   ├── ai_agentic.c/.h            #   Agentic loop state (iteration count, rate limits per message)
│   │   ├── ai_http.c/.h               #   AI HTTP interface
│   │   ├── ai_panel_layout.c/.h       #   AI panel geometry (header/thread/status line/composer, Thinking disclosure)
│   │   ├── ai_panel_states.c/.h       #   Copy for the panel's empty/no-key/no-session states
│   │   ├── ai_prompt.c/.h             #   Conversations, request building, response parsing
│   │   ├── ai_tool_web_fetch.c/.h     #   Web fetch tool (arbitrary URL fetch, as an AI tool call)
│   │   ├── ai_tool_web_search.c/.h    #   Web search tool (DuckDuckGo API/HTML or custom)
│   │   ├── ai_tools.c/.h              #   AI tool-call registry and dispatch
│   │   ├── app_font.c/.h              #   Font management and size snapping
│   │   ├── base64.c/.h                #   Base64 encoding (OpenSSL)
│   │   ├── chat_activity.c/.h         #   Activity indicator state machine
│   │   ├── chat_approval.c/.h         #   Per-batch command approval queue
│   │   ├── chat_msg.c/.h              #   Chat message list with secure wipe
│   │   ├── chat_thinking.c/.h         #   Thinking/reasoning display state
│   │   ├── cli_args.c/.h              #   Command-line flag parsing (--ui-demo, --theme, -sn/-h/-nc/-l/-v/-?)
│   │   ├── cmd_batch.c/.h             #   Set of pending command-approval batches per AI session (up to 8)
│   │   ├── cmd_classify.c/.h          #   Command safety classification (Linux, Cisco, HP, Aruba, PAN-OS, Junos, FortiOS, VyOS, RouterOS)
│   │   ├── cmd_detect.c/.h            #   Device-platform detection from the login banner and prompt
│   │   ├── connect_anim.c/.h          #   Connection animation dots
│   │   ├── display_buffer.c/.h        #   Display invalidation tracking
│   │   ├── edit_scroll.c/.h           #   Scroll math for text editors
│   │   ├── html_util.c/.h             #   HTML tag stripping for web-fetch/search tool results
│   │   ├── json_validate.c/.h         #   JSON string escaping for AI request bodies
│   │   ├── log_format.c/.h            #   Log filename formatting (strftime)
│   │   ├── logger.c/.h                #   File and stderr logging
│   │   ├── md_table.c/.h              #   Markdown table parsing/column layout (pure; src/ui/md_render.c paints them)
│   │   ├── ns_hover.c/.h              #   Hover/hot-state tracker for painted controls
│   │   ├── ns_layout.c/.h             #   Pure layout geometry (buttons, cards, approval card + hit-test)
│   │   ├── ns_motion.c/.h             #   Easing curves and animation progress (one timer, all panels)
│   │   ├── ns_scale.c/.h              #   The one DPI-scaling helper (round-half-up, base 96)
│   │   ├── ns_type.c/.h               #   Spacing grid, radii, strokes, type ramp, font-slot key
│   │   ├── paste_preview.c/.h         #   Paste preview with size constraints
│   │   ├── secure_zero.h              #   Volatile memset for secrets
│   │   ├── selection.c/.h             #   Text selection (pixel-to-cell)
│   │   ├── settings_layout.c/.h    #   Settings window layout (pages, panes, rows)
│   │   ├── shell_prompt.c/.h          #   Detects a shell waiting at a prompt (gates one-at-a-time dispatch)
│   │   ├── snap.c/.h                  #   Window grid snapping
│   │   ├── ssh_timeout.c/.h           #   Network-failure rail (link declared dead after a socket-silence threshold)
│   │   ├── stick_scroll.c/.h          #   Stick-to-bottom / smart-scroll decision logic for the chat list and terminal
│   │   ├── string_utils.c/.h          #   String helpers, ANSI stripping, UTF-8
│   │   ├── tab_manager.c/.h           #   Tab data model
│   │   ├── term_extract.c/.h          #   Terminal text extraction
│   │   ├── theme.c/.h                 #   Color calculations (luminance, bg detection, per-role contrast)
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
│       ├── icons.c/.h                #   Built-in vector icon set (GDI+ paths; no icon-font dependency)
│       ├── resource.h                 #   Version macros and resource IDs
│       ├── themed_button.h           #   Themed button widget (paints via ns_draw_button)
│       ├── custom_scrollbar.h        #   Custom scrollbar widget
│       ├── dpi_util.h                #   DPI-aware layout helpers
│       ├── ns_draw.c/.h              #   The one drawing module (grown from ui_draw): chips, cards, buttons, focus rings
│       ├── ns_font.c/.h              #   Font cache keyed on (role, dpi, face); replaces per-call CreateFont
│       ├── ns_tokens.h               #   Read-only accessor for window.c's resolved ThemeTokens
│       ├── ns_reduced_motion.h       #   SPI_GETCLIENTAREAANIMATION reduced-motion flag
│       ├── redraw_log.h              #   Optional REDRAW_DEBUG file logging helper
│       ├── ai_dock.h, ai_chat_testable.h, menubar_line.h, ui.h
│       ├── nutshell.rc, resource.rc   #   Windows resource scripts
│       └── fonts/                     #   Embedded Inter Regular + Bold TTF
├── tests/                              # 1,804 unit tests across 76 test files
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
the full design, and run `nutshell.exe --ui-demo=all` (or any of the nine
individual states) to preview every themed panel without a live SSH session
— the same states the integration suite's `ui_gallery` case screenshots
across all four themes.
