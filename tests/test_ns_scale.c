#include "test_framework.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "settings_layout.h"
#include <stdlib.h>

/* ---- ns_scale ---- */

int test_ns_scale_identity_at_96(void) {
    TEST_BEGIN();
    for (int px = 1; px <= 64; px++) {
        ASSERT_EQ(ns_scale(px, 96), px);
    }
    TEST_END();
}

int test_ns_scale_monotonic_across_dpis(void) {
    TEST_BEGIN();
    int dpis[] = { 96, 120, 144, 192 };
    for (int px = 1; px <= 64; px++) {
        int prev = 0;
        for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
            int cur = ns_scale(px, dpis[i]);
            ASSERT_TRUE(cur >= prev);
            prev = cur;
        }
    }
    TEST_END();
}

int test_ns_scale_never_zero_for_positive_px(void) {
    TEST_BEGIN();
    int dpi_table[] = { 96, 120, 144, 168, 192, 216, 240, 288 };
    for (size_t i = 0; i < sizeof(dpi_table) / sizeof(dpi_table[0]); i++) {
        ASSERT_TRUE(ns_scale(1, dpi_table[i]) >= 1);
    }
    TEST_END();
}

int test_ns_scale_matches_settings_scale(void) {
    TEST_BEGIN();
    int dpis[] = { 96, 120, 144, 192 };
    for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        for (int px = 1; px <= 64; px++) {
            ASSERT_EQ(ns_scale(px, dpis[i]), settings_scale(px, dpis[i]));
        }
    }
    TEST_END();
}

int test_ns_scale_dpi_le_zero_treated_as_96(void) {
    TEST_BEGIN();
    ASSERT_EQ(ns_scale(50, 0), ns_scale(50, 96));
    ASSERT_EQ(ns_scale(50, -10), ns_scale(50, 96));
    ASSERT_EQ(ns_scale(1, 0), 1);
    TEST_END();
}

/* ---- ns_type: grid ---- */

int test_ns_type_sp_multiples_of_4(void) {
    TEST_BEGIN();
    ASSERT_EQ(SP_XS % 4, 0);
    ASSERT_EQ(SP_SM % 4, 0);
    ASSERT_EQ(SP_MD % 4, 0);
    ASSERT_EQ(SP_LG % 4, 0);
    ASSERT_EQ(SP_XL % 4, 0);
    ASSERT_EQ(SP_XXL % 4, 0);
    TEST_END();
}

int test_ns_type_sz_multiples_of_4(void) {
    TEST_BEGIN();
    ASSERT_EQ(SZ_CTRL_H % 4, 0);
    ASSERT_EQ(SZ_TAB_H % 4, 0);
    ASSERT_EQ(SZ_ICON % 4, 0);
    ASSERT_EQ(SZ_TAG_H % 4, 0);
    ASSERT_EQ(SZ_AVATAR % 4, 0);
    ASSERT_EQ(SZ_BTN_MIN_W % 4, 0);
    TEST_END();
}

int test_ns_type_radii_multiples_of_4(void) {
    TEST_BEGIN();
    ASSERT_EQ(R_CTRL % 4, 0);
    ASSERT_EQ(R_CARD % 4, 0);
    TEST_END();
}

int test_ns_type_strokes_exact(void) {
    TEST_BEGIN();
    ASSERT_EQ(STROKE_HAIRLINE, 1);
    ASSERT_EQ(STROKE_RULE, 2);
    ASSERT_EQ(STROKE_BAR, 3);
    TEST_END();
}

/* ---- ns_type: ramp ---- */

int test_ns_type_ramp_strictly_increasing_px(void) {
    TEST_BEGIN();
    int dpis[] = { 96, 144, 192 };
    for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        int dpi = dpis[i];
        int caption = ns_type_font_px(FONT_CAPTION, dpi, 10);
        int body    = ns_type_font_px(FONT_BODY, dpi, 10);
        int title   = ns_type_font_px(FONT_TITLE, dpi, 10);
        int heading = ns_type_font_px(FONT_HEADING, dpi, 10);
        ASSERT_TRUE(caption < body);
        ASSERT_TRUE(body < title);
        ASSERT_TRUE(title < heading);
    }
    TEST_END();
}

int test_ns_type_font_px_mono_uses_mono_size(void) {
    TEST_BEGIN();
    int small = ns_type_font_px(FONT_MONO, 96, 10);
    int large = ns_type_font_px(FONT_MONO, 96, 20);
    ASSERT_TRUE(large > small);
    ASSERT_EQ(small, ns_type_font_px(FONT_MONO, 96, 10));
    TEST_END();
}

int test_ns_type_font_weights(void) {
    TEST_BEGIN();
    ASSERT_EQ(ns_type_font(FONT_CAPTION)->weight, 400);
    ASSERT_EQ(ns_type_font(FONT_BODY)->weight, 400);
    ASSERT_EQ(ns_type_font(FONT_MONO)->weight, 400);
    ASSERT_EQ(ns_type_font(FONT_TITLE)->weight, 600);
    ASSERT_EQ(ns_type_font(FONT_HEADING)->weight, 600);
    TEST_END();
}

int test_ns_type_font_role_oob_returns_body(void) {
    TEST_BEGIN();
    const NsFontSpec *body = ns_type_font(FONT_BODY);
    const NsFontSpec *past_end = ns_type_font(FONT_ROLE_COUNT);
    const NsFontSpec *negative = ns_type_font((NsFontRole)(-1));
    ASSERT_NOT_NULL(body);
    ASSERT_NOT_NULL(past_end);
    ASSERT_NOT_NULL(negative);
    ASSERT_EQ(past_end->size_pt, body->size_pt);
    ASSERT_EQ(past_end->weight, body->weight);
    ASSERT_EQ(negative->size_pt, body->size_pt);
    ASSERT_EQ(negative->weight, body->weight);
    TEST_END();
}

/* ---- ns_type: font cache slot ---- */

int test_ns_type_font_slot_unique_and_bounded(void) {
    TEST_BEGIN();
    int dpi_table[] = { 96, 120, 144, 168, 192, 216, 240, 288 };
    int n_dpi = (int)(sizeof(dpi_table) / sizeof(dpi_table[0]));
    int total = FONT_ROLE_COUNT * n_dpi * 4;
    int *seen = (int *)malloc((size_t)total * sizeof(int));
    int count = 0;

    for (int role = 0; role < FONT_ROLE_COUNT; role++) {
        for (int d = 0; d < n_dpi; d++) {
            for (int face = 0; face < 4; face++) {
                int slot = ns_type_font_slot((NsFontRole)role, dpi_table[d], face);
                ASSERT_TRUE(slot >= 0);
                ASSERT_TRUE(slot < NS_FONT_SLOTS);
                for (int k = 0; k < count; k++) {
                    ASSERT_TRUE(seen[k] != slot);
                }
                seen[count++] = slot;
            }
        }
    }

    free(seen);
    TEST_END();
}

int test_ns_type_font_slot_stable_across_calls(void) {
    TEST_BEGIN();
    int a = ns_type_font_slot(FONT_HEADING, 144, 2);
    int b = ns_type_font_slot(FONT_HEADING, 144, 2);
    int c = ns_type_font_slot(FONT_HEADING, 144, 2);
    ASSERT_EQ(a, b);
    ASSERT_EQ(b, c);
    TEST_END();
}

int test_ns_type_font_slot_nearest_dpi_mapping(void) {
    TEST_BEGIN();
    ASSERT_EQ(ns_type_font_slot(FONT_BODY, 150, 0), ns_type_font_slot(FONT_BODY, 144, 0));
    ASSERT_EQ(ns_type_font_slot(FONT_BODY, 100, 0), ns_type_font_slot(FONT_BODY, 96, 0));
    ASSERT_TRUE(ns_type_font_slot(FONT_BODY, 96, 0) != ns_type_font_slot(FONT_BODY, 144, 0));
    TEST_END();
}

/* ---- ns_type: font face id ---- */

int test_ns_type_font_face_id_proportional_is_zero(void) {
    TEST_BEGIN();
    ASSERT_EQ(ns_type_font_face_id(FONT_CAPTION), 0);
    ASSERT_EQ(ns_type_font_face_id(FONT_BODY), 0);
    ASSERT_EQ(ns_type_font_face_id(FONT_TITLE), 0);
    ASSERT_EQ(ns_type_font_face_id(FONT_HEADING), 0);
    TEST_END();
}

int test_ns_type_font_face_id_mono_is_two(void) {
    TEST_BEGIN();
    ASSERT_EQ(ns_type_font_face_id(FONT_MONO), 2);
    TEST_END();
}

/* ---- ns_type: pill ---- */

int test_ns_type_pill(void) {
    TEST_BEGIN();
    ASSERT_EQ(ns_type_pill(28), 14);
    ASSERT_EQ(ns_type_pill(20), 10);
    ASSERT_EQ(ns_type_pill(0), 0);
    TEST_END();
}
