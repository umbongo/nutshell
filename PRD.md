# Product Requirements Document: Nutshell

## 1. Overview
Nutshell is a lightweight native C SSH client for Windows. The goal is to produce a lightweight, high-performance SSH client with a tabbed interface, mirroring the functionality of the original Go application but with a minimal memory footprint and zero runtime dependencies (other than the OS and essential crypto libraries).

## 2. Core Features

### 2.1. Session Management
-   **SSH Connection**: Establish secure shell connections to remote servers.
-   **Authentication**: Password and SSH private key authentication with passphrase prompt and retry.
-   **Password Encryption**: AES-256-GCM via OpenSSL; PBKDF2-SHA256 key derived from Windows MachineGuid. Auto-encrypts on save, decrypts on load. Plaintext passwords auto-migrated.
-   **Host Key Verification**: TOFU (Trust On First Use) via libssh2 known_hosts API. First-connect fingerprint dialog, mismatch warning with block.
-   **PTY Management**: Request and resize pseudo-terminals on window resize and zoom. Deduplicates identical resize requests.

### 2.2. Terminal Emulation
-   **VT100/ANSI Parsing**: Custom terminal state machine — CSI, OSC, SGR, cursor movement, line editing.
-   **256-Colour and Truecolor**: SGR 38;5;n / 48;5;n (256-colour) and 38;2;r;g;b / 48;2;r;g;b (truecolor). Full xterm 6×6×6 cube + grayscale ramp.
-   **Extended VT Sequences**: OSC 0/2 (window title), DECTCEM (cursor show/hide), alternate screen buffer (?1049), application cursor keys (?1), insert mode (4).
-   **Buffer Management**: Scrollback ring buffer, 10,000 lines default (configurable 100–50,000). Resize reflow respects actual content width.

### 2.3. User Interface
-   **Native Windows UI**: Win32 API directly (user32, gdi32, kernel32, comctl32). No GTK/Qt/Fyne.
-   **Tabbed Interface**: Owner-drawn tab strip with multi-session support, status indicators, close buttons, Ctrl+T/Ctrl+W.
-   **Zoom**: Ctrl+=/-, Ctrl+Scroll. Font snapped to discrete sizes (6–20 pt). Gutter-free fit with fallback. Persisted to settings.
-   **Paste Confirmation**: Dialog for pastes >64 bytes or containing newlines. Truncated preview, line count. Inter-line delay from settings.
-   **Session File Logging**: ANSI-stripped output to timestamped files. Configurable directory (default: exe directory) and strftime name format.
-   **Configuration**: JSON profiles and settings via `nutshell.config` in the executable's directory.

### 2.4. Visual Specification (match Go/Fyne version)

#### Window
-   **Initial size**: 1024 x 680 px, centred on screen.
-   **Title**: "Nutshell".
-   **Layout**: Header (toolbar + tab strip) → separator → content area (terminal or error).

#### Theme & Colours
-   **Default foreground**: #0C0C0C (near-black). **Default background**: #F2F2F2 (light gray).
-   **Auto light/dark**: UI theme chosen by BT.709 luminance (> 0.5 = light). Title bar set via `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)`.
-   **Palette** (user-selectable): Light Gray, Black, Dark Green, Bright Green, Dark Blue, Cyan Blue, Dark Red, Dark Yellow, Dark Magenta, Medium Gray, White, Bright Yellow, Bright Cyan, Bright Red.

#### Fonts
-   **Default**: Consolas 10 pt. **Range**: 6–20 pt (discrete sizes: 6, 8, 10, 12, 14, 16, 18, 20).
-   **Options**: Consolas, Courier New, Lucida Console, Cascadia Code, Cascadia Mono.
-   **Zoom**: Ctrl+=/-, Ctrl+ScrollUp/Down adjusts font size ±1 pt, persisted to settings. Gutter-free fit with fallback across full 6–72 range.

#### Tab Strip
-   **Layout**: `[+] [<] [tab1] [tab2] … [>] [⚙]` — horizontally scrollable.
-   **Active tab**: primary-colour background, contrasting text. **Inactive**: transparent, foreground text.
-   **Tab border**: 1 px stroke, foreground colour, 4 px corner radius.
-   **Per-tab indicators**: Status dot (12×H, yellow=connecting, green=connected, red=error, gray=idle) and logging button (12×H, "L", green=active, gray=inactive). Full inner tab height, equidistant 3 px gaps.
-   **Tooltip on hover**: Table format — Name, Host, User, Status (duration), Logging status. Footnote: `[L] = toggle session logging`.

#### Terminal Area
-   **Default PTY**: 80 × 24, xterm-256color.
-   **Scrollback**: 10,000 lines default (configurable 100–50,000).
-   **Monospace** rendering with the selected font.
-   **Resize**: PTY resized on window resize.

#### Session Manager Dialog
-   **Size**: ~705 × 300 px (470 × 185 dialog units). Two-column layout.
-   **Left**: Saved Sessions list (single-click selects, double-click connects), [New], [Edit], [Delete].
-   **Right**: Connection form — Name, Host, Port (default 22), Username, Auth type (Password/Key), Password field, Key file path with "..." browse button (`GetOpenFileNameA`), [Save], [Connect], [Cancel].

#### Settings Dialog
-   **Size**: 480 × 530 px, fixed.
-   **Fields**: Font, Font size, Foreground colour (swatch + ChooseColor), Background colour (swatch + ChooseColor), Paste delay (350 ms), Session logging toggle, Log directory, Log name format (with strftime tooltip on hover listing specifiers), Scrollback lines (10,000).
-   **Validation**: `settings_validate()` snaps font_size to nearest allowed discrete size (6–20), clamps scrollback [100–50,000], paste_delay [0–5,000].
-   **Buttons**: [Save] (primary), [Cancel]. Footer: copyright.

#### Error State
-   Connection/PTY errors displayed as multi-line message with [Reconnect] and [Close Tab] buttons.

## 3. Design Priorities

In order of precedence:
1.  **Security** — no buffer overflows, no format-string bugs, validated inputs at every boundary, secrets zeroed after use.
2.  **Safety** — crash-free operation; every allocation checked, every error path handled.
3.  **Performance** — fast startup, low CPU at idle, dirty-rect rendering, minimal allocations in hot paths.
4.  **File Size** — static link only what is needed; strip symbols in release builds; no bundled runtimes.
5.  **Visual Fidelity** — match the Go/Fyne version's layout and behaviour (see §2.4).

## 4. Technical Constraints & Architecture

### 4.1. Language & Standards
-   **Language**: C11 (ISO/IEC 9899:2011).
-   **Compiler**: GCC (MinGW-w64) or MSVC.

### 4.2. Dependencies
-   **Strict Limit**: Import as few external modules as possible.
-   **Crypto**: Link against `OpenSSL` (AES-256-GCM password encryption, PBKDF2) and `libssh2` (SSH protocol, known_hosts). Avoid high-level frameworks.
-   **UI**: Native Win32 API (user32.dll, gdi32.dll, kernel32.dll). No GTK/Qt/Fyne.

### 4.3. Development Methodology
-   **TDD**: Write tests before implementation.
-   **Custom Modules**: Prefer writing custom implementations for:
    -   Data Structures (Vectors, HashMaps).
    -   String Handling.
    -   Config Parsing.
    -   UI Widget abstraction.

### 4.4. Static Analysis & Memory Safety
-   **Linter**: `cppcheck` (`--enable=warning,style,performance,portability --std=c11`).
-   **Compiler Warnings**: `-Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion`.
-   **Runtime Sanitizers**: AddressSanitizer + UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`) enabled for all debug/test builds.
-   **Windows Validation**: Dr. Memory for final release testing on Windows.

## 5. Confirmed Design Decisions

Decisions locked in during implementation. Do not revisit without a clear reason.

### 5.1. Memory Allocation
-   **`xmalloc` / `xcalloc` / `xrealloc`** abort the process on OOM rather than returning NULL. All callers treat the return value as unconditionally valid.
-   OOM error is reported via `fputs` (not `fprintf`) to avoid any format-string processing when the heap may be corrupt.
-   Callers set freed pointers to NULL manually; ASan catches any dangling access.

### 5.2. Logger API
-   `log_write()` accepts a **pre-formatted `const char *`** string — no variadic format string. Callers use `snprintf` before logging. Rationale: eliminates format-string vulnerabilities at every log call site.
-   File open failure is **non-fatal** — falls back to stderr-only. The logger must never crash the application.
-   `log_close()` is idempotent (safe to call multiple times).
-   `localtime()` is used in Phase 1 (not thread-safe). To be replaced with `localtime_s` (Windows) / `localtime_r` (POSIX) in Phase 6.

### 5.3. Vector
-   Stores `void *` — ownership of pointed-to data is **not** managed by the vector.
-   `vec_free()` zeroes the struct, leaving it in the init-empty state so it can be reused without a second `vec_init()` call.
-   Initial backing capacity: 8 elements; doubles on overflow.
-   Out-of-bounds `vec_get()` returns NULL. Out-of-bounds `vec_set()` / `vec_remove()` are no-ops.

### 5.4. String Utilities
-   `str_cat()` uses `strncat` with an explicit `remaining` calculation; always null-terminates even on truncation.
-   `str_trim()` operates in-place with `memmove` for the leading-whitespace shift — no allocation.
-   `str_dup()` returns NULL for a NULL input (not an abort); callers that require a non-NULL result must check.

### 5.5. Test Framework
-   ASSERT macros set a **local `_tf_local` flag** (not `abort()`/`return`), so all failures within a test function are reported before the function returns.
-   Each test function returns `int` (0 = pass, 1 = fail). The runner records pass/fail counts.
-   Global counters (`_tf_total`, `_tf_passed`, `_tf_failed`) are defined once in `runner.c` and declared `extern` in `test_framework.h`.
-   Test runner (`tests/runner.c`) uses explicit forward declarations — no test-discovery magic.

### 5.6. Build System
-   `make` → cross-compiles core to `build/win/libnutshell_core.a` (Windows static lib). Will become `nutshell.exe` in Phase 5.
-   `make test` → compiles and runs the test suite natively on Linux with ASan + UBSan.
-   `make debug` → builds the test runner without running it (for use with `gdb`).
-   `make lint` → cppcheck with `--error-exitcode=1`; a warning is a build failure.

## 6. Milestones (high-level)

1.  **Foundation**: Build system, test runner, and core data structures.
2.  **Terminal Core**: ANSI parser and render buffer.
3.  **SSH Layer**: Network socket handling and SSH protocol integration.
4.  **UI Shell**: Main window, tabs, and rendering loop.
5.  **Integration**: Wiring the terminal output to the UI.

## 7. Testing Strategy
-   **Unit Tests**: Custom test harness (865 tests) for logic (parsing, config, data structures, tooltips, themes, paste, zoom, ANSI strip, settings validation, AI prompt, display buffer, edit scroll, tabs, fonts, and more).
-   **Integration Tests**: Mock SSH server for connection testing.
-   **Reference**: Use `../golang/` tests as logic references.
-   **Server Compatibility Matrix**: OpenSSH 8.x (Ubuntu 22.04), OpenSSH 9.x (Debian 12), Dropbear, Windows OpenSSH Server — both password auth and Ed25519/RSA-4096 key auth.
-   **Security**: Passwords and API keys encrypted at rest with AES-256-GCM. TOFU host key verification.

## 8. Future Scope
-   DPAPI for password encryption, thread safety improvements.
-   Dr. Memory audit on Windows release build.
-   Port to Linux (X11/Wayland) using platform-specific UI layers.
-   SFTP support.