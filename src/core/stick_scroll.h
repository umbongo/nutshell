#ifndef NUTSHELL_CORE_STICK_SCROLL_H
#define NUTSHELL_CORE_STICK_SCROLL_H

/*
 * Pure arithmetic for "stick to bottom" scroll behaviour, as used by the
 * AI Assist chat list (src/ui/chat_listview.c).
 *
 * The list keeps one bit of state, stick_to_bottom:
 *   - Any user-driven scroll action (wheel, scrollbar, keyboard, drag)
 *     recomputes it from the resulting position: at/past the bottom ->
 *     stuck (1); anywhere else -> released (0).
 *   - Content or viewport changes (new streamed text, a growing Thinking
 *     box, a resize) never change the bit themselves. Instead they consult
 *     it: stuck keeps the view pinned to the new bottom; not stuck keeps
 *     the current position (re-clamped to the new valid range).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Called after a user-driven scroll action. scroll_y is the position the
 * user just scrolled to; max_scroll is the current maximum scroll offset
 * (total_height - viewport_height). Returns 1 (stuck) when scroll_y is at
 * or beyond max_scroll, or when max_scroll <= 0 (content fits the
 * viewport, so there is nowhere else to be but "at the bottom"). Returns
 * 0 (released) otherwise. */
int stick_scroll_after_user(int scroll_y, int max_scroll);

/* Called after a content or viewport change (recalc_layout, WM_SIZE).
 * stick is the current stick_to_bottom bit; scroll_y is the position
 * before the change; max_scroll is the new maximum scroll offset.
 * Returns the scroll position to use: max(max_scroll, 0) when stuck,
 * otherwise scroll_y clamped to [0, max(max_scroll, 0)]. */
int stick_scroll_on_layout(int stick, int scroll_y, int max_scroll);

/* Called after a layout change that moved or resized the first visible
 * item (a command card folding to one row, say). stick is the
 * stick_to_bottom bit; scroll_y the position before; old_top and new_top the
 * item's top in content coordinates before and after; new_h its height now.
 * Pinned: scroll_y unchanged (stick_scroll_on_layout() follows the bottom).
 * Not pinned: the view keeps the same offset into that item, clamped to its
 * last pixel (new_h - 1), so what the reader looks at stays put when content
 * above it shrinks. Never negative. */
int stick_scroll_anchor(int stick, int scroll_y, int old_top, int new_top,
                        int new_h);

/* Which item the view should stay on when the layout above it changes (a
 * command card folding): walk the items of the LAST layout. heights[i] is
 * item i's height then (0 = not shown, no gap), redirect[i] is the index of
 * the item to anchor on instead when item i has just been hidden (its fold
 * group's first row), or -1. A group head whose own fold state changed in
 * this refresh (a tall card that folded to one short row) sets
 * redirect[i] = i: its old offset would be clamped to the last pixel of the
 * fold row, so it too anchors at offset 0. gap is the space between items and above the
 * first; scroll_y the position. Returns the first item whose bottom is below
 * scroll_y and stores in *offset how far scroll_y sits into it (negative in
 * the gap above the first item); an item that was just hidden gives its
 * redirect target with offset 0, so the fold row sits at the top of the view.
 * Returns -1 (offset untouched) when nothing is at or below scroll_y, or on
 * bad arguments. */
int stick_scroll_pick_anchor(const int *heights, const int *redirect, int n,
                             int gap, int scroll_y, int *offset);

/* The position that keeps a row where it is on screen across a layout
 * change: screen_y is the row's top relative to the top of the view before
 * (its content top minus scroll_y), new_top its content top now, max_scroll
 * the new maximum offset. Clamped to [0, max(max_scroll, 0)]. */
int stick_scroll_hold_row(int screen_y, int new_top, int max_scroll);

#ifdef __cplusplus
}
#endif

#endif /* NUTSHELL_CORE_STICK_SCROLL_H */
