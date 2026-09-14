#include "test_framework.h"
#include "about_layout.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

/* The three text line heights used by most cases: representative pixel
 * heights for FONT_HEADING / FONT_BODY / FONT_CAPTION at 96 DPI. */
#define T_H 18
#define T_B 14
#define T_C 12

static int rect_bottom(AboutRect r) { return r.y + r.h; }

/* ---- Exact geometry at 96 DPI ------------------------------------------ */

int test_about_layout_96_exact_positions(void) {
    TEST_BEGIN();
    AboutLayout l;
    about_layout_compute(&l, 96, ABOUT_ICON_96, T_H, T_B, T_C);

    ASSERT_EQ(l.client_w, ABOUT_CLIENT_W_96);

    /* SP_XL top margin, icon centred */
    ASSERT_EQ(l.icon.y, SP_XL);
    ASSERT_EQ(l.icon.w, ABOUT_ICON_96);
    ASSERT_EQ(l.icon.h, ABOUT_ICON_96);
    ASSERT_EQ(l.icon.x, (ABOUT_CLIENT_W_96 - ABOUT_ICON_96) / 2);

    /* SP_LG between icon and title */
    ASSERT_EQ(l.title.y, SP_XL + ABOUT_ICON_96 + SP_LG);
    ASSERT_EQ(l.title.h, T_H);

    /* SP_SM between title and tagline */
    ASSERT_EQ(l.tagline.y, l.title.y + T_H + SP_SM);
    ASSERT_EQ(l.tagline.h, T_B);

    /* SP_XS between tagline and copyright */
    ASSERT_EQ(l.copyright.y, l.tagline.y + T_B + SP_XS);
    ASSERT_EQ(l.copyright.h, T_C);

    /* SP_XL between the text block and the button */
    ASSERT_EQ(l.button.y, l.copyright.y + T_C + SP_XL);
    ASSERT_EQ(l.button.w, SZ_BTN_MIN_W);
    ASSERT_EQ(l.button.h, SZ_CTRL_H);

    /* SP_LG bottom margin */
    ASSERT_EQ(l.client_h, l.button.y + SZ_CTRL_H + SP_LG);
    TEST_END();
}

/* ---- DPI scaling ------------------------------------------------------- */

int test_about_layout_192_is_exactly_double_96(void) {
    TEST_BEGIN();
    AboutLayout a, b;
    about_layout_compute(&a, 96, ABOUT_ICON_96, T_H, T_B, T_C);
    about_layout_compute(&b, 192, ABOUT_ICON_96 * 2, T_H * 2, T_B * 2, T_C * 2);

    ASSERT_EQ(b.client_w, a.client_w * 2);
    ASSERT_EQ(b.client_h, a.client_h * 2);

    ASSERT_EQ(b.icon.x, a.icon.x * 2);
    ASSERT_EQ(b.icon.y, a.icon.y * 2);
    ASSERT_EQ(b.icon.w, a.icon.w * 2);
    ASSERT_EQ(b.icon.h, a.icon.h * 2);

    ASSERT_EQ(b.title.y, a.title.y * 2);
    ASSERT_EQ(b.title.h, a.title.h * 2);
    ASSERT_EQ(b.tagline.y, a.tagline.y * 2);
    ASSERT_EQ(b.tagline.h, a.tagline.h * 2);
    ASSERT_EQ(b.copyright.y, a.copyright.y * 2);
    ASSERT_EQ(b.copyright.h, a.copyright.h * 2);

    ASSERT_EQ(b.button.x, a.button.x * 2);
    ASSERT_EQ(b.button.y, a.button.y * 2);
    ASSERT_EQ(b.button.w, a.button.w * 2);
    ASSERT_EQ(b.button.h, a.button.h * 2);
    TEST_END();
}

int test_about_layout_dpi_le_zero_treated_as_96(void) {
    TEST_BEGIN();
    AboutLayout base, zero, neg;
    about_layout_compute(&base, 96, ABOUT_ICON_96, T_H, T_B, T_C);
    about_layout_compute(&zero, 0, ABOUT_ICON_96, T_H, T_B, T_C);
    about_layout_compute(&neg, -144, ABOUT_ICON_96, T_H, T_B, T_C);
    ASSERT_EQ(memcmp(&zero, &base, sizeof(base)), 0);
    ASSERT_EQ(memcmp(&neg, &base, sizeof(base)), 0);
    TEST_END();
}

int test_about_layout_grows_with_dpi(void) {
    TEST_BEGIN();
    int dpis[] = {96, 120, 144, 168, 192, 240};
    int prev_h = 0, prev_w = 0;
    for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        AboutLayout l;
        about_layout_compute(&l, dpis[i], ns_scale(ABOUT_ICON_96, dpis[i]),
                             ns_scale(T_H, dpis[i]), ns_scale(T_B, dpis[i]),
                             ns_scale(T_C, dpis[i]));
        ASSERT_TRUE(l.client_h >= prev_h);
        ASSERT_TRUE(l.client_w >= prev_w);
        ASSERT_EQ(l.client_w, ns_scale(ABOUT_CLIENT_W_96, dpis[i]));
        prev_h = l.client_h;
        prev_w = l.client_w;
    }
    TEST_END();
}

/* ---- Ordering and containment ------------------------------------------ */

int test_about_layout_elements_ordered_and_do_not_overlap(void) {
    TEST_BEGIN();
    int dpis[] = {96, 144, 192};
    for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        int dpi = dpis[i];
        AboutLayout l;
        about_layout_compute(&l, dpi, ns_scale(ABOUT_ICON_96, dpi),
                             ns_scale(T_H, dpi), ns_scale(T_B, dpi),
                             ns_scale(T_C, dpi));
        ASSERT_TRUE(l.icon.y > 0);
        ASSERT_TRUE(rect_bottom(l.icon) <= l.title.y);
        ASSERT_TRUE(rect_bottom(l.title) <= l.tagline.y);
        ASSERT_TRUE(rect_bottom(l.tagline) <= l.copyright.y);
        ASSERT_TRUE(rect_bottom(l.copyright) <= l.button.y);
        ASSERT_TRUE(rect_bottom(l.button) < l.client_h);
        /* Nothing sticks out sideways. */
        ASSERT_TRUE(l.icon.x >= 0 && l.icon.x + l.icon.w <= l.client_w);
        ASSERT_TRUE(l.button.x >= 0 && l.button.x + l.button.w <= l.client_w);
    }
    TEST_END();
}

int test_about_layout_text_rows_span_full_width(void) {
    TEST_BEGIN();
    AboutLayout l;
    about_layout_compute(&l, 144, ns_scale(ABOUT_ICON_96, 144), 27, 21, 18);
    ASSERT_EQ(l.title.x, 0);
    ASSERT_EQ(l.title.w, l.client_w);
    ASSERT_EQ(l.tagline.x, 0);
    ASSERT_EQ(l.tagline.w, l.client_w);
    ASSERT_EQ(l.copyright.x, 0);
    ASSERT_EQ(l.copyright.w, l.client_w);
    TEST_END();
}

/* ---- Button ------------------------------------------------------------ */

int test_about_layout_button_centred_and_on_grid(void) {
    TEST_BEGIN();
    int dpis[] = {96, 120, 144, 192};
    for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        int dpi = dpis[i];
        AboutLayout l;
        about_layout_compute(&l, dpi, ns_scale(ABOUT_ICON_96, dpi),
                             ns_scale(T_H, dpi), ns_scale(T_B, dpi),
                             ns_scale(T_C, dpi));
        ASSERT_EQ(l.button.w, ns_scale(SZ_BTN_MIN_W, dpi));
        ASSERT_EQ(l.button.h, ns_scale(SZ_CTRL_H, dpi));
        ASSERT_EQ(l.button.x, (l.client_w - l.button.w) / 2);
        /* Left and right gutters differ by at most one rounding pixel. */
        int right_gutter = l.client_w - (l.button.x + l.button.w);
        ASSERT_TRUE(right_gutter - l.button.x <= 1);
        ASSERT_TRUE(right_gutter - l.button.x >= 0);
    }
    TEST_END();
}

int test_about_layout_total_height_is_button_bottom_plus_margin(void) {
    TEST_BEGIN();
    int dpis[] = {96, 120, 144, 168, 192, 240, 288};
    for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        int dpi = dpis[i];
        AboutLayout l;
        about_layout_compute(&l, dpi, ns_scale(ABOUT_ICON_96, dpi),
                             ns_scale(T_H, dpi), ns_scale(T_B, dpi),
                             ns_scale(T_C, dpi));
        ASSERT_EQ(l.client_h,
                  l.button.y + l.button.h + ns_scale(SP_LG, dpi));
    }
    TEST_END();
}

/* ---- Degenerate input -------------------------------------------------- */

int test_about_layout_zero_line_heights_still_sane(void) {
    TEST_BEGIN();
    AboutLayout l;
    about_layout_compute(&l, 96, 0, 0, 0, 0);

    ASSERT_TRUE(l.client_w > 0);
    ASSERT_TRUE(l.client_h > 0);
    ASSERT_TRUE(l.icon.x >= 0 && l.icon.y >= 0);
    ASSERT_EQ(l.icon.w, 0);
    ASSERT_EQ(l.icon.h, 0);
    ASSERT_EQ(l.title.h, 0);
    ASSERT_EQ(l.tagline.h, 0);
    ASSERT_EQ(l.copyright.h, 0);
    /* Still ordered, still inside the client area. */
    ASSERT_TRUE(rect_bottom(l.icon) <= l.title.y);
    ASSERT_TRUE(rect_bottom(l.title) <= l.tagline.y);
    ASSERT_TRUE(rect_bottom(l.tagline) <= l.copyright.y);
    ASSERT_TRUE(rect_bottom(l.copyright) <= l.button.y);
    ASSERT_TRUE(rect_bottom(l.button) < l.client_h);
    /* The button keeps its grid size even with no text. */
    ASSERT_EQ(l.button.w, SZ_BTN_MIN_W);
    ASSERT_EQ(l.button.h, SZ_CTRL_H);
    TEST_END();
}

int test_about_layout_negative_sizes_clamp_to_zero(void) {
    TEST_BEGIN();
    AboutLayout neg, zero;
    about_layout_compute(&neg, 96, -80, -18, -14, -12);
    about_layout_compute(&zero, 96, 0, 0, 0, 0);
    ASSERT_EQ(memcmp(&neg, &zero, sizeof(zero)), 0);
    TEST_END();
}

int test_about_layout_null_out_is_safe(void) {
    TEST_BEGIN();
    about_layout_compute(NULL, 96, ABOUT_ICON_96, T_H, T_B, T_C);
    ASSERT_TRUE(1);
    TEST_END();
}
