#include "test_framework.h"
#include "ai_panel_layout.h"
#include "ai_panel_states.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

/* ===========================================================================
 * AI Assist panel tests (plan: docs/superpowers/plans/2026-09-07-ai-assist-panel.md
 * task 1) -- see
 * docs/superpowers/specs/2026-09-07-ai-assist-panel-design.md "Structure
 * (frame B)", "Thought process" and "Empty and blocked states (frame C)".
 * ===========================================================================
 */

static int rects_overlap(NsRect a, NsRect b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return 0;
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

static int rect_inside(NsRect inner, NsRect outer)
{
    if (inner.w <= 0 || inner.h <= 0) return 1;
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

/* ---- ai_panel_layout -------------------------------------------------------- */

int test_ai_panel_layout_tiles_exact_sum_96(void)
{
    TEST_BEGIN();
    NsRect panel = { 0, 0, 400, 600 };
    AiPanelLayout out;
    ai_panel_layout(panel, 96, 40, &out);

    ASSERT_EQ(out.header.h, 32);   /* SP_XXL at 96 */
    ASSERT_EQ(out.status.h, 28);   /* SZ_CTRL_H at 96 */
    ASSERT_EQ(out.composer.h, 40);
    ASSERT_TRUE(out.thread.h >= 0);
    ASSERT_EQ(out.sash.h, 8);      /* SP_SM at 96 */
    ASSERT_EQ(out.header.h + out.thread.h + out.status.h + out.sash.h + out.composer.h, panel.h);

    ASSERT_TRUE(rect_inside(out.sash, panel));
    ASSERT_TRUE(!rects_overlap(out.sash, out.thread));
    ASSERT_TRUE(!rects_overlap(out.sash, out.status));
    ASSERT_TRUE(!rects_overlap(out.sash, out.composer));
    ASSERT_TRUE(rect_inside(out.header, panel));
    ASSERT_TRUE(rect_inside(out.thread, panel));
    ASSERT_TRUE(rect_inside(out.status, panel));
    ASSERT_TRUE(rect_inside(out.composer, panel));
    ASSERT_TRUE(!rects_overlap(out.header, out.thread));
    ASSERT_TRUE(!rects_overlap(out.thread, out.status));
    ASSERT_TRUE(!rects_overlap(out.status, out.composer));
    ASSERT_TRUE(!rects_overlap(out.header, out.status));
    ASSERT_TRUE(!rects_overlap(out.header, out.composer));
    ASSERT_TRUE(!rects_overlap(out.thread, out.composer));

    /* top to bottom order */
    ASSERT_TRUE(out.header.y <= out.thread.y);
    ASSERT_TRUE(out.thread.y <= out.status.y);
    ASSERT_TRUE(out.status.y <= out.composer.y);
    TEST_END();
}

int test_ai_panel_layout_tiles_exact_sum_192(void)
{
    TEST_BEGIN();
    NsRect panel = { 0, 0, 800, 1200 };
    AiPanelLayout out;
    ai_panel_layout(panel, 192, 80, &out);

    ASSERT_EQ(out.header.h, 64);   /* SP_XXL at 192 */
    ASSERT_EQ(out.status.h, 56);   /* SZ_CTRL_H at 192 */
    ASSERT_EQ(out.composer.h, 80);
    ASSERT_TRUE(out.thread.h >= 0);
    ASSERT_EQ(out.sash.h, 16);     /* SP_SM at 192 */
    ASSERT_EQ(out.header.h + out.thread.h + out.status.h + out.sash.h + out.composer.h, panel.h);
    ASSERT_TRUE(rect_inside(out.sash, panel));
    ASSERT_TRUE(rect_inside(out.header, panel));
    ASSERT_TRUE(rect_inside(out.thread, panel));
    ASSERT_TRUE(rect_inside(out.status, panel));
    ASSERT_TRUE(rect_inside(out.composer, panel));
    TEST_END();
}

int test_ai_panel_layout_thread_nonnegative_when_panel_tiny(void)
{
    TEST_BEGIN();
    NsRect panel = { 0, 0, 100, 10 };
    AiPanelLayout out;
    ai_panel_layout(panel, 96, 40, &out);
    ASSERT_TRUE(out.thread.h >= 0);
    ASSERT_EQ(out.thread.h, 0);
    TEST_END();
}

int test_ai_panel_layout_sash_between_status_and_composer(void)
{
    TEST_BEGIN();
    NsRect panel = { 10, 20, 400, 600 };
    AiPanelLayout out;
    ai_panel_layout(panel, 96, 60, &out);

    ASSERT_EQ(out.sash.x, panel.x);
    ASSERT_EQ(out.sash.w, panel.w);
    ASSERT_EQ(out.sash.h, 8);
    /* sash sits directly above the composer, status directly above the sash */
    ASSERT_EQ(out.sash.y + out.sash.h, out.composer.y);
    ASSERT_EQ(out.status.y + out.status.h, out.sash.y);
    ASSERT_EQ(out.thread.y + out.thread.h, out.status.y);
    ASSERT_EQ(out.composer.y + out.composer.h, panel.y + panel.h);
    TEST_END();
}

int test_ai_panel_layout_sash_tiny_panel_keeps_thread_nonnegative(void)
{
    TEST_BEGIN();
    NsRect panel = { 0, 0, 100, 10 };
    AiPanelLayout out;
    ai_panel_layout(panel, 96, 40, &out);
    ASSERT_EQ(out.thread.h, 0);
    ASSERT_EQ(out.sash.h, 8);
    TEST_END();
}

/* ---- ai_composer_clamp_h ------------------------------------------------------ */

int test_ai_composer_clamp_h_96(void)
{
    TEST_BEGIN();
    /* min = 2*16 + 2*8 = 48, max = 600/2 = 300 */
    ASSERT_EQ(ai_composer_clamp_h(46, 600, 16, 96), 48);    /* below min */
    ASSERT_EQ(ai_composer_clamp_h(48, 600, 16, 96), 48);    /* at min */
    ASSERT_EQ(ai_composer_clamp_h(120, 600, 16, 96), 120);  /* in range */
    ASSERT_EQ(ai_composer_clamp_h(300, 600, 16, 96), 300);  /* at max */
    ASSERT_EQ(ai_composer_clamp_h(900, 600, 16, 96), 300);  /* above max */
    TEST_END();
}

int test_ai_composer_clamp_h_192(void)
{
    TEST_BEGIN();
    /* min = 2*32 + 2*16 = 96, max = 1200/2 = 600 */
    ASSERT_EQ(ai_composer_clamp_h(10, 1200, 32, 192), 96);
    ASSERT_EQ(ai_composer_clamp_h(200, 1200, 32, 192), 200);
    ASSERT_EQ(ai_composer_clamp_h(5000, 1200, 32, 192), 600);
    TEST_END();
}

int test_ai_composer_clamp_h_minimum_wins_on_tiny_panel(void)
{
    TEST_BEGIN();
    /* panel_h/2 = 25 < min 48: the minimum wins */
    ASSERT_EQ(ai_composer_clamp_h(100, 50, 16, 96), 48);
    ASSERT_EQ(ai_composer_clamp_h(10, 50, 16, 96), 48);
    ASSERT_EQ(ai_composer_clamp_h(100, 0, 16, 96), 48);
    TEST_END();
}

int test_ai_composer_clamp_h_bad_inputs(void)
{
    TEST_BEGIN();
    /* line_h <= 0 -> ns_scale(16, dpi): min = 32 + 16 = 48 at 96 DPI */
    ASSERT_EQ(ai_composer_clamp_h(0, 600, 0, 96), 48);
    ASSERT_EQ(ai_composer_clamp_h(100, 600, -5, 96), 100);
    ASSERT_EQ(ai_composer_clamp_h(0, 1200, 0, 192), 96);
    /* negative want and negative panel */
    ASSERT_EQ(ai_composer_clamp_h(-50, 600, 16, 96), 48);
    ASSERT_EQ(ai_composer_clamp_h(-50, -600, 16, 96), 48);
    TEST_END();
}

/* ---- ai_status_layout (2026-10-18 auto-approve spec, section 3) ------------ */

/* Measured widths at 96 DPI, near the real caption font. Thresholds for
 * these (SP_SM 8, SP_MD 12, bar 60, button 72+16 / 28+16):
 *   tier 1 >= 200 + 12 + 60 + 8 + 64 + 8 + 88 + 8 = 448
 *   tier 2 >= 200 + 12 + 64 + 8 + 88 + 8           = 380
 *   tier 3 >= 200 + 12 + 88 + 8                    = 308
 *   tier 4 >=  80 + 12 + 44 + 8                    = 144 */
static const AiStatusMeasure STATUS_M96 = {
    .policy_full_w = 200, .policy_compact_w = 80,
    .auto_full_w = 72, .auto_compact_w = 28, .meter_text_w = 64
};

static AiStatusLayout status_at(int w, int dpi, const AiStatusMeasure *m)
{
    NsRect status = { 10, 4, w, ns_scale(SZ_CTRL_H, dpi) };
    AiStatusLayout out;
    ai_status_layout(status, dpi, m, &out);
    return out;
}

/* The pill as it is seen: its rect cut at policy_clip. */
static NsRect visible_pill(const AiStatusLayout *l)
{
    NsRect v = l->policy;
    if (v.x + v.w > l->policy_clip) v.w = l->policy_clip - v.x;
    if (v.w < 0) v.w = 0;
    return v;
}

static AiStatusMeasure scaled_measure(int dpi)
{
    AiStatusMeasure m;
    m.policy_full_w    = ns_scale(STATUS_M96.policy_full_w, dpi);
    m.policy_compact_w = ns_scale(STATUS_M96.policy_compact_w, dpi);
    m.auto_full_w      = ns_scale(STATUS_M96.auto_full_w, dpi);
    m.auto_compact_w   = ns_scale(STATUS_M96.auto_compact_w, dpi);
    m.meter_text_w     = ns_scale(STATUS_M96.meter_text_w, dpi);
    return m;
}

int test_ai_status_layout_tier_inside_each_band_96(void)
{
    TEST_BEGIN();
    static const int widths[5] = { 600, 400, 330, 200, 100 };
    for (int i = 0; i < 5; i++) {
        AiStatusLayout l = status_at(widths[i], 96, &STATUS_M96);
        ASSERT_EQ(l.tier, i + 1);
        ASSERT_EQ(l.compact, i >= 3 ? 1 : 0);
        ASSERT_EQ(l.meter_bar.w > 0, i == 0);
        ASSERT_EQ(l.meter_text.w > 0, i <= 1);
        ASSERT_EQ(l.policy.w, i >= 3 ? 80 : 200);
        ASSERT_EQ(l.auto_btn.w, i >= 3 ? 44 : 88);
    }
    TEST_END();
}

int test_ai_status_layout_tier1_geometry_96(void)
{
    TEST_BEGIN();
    AiStatusLayout l = status_at(600, 96, &STATUS_M96);
    /* Row {10, 4, 600, 28}: button 88 wide ending 8 in from 610. */
    ASSERT_EQ(l.auto_btn.x, 514);
    ASSERT_EQ(l.policy_clip, 510);
    ASSERT_EQ(l.meter_text.x, 442);    /* 514 - 8 - 64 */
    ASSERT_EQ(l.meter_bar.x, 374);     /* 442 - 8 - 60 */
    ASSERT_EQ(l.label.x, 222);         /* 10 + 200 + 12 */
    ASSERT_EQ(l.label.w, 284);         /* to 514 - 8 */
    ASSERT_EQ(l.policy.x, 10);
    ASSERT_EQ(l.policy.h, 20);         /* SZ_CTRL_H - 2 * SP_XS */
    ASSERT_EQ(l.policy.y, 8);
    ASSERT_EQ(l.auto_btn.y, l.policy.y);
    ASSERT_EQ(l.auto_btn.h, l.policy.h);
    TEST_END();
}

int test_ai_status_layout_thresholds_96(void)
{
    TEST_BEGIN();
    static const int t[4] = { 448, 380, 308, 144 };
    for (int i = 0; i < 4; i++) {
        AiStatusLayout at = status_at(t[i], 96, &STATUS_M96);
        AiStatusLayout below = status_at(t[i] - 1, 96, &STATUS_M96);
        ASSERT_EQ(at.tier, i + 1);
        ASSERT_EQ(below.tier, i + 2);
    }
    TEST_END();
}

/* Review Focus 1: at 125 % and 150 % scaling every threshold still flips
 * the tier exactly. The thresholds are hand-computed literals, not the
 * production formula run again. ns_scale() = round(px * dpi / 96).
 *
 * 120 DPI (x1.25): sm 10, md 15, bar 75; fixture measures full 250,
 * compact 100, auto 90 / 35, meter text 80; bf 90+20 = 110, bc 35+20 = 55.
 *   tier 1 >= 250+15+75+10+80+10+110+10 = 560
 *   tier 2 >= 250+15+80+10+110+10       = 475
 *   tier 3 >= 250+15+110+10             = 385
 *   tier 4 >= 100+15+55+10              = 180
 * 144 DPI (x1.5): sm 12, md 18, bar 90; full 300, compact 120, auto
 * 108 / 42, meter text 96; bf 108+24 = 132, bc 42+24 = 66.
 *   tier 1 >= 300+18+90+12+96+12+132+12 = 672
 *   tier 2 >= 300+18+96+12+132+12       = 570
 *   tier 3 >= 300+18+132+12             = 462
 *   tier 4 >= 120+18+66+12              = 216 */
int test_ai_status_layout_thresholds_120_and_144(void)
{
    TEST_BEGIN();
    static const int dpis[2] = { 120, 144 };
    static const int want[2][4] = { { 560, 475, 385, 180 },
                                    { 672, 570, 462, 216 } };
    static const int meas[2][5] = { { 250, 100, 90, 35, 80 },
                                    { 300, 120, 108, 42, 96 } };
    for (int di = 0; di < 2; di++) {
        int dpi = dpis[di];
        AiStatusMeasure m = scaled_measure(dpi);
        /* Anchor the hand arithmetic to what ns_scale() really gives. */
        ASSERT_EQ(m.policy_full_w, meas[di][0]);
        ASSERT_EQ(m.policy_compact_w, meas[di][1]);
        ASSERT_EQ(m.auto_full_w, meas[di][2]);
        ASSERT_EQ(m.auto_compact_w, meas[di][3]);
        ASSERT_EQ(m.meter_text_w, meas[di][4]);
        for (int i = 0; i < 4; i++) {
            AiStatusLayout at = status_at(want[di][i], dpi, &m);
            AiStatusLayout below = status_at(want[di][i] - 1, dpi, &m);
            if (at.tier != i + 1 || below.tier != i + 2) {
                printf("  dpi %d threshold %d (%d px): tier %d at, %d below\n",
                       dpi, i, want[di][i], at.tier, below.tier);
                _tf_local_fail = 1;
            }
        }
    }
    TEST_END();
}

/* Review Focus 2: no context meter (no key, unknown model) -- tiers 1 and 2
 * cannot exist, and the busy label still ends before the button. */
int test_ai_status_layout_no_meter_goes_straight_to_tier3(void)
{
    TEST_BEGIN();
    AiStatusMeasure m = STATUS_M96;
    m.meter_text_w = 0;
    AiStatusLayout l = status_at(600, 96, &m);
    ASSERT_EQ(l.tier, AI_STATUS_TIER_NO_METER);
    ASSERT_EQ(l.compact, 0);
    ASSERT_EQ(l.meter_bar.w, 0);
    ASSERT_EQ(l.meter_text.w, 0);
    ASSERT_TRUE(l.label.w > 0);
    ASSERT_EQ(l.label.x + l.label.w, l.auto_btn.x - 8);
    ASSERT_EQ(status_at(308, 96, &m).tier, AI_STATUS_TIER_NO_METER);
    ASSERT_EQ(status_at(307, 96, &m).tier, AI_STATUS_TIER_COMPACT);
    TEST_END();
}

/* Review Focus 3: every width from 0 to 1400 at 96 and 192 DPI -- the
 * button keeps its rect at the right edge, the clip sits SP_XS left of
 * it, the pill (as seen), the button, the meter and the label never
 * overlap one another (label and meter are alternatives and may), and
 * widening the row never moves to a narrower tier. */
int test_ai_status_layout_width_sweep_invariants(void)
{
    TEST_BEGIN();
    static const int dpis[2] = { 96, 192 };
    for (int di = 0; di < 2; di++) {
        int dpi = dpis[di];
        AiStatusMeasure m = scaled_measure(dpi);
        int sm = ns_scale(SP_SM, dpi), xs = ns_scale(SP_XS, dpi);
        int prev_tier = AI_STATUS_TIER_CLIPPED;
        for (int w = 0; w <= 1400; w++) {
            NsRect status = { 10, 4, w, ns_scale(SZ_CTRL_H, dpi) };
            AiStatusLayout l;
            ai_status_layout(status, dpi, &m, &l);
            NsRect pill = visible_pill(&l);
            int bw = (l.compact ? m.auto_compact_w : m.auto_full_w) + 2 * sm;
            int bad = 0;
            if (l.tier < 1 || l.tier > 5 || l.tier > prev_tier) bad = 1;
            if (l.compact != (l.tier >= AI_STATUS_TIER_COMPACT)) bad = 1;
            if (l.auto_btn.w != bw) bad = 1;
            if (l.auto_btn.x + l.auto_btn.w != status.x + w - sm) bad = 1;
            if (l.policy_clip != l.auto_btn.x - xs) bad = 1;
            if (l.label.w < 0) bad = 1;
            if (l.label.w > 0 && l.label.x + l.label.w != l.auto_btn.x - sm) bad = 1;
            if (rects_overlap(pill, l.auto_btn) || rects_overlap(pill, l.meter_bar) ||
                rects_overlap(pill, l.meter_text) || rects_overlap(pill, l.label) ||
                rects_overlap(l.auto_btn, l.meter_bar) ||
                rects_overlap(l.auto_btn, l.meter_text) ||
                rects_overlap(l.auto_btn, l.label) ||
                rects_overlap(l.meter_bar, l.meter_text)) bad = 1;
            if (!rect_inside(l.meter_bar, status) || !rect_inside(l.meter_text, status) ||
                !rect_inside(l.label, status)) bad = 1;
            if (bad) {
                printf("  dpi %d w %d: tier %d compact %d btn {%d,%d} clip %d "
                       "pill {%d,%d} label {%d,%d}\n", dpi, w, l.tier, l.compact,
                       l.auto_btn.x, l.auto_btn.w, l.policy_clip, pill.x, pill.w,
                       l.label.x, l.label.w);
                _tf_local_fail = 1;
                break;
            }
            prev_tier = l.tier;
        }
    }
    TEST_END();
}

int test_ai_status_layout_negative_and_null_inputs(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 600, 28 };
    AiStatusMeasure bad = { -40, -10, -5, -5, -10 };
    AiStatusLayout out;
    ai_status_layout(status, 96, &bad, &out);
    ASSERT_EQ(out.policy.w, 0);
    ASSERT_EQ(out.meter_text.w, 0);
    ASSERT_EQ(out.meter_bar.w, 0);
    ASSERT_EQ(out.auto_btn.w, 16);                 /* 0 + 2 * SP_SM */
    ai_status_layout(status, 96, NULL, &out);      /* NULL measure = all zero */
    ASSERT_EQ(out.tier, AI_STATUS_TIER_NO_METER);
    ai_status_layout(status, 96, &STATUS_M96, NULL);   /* must not crash */
    NsRect neg = { 0, 0, -50, 28 };
    ai_status_layout(neg, 96, &STATUS_M96, &out);
    ASSERT_EQ(out.tier, AI_STATUS_TIER_CLIPPED);
    ASSERT_EQ(out.label.w, 0);
    TEST_END();
}

int test_ai_status_hit_cells_button_and_clipped_cell(void)
{
    TEST_BEGIN();
    int full_text[NS_POLICY_STOPS]  = { 40, 30, 50, 60 };  /* cells 56 46 66 76 */
    int short_text[NS_POLICY_STOPS] = { 8, 8, 8, 8 };      /* cells 24 each */
    AiStatusMeasure m = STATUS_M96;
    m.policy_full_w = ns_policy_width(full_text, 96);      /* 244 */
    m.policy_compact_w = ns_policy_width(short_text, 96);  /* 96 */

    NsRect wide = { 10, 4, 600, 28 };
    AiStatusLayout sl;
    NsPolicyLayout pl;
    ai_status_layout(wide, 96, &m, &sl);
    ns_policy_layout(sl.policy, full_text, sl.policy_clip, wide.h, 96, &pl);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        int stop = -99;
        NsRect c = pl.cell[k];
        ASSERT_EQ(ai_status_hit(&sl, &pl, c.x + c.w / 2, c.y + c.h / 2, &stop), HIT_POLICY_ALLOWED);
        ASSERT_EQ(stop, k);
    }
    int stop = -99;
    NsRect b = sl.auto_btn;
    ASSERT_EQ(ai_status_hit(&sl, &pl, b.x + b.w / 2, b.y + b.h / 2, &stop), HIT_POLICY_AUTO);
    ASSERT_EQ(stop, -1);
    ASSERT_EQ(ai_status_hit(&sl, &pl, pl.cell[3].x + pl.cell[3].w + 2, 18, &stop), HIT_NONE);

    /* Tier 5: row 100 wide, button at 58..102, clip 54. Compact cell 0 is
     * 10..34 (whole), cell 1 is 34..58 (cut at 54: unhittable). */
    NsRect narrow = { 10, 4, 100, 28 };
    ai_status_layout(narrow, 96, &m, &sl);
    ASSERT_EQ(sl.tier, AI_STATUS_TIER_CLIPPED);
    ns_policy_layout(sl.policy, short_text, sl.policy_clip, narrow.h, 96, &pl);
    stop = -99;
    ASSERT_EQ(ai_status_hit(&sl, &pl, 20, 18, &stop), HIT_POLICY_ALLOWED);
    ASSERT_EQ(stop, 0);
    stop = -99;
    ASSERT_EQ(ai_status_hit(&sl, &pl, 40, 18, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    b = sl.auto_btn;
    ASSERT_EQ(ai_status_hit(&sl, &pl, b.x + b.w / 2, b.y + b.h / 2, NULL), HIT_POLICY_AUTO);

    stop = -99;
    ASSERT_EQ(ai_status_hit(NULL, NULL, 0, 0, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    TEST_END();
}

/* ---- thinking_layout ---------------------------------------------------------- */

int test_thinking_layout_collapsed_body_zero(void)
{
    TEST_BEGIN();
    NsRect avail = { 0, 0, 400, 300 };
    ThinkingLayout out;
    thinking_layout(avail, 0, 500, 200, 96, &out);
    ASSERT_EQ(out.body.h, 0);
    ASSERT_EQ(out.total_h, out.row.h);
    ASSERT_EQ(out.row.h, 28);
    TEST_END();
}

int test_thinking_layout_expanded_body_uses_text_h_under_cap(void)
{
    TEST_BEGIN();
    NsRect avail = { 0, 0, 400, 300 };
    ThinkingLayout out;
    thinking_layout(avail, 1, 50, 200, 96, &out);
    ASSERT_EQ(out.body.h, 50);
    ASSERT_EQ(out.total_h, out.row.h + 4 /* SP_XS */ + 50);
    TEST_END();
}

int test_thinking_layout_expanded_body_capped(void)
{
    TEST_BEGIN();
    NsRect avail = { 0, 0, 400, 300 };
    ThinkingLayout out;
    thinking_layout(avail, 1, 500, 200, 96, &out);
    ASSERT_EQ(out.body.h, 200);
    ASSERT_EQ(out.total_h, out.row.h + 4 + 200);
    TEST_END();
}

int test_thinking_layout_row_at_top_and_chevron_inset(void)
{
    TEST_BEGIN();
    NsRect avail = { 10, 20, 400, 300 };
    ThinkingLayout out;
    thinking_layout(avail, 0, 0, 200, 96, &out);
    ASSERT_EQ(out.row.x, avail.x);
    ASSERT_EQ(out.row.y, avail.y);
    ASSERT_EQ(out.row.w, avail.w);
    ASSERT_EQ(out.chevron.x, avail.x + 8 /* SP_SM */);
    ASSERT_TRUE(out.chevron.y >= out.row.y);
    ASSERT_TRUE(out.chevron.y + out.chevron.h <= out.row.y + out.row.h);
    ASSERT_TRUE(out.label.x > out.chevron.x + out.chevron.w);
    ASSERT_TRUE(out.summary.x > out.label.x);
    TEST_END();
}

int test_thinking_layout_all_rects_inside_avail_when_expanded(void)
{
    TEST_BEGIN();
    NsRect avail = { 5, 5, 400, 300 };
    ThinkingLayout out;
    thinking_layout(avail, 1, 50, 200, 96, &out);
    ASSERT_TRUE(rect_inside(out.row, avail));
    ASSERT_TRUE(rect_inside(out.chevron, avail));
    ASSERT_TRUE(rect_inside(out.label, avail));
    ASSERT_TRUE(rect_inside(out.summary, avail));
    ASSERT_TRUE(rect_inside(out.body, avail));
    TEST_END();
}

int test_thinking_layout_no_overlap_between_row_elements_and_body(void)
{
    TEST_BEGIN();
    NsRect avail = { 0, 0, 400, 300 };
    ThinkingLayout out;
    thinking_layout(avail, 1, 50, 200, 96, &out);
    ASSERT_TRUE(!rects_overlap(out.chevron, out.label));
    ASSERT_TRUE(!rects_overlap(out.label, out.summary));
    ASSERT_TRUE(!rects_overlap(out.row, out.body));
    ASSERT_TRUE(out.body.y >= out.row.y + out.row.h);
    TEST_END();
}

/* ---- ai_thinking_max_body_h ---------------------------------------------------
 * The expanded Thinking body's height cap: THINKING_MAX_LINES (50) lines of
 * the body font's own measured line height (maintainer request,
 * 2026-09-24: collapsed by default, but up to 50 lines and smart-scrollable
 * once opened). See ai_panel_layout.h. */

int test_ai_thinking_max_body_h_multiplies_by_50_lines(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_thinking_max_body_h(16), 800);   /* 16px line at 96 DPI */
    ASSERT_EQ(ai_thinking_max_body_h(1), 50);
    TEST_END();
}

int test_ai_thinking_max_body_h_scales_with_dpi(void)
{
    TEST_BEGIN();
    /* A caller measuring the same font role at a higher DPI gets a taller
     * line height (e.g. ns_font()/TEXTMETRIC at 2x DPI); the cap scales
     * with it exactly, same as thinking_layout()'s own DPI-scaled row/pad
     * values -- 50 lines always means 50 lines, at any DPI. */
    ASSERT_EQ(ai_thinking_max_body_h(32), 2 * ai_thinking_max_body_h(16));
    TEST_END();
}

int test_ai_thinking_max_body_h_zero_or_negative_line_height(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_thinking_max_body_h(0), 0);
    ASSERT_EQ(ai_thinking_max_body_h(-5), 0);
    TEST_END();
}

/* End-to-end: the real 50-line cap, fed through thinking_layout() exactly
 * as build_thinking_layout() (src/ui/chat_listview.c) does. */
int test_thinking_layout_with_50_line_cap_end_to_end(void)
{
    TEST_BEGIN();
    int line_h = 20;
    int max_h = ai_thinking_max_body_h(line_h);  /* 1000 */
    NsRect avail = { 0, 0, 400, 300 };
    ThinkingLayout out;

    /* Short content (5 lines) -> natural height, no clamp. */
    thinking_layout(avail, 1, line_h * 5, max_h, 96, &out);
    ASSERT_EQ(out.body.h, line_h * 5);

    /* Long content (80 lines) -> clamped at exactly 50 lines. */
    thinking_layout(avail, 1, line_h * 80, max_h, 96, &out);
    ASSERT_EQ(out.body.h, max_h);
    ASSERT_EQ(out.body.h, line_h * THINKING_MAX_LINES);
    TEST_END();
}

/* ---- ai_thinking_summary ------------------------------------------------------ */

int test_ai_thinking_summary_words_format(void)
{
    TEST_BEGIN();
    char buf[64];
    int len = ai_thinking_summary(240, 0, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "\xC2\xB7 240 words");
    ASSERT_EQ(len, (int)strlen(buf));
    TEST_END();
}

int test_ai_thinking_summary_streaming_format(void)
{
    TEST_BEGIN();
    char buf[64];
    int len = ai_thinking_summary(0, 1, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "\xC2\xB7 streaming\xE2\x80\xA6");
    ASSERT_EQ(len, (int)strlen(buf));
    TEST_END();
}

int test_ai_thinking_summary_truncates_safely(void)
{
    TEST_BEGIN();
    char buf[5];
    int len = ai_thinking_summary(999, 0, buf, sizeof buf);
    ASSERT_EQ(len, (int)strlen(buf));
    ASSERT_TRUE(len < (int)sizeof(buf));
    TEST_END();
}

/* ---- thinking_wheel_over_box -----------------------------------------------------
 * Root-cause bug (maintainer report, 2026-09-25): "I can't stop or scroll
 * the thinking window. Instead, I only control the AI assist panel." The
 * first fix here (input-box wheel handler swallowing WM_MOUSEWHEEL) was
 * real but not the actual cause on the maintainer's machine: its
 * MouseWheelRouting is 2 (route to the window under the cursor, the
 * Windows 10/11 default), so the wheel does reach the chat list. The
 * actual bug was this hit-test's old "box must be fully visible" rule: the
 * box is capped at THINKING_MAX_LINES (50 lines, ~800-1000px) regardless
 * of viewport size, so on a typical ~700-800px thread viewport it
 * routinely can't fit on screen at all once past ~40 lines of reasoning --
 * meaning it could *never* take the wheel, exactly matching "I only
 * control the AI assist panel". Renamed from thinking_wheel_hits_box to
 * thinking_wheel_over_box: it now tests the box's visible (viewport-
 * clipped) area rather than requiring the whole box on screen; see
 * thinking_wheel_should_chain below for the boundary/streaming logic that
 * rule used to (incorrectly) fold in here.
 * ------------------------------------------------------------------------ */

int test_thinking_wheel_over_box_cursor_inside_overflowing_box(void)
{
    TEST_BEGIN();
    NsRect body = { 20, 40, 300, 200 };
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 100, 100), 1);
    TEST_END();
}

int test_thinking_wheel_over_box_content_shorter_than_box_no_overflow(void)
{
    TEST_BEGIN();
    /* Content fits entirely inside the box -- nothing to scroll, so the
     * wheel must bubble to the outer list even with the cursor over it. */
    NsRect body = { 20, 40, 300, 200 };
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 200, 100, 100), 0);
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 150, 100, 100), 0);
    TEST_END();
}

int test_thinking_wheel_over_box_cursor_outside_body_rect(void)
{
    TEST_BEGIN();
    NsRect body = { 20, 40, 300, 200 };
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 10, 100), 0);   /* left of box */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 400, 100), 0);  /* right of box */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 100, 10), 0);   /* above box */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 100, 300), 0);  /* below box */
    TEST_END();
}

int test_thinking_wheel_over_box_cursor_at_edges_is_inclusive_low_exclusive_high(void)
{
    TEST_BEGIN();
    NsRect body = { 20, 40, 300, 200 };
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 20, 40), 1);    /* top-left corner: in */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 319, 239), 1);  /* last in-bounds pixel */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 320, 100), 0);  /* one past right edge */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 100, 240), 0);  /* one past bottom edge */
    TEST_END();
}

int test_thinking_wheel_over_box_partially_visible_at_bottom_still_hits_visible_part(void)
{
    TEST_BEGIN();
    /* A box taller than the viewport, cut off at the bottom -- the
     * fixed bug: this must still take the wheel over the part that IS on
     * screen, or a reply past ~40 lines of reasoning could never be
     * scrolled by wheel at all. */
    NsRect body = { 20, 500, 300, 400 };  /* body.y + h = 900, viewport 600 */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 1000, 100, 550), 1);  /* inside visible slice */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 1000, 100, 650), 0);  /* below viewport entirely */
    TEST_END();
}

int test_thinking_wheel_over_box_partially_visible_above_top_still_hits_visible_part(void)
{
    TEST_BEGIN();
    NsRect body = { 20, -300, 300, 400 };  /* scrolled mostly above the viewport top */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 1000, 100, 50), 1);   /* inside visible slice */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 1000, 100, -50), 0);  /* above viewport entirely */
    TEST_END();
}

int test_thinking_wheel_over_box_taller_than_viewport_50_lines(void)
{
    TEST_BEGIN();
    /* The concrete scenario from the bug report: a 50-line box (~1000px
     * at a 20px line height, capped and still overflowing -- reasoning
     * longer than 50 lines) in a ~700px thread viewport. The box itself
     * can never be fully on screen, but the middle of it is still visible
     * and must take the wheel there. */
    int line_h = 20;
    int max_h = ai_thinking_max_body_h(line_h);  /* 1000, the box's own cap */
    int full_content_h = max_h + 500;            /* reasoning overflows even the cap */
    NsRect body = { 20, 0, 300, max_h };
    ASSERT_EQ(thinking_wheel_over_box(body, 700, full_content_h, 100, 350), 1);
    TEST_END();
}

int test_thinking_wheel_over_box_collapsed_zero_size_never_hits(void)
{
    TEST_BEGIN();
    NsRect body = { 20, 40, 0, 0 };  /* collapsed: thinking_layout() zeroes body */
    ASSERT_EQ(thinking_wheel_over_box(body, 600, 500, 20, 40), 0);
    TEST_END();
}

/* ---- thinking_wheel_should_chain -------------------------------------------------
 * Decides, once the cursor is already known to be over the box, whether a
 * given notch should still bubble to the outer list because the box is at
 * the limit the wheel is pushing toward. */

int test_thinking_wheel_should_chain_scrolling_down_mid_content_stays_in_box(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_should_chain(100, 500, -NS_WHEEL_DELTA), 0);
    TEST_END();
}

int test_thinking_wheel_should_chain_scrolling_up_mid_content_stays_in_box(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_should_chain(100, 500, NS_WHEEL_DELTA), 0);
    TEST_END();
}

int test_thinking_wheel_should_chain_down_at_bottom_chains(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_should_chain(500, 500, -NS_WHEEL_DELTA), 1);
    TEST_END();
}

int test_thinking_wheel_should_chain_up_at_top_chains(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_should_chain(0, 500, NS_WHEEL_DELTA), 1);
    TEST_END();
}

int test_thinking_wheel_should_chain_down_not_yet_at_bottom_stays(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_should_chain(499, 500, -NS_WHEEL_DELTA), 0);
    TEST_END();
}

int test_thinking_wheel_should_chain_up_not_yet_at_top_stays(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_should_chain(1, 500, NS_WHEEL_DELTA), 0);
    TEST_END();
}

int test_thinking_wheel_should_chain_no_overflow_always_chains(void)
{
    TEST_BEGIN();
    /* max_scroll <= 0: nothing to scroll, so any direction chains --
     * covers both the "content fits" case and a not-yet-measured box. */
    ASSERT_EQ(thinking_wheel_should_chain(0, 0, -NS_WHEEL_DELTA), 1);
    ASSERT_EQ(thinking_wheel_should_chain(0, 0, NS_WHEEL_DELTA), 1);
    ASSERT_EQ(thinking_wheel_should_chain(0, -10, -NS_WHEEL_DELTA), 1);
    TEST_END();
}

int test_thinking_wheel_should_chain_streaming_auto_follow_at_max_chains_down(void)
{
    TEST_BEGIN();
    /* The scenario the old "fully visible" rule was guarding against: a
     * streaming, auto-following box sits at scroll_y == max_scroll almost
     * continuously (stick_scroll_on_layout keeps snapping it to the new
     * bottom as content grows). A wheel-down notch here must chain to the
     * outer list immediately so it can still reach bottom and re-engage
     * its own stick-to-bottom -- regardless of the box's on-screen size. */
    int max_scroll = 5000;  /* a long, still-growing reasoning stream */
    ASSERT_EQ(thinking_wheel_should_chain(max_scroll, max_scroll, -NS_WHEEL_DELTA), 1);
    /* Scrolling up (away from the streamed bottom) does NOT chain -- the
     * user is reading back through what already arrived. */
    ASSERT_EQ(thinking_wheel_should_chain(max_scroll, max_scroll, NS_WHEEL_DELTA), 0);
    TEST_END();
}

/* ---- thinking_wheel_scroll ------------------------------------------------------ */

int test_thinking_wheel_scroll_down_notch_moves_forward(void)
{
    TEST_BEGIN();
    /* Wheel toward the user (negative delta) scrolls content forward. */
    ASSERT_EQ(thinking_wheel_scroll(0, -NS_WHEEL_DELTA, 40, 500), 40);
    TEST_END();
}

int test_thinking_wheel_scroll_up_notch_moves_backward(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_scroll(100, NS_WHEEL_DELTA, 40, 500), 60);
    TEST_END();
}

int test_thinking_wheel_scroll_clamps_to_zero(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_scroll(10, NS_WHEEL_DELTA, 40, 500), 0);
    TEST_END();
}

int test_thinking_wheel_scroll_overshoot_clamps_to_max(void)
{
    TEST_BEGIN();
    /* A single huge/fast wheel delta must not overshoot past max_scroll. */
    ASSERT_EQ(thinking_wheel_scroll(0, -NS_WHEEL_DELTA * 50, 40, 500), 500);
    TEST_END();
}

int test_thinking_wheel_scroll_exactly_at_bottom_stays_at_max(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_scroll(500, -NS_WHEEL_DELTA, 40, 500), 500);
    TEST_END();
}

int test_thinking_wheel_scroll_negative_max_scroll_clamps_to_zero(void)
{
    TEST_BEGIN();
    ASSERT_EQ(thinking_wheel_scroll(0, -NS_WHEEL_DELTA, 40, -10), 0);
    TEST_END();
}

int test_thinking_wheel_scroll_touchpad_fractional_delta_rounds_to_no_movement(void)
{
    TEST_BEGIN();
    /* A high-resolution touchpad can report a delta far smaller than one
     * full WHEEL_DELTA notch (2026-09-25 review, item 3): 2*40/120 truncates
     * to 0px. thinking_wheel_scroll() itself just reports "no movement" --
     * it is the caller's job (see thinking_wheel_should_chain(), and
     * chat_listview.c's WM_MOUSEWHEEL handler) not to mistake that for "at
     * a boundary, let the outer list have it": should_chain() is evaluated
     * BEFORE this call and only looks at old_scroll vs. max_scroll, so a
     * mid-content notch that happens to move 0px is still consumed by the
     * box rather than leaking to the outer list. */
    ASSERT_EQ(thinking_wheel_scroll(100, -2, 40, 500), 100);
    ASSERT_EQ(thinking_wheel_should_chain(100, 500, -2), 0);
    TEST_END();
}

/* ---- ai_panel_states ------------------------------------------------------------ */

int test_ai_panel_state_titles_actions_and_suggestions_flag(void)
{
    TEST_BEGIN();
    const AiPanelState *empty = ai_panel_state(AI_STATE_EMPTY);
    const AiPanelState *no_key = ai_panel_state(AI_STATE_NO_KEY);
    const AiPanelState *no_session = ai_panel_state(AI_STATE_NO_SESSION);

    ASSERT_NOT_NULL(empty);
    ASSERT_NOT_NULL(no_key);
    ASSERT_NOT_NULL(no_session);

    ASSERT_STR_EQ(empty->title, "Ask about this session");
    ASSERT_NULL(empty->action_label);
    ASSERT_EQ(empty->has_suggestions, 1);

    ASSERT_STR_EQ(no_key->title, "Add an API key to use the assistant");
    ASSERT_STR_EQ(no_key->action_label, "Open Settings");
    ASSERT_EQ(no_key->has_suggestions, 0);

    ASSERT_STR_EQ(no_session->title, "Connect to a session first");
    ASSERT_STR_EQ(no_session->action_label, "Open Session Manager");
    ASSERT_EQ(no_session->has_suggestions, 0);
    TEST_END();
}

int test_ai_panel_state_null_for_bad_id(void)
{
    TEST_BEGIN();
    ASSERT_NULL(ai_panel_state((AiPanelStateId)-1));
    ASSERT_NULL(ai_panel_state(AI_STATE_COUNT));

    char buf[8] = "x";
    int len = ai_panel_state_body(AI_STATE_COUNT, 1000, buf, sizeof buf);
    ASSERT_EQ(len, 0);
    ASSERT_STR_EQ(buf, "");
    TEST_END();
}

int test_ai_panel_state_body_empty_substitutes_thousands(void)
{
    TEST_BEGIN();
    char buf[256];
    ai_panel_state_body(AI_STATE_EMPTY, 1000, buf, sizeof buf);
    ASSERT_STR_EQ(buf,
        "The assistant sees the last 1,000 lines of your terminal and can run commands with your approval.");

    ai_panel_state_body(AI_STATE_EMPTY, 500, buf, sizeof buf);
    ASSERT_STR_EQ(buf,
        "The assistant sees the last 500 lines of your terminal and can run commands with your approval.");

    ai_panel_state_body(AI_STATE_EMPTY, 1234567, buf, sizeof buf);
    ASSERT_STR_EQ(buf,
        "The assistant sees the last 1,234,567 lines of your terminal and can run commands with your approval.");
    TEST_END();
}

int test_ai_panel_state_body_other_states_ignore_context_lines(void)
{
    TEST_BEGIN();
    char a[256], b[256];
    ai_panel_state_body(AI_STATE_NO_KEY, 1, a, sizeof a);
    ai_panel_state_body(AI_STATE_NO_KEY, 999999, b, sizeof b);
    ASSERT_STR_EQ(a, b);
    ASSERT_STR_EQ(a, "Choose a provider and paste a key in Settings. Nothing is sent anywhere until you do.");

    ai_panel_state_body(AI_STATE_NO_SESSION, 1, a, sizeof a);
    ASSERT_STR_EQ(a, "The assistant works on a live terminal. Open the Session Manager to connect.");
    TEST_END();
}

int test_ai_panel_suggestions_exactly_three_nonempty(void)
{
    TEST_BEGIN();
    int count = -1;
    const char *const *sug = ai_panel_suggestions(&count);
    ASSERT_EQ(count, 3);
    ASSERT_NOT_NULL(sug);
    for (int i = 0; i < 3; i++) {
        ASSERT_NOT_NULL(sug[i]);
        ASSERT_TRUE(strlen(sug[i]) > 0);
    }
    ASSERT_STR_EQ(sug[0], "What's using disk?");
    ASSERT_STR_EQ(sug[1], "Why did that fail?");
    ASSERT_STR_EQ(sug[2], "Summarise the log");
    TEST_END();
}
