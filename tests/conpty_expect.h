#ifndef NUTSHELL_CONPTY_EXPECT_H
#define NUTSHELL_CONPTY_EXPECT_H

/* tests/conpty_expect.h -- the console probe's four scenarios, and what
 * docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md predicts
 * conhost shows after each resize: section 1.3 (top-anchored: the old
 * screen top stays, rows below the later of the cursor and the last
 * non-blank row are not carried, an overflow scrolls out from the top and
 * the cursor follows its row) with section 2's cursor rule.
 *
 * Pure, portable C. Built into `make test`, where
 * tests/test_conpty_expect.c checks it, and into `make wintest`, where the
 * parity case in tests/test_local_pty.c compares conhost's own buffer with
 * it. conhost's buffer is its viewport, so a scenario's "old screen" is the
 * last old_rows rows of its text laid out at old_cols. */

#include <stddef.h>

#define CPX_MAX_LINES 32
#define CPX_MAX_ROWS  48
#define CPX_MAX_COLS  80

typedef struct {
    const char *name;            /* grow, shrink_bottom, shrink_below, width */
    int old_cols, old_rows;      /* the size the probe child writes at       */
    int new_cols, new_rows;      /* the one resize the case makes            */
    int line_count;
    const char *lines[CPX_MAX_LINES]; /* written with "\r\n" between them    */
    int cursor_line;             /* index into lines                         */
    int cursor_offset;           /* cells from the start of that line        */
} CpxScenario;

typedef struct {
    int rows, cols;
    char text[CPX_MAX_ROWS][CPX_MAX_COLS + 1]; /* trailing spaces trimmed */
    int cursor_row, cursor_col;
} CpxView;

int cpx_scenario_count(void);
const CpxScenario *cpx_scenario_at(int index);       /* NULL out of range */
const CpxScenario *cpx_scenario_find(const char *name); /* NULL if none   */

/* Where a cursor offset cells into a line of len cells lands at cols:
 * inside the content by division; past it, the content's end plus the
 * distance, clamped to cols - 1 (spec section 2). */
void cpx_place(int len, int offset, int cols, int *row_in_line, int *col);

/* conhost's viewport once the child has written the scenario at
 * old_cols x old_rows. Returns 0, or -1 when the scenario cannot be
 * predicted (bad sizes, or a line straddling the viewport's top, whose
 * head conhost no longer has). */
int cpx_initial_view(const CpxScenario *s, CpxView *out);

/* The viewport section 1.3 predicts after the resize to
 * new_cols x new_rows. Returns 0, or -1 as above. */
int cpx_expected_after(const CpxScenario *s, CpxView *out);

/* Number of differences: one per differing row (rows missing from the
 * shorter view count as ""), plus one for the cursor, plus one for the
 * size. 0 means equal. */
int cpx_view_diff(const CpxView *a, const CpxView *b);

/* The line's tag (its text up to the first space) into out; returns its
 * length, 0 for an empty line. Tags are unique within a scenario. */
size_t cpx_tag(const char *line, char *out, size_t out_size);

#endif /* NUTSHELL_CONPTY_EXPECT_H */
