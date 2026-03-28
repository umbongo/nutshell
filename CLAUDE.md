# CLAUDE.md — Nutshell Project Instructions



## MANDATORY: Version Bump Before Every Build

**Before running `make` (any target that produces a binary), increment the patch version in ALL THREE files:**

1. `src/ui/resource.h` — update `APP_VERSION` string (e.g., `"0.9.31"` -> `"0.9.32"`)
2. `README.md` — update the `**Version**:` line
3. `src/ui/nutshell.rc` — update `FILEVERSION`, `PRODUCTVERSION`, and both `FileVersion`/`ProductVersion` string values

No exceptions. Every build gets a unique version number.

## Build Commands

- **Always `make clean && make release`** — never `make release` alone.
- **Always run `make test` after changes** to verify nothing is broken.

## Test-Driven Development

Write tests before implementation code. Include corner cases, positive and negative tests.

## Software development rules for Claude

when planning and debugging, use the latest version of Opus. When implementing the changes, use the latest version of sonnet. 
Once implemented ensure that Opus checks Sonnent's work.

## Full Guide

Read `agents.md` for the complete set of lessons and conventions for this codebase.

