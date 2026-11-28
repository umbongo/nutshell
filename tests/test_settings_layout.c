#include "test_framework.h"
#include "settings_layout.h"
#include "ns_scale.h"
#include "dlog_types.h"
#include <stdio.h>
#include <stdlib.h>
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

/* The label column is wide enough for the longest right-aligned label,
 * "Commands as tool calls:" (a STATIC wraps and clips what does not fit).
 * make wintest measures the real text; this pins the number. */
int test_sl_label_column_width(void) {
    TEST_BEGIN();
    SettingsMetrics m;
    settings_metrics_init(&m, 96);
    ASSERT_EQ(m.label_w, 168);
    settings_metrics_init(&m, 144);
    ASSERT_EQ(m.label_w, 252);
    /* The default 760-px client leaves the controls room beside it:
     * pane = 760 - nav_w, the widest fixed row (Max total) is 340. */
    settings_metrics_init(&m, 96);
    ASSERT_TRUE(760 - m.nav_w - m.pad - m.label_w - m.gap - m.pad >= 340);
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

int test_sl_nav_count_is_14(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_nav_count(), 14);
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
            /* depth is 0 (Debugging, About: standalone) or 1 (child of a group) */
            ASSERT_TRUE(e->depth == 0 || e->depth == 1);
        }
    }
    TEST_END();
}

int test_sl_nav_exact_sequence(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_nav_count(), 14);

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
        { SETTINGS_PAGE_VOICE,        1, 0, "Voice" },
        { SETTINGS_PAGE_AI_WEB,       1, 0, "Web Access" },
        { SETTINGS_PAGE_DEBUG,        0, 0, "Debugging" },
        { SETTINGS_PAGE_ABOUT,        0, 0, "About" },
    };

    for (int i = 0; i < 14; i++) {
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
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_VOICE), "AI Assistant > Voice");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_AI_WEB), "AI Assistant > Web Access");
    ASSERT_STR_EQ(settings_page_title(SETTINGS_PAGE_DEBUG), "Debugging");
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

int test_sl_scroll_to_show_in_view_unchanged(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_to_show(100, 150, 30, 200, 1000), 100);
    ASSERT_EQ(settings_scroll_to_show(100, 100, 200, 200, 1000), 100);
    TEST_END();
}

int test_sl_scroll_to_show_above_scrolls_up(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_to_show(300, 120, 30, 200, 1000), 120);
    TEST_END();
}

int test_sl_scroll_to_show_below_scrolls_just_enough(void) {
    TEST_BEGIN();
    /* bottom at 500 must land on the viewport's bottom edge: 500 - 200 */
    ASSERT_EQ(settings_scroll_to_show(0, 470, 30, 200, 1000), 300);
    TEST_END();
}

int test_sl_scroll_to_show_taller_than_view_top_aligned(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_to_show(0, 400, 300, 200, 1000), 400);
    ASSERT_EQ(settings_scroll_to_show(900, 400, 300, 200, 1000), 400);
    TEST_END();
}

int test_sl_scroll_to_show_clamped_at_both_ends(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_to_show(50, -20, 30, 200, 1000), 0);
    ASSERT_EQ(settings_scroll_to_show(0, 990, 30, 200, 1000), 800);
    ASSERT_EQ(settings_scroll_to_show(10, 50, 30, 200, 100), 0);
    TEST_END();
}

/* Source-reading gate: the Settings window registers its controls in a fixed
 * table (MAX_SETTINGS_CTRLS) and add_ctrl() refuses one when it is full, so a
 * row past the cap is silently dropped. Count the registrations in the source
 * and keep a margin of 8 under the cap. */
#define SETTINGS_CTRL_MARGIN 8

static char *sl_read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

static int sl_count_in(const char *line, const char *needle)
{
    int n = 0;
    for (const char *p = line; (p = strstr(p, needle)) != NULL; p += strlen(needle))
        n++;
    return n;
}

int test_settings_ctrl_table_has_room(void) {
    TEST_BEGIN();
    char *src = sl_read_whole("src/ui/settings.c");
    if (!src) {
        const char *ci = getenv("CI");
        if (ci && ci[0]) {
            printf("  src/ui/settings.c not readable under CI\n");
            ASSERT_TRUE(0);
        } else {
            printf("  [settings] src/ui/settings.c not readable from cwd -- skipping\n");
        }
        TEST_END();
    }
    int cap = 0, count = 0;
    for (char *line = src; line && *line; ) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        const char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (strncmp(t, "#define MAX_SETTINGS_CTRLS", 26) == 0) {
            cap = atoi(t + 26);
        } else if (strncmp(t, "/*", 2) != 0 && *t != '*' && strncmp(t, "//", 2) != 0) {
            /* Creation sites pass the window data as `nd`; the helpers'
             * own `d` calls (inside dbg_add) are not registrations. */
            count += sl_count_in(t, "add_ctrl(nd,");
            int dbg = sl_count_in(t, "dbg_add(nd,");
            if (dbg && strstr(t, "SDBG_LEVEL_RENDER + cat"))
                count += dbg * (int)DLOG_CAT_COUNT;   /* the per-category loop */
            else
                count += dbg;
        }
        line = nl ? nl + 1 : NULL;
    }
    free(src);
    ASSERT_TRUE(cap > 0);
    ASSERT_TRUE(count > 0);
    if (count > cap - SETTINGS_CTRL_MARGIN)
        printf("  Settings registers %d controls; MAX_SETTINGS_CTRLS is %d "
               "(needs a margin of %d)\n", count, cap, SETTINGS_CTRL_MARGIN);
    ASSERT_TRUE(count <= cap - SETTINGS_CTRL_MARGIN);
    TEST_END();
}

int test_sl_scroll_clamp_zero_max(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_clamp(0, 0), 0);
    ASSERT_EQ(settings_scroll_clamp(50, 0), 0);
    ASSERT_EQ(settings_scroll_clamp(-50, 0), 0);
    TEST_END();
}

/* ---- settings_initial_placement ---- */

int test_sl_initial_1080p_fits_behaviour(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    int cw = 0, ch = 0, wx = 0, wy = 0;
    settings_initial_placement(&m, 22, 0, 0, 1920, 1032, 16, 39, &cw, &ch, &wx, &wy);
    ASSERT_EQ(cw, 760);
    ASSERT_EQ(ch, 744);
    ASSERT_EQ(wx, (1920 - 776) / 2);
    ASSERT_EQ(wy, (1032 - 783) / 2);
    TEST_END();
}

int test_sl_initial_scroll_expectations(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    int cw = 0, ch = 0, wx = 0, wy = 0;
    settings_initial_placement(&m, 22, 0, 0, 1920, 1032, 16, 39, &cw, &ch, &wx, &wy);
    ASSERT_EQ(settings_page_height(22, &m), 688);
    ASSERT_EQ(settings_page_height(21, &m), 658);
    ASSERT_EQ(settings_scroll_max(ch - m.btnbar_h, settings_page_height(22, &m)), 0);
    ASSERT_EQ(settings_scroll_max(688, settings_page_height(21, &m)), 0);
    TEST_END();
}

int test_sl_initial_follows_row_count(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    int cw = 0, ch = 0, wx = 0, wy = 0;
    settings_initial_placement(&m, 17, 0, 0, 1920, 1032, 16, 39, &cw, &ch, &wx, &wy);
    ASSERT_EQ(ch, 594);
    TEST_END();
}

int test_sl_initial_laptop_height(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    int cw = 0, ch = 0, wx = 0, wy = 0;
    settings_initial_placement(&m, 22, 0, 0, 1366, 728, 16, 39, &cw, &ch, &wx, &wy);
    ASSERT_EQ(ch, 641);
    ASSERT_EQ(settings_scroll_max(641 - m.btnbar_h, 688), 103);
    TEST_END();
}

int test_sl_initial_tiny_work_area(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    int cw = 0, ch = 0, wx = 0, wy = 0;
    settings_initial_placement(&m, 22, 0, 0, 640, 400, 16, 39, &cw, &ch, &wx, &wy);
    ASSERT_EQ(cw, m.min_w);
    ASSERT_EQ(cw, 620);
    ASSERT_EQ(ch, m.min_h);
    ASSERT_EQ(m.min_h, 420);
    ASSERT_EQ(wx, (640 - (m.min_w + 16)) / 2);
    ASSERT_EQ(wy, 0);
    TEST_END();
}

int test_sl_initial_narrow_clamps_width(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    int cw = 0, ch = 0, wx = 0, wy = 0;
    settings_initial_placement(&m, 22, 0, 0, 700, 1032, 16, 39, &cw, &ch, &wx, &wy);
    ASSERT_EQ(cw, 700 - 16 - 48);
    TEST_END();
}

int test_sl_initial_144dpi_capped(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 144);
    int cw = 0, ch = 0, wx = 0, wy = 0;
    settings_initial_placement(&m, 22, 0, 0, 1920, 1008, 24, 57, &cw, &ch, &wx, &wy);
    ASSERT_EQ(ch, 879);
    ASSERT_TRUE(wx >= 0);
    ASSERT_TRUE(wy >= 0);
    ASSERT_TRUE(wx + cw + 24 <= 1920);
    ASSERT_TRUE(wy + ch + 57 <= 1008);
    TEST_END();
}

int test_sl_initial_negative_origin(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    int cw = 0, ch = 0, wx = 0, wy = 0;
    settings_initial_placement(&m, 22, -1920, -200, 1920, 1032, 16, 39, &cw, &ch, &wx, &wy);
    ASSERT_TRUE(wx >= -1920);
    ASSERT_TRUE(wx + cw + 16 <= 0);
    ASSERT_TRUE(wy >= -200);
    ASSERT_TRUE(wy + ch + 39 <= 832);
    ASSERT_EQ(wx, -1348);
    ASSERT_EQ(wy, -76);
    TEST_END();
}

/* ---- A DPI change (WM_DPICHANGED): the same layout at 96/120/144/192 ---- */

static const int k_dpis[] = { 96, 120, 144, 192 };
#define N_DPIS ((int)(sizeof(k_dpis) / sizeof(k_dpis[0])))

int test_sl_dpi_metrics_scale_with_dpi(void) {
    TEST_BEGIN();
    for (int i = 0; i < N_DPIS; i++) {
        SettingsMetrics m;
        settings_metrics_init(&m, k_dpis[i]);
        ASSERT_EQ(m.dpi, k_dpis[i]);
        ASSERT_EQ(m.nav_w, ns_scale(168, k_dpis[i]));
        ASSERT_EQ(m.label_w, ns_scale(168, k_dpis[i]));
        ASSERT_EQ(m.row_h, ns_scale(30, k_dpis[i]));
        ASSERT_EQ(m.min_w, ns_scale(620, k_dpis[i]));
        /* A row's control always fits in its row, at every scale. */
        ASSERT_TRUE(m.ctrl_h <= m.row_h);
        ASSERT_TRUE(m.btn_h <= m.btnbar_h);
    }
    TEST_END();
}

int test_sl_dpi_min_size_scales(void) {
    TEST_BEGIN();
    SettingsMetrics prev;
    settings_metrics_init(&prev, 96);
    for (int i = 1; i < N_DPIS; i++) {
        SettingsMetrics m;
        settings_metrics_init(&m, k_dpis[i]);
        ASSERT_TRUE(m.min_w > prev.min_w);
        ASSERT_TRUE(m.min_h > prev.min_h);
        /* The scale is the DPI ratio, to within a pixel of rounding. */
        int want_w = 620 * k_dpis[i] / 96;
        ASSERT_TRUE(m.min_w >= want_w - 1 && m.min_w <= want_w + 1);
        prev = m;
    }
    TEST_END();
}

int test_sl_dpi_min_window_adds_the_frame(void) {
    TEST_BEGIN();
    for (int i = 0; i < N_DPIS; i++) {
        SettingsMetrics m;
        settings_metrics_init(&m, k_dpis[i]);
        int w = 0, h = 0;
        settings_min_window_size(&m, ns_scale(16, k_dpis[i]),
                                 ns_scale(39, k_dpis[i]), &w, &h);
        ASSERT_EQ(w, m.min_w + ns_scale(16, k_dpis[i]));
        ASSERT_EQ(h, m.min_h + ns_scale(39, k_dpis[i]));
    }
    int w = 7, h = 7;
    settings_min_window_size(NULL, 1, 1, &w, &h);
    ASSERT_EQ(w, 7);                      /* a NULL metrics writes nothing */
    settings_min_window_size(NULL, 1, 1, NULL, NULL);   /* and nulls are safe */
    TEST_END();
}

int test_sl_dpi_min_size_regions_dont_overlap(void) {
    TEST_BEGIN();
    for (int i = 0; i < N_DPIS; i++) {
        SettingsMetrics m;
        settings_metrics_init(&m, k_dpis[i]);
        SettingsRect nav, content, buttons;
        settings_layout_regions(m.min_w, m.min_h, &m, &nav, &content, &buttons);
        ASSERT_EQ(nav.x + nav.w, content.x);              /* side by side   */
        ASSERT_EQ(content.x + content.w, m.min_w);        /* no clipping    */
        ASSERT_EQ(nav.y + nav.h, buttons.y);              /* nav above bar  */
        ASSERT_EQ(content.y + content.h, buttons.y);
        ASSERT_EQ(buttons.y + buttons.h, m.min_h);
        ASSERT_TRUE(content.w > m.label_w + m.gap + 2 * m.pad);  /* a control fits */
        /* The Save/Cancel pair and the footer fit the bar. */
        ASSERT_TRUE(2 * m.btn_w + m.gap + 2 * m.pad < buttons.w);
    }
    TEST_END();
}

int test_sl_dpi_rows_stay_inside_the_page(void) {
    TEST_BEGIN();
    for (int i = 0; i < N_DPIS; i++) {
        SettingsMetrics m;
        settings_metrics_init(&m, k_dpis[i]);
        SettingsRect nav, content, buttons;
        settings_layout_regions(m.min_w, m.min_h, &m, &nav, &content, &buttons);
        for (int row = 0; row < 20; row++) {
            SettingsRect l, c;
            settings_row_rects(row, content.w, 0, 0, &m, &l, &c);
            ASSERT_TRUE(l.x >= 0 && c.x >= l.x + l.w);      /* no overlap   */
            ASSERT_TRUE(c.x + c.w <= content.w);            /* no clipping  */
            ASSERT_TRUE(l.y == c.y && c.h <= m.row_h);
        }
    }
    TEST_END();
}

int test_sl_scroll_rescale(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_scroll_rescale(0, 96, 192), 0);
    ASSERT_EQ(settings_scroll_rescale(100, 96, 192), 200);
    ASSERT_EQ(settings_scroll_rescale(200, 192, 96), 100);
    ASSERT_EQ(settings_scroll_rescale(100, 96, 144), 150);
    ASSERT_EQ(settings_scroll_rescale(100, 120, 120), 100);
    ASSERT_EQ(settings_scroll_rescale(-5, 96, 192), 0);        /* never negative */
    ASSERT_EQ(settings_scroll_rescale(100, 0, 192), 100);      /* bad dpi: unchanged */
    ASSERT_EQ(settings_scroll_rescale(100, 96, 0), 100);
    /* There and back again returns to within a pixel. */
    int there = settings_scroll_rescale(333, 96, 120);
    int back = settings_scroll_rescale(there, 120, 96);
    ASSERT_TRUE(back >= 332 && back <= 334);
    TEST_END();
}

/* ---- Row layout: no row's content past the page at the minimum width ---- */

/* The widest case the dialog has: the page scrolls, so the scrollbar's 14 px
 * come off the width as well. */
#define TEST_CSB_W 14

static int usable_at_min(const SettingsMetrics *m)
{
    SettingsRect nav, content, buttons;
    settings_layout_regions(m->min_w, m->min_h, m, &nav, &content, &buttons);
    return content.w - 1 - TEST_CSB_W;   /* the page host is inset by 1 */
}

int test_sl_row_layout_matches_plain_rects_when_it_fits(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    SettingsRowSpec sp = { 340, 260, 0, 0, 0 };   /* the Keep-at-most row */
    SettingsRowGeom g;
    settings_row_layout(&m, 3, 1000, 0, &sp, &g);
    ASSERT_EQ(g.ctrl.x, m.pad + m.label_w + m.gap);
    ASSERT_EQ(g.ctrl.w, 70);                       /* asked width, kept   */
    ASSERT_EQ(g.extra.x, g.ctrl.x + 70 + m.gap);
    ASSERT_EQ(g.extra.w, 260);
    SettingsRowSpec st = { 0, 84, 56, 0, 0 };     /* a stretched row     */
    settings_row_layout(&m, 3, 1000, 0, &st, &g);
    ASSERT_EQ(g.extra2.w, 56);
    ASSERT_EQ(g.extra2.x + g.extra2.w, 1000 - m.pad);
    ASSERT_EQ(g.extra.x + g.extra.w + m.gap, g.extra2.x);
    ASSERT_EQ(g.ctrl.x + g.ctrl.w + m.gap, g.extra.x);
    TEST_END();
}

int test_sl_row_layout_null_safe(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    SettingsRowSpec sp = { 0, 0, 0, 0, 0 };
    SettingsRowGeom g;
    settings_row_layout(NULL, 0, 500, 0, &sp, &g);
    settings_row_layout(&m, 0, 500, 0, NULL, &g);
    settings_row_layout(&m, 0, 500, 0, &sp, NULL);
    SettingsRowSpec out;
    ASSERT_EQ(settings_debug_slot_spec(-1, &out), 0);
    ASSERT_EQ(settings_debug_slot_spec(SDBG_COUNT, &out), 0);
    ASSERT_EQ(settings_debug_slot_spec(SDBG_NOTE, NULL), 0);
    ASSERT_EQ(settings_debug_slot_spec(SDBG_MAX_TOTAL, &out), 1);
    ASSERT_EQ(out.extra_w, 260);
    TEST_END();
}

static int debug_rows_fit_at(int dpi)
{
    SettingsMetrics m; settings_metrics_init(&m, dpi);
    int usable = usable_at_min(&m);
    SettingsDebugPos pos[SDBG_COUNT];
    (void)settings_debug_layout(1, pos);
    for (int s = 0; s < SDBG_COUNT; s++) {
        if (!pos[s].visible) continue;
        SettingsRowSpec sp;
        if (!settings_debug_slot_spec(s, &sp)) return 0;
        SettingsRowGeom g;
        settings_row_layout(&m, pos[s].row, usable, 0, &sp, &g);
        int right = usable - m.pad;
        if (g.ctrl.w <= 0) return 0;
        if (g.ctrl.x + g.ctrl.w > right) return 0;                 /* past the page */
        if (g.extra.w > 0 && g.extra.x + g.extra.w > right) return 0;
        if (g.extra2.w > 0 && g.extra2.x + g.extra2.w > right) return 0;
        if (g.extra.w > 0 && g.ctrl.x + g.ctrl.w > g.extra.x) return 0;   /* overlap */
        if (g.extra2.w > 0 && g.extra.w > 0 && g.extra.x + g.extra.w > g.extra2.x) return 0;
        if (!sp.full_w && g.label.x + g.label.w > g.ctrl.x) return 0;
    }
    return 1;
}

int test_sl_debug_rows_fit_at_min_width_96(void) {
    TEST_BEGIN();
    ASSERT_TRUE(debug_rows_fit_at(96));
    TEST_END();
}

int test_sl_debug_rows_fit_at_min_width_144(void) {
    TEST_BEGIN();
    ASSERT_TRUE(debug_rows_fit_at(144));
    TEST_END();
}

int test_sl_debug_rows_fit_at_min_width_120_and_192(void) {
    TEST_BEGIN();
    ASSERT_TRUE(debug_rows_fit_at(120));
    ASSERT_TRUE(debug_rows_fit_at(192));
    TEST_END();
}

int test_sl_debug_keep_at_most_edit_keeps_its_width(void) {
    TEST_BEGIN();
    /* At the minimum width the "using N MB" text gives way, not the edit. */
    int dpis[2] = { 96, 144 };
    for (int i = 0; i < 2; i++) {
        SettingsMetrics m; settings_metrics_init(&m, dpis[i]);
        SettingsRowSpec sp;
        ASSERT_EQ(settings_debug_slot_spec(SDBG_MAX_TOTAL, &sp), 1);
        SettingsRowGeom g;
        settings_row_layout(&m, 14, usable_at_min(&m), 0, &sp, &g);
        ASSERT_EQ(g.ctrl.w, ns_scale(70, dpis[i]));
        ASSERT_TRUE(g.extra.w > ns_scale(100, dpis[i]));   /* the text still has room */
        ASSERT_TRUE(g.extra.w < ns_scale(260, dpis[i]));   /* and was the part cut    */
    }
    TEST_END();
}

int test_sl_row_layout_tiny_page_never_negative(void) {
    TEST_BEGIN();
    SettingsMetrics m; settings_metrics_init(&m, 96);
    SettingsRowSpec sp = { 340, 260, 0, 0, 0 };
    SettingsRowGeom g;
    settings_row_layout(&m, 1, 100, 0, &sp, &g);   /* narrower than the label column */
    ASSERT_TRUE(g.ctrl.w >= 0);
    ASSERT_TRUE(g.extra.w >= 0);
    TEST_END();
}
