# To-Do List

## Phase 1: Foundation & Tooling ✓
- [x] Create `Makefile` with compile, clean, and `make test` targets (MinGW).
- [x] Add `make lint` target (cppcheck: `--enable=warning,style,performance,portability --std=c11`).
- [x] Add `make debug` target (ASan + UBSan: `-fsanitize=address,undefined`).
- [x] Configure GCC warning flags (`-Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion`).
- [x] Implement `tests/test_framework.h` (Minimal unit test runner with ASSERT_EQ, ASSERT_TRUE, test registration).
- [x] Implement `src/core/xmalloc.c` (malloc/realloc/free wrappers that abort on failure).
- [x] Implement `src/core/vector.c` (Dynamic array — init, push, get, free).
- [x] Implement `src/core/string_utils.c` (Safe string manipulation — snprintf wrappers, duplication, trim).
- [x] Implement `src/core/logger.c` (File and console logging).
- [x] Write tests for vector, string_utils, and logger.

## Phase 2: Configuration & State ✓
- [x] Define `Config` struct in header.
- [x] Implement JSON tokenizer (`src/config/json_tokenizer.c`).
- [x] Implement JSON parser (recursive descent, builds from tokenizer output).
- [x] Create `src/config/loader.c` to read `config.json` into Config struct.
- [x] Write tests for JSON parsing and config loading (reference `z:\golang\internal\config`).

## Phase 3: Terminal Emulation (The "Brain")
- [x] Define `Terminal` struct (grid, cursor position, attributes, mode flags).
- [x] Implement screen buffer (`src/term/buffer.c` — rows, cols, scrollback ring buffer).
- [x] Implement ANSI escape sequence parser (`src/term/parser.c` — CSI, OSC, SGR sequences).
- [x] Implement cursor movement and line editing (CUP, CUU/CUD/CUF/CUB, ED, EL).
- [x] Implement SGR attribute handling (colors, bold, underline, reset).
- [x] Write tests for ANSI parsing (colors, cursor moves, edge cases).

## Phase 4: SSH Networking
- [x] Link against `libssh2` and `openssl` in Makefile.
- [x] Implement SSH connection and authentication (`src/ssh/session.c` — password + key auth).
- [x] Implement channel open and data read/write (`src/ssh/channel.c`).
- [x] Implement PTY request and resize (`src/ssh/pty.c`).
- [x] Implement non-blocking socket I/O loop.

## Phase 5: User Interface (Win32)
- [x] Create main application window (`src/ui/window.c` — WndProc, message loop).
- [x] Implement terminal renderer (`src/ui/renderer.c` — GDI/Direct2D drawing of Terminal buffer).
- [x] Implement keyboard input handling (`WM_KEYDOWN` / `WM_CHAR` → SSH channel write).
- [x] Implement resize handling (`WM_SIZE` → buffer resize → PTY resize).
- [x] Implement tab control (`src/ui/tabs.c` — owner-drawn tab strip, tab create/close/switch).

## Phase 6: Polish
- [x] Add application icon and version resource.
- [ ] Optimize rendering (dirty rect tracking, partial redraws).
- [ ] Run Dr. Memory on Windows release build for final memory audit.

## Backlog
- [ ] SFTP File Browser.
- [ ] Port to Linux (X11).

---

## Phase 7: Security — Password Encryption (P1-01) 🔴 CRITICAL

> **TDD rule:** Write all tests first. Run them and confirm they fail. Then implement. Re-run until green. If a test fails after implementation, verify the test is still correct before changing it — fix or remove only invalid tests.

### 7.1 Write tests — `tests/test_crypto.c`
- [ ] **Positive — round-trip**: encrypt a known plaintext password; verify ciphertext differs from plaintext; decrypt ciphertext; verify decrypted value matches original.
- [ ] **Positive — multiple values**: repeat round-trip for empty string `""`, single char `"x"`, 128-char password, password containing Unicode (`"pässwörð"`), and a password containing null-adjacent bytes.
- [ ] **Positive — deterministic key derivation**: call `crypto_derive_key()` twice on the same machine; verify both calls return identical key material.
- [ ] **Positive — migration detection**: pass a known plaintext password string (no base64 encryption prefix) to `crypto_is_encrypted()`; verify it returns `false`. Pass a valid encrypted blob; verify it returns `true`.
- [ ] **Negative — wrong key**: encrypt with the real key; attempt decryption with a zeroed-out key buffer; verify the function returns an error code and outputs no plaintext.
- [ ] **Negative — truncated ciphertext**: encrypt a password; strip the last 4 bytes from the ciphertext; attempt decryption; verify graceful failure (error code, no crash, no partial output).
- [ ] **Negative — tampered ciphertext**: encrypt a password; flip one byte in the middle of the ciphertext; verify decryption fails with an authentication error (GCM tag mismatch).
- [ ] **Negative — null inputs**: call `crypto_encrypt(NULL, ...)` and `crypto_decrypt(NULL, ...)`; verify both return an error code and do not crash.
- [ ] **Corner — empty password**: encrypt `""`; verify ciphertext is non-empty (nonce + tag overhead); decrypt and verify result is `""`.
- [ ] **Corner — max-length password**: encrypt a 4096-byte string; verify successful round-trip.
- [ ] **Corner — nonce uniqueness**: encrypt the same plaintext 1000 times; collect all nonces; verify no two nonces are identical (collision would indicate a broken RNG).
- [ ] **Corner — output buffer sizing**: verify `crypto_encrypt()` correctly reports the required output buffer size before writing, and returns an error if the caller supplies a too-small buffer.

### 7.2 Implement `src/crypto/crypto.c` + `src/crypto/crypto.h`
- [ ] Add `src/crypto/` directory and `crypto.h` declaring: `crypto_derive_key()`, `crypto_encrypt()`, `crypto_decrypt()`, `crypto_is_encrypted()`.
- [ ] Implement `crypto_derive_key()`: read `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Cryptography\MachineGuid` from the Windows registry; feed it into PBKDF2-SHA256 (via Windows CNG `BCryptDeriveKeyPBKDF2`) with a fixed application salt and 100,000 iterations; output a 32-byte key.
- [ ] Implement `crypto_encrypt()`: generate a 12-byte random nonce via `BCryptGenRandom`; encrypt with AES-256-GCM (`BCryptEncrypt`); output format: `base64(nonce || ciphertext || 16-byte GCM tag)`; prepend a version prefix (`$aes256gcm$v1$`) so `crypto_is_encrypted()` can distinguish from plaintext.
- [ ] Implement `crypto_decrypt()`: strip prefix; base64-decode; split nonce, ciphertext, and tag; call `BCryptDecrypt`; verify GCM tag; write plaintext to caller-supplied buffer.
- [ ] Implement `crypto_is_encrypted()`: returns `true` if string starts with `$aes256gcm$v1$`.
- [ ] Update `src/config/loader.c`: on config load, for each profile where `password` does not pass `crypto_is_encrypted()`, re-encrypt and rewrite the JSON file (migration path).
- [ ] Update `src/config/loader.c`: on config load for encrypted passwords, call `crypto_decrypt()` before storing in the in-memory `Config` struct; on save, always call `crypto_encrypt()`.
- [ ] Add `src/crypto/crypto.c` to `Makefile` compile targets.

### 7.3 Run tests and validate
- [ ] Run `make test` — all `test_crypto` cases must pass.
- [ ] Run `make debug` (ASan + UBSan build) and re-run tests — zero errors.
- [ ] Run `make lint` — zero cppcheck warnings in `src/crypto/`.
- [ ] For any failing test: confirm the test logic is still correct against the spec above. If the test itself is wrong, correct or remove it and document why. Fix code for all remaining failures.
- [ ] Manual smoke-test: save a profile with a password; open `config.json` in a text editor; confirm the password field contains the `$aes256gcm$v1$` prefix and no readable plaintext.

---

## Phase 8: Security — TOFU Host Key Verification (P1-02) 🔴 CRITICAL

### 8.1 Write tests — `tests/test_knownhosts.c`
- [ ] **Positive — first connection stores key**: call `knownhosts_check()` for a host not in the file; verify it returns `KNOWNHOSTS_NEW`; call `knownhosts_add()`; re-check; verify it returns `KNOWNHOSTS_OK`.
- [ ] **Positive — matching key accepted silently**: add a host+key entry; call `knownhosts_check()` with the same key bytes; verify `KNOWNHOSTS_OK` with no side effects.
- [ ] **Positive — file created on first write**: point `knownhosts_init()` at a path that does not exist; call `knownhosts_add()`; verify the file is now present and contains the correct entry.
- [ ] **Positive — multiple hosts coexist**: add three different host+key pairs; verify each is independently retrievable and doesn't interfere with the others.
- [ ] **Negative — key mismatch returns error**: add a host with key A; call `knownhosts_check()` with key B for the same host; verify it returns `KNOWNHOSTS_MISMATCH`.
- [ ] **Negative — corrupted file handled**: write garbage bytes to the known_hosts file; call `knownhosts_init()`; verify it returns an error code and does not crash.
- [ ] **Negative — null/empty hostname**: call `knownhosts_check(NULL, ...)` and `knownhosts_check("", ...)`; verify both return an error code without crashing.
- [ ] **Corner — same host, different ports**: add `myhost:22` with key A and `myhost:2222` with key B; verify each port maps to the correct key independently.
- [ ] **Corner — IPv6 address**: add `[::1]:22` with a key; verify lookup by the same IPv6 address succeeds.
- [ ] **Corner — key with trailing whitespace/newline**: pass a key buffer with a trailing `\n`; verify it is stored and matched correctly (trimmed or handled consistently).
- [ ] **Corner — file missing at check time**: add an entry, delete the known_hosts file from disk, call `knownhosts_check()`; verify it returns `KNOWNHOSTS_NEW` (not a crash or mismatch).

### 8.2 Implement `src/ssh/knownhosts.c` + `src/ssh/knownhosts.h`
- [ ] Define return codes: `KNOWNHOSTS_OK`, `KNOWNHOSTS_NEW`, `KNOWNHOSTS_MISMATCH`, `KNOWNHOSTS_ERROR`.
- [ ] Implement `knownhosts_init(path)`: open or create the file at `%APPDATA%\sshclient\known_hosts`; load existing entries using `libssh2_knownhost_readfile()`.
- [ ] Implement `knownhosts_check(host, port, key, key_len)`: call `libssh2_knownhost_checkp()`; map libssh2 return values to the above codes.
- [ ] Implement `knownhosts_add(host, port, key, key_len)`: call `libssh2_knownhost_addc()` then `libssh2_knownhost_writefile()` to persist.
- [ ] Implement `knownhosts_free()`: call `libssh2_knownhost_free()`.
- [ ] Wire into `src/ssh/session.c` post-handshake: call `knownhosts_check()`; on `KNOWNHOSTS_NEW`, show a dialog displaying the SHA-256 fingerprint (hex formatted) and ask the user to accept or reject; on `KNOWNHOSTS_MISMATCH`, show a prominent warning dialog with the old and new fingerprints, block connection unless user explicitly overrides.
- [ ] Format fingerprint as `SHA256:base64` (matching OpenSSH display format).

### 8.3 Run tests and validate
- [ ] Run `make test` — all `test_knownhosts` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors.
- [ ] Run `make lint` — zero warnings in `src/ssh/knownhosts.c`.
- [ ] For any failing test: verify test correctness first; fix or remove invalid tests; fix code for all valid failures.
- [ ] Manual smoke-test: connect to a fresh server; accept fingerprint; disconnect; reconnect — verify no dialog on second connection. Manually edit `known_hosts` to change the key; reconnect — verify the mismatch warning dialog appears.

---

## Phase 9: Reliability — PTY Window Resize (P1-03) 🟡 HIGH

### 9.1 Write tests — `tests/test_pty_resize.c`
- [ ] **Positive — resize sends correct dimensions**: mock `libssh2_channel_request_pty_size()`; simulate a `WM_SIZE` event with a client area of 800×600 pixels and a character cell of 8×16 px; verify mock was called with cols=100, rows=37.
- [ ] **Positive — initial size matches window**: after connect, verify that the PTY size sent at connection time matches the actual window client area divided by character cell size (not a hard-coded 80×24).
- [ ] **Positive — resize to larger dimensions**: verify resize from 80×24 to 220×50 calls `libssh2_channel_request_pty_size(channel, 220, 50)`.
- [ ] **Positive — resize to smaller dimensions**: verify resize from 220×50 down to 40×10 calls the function with 40×10.
- [ ] **Negative — resize before channel open is a no-op**: simulate `WM_SIZE` when `channel == NULL`; verify `libssh2_channel_request_pty_size()` is NOT called and no crash occurs.
- [ ] **Negative — zero pixel dimensions**: simulate `WM_SIZE` with `cx=0, cy=0`; verify the function is not called (or is called with minimum 1×1 to avoid divide-by-zero).
- [ ] **Corner — exact cell boundary**: client area exactly 80×8 px with cell 8×8 px → cols=10, rows=1; verify correct calculation.
- [ ] **Corner — non-divisible area**: client area 85×19 px with cell 8×16 px → cols=10 (floor), rows=1 (floor); verify floor division is used.
- [ ] **Corner — same size after resize**: simulate two consecutive `WM_SIZE` events with identical dimensions; verify `libssh2_channel_request_pty_size()` is only called once (deduplication).
- [ ] **Corner — rapid successive resizes**: simulate 50 `WM_SIZE` events in a tight loop; verify the final call matches the last dimensions and no intermediate calls cause errors.

### 9.2 Implement PTY resize in `src/ssh/pty.c` and `src/ui/window.c`
- [ ] In `src/ssh/pty.c`: implement `pty_resize(channel, cols, rows)` which calls `libssh2_channel_request_pty_size()` and returns the libssh2 error code. Store last sent `(cols, rows)` and skip the call if dimensions are unchanged.
- [ ] In `src/ui/window.c` `WM_SIZE` handler: retrieve client area dimensions; divide by the renderer's `char_width` and `char_height` (use floor division); call `pty_resize()` if a channel is active.
- [ ] On connect (after PTY allocated): call `pty_resize()` immediately with the current window dimensions rather than a hardcoded 80×24.
- [ ] Guard against zero `char_width` or `char_height` to prevent divide-by-zero.

### 9.3 Run tests and validate
- [ ] Run `make test` — all `test_pty_resize` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors.
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: connect to a server; run `stty size`; verify reported rows and columns match the terminal window. Resize the window; run `stty size` again; verify the updated dimensions.

---

## Phase 10: Reliability — SSH Key Passphrase Prompt (P1-04) 🟡 HIGH

### 10.1 Write tests — `tests/test_key_auth.c`
- [ ] **Positive — unencrypted key auth succeeds with empty passphrase**: mock `libssh2_userauth_publickey_fromfile()` to return `LIBSSH2_ERROR_NONE` when passphrase is `""`; verify `key_auth_connect()` returns success without prompting.
- [ ] **Positive — encrypted key auth succeeds after passphrase entry**: mock to return `LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED` on first call, then `LIBSSH2_ERROR_NONE` on retry with a correct passphrase; verify `key_auth_connect()` requests a passphrase and returns success.
- [ ] **Positive — passphrase cached for session**: after successful auth, call `key_auth_get_cached_passphrase()`; verify it returns the passphrase used (for potential re-auth within same session).
- [ ] **Negative — wrong passphrase**: mock to always return `LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED` regardless of passphrase; verify the error is surfaced and the function returns failure after one retry (do not loop indefinitely).
- [ ] **Negative — key file not found**: mock to return `LIBSSH2_ERROR_FILE`; verify error is reported and no passphrase dialog is shown (wrong error type).
- [ ] **Negative — empty key path**: call `key_auth_connect()` with `key_path = ""`; verify it returns an error immediately.
- [ ] **Negative — passphrase dialog cancelled**: simulate the user clicking Cancel in the passphrase dialog; verify `key_auth_connect()` returns a cancellation error code and does not retry.
- [ ] **Corner — passphrase with special characters**: use passphrase containing `!@#$%^&*()"\\'`; verify it is passed verbatim to libssh2 without truncation or escaping.
- [ ] **Corner — very long passphrase (512 chars)**: verify it is not truncated.
- [ ] **Corner — passphrase not persisted to disk**: after successful auth, verify the passphrase does not appear anywhere in `config.json` or in any log file.

### 10.2 Implement passphrase support in `src/ssh/session.c`
- [ ] On `LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED` or `LIBSSH2_ERROR_FILE` from `libssh2_userauth_publickey_fromfile()`: show a modal passphrase input dialog (password-style `ES_PASSWORD` edit control).
- [ ] Retry `libssh2_userauth_publickey_fromfile()` once with the entered passphrase.
- [ ] If the user cancels the dialog, abort authentication and surface a cancellation error.
- [ ] Store the passphrase in a heap-allocated buffer within the session struct for the lifetime of the session; zero and free it on session close (`SecureZeroMemory`).
- [ ] Never write the passphrase to `config.json`, the debug log, or any file.

### 10.3 Run tests and validate
- [ ] Run `make test` — all `test_key_auth` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors; confirm no passphrase bytes appear in memory after session close.
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: configure a profile pointing to an encrypted private key; connect; verify passphrase prompt appears; enter correct passphrase; verify successful login.

---

## Phase 11: Feature — 256-Color and Truecolor Support (P2-02) 🟡 HIGH

### 11.1 Write tests — `tests/test_color.c`
- [x] **Positive — SGR 38;5;n sets 256-color foreground**: feed the sequence `\033[38;5;196m` (ANSI red, index 196) into the parser; verify the active cell attribute has `fg_mode = COLOR_256` and `fg_index = 196`.
- [x] **Positive — SGR 48;5;n sets 256-color background**: feed `\033[48;5;21m` (blue, index 21); verify `bg_mode = COLOR_256` and `bg_index = 21`.
- [x] **Positive — SGR 38;2;r;g;b sets truecolor foreground**: feed `\033[38;2;255;128;0m`; verify `fg_mode = COLOR_RGB`, `fg_r=255`, `fg_g=128`, `fg_b=0`.
- [x] **Positive — SGR 48;2;r;g;b sets truecolor background**: feed `\033[48;2;10;20;30m`; verify `bg_mode = COLOR_RGB`, `bg_r=10`, `bg_g=20`, `bg_b=30`.
- [x] **Positive — 256-color palette indices 0–15 match classic ANSI colors**: verify that `color256_to_rgb(0)` equals `#000000`, `color256_to_rgb(7)` equals `#C0C0C0`, `color256_to_rgb(15)` equals `#FFFFFF`, `color256_to_rgb(9)` equals the bright-red value.
- [x] **Positive — 256-color palette 16–231 is the 6×6×6 color cube**: verify `color256_to_rgb(16)` equals `#000000`, `color256_to_rgb(231)` equals `#FFFFFF`, and a mid-cube sample `color256_to_rgb(46)` equals `#00FF00`.
- [x] **Positive — 256-color palette 232–255 is the grayscale ramp**: verify `color256_to_rgb(232)` equals `#080808` and `color256_to_rgb(255)` equals `#EEEEEE`.
- [x] **Positive — SGR 0 resets extended colors**: set a 256-color fg; feed `\033[0m`; verify `fg_mode` resets to `COLOR_ANSI16` and the default color is restored.
- [x] **Positive — mixed SGR in one sequence**: feed `\033[1;38;5;82;48;2;0;0;128m` (bold + 256fg + truecolor bg); verify all three attributes are set correctly.
- [x] **Negative — SGR 38;5 without n parameter**: feed `\033[38;5m` (missing index); verify parser does not crash and does not change fg color.
- [x] **Negative — SGR 38;2 with only two sub-params**: feed `\033[38;2;255;128m` (missing blue); verify no crash and no color change.
- [x] **Negative — index out of range**: feed `\033[38;5;256m`; verify parser ignores it without crash.
- [x] **Negative — negative RGB component**: feed `\033[38;2;-1;0;0m`; verify parser ignores it gracefully.
- [x] **Corner — 256-color interleaved with classic SGR**: feed `\033[31m\033[38;5;200m`; verify the 256-color value overrides the classic red without resetting other attributes.
- [x] **Corner — truecolor with all-zero values**: feed `\033[38;2;0;0;0m`; verify fg is set to pure black (not left at default).
- [x] **Corner — truecolor with max values**: feed `\033[38;2;255;255;255m`; verify fg is pure white.
- [x] **Corner — renderer uses SetTextColor with full RGB**: verify that `renderer_draw_cell()` calls `SetTextColor(hdc, RGB(r,g,b))` using the resolved RGB value for both 256-color and truecolor modes.

### 11.2 Implement 256-color and truecolor in `src/term/parser.c` and `src/ui/renderer.c`
- [x] In `src/term/cell.h`: extend the cell color attribute struct to hold `color_mode` (ANSI16 / COLOR_256 / COLOR_RGB), `color_index` (0–255), and `rgb` (packed `uint32_t` or three `uint8_t` fields) for both fg and bg.
- [x] In `src/term/parser.c` SGR handler: after encountering param `38` or `48`, peek at the next sub-parameter; dispatch to 256-color or truecolor branch; consume the correct number of sub-parameters in each case.
- [x] Add `color256_to_rgb(uint8_t index)` lookup function: indices 0–15 → classic ANSI palette; 16–231 → 6×6×6 cube formula; 232–255 → grayscale ramp.
- [x] In `src/ui/renderer.c` `renderer_draw_cell()`: resolve the cell's fg and bg to a final `COLORREF` based on `color_mode` before calling `SetTextColor` and `SetBkColor`.

### 11.3 Run tests and validate
- [x] Run `make test` — all `test_color` cases must pass.
- [x] Run `make debug` — zero sanitizer errors.
- [x] Run `make lint` — zero warnings.
- [x] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [x] Manual smoke-test: run `printf '\033[38;5;196mRed256\033[0m \033[38;2;0;255;128mTruecolor\033[0m\n'` in a connected session; verify correct colors are displayed.

---

## Phase 12: Feature — Multi-Tab Sessions (P2-01) 🟡 HIGH

### 12.1 Write tests — `tests/test_tabs.c`
- [ ] **Positive — create first tab**: call `tabs_new()`; verify the tab count is 1 and the active tab index is 0.
- [ ] **Positive — create multiple tabs**: call `tabs_new()` three times; verify count is 3 and each tab has a unique `session_id`.
- [ ] **Positive — switch active tab**: with 3 tabs, call `tabs_switch(1)`; verify `tabs_active_index()` returns 1 and `tabs_active()` points to the second session struct.
- [ ] **Positive — close middle tab**: with tabs [0,1,2], close tab 1; verify count is 2; verify the remaining tabs are [0,2] and their data is intact.
- [ ] **Positive — close last tab**: with 1 tab, close it; verify count is 0 and `tabs_active()` returns NULL.
- [ ] **Positive — tab state is independent**: write to the terminal buffer of tab 0; verify tab 1's buffer is unmodified; switch to tab 0; verify the data is still present.
- [ ] **Negative — switch to invalid index**: call `tabs_switch(99)` with only 3 tabs; verify active tab is unchanged and no crash.
- [ ] **Negative — close invalid index**: call `tabs_close(99)`; verify no crash and tab count is unchanged.
- [ ] **Negative — tab_active() when no tabs**: call `tabs_active()` with zero tabs; verify it returns `NULL` without crashing.
- [ ] **Corner — maximum tab limit**: create tabs up to `TABS_MAX` (e.g., 32); verify the 33rd `tabs_new()` returns an error code rather than exceeding the limit.
- [ ] **Corner — close active tab**: close the currently active tab; verify the active index moves to the previous tab (or next if it was the first).
- [ ] **Corner — re-open after close**: create 3 tabs, close the middle one, create a new tab; verify the new tab's ID does not collide with the old IDs.
- [ ] **Corner — tab connection status updates**: set tab 0 status to `TAB_CONNECTED`; verify `tabs_get_status(0)` returns `TAB_CONNECTED` and tab 1 is unaffected.

### 12.2 Implement `src/ui/tabs.c` + `src/ui/tabs.h`
- [ ] Define `TabStatus` enum: `TAB_IDLE`, `TAB_CONNECTING`, `TAB_CONNECTED`, `TAB_DISCONNECTED`.
- [ ] Define `Tab` struct: `session`, `channel`, `terminal`, `tab_status`, `session_id`, `connect_time`.
- [ ] Implement `tabs_init()`, `tabs_new()`, `tabs_close(index)`, `tabs_switch(index)`, `tabs_active()`, `tabs_active_index()`, `tabs_count()`, `tabs_get_status(index)`, `tabs_set_status(index, status)`, `tabs_free()`.
- [ ] Use the existing `vector.h` dynamic array for the tab list.
- [ ] In `src/ui/window.c`: add a `WC_TABCONTROL` (or owner-drawn strip) above the terminal area; wire `TCN_SELCHANGE` to `tabs_switch()`; redraw the terminal area from the new active tab's buffer on switch.
- [ ] Handle `Ctrl+T` to create a new tab (opens session manager dialog pre-filled with last-used host).
- [ ] Handle `Ctrl+W` to close the active tab (with a confirm dialog if connected).
- [ ] Draw a coloured indicator dot per tab: yellow = `TAB_CONNECTING`, green = `TAB_CONNECTED`, grey = `TAB_IDLE`, red = `TAB_DISCONNECTED`.
- [ ] Route all SSH I/O thread events, keyboard input, and terminal redraws to the active tab's session.

### 12.3 Run tests and validate
- [ ] Run `make test` — all `test_tabs` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors (especially for tab close and session teardown paths).
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: open three tabs to different servers; type in each tab; verify terminal states do not bleed between tabs; close middle tab; verify remaining tabs continue working.

---

## Phase 13: Feature — Settings Dialog (P2-03) 🔵 MEDIUM

### 13.1 Write tests — `tests/test_settings.c`
- [ ] **Positive — defaults applied for empty config**: call `settings_load_defaults()`; verify `font_name = "Consolas"`, `font_size = 12`, `scrollback_lines = 3000`, `paste_delay_ms = 350`.
- [ ] **Positive — valid settings parse correctly**: write a JSON snippet with all settings fields; call `config_load()`; verify each field matches the expected value.
- [ ] **Positive — settings serialise round-trip**: load settings, modify `font_size` to 16, call `config_save()`, reload from disk; verify `font_size == 16` and all other fields are unchanged.
- [ ] **Negative — font_size below minimum**: set `font_size = 0` in JSON; verify `config_load()` replaces it with the default (12) and logs a warning.
- [ ] **Negative — font_size above maximum**: set `font_size = 999`; verify it is clamped to the maximum (72) and a warning is logged.
- [ ] **Negative — scrollback_lines zero**: set `scrollback_lines = 0`; verify it is replaced with the default (3000).
- [ ] **Negative — paste_delay_ms negative**: set `paste_delay_ms = -1`; verify it is replaced with the default (350).
- [ ] **Negative — unknown setting keys are ignored**: add `"future_setting": "x"` to the JSON; verify `config_load()` succeeds and does not crash or reject the file.
- [ ] **Corner — font_name with special characters**: set `font_name = "Cascadia Code PL"`; verify it is stored and serialised without truncation.
- [ ] **Corner — fg_color and bg_color as hex strings**: set `fg_color = "#0C0C0C"` and `bg_color = "#F2F2F2"`; verify they are parsed to correct `COLORREF` values.
- [ ] **Corner — config file missing entirely**: delete config file; call `config_load()`; verify it returns defaults and creates a new file with defaults on first `config_save()`.

### 13.2 Implement settings dialog `src/ui/settings_dialog.c`
- [ ] Create `IDD_SETTINGS` dialog resource with: font name combo box (populated via `EnumFontFamiliesEx()` filtered to `FIXED_PITCH`); font size spin control (range 6–72); foreground colour button (opens `ChooseColor()`); background colour button; scrollback lines edit (numeric only); paste delay edit (numeric only); OK and Cancel buttons.
- [ ] On OK: validate all fields; clamp out-of-range values; write to in-memory `Config` struct; call `config_save()`; call `renderer_set_font()` and `renderer_set_colors()` to apply changes immediately without restarting.
- [ ] Add `Settings...` item to the application menu or toolbar and wire it to open `IDD_SETTINGS`.
- [ ] Add validation logic in `config_load()` to clamp or default invalid values (reused by both the dialog and the loader).

### 13.3 Run tests and validate
- [ ] Run `make test` — all `test_settings` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors.
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: open Settings; change font to `Courier New` size 14; click OK; verify terminal redraws with the new font without restarting the application.

---

## Phase 14: Feature — Session File Logging (P2-04) 🔵 MEDIUM

### 14.1 Write tests — `tests/test_session_log.c`
- [ ] **Positive — output written to file**: call `session_log_open()`; write known data via `session_log_write()`; call `session_log_close()`; read the file back; verify the content matches.
- [ ] **Positive — ANSI sequences are stripped**: write `"hello \033[31mworld\033[0m\n"` via `session_log_write()`; verify the file contains only `"hello world\n"`.
- [ ] **Positive — filename template**: call `session_log_open("myserver", log_dir)`; verify the created filename matches the pattern `YYYY-MM-DD_HH-MM-SS_myserver.log`.
- [ ] **Positive — multiple writes accumulate**: write three separate chunks; verify the file contains all three in order with no gaps or duplicates.
- [ ] **Positive — logging disabled means no file**: call `session_log_open()` with `enabled = false`; call `session_log_write()`; verify no file is created and no error is returned.
- [ ] **Negative — write to read-only directory**: point log dir at a read-only path; call `session_log_open()`; verify it returns an error code and does not crash.
- [ ] **Negative — write after close is a no-op**: call `session_log_close()`; call `session_log_write()` again; verify no crash and no write.
- [ ] **Negative — null data pointer**: call `session_log_write(NULL, 10)`; verify graceful error return.
- [ ] **Corner — empty write**: call `session_log_write("", 0)`; verify no crash and file is unchanged.
- [ ] **Corner — very large write (1 MB)**: write a 1 MB buffer in one call; verify all bytes appear in the log file.
- [ ] **Corner — OSC sequences stripped**: write `"\033]0;Window Title\007data"` ; verify file contains only `"data"`.
- [ ] **Corner — partial ANSI sequence at buffer boundary**: write `"\033[3"` then `"1mtext\033[0m"` in two separate calls; verify the split sequence is handled and `"text"` appears in the log.
- [ ] **Corner — binary data in stream**: write a buffer containing `\x00\x01\x02` mixed with printable text; verify the log does not crash and printable text is preserved.

### 14.2 Implement `src/core/session_log.c` + `src/core/session_log.h`
- [ ] Implement `session_log_open(hostname, log_dir, enabled)`: build filename from timestamp + hostname; open file for append; return a `SessionLog` handle.
- [ ] Implement `session_log_write(log, data, len)`: run `data` through the ANSI/OSC strip state machine; write clean text to the file.
- [ ] Implement `session_log_close(log)`: flush and close file; zero the handle.
- [ ] ANSI strip state machine: handle ESC sequences (`\033[...m`, `\033]...\007`, `\033[...h/l`, etc.) robustly across split buffer boundaries; strip control characters below `\x20` except `\n`, `\r`, `\t`.
- [ ] Add `logging_enabled` (bool) and `log_path` (string) to `Config` settings and JSON schema.
- [ ] In `src/ssh/channel.c` read loop: if logging is enabled, pass received data to `session_log_write()` before feeding to the terminal emulator.
- [ ] Add a logging toggle button or menu item; update a visible indicator (e.g., status bar text or tab label suffix) when logging is active.

### 14.3 Run tests and validate
- [ ] Run `make test` — all `test_session_log` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors.
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: enable logging, connect to a server, run `ls -la --color=always`; open the log file; verify it contains readable directory listing with no ANSI escape codes.

---

## Phase 15: Feature — Paste Confirmation Dialog (P2-05) 🔵 MEDIUM

### 15.1 Write tests — `tests/test_paste.c`
- [ ] **Positive — short paste bypasses dialog**: call `paste_should_confirm("hello", 5)`; verify it returns `false` (threshold not reached).
- [ ] **Positive — long paste triggers confirmation**: call `paste_should_confirm()` with a 128-byte string; verify it returns `true`.
- [ ] **Positive — multi-line paste triggers confirmation**: call `paste_should_confirm("line1\nline2", 11)`; verify it returns `true` (contains newline regardless of length).
- [ ] **Positive — inter-line delay applied**: mock `Sleep()`; call `paste_send_with_delay("a\nb\nc", channel, 100)`; verify `Sleep(100)` was called exactly twice (between lines, not after last).
- [ ] **Positive — single-line paste uses no delay**: call `paste_send_with_delay("hello", channel, 100)`; verify `Sleep()` is never called.
- [ ] **Negative — cancelled paste sends nothing**: simulate dialog cancel; verify zero bytes are written to the channel.
- [ ] **Negative — null paste data**: call `paste_should_confirm(NULL, 0)`; verify no crash and returns `false`.
- [ ] **Negative — paste with only whitespace/newlines**: paste `"\n\n\n"`; verify confirmation is triggered (contains newlines).
- [ ] **Corner — exactly at threshold (64 bytes)**: verify behaviour is consistent — either always confirm at exactly 64 or only above 64; document the chosen rule and test it explicitly.
- [ ] **Corner — paste containing only non-printable bytes**: paste `"\x01\x02\x03"`; verify confirmation is not triggered (not printable text — should be sent directly or blocked).
- [ ] **Corner — paste_delay_ms = 0**: set delay to 0ms; verify `Sleep(0)` or no sleep is called and no crash.
- [ ] **Corner — very large paste (100 KB)**: verify the confirmation dialog renders a truncated preview (e.g., first 500 chars + "... (N lines total)") rather than the full content; verify the full content is still sent on confirm.

### 15.2 Implement paste handling in `src/ui/window.c`
- [ ] Implement `paste_should_confirm(text, len)`: returns `true` if `len >= PASTE_CONFIRM_THRESHOLD` (default 64) or if `text` contains a `\n` or `\r` character.
- [ ] Implement `paste_send_with_delay(text, channel, delay_ms)`: split text on `\n`; write each line to channel; call `Sleep(delay_ms)` between lines.
- [ ] In the `WM_PASTE` / clipboard handler: read clipboard text; call `paste_should_confirm()`; if true, open a modal dialog showing line count and a truncated preview; on Confirm, call `paste_send_with_delay()`; on Cancel, discard.
- [ ] Read `paste_delay_ms` from the in-memory `Config` struct (set via Settings dialog).

### 15.3 Run tests and validate
- [ ] Run `make test` — all `test_paste` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors.
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: copy a multi-line shell script to the clipboard; paste into the terminal; verify the confirmation dialog appears with a preview and line count; confirm; verify the script is typed into the session with inter-line pauses.

---

## Phase 16: Feature — Zoom Control (P2-06) 🔵 MEDIUM

### 16.1 Write tests — `tests/test_zoom.c`
- [ ] **Positive — zoom in increases font size**: call `zoom_in()` with current size 12; verify new size is 13.
- [ ] **Positive — zoom out decreases font size**: call `zoom_out()` with current size 12; verify new size is 11.
- [ ] **Positive — zoom triggers PTY resize**: mock `pty_resize()`; call `zoom_in()`; verify `pty_resize()` was called with updated col/row values.
- [ ] **Positive — terminal dimensions recalculated**: after `zoom_in()`, verify `terminal_cols = floor(client_width / new_char_width)`.
- [ ] **Negative — zoom in at maximum (72pt) is a no-op**: call `zoom_in()` with size 72; verify size remains 72 and `pty_resize()` is NOT called.
- [ ] **Negative — zoom out at minimum (6pt) is a no-op**: call `zoom_out()` with size 6; verify size remains 6 and `pty_resize()` is NOT called.
- [ ] **Negative — zoom when no channel is open**: call `zoom_in()` with `channel = NULL`; verify font size changes but `pty_resize()` is not called and no crash.
- [ ] **Corner — zoom in then zoom out returns to original size**: 12 → zoom_in → 13 → zoom_out → 12; verify exact round-trip.
- [ ] **Corner — multiple zoom steps**: zoom in 10 times from 12; verify size is 22 (not >72).
- [ ] **Corner — scroll wheel zoom (Ctrl+Scroll up)**: simulate `WM_MOUSEWHEEL` with `WHEEL_DELTA = 120` and `Ctrl` held; verify `zoom_in()` is called once. Simulate 3 ticks (`WHEEL_DELTA = 360`); verify `zoom_in()` called 3 times.

### 16.2 Implement zoom in `src/ui/window.c` and `src/ui/renderer.c`
- [ ] Implement `zoom_in()` and `zoom_out()` in `renderer.c`: clamp to [6, 72]; call `renderer_set_font()` with the new size; recalculate `char_width` and `char_height`; call `pty_resize()` if a channel is active.
- [ ] In `WM_MOUSEWHEEL` handler: check `GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL`; divide `GET_WHEEL_DELTA_WPARAM(wParam)` by `WHEEL_DELTA` to get tick count; call `zoom_in()` or `zoom_out()` that many times.
- [ ] Add `Ctrl+=` (zoom in) and `Ctrl+-` (zoom out) to the `WM_KEYDOWN` handler.
- [ ] Persist the current font size to `Config` and `config_save()` after each zoom change so it is remembered across sessions.

### 16.3 Run tests and validate
- [ ] Run `make test` — all `test_zoom` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors.
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: connect to a server; Ctrl+Scroll up to zoom in; verify font grows and the shell reflows correctly (`stty size` shows updated cols/rows); zoom back out; verify it returns to original size.

---

## Phase 17: Polish — Hover Tooltips (P3-01) 🔵 MEDIUM

### 17.1 Write tests — `tests/test_tooltip.c`
- [ ] **Positive — tooltip text for connected tab**: set tab 0 status to `TAB_CONNECTED` with username `"tom"`, host `"dev.example.com"`, connected 90 seconds ago; call `tooltip_build_text(0, buf, sizeof(buf))`; verify buf contains `"tom"`, `"dev.example.com"`, and `"1m 30s"`.
- [ ] **Positive — tooltip text for disconnected tab**: set status to `TAB_DISCONNECTED`; verify tooltip shows `"Disconnected"`.
- [ ] **Positive — duration formatting**: verify `tooltip_format_duration(3661)` returns `"1h 1m 1s"`, `tooltip_format_duration(59)` returns `"59s"`, `tooltip_format_duration(0)` returns `"0s"`.
- [ ] **Negative — tooltip for invalid tab index**: call `tooltip_build_text(99, buf, sizeof(buf))`; verify no crash and buffer is empty or contains an error string.
- [ ] **Corner — very long hostname (255 chars)**: verify tooltip text is truncated to fit without buffer overflow.
- [ ] **Corner — log path in tooltip when logging active**: enable session logging; verify the log file path appears in the tooltip text.

### 17.2 Implement tooltips in `src/ui/tabs.c` and `src/ui/window.c`
- [ ] Register a `TOOLTIPS_CLASS` control as a child of the main window.
- [ ] For each tab, add a `TOOLINFO` entry pointing to the tab's rect; update the rect on tab resize.
- [ ] Implement `TTN_NEEDTEXT` handler: call `tooltip_build_text()` to build and return the tooltip string.
- [ ] Implement `tooltip_build_text()` and `tooltip_format_duration()` as pure functions (no Win32 dependency) so they are testable.
- [ ] Track `connect_time` as a `ULONGLONG` from `GetTickCount64()` in the `Tab` struct; compute elapsed in `tooltip_build_text()`.

### 17.3 Run tests and validate
- [ ] Run `make test` — all `test_tooltip` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors.
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: connect to a server; hover over the tab for 2+ seconds; verify the tooltip appears showing the correct host, user, and elapsed time.

---

## Phase 18: Polish — Additional VT Sequences (P3-03) 🔵 LOW

### 18.1 Write tests — `tests/test_vt_sequences.c`
- [ ] **OSC 0 — window title**: feed `"\033]0;My Title\007"`; verify `terminal_get_title()` returns `"My Title"`.
- [ ] **OSC 2 — window title (same as OSC 0)**: feed `"\033]2;Other Title\007"`; verify title is `"Other Title"`.
- [ ] **OSC with ST terminator**: feed `"\033]0;Title\033\\"`; verify title is set correctly (alternative string terminator).
- [ ] **DECTCEM show cursor (`\033[?25h`)**: set cursor hidden; feed the sequence; verify `cursor_visible == true`.
- [ ] **DECTCEM hide cursor (`\033[?25l`)**: set cursor visible; feed; verify `cursor_visible == false`.
- [ ] **Alternate screen enter (`\033[?1049h`)**: feed sequence; verify `terminal_is_alt_screen()` returns `true`; verify the primary screen buffer is saved.
- [ ] **Alternate screen exit (`\033[?1049l`)**: enter alt screen, write text, exit; verify the primary screen content is restored exactly.
- [ ] **Application cursor keys enable (`\033[?1h`)**: enable; feed `\033[A` (up arrow); verify the generated sequence sent back to host is `\033OA`.
- [ ] **Application cursor keys disable (`\033[?1l`)**: disable; feed up arrow; verify sequence sent is `\033[A`.
- [ ] **Insert mode enable (`\033[4h`)**: set insert mode; write character at mid-line; verify characters to the right shift right (not overwritten).
- [ ] **Insert mode disable (`\033[4l`)**: set to replace mode; write character; verify it overwrites the existing character.
- [ ] **Negative — OSC with no terminator (truncated)**: feed `"\033]0;Unterminated"` without `\007`; verify no crash; title unchanged.
- [ ] **Negative — unknown private mode (`\033[?999h`)**: verify parser ignores it without crash.
- [ ] **Corner — OSC title with special characters**: set title to `"Title: <test> & 'quotes'"`; verify stored verbatim.
- [ ] **Corner — alt screen content isolation**: write to alt screen; exit; verify primary screen text is restored and alt screen content is not visible.

### 18.2 Implement additional VT sequences in `src/term/parser.c`
- [ ] **OSC 0/2**: parse `\033]Ps;Pt\007` and `\033]Ps;Pt\033\\`; store title in `terminal->title`; on title change, call `SetWindowText()` in the renderer.
- [ ] **DECTCEM** (`?25h/l`): set `terminal->cursor_visible`; renderer skips cursor draw when false.
- [ ] **Alternate screen buffer** (`?1049h/l`): allocate a secondary buffer; on enter, save primary buffer and cursor position, switch to secondary; on exit, restore primary.
- [ ] **Application cursor keys** (`?1h/l`): set `terminal->app_cursor_keys`; in keyboard handler, send `\033O` prefix instead of `\033[` for arrow keys when enabled.
- [ ] **Insert mode** (`4h/l`): set `terminal->insert_mode`; in `term_write_char()`, shift characters right before inserting when enabled.

### 18.3 Run tests and validate
- [ ] Run `make test` — all `test_vt_sequences` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors.
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: run `vim` in a connected session; verify it opens in the alternate screen, the title bar updates to `"VIM"`, and the cursor hides in normal mode. Exit vim; verify the shell prompt is restored.

---

## Phase 19: Polish — Dynamic Light/Dark Theme (P3-02) 🔵 LOW

### 19.1 Write tests — `tests/test_theme.c`
- [ ] **Positive — dark background returns dark mode**: call `theme_is_dark(RGB(12, 12, 12))`; verify it returns `true` (L ≈ 0.02, below 0.5 threshold).
- [ ] **Positive — light background returns light mode**: call `theme_is_dark(RGB(242, 242, 242))`; verify it returns `false` (L ≈ 0.90).
- [ ] **Positive — mid-gray at threshold**: call `theme_is_dark(RGB(128, 128, 128))`; verify the result is deterministic and matches the documented threshold rule (L ≈ 0.216, so `true`).
- [ ] **Positive — luminance formula is correct**: verify `theme_luminance(RGB(255, 0, 0))` ≈ 0.2126, `theme_luminance(RGB(0, 255, 0))` ≈ 0.7152, `theme_luminance(RGB(0, 0, 255))` ≈ 0.0722.
- [ ] **Corner — pure black**: `theme_is_dark(RGB(0,0,0))` → `true`.
- [ ] **Corner — pure white**: `theme_is_dark(RGB(255,255,255))` → `false`.

### 19.2 Implement theme detection in `src/ui/renderer.c`
- [ ] Implement `theme_luminance(COLORREF)` and `theme_is_dark(COLORREF)` as pure functions.
- [ ] On settings apply (and on startup): call `theme_is_dark(bg_color)`; call `DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))` to match the title bar. Guard with a version check (`IsWindows10OrGreater()`) for older systems.

### 19.3 Run tests and validate
- [ ] Run `make test` — all `test_theme` cases must pass.
- [ ] Run `make debug` — zero sanitizer errors.
- [ ] Run `make lint` — zero warnings.
- [ ] For any failing test: verify test correctness; fix or remove invalid tests; fix code for valid failures.
- [ ] Manual smoke-test: in Settings, set background to `#0C0C0C`; verify the window title bar switches to dark. Set background to `#F2F2F2`; verify it switches to light.

---

## Phase 20: Polish — Error Dialogs, Double-Click Connect (P3-04, P3-05) 🔵 LOW

- [ ] **P3-05 Error dialogs — connection failure**: replace `LOG_ERROR(...)` with `MessageBox(hwnd, message, "Connection Error", MB_OK | MB_ICONERROR)` for: failed socket connect, failed SSH handshake, failed authentication, channel open failure.
- [ ] **P3-05 Error dialogs — config failure**: if `config_load()` returns an error, show a dialog and offer to reset to defaults rather than silently using defaults.
- [ ] **P3-04 Double-click to connect**: in the session manager dialog's `WM_NOTIFY` handler, handle `NM_DBLCLK` on the profile list control; call the same connect function as the Connect button.
- [ ] Run `make test && make debug && make lint` — all clean.
- [ ] Manual smoke-test: double-click a profile in the session manager; verify it connects immediately. Attempt a connection to an invalid host; verify an error dialog appears with a meaningful message.

---

## Phase 21: Final Audit

- [ ] Run the full test suite (`make test`) — all tests green.
- [ ] Run the sanitizer build (`make debug`) with all test cases — zero ASan / UBSan errors.
- [ ] Run `make lint` — zero cppcheck warnings across the entire `src/` tree.
- [ ] Run Dr. Memory on the Windows release build — zero memory leaks or invalid accesses.
- [ ] Security checklist: confirm no plaintext passwords in `config.json`; confirm TOFU dialog appears on first connect to each new host; confirm mismatch warning appears when host key changes; confirm passphrases do not appear in logs or on disk.
- [ ] Compatibility matrix: test against OpenSSH 8.x (Ubuntu 22.04), OpenSSH 9.x (Debian 12), Dropbear, and Windows OpenSSH Server — both password auth and Ed25519/RSA-4096 key auth on each.
