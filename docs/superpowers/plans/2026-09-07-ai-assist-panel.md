# AI Assist Panel — Implementation Plan

> **For agentic workers:** implement task by task, in order, TDD. Do not start a
> task until the previous one's verification is green. Global constraints are
> the same as the foundation plan (version bump per binary, `mingw32-make`
> only, `-Werror` pitfalls in CLAUDE.md, CRLF check, no AI attribution in
> commits, gates in `tests/test_ui_tokens.c` must stay at zero).

**Spec:** [2026-09-07-ai-assist-panel-design.md](../specs/2026-09-07-ai-assist-panel-design.md)
**Baseline at plan time:** v1.0.92, 1,665 native tests, 12 integration cases.

| # | Task | New | Touches |
|---|---|---|---|
| 1 | Core: panel layout, status-line geometry, card v2 layout, mode labels, state copy | `src/core/ai_panel_layout.{c,h}`, `src/core/ai_panel_states.{c,h}`, `tests/test_ai_panel.c` | `src/core/ns_layout.{c,h}` (card v2), `tests/test_ns_layout.c`, `runner.c` |
| 2 | Card 1 in the chat list | — | `chat_listview.c` (paint + hit via v2 layout; Deny all / Run selected wired to existing approve/deny paths; per-row Allow/Deny removed) |
| 3 | Header + status line (segmented mode control, auto approve, meter) | `ns_draw_segmented`, `ns_draw_meter` in `ns_draw.{c,h}` | `ai_chat.c` (replace tab buttons/labels/context bar; tooltips; `ns_hover` for the status line) |
| 4 | Empty / no-key / no-session states; MessageBoxes gone; Settings opens on Provider | — | `chat_listview.c` (empty-state paint + chip/button hit), `ai_chat.c`, `window.c` (`on_ai_clicked`), `settings.c`/`settings_dlg.h` (initial page) |
| 5 | Demo states, gallery, docs, review set | — | `ui_demo.c`, `test_ui_demo.c`, `Run-Integration.ps1` (gallery + `ai_panel_opens_without_key`), README, help_guide, checkpoint |

### Task 1 — core
- `ai_panel_layout(NsRect panel, int dpi, int composer_h, AiPanelLayout *out)` → `{ header, thread, status, composer }` tiling the rect; header height `SP_XXL`, status `SZ_CTRL_H`, composer = `composer_h`.
- `ai_status_layout(NsRect status, int dpi, int seg0_text_w, int seg1_text_w, int auto_text_w, AiStatusLayout *out)` → `{ seg[2], auto_label, meter_bar, meter_text }` left-to-right with `SP_SM` gaps, meter right-aligned; no overlap.
- `const char *ai_modes_label(int auto_on, int all_setting)` → "off" / "safe only" / "all"; `ai_permit_label(int on)` → "off"/"on".
- `thinking_layout(NsRect avail, int expanded, int body_text_h, int max_body_h, int dpi, ThinkingLayout *out)` → `{ row, chevron, label, summary, body, total_h }`; `ai_thinking_summary(int words, int streaming, char *buf, size_t cap)` → "· 240 words" / "· streaming…".
- `ns_layout`: `approval_card_layout` v2 (rows `{checkbox, text, tag}`, `header`, `deny_all`, `run_selected`, `run_enabled` from a `checked[]` input), `approval_card_hit` with `HIT_RUN_SELECTED`/`HIT_DENY_ALL`; drop two-line logic and `approval_row_height` doubling. Update `test_ns_layout.c` for the new shape (keep the invariants: no overlap, inside parent, hit round-trip, ellipsis, 16-command scrolling).
- `ai_panel_states`: table of `{ id, title, body_fmt, action_label }` and `ai_panel_suggestions()` (three strings); `ai_panel_state_body(id, context_lines, buf, cap)` substitutes N.
- Tests as listed in the spec. Compile-check core objects; no binary.

### Task 2 — card 1 and the thinking disclosure
- Paint from the v2 layout: header line ("N commands · M held", reason), rows, ghost `Deny all` + primary `Run N selected` (disabled state when `run_enabled == 0`). Remove per-row Allow/Deny painting and the `IDC_CMD_APPROVE_BASE/DENY_BASE` posts for rows; keep the checkbox toggle; `HIT_RUN_SELECTED` → approve all checked pending entries (existing approve path); `HIT_DENY_ALL` → existing deny-all. "Blocked" label and lock icon removed.
- Thinking disclosure per the spec's "Thought process" section: `thinking_layout()` in core (task 1 adds it with tests: row/chevron/label/summary/body inside the parent, body zero-height when collapsed, body capped at `max_h`), painted in `chat_listview.c` from that layout with `ns_hover` and the hand cursor; summary text `· N words` / `· streaming…`; open while streaming, collapse when reply text starts unless the user opened it (`show_thinking` per session, lives in `AiSessionState`); no row when a reply has no reasoning. Bump, build, gallery (`approval`, `chat`, `all`).

### Task 3 — header and status line
- `ns_draw_segmented(HDC, const NsRect seg[2], const char *labels[2], int selected, int selected_is_warning, tokens, hover_state[2], HFONT, dpi)` and `ns_draw_meter(HDC, const NsRect *bar, double fraction, tokens)`.
- `ai_chat.c`: delete the three owner-drawn tab buttons and the boxed context label; header = session label (`FONT_TITLE`), model chip, three icon buttons (themed_button with icon, tooltips via `TTM_ADDTOOL`); status line painted in `WM_PAINT` from `ai_status_layout` with `ns_hover`; clicks map to the existing `IDC_CHAT_PERMIT` / `IDC_CHAT_AUTOAPPROVE` handlers (PostMessage WM_COMMAND to self so the harness's `Set-NutshellAiAutoApprove` still works). `relayout()` uses `ai_panel_layout`. Bump, build, gallery, captures.

### Task 4 — states and gates
- `chat_listview.c`: when the list is empty and a state id is set, paint the state (glyph, title, body, chips or button) from `ai_panel_states`; hit-test chips → post a "send prompt" message with the chip text to `ai_chat`; button → post `WM_COMMAND` (`IDM_EDIT_SETTINGS` with page = Provider, or `IDM_FILE_CONNECT`).
- `ai_chat.c` decides the state: no session → `no_session`; empty key → `no_key`; else `empty`. `window.c` `on_ai_clicked`: always open the panel; delete both MessageBoxes. `settings_dlg_show(..., initial_page)`.
- Bump, build, gallery, capture the panel with an empty key and with no session.

### Task 5 — demo, gallery, docs, review
- `ui_demo`: `nokey`, `nosession`; `empty` uses the real empty state; tests.
- `Run-Integration.ps1`: `ui_gallery` covers the new states; `ai_panel_opens_without_key` (keystroke-free). Docs: README AI Chat Assistant section (switches, card, states), help_guide text, CLAUDE.md if any new rule, checkpoint todo. Bump, full native + wintest + gallery; leave the "after" set for Thomas.
