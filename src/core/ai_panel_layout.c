#include "ai_panel_layout.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <stdio.h>
#include <string.h>

void ai_panel_layout(NsRect panel, int dpi, int composer_h, AiPanelLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (composer_h < 0) composer_h = 0;

    int header_h = ns_scale(SP_XXL, dpi);
    int status_h = ns_scale(SZ_CTRL_H, dpi);

    out->header.x = panel.x;
    out->header.y = panel.y;
    out->header.w = panel.w;
    out->header.h = header_h;

    out->composer.x = panel.x;
    out->composer.w = panel.w;
    out->composer.h = composer_h;
    out->composer.y = panel.y + panel.h - composer_h;

    out->status.x = panel.x;
    out->status.w = panel.w;
    out->status.h = status_h;
    out->status.y = out->composer.y - status_h;

    out->thread.x = panel.x;
    out->thread.w = panel.w;
    out->thread.y = out->header.y + out->header.h;
    out->thread.h = out->status.y - out->thread.y;
    if (out->thread.h < 0) out->thread.h = 0;
}

void ai_status_layout(NsRect status, int dpi, int policy_w, int meter_text_w,
                       AiStatusLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    if (policy_w < 0) policy_w = 0;
    if (meter_text_w < 0) meter_text_w = 0;

    int pad_sm = ns_scale(SP_SM, dpi);
    int gap_md = ns_scale(SP_MD, dpi);
    int seg_h  = ns_scale(SZ_CTRL_H, dpi) - 2 * ns_scale(SP_XS, dpi);
    if (seg_h < 0) seg_h = 0;
    int seg_y  = status.y + (status.h - seg_h) / 2;

    out->policy.x = status.x;
    out->policy.y = seg_y;
    out->policy.w = policy_w;
    out->policy.h = seg_h;

    int content_right_limit = out->policy.x + out->policy.w + gap_md;
    int right_edge = status.x + status.w;
    int bar_w = ns_scale(60, dpi);

    /* Meter text is right-aligned with an SP_SM inset; the bar sits to its
     * left with an SP_SM gap. Drop the bar first when the line is tight,
     * then the text too, before squeezing the segments/auto label. */
    int meter_text_x = right_edge - pad_sm - meter_text_w;
    int meter_bar_x  = meter_text_x - pad_sm - bar_w;

    int have_text = meter_text_w > 0 && meter_text_x >= content_right_limit;
    int have_bar  = have_text && bar_w > 0 && meter_bar_x >= content_right_limit;

    if (have_text) {
        out->meter_text.x = meter_text_x;
        out->meter_text.y = seg_y;
        out->meter_text.w = meter_text_w;
        out->meter_text.h = seg_h;
    }
    if (have_bar) {
        out->meter_bar.x = meter_bar_x;
        out->meter_bar.y = seg_y;
        out->meter_bar.w = bar_w;
        out->meter_bar.h = seg_h;
    }
}

void thinking_layout(NsRect avail, int expanded, int body_text_h, int max_body_h,
                      int dpi, ThinkingLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    int pad_sm = ns_scale(SP_SM, dpi);
    int pad_xs = ns_scale(SP_XS, dpi);
    int row_h  = ns_scale(SZ_CTRL_H, dpi);
    int chev_sz = ns_scale(SZ_ICON, dpi);

    out->row.x = avail.x;
    out->row.y = avail.y;
    out->row.w = avail.w;
    out->row.h = row_h;

    out->chevron.x = avail.x + pad_sm;
    out->chevron.y = out->row.y + (row_h - chev_sz) / 2;
    out->chevron.w = chev_sz;
    out->chevron.h = chev_sz;

    int right_edge = avail.x + avail.w;
    int label_x = out->chevron.x + chev_sz + pad_sm;
    int remaining_w = right_edge - label_x;
    if (remaining_w < 0) remaining_w = 0;

    /* No text-width inputs are given for label/summary (their pixel width
     * depends on the caller's font and the live word count), so split the
     * remaining row between them; the painter draws each with its own
     * measured extent and these rects only bound where it may draw. */
    int label_w = remaining_w / 2;
    int summary_x = label_x + label_w + pad_sm;
    int summary_w = right_edge - summary_x;
    if (summary_w < 0) summary_w = 0;

    out->label.x = label_x;
    out->label.y = out->row.y;
    out->label.w = label_w;
    out->label.h = row_h;

    out->summary.x = summary_x;
    out->summary.y = out->row.y;
    out->summary.w = summary_w;
    out->summary.h = row_h;

    /* STROKE_BAR is a fixed pixel width, not DPI-scaled (see ns_type.h). */
    out->body.x = avail.x + pad_sm + STROKE_BAR + pad_sm;
    out->body.y = out->row.y + row_h + pad_xs;
    out->body.w = right_edge - out->body.x;
    if (out->body.w < 0) out->body.w = 0;

    int body_h = 0;
    if (expanded) {
        body_h = (body_text_h < max_body_h) ? body_text_h : max_body_h;
        if (body_h < 0) body_h = 0;
    }
    out->body.h = body_h;

    out->total_h = row_h + (expanded ? pad_xs + body_h : 0);
}

int ai_thinking_max_body_h(int line_height_px)
{
    if (line_height_px <= 0) return 0;
    return line_height_px * THINKING_MAX_LINES;
}

int ai_thinking_summary(int words, int streaming, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;

    if (streaming) {
        snprintf(buf, cap, "\xC2\xB7 streaming\xE2\x80\xA6");
    } else {
        if (words < 0) words = 0;
        snprintf(buf, cap, "\xC2\xB7 %d words", words);
    }
    return (int)strlen(buf);
}

int thinking_wheel_over_box(NsRect body, int viewport_h, int full_content_h,
                             int cursor_x, int cursor_y)
{
    if (body.w <= 0 || body.h <= 0) return 0;
    if (full_content_h <= body.h) return 0;   /* nothing to scroll */

    /* Clip the body rect to the viewport -- a box taller than the
     * viewport, or scrolled partly above/below it, is still interactive
     * over whichever part of it is actually on screen. */
    int vis_top = body.y > 0 ? body.y : 0;
    int vis_bottom = body.y + body.h < viewport_h ? body.y + body.h : viewport_h;
    if (vis_top >= vis_bottom) return 0;  /* entirely off-screen */

    if (cursor_x < body.x || cursor_x >= body.x + body.w) return 0;
    if (cursor_y < vis_top || cursor_y >= vis_bottom) return 0;

    return 1;
}

int thinking_wheel_should_chain(int old_scroll, int max_scroll, int wheel_delta)
{
    if (max_scroll < 0) max_scroll = 0;
    if (max_scroll <= 0) return 1;  /* nothing to scroll: always chain */

    if (wheel_delta > 0) return old_scroll <= 0;             /* at top */
    if (wheel_delta < 0) return old_scroll >= max_scroll;    /* at bottom */
    return 1;  /* no motion at all: nothing for the box to do */
}

int thinking_wheel_scroll(int old_scroll, int wheel_delta, int scroll_amount_px,
                           int max_scroll)
{
    if (max_scroll < 0) max_scroll = 0;

    int new_scroll = old_scroll - (wheel_delta * scroll_amount_px) / NS_WHEEL_DELTA;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;
    return new_scroll;
}
