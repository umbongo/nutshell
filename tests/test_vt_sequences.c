#include "test_framework.h"
#include "term.h"
#include <string.h>

/* ---- OSC title (window title) -------------------------------------------- */

int test_vt_osc_title_0(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "\033]0;My Title\007", 14);
    ASSERT_STR_EQ(t->title, "My Title");
    term_free(t);
    TEST_END();
}

int test_vt_osc_title_2(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "\033]2;Other Title\007", 16);
    ASSERT_STR_EQ(t->title, "Other Title");
    term_free(t);
    TEST_END();
}

/* OSC with ESC \ (ST) terminator instead of BEL */
int test_vt_osc_st_terminator(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* \033]0;Title\033\ — ST = ESC backslash */
    term_process(t, "\033]0;Title\033\\", 11);
    ASSERT_STR_EQ(t->title, "Title");
    term_free(t);
    TEST_END();
}

/* ---- DECTCEM cursor visibility ------------------------------------------- */

int test_vt_dectcem_show(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Hide, then show */
    term_process(t, "\033[?25l", 6);
    ASSERT_TRUE(t->cursor.visible == false);
    term_process(t, "\033[?25h", 6);
    ASSERT_TRUE(t->cursor.visible == true);
    term_free(t);
    TEST_END();
}

int test_vt_dectcem_hide(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    ASSERT_TRUE(t->cursor.visible == true); /* default */
    term_process(t, "\033[?25l", 6);
    ASSERT_TRUE(t->cursor.visible == false);
    term_free(t);
    TEST_END();
}

/* ---- Alternate screen buffer --------------------------------------------- */

int test_vt_alt_screen_enter(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    ASSERT_TRUE(t->alt_screen_active == false);
    term_process(t, "\033[?1049h", 8);
    ASSERT_TRUE(t->alt_screen_active == true);
    term_free(t);
    TEST_END();
}

int test_vt_alt_screen_exit(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Write to primary screen */
    term_process(t, "HELLO", 5);
    /* Enter alt screen and clear it */
    term_process(t, "\033[?1049h", 8);
    term_process(t, "\033[2J", 4); /* clear alt screen */
    /* Exit — primary restored */
    term_process(t, "\033[?1049l", 8);
    ASSERT_TRUE(t->alt_screen_active == false);
    /* Primary screen first cell should still be 'H' */
    TermRow *row0 = t->lines[(t->lines_start +
                               ((t->lines_count >= t->rows) ?
                                (t->lines_count - t->rows) : 0))
                              % t->lines_capacity];
    ASSERT_TRUE(row0->cells[0].codepoint == 'H');
    term_free(t);
    TEST_END();
}

/* ---- Application cursor keys --------------------------------------------- */

int test_vt_app_cursor_enable(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    ASSERT_TRUE(t->app_cursor_keys == false);
    term_process(t, "\033[?1h", 5);
    ASSERT_TRUE(t->app_cursor_keys == true);
    term_free(t);
    TEST_END();
}

int test_vt_app_cursor_disable(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "\033[?1h", 5);
    term_process(t, "\033[?1l", 5);
    ASSERT_TRUE(t->app_cursor_keys == false);
    term_free(t);
    TEST_END();
}

/* ---- Insert mode --------------------------------------------------------- */

int test_vt_insert_mode_enable(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Write "AB" at columns 0,1 */
    term_process(t, "AB", 2);
    /* Home cursor to col 0, enable insert mode, write 'X' */
    term_process(t, "\033[1;1H", 6); /* CUP row=1 col=1 */
    term_process(t, "\033[4h", 4);   /* insert mode on */
    term_process(t, "X", 1);
    /* Now col 0 = 'X', col 1 = 'A', col 2 = 'B' */
    TermRow *row = t->lines[(t->lines_start +
                              ((t->lines_count >= t->rows) ?
                               (t->lines_count - t->rows) : 0))
                             % t->lines_capacity];
    ASSERT_TRUE(row->cells[0].codepoint == 'X');
    ASSERT_TRUE(row->cells[1].codepoint == 'A');
    ASSERT_TRUE(row->cells[2].codepoint == 'B');
    term_free(t);
    TEST_END();
}

int test_vt_insert_mode_disable(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "AB", 2);
    term_process(t, "\033[1;1H", 6);
    term_process(t, "\033[4l", 4); /* replace mode (default) */
    term_process(t, "X", 1);
    /* Replace mode: col 0 = 'X', col 1 = 'B' is untouched */
    TermRow *row = t->lines[(t->lines_start +
                              ((t->lines_count >= t->rows) ?
                               (t->lines_count - t->rows) : 0))
                             % t->lines_capacity];
    ASSERT_TRUE(row->cells[0].codepoint == 'X');
    ASSERT_TRUE(row->cells[1].codepoint == 'B');
    term_free(t);
    TEST_END();
}

/* ---- Negative / edge cases ----------------------------------------------- */

int test_vt_osc_no_terminator(void)
{
    TEST_BEGIN();
    /* No BEL or ST — title should not be set, no crash */
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "\033]0;Unterminated", 16);
    /* title unchanged (empty) */
    ASSERT_STR_EQ(t->title, "");
    term_free(t);
    TEST_END();
}

int test_vt_unknown_private_mode(void)
{
    TEST_BEGIN();
    /* Unknown private mode — must not crash */
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "\033[?999h", 7);
    ASSERT_TRUE(1); /* no crash */
    term_free(t);
    TEST_END();
}

/* ---- Corner cases -------------------------------------------------------- */

int test_vt_osc_special_chars(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "\033]0;Title: <test> & 'quotes'\007", 29);
    ASSERT_STR_EQ(t->title, "Title: <test> & 'quotes'");
    term_free(t);
    TEST_END();
}

int test_vt_alt_screen_isolation(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Write to primary */
    term_process(t, "PRIMARY", 7);
    /* Enter alt screen, write something different */
    term_process(t, "\033[?1049h", 8);
    term_process(t, "\033[1;1H", 6);
    term_process(t, "ALTDATA", 7);
    /* Exit alt screen */
    term_process(t, "\033[?1049l", 8);
    /* Primary first-row first cell should still be 'P' */
    int top = (t->lines_count >= t->rows) ? (t->lines_count - t->rows) : 0;
    TermRow *row = t->lines[(t->lines_start + top) % t->lines_capacity];
    ASSERT_TRUE(row->cells[0].codepoint == 'P');
    ASSERT_TRUE(row->cells[1].codepoint == 'R');
    term_free(t);
    TEST_END();
}

/* Regression: man/less sets SGR attrs in alt screen; after exit, current_attr
 * must be reset to defaults so new text doesn't inherit the alt screen colors. */
int test_vt_alt_screen_resets_attr(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Enter alt screen */
    term_process(t, "\033[?1049h", 8);
    /* Set a background color (SGR 42 = green bg) */
    term_process(t, "\033[42m", 5);
    ASSERT_TRUE(t->current_attr.bg_mode != COLOR_DEFAULT);
    /* Exit alt screen */
    term_process(t, "\033[?1049l", 8);
    /* current_attr should be reset to defaults */
    ASSERT_EQ(t->current_attr.bg_mode, COLOR_DEFAULT);
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_DEFAULT);
    ASSERT_EQ(t->current_attr.flags, 0);
    term_free(t);
    TEST_END();
}
