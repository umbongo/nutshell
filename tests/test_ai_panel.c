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

static void collect_status_rects(const AiStatusLayout *l, NsRect out[5])
{
    out[0] = l->seg[0];
    out[1] = l->seg[1];
    out[2] = l->auto_label;
    out[3] = l->meter_bar;
    out[4] = l->meter_text;
}

static int status_no_overlap_and_inside(const AiStatusLayout *l, NsRect line)
{
    NsRect r[5];
    collect_status_rects(l, r);
    for (int i = 0; i < 5; i++) {
        if (!rect_inside(r[i], line)) return 0;
        for (int j = i + 1; j < 5; j++)
            if (rects_overlap(r[i], r[j])) return 0;
    }
    return 1;
}

int test_ai_status_layout_no_overlap_inside_line_96(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 600, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, 50, 70, 90, 60, &out);
    ASSERT_TRUE(status_no_overlap_and_inside(&out, status));
    TEST_END();
}

int test_ai_status_layout_no_overlap_inside_line_192(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 1200, 56 };
    AiStatusLayout out;
    ai_status_layout(status, 192, 50, 70, 90, 60, &out);
    ASSERT_TRUE(status_no_overlap_and_inside(&out, status));
    TEST_END();
}

int test_ai_status_layout_segments_touch(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 600, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, 50, 70, 90, 60, &out);
    ASSERT_EQ(out.seg[1].x, out.seg[0].x + out.seg[0].w);
    ASSERT_EQ(out.seg[0].y, out.seg[1].y);
    ASSERT_EQ(out.seg[0].h, out.seg[1].h);
    TEST_END();
}

int test_ai_status_layout_wide_line_shows_full_meter(void)
{
    TEST_BEGIN();
    NsRect status = { 0, 0, 600, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, 50, 70, 90, 60, &out);
    ASSERT_TRUE(out.meter_bar.w > 0 && out.meter_bar.h > 0);
    ASSERT_TRUE(out.meter_text.w > 0 && out.meter_text.h > 0);
    ASSERT_TRUE(out.meter_bar.x < out.meter_text.x);
    TEST_END();
}

int test_ai_status_layout_meter_bar_dropped_when_narrow(void)
{
    TEST_BEGIN();
    /* Narrow enough that the bar collides with the segments/auto label,
     * but wide enough that the meter text alone still fits. */
    NsRect status = { 0, 0, 350, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, 50, 70, 90, 60, &out);
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
     * segments/auto label (which are never squeezed), but still wide
     * enough that the segments and auto label themselves fit. */
    NsRect status = { 0, 0, 280, 28 };
    AiStatusLayout out;
    ai_status_layout(status, 96, 50, 70, 90, 60, &out);
    ASSERT_EQ(out.meter_bar.w, 0);
    ASSERT_EQ(out.meter_text.w, 0);
    ASSERT_TRUE(status_no_overlap_and_inside(&out, status));
    TEST_END();
}

/* ---- ai_modes_label / ai_permit_label ---------------------------------------- */

int test_ai_modes_label_off_safe_and_all(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(ai_modes_label(0, 0), "off");
    ASSERT_STR_EQ(ai_modes_label(0, 1), "off");
    ASSERT_STR_EQ(ai_modes_label(0, 2), "off");
    ASSERT_STR_EQ(ai_modes_label(1, 0), "safe only");
    ASSERT_STR_EQ(ai_modes_label(1, 1), "safe + write");
    ASSERT_STR_EQ(ai_modes_label(1, 2), "all");
    /* Out-of-range levels clamp rather than misbehave. */
    ASSERT_STR_EQ(ai_modes_label(1, -1), "safe only");
    ASSERT_STR_EQ(ai_modes_label(1, 3), "all");
    TEST_END();
}

int test_ai_permit_label_off_and_on(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(ai_permit_label(0), "off");
    ASSERT_STR_EQ(ai_permit_label(1), "on");
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
