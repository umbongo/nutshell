# Settings Window Redesign — PuTTY-style Category Navigation

**Date**: 2026-08-29
**Branch**: `fixes`
**Status**: Approved for implementation

## Problem

The Settings window is a single 400 x 1022 px fixed-size window. Every
setting is stacked in one column separated by etched rules. Three things
are wrong with it:

1. **It cannot be resized.** `WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU`
   has no `WS_THICKFRAME`.
2. **It does not fit on screen.** 1022 px of content plus title bar and
   taskbar exceeds the usable height of a 1080p display, so the Save and
   Cancel buttons and the last settings are unreachable.
3. **It does not scale.** Every control is placed by hand at an absolute
   `y` that accumulates through 400 lines of `WM_CREATE`. Adding a
   setting means re-deriving the window height by hand.

## Goal

A resizable, PuTTY-style settings window: a category list on the left, one
page of controls at a time on the right, Save/Cancel pinned to the bottom.
Every setting reachable without scrolling at the default size, and adding a
future setting means adding one table row, not recomputing a layout.

## Category Structure

A two-level list. Group headers are non-selectable; leaf entries are pages.

```
General                       (header)
    Appearance                colour scheme, terminal font, font size, AI assist font
    Terminal                  scrollback lines, paste delay
    Logging                   log directory, log name format, debug terminal log
Connection                    (header)
    SSH                       user idle timeout
    Startup                   auto-connect at startup, session
AI Assistant                  (header)
    Provider                  provider, API key, model (+refresh), base URL
    Behaviour                 system-wide AI instructions, render AI markdown
    Web Access                search engine, search URL, max results, permit web fetch
About                         (standalone page) version, copyright
```

Rationale for the split: no page holds more than six rows, so every page
fits without scrolling at the default window size, and each group has
obvious room to grow. Fonts are gathered under Appearance rather than
scattered, because a user hunting for "how do I change a font" looks in one
place.

## Architecture

### Portable layout module — `src/core/settings_layout.{c,h}`

Pure C, no Win32, compiled into both the Windows build and the native test
build. It owns everything about the layout that can be reasoned about
without a window handle:

- **Page table.** `SettingsPageInfo { int id; int parent; const char *label; }`
  plus `settings_page_count()` / `settings_page_at(i)`. Group headers are
  entries whose `id` is `SETTINGS_PAGE_NONE`-parented and marked as headers.
- **Nav model.** `settings_nav_count()` and `settings_nav_at(i)` returning
  `{ page_id, depth, is_header }` — the flattened list the owner-drawn
  listbox renders.
- **Metrics.** `settings_metrics_init(SettingsMetrics *m, int dpi)` fills
  DPI-scaled constants: nav width, padding, label column width, row height,
  control height, button-bar height, minimum window size.
- **Regions.** `settings_layout_regions(client_w, client_h, m, &nav, &content, &buttons)`
  splitting the client area into the three panes as plain
  `SettingsRect { x, y, w, h }` values.
- **Rows.** `settings_row_rects(row_index, content_w, ctrl_w, m, &label, &ctrl)`
  placing one label/control pair inside the content pane.
- **Scrolling.** `settings_scroll_max(view_h, content_h)` and
  `settings_scroll_clamp(pos, max)`.

Putting this in `src/core/` rather than `src/ui/` is what makes the layout
testable — `NON_TEST_SRCS` excludes all of `src/ui/`.

### Win32 shell — `src/ui/settings.c` (rewritten)

- **Main window** gains `WS_THICKFRAME | WS_MAXIMIZEBOX`, defaults to
  760 x 560 DPI-scaled, and enforces a floor through `WM_GETMINMAXINFO`
  using the metrics from the layout module.
- **Nav listbox**: `LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY`,
  owner-drawn against the active `ThemeColors` so it matches the rest of the
  dialog. Header rows draw dim and are skipped when the selection moves.
  An owner-drawn listbox is used rather than a `SysTreeView32` because the
  common-controls tree renders its lines, expand buttons, and selection
  highlight in the system light theme, which fights every one of the four
  Onyx schemes.
- **Page host**: a child window of class `Nutshell_SettingsPage` filling the
  content region. Every page's controls are children of it, created once at
  startup and shown or hidden by page id. The host clips them, which is what
  makes scrolling possible, and it handles `WM_CTLCOLOR*`, `WM_DRAWITEM`,
  `WM_VSCROLL`, and `WM_MOUSEWHEEL` for its own children, forwarding
  `WM_COMMAND` to the main window.
- **Page scrolling**: when a page is taller than the host, the existing
  `csb_*` custom scrollbar appears on the right of the content pane and the
  page relayouts at a negative `y` offset. Reusing the relayout path for
  scrolling means there is only one code path that positions controls.
- **Save/Cancel** and the version/copyright footer sit in the bottom bar,
  repositioned on `WM_SIZE`.

### What does not change

The `IDOK` save handler keeps its current structure — it reads controls by
ID, and IDs stay unique across the whole window. The only edit is that
lookups move from the main window to the page host. Provider-dependent
show/hide of the custom URL and search URL fields, the model-refresh worker
thread, the tooltip table, and `settings_validate()` clamping all carry over
unchanged. This keeps a risky rewrite confined to layout.

## Testing

New `tests/test_settings_layout.c` covering:

- Region split: panes tile the client area exactly, with no gaps or overlap,
  at 96 and 144 DPI.
- Region split at the minimum window size stays non-degenerate (all widths
  and heights positive).
- Row placement: row `n` sits `n * row_height` below row 0; label and control
  do not overlap; the control stays inside the content pane.
- Nav model: every non-header nav entry maps to a real page; headers have
  depth 0 and children depth 1; the flattened order matches the page table.
- Scroll maths: `scroll_max` is 0 when content fits, positive when it does
  not, and `scroll_clamp` pins to `[0, max]` including negative input.
- Metrics scale monotonically with DPI and never round to zero.

All declared and called from `tests/runner.c`. Written before the
implementation, per the project's TDD rule.

## Version

Bump `1.0.70` -> `1.0.71` in `src/ui/resource.h` (both `APP_VERSION` and
`APP_VERSION_BINARY`) and in `README.md`.
