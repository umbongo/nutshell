#include "test_framework.h"
#include "cheatsheet_layout.h"
#include "shortcuts.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

/* Natively there are no fonts to measure, so text widths are FIXED
 * PER-CHARACTER ESTIMATES of the Inter UI face at 96 DPI, scaled with
 * ns_scale(): 7 px a character in FONT_BODY (13 px), 6 in FONT_CAPTION
 * (12 px), 10 in FONT_HEADING (19 px semibold). Line heights are the type
 * ramp's at 96 DPI (ns_type.c: px x line_height, rounded): body 18,
 * caption 16, heading 24. The frame estimate is a WS_CAPTION popup with
 * WS_EX_DLGMODALFRAME: 16 px wide, 39 px tall at 96 DPI. The scrollbar
 * is CSB_WIDTH (14) scaled. The real window measures instead. */
#define EST_BODY_CHAR_W_96    7
#define EST_CAPTION_CHAR_W_96 6
#define EST_HEADING_CHAR_W_96 10
#define EST_BODY_LINE_96      18
#define EST_CAPTION_LINE_96   16
#define EST_HEADING_LINE_96   24
#define EST_NC_W_96           16
#define EST_NC_H_96           39
#define EST_SCROLLBAR_W_96    14

static int est_w(const char *s, int char_w_96, int dpi)
{
    return ns_scale((int)strlen(s) * char_w_96, dpi);
}

/* The estimate callback: ctx points at the DPI. */
static int est_measure(void *ctx, NsFontRole role, const char *text)
{
    int dpi = *(const int *)ctx;
    int cw = role == FONT_HEADING ? EST_HEADING_CHAR_W_96
           : role == FONT_CAPTION ? EST_CAPTION_CHAR_W_96
           : EST_BODY_CHAR_W_96;
    return est_w(text, cw, dpi);
}

/* The real table, measured with the estimates, in a work_w x work_h area. */
static void fill_input(CsInput *in, int dpi, int work_w, int work_h)
{
    memset(in, 0, sizeof(*in));
    in->dpi = dpi;
    in->work_w = work_w;
    in->work_h = work_h;
    in->nc_w = ns_scale(EST_NC_W_96, dpi);
    in->nc_h = ns_scale(EST_NC_H_96, dpi);
    in->body_h = ns_scale(EST_BODY_LINE_96, dpi);
    in->caption_h = ns_scale(EST_CAPTION_LINE_96, dpi);
    in->heading_h = ns_scale(EST_HEADING_LINE_96, dpi);
    in->scrollbar_w = ns_scale(EST_SCROLLBAR_W_96, dpi);

    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    cheatsheet_input_from_table(in, t, n, est_measure, &dpi);
}

static int inside(CsRect r, int w, int h)
{
    return r.x >= 0 && r.y >= 0 && r.w >= 0 && r.h >= 0 &&
           r.x + r.w <= w && r.y + r.h <= h;
}

static int contains(CsRect outer, CsRect r)
{
    return r.x >= outer.x && r.y >= outer.y &&
           r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}

static int overlap(CsRect a, CsRect b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return 0;
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

/* Every check that holds for any layout: page items inside the page and
 * apart from each other, row parts inside their row, the fixed parts
 * inside the client area and apart. Returns the number of failures. */
static int check_geometry(const CsLayout *l)
{
    int bad = 0;
    int page_w = 0;
    for (int s = 0; s < l->n_sections; s++) {
        int right = l->heading[s].x + l->heading[s].w;
        if (right > page_w) page_w = right;
    }
    page_w += 1; /* exclusive bound */
    CsRect items[2 * CS_MAX_SECTIONS + CS_MAX_ROWS];
    int ni = 0;
    for (int s = 0; s < l->n_sections; s++) {
        items[ni++] = l->heading[s];
        items[ni++] = l->rule[s];
    }
    for (int i = 0; i < l->n_rows; i++) {
        items[ni++] = l->row[i];
        if (!contains(l->row[i], l->chord[i]))  bad++;
        if (!contains(l->row[i], l->action[i])) bad++;
        if (!contains(l->row[i], l->cond[i]))   bad++;
        if (overlap(l->chord[i], l->action[i])) bad++;
        if (overlap(l->action[i], l->cond[i]))  bad++;
    }
    for (int a = 0; a < ni; a++) {
        if (!inside(items[a], page_w, l->page_h)) bad++;
        for (int b = a + 1; b < ni; b++)
            if (overlap(items[a], items[b])) bad++;
    }
    if (!inside(l->view, l->client_w, l->client_h))   bad++;
    if (!inside(l->button, l->client_w, l->client_h)) bad++;
    if (overlap(l->view, l->button)) bad++;
    if (l->scrolls) {
        if (!inside(l->scrollbar, l->client_w, l->client_h)) bad++;
        if (overlap(l->scrollbar, l->view))   bad++;
        if (overlap(l->scrollbar, l->button)) bad++;
        if (l->view.h >= l->page_h) bad++;
    } else {
        if (l->scrollbar.w != 0 || l->scrollbar.h != 0) bad++;
        if (l->view.h != l->page_h) bad++;
    }
    return bad;
}

/* The sheet has room for every row and section of the table. */
int test_cheatsheet_layout_capacity_holds_the_table(void)
{
    TEST_BEGIN();
    size_t n = 0;
    (void)shortcuts_table(&n);
    ASSERT_TRUE(n <= (size_t)CS_MAX_ROWS);
    ASSERT_TRUE(shortcuts_sections(NULL, 0u) <= (size_t)CS_MAX_SECTIONS);
    TEST_END();
}

int test_cheatsheet_layout_two_columns_split_1_4_and_5_7_on_1080p_at_96(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 96, 1920, 1040);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.n_sections, 7);
    ASSERT_EQ(l.columns, 2);
    for (int s = 0; s < 7; s++) ASSERT_EQ(l.heading_col[s], s < 4 ? 0 : 1);
    int i = 0;
    for (int s = 0; s < l.n_sections; s++)
        for (int r = 0; r < in.section_rows[s]; r++, i++)
            ASSERT_EQ(l.row_col[i], s < 4 ? 0 : 1);
    ASSERT_EQ(i, l.n_rows);
    ASSERT_EQ(check_geometry(&l), 0);
    /* Spec 3.1 expects no scrollbar here. With alternatives joined the
     * table is 50 rows, and the first estimate (before the PgUp/PgDn
     * condition lines moved into their actions) needed 962 px plus the
     * 52 px Close row against 1,001 available. The fit-at-96-DPI ruling
     * (the maintainer's 5 percent rule) removed those two lines, which
     * makes it one page; asserted below. The numbers are still printed. */
    printf("  [cheatsheet] 1920x1040 @96: page %d px, client %dx%d, %s\n",
           l.page_h, l.client_w, l.client_h, l.scrolls ? "scrolls" : "one page");
    ASSERT_EQ(l.scrolls, 0);
    TEST_END();
}

/* The scrollbar appears only when the page is taller than the work area:
 * the same table on a work area with room to spare is one page. */
int test_cheatsheet_layout_no_scrollbar_when_the_page_fits(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 96, 1920, 1300);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.columns, 2);
    ASSERT_EQ(l.scrolls, 0);
    ASSERT_EQ(l.scrollbar.w, 0);
    ASSERT_EQ(l.view.h, l.page_h);
    ASSERT_TRUE(l.client_h <= 1300 - in.nc_h);
    ASSERT_EQ(check_geometry(&l), 0);
    TEST_END();
}

int test_cheatsheet_layout_scrolls_on_1366x768_at_120(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 120, 1366, 718);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.scrolls, 1);
    ASSERT_TRUE(l.client_h <= 718 - in.nc_h);
    ASSERT_TRUE(l.client_w <= 1366 - in.nc_w);
    ASSERT_TRUE(inside(l.button, l.client_w, l.client_h));
    ASSERT_TRUE(inside(l.scrollbar, l.client_w, l.client_h));
    ASSERT_TRUE(l.scrollbar.w > 0 && l.scrollbar.h == l.view.h);
    ASSERT_TRUE(l.button.y >= l.view.y + l.view.h);   /* Close stays outside the scroll */
    ASSERT_EQ(check_geometry(&l), 0);
    TEST_END();
}

int test_cheatsheet_layout_one_column_and_scrolls_at_192_on_1080p(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 192, 1920, 984);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.columns, 1);
    ASSERT_EQ(l.scrolls, 1);
    for (int s = 0; s < l.n_sections; s++) ASSERT_EQ(l.heading_col[s], 0);
    for (int s = 1; s < l.n_sections; s++)               /* section order kept */
        ASSERT_TRUE(l.heading[s].y > l.heading[s - 1].y);
    ASSERT_EQ(check_geometry(&l), 0);
    TEST_END();
}

/* With room for everything, 192 DPI is twice 96 DPI to within rounding. */
int test_cheatsheet_layout_192_is_twice_96(void)
{
    TEST_BEGIN();
    CsInput a, b;
    CsLayout la, lb;
    fill_input(&a, 96, 8000, 8000);
    fill_input(&b, 192, 8000, 8000);
    cheatsheet_layout_compute(&la, &a);
    cheatsheet_layout_compute(&lb, &b);
    ASSERT_EQ(la.columns, 2);
    ASSERT_EQ(lb.columns, 2);
    ASSERT_EQ(la.scrolls, 0);
    ASSERT_EQ(lb.scrolls, 0);
    /* Tolerance: 2 px of rounding, plus 1 px per section in the height,
     * because each section's rule is STROKE_HAIRLINE, which ns_type.h
     * keeps unscaled at every DPI. */
    int dw = lb.client_w - 2 * la.client_w;
    int dh = lb.client_h - 2 * la.client_h;
    ASSERT_TRUE(dw >= -2 && dw <= 2);
    ASSERT_TRUE(dh >= -2 - CS_MAX_SECTIONS && dh <= 2 + CS_MAX_SECTIONS);
    ASSERT_EQ(check_geometry(&la), 0);
    ASSERT_EQ(check_geometry(&lb), 0);
    TEST_END();
}

/* The table is written so every action fits 360 px at 96 DPI (spec
 * section 1), measured with the body estimate. */
int test_cheatsheet_layout_every_action_fits_360(void)
{
    TEST_BEGIN();
    static const int dpis[] = { 96, 120, 144, 168, 192 };
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (size_t d = 0; d < sizeof(dpis) / sizeof(dpis[0]); d++) {
        for (size_t i = 0; i < n; i++) {
            int w = est_w(t[i].action, EST_BODY_CHAR_W_96, dpis[d]);
            if (w > ns_scale(360, dpis[d])) {
                printf("  too wide at %d DPI: \"%s\" (%d px)\n", dpis[d], t[i].action, w);
                ASSERT_TRUE(0);
            }
        }
    }
    TEST_END();
}

/* Not an assertion about a number the spec fixes: it reports where two
 * columns stop fitting a 1920 x 1040 work area, so a change to the table
 * that moves the threshold shows up in the test log. */
int test_cheatsheet_layout_reports_where_two_columns_stop_fitting_1080p(void)
{
    TEST_BEGIN();
    int first_one_col = 0;
    for (int dpi = 96; dpi <= 288 && !first_one_col; dpi++) {
        CsInput in;
        CsLayout l;
        fill_input(&in, dpi, 1920, 1040);
        cheatsheet_layout_compute(&l, &in);
        if (l.columns == 1) first_one_col = dpi;
    }
    printf("  [cheatsheet] two columns stop fitting 1920x1040 at %d DPI\n", first_one_col);
    ASSERT_TRUE(first_one_col > 120);      /* 96 and 120 DPI keep two columns */
    TEST_END();
}

/* A work area shorter than the button row still gives a usable window:
 * one line of view, the button inside the client, nothing negative. */
int test_cheatsheet_layout_tiny_work_area_stays_sane(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    fill_input(&in, 192, 640, 120);
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.columns, 1);
    ASSERT_EQ(l.scrolls, 1);
    ASSERT_TRUE(l.view.h >= in.body_h);
    ASSERT_TRUE(l.client_w > 0 && l.client_h > 0);
    ASSERT_TRUE(l.client_w <= 640 - in.nc_w);
    ASSERT_TRUE(inside(l.button, l.client_w, l.client_h));
    ASSERT_TRUE(l.button.w > 0 && l.button.h > 0);
    TEST_END();
}

int test_cheatsheet_layout_degenerate_inputs(void)
{
    TEST_BEGIN();
    CsInput in;
    CsLayout l;
    cheatsheet_layout_compute(NULL, NULL);          /* no crash */
    memset(&in, 0, sizeof(in));
    cheatsheet_layout_compute(NULL, &in);
    cheatsheet_layout_compute(&l, &in);             /* nothing at all */
    ASSERT_EQ(l.n_rows, 0);
    ASSERT_TRUE(l.client_w > 0);
    ASSERT_TRUE(l.client_h > 0);
    in.dpi = -5;                                     /* <= 0 is 96 */
    in.n_sections = 99;                              /* clamped */
    in.section_rows[0] = 1000;                       /* clamped to CS_MAX_ROWS */
    in.body_h = -3;                                  /* clamped to 0 */
    in.work_w = 1920; in.work_h = 1040;
    cheatsheet_layout_compute(&l, &in);
    ASSERT_EQ(l.n_sections, CS_MAX_SECTIONS);
    ASSERT_EQ(l.n_rows, CS_MAX_ROWS);
    ASSERT_TRUE(l.view.h >= 1);
    TEST_END();
}

int test_cheatsheet_scroll_clamp_both_ends(void)
{
    TEST_BEGIN();
    ASSERT_EQ(cheatsheet_scroll_clamp(-10, 1000, 400), 0);
    ASSERT_EQ(cheatsheet_scroll_clamp(0, 1000, 400), 0);
    ASSERT_EQ(cheatsheet_scroll_clamp(250, 1000, 400), 250);
    ASSERT_EQ(cheatsheet_scroll_clamp(600, 1000, 400), 600);
    ASSERT_EQ(cheatsheet_scroll_clamp(601, 1000, 400), 600);
    ASSERT_EQ(cheatsheet_scroll_clamp(99999, 1000, 400), 600);
    ASSERT_EQ(cheatsheet_scroll_clamp(50, 300, 400), 0);   /* page shorter than view */
    ASSERT_EQ(cheatsheet_scroll_clamp(50, 400, 400), 0);
    TEST_END();
}

int test_cheatsheet_place_centres_then_keeps_to_the_work_area(void)
{
    TEST_BEGIN();
    int x = 0, y = 0;
    /* Centred on an owner well inside the work area. */
    cheatsheet_place(100, 100, 1000, 800, 400, 300, 0, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, 400);
    ASSERT_EQ(y, 350);
    /* Owner hanging off the right and bottom edges: pulled back in. */
    cheatsheet_place(1700, 900, 800, 600, 600, 500, 0, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, 1920 - 600);
    ASSERT_EQ(y, 1040 - 500);
    /* Owner on a second monitor left of the primary (negative x). */
    cheatsheet_place(-1900, 50, 800, 600, 600, 500, -1920, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, -1900 + 100);
    ASSERT_EQ(y, 100);
    /* Owner off the top-left: pulled to the work area's corner. */
    cheatsheet_place(-500, -400, 600, 400, 600, 500, 0, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, 0);
    ASSERT_EQ(y, 0);
    /* Window bigger than the work area: its top-left corner wins. */
    cheatsheet_place(0, 0, 800, 600, 3000, 2000, 0, 0, 1920, 1040, &x, &y);
    ASSERT_EQ(x, 0);
    ASSERT_EQ(y, 0);
    /* NULL outputs are allowed. */
    cheatsheet_place(0, 0, 10, 10, 5, 5, 0, 0, 100, 100, NULL, NULL);
    TEST_END();
}

/* A fake measurer for the helper's own tests: one pixel a character, and
 * a per-role bias so a width can be traced to the font role that made it. */
static int fake_measure(void *ctx, NsFontRole role, const char *text)
{
    (void)ctx;
    int bias = role == FONT_HEADING ? 1000 : role == FONT_CAPTION ? 100 : 0;
    return (int)strlen(text) + bias;
}

int test_cheatsheet_input_from_table_fills_the_table_fields(void)
{
    TEST_BEGIN();
    static const Shortcut t[] = {
        { "Alpha", "Ctrl+A",        "Act one",           "",           0u, 0 },
        { "Alpha", "Ctrl+Shift+BB", "Act two",           "when x",     0u, 0 },
        { "Beta",  "F1",            "A much longer act", "",           0u, 0 },
        { "Gamma", "Esc",           "Short",     "a long condition!", 0u, 0 },
        { "Gamma", "Up",            "Short",             "",           0u, 0 },
        { "Gamma", "Down",          "Short",             "",           0u, 0 },
    };
    CsInput in;
    memset(&in, 0, sizeof(in));
    in.dpi = 144;                      /* caller fields are left alone */
    in.body_h = 18;
    in.section_rows[5] = 7;            /* stale table fields are cleared */
    in.has_cond[40] = 1;
    in.chord_w = 9999;
    cheatsheet_input_from_table(&in, t, 6, fake_measure, NULL);
    ASSERT_EQ(in.n_sections, 3);
    ASSERT_EQ(in.section_rows[0], 2);
    ASSERT_EQ(in.section_rows[1], 1);
    ASSERT_EQ(in.section_rows[2], 3);
    ASSERT_EQ(in.section_rows[3], 0);
    ASSERT_EQ(in.section_rows[5], 0);
    ASSERT_EQ((int)in.has_cond[0], 0);
    ASSERT_EQ((int)in.has_cond[1], 1);
    ASSERT_EQ((int)in.has_cond[2], 0);
    ASSERT_EQ((int)in.has_cond[3], 1);
    ASSERT_EQ((int)in.has_cond[40], 0);
    ASSERT_EQ(in.heading_w, 1000 + 5);                 /* FONT_HEADING: "Alpha"/"Gamma" */
    ASSERT_EQ(in.chord_w, (int)strlen("Ctrl+Shift+BB")); /* FONT_BODY */
    ASSERT_EQ(in.text_w, 100 + (int)strlen("a long condition!")); /* FONT_CAPTION wins */
    ASSERT_EQ(in.dpi, 144);
    ASSERT_EQ(in.body_h, 18);
    TEST_END();
}

int test_cheatsheet_input_from_table_stops_at_the_section_capacity(void)
{
    TEST_BEGIN();
    static const char *const names[CS_MAX_SECTIONS + 1] = {
        "S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8"
    };
    Shortcut t[CS_MAX_SECTIONS + 1];
    for (int i = 0; i <= CS_MAX_SECTIONS; i++) {
        Shortcut r = { names[i], "K", "A", "", 0u, 0 };
        t[i] = r;
    }
    CsInput in;
    memset(&in, 0, sizeof(in));
    cheatsheet_input_from_table(&in, t, (size_t)CS_MAX_SECTIONS + 1u,
                                fake_measure, NULL);
    ASSERT_EQ(in.n_sections, CS_MAX_SECTIONS);
    for (int s = 0; s < CS_MAX_SECTIONS; s++) ASSERT_EQ(in.section_rows[s], 1);
    /* Exactly CS_MAX_SECTIONS sections is not over capacity. */
    cheatsheet_input_from_table(&in, t, (size_t)CS_MAX_SECTIONS, fake_measure, NULL);
    ASSERT_EQ(in.n_sections, CS_MAX_SECTIONS);
    /* NULL arguments change nothing and do not crash. */
    cheatsheet_input_from_table(NULL, t, 1, fake_measure, NULL);
    cheatsheet_input_from_table(&in, NULL, 1, fake_measure, NULL);
    cheatsheet_input_from_table(&in, t, 1, NULL, NULL);
    ASSERT_EQ(in.n_sections, CS_MAX_SECTIONS);
    /* An empty table gives an empty input. */
    cheatsheet_input_from_table(&in, t, 0, fake_measure, NULL);
    ASSERT_EQ(in.n_sections, 0);
    TEST_END();
}

int test_cheatsheet_input_from_table_stops_at_the_row_capacity(void)
{
    TEST_BEGIN();
    Shortcut t[CS_MAX_ROWS + 4];
    for (int i = 0; i < CS_MAX_ROWS + 4; i++) {
        Shortcut r = { "Only", "K", "A", "c", 0u, 0 };
        t[i] = r;
    }
    CsInput in;
    memset(&in, 0, sizeof(in));
    cheatsheet_input_from_table(&in, t, (size_t)CS_MAX_ROWS + 4u, fake_measure, NULL);
    ASSERT_EQ(in.n_sections, 1);
    ASSERT_EQ(in.section_rows[0], CS_MAX_ROWS);
    TEST_END();
}
