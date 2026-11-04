# ai_chat.c Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut `src/ui/ai_chat.c` (6,055 lines) into six responsibility files, a residual `ai_chat.c` and `ai_chat_internal.h`, with no behaviour change.

**Architecture:** Pure cut-and-paste. Shared types, macros and prototypes go into `src/ui/ai_chat_internal.h`. A function loses `static` only when another file calls it, and then takes the `ai_chat_` prefix. One commit per move, each cross-compiled with `-Werror`. One version bump and one committed exe, both in the last task.

**Tech Stack:** C11, MinGW-w64 cross-compile (`x86_64-w64-mingw32-gcc`, `-flto`), Win32.

**Spec:** `docs/superpowers/specs/2026-11-05-ai-chat-split-design.md`. Read it with this plan. Its §3 (ranges), §4 (rename table) and §5 (order) are binding.

## Global Constraints

- Worktree `~/nutshell-wt/split`, branch `refactor/ai-chat-split`, based on `main` `1f85a3a`. Host `automaton` only (`hostname`). All rules in `CLAUDE.md` apply.
- **Line numbers** are at `1f85a3a`. After earlier tasks they shift. Find each block by name: it starts at the first named function's leading comment and ends at the last one's closing `}`. Copy bytes as they are. Do not reflow, re-indent or retype anything.
- **No behaviour change.** Allowed edits: moving lines, dropping `static`, `static` → `static inline` (two helpers, Task 1), renaming per spec §4 (in `src/ui/ai_chat*.c` and the header, comments included, **never inside a string literal**), prototypes, include boilerplate, deleting the three forward declarations spec §3 names, and a 1-3 line comment at the top of each new file. `WM_AI_*`, `IDC_*` and `TIMER_*` values are unchanged (`WM_AI_GET_POLICY` = `WM_USER + 104`).
- **New `.c` frame:** spec §1, exactly: winsock2 block, `#include "ai_chat_internal.h"`, `#ifdef _WIN32`, `#include <stdio.h>`, code, `#endif /* _WIN32 */`.
- **Line endings:** `src/ui/ai_chat.c` is LF. All new files are LF. Every other touched file keeps its endings.
- **No Makefile edit.** No change to any test's rules or to any CI file.
- **Build check** (every task): `make clean && make release 2>&1 | tee "$T/build.log"; rc=${PIPESTATUS[0]}; git restore build/win/nutshell.exe; echo rc=$rc; grep -cE 'warning:|error:' "$T/build.log"`. Expected `rc=0` and `0`. `$T` is a `mktemp -d` scratch directory outside the repo. The exe is restored every time, on success and on failure. It is committed only in Task 9.
- **Per-commit checks** (every task, before committing):
  1. `git diff --cached --stat` and `git diff --cached --stat --ignore-cr-at-eol` print the same numbers.
  2. `git ls-files --eol src/ui/ai_chat*.c src/ui/ai_chat_internal.h` shows `i/lf` for every file.
  3. `grep -nE 'RGB\( *[0-9]|#define S\(|CLV_SCALE|MulDiv\([^)]*, *96 *\)' src/ui/ai_chat*.c` prints nothing.
  4. `grep -c CreateFont src/ui/ai_chat*.c` sums to 8.
  5. Every non-static top-level name in the split files has the prefix: `grep -nE '^[A-Za-z_].*\(' src/ui/ai_chat_*.c src/ui/ai_chat_internal.h | grep -vE ':(static|typedef|extern) |ai_chat_'` prints nothing.
  6. `git status --short`: read every `??` line, stage new files by path, and never use `git add -u` on its own.
- **Commit trailer**, exactly (blank line between `Models:` and `Co-Authored-By:`):

```
Models: claude-opus-5-5 (orchestration, review); claude-sonnet-5-5 (implementation, task review)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg
```

## Review Focus

1. **A dropped `static` without the prefix.** `settings.c` has its own `static void relayout(HWND, SettingsDlgData *)` and `static void add_tooltip(HWND, HWND, const char *)`. An unprefixed global `relayout`/`add_tooltip` gives two unrelated functions the same name in one exe. It becomes a conflicting-types error the moment either file sees the other's prototype, and it misleads grep and the debugger. This is pinned by per-commit check 5 in every move task.
2. **Include order that breaks `winsock2.h`/`windows.h`.** `ai_chat.h` includes `<windows.h>`. If any project header comes before `winsock2.h`, the build fails with redefinition errors. Pinned in Task 1 Step 2 (the header's first include) and each move task's frame check (`head -6` of the new file).
3. **A missing `<stdio.h>`.** Every split file except `ai_chat_input.c` calls `snprintf`. The header currently pulls `<stdio.h>` in, but `CLAUDE.md` requires it explicitly. Pinned in Task 9 Step 4: `grep -L '#include <stdio.h>' src/ui/ai_chat_*.c` prints nothing.
4. **`-Wshadow` from a newly global name.** A parameter or local in any `ai_chat*.c` that matches one of the 42 exported names breaks `-Werror`. Fix it by checking the rename table, never by editing the moved code. Pinned by the build check in each task.
5. **Unused-static warnings under `-flto`.** A plain `static` function or object in the header warns in every TU that does not use it. So `CLSID_PNG` must stay out of the header, and the two `active_*` helpers must be `static inline`. A private `static` left in `ai_chat.c` after its last caller moves also warns. Pinned in Task 1 Step 3 (`grep -nE '^static ' src/ui/ai_chat_internal.h | grep -v 'static inline'` prints nothing) and by every build check.

---

### Task 1: `ai_chat_internal.h`

**Files:** Create `src/ui/ai_chat_internal.h`. Modify `src/ui/ai_chat.c:1-441`.

**Interfaces:**
- Produces: the header, with an include guard `NUTSHELL_AI_CHAT_INTERNAL_H` and every type and macro below, used unchanged by all later tasks. `static inline SessionKind active_session_kind(const AiChatData *d)` and `static inline const char *active_shell_name(const AiChatData *d)`.

- [ ] **Step 0: Baseline.** On the untouched branch, run `make test 2>&1 | tail -3` and `xvfb-run -a make wintest 2>&1 | tail -3`. Record both counts (0 failed) for Task 9. If Wine reports that no driver could be loaded, check `pgrep -a wine` first, and run `wineserver -k` only when no other session's run shows up there.
- [ ] **Step 1: Create the header.** It holds the guard, then `#ifdef _WIN32` / `#include <winsock2.h>` / `#endif`, then `#include "ai_chat.h"`, then inside `#ifdef _WIN32` these `ai_chat.c` lines in this order: 9-62 (includes), 64-88 (GDI+ declarations), 98-148 (`IDC_*`, `WM_AI_*`, `TIMER_*`), 171-421 (`AiChatData` with its comment), 425-438 (the two `active_*` helpers, each `static` → `static inline`), 440-441 (`ACTIVE_BUSY`). End with an empty `/* Prototypes */` area, then `#endif` ×2.
- [ ] **Step 2: Rework `ai_chat.c`.** Delete the lines copied in Step 1. Lines 1-8 become: winsock2 block, blank, `#include "ai_chat_internal.h"`, blank, `#ifdef _WIN32`. Keep 90-96 (`CLSID_PNG`, `AI_CHAT_CLASS`), 150-169, 423 and 443-451. Check: `head -12 src/ui/ai_chat_internal.h` shows `winsock2.h` before `"ai_chat.h"`.
- [ ] **Step 3: Check.** `grep -nE '^static ' src/ui/ai_chat_internal.h | grep -v 'static inline'` prints nothing. Run the build check: `rc=0`, `0`.
- [ ] **Step 4: Commit** after the per-commit checks: `refactor(ai): add ai_chat_internal.h with the panel's shared types`.

### Task 2: stream thread → `ai_chat_stream.c`

**Files:** Create `src/ui/ai_chat_stream.c`. Modify `src/ui/ai_chat.c` (453-472, 474-958), `src/ui/ai_chat_internal.h`.

**Interfaces:**
- Produces: `unsigned __stdcall ai_chat_stream_thread_proc(void *raw_arg);` and `void ai_chat_fetch_on_hop(void *user, const char *display_url, int refused);`. In the header: `AiResponseMsg` and `AiStreamChunk`.

- [ ] **Step 1:** Move 453-472 (the stream-id comment, `AiResponseMsg`, `AiStreamChunk`) into the header, after `ACTIVE_BUSY`.
- [ ] **Step 2:** Move 474-958 (`StreamContext` through `ai_stream_thread_proc`) into the new file, inside the spec §1 frame. Drop `static` on `ai_stream_thread_proc` and `fetch_on_hop` and rename them to `ai_chat_stream_thread_proc` and `ai_chat_fetch_on_hop`, including the two uses in `launch_stream_thread` (`s->fetch.on_hop = …`, `_beginthreadex(…)`). The other six functions and `StreamContext` stay `static`.
- [ ] **Step 3:** Add both prototypes under `/* ai_chat_stream.c */` in the header. `head -6 src/ui/ai_chat_stream.c` matches the frame. Run the build check: `rc=0`, `0`.
- [ ] **Step 4: Commit:** `refactor(ai): move the stream thread to ai_chat_stream.c`.

### Task 3: message box → `ai_chat_input.c`

**Files:** Create `src/ui/ai_chat_input.c`. Modify `src/ui/ai_chat.c` (90-94, 150-154, 2623-2831, 3378-3384), the header.

**Interfaces:**
- Produces: `LRESULT CALLBACK ai_chat_input_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);`, `int ai_chat_input_paste_text(HWND hInput, HWND clip_owner);`, `void ai_chat_input_sync_scroll(AiChatData *d);`

- [ ] **Step 1:** Move `CLSID_PNG` (90-94, as a file `static const`), `clipboard_to_png_attachment` (2623-2695), `input_paste_text` and `InputSubclassProc` (2699-2831), and `input_sync_scroll` (3378-3384) into the new file. Delete the forward declarations at 150-154 and 2697.
- [ ] **Step 2:** Rename per spec §4 and drop `static` on the three exported functions. This covers the `RemoveWindowSubclass` self-reference and the callers in `ai_chat.c`: `SetWindowSubclass` in `WM_CREATE`, `chat_apply_zoom`, and the `WndProc` uses of paste and sync. `clipboard_to_png_attachment` stays `static`.
- [ ] **Step 3:** Add the prototypes under `/* ai_chat_input.c */`, then check the frame and run the build check.
- [ ] **Step 4: Commit:** `refactor(ai): move the message box subclass and paste to ai_chat_input.c`.

### Task 4: dispatcher → `ai_chat_dispatch.c`

**Files:** Create `src/ui/ai_chat_dispatch.c`. Modify `src/ui/ai_chat.c` (1608-1644, 1900-1948, 1996-2621), the header.

**Interfaces:**
- Consumes: `active_session_kind`, `active_shell_name`, `ACTIVE_BUSY` (Task 1).
- Produces: `CmdBatch *ai_chat_resolve_batch(AiChatData *d, LPARAM lParam);`, `void ai_chat_settle_batch_if_done(AiChatData *d, CmdBatch *batch);`, `void ai_chat_dispatch_start(AiChatData *d, int batch_id);`, `void ai_chat_maybe_start_next_batch(AiChatData *d);`, `void ai_chat_dispatch_cancel(AiChatData *d, const char *status_msg, int notify);`, `void ai_chat_dispatch_tick(AiChatData *d);`. Also, still defined in `ai_chat.c`: `void ai_chat_send_continue_message(AiChatData *d, const char *msg_text, int unattended);` and `void ai_chat_start_indicator(AiChatData *d, const char *base);`.

- [ ] **Step 1:** Move `resolve_batch`–`settle_batch_if_done` (1608-1644), `execute_command` (1900-1948) and `dispatch_start`–`dispatch_tick` (1996-2621) into the new file, keeping that order. `execute_command`, `describe_cursor_row` and `dispatch_log_stall_if_due` stay `static`.
- [ ] **Step 2:** Rename the six exported functions. In `ai_chat.c`, also drop `static` on `send_continue_message` and `start_indicator` and rename them, because the dispatcher calls both. Put their prototypes under `/* ai_chat_send.c */` and `/* ai_chat_view.c */`.
- [ ] **Step 3:** Check that the log text survived. `grep -c '"dispatch_start: batch=%d kind=%s shell=%s mode=%s"' src/ui/ai_chat_dispatch.c` prints `1`, and `grep -n '"ai_chat_' src/ui/ai_chat*.c` prints nothing.
- [ ] **Step 4:** Check the frame and run the build check, then commit: `refactor(ai): move the command dispatcher to ai_chat_dispatch.c`.

### Task 5: send path → `ai_chat_send.c`

**Files:** Create `src/ui/ai_chat_send.c`. Modify `src/ui/ai_chat.c` (1323-1606, 1646-1898, 1950-1994), the header.

**Interfaces:**
- Consumes: `ai_chat_stream_thread_proc`, `ai_chat_fetch_on_hop` (Task 2), `ai_chat_start_indicator` (Task 4), `AiResponseMsg`/`AiStreamChunk`.
- Produces: `void ai_chat_register_tools(AiChatData *d, const char *search_provider, const char *search_url, int max_search_results, int web_fetch_enabled);`, `void ai_chat_launch_stream_thread(AiChatData *d, const char *src);`, `void ai_chat_abort_session_stream(AiChatData *d, AiSessionState *state);`, `void ai_chat_drain_stream_messages(HWND hwnd);`, `void ai_chat_cancel_active_stream(AiChatData *d);`, `void ai_chat_send_user_message(AiChatData *d);`. `ai_chat_send_continue_message` moves here too. Also, still in `ai_chat.c`: `void ai_chat_update_context_bar(AiChatData *d);`.

- [ ] **Step 1:** Move `debug_tool_active`–`drain_stream_messages` (1323-1606), `cancel_active_stream`–`send_user_message` (1646-1898) and `ai_chat_send_continue_message` (1950-1994) into the new file. `debug_tool_active`, `launch_failed`, `ctx_buffers_ensure` and `input_text_utf8` stay `static`.
- [ ] **Step 2:** Rename the six exported functions. In `ai_chat.c`, drop `static` on `update_context_bar` and rename it to `ai_chat_update_context_bar`, because `launch_failed` and `cancel_active_stream` call it. Its prototype goes under `/* ai_chat_status.c */`.
- [ ] **Step 3:** Check the frame and run the build check, then commit: `refactor(ai): move the send path and stream lifecycle to ai_chat_send.c`.

### Task 6: status line → `ai_chat_status.c`

**Files:** Create `src/ui/ai_chat_status.c`. Modify `src/ui/ai_chat.c` (423, 1002-1129, 2995-3198, 3309-3376), the header.

**Interfaces:**
- Produces: `void ai_chat_invalidate_status_line(AiChatData *d);`, `void ai_chat_request_policy_change(HWND hwnd, AiChatData *d, CmdPolicy next);`, `int ai_chat_sash_hit(AiChatData *d, int x, int y);`, `void ai_chat_paint_sash(AiChatData *d, HDC hdc);`, `void ai_chat_get_status_paint(AiChatData *d, HDC hdc, AiStatusPaint *out);`, `int ai_chat_status_hit(AiChatData *d, int x, int y);`, `int ai_chat_status_rect(AiChatData *d, int id, RECT *out_rc);`, `void ai_chat_status_tools_update(AiChatData *d);`, `void ai_chat_paint_status_line(AiChatData *d, HDC hdc);`. `ai_chat_update_context_bar` moves here. Also, still in `ai_chat.c`: `void ai_chat_compute_layout(AiChatData *d, AiPanelLayout *out);` and `RECT ai_chat_to_RECT(NsRect r);`. In the header: `STATUS_HIT_*`, `STATUS_TOOL_*`, `AiStatusPaint`.

- [ ] **Step 1:** Move 3004-3022 (`STATUS_HIT_*`, `STATUS_TOOL_*` and `AiStatusPaint`, with their comments) into the header, before the prototypes.
- [ ] **Step 2:** Move `invalidate_status_line`–`update_context_bar` (1002-1129), the status banner comment (2995-3002), `ai_chat_format_meter_text`–`status_tools_update` (3024-3198) and `paint_status_line` (3309-3376) into the new file. Delete the forward declaration at 423. `apply_policy_change`, `ai_chat_format_meter_text`, `ai_chat_status_rect_for_id` and `ai_chat_pt_in_rect` stay `static`.
- [ ] **Step 3:** Rename per spec §4 and drop `static` on the exported functions. In `ai_chat.c`, drop `static` on `ai_chat_compute_layout` and `ai_chat_to_RECT`, with their prototypes under `/* ai_chat_view.c */`.
- [ ] **Step 4:** Check the frame and run the build check, then commit: `refactor(ai): move the status line and policy control to ai_chat_status.c`.

### Task 7: layout and painting → `ai_chat_view.c`

**Files:** Create `src/ui/ai_chat_view.c`. Modify `src/ui/ai_chat.c` (156-169, 961-1000, 1131-1321, 2833-2993, 3200-3307, 3386-3443), the header.

**Interfaces:**
- Consumes: `ai_chat_input_sync_scroll` (Task 3), `ai_chat_invalidate_status_line`, `ai_chat_status_tools_update`, `ai_chat_update_context_bar` (Task 6).
- Produces: `LRESULT CALLBACK ai_chat_btn_noerase_subclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);`, `int ai_chat_unscale(int px, int dpi);`, `int ai_chat_input_h(const AiChatData *d);`, `void ai_chat_update_panel_state(AiChatData *d);`, `void ai_chat_thinking_history_clear(AiChatData *d);`, `void ai_chat_rebuild_display(AiChatData *d);`, `void ai_chat_add_tooltip(HWND hTooltip, HWND hCtrl, const char *text);`, `void ai_chat_draw_header_icon_button(LPDRAWITEMSTRUCT dis, const ThemeColors *theme, int dpi, NsIconId icon);`, `void ai_chat_relayout(AiChatData *d);`, `void ai_chat_paint_header(AiChatData *d, HDC hdc);`, `void ai_chat_apply_zoom(AiChatData *d, int delta);`. `ai_chat_start_indicator`, `ai_chat_compute_layout` and `ai_chat_to_RECT` move here too. In the header: `BTN_NOERASE_SUBCLASS_ID`, `AI_INPUT_BASE_H`.

- [ ] **Step 1:** Move line 159 (`BTN_NOERASE_SUBCLASS_ID`) and 961-963 (`AI_INPUT_BASE_H` with its comment) into the header.
- [ ] **Step 2:** Move these into the new file: 156-158 and 160-169 (`btn_noerase_subclass`), 965-1000 (`ai_chat_unscale`–`ai_chat_compute_layout`), 1131-1321 (`update_panel_state`–`chat_rebuild_display`), 2833-2993 (`add_tooltip`–`relayout`), 3200-3307 (`ai_chat_to_RECT`, `paint_header`) and 3386-3443 (`chat_apply_zoom`, with its 3 `CreateFont` calls unchanged). `append_batch_command_items` and `ai_chat_header_chip` stay `static`.
- [ ] **Step 3:** Rename per spec §4. This covers `btn_noerase_subclass`'s `RemoveWindowSubclass` self-reference and the `WM_CREATE` subclass loop.
- [ ] **Step 4:** Check that `ai_chat.c` now holds only the spec §3 residue: `grep -nE '^static ' src/ui/ai_chat.c` lists exactly: `AI_CHAT_CLASS`, `do_session_switch` (declaration and definition), `AiChatWndProc`, `ai_chat_seed_policy`, `ai_streams_done`. Check the frame and run the build check.
- [ ] **Step 5: Commit:** `refactor(ai): move layout, header and zoom to ai_chat_view.c`.

### Task 8: stale comments (mechanical)

**Files:** Modify comments only, in the files and lines listed in spec §6: `src/ui/window.c`, `src/ui/settings.c`, `src/core/dispatch_line_clear.h`, `src/core/chat_msg.h`, `src/core/shell_prompt.h`, `src/core/paste_filter.h`, `src/core/ui_demo.c`, `tests/test_ai_stream.c`, `tests/test_local_pty.c`, `tests/test_ai_prompt.c`, `tests/test_ui_demo.c`.

- [ ] **Step 1:** Replace each old function name with its spec §4 name. Where the comment says `src/ui/ai_chat.c` (or `ai_chat.c's`) for a moved function, name its new file instead. `execute_command()` keeps its name; only its file changes, to `ai_chat_dispatch.c`. Leave `settings.c`'s own `relayout()`/`add_tooltip()` references alone; only line 2102 refers to the panel.
- [ ] **Step 2:** `git diff -U0 | grep '^[-+][^-+]' | grep -vE '^\s*[-+]\s*(/?\*|//)'` prints nothing, so only comment lines changed. Run `make test`: same count as Step 0, 0 failed. Run the build check.
- [ ] **Step 3: Commit:** `docs(ai): name the split ai_chat functions in comments`.

### Task 9: bump, build, prove, commit the exe

**Files:** Modify `src/ui/resource.h` (`APP_VERSION`, `APP_VERSION_BINARY`), `README.md` (`**Version**:` line, the `ai_chat.c/.h` tree line 657), `build/win/nutshell.exe`.

- [ ] **Step 1: Bump.** Set `V` to patch + 1 over the greater of `git show origin/main:src/ui/resource.h` and the working tree's `APP_VERSION`, after `git fetch origin`. After `.99` comes the next minor. Write `V` into `APP_VERSION`, `APP_VERSION_BINARY` (`X,Y,Z,0`) and README's `**Version**: vX.Y.Z`. List the new files under `ai_chat.c/.h` in README's tree, with one-phrase descriptions in the existing column layout.
- [ ] **Step 2: Build.** `make clean && make release`: exit 0, no `warning:`. Do **not** restore the exe this time.
- [ ] **Step 3: Tests.** `make test`: Step-0 count, 0 failed, and `make test 2>&1 | grep -c 'warning:'` is 0. `xvfb-run -a make wintest`: Step-0 count, 0 failed. `make lint`: 0 errors, 0 warnings. If the move itself produces a new cppcheck finding, stop and report it; do not suppress it.
- [ ] **Step 4: Static proofs.** `grep -L '#include <stdio.h>' src/ui/ai_chat_*.c` prints nothing (Review Focus 3). The six per-commit checks pass. `bash .github/scripts/check-version.sh "$(git merge-base origin/main HEAD)"` passes.
- [ ] **Step 5: Move proof.** Write `$T/unrename.sed` with one line per spec §4 rename, new → old, for example `s/\bai_chat_relayout\b/relayout/g`, `s/\bai_chat_rebuild_display\b/chat_rebuild_display/g`, `s/\bai_chat_input_subclass_proc\b/InputSubclassProc/g`, `s/\bai_chat_stream_thread_proc\b/ai_stream_thread_proc/g`, and so on for all 34. Then run:

```bash
norm() { sed -E 's/^static (inline )?//' | grep -v '^[[:space:]]*$' | sort; }
git show origin/main:src/ui/ai_chat.c | norm > "$T/before"
cat src/ui/ai_chat.c src/ui/ai_chat_*.c src/ui/ai_chat_internal.h | sed -E -f "$T/unrename.sed" | norm > "$T/after"
diff "$T/before" "$T/after" | grep '^[<>]'
git diff --stat origin/main...HEAD -- src/ui/
```

Expected: every `>` line is boilerplate (guard, `#ifdef`/`#endif`, include lines, top-of-file comments, `/* ai_chat_<x>.c */` group comments) or part of a prototype. Every `<` line is part of the three deleted forward declarations (including the `/* Forward declaration for input subclass */` comment above the first). Anything else means a line changed in transit: find it and restore it. Then page through `git diff --color-moved=dimmed-zebra --color-moved-ws=allow-indentation-change origin/main...HEAD -- src/ui/ | less -R`. The only uncoloured `-`/`+` lines should be renamed call sites, dropped `static`, prototypes and boilerplate. Record `wc -l src/ui/ai_chat.c src/ui/ai_chat_*.c src/ui/ai_chat_internal.h` for the PR body.
- [ ] **Step 6: Commit** `src/ui/resource.h README.md build/win/nutshell.exe` (by path; check `git status --short` first): `build: split ai_chat.c (vX.Y.Z)`.
- [ ] **Step 7: Hand off.** Push and open a **draft** PR. Its body carries the file table with `wc -l`, the test, wintest and lint counts, the move-proof result, the manual Windows checks still owed (`--ui-demo=all` on four themes; one session that streams a reply, pastes an image and runs one approved command; harness `-Tier gate` AI-panel cases), the `Models:` line and the Claude Code footer. The maintainer marks it ready and merges.
