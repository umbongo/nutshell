#include "test_framework.h"
#include "term.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TMP_CFG_CC "/tmp/nutshell_test_cc.json"

static void feed(Terminal *t, const char *s) {
    term_process(t, s, strlen(s));
}

/* Helper: get a visible screen row from the terminal */
static TermRow *screen_row(Terminal *t, int row) {
    int total = t->lines_count;
    int top = (total >= t->rows) ? (total - t->rows) : 0;
    int logical = top + row;
    if (logical >= total) return NULL;
    int phys = (t->lines_start + logical) % t->lines_capacity;
    return t->lines[phys];
}

/* ---- Default colours are Pure Light ------------------------------------ */

int test_cc_default_is_pure_light(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(s.foreground_colour, "#E0E0E0");
    ASSERT_STR_EQ(s.background_colour, "#121212");
    ASSERT_STR_EQ(s.colour_scheme, "Onyx Synapse");
    TEST_END();
}

/* ---- Terminal init cells use COLOR_DEFAULT ------------------------------ */

int test_cc_init_cells_color_default(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    TermRow *r = screen_row(t, 0);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ((int)r->cells[0].attr.fg_mode, (int)COLOR_DEFAULT);
    ASSERT_EQ((int)r->cells[0].attr.bg_mode, (int)COLOR_DEFAULT);
    /* Check last cell too */
    ASSERT_EQ((int)r->cells[9].attr.fg_mode, (int)COLOR_DEFAULT);
    ASSERT_EQ((int)r->cells[9].attr.bg_mode, (int)COLOR_DEFAULT);
    term_free(t);
    TEST_END();
}

/* ---- After resize, new cells use COLOR_DEFAULT ------------------------- */

int test_cc_resize_preserves_color_default(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    /* Fill all rows so lines_count >= rows after resize */
    feed(t, "L1\nL2\nL3\nL4");
    term_resize(t, 6, 20);
    /* Check visible rows — new expanded cells should be COLOR_DEFAULT */
    for (int row = 0; row < t->rows; row++) {
        TermRow *r = screen_row(t, row);
        if (!r) continue;
        /* Check a cell in the expanded column area */
        ASSERT_EQ((int)r->cells[t->cols - 1].attr.fg_mode, (int)COLOR_DEFAULT);
        ASSERT_EQ((int)r->cells[t->cols - 1].attr.bg_mode, (int)COLOR_DEFAULT);
    }
    term_free(t);
    TEST_END();
}

/* ---- After resize smaller then larger, cells stay COLOR_DEFAULT -------- */

int test_cc_resize_shrink_grow(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(10, 40, 0);
    /* Fill enough rows to have content */
    for (int i = 0; i < 10; i++) feed(t, "Line\n");
    term_resize(t, 5, 20);
    term_resize(t, 10, 40);
    for (int row = 0; row < t->rows; row++) {
        TermRow *r = screen_row(t, row);
        if (!r) continue;
        /* Check a cell beyond content */
        ASSERT_EQ((int)r->cells[t->cols - 1].attr.fg_mode, (int)COLOR_DEFAULT);
        ASSERT_EQ((int)r->cells[t->cols - 1].attr.bg_mode, (int)COLOR_DEFAULT);
    }
    term_free(t);
    TEST_END();
}

/* ---- After scroll, new row uses COLOR_DEFAULT -------------------------- */

int test_cc_scroll_new_row_color_default(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    /* Fill all rows then scroll to create a new one */
    feed(t, "A\nB\nC\nD\n");
    TermRow *last = screen_row(t, 3);
    ASSERT_NOT_NULL(last);
    /* The new (scrolled) row should have COLOR_DEFAULT cells */
    for (int c = 0; c < t->cols; c++) {
        ASSERT_EQ((int)last->cells[c].attr.fg_mode, (int)COLOR_DEFAULT);
        ASSERT_EQ((int)last->cells[c].attr.bg_mode, (int)COLOR_DEFAULT);
    }
    term_free(t);
    TEST_END();
}

/* ---- ED2 (clear screen) resets cells to COLOR_DEFAULT ------------------ */

int test_cc_erase_display_color_default(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    /* Write coloured text first */
    feed(t, "\033[31mRed text\033[0m");
    /* Clear entire screen */
    feed(t, "\033[2J");
    for (int row = 0; row < t->rows; row++) {
        TermRow *r = screen_row(t, row);
        ASSERT_NOT_NULL(r);
        for (int c = 0; c < t->cols; c++) {
            ASSERT_EQ((int)r->cells[c].attr.fg_mode, (int)COLOR_DEFAULT);
            ASSERT_EQ((int)r->cells[c].attr.bg_mode, (int)COLOR_DEFAULT);
        }
    }
    term_free(t);
    TEST_END();
}

/* ---- EL (erase line) resets cells to COLOR_DEFAULT --------------------- */

int test_cc_erase_line_color_default(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "\033[32mGreen\033[0m");
    feed(t, "\033[1;1H"); /* cursor home */
    feed(t, "\033[K");     /* erase to end of line */
    TermRow *r = screen_row(t, 0);
    ASSERT_NOT_NULL(r);
    for (int c = 0; c < t->cols; c++) {
        ASSERT_EQ((int)r->cells[c].attr.fg_mode, (int)COLOR_DEFAULT);
        ASSERT_EQ((int)r->cells[c].attr.bg_mode, (int)COLOR_DEFAULT);
    }
    term_free(t);
    TEST_END();
}

/* ---- Alt screen enter: fresh cells are COLOR_DEFAULT ------------------- */

int test_cc_alt_screen_color_default(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "\033[31mColored\033[0m");
    term_alt_screen_enter(t);
    for (int row = 0; row < t->rows; row++) {
        TermRow *r = screen_row(t, row);
        ASSERT_NOT_NULL(r);
        for (int c = 0; c < t->cols; c++) {
            ASSERT_EQ((int)r->cells[c].attr.fg_mode, (int)COLOR_DEFAULT);
            ASSERT_EQ((int)r->cells[c].attr.bg_mode, (int)COLOR_DEFAULT);
        }
    }
    term_alt_screen_exit(t);
    term_free(t);
    TEST_END();
}

/* ---- SGR reset restores COLOR_DEFAULT for new chars -------------------- */

int test_cc_sgr_reset_color_default(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "\033[38;2;255;0;0mR\033[0mN");
    TermRow *r = screen_row(t, 0);
    ASSERT_NOT_NULL(r);
    /* Cell 0 = 'R' with RGB colour */
    ASSERT_EQ((int)r->cells[0].attr.fg_mode, (int)COLOR_RGB);
    /* Cell 1 = 'N' after reset — should be COLOR_DEFAULT */
    ASSERT_EQ((int)r->cells[1].attr.fg_mode, (int)COLOR_DEFAULT);
    ASSERT_EQ((int)r->cells[1].attr.bg_mode, (int)COLOR_DEFAULT);
    term_free(t);
    TEST_END();
}

/* ---- Config roundtrip preserves Pure Light colours --------------------- */

int test_cc_config_roundtrip_pure_light(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    ASSERT_STR_EQ(cfg->settings.foreground_colour, "#E0E0E0");
    ASSERT_STR_EQ(cfg->settings.background_colour, "#121212");
    ASSERT_STR_EQ(cfg->settings.colour_scheme, "Onyx Synapse");

    int rc = config_save(cfg, TMP_CFG_CC);
    ASSERT_EQ(rc, 0);

    Config *loaded = config_load(TMP_CFG_CC);
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(loaded->settings.foreground_colour, "#E0E0E0");
    ASSERT_STR_EQ(loaded->settings.background_colour, "#121212");
    ASSERT_STR_EQ(loaded->settings.colour_scheme, "Onyx Synapse");

    config_free(cfg);
    config_free(loaded);
    remove(TMP_CFG_CC);
    TEST_END();
}

/* ---- Multiple resizes don't corrupt colour mode ------------------------ */

int test_cc_repeated_resize(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Fill rows so lines_count covers the screen */
    for (int i = 0; i < 24; i++) feed(t, "X\n");
    /* Resize through several sizes */
    term_resize(t, 12, 40);
    term_resize(t, 30, 100);
    term_resize(t, 24, 80);
    /* All visible cells should still be COLOR_DEFAULT */
    for (int row = 0; row < t->rows; row++) {
        TermRow *r = screen_row(t, row);
        if (!r) continue;
        for (int c = 0; c < t->cols; c++) {
            ASSERT_EQ((int)r->cells[c].attr.fg_mode, (int)COLOR_DEFAULT);
            ASSERT_EQ((int)r->cells[c].attr.bg_mode, (int)COLOR_DEFAULT);
        }
    }
    term_free(t);
    TEST_END();
}

/* ---- Scrollback + resize preserves COLOR_DEFAULT ----------------------- */

int test_cc_scrollback_resize(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 50);
    /* Generate scrollback */
    for (int i = 0; i < 20; i++) {
        char line[16];
        snprintf(line, sizeof(line), "Line%d\n", i);
        feed(t, line);
    }
    term_resize(t, 6, 15);
    /* Check all visible rows */
    for (int row = 0; row < t->rows; row++) {
        TermRow *r = screen_row(t, row);
        if (!r) continue;
        ASSERT_EQ((int)r->cells[t->cols - 1].attr.fg_mode, (int)COLOR_DEFAULT);
        ASSERT_EQ((int)r->cells[t->cols - 1].attr.bg_mode, (int)COLOR_DEFAULT);
    }
    term_free(t);
    TEST_END();
}

/* ---- Write after resize uses COLOR_DEFAULT ----------------------------- */

int test_cc_write_after_resize(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    /* Fill rows so resize has content */
    feed(t, "A\nB\nC\nD");
    term_resize(t, 6, 20);
    /* Move cursor to a known position and write */
    feed(t, "\033[1;1H");  /* cursor home */
    feed(t, "After resize");
    TermRow *r = screen_row(t, 0);
    if (r) {
        /* 'A' should be COLOR_DEFAULT (no SGR was set) */
        ASSERT_EQ((int)r->cells[0].attr.fg_mode, (int)COLOR_DEFAULT);
        ASSERT_EQ((int)r->cells[0].attr.bg_mode, (int)COLOR_DEFAULT);
    }
    term_free(t);
    TEST_END();
}
