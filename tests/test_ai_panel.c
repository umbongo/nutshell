#include "test_framework.h"
#include "ai_panel_layout.h"
#include "ai_panel_states.h"
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
    ASSERT_EQ(out.header.h + out.thread.h + out.status.h + out.composer.h, panel.h);

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
    ASSERT_EQ(out.header.h + out.thread.h + out.status.h + out.composer.h, panel.h);
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

/* ---- ai_status_layout -------------------------------------------------------- */

/* A policy control of the width the four real stop labels come to at 96 DPI
 * in the caption font, near enough: four cells of label + 2*SP_SM. */
#define TEST_POLICY_W 220

static void collect_status_rects(const AiStatusLayout *l, NsRect out[3])
{
    out[0] = l->policy;
    out[1] = l->meter_bar;
    out[2] = l->meter_text;
}

static int status_no_overlap_and_inside(const AiStatusLayout *l, NsRect line)
{
    NsRect r[3];
    collect_status_rects(l, r);
    for (int i = 0; i < 3; i++) {
        if (!rect_inside(r[i], line)) return 0;
        for (int j = i + 1; j < 3; j++)
            if (rects_overlap(r[i], r[j])) return 0;
    }
    return 1;
}

int test_ai_status_layout_no_overlap_inside_line_96(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 600, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, TEST_POLICY_W, 60, &out);
    ASSERT_TRUE(status_no_overlap_and_inside(&out, status));
    TEST_END();
}

int test_ai_status_layout_no_overlap_inside_line_192(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 1200, 56 };
    AiStatusLayout out;
    ai_status_layout(status, 192, 2 * TEST_POLICY_W, 60, &out);
    ASSERT_TRUE(status_no_overlap_and_inside(&out, status));
    TEST_END();
}

/* The policy control is flush with the left edge of the line and vertically
 * centred in it, at the full width the caller measured -- it is never
 * squeezed, however narrow the line gets (the meter goes first). */
int test_ai_status_layout_policy_is_left_aligned_and_full_width(void)
{
    TEST_BEGIN();
    NsRect status = { 7, 11, 600, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, TEST_POLICY_W, 60, &out);
    ASSERT_EQ(out.policy.x, status.x);
    ASSERT_EQ(out.policy.w, TEST_POLICY_W);
    ASSERT_TRUE(out.policy.h > 0 && out.policy.h < status.h);
    ASSERT_EQ(out.policy.y - status.y, (status.h - out.policy.h) / 2);

    NsRect tiny = { 7, 11, 90, 28 };
    ai_status_layout(tiny, 96, TEST_POLICY_W, 60, &out);
    ASSERT_EQ(out.policy.w, TEST_POLICY_W);
    TEST_END();
}

int test_ai_status_layout_wide_line_shows_full_meter(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 600, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, TEST_POLICY_W, 60, &out);
    ASSERT_TRUE(out.meter_bar.w > 0 && out.meter_bar.h > 0);
    ASSERT_TRUE(out.meter_text.w > 0 && out.meter_text.h > 0);
    ASSERT_TRUE(out.meter_bar.x < out.meter_text.x);
    TEST_END();
}

int test_ai_status_layout_meter_bar_dropped_when_narrow(void)
{
    TEST_BEGIN();
    /* Narrow enough that the bar collides with the policy control, but wide
     * enough that the meter text alone still fits. */
    NsRect status = { 0, 0, 360, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, TEST_POLICY_W, 60, &out);
    ASSERT_EQ(out.meter_bar.w, 0);
    ASSERT_EQ(out.meter_bar.h, 0);
    ASSERT_TRUE(out.meter_text.w > 0);
    ASSERT_TRUE(status_no_overlap_and_inside(&out, status));
    TEST_END();
}

int test_ai_status_layout_meter_text_dropped_when_very_narrow(void)
{
    TEST_BEGIN();
    /* Narrow enough that even the meter text alone would collide with the
     * policy control (which is never squeezed). */
    NsRect status = { 0, 0, 290, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, TEST_POLICY_W, 60, &out);
    ASSERT_EQ(out.meter_bar.w, 0);
    ASSERT_EQ(out.meter_text.w, 0);
    ASSERT_TRUE(status_no_overlap_and_inside(&out, status));
    TEST_END();
}

int test_ai_status_layout_negative_widths_are_clamped(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 600, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, -40, -10, &out);
    ASSERT_EQ(out.policy.w, 0);
    ASSERT_EQ(out.meter_text.w, 0);
    ASSERT_EQ(out.meter_bar.w, 0);
    ai_status_layout(status, 96, TEST_POLICY_W, 60, NULL);  /* must not crash */
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
