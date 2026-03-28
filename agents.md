# Agents Guide — Lessons Learned and Tips

This file captures hard-won lessons from developing this codebase. If you are an AI assistant (or human contributor), read this before making changes.

## Build System

- **MANDATORY — Always bump the version before building.** Before running any `make` target that produces a binary, increment the patch version in **all three files**:
  1. `src/ui/resource.h` — the `APP_VERSION` string (e.g., `"0.9.31"` → `"0.9.32"`)
  2. `README.md` — the `**Version**:` line
  3. `src/ui/nutshell.rc` — `FILEVERSION`, `PRODUCTVERSION`, and both `FileVersion`/`ProductVersion` string values

  No exceptions. Every build gets a unique version number. This directive is also in `CLAUDE.md` for automatic loading.
- **Always use `make clean && make release`** for builds. Never use `make release` alone — always clean first to ensure a full rebuild.
- **Always use test driven development.** write tests before writing the code, include corner cases, positive and negative tests. 
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

- Use `secure_zero()` (from `src/core/secure_zero.h`) to wipe passwords and keys — never plain `memset`, which the compiler can optimize away. Follow this pattern for API keys and any credential fields.
- API keys are stored encrypted in `nutshell.config` using `crypto_encrypt()`/`crypto_decrypt()`, same as profile passwords.
