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
 * (the rest, never negative), status line (SZ_CTRL_H tall, directly above
 * the composer), composer (the caller's own height, e.g. a multi-line
 * input that grows). */
typedef struct { NsRect header, thread, status, composer; } AiPanelLayout;

void ai_panel_layout(NsRect panel, int dpi, int composer_h, AiPanelLayout *out);

/* Status-line controls, left to right: the command-policy control (one
 * pill of four stops, sliced up by ns_policy_layout() -- this module only
 * places it), a spacer, then the context meter (bar + "used / limit" text)
 * right-aligned. `policy_w` is the control's pixel width, from
 * ns_policy_width() over the caller's measured stop labels; `meter_text_w`
 * is the measured width of the meter numbers. When the line is too narrow
 * to fit everything, meter_bar is dropped first (zero-size), then
 * meter_text, before the policy control is squeezed. */
typedef struct { NsRect policy, meter_bar, meter_text; } AiStatusLayout;

void ai_status_layout(NsRect status, int dpi, int policy_w, int meter_text_w,
                       AiStatusLayout *out);

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
