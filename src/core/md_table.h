#ifndef NUTSHELL_MD_TABLE_H
#define NUTSHELL_MD_TABLE_H

/*
 * md_table — pure, portable helpers for parsing and laying out markdown
 * tables (`| a | b |` rows). No Win32 dependency; src/ui/md_render.c does
 * the GDI measuring/painting on top of these.
 */

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
