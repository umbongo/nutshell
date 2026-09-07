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

#ifdef __cplusplus
}
#endif

#endif /* NUTSHELL_CORE_STICK_SCROLL_H */
