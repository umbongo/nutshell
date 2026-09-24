#include "test_framework.h"
#include "term.h"
#include <stdlib.h>
#include <stdio.h>

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
    // VT100/xterm: ESC[2J clears screen but does NOT move cursor.
    // Cursor stays at row=4,col=9 (from step 5's ESC[5;10H).
    term_process(term, "\x1B[2J", 4);
    ASSERT_EQ(get_cell(term, 0, 0).codepoint, 0); // Cleared
    ASSERT_EQ(term->cursor.row, 4); // Cursor unchanged (was at row 4 from step 5)
    ASSERT_EQ(term->cursor.col, 9); // Cursor unchanged (was at col 9 from step 5)

    // 7. Split Escape Sequence — move cursor home first so the 'G' lands at (0,0)
    term_process(term, "\x1B[H", 3); // ESC[H = cursor home
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

/* ---- Hardening: UTF-8 decoder corner cases ------------------------------ */

/* Overlong 2-byte encoding of NUL (C0 80): both bytes are individually
 * invalid lead bytes (0xC0/0xC1 never start a valid sequence), so each
 * gets its own replacement character. */
int test_term_utf8_reject_overlong_2byte(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xC0\x80" "A", 3);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 1).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 2).codepoint, (uint32_t)'A');

    term_free(t);
    TEST_END();
}

/* Overlong 3-byte encoding (E0 80 80 would be U+0000): the lead byte's
 * maximal subpart is itself alone (second byte out of the E0-specific
 * A0-BF range), then each remaining byte is its own stray continuation. */
int test_term_utf8_reject_overlong_3byte(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xE0\x80\x80", 3);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 1).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 2).codepoint, 0xFFFDu);

    term_free(t);
    TEST_END();
}

/* Classic surrogate-encoding attack: ED A0 80 would encode U+D800 (a UTF-16
 * high surrogate) if the second-byte range weren't restricted to 80-9F for
 * lead byte ED. */
int test_term_utf8_reject_surrogate(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xED\xA0\x80" "A", 4);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 1).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 2).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 3).codepoint, (uint32_t)'A');

    term_free(t);
    TEST_END();
}

/* U+D7FF is the highest codepoint before the surrogate range -- must still
 * decode normally. */
int test_term_utf8_surrogate_boundary_valid(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xED\x9F\xBF", 3);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0xD7FFu);

    term_free(t);
    TEST_END();
}

/* U+10FFFF is the highest valid Unicode codepoint -- must still decode. */
int test_term_utf8_max_codepoint_valid(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xF4\x8F\xBF\xBF", 4);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0x10FFFFu);

    term_free(t);
    TEST_END();
}

/* One past the maximum: F4 90 80 80 would encode U+110000. The lead byte's
 * maximal subpart is itself alone, then three more stray bytes. */
int test_term_utf8_reject_above_max_codepoint(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xF4\x90\x80\x80" "A", 5);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 1).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 2).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 3).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 4).codepoint, (uint32_t)'A');

    term_free(t);
    TEST_END();
}

/* A 3-byte sequence cut short by an ASCII byte: the two consumed bytes
 * collapse into ONE replacement character (a single maximal subpart), not
 * one per byte -- distinct from the stray-continuation-byte cases above. */
int test_term_utf8_truncated_by_ascii(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xE2\x82X", 3);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 1).codepoint, (uint32_t)'X');

    term_free(t);
    TEST_END();
}

/* An unexpected lone continuation byte with no pending sequence at all. */
int test_term_utf8_lone_continuation_byte(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\x80" "A", 2);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0xFFFDu);
    ASSERT_EQ(get_cell(t, 0, 1).codepoint, (uint32_t)'A');

    term_free(t);
    TEST_END();
}

/* A sequence genuinely truncated at the end of the buffer (more bytes due
 * later) must not emit anything early, and must leave the decoder state
 * pending rather than resetting it. */
int test_term_utf8_incomplete_stays_pending(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xE2\x82", 2);

    ASSERT_EQ(t->utf8_remaining, 1);
    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0u);

    /* Completing it later in a second term_process() call still works. */
    term_process(t, "\xAC", 1);
    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0x20ACu);

    term_free(t);
    TEST_END();
}

/* Hardening: a pending sequence must not survive an intervening escape
 * sequence. Two bytes of a 3-byte euro sign, then a complete (and
 * otherwise harmless) SGR escape sequence, then the byte that would have
 * completed the euro sign had it not been interrupted. Before the fix this
 * byte (0x80, within the generic 0x80-0xBF continuation range the pending
 * state was left in) silently completed a codepoint built from bytes on
 * both sides of the escape sequence; after the fix the pending sequence is
 * flushed as one replacement character the moment ESC arrives, the escape
 * sequence is handled normally, and the leftover 0x80 is then decoded
 * fresh as its own (stray-continuation) replacement character. */
int test_term_utf8_pending_reset_by_escape_sequence(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xE2\x82" "\x1B[31m" "\x80" "X", 9);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0xFFFDu);  /* flushed at ESC */
    ASSERT_EQ(get_cell(t, 0, 1).codepoint, 0xFFFDu);  /* stray 0x80, decoded fresh */
    ASSERT_EQ(get_cell(t, 0, 2).codepoint, (uint32_t)'X');
    ASSERT_EQ(t->utf8_remaining, 0);

    term_free(t);
    TEST_END();
}

/* Same hardening, but the intervening byte is a plain C0 control (LF)
 * rather than the start of an escape sequence -- LF is handled directly in
 * TERM_STATE_NORMAL and never calls term_put_char_utf8() either, so it
 * needs the same flush-before-handling treatment. */
int test_term_utf8_pending_reset_by_control_byte(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\xE2\x82" "\n", 3);

    ASSERT_EQ(get_cell(t, 0, 0).codepoint, 0xFFFDu);
    ASSERT_EQ(t->cursor.row, 1);   /* LF still advanced the cursor normally */
    ASSERT_EQ(t->utf8_remaining, 0);

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

int test_term_bracketed_paste_enable(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    ASSERT_EQ(t->bracketed_paste_mode, false);
    term_process(t, "\x1B[?2004h", 8);
    ASSERT_EQ(t->bracketed_paste_mode, true);

    term_free(t);
    TEST_END();
}

int test_term_bracketed_paste_disable(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    term_process(t, "\x1B[?2004h", 8);
    ASSERT_EQ(t->bracketed_paste_mode, true);
    term_process(t, "\x1B[?2004l", 8);
    ASSERT_EQ(t->bracketed_paste_mode, false);

    term_free(t);
    TEST_END();
}

int test_term_bracketed_paste_persists_after_reset(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* Enable bracketed paste, then send SGR reset — mode should survive */
    term_process(t, "\x1B[?2004h", 8);
    term_process(t, "\x1B[0m", 4);
    ASSERT_EQ(t->bracketed_paste_mode, true);

    term_free(t);
    TEST_END();
}

/* ---- Smart scrolling: term_scroll keeps a scrolled-up view anchored ---- */

/* At the bottom (offset 0) new output must keep following. */
int test_term_scroll_follows_when_at_bottom(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    ASSERT_EQ(t->scrollback_offset, 0);
    for (int i = 0; i < 20; i++) term_scroll(t);
    ASSERT_EQ(t->scrollback_offset, 0);
    term_free(t);
    TEST_END();
}

/* Scrolled up: each new line pushed in moves the offset by one so the
 * same history lines stay on screen. */
int test_term_scroll_anchors_when_scrolled_up(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    for (int i = 0; i < 20; i++) term_scroll(t);   /* 20 lines of history */
    t->scrollback_offset = 7;
    term_scroll(t);
    ASSERT_EQ(t->scrollback_offset, 8);
    for (int i = 0; i < 5; i++) term_scroll(t);
    ASSERT_EQ(t->scrollback_offset, 13);
    term_free(t);
    TEST_END();
}

/* The anchor can never point past the oldest line the buffer still holds:
 * clamp to lines_count - rows once history is exhausted. */
int test_term_scroll_anchor_clamps_to_history(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    for (int i = 0; i < 10; i++) term_scroll(t);   /* lines_count = 15 */
    t->scrollback_offset = 10;                      /* at the very top */
    term_scroll(t);                                 /* lines_count = 16 */
    ASSERT_EQ(t->scrollback_offset, 11);            /* still the top */
    term_free(t);
    TEST_END();
}

/* With the ring buffer full the oldest line is evicted on every scroll,
 * so an anchor at the top stays at max_scrollback rather than growing. */
int test_term_scroll_anchor_clamps_to_max_scrollback(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 8);
    for (int i = 0; i < 30; i++) term_scroll(t);   /* buffer long since full */
    t->scrollback_offset = 8;
    term_scroll(t);
    ASSERT_EQ(t->scrollback_offset, 8);
    term_scroll(t);
    ASSERT_EQ(t->scrollback_offset, 8);
    term_free(t);
    TEST_END();
}

/* A region scroll (scroll region narrower than the screen) never touches
 * scrollback and so must not move the anchor either. */
int test_term_scroll_region_does_not_move_anchor(void) {
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    for (int i = 0; i < 20; i++) term_scroll(t);
    t->scrollback_offset = 4;
    term_scroll_up(t, 1, 3, 1);
    ASSERT_EQ(t->scrollback_offset, 4);
    term_free(t);
    TEST_END();
}

/* ---- term_at_prompt() / write_seq (prompt-gated command dispatch) ------- */

int test_term_at_prompt_simple_dollar(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "thomas@tompi:~$ ", 17);
    ASSERT_EQ(term_at_prompt(t), 1);
    term_free(t);
    TEST_END();
}

int test_term_at_prompt_after_command_output(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "Reading package lists...\r\n", 27);
    ASSERT_EQ(term_at_prompt(t), 0);
    term_free(t);
    TEST_END();
}

int test_term_at_prompt_password_prompt(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "[sudo] password for thomas: ", 29);
    ASSERT_EQ(term_at_prompt(t), 0);
    term_free(t);
    TEST_END();
}

int test_term_at_prompt_text_typed_after_prompt(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    /* Cursor lands right after "ls" -- the line ends in 's', not a
     * prompt character, even though it started as "$ ". */
    term_process(t, "$ ls", 4);
    ASSERT_EQ(term_at_prompt(t), 0);
    term_free(t);
    TEST_END();
}

int test_term_at_prompt_alt_screen_active(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "$ ", 2);
    ASSERT_EQ(term_at_prompt(t), 1);
    term_alt_screen_enter(t);
    ASSERT_EQ(term_at_prompt(t), 0);
    term_free(t);
    TEST_END();
}

int test_term_at_prompt_trailing_content_after_cursor(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "$ ls", 4);
    /* Move the cursor back into the middle of the row -- "ls" is still
     * sitting on screen after it, so this must not read as a prompt. */
    t->cursor.col = 2;
    ASSERT_EQ(term_at_prompt(t), 0);
    term_free(t);
    TEST_END();
}

/* ---- term_at_continuation_prompt() ----------------------------------- */

/* Windows PowerShell 5.1 under ConPTY, started without -NoLogo: the banner,
 * then a primary prompt wrapped in the cursor hide/show ConPTY emits. */
int test_term_at_prompt_powershell_primary(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    static const char out[] =
        "Windows PowerShell\r\n"
        "Copyright (C) Microsoft Corporation. All rights reserved.\r\n"
        "\r\n"
        "Install the latest PowerShell for new features and improvements! "
        "https://aka.ms/PSWindows\r\n"
        "\r\n"
        "\x1b[?25lPS C:\\Users\\thoma> \x1b[?25h";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_at_prompt(t), 1);
    ASSERT_EQ(term_at_continuation_prompt(t), 0);
    term_free(t);
    TEST_END();
}

/* An unfinished pipeline: PSReadLine moves to a new line and shows ">> ". */
int test_term_at_prompt_powershell_continuation(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    static const char out[] =
        "PS C:\\Users\\thoma> \x1b[93mGet-ChildItem\x1b[0m |\r\n"
        ">> ";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_at_prompt(t), 0);
    ASSERT_EQ(term_at_continuation_prompt(t), 1);
    term_free(t);
    TEST_END();
}

int test_term_at_continuation_prompt_true(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "printf 'abc'\r\n> ", 16);
    ASSERT_EQ(term_at_continuation_prompt(t), 1);
    term_free(t);
    TEST_END();
}

int test_term_at_continuation_prompt_false(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "user@host:~$ ", 13);
    ASSERT_EQ(term_at_continuation_prompt(t), 0);
    term_free(t);
    TEST_END();
}

int test_term_at_prompt_dollar_still_positive(void) {
    TEST_BEGIN();
    /* term_at_prompt() is unchanged for an ordinary primary prompt. */
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "$ ", 2);
    ASSERT_EQ(term_at_prompt(t), 1);
    term_free(t);
    TEST_END();
}

int test_term_at_prompt_continuation_negative(void) {
    TEST_BEGIN();
    /* A zsh continuation prompt is not a primary prompt. */
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "dquote> ", 8);
    ASSERT_EQ(term_at_prompt(t), 0);
    term_free(t);
    TEST_END();
}

/* ---- term_at_unambiguous_prompt() (dispatch_line_clear.h no-prefix
 * safety check) ----------------------------------------------------- */

int test_term_at_unambiguous_prompt_bare_powershell(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    static const char out[] = "PS C:\\Users\\thoma> ";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_at_prompt(t), 1);
    ASSERT_EQ(term_at_unambiguous_prompt(t), 1);
    term_free(t);
    TEST_END();
}

int test_term_at_unambiguous_prompt_typed_redirect_negative(void) {
    TEST_BEGIN();
    /* The bug case: the user paused mid-command after typing a trailing
     * '>' redirection -- term_at_prompt() alone reads this row as
     * prompt-shaped, which is exactly the gap term_at_unambiguous_prompt()
     * closes for a no-prefix shell. */
    Terminal *t = term_init(24, 80, 100);
    static const char out[] = "PS C:\\Users\\thoma> ls -la >";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_at_prompt(t), 1);
    ASSERT_EQ(term_at_unambiguous_prompt(t), 0);
    term_free(t);
    TEST_END();
}

int test_term_at_unambiguous_prompt_cmd_bare(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    static const char out[] = "C:\\Users\\thoma>";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_at_unambiguous_prompt(t), 1);
    term_free(t);
    TEST_END();
}

int test_term_at_unambiguous_prompt_bash_bare(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    static const char out[] = "thomas@tompi:~$ ";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_at_unambiguous_prompt(t), 1);
    term_free(t);
    TEST_END();
}

int test_term_at_unambiguous_prompt_trailing_typed_text_negative(void) {
    TEST_BEGIN();
    /* Cursor lands right after "ls" -- not even term_at_prompt() reads
     * this as a prompt, and term_at_unambiguous_prompt() agrees. */
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "$ ls", 4);
    ASSERT_EQ(term_at_unambiguous_prompt(t), 0);
    term_free(t);
    TEST_END();
}

int test_term_at_unambiguous_prompt_alt_screen_active(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "$ ", 2);
    ASSERT_EQ(term_at_unambiguous_prompt(t), 1);
    term_alt_screen_enter(t);
    ASSERT_EQ(term_at_unambiguous_prompt(t), 0);
    term_free(t);
    TEST_END();
}

/* ---- term_cursor_row_is_windows_prompt() (dispatch_line_clear.h no-
 * prefix-at-a-Windows-prompt override) ------------------------------- */

int test_term_cursor_row_is_windows_prompt_powershell(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    static const char out[] = "PS C:\\Users\\thoma> ";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_cursor_row_is_windows_prompt(t), 1);
    term_free(t);
    TEST_END();
}

int test_term_cursor_row_is_windows_prompt_cmd(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    static const char out[] = "C:\\Users\\thoma>";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_cursor_row_is_windows_prompt(t), 1);
    term_free(t);
    TEST_END();
}

int test_term_cursor_row_is_windows_prompt_bash_negative(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    static const char out[] = "thomas@tompi:~$ ";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_cursor_row_is_windows_prompt(t), 0);
    term_free(t);
    TEST_END();
}

int test_term_cursor_row_is_windows_prompt_alt_screen_active(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    static const char out[] = "C:\\Users\\thoma>";
    term_process(t, out, sizeof(out) - 1);
    ASSERT_EQ(term_cursor_row_is_windows_prompt(t), 1);
    term_alt_screen_enter(t);
    ASSERT_EQ(term_cursor_row_is_windows_prompt(t), 0);
    term_free(t);
    TEST_END();
}

int test_term_write_seq_increments_on_data(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    ASSERT_EQ(t->write_seq, 0);
    term_process(t, "a", 1);
    ASSERT_EQ(t->write_seq, 1);
    term_process(t, "bcd", 3);
    ASSERT_EQ(t->write_seq, 2);
    term_free(t);
    TEST_END();
}

int test_term_write_seq_unchanged_on_zero_len(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_process(t, "a", 1);
    unsigned long before = t->write_seq;
    term_process(t, "", 0);
    ASSERT_EQ(t->write_seq, before);
    term_free(t);
    TEST_END();
}

/* Read the first visible screen row the renderer would draw, honouring
 * scrollback_offset the same way renderer.c's get_visible_row() does. */
static void get_top_visible_text(Terminal *t, char *out, int out_len) {
    int top = (t->lines_count >= t->rows)
            ? (t->lines_count - t->rows - t->scrollback_offset)
            : -t->scrollback_offset;
    if (top < 0) top = 0;
    TermRow *row = t->lines[(t->lines_start + top) % t->lines_capacity];
    int n = 0;
    for (int c = 0; c < row->len && n < out_len - 1; c++)
        out[n++] = (char)(row->cells[c].codepoint ? row->cells[c].codepoint : ' ');
    out[n] = '\0';
}

/* Minimise/restore: WM_SIZE(SIZE_MINIMIZED) used to drive the grid to 1x1
 * and back. Whatever the window layer does, the pure reflow round trip
 * through a degenerate geometry must leave a scrolled-back view anchored on
 * the same history line (the smart-scroll contract from term_scroll()). */
int test_term_resize_degenerate_round_trip_keeps_scroll_anchor(void) {
    TEST_BEGIN();
    Terminal *t = term_init(15, 80, 3000);
    char buf[32];
    for (int i = 1; i <= 200; i++) {
        int n = snprintf(buf, sizeof(buf), "%d\r\n", i);
        term_process(t, buf, (size_t)n);
    }
    term_process(t, "thomas@tompi:~$ ", 16);

    /* One PgUp: the same jump window.c's scroll_page_up() makes. */
    t->scrollback_offset = 15;
    char before[96], after[96];
    get_top_visible_text(t, before, sizeof(before));
    ASSERT_STR_EQ(before, "172");

    term_resize(t, 1, 1);
    term_resize(t, 15, 80);

    ASSERT_EQ(t->rows, 15);
    ASSERT_EQ(t->cols, 80);
    ASSERT_EQ(t->scrollback_offset, 15);
    get_top_visible_text(t, after, sizeof(after));
    ASSERT_STR_EQ(after, before);

    term_free(t);
    TEST_END();
}
