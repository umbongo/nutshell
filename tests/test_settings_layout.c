#include "test_framework.h"
#include "settings_layout.h"
#include "ns_scale.h"
#include <string.h>

/* ---- ns_scale (settings_layout's own scale alias was removed in the
 * Design-System Foundation's final migration task; these tests now cover
 * ns_scale() directly, at the exact px/dpi pairs the Settings window uses,
 * so a regression there still gets caught here) ---- */

int test_sl_scale_identity_at_96(void) {
    TEST_BEGIN();
    ASSERT_EQ(ns_scale(168, 96), 168);
    ASSERT_EQ(ns_scale(1, 96), 1);
    ASSERT_EQ(ns_scale(0, 96), 0);
    TEST_END();
}

int test_sl_scale_144_is_one_and_half(void) {
    TEST_BEGIN();
    ASSERT_EQ(ns_scale(96, 144), 144);
    ASSERT_EQ(ns_scale(100, 144), 150);
    TEST_END();
}

int test_sl_scale_never_zero_for_positive_px(void) {
    TEST_BEGIN();
    /* Even a tiny px value at a tiny dpi must not round to zero. */
    ASSERT_TRUE(ns_scale(1, 1) >= 1);
    ASSERT_TRUE(ns_scale(1, 48) >= 1);
    ASSERT_TRUE(ns_scale(1, 96) >= 1);
    TEST_END();
}

int test_sl_scale_monotonic_in_dpi(void) {
    TEST_BEGIN();
    int prev = ns_scale(100, 48);
    int dpis[] = {72, 96, 120, 144, 168, 192, 240};
    for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        int cur = ns_scale(100, dpis[i]);
        ASSERT_TRUE(cur >= prev);
        prev = cur;
    }
    TEST_END();
}

/* ---- settings_metrics_init ---- */

int test_sl_metrics_init_96_matches_base(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    ASSERT_EQ(m.dpi, 96);
    ASSERT_TRUE(m.nav_w > 0);
    ASSERT_TRUE(m.pad > 0);
    ASSERT_TRUE(m.label_w > 0);
    ASSERT_TRUE(m.gap > 0);
    ASSERT_TRUE(m.row_h > 0);
    ASSERT_TRUE(m.ctrl_h > 0);
    ASSERT_TRUE(m.nav_item_h > 0);
    ASSERT_TRUE(m.btnbar_h > 0);
    ASSERT_TRUE(m.btn_w > 0);
    ASSERT_TRUE(m.btn_h > 0);
    ASSERT_TRUE(m.min_w > 0);
    ASSERT_TRUE(m.min_h > 0);
    TEST_END();
}

int test_sl_metrics_init_dpi_le_zero_treated_96(void) {
    TEST_BEGIN();
    SettingsMetrics zero, neg, base;
    settings_metrics_init(&zero, 0);
    settings_metrics_init(&neg, -50);
    settings_metrics_init(&base, 96);
    ASSERT_EQ(zero.dpi, 96);
    ASSERT_EQ(neg.dpi, 96);
    ASSERT_EQ(zero.nav_w, base.nav_w);
    ASSERT_EQ(neg.nav_w, base.nav_w);
    ASSERT_EQ(zero.min_h, base.min_h);
    TEST_END();
}

int test_sl_metrics_init_null_safe(void) {
    TEST_BEGIN();
    /* Must not crash. */
    settings_metrics_init(NULL, 96);
    settings_metrics_init(NULL, 0);
    TEST_END();
}

int test_sl_metrics_monotonic_with_dpi(void) {
    TEST_BEGIN();
    SettingsMetrics m96, m144, m192;
    settings_metrics_init(&m96, 96);
    settings_metrics_init(&m144, 144);
    settings_metrics_init(&m192, 192);
    ASSERT_TRUE(m144.nav_w >= m96.nav_w);
    ASSERT_TRUE(m192.nav_w >= m144.nav_w);
    ASSERT_TRUE(m144.row_h >= m96.row_h);
    ASSERT_TRUE(m192.row_h >= m144.row_h);
    ASSERT_TRUE(m144.btnbar_h >= m96.btnbar_h);
    ASSERT_TRUE(m144.min_w >= m96.min_w);
    ASSERT_TRUE(m144.min_h >= m96.min_h);
    TEST_END();
}

int test_sl_metrics_never_zero_across_dpis(void) {
    TEST_BEGIN();
    int dpis[] = {48, 72, 96, 120, 144, 168, 192, 240, 288};
    for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        SettingsMetrics m;
        settings_metrics_init(&m, dpis[i]);
        ASSERT_TRUE(m.nav_w > 0);
        ASSERT_TRUE(m.pad > 0);
        ASSERT_TRUE(m.label_w > 0);
        ASSERT_TRUE(m.gap > 0);
        ASSERT_TRUE(m.row_h > 0);
        ASSERT_TRUE(m.ctrl_h > 0);
        ASSERT_TRUE(m.nav_item_h > 0);
        ASSERT_TRUE(m.btnbar_h > 0);
        ASSERT_TRUE(m.btn_w > 0);
        ASSERT_TRUE(m.btn_h > 0);
        ASSERT_TRUE(m.min_w > 0);
        ASSERT_TRUE(m.min_h > 0);
    }
    TEST_END();
}

/* min_h must be big enough for the whole nav list plus the button bar,
 * at every DPI we scale to. */
int test_sl_min_h_fits_full_nav_list(void) {
    TEST_BEGIN();
    int dpis[] = {96, 120, 144, 168, 192, 240};
    for (size_t i = 0; i < sizeof(dpis) / sizeof(dpis[0]); i++) {
        SettingsMetrics m;
        settings_metrics_init(&m, dpis[i]);
        int nav_list_h = m.pad * 2 + m.nav_item_h * settings_nav_count();
        ASSERT_TRUE(m.min_h >= nav_list_h + m.btnbar_h);
    }
    TEST_END();
}

/* ---- Nav model ---- */

int test_sl_nav_count_is_12(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_nav_count(), 12);
    TEST_END();
}

int test_sl_nav_at_oob_null(void) {
    TEST_BEGIN();
    ASSERT_NULL(settings_nav_at(-1));
    ASSERT_NULL(settings_nav_at(settings_nav_count()));
    ASSERT_NULL(settings_nav_at(9999));
    TEST_END();
}

int test_sl_nav_headers_well_formed(void) {
    TEST_BEGIN();
    int n = settings_nav_count();
    for (int i = 0; i < n; i++) {
        const SettingsNavEntry *e = settings_nav_at(i);
        ASSERT_NOT_NULL(e);
        ASSERT_NOT_NULL(e->label);
        if (e->is_header) {
            ASSERT_EQ(e->depth, 0);
            ASSERT_EQ(e->page_id, SETTINGS_PAGE_NONE);
        }
    }
    TEST_END();
}

int test_sl_nav_children_well_formed(void) {
    TEST_BEGIN();
    int n = settings_nav_count();
    for (int i = 0; i < n; i++) {
        const SettingsNavEntry *e = settings_nav_at(i);
        if (!e->is_header) {
            ASSERT_TRUE(e->page_id != SETTINGS_PAGE_NONE);
            ASSERT_TRUE(e->page_id >= 0 && e->page_id < SETTINGS_PAGE_COUNT);
            /* depth is 0 (About, standalone) or 1 (child of a group) */
            ASSERT_TRUE(e->depth == 0 || e->depth == 1);
        }
    }
    TEST_END();
}

int test_sl_nav_exact_sequence(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_nav_count(), 12);

    struct { int page_id; int depth; int is_header; const char *label; } expect[] = {
        { SETTINGS_PAGE_NONE,         0, 1, "General" },
        { SETTINGS_PAGE_APPEARANCE,   1, 0, "Appearance" },
        { SETTINGS_PAGE_TERMINAL,     1, 0, "Terminal" },
        { SETTINGS_PAGE_LOGGING,      1, 0, "Logging" },
        { SETTINGS_PAGE_NONE,         0, 1, "Connection" },
        { SETTINGS_PAGE_SSH,          1, 0, "SSH" },
        { SETTINGS_PAGE_STARTUP,      1, 0, "Startup" },
        { SETTINGS_PAGE_NONE,         0, 1, "AI Assistant" },
        { SETTINGS_PAGE_AI_PROVIDER,  1, 0, "Provider" },
        { SETTINGS_PAGE_AI_BEHAVIOUR, 1, 0, "Behaviour" },
        { SETTINGS_PAGE_AI_WEB,       1, 0, "Web Access" },
        { SETTINGS_PAGE_ABOUT,        0, 0, "About" },
    };

    for (int i = 0; i < 12; i++) {
        const SettingsNavEntry *e = settings_nav_at(i);
        ASSERT_NOT_NULL(e);
        ASSERT_EQ(e->page_id, expect[i].page_id);
        ASSERT_EQ(e->depth, expect[i].depth);
        ASSERT_EQ(e->is_header, expect[i].is_header);
        ASSERT_STR_EQ(e->label, expect[i].label);
    }
    TEST_END();
}

int test_sl_nav_page_ids_unique(void) {
    TEST_BEGIN();
    int n = settings_nav_count();
    for (int i = 0; i < n; i++) {
        const SettingsNavEntry *a = settings_nav_at(i);
        if (a->is_header) continue;
        for (int j = i + 1; j < n; j++) {
            const SettingsNavEntry *b = settings_nav_at(j);
            if (b->is_header) continue;
            ASSERT_TRUE(a->page_id != b->page_id);
        }
    }
    TEST_END();
}

int test_sl_nav_covers_every_page(void) {
    TEST_BEGIN();
    for (int pid = 0; pid < SETTINGS_PAGE_COUNT; pid++) {
        int idx = settings_nav_index_of_page(pid);
        ASSERT_TRUE(idx >= 0);
        const SettingsNavEntry *e = settings_nav_at(idx);
        ASSERT_NOT_NULL(e);
        ASSERT_EQ(e->page_id, pid);
        ASSERT_FALSE(e->is_header);
    }
    TEST_END();
}

int test_sl_nav_index_of_page_none_is_invalid(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_nav_index_of_page(SETTINGS_PAGE_NONE), -1);
    ASSERT_EQ(settings_nav_index_of_page(9999), -1);
    ASSERT_EQ(settings_nav_index_of_page(-99), -1);
    TEST_END();
}

int test_sl_nav_first_page_is_appearance(void) {
    TEST_BEGIN();
    int idx = settings_nav_first_page();
    ASSERT_TRUE(idx >= 0);
    const SettingsNavEntry *e = settings_nav_at(idx);
    ASSERT_NOT_NULL(e);
    ASSERT_FALSE(e->is_header);
    ASSERT_EQ(e->page_id, SETTINGS_PAGE_APPEARANCE);
    TEST_END();
}

/* ---- settings_page_title ---- */

int test_sl_page_title_valid(void) {
    TEST_BEGIN();
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_APPEARANCE), "General > Appearance");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_TERMINAL), "General > Terminal");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_LOGGING), "General > Logging");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_SSH), "Connection > SSH");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_STARTUP), "Connection > Startup");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_AI_PROVIDER), "AI Assistant > Provider");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_AI_BEHAVIOUR), "AI Assistant > Behaviour");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_AI_WEB), "AI Assistant > Web Access");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_ABOUT), "About");
    TEST_END();
}

int test_sl_page_title_oob_null(void) {
    TEST_BEGIN();
    ASSERT_NULL(settings_page_title(SETTINGS_PAGE_NONE));
    ASSERT_NULL(settings_page_title(SETTINGS_PAGE_COUNT));
    ASSERT_NULL(settings_page_title(9999));
    ASSERT_NULL(settings_page_title(-99));
    TEST_END();
}

/* ---- settings_layout_regions ---- */

/* Returns 1 if the three panes tile the client area exactly (no gap, no
 * overlap) and match the expected geometry. Plain bool return so it can be
 * shared between test functions without touching their local _tf_local_fail. */
static int panes_tile_exactly(int client_w, int client_h, const SettingsMetrics *m) {
    SettingsRect nav, content, buttons;
    settings_layout_regions(client_w, client_h, m, &nav, &content, &buttons);

    if (nav.x != 0 || nav.y != 0) return 0;
    if (nav.w != m->nav_w) return 0;
    if (nav.h != client_h - m->btnbar_h) return 0;

    if (content.x != nav.w || content.y != 0) return 0;
    if (content.w != client_w - m->nav_w) return 0;
    if (content.h != client_h - m->btnbar_h) return 0;

    if (buttons.x != 0 || buttons.y != client_h - m->btnbar_h) return 0;
    if (buttons.w != client_w) return 0;
    if (buttons.h != m->btnbar_h) return 0;

    long long total = (long long)nav.w * nav.h + (long long)content.w * content.h +
                       (long long)buttons.w * buttons.h;
    return total == (long long)client_w * client_h;
}

int test_sl_regions_tile_exactly_96dpi(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    ASSERT_TRUE(panes_tile_exactly(800, 600, &m));
    TEST_END();
}

int test_sl_regions_tile_exactly_144dpi(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 144);
    ASSERT_TRUE(panes_tile_exactly(1100, 820, &m));
    TEST_END();
}

int test_sl_regions_nondegenerate_at_min_size(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    SettingsRect nav, content, buttons;
    settings_layout_regions(m.min_w, m.min_h, &m, &nav, &content, &buttons);
    ASSERT_TRUE(nav.w > 0);
    ASSERT_TRUE(nav.h > 0);
    ASSERT_TRUE(content.w > 0);
    ASSERT_TRUE(content.h > 0);
    ASSERT_TRUE(buttons.w > 0);
    ASSERT_TRUE(buttons.h > 0);
    TEST_END();
}

int test_sl_regions_nondegenerate_at_min_size_144dpi(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 144);
    SettingsRect nav, content, buttons;
    settings_layout_regions(m.min_w, m.min_h, &m, &nav, &content, &buttons);
    ASSERT_TRUE(nav.w > 0);
    ASSERT_TRUE(nav.h > 0);
    ASSERT_TRUE(content.w > 0);
    ASSERT_TRUE(content.h > 0);
    ASSERT_TRUE(buttons.w > 0);
    ASSERT_TRUE(buttons.h > 0);
    TEST_END();
}

int test_sl_regions_null_out_pointers_safe(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    /* No crash with any subset of NULL out pointers. */
    settings_layout_regions(800, 600, &m, NULL, NULL, NULL);
    SettingsRect nav;
    settings_layout_regions(800, 600, &m, &nav, NULL, NULL);
    SettingsRect content;
    settings_layout_regions(800, 600, &m, NULL, &content, NULL);
    SettingsRect buttons;
    settings_layout_regions(800, 600, &m, NULL, NULL, &buttons);
    ASSERT_TRUE(nav.w == m.nav_w);
    ASSERT_TRUE(content.w == 800 - m.nav_w);
    ASSERT_TRUE(buttons.h == m.btnbar_h);
    TEST_END();
}

int test_sl_regions_null_metrics_safe(void) {
    TEST_BEGIN();
    SettingsRect nav, content, buttons;
    /* Must not crash even without metrics. */
    settings_layout_regions(800, 600, NULL, &nav, &content, &buttons);
    TEST_END();
}

/* ---- settings_row_rects ---- */

int test_sl_row_rects_pitch(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    int content_w = 500;

    SettingsRect label0, ctrl0, label3, ctrl3;
    settings_row_rects(0, content_w, 0, 0, &m, &label0, &ctrl0);
    settings_row_rects(3, content_w, 0, 0, &m, &label3, &ctrl3);

    ASSERT_EQ(label3.y - label0.y, 3 * m.row_h);
    ASSERT_EQ(ctrl3.y - ctrl0.y, 3 * m.row_h);
    TEST_END();
}

int test_sl_row_rects_label_ctrl_no_overlap(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    int content_w = 500;
    for (int row = 0; row < 6; row++) {
        SettingsRect label, ctrl;
        settings_row_rects(row, content_w, 0, 0, &m, &label, &ctrl);
        /* x-ranges must be disjoint: label ends at or before ctrl begins */
        ASSERT_TRUE(label.x + label.w <= ctrl.x);
    }
    TEST_END();
}

int test_sl_row_rects_ctrl_stretch_stays_in_content(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    int content_w = 500;
    SettingsRect label, ctrl;
    settings_row_rects(1, content_w, 0, 0, &m, &label, &ctrl);
    ASSERT_TRUE(ctrl.x >= 0);
    ASSERT_TRUE(ctrl.x + ctrl.w <= content_w);
    ASSERT_TRUE(ctrl.w > 0);
    TEST_END();
}

int test_sl_row_rects_ctrl_explicit_width(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    int content_w = 500;
    SettingsRect label, ctrl;
    settings_row_rects(0, content_w, 60, 0, &m, &label, &ctrl);
    ASSERT_EQ(ctrl.w, 60);
    ASSERT_TRUE(ctrl.x + ctrl.w <= content_w);
    TEST_END();
}

int test_sl_row_rects_y_offset_scrolls_up(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    int content_w = 500;
    SettingsRect label_no_off, ctrl_no_off, label_off, ctrl_off;
    settings_row_rects(2, content_w, 0, 0, &m, &label_no_off, &ctrl_no_off);
    settings_row_rects(2, content_w, 0, 40, &m, &label_off, &ctrl_off);
    ASSERT_EQ(label_no_off.y - label_off.y, 40);
    ASSERT_EQ(ctrl_no_off.y - ctrl_off.y, 40);
    TEST_END();
}

int test_sl_row_rects_null_out_pointers_safe(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    settings_row_rects(0, 500, 0, 0, &m, NULL, NULL);
    SettingsRect label;
    settings_row_rects(0, 500, 0, 0, &m, &label, NULL);
    SettingsRect ctrl;
    settings_row_rects(0, 500, 0, 0, &m, NULL, &ctrl);
    TEST_END();
}

int test_sl_row_rects_null_metrics_safe(void) {
    TEST_BEGIN();
    SettingsRect label, ctrl;
    settings_row_rects(0, 500, 0, 0, NULL, &label, &ctrl);
    TEST_END();
}

/* ---- settings_page_height ---- */

int test_sl_page_height_zero_rows(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    ASSERT_EQ(settings_page_height(0, &m), m.pad * 2);
    TEST_END();
}

int test_sl_page_height_scales_with_rows(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    ASSERT_EQ(settings_page_height(5, &m), m.pad * 2 + 5 * m.row_h);
    TEST_END();
}

int test_sl_page_height_monotonic(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    int prev = settings_page_height(0, &m);
    for (int rows = 1; rows <= 10; rows++) {
        int cur = settings_page_height(rows, &m);
        ASSERT_TRUE(cur > prev);
        prev = cur;
    }
    TEST_END();
}

/* ---- Scroll maths ---- */

int test_sl_scroll_max_zero_when_fits(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_max(400, 400), 0);
    ASSERT_EQ(settings_scroll_max(400, 300), 0);
    ASSERT_EQ(settings_scroll_max(400, 0), 0);
    TEST_END();
}

int test_sl_scroll_max_positive_when_overflow(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_max(400, 600), 200);
    ASSERT_EQ(settings_scroll_max(100, 101), 1);
    TEST_END();
}

int test_sl_scroll_max_never_negative(void) {
    TEST_BEGIN();
    ASSERT_TRUE(settings_scroll_max(0, 0) >= 0);
    ASSERT_TRUE(settings_scroll_max(1000, -50) >= 0);
    TEST_END();
}

int test_sl_scroll_clamp_within_range(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_clamp(50, 200), 50);
    ASSERT_EQ(settings_scroll_clamp(0, 200), 0);
    ASSERT_EQ(settings_scroll_clamp(200, 200), 200);
    TEST_END();
}

int test_sl_scroll_clamp_negative_pins_zero(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_clamp(-1, 200), 0);
    ASSERT_EQ(settings_scroll_clamp(-1000, 200), 0);
    TEST_END();
}

int test_sl_scroll_clamp_over_max_pins_max(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_clamp(300, 200), 200);
    ASSERT_EQ(settings_scroll_clamp(999999, 200), 200);
    TEST_END();
}

int test_sl_scroll_clamp_zero_max(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_clamp(0, 0), 0);
    ASSERT_EQ(settings_scroll_clamp(50, 0), 0);
    ASSERT_EQ(settings_scroll_clamp(-50, 0), 0);
    TEST_END();
}
