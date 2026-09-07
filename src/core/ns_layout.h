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

/* One approval-card row, left to right: selection checkbox, command text
 * (single line, ellipsised when it doesn't fit), risk tag right-aligned.
 * `ellipsis` is set when the caller-supplied full-text pixel width exceeds
 * `text.w`. `held` mirrors the caller's held[] input for this row (a held
 * row paints its checkbox disabled and its text dimmed; it never counts
 * toward `run_enabled`). Rows are always single-line — v1's two-line
 * wrapping (and the per-row Allow/Deny buttons that made it necessary) is
 * gone now that the card has a single Run N selected action. */
typedef struct {
    NsRect checkbox, text, tag;
    int ellipsis;
    int held;
} ApprovalRowLayout;

/* The whole approval card v2: a header row ("N commands · M held"), up to
 * APPROVAL_MAX_CMDS rows (rows beyond n_rows, or beyond the visible
 * window, are zeroed), and the two card-wide actions (Deny all, Run N
 * selected) below them. `first_visible` is always 0 for a single
 * ns_layout call — the caller re-slices its own command array (widths +
 * n) to scroll further rows into view. `run_enabled` is
 * any(checked[i] && !held[i]) over all n rows (not just the visible
 * ones), so a row scrolled out of view can still enable Run selected. */
typedef struct {
    ApprovalRowLayout rows[APPROVAL_MAX_CMDS];
    int n_rows, first_visible, scrollable, run_enabled;
    NsRect header, deny_all, run_selected, viewport;
} ApprovalCardLayout;

enum {
    HIT_NONE,
    HIT_HEADER,
    HIT_TEXT,
    HIT_CHECKBOX,
    HIT_TAG,
    HIT_DENY_ALL,
    HIT_RUN_SELECTED
};

void ns_button_layout(NsRect r, int has_icon, int dpi, NsButtonLayout *out);
void ns_card_layout(NsRect r, int dpi, NsCardLayout *out);

/* Lay out `n` command rows (n clamped to APPROVAL_MAX_CMDS) inside `r`,
 * a header row above them, and the deny_all/run_selected actions below
 * them. `cmd_text_w[i]` is the full (unellipsised) pixel width of row i's
 * command text, measured by the caller in its own font; `checked[i]` and
 * `held[i]` are the caller's per-row selection/held state (either may be
 * NULL, treated as all-zero); `text_h` is the command font's line height,
 * used to grow the row height beyond SZ_CTRL_H if needed. n <= 0 zeroes
 * `out` (rows/header/deny_all/run_selected all zero-size) and does not
 * crash. */
void approval_card_layout(NsRect r, int n, const int *cmd_text_w,
                          const int *checked, const int *held,
                          int text_h, int dpi, ApprovalCardLayout *out);

/* Hit-test a point against a laid-out approval card. Returns a HIT_*
 * constant; `*row_out` (when non-NULL) receives the row index for
 * HIT_TEXT/HIT_CHECKBOX/HIT_TAG, or -1 otherwise (including HIT_NONE,
 * HIT_HEADER, HIT_DENY_ALL, HIT_RUN_SELECTED). Never crashes on a NULL
 * layout. */
int approval_card_hit(const ApprovalCardLayout *l, int x, int y, int *row_out);

/* Height of one approval row with command-text line height `text_h` at
 * `dpi`: max(SZ_CTRL_H, text_h + 2*SP_XS). No longer takes a card width —
 * v2 rows are always single-line regardless of card width. Callers that
 * need the row height before they have a layout (scroll maths, container
 * sizing) must use this so they agree with approval_card_layout. */
int approval_row_height(int text_h, int dpi);

/* Lay out one settled (already-decided) command as a single compact inline
 * row: command text on the left starting at r.x, an outcome chip (ran/held/
 * denied/skipped) right-aligned at width `chip_w`, height SZ_TAG_H scaled,
 * both vertically centred in a row of height approval_row_height(text_h,
 * dpi). No checkbox (settled rows aren't interactive) -- `out->checkbox` is
 * zeroed and `out->held` is always 0 (unlike ApprovalRowLayout's live-card
 * use, "held" isn't a settled-row concept; the outcome chip carries that
 * meaning instead). `cmd_text_w` is the full (unellipsised) pixel width of
 * the command text, measured by the caller in its own font; `out->ellipsis`
 * is set when it exceeds the text rect's width. `chip_w` <= 0 gives the
 * text the full row width (no chip drawn). Never crashes on NULL `out`. */
void settled_row_layout(NsRect r, int cmd_text_w, int chip_w, int text_h,
                        int dpi, ApprovalRowLayout *out);

#endif /* NUTSHELL_NS_LAYOUT_H */
