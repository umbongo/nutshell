/* src/core/chat_copy.c -- see chat_copy.h. */
#include "chat_copy.h"
#include "chat_exec.h"
#include "markdown.h"
#include "md_table.h"
#include "text_runs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── growable output buffer ─────────────────────────────────────────── */

typedef struct { char *p; size_t n, cap; int oom; } CopyBuf;

static void cb_put(CopyBuf *b, const char *s, size_t n)
{
    if (b->oom || n == 0) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n + 1) cap *= 2;
        char *q = (char *)realloc(b->p, cap);
        if (!q) { b->oom = 1; return; }
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}

/* Copy text[a, b) intersected with [lo, hi). */
static void cb_range(CopyBuf *b, const char *text, int a, int e, int lo, int hi)
{
    if (a < lo) a = lo;
    if (e > hi) e = hi;
    if (e > a) cb_put(b, text + a, (size_t)(e - a));
}

static int is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }
static int is_ws(char c) { return c == ' ' || c == '\t'; }

/* ── one source line, NUL-terminated, for the classifier ─────────────── */

typedef struct { char *p; int cap; } LineBuf;

static const char *lb_set(LineBuf *lb, const char *s, int n)
{
    if (n + 1 > lb->cap) {
        int cap = lb->cap ? lb->cap : 256;
        while (cap < n + 1) cap *= 2;
        char *q = (char *)realloc(lb->p, (size_t)cap);
        if (!q) return NULL;
        lb->p = q;
        lb->cap = cap;
    }
    memcpy(lb->p, s, (size_t)n);
    lb->p[n] = '\0';
    return lb->p;
}

/* Span-parsed content of a line: line[cs, ce) is the content and `src`
 * the source offset of line[0]; copies what lies in [lo, hi) (source
 * offsets). Whitespace at the start of the content is skipped across spans,
 * as render_inline_spans drops it at the start of a line. */
static void emit_spans(CopyBuf *b, const char *line, int cs, int ce, int src,
                       int lo, int hi)
{
    if (ce <= cs) return;
    MdSpan spans[MD_MAX_SPANS];
    int n = md_parse_inline(line + cs, ce - cs, spans);
    int at_start = 1;
    for (int i = 0; i < n; i++) {
        int a = cs + spans[i].start, e = cs + spans[i].end;   /* in line */
        if (at_start) {
            while (a < e && is_ws(line[a])) a++;
            if (a < e) at_start = 0;
        }
        int ga = src + a, ge = src + e;                        /* in source */
        if (ga < lo) ga = lo;
        if (ge > hi) ge = hi;
        if (ge > ga) cb_put(b, line + (ga - src), (size_t)(ge - ga));
    }
}

/* ── table block, mirroring md_render_table_block ───────────────────── */

typedef struct { int ls, le; } RowSpan;

/* The rows of the block starting at line [ls, le) of seg_text[0, seg_end)
 * (offsets relative to the segment). Returns the count; *next is the
 * offset just past the last row's newline. */
static int table_rows(const char *t, int seg_end, int ls, int le,
                      RowSpan *rows, int *next, LineBuf *lb)
{
    int n = 0;
    rows[n].ls = ls;
    rows[n].le = le;
    n++;
    int p = (le < seg_end) ? le + 1 : le;
    while (p < seg_end && n < MD_TABLE_MAX_ROWS) {
        int q = p;
        while (q < seg_end && t[q] != '\n') q++;
        const char *line = lb_set(lb, t + p, q - p);
        if (!line || md_classify_line(line, 0).type != MD_LINE_TABLE) break;
        rows[n].ls = p;
        rows[n].le = q;
        n++;
        p = (q < seg_end) ? q + 1 : q;
    }
    *next = p;
    return n;
}

/* Emit one table row's cells 0..ncols-1 that the range touches, joined with
 * '\t'. `src` is the source offset of the segment start. */
static void emit_row(CopyBuf *b, const char *t, RowSpan row, int ncols,
                     int src, int lo, int hi, LineBuf *lb)
{
    const char *line = lb_set(lb, t + row.ls, row.le - row.ls);
    if (!line) return;
    MdTableCell cells[MD_TABLE_MAX_COLS];
    int count = md_table_split_row(line, cells, MD_TABLE_MAX_COLS);
    int first = -1, last = -1;
    int cs[MD_TABLE_MAX_COLS], ce[MD_TABLE_MAX_COLS];
    for (int j = 0; j < ncols; j++) {
        if (j < count) {
            cs[j] = src + row.ls + (int)(cells[j].start - line);
            ce[j] = cs[j] + cells[j].len;
        } else {
            cs[j] = ce[j] = src + row.le;     /* a missing cell: row end */
        }
        /* An empty or missing cell counts when the range reaches its
         * position (inclusive), so a whole row keeps every column. */
        int in = (cs[j] < ce[j]) ? (cs[j] < hi && ce[j] > lo)
                                 : (cs[j] >= lo && cs[j] <= hi);
        if (in) {
            if (first < 0) first = j;
            last = j;
        }
    }
    for (int j = first; j >= 0 && j <= last; j++) {
        if (j > first) cb_put(b, "\t", 1);
        if (j < count)
            emit_spans(b, line, (int)(cells[j].start - line),
                       (int)(cells[j].start - line) + cells[j].len,
                       src + row.ls, lo, hi);
    }
}

/* ── one prose segment ───────────────────────────────────────────────── */

/* Walk the markdown prose segment text[s0, s1) line by line, the state
 * reset, and copy what lies in [lo, hi). */
static void emit_markdown(CopyBuf *b, const char *text, int s0, int s1,
                          int lo, int hi, LineBuf *lb, LineBuf *lb2)
{
    const char *t = text + s0;      /* segment-relative */
    int seg_end = s1 - s0;
    int in_code = 0, olist = 0;
    int p = 0;
    while (p < seg_end) {
        int q = p;
        while (q < seg_end && t[q] != '\n') q++;
        int ls = s0 + p, le = s0 + q;           /* source offsets */
        const char *line = lb_set(lb, t + p, q - p);
        if (!line) { b->oom = 1; return; }
        MdLineInfo info = md_classify_line(line, in_code);

        if (info.type == MD_LINE_TABLE) {
            olist = 0;
            RowSpan rows[MD_TABLE_MAX_ROWS];
            int next = 0;
            int n = table_rows(t, seg_end, p, q, rows, &next, lb);
            /* The painter's column count, written once in md_table.c. */
            const char *hdr = lb_set(lb, t + rows[0].ls, rows[0].le - rows[0].ls);
            const char *sep = (n >= 2)
                ? lb_set(lb2, t + rows[1].ls, rows[1].le - rows[1].ls) : NULL;
            if (!hdr || (n >= 2 && !sep)) { b->oom = 1; return; }
            int has_sep = 0;
            int ncols = md_table_drawn_cols(hdr, sep, &has_sep);
            for (int r = 0; r < n; r++) {
                if (has_sep && r == 1) continue;   /* alignment row: nothing */
                emit_row(b, t, rows[r], ncols, s0, lo, hi, lb);
                int nl = s0 + rows[r].le;          /* the row's '\n', if any */
                if (rows[r].le < seg_end && nl >= lo && nl < hi)
                    cb_put(b, "\n", 1);
            }
            p = next;
            continue;
        }

        switch (info.type) {
        case MD_LINE_CODE_FENCE:
            in_code = !in_code;
            break;
        case MD_LINE_CODE:
            cb_range(b, text, ls, le, lo, hi);
            break;
        case MD_LINE_HEADING:
        case MD_LINE_BLOCKQUOTE:
            emit_spans(b, line, info.content_offset, q - p, ls, lo, hi);
            break;
        case MD_LINE_ULIST:
            /* the marker copies when the range covers a byte of it */
            if (ls < hi && ls + info.content_offset > lo) cb_put(b, "- ", 2);
            emit_spans(b, line, info.content_offset, q - p, ls, lo, hi);
            break;
        case MD_LINE_OLIST: {
            olist++;
            if (ls < hi && ls + info.content_offset > lo) {
                char num[16];
                int k = snprintf(num, sizeof(num), "%d. ", olist);
                if (k > 0) cb_put(b, num, (size_t)k);
            }
            emit_spans(b, line, info.content_offset, q - p, ls, lo, hi);
            break;
        }
        case MD_LINE_PARAGRAPH:
            olist = 0;
            emit_spans(b, line, 0, q - p, ls, lo, hi);
            break;
        case MD_LINE_EMPTY:
            olist = 0;
            break;
        case MD_LINE_HRULE:
        case MD_LINE_TABLE:
            break;
        }
        /* The line break, except after a fence line, which copies as
         * nothing at all (a rule and an empty line keep theirs). */
        if (info.type != MD_LINE_CODE_FENCE && q < seg_end && le >= lo && le < hi)
            cb_put(b, "\n", 1);
        p = (q < seg_end) ? q + 1 : q;
    }
}

int chat_copy_visible(const char *text, int len, int start, int end,
                      int flags, char **out)
{
    if (out) *out = NULL;
    if (!text || !out || len < 0) return -1;
    if (start < 0) start = 0;
    if (end > len) end = len;
    while (start > 0 && start < len && is_cont((unsigned char)text[start]))
        start--;
    while (end < len && end > 0 && is_cont((unsigned char)text[end]))
        end++;

    CopyBuf b = { NULL, 0, 0, 0 };
    LineBuf lb = { NULL, 0 }, lb2 = { NULL, 0 };
    if (start < end) {
        int pos = 0, copied_any = 0, more;
        ChatSeg seg;
        if (flags & CHAT_COPY_EXEC) {
            more = chat_exec_split(text, len, &pos, &seg);
        } else {
            seg.kind = CHAT_SEG_PROSE;
            seg.start = 0;
            seg.end = len;
            more = 1;
        }
        while (more) {
            int touched = seg.start < seg.end &&
                          seg.start < end && seg.end > start;
            if (touched) {
                /* Segments paint on lines of their own: one '\n' between
                 * two that both copy something. */
                CopyBuf piece = { NULL, 0, 0, 0 };
                if (seg.kind == CHAT_SEG_PROSE && (flags & CHAT_COPY_MD))
                    emit_markdown(&piece, text, seg.start, seg.end, start, end,
                                  &lb, &lb2);
                else
                    cb_range(&piece, text, seg.start, seg.end, start, end);
                if (piece.oom) b.oom = 1;
                if (piece.n > 0) {
                    if (copied_any) cb_put(&b, "\n", 1);
                    cb_put(&b, piece.p, piece.n);
                    copied_any = 1;
                }
                free(piece.p);
            }
            more = (flags & CHAT_COPY_EXEC)
                       ? chat_exec_split(text, len, &pos, &seg) : 0;
        }
    }
    free(lb.p);
    free(lb2.p);
    if (!b.p) {                      /* nothing copied: an empty string */
        b.p = (char *)malloc(1);
        if (b.p) b.p[0] = '\0';
    }
    if (b.oom || !b.p) { free(b.p); return -1; }
    *out = b.p;
    return (int)b.n;
}

/* ── across items ───────────────────────────────────────────────────── */

int chat_sel_cmp(ChatSelPos a, ChatSelPos b)
{
    if (a.msg_id != b.msg_id) return a.msg_id < b.msg_id ? -1 : 1;
    if (a.part != b.part) return a.part < b.part ? -1 : 1;
    if (a.off != b.off) return a.off < b.off ? -1 : 1;
    return 0;
}

int chat_sel_part_range(ChatSelPos a, ChatSelPos b, int msg_id, int part,
                        int part_len, int *s, int *e)
{
    if (!s || !e) return 0;
    *s = *e = 0;
    if (part_len < 0) return 0;
    ChatSelPos lo = { msg_id, part, 0 }, hi = { msg_id, part, part_len };
    if (chat_sel_cmp(b, lo) <= 0 || chat_sel_cmp(a, hi) >= 0) return 0;
    *s = (chat_sel_cmp(a, lo) > 0) ? a.off : 0;
    *e = (chat_sel_cmp(b, hi) < 0) ? b.off : part_len;
    if (*s < 0) *s = 0;
    if (*e > part_len) *e = part_len;
    return *e > *s;
}

static int thinking_shown(const ChatMsgItem *it)
{
    return it->type == CHAT_ITEM_AI_TEXT && it->u.ai.thinking_text &&
           it->u.ai.thinking_text[0] && !it->u.ai.thinking_collapsed;
}

/* Append the copy of one part's [s, e) as a "\n\n"-joined piece. */
static void add_piece(CopyBuf *b, const char *text, int len, int s, int e,
                      int flags)
{
    char *piece = NULL;
    int n = chat_copy_visible(text, len, s, e, flags, &piece);
    if (n < 0) { b->oom = 1; return; }
    if (n > 0) {
        if (b->n > 0) cb_put(b, "\n\n", 2);
        cb_put(b, piece, (size_t)n);
    }
    free(piece);
}

size_t chat_copy_selection(const ChatMsgList *list, ChatSelPos a,
                           ChatSelPos b, int markdown, char **out)
{
    if (out) *out = NULL;
    if (!list || !out) return 0;
    if (chat_sel_cmp(a, b) > 0) { ChatSelPos t = a; a = b; b = t; }
    if (chat_sel_cmp(a, b) == 0) return 0;
    const ChatMsgItem *first = chat_msg_find(list, a.msg_id);
    if (!first || !chat_msg_find(list, b.msg_id)) return 0;

    CopyBuf buf = { NULL, 0, 0, 0 };
    for (const ChatMsgItem *it = first; it; it = it->next) {
        if (it->id > b.msg_id) break;
        if (it->type == CHAT_ITEM_COMMAND) continue;
        int s, e;
        if (thinking_shown(it)) {
            const char *th = it->u.ai.thinking_text;
            int tl = (int)strlen(th);
            if (chat_sel_part_range(a, b, it->id, TR_PART_THINKING, tl, &s, &e))
                add_piece(&buf, th, tl, s, e, 0);
        }
        const char *tx = it->text ? it->text : "";
        int len = (int)it->text_len;
        if (chat_sel_part_range(a, b, it->id, TR_PART_MAIN, len, &s, &e)) {
            int flags = 0;
            if (it->type == CHAT_ITEM_AI_TEXT)
                flags = CHAT_COPY_EXEC | (markdown ? CHAT_COPY_MD : 0);
            add_piece(&buf, tx, len, s, e, flags);
        }
    }
    if (buf.oom || buf.n == 0) { free(buf.p); return 0; }
    *out = buf.p;
    return buf.n;
}

int chat_sel_all(const ChatMsgList *list, ChatSelPos *a, ChatSelPos *b)
{
    if (!list || !a || !b) return 0;
    const ChatMsgItem *first = NULL, *last = NULL;
    for (const ChatMsgItem *it = list->head; it; it = it->next) {
        if (it->type == CHAT_ITEM_COMMAND) continue;
        if (it->text_len == 0 && !thinking_shown(it)) continue;
        if (!first) first = it;
        last = it;
    }
    if (!first) return 0;
    a->msg_id = first->id;
    a->part = thinking_shown(first) ? TR_PART_THINKING : TR_PART_MAIN;
    a->off = 0;
    b->msg_id = last->id;
    if (last->text_len > 0) {
        b->part = TR_PART_MAIN;
        b->off = (int)last->text_len;
    } else {
        b->part = TR_PART_THINKING;
        b->off = (int)strlen(last->u.ai.thinking_text);
    }
    return chat_sel_cmp(*a, *b) < 0;
}
