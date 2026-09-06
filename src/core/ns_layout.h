#ifndef NUTSHELL_NS_LAYOUT_H
#define NUTSHELL_NS_LAYOUT_H

/*
 * ns_layout — pure geometry from (rect, dpi, content) to sub-rects, shared
 * by paint and hit-test. Portable, no windows.h. See
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 3 ("Geometry in core").
 *
 * Sizes come from ns_type.h via ns_scale(): row height SZ_CTRL_H, tag
 * SZ_TAG_H, buttons SZ_BTN_MIN_W x SZ_CTRL_H, gaps SP_SM/SP_MD, card inset
 * SP_MD. All rects are in the same coordinate space as the input NsRect
 * (i.e. absolute client coordinates when the caller passes an
 * already-positioned rect).
 */

#include "chat_approval.h"   /* APPROVAL_MAX_CMDS */

typedef struct { int x, y, w, h; } NsRect;

/* ns_button_layout: icon (SZ_ICON, only when has_icon) on the left, label
 * filling the rest. has_icon = 0 gives a zero-size icon and a label
 * spanning the whole (inset) rect. */
typedef struct { NsRect icon, label; } NsButtonLayout;

/* ns_card_layout: a generic card split into an inset interior, a header
 * strip (height SZ_CTRL_H) and the body below it. */
typedef struct { NsRect inset, header, body; } NsCardLayout;

/* Rows beyond this many are laid out inside the card but scroll off (see
 * approval_card_layout). Matches the visible-row cap in the spec. */
#define APPROVAL_VISIBLE_MAX 8

/* One approval-card row, left to right: safety tag, command text
 * (single line, ellipsised when it doesn't fit), selection checkbox,
 * Allow button, Deny button. `ellipsis` is set when the caller-supplied
 * full-text pixel width exceeds `text.w`. */
typedef struct {
    NsRect tag, text, checkbox, allow, deny;
    int ellipsis;
} ApprovalRowLayout;

/* The whole approval card: up to APPROVAL_MAX_CMDS rows (rows beyond
 * n_rows, or beyond the visible window, are zeroed), plus the two card-wide
 * actions (Allow All, Cancel) and the rows' viewport rect. `first_visible`
 * is always 0 for a single ns_layout call — the caller re-slices its own
 * command array (widths + n) to scroll further rows into view. */
typedef struct {
    ApprovalRowLayout rows[APPROVAL_MAX_CMDS];
    int n_rows, first_visible, scrollable;
    NsRect allow_all, cancel, viewport;
} ApprovalCardLayout;

enum {
    HIT_NONE,
    HIT_TAG,
    HIT_TEXT,
    HIT_CHECKBOX,
    HIT_ALLOW,
    HIT_DENY,
    HIT_ALLOW_ALL,
    HIT_CANCEL
};

void ns_button_layout(NsRect r, int has_icon, int dpi, NsButtonLayout *out);
void ns_card_layout(NsRect r, int dpi, NsCardLayout *out);

/* Lay out `n` command rows (n clamped to APPROVAL_MAX_CMDS) inside `r`,
 * plus the allow_all/cancel actions below them. `cmd_text_w[i]` is the
 * full (unellipsised) pixel width of row i's command text, measured by the
 * caller in its own font; `text_h` is that font's line height, used to
 * grow the row height beyond SZ_CTRL_H if needed. n <= 0 zeroes `out`
 * (rows/allow_all/cancel all zero-size) and does not crash. */
void approval_card_layout(NsRect r, int n, const int *cmd_text_w, int text_h,
                          int dpi, ApprovalCardLayout *out);

/* Hit-test a point against a laid-out approval card. Returns a HIT_*
 * constant; `*row_out` (when non-NULL) receives the row index for
 * HIT_TAG/HIT_TEXT/HIT_CHECKBOX/HIT_ALLOW/HIT_DENY, or -1 otherwise
 * (including HIT_NONE, HIT_ALLOW_ALL, HIT_CANCEL). Never crashes on a
 * NULL layout. */
int approval_card_hit(const ApprovalCardLayout *l, int x, int y, int *row_out);

#endif /* NUTSHELL_NS_LAYOUT_H */
