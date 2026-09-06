#ifndef NUTSHELL_NS_HOVER_H
#define NUTSHELL_NS_HOVER_H

/*
 * ns_hover -- a tiny, portable hover/press tracker shared by every painted
 * (non-child-window) interactive element: the approval card in
 * chat_listview.c, the tab strip in tabs.c. See
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 3 ("Hover tracker").
 *
 * The owner runs its own hit-test on every WM_MOUSEMOVE, feeds the
 * resulting element id (an arbitrary caller-defined int; -1 means "over
 * nothing hittable") into ns_hover_move(), and invalidates only the two
 * rects (old_id, new_id) that changed state -- never the whole window.
 * TrackMouseEvent() + WM_MOUSELEAVE drive ns_hover_leave() so the hot
 * element clears when the cursor leaves the window entirely.
 *
 * Pure C, no windows.h -- fully testable on Linux.
 */

typedef struct {
    int hot_id;      /* element id under the cursor, or -1 */
    int pressed_id;  /* element id with the mouse button down, or -1 */
} NsHover;

typedef struct {
    int changed;   /* 1 if this call changed the tracked id, else 0 */
    int old_id;    /* value before the call */
    int new_id;    /* value after the call */
} NsHoverChange;

/* Reset both ids to -1 (nothing hot, nothing pressed). */
void ns_hover_init(NsHover *h);

/* Report the hit-test result for the current mouse position. hit_id = -1
 * means the cursor is over nothing hittable. Updates h->hot_id and
 * reports whether it changed. Safe to call every WM_MOUSEMOVE, including
 * when hit_id repeats the current hot id (changed = 0). */
NsHoverChange ns_hover_move(NsHover *h, int hit_id);

/* Equivalent to ns_hover_move(h, -1) -- call from WM_MOUSELEAVE. */
NsHoverChange ns_hover_leave(NsHover *h);

/* Record that the mouse button went down over hit_id (may differ from
 * hot_id, e.g. after a drag; callers normally pass the current hot id). */
NsHoverChange ns_hover_press(NsHover *h, int hit_id);

/* Clear the pressed id (mouse button released, or capture lost). */
NsHoverChange ns_hover_release(NsHover *h);

/* NsBtnState-shaped state for `id`: 0 = rest, 1 = hover, 2 = pressed.
 * A negative `id` (e.g. HIT_NONE mapped to -1) always reads as rest. */
int ns_hover_state_for(const NsHover *h, int id);

#endif /* NUTSHELL_NS_HOVER_H */
