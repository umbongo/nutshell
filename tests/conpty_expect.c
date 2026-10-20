/* tests/conpty_expect.c -- see conpty_expect.h. */

#include "conpty_expect.h"
#include <string.h>

/* No line's length is a multiple of old_cols: a legacy console moves the
 * cursor to the next row as soon as a row fills, so "\r\n" after an
 * exactly full row would leave a blank row the layout below does not have
 * (tests/test_conpty_expect.c checks this for every scenario). */
static const CpxScenario SCENARIOS[] = {
    { "grow", 40, 12, 40, 20, 21,
      { "<01> alpha", "<02> alpha", "<03> alpha", "<04> alpha", "<05> alpha",
        "<06> alpha", "<07> alpha", "<08> alpha", "<09> alpha", "<10> alpha",
        "<11> alpha", "<12> alpha", "<13> alpha", "<14> alpha", "<15> alpha",
        "<16> alpha", "<17> alpha", "<18> alpha", "<19> alpha", "<20> alpha",
        "P> " },
      20, 3 },
    { "shrink_bottom", 40, 12, 40, 8, 21,
      { "<01> alpha", "<02> alpha", "<03> alpha", "<04> alpha", "<05> alpha",
        "<06> alpha", "<07> alpha", "<08> alpha", "<09> alpha", "<10> alpha",
        "<11> alpha", "<12> alpha", "<13> alpha", "<14> alpha", "<15> alpha",
        "<16> alpha", "<17> alpha", "<18> alpha", "<19> alpha", "<20> alpha",
        "P> " },
      20, 3 },
    { "shrink_below", 40, 12, 40, 8, 11,
      { "<01> alpha", "<02> alpha", "<03> alpha", "<04> alpha", "<05> alpha",
        "<06> alpha", "P> ", "", "", "", "<B> below" },
      6, 3 },
    { "width", 40, 12, 25, 12, 8,
      { "<W1> abcdefghijklmnopqrstuvwxyz012",
        "<W2> abcdefghijklmnopqrstuvwxyz012",
        "<W3> abcdefghijklmnopqrstuvwxyz012",
        "<WL> abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTU",
        "<W4> abcdefghijklmnopqrstuvwxyz012",
        "<W5> abcdefghijklmnopqrstuvwxyz012",
        "<W6> abcdefghijklmnopqrstuvwxyz012",
        "P> " },
      7, 3 },
};

#define CPX_SCENARIO_COUNT ((int)(sizeof(SCENARIOS) / sizeof(SCENARIOS[0])))
#define CPX_LAYOUT_MAX 128

typedef struct {
    int count;
    char text[CPX_LAYOUT_MAX][CPX_MAX_COLS + 1];
    int line_of[CPX_LAYOUT_MAX];   /* the logical line each row belongs to */
    int chunk_of[CPX_LAYOUT_MAX];  /* 0 on a line's first row              */
} CpxLayout;

int cpx_scenario_count(void)
{
    return CPX_SCENARIO_COUNT;
}

const CpxScenario *cpx_scenario_at(int index)
{
    if (index < 0 || index >= CPX_SCENARIO_COUNT) return NULL;
    return &SCENARIOS[index];
}

const CpxScenario *cpx_scenario_find(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < CPX_SCENARIO_COUNT; i++)
        if (strcmp(SCENARIOS[i].name, name) == 0) return &SCENARIOS[i];
    return NULL;
}

void cpx_place(int len, int offset, int cols, int *row_in_line, int *col)
{
    if (cols < 1) cols = 1;
    if (len < 0) len = 0;
    if (offset < 0) offset = 0;
    if (offset < len) {
        *row_in_line = offset / cols;
        *col = offset % cols;
        return;
    }
    int end_row = (len == 0) ? 0 : (len - 1) / cols;
    int end_col = (len == 0) ? 0 : (len - 1) % cols + 1; /* may equal cols */
    int c = end_col + (offset - len);
    /* This is conhost's cursor (a console reports X = cols - 1 with its own
     * delayed-wrap flag), not the model's pending-wrap column; model_view
     * copies the model's col == cols as is. */
    if (c > cols - 1) c = cols - 1;
    *row_in_line = end_row;
    *col = c;
}

static void trim_right(char *s)
{
    size_t n = strlen(s);
    while (n > 0u && s[n - 1u] == ' ') n--;
    s[n] = '\0';
}

static int line_len(const CpxScenario *s, int i)
{
    return (int)strlen(s->lines[i]);
}

/* Lines first..last laid out at cols, one row per cols cells, an empty
 * line taking one row. Returns the row count, or -1 when it overflows. */
static int layout(const CpxScenario *s, int first, int last, int cols,
                  CpxLayout *lay)
{
    lay->count = 0;
    for (int i = first; i <= last; i++) {
        int len = line_len(s, i);
        int rows = (len == 0) ? 1 : (len + cols - 1) / cols;
        for (int k = 0; k < rows; k++) {
            if (lay->count >= CPX_LAYOUT_MAX) return -1;
            int start = k * cols;
            int n = len - start;
            if (n > cols) n = cols;
            if (n < 0) n = 0;
            char *dst = lay->text[lay->count];
            if (n > 0) memcpy(dst, s->lines[i] + start, (size_t)n);
            dst[n] = '\0';
            trim_right(dst);
            lay->line_of[lay->count] = i;
            lay->chunk_of[lay->count] = k;
            lay->count++;
        }
    }
    return lay->count;
}

static int first_row_of(const CpxLayout *lay, int line)
{
    for (int r = 0; r < lay->count; r++)
        if (lay->line_of[r] == line) return r;
    return -1;
}

static int sizes_ok(const CpxScenario *s)
{
    return s && s->line_count > 0 && s->line_count <= CPX_MAX_LINES &&
           s->cursor_line >= 0 && s->cursor_line < s->line_count &&
           s->old_cols >= 1 && s->old_cols <= CPX_MAX_COLS &&
           s->new_cols >= 1 && s->new_cols <= CPX_MAX_COLS &&
           s->old_rows >= 1 && s->old_rows <= CPX_MAX_ROWS &&
           s->new_rows >= 1 && s->new_rows <= CPX_MAX_ROWS;
}

/* The old screen: the last old_rows rows at old_cols, and its top row. */
static int old_screen(const CpxScenario *s, CpxLayout *lay, int *top)
{
    if (layout(s, 0, s->line_count - 1, s->old_cols, lay) < 0) return -1;
    *top = (lay->count > s->old_rows) ? lay->count - s->old_rows : 0;
    if (lay->chunk_of[*top] != 0) return -1;
    return 0;
}

int cpx_initial_view(const CpxScenario *s, CpxView *out)
{
    static CpxLayout lay;
    int top = 0;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!sizes_ok(s) || old_screen(s, &lay, &top) < 0) return -1;

    out->rows = s->old_rows;
    out->cols = s->old_cols;
    for (int r = 0; r < s->old_rows; r++)
        if (top + r < lay.count)
            memcpy(out->text[r], lay.text[top + r], sizeof(out->text[r]));

    int rr = 0, cc = 0;
    cpx_place(line_len(s, s->cursor_line), s->cursor_offset, s->old_cols,
              &rr, &cc);
    out->cursor_row = first_row_of(&lay, s->cursor_line) + rr - top;
    out->cursor_col = cc;
    return (out->cursor_row >= 0 && out->cursor_row < s->old_rows) ? 0 : -1;
}

int cpx_expected_after(const CpxScenario *s, CpxView *out)
{
    static CpxLayout old_lay, lay;
    CpxView before;
    int top = 0;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (cpx_initial_view(s, &before) < 0) return -1;
    if (old_screen(s, &old_lay, &top) < 0) return -1;

    /* E: the later of the cursor's row and the last non-blank row of the
     * old screen; rows after it are blank and are not carried. */
    int e_row = before.cursor_row;
    for (int r = s->old_rows - 1; r > e_row; r--) {
        if (before.text[r][0] != '\0') { e_row = r; break; }
    }
    int first_line = old_lay.line_of[top];
    int last_line = old_lay.line_of[top + e_row];

    int seg = layout(s, first_line, last_line, s->new_cols, &lay);
    if (seg < 0) return -1;
    int excess = (seg > s->new_rows) ? seg - s->new_rows : 0;

    out->rows = s->new_rows;
    out->cols = s->new_cols;
    for (int r = 0; r < s->new_rows; r++)
        if (excess + r < seg)
            memcpy(out->text[r], lay.text[excess + r], sizeof(out->text[r]));

    int rr = 0, cc = 0;
    cpx_place(line_len(s, s->cursor_line), s->cursor_offset, s->new_cols,
              &rr, &cc);
    out->cursor_row = first_row_of(&lay, s->cursor_line) + rr - excess;
    if (out->cursor_row < 0) out->cursor_row = 0;   /* its row scrolled out */
    out->cursor_col = cc;
    return 0;
}

int cpx_view_diff(const CpxView *a, const CpxView *b)
{
    int diff = 0;
    if (a->rows != b->rows || a->cols != b->cols) diff++;
    if (a->cursor_row != b->cursor_row || a->cursor_col != b->cursor_col)
        diff++;
    int rows = a->rows > b->rows ? a->rows : b->rows;
    if (rows > CPX_MAX_ROWS) rows = CPX_MAX_ROWS;
    for (int r = 0; r < rows; r++) {
        const char *ta = (r < a->rows) ? a->text[r] : "";
        const char *tb = (r < b->rows) ? b->text[r] : "";
        if (strcmp(ta, tb) != 0) diff++;
    }
    return diff;
}

size_t cpx_tag(const char *line, char *out, size_t out_size)
{
    size_t n = 0u;
    if (!out || out_size == 0u) return 0u;
    while (line && line[n] && line[n] != ' ' && n + 1u < out_size) {
        out[n] = line[n];
        n++;
    }
    out[n] = '\0';
    return n;
}
