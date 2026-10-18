/* tests/fake_md_painter.h -- a native model of the chat panel's paint pass,
 * for the painted-equals-copied tests (spec 2026-10-18 section 7). */
#ifndef NUTSHELL_FAKE_MD_PAINTER_H
#define NUTSHELL_FAKE_MD_PAINTER_H

#include "text_runs.h"

#define FAKE_MAX_RUNS  1024
#define FAKE_MAX_LINES 256

/* One source line as the fake painter walked it. ncols > 0 for a table
 * row (its column count), -1 for a table alignment row, 0 otherwise. */
typedef struct { int ls, le; int first_run, n_runs; int ncols; } FakeLine;

typedef struct {
    TextRuns runs;
    int      cell_of[FAKE_MAX_RUNS];   /* table column of each run, else -1 */
    FakeLine lines[FAKE_MAX_LINES];
    int      n_lines;
    int      offset_errors;            /* runs whose bytes != the word drawn */
} FakePaint;

/* Walk an AI reply's text the way chat_listview + md_render paint it --
 * segments from chat_exec_split, md_render_core's line walk per segment
 * (state reset), render_inline_spans' tokens (md_next_word over
 * md_parse_inline spans, whitespace dropped at a line start), one run per
 * code line, a synthetic run per bullet or number, table cells at their
 * offset in the row, MD_PLAIN lines for commands and plain prose -- with no
 * wrapping (one visual line per source line), appending runs with the same
 * base-offset arithmetic md_render.c uses. `fp` must be zeroed first. */
void fake_paint(const char *text, int len, int markdown, FakePaint *fp);
void fake_paint_free(FakePaint *fp);

/* The painted text of fp->lines[i]: each run's synthetic text or bytes in
 * order; for a table row, column by column joined with '\t' (ncols
 * columns, empty ones included). Returns the length written to out. */
int fake_line_text(const FakePaint *fp, const char *text, int i,
                   char *out, int cap);

#endif
