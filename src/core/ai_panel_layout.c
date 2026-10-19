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
    int sash_h = ns_scale(SP_SM, dpi);

    out->header.x = panel.x;
    out->header.y = panel.y;
    out->header.w = panel.w;
    out->header.h = header_h;

    out->composer.x = panel.x;
    out->composer.w = panel.w;
    out->composer.h = composer_h;
    out->composer.y = panel.y + panel.h - composer_h;

    out->sash.x = panel.x;
    out->sash.w = panel.w;
    out->sash.h = sash_h;
    out->sash.y = out->composer.y - sash_h;

    out->status.x = panel.x;
    out->status.w = panel.w;
    out->status.h = status_h;
    out->status.y = out->sash.y - status_h;

    out->thread.x = panel.x;
    out->thread.w = panel.w;
    out->thread.y = out->header.y + out->header.h;
    out->thread.h = out->status.y - out->thread.y;
    if (out->thread.h < 0) out->thread.h = 0;
}

int ai_composer_clamp_h(int want_h, int panel_h, int line_h, int dpi)
{
    if (line_h <= 0) line_h = ns_scale(16, dpi);
    int min_h = 2 * line_h + 2 * ns_scale(SP_SM, dpi);
    int max_h = panel_h / 2;

    if (want_h > max_h) want_h = max_h;
    if (want_h < min_h) want_h = min_h;   /* the minimum wins over the maximum */
    return want_h;
}

static int nonneg(int v) { return v > 0 ? v : 0; }

void ai_status_layout(NsRect status, int dpi, const AiStatusMeasure *m,
                       AiStatusLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    int full_w = m ? nonneg(m->policy_full_w) : 0;
    int comp_w = m ? nonneg(m->policy_compact_w) : 0;
    int afull  = m ? nonneg(m->auto_full_w) : 0;
    int acomp  = m ? nonneg(m->auto_compact_w) : 0;
    int text_w = m ? nonneg(m->meter_text_w) : 0;

    int pad_xs = ns_scale(SP_XS, dpi);
    int pad_sm = ns_scale(SP_SM, dpi);
    int gap_md = ns_scale(SP_MD, dpi);
    int bar_w  = ns_scale(60, dpi);
    int seg_h  = ns_scale(SZ_CTRL_H, dpi) - 2 * pad_xs;
    if (seg_h < 0) seg_h = 0;
    int seg_y  = status.y + (status.h - seg_h) / 2;
    int w      = nonneg(status.w);

    int btn_full = afull + 2 * pad_sm;
    int btn_comp = acomp + 2 * pad_sm;
    int need_full    = full_w + gap_md + bar_w + pad_sm + text_w + pad_sm + btn_full + pad_sm;
    int need_text    = full_w + gap_md + text_w + pad_sm + btn_full + pad_sm;
    int need_nometer = full_w + gap_md + btn_full + pad_sm;
    int need_comp    = comp_w + gap_md + btn_comp + pad_sm;

    int tier;
    if (text_w > 0 && w >= need_full)      tier = AI_STATUS_TIER_FULL;
    else if (text_w > 0 && w >= need_text) tier = AI_STATUS_TIER_METER_TEXT;
    else if (w >= need_nometer)            tier = AI_STATUS_TIER_NO_METER;
    else if (w >= need_comp)               tier = AI_STATUS_TIER_COMPACT;
    else                                   tier = AI_STATUS_TIER_CLIPPED;
    out->tier = tier;
    out->compact = (tier >= AI_STATUS_TIER_COMPACT) ? 1 : 0;

    int btn_w = out->compact ? btn_comp : btn_full;
    int right = status.x + w;
    out->auto_btn.x = right - pad_sm - btn_w;
    out->auto_btn.y = seg_y;
    out->auto_btn.w = btn_w;
    out->auto_btn.h = seg_h;

    out->policy.x = status.x;
    out->policy.y = seg_y;
    out->policy.w = out->compact ? comp_w : full_w;
    out->policy.h = seg_h;
    out->policy_clip = out->auto_btn.x - pad_xs;

    int meter_right = out->auto_btn.x - pad_sm;
    if (tier <= AI_STATUS_TIER_METER_TEXT) {
        out->meter_text.x = meter_right - text_w;
        out->meter_text.y = seg_y;
        out->meter_text.w = text_w;
        out->meter_text.h = seg_h;
        if (tier == AI_STATUS_TIER_FULL) {
            out->meter_bar.x = out->meter_text.x - pad_sm - bar_w;
            out->meter_bar.y = seg_y;
            out->meter_bar.w = bar_w;
            out->meter_bar.h = seg_h;
        }
    }

    int pill_right = out->policy.x + out->policy.w;
    if (pill_right > out->policy_clip) pill_right = out->policy_clip;
    int label_x = pill_right + gap_md;
    if (meter_right - label_x > 0) {
        out->label.x = label_x;
        out->label.y = seg_y;
        out->label.w = meter_right - label_x;
        out->label.h = seg_h;
    }
}

int ai_status_hit(const AiStatusLayout *sl, const NsPolicyLayout *pl,
                  int x, int y, int *stop_out)
{
    if (stop_out) *stop_out = -1;
    if (sl) {
        NsRect b = sl->auto_btn;
        if (b.w > 0 && b.h > 0 && x >= b.x && x < b.x + b.w &&
            y >= b.y && y < b.y + b.h)
            return HIT_POLICY_AUTO;
    }
    return ns_policy_hit(pl, x, y, stop_out);
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
