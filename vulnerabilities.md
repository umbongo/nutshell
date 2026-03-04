# Security Vulnerability Report

*Generated March 2026 — Code audit of Conga.SSH C rewrite*

---

## Critical

### C-1. Password encryption key derived from public machine identifiers

**File:** `src/crypto/crypto.c:23, 88–95`

The PBKDF2 salt is a compile-time constant (`"CongaSSH-v1"`). On Windows the key material is the `MachineGuid` registry value, which is readable by any local user. On non-Windows platforms the key material is the hostname, which is public knowledge. Any user on the same machine (or anyone who knows the hostname) can derive the identical encryption key and decrypt all saved passwords from `config.json`.

**Impact:** Complete compromise of all encrypted passwords by any local user or anyone who knows the hostname (non-Windows).

**Recommended fix:** Use Windows DPAPI (`CryptProtectData` / `CryptUnprotectData`) for per-user encryption. On Linux, use the user's keyring or require a master passphrase. At minimum, incorporate a randomly generated per-file salt stored alongside the ciphertext.

---

## High

### H-1. Race condition in connection thread

**File:** `src/ui/window.c:325–447 (connection_thread), 750–795 (WM_TIMER)`

`connection_thread()` runs on a background thread and reads/writes `Session` struct fields (`ssh`, `channel`, `conn_result`, `conn_error`). The WM_TIMER handler on the main thread reads the same fields and calls `ssh_io_poll()`. Only `conn_cancelled` is `volatile`; other shared fields have no synchronisation.

**Impact:** Data races can lead to corrupted state, use-after-free, or undefined behaviour.

**Recommended fix:** Use a critical section or mutex to protect shared Session fields, or ensure the main thread never reads connection-phase fields until `WM_CONN_DONE` is posted.

### H-2. Deprecated `gethostbyname()` for DNS resolution

**File:** `src/term/ssh_session.c:72`

`gethostbyname()` is deprecated, uses static internal storage, and is not thread-safe. Concurrent connections from multiple tabs will corrupt each other's DNS results.

**Impact:** Connecting to the wrong host, or crashes when multiple tabs connect simultaneously.

**Recommended fix:** Use `getaddrinfo()` which is thread-safe and supports IPv6.

### H-3. Plaintext password lingers in memory

**File:** `src/config/loader.c:226–235`, `src/ui/window.c:478`

After decryption, the plaintext password lives in `Profile.password[256]` for the entire application lifetime. When copied to `Session.conn_profile`, that copy is never zeroed either.

**Impact:** Plaintext passwords visible in crash dumps, memory forensics, or page files.

**Recommended fix:** `SecureZeroMemory(s->conn_profile.password, ...)` after authentication completes. Consider `VirtualLock()` to prevent paging.

### H-4. TOFU broken for non-RSA host keys

**File:** `src/term/knownhosts.c:112`

`knownhosts_add()` hardcodes `LIBSSH2_KNOWNHOST_KEY_SSHRSA` regardless of the actual key type. For ECDSA or Ed25519 keys, the stored entry has the wrong type, so subsequent checks always return `KNOWNHOSTS_NEW` — the user is prompted every time, training them to always click "Yes."

**Impact:** Host key verification silently broken for non-RSA keys. Users become habituated to accepting, defeating MITM protection.

**Recommended fix:** Read the actual key type from `libssh2_session_hostkey()` and map to the correct `LIBSSH2_KNOWNHOST_KEY_*` constant.

---

## Medium

### M-1. Integer overflow in base64 size calculations

**File:** `src/crypto/crypto.c:30–40`

`b64_enc_size` computes `((src_len + 2u) / 3u) * 4u + 1u`. For `src_len` near `SIZE_MAX`, the addition wraps around, leading to a too-small allocation and heap buffer overflow in `b64_decode`/`b64_encode`.

**Recommended fix:** Reject input strings longer than 64 KB (unreasonable for a password blob).

### M-2. Unchecked `RegQueryValueExA` return value

**File:** `src/crypto/crypto.c:88–89`

If `MachineGuid` is missing (rare Windows configurations), `material` contains uninitialised stack data. `strlen(material)` may read past the buffer.

**Recommended fix:** Check the return value; null-terminate `material` on failure.

### M-3. No file size limit when reading config

**File:** `src/config/loader.c:14–36`

`read_file()` allocates based on `ftell()` with no upper bound. A multi-gigabyte file (corruption or symlink attack) triggers `xmalloc` abort.

**Recommended fix:** Reject files larger than 1 MB.

### M-4. Non-atomic config file save

**File:** `src/config/loader.c:248–326`

`fopen(path, "w")` truncates the file immediately. A crash mid-write loses all configuration.

**Recommended fix:** Write to `config.json.tmp` first, then `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`.

### M-5. Passphrase not zeroed on dialog cancellation

**File:** `src/ui/window.c:210–254`

When the user cancels the passphrase dialog, `ctx.buf` still contains the typed passphrase. `SecureZeroMemory` is only called on the OK path.

**Recommended fix:** Always call `SecureZeroMemory(ctx.buf, sizeof(ctx.buf))` before returning.

### M-6. User-controlled `strftime` format string

**File:** `src/ui/window.c:294–297`

`log_format` from `config.json` is passed directly to `strftime()`. While generally safe, unknown specifiers have undefined behaviour on some platforms.

**Recommended fix:** Validate against a whitelist of allowed specifiers before use.

### M-7. No port range validation

**File:** `src/term/ssh_session.c:68`

Port is cast to `uint16_t` via `htons()` without checking range 1–65535. Port 0 or negative values from corrupted config silently misbehave.

**Recommended fix:** `if (port < 1 || port > 65535) return -1;`

### M-8. Config path is relative — CWD can change after file dialog

**File:** `src/ui/window.c:719`, `src/ui/settings.c:317`, `src/ui/session_manager.c`

`"config.json"` is relative to CWD. The SSH key browse dialog (`GetOpenFileNameA`) can change CWD, so subsequent saves may write to the wrong directory.

**Recommended fix:** Resolve to an absolute path at startup using `get_exe_dir()` and pass it everywhere.

---

## Low

### L-1. `memset` to zero secrets may be optimised away

**Files:** `src/crypto/crypto.c:227,237`, `src/config/loader.c:116`, `src/term/ssh_session.c:43`

The compiler may eliminate `memset` calls as dead stores. The codebase uses `SecureZeroMemory` in some places but `memset` in others.

**Recommended fix:** Use `SecureZeroMemory()` (Windows) or `explicit_bzero()` consistently for all secret-zeroing.

### L-2. `EVP_EncryptUpdate` — `size_t` to `int` truncation

**File:** `src/crypto/crypto.c:145`

`pt_len` is `size_t` but cast to `int`. Unrealistic for passwords but the API allows arbitrary plaintext.

**Recommended fix:** Guard: `if (pt_len > INT_MAX) return CRYPTO_ERR_ARGS;`

### L-3. JSON serialiser doesn't escape control characters below 0x20

**File:** `src/config/loader.c:42–57`

Only `"`, `\\`, `\n`, `\r`, `\t` are escaped. Other control characters produce invalid JSON per RFC 8259.

**Recommended fix:** Escape characters below 0x20 with `\uXXXX` notation.

### L-4. CSI parameter bounds — fragile by construction

**File:** `src/term/parser.c:446–448`

`csi_params[csi_param_count - 1]` is safe by construction (count always ≥1 when digits arrive) but an explicit bounds check would be more robust.

**Recommended fix:** Add: `if (term->csi_param_count > 0 && term->csi_param_count <= TERM_MAX_CSI_PARAMS)`.

### L-5. `ssh_io_poll` accumulator is `int`

**File:** `src/config/ssh_io.c:28,40`

`total_read` is `int`; safe at current `MAX_LOOPS=10` but fragile if the constant changes.

**Recommended fix:** Use `size_t`.

---

## Informational

### I-1. No SSH connection timeout

**File:** `src/term/ssh_session.c:60–83`

TCP `connect()` uses the OS default timeout (potentially 21+ seconds). No explicit timeout is set.

### I-2. `WSAStartup`/`WSACleanup` called per-session

**File:** `src/term/ssh_session.c:28–29, 52–54`

WSA reference-counts these calls, but calling `WSACleanup` for the first disconnecting session could affect others.

**Recommended fix:** Call once in `main()` / `WinMain()` and once on exit.

### I-3. Tab limit return value unchecked

**File:** `src/core/tab_manager.c:14`

`tabmgr_add` returns -1 at the limit, but the caller in `window.c` doesn't check it.

**Recommended fix:** Show an error dialog when the tab limit is reached.
