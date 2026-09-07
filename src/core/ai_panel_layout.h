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

/* Status-line controls, left to right: the two-segment mode control
 * (Read-only / Read + write) as one touching control, the auto-approve
 * label, a spacer, then the context meter (bar + "used / limit" text)
 * right-aligned. `seg0_text_w`/`seg1_text_w`/`auto_text_w`/`meter_text_w`
 * are pixel widths the caller measured for each label in its own font.
 * When the line is too narrow to fit everything, meter_bar is dropped
 * first (zero-size), then meter_text, before anything else is squeezed. */
typedef struct { NsRect seg[2], auto_label, meter_bar, meter_text; } AiStatusLayout;

void ai_status_layout(NsRect status, int dpi, int seg0_text_w, int seg1_text_w,
                       int auto_text_w, int meter_text_w, AiStatusLayout *out);

/* "Auto approve: <label>" text: "off" when auto-approve is off; otherwise
 * "safe only" / "safe + write" / "all" for level 0/1/2 (AutoApproveLevel).
 * Out-of-range levels clamp: <0 to "safe only", >2 to "all". */
const char *ai_modes_label(int auto_on, int level);

/* Read + write segment label: "off" / "on". */
const char *ai_permit_label(int on);

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

/* Formats the collapsed/streaming summary text into `buf` (UTF-8 middle
 * dot U+00B7 lead-in): "· 240 words" or, while still streaming, "·
 * streaming…" (U+2026 ellipsis). Returns the formatted length (like
 * snprintf, but always the length of what's actually in `buf` after any
 * truncation to `cap`). buf/cap==0 is a no-op returning 0. */
int ai_thinking_summary(int words, int streaming, char *buf, size_t cap);

#endif /* NUTSHELL_AI_PANEL_LAYOUT_H */
