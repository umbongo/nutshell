#include "voice_speak.h"

#include "markdown.h"

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
        char *np = (char *)realloc(b->p, nc);
        if (!np) { b->oom = 1; return; }
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
 * is left alone. Linear: a closer that is not found is never searched for
 * again. */
static char *strip_spans(const char *s, size_t n)
{
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    size_t o = 0, i = 0;
    int think_ok = 1, exec_ok = 1;
    while (i < n) {
        if (think_ok && s[i] == '<' && n - i >= 7 && memcmp(s + i, "<think>", 7) == 0) {
            const char *e = NULL;
            for (size_t j = i + 7; j + 8 <= n; j++)
                if (s[j] == '<' && memcmp(s + j, "</think>", 8) == 0) { e = s + j; break; }
            if (e) { i = (size_t)(e - s) + 8; out[o++] = ' '; continue; }
            think_ok = 0;
        }
        if (exec_ok && s[i] == '[' && n - i >= 6 && memcmp(s + i, "[EXEC]", 6) == 0) {
            const char *e = NULL;
            for (size_t j = i + 6; j + 7 <= n; j++)
                if (s[j] == '[' && memcmp(s + j, "[/EXEC]", 7) == 0) { e = s + j; break; }
            if (e) { i = (size_t)(e - s) + 7; out[o++] = ' '; continue; }
            exec_ok = 0;
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
                long pc = find_bounded(s, (size_t)close + 2, n, ')', 2000);
                if (pc >= 0) {
                    inline_text(b, s + i + 1, (size_t)close - i - 1);
                    i = (size_t)pc + 1;
                    continue;
                }
            }
        }
        if ((c == 'h' || c == 'H') && (i == 0 || !is_alnum(s[i - 1]))) {
            size_t k = 0;
            if (n - i >= 7 && strncmp(s + i, "http://", 7) == 0) k = 7;
            else if (n - i >= 8 && strncmp(s + i, "https://", 8) == 0) k = 8;
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

char *voice_speak_text(const char *markdown)
{
    if (!markdown) markdown = "";
    char *src = strip_spans(markdown, strlen(markdown));
    if (!src) return NULL;
    size_t n = strlen(src);

    Buf out = {0};
    char  *line = NULL;
    size_t linecap = 0;
    int in_fence = 0, in_table = 0, in_icode = 0, prev_blank = 1;
    size_t pos = 0;
    while (pos < n) {
        size_t e = pos;
        while (e < n && src[e] != '\n') e++;
        size_t ll = e - pos;
        if (ll > 0 && src[pos + ll - 1] == '\r') ll--;
        if (ll + 1 > linecap) {
            size_t nc = linecap ? linecap : 256;
            while (nc < ll + 1) nc *= 2;
            char *nl = (char *)realloc(line, nc);
            if (!nl) { free(src); free(line); free(out.p); return NULL; }
            line = nl;
            linecap = nc;
        }
        memcpy(line, src + pos, ll);
        line[ll] = '\0';
        pos = e < n ? e + 1 : n;

        size_t lead = 0;
        while (lead < ll && (line[lead] == ' ' || line[lead] == '\t')) lead++;
        int blank = (lead == ll);

        MdLineInfo info = md_classify_line(line, in_fence);
        if (info.type == MD_LINE_CODE_FENCE) {
            if (!in_fence) { buf_put(&out, CODE_PHRASE " ", sizeof(CODE_PHRASE)); in_fence = 1; }
            else in_fence = 0;
            in_table = in_icode = 0;
            prev_blank = 0;
            continue;
        }
        if (in_fence) continue;

        if (info.type == MD_LINE_TABLE) {
            if (!in_table) buf_put(&out, TABLE_PHRASE " ", sizeof(TABLE_PHRASE));
            in_table = 1; in_icode = 0; prev_blank = 0;
            continue;
        }
        in_table = 0;

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
            put_item(&out, t + ti.content_offset, ll - lead - (size_t)ti.content_offset);
            break;
        case MD_LINE_HRULE:
            buf_ch(&out, ' ');
            break;
        case MD_LINE_BLOCKQUOTE: {
            const char *q = t;
            size_t ql = ll - lead;
            while (ql > 0 && *q == '>') {
                q++; ql--;
                while (ql > 0 && *q == ' ') { q++; ql--; }
            }
            inline_text(&out, q, ql);
            buf_ch(&out, ' ');
            break;
        }
        default:
            inline_text(&out, t, ll - lead);
            buf_ch(&out, ' ');
            break;
        }
    }
    free(src);
    free(line);
    if (out.oom) { free(out.p); return NULL; }

    /* collapse whitespace, trim */
    char *res = (char *)malloc(out.len + 1);
    if (!res) { free(out.p); return NULL; }
    size_t o = 0;
    int pend = 0;
    for (size_t i = 0; i < out.len; i++) {
        if (is_ws(out.p[i])) { pend = 1; continue; }
        if (pend && o > 0) res[o++] = ' ';
        pend = 0;
        res[o++] = out.p[i];
    }
    res[o] = '\0';
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
    while (n > 0 && (is_ws(text[n - 1]) || text[n - 1] == '"' || text[n - 1] == '\''
                     || text[n - 1] == ')')) n--;
    return n > 0 && text[n - 1] == '?';
}
