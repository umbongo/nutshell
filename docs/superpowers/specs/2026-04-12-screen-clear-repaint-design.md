# Screen Clear Repaint Bug — Design Spec

**Date**: 2026-04-12
**Bug**: After exiting a TUI app (nano, man), stale screen content remains visible; only newly-written rows repaint. Each subsequent newline progressively clears the old content.

---

## Root Cause

`term_alt_screen_exit` (buffer.c:443) marks dirty rows by iterating `0..lines_count-1`. The renderer displays rows `0..rows-1` (screen rows). When `lines_count < rows` — a fresh terminal, small window, or few commands before launching a TUI app — rows beyond `lines_count` are visible on screen but never marked dirty.

The renderer has a row-level skip optimisation (renderer.c:244) that short-circuits to the next row when `!row->dirty`. Even after `full_redraw_needed` invalidates the display buffer shadow (all cells → `0xFFFFFFFF`), these non-dirty rows are skipped before the cell-level check is reached. The GDI surface retains the stale alt-screen pixels for those rows.

`term_mark_all_dirty` (buffer.c:379) has the same loop bound bug.

A secondary timing issue: the 16ms paint cooldown in the WM_TIMER handler can suppress `invalidate_terminal` on the tick that processes the alt-screen exit escape sequence, delaying the repaint by up to ~26ms (cooldown + timer interval).

### Why It's Intermittent

Triggered only when `lines_count < rows`:
- Fresh terminal with few commands before running the TUI app
- Small terminal area (e.g., AI panel docked, reducing rows/cols)
- Terminal resized larger after initial use

---

## Fix: Three-Part (Approach C)

### Part 1 — Fix Dirty Marking Scope (`buffer.c`)

Change both `term_alt_screen_exit` and `term_mark_all_dirty` to mark all visible screen rows dirty, using `screen_to_phys` (same helper used by scroll functions) to iterate screen rows `0..rows-1` instead of logical rows `0..lines_count-1`.

**`term_alt_screen_exit`** — replace the dirty loop:
```c
// Before:
for (int i = 0; i < term->lines_count; i++) {
    int idx = (term->lines_start + i) % term->lines_capacity;
    if (term->lines[idx])
        term->lines[idx]->dirty = true;
}

// After:
for (int i = 0; i < term->rows; i++) {
    int idx = screen_to_phys(term, i);
    if (idx >= 0 && idx < term->lines_capacity && term->lines[idx])
        term->lines[idx]->dirty = true;
}
```

**`term_mark_all_dirty`** — same change (loop `0..rows-1` with `screen_to_phys`). Note: `screen_to_phys` is `static` in buffer.c so this is an in-file change with no header impact.

### Part 2 — Renderer Respects `full_redraw_needed` (`renderer.c`)

Add a `force_all_rows` flag in `renderer_draw`. When `full_redraw_needed` fires, set this flag and bypass the row-level dirty skip. Every row then reaches the cell-level shadow check; cells that already match the shadow still get skipped — no wasted GDI calls. This is a defense-in-depth fix that protects against any future event that sets `full_redraw_needed` without fully dirtying all rows.

```c
bool force_all_rows = false;
if (term->full_redraw_needed) {
    dispbuf_invalidate(&r->dispbuf);
    term->full_redraw_needed = false;
    force_all_rows = true;
}

// Row skip condition becomes:
if (!force_all_rows &&
    term->scrollback_offset == 0 &&
    !row->dirty && row_idx != cursor_row && row_idx != prev_row &&
    !has_sel) continue;
```

### Part 3 — Cooldown Bypass for Critical Repaints (`window.c`)

In the WM_TIMER session poll loop, bypass the 16ms paint cooldown when the active session's terminal has `full_redraw_needed` set. Also trigger `dispbuf_invalidate` on critical repaints (shadow is stale after a buffer swap regardless of whether new data arrived).

```c
if (poll_rc > 0 || term_has_dirty_rows(s->term)) {
    DWORD now = GetTickCount();
    bool critical = (s == g_active_session &&
                     s->term->full_redraw_needed);
    if (critical || now - g_last_paint_tick >= PAINT_COOLDOWN_MS) {
        if (poll_rc > 0 || critical)
            dispbuf_invalidate(&g_renderer.dispbuf);
        invalidate_terminal(hwnd);
        g_last_paint_tick = now;
    }
}
```

---

## Files Changed

| File | Change |
|------|--------|
| `src/term/buffer.c` | Fix dirty marking loop in `term_alt_screen_exit` and `term_mark_all_dirty` |
| `src/ui/renderer.c` | Add `force_all_rows` flag; bypass row-level skip when `full_redraw_needed` |
| `src/ui/window.c` | Bypass paint cooldown and force `dispbuf_invalidate` when `full_redraw_needed` |

---

## Testing

- **Existing tests**: `make test` — all must pass (no behaviour changes for normal operation)
- **New test in `tests/test_vt_sequences.c`** (or a new `tests/test_dirty.c`): simulate alt-screen enter/exit on a fresh terminal where `lines_count < rows`; assert all screen rows are dirty after exit
- **Manual**: SSH to a host, run nano/man in a small window; exit; verify clean repaint without residual pixels

---

## Non-Goals

- No changes to the public API or terminal struct layout
- No changes to the paint cooldown value (16ms stays)
- No changes to scrollback behaviour
