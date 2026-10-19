/* tests/fake_md_painter.c -- see fake_md_painter.h. Keep in step with
 * src/ui/md_render.c: every rule here is one the real renderer follows. */
#include "fake_md_painter.h"
#include "chat_exec.h"
#include "markdown.h"
#include "md_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_line(FakePaint *fp, int ls, int le, int ncols)
{
    if (fp->n_lines >= FAKE_MAX_LINES) return;
    FakeLine *l = &fp->lines[fp->n_lines++];
    l->ls = ls; l->le = le; l->ncols = ncols;
    l->first_run = fp->runs.n;
    l->n_runs = 0;
}

/* Append one run for source bytes [start, end); `word` is the pointer the
 * renderer would have handed TextOutW (into its own line copy). */
static void add_run(FakePaint *fp, const char *text, int start, int end,
                    const char *word, int flags, const char *synth, int cell)
{
    if (fp->runs.n >= FAKE_MAX_RUNS) return;
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.x = fp->runs.n * 10;               /* left to right, never wrapping */
    t.y = fp->n_lines * 20;
    t.w = synth ? 10 : 6 * (end - start);
    t.h = 16;
    t.start = start; t.end = end;
    t.part = TR_PART_MAIN; t.flags = flags;
    if (synth) snprintf(t.synthetic, sizeof(t.synthetic), "%s", synth);
    else if (word && end > start &&
             memcmp(text + start, word, (size_t)(end - start)) != 0)
        fp->offset_errors++;
    int cum[2] = { 0, t.w }, cb[2] = { 0, end - start };
    int idx = text_runs_append(&fp->runs, &t, cum, cb, synth ? 0 : 2);
    if (idx >= 0) {
        fp->cell_of[idx] = cell;
        if (fp->n_lines > 0) fp->lines[fp->n_lines - 1].n_runs++;
    }
}

/* render_inline_spans: tokens of line[cs, ce); src = source offset of
 * line[0]. Whitespace tokens at the start of the line are dropped. */
static void tokens(FakePaint *fp, const char *text, const char *line,
                   int cs, int ce, int src, int cell)
{
    if (ce <= cs) return;
    const char *c = line + cs;
    MdSpan spans[MD_MAX_SPANS];
    int n = md_parse_inline(c, ce - cs, spans);
    int at_start = 1;
    for (int i = 0; i < n; i++) {
        int off = spans[i].start;
        while (off < spans[i].end) {
            int ws = 0, we = 0;
            if (!md_next_word(c, spans[i].end, off, &ws, &we)) break;
            int is_ws = (c[ws] == ' ' || c[ws] == '\t');
            if (!(is_ws && at_start)) {
                add_run(fp, text, src + cs + ws, src + cs + we, c + ws, 0,
                        NULL, cell);
                at_start = 0;
            }
            off = we;
        }
    }
}

/* MD_PLAIN: every token kept, leading whitespace included. */
static void plain(FakePaint *fp, const char *text, int s0, int s1)
{
    int pos = 0, ls, le;
    while (text_next_line(text + s0, s1 - s0, &pos, &ls, &le)) {
        add_line(fp, s0 + ls, s0 + le, 0);
        if (le == ls) {
            add_run(fp, text, s0 + ls, s0 + ls, NULL, 0, NULL, -1);
            continue;
        }
        const char *line = text + s0 + ls;
        int off = 0;
        while (off < le - ls) {
            int ws = 0, we = 0;
            if (!md_next_word(line, le - ls, off, &ws, &we)) break;
            add_run(fp, text, s0 + ls + ws, s0 + ls + we, line + ws, 0,
                    NULL, -1);
            off = we;
        }
    }
}

static char *dup_line(const char *s, int n)
{
    char *p = (char *)malloc((size_t)n + 1);
    if (!p) return NULL;
    memcpy(p, s, (size_t)n);
    p[n] = '\0';
    return p;
}

/* md_render_core over the segment text[s0, s1), with base s0. */
static void markdown_seg(FakePaint *fp, const char *text, int s0, int s1)
{
    int in_code = 0, olist = 0, p = s0;
    while (p < s1) {
        int q = p;
        while (q < s1 && text[q] != '\n') q++;
        char *line = dup_line(text + p, q - p);
        if (!line) return;
        MdLineInfo info = md_classify_line(line, in_code);

        if (info.type == MD_LINE_TABLE) {
            olist = 0;
            int rs[MD_TABLE_MAX_ROWS], re[MD_TABLE_MAX_ROWS], n = 0;
            rs[n] = p; re[n] = q; n++;
            int r = (q < s1) ? q + 1 : q;
            while (r < s1 && n < MD_TABLE_MAX_ROWS) {
                int e = r;
                while (e < s1 && text[e] != '\n') e++;
                char *l2 = dup_line(text + r, e - r);
                int is_t = l2 && md_classify_line(l2, 0).type == MD_LINE_TABLE;
                free(l2);
                if (!is_t) break;
                rs[n] = r; re[n] = e; n++;
                r = (e < s1) ? e + 1 : e;
            }
            char *h = dup_line(text + rs[0], re[0] - rs[0]);
            char *sep = n >= 2 ? dup_line(text + rs[1], re[1] - rs[1]) : NULL;
            int has_sep = 0;
            int ncols = md_table_drawn_cols(h, sep, &has_sep);
            free(h);
            free(sep);
            for (int k = 0; k < n; k++) {
                if (has_sep && k == 1) { add_line(fp, rs[k], re[k], -1); continue; }
                add_line(fp, rs[k], re[k], ncols);
                char *row = dup_line(text + rs[k], re[k] - rs[k]);
                if (!row) continue;
                MdTableCell cells[MD_TABLE_MAX_COLS];
                int count = md_table_split_row(row, cells, MD_TABLE_MAX_COLS);
                for (int j = 0; j < ncols && j < count; j++) {
                    int coff = (int)(cells[j].start - row);
                    tokens(fp, text, row, coff, coff + cells[j].len, rs[k], j);
                }
                free(row);
            }
            free(line);
            p = r;
            continue;
        }

        add_line(fp, p, q, 0);
        switch (info.type) {
        case MD_LINE_CODE_FENCE:
            in_code = !in_code;
            break;
        case MD_LINE_CODE:
            add_run(fp, text, p, q, line, TR_FLAG_CODE, NULL, -1);
            break;
        case MD_LINE_HEADING:
        case MD_LINE_BLOCKQUOTE:
            tokens(fp, text, line, info.content_offset, q - p, p, -1);
            break;
        case MD_LINE_ULIST:
            add_run(fp, text, p, p + info.content_offset, NULL, 0, "- ", -1);
            tokens(fp, text, line, info.content_offset, q - p, p, -1);
            break;
        case MD_LINE_OLIST: {
            char num[16];
            snprintf(num, sizeof(num), "%d. ", ++olist);
            add_run(fp, text, p, p + info.content_offset, NULL, 0, num, -1);
            tokens(fp, text, line, info.content_offset, q - p, p, -1);
            break;
        }
        case MD_LINE_PARAGRAPH:
            olist = 0;
            tokens(fp, text, line, 0, q - p, p, -1);
            break;
        case MD_LINE_EMPTY:
            olist = 0;
            add_run(fp, text, p, p, NULL, 0, NULL, -1);
            break;
        case MD_LINE_HRULE:
        case MD_LINE_TABLE:
            break;
        }
        free(line);
        p = (q < s1) ? q + 1 : q;
    }
}

void fake_paint(const char *text, int len, int markdown, FakePaint *fp)
{
    int pos = 0;
    ChatSeg seg;
    while (chat_exec_split(text, len, &pos, &seg)) {
        if (seg.kind == CHAT_SEG_PROSE && markdown)
            markdown_seg(fp, text, seg.start, seg.end);
        else
            plain(fp, text, seg.start, seg.end);
    }
}

void fake_paint_free(FakePaint *fp)
{
    text_runs_free(&fp->runs);
}

static int put(char *out, int n, int cap, const char *s, int k)
{
    if (n + k >= cap) k = cap - 1 - n;
    if (k > 0) { memcpy(out + n, s, (size_t)k); n += k; }
    out[n] = '\0';
    return n;
}

int fake_line_text(const FakePaint *fp, const char *text, int i,
                   char *out, int cap)
{
    const FakeLine *l = &fp->lines[i];
    int n = 0;
    out[0] = '\0';
    int cols = l->ncols > 0 ? l->ncols : 1;
    for (int j = 0; j < cols; j++) {
        if (l->ncols > 0 && j > 0) n = put(out, n, cap, "\t", 1);
        for (int k = l->first_run; k < l->first_run + l->n_runs; k++) {
            const TextRun *t = &fp->runs.v[k];
            if (l->ncols > 0 && fp->cell_of[k] != j) continue;
            if (t->synthetic[0])
                n = put(out, n, cap, t->synthetic, (int)strlen(t->synthetic));
            else
                n = put(out, n, cap, text + t->start, t->end - t->start);
        }
    }
    return n;
}
