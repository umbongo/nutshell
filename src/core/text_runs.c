/* src/core/text_runs.c -- see text_runs.h. */
#include "text_runs.h"
#include <stdlib.h>
#include <string.h>

void text_runs_init(TextRuns *r)
{
    if (r) memset(r, 0, sizeof(*r));
}

void text_runs_reset(TextRuns *r)
{
    if (!r) return;
    r->n = 0;
    r->cum_n = 0;
    r->line_count = 0;
}

void text_runs_free(TextRuns *r)
{
    if (!r) return;
    free(r->v);
    free(r->cum);
    free(r->cum_byte);
    memset(r, 0, sizeof(*r));
}

/* Grow r->v to hold at least `need` runs. */
static int grow_runs(TextRuns *r, int need)
{
    if (need <= r->cap) return 1;
    int cap = r->cap ? r->cap * 2 : 64;
    while (cap < need) cap *= 2;
    TextRun *v = (TextRun *)realloc(r->v, (size_t)cap * sizeof(TextRun));
    if (!v) return 0;
    r->v = v;
    r->cap = cap;
    return 1;
}

/* Grow the two boundary arrays together to hold at least `need`. */
static int grow_cum(TextRuns *r, int need)
{
    if (need <= r->cum_cap) return 1;
    int cap = r->cum_cap ? r->cum_cap * 2 : 256;
    while (cap < need) cap *= 2;
    int *c = (int *)realloc(r->cum, (size_t)cap * sizeof(int));
    if (!c) return 0;
    r->cum = c;
    int *b = (int *)realloc(r->cum_byte, (size_t)cap * sizeof(int));
    if (!b) return 0;   /* r->cum is larger than needed; harmless */
    r->cum_byte = b;
    r->cum_cap = cap;
    return 1;
}

int text_runs_append(TextRuns *r, const TextRun *proto,
                     const int *cum, const int *cum_byte, int n_cum)
{
    if (!r || !proto || n_cum < 0) return -1;
    if (n_cum > 0 && (!cum || !cum_byte)) return -1;
    if (!grow_runs(r, r->n + 1)) return -1;
    if (n_cum > 0 && !grow_cum(r, r->cum_n + n_cum)) return -1;

    TextRun *t = &r->v[r->n];
    *t = *proto;
    t->synthetic[sizeof(t->synthetic) - 1] = '\0';
    if (n_cum > 0) {
        memcpy(r->cum + r->cum_n, cum, (size_t)n_cum * sizeof(int));
        memcpy(r->cum_byte + r->cum_n, cum_byte, (size_t)n_cum * sizeof(int));
    }
    t->first_cum = r->cum_n;
    t->n_cum = n_cum;
    r->cum_n += n_cum;

    if (r->n > 0 && r->v[r->n - 1].y == t->y && r->v[r->n - 1].part == t->part)
        t->line = r->v[r->n - 1].line;
    else
        t->line = r->line_count++;
    return r->n++;
}

/* Length of the UTF-8 character starting at s[i] (i < len): 1 for ASCII,
 * an invalid byte or a sequence cut short, else 2 to 4. */
static int utf8_char_len(const char *s, int i, int len)
{
    unsigned char c = (unsigned char)s[i];
    int n;
    if (c < 0x80) return 1;
    else if ((c & 0xE0) == 0xC0) n = 2;
    else if ((c & 0xF0) == 0xE0) n = 3;
    else if ((c & 0xF8) == 0xF0) n = 4;
    else return 1;
    if (i + n > len) return 1;
    for (int k = 1; k < n; k++)
        if (((unsigned char)s[i + k] & 0xC0) != 0x80) return 1;
    return n;
}

int text_runs_cum_from_utf16(const char *s, int len, const int *dx16, int n16,
                             int *cum, int *cum_byte, int cap)
{
    if (!s || !cum || !cum_byte || cap < 1 || len < 0) return -1;
    if (n16 > 0 && !dx16) return -1;
    int k = 0, i = 0, u = 0;
    cum[k] = 0;
    cum_byte[k] = 0;
    k++;
    while (i < len) {
        int cl = utf8_char_len(s, i, len);
        i += cl;
        u += (cl == 4) ? 2 : 1;
        if (k >= cap) return -1;
        int ui = u < n16 ? u : n16;
        cum[k] = ui > 0 ? dx16[ui - 1] : 0;
        cum_byte[k] = i;
        k++;
    }
    return k;
}

int text_runs_utf8_prefix_for_utf16(const char *s, int len, int n16)
{
    if (!s || len <= 0 || n16 <= 0) return 0;
    int i = 0, u = 0;
    while (i < len) {
        int cl = utf8_char_len(s, i, len);
        int units = (cl == 4) ? 2 : 1;
        if (u + units > n16) break;
        u += units;
        i += cl;
    }
    return i;
}

int text_runs_span_px(int run_start, int run_end,
                      const int *cum, const int *cum_byte, int n_cum,
                      int sel_start, int sel_end, int *x0, int *x1)
{
    if (!cum || !cum_byte || !x0 || !x1 || n_cum < 2) return 0;
    if (sel_start >= sel_end || sel_end <= run_start || sel_start >= run_end)
        return 0;
    int lo = (sel_start > run_start ? sel_start : run_start) - run_start;
    int hi = (sel_end < run_end ? sel_end : run_end) - run_start;
    int a = -1, b = -1;
    for (int k = 0; k < n_cum; k++) {
        if (a < 0 && cum_byte[k] >= lo) a = k;
        if (cum_byte[k] <= hi) b = k;
    }
    if (a < 0 || b < 0 || cum[b] <= cum[a]) return 0;
    *x0 = cum[a];
    *x1 = cum[b];
    return 1;
}

/* Offset a synthetic run resolves to: the start of the next real run on its
 * line (smallest x right of it), else its own end. */
static int synthetic_target(const TextRuns *r, int si)
{
    const TextRun *s = &r->v[si];
    int best = -1;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (t->line != s->line || t->synthetic[0] || t->x <= s->x) continue;
        if (best < 0 || t->x < r->v[best].x) best = i;
    }
    return best >= 0 ? r->v[best].start : s->end;
}

/* Offset for the left edge of run i: its start, or a synthetic's target. */
static int run_left_offset(const TextRuns *r, int i)
{
    return r->v[i].synthetic[0] ? synthetic_target(r, i) : r->v[i].start;
}

/* Nearest character boundary in run i to x. */
static int nearest_boundary(const TextRuns *r, int i, int x)
{
    const TextRun *t = &r->v[i];
    if (t->synthetic[0]) return synthetic_target(r, i);
    if (t->n_cum < 1) return t->start;
    int best = 0, best_d = -1;
    for (int k = 0; k < t->n_cum; k++) {
        int d = t->x + r->cum[t->first_cum + k] - x;
        if (d < 0) d = -d;
        if (best_d < 0 || d < best_d) { best = k; best_d = d; }
    }
    return t->start + r->cum_byte[t->first_cum + best];
}

int text_runs_hit(const TextRuns *r, int x, int y, int *part_out)
{
    if (!r || r->n <= 0) return -1;

    /* The visual line: a run that holds y, else the nearest one above. */
    int li = -1;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (y >= t->y && y < t->y + t->h) { li = i; break; }
    }
    if (li < 0) {
        int top = r->v[0].y, above = -1;
        for (int i = 0; i < r->n; i++) {
            const TextRun *t = &r->v[i];
            if (t->y < top) top = t->y;
            if (t->y + t->h <= y &&
                (above < 0 || t->y + t->h > r->v[above].y + r->v[above].h))
                above = i;
        }
        if (y < top) {
            if (part_out) *part_out = r->v[0].part;
            return run_left_offset(r, 0);
        }
        int below_all = 1;
        for (int i = 0; i < r->n; i++)
            if (r->v[i].y + r->v[i].h > y) { below_all = 0; break; }
        if (below_all || above < 0) {
            if (part_out) *part_out = r->v[r->n - 1].part;
            return r->v[r->n - 1].end;
        }
        li = above;
    }

    int ly = r->v[li].y, part = r->v[li].part;
    if (part_out) *part_out = part;

    /* Runs on that line: same y and part (table cells of one row share a y
     * but were appended as separate lines, so match on y, not line). */
    int left = -1, right = -1, left_of = -1;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (t->y != ly || t->part != part) continue;
        if (left < 0 || t->x < r->v[left].x) left = i;
        if (right < 0 || t->x > r->v[right].x) right = i;
        if (x >= t->x && x < t->x + t->w) return nearest_boundary(r, i, x);
        if (t->x + t->w <= x &&
            (left_of < 0 || t->x + t->w > r->v[left_of].x + r->v[left_of].w))
            left_of = i;
    }
    if (x < r->v[left].x) return run_left_offset(r, left);
    if (x >= r->v[right].x + r->v[right].w) return r->v[right].end;
    if (left_of >= 0) {
        if (r->v[left_of].synthetic[0]) return synthetic_target(r, left_of);
        return r->v[left_of].end;
    }
    return run_left_offset(r, left);
}

int text_runs_run_at(const TextRuns *r, int x, int y)
{
    if (!r) return -1;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h)
            return i;
    }
    return -1;
}

static int is_ws(char c) { return c == ' ' || c == '\t'; }

void text_runs_word(const char *text, int text_len, const TextRun *run,
                    int off, int *s, int *e)
{
    if (!s || !e) return;
    *s = *e = off;
    if (!text || !run || run->synthetic[0]) return;
    int rs = run->start, re = run->end;
    if (rs < 0) rs = 0;
    if (re > text_len) re = text_len;
    if (rs >= re) return;
    if (!(run->flags & TR_FLAG_CODE)) {
        while (re > rs && is_ws(text[re - 1])) re--;
        *s = rs;
        *e = re;
        return;
    }
    if (off < rs) off = rs;
    if (off > re) off = re;
    int a;
    if (off < re && !is_ws(text[off])) a = off;
    else if (off > rs && !is_ws(text[off - 1])) a = off - 1;
    else { *s = *e = off; return; }
    int b = a;
    while (a > rs && !is_ws(text[a - 1])) a--;
    while (b < re && !is_ws(text[b])) b++;
    *s = a;
    *e = b;
}

void text_line_at(const char *text, int len, int off, int *s, int *e)
{
    if (!s || !e) return;
    if (!text || len <= 0) { *s = *e = 0; return; }
    if (off < 0) off = 0;
    if (off > len) off = len;
    int a = off, b = off;
    while (a > 0 && text[a - 1] != '\n') a--;
    while (b < len && text[b] != '\n') b++;
    *s = a;
    *e = b;
}

int text_next_line(const char *s, int len, int *pos, int *ls, int *le)
{
    if (!s || !pos || !ls || !le || len <= 0) return 0;
    int p = *pos;
    if (p < 0 || p >= len) return 0;
    int q = p;
    while (q < len && s[q] != '\n') q++;
    int end = q;
    if (q < len && end > p && s[end - 1] == '\r') end--;
    *ls = p;
    *le = end;
    *pos = (q < len) ? q + 1 : q;
    return 1;
}
