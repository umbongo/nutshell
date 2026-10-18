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
 * (single line, ellipsised when it doesn't fit), a copy-to-clipboard icon
 * (SZ_ICON square, SP_XS gap either side, vertically centred like the
 * checkbox), risk tag right-aligned. On a row too narrow to give the text at
 * least SZ_ICON of width next to the copy icon, `copy` is zeroed (w = h = 0,
 * and never hit) and the text takes the space back; the tag is the last to
 * go.
 * `ellipsis` is set when the caller-supplied full-text pixel width exceeds
 * `text.w`. `held` mirrors the caller's held[] input for this row (a held
 * row paints its checkbox disabled and its text dimmed; it never counts
 * toward `run_enabled`). Rows are always single-line — v1's two-line
 * wrapping (and the per-row Allow/Deny buttons that made it necessary) is
 * gone now that the card has a single Run N selected action. */
typedef struct {
    NsRect checkbox, text, copy, tag;
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
    HIT_RUN_SELECTED,
    HIT_POLICY_ALLOWED,
    HIT_POLICY_UNATTENDED,
    HIT_COPY,           /* appended: the numeric values above are unchanged */
    HIT_POLICY_AUTO     /* appended 2026-10-18: the status line's Auto-approve
                         * button. HIT_POLICY_UNATTENDED keeps its value but is
                         * never produced since the rail went. */
};

/* Stops on the status line's AI policy control: Read / Unknown / Write /
 * Critical, one cell per CmdSafetyLevel. See cmd_policy.h and
 * docs/superpowers/specs/2026-09-11-status-policy-control-design.md
 * section 4 ("The control"). */
#define NS_POLICY_STOPS 4

/* One pill, NS_POLICY_STOPS touching cells, one band each: clicking a cell
 * sets the level (docs/superpowers/specs/2026-10-18-ai-auto-approve-button-
 * design.md section 3). `cell[k]` is the painted cell (full height of the
 * input rect). `hit[k]` is its click target: the cell's column, max(hit_h,
 * r.h) tall and centred on r -- or zero-size when the cell does not end at
 * or before `clip_right`, so a cell the Auto-approve button covers in part
 * neither takes a click nor shows a tooltip. `clip_right` is the exclusive
 * x past which nothing of the pill paints (ns_draw_policy() clips to it).
 * `total_w` is the pill's unclipped width, matching ns_policy_width(). */
typedef struct {
    NsRect cell[NS_POLICY_STOPS];
    NsRect hit[NS_POLICY_STOPS];
    int clip_right;
    int total_w;
} NsPolicyLayout;

/* Total pixel width of the policy pill at `dpi`: the sum of each cell's
 * width, max(1, stop_text_w[k] + 2*ns_scale(SP_SM, dpi)). A NULL
 * `stop_text_w`, or a negative entry, counts that cell's text as 0 wide.
 * Agrees with `out->total_w` from ns_policy_layout for the same inputs. */
int ns_policy_width(const int stop_text_w[NS_POLICY_STOPS], int dpi);

/* Lay out the policy pill inside `r` (cells span all of r.h). A cell k is
 * hittable when cell[k].x + cell[k].w <= clip_right. NULL-safe; a
 * non-positive r.h lays out zero-height cells without crashing. */
void ns_policy_layout(NsRect r, const int stop_text_w[NS_POLICY_STOPS],
                      int clip_right, int hit_h, int dpi,
                      NsPolicyLayout *out);

/* Hit-test a point against a laid-out pill. Returns HIT_POLICY_ALLOWED with
 * `*stop_out` set to the cell when (x, y) is inside that cell's hit rect,
 * otherwise HIT_NONE with `*stop_out = -1`. Zero-size (clipped) rects
 * never hit. NULL `l` and NULL `stop_out` are safe. */
int ns_policy_hit(const NsPolicyLayout *l, int x, int y, int *stop_out);

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
 * HIT_TEXT/HIT_CHECKBOX/HIT_TAG/HIT_COPY, or -1 otherwise (including HIT_NONE,
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
 * dpi). The copy icon is placed exactly as in approval_card_layout (left of
 * the chip, or at the right edge when there is no chip) and dropped the same
 * way when the text would get less than SZ_ICON. No checkbox (settled rows
 * aren't interactive) -- `out->checkbox` is
 * zeroed and `out->held` is always 0 (unlike ApprovalRowLayout's live-card
 * use, "held" isn't a settled-row concept; the outcome chip carries that
 * meaning instead). `cmd_text_w` is the full (unellipsised) pixel width of
 * the command text, measured by the caller in its own font; `out->ellipsis`
 * is set when it exceeds the text rect's width. `chip_w` <= 0 draws no chip
 * (the copy icon then sits at the right edge). Never crashes on NULL `out`. */
void settled_row_layout(NsRect r, int cmd_text_w, int chip_w, int text_h,
                        int dpi, ApprovalRowLayout *out);

#endif /* NUTSHELL_NS_LAYOUT_H */
