/* Native tests for the themed checkbox geometry (src/core/ns_check_geom.c). */
#include "test_framework.h"
#include "ns_check_geom.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <math.h>

int test_ns_check_layout_box_is_square_and_scaled(void)
{
    TEST_BEGIN();
    static const int dpis[] = { 96, 120, 144, 192 };
    for (size_t i = 0; i < sizeof dpis / sizeof dpis[0]; i++) {
        int dpi = dpis[i];
        NsCheckLayout L;
        ns_check_layout(400, ns_scale(SZ_CTRL_H, dpi), dpi, &L);
        ASSERT_EQ(L.box_w, ns_scale(SZ_CHECK, dpi));
        ASSERT_EQ(L.box_h, L.box_w);
        ASSERT_EQ(L.box_x, ns_scale(SP_XS, dpi));
        ASSERT_EQ(L.radius, ns_scale(R_CHECK, dpi));
    }
    NsCheckLayout L96;
    ns_check_layout(300, 28, 96, &L96);
    ASSERT_EQ(L96.box_w, 20);
    ASSERT_EQ(L96.box_x, 4);
    ASSERT_EQ(L96.box_y, 4);
    TEST_END();
}

int test_ns_check_layout_inside_control_and_centred(void)
{
    TEST_BEGIN();
    static const int dpis[] = { 96, 144, 192 };
    for (size_t i = 0; i < sizeof dpis / sizeof dpis[0]; i++) {
        int dpi = dpis[i];
        int w = ns_scale(260, dpi), h = ns_scale(SZ_CTRL_H, dpi);
        NsCheckLayout L;
        ns_check_layout(w, h, dpi, &L);
        ASSERT_TRUE(L.box_y >= 0);
        ASSERT_TRUE(L.box_y + L.box_h <= h);
        ASSERT_TRUE(L.box_x + L.box_w <= w);
        int top = L.box_y, bottom = h - (L.box_y + L.box_h);
        ASSERT_TRUE(top - bottom <= 1 && bottom - top <= 1);
        ASSERT_EQ(L.label_x, L.box_x + L.box_w + ns_scale(SP_SM, dpi));
        ASSERT_EQ(L.label_x + L.label_w, w);
        ASSERT_EQ(L.label_h, h);
        ASSERT_TRUE(L.label_x >= L.box_x + L.box_w);
    }
    TEST_END();
}

int test_ns_check_layout_small_control_shrinks_box(void)
{
    TEST_BEGIN();
    NsCheckLayout L;
    ns_check_layout(200, 10, 96, &L);
    ASSERT_EQ(L.box_w, 10);
    ASSERT_EQ(L.box_h, 10);
    ASSERT_EQ(L.box_y, 0);
    ASSERT_TRUE(L.radius * 2 <= 10);
    ns_check_layout(14, 40, 96, &L);
    ASSERT_TRUE(L.box_x + L.box_w <= 14);
    ASSERT_EQ(L.label_w, 14 - L.label_x);
    ASSERT_TRUE(L.label_x <= 14);
    ASSERT_TRUE(L.label_w >= 0);
    TEST_END();
}

int test_ns_check_layout_narrow_control_clamps_label(void)
{
    TEST_BEGIN();
    for (int w = 1; w <= 40; w++) {
        NsCheckLayout L;
        ns_check_layout(w, 28, 96, &L);
        ASSERT_TRUE(L.box_x + L.box_w <= w);
        ASSERT_TRUE(L.label_x <= w);
        ASSERT_TRUE(L.label_w >= 0);
        ASSERT_EQ(L.label_x + L.label_w, w);
    }
    TEST_END();
}

int test_ns_check_layout_empty_and_bad_dpi(void)
{
    TEST_BEGIN();
    NsCheckLayout L;
    ns_check_layout(0, 28, 96, &L);
    ASSERT_EQ(L.box_w, 0);
    ASSERT_EQ(L.label_w, 0);
    ns_check_layout(100, 0, 96, &L);
    ASSERT_EQ(L.box_h, 0);
    ns_check_layout(-5, -5, 96, &L);
    ASSERT_EQ(L.box_w, 0);
    NsCheckLayout Z;
    ns_check_layout(300, 28, 0, &Z);
    NsCheckLayout N;
    ns_check_layout(300, 28, 96, &N);
    ASSERT_EQ(Z.box_w, N.box_w);
    ns_check_layout(300, 28, 96, NULL);   /* must not crash */
    TEST_END();
}

int test_ns_check_mark_inside_box_and_shaped(void)
{
    TEST_BEGIN();
    static const int sides[] = { 18, 27, 36 };
    for (size_t i = 0; i < sizeof sides / sizeof sides[0]; i++) {
        float p[6];
        int bx = 3, by = 7, bw = sides[i];
        ns_check_mark(bx, by, bw, p);
        for (int k = 0; k < 3; k++) {
            ASSERT_TRUE(p[2 * k] > (float)bx);
            ASSERT_TRUE(p[2 * k] < (float)(bx + bw));
            ASSERT_TRUE(p[2 * k + 1] > (float)by);
            ASSERT_TRUE(p[2 * k + 1] < (float)(by + bw));
        }
        ASSERT_TRUE(p[0] < p[2] && p[2] < p[4]);   /* left to right */
        ASSERT_TRUE(p[3] > p[1] && p[3] > p[5]);   /* the vertex is lowest */
    }
    TEST_END();
}

int test_ns_check_mark_scales_linearly(void)
{
    TEST_BEGIN();
    float a[6], b[6];
    ns_check_mark(0, 0, 18, a);
    ns_check_mark(0, 0, 36, b);
    for (int k = 0; k < 6; k++)
        ASSERT_TRUE(fabsf(b[k] - 2.0f * a[k]) < 0.001f);
    TEST_END();
}

int test_ns_check_mark_width_scales_with_floor(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ns_check_mark_width(96), 2);
    ASSERT_EQ(ns_check_mark_width(144), 3);
    ASSERT_EQ(ns_check_mark_width(192), 4);
    ASSERT_EQ(ns_check_mark_width(0), 2);
    ASSERT_TRUE(ns_check_mark_width(24) >= 2);
    TEST_END();
}
