#include "voice_speak.h"

#include "markdown.h"
#include "secure_zero.h"

#include <stdlib.h>
#include <string.h>

#define CODE_PHRASE  "The code is on screen."
#define TABLE_PHRASE "The table is on screen."

typedef struct {
    char  *p;
    size_t len, cap;
    int    oom;
} Buf;

static void buf_put(Buf *b, const char *s, size_t n)
{
    if (b->oom || n == 0) return;
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        while (nc < b->len + n + 1) nc *= 2;
        /* not realloc: the old block would be freed unwiped (it holds the reply's words) */
        char *np = (char *)malloc(nc);
        if (!np) { b->oom = 1; return; }
        if (b->p) {
            memcpy(np, b->p, b->len + 1);
            secure_zero(b->p, b->cap);
            free(b->p);
        }
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void buf_ch(Buf *b, char c) { buf_put(b, &c, 1); }

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int is_alnum(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (unsigned char)c >= 0x80;
}

/* Remove <think>...</think> and [EXEC]...[/EXEC] spans. An unterminated opener
 * drops everything to the end of the text (so the closer search runs at most
 * once without a hit: linear). */
static char *strip_spans(const char *s, size_t n)
{
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    size_t o = 0, i = 0;
    while (i < n) {
        if (s[i] == '<' && n - i >= 7 && memcmp(s + i, "<think>", 7) == 0) {
            const char *e = NULL;
            for (size_t j = i + 7; j + 8 <= n; j++)
                if (s[j] == '<' && memcmp(s + j, "</think>", 8) == 0) { e = s + j; break; }
            if (e) { i = (size_t)(e - s) + 8; out[o++] = ' '; continue; }
            break; /* unterminated: reasoning must never be read aloud */
        }
        if (s[i] == '[' && n - i >= 6 && memcmp(s + i, "[EXEC]", 6) == 0) {
            const char *e = NULL;
            for (size_t j = i + 6; j + 7 <= n; j++)
                if (s[j] == '[' && memcmp(s + j, "[/EXEC]", 7) == 0) { e = s + j; break; }
            if (e) { i = (size_t)(e - s) + 7; out[o++] = ' '; continue; }
            break; /* unterminated: a command must never be read aloud */
        }
        out[o++] = s[i++];
    }
    out[o] = '\0';
    return out;
}

/* Find, within `lim` bytes of s[from], the char c; returns index or -1. */
static long find_bounded(const char *s, size_t from, size_t n, char c, size_t lim)
{
    size_t end = from + lim < n ? from + lim : n;
    for (size_t i = from; i < end; i++) {
        if (s[i] == '\n') return -1;
        if (s[i] == c) return (long)i;
    }
    return -1;
}

/* Index of the ')' closing a '(' already consumed, counting nested pairs;
 * -1 when not within lim bytes on this line. */
static long find_close_paren(const char *s, size_t from, size_t n, size_t lim)
{
    size_t end = from + lim < n ? from + lim : n;
    int depth = 1;
    for (size_t i = from; i < end; i++) {
        if (s[i] == '\n') return -1;
        if (s[i] == '(') depth++;
        else if (s[i] == ')' && --depth == 0) return (long)i;
    }
    return -1;
}

/* Inline text of one line: drops emphasis marks, backticks and bare URLs,
 * reads [text](url) as text. */
static void inline_text(Buf *b, const char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        if (c == '`' || c == '*' || c == '~') { i++; continue; }
        if (c == '_') {
            int l = i > 0 && is_alnum(s[i - 1]);
            int r = i + 1 < n && is_alnum(s[i + 1]);
            if (!(l && r)) { i++; continue; }
        }
        if (c == '!' && i + 1 < n && s[i + 1] == '[') { i++; continue; }
        if (c == '[') {
            long close = find_bounded(s, i + 1, n, ']', 1000);
            if (close >= 0 && (size_t)close + 1 < n && s[close + 1] == '(') {
                long pc = find_close_paren(s, (size_t)close + 2, n, 2000);
                if (pc >= 0) {
                    inline_text(b, s + i + 1, (size_t)close - i - 1);
                    i = (size_t)pc + 1;
                    continue;
                }
            }
        }
        if ((c == 'h' || c == 'H' || c == 'w') && (i == 0 || !is_alnum(s[i - 1]))) {
            size_t k = 0;
            if (n - i >= 7 && strncmp(s + i, "http://", 7) == 0) k = 7;
            else if (n - i >= 8 && strncmp(s + i, "https://", 8) == 0) k = 8;
            else if (n - i >= 4 && strncmp(s + i, "www.", 4) == 0) k = 4;
            if (k) {
                i += k;
                while (i < n && !is_ws(s[i])) i++;
                continue;
            }
        }
        if (c == '\xC2' && i + 1 < n && s[i + 1] == '\xA0') { buf_ch(b, ' '); i += 2; continue; }
        buf_ch(b, c);
        i++;
    }
}

/* 1 when text[0..len) already ends in closing punctuation. */
static int closed(const char *s, size_t len)
{
    while (len > 0) {
        char c = s[len - 1];
        if (c == ')' || c == '"' || c == '\'' || c == ']') { len--; continue; }
        if (len >= 3 && (unsigned char)s[len - 3] == 0xE2 && (unsigned char)s[len - 2] == 0x80
            && ((unsigned char)c == 0x9D || (unsigned char)c == 0x99)) { len -= 3; continue; }
        break;
    }
    if (len == 0) return 1;
    char c = s[len - 1];
    return c == '.' || c == '!' || c == '?' || c == ':' || c == ';';
}

/* Append one list item or heading: its inline text, plus '.' when it has no
 * closing punctuation, plus a space. */
static void put_item(Buf *b, const char *s, size_t n)
{
    size_t start = b->len;
    inline_text(b, s, n);
    if (b->oom) return;
    size_t e = b->len;
    while (e > start && is_ws(b->p[e - 1])) e--;
    int has_content = 0;
    for (size_t k = start; k < e; k++) if (!is_ws(b->p[k])) { has_content = 1; break; }
    b->len = e;
    if (has_content && !closed(b->p + start, e - start)) buf_ch(b, '.');
    buf_ch(b, ' ');
}

/* Strip leading blockquote marks ('>' and one optional space, up to 3 spaces
 * of indent before each) from raw[0..len); returns the offset of the rest. */
static size_t quote_off(const char *raw, size_t len)
{
    size_t o = 0;
    for (;;) {
        size_t k = o;
        while (k < len && k - o < 3 && raw[k] == ' ') k++;
        if (k < len && raw[k] == '>') {
            k++;
            if (k < len && raw[k] == ' ') k++;
            o = k;
        } else {
            return o;
        }
    }
}

static int has_pipe(const char *s, size_t n)
{
    return n > 0 && memchr(s, '|', n) != NULL;
}

/* A table separator row: only '-', ':', '|' and blanks, with a '-', and a
 * pipe either in it or in the row above. */
static int is_sep_row(const char *s, size_t n, int above_has_pipe)
{
    int dash = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '-') dash = 1;
        else if (c != ':' && c != '|' && c != ' ' && c != '\t') return 0;
    }
    return dash && (has_pipe(s, n) || above_has_pipe);
}

/* A list marker at s[i] ("-", "*", "+", or digits then "." or ")", then a blank):
 * the index of the first byte after it and its blanks, else i. */
static size_t after_list_marker(const char *s, size_t n, size_t i)
{
    size_t k = i;
    if (k < n && (s[k] == '-' || s[k] == '*' || s[k] == '+')) {
        k++;
    } else {
        while (k < n && s[k] >= '0' && s[k] <= '9') k++;
        if (k == i || k >= n || (s[k] != '.' && s[k] != ')')) return i;
        k++;
    }
    if (k >= n || (s[k] != ' ' && s[k] != '\t')) return i;
    while (k < n && (s[k] == ' ' || s[k] == '\t')) k++;
    return k;
}

/* Fence run at the start of s after any leading blanks (an indent of four or more
 * spaces, or a tab, under a list item is still a fence) and one list marker: returns
 * the run length (>= 3), sets *ch and, when at is given, the index where the run
 * starts; else 0. */
static size_t fence_run(const char *s, size_t n, char *ch, size_t *at)
{
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    i = after_list_marker(s, n, i);
    if (i >= n || (s[i] != '`' && s[i] != '~')) return 0;
    char c = s[i];
    size_t k = i;
    while (k < n && s[k] == c) k++;
    if (k - i < 3) return 0;
    /* A backtick fence's info string holds no backtick (CommonMark): "```x``` text" is an
     * inline span, and treating it as an opener would hide the rest of the reply. In column 0
     * (i == 0) the chat renderer toggles on the first three bytes alone, so the speech does too. */
    if (c == '`' && i > 0)
        for (size_t j = k; j < n; j++)
            if (s[j] == '`') return 0;
    *ch = c;
    if (at) *at = i;
    return k - i;
}

char *voice_speak_text(const char *markdown)
{
    if (!markdown) markdown = "";
    char *src = strip_spans(markdown, strlen(markdown));
    if (!src) return NULL;
    size_t n = strlen(src);

    Buf out = {0};
    char  *line = NULL;
    size_t linecap = 0;
    int in_table = 0, table_rows = 0, in_icode = 0, prev_blank = 1;
    int in_fence = 0;
    char fch = 0;
    size_t flen = 0;
    size_t pos = 0;
    while (pos < n) {
        size_t e = pos;
        while (e < n && src[e] != '\n') e++;
        size_t ll = e - pos;
        if (ll > 0 && src[pos + ll - 1] == '\r') ll--;
        size_t qo = quote_off(src + pos, ll);
        const char *q = src + pos + qo;
        size_t ql = ll - qo;
        /* the next line, for table detection */
        size_t np = e < n ? e + 1 : n, ne = np;
        while (ne < n && src[ne] != '\n') ne++;
        size_t nl_len = ne - np;
        if (nl_len > 0 && src[np + nl_len - 1] == '\r') nl_len--;
        size_t nqo = quote_off(src + np, nl_len);
        const char *nq = src + np + nqo;
        size_t nql = nl_len - nqo;

        if (ql + 1 > linecap) {
            size_t nc = linecap ? linecap : 256;
            while (nc < ql + 1) nc *= 2;
            char *nl = (char *)malloc(nc);
            if (!nl) {
                secure_zero(src, strlen(src) + 1); free(src);
                if (line) secure_zero(line, linecap);
                free(line);
                if (out.p) secure_zero(out.p, out.cap);
                free(out.p);
                return NULL;
            }
            if (line) { secure_zero(line, linecap); free(line); }
            line = nl;
            linecap = nc;
        }
        memcpy(line, q, ql);
        line[ql] = '\0';
        pos = np;

        /* fences: ``` or ~~~, closed by the same char, at least as long */
        char rc = 0;
        size_t at = 0;
        size_t run = fence_run(line, ql, &rc, &at);
        if (in_fence) {
            if (fch == '`' && md_classify_line(line, 1).type == MD_LINE_CODE_FENCE) {
                in_fence = 0;   /* the renderer closes on any column-0 line starting with ``` */
            } else if (run >= flen && rc == fch) {
                size_t k = at + run;
                while (k < ql && (line[k] == ' ' || line[k] == '\t')) k++;
                if (k == ql) in_fence = 0;
            }
            continue;
        }
        if (run) {
            buf_put(&out, CODE_PHRASE " ", sizeof(CODE_PHRASE));
            in_fence = 1; fch = rc; flen = run;
            in_table = in_icode = 0;
            prev_blank = 0;
            continue;
        }

        size_t lead = 0;
        while (lead < ql && (line[lead] == ' ' || line[lead] == '\t')) lead++;
        int blank = (lead == ql);
        int pipe_here = has_pipe(line, ql);

        if (in_table) {
            if (!blank && (pipe_here || (table_rows == 1 && is_sep_row(line, ql, 1)))) {
                table_rows++;
                prev_blank = 0;
                continue;
            }
            in_table = 0;
        }
        if (!blank) {
            MdLineInfo info = md_classify_line(line, 0);
            int starts = info.type == MD_LINE_TABLE
                || (nql > 0 && !is_sep_row(line, ql, 0) && is_sep_row(nq, nql, pipe_here)
                    && (pipe_here || has_pipe(nq, nql)));
            if (starts) {
                buf_put(&out, TABLE_PHRASE " ", sizeof(TABLE_PHRASE));
                in_table = 1; table_rows = 1; in_icode = 0; prev_blank = 0;
                continue;
            }
        }

        if (!blank && lead >= 1 && (line[0] == '\t' || strncmp(line, "    ", 4) == 0)
            && (prev_blank || in_icode)) {
            if (!in_icode) buf_put(&out, CODE_PHRASE " ", sizeof(CODE_PHRASE));
            in_icode = 1; prev_blank = 0;
            continue;
        }
        if (blank) {
            prev_blank = 1;
            buf_ch(&out, ' ');
            continue;
        }
        in_icode = 0;
        prev_blank = 0;

        const char *t = line + lead;
        MdLineInfo ti = md_classify_line(t, 0);
        switch (ti.type) {
        case MD_LINE_HEADING:
        case MD_LINE_ULIST:
        case MD_LINE_OLIST:
            put_item(&out, t + ti.content_offset, ql - lead - (size_t)ti.content_offset);
            break;
        case MD_LINE_HRULE:
            buf_ch(&out, ' ');
            break;
        default:
            inline_text(&out, t, ql - lead);
            buf_ch(&out, ' ');
            break;
        }
    }
    secure_zero(src, strlen(src) + 1);
    free(src);
    if (line) secure_zero(line, linecap);
    free(line);
    if (out.oom) { if (out.p) secure_zero(out.p, out.cap); free(out.p); return NULL; }

    /* collapse whitespace, trim */
    char *res = (char *)malloc(out.len + 1);
    if (!res) { secure_zero(out.p, out.cap); free(out.p); return NULL; }
    size_t o = 0;
    int pend = 0;
    for (size_t i = 0; i < out.len; i++) {
        if (is_ws(out.p[i])) { pend = 1; continue; }
        if (pend && o > 0) res[o++] = ' ';
        pend = 0;
        res[o++] = out.p[i];
    }
    res[o] = '\0';
    secure_zero(out.p, out.cap);
    free(out.p);
    return res;
}

/* ---- chunking ---- */

/* End (exclusive) of the sentence starting at text[pos], looking no further
 * than text[pos+limit]; (size_t)-1 when it does not end within limit bytes. */
static size_t sentence_end(const char *t, size_t pos, size_t limit)
{
    for (size_t i = pos; t[i] != '\0'; i++) {
        if (i - pos >= limit) return (size_t)-1;
        char c = t[i];
        if ((c == '.' || c == '!' || c == '?') && (t[i + 1] == '\0' || is_ws(t[i + 1]))) {
            if (i + 1 - pos > limit) return (size_t)-1;
            return i + 1;
        }
    }
    return strlen(t + pos) <= limit ? pos + strlen(t + pos) : (size_t)-1;
}

size_t voice_speak_chunks(const char *text, size_t from, size_t *starts, size_t *lens,
                          size_t max)
{
    if (!text || !starts || !lens || max == 0) return 0;
    size_t total = strlen(text);
    if (from > total) return 0;
    size_t pos = from, count = 0;
    while (count < max) {
        while (text[pos] && is_ws(text[pos])) pos++;
        if (!text[pos]) break;
        size_t limit = count == 0 ? VOICE_SPEAK_FIRST_CHUNK : VOICE_SPEAK_CHUNK_MAX;
        size_t end = pos;
        size_t scan = pos;
        for (;;) {
            while (text[scan] && is_ws(text[scan])) scan++;
            if (!text[scan]) break;
            if (scan - pos >= limit) break;
            size_t se = sentence_end(text, scan, limit - (scan - pos));
            if (se == (size_t)-1) break;
            end = se;
            scan = se;
        }
        if (end == pos) {
            /* first sentence is over the limit: split it */
            size_t lim_end = pos;
            while (lim_end < pos + limit && text[lim_end]) lim_end++;
            if (!text[lim_end] && lim_end - pos <= limit) {
                end = lim_end;
            } else {
                size_t sp = lim_end;
                while (sp > pos && !is_ws(text[sp])) sp--;
                if (sp > pos) end = sp;
                else {
                    end = lim_end;
                    while (end > pos && ((unsigned char)text[end] & 0xC0) == 0x80) end--;
                    if (end == pos) end = lim_end; /* cannot happen for limit >= 4 */
                }
            }
        }
        starts[count] = pos;
        lens[count] = end - pos;
        count++;
        pos = end;
    }
    return count;
}

int voice_speak_ends_with_question(const char *text)
{
    if (!text) return 0;
    size_t n = strlen(text);
    for (;;) {
        if (n > 0 && (is_ws(text[n - 1]) || text[n - 1] == '"' || text[n - 1] == '\''
                      || text[n - 1] == ')')) { n--; continue; }
        if (n >= 3 && (unsigned char)text[n - 3] == 0xE2 && (unsigned char)text[n - 2] == 0x80
            && ((unsigned char)text[n - 1] == 0x9D || (unsigned char)text[n - 1] == 0x99)) {
            n -= 3;
            continue;
        }
        break;
    }
    return n > 0 && text[n - 1] == '?';
}
