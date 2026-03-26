#include "test_framework.h"
#include "term.h"
#include <stdlib.h>

// Helper to get a cell from the screen (0-based row/col)
static TermCell get_cell(Terminal *term, int row, int col) {
    // Re-implement logic from parser.c for testing
    int top_logical = (term->lines_count >= term->rows) ? (term->lines_count - term->rows) : 0;
    int logical_idx = top_logical + row;
    int physical_idx = (term->lines_start + logical_idx) % term->lines_capacity;
    return term->lines[physical_idx]->cells[col];
}

int test_term_buffer(void) {
    TEST_BEGIN();
    
    // Init with 24 rows, 80 cols, 5 lines of scrollback
    Terminal *term = term_init(24, 80, 5);
    ASSERT_TRUE(term != NULL);
    ASSERT_EQ(term->rows, 24);
    ASSERT_EQ(term->lines_capacity, 29); // 24 + 5
    ASSERT_EQ(term->lines_count, 24);    // Initial screen
    ASSERT_EQ(term->lines_start, 0);
    
    // Scroll 10 times.
    // First 5 scrolls fill the scrollback buffer (count goes 24 -> 29).
    // Next 5 scrolls recycle lines (start moves 0 -> 5).
    for (int i = 0; i < 10; i++) {
        term_scroll(term);
    }
    
    ASSERT_EQ(term->lines_count, 29); // Capped at capacity
    ASSERT_EQ(term->lines_start, 5);  // Shifted by 5 (10 total - 5 capacity fill)
    
    term_free(term);
    
    TEST_END();
}

int test_term_parser(void) {
    TEST_BEGIN();
    
    Terminal *term = term_init(24, 80, 100);
    
    // 1. Basic text
    term_process(term, "Hello", 5);
    ASSERT_EQ(term->cursor.col, 5);
    ASSERT_EQ(get_cell(term, 0, 0).codepoint, 'H');
    ASSERT_EQ(get_cell(term, 0, 4).codepoint, 'o');
    
    // 2. Newline
    term_process(term, "\r\n", 2);
    ASSERT_EQ(term->cursor.row, 1);
    ASSERT_EQ(term->cursor.col, 0);
    
    // 3. Colors (Red FG)
    term_process(term, "\x1B[31mRed", 8);
    TermCell c = get_cell(term, 1, 0);
    ASSERT_EQ(c.codepoint, 'R');
    ASSERT_EQ(c.attr.fg, 0xCC3333); // Red from palette (index 1)
    
    // 4. Cursor Movement (Up 1)
    term_process(term, "\x1B[A", 3);
    ASSERT_EQ(term->cursor.row, 0);
    
    // 5. Cursor Position (Row 5, Col 10) -> 1-based args
    term_process(term, "\x1B[5;10H", 7);
    ASSERT_EQ(term->cursor.row, 4); // 0-based
    ASSERT_EQ(term->cursor.col, 9); // 0-based
    
    // 6. Erase Display (Clear Screen)
    term_process(term, "\x1B[2J", 4);
    ASSERT_EQ(get_cell(term, 0, 0).codepoint, 0); // Cleared
    ASSERT_EQ(term->cursor.row, 0); // Reset to top-left
    
    // 7. Split Escape Sequence
    term_process(term, "\x1B[3", 3);
    term_process(term, "2mGreen", 7);
    TermCell c2 = get_cell(term, 0, 0); // 'G'
    ASSERT_EQ(c2.codepoint, 'G');
    ASSERT_EQ(c2.attr.fg, 0x4CB84C); // Green (32)

    // 8. Tab Character
    term->cursor.col = 0;
    term_process(term, "\t", 1);
    ASSERT_EQ(term->cursor.col, 8);
    term_process(term, "\t", 1);
    ASSERT_EQ(term->cursor.col, 16);

    // 9. Erase Line (BCE check)
    // Set BG to Red (41)
    term_process(term, "\x1B[41m", 5);
    // Erase Line
    term_process(term, "\x1B[2K", 4);
    // Check cell has Red BG
    TermCell bce_cell = get_cell(term, term->cursor.row, 0);
    ASSERT_EQ(bce_cell.attr.bg, 0xCC3333);

    term_free(term);

    // 10. Scrolling Test
    Terminal *t2 = term_init(3, 10, 2);
    // Write 1, 2, 3
    term_process(t2, "1\r\n2\r\n3", 7);
    // Screen:
    // 1
    // 2
    // 3 (cursor here)
    ASSERT_EQ(get_cell(t2, 0, 0).codepoint, '1');
    ASSERT_EQ(get_cell(t2, 2, 0).codepoint, '3');
    
    // Write 4 -> Scroll
    term_process(t2, "\r\n4", 3);
    // Screen:
    // 2
    // 3
    // 4
    ASSERT_EQ(get_cell(t2, 0, 0).codepoint, '2');
    ASSERT_EQ(get_cell(t2, 2, 0).codepoint, '4');

    // Write 5 -> Scroll
    term_process(t2, "\r\n5", 3);
    ASSERT_EQ(get_cell(t2, 0, 0).codepoint, '3');
    ASSERT_EQ(get_cell(t2, 2, 0).codepoint, '5');

    term_free(t2);

    TEST_END();
}

int test_term_cursor_moves(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    
    // Move to 10, 10
    term_process(t, "\x1B[11;11H", 8);
    ASSERT_EQ(t->cursor.row, 10);
    ASSERT_EQ(t->cursor.col, 10);
    
    // Up 2 (A)
    term_process(t, "\x1B[2A", 4);
    ASSERT_EQ(t->cursor.row, 8);
    
    // Down 1 (B)
    term_process(t, "\x1B[B", 3);
    ASSERT_EQ(t->cursor.row, 9);
    
    // Right 2 (C)
    term_process(t, "\x1B[2C", 4);
    ASSERT_EQ(t->cursor.col, 12);
    
    // Left 1 (D)
    term_process(t, "\x1B[D", 3);
    ASSERT_EQ(t->cursor.col, 11);
    
    // Next Line (E) - down and to col 0
    term_process(t, "\x1B[E", 3);
    ASSERT_EQ(t->cursor.row, 10);
    ASSERT_EQ(t->cursor.col, 0);
    
    // Prev Line (F) - up and to col 0
    term_process(t, "\x1B[F", 3);
    ASSERT_EQ(t->cursor.row, 9);
    ASSERT_EQ(t->cursor.col, 0);
    
    // End (4~) - to end of line (79)
    term_process(t, "\x1B[4~", 4);
    ASSERT_EQ(t->cursor.col, 79);
    
    term_free(t);
    TEST_END();
}

int test_term_extended_moves(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    
    // CHA (G) - Absolute column 5
    term_process(t, "\x1B[5G", 4);
    ASSERT_EQ(t->cursor.col, 4); 

    // VPA (d) - Absolute row 3
    term_process(t, "\x1B[3d", 4);
    ASSERT_EQ(t->cursor.row, 2); 
    
    term_free(t);
    TEST_END();
}

int test_term_sgr_flags(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    
    // Bold
    term_process(t, "\x1B[1m", 4);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_BOLD, TERM_ATTR_BOLD);
    
    // Underline
    term_process(t, "\x1B[4m", 4);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_UNDERLINE, TERM_ATTR_UNDERLINE);
    
    // Reset
    term_process(t, "\x1B[0m", 4);
    ASSERT_EQ(t->current_attr.flags, 0);
    
    term_free(t);
    TEST_END();
}

int test_term_resize_basic(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    
    term_resize(t, 30, 100);
    ASSERT_EQ(t->rows, 30);
    ASSERT_EQ(t->cols, 100);
    
    term_free(t);
    TEST_END();
}

int test_term_resize_reflow(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    
    // Write "123456789012345" (15 chars)
    // Should wrap:
    // Line 0: "1234567890" (wrapped=false)
    // Line 1: "12345"      (wrapped=true)
    term_process(t, "123456789012345", 15);
    
    ASSERT_EQ(get_cell(t, 0, 0).codepoint, '1');
    ASSERT_EQ(get_cell(t, 0, 9).codepoint, '0');
    ASSERT_EQ(get_cell(t, 1, 0).codepoint, '1');
    ASSERT_EQ(get_cell(t, 1, 4).codepoint, '5');
    
    // Resize to 20 cols. Should unwrap to single line.
    term_resize(t, 5, 20);
    ASSERT_EQ(t->cols, 20);
    ASSERT_EQ(get_cell(t, 0, 0).codepoint, '1');
    ASSERT_EQ(get_cell(t, 0, 14).codepoint, '5');
    // Line 1 should be empty/new (codepoint 0)
    ASSERT_EQ(get_cell(t, 1, 0).codepoint, 0);
    
    // Resize to 5 cols. Should wrap to 3 lines.
    term_resize(t, 5, 5);
    ASSERT_EQ(t->cols, 5);
    ASSERT_EQ(get_cell(t, 0, 4).codepoint, '5');
    ASSERT_EQ(get_cell(t, 1, 0).codepoint, '6');
    ASSERT_EQ(get_cell(t, 2, 0).codepoint, '1');
    
    term_free(t);
    TEST_END();
}

int test_term_resize_cursor_edge(void) {
    TEST_BEGIN();
    Terminal *t = term_init(10, 10, 100);
    
    // Write 10 chars "0123456789". Cursor should be at col 10.
    term_process(t, "0123456789", 10);
    
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 10);
    
    // Resize to 20 cols. Cursor should remain at col 10.
    term_resize(t, 10, 20);
    ASSERT_EQ(t->cursor.row, 0);
    ASSERT_EQ(t->cursor.col, 10);
    
    // Resize to 5 cols. Should wrap to 3 lines. Cursor at row 2, col 0.
    term_resize(t, 10, 5);
    ASSERT_EQ(t->cursor.row, 2);
    ASSERT_EQ(t->cursor.col, 0);
    
    term_free(t);
    TEST_END();
}

int test_term_utf8(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    // Euro sign €: E2 82 AC
    term_process(t, "\xE2\x82\xAC", 3);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0x20AC);

    term_free(t);
    TEST_END();
}

int test_term_sgr_bold_off(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\x1B[1m", 4);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_BOLD, TERM_ATTR_BOLD);

    term_process(t, "\x1B[22m", 5);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_BOLD, 0);
    ASSERT_EQ(t->current_attr.flags, 0);

    term_free(t);
    TEST_END();
}

int test_term_sgr_underline_off(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\x1B[4m", 4);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_UNDERLINE, TERM_ATTR_UNDERLINE);

    term_process(t, "\x1B[24m", 5);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_UNDERLINE, 0);
    ASSERT_EQ(t->current_attr.flags, 0);

    term_free(t);
    TEST_END();
}

int test_term_sgr_blink_off(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\x1B[5m", 4);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_BLINK, TERM_ATTR_BLINK);

    term_process(t, "\x1B[25m", 5);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_BLINK, 0);
    ASSERT_EQ(t->current_attr.flags, 0);

    term_free(t);
    TEST_END();
}

int test_term_sgr_reverse_off(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\x1B[7m", 4);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_REVERSE, TERM_ATTR_REVERSE);

    term_process(t, "\x1B[27m", 5);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_REVERSE, 0);
    ASSERT_EQ(t->current_attr.flags, 0);

    term_free(t);
    TEST_END();
}

int test_term_sgr_selective_off(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* Set bold + underline */
    term_process(t, "\x1B[1;4m", 6);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_BOLD, TERM_ATTR_BOLD);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_UNDERLINE, TERM_ATTR_UNDERLINE);

    /* Turn off bold only — underline should remain */
    term_process(t, "\x1B[22m", 5);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_BOLD, 0);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_UNDERLINE, TERM_ATTR_UNDERLINE);

    /* Turn off underline — flags should be zero */
    term_process(t, "\x1B[24m", 5);
    ASSERT_EQ(t->current_attr.flags, 0);

    term_free(t);
    TEST_END();
}

int test_term_sgr_reverse_off_man_scenario(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* Simulate less: reverse on, write text, reverse off, write text */
    term_process(t, "\x1B[7m" "standout" "\x1B[27m" "normal", 23);

    /* 's' at col 0 should have reverse set */
    TermCell s_cell = get_cell(t, 0, 0);
    ASSERT_EQ(s_cell.attr.flags & TERM_ATTR_REVERSE, TERM_ATTR_REVERSE);

    /* 'n' at col 8 should NOT have reverse */
    TermCell n_cell = get_cell(t, 0, 8);
    ASSERT_EQ(n_cell.attr.flags & TERM_ATTR_REVERSE, 0);

    term_free(t);
    TEST_END();
}

int test_term_sgr_reset_after_turnoff(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\x1B[7m", 4);
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_REVERSE, TERM_ATTR_REVERSE);

    term_process(t, "\x1B[27m", 5);
    ASSERT_EQ(t->current_attr.flags, 0);

    /* Full reset on already-cleared flags is a no-op */
    term_process(t, "\x1B[0m", 4);
    ASSERT_EQ(t->current_attr.flags, 0);

    term_free(t);
    TEST_END();
}