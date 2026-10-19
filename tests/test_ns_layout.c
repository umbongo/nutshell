#include "test_framework.h"
#include "ns_layout.h"
#include "chat_approval.h"
#include "cmd_policy.h"
#include "ns_scale.h"
#include "ns_type.h"

/* ===========================================================================
 * ns_layout tests (Design-System Foundation, task 5; approval card v2 per
 * the AI Assist Panel plan, task 1) -- see
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 3 and docs/superpowers/specs/2026-09-07-ai-assist-panel-design.md
 * "Approval card (card 1)".
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
    if (inner.w <= 0 || inner.h <= 0) return 1;   /* zero-size: trivially fine */
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

static NsRect rect_center(NsRect r)
{
    NsRect c = { r.x + r.w / 2, r.y + r.h / 2, 0, 0 };
    return c;
}

/* ---- ns_button_layout ---------------------------------------------------- */

int test_ns_button_layout_with_icon_no_overlap_96(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 20, 120, 28 };
    NsButtonLayout out;
    ns_button_layout(r, 1, 96, &out);
    ASSERT_TRUE(!rects_overlap(out.icon, out.label));
    ASSERT_TRUE(rect_inside(out.icon, r));
    ASSERT_TRUE(rect_inside(out.label, r));
    ASSERT_TRUE(out.icon.w > 0 && out.icon.h > 0);
    ASSERT_TRUE(out.label.w > 0 && out.label.h > 0);
    TEST_END();
}

int test_ns_button_layout_with_icon_no_overlap_192(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 240, 56 };
    NsButtonLayout out;
    ns_button_layout(r, 1, 192, &out);
    ASSERT_TRUE(!rects_overlap(out.icon, out.label));
    ASSERT_TRUE(rect_inside(out.icon, r));
    ASSERT_TRUE(rect_inside(out.label, r));
    TEST_END();
}

int test_ns_button_layout_no_icon_zero_icon(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 100, 28 };
    NsButtonLayout out;
    ns_button_layout(r, 0, 96, &out);
    ASSERT_EQ(out.icon.w, 0);
    ASSERT_EQ(out.icon.h, 0);
    ASSERT_TRUE(out.label.w > 0);
    ASSERT_TRUE(rect_inside(out.label, r));
    TEST_END();
}

/* ---- ns_card_layout -------------------------------------------------------- */

int test_ns_card_layout_no_overlap_inside_parent_96(void)
{
    TEST_BEGIN();
    NsRect r = { 5, 5, 300, 200 };
    NsCardLayout out;
    ns_card_layout(r, 96, &out);
    ASSERT_TRUE(rect_inside(out.inset, r));
    ASSERT_TRUE(rect_inside(out.header, out.inset));
    ASSERT_TRUE(rect_inside(out.body, out.inset));
    ASSERT_TRUE(!rects_overlap(out.header, out.body));
    TEST_END();
}

int test_ns_card_layout_no_overlap_inside_parent_192(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 600, 400 };
    NsCardLayout out;
    ns_card_layout(r, 192, &out);
    ASSERT_TRUE(rect_inside(out.inset, r));
    ASSERT_TRUE(rect_inside(out.header, out.inset));
    ASSERT_TRUE(rect_inside(out.body, out.inset));
    ASSERT_TRUE(!rects_overlap(out.header, out.body));
    TEST_END();
}

int test_ns_card_layout_tiny_rect_no_crash(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 2, 2 };
    NsCardLayout out;
    ns_card_layout(r, 96, &out);
    ASSERT_TRUE(out.body.w >= 0 && out.body.h >= 0);
    ASSERT_TRUE(out.header.w >= 0 && out.header.h >= 0);
    TEST_END();
}

/* ---- approval_card_layout: empty card -------------------------------------- */

int test_approval_card_layout_zero_commands_hides_everything(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 500, 300 };
    ApprovalCardLayout out;
    approval_card_layout(r, 0, NULL, NULL, NULL, 16, 96, &out);
    ASSERT_EQ(out.n_rows, 0);
    ASSERT_EQ(out.header.w, 0);
    ASSERT_EQ(out.header.h, 0);
    ASSERT_EQ(out.deny_all.w, 0);
    ASSERT_EQ(out.deny_all.h, 0);
    ASSERT_EQ(out.run_selected.w, 0);
    ASSERT_EQ(out.run_selected.h, 0);
    ASSERT_EQ(out.run_enabled, 0);
    TEST_END();
}

int test_approval_card_layout_negative_commands_no_crash(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 500, 300 };
    ApprovalCardLayout out;
    approval_card_layout(r, -3, NULL, NULL, NULL, 16, 96, &out);
    ASSERT_EQ(out.n_rows, 0);
    TEST_END();
}

/* ---- approval_card_layout: normal rows ------------------------------------- */

static NsRect big_card_rect(void)
{
    NsRect r = { 0, 0, 600, 500 };
    return r;
}

int test_approval_card_layout_rows_no_overlap_inside_parent_96(void)
{
    TEST_BEGIN();
    NsRect r = big_card_rect();
    int widths[3] = { 40, 300, 90 };
    int checked[3] = { 1, 1, 1 };
    ApprovalCardLayout out;
    approval_card_layout(r, 3, widths, checked, NULL, 16, 96, &out);

    ASSERT_EQ(out.n_rows, 3);
    ASSERT_TRUE(rect_inside(out.header, r));
    for (int i = 0; i < 3; i++) {
        const ApprovalRowLayout *row = &out.rows[i];
        ASSERT_TRUE(rect_inside(row->checkbox, r));
        ASSERT_TRUE(rect_inside(row->text, r));
        ASSERT_TRUE(rect_inside(row->tag, r));

        ASSERT_TRUE(!rects_overlap(row->checkbox, row->text));
        ASSERT_TRUE(!rects_overlap(row->text, row->tag));
        ASSERT_TRUE(!rects_overlap(out.header, row->checkbox));
        ASSERT_TRUE(!rects_overlap(out.header, row->text));
        ASSERT_TRUE(!rects_overlap(out.header, row->tag));

        for (int j = 0; j < 3; j++) {
            if (j == i) continue;
            ASSERT_TRUE(!rects_overlap(row->tag, out.rows[j].tag));
            ASSERT_TRUE(!rects_overlap(row->text, out.rows[j].text));
        }
    }
    ASSERT_TRUE(rect_inside(out.deny_all, r));
    ASSERT_TRUE(rect_inside(out.run_selected, r));
    ASSERT_TRUE(!rects_overlap(out.deny_all, out.run_selected));
    TEST_END();
}

int test_approval_card_layout_rows_no_overlap_inside_parent_192(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 1000, 900 };
    int widths[3] = { 40, 300, 90 };
    ApprovalCardLayout out;
    approval_card_layout(r, 3, widths, NULL, NULL, 20, 192, &out);

    ASSERT_EQ(out.n_rows, 3);
    for (int i = 0; i < 3; i++) {
        const ApprovalRowLayout *row = &out.rows[i];
        ASSERT_TRUE(rect_inside(row->checkbox, r));
        ASSERT_TRUE(rect_inside(row->text, r));
        ASSERT_TRUE(rect_inside(row->tag, r));
    }
    ASSERT_TRUE(rect_inside(out.deny_all, r));
    ASSERT_TRUE(rect_inside(out.run_selected, r));
    TEST_END();
}

int test_approval_card_layout_action_row_does_not_overlap_rows(void)
{
    TEST_BEGIN();
    NsRect r = big_card_rect();
    int widths[5] = { 40, 300, 90, 120, 60 };
    ApprovalCardLayout out;
    approval_card_layout(r, 5, widths, NULL, NULL, 16, 96, &out);
    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(!rects_overlap(out.rows[i].checkbox, out.deny_all));
        ASSERT_TRUE(!rects_overlap(out.rows[i].text, out.deny_all));
        ASSERT_TRUE(!rects_overlap(out.rows[i].tag, out.deny_all));
        ASSERT_TRUE(!rects_overlap(out.rows[i].checkbox, out.run_selected));
        ASSERT_TRUE(!rects_overlap(out.rows[i].text, out.run_selected));
        ASSERT_TRUE(!rects_overlap(out.rows[i].tag, out.run_selected));
    }
    TEST_END();
}

/* ---- approval_card_layout: header above rows -------------------------------- */

int test_approval_card_layout_header_above_rows(void)
{
    TEST_BEGIN();
    NsRect r = big_card_rect();
    int widths[2] = { 40, 300 };
    ApprovalCardLayout out;
    approval_card_layout(r, 2, widths, NULL, NULL, 16, 96, &out);

    ASSERT_TRUE(out.header.h > 0);
    ASSERT_TRUE(out.header.y < out.rows[0].checkbox.y);
    ASSERT_TRUE(out.header.y + out.header.h <= out.rows[0].checkbox.y);
    ASSERT_TRUE(!rects_overlap(out.header, out.rows[0].checkbox));
    ASSERT_TRUE(!rects_overlap(out.header, out.rows[0].text));
    ASSERT_TRUE(!rects_overlap(out.header, out.rows[0].tag));
    TEST_END();
}

/* ---- approval_card_layout: scrolling ---------------------------------------- */

int test_approval_card_layout_16_commands_scrollable(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 600, 800 };
    int widths[16];
    for (int i = 0; i < 16; i++) widths[i] = 50 + i;
    ApprovalCardLayout out;
    approval_card_layout(r, 16, widths, NULL, NULL, 16, 96, &out);

    ASSERT_EQ(out.n_rows, 16);
    ASSERT_TRUE(out.scrollable);
    ASSERT_TRUE(rect_inside(out.viewport, r));

    for (int i = 0; i < APPROVAL_VISIBLE_MAX; i++) {
        ASSERT_TRUE(out.rows[i].tag.w > 0);
        ASSERT_TRUE(out.rows[i].tag.h > 0);
    }
    for (int i = APPROVAL_VISIBLE_MAX; i < 16; i++) {
        ASSERT_EQ(out.rows[i].tag.w, 0);
        ASSERT_EQ(out.rows[i].text.w, 0);
        ASSERT_EQ(out.rows[i].checkbox.w, 0);
    }
    TEST_END();
}

int test_approval_card_layout_exactly_visible_max_not_scrollable(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 600, 500 };
    int widths[APPROVAL_VISIBLE_MAX];
    for (int i = 0; i < APPROVAL_VISIBLE_MAX; i++) widths[i] = 40;
    ApprovalCardLayout out;
    approval_card_layout(r, APPROVAL_VISIBLE_MAX, widths, NULL, NULL, 16, 96, &out);
    ASSERT_FALSE(out.scrollable);
    for (int i = 0; i < APPROVAL_VISIBLE_MAX; i++)
        ASSERT_TRUE(out.rows[i].tag.w > 0);
    TEST_END();
}

/* ---- approval_card_layout: ellipsis ------------------------------------------ */

int test_approval_card_layout_ellipsis_when_text_too_wide(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 400, 300 };
    int widths[1] = { 100000 };  /* far wider than any text box could be */
    ApprovalCardLayout out;
    approval_card_layout(r, 1, widths, NULL, NULL, 16, 96, &out);
    ASSERT_TRUE(out.rows[0].text.w > 0);
    ASSERT_EQ(out.rows[0].ellipsis, 1);
    TEST_END();
}

int test_approval_card_layout_no_ellipsis_when_text_fits(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 400, 300 };
    int widths[1] = { 4 };  /* trivially narrow */
    ApprovalCardLayout out;
    approval_card_layout(r, 1, widths, NULL, NULL, 16, 96, &out);
    ASSERT_EQ(out.rows[0].ellipsis, 0);
    TEST_END();
}

int test_approval_card_layout_ellipsis_boundary_exact_fit(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 400, 300 };
    ApprovalCardLayout probe;
    approval_card_layout(r, 1, NULL, NULL, NULL, 16, 96, &probe);
    int exact = probe.rows[0].text.w;

    int widths_fit[1]     = { exact };
    int widths_overflow[1] = { exact + 1 };
    ApprovalCardLayout a, b;
    approval_card_layout(r, 1, widths_fit, NULL, NULL, 16, 96, &a);
    approval_card_layout(r, 1, widths_overflow, NULL, NULL, 16, 96, &b);
    ASSERT_EQ(a.rows[0].ellipsis, 0);
    ASSERT_EQ(b.rows[0].ellipsis, 1);
    TEST_END();
}

/* ---- approval_card_layout: run_enabled truth table --------------------------- */

int test_approval_card_layout_run_enabled_truth_table(void)
{
    TEST_BEGIN();
    NsRect r = big_card_rect();
    int widths[3] = { 40, 40, 40 };
    ApprovalCardLayout out;

    /* Nothing checked -> disabled. */
    int none_checked[3] = { 0, 0, 0 };
    approval_card_layout(r, 3, widths, none_checked, NULL, 16, 96, &out);
    ASSERT_EQ(out.run_enabled, 0);

    /* One checked, nothing held -> enabled. */
    int one_checked[3] = { 0, 1, 0 };
    approval_card_layout(r, 3, widths, one_checked, NULL, 16, 96, &out);
    ASSERT_EQ(out.run_enabled, 1);

    /* Checked but that same row is held -> disabled. */
    int checked_and_held[3] = { 0, 1, 0 };
    int held_same[3]        = { 0, 1, 0 };
    approval_card_layout(r, 3, widths, checked_and_held, held_same, 16, 96, &out);
    ASSERT_EQ(out.run_enabled, 0);

    /* All held, all checked -> disabled. */
    int all_checked[3] = { 1, 1, 1 };
    int all_held[3]    = { 1, 1, 1 };
    approval_card_layout(r, 3, widths, all_checked, all_held, 16, 96, &out);
    ASSERT_EQ(out.run_enabled, 0);

    /* One checked-and-not-held among held rows -> enabled. */
    int mixed_checked[3] = { 1, 1, 1 };
    int mixed_held[3]    = { 1, 0, 1 };
    approval_card_layout(r, 3, widths, mixed_checked, mixed_held, 16, 96, &out);
    ASSERT_EQ(out.run_enabled, 1);

    /* NULL checked -> disabled regardless of held. */
    approval_card_layout(r, 3, widths, NULL, all_held, 16, 96, &out);
    ASSERT_EQ(out.run_enabled, 0);

    TEST_END();
}

int test_approval_card_layout_held_row_flag(void)
{
    TEST_BEGIN();
    NsRect r = big_card_rect();
    int widths[2] = { 40, 40 };
    int held[2] = { 0, 1 };
    ApprovalCardLayout out;
    approval_card_layout(r, 2, widths, NULL, held, 16, 96, &out);
    ASSERT_EQ(out.rows[0].held, 0);
    ASSERT_EQ(out.rows[1].held, 1);
    TEST_END();
}

/* ---- approval_card_hit ------------------------------------------------------- */

int test_approval_card_hit_round_trip_all_row_elements(void)
{
    TEST_BEGIN();
    NsRect r = big_card_rect();
    int widths[4] = { 40, 300, 90, 20 };
    ApprovalCardLayout out;
    approval_card_layout(r, 4, widths, NULL, NULL, 16, 96, &out);

    for (int i = 0; i < 4; i++) {
        const ApprovalRowLayout *row = &out.rows[i];
        int row_out;
        NsRect c;

        c = rect_center(row->checkbox);
        ASSERT_EQ(approval_card_hit(&out, c.x, c.y, &row_out), HIT_CHECKBOX);
        ASSERT_EQ(row_out, i);

        c = rect_center(row->text);
        ASSERT_EQ(approval_card_hit(&out, c.x, c.y, &row_out), HIT_TEXT);
        ASSERT_EQ(row_out, i);

        c = rect_center(row->tag);
        ASSERT_EQ(approval_card_hit(&out, c.x, c.y, &row_out), HIT_TAG);
        ASSERT_EQ(row_out, i);
    }
    TEST_END();
}

int test_approval_card_hit_round_trip_actions(void)
{
    TEST_BEGIN();
    NsRect r = big_card_rect();
    int widths[2] = { 40, 300 };
    ApprovalCardLayout out;
    approval_card_layout(r, 2, widths, NULL, NULL, 16, 96, &out);

    int row_out = 123;
    NsRect c = rect_center(out.header);
    ASSERT_EQ(approval_card_hit(&out, c.x, c.y, &row_out), HIT_HEADER);
    ASSERT_EQ(row_out, -1);

    row_out = 123;
    c = rect_center(out.deny_all);
    ASSERT_EQ(approval_card_hit(&out, c.x, c.y, &row_out), HIT_DENY_ALL);
    ASSERT_EQ(row_out, -1);

    row_out = 123;
    c = rect_center(out.run_selected);
    ASSERT_EQ(approval_card_hit(&out, c.x, c.y, &row_out), HIT_RUN_SELECTED);
    ASSERT_EQ(row_out, -1);
    TEST_END();
}

int test_approval_card_hit_miss_returns_none(void)
{
    TEST_BEGIN();
    NsRect r = big_card_rect();
    int widths[2] = { 40, 300 };
    ApprovalCardLayout out;
    approval_card_layout(r, 2, widths, NULL, NULL, 16, 96, &out);

    int row_out = 42;
    ASSERT_EQ(approval_card_hit(&out, -1000, -1000, &row_out), HIT_NONE);
    ASSERT_EQ(row_out, -1);
    TEST_END();
}

int test_approval_card_hit_null_layout_safe(void)
{
    TEST_BEGIN();
    int row_out = 7;
    ASSERT_EQ(approval_card_hit(NULL, 10, 10, &row_out), HIT_NONE);
    ASSERT_EQ(row_out, -1);
    ASSERT_EQ(approval_card_hit(NULL, 10, 10, NULL), HIT_NONE);
    TEST_END();
}

int test_approval_card_hit_scrolled_out_rows_not_hit(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 600, 800 };
    int widths[16];
    for (int i = 0; i < 16; i++) widths[i] = 50;
    ApprovalCardLayout out;
    approval_card_layout(r, 16, widths, NULL, NULL, 16, 96, &out);

    /* Rows past the visible window are zero-size, so their "would be"
     * position (row 9's tag, had it been sized) never registers a hit;
     * probing the coordinates where a zero-size rect's corner sits must
     * not match anything. */
    for (int i = APPROVAL_VISIBLE_MAX; i < 16; i++) {
        int row_out;
        int hit = approval_card_hit(&out, out.rows[i].tag.x, out.rows[i].tag.y, &row_out);
        ASSERT_TRUE(hit == HIT_NONE || row_out != i);
    }
    TEST_END();
}

/* ---- v2: rows stay single-line and keep a real text box in a narrow card ---- */

int test_approval_card_layout_narrow_card_keeps_text_width(void)
{
    TEST_BEGIN();
    /* At 192 DPI a 560 px card is narrow, but with per-row Allow/Deny gone
     * the row never wraps: text keeps at least NS_TAG_W*2 (scaled) width. */
    NsRect r = { 0, 0, 560, 800 };
    int widths[3] = { 300, 300, 300 };
    ApprovalCardLayout l;
    approval_card_layout(r, 3, widths, NULL, NULL, 24, 192, &l);

    int tag_w = l.rows[0].tag.w;
    ASSERT_TRUE(tag_w > 0);
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(l.rows[i].text.w >= tag_w * 2);
        ASSERT_TRUE(!rects_overlap(l.rows[i].checkbox, l.rows[i].text));
        ASSERT_TRUE(!rects_overlap(l.rows[i].text, l.rows[i].tag));
        /* single-line: checkbox, text and tag all share one row height */
        ASSERT_TRUE(l.rows[i].checkbox.y + l.rows[i].checkbox.h <= l.rows[i].text.y + l.rows[i].text.h);
    }
    TEST_END();
}

int test_approval_row_height_matches_layout(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 560, 800 };
    int widths[2] = { 10, 10 };
    ApprovalCardLayout l;
    approval_card_layout(r, 2, widths, NULL, NULL, 24, 192, &l);
    int row_h = l.rows[1].checkbox.y - l.rows[0].checkbox.y;
    ASSERT_EQ(approval_row_height(24, 192), row_h);

    NsRect wide = { 0, 0, 1600, 800 };
    approval_card_layout(wide, 2, widths, NULL, NULL, 24, 192, &l);
    ASSERT_EQ(approval_row_height(24, 192), l.rows[1].checkbox.y - l.rows[0].checkbox.y);
    TEST_END();
}

/* ---- settled_row_layout: settled command inline rows ------------------- */

int test_settled_row_layout_chip_right_aligned(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 20, 400, 100 };
    ApprovalRowLayout out;
    settled_row_layout(r, 50, 40, 16, 96, &out);
    ASSERT_EQ(out.tag.x + out.tag.w, r.x + r.w);
    ASSERT_EQ(out.tag.w, 40);
    TEST_END();
}

int test_settled_row_layout_text_never_overlaps_chip(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 400, 100 };
    ApprovalRowLayout out;
    settled_row_layout(r, 5000, 40, 16, 96, &out);
    ASSERT_TRUE(out.text.x + out.text.w <= out.tag.x);
    TEST_END();
}

int test_settled_row_layout_ellipsis_only_when_too_wide(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 400, 100 };
    ApprovalRowLayout fits, overflow;
    settled_row_layout(r, 4, 40, 16, 96, &fits);
    ASSERT_EQ(fits.ellipsis, 0);
    settled_row_layout(r, 100000, 40, 16, 96, &overflow);
    ASSERT_EQ(overflow.ellipsis, 1);
    TEST_END();
}

int test_settled_row_layout_zero_chip_w_gives_text_full_width(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 400, 100 };
    ApprovalRowLayout out;
    settled_row_layout(r, 50, 0, 16, 96, &out);
    /* 2026-10-13: the copy icon now sits at the right edge even with no chip,
     * so the text gets the row minus the icon and its SP_XS gap (it used to
     * get the full row width). */
    ASSERT_EQ(out.text.w, r.w - ns_scale(SZ_ICON, 96) - ns_scale(SP_XS, 96));
    ASSERT_EQ(out.copy.x + out.copy.w, r.x + r.w);
    ASSERT_EQ(out.tag.w, 0);
    /* A row too narrow for any text next to the icon drops the icon and the
     * text takes the full width, as before. */
    NsRect tiny = { 0, 0, 30, 100 };
    settled_row_layout(tiny, 50, 0, 16, 96, &out);
    ASSERT_EQ(out.copy.w, 0);
    ASSERT_EQ(out.text.w, tiny.w);
    TEST_END();
}

int test_settled_row_layout_row_height_matches_approval_row_height(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 400, 100 };
    ApprovalRowLayout out;
    settled_row_layout(r, 50, 40, 16, 96, &out);
    ASSERT_EQ(out.text.h, approval_row_height(16, 96));

    NsRect r2 = { 0, 0, 400, 100 };
    ApprovalRowLayout out2;
    settled_row_layout(r2, 50, 40, 24, 192, &out2);
    ASSERT_EQ(out2.text.h, approval_row_height(24, 192));
    TEST_END();
}

int test_settled_row_layout_checkbox_and_held_zeroed(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 400, 100 };
    ApprovalRowLayout out;
    settled_row_layout(r, 50, 40, 16, 96, &out);
    ASSERT_EQ(out.checkbox.x, 0);
    ASSERT_EQ(out.checkbox.y, 0);
    ASSERT_EQ(out.checkbox.w, 0);
    ASSERT_EQ(out.checkbox.h, 0);
    ASSERT_EQ(out.held, 0);
    TEST_END();
}

int test_settled_row_layout_text_starts_at_rect_left(void)
{
    TEST_BEGIN();
    NsRect r = { 25, 30, 400, 100 };
    ApprovalRowLayout out;
    settled_row_layout(r, 50, 40, 16, 96, &out);
    ASSERT_EQ(out.text.x, r.x);
    TEST_END();
}

/* ---- copy icon (2026-10-13 command details / copy design) ---------------- */

int test_hit_copy_appended_without_renumbering(void)
{
    TEST_BEGIN();
    ASSERT_EQ(HIT_NONE, 0);
    ASSERT_EQ(HIT_POLICY_ALLOWED, 7);
    ASSERT_EQ(HIT_POLICY_UNATTENDED, 8);
    ASSERT_EQ(HIT_COPY, 9);
    /* 2026-10-18: the Auto-approve button's id is appended after HIT_COPY;
     * HIT_POLICY_UNATTENDED keeps its value and is never produced. */
    ASSERT_EQ(HIT_POLICY_AUTO, 10);
    TEST_END();
}

/* Returns 1 if every check holds (the helper has no TEST_BEGIN of its own). */
#define CK(cond) do { if (!(cond)) { printf("  Assertion failed: %s\n", #cond); return 0; } } while (0)

static int check_copy_placement(int dpi, int text_h)
{
    NsRect r = { 0, 0, 700, 500 };
    int widths[3] = { 40, 300, 9000 };
    ApprovalCardLayout out;
    approval_card_layout(r, 3, widths, NULL, NULL, text_h, dpi, &out);
    int xs = ns_scale(SP_XS, dpi);
    int icon = ns_scale(SZ_ICON, dpi);
    for (int i = 0; i < 3; i++) {
        const ApprovalRowLayout *row = &out.rows[i];
        CK((row->copy.w) == (icon));
        CK((row->copy.h) == (icon));
        /* SP_XS on either side, between text and tag. */
        CK((row->copy.x - xs) == (row->text.x + row->text.w));
        CK((row->copy.x + row->copy.w + xs) == (row->tag.x));
        /* Vertically centred like the checkbox (same size, same y). */
        CK((row->copy.y) == (row->checkbox.y));
        CK(rect_inside(row->copy, r));
        CK(!rects_overlap(row->copy, row->text));
        CK(!rects_overlap(row->copy, row->tag));
        CK(!rects_overlap(row->copy, row->checkbox));
        CK(!rects_overlap(row->copy, out.header));
        CK(!rects_overlap(row->copy, out.deny_all));
        CK(!rects_overlap(row->copy, out.run_selected));
    }
    /* Ellipsis follows the shrunk text rect. */
    CK((out.rows[0].ellipsis) == (0));
    CK((out.rows[2].ellipsis) == (1));
    return 1;
}
#undef CK

int test_approval_card_layout_copy_rect_placement_96(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(check_copy_placement(96, 16));
    /* Exact numbers at 96 DPI: card 700 wide, pad 12 -> area 676; checkbox
     * 20 + gap 8 -> text.x 28; tag 64 at x 624; copy 20 at x 600; text ends
     * at 596 (SP_XS gap). Pinned so an accidental grid change is noticed. */
    NsRect r = { 0, 0, 700, 500 };
    ApprovalCardLayout out;
    approval_card_layout(r, 1, NULL, NULL, NULL, 16, 96, &out);
    ASSERT_EQ(out.rows[0].tag.x, 12 + 676 - 64);
    ASSERT_EQ(out.rows[0].copy.x, 12 + 676 - 64 - 4 - 20);
    ASSERT_EQ(out.rows[0].text.x + out.rows[0].text.w, out.rows[0].copy.x - 4);
    TEST_END();
}

int test_approval_card_layout_copy_rect_placement_192(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(check_copy_placement(192, 24));
    TEST_END();
}

int test_approval_card_layout_copy_shrinks_text_by_icon_and_gaps(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 700, 500 };
    ApprovalCardLayout out;
    approval_card_layout(r, 1, NULL, NULL, NULL, 16, 96, &out);
    const ApprovalRowLayout *row = &out.rows[0];
    /* text + gap + copy + gap + tag exactly tile checkbox-right to tag-right */
    int left = row->checkbox.x + row->checkbox.w + ns_scale(SP_SM, 96);
    ASSERT_EQ(row->text.x, left);
    ASSERT_EQ(row->text.w,
              row->tag.x - ns_scale(SP_XS, 96) * 2 - row->copy.w - left);
    TEST_END();
}

int test_approval_card_hit_round_trip_copy(void)
{
    TEST_BEGIN();
    NsRect r = big_card_rect();
    int widths[4] = { 40, 300, 90, 20 };
    ApprovalCardLayout out;
    approval_card_layout(r, 4, widths, NULL, NULL, 16, 96, &out);
    for (int i = 0; i < 4; i++) {
        int row_out = -2;
        NsRect c = rect_center(out.rows[i].copy);
        ASSERT_EQ(approval_card_hit(&out, c.x, c.y, &row_out), HIT_COPY);
        ASSERT_EQ(row_out, i);
        /* All four corners of the rect hit it, one pixel outside does not. */
        const NsRect *cr = &out.rows[i].copy;
        ASSERT_EQ(approval_card_hit(&out, cr->x, cr->y, NULL), HIT_COPY);
        ASSERT_EQ(approval_card_hit(&out, cr->x + cr->w - 1,
                                    cr->y + cr->h - 1, NULL), HIT_COPY);
        ASSERT_TRUE(approval_card_hit(&out, cr->x + cr->w, c.y, NULL) != HIT_COPY);
        ASSERT_TRUE(approval_card_hit(&out, cr->x - 1, c.y, NULL) != HIT_COPY);
        /* The text and tag still hit as before, next to it. */
        NsRect t = rect_center(out.rows[i].text);
        ASSERT_EQ(approval_card_hit(&out, t.x, t.y, NULL), HIT_TEXT);
        NsRect g = rect_center(out.rows[i].tag);
        ASSERT_EQ(approval_card_hit(&out, g.x, g.y, NULL), HIT_TAG);
    }
    TEST_END();
}

int test_approval_card_layout_copy_dropped_first_on_narrow_row(void)
{
    TEST_BEGIN();
    /* 96 DPI, pad 12: area.w = r.w - 24. text.w with the copy icon =
     * area.w - 120, so the icon survives while that is >= SZ_ICON (20):
     * r.w >= 164. */
    ApprovalCardLayout keep, drop;
    NsRect rk = { 0, 0, 164, 400 };
    NsRect rd = { 0, 0, 163, 400 };
    approval_card_layout(rk, 1, NULL, NULL, NULL, 16, 96, &keep);
    approval_card_layout(rd, 1, NULL, NULL, NULL, 16, 96, &drop);
    ASSERT_EQ(keep.rows[0].copy.w, 20);
    ASSERT_EQ(keep.rows[0].text.w, 20);
    ASSERT_EQ(drop.rows[0].copy.w, 0);
    ASSERT_EQ(drop.rows[0].copy.h, 0);
    /* The text got its space back (old geometry: SP_SM before the tag). */
    ASSERT_EQ(drop.rows[0].text.w,
              drop.rows[0].tag.x - ns_scale(SP_SM, 96) - drop.rows[0].text.x);
    ASSERT_TRUE(drop.rows[0].text.w > 0);
    ASSERT_TRUE(drop.rows[0].tag.w > 0);

    /* Zero-size copy rect is never returned by the hit test, anywhere. */
    int copy_hits = 0;
    for (int y = -2; y < 402; y++)
        for (int x = -2; x < 170; x++)
            if (approval_card_hit(&drop, x, y, NULL) == HIT_COPY) copy_hits++;
    ASSERT_EQ(copy_hits, 0);
    /* Including the old position, where the text now is. */
    NsRect c = rect_center(drop.rows[0].text);
    ASSERT_EQ(approval_card_hit(&drop, c.x, c.y, NULL), HIT_TEXT);
    TEST_END();
}

int test_approval_card_layout_copy_invariants_over_widths(void)
{
    TEST_BEGIN();
    const int dpis[2] = { 96, 192 };
    for (int d = 0; d < 2; d++) {
        int dpi = dpis[d];
        int icon = ns_scale(SZ_ICON, dpi);
        for (int w = 0; w <= 700; w += 3) {
            NsRect r = { 0, 0, w, 300 };
            ApprovalCardLayout out;
            approval_card_layout(r, 2, NULL, NULL, NULL, 16, dpi, &out);
            for (int i = 0; i < 2; i++) {
                const ApprovalRowLayout *row = &out.rows[i];
                if (row->copy.w > 0) {
                    ASSERT_EQ(row->copy.w, icon);
                    ASSERT_EQ(row->copy.h, icon);
                    ASSERT_TRUE(row->text.w >= icon);
                    ASSERT_TRUE(!rects_overlap(row->copy, row->text));
                    ASSERT_TRUE(!rects_overlap(row->copy, row->tag));
                    ASSERT_TRUE(!rects_overlap(row->copy, row->checkbox));
                } else {
                    ASSERT_EQ(row->copy.h, 0);
                    ASSERT_TRUE(row->text.w >= 0);
                    NsRect c = rect_center(row->copy);
                    ASSERT_TRUE(approval_card_hit(&out, c.x, c.y, NULL) != HIT_COPY);
                }
                /* The tag is last to go: present whenever the row is. */
                ASSERT_EQ(row->tag.w, ns_scale(64, dpi));
            }
        }
    }
    TEST_END();
}

int test_settled_row_layout_copy_rect_with_chip(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 20, 400, 100 };
    ApprovalRowLayout out;
    settled_row_layout(r, 50, 40, 16, 96, &out);
    ASSERT_EQ(out.copy.w, 20);
    ASSERT_EQ(out.copy.h, 20);
    ASSERT_EQ(out.copy.x + out.copy.w + 4, out.tag.x);
    ASSERT_EQ(out.text.x + out.text.w + 4, out.copy.x);
    ASSERT_EQ(out.copy.y, r.y + (approval_row_height(16, 96) - 20) / 2);
    ASSERT_TRUE(!rects_overlap(out.copy, out.text));
    ASSERT_TRUE(!rects_overlap(out.copy, out.tag));
    ASSERT_TRUE(rect_inside(out.copy, r));
    TEST_END();
}

int test_settled_row_layout_copy_rect_192(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 800, 100 };
    ApprovalRowLayout out;
    settled_row_layout(r, 50, 80, 24, 192, &out);
    ASSERT_EQ(out.copy.w, 40);
    ASSERT_EQ(out.copy.x + out.copy.w + 8, out.tag.x);
    ASSERT_EQ(out.text.x + out.text.w + 8, out.copy.x);
    ASSERT_TRUE(!rects_overlap(out.copy, out.text));
    ASSERT_TRUE(!rects_overlap(out.copy, out.tag));
    TEST_END();
}

int test_settled_row_layout_copy_dropped_on_narrow_row(void)
{
    TEST_BEGIN();
    /* chip 40, icon 20, two SP_XS gaps: icon survives while the text keeps
     * >= 20, i.e. r.w >= 20 + 4 + 20 + 4 + 40 = 88. */
    NsRect keep = { 0, 0, 88, 100 };
    NsRect drop = { 0, 0, 87, 100 };
    ApprovalRowLayout a, b;
    settled_row_layout(keep, 50, 40, 16, 96, &a);
    settled_row_layout(drop, 50, 40, 16, 96, &b);
    ASSERT_EQ(a.copy.w, 20);
    ASSERT_EQ(a.text.w, 20);
    ASSERT_EQ(b.copy.w, 0);
    ASSERT_EQ(b.copy.h, 0);
    /* The chip survives, and the text gets the old SP_SM gap geometry. */
    ASSERT_EQ(b.tag.w, 40);
    ASSERT_EQ(b.text.w, b.tag.x - 8 - drop.x);
    ASSERT_TRUE(!rects_overlap(b.text, b.tag));
    TEST_END();
}

/* ===========================================================================
 * ns_policy_layout / ns_policy_width / ns_policy_hit -- the status line's
 * policy pill: four touching cells, one band each, cut at `clip_right`
 * (docs/superpowers/specs/2026-10-18-ai-auto-approve-button-design.md s3).
 * ===========================================================================
 */

static const int POLICY_TEXT_W[NS_POLICY_STOPS] = { 40, 30, 50, 60 };
/* At 96 DPI the cells are 56, 46, 66, 76 wide (text + 2 * SP_SM). */

/* ---- cells touch, in order, sum to total_w == ns_policy_width() -------- */

int test_ns_policy_layout_cells_touch_and_sum_to_total_w_96(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 5, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &out);

    ASSERT_EQ(out.cell[0].x, r.x);
    for (int k = 1; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.cell[k].x, out.cell[k - 1].x + out.cell[k - 1].w);
        ASSERT_EQ(out.cell[k].y, r.y);
        ASSERT_EQ(out.cell[k].h, r.h);
    }
    int sum = 0;
    for (int k = 0; k < NS_POLICY_STOPS; k++) sum += out.cell[k].w;
    ASSERT_EQ(sum, out.total_w);
    ASSERT_EQ(out.total_w, ns_policy_width(POLICY_TEXT_W, 96));
    ASSERT_EQ(out.clip_right, r.x + r.w);
    TEST_END();
}

int test_ns_policy_layout_cells_touch_and_sum_to_total_w_192(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 5, 0, 56 };
    r.w = ns_policy_width(POLICY_TEXT_W, 192);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 192, &out);

    ASSERT_EQ(out.cell[0].x, r.x);
    for (int k = 1; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.cell[k].x, out.cell[k - 1].x + out.cell[k - 1].w);
        ASSERT_EQ(out.cell[k].y, r.y);
        ASSERT_EQ(out.cell[k].h, r.h);
    }
    int sum = 0;
    for (int k = 0; k < NS_POLICY_STOPS; k++) sum += out.cell[k].w;
    ASSERT_EQ(sum, out.total_w);
    ASSERT_EQ(out.total_w, ns_policy_width(POLICY_TEXT_W, 192));
    TEST_END();
}

/* ---- the hit rect is the cell's column, max(hit_h, r.h) tall, centred -- */

int test_ns_policy_layout_hit_rect_spans_taller_box_96(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 14, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, 56, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.hit[k].x, out.cell[k].x);
        ASSERT_EQ(out.hit[k].w, out.cell[k].w);
        ASSERT_EQ(out.hit[k].h, 56);
        ASSERT_EQ(out.hit[k].y, r.y + (r.h - 56) / 2);
    }
    TEST_END();
}

int test_ns_policy_layout_hit_rect_spans_taller_box_192(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 28, 0, 56 };
    r.w = ns_policy_width(POLICY_TEXT_W, 192);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, 112, 192, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.hit[k].x, out.cell[k].x);
        ASSERT_EQ(out.hit[k].w, out.cell[k].w);
        ASSERT_EQ(out.hit[k].h, 112);
        ASSERT_EQ(out.hit[k].y, r.y + (r.h - 112) / 2);
    }
    TEST_END();
}

/* ---- point-in-cell hit-tests to (HIT_POLICY_ALLOWED, stop) ------------- */

int test_ns_policy_hit_all_four_cells(void)
{
    TEST_BEGIN();
    NsRect r = { 20, 20, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        int stop = -99;
        NsRect c = out.cell[k];
        ASSERT_EQ(ns_policy_hit(&out, c.x + c.w / 2, c.y + c.h / 2, &stop), HIT_POLICY_ALLOWED);
        ASSERT_EQ(stop, k);
        /* The bottom row of the cell, where the rail used to be, is the
         * same cell now. */
        stop = -99;
        ASSERT_EQ(ns_policy_hit(&out, c.x + 1, c.y + c.h - 1, &stop), HIT_POLICY_ALLOWED);
        ASSERT_EQ(stop, k);
    }
    TEST_END();
}

int test_ns_policy_hit_outside_returns_none(void)
{
    TEST_BEGIN();
    NsRect r = { 20, 20, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &out);

    int cy = r.y + r.h / 2;
    int stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, r.x - 5, cy, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, r.x + out.total_w + 5, cy, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    int cx = r.x + out.total_w / 2;
    stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, cx, r.y - 5, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, cx, r.y + r.h + 5, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    TEST_END();
}

/* ---- NULL-safety, zero-height rect, negative stop_text_w --------------- */

int test_ns_policy_layout_null_out_is_noop(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 200, 28 };
    ns_policy_layout(r, POLICY_TEXT_W, 200, 28, 96, NULL); /* must not crash */
    TEST_END();
}

int test_ns_policy_width_null_stop_text_w_safe(void)
{
    TEST_BEGIN();
    int pad = ns_scale(SP_SM, 96);
    ASSERT_EQ(ns_policy_width(NULL, 96), 4 * (2 * pad > 1 ? 2 * pad : 1));
    TEST_END();
}

int test_ns_policy_layout_null_stop_text_w_no_negative_widths(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 200, 28 };
    NsPolicyLayout out;
    ns_policy_layout(r, NULL, 200, 28, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_TRUE(out.cell[k].w >= 1);
    }
    TEST_END();
}

int test_ns_policy_layout_zero_height_rect_safe(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 200, 0 };
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, 200, 0, 96, &out); /* must not crash */
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_TRUE(out.cell[k].w >= 1);
        ASSERT_EQ(ns_policy_hit(&out, out.cell[k].x, 0, NULL), HIT_NONE);
    }
    TEST_END();
}

int test_ns_policy_layout_negative_stop_text_w_no_negative_widths(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 200, 28 };
    int text_w[NS_POLICY_STOPS] = { 40, -50, 50, 60 };
    NsPolicyLayout out;
    ns_policy_layout(r, text_w, 200, 28, 96, &out);
    ASSERT_TRUE(out.cell[1].w >= 1);
    int pad = ns_scale(SP_SM, 96);
    int expect = 2 * pad;
    if (expect < 1) expect = 1;
    ASSERT_EQ(out.cell[1].w, expect);
    ASSERT_TRUE(ns_policy_width(text_w, 96) > 0);
    TEST_END();
}

int test_ns_policy_hit_null_layout_safe(void)
{
    TEST_BEGIN();
    int stop = -99;
    ASSERT_EQ(ns_policy_hit(NULL, 5, 5, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    TEST_END();
}

int test_ns_policy_hit_null_stop_out_safe(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &out);
    NsRect c = out.hit[0];
    ASSERT_EQ(ns_policy_hit(&out, c.x + c.w / 2, c.y + c.h / 2, NULL), HIT_POLICY_ALLOWED);
    TEST_END();
}

/* ---- hit_h shorter than r.h still gives a target >= the painted cell --- */

int test_ns_policy_layout_hit_h_smaller_than_r_h_96(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, 10 /* hit_h < r.h */, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_TRUE(out.hit[k].h >= out.cell[k].h);
    }
    TEST_END();
}

int test_ns_policy_layout_hit_h_smaller_than_r_h_192(void)
{
    TEST_BEGIN();
    NsRect r = { 0, 0, 0, 56 };
    r.w = ns_policy_width(POLICY_TEXT_W, 192);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, 20 /* hit_h < r.h */, 192, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_TRUE(out.hit[k].h >= out.cell[k].h);
    }
    TEST_END();
}

/* ---- the clip (spec section 3, tier 5) --------------------------------- */

int test_ns_policy_layout_clip_marks_partial_cells_unhittable(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout full, out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &full);
    /* One pixel into cell 2: cells 0 and 1 whole, 2 cut, 3 gone. */
    int clip = full.cell[2].x + 1;
    ns_policy_layout(r, POLICY_TEXT_W, clip, r.h, 96, &out);
    ASSERT_EQ(out.clip_right, clip);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        /* Painted geometry does not move; only hittability changes. */
        ASSERT_EQ(out.cell[k].x, full.cell[k].x);
        ASSERT_EQ(out.cell[k].w, full.cell[k].w);
    }
    ASSERT_TRUE(out.hit[0].w > 0 && out.hit[0].h > 0);
    ASSERT_TRUE(out.hit[1].w > 0 && out.hit[1].h > 0);
    ASSERT_EQ(out.hit[2].w, 0);
    ASSERT_EQ(out.hit[3].w, 0);
    TEST_END();
}

int test_ns_policy_layout_clip_on_a_cell_edge_keeps_that_cell(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout full, out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &full);
    /* clip_right is exclusive: a cell ending exactly on it is whole. */
    int clip = full.cell[1].x + full.cell[1].w;
    ns_policy_layout(r, POLICY_TEXT_W, clip, r.h, 96, &out);
    ASSERT_TRUE(out.hit[1].w > 0);
    ASSERT_EQ(out.hit[2].w, 0);
    ns_policy_layout(r, POLICY_TEXT_W, clip - 1, r.h, 96, &out);
    ASSERT_EQ(out.hit[1].w, 0);
    TEST_END();
}

int test_ns_policy_hit_refuses_a_clipped_cell(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout full, out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x + r.w, r.h, 96, &full);
    int clip = full.cell[2].x + 20;           /* cell 2's left 20 px still paint */
    ns_policy_layout(r, POLICY_TEXT_W, clip, r.h, 96, &out);
    int stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, full.cell[2].x + 5, 14, &stop), HIT_NONE);
    ASSERT_EQ(stop, -1);
    stop = -99;
    ASSERT_EQ(ns_policy_hit(&out, full.cell[1].x + 5, 14, &stop), HIT_POLICY_ALLOWED);
    ASSERT_EQ(stop, 1);
    TEST_END();
}

/* Review Focus 3: a row narrower than the button puts the clip left of the
 * pill -- nothing may take a click. */
int test_ns_policy_layout_clip_left_of_pill_hits_nothing(void)
{
    TEST_BEGIN();
    NsRect r = { 10, 0, 0, 28 };
    r.w = ns_policy_width(POLICY_TEXT_W, 96);
    NsPolicyLayout out;
    ns_policy_layout(r, POLICY_TEXT_W, r.x - 30, r.h, 96, &out);
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        ASSERT_EQ(out.hit[k].w, 0);
        ASSERT_TRUE(out.cell[k].w >= 1);
        int stop = -99;
        ASSERT_EQ(ns_policy_hit(&out, out.cell[k].x + 2, 14, &stop), HIT_NONE);
        ASSERT_EQ(stop, -1);
    }
    TEST_END();
}
