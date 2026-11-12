# Thinking row modes: plan

Spec: `docs/superpowers/specs/2026-11-12-thinking-row-modes-design.md`.
Branch `feat/thinking-row-modes` from `f842775`. Each task is a Sonnet
sub-agent (`claude-sonnet-5-5`), tests first, line endings preserved
(`git diff --stat` must equal `git diff --stat --ignore-cr-at-eol`).

## Task 1: portable pieces (native-testable)

1. Tests, then `src/core/thinking_mode.[ch]` (the spec's model section);
   register the tests in `tests/runner.c`.
2. Tests, then `Settings.ai_thinking_mode`: default, load table (missing,
   each token, unknown, wrong case, non-string), round trip, validate clamp,
   `test_config_roundtrip_all_fields`.
3. Three `ThemeChatColors` tokens, values for the four themes, contrast
   pairs at `FLOOR_TEXT` on the background the row is painted on.
4. `SH_AI_THINKING_MODE` in `settings_help` (count 47 -> 48, default text
   "Automatic"), and its tests.
5. `make -j test`. Commit.

## Task 2: Windows UI wiring

1. `chat_msg.h`: `thinking_mode` replaces `thinking_collapsed`, drop
   `thinking_user_set`; fix `chat_msg.c`, `chat_copy.c`, `chat_rebuild.c`
   and their tests.
2. `chat_listview.c`: pref setter, mode in `build_thinking_layout()`, compact
   offset and no thumb, wheel only in open, label colour, chevron, click
   cycle posting `IDC_CHAT_THINKING_MODE`.
3. `ai_chat*.c`: drop `show_thinking`; handle `IDC_CHAT_THINKING_MODE`
   (update and save config); push the pref at startup and after Settings
   (`window.c`); Save export rule; make sure a stopped or failed reply sets
   `thinking_complete`.
4. `settings.c`: the "Thinking display:" combo, read on OK.
5. `--ui-demo=thinking-compact|thinking-open` (`ui_demo.c`, `ai_chat.c`),
   with tests.
6. README (Thinking description, Behaviour list), integration
   `settings_help_tool_count` 82 -> 83 and its README line.
7. `make -j test`, `make test SAN=1`, `make lint-gate`,
   `make clean && make release` (compile check, no bump, then
   `git restore build/win/nutshell.exe`), `xvfb-run -a make wintest`.
   Commit.

## Task 3: final review

A fresh Opus agent reads `git diff origin/main...HEAD` and the spec and
returns ranked findings; they go to the maintainer before any fix.
