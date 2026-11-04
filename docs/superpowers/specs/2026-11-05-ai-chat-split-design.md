# Split of `src/ui/ai_chat.c` — design

Date: 2026-11-05. Status: scope decided by the maintainer's session on
2026-11-04; the decisions below are the author's unless marked "decided".
Base: `main` at `1f85a3a` (the merge of PR #85, whose `src/ui/ai_chat.c` is
byte-identical to `8417dba`). Every line number below is at that commit.
Branch `refactor/ai-chat-split` (worktree `~/nutshell-wt/split`).

## Why

`src/ui/ai_chat.c` is 6,055 lines: the AI stream thread, the command
dispatcher, the send path, the status line, the panel's layout and painting,
the message box's subclass, a 1,854-line window procedure and the public API
all share one file. Every AI-panel change touches it, every review has to
page through it, and a sub-agent cannot hold it in context. The file has no
file-scope mutable state (everything lives in `AiChatData`, reached through
`GWLP_USERDATA` on the UI thread; the stream thread owns its `AiStream`), so
it splits along responsibility lines with one internal header and no new
accessors.

## Goals

1. `src/ui/ai_chat.c` is cut into seven files plus one internal header, each
   with one responsibility (decided):

   | File | Holds | Size (approx.) |
   |---|---|---|
   | `ai_chat_internal.h` | shared includes, GDI+ flat-API declarations, `IDC_*`, `WM_AI_*`, `TIMER_*`, `AiChatData`, `AiResponseMsg`, `AiStreamChunk`, `STATUS_*` macros, `AiStatusPaint`, `ACTIVE_BUSY`, the two `active_*` helpers, prototypes | ~500 |
   | `ai_chat_stream.c` | SSE parsing and the background stream thread | ~495 |
   | `ai_chat_input.c` | message-box subclass, paste (text and clipboard PNG), its scrollbar sync | ~230 |
   | `ai_chat_dispatch.c` | batch resolution and the prompt-gated command dispatcher | ~725 |
   | `ai_chat_send.c` | tool registry, stream launch/abort/cancel, user and continue turns | ~595 |
   | `ai_chat_status.c` | policy changes, context meter, status line geometry, hit-test, tooltips, paint, sash | ~390 |
   | `ai_chat_view.c` | layout helpers, panel state, busy indicator, display rebuild, header, relayout, zoom | ~585 |
   | `ai_chat.c` (residual) | window class, `AiChatWndProc`, public API (`ai_chat.h`), `do_session_switch`, `ai_chat_seed_policy`, `ai_streams_done` | ~2,640 |

2. No behaviour change of any kind: same messages, same values, same log
   text, same exe behaviour. The only edits besides moving lines are dropping
   `static` on symbols another file now calls, the `ai_chat_` prefix on those
   symbols, prototypes, include boilerplate, and the deletion of three
   forward declarations the header replaces.
3. Each move is one commit that cross-compiles clean with `-Werror`.
4. Comments elsewhere in `src/` and `tests/` that name a renamed function
   name it by its new name.

## Non-goals

- The "phase 2" extraction of `AiChatWndProc` cases into `on_xxx` handlers
  (decided: out of scope). The window procedure moves nowhere.
- Trimming include lists. The original include block moves verbatim into the
  header; pruning it is a later, separate change.
- Making any of this natively testable (moving logic to `src/core/`).
- Renaming anything that stays file-private, or tidying comments beyond the
  renamed names (positional words such as "below" in a moved comment stay).
- Removing the unused `#include "markdown.h"` (ai_chat.c:451; no `md_*`
  symbol is used). It stays where it is.
- Updating `.claude/memory/MEMORY.md`'s line counts (the next retrospective
  does that).

## Anti-goals (would be a failure even if it technically works)

- **Any behaviour change smuggled into a move**, including a log string:
  `"dispatch_start: batch=%d kind=%s shell=%s mode=%s"` (ai_chat.c:2035) is
  log text, not a symbol, and keeps its exact spelling.
- **A changed message or control id.** `WM_AI_GET_POLICY` stays
  `WM_USER + 104` (the harness's `NutshellIT.psm1:569` sends it); every
  `WM_AI_*`, `IDC_*` and `TIMER_*` value is unchanged (decided).
- **Changing a gate or a test to make the split pass** (`test_ui_tokens.c`,
  `test_dlog_gate.c`, `check-version.sh`, `checks.yml`).
- **`ai_chat_internal.h` included anywhere but `src/ui/ai_chat*.c`.**
- **A committed exe that was not built from the final commit's sources**, or
  an exe committed before the version bump.
- **Line endings flipped.** A whole-file CRLF/LF flip hides the move from
  review.

## Constraints

- Every rule in `CLAUDE.md` applies. In particular:
  - Windows flags `-std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow
    -Wformat=2 -Wconversion -flto`; every file compiles clean.
  - `#include <winsock2.h>` before `<windows.h>`. `ai_chat.h` includes
    `<windows.h>` directly, so `winsock2.h` must come first in every TU.
  - Any `.c` that calls `snprintf` includes `<stdio.h>` explicitly.
  - Version bump before every build that is committed; patch never past 99.
- `src/ui/*.c` is compiled only by `make release` (it is in `NON_TEST_SRCS`),
  so `make clean && make release` on `automaton` is the only compile check
  for these files. The Makefile picks new files up by wildcard: no Makefile
  edit (decided).
- `tests/test_ui_tokens.c` scans every `src/ui/*.c` (not `.h`): no literal
  `RGB(`, no `#define S(`/`CLV_SCALE`, no `MulDiv(..., 96)` may appear in a
  new file. It does not scan for `CreateFont`; `ai_chat.c` has 8 calls today
  (3 in `chat_apply_zoom`, 5 in `WM_CREATE`) and the split must neither add
  nor remove one: the total over `src/ui/ai_chat*.c` stays 8.
- `test_dlog_gate.c` scans by path and line but has no `ai_chat.c` entry in
  its allow-list, so moving DLOG calls is safe.
- **Line endings.** `src/ui/ai_chat.c` is LF in the index (`git ls-files
  --eol`: `i/lf w/lf`); `src/ui/ai_chat.h` is CRLF and is not touched. Every
  new file is LF. The other touched files (`window.c`, `settings.c`,
  `src/core/{dispatch_line_clear.h,chat_msg.h,shell_prompt.h,paste_filter.h,
  ui_demo.c}`, `tests/test_{ai_stream,local_pty,ai_prompt,ui_demo}.c`,
  `README.md`, `src/ui/resource.h`) keep their endings. `git diff --stat`
  must equal `git diff --stat --ignore-cr-at-eol` at every commit.
- Commits: subjects `refactor(ai): move <what> to ai_chat_<x>.c`; the final
  one `build: split ai_chat.c (vX.Y.Z)`. Trailer block, exactly:

  ```
  Models: claude-opus-5-5 (orchestration, review); claude-sonnet-5-5 (implementation, task review)

  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01AHuBtBVZJxrKdjJtkyPnxg
  ```

## What has been decided

### 1. File layout and the shape of every new file

Each new `.c` file mirrors `ai_chat.c`'s frame, so `ai_chat_internal.h` is
always the first project header and `winsock2.h` precedes `windows.h`:

```c
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"

#ifdef _WIN32
#include <stdio.h>
/* ...moved code... */
#endif /* _WIN32 */
```

`ai_chat_internal.h` has an include guard (`NUTSHELL_AI_CHAT_INTERNAL_H`),
includes `winsock2.h` (under `_WIN32`) then `"ai_chat.h"` outside the
`_WIN32` block (as `ai_chat.c` does, so no TU is ever empty under
`-Wpedantic`), then everything else inside `#ifdef _WIN32`. Its prototypes
come last, grouped under a `/* ai_chat_<x>.c */` comment per owning file.

### 2. What goes into the header

From `ai_chat.c` at `1f85a3a`, verbatim except where noted:
lines 9-62 (include block), 64-88 (GDI+ flat-API typedefs and externs:
`GdiplusStartup`/`Shutdown` are used by the window procedure, the image
functions by the paste code), 98-148 (`IDC_*`, `WM_AI_*`, `TIMER_*`),
171-421 (`AiChatData` with its threading comment), 425-438
(`active_session_kind`, `active_shell_name`, changed from `static` to
`static inline`; send, dispatch and the residual all call them), 440-441
(`ACTIVE_BUSY`), 453-472 (`AiResponseMsg`, `AiStreamChunk`), 3004-3022
(`STATUS_HIT_*`, `STATUS_TOOL_*`, `AiStatusPaint`: the window procedure uses
them), 159 (`BTN_NOERASE_SUBCLASS_ID`) and 961-963 (`AI_INPUT_BASE_H`).
`CLSID_PNG` (90-94) goes to `ai_chat_input.c`, its only user, as a file
`static const`; `AI_CHAT_CLASS` (96) stays in `ai_chat.c`.

### 3. What moves where (line ranges at `1f85a3a`, each including its leading comment)

| Target | Ranges | Functions |
|---|---|---|
| `ai_chat_stream.c` | 474-958 | `StreamContext` (private type), `stream_post_text`, `stream_process_line`, `stream_callback`, `post_response`, `post_error_response`, `post_tool_msg`, `fetch_on_hop`, `ai_stream_thread_proc` |
| `ai_chat_input.c` | 90-94, 2623-2695, 2699-2831, 3378-3384 | `CLSID_PNG`, `clipboard_to_png_attachment`, `input_paste_text`, `InputSubclassProc`, `input_sync_scroll` |
| `ai_chat_dispatch.c` | 1608-1644, 1900-1948, 1996-2621 | `resolve_batch`, `settle_batch_if_done`, `execute_command`, `dispatch_start`, `maybe_start_next_batch`, `dispatch_cancel`, `describe_cursor_row`, `dispatch_log_stall_if_due`, `dispatch_tick` |
| `ai_chat_send.c` | 1323-1606, 1646-1898, 1950-1994 | `debug_tool_active`, `chat_register_tools`, `launch_failed`, `launch_stream_thread`, `abort_session_stream`, `drain_stream_messages`, `cancel_active_stream`, `ctx_buffers_ensure`, `input_text_utf8`, `send_user_message`, `send_continue_message` |
| `ai_chat_status.c` | 1002-1129, 2995-3002, 3024-3198, 3309-3376 | `invalidate_status_line`, `apply_policy_change`, `request_policy_change`, `update_context_bar`, status banner comment, `ai_chat_format_meter_text`, `ai_chat_status_rect_for_id`, `ai_chat_pt_in_rect`, `ai_chat_sash_hit`, `paint_sash`, `ai_chat_get_status_paint`, `ai_chat_status_hit`, `ai_chat_status_rect`, `status_tools_update`, `paint_status_line` |
| `ai_chat_view.c` | 156-158 + 160-169, 965-1000, 1131-1321, 2833-2993, 3200-3307, 3386-3443 | `btn_noerase_subclass`, `ai_chat_unscale`, `ai_chat_input_h`, `ai_chat_compute_layout`, `update_panel_state`, `start_indicator`, `thinking_history_clear`, `append_batch_command_items`, `chat_rebuild_display`, `add_tooltip`, `draw_header_icon_button`, `ai_chat_header_chip`, `relayout`, `ai_chat_to_RECT`, `paint_header`, `chat_apply_zoom` |
| `ai_chat.c` keeps | 1-8 (reworked), 96, 443-451, 3445-6055 | `AI_CHAT_CLASS`, `do_session_switch` forward declaration, `markdown.h`, `AiChatWndProc`, the public API, `ai_chat_seed_policy`, `do_session_switch`, `ai_streams_done` |

Deleted, because the header's prototypes replace them: the forward
declarations of `InputSubclassProc` (150-154), `status_tools_update` (423)
and `input_sync_scroll` (2697).

### 4. Exported names (decided: `ai_chat_` prefix on every de-static'd name)

A symbol loses `static` only when another file calls it. When it does, it
gets the `ai_chat_` prefix unless it already has it; a leading `chat_` is
dropped rather than doubled, and the two CamelCase Win32-style callbacks
become snake case. The rename applies to every reference in
`src/ui/ai_chat*.c` and the header, comments included, in the commit where
the symbol first loses `static`; string literals are never touched.

| Owner | Old | New |
|---|---|---|
| stream | `ai_stream_thread_proc` | `ai_chat_stream_thread_proc` (frees the `ai_stream_` namespace for `src/core/ai_stream.h`) |
| stream | `fetch_on_hop` | `ai_chat_fetch_on_hop` |
| input | `InputSubclassProc`, `input_paste_text`, `input_sync_scroll` | `ai_chat_input_subclass_proc`, `ai_chat_input_paste_text`, `ai_chat_input_sync_scroll` |
| dispatch | `resolve_batch`, `settle_batch_if_done`, `dispatch_start`, `maybe_start_next_batch`, `dispatch_cancel`, `dispatch_tick` | same with `ai_chat_` prefix |
| send | `chat_register_tools` | `ai_chat_register_tools` |
| send | `launch_stream_thread`, `abort_session_stream`, `drain_stream_messages`, `cancel_active_stream`, `send_user_message`, `send_continue_message` | same with `ai_chat_` prefix |
| status | `invalidate_status_line`, `request_policy_change`, `update_context_bar`, `paint_sash`, `status_tools_update`, `paint_status_line` | same with `ai_chat_` prefix |
| status | `ai_chat_sash_hit`, `ai_chat_get_status_paint`, `ai_chat_status_hit`, `ai_chat_status_rect` | unchanged, `static` dropped |
| view | `btn_noerase_subclass`, `update_panel_state`, `start_indicator`, `thinking_history_clear`, `add_tooltip`, `draw_header_icon_button`, `relayout`, `paint_header` | same with `ai_chat_` prefix |
| view | `chat_rebuild_display`, `chat_apply_zoom` | `ai_chat_rebuild_display`, `ai_chat_apply_zoom` |
| view | `ai_chat_unscale`, `ai_chat_input_h`, `ai_chat_compute_layout`, `ai_chat_to_RECT` | unchanged, `static` dropped |

Stay `static` (one caller file): `StreamContext` and the six stream helpers;
`clipboard_to_png_attachment`; `execute_command` (decided),
`describe_cursor_row`, `dispatch_log_stall_if_due`; `debug_tool_active`,
`launch_failed`, `ctx_buffers_ensure`, `input_text_utf8`;
`apply_policy_change`, `ai_chat_format_meter_text`,
`ai_chat_status_rect_for_id`, `ai_chat_pt_in_rect`;
`append_batch_command_items`, `ai_chat_header_chip`; and in the residual
`ai_chat_seed_policy`, `do_session_switch`, `ai_streams_done`,
`AiChatWndProc`. None of the 42 new global names exists anywhere else in
`src/` or `tests/` (checked against `1f85a3a`, including
`ai_chat_testable.h`).

### 5. Order and the build per commit (decided)

Header, stream, input, dispatch, send, status, view, stale comments, final
build. A move that calls a function still in `ai_chat.c` exports that
function in the same commit, under its final name and in its future owner's
prototype group (dispatch exports `ai_chat_send_continue_message` and
`ai_chat_start_indicator`; send exports `ai_chat_update_context_bar`; status
exports `ai_chat_compute_layout` and `ai_chat_to_RECT`).

Every commit is checked with `make clean && make release` on `automaton`;
the rebuilt exe is restored (`git restore build/win/nutshell.exe`) and never
committed until the final task. A failed `make release` after `make clean`
deletes the tracked exe; restore it before anything else. There is one
version bump, in the final task: patch + 1 over the greater of `main`'s and
the branch's `APP_VERSION` at that moment (never a literal written in
advance; after `.99` comes the next minor).

### 6. Stale comments elsewhere

One mechanical commit renames the old names in comments of
`src/ui/window.c` (985, 2323), `src/ui/settings.c` (2102),
`src/core/dispatch_line_clear.h` (7, 60, 106, 130, 145, 164, 172),
`src/core/chat_msg.h` (223), `src/core/shell_prompt.h` (42),
`src/core/paste_filter.h` (50), `src/core/ui_demo.c` (41, 66, 318),
`tests/test_ai_stream.c` (403, 406), `tests/test_local_pty.c` (591, 593,
617, 646, 652), `tests/test_ai_prompt.c` (2394), `tests/test_ui_demo.c`
(531), and where such a comment says `src/ui/ai_chat.c` for a moved
function, the new file. `execute_command()` keeps its name (still private)
but its file becomes `ai_chat_dispatch.c`. `docs/` is history and is not
touched. `settings.c`'s own `static relayout`/`add_tooltip` are its own
functions and stay. README's source tree gains the new files in the final
task.

## Left to the builder

- A one- to three-line comment at the top of each new file saying what it
  holds.
- The order of prototypes inside each group, and of the moved blocks inside
  each new file (original order is the default).
- Line-wrapping of the new prototypes.
- The README tree wording for the new files (`ai_chat.c/.h` line 657 and its
  "(140 KB)" note), within the column layout already used there.

## Verification before the gate

- `make test`: the same count as `main` before the first commit, 0 failed,
  0 warnings (`make test 2>&1 | grep -c 'warning:'` is 0). (`src/ui` is not
  in the test build, so this proves the comment edits broke nothing.)
- `xvfb-run -a make wintest`: the same count as on `main` (45 on
  2026-11-03), 0 failed.
- `make lint`: 0 errors, 0 warnings.
- `make clean && make release` with the bump; `check-version.sh` against the
  merge base passes.
- **Proof of a pure move:** a sorted-multiset comparison of
  `main:src/ui/ai_chat.c` against the concatenation of the new files, after
  undoing the renames and stripping a leading `static `/`static inline `,
  leaves only boilerplate (guards, `#ifdef`/`#endif`, include lines, file
  comments) and prototypes. The exact command is in the plan's final task.
  Also: `git diff --color-moved=dimmed-zebra --color-moved-ws=allow-indentation-change main...HEAD -- src/ui/`
  shows every removed `ai_chat.c` block as moved; the only uncoloured
  `-`/`+` lines are renamed call sites, dropped `static`, prototypes and
  boilerplate. And `git diff --stat main...HEAD -- src/ui/`: lines added
  across the new files minus lines removed from `ai_chat.c` equals the
  boilerplate-and-prototype count from the multiset check.
- `CreateFont` count over `src/ui/ai_chat*.c` is 8; the string
  `"dispatch_start: batch=` appears once, in `ai_chat_dispatch.c`.
- `wc -l src/ui/ai_chat*.c src/ui/ai_chat_internal.h` goes in the PR body.
- On Windows, by hand (cannot be done on `automaton`): `nutshell.exe
  --ui-demo=all` on the four themes shows nothing out of place; one real
  session sends a message, streams a reply, pastes an image, and runs one
  approved command; the integration harness's `-Tier gate` AI-panel cases
  pass.
