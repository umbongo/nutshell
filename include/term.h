#ifndef TERM_H
#define TERM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TERM_MAX_CSI_PARAMS 16

/* Terminal Attributes */
typedef struct {
    uint32_t fg;
    uint32_t bg;
    uint8_t flags; // 1=Bold, 2=Underline, 4=Blink, 8=Reverse
} TermAttr;

#define TERM_ATTR_BOLD      (1 << 0)
#define TERM_ATTR_UNDERLINE (1 << 1)
#define TERM_ATTR_BLINK     (1 << 2)
#define TERM_ATTR_REVERSE   (1 << 3)

/* Terminal Cell */
typedef struct {
    uint32_t codepoint;
    TermAttr attr;
} TermCell;

/* Terminal Row */
typedef struct {
    TermCell *cells; // Array of 'cols' cells
    int len;         // Length of used cells (optimization)
    bool dirty;      // For rendering
    bool wrapped;    // Line wrapped from previous
} TermRow;

/* Cursor */
typedef struct {
    int row; // 0-based screen row
    int col; // 0-based screen col
    bool visible;
} TermCursor;

/* Parser State Enum */
typedef enum {
    TERM_STATE_NORMAL = 0,
    TERM_STATE_ESC,
    TERM_STATE_CSI,
    TERM_STATE_OSC
} TermState;

/* Terminal State */
typedef struct Terminal {
    int rows;
    int cols;
    int max_scrollback;

    /* Ring buffer of rows */
    TermRow **lines;     // Array of pointers to TermRow
    int lines_capacity;  // rows + max_scrollback
    int lines_start;     // Index of the first valid line (oldest)
    int lines_count;     // Total number of lines (includes scrollback + screen)
    
    /* View state */
    int scrollback_offset; // 0 = at bottom. >0 = viewing history.

    TermCursor cursor;
    TermAttr current_attr;

    /* Parser State */
    TermState state;
    int csi_params[TERM_MAX_CSI_PARAMS];
    int csi_param_count;
    char osc_buffer[256];
    int osc_len;
    
    TermCursor saved_cursor;
} Terminal;

/* Public API */
Terminal *term_init(int rows, int cols, int max_scrollback);
void term_free(Terminal *term);
void term_resize(Terminal *term, int rows, int cols);
void term_scroll(Terminal *term);
void term_process(Terminal *term, const char *data, size_t len);

#endif /* TERM_H */