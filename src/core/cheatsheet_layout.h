#ifndef NUTSHELL_CHEATSHEET_LAYOUT_H
#define NUTSHELL_CHEATSHEET_LAYOUT_H

#include <stddef.h>
#include "shortcuts.h"
#include "ns_type.h"

/*
 * cheatsheet_layout — portable (Win32-free) layout math for the keyboard
 * shortcuts sheet (spec docs/superpowers/specs/2026-10-20-keyboard-cheat-
 * sheet-design.md section 2.2). Pure C, no windows.h: compiled into both
 * the MinGW build and the native tests (tests/test_cheatsheet_layout.c).
 *
 * The Win32 side (src/ui/cheatsheet_dlg.c) measures the text and the line
 * heights with the real fonts and passes them in; the native test passes
 * fixed per-character estimates instead.
 *
 * Coordinates: everything that scrolls (headings, rules, rows) is in PAGE
 * coordinates, origin at the top-left of the scrolled content; the painter
 * subtracts the scroll offset and adds view.y. view, scrollbar and button
 * are in CLIENT coordinates and never scroll.
 */

enum {
    CS_MAX_SECTIONS = 8,
    CS_MAX_ROWS     = 96,
    CS_LEFT_SECTIONS = 4   /* in two columns: sections 1-4 left, 5-7 right */
};

typedef struct { int x, y, w, h; } CsRect;

typedef struct {
    int dpi;               /* <= 0 is treated as 96 */
    int work_w, work_h;    /* monitor work area, px */
    int nc_w, nc_h;        /* what the frame and caption add to the client size */
    int n_sections;        /* <= CS_MAX_SECTIONS */
    int section_rows[CS_MAX_SECTIONS];  /* rows per section, display order */
    unsigned char has_cond[CS_MAX_ROWS]; /* per row, display order: 1 = condition line */
    int chord_w;           /* widest chord, px */
    int text_w;            /* widest action or condition, px */
    int heading_w;         /* widest section heading, px */
    int body_h;            /* FONT_BODY line height, px */
    int caption_h;         /* FONT_CAPTION line height, px */
    int heading_h;         /* FONT_HEADING line height, px */
    int scrollbar_w;       /* the themed scrollbar's width (CSB_WIDTH), px */
} CsInput;

typedef struct {
    int columns;           /* 1 or 2 */
    int col_w;             /* one column's width */
    int client_w, client_h;
    int page_h;            /* full height of the scrolled content */
    int scrolls;           /* 1 when page_h > view.h */
    CsRect view;           /* the scrolled viewport, client coords */
    CsRect scrollbar;      /* client coords; all zero when !scrolls */
    CsRect button;         /* Close, client coords, below the view */
    int n_sections, n_rows;
    int    heading_col[CS_MAX_SECTIONS];
    CsRect heading[CS_MAX_SECTIONS];   /* page coords */
    CsRect rule[CS_MAX_SECTIONS];      /* border-colour line under the heading */
    int    row_col[CS_MAX_ROWS];
    CsRect row[CS_MAX_ROWS];           /* the whole row, both lines */
    CsRect chord[CS_MAX_ROWS];
    CsRect action[CS_MAX_ROWS];
    CsRect cond[CS_MAX_ROWS];          /* h == 0 when the row has none */
} CsLayout;

/* Measures `text` in `role`'s font, in pixels, at the caller's DPI. The
 * window passes a GDI measurer; the native tests pass an estimate. */
typedef int (*CsMeasureFn)(void *ctx, NsFontRole role, const char *text);

/* Fill the table-derived fields of `in` from the n rows of `t`: n_sections,
 * section_rows[], has_cond[], heading_w (headings via FONT_HEADING),
 * chord_w and text_w (chords and actions via FONT_BODY, conditions via
 * FONT_CAPTION, into text_w). The input is zeroed in those fields first;
 * dpi, work_*, nc_*, the line heights and scrollbar_w are the caller's.
 * Stops at CS_MAX_ROWS rows and CS_MAX_SECTIONS sections. Does nothing
 * when `in`, `t` or `measure` is NULL. */
void cheatsheet_input_from_table(CsInput *in, const Shortcut *t, size_t n,
                                 CsMeasureFn measure, void *ctx);

/* Lay the sheet out. Negative sizes are clamped to 0; n_sections and the
 * row counts are clamped to the CS_MAX_* capacities. `out` may be NULL. */
void cheatsheet_layout_compute(CsLayout *out, const CsInput *in);

/* Clamp a scroll offset to [0, max(0, page_h - view_h)]. */
int cheatsheet_scroll_clamp(int offset, int page_h, int view_h);

/* Where a win_w x win_h window goes: centred on the owner's rectangle,
 * then moved to lie inside the work area (top-left wins when the window is
 * larger than the work area). */
void cheatsheet_place(int owner_x, int owner_y, int owner_w, int owner_h,
                      int win_w, int win_h,
                      int work_x, int work_y, int work_w, int work_h,
                      int *out_x, int *out_y);

#endif /* NUTSHELL_CHEATSHEET_LAYOUT_H */
