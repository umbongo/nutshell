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

void ai_status_layout(NsRect status, int dpi, int seg0_text_w, int seg1_text_w,
                       int auto_text_w, int meter_text_w, AiStatusLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    if (seg0_text_w < 0) seg0_text_w = 0;
    if (seg1_text_w < 0) seg1_text_w = 0;
    if (auto_text_w < 0) auto_text_w = 0;
    if (meter_text_w < 0) meter_text_w = 0;

    int pad_sm = ns_scale(SP_SM, dpi);
    int gap_md = ns_scale(SP_MD, dpi);
    int seg_h  = ns_scale(SZ_CTRL_H, dpi) - 2 * ns_scale(SP_XS, dpi);
    if (seg_h < 0) seg_h = 0;
    int seg_y  = status.y + (status.h - seg_h) / 2;

    int seg0_w = seg0_text_w + 2 * pad_sm;
    int seg1_w = seg1_text_w + 2 * pad_sm;

    out->seg[0].x = status.x;
    out->seg[0].y = seg_y;
    out->seg[0].w = seg0_w;
    out->seg[0].h = seg_h;

    /* Touching seg[0] -- the two segments read as one control. */
    out->seg[1].x = out->seg[0].x + seg0_w;
    out->seg[1].y = seg_y;
    out->seg[1].w = seg1_w;
    out->seg[1].h = seg_h;

    out->auto_label.x = out->seg[1].x + seg1_w + gap_md;
    out->auto_label.y = seg_y;
    out->auto_label.w = auto_text_w;
    out->auto_label.h = seg_h;

    int content_right_limit = out->auto_label.x + out->auto_label.w;
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

const char *ai_modes_label(int auto_on, int level)
{
    if (!auto_on) return "off";
    if (level <= 0) return "safe only";
    if (level == 1) return "safe + unknown";
    if (level == 2) return "safe + write";
    if (level == 3) return "safe + unknown + write";
    return "all"; /* level >= 4, including out-of-range values */
}

const char *ai_permit_label(int on)
{
    return on ? "on" : "off";
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
