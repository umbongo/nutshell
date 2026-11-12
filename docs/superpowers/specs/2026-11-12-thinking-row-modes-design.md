# Thinking row: three visual modes: design

Date: 2026-11-12. Status: draft for the maintainer's review. Source of truth:
`main` at `f842775` (v1.2.82). Branch `feat/thinking-row-modes`.

The request (Open list of `2026-09-06-ui-redesign-notes.md`, maintainer,
2026-11-12): the AI chat panel's Thinking row is either collapsed (one line)
or open (the whole text, capped at 50 lines with an inner scroll). Add a
compact mode that shows four lines of the streaming text, and make a click on
the row cycle collapsed, compact, open. Defaults: compact while the text
streams, fully collapsed once the reply is idle. The label ("Thinking")
changes colour with the mode from a pastel palette: compact green, collapsed
brown, open grey, as new theme tokens, one set per theme. A mode the user
clicks to sticks, at idle and across restarts: one global choice for every
Thinking row, saved in the config, replacing the session-wide preference that
`IDC_CHAT_THINKING_OPENED/CLOSED` fed. Settings gets a control that resets it
to the defaults.

Decided by the main session, 2026-11-12: compact always shows the **last**
four wrapped lines of the thinking text. It follows the stream as it grows; on
a finished row it shows the final four lines. Compact has no inner scroll;
open mode is for reading the whole text.

## What is there today

- Per item: `ChatMsgItem.u.ai.thinking_collapsed` (1 = closed) and
  `thinking_user_set` (`src/core/chat_msg.h`), plus the open box's
  `thinking_scroll_y`/`thinking_autoscroll` and a wrap cache.
- Per panel: `AiChatData.show_thinking`, mirrored per session in
  `AiSessionState.show_thinking`, set by `IDC_CHAT_THINKING_OPENED/CLOSED`
  (posted by the click in `chat_listview.c`), reset on New Chat, used by the
  WM_AI_STREAM update to open a new streaming row, and by Save to decide
  whether thinking text goes into the saved file.
- Geometry: `thinking_layout()` (`src/core/ai_panel_layout.c`) takes an
  `expanded` flag and a `max_body_h`; `build_thinking_layout()` in
  `chat_listview.c` is the one source for measure, paint, hover, click and
  wheel. The thinking body is painted in `MD_MODE_PLAIN`, one font, no extra
  line spacing, so the wrapped height is a whole number of `line_h`.
- Streaming per item: `u.ai.thinking_complete` is 0 until the reply finishes
  (set at stream finish and on rebuild).
- `src/core/chat_thinking.[ch]` is dead code (tests only); untouched here.

## Design

### Model (`src/core/thinking_mode.[ch]`, pure, native-tested)

```c
typedef enum { THINK_MODE_COLLAPSED = 0, THINK_MODE_COMPACT = 1,
               THINK_MODE_OPEN = 2 } ThinkMode;
typedef enum { THINK_PREF_AUTO = 0, THINK_PREF_COLLAPSED = 1,
               THINK_PREF_COMPACT = 2, THINK_PREF_OPEN = 3 } ThinkPref;
#define THINK_COMPACT_LINES 4
```

- `think_mode_next(m)`: collapsed -> compact -> open -> collapsed; an out of
  range value behaves as collapsed.
- `think_mode_effective(pref, streaming)`: `AUTO` gives compact while
  streaming and collapsed when idle; any other pref gives its own mode in
  both states.
- `think_pref_from_mode(m)`: the sticky pref a click stores.
- `think_pref_name(p)` / `think_pref_parse(s)`: config tokens `"auto"`,
  `"collapsed"`, `"compact"`, `"open"`; NULL, unknown, wrong case -> `AUTO`.
- `think_mode_body_cap(m, line_h, open_cap)`: 0 collapsed,
  `THINK_COMPACT_LINES * line_h` compact, `open_cap` open.
- `think_compact_offset(full_h, vis_h)`: `max(0, full_h - vis_h)`, the
  scroll that puts the last lines in view.
- `think_mode_label_colour(chat, m)`: picks the token for the mode.

### Config

`Settings.ai_thinking_mode` (`int`, a `ThinkPref`), JSON key
`"ai_thinking_mode"` with the tokens above, default `"auto"`.
`settings_validate()` clamps out-of-range values to `AUTO`. A config without
the key, or with an unknown value, loads as `AUTO`.

### Theme tokens

Three fields appended to `ThemeChatColors` (`src/core/ui_theme.h`):
`thinking_compact` (green), `thinking_collapsed` (brown), `thinking_open`
(grey), one value per theme in `ui_theme.c`. Each is a text colour on the chat
panel's background and joins `k_pairs` in `tests/test_theme_contrast.c` at
`FLOOR_TEXT` (4.5:1), so no exemption is added.

### Listview

- Each item's mode is no longer stored as a click result: the list view holds
  the global `ThinkPref` (`chat_listview_set_thinking_pref()`), and
  `build_thinking_layout()` computes the item's mode with
  `think_mode_effective(pref, !thinking_complete)` and writes it to
  `u.ai.thinking_mode` (which replaces `thinking_collapsed`; `chat_copy.c`
  reads it). `thinking_user_set` is removed.
- `thinking_layout()` is called with `expanded = mode != COLLAPSED` and
  `max_body_h = think_mode_body_cap(...)`. Collapsed geometry is unchanged,
  so the fold rows that reuse it still line up.
- Compact: the clip offset is `think_compact_offset(full_h, vis_h)` every
  layout, no scrollbar thumb, the wheel over the box chains to the list (no
  inner scroll), and the text wraps at the full body width (no scrollbar
  gutter).
- Open: unchanged behaviour (50-line cap, inner scroll, autoscroll).
- Label colour: `think_mode_label_colour(&theme->chat, mode)`.
- Click on the row: `next = think_mode_next(current effective mode of that
  row)`; the list view's pref becomes `think_pref_from_mode(next)`, every
  item re-lays out, and it posts `WM_COMMAND IDC_CHAT_THINKING_MODE` with the
  pref in `lParam` to the panel.

### Panel and Settings

- `IDC_CHAT_THINKING_MODE` replaces `IDC_CHAT_THINKING_OPENED/CLOSED`. The
  panel stores the pref in the live config's `settings.ai_thinking_mode` and
  saves the config. `show_thinking` (panel and per session) is removed with
  the stream-time auto-open it drove.
- AI Assistant > Behaviour gets a combo, "Thinking display:", with
  Automatic (compact while streaming, collapsed when idle), Always collapsed,
  Always compact, Always open. Choosing Automatic is the reset to the
  defaults. It has a `settings_help` entry like every other control. On OK
  the chat panel receives the new pref.
- Startup: the panel reads the pref from the loaded config.

### Demo

`--ui-demo=thinking-compact` and `--ui-demo=thinking-open` sit beside the
existing `thinking` (collapsed) state; each sets the pref in memory only,
never saved. Not part of `all`, like `thinking`.

## Implementer's calls

Made by the orchestrator (claude-opus-5-5) while the maintainer was away;
each is open to reversal.

1. **The reset control is a combo, not a button.** Its Automatic entry is the
   reset; the other three let the choice be made without clicking a row, and
   the combo shows what is in force. It follows the `ai_runcmd_mode` combo
   pattern, so no new owner-draw button.
2. **The mode is global, so a click restyles every Thinking row,** finished
   or streaming, not only the one clicked. That is what "one global choice
   for every Thinking row" says.
3. **The click cycles from the row's current effective mode.** On an idle
   row under Automatic (collapsed) the first click gives compact and pins
   compact; there is no click that returns to Automatic, which is the
   Settings combo's job.
4. **"Streaming" means the reply has not finished** (`thinking_complete` is
   0), not "thinking text still arriving": during the answer phase the row
   stays compact, then collapses when the reply ends. A stopped or failed
   reply counts as finished.
5. **A sticky pref applies while streaming too,** e.g. "Always collapsed"
   keeps a streaming row collapsed.
6. **Chevron:** pointing right when collapsed, down for compact and open.
7. **Copy** includes the thinking text when the row is compact or open (the
   whole text, not just the four visible lines), as open does today.
8. **Save** (the chat-to-file export) includes thinking text when the idle
   mode under the current pref is not collapsed.
9. **Light themes** take deeper, muted versions of the pastels so the label
   meets 4.5:1; the dark themes take the true pastels.
10. **Compact rows shorter than four lines** take their own height; there is
    no padding to four lines.
11. **A new module** `thinking_mode.[ch]` rather than reviving the dead
    `chat_thinking.[ch]`, which stays as it is.
12. **The config token for "no choice" is `"auto"`**, matching
    `ai_runcmd_mode`'s Automatic.
13. **The click's save goes through `window.c`**: the panel forwards
    `IDC_CHAT_THINKING_MODE` to the main window, which writes `g_config`
    with `config_save(g_config, active_config_path())` and does nothing
    under `--ui-demo`, so a demo never writes the real config.
14. **`--ui-demo=chat` and `all` force the demo pref to open** (they showed
    the Thinking body open before, and the chat selection case crosses it);
    the other demo states keep the saved pref. In memory only.
15. **A failed reply ends its row's streaming state**: the error path now
    sets `thinking_complete`, so the row stops being compact under
    Automatic.
16. **A mode change drops the row's wrap cache**, since compact wraps
    without the open box's scrollbar gutter.
17. **A click does not hold the clicked row's screen position**; the list
    re-lays out and, if pinned to the bottom, follows the bottom.
18. **The Settings combo sits after "Render AI markdown"** on AI Assistant >
    Behaviour, its items in `ThinkPref` order.

## Testing

- Native: the model functions (cycle, effective mode in all 8 pref x
  streaming cases, parse/name round trip, unknown tokens, body cap, offset,
  label colour), config default/load table/round trip/validate clamp, the
  contrast pairs on all four themes, the settings_help table.
- Token gates (`tests/test_ui_tokens.c`): no `RGB(` literal, scale macro or
  `CreateFont` added in `src/ui`.
- Windows hand checks (not runnable here; they go to the Open list): the three
  modes on the four themes via `--ui-demo=thinking`, `thinking-compact`,
  `thinking-open`; a live streaming reply under Automatic (compact, then
  collapsed); the click cycle; restart keeps the choice; Settings Automatic
  resets; `settings_help_tool_count` now expects one more tool.
