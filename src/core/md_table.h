#ifndef NUTSHELL_MD_TABLE_H
#define NUTSHELL_MD_TABLE_H

/*
 * md_table — pure, portable helpers for parsing and laying out markdown
 * tables (`| a | b |` rows). No Win32 dependency; src/ui/md_render.c does
 * the GDI measuring/painting on top of these.
 */

/* Table block limits, shared by the renderer (src/ui/md_render.c) and the
 * copier (src/core/chat_copy.c) so both draw and copy the same cells: a
 * block holds at most MD_TABLE_MAX_ROWS consecutive table lines (header,
 * alignment row and body; the next line starts a new block) and draws at
 * most MD_TABLE_MAX_COLS columns. */
#define MD_TABLE_MAX_ROWS 64
#define MD_TABLE_MAX_COLS 16

typedef struct {
    const char *start;  /* Points into the caller's line buffer. */
    int         len;
} MdTableCell;

enum { MD_ALIGN_LEFT = 0, MD_ALIGN_CENTER, MD_ALIGN_RIGHT };

/* Split one `|`-delimited table row into trimmed cell views into `line`.
 * Handles a missing outer pipe on either end, `\|` escaped pipes (kept
 * literal in the cell text, never treated as a delimiter), and empty
 * cells. Returns the number of cells found (<= max_cells); a row with
 * fewer cells than expected simply returns fewer -- the caller pads out
 * to the header's column count. Returns 0 for a NULL/empty line or a
 * non-positive max_cells. */
int md_table_split_row(const char *line, MdTableCell *cells, int max_cells);

/* Parse a `|---|:--:|--:|` separator row into per-column alignment
 * (MD_ALIGN_LEFT/CENTER/RIGHT, written to `align[0..returned)`). Returns
 * the number of columns (<= max_cols), or 0 if `sep_line` is not a valid
 * separator row. */
int md_table_alignments(const char *sep_line, int *align, int max_cols);

/* How many columns the renderer draws for a table block, written once so
 * the painter, the hit-test and the copier agree. `header_row` is the
 * block's first line and `second_row` its second (NULL when the block has
 * only one row); both are NUL-terminated.
 *  - If `second_row` is a separator row (md_is_table_separator), *has_sep
 *    is set to 1 (it is not a body row and paints nothing) and the count is
 *    the separator's cell count (md_table_alignments).
 *  - Otherwise *has_sep is 0 and the count is the header's cell count.
 *  - Either way the count is capped at MD_TABLE_MAX_COLS, and is at least 1
 *    (an empty header still draws one column).
 * `has_sep` may be NULL. A NULL `header_row` yields 1. */
int md_table_drawn_cols(const char *header_row, const char *second_row,
                        int *has_sep);

/* Fit `ncols` column natural widths into `avail` pixels: unchanged
 * (`out == natural`) if they already fit; otherwise shrink the widest
 * column(s) first, floored at `minimum[i]`, splitting the reduction
 * evenly among tied-widest columns each round, until the total fits or
 * every column is at its minimum. If even the minimums don't fit avail,
 * `out` is set to `minimum` verbatim -- the table overflows and the
 * caller clips. No-op if any pointer is NULL or ncols <= 0. */
void md_table_fit_columns(const int *natural, const int *minimum, int ncols,
                          int avail, int *out);

#endif /* NUTSHELL_MD_TABLE_H */
