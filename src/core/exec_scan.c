/* src/core/exec_scan.c -- see exec_scan.h. */
#include "exec_scan.h"
#include "md_table.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* First occurrence of the NUL-terminated `needle` in [from, end), or NULL. */
static const char *exec_find(const char *from, const char *end,
                             const char *needle)
{
    size_t n = strlen(needle);
    if (from >= end) return NULL;
    for (; (size_t)(end - from) >= n; from++) {
        if (*from == needle[0] && memcmp(from, needle, n) == 0) return from;
    }
    return NULL;
}

void exec_scan_init(ExecScan *s, const char *text, size_t len)
{
    s->text = text;
    s->end = text + len;
    s->pos = text;
    s->fences = NULL;
    s->n_fences = 0;
    s->failed = 0;
    s->line_off = (size_t)-1;
    s->code = NULL;
    s->n_code = 0;
    s->cap_code = 0;
    s->line_failed = 0;

    int n = md_code_blocks(text, len, NULL, 0);
    if (n <= 0) return;

    MdCodeSpan *bodies = (MdCodeSpan *)malloc((size_t)n * sizeof(*bodies));
    s->fences = (ExecFence *)malloc((size_t)n * sizeof(*s->fences));
    if (!bodies || !s->fences) {
        free(bodies);
        free(s->fences);
        s->fences = NULL;
        s->failed = 1;   /* fail safe: nothing runs */
        return;
    }
    md_code_blocks(text, len, bodies, n);
    for (int i = 0; i < n; i++) {
        /* Back to the start of the opening fence line... */
        size_t ws = bodies[i].start;
        if (ws > 0 && text[ws - 1] == '\n') ws--;
        while (ws > 0 && text[ws - 1] != '\n') ws--;
        /* ...and on to the end of the closing fence line (an unterminated
         * fence runs to the end of the text). The body's end was trimmed
         * back over CR and LF, so only line breaks lie between it and the
         * closing fence line. */
        size_t we = bodies[i].end;
        while (we < len && (text[we] == '\r' || text[we] == '\n')) we++;
        while (we < len && text[we] != '\n') we++;
        s->fences[i].start = ws;
        s->fences[i].end = we;
    }
    s->n_fences = n;
    free(bodies);
}

void exec_scan_free(ExecScan *s)
{
    free(s->fences);
    s->fences = NULL;
    free(s->code);
    s->code = NULL;
}

/* Record the code spans of one md_parse_inline() run; `base` is the offset
 * of the parsed text within the whole reply. */
static void exec_add_code_spans(ExecScan *s, const MdSpan *spans, int n,
                                size_t base)
{
    for (int i = 0; i < n; i++) {
        if (spans[i].type != MD_SPAN_CODE) continue;
        if (s->n_code == s->cap_code) {
            int cap = s->cap_code ? s->cap_code * 2 : 8;
            size_t *grown = (size_t *)realloc(s->code,
                                              (size_t)cap * 2 * sizeof(size_t));
            if (!grown) { s->line_failed = 1; return; }
            s->code = grown;
            s->cap_code = cap;
        }
        s->code[2 * s->n_code] = base + (size_t)spans[i].start;
        s->code[2 * s->n_code + 1] = base + (size_t)spans[i].end;
        s->n_code++;
    }
}

/* Find the inline code spans of the line starting at `ls`, the way the
 * renderer (src/ui/md_render.c) does: classify the line, then parse its
 * content -- from content_offset for a heading, list item or blockquote,
 * each cell on its own for a table row, the whole line otherwise. Fenced
 * lines never get here (the fence check quotes them first), so the line is
 * classified outside a code block, as the renderer sees it. */
static void exec_load_line(ExecScan *s, size_t ls)
{
    s->line_off = ls;
    s->n_code = 0;
    s->line_failed = 0;

    const char *line = s->text + ls;
    size_t room = (size_t)(s->end - line);
    size_t len = 0;
    while (len < room && line[len] && line[len] != '\n') len++;
    if (len > 0x7FFFFFFF) { s->line_failed = 1; return; }

    char *copy = (char *)malloc(len + 1);
    if (!copy) { s->line_failed = 1; return; }
    memcpy(copy, line, len);
    copy[len] = '\0';

    MdSpan spans[MD_MAX_SPANS];
    MdLineInfo info = md_classify_line(copy, 0);
    if (info.type == MD_LINE_TABLE) {
        MdTableCell cells[MD_TABLE_MAX_COLS];
        int nc = md_table_split_row(copy, cells, MD_TABLE_MAX_COLS);
        for (int i = 0; i < nc; i++) {
            size_t co = (size_t)(cells[i].start - copy);
            int n = md_parse_inline(cells[i].start, cells[i].len, spans);
            exec_add_code_spans(s, spans, n, ls + co);
        }
    } else {
        size_t off = (size_t)info.content_offset;
        if (off > len) off = len;
        int n = md_parse_inline(copy + off, (int)(len - off), spans);
        exec_add_code_spans(s, spans, n, ls + off);
    }
    free(copy);
}

int exec_scan_quoted(ExecScan *s, const char *marker)
{
    if (s->failed) return 1;
    size_t off = (size_t)(marker - s->text);
    for (int i = 0; i < s->n_fences; i++) {
        if (off >= s->fences[i].start && off < s->fences[i].end) return 1;
    }

    size_t ls = off;
    while (ls > 0 && s->text[ls - 1] != '\n') ls--;
    if (s->line_off != ls) exec_load_line(s, ls);
    if (s->line_failed) return 1;   /* fail safe: nothing runs */
    for (int i = 0; i < s->n_code; i++) {
        if (off >= s->code[2 * i] && off < s->code[2 * i + 1]) return 1;
    }
    return 0;
}

int exec_scan_next(ExecScan *s, ExecMark *m)
{
    const char *search = s->pos;
    for (;;) {
        const char *marker = exec_find(search, s->end, "[EXEC]");
        if (!marker) return 0;
        const char *start = marker + 6; /* skip "[EXEC]" */
        const char *end = exec_find(start, s->end, "[/EXEC]");
        if (!end) return 0;

        const char *inner = exec_find(start, s->end, "[EXEC]");
        if (inner && inner < end) {
            search = inner;
            continue;
        }

        m->prev = s->pos;
        m->marker = marker;
        m->start = start;
        m->end = end;
        m->quoted = exec_scan_quoted(s, marker);
        s->pos = end + 7; /* skip "[/EXEC]" */
        return 1;
    }
}
