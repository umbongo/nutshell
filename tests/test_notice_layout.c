#include "test_framework.h"
#include "notice_layout.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <stdlib.h>
#include <string.h>

static void mk_in(NoticeLayoutIn *in, int dpi, int items, int action)
{
    memset(in, 0, sizeof(*in));
    in->dpi = dpi;
    in->title_h = ns_scale(20, dpi);
    in->opening_h = ns_scale(36, dpi);
    in->closing_h = ns_scale(18, dpi);
    in->item_count = items;
    in->label_w = ns_scale(110, dpi);
    for (int i = 0; i < items; i++) in->value_h[i] = ns_scale(18 + 18 * (i % 2), dpi);
    in->has_action = action;
}

static int empty(NsRect r) { return r.w == 0 && r.h == 0; }

static int overlap(NsRect a, NsRect b)
{
    if (empty(a) || empty(b)) return 0;
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

int test_notice_layout_buttons_right_aligned(void)
{
    TEST_BEGIN();
    int dpis[2] = { 96, 192 };
    for (int d = 0; d < 2; d++) {
        NoticeLayoutIn in;
        NoticeLayout L;
        mk_in(&in, dpis[d], 2, 1);
        notice_layout(&in, &L);
        int pad = ns_scale(SP_LG, dpis[d]);
        ASSERT_EQ(L.ok.x + L.ok.w, L.client_w - pad);
        ASSERT_TRUE(L.action.x + L.action.w < L.ok.x);
        ASSERT_EQ(L.action.y, L.ok.y);
        ASSERT_EQ(L.action.h, L.ok.h);
        ASSERT_EQ(L.client_w, ns_scale(460, dpis[d]));
        ASSERT_EQ(L.ok.y + L.ok.h + pad, L.client_h);
    }
    TEST_END();
}

int test_notice_layout_rows_in_order_without_overlap(void)
{
    TEST_BEGIN();
    int dpis[2] = { 96, 192 };
    for (int d = 0; d < 2; d++) {
        NoticeLayoutIn in;
        NoticeLayout L;
        mk_in(&in, dpis[d], 3, 1);
        notice_layout(&in, &L);
        NsRect seq[12];
        int n = 0;
        seq[n++] = L.title; seq[n++] = L.opening;
        for (int i = 0; i < 3; i++) { seq[n++] = L.label[i]; }
        seq[n++] = L.closing; seq[n++] = L.ok;
        for (int i = 0; i + 1 < n; i++)
            ASSERT_TRUE(seq[i].y + seq[i].h <= seq[i + 1].y);
        for (int i = 0; i < 3; i++) {
            ASSERT_EQ(L.label[i].y, L.value[i].y);
            ASSERT_TRUE(L.label[i].x + L.label[i].w <= L.value[i].x);
            ASSERT_TRUE(L.value[i].x + L.value[i].w <= L.client_w);
        }
        NsRect all[16];
        n = 0;
        all[n++] = L.icon; all[n++] = L.title; all[n++] = L.opening;
        for (int i = 0; i < 3; i++) { all[n++] = L.label[i]; all[n++] = L.value[i]; }
        all[n++] = L.closing; all[n++] = L.action; all[n++] = L.ok;
        for (int i = 0; i < n; i++) {
            ASSERT_TRUE(all[i].x >= L.bar.w);
            ASSERT_TRUE(all[i].x + all[i].w <= L.client_w);
            ASSERT_TRUE(all[i].y + all[i].h <= L.client_h);
            for (int j = i + 1; j < n; j++) ASSERT_FALSE(overlap(all[i], all[j]));
        }
        ASSERT_EQ(L.bar.h, L.client_h);
        ASSERT_EQ(L.bar.w, ns_scale(4, dpis[d]));
        ASSERT_TRUE(L.icon.x + L.icon.w <= L.title.x);
        ASSERT_EQ(L.text_w, L.client_w - L.bar.w - 2 * ns_scale(SP_LG, dpis[d]));
    }
    TEST_END();
}

int test_notice_layout_no_action_and_no_closing(void)
{
    TEST_BEGIN();
    NoticeLayoutIn in;
    NoticeLayout a, b;
    mk_in(&in, 96, 1, 1);
    notice_layout(&in, &a);
    in.has_action = 0;
    notice_layout(&in, &b);
    ASSERT_TRUE(empty(b.action));
    ASSERT_FALSE(empty(a.action));
    ASSERT_EQ(a.ok.x, b.ok.x);
    ASSERT_EQ(a.ok.y, b.ok.y);
    ASSERT_EQ(a.client_h, b.client_h);

    in.closing_h = 0;
    in.item_count = 0;
    NoticeLayout c;
    notice_layout(&in, &c);
    ASSERT_TRUE(empty(c.closing));
    ASSERT_TRUE(c.client_h < b.client_h);
    ASSERT_TRUE(c.opening.y + c.opening.h < c.ok.y);
    TEST_END();
}

int test_notice_layout_grows_with_items(void)
{
    TEST_BEGIN();
    NoticeLayoutIn in;
    NoticeLayout L;
    int prev = 0;
    for (int n = 0; n <= 5; n++) {
        mk_in(&in, 96, n, 1);
        notice_layout(&in, &L);
        if (n > 0) ASSERT_TRUE(L.client_h > prev);
        prev = L.client_h;
    }
    mk_in(&in, 96, NOTICE_ITEMS_MAX + 7, 0); /* clamped, no overflow */
    notice_layout(&in, &L);
    ASSERT_TRUE(L.client_h > 0);
    TEST_END();
}

int test_notice_layout_192_is_twice_96(void)
{
    TEST_BEGIN();
    NoticeLayoutIn a, b;
    NoticeLayout A, B;
    mk_in(&a, 96, 3, 1);
    mk_in(&b, 192, 3, 1);
    notice_layout(&a, &A);
    notice_layout(&b, &B);
    ASSERT_TRUE(abs(B.client_w - 2 * A.client_w) <= 1);
    ASSERT_TRUE(abs(B.client_h - 2 * A.client_h) <= 1);
    ASSERT_TRUE(abs(B.ok.x - 2 * A.ok.x) <= 1);
    ASSERT_TRUE(abs(B.ok.y - 2 * A.ok.y) <= 1);
    ASSERT_TRUE(abs(B.action.w - 2 * A.action.w) <= 1);
    ASSERT_TRUE(abs(B.value[2].y - 2 * A.value[2].y) <= 1);
    ASSERT_TRUE(abs(B.text_w - 2 * A.text_w) <= 1);
    TEST_END();
}

/* A label wider than the text column leaves the value column empty, never
 * negative, and nothing leaves the client area vertically. */
int test_notice_layout_very_wide_label_keeps_value_width_non_negative(void)
{
    TEST_BEGIN();
    int dpis[2] = { 96, 192 };
    for (int d = 0; d < 2; d++) {
        NoticeLayoutIn in;
        NoticeLayout L;
        mk_in(&in, dpis[d], 2, 1);
        in.label_w = ns_scale(900, dpis[d]);
        notice_layout(&in, &L);
        for (int i = 0; i < 2; i++) {
            ASSERT_TRUE(L.value[i].w >= 0);
            ASSERT_TRUE(L.value[i].x >= L.bar.w);
            ASSERT_TRUE(L.value[i].y + L.value[i].h <= L.client_h);
        }
    }
    TEST_END();
}

/* The title row is as tall as the measured title line (and at least the
 * round glyph), and the opening starts below it. */
int test_notice_layout_title_row_follows_measured_height(void)
{
    TEST_BEGIN();
    NoticeLayoutIn in;
    NoticeLayout a, b;
    mk_in(&in, 96, 1, 0);
    in.title_h = 40;
    notice_layout(&in, &a);
    ASSERT_EQ(a.title.h, 40);
    ASSERT_TRUE(a.opening.y >= a.title.y + 40);
    in.title_h = 4;                      /* shorter than the glyph: the glyph wins */
    notice_layout(&in, &b);
    ASSERT_EQ(b.title.h, ns_scale(16, 96));
    ASSERT_TRUE(b.client_h < a.client_h);
    TEST_END();
}
