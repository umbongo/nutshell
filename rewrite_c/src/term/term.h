#ifndef CONGA_TERM_TERM_H
#define CONGA_TERM_TERM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TERM_ATTR_BOLD      (1 << 0)
#define TERM_ATTR_UNDERLINE (1 << 1)
#define TERM_ATTR_REVERSE   (1 << 2)
#define TERM_ATTR_BLINK     (1 << 3)

#define TERM_MAX_CSI_PARAMS 16

typedef enum {
    TERM_STATE_NORMAL,
    TERM_STATE_ESC,
    TERM_STATE_CSI,
    TERM_STATE_OSC
} TermState;

/* How a cell's fg/bg colour was specified. */
typedef enum {
    COLOR_DEFAULT = 0, /* use the terminal's configured default */
    COLOR_ANSI16,      /* one of the 16 classic ANSI palette entries */
    COLOR_256,         /* xterm 256-colour palette (fg_index / bg_index) */
    COLOR_RGB          /* 24-bit truecolor */
} ColorMode;

typedef struct {
    uint32_t fg;       /* resolved 0xRRGGBB; 0 = black when mode != COLOR_DEFAULT */
    uint32_t bg;       /* resolved 0xRRGGBB */
    uint8_t  fg_mode;  /* ColorMode */
    uint8_t  bg_mode;  /* ColorMode */
    uint8_t  fg_index; /* raw palette index (0-255) when fg_mode == COLOR_256 */
    uint8_t  bg_index; /* raw palette index (0-255) when bg_mode == COLOR_256 */
    uint8_t  flags;    /* TERM_ATTR_* bitmask */
} TermAttr;

typedef struct {
    uint32_t codepoint;
    TermAttr attr;
} TermCell;

typedef struct {
    TermCell *cells;
    int width;
    int len;
    bool dirty;
    bool wrapped;
} TermRow;

typedef struct {
    int row;
    int col;
    bool visible;
} TermCursor;

typedef struct {
    int rows;
    int cols;

    TermRow **lines; /* Ring buffer of TermRow* */
    int lines_capacity;
    int lines_count;
    int lines_start;

    int scrollback_offset;
    int max_scrollback;

    TermCursor cursor;
    TermAttr current_attr;

    TermState state;
    int csi_params[TERM_MAX_CSI_PARAMS];
    int csi_param_count;
    bool csi_private;     /* true when CSI sequence has '?' prefix */
    char osc_buffer[256];
    int osc_len;
    TermCursor saved_cursor;

    uint32_t utf8_codepoint;
    int utf8_remaining;

    /* Window title (set by OSC 0/2) */
    char title[128];

    /* Terminal modes */
    bool app_cursor_keys; /* ?1h/l  — application cursor key sequences */
    bool insert_mode;     /* 4h/l   — insert vs. replace mode           */

    /* Alternate screen buffer (?1049h/l) */
    bool alt_screen_active;
    TermRow  **primary_lines;
    int        primary_lines_capacity;
    int        primary_lines_count;
    int        primary_lines_start;
    TermCursor primary_cursor;
} Terminal;

Terminal *term_init(int rows, int cols, int max_scrollback);
void term_free(Terminal *term);
void term_resize(Terminal *term, int rows, int cols);
void term_process(Terminal *term, const char *data, size_t len);
void term_scroll(Terminal *term);
void term_alt_screen_enter(Terminal *term);
void term_alt_screen_exit(Terminal *term);

/* Convert an xterm 256-colour palette index to 0xRRGGBB.
 * Indices 0-15:   standard ANSI colours.
 * Indices 16-231: 6×6×6 colour cube.
 * Indices 232-255: 24-step greyscale ramp.
 */
uint32_t color256_to_rgb(uint8_t index);

#endif