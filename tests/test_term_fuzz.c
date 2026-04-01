#include "test_framework.h"
#include "term.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Terminal parser robustness / fuzz-style tests
 *
 * These exercise malformed escape sequences, boundary conditions,
 * and untrusted-input scenarios that a remote SSH host could send.
 * ============================================================ */

/* Helper: get cell from visible screen row/col */
static TermCell fuzz_get_cell(Terminal *term, int row, int col)
{
    int top = (term->lines_count >= term->rows)
            ? (term->lines_count - term->rows) : 0;
    int logical = top + row;
    int physical = (term->lines_start + logical) % term->lines_capacity;
    return term->lines[physical]->cells[col];
}

/* --- Incomplete / truncated escape sequences --- */

int test_term_fuzz_incomplete_esc(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* lone ESC at end of stream */
    term_process(t, "\x1B", 1);
    /* follow with normal text — ESC + 'H' may be consumed as a valid
     * escape (e.g., HTS).  The key assertion is no crash and recovery. */
    term_process(t, "Hello", 5);
    /* parser must recover and write remaining text without crashing */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_term_fuzz_incomplete_csi(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* ESC [ with no final byte */
    term_process(t, "\x1B[", 2);
    /* send text — parser should recover to normal state */
    term_process(t, "AB", 2);
    /* exact behavior varies but must not crash */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_term_fuzz_csi_no_params(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* CSI with final byte but no params — many commands default to 1 */
    term_process(t, "Hello", 5);
    ASSERT_EQ(t->cursor.col, 5);
    term_process(t, "\x1B[D", 3); /* cursor left, default 1 */
    ASSERT_EQ(t->cursor.col, 4);
    term_free(t);
    TEST_END();
}

/* --- Overflow-probing CSI parameter counts --- */

int test_term_fuzz_csi_many_params(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Send CSI with 32 parameters (more than TERM_MAX_CSI_PARAMS=16) */
    term_process(t, "\x1B[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18;19;20;21;22;23;24;25;26;27;28;29;30;31;32m", 89);
    /* must not crash — extra params ignored */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_term_fuzz_csi_huge_param_value(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Giant parameter value — cursor move with absurd count */
    term_process(t, "\x1B[99999H", 8);
    /* cursor should be clamped to screen bounds */
    ASSERT_TRUE(t->cursor.row >= 0 && t->cursor.row < t->rows);
    ASSERT_TRUE(t->cursor.col >= 0 && t->cursor.col < t->cols);
    term_free(t);
    TEST_END();
}

int test_term_fuzz_csi_zero_param(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* CSI 0 H — should go to row 0, col 0 (CUP treats 0 as 1) */
    term_process(t, "\x1B[5;5H", 6); /* move to (4,4) */
    term_process(t, "\x1B[0;0H", 6); /* 0;0 = 1;1 = (0,0) */
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 0);
    term_free(t);
    TEST_END();
}

/* --- Malformed OSC sequences --- */

int test_term_fuzz_osc_unterminated(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* OSC without ST terminator — send partial, then normal text */
    term_process(t, "\x1B]0;My Title", 12);
    /* BEL terminates OSC in practice */
    term_process(t, "\x07", 1);
    ASSERT_STR_EQ(t->title, "My Title");
    term_free(t);
    TEST_END();
}

int test_term_fuzz_osc_very_long_title(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Title longer than osc_buffer (256 bytes) */
    char buf[512];
    memcpy(buf, "\x1B]0;", 4);
    memset(buf + 4, 'X', 300);
    buf[304] = '\x07';
    buf[305] = '\0';
    term_process(t, buf, 305);
    /* title should be truncated, not overflow */
    ASSERT_TRUE(strlen(t->title) < 256);
    term_free(t);
    TEST_END();
}

int test_term_fuzz_osc_empty_title(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Set title then clear it */
    term_process(t, "\x1B]0;Hello\x07", 10);
    ASSERT_STR_EQ(t->title, "Hello");
    term_process(t, "\x1B]0;\x07", 5);
    ASSERT_STR_EQ(t->title, "");
    term_free(t);
    TEST_END();
}

/* --- UTF-8 edge cases from remote host --- */

int test_term_fuzz_utf8_overlong(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Overlong encoding of '/' (U+002F): C0 AF */
    term_process(t, "\xC0\xAF", 2);
    /* must not crash; may render replacement char or skip */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_term_fuzz_utf8_truncated(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Start of 3-byte sequence, only send 2 bytes, then ASCII */
    term_process(t, "\xE2\x82", 2);
    term_process(t, "OK", 2);
    /* parser should recover; "OK" should appear */
    /* exact cell position depends on how invalid bytes are handled */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_term_fuzz_utf8_4byte(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* U+1F600 grinning face: F0 9F 98 80 */
    term_process(t, "\xF0\x9F\x98\x80", 4);
    ASSERT_EQ(fuzz_get_cell(t, 0, 0).codepoint, 0x1F600);
    term_free(t);
    TEST_END();
}

int test_term_fuzz_utf8_invalid_continuation(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Start 2-byte seq (C3), follow with non-continuation byte */
    term_process(t, "\xC3" "A", 2);
    /* must not crash */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

/* --- Rapid state transitions --- */

int test_term_fuzz_rapid_alt_screen(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Toggle alt screen rapidly */
    for (int i = 0; i < 50; i++) {
        term_process(t, "\x1B[?1049h", 8); /* enter alt screen */
        term_process(t, "\x1B[?1049l", 8); /* exit alt screen */
    }
    ASSERT_FALSE(t->alt_screen_active);
    term_free(t);
    TEST_END();
}

int test_term_fuzz_double_alt_screen_enter(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "primary", 7);
    term_process(t, "\x1B[?1049h", 8);
    term_process(t, "alt", 3);
    /* enter again without exiting */
    term_process(t, "\x1B[?1049h", 8);
    /* exit once */
    term_process(t, "\x1B[?1049l", 8);
    /* should be back on primary */
    ASSERT_FALSE(t->alt_screen_active);
    term_free(t);
    TEST_END();
}

/* --- Scroll region abuse --- */

int test_term_fuzz_scroll_region_inverted(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Set scroll region with top > bottom */
    term_process(t, "\x1B[20;5r", 7);
    /* should be rejected or clamped; must not crash */
    term_process(t, "text\r\n", 6);
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_term_fuzz_scroll_region_zero(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* DECSTBM with 0;0 means full screen */
    term_process(t, "\x1B[0;0r", 6);
    ASSERT_EQ(t->scroll_top, 0);
    ASSERT_EQ(t->scroll_bot, 23);
    term_free(t);
    TEST_END();
}

int test_term_fuzz_scroll_region_single_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Scroll region of exactly one row */
    term_process(t, "\x1B[5;5r", 6);
    /* should be valid; writing and scrolling must not crash */
    term_process(t, "x\r\n", 3);
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

/* --- Erase operations at boundaries --- */

int test_term_fuzz_erase_at_origin(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Erase in line at (0,0) — edge case for cursor-relative erase */
    term_process(t, "\x1B[K", 3);  /* erase to end of line */
    term_process(t, "\x1B[1K", 4); /* erase to start of line */
    term_process(t, "\x1B[2K", 4); /* erase entire line */
    /* must not crash */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_term_fuzz_erase_display_modes(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "fill", 4);
    term_process(t, "\x1B[J", 3);  /* erase below */
    term_process(t, "\x1B[1J", 4); /* erase above */
    term_process(t, "\x1B[2J", 4); /* erase all */
    term_process(t, "\x1B[3J", 4); /* erase scrollback (xterm ext) */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

/* --- Binary / control character flood --- */

int test_term_fuzz_all_control_chars(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Send every byte 0x00-0x1F except ESC */
    char buf[31];
    int pos = 0;
    for (int c = 0; c < 0x20; c++) {
        if (c == 0x1B) continue; /* skip ESC */
        buf[pos++] = (char)c;
    }
    term_process(t, buf, (size_t)pos);
    /* must not crash */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_term_fuzz_binary_garbage(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Random-looking high bytes */
    char buf[256];
    for (int i = 0; i < 256; i++) {
        buf[i] = (char)(i ^ 0xA5);
    }
    term_process(t, buf, 256);
    /* must not crash */
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

/* --- Resize during special states --- */

int test_term_fuzz_resize_during_alt_screen(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "\x1B[?1049h", 8);
    term_resize(t, 40, 120);
    ASSERT_EQ(t->rows, 40);
    ASSERT_EQ(t->cols, 120);
    term_process(t, "\x1B[?1049l", 8);
    term_free(t);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_term_fuzz_resize_tiny(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "Hello World", 11);
    term_resize(t, 1, 1);
    ASSERT_EQ(t->rows, 1);
    ASSERT_EQ(t->cols, 1);
    /* cursor must be within bounds */
    ASSERT_TRUE(t->cursor.row >= 0 && t->cursor.row < t->rows);
    ASSERT_TRUE(t->cursor.col >= 0 && t->cursor.col <= t->cols);
    term_free(t);
    TEST_END();
}

int test_term_fuzz_resize_same_size(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "Hello", 5);
    term_resize(t, 24, 80);
    ASSERT_EQ(fuzz_get_cell(t, 0, 0).codepoint, 'H');
    term_free(t);
    TEST_END();
}

/* --- Wrapping edge cases --- */

int test_term_fuzz_write_at_last_column(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 10, 100);
    /* Fill exactly 10 chars — cursor at col 10 (past end) */
    term_process(t, "0123456789", 10);
    ASSERT_EQ(t->cursor.col, 10);
    /* Next char should wrap to line 1 */
    term_process(t, "A", 1);
    ASSERT_EQ(t->cursor.row, 1);
    ASSERT_EQ(fuzz_get_cell(t, 1, 0).codepoint, 'A');
    term_free(t);
    TEST_END();
}

int test_term_fuzz_cr_at_last_column(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 10, 100);
    term_process(t, "0123456789", 10);
    /* CR should go to col 0, same row */
    term_process(t, "\r", 1);
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 0);
    term_free(t);
    TEST_END();
}

/* --- Split escape across process calls --- */

int test_term_fuzz_split_csi_across_calls(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Send SGR red in 3 separate chunks */
    term_process(t, "\x1B", 1);
    term_process(t, "[3", 2);
    term_process(t, "1m", 2);
    term_process(t, "R", 1);
    TermCell c = fuzz_get_cell(t, 0, 0);
    ASSERT_EQ(c.codepoint, 'R');
    ASSERT_EQ(c.attr.fg, 0xCC3333);
    term_free(t);
    TEST_END();
}

int test_term_fuzz_split_osc_across_calls(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "\x1B]", 2);
    term_process(t, "0;Ti", 4);
    term_process(t, "tle\x07", 4);
    ASSERT_STR_EQ(t->title, "Title");
    term_free(t);
    TEST_END();
}
