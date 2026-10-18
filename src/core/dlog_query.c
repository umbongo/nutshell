/* src/core/dlog_query.c -- see dlog_query.h */
#include "dlog_query.h"
#include "dlog.h"
#include "json_parser.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOT_BYTES   (DLOG_QUERY_LINE_MAX + 32)
#define CARRY_MAX    8192u
#define READ_CHUNK   65536u
#define MAX_FILES    600
#define MAX_RECENT   6          /* main files read for one query */
#define MAX_EVENTS   128
#define MAX_ISSUES   8
#define WINDOW_BEFORE_MARK_MS 120000LL

/* ---- the line parser --------------------------------------------------------- */

int dlog_line_parse(const char *line, size_t len, DlogLineInfo *li)
{
    if (!line || !li || len < 38) return 0;
    long long ms;
    if (!dlog_parse_timestamp(line, &ms)) return 0;
    if (line[DLOG_TS_LEN] != ' ' || line[DLOG_TS_LEN + 6] != ' ') return 0;
    char tag[6];
    memcpy(tag, line + DLOG_TS_LEN + 1, 5);
    tag[5] = '\0';
    int ev = dlog_ev_level_from_tag(tag);
    if (ev < 0) return 0;
    const char *p = line + DLOG_TS_LEN + 7;
    const char *end = line + len;

    const char *t = p;
    while (p < end && *p != ' ') p++;
    if (p == t || p >= end) return 0;
    char word[16];
    size_t wl = (size_t)(p - t);
    if (wl >= sizeof(word)) return 0;
    memcpy(word, t, wl);
    word[wl] = '\0';
    int cat = dlog_cat_from_name(word);
    if (cat < 0) return 0;
    p++;

    t = p;
    while (p < end && *p != ' ') p++;
    if (p == t || p >= end) return 0;
    unsigned sid = 0;
    if (!(p - t == 1 && *t == '-')) {
        if (*t != 's' || p - t < 2) return 0;
        for (const char *d = t + 1; d < p; d++) {
            if (*d < '0' || *d > '9') return 0;
            sid = sid * 10u + (unsigned)(*d - '0');
        }
    }
    p++;

    t = p;                                            /* thread */
    while (p < end && *p != ' ') p++;
    if (p == t || p >= end) return 0;
    p++;

    t = p;                                            /* event */
    while (p < end && *p != ' ') p++;
    if (p == t) return 0;
    li->ms = ms;
    li->ev_level = ev;
    li->cat = cat;
    li->sid = sid;
    li->event = t;
    li->event_len = (size_t)(p - t);
    return 1;
}

/* ---- defaults, JSON, description ------------------------------------------------ */

void dlog_query_defaults(DlogQuery *q)
{
    if (!q) return;
    memset(q, 0, sizeof(*q));
    q->since_minutes = DLOG_QUERY_DEFAULT_MINUTES;
    q->category = -1;
    q->around_issue = -1;
    q->max_lines = DLOG_QUERY_DEFAULT_LINES;
}

static void set_err(char *err, size_t cap, const char *msg)
{
    if (err && cap) (void)snprintf(err, cap, "%s", msg);
}

static int clamp_int(double v, int lo, int hi)
{
    if (v < (double)lo) return lo;
    if (v > (double)hi) return hi;
    return (int)v;
}

int dlog_query_from_json(const char *json, DlogQuery *q, char *err, size_t err_cap)
{
    set_err(err, err_cap, "");
    if (!q) { set_err(err, err_cap, "no query"); return 0; }
    dlog_query_defaults(q);
    if (!json) return 1;
    while (*json == ' ' || *json == '\t' || *json == '\r' || *json == '\n') json++;
    if (!*json) return 1;
    JsonNode *root = json_parse(json);
    if (!root || root->type != JSON_OBJECT) {
        if (root) json_free(root);
        set_err(err, err_cap, "arguments are not a JSON object");
        return 0;
    }
    int ok = 1;
    const JsonNode *n;

    if ((n = json_obj_get(root, "since_minutes"))) {
        if (n->type != JSON_NUMBER) { set_err(err, err_cap, "since_minutes must be a number"); ok = 0; }
        else q->since_minutes = clamp_int(n->as.num_val, 1, 1440);
    }
    if (ok && (n = json_obj_get(root, "category"))) {
        int c = (n->type == JSON_STRING) ? dlog_cat_from_name(n->as.str_val) : -1;
        if (c < 0) { set_err(err, err_cap, "category must be one of render, term, ai, ssh, shell, general"); ok = 0; }
        else q->category = c;
    }
    if (ok && (n = json_obj_get(root, "min_level"))) {
        int l = (n->type == JSON_STRING) ? dlog_ev_level_from_word(n->as.str_val) : -1;
        if (l < 0) { set_err(err, err_cap, "min_level must be one of error, warn, info, debug, trace"); ok = 0; }
        else q->min_level = l;
    }
    if (ok && (n = json_obj_get(root, "contains"))) {
        if (n->type != JSON_STRING) { set_err(err, err_cap, "contains must be a string"); ok = 0; }
        else {
            size_t l = strlen(n->as.str_val);
            if (l > 128) l = 128;
            memcpy(q->contains, n->as.str_val, l);
            q->contains[l] = '\0';
        }
    }
    if (ok && (n = json_obj_get(root, "session"))) {
        unsigned sid = 0;
        int good = 0;
        if (n->type == JSON_NUMBER && n->as.num_val >= 1 && n->as.num_val < 1e9) {
            sid = (unsigned)n->as.num_val;
            good = 1;
        } else if (n->type == JSON_STRING) {
            const char *s = n->as.str_val;
            if (*s == 's' || *s == 'S') s++;
            if (*s) {
                good = 1;
                for (; *s; s++) {
                    if (*s < '0' || *s > '9' || sid > 100000000u) { good = 0; break; }
                    sid = sid * 10u + (unsigned)(*s - '0');
                }
            }
            if (sid == 0) good = 0;
        }
        if (!good) { set_err(err, err_cap, "session must look like s2"); ok = 0; }
        else q->session = sid;
    }
    if (ok && (n = json_obj_get(root, "around_issue"))) {
        if (n->type != JSON_NUMBER) { set_err(err, err_cap, "around_issue must be a number"); ok = 0; }
        else q->around_issue = clamp_int(n->as.num_val, 0, 1000000);
    }
    if (ok && (n = json_obj_get(root, "summary"))) {
        if (n->type != JSON_BOOL) { set_err(err, err_cap, "summary must be true or false"); ok = 0; }
        else q->summary = n->as.bool_val ? 1 : 0;
    }
    if (ok && (n = json_obj_get(root, "max_lines"))) {
        if (n->type != JSON_NUMBER) { set_err(err, err_cap, "max_lines must be a number"); ok = 0; }
        else q->max_lines = clamp_int(n->as.num_val, 1, 1000);
    }
    json_free(root);
    return ok;
}

static void append(char *buf, size_t cap, const char *fmt, ...) DLOG_PRINTF(3, 4);
static void append(char *buf, size_t cap, const char *fmt, ...)
{
    size_t o = strlen(buf);
    if (o + 1 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf + o, cap - o, fmt, ap);
    va_end(ap);
}

void dlog_query_describe(const DlogQuery *q, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!q) return;
    char buf[200];
    buf[0] = '\0';
    const char *sep = "";
    if (q->category >= 0 && dlog_cat_name(q->category)) {
        append(buf, sizeof(buf), "%s%s", sep, dlog_cat_name(q->category));
        sep = ", ";
    }
    if (q->session) {
        append(buf, sizeof(buf), "%ss%u", sep, q->session);
        sep = ", ";
    }
    if (q->min_level > 0 && q->min_level <= 5) {
        static const char *const w[6] = { "", "error", "warn", "info", "debug", "trace" };
        append(buf, sizeof(buf), "%smin %s", sep, w[q->min_level]);
        sep = ", ";
    }
    if (q->contains[0]) {
        append(buf, sizeof(buf), "%scontains \"%.24s\"", sep, q->contains);
        sep = ", ";
    }
    if (q->summary) {
        append(buf, sizeof(buf), "%ssummary", sep);
        sep = ", ";
    }
    if (q->around_issue == 0)
        append(buf, sizeof(buf), "%saround the latest issue", sep);
    else if (q->around_issue > 0)
        append(buf, sizeof(buf), "%saround issue %d", sep, q->around_issue);
    else if (q->since_minutes > 0)
        append(buf, sizeof(buf), "%slast %d min", sep, q->since_minutes);
    (void)snprintf(out, cap, "%s", buf);
}

const char *dlog_tool_description(void)
{
    return "Read Nutshell's own debug log (read-only). Start with summary=true, then narrow with category, "
           "session, contains or min_level; around_issue reads the moment the user marked with Ctrl+Shift+M "
           "(the 2 minutes before it plus the state snapshot). Lines read: time LEVEL category session "
           "thread event key=value. Output is capped at 16 KB and keeps the newest lines.";
}

const char *dlog_tool_schema(void)
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"since_minutes\":{\"type\":\"integer\",\"description\":\"How far back, 1-1440 (default 30)\"},"
           "\"category\":{\"type\":\"string\",\"enum\":[\"render\",\"term\",\"ai\",\"ssh\",\"shell\",\"general\"]},"
           "\"min_level\":{\"type\":\"string\",\"enum\":[\"error\",\"warn\",\"info\",\"debug\",\"trace\"],"
           "\"description\":\"Most verbose level to include\"},"
           "\"contains\":{\"type\":\"string\",\"description\":\"Case-insensitive text, max 128 chars\"},"
           "\"session\":{\"type\":\"string\",\"description\":\"Session id such as s2\"},"
           "\"around_issue\":{\"type\":\"integer\",\"description\":\"0 = latest issue mark, n = issue n\"},"
           "\"summary\":{\"type\":\"boolean\",\"description\":\"Counts per category, level and event plus the issue list, no lines\"},"
           "\"max_lines\":{\"type\":\"integer\",\"description\":\"1-1000, default 200\"}}}";
}

/* ---- the run ----------------------------------------------------------------------- */

typedef struct { char name[48]; long count; } EvCount;
typedef struct { char at[12]; unsigned n; } IssueSeen;

typedef struct {
    const DlogQuery *q;
    int       max_lines;
    long long from_ms;           /* lines before this are ignored (0 = none) */
    long long mark_ms;           /* around mode, pass 1 */
    long long last_seen_ms;

    char     *slots;             /* ring of max_lines slots */
    int       head, count;
    long      matched, malformed;
    char      first_ts[12], last_ts[12];

    long      cat_cnt[DLOG_CAT_COUNT];
    long      lvl_cnt[6];
    EvCount   ev[MAX_EVENTS];
    int       nev;
    long      ev_other;
    IssueSeen issues[MAX_ISSUES];
    int       nissues;
    long      issues_total;
} Run;

typedef void (*LineCb)(Run *r, const char *line, size_t stored, size_t true_len);

static int ci_find(const char *hay, size_t n, const char *needle)
{
    size_t m = strlen(needle);
    if (m == 0) return 1;
    if (m > n) return 0;
    for (size_t i = 0; i + m <= n; i++) {
        size_t j = 0;
        for (; j < m; j++) {
            unsigned char a = (unsigned char)hay[i + j], b = (unsigned char)needle[j];
            if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (j == m) return 1;
    }
    return 0;
}

static int passes(const DlogQuery *q, const DlogLineInfo *li, const char *line, size_t len)
{
    int is_issue = li->ev_level == (int)DLOG_ISSUE;      /* a mark shows whatever else is filtered */
    if (q->category >= 0 && !is_issue && li->cat != q->category) return 0;
    if (q->session && !is_issue && li->sid != q->session) return 0;
    if (q->min_level > 0 && !is_issue && li->ev_level > q->min_level) return 0;
    if (q->contains[0] && !ci_find(line, len, q->contains)) return 0;
    return 1;
}

static void note_issue(Run *r, const DlogLineInfo *li, const char *line, size_t len)
{
    if (li->ev_level != (int)DLOG_ISSUE || li->event_len != 10 || memcmp(li->event, "issue.mark", 10) != 0) return;
    r->issues_total++;
    if (r->nissues >= MAX_ISSUES) return;
    IssueSeen *is = &r->issues[r->nissues++];
    memcpy(is->at, line + 11, 8);
    is->at[8] = '\0';
    is->n = 0;
    const char *p = li->event + li->event_len;
    const char *end = line + len;
    while (p + 3 < end) {
        if (p[0] == ' ' && p[1] == 'n' && p[2] == '=') {
            p += 3;
            while (p < end && *p >= '0' && *p <= '9') is->n = is->n * 10u + (unsigned)(*p++ - '0');
            break;
        }
        p++;
    }
}

static void feed_cb(Run *r, const char *line, size_t stored, size_t true_len)
{
    DlogLineInfo li;
    if (!dlog_line_parse(line, stored, &li)) { r->malformed++; return; }
    if (r->from_ms && li.ms < r->from_ms) return;
    note_issue(r, &li, line, stored);
    if (!passes(r->q, &li, line, stored)) return;
    r->matched++;
    if (r->q->summary) {
        r->cat_cnt[li.cat]++;
        if (li.ev_level >= 0 && li.ev_level <= 5) r->lvl_cnt[li.ev_level]++;
        int i = 0;
        for (; i < r->nev; i++)
            if (strlen(r->ev[i].name) == li.event_len && memcmp(r->ev[i].name, li.event, li.event_len) == 0) break;
        if (i < r->nev) r->ev[i].count++;
        else if (r->nev < MAX_EVENTS && li.event_len < sizeof(r->ev[0].name)) {
            memcpy(r->ev[r->nev].name, li.event, li.event_len);
            r->ev[r->nev].name[li.event_len] = '\0';
            r->ev[r->nev].count = 1;
            r->nev++;
        } else r->ev_other++;
        memcpy(r->last_ts, line + 11, 8);
        r->last_ts[8] = '\0';
        if (!r->first_ts[0]) memcpy(r->first_ts, r->last_ts, 9);
        return;
    }
    if (!r->slots) return;                     /* out of memory for the line ring: the lines are skipped */
    char *slot = r->slots + (size_t)((r->head + r->count) % r->max_lines) * SLOT_BYTES;
    if (r->count == r->max_lines) {            /* full: overwrite the oldest */
        slot = r->slots + (size_t)r->head * SLOT_BYTES;
        r->head = (r->head + 1) % r->max_lines;
        r->count--;
    }
    size_t keep = true_len < DLOG_QUERY_LINE_MAX ? true_len : DLOG_QUERY_LINE_MAX;
    if (keep > stored) keep = stored;
    memcpy(slot, line, keep);
    if (true_len > keep) {
        int w = snprintf(slot + keep, SLOT_BYTES - keep, " +%zu bytes", true_len - keep);
        keep = (w > 0) ? keep + (size_t)w : keep;
    }
    slot[keep] = '\0';
    r->count++;
}

static void mark_cb(Run *r, const char *line, size_t stored, size_t true_len)
{
    (void)true_len;
    DlogLineInfo li;
    if (!dlog_line_parse(line, stored, &li)) return;
    r->last_seen_ms = li.ms;
    if (li.ev_level == (int)DLOG_ISSUE && li.event_len == 10 && memcmp(li.event, "issue.mark", 10) == 0)
        r->mark_ms = li.ms;
}

/* Split a text into complete lines and call cb on each. A line without its LF
 * (still being written) is not used. */
static void feed_text(Run *r, const char *text, size_t len, LineCb cb)
{
    size_t i = 0;
    while (i < len) {
        const char *nl = memchr(text + i, '\n', len - i);
        if (!nl) break;
        size_t l = (size_t)(nl - (text + i));
        size_t t = l;
        if (t > 0 && text[i + t - 1] == '\r') t--;
        size_t stored = t < CARRY_MAX ? t : CARRY_MAX;
        cb(r, text + i, stored, t);
        i += l + 1;
    }
}

static void feed_file(Run *r, const DlogQueryIO *io, const char *name, LineCb cb)
{
    char *chunk = (char *)malloc(READ_CHUNK);
    char *carry = (char *)malloc(CARRY_MAX);
    if (!chunk || !carry) { free(chunk); free(carry); return; }
    unsigned long long off = 0;
    size_t clen = 0, true_len = 0;
    for (;;) {
        long n = io->read(io->ctx, name, off, chunk, READ_CHUNK);
        if (n <= 0) break;
        off += (unsigned long long)n;
        size_t i = 0, un = (size_t)n;
        while (i < un) {
            const char *nl = memchr(chunk + i, '\n', un - i);
            size_t seg = nl ? (size_t)(nl - (chunk + i)) : un - i;
            size_t room = CARRY_MAX - clen;
            size_t take = seg < room ? seg : room;
            memcpy(carry + clen, chunk + i, take);
            clen += take;
            true_len += seg;
            i += seg;
            if (nl) {
                size_t t = clen;
                if (t > 0 && carry[t - 1] == '\r') { t--; true_len--; }
                cb(r, carry, t, true_len);
                clen = 0;
                true_len = 0;
                i++;
            }
        }
    }
    free(chunk);
    free(carry);
}

/* ---- output ------------------------------------------------------------------------- */

static size_t put(char *out, size_t cap, size_t o, const char *fmt, ...) DLOG_PRINTF(4, 5);
static size_t put(char *out, size_t cap, size_t o, const char *fmt, ...)
{
    if (o + 1 >= cap) return o;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + o, cap - o, fmt, ap);
    va_end(ap);
    if (w < 0) return o;
    if ((size_t)w >= cap - o) return cap - 1;
    return o + (size_t)w;
}

static const char *slot_at(const Run *r, int i)
{
    return r->slots + (size_t)((r->head + i) % r->max_lines) * SLOT_BYTES;
}

static void files_text(char *dst, size_t cap, const char *const *names, int n)
{
    size_t o = 0;
    dst[0] = '\0';
    for (int i = 0; i < n; i++) {
        int w = snprintf(dst + o, cap - o, "%s%s", i ? ", " : "", names[i]);
        if (w < 0 || (size_t)w >= cap - o) {
            if (o + 4 < cap) memcpy(dst + o, "...", 4);
            return;
        }
        o += (size_t)w;
    }
}

static size_t emit_lines(Run *r, const char *title, const char *files, char *out, size_t cap_in)
{
    size_t cap = cap_in < (size_t)DLOG_QUERY_OUT_MAX + 1 ? cap_in : (size_t)DLOG_QUERY_OUT_MAX + 1;
    int shown = r->count;
    char head[600];
    size_t hl = 0;
    for (int pass = 0; pass < 4; pass++) {
        char win[40];
        if (shown > 0) {
            char a[10], b[10];
            memcpy(a, slot_at(r, r->count - shown) + 11, 8);
            a[8] = '\0';
            memcpy(b, slot_at(r, r->count - 1) + 11, 8);
            b[8] = '\0';
            (void)snprintf(win, sizeof(win), "window %s-%s", a, b);
        } else {
            (void)snprintf(win, sizeof(win), "window -");
        }
        int w;
        if (shown < r->matched)
            w = snprintf(head, sizeof(head), "# %s%ld matching lines, showing last %d; files: %s; %s", title,
                         r->matched, shown, files, win);
        else
            w = snprintf(head, sizeof(head), "# %s%ld matching lines; files: %s; %s", title, r->matched, files, win);
        if (w < 0) w = 0;
        if ((size_t)w >= sizeof(head)) w = (int)sizeof(head) - 1;
        if (r->malformed > 0)
            w += snprintf(head + w, sizeof(head) - (size_t)w, "; skipped %ld unreadable lines", r->malformed);
        if ((size_t)w >= sizeof(head)) w = (int)sizeof(head) - 1;
        hl = (size_t)w + 1;                       /* + LF */
        size_t budget = (cap > hl + 1) ? cap - 1 - hl : 0;
        int fit = 0;
        size_t used = 0;
        for (int i = r->count - 1; i >= 0 && fit < r->count; i--) {
            size_t l = strlen(slot_at(r, i)) + 1;
            if (used + l > budget) break;
            used += l;
            fit++;
        }
        if (fit == shown) break;
        shown = fit;
    }
    size_t o = 0;
    o = put(out, cap, o, "%s\n", head);
    for (int i = r->count - shown; i < r->count; i++) o = put(out, cap, o, "%s\n", slot_at(r, i));
    out[o < cap ? o : cap - 1] = '\0';
    return strlen(out);
}

static size_t emit_summary(Run *r, const char *title, const char *files, const char *const *issue_files, int nissue_files,
                           char *out, size_t cap_in)
{
    size_t cap = cap_in < (size_t)DLOG_QUERY_OUT_MAX + 1 ? cap_in : (size_t)DLOG_QUERY_OUT_MAX + 1;
    size_t o = 0;
    char win[40];
    if (r->first_ts[0]) (void)snprintf(win, sizeof(win), "window %s-%s", r->first_ts, r->last_ts);
    else (void)snprintf(win, sizeof(win), "window -");
    o = put(out, cap, o, "# summary: %s%ld matching lines; files: %s; %s", title, r->matched, files, win);
    if (r->malformed > 0) o = put(out, cap, o, "; skipped %ld unreadable lines", r->malformed);
    o = put(out, cap, o, "\n");

    o = put(out, cap, o, "categories:");
    for (int c = 0; c < (int)DLOG_CAT_COUNT; c++)
        if (r->cat_cnt[c]) o = put(out, cap, o, " %s=%ld", dlog_cat_name(c), r->cat_cnt[c]);
    o = put(out, cap, o, "\nlevels:");
    static const int order[6] = { DLOG_ERROR, DLOG_WARN, DLOG_INFO, DLOG_DEBUG, DLOG_TRACE, DLOG_ISSUE };
    for (int k = 0; k < 6; k++) {
        int lv = order[k];
        if (r->lvl_cnt[lv]) {
            char tag[8];
            (void)snprintf(tag, sizeof(tag), "%s", dlog_ev_level_tag(lv));
            for (char *t = tag; *t; t++) if (*t == ' ') *t = '\0';
            o = put(out, cap, o, " %s=%ld", tag, r->lvl_cnt[lv]);
        }
    }
    /* top events */
    int idx[MAX_EVENTS];
    for (int i = 0; i < r->nev; i++) idx[i] = i;
    for (int i = 0; i < r->nev; i++)                      /* selection of the top 15 */
        for (int j = i + 1; j < r->nev; j++)
            if (r->ev[idx[j]].count > r->ev[idx[i]].count) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
    int top = r->nev < 15 ? r->nev : 15;
    o = put(out, cap, o, "\nevents:");
    for (int i = 0; i < top; i++) o = put(out, cap, o, " %s=%ld", r->ev[idx[i]].name, r->ev[idx[i]].count);
    long more = (long)(r->nev - top) + r->ev_other;
    if (more > 0) o = put(out, cap, o, " (+%ld more kinds)", more);

    o = put(out, cap, o, "\nissues:");
    if (r->nissues == 0) o = put(out, cap, o, " none in this window");
    for (int i = 0; i < r->nissues; i++) o = put(out, cap, o, " n=%u at %s;", r->issues[i].n, r->issues[i].at);
    if (r->issues_total > r->nissues) o = put(out, cap, o, " (+%ld more)", r->issues_total - r->nissues);
    if (nissue_files > 0) {
        o = put(out, cap, o, "\nissue files:");
        for (int i = 0; i < nissue_files && i < 4; i++) o = put(out, cap, o, " %s", issue_files[i]);
    }
    o = put(out, cap, o, "\n");
    out[o < cap ? o : cap - 1] = '\0';
    return strlen(out);
}

/* ---- running over a file source ------------------------------------------------------------ */

typedef struct {
    DlogFileInfo fi;
    DlogNameInfo ni;
} Entry;

static int entry_cmp(const void *a, const void *b)
{
    return dlog_name_compare(&((const Entry *)a)->ni, &((const Entry *)b)->ni);
}

static void init_run(Run *r, const DlogQuery *q)
{
    memset(r, 0, sizeof(*r));
    r->q = q;
    int ml = q->max_lines <= 0 ? DLOG_QUERY_DEFAULT_LINES : q->max_lines;
    if (ml > 1000) ml = 1000;
    r->max_lines = ml;
    if (!q->summary) r->slots = (char *)malloc((size_t)ml * SLOT_BYTES);
    if (!r->slots && !q->summary) r->max_lines = 1;
}

static long long now_of(const DlogQuery *q)
{
    if (q->now_ms) return q->now_ms;
    const DlogPlatform *p = dlog_platform();
    return p ? p->unix_ms() : 0;
}

static size_t msg_only(char *out, size_t cap, const char *fmt, ...) DLOG_PRINTF(3, 4);
static size_t msg_only(char *out, size_t cap, const char *fmt, ...)
{
    if (!out || cap == 0) return 0;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    if (w < 0) { out[0] = '\0'; return 0; }
    return strlen(out);
}

size_t dlog_query_run(const DlogQuery *q, const DlogQueryIO *io, char *out, size_t cap)
{
    if (!q || !io || !io->list || !io->read || !out || cap == 0) return 0;
    out[0] = '\0';
    DlogFileInfo *list = (DlogFileInfo *)malloc(sizeof(DlogFileInfo) * MAX_FILES);
    Entry *mains = (Entry *)malloc(sizeof(Entry) * MAX_FILES);
    Entry *issues = (Entry *)malloc(sizeof(Entry) * MAX_FILES);
    if (!list || !mains || !issues) { free(list); free(mains); free(issues); return 0; }
    int nl = io->list(io->ctx, list, MAX_FILES);
    int nm = 0, ni = 0;
    for (int i = 0; i < nl; i++) {
        DlogNameInfo info;
        if (!dlog_name_parse(list[i].name, &info) || info.is_png) continue;
        Entry e;
        e.fi = list[i];
        e.ni = info;
        if (info.issue) issues[ni++] = e;
        else mains[nm++] = e;
    }
    qsort(mains, (size_t)nm, sizeof(Entry), entry_cmp);
    qsort(issues, (size_t)ni, sizeof(Entry), entry_cmp);

    Run r;
    init_run(&r, q);
    size_t result;

    if (q->around_issue >= 0) {
        int pick = -1;
        for (int i = ni - 1; i >= 0; i--) {
            if (q->around_issue == 0 || (int)issues[i].ni.issue == q->around_issue) { pick = i; break; }
        }
        if (pick < 0) {
            if (ni == 0) {
                result = msg_only(out, cap, "# no issue marks in the log folder\n");
            } else {
                char nums[80];
                size_t o = 0;
                nums[0] = '\0';
                for (int i = 0; i < ni && o + 8 < sizeof(nums); i++)
                    o += (size_t)snprintf(nums + o, sizeof(nums) - o, "%s%u", i ? ", " : "", issues[i].ni.issue);
                result = msg_only(out, cap, "# no issue %d in the log folder (issue marks found: %s)\n", q->around_issue, nums);
            }
        } else {
            const char *name = issues[pick].fi.name;
            r.last_seen_ms = 0;
            feed_file(&r, io, name, mark_cb);
            long long mark = r.mark_ms ? r.mark_ms : r.last_seen_ms;
            r.from_ms = mark > WINDOW_BEFORE_MARK_MS ? mark - WINDOW_BEFORE_MARK_MS : 0;
            char title[64];
            (void)snprintf(title, sizeof(title), "issue %u: ", issues[pick].ni.issue);
            const char *names[1] = { name };
            char files[200];
            files_text(files, sizeof(files), names, 1);
            feed_file(&r, io, name, feed_cb);
            result = q->summary ? emit_summary(&r, title, files, NULL, 0, out, cap)
                                : emit_lines(&r, title, files, out, cap);
        }
    } else if (nm == 0) {
        result = msg_only(out, cap, "# no log files in the log folder (nothing has been written yet)\n");
    } else {
        long long now = now_of(q);
        r.from_ms = (q->since_minutes > 0 && now > 0) ? now - (long long)q->since_minutes * 60000LL : 0;
        int first = nm;
        int taken = 0;
        for (int i = nm - 1; i >= 0 && taken < MAX_RECENT; i--) {
            if (r.from_ms && mains[i].fi.mtime_ms > 0 && mains[i].fi.mtime_ms < r.from_ms) break;
            first = i;
            taken++;
        }
        const char *names[MAX_RECENT];
        int nn = 0;
        for (int i = first; i < nm; i++) {
            names[nn++] = mains[i].fi.name;
            feed_file(&r, io, mains[i].fi.name, feed_cb);
        }
        char files[260];
        files_text(files, sizeof(files), names, nn);
        if (nn == 0) (void)snprintf(files, sizeof(files), "none in the time window");
        const char *inames[4];
        int nin = 0;
        for (int i = ni - 1; i >= 0 && nin < 4; i--) inames[nin++] = issues[i].fi.name;
        result = q->summary ? emit_summary(&r, "", files, inames, nin, out, cap) : emit_lines(&r, "", files, out, cap);
    }
    free(r.slots);
    free(list);
    free(mains);
    free(issues);
    return result;
}

/* ---- one in-memory text --------------------------------------------------------------------- */

size_t dlog_query_text(const DlogQuery *q, const char *text, size_t len, char *out, size_t cap)
{
    if (!q || !out || cap == 0) return 0;
    out[0] = '\0';
    Run r;
    init_run(&r, q);
    long long now = now_of(q);
    r.from_ms = (q->since_minutes > 0 && q->around_issue < 0 && now > 0) ? now - (long long)q->since_minutes * 60000LL : 0;
    if (q->around_issue >= 0) {
        feed_text(&r, text ? text : "", text ? len : 0, mark_cb);
        long long mark = r.mark_ms ? r.mark_ms : r.last_seen_ms;
        r.from_ms = mark > WINDOW_BEFORE_MARK_MS ? mark - WINDOW_BEFORE_MARK_MS : 0;
    }
    feed_text(&r, text ? text : "", text ? len : 0, feed_cb);
    size_t n = q->summary ? emit_summary(&r, "", "(text)", NULL, 0, out, cap) : emit_lines(&r, "", "(text)", out, cap);
    free(r.slots);
    return n;
}

/* ---- live ------------------------------------------------------------------------------------- */

typedef struct {
    const DlogPlatform *p;
    char dir[DLOG_MAX_PATH];
} LiveCtx;

static int live_list(void *ctx, DlogFileInfo *out, size_t cap)
{
    LiveCtx *c = (LiveCtx *)ctx;
    int n = c->p->list_dir(c->dir, out, cap);
    return n < 0 ? 0 : n;
}

static long live_read(void *ctx, const char *name, unsigned long long off, void *buf, size_t cap)
{
    LiveCtx *c = (LiveCtx *)ctx;
    char path[DLOG_MAX_PATH];
    if (!c->p->file_read || !dlog_join(path, sizeof(path), c->dir, name)) return -1;
    return c->p->file_read(path, off, buf, cap);
}

size_t dlog_read_log(const DlogQuery *q, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!q) return 0;
    if (!dlog_is_started()) return msg_only(out, cap, "# debug log is not running\n");
    (void)dlog_flush_wait(200);
    DlogQuery qq = *q;
    const DlogPlatform *p = dlog_platform();
    if (!p) return msg_only(out, cap, "# debug log is not running\n");
    if (!qq.now_ms) qq.now_ms = p->unix_ms();

    DlogStatus st;
    dlog_status(&st);
    LiveCtx ctx;
    ctx.p = p;
    (void)snprintf(ctx.dir, sizeof(ctx.dir), "%s", st.dir);
    int have_files = 0;
    if (st.dir[0] && !st.mem_only && p->file_read) {
        DlogFileInfo *probe = (DlogFileInfo *)malloc(sizeof(DlogFileInfo) * MAX_FILES);
        if (probe) {
            int n = p->list_dir(st.dir, probe, MAX_FILES);
            for (int i = 0; i < n; i++) {
                DlogNameInfo ni;
                if (dlog_name_parse(probe[i].name, &ni) && !ni.is_png) { have_files = 1; break; }
            }
            free(probe);
        }
    }
    if (have_files) {
        DlogQueryIO io = { live_list, live_read, &ctx };
        return dlog_query_run(&qq, &io, out, cap);
    }
    size_t rs = dlog_recorder_size();
    char *text = (char *)malloc(rs + 16);
    if (!text) return msg_only(out, cap, "# out of memory\n");
    size_t got = dlog_recorder_copy(text, rs + 8);
    size_t n = dlog_query_text(&qq, text, got, out, cap);
    free(text);
    return n;
}
