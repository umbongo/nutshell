#include "term.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "dlog.h"

/* Standard ANSI Palette (indices 0-15)
 * Normal colours (0-7) are brightened from classic CGA values so that
 * blue, red, magenta, and green remain legible on dark backgrounds. */
static const uint32_t PALETTE[16] = {
    0x000000, 0xCC3333, 0x4CB84C, 0xCCAA33, 0x5C7FD6, 0xB266CC, 0x44BBBB, 0xC0C0C0, /* 0-7  */
    0x808080, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5599FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF  /* 8-15 */
};

/* Convert an xterm 256-colour palette index to 0xRRGGBB. */
uint32_t color256_to_rgb(uint8_t index) {
    if (index < 16) return PALETTE[index];

    if (index < 232) {
        /* 6×6×6 colour cube: indices 16-231. */
        int n = index - 16;
        int r = n / 36;
        int g = (n % 36) / 6;
        int b = n % 6;
        uint32_t rv = (r == 0) ? 0u : (uint32_t)(55 + 40 * r);
        uint32_t gv = (g == 0) ? 0u : (uint32_t)(55 + 40 * g);
        uint32_t bv = (b == 0) ? 0u : (uint32_t)(55 + 40 * b);
        return (rv << 16) | (gv << 8) | bv;
    }

    /* 24-step greyscale ramp: indices 232-255. */
    uint32_t v = (uint32_t)(8 + 10 * (index - 232));
    return (v << 16) | (v << 8) | v;
}

/* Helper: the row at a screen row (0-based from the top of the live screen)
 * for writing. term_screen_to_phys() is the live mapping (term.h). Every
 * parser write reaches a row through here -- cell writes, insert-mode shifts,
 * ICH/DCH, EL/ED/ECH, the auto-wrap flag -- so this is the one place the
 * parser stamps a row (spec 2026-10-20 section 2.2). Cursor motion never
 * calls it. */
static TermRow *get_screen_row(Terminal *term, int screen_row) {
    if (screen_row < 0 || screen_row >= term->rows) return NULL;
    TermRow *row = term->lines[term_screen_to_phys(term, screen_row)];
    if (row) term_row_stamp(term, row);
    return row;
}

static void clamp_cursor(Terminal *term) {
    if (term->cursor.row < 0) term->cursor.row = 0;
    if (term->cursor.row >= term->rows) term->cursor.row = term->rows - 1;
    if (term->cursor.col < 0) term->cursor.col = 0;
    if (term->cursor.col >= term->cols) term->cursor.col = term->cols - 1;
}

static void term_put_char(Terminal *term, uint32_t c) {
    if (term->cursor.col >= term->cols) {
        // Auto-wrap
        term->cursor.col = 0;
        term->cursor.row++;
        if (term->cursor.row >= term->rows) {
            term_scroll(term);
            term->cursor.row = term->rows - 1;
        }
        
        // Mark new line as wrapped
        TermRow *row = get_screen_row(term, term->cursor.row);
        if (row) row->wrapped = true;
    }

    TermRow *row = get_screen_row(term, term->cursor.row);
    if (row) {
        if (term->insert_mode) {
            /* Shift cells from cursor rightwards by one, dropping the last */
            for (int i = term->cols - 1; i > term->cursor.col; i--)
                row->cells[i] = row->cells[i - 1];
            if (row->len < term->cols) row->len++;
        }
        row->cells[term->cursor.col].codepoint = c;
        row->cells[term->cursor.col].attr = term->current_attr;
        row->dirty = true;
        if (term->cursor.col >= row->len) row->len = term->cursor.col + 1;
    }
    term->cursor.col++;
}

/* Hardening: decode UTF-8 one byte at a time, rejecting overlong encodings,
 * UTF-16 surrogates (U+D800-U+DFFF), codepoints past U+10FFFF, unexpected
 * continuation bytes and truncated sequences. Follows the Unicode standard's
 * recommended "maximal subpart" substitution: a malformed sequence emits
 * exactly one U+FFFD for the longest valid prefix it had, and the byte that
 * broke it is re-examined as a fresh character rather than swallowed --
 * that byte was never actually part of the bad sequence. See term.h's
 * utf8_lo/utf8_hi doc comment for how the per-lead-byte bounds work. */
static void term_put_char_utf8(Terminal *term, unsigned char c) {
    if (term->utf8_remaining > 0) {
        if (c >= term->utf8_lo && c <= term->utf8_hi) {
            term->utf8_codepoint = (term->utf8_codepoint << 6) | (uint32_t)(c & 0x3F);
            term->utf8_remaining--;
            /* Only the first continuation byte after the lead byte is
             * range-restricted; every one after that is generic. */
            term->utf8_lo = 0x80;
            term->utf8_hi = 0xBF;
            if (term->utf8_remaining == 0) {
                term_put_char(term, term->utf8_codepoint);
            }
            return;
        }
        /* Sequence broke before completing: emit one replacement character
         * for the maximal subpart and reprocess this byte fresh -- it is
         * either plain ASCII, a new lead byte, or itself invalid. */
        term->utf8_remaining = 0;
        term_put_char(term, 0xFFFDu);
        term_put_char_utf8(term, c);
        return;
    }

    if (c < 0x80) {
        term_put_char(term, (uint32_t)c);
    } else if (c >= 0xC2 && c <= 0xDF) {           /* 2-byte lead */
        term->utf8_codepoint = (uint32_t)(c & 0x1F);
        term->utf8_remaining = 1;
        term->utf8_lo = 0x80; term->utf8_hi = 0xBF;
    } else if (c == 0xE0) {                        /* 3-byte, excl. overlong */
        term->utf8_codepoint = (uint32_t)(c & 0x0F);
        term->utf8_remaining = 2;
        term->utf8_lo = 0xA0; term->utf8_hi = 0xBF;
    } else if (c == 0xED) {                        /* 3-byte, excl. surrogates */
        term->utf8_codepoint = (uint32_t)(c & 0x0F);
        term->utf8_remaining = 2;
        term->utf8_lo = 0x80; term->utf8_hi = 0x9F;
    } else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) {
        term->utf8_codepoint = (uint32_t)(c & 0x0F);
        term->utf8_remaining = 2;
        term->utf8_lo = 0x80; term->utf8_hi = 0xBF;
    } else if (c == 0xF0) {                        /* 4-byte, excl. overlong */
        term->utf8_codepoint = (uint32_t)(c & 0x07);
        term->utf8_remaining = 3;
        term->utf8_lo = 0x90; term->utf8_hi = 0xBF;
    } else if (c >= 0xF1 && c <= 0xF3) {
        term->utf8_codepoint = (uint32_t)(c & 0x07);
        term->utf8_remaining = 3;
        term->utf8_lo = 0x80; term->utf8_hi = 0xBF;
    } else if (c == 0xF4) {                        /* 4-byte, excl. > U+10FFFF */
        term->utf8_codepoint = (uint32_t)(c & 0x07);
        term->utf8_remaining = 3;
        term->utf8_lo = 0x80; term->utf8_hi = 0x8F;
    } else {
        /* 0x80-0xC1: stray continuation byte or overlong 2-byte lead.
         * 0xF5-0xFF: would only ever encode past U+10FFFF. Either way,
         * one invalid byte, one replacement character. */
        term_put_char(term, 0xFFFDu);
    }
}

/* ---- CSI Handlers --------------------------------------------------------- */

static int get_param(Terminal *term, int idx, int default_val) {
    if (idx >= term->csi_param_count) return default_val;
    int val = term->csi_params[idx];
    return (val <= 0) ? default_val : val;
}

/* Helper: full SGR reset of all colour and style fields. */
static void sgr_reset(Terminal *term) {
    term->current_attr.fg       = 0;
    term->current_attr.bg       = 0;
    term->current_attr.fg_mode  = COLOR_DEFAULT;
    term->current_attr.bg_mode  = COLOR_DEFAULT;
    term->current_attr.fg_index = 0;
    term->current_attr.bg_index = 0;
    term->current_attr.flags    = 0;
}

/* Applies a 256-colour or truecolour value to the foreground (38) or
 * background (48). 58 (underline colour) is not supported and is a no-op. */
static void sgr_apply_colour(Terminal *term, int which, bool is_256,
                             int a, int g, int b)
{
    if (which == 58) return;
    uint32_t *rgb      = (which == 38) ? &term->current_attr.fg : &term->current_attr.bg;
    uint8_t *mode      = (which == 38) ? &term->current_attr.fg_mode : &term->current_attr.bg_mode;
    uint8_t *index     = (which == 38) ? &term->current_attr.fg_index : &term->current_attr.bg_index;

    if (is_256) {
        if (a < 0 || a > 255) return;
        *mode  = (uint8_t)COLOR_256;
        *index = (uint8_t)a;
        *rgb   = color256_to_rgb((uint8_t)a);
    } else {
        if (a < 0 || a > 255 || g < 0 || g > 255 || b < 0 || b > 255) return;
        *mode = (uint8_t)COLOR_RGB;
        *rgb  = ((uint32_t)a << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

/* True when csi_params[idx] was introduced by ':'. */
static bool sgr_is_sub(const Terminal *term, int idx)
{
    return idx > 0 && idx < TERM_MAX_CSI_PARAMS &&
           (term->csi_sub_mask & (uint16_t)(1u << idx)) != 0;
}

/* Parses an extended colour (38/48/58) whose code is at index i and returns
 * the index of the last parameter it consumed. Semicolon form 38;5;n and
 * 38;2;r;g;b: a valid one consumes its values; a truncated one, or one with
 * any other selector, consumes the rest of the sequence and changes nothing
 * (xterm), so its selector and values are never applied as SGR codes. Colon
 * form 38:5:n, 38:2:r:g:b and 38:2:cs:r:g:b (the colour-space id may be
 * empty): colon groups are self-delimiting, so an invalid one consumes only
 * its own run. A mixed 38;2:r:g:b is read as the colon form. */
static int sgr_extended_colour(Terminal *term, int i)
{
    const int count = term->csi_param_count;
    const int *pv   = term->csi_params;
    const int which = pv[i];

    /* The colon run: from the selector at i+1 to the last sub-parameter. */
    int run_end = i + 1;                 /* one past the last parameter used */
    if (i + 1 < count) {
        run_end = i + 2;
        while (run_end < count && sgr_is_sub(term, run_end)) run_end++;
    }
    const bool colon = i + 1 < count &&
                       (sgr_is_sub(term, i + 1) || run_end > i + 2);

    if (colon) {
        int n = run_end - (i + 1);       /* selector plus its values */
        if (pv[i + 1] == 5 && n >= 2) {
            sgr_apply_colour(term, which, true, pv[i + 2], 0, 0);
        } else if (pv[i + 1] == 2 && n >= 5) {
            sgr_apply_colour(term, which, false, pv[i + 3], pv[i + 4], pv[i + 5]);
        } else if (pv[i + 1] == 2 && n == 4) {
            sgr_apply_colour(term, which, false, pv[i + 2], pv[i + 3], pv[i + 4]);
        }
        return run_end - 1;
    }

    if (i + 1 < count) {
        if (pv[i + 1] == 5 && i + 2 < count) {
            sgr_apply_colour(term, which, true, pv[i + 2], 0, 0);
            return i + 2;
        }
        if (pv[i + 1] == 2 && i + 4 < count) {
            sgr_apply_colour(term, which, false, pv[i + 2], pv[i + 3], pv[i + 4]);
            return i + 4;
        }
    }
    return count - 1;                    /* truncated or unknown: eat the rest */
}

static void handle_sgr(Terminal *term) {
    if (term->csi_param_count == 0) {
        sgr_reset(term);
        return;
    }

    for (int i = 0; i < term->csi_param_count; i++) {
        /* A sub-parameter ("4:3") belongs to the code before it, which has
         * already applied itself; it is never a code of its own. */
        if (sgr_is_sub(term, i)) continue;
        int p = term->csi_params[i];
        switch (p) {
            case 0: /* Full reset */
                sgr_reset(term);
                break;
            case 1: term->current_attr.flags |= TERM_ATTR_BOLD;      break;
            case 2: term->current_attr.flags |= TERM_ATTR_DIM;       break;
            case 4:
                /* "4:0" is "no underline"; plain 4 and 4:1..4:5 (the styles)
                 * all underline. */
                if (i + 1 < term->csi_param_count && sgr_is_sub(term, i + 1) &&
                        term->csi_params[i + 1] == 0)
                    term->current_attr.flags &= (uint8_t)~TERM_ATTR_UNDERLINE;
                else
                    term->current_attr.flags |= TERM_ATTR_UNDERLINE;
                break;
            case 5: term->current_attr.flags |= TERM_ATTR_BLINK;      break;
            case 7: term->current_attr.flags |= TERM_ATTR_REVERSE;    break;

            /* 22 is "normal intensity" (ECMA-48): neither bold nor dim. */
            case 22: term->current_attr.flags &= (uint8_t)~(TERM_ATTR_BOLD | TERM_ATTR_DIM); break;
            case 24: term->current_attr.flags &= (uint8_t)~TERM_ATTR_UNDERLINE;  break;
            case 25: term->current_attr.flags &= (uint8_t)~TERM_ATTR_BLINK;      break;
            case 27: term->current_attr.flags &= (uint8_t)~TERM_ATTR_REVERSE;    break;

            /* Classic 16-colour foreground (30-37, 90-97) */
            case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37:
                term->current_attr.fg_mode  = COLOR_ANSI16;
                term->current_attr.fg_index = (uint8_t)(p - 30);
                term->current_attr.fg       = PALETTE[p - 30];
                break;
            case 39: /* default fg */
                term->current_attr.fg_mode  = COLOR_DEFAULT;
                term->current_attr.fg_index = 0;
                term->current_attr.fg       = 0;
                break;

            /* Classic 16-colour background (40-47, 100-107) */
            case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
                term->current_attr.bg_mode  = COLOR_ANSI16;
                term->current_attr.bg_index = (uint8_t)(p - 40);
                term->current_attr.bg       = PALETTE[p - 40];
                break;
            case 49: /* default bg */
                term->current_attr.bg_mode  = COLOR_DEFAULT;
                term->current_attr.bg_index = 0;
                term->current_attr.bg       = 0;
                break;

            /* Bright foreground (90-97) */
            case 90: case 91: case 92: case 93: case 94: case 95: case 96: case 97:
                term->current_attr.fg_mode  = COLOR_ANSI16;
                term->current_attr.fg_index = (uint8_t)(p - 90 + 8);
                term->current_attr.fg       = PALETTE[p - 90 + 8];
                break;

            /* Bright background (100-107) */
            case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
                term->current_attr.bg_mode  = COLOR_ANSI16;
                term->current_attr.bg_index = (uint8_t)(p - 100 + 8);
                term->current_attr.bg       = PALETTE[p - 100 + 8];
                break;

            /* Extended colour: 38 foreground, 48 background, 58 underline
             * colour (parsed so its parameters are consumed, not applied). */
            case 38: case 48: case 58:
                i = sgr_extended_colour(term, i);
                break;

            default:
                break;
        }
    }
}

/* ---- OSC / Private-mode helpers ------------------------------------------ */

static void process_osc(Terminal *term)
{
    /* OSC buffer format: "Ps;Pt" where Ps is numeric code, Pt is text */
    int semi = -1;
    for (int i = 0; i < term->osc_len; i++) {
        if (term->osc_buffer[i] == ';') { semi = i; break; }
    }
    if (semi < 0) goto osc_done;

    /* Parse numeric code */
    int code = 0;
    for (int i = 0; i < semi; i++) {
        if (term->osc_buffer[i] < '0' || term->osc_buffer[i] > '9') goto osc_done;
        /* A host controls the digits: stop accumulating once the code is far
         * past any OSC number we handle, so it can neither overflow nor wrap
         * into 0 or 2. */
        if (code < 100000) code = code * 10 + (term->osc_buffer[i] - '0');
    }

    if (code == 0 || code == 2) {
        const char *text = term->osc_buffer + semi + 1;
        (void)snprintf(term->title, sizeof(term->title), "%s", text);
        DLOG_CONTENT(DLOG_TERM, DLOG_DEBUG, term->log_id, "term.title", "text",
                     text, strlen(text));
    }
osc_done:
    term->osc_len = 0;
    term->osc_buffer[0] = '\0';
}

static void handle_private_mode(Terminal *term, bool set)
{
    int mode = (term->csi_param_count > 0) ? term->csi_params[0] : 0;
    switch (mode) {
        case 1:    term->app_cursor_keys = set; break;
        case 25:   term->cursor.visible  = set; break;
        case 1049: if (set) term_alt_screen_enter(term);
                   else     term_alt_screen_exit(term);
                   break;
        case 2004: term->bracketed_paste_mode = set; break;
        default:   break; /* ignore unknown private modes */
    }
}

/* --------------------------------------------------------------------------- */

static void handle_csi(Terminal *term, char final) {
    int n, m;
    switch (final) {
        case 'A': // CUU - Up
            term->cursor.row -= get_param(term, 0, 1);
            clamp_cursor(term);
            break;
        case 'B': // CUD - Down
            term->cursor.row += get_param(term, 0, 1);
            clamp_cursor(term);
            break;
        case 'C': // CUF - Forward
            term->cursor.col += get_param(term, 0, 1);
            clamp_cursor(term);
            break;
        case 'D': // CUB - Back
            term->cursor.col -= get_param(term, 0, 1);
            clamp_cursor(term);
            break;
        case 'H': // CUP - Cursor Position
        case 'f':
            n = get_param(term, 0, 1);
            m = get_param(term, 1, 1);
            term->cursor.row = n - 1;
            term->cursor.col = m - 1;
            clamp_cursor(term);
            break;
        case 'J': /* ED — Erase in Display */
            n = get_param(term, 0, 0);
            if (n == 0) {
                /* Clear from cursor to end of screen. From the home position
                 * this is `clear` (ESC[H ESC[J): a whole-screen erase, counted
                 * like ED 2 (term.h, erase_seq). */
                if (!term->alt_screen_active && term->cursor.row == 0 &&
                    term->cursor.col == 0)
                    term->erase_seq++;
                TermRow *cr0 = get_screen_row(term, term->cursor.row);
                if (cr0) {
                    for (int c = term->cursor.col; c < term->cols; c++) {
                        cr0->cells[c].codepoint = 0;
                        cr0->cells[c].attr = term->current_attr;
                    }
                    if (term->cursor.col == 0) cr0->wrapped = false;   /* whole row erased */
                    cr0->dirty = true;
                }
                for (int r = term->cursor.row + 1; r < term->rows; r++) {
                    TermRow *rr = get_screen_row(term, r);
                    if (rr) {
                        for (int c = 0; c < term->cols; c++) {
                            rr->cells[c].codepoint = 0;
                            rr->cells[c].attr = term->current_attr;
                        }
                        rr->len = 0;
                        rr->wrapped = false;
                        rr->dirty = true;
                    }
                }
            } else if (n == 1) {
                /* Clear from start of screen to cursor */
                for (int r = 0; r < term->cursor.row; r++) {
                    TermRow *rr = get_screen_row(term, r);
                    if (rr) {
                        for (int c = 0; c < term->cols; c++) {
                            rr->cells[c].codepoint = 0;
                            rr->cells[c].attr = term->current_attr;
                        }
                        rr->len = 0;
                        rr->wrapped = false;
                        rr->dirty = true;
                    }
                }
                TermRow *cr1 = get_screen_row(term, term->cursor.row);
                if (cr1) {
                    /* C4: cursor.col can legitimately equal term->cols
                     * between characters (deferred auto-wrap, term_put_char
                     * only wraps on the *next* printable byte) -- clamp the
                     * erase bound, not the cursor itself, or this walks one
                     * TermCell past the row allocation. */
                    int cc = term->cursor.col;
                    if (cc > term->cols - 1) cc = term->cols - 1;
                    for (int c = 0; c <= cc; c++) {
                        cr1->cells[c].codepoint = 0;
                        cr1->cells[c].attr = term->current_attr;
                    }
                    if (cc >= term->cols - 1) cr1->wrapped = false;    /* whole row erased */
                    cr1->dirty = true;
                }
            } else if (n == 2) {
                /* Clear entire screen — VT100: does NOT move cursor (xterm compat) */
                if (!term->alt_screen_active) term->erase_seq++;
                for (int r = 0; r < term->rows; r++) {
                    TermRow *rr = get_screen_row(term, r);
                    if (rr) {
                        for (int c = 0; c < term->cols; c++) {
                            rr->cells[c].codepoint = 0;
                            rr->cells[c].attr = term->current_attr;
                        }
                        rr->len = 0;
                        rr->dirty = true;
                        rr->wrapped = false;
                    }
                }
                /* Cursor position unchanged — applications that want cursor home
                 * send ESC[H separately.  Moving here was non-standard and caused
                 * the bash prompt to appear at the top after TUI apps exited. */
                DLOG(DLOG_TERM, DLOG_DEBUG, term->log_id, "term.clear",
                     "mode=2 alt=%d cursor_row=%d", term->alt_screen_active ? 1 : 0,
                     term->cursor.row);
            }
            break;
        case 'K': // EL - Erase in Line
            n = get_param(term, 0, 0);
            TermRow *row = get_screen_row(term, term->cursor.row);
            if (row) {
                /* C4: clamp the erase bound to the last real column --
                 * cursor.col can be term->cols between characters (deferred
                 * auto-wrap). Do not clamp term->cursor itself, or the
                 * pending wrap that must fire on the next printable char
                 * would be lost. */
                int cc = term->cursor.col;
                if (cc > term->cols - 1) cc = term->cols - 1;
                int start = 0, end = term->cols;
                if (n == 0) start = cc; // Cursor to end
                else if (n == 1) end = cc + 1; // Start to cursor
                // n=2 is entire line (0 to cols)

                for (int c = start; c < end; c++) {
                    row->cells[c].codepoint = 0;
                    row->cells[c].attr = term->current_attr;
                }
                /* A whole-row erase ends the soft wrap onto this row (spec
                 * 2026-10-20 section 2.4): EL 2, EL 0 from column 0, or EL 1
                 * to the last column. Cursor motion never touches the flag -- readline moves
                 * onto a continuation row with a bare LF and back with CUU. */
                if (n == 2 || (n == 0 && cc == 0)
                    || (n == 1 && cc >= term->cols - 1)) row->wrapped = false;
                row->dirty = true;
            }
            break;
        case 'm': // SGR
            handle_sgr(term);
            break;
        case 's': // Save Cursor
            term->saved_cursor = term->cursor;
            break;
        case 'u': // Restore Cursor
            term->cursor = term->saved_cursor;
            clamp_cursor(term);
            break;
        case 'E': /* CNL — cursor next line */
            n = get_param(term, 0, 1);
            term->cursor.row += n;
            term->cursor.col = 0;
            clamp_cursor(term);
            break;
        case 'F': /* CPL — cursor previous line */
            n = get_param(term, 0, 1);
            term->cursor.row -= n;
            term->cursor.col = 0;
            clamp_cursor(term);
            break;
        case 'G': /* CHA — cursor horizontal absolute */
        case '`': /* HPA */
            n = get_param(term, 0, 1);
            term->cursor.col = n - 1;
            clamp_cursor(term);
            break;
        case 'd': /* VPA — line position absolute */
            n = get_param(term, 0, 1);
            term->cursor.row = n - 1;
            clamp_cursor(term);
            break;
        case 'h': /* SM — Set Mode (non-private) */
            if (get_param(term, 0, 0) == 4) term->insert_mode = true;
            break;
        case 'l': /* RM — Reset Mode (non-private) */
            if (get_param(term, 0, 0) == 4) term->insert_mode = false;
            break;
        case '~': /* VT220 keys */
            n = get_param(term, 0, 0);
            if (n == 1) term->cursor.col = 0; /* Home */
            else if (n == 4) term->cursor.col = term->cols - 1; /* End */
            /* 2=Ins, 3=Del, 5=PgUp, 6=PgDn - ignored for now */
            break;
        case 'r': { /* DECSTBM — Set Scrolling Region */
            int sr_top = get_param(term, 0, 1) - 1;
            int sr_bot = get_param(term, 1, term->rows) - 1;
            if (sr_top < 0) sr_top = 0;
            if (sr_bot >= term->rows) sr_bot = term->rows - 1;
            if (sr_top < sr_bot) {
                term->scroll_top = sr_top;
                term->scroll_bot = sr_bot;
            } else {
                term->scroll_top = 0;
                term->scroll_bot = term->rows - 1;
            }
            term->cursor.row = 0;
            term->cursor.col = 0;
            break;
        }
        case 'L': { /* IL — Insert Lines */
            int il_n = get_param(term, 0, 1);
            if (il_n > term->rows) il_n = term->rows;  /* H9/L4 */
            if (term->cursor.row >= term->scroll_top &&
                term->cursor.row <= term->scroll_bot) {
                term_scroll_down(term, term->cursor.row,
                                 term->scroll_bot, il_n);
            }
            break;
        }
        case 'M': { /* DL — Delete Lines */
            int dl_n = get_param(term, 0, 1);
            if (dl_n > term->rows) dl_n = term->rows;  /* H9/L4 */
            if (term->cursor.row >= term->scroll_top &&
                term->cursor.row <= term->scroll_bot) {
                term_scroll_up(term, term->cursor.row,
                               term->scroll_bot, dl_n);
            }
            break;
        }
        case 'S': { /* SU — Scroll Up */
            int su_n = get_param(term, 0, 1);
            if (su_n > term->rows) su_n = term->rows;  /* H9/L4 */
            term_scroll_up(term, term->scroll_top, term->scroll_bot, su_n);
            break;
        }
        case 'T': { /* SD — Scroll Down */
            int sd_n = get_param(term, 0, 1);
            if (sd_n > term->rows) sd_n = term->rows;  /* H9/L4 */
            term_scroll_down(term, term->scroll_top, term->scroll_bot, sd_n);
            break;
        }
        case '@': { /* ICH — Insert Characters */
            int ich_n = get_param(term, 0, 1);
            if (ich_n > term->cols) ich_n = term->cols;  /* H9/L4 */
            TermRow *ich_row = get_screen_row(term, term->cursor.row);
            if (ich_row) {
                for (int ic = term->cols - 1; ic >= term->cursor.col + ich_n; ic--)
                    ich_row->cells[ic] = ich_row->cells[ic - ich_n];
                for (int ic = term->cursor.col;
                     ic < term->cursor.col + ich_n && ic < term->cols; ic++) {
                    ich_row->cells[ic].codepoint = 0;
                    ich_row->cells[ic].attr = term->current_attr;
                }
                ich_row->dirty = true;
            }
            break;
        }
        case 'P': { /* DCH — Delete Characters */
            int dch_n = get_param(term, 0, 1);
            if (dch_n > term->cols) dch_n = term->cols;  /* H9/L4 */
            TermRow *dch_row = get_screen_row(term, term->cursor.row);
            if (dch_row) {
                int dch_end = term->cols - dch_n;
                if (dch_end < term->cursor.col) dch_end = term->cursor.col;
                for (int dc = term->cursor.col; dc < dch_end; dc++)
                    dch_row->cells[dc] = dch_row->cells[dc + dch_n];
                for (int dc = dch_end; dc < term->cols; dc++) {
                    dch_row->cells[dc].codepoint = 0;
                    dch_row->cells[dc].attr = term->current_attr;
                }
                dch_row->dirty = true;
            }
            break;
        }
        case 'X': { /* ECH — Erase Characters (cursor stays) */
            int ech_n = get_param(term, 0, 1);
            if (ech_n > term->cols) ech_n = term->cols;  /* H9/L4 */
            TermRow *ech_row = get_screen_row(term, term->cursor.row);
            if (ech_row) {
                for (int ec = term->cursor.col;
                     ec < term->cursor.col + ech_n && ec < term->cols; ec++) {
                    ech_row->cells[ec].codepoint = 0;
                    ech_row->cells[ec].attr = term->current_attr;
                }
                ech_row->dirty = true;
            }
            break;
        }
    }
}

/* ---- State Machine -------------------------------------------------------- */

void term_process(Terminal *term, const char *data, size_t len) {
    if (!term || !data) return;
    if (len > 0) term->write_seq++;

    int prev_cursor_row = term->cursor.row;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];

        switch (term->state) {
            case TERM_STATE_NORMAL:
                /* Hardening: a pending multi-byte UTF-8 sequence must not
                 * survive a C0 control or DEL, and especially not the ESC
                 * that starts an escape/CSI/OSC sequence -- none of those
                 * call term_put_char_utf8(), so without this the pending
                 * state (utf8_remaining/utf8_lo/utf8_hi, left over from
                 * before the control byte) would still be waiting when
                 * TERM_STATE_NORMAL resumes, and a continuation byte
                 * arriving after the intervening bytes could complete a
                 * codepoint built from lead/continuation bytes that never
                 * belonged together. Flush with one replacement character,
                 * matching term_put_char_utf8()'s own resync behaviour for
                 * a sequence broken by an invalid continuation byte, then
                 * fall through to handle the control normally. DEL (0x7F)
                 * is included here even though it is also >= 0x20 and so
                 * would otherwise reach term_put_char_utf8() (which already
                 * resyncs) -- handling it in the same place as every other
                 * control keeps this one rule easy to audit. */
                if ((c < 0x20 || c == 0x7F) && term->utf8_remaining > 0) {
                    term->utf8_remaining = 0;
                    term_put_char(term, 0xFFFDu);
                }
                if (c == 0x1B) {
                    term->state = TERM_STATE_ESC;
                } else if (c == '\r') {
                    term->cursor.col = 0;
                } else if (c == '\n') {
                    if (term->cursor.row == term->scroll_bot) {
                        term_scroll_up(term, term->scroll_top,
                                       term->scroll_bot, 1);
                    } else if (term->cursor.row < term->rows - 1) {
                        term->cursor.row++;
                    }
                } else if (c == '\b') {
                    if (term->cursor.col > 0) term->cursor.col--;
                } else if (c == '\t') {
                    int next_tab = (term->cursor.col / 8 + 1) * 8;
                    if (next_tab >= term->cols) next_tab = term->cols - 1;
                    term->cursor.col = next_tab;
                } else if (c >= 0x20) {
                    term_put_char_utf8(term, c);
                }
                break;

            case TERM_STATE_ESC:
                if (c == '[') {
                    term->state = TERM_STATE_CSI;
                    term->csi_param_count = 0;
                    term->csi_params[0] = 0;
                    term->csi_sub_mask = 0;
                    term->csi_overflow = false;
                    term->csi_private = false;
                } else if (c == ']') {
                    term->state = TERM_STATE_OSC;
                    term->osc_len = 0;
                    term->osc_buffer[0] = '\0';
                } else if (c == 'M') {
                    /* RI — Reverse Index */
                    if (term->cursor.row == term->scroll_top) {
                        term_scroll_down(term, term->scroll_top,
                                         term->scroll_bot, 1);
                    } else if (term->cursor.row > 0) {
                        term->cursor.row--;
                    }
                    term->state = TERM_STATE_NORMAL;
                } else if (c == 'D') {
                    /* IND — Index (same as LF within scroll region) */
                    if (term->cursor.row == term->scroll_bot) {
                        term_scroll_up(term, term->scroll_top,
                                       term->scroll_bot, 1);
                    } else if (term->cursor.row < term->rows - 1) {
                        term->cursor.row++;
                    }
                    term->state = TERM_STATE_NORMAL;
                } else if (c == '7') {
                    /* DECSC — Save Cursor */
                    term->saved_cursor = term->cursor;
                    term->state = TERM_STATE_NORMAL;
                } else if (c == '8') {
                    /* DECRC — Restore Cursor */
                    term->cursor = term->saved_cursor;
                    if (term->cursor.row < 0) term->cursor.row = 0;
                    if (term->cursor.col < 0) term->cursor.col = 0;
                    if (term->cursor.row >= term->rows)
                        term->cursor.row = term->rows - 1;
                    if (term->cursor.col >= term->cols)
                        term->cursor.col = term->cols - 1;
                    term->state = TERM_STATE_NORMAL;
                } else if (c == 'E') {
                    /* NEL — Next Line (CR + LF) */
                    term->cursor.col = 0;
                    if (term->cursor.row == term->scroll_bot) {
                        term_scroll_up(term, term->scroll_top,
                                       term->scroll_bot, 1);
                    } else if (term->cursor.row < term->rows - 1) {
                        term->cursor.row++;
                    }
                    term->state = TERM_STATE_NORMAL;
                } else if (c == '(' || c == ')' || c == '*' ||
                           c == '+' || c == '#') {
                    /* SCS / DECDHL / DECALN — consume the next byte */
                    term->state = TERM_STATE_SCS;
                } else {
                    term->state = TERM_STATE_NORMAL;
                }
                break;

            case TERM_STATE_SCS:
                /* Consume charset designator or line-attribute byte.
                 * We don't implement alternate charsets — just eat it. */
                term->state = TERM_STATE_NORMAL;
                break;

            case TERM_STATE_CSI:
                if (c == '?') {
                    term->csi_private = true;
                } else if (isdigit(c)) {
                    if (term->csi_overflow) break; /* past the cap: dropped */
                    if (term->csi_param_count == 0) term->csi_param_count = 1;
                    /* L-4: explicit bounds check before array access */
                    if (term->csi_param_count > 0 &&
                            term->csi_param_count <= TERM_MAX_CSI_PARAMS) {
                        int *p = &term->csi_params[term->csi_param_count - 1];
                        /* L4/H9: saturate at 65535 instead of overflowing --
                         * computed in long so the intermediate can never
                         * overflow int even starting from the saturated
                         * value (65535*10+9 fits easily). An unbounded
                         * digit accumulator here is what let ESC[2147483647@
                         * turn into a huge negative count downstream. */
                        long v = (long)(*p) * 10 + (c - '0');
                        if (v > 65535L) v = 65535L;
                        *p = (int)v;
                    }
                } else if (c == ';' || c == ':') {
                    /* ':' starts a sub-parameter of the preceding parameter
                     * (ISO 8613-6, as in SGR 4:3 and 38:2::r:g:b). An empty
                     * first parameter before ';' or ':' counts as one. */
                    if (term->csi_param_count == 0)
                        term->csi_param_count = 1;
                    if (term->csi_param_count < TERM_MAX_CSI_PARAMS) {
                        term->csi_param_count++;
                        term->csi_params[term->csi_param_count - 1] = 0;
                        if (c == ':')
                            term->csi_sub_mask |= (uint16_t)(1u << (term->csi_param_count - 1));
                        else
                            term->csi_sub_mask &= (uint16_t)~(1u << (term->csi_param_count - 1));
                    } else {
                        /* No slot left: later digits must not merge into
                         * the last one (";10;20;30" became "102030"). */
                        term->csi_overflow = true;
                    }
                } else if (c >= 0x40 && c <= 0x7E) {
                    if (term->csi_sub_mask != 0 &&
                            !(c == 'm' && !term->csi_private)) {
                        /* Only SGR defines sub-parameters; any other CSI
                         * carrying one is ignored whole (xterm, VTE). */
                    } else if (term->csi_private && (c == 'h' || c == 'l')) {
                        handle_private_mode(term, c == 'h');
                    } else {
                        handle_csi(term, (char)c);
                    }
                    term->csi_private = false;
                    term->state = TERM_STATE_NORMAL;
                }
                break;

            case TERM_STATE_OSC:
                if (c == 0x07) { /* BEL terminates OSC */
                    process_osc(term);
                    term->state = TERM_STATE_NORMAL;
                } else if (c == 0x1B) { /* ESC starts ST (ESC \) — process OSC now */
                    process_osc(term);
                    term->state = TERM_STATE_ESC; /* consume the following '\' */
                } else {
                    if (term->osc_len < 255) {
                        term->osc_buffer[term->osc_len++] = (char)c;
                        term->osc_buffer[term->osc_len] = '\0';
                    }
                }
                break;
        }
    }

    /* If the cursor moved to a different row, mark the old row dirty
     * so the renderer erases the old cursor block. term_screen_to_phys()
     * (term.h), not a hand-rolled copy of the mapping with its own
     * `< lines_count` check: prev_cursor_row is a screen row (clamp_cursor()
     * keeps cursor.row in [0, rows) always), and the row it names is
     * exactly as real in the sparse state right after a resize as any
     * other -- the old cursor block needs erasing there too. */
    if (term->cursor.row != prev_cursor_row) {
        int phys = term_screen_to_phys(term, prev_cursor_row);
        if (phys >= 0 && phys < term->lines_capacity && term->lines[phys]) {
            term->lines[phys]->dirty = true;
        }
    }
}
