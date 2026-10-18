#ifndef NUTSHELL_AI_PANEL_LAYOUT_H
#define NUTSHELL_AI_PANEL_LAYOUT_H

/*
 * ai_panel_layout — pure geometry for the AI Assist panel: the top-level
 * tiling (header / thread / status line / composer), the status-line's
 * internal controls (mode segmented control, auto-approve label, context
 * meter), the mode/permit label strings, and the "Thinking" disclosure
 * above a reply. Portable, no windows.h; shared by paint and hit-test in
 * src/ui. See docs/superpowers/specs/2026-09-07-ai-assist-panel-design.md
 * "Structure (frame B)" and "Thought process".
 */

#include "ns_layout.h"
#include <stddef.h>

/* Top-level panel tiling, top to bottom: header (SP_XXL tall), thread
 * (the rest, never negative), status line (SZ_CTRL_H tall), sash (SP_SM
 * tall, the drag handle that resizes the composer), composer (the caller's
 * own height, e.g. a multi-line input that grows). The sash is directly
 * above the composer and the status line directly above the sash; header
 * + thread + status + sash + composer sum to the panel height. */
typedef struct { NsRect header, thread, status, sash, composer; } AiPanelLayout;

void ai_panel_layout(NsRect panel, int dpi, int composer_h, AiPanelLayout *out);

/* Clamps a requested composer height to [2 * line_h + 2 * ns_scale(SP_SM,
 * dpi), panel_h / 2]. The minimum wins when the two conflict (a tiny
 * panel). line_h is the input font's line height in device pixels; a
 * value <= 0 is treated as ns_scale(16, dpi). */
int ai_composer_clamp_h(int want_h, int panel_h, int line_h, int dpi);

/* Status-line controls, left to right (docs/superpowers/specs/
 * 2026-10-18-ai-auto-approve-button-design.md section 3): the policy pill
 * (sliced by ns_policy_layout() -- this module only places it), free
 * space, the context meter (bar + "used / limit" text) or the busy label,
 * and the Auto-approve button at the right edge. The caller measures both
 * label sets; this picks one of five tiers as the row narrows:
 *   1 full labels, meter bar and text   2 full labels, meter text only
 *   3 full labels, no meter             4 compact labels R U W C / "Auto"
 *   5 compact, and the pill clipped at the button's left edge - SP_XS.
 * With sm = SP_SM, md = SP_MD, bar = 60 px, bf/bc = button text + 2*sm
 * (all through ns_scale): tier 1 needs full + md + bar + sm + text + sm +
 * bf + sm; tier 2 full + md + text + sm + bf + sm; tier 3 full + md + bf +
 * sm; tier 4 compact + md + bc + sm. No meter (text 0) skips 1 and 2. */
typedef struct {
    int policy_full_w;     /* ns_policy_width() over the full labels */
    int policy_compact_w;  /* ns_policy_width() over "R" "U" "W" "C" */
    int auto_full_w;       /* text width of "Auto-approve" */
    int auto_compact_w;    /* text width of "Auto" */
    int meter_text_w;      /* text width of "used / limit"; 0 = no meter */
} AiStatusMeasure;

enum {
    AI_STATUS_TIER_FULL = 1, AI_STATUS_TIER_METER_TEXT = 2,
    AI_STATUS_TIER_NO_METER = 3, AI_STATUS_TIER_COMPACT = 4,
    AI_STATUS_TIER_CLIPPED = 5
};

/* `policy` is the pill: x = status.x, w = the chosen label set's width (it
 * may run past `policy_clip` in tier 5; nothing paints or hits there).
 * `auto_btn` ends SP_SM in from the row's right edge in every tier. The
 * meter rects end at auto_btn.x - SP_SM. `label` is the busy label's
 * rect: from the visible pill's right edge + SP_MD to auto_btn.x - SP_SM,
 * zero-size when that is under 1 px. ai_chat.c and ns_draw.c compute no
 * rect of their own. */
typedef struct {
    NsRect policy;
    int    policy_clip;
    NsRect auto_btn;
    NsRect meter_bar, meter_text;
    NsRect label;
    int    compact;
    int    tier;
} AiStatusLayout;

/* NULL `m` is all zero; negative widths count as 0. NULL `out` is a no-op. */
void ai_status_layout(NsRect status, int dpi, const AiStatusMeasure *m,
                       AiStatusLayout *out);

/* Hit-test the status line: HIT_POLICY_AUTO inside sl->auto_btn,
 * otherwise ns_policy_hit() on `pl` (HIT_POLICY_ALLOWED with *stop_out, or
 * HIT_NONE). `*stop_out` is -1 unless a cell was hit. NULL-safe. */
int ai_status_hit(const AiStatusLayout *sl, const NsPolicyLayout *pl,
                  int x, int y, int *stop_out);

/* The "Thinking" disclosure above a reply that carried reasoning: a
 * clickable row (chevron, "Thinking" label, a dimmed summary) and, when
 * expanded, a body box below it with a STROKE_BAR-wide left bar reserved.
 * `body_text_h` is the wrapped reasoning text's measured height; `max_body_h`
 * caps it (half the thread height, per the spec) so long reasoning scrolls
 * inside the box rather than pushing the reply down. `total_h` is the
 * space the whole disclosure occupies (row alone when collapsed). */
typedef struct { NsRect row, chevron, label, summary, body; int total_h; } ThinkingLayout;

void thinking_layout(NsRect avail, int expanded, int body_text_h, int max_body_h,
                      int dpi, ThinkingLayout *out);

/* The expanded Thinking body is capped at THINKING_MAX_LINES lines of its
 * own text (maintainer request, 2026-09-24: collapsed by default, but if
 * opened, up to 50 lines and smart-scrollable) -- shorter content gets its
 * natural height instead (see thinking_layout()'s body_text_h/max_body_h
 * clamp above). ai_thinking_max_body_h() turns that line count into the
 * max_body_h pixel value thinking_layout() wants, given the body font's
 * actual measured line height (e.g. a Win32 TEXTMETRIC's tmHeight at the
 * font thinking_layout()'s caller paints with) -- already DPI-correct at
 * that font, so this is a straight multiply, no further DPI math needed.
 * Returns 0 if line_height_px <= 0. */
#define THINKING_MAX_LINES 50
int ai_thinking_max_body_h(int line_height_px);

/* Formats the collapsed/streaming summary text into `buf` (UTF-8 middle
 * dot U+00B7 lead-in): "· 240 words" or, while still streaming, "·
 * streaming…" (U+2026 ellipsis). Returns the formatted length (like
 * snprintf, but always the length of what's actually in `buf` after any
 * truncation to `cap`). buf/cap==0 is a no-op returning 0. */
int ai_thinking_summary(int words, int streaming, char *buf, size_t cap);

/* ── Expanded Thinking box: mouse-wheel routing ──────────────────────────
 * Pulled out of src/ui/chat_listview.c's WM_MOUSEWHEEL handler so the
 * hit-test and clamping are plain, tested functions instead of inline
 * arithmetic duplicated at the call site. */

/* Matches Win32's WHEEL_DELTA (one full notch); kept local so src/core
 * stays windows.h-free. Guaranteed by the Win32 ABI to never change. */
#define NS_WHEEL_DELTA 120

/* Whether the cursor at (cursor_x, cursor_y) is over the expanded
 * Thinking box's currently *visible* area, and the box actually has
 * content to scroll. `body` is the box's content rect, as built by
 * thinking_layout() -- it is capped at THINKING_MAX_LINES (50) lines
 * regardless of viewport size (maintainer decision, 2026-09-24: see
 * docs/superpowers/specs/2026-09-07-ai-assist-panel-design.md "Thought
 * process"), so on a short window it routinely exceeds `viewport_h` and
 * is clipped top and/or bottom by the chat list's own viewport -- only
 * requiring the *whole* box to fit on screen (the original rule) meant a
 * reply past ~40 lines of reasoning could never take the wheel at all,
 * which was the actual bug behind "I can't stop or scroll the thinking
 * window. Instead, I only control the AI assist panel." (2026-09-25).
 * `full_content_h` is the reasoning text's full, unclamped height. This
 * only gates by position/visibility; whether a given wheel notch should
 * still chain to the outer list once the box hits its own top/bottom is
 * thinking_wheel_should_chain()'s job. */
int thinking_wheel_over_box(NsRect body, int viewport_h, int full_content_h,
                             int cursor_x, int cursor_y);

/* Whether a wheel notch (GET_WHEEL_DELTA_WPARAM's raw signed
 * `wheel_delta`) over an already-hit box should chain to the outer list
 * instead of scrolling the box, given the box's current scroll position
 * `old_scroll` and its `max_scroll` (full_content_h - body.h, clamped to
 * >= 0). Chains exactly when the box is already at the limit the wheel is
 * pushing toward: scroll_y == 0 with an upward notch (wheel_delta > 0), or
 * scroll_y >= max_scroll with a downward one -- ordinary nested-scroll
 * chaining, same as a scrollable div handing off to the page beneath it.
 * A box with nothing to scroll (max_scroll <= 0) always chains.
 *
 * This is also what replaces the old "box must be fully visible" rule for
 * the streaming-swallows-wheel-down concern it was guarding against: while
 * a reply streams, the box auto-follows (thinking_autoscroll), so
 * old_scroll sits at max_scroll almost continuously -- wheel-down chains
 * to the outer list immediately, letting it reach bottom and re-engage its
 * own stick-to-bottom, with no dependency on the box's on-screen size. */
int thinking_wheel_should_chain(int old_scroll, int max_scroll, int wheel_delta);

/* Applies one WM_MOUSEWHEEL notch to old_scroll, clamped to
 * [0, max_scroll]. `wheel_delta` is GET_WHEEL_DELTA_WPARAM's raw signed
 * value; `scroll_amount_px` is the (already DPI-scaled) pixel distance one
 * full NS_WHEEL_DELTA notch moves. Positive wheel_delta (wheel away from
 * the user) decreases the scroll offset -- same convention the outer chat
 * list's own scroll_y uses. max_scroll <= 0 clamps to 0. A delta too small
 * to move even one pixel (e.g. a high-resolution touchpad's fractional
 * notch) still returns old_scroll unchanged; the caller must not treat
 * that as "nothing to do here, bubble it" -- thinking_wheel_should_chain()
 * already decided this notch belongs to the box, so it must still be
 * consumed (not leaked to the outer list) even when it moves nothing. */
int thinking_wheel_scroll(int old_scroll, int wheel_delta, int scroll_amount_px,
                           int max_scroll);

#endif /* NUTSHELL_AI_PANEL_LAYOUT_H */
