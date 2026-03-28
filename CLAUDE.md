# CLAUDE.md — Nutshell Project Instructions

## MANDATORY: Version Bump Before Every Build

**Before running `make` (any target that produces a binary), increment the patch version in ALL THREE files:**

1. `src/ui/resource.h` — update `APP_VERSION` string (e.g., `"1.0.02"` -> `"1.0.03"`)
2. `README.md` — update the `**Version**:` line
3. `src/ui/nutshell.rc` — update `FILEVERSION`, `PRODUCTVERSION`, and both `FileVersion`/`ProductVersion` string values

No exceptions. Every build gets a unique version number.

## Build Commands

- **Always `make clean && make release`** — never `make release` alone.
- **Always run `make test` after changes** to verify nothing is broken.
- **Two compilers**: `x86_64-w64-mingw32-gcc` for the Windows cross-compile (`make`), native `gcc` for tests (`make test`). Code must compile clean under both with `-Werror`.
- **vcpkg include order matters**: `VCPKG_INC` must come before `-Isrc/term` so the real `libssh2.h` (with macros) overrides the local test stub.
- **Linker order matters**: vcpkg libs (`-lssh2 -lssl -lcrypto -lzlib`) must come before Windows system libs (`-lws2_32 -lgdi32 ...`).
- **`NON_TEST_SRCS`**: Files in `src/ui/` are excluded from test builds. If you add portable logic that needs testing, put it in `src/core/`, not `src/ui/`.

## Test-Driven Development

Write tests before implementation code. Include corner cases, positive and negative tests.

- Test framework: custom `test_framework.h` with `TEST_BEGIN()`/`TEST_END()`, `ASSERT_EQ`, `ASSERT_STR_EQ`, `ASSERT_TRUE`, `ASSERT_NULL`, `ASSERT_NOT_NULL`.
- All test functions are declared and called in `tests/runner.c`.
- Tests run natively on Linux — Win32 API calls are stubbed or excluded.

## Software Development Rules for Claude

When planning and debugging, use the latest version of Opus. When implementing the changes, use the latest version of Sonnet. Once implemented ensure that Opus checks Sonnet's work.

## Two config.h Files

There are two copies of `config.h`: `include/config.h` (used by Windows cross-compile) and `src/config/config.h` (used by native test builds). When modifying the `Settings` struct, **both must be updated in sync** or one build will fail with missing-member errors.

## Cross-Compile Pitfalls

- **winsock2.h before windows.h**: Any file that transitively includes `windows.h` (e.g., via `ssh_session.h`) and also needs winsock must `#include <winsock2.h>` first, or you get redefinition errors.
- **`_snwprintf` is MSVC-only**: Use `swprintf` (ISO C) instead for MinGW compatibility.
- **`-Wshadow` is strict**: Local variables must not shadow function parameters. Common trap: naming a local `msg` inside a WndProc that has `UINT msg` as a parameter.
- **`-Wconversion` catches size_t/int mismatches**: When calling functions that take `size_t`, cast explicitly (e.g., `(size_t)strlen(cmd)` not `(int)strlen(cmd)`).
- **Missing `#include <stdio.h>`**: If a `.c` file uses `snprintf` but only includes domain headers, MinGW will error on implicit declaration. Always include `<stdio.h>` explicitly.

## Terminal Buffer

- `TermRow.len` tracks actual written content width. `TermRow.width` is the allocated capacity but is **not initialized** when rows are allocated with `xmalloc`. Always use `row->len` for content boundaries.
- When extracting terminal text, skip trailing empty rows to avoid spurious blank lines. Use a two-pass approach: first find last non-empty row, then extract.

## JSON Handling

- Use `json_parse()` + `json_obj_get()`/`json_obj_str()` for reading. Always `json_free()` the root.
- For building JSON output (e.g., AI request bodies), use `snprintf` with manual escaping via a `json_escape_str()` helper. There is no JSON builder library.
- Config fields are fixed-size `char[256]` arrays — no heap allocation per field.

## Secrets

- Use `secure_zero()` (from `src/core/secure_zero.h`) to wipe passwords and keys — never plain `memset`, which the compiler can optimize away.
- API keys are stored encrypted in `nutshell.config` using `crypto_encrypt()`/`crypto_decrypt()`, same as profile passwords.
