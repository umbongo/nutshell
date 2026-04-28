# AI Chat Markdown — Table Detection + User Toggle (Design Spec)

**Status:** Draft, 2026-04-28

## Background

The AI Chat panel renders responses through the markdown layouter at `src/ui/md_render.c`. After the layout fix in commit `429d5f1`+ (markdown layout fix plan, 2026-04-28-ai-chat-markdown-layout-fix.md), most prose renders correctly. Two issues remain:

1. **Indented tables don't render as tables.** The AI sometimes emits markdown tables prefixed with leading whitespace (or with a leading character that isn't `|`). `md_classify_line` only flags `MD_LINE_TABLE` when `line[0] == '|'`, so such lines fall through to `MD_LINE_PARAGRAPH` and render as wrapped prose with each backtick-wrapped cell turning into a grey inline-code span — no column alignment, no grid feel.
2. **No user opt-out.** If the user prefers raw text or hits a markdown rendering bug, there's no way to disable rendering and see plain text.

## Goals

- AI markdown tables render as monospace blocks even with leading whitespace.
- A Settings checkbox lets the user toggle markdown rendering on/off; default ON.
- When markdown is off, AI text renders as plain word-wrapped UTF-8 with the regular GUI font, while `[EXEC]` markers still get the existing purple monospace treatment.

## Non-Goals

- Real grid rendering (parsing `|`-separated cells, measuring per-column widths, drawing rule lines). Out of scope — would need its own spec.
- Standard CommonMark loose-block-marker compliance (3-space indent for `#`, `-`, `>`, etc.). Only the table check is loosened, since that's the visible bug.
- Per-message toggle. Single global setting.

## Design

### Loosen `md_classify_line` table check

In `src/ui/markdown.h:118-122`, the existing check is:

```c
/* Table: starts with | */
if (line[0] == '|') {
    info.type = MD_LINE_TABLE;
    return info;
}
```

Replace with: skip leading spaces and tabs, then check the first non-whitespace char. The `content_offset` field is set so the renderer can choose to honour or ignore the leading indent. The current `MD_LINE_TABLE` render branch in `md_render.c` passes the whole line (with leading whitespace) to `DrawTextW` — that's fine; the leading spaces just indent the table block, which looks acceptable.

`md_is_table_separator` (also in `markdown.h:296-304`) needs the same leading-whitespace skip so separator lines aren't misclassified.

### Settings checkbox

Follow the established `ai_web_fetch_enabled` pattern end-to-end:
- Field on `Settings` struct.
- Default in `config_default_settings` — default to **1** (markdown ON).
- JSON read in `loader.c` settings deserialiser.
- JSON write in `loader.c` settings serialiser.
- Tooltip table entry.
- IDC_* control ID.
- BS_AUTOCHECKBOX in the AI Assist section of the Settings dialog (after "Permit Web Fetch").
- `IsDlgButtonChecked` read on OK.

### ChatListView wiring

Two ways to give `chat_listview` the toggle state:

(a) Pass a `const Settings *` pointer in.
(b) Add a single `int render_markdown` field on `ChatListView` with a setter, populated from `g_config.settings.markdown_render_enabled` at init and on Settings-OK.

Pick (b) — looser coupling, no new include of `config.h`, matches the existing `theme` pointer pattern.

When the flag is **on**, the three `md_render_text` calls in `draw_ai_text_with_exec` (lines 923/938/960) and the `md_measure_text` call in `measure_item` (line 731) work as today.

When the flag is **off**:
- Render: replace `md_render_text` with `draw_text_utf8(hdc, text, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX)`. This already exists at `chat_listview.c:188-196`. `[EXEC]` segments still get their existing purple/monospace treatment because the `draw_ai_text_with_exec` outer loop is unchanged — only the *non-EXEC* segments switch to plain rendering.
- Measure: replace `md_measure_text` with a single `DrawTextW(DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX)` against `lv->hFont`, mirroring the user-bubble measurement at `chat_listview.c:706-714`.

The flag must update **without restart** — when the user toggles it via Settings → OK, the existing settings-applied path (which already invalidates the chat list view for theme changes) calls `chat_listview_set_render_markdown(...)` then `chat_listview_invalidate(...)`.

## Acceptance Criteria

1. Markdown table lines starting with leading spaces or tabs render in monospace as a table block.
2. Existing tests in `tests/test_markdown_render.c` continue to pass; new tests cover leading-whitespace cases for both `md_classify_line` and `md_is_table_separator`.
3. New Settings checkbox "Render AI markdown" appears in the AI Assist section with tooltip "Render AI replies as formatted markdown. Turn off to see raw text."
4. Default value is ON. Persists across restarts.
5. Toggling OFF + OK switches all already-rendered AI bubbles to plain text on next paint without restart.
6. Plain-text mode still purple-highlights `[EXEC]` blocks.
7. Cross-compile clean under `-Werror -Wshadow -Wconversion -Wpedantic`. Native test suite passes.

## Out of Scope (follow-up items)

- True grid table rendering (cell parsing, column-width measurement, alignment honouring `:--:` separators).
- Loosening other block-marker classifiers (`#`, `-`, `>`) for leading whitespace.
- DPI scaling of `MD_*` spacing constants (still pending from the prior plan).
