#include "term.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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

/* Helper: Get pointer to a screen row (0-based index from top of visible screen) */
static TermRow *get_screen_row(Terminal *term, int screen_row) {
    if (screen_row < 0 || screen_row >= term->rows) return NULL;
    
    // The screen is always the last 'rows' lines of the buffer.
    // logical_idx 0 is the oldest line in the buffer.
    // logical_idx of top of screen = term->lines_count - term->rows.
    // If lines_count < rows (startup), top is 0.
    
    int top_logical = (term->lines_count >= term->rows) ? (term->lines_count - term->rows) : 0;
    int logical_idx = top_logical + screen_row;
    int physical_idx = (term->lines_start + logical_idx) % term->lines_capacity;
    
    return term->lines[physical_idx];
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

static void term_put_char_utf8(Terminal *term, unsigned char c) {
    if (term->utf8_remaining > 0) {
        if ((c & 0xC0) == 0x80) {
            term->utf8_codepoint = (term->utf8_codepoint << 6) | (c & 0x3F);
            term->utf8_remaining--;
            if (term->utf8_remaining == 0) {
                term_put_char(term, term->utf8_codepoint);
            }
        } else {
            term->utf8_remaining = 0;
        }
    } else {
        if (c < 0x80) {
            term_put_char(term, (uint32_t)c);
        } else if ((c & 0xE0) == 0xC0) {
            term->utf8_codepoint = c & 0x1F;
            term->utf8_remaining = 1;
        } else if ((c & 0xF0) == 0xE0) {
            term->utf8_codepoint = c & 0x0F;
            term->utf8_remaining = 2;
        } else if ((c & 0xF8) == 0xF0) {
            term->utf8_codepoint = c & 0x07;
            term->utf8_remaining = 3;
        }
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

static void handle_sgr(Terminal *term) {
    if (term->csi_param_count == 0) {
        sgr_reset(term);
        return;
    }

    for (int i = 0; i < term->csi_param_count; i++) {
        int p = term->csi_params[i];
        switch (p) {
            case 0: /* Full reset */
                sgr_reset(term);
                break;
            case 1: term->current_attr.flags |= TERM_ATTR_BOLD;      break;
            case 4: term->current_attr.flags |= TERM_ATTR_UNDERLINE;  break;
            case 5: term->current_attr.flags |= TERM_ATTR_BLINK;      break;
            case 7: term->current_attr.flags |= TERM_ATTR_REVERSE;    break;

            case 22: term->current_attr.flags &= (uint8_t)~TERM_ATTR_BOLD;      break;
            case 24: term->current_attr.flags &= (uint8_t)~TERM_ATTR_UNDERLINE;  break;
            case 25: term->current_attr.flags &= (uint8_t)~TERM_ATTR_BLINK;      break;
            case 27: term->current_attr.flags &= (uint8_t)~TERM_ATTR_REVERSE;    break;

            /* Classic 16-colour foreground (30-37, 90-97) */
            case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37:
                term->current_attr.fg_mode  = COLOR_ANSI16;
                term->current_attr.fg_index = 0;
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
                term->current_attr.bg_index = 0;
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
                term->current_attr.fg_index = 0;
                term->current_attr.fg       = PALETTE[p - 90 + 8];
                break;

            /* Bright background (100-107) */
            case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
                term->current_attr.bg_mode  = COLOR_ANSI16;
                term->current_attr.bg_index = 0;
                term->current_attr.bg       = PALETTE[p - 100 + 8];
                break;

            /* Extended foreground: 38;5;n (256-colour) or 38;2;r;g;b (truecolor) */
            case 38:
                if (i + 2 < term->csi_param_count && term->csi_params[i + 1] == 5) {
                    int idx = term->csi_params[i + 2];
                    if (idx >= 0 && idx <= 255) {
                        term->current_attr.fg_mode  = COLOR_256;
                        term->current_attr.fg_index = (uint8_t)idx;
                        term->current_attr.fg       = color256_to_rgb((uint8_t)idx);
                    }
                    i += 2;
                } else if (i + 4 < term->csi_param_count && term->csi_params[i + 1] == 2) {
                    int r = term->csi_params[i + 2];
                    int g = term->csi_params[i + 3];
                    int b = term->csi_params[i + 4];
                    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                        term->current_attr.fg_mode = COLOR_RGB;
                        term->current_attr.fg      = ((uint32_t)r << 16) |
                                                     ((uint32_t)g <<  8) |
                                                      (uint32_t)b;
                    }
                    i += 4;
                }
                break;

            /* Extended background: 48;5;n (256-colour) or 48;2;r;g;b (truecolor) */
            case 48:
                if (i + 2 < term->csi_param_count && term->csi_params[i + 1] == 5) {
                    int idx = term->csi_params[i + 2];
                    if (idx >= 0 && idx <= 255) {
                        term->current_attr.bg_mode  = COLOR_256;
                        term->current_attr.bg_index = (uint8_t)idx;
                        term->current_attr.bg       = color256_to_rgb((uint8_t)idx);
                    }
                    i += 2;
                } else if (i + 4 < term->csi_param_count && term->csi_params[i + 1] == 2) {
                    int r = term->csi_params[i + 2];
                    int g = term->csi_params[i + 3];
                    int b = term->csi_params[i + 4];
                    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                        term->current_attr.bg_mode = COLOR_RGB;
                        term->current_attr.bg      = ((uint32_t)r << 16) |
                                                     ((uint32_t)g <<  8) |
                                                      (uint32_t)b;
                    }
                    i += 4;
                }
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
        code = code * 10 + (term->osc_buffer[i] - '0');
    }

    if (code == 0 || code == 2) {
        const char *text = term->osc_buffer + semi + 1;
        (void)snprintf(term->title, sizeof(term->title), "%s", text);
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
                /* Clear from cursor to end of screen */
                TermRow *cr0 = get_screen_row(term, term->cursor.row);
                if (cr0) {
                    for (int c = term->cursor.col; c < term->cols; c++) {
                        cr0->cells[c].codepoint = 0;
                        cr0->cells[c].attr = term->current_attr;
                    }
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
                        rr->dirty = true;
                    }
                }
                TermRow *cr1 = get_screen_row(term, term->cursor.row);
                if (cr1) {
                    for (int c = 0; c <= term->cursor.col; c++) {
                        cr1->cells[c].codepoint = 0;
                        cr1->cells[c].attr = term->current_attr;
                    }
                    cr1->dirty = true;
                }
            } else if (n == 2) {
                /* Clear entire screen */
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
                term->cursor.row = 0;
                term->cursor.col = 0;
            }
            break;
        case 'K': // EL - Erase in Line
            n = get_param(term, 0, 0);
            TermRow *row = get_screen_row(term, term->cursor.row);
            if (row) {
                int start = 0, end = term->cols;
                if (n == 0) start = term->cursor.col; // Cursor to end
                else if (n == 1) end = term->cursor.col + 1; // Start to cursor
                // n=2 is entire line (0 to cols)
                
                for (int c = start; c < end; c++) {
                    row->cells[c].codepoint = 0;
                    row->cells[c].attr = term->current_attr;
                }
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
            if (term->cursor.row >= term->scroll_top &&
                term->cursor.row <= term->scroll_bot) {
                term_scroll_down(term, term->cursor.row,
                                 term->scroll_bot, il_n);
            }
            break;
        }
        case 'M': { /* DL — Delete Lines */
            int dl_n = get_param(term, 0, 1);
            if (term->cursor.row >= term->scroll_top &&
                term->cursor.row <= term->scroll_bot) {
                term_scroll_up(term, term->cursor.row,
                               term->scroll_bot, dl_n);
            }
            break;
        }
        case 'S': { /* SU — Scroll Up */
            int su_n = get_param(term, 0, 1);
            term_scroll_up(term, term->scroll_top, term->scroll_bot, su_n);
            break;
        }
        case 'T': { /* SD — Scroll Down */
            int sd_n = get_param(term, 0, 1);
            term_scroll_down(term, term->scroll_top, term->scroll_bot, sd_n);
            break;
        }
        case '@': { /* ICH — Insert Characters */
            int ich_n = get_param(term, 0, 1);
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

    int prev_cursor_row = term->cursor.row;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];

        switch (term->state) {
            case TERM_STATE_NORMAL:
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
                    if (term->csi_param_count == 0) term->csi_param_count = 1;
                    /* L-4: explicit bounds check before array access */
                    if (term->csi_param_count > 0 &&
                            term->csi_param_count <= TERM_MAX_CSI_PARAMS) {
                        int *p = &term->csi_params[term->csi_param_count - 1];
                        *p = (*p * 10) + (c - '0');
                    }
                } else if (c == ';') {
                    if (term->csi_param_count < TERM_MAX_CSI_PARAMS) {
                        term->csi_param_count++;
                        term->csi_params[term->csi_param_count - 1] = 0;
                    }
                } else if (c >= 0x40 && c <= 0x7E) {
                    if (term->csi_private && (c == 'h' || c == 'l')) {
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
     * so the renderer erases the old cursor block. */
    if (term->cursor.row != prev_cursor_row) {
        int screen_top = (term->lines_count >= term->rows)
                         ? (term->lines_count - term->rows) : 0;
        int logical_old = screen_top + prev_cursor_row;
        if (logical_old >= 0 && logical_old < term->lines_count) {
            int phys = (term->lines_start + logical_old) % term->lines_capacity;
            term->lines[phys]->dirty = true;
        }
    }
}
