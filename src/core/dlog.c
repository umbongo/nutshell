/* src/core/dlog.c -- see dlog.h and the design spec.
 *
 * Layout of this file: state; helpers; the ring buffers; the producer path
 * (dlog_emit and friends); repeat collapsing; files (open, rotate, retain,
 * fall back); the pump / writer thread; configuration; issue marks.
 *
 * Locking: ONE lock (the platform's), held only to copy a finished line into
 * the queue and the recorder and to touch the repeat bookkeeping. Everything
 * that does I/O (the pump) runs without it. State private to the pump (file
 * handle, size, part number) is serialised by the pump_busy flag, not the lock.
 */
#include "dlog.h"
#include "dlog_internal.h"
#include "config.h"
#include "secure_zero.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_BYTES   65536u
#define REPEAT_SLOTS  DLOG_CAT_COUNT
#define LINE_BUF      (DLOG_LINE_MAX + 64)

/* ---- state ---------------------------------------------------------------------- */

unsigned char g_dlog_capture[DLOG_CAT_COUNT];
static unsigned char g_file_max[DLOG_CAT_COUNT];   /* highest event level the FILE wants */
static int g_content;
static int g_screenshot;
static int g_enabled;

typedef struct {
    char  *buf;
    size_t cap, head, tail, used;
} Ring;

typedef struct {
    int      valid;
    unsigned count;
    unsigned sid;
    int      lvl;
    size_t   body_len;
    char     body[DLOG_LINE_MAX];
    unsigned since;          /* mono ms of the last line written for this run */
    long long last_ms;
    int      last_off;
    char     thread[8];
} Rep;

static struct {
    const DlogPlatform *p;
    int  started;            /* 0/1, atomic */
    int  threaded;
    int  stop;
    int  crashing;
    int  pump_busy;

    char exe_dir[260], exe_path[320], version[32], extra[256];
    unsigned pid;
    long long start_ms;
    int  start_off;
    char stem[64];

    DlogConfig cfg;
    char dir_resolved[DLOG_MAX_PATH];
    unsigned long long file_cap, total_cap;
    char header[1536];
    int  dir_changed;

    Ring q, rec;
    size_t q_reserve;
    unsigned long long dropped, dropped_total;
    unsigned long long q_in, q_out;
    char *chunk;

    Rep rep[REPEAT_SLOTS];

    /* pump-private (guarded by pump_busy) */
    void *fh;
    char  cur_name[96];
    char  prev_name[96];
    unsigned long long cur_size;
    unsigned part;
    char  dir_in_use[DLOG_MAX_PATH];
    int   using_fallback;
    int   mem_only;
    unsigned retry_at;
    int   have_retry;
    int   format_written;
    int   file_open;         /* mirrors fh != NULL, atomic, for other threads */

    /* shared status, written under the lock */
    char path[DLOG_MAX_PATH];
    char note[256];
    unsigned issues;
    unsigned long long folder_bytes;
    unsigned folder_files;
} G;

static _Thread_local char t_tag[8];
static _Thread_local DlogEscState t_esc[16];

static char g_api_key[256];

/* ---- lock helpers ----------------------------------------------------------------- */

static int lk(void)
{
    if (G.crashing) {
        if (G.p->try_lock) return G.p->try_lock() ? 1 : 0;
        return 0;
    }
    G.p->lock();
    return 1;
}
static void ul(int held) { if (held) G.p->unlock(); }

static void wake_writer(void) { if (G.p && G.p->wake) G.p->wake(); }

/* ---- rings -------------------------------------------------------------------------- */

static int ring_init(Ring *r, size_t cap)
{
    r->buf = (char *)malloc(cap);
    if (!r->buf) return 0;
    r->cap = cap;
    r->head = r->tail = r->used = 0;
    return 1;
}

static void ring_free(Ring *r)
{
    free(r->buf);
    memset(r, 0, sizeof(*r));
}

static void ring_push(Ring *r, const char *src, size_t n)
{
    size_t first = r->cap - r->head;
    if (first > n) first = n;
    memcpy(r->buf + r->head, src, first);
    if (n > first) memcpy(r->buf, src + first, n - first);
    r->head = (r->head + n) % r->cap;
    r->used += n;
}

static char ring_at(const Ring *r, size_t i) { return r->buf[(r->tail + i) % r->cap]; }

static void ring_drop_line(Ring *r)
{
    while (r->used > 0) {
        char c = r->buf[r->tail];
        r->tail = (r->tail + 1) % r->cap;
        r->used--;
        if (c == '\n') break;
    }
}

/* Overwriting ring of whole lines (the recorder). */
static void ring_put_overwrite(Ring *r, const char *line, size_t len)
{
    if (!r->buf || len > r->cap) return;
    while (r->cap - r->used < len) ring_drop_line(r);
    ring_push(r, line, len);
}

/* Copy out whole lines, up to max bytes, consuming them. */
static size_t ring_take_lines(Ring *r, char *dst, size_t max)
{
    size_t n = r->used < max ? r->used : max;
    if (n == 0) return 0;
    size_t first = r->cap - r->tail;
    if (first > n) first = n;
    memcpy(dst, r->buf + r->tail, first);
    if (n > first) memcpy(dst + first, r->buf, n - first);
    if (n < r->used) {                  /* cut back to the last LF */
        size_t k = n;
        while (k > 0 && dst[k - 1] != '\n') k--;
        if (k > 0) n = k;
    }
    r->tail = (r->tail + n) % r->cap;
    r->used -= n;
    return n;
}

/* ---- small helpers --------------------------------------------------------------------- */

static void set_str(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

static const char *thread_tag(void) { return t_tag[0] ? t_tag : "?"; }

void dlog_set_thread_tag(const char *tag)
{
    size_t i = 0;
    if (tag) for (; tag[i] && i < 7; i++) t_tag[i] = tag[i];
    t_tag[i] = '\0';
}

const DlogPlatform *dlog_platform(void) { return G.started ? G.p : NULL; }
int dlog_is_started(void) { return __atomic_load_n(&G.started, __ATOMIC_ACQUIRE); }
int dlog_enabled(void) { return __atomic_load_n(&g_enabled, __ATOMIC_RELAXED); }
int dlog_content_enabled(void) { return __atomic_load_n(&g_content, __ATOMIC_RELAXED); }
int dlog_screenshot_enabled(void) { return __atomic_load_n(&g_screenshot, __ATOMIC_RELAXED); }

static void os_err(int err, char *buf, size_t cap)
{
    if (G.p && G.p->err_text) G.p->err_text(err, buf, cap);
    else (void)snprintf(buf, cap, "error %d", err);
}

/* ---- repeat collapsing and queueing (called with the lock held) ----------------------------- */

static void enqueue_locked(const char *line, size_t len, int to_file, int to_rec, int lvl, int *wake)
{
    if (!G.started) return;
    if (to_rec) ring_put_overwrite(&G.rec, line, len);
    if (to_file) {
        int important = (lvl <= (int)DLOG_WARN);          /* ISSUE, ERROR, WARN */
        size_t freeb = G.q.cap - G.q.used;
        size_t need = important ? len : len + G.q_reserve;
        if (freeb < need) {
            G.dropped++;
            G.dropped_total++;
        } else {
            ring_push(&G.q, line, len);
            __atomic_add_fetch(&G.q_in, (unsigned long long)len, __ATOMIC_RELEASE);
        }
        if (important) *wake = 1;
    }
}

static void flush_repeat_locked(int cat, unsigned mono, int *wake)
{
    Rep *r = &G.rep[cat];
    if (!r->valid || r->count == 0) return;
    char line[LINE_BUF];
    char last[DLOG_TS_LEN + 4];
    dlog_format_timestamp(r->last_ms, r->last_off, last);
    char ev[64];
    size_t k = 0;
    while (k + 1 < sizeof(ev) && k < r->body_len && r->body[k] != ' ') { ev[k] = r->body[k]; k++; }
    ev[k] = '\0';
    long long now = G.p->unix_ms();
    int off = G.p->utc_offset_min();
    size_t pl = dlog_make_prefix(line, sizeof(line), now, off, r->lvl, cat, r->sid, r->thread);
    if (pl) {
        DlogOut o;
        dlog_out_init(&o, line, DLOG_LINE_MAX, pl, DLOG_OUT_RESERVE);
        dlog_out_event(&o, "repeat");
        char fields[160];
        (void)snprintf(fields, sizeof(fields), "n=%u last=%s of=%s", r->count, last, ev);
        dlog_out_raw_fields(&o, fields);
        size_t len = dlog_out_finish(&o, 1);
        int to_file = (r->lvl <= (int)__atomic_load_n(&g_file_max[cat], __ATOMIC_RELAXED)) && !G.mem_only;
        int to_rec = (r->lvl <= (int)g_dlog_capture[cat]);
        enqueue_locked(line, len, to_file, to_rec, r->lvl, wake);
    }
    r->count = 0;
    r->since = mono;
}

static void emit_v(int cat, int lvl, unsigned sid, const char *event, const char *fmt,
                   va_list ap, const char *raw, int force)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return;
    if (cat < 0 || cat >= (int)DLOG_CAT_COUNT) return;
    int fmax = (int)__atomic_load_n(&g_file_max[cat], __ATOMIC_RELAXED);
    int cmax = (int)__atomic_load_n(&g_dlog_capture[cat], __ATOMIC_RELAXED);
    int to_file = force || lvl <= fmax;
    int to_rec = force || lvl <= cmax;
    if (to_file && __atomic_load_n(&G.mem_only, __ATOMIC_RELAXED)) to_file = 0;
    if (!to_file && !to_rec) return;

    char line[LINE_BUF];
    long long ms = G.p->unix_ms();
    int off = G.p->utc_offset_min();
    unsigned mono = G.p->mono_ms();
    const char *tag = thread_tag();
    size_t pl = dlog_make_prefix(line, sizeof(line), ms, off, lvl, cat, sid, tag);
    if (pl == 0) return;
    DlogOut o;
    dlog_out_init(&o, line, DLOG_LINE_MAX, pl, DLOG_OUT_RESERVE);
    dlog_out_event(&o, event);
    if (raw) dlog_out_raw_fields(&o, raw);
    else if (fmt) dlog_out_fmt(&o, fmt, ap);
    size_t len = dlog_out_finish(&o, 1);
    /* the last line of defence, on the body only */
    size_t body = dlog_redact_line(line + pl, len - pl, DLOG_LINE_MAX - pl + 1);
    len = pl + body;
    size_t body_len = body - 1;               /* without the LF */

    int wake = 0;
    int held = lk();
    if (G.started) {
        Rep *r = &G.rep[cat];
        int collapsible = !force && lvl != (int)DLOG_ISSUE;
        if (collapsible && r->valid && r->sid == sid && r->lvl == lvl && r->body_len == body_len &&
            memcmp(r->body, line + pl, body_len) == 0) {
            r->count++;
            r->last_ms = ms;
            r->last_off = off;
            if ((unsigned)(mono - r->since) >= DLOG_REPEAT_MS) flush_repeat_locked(cat, mono, &wake);
        } else {
            if (collapsible) {
                if (r->valid && r->count) flush_repeat_locked(cat, mono, &wake);
                r->valid = 1;
                r->count = 0;
                r->sid = sid;
                r->lvl = lvl;
                r->body_len = body_len;
                memcpy(r->body, line + pl, body_len);
                r->since = mono;
                r->last_ms = ms;
                r->last_off = off;
                set_str(r->thread, sizeof(r->thread), tag);
            }
            enqueue_locked(line, len, to_file, to_rec, lvl, &wake);
        }
    }
    ul(held);
    if (wake) wake_writer();
}

void dlog_emit(DlogCat cat, DlogEvLevel lvl, unsigned sid, const char *event, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit_v((int)cat, (int)lvl, sid, event, fmt, ap, NULL, 0);
    va_end(ap);
}

void dlog_emit_fields(DlogCat cat, DlogEvLevel lvl, unsigned sid, const char *event, const char *fields)
{
    va_list ap;
    memset(&ap, 0, sizeof(ap));
    emit_v((int)cat, (int)lvl, sid, event, NULL, ap, fields ? fields : "", 0);
}

/* A line the logger itself writes, regardless of levels. */
static void emit_forced(int cat, int lvl, unsigned sid, const char *event, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit_v(cat, lvl, sid, event, fmt, ap, NULL, 1);
    va_end(ap);
}

void dlog_emit_content(DlogCat cat, DlogEvLevel lvl, unsigned sid, const char *event,
                       const char *key, const void *data, size_t len)
{
    if (!dlog_content_enabled() || !data) return;
    if ((int)lvl > dlog_capture_level((int)cat)) return;
    size_t take = len < 3900 ? len : 3900;
    char scrubbed[3900 + 8 * 24];
    size_t sl = dlog_scrub_copy(scrubbed, sizeof(scrubbed), (const char *)data, take);
    char esc[4000];
    size_t used = 0;
    DlogEscState fresh;
    DlogEscState *st = &fresh;
    dlog_esc_init(&fresh);
    if (event && strcmp(event, "term.rx") == 0) st = &t_esc[sid % 16u];
    (void)dlog_escape_bytes(st, scrubbed, sl, esc, sizeof(esc), &used);
    char k[48];
    set_str(k, sizeof(k), (key && key[0]) ? key : "data");
    for (char *c = k; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_')) *c = '_';
    char fields[4200];
    (void)snprintf(fields, sizeof(fields), "%s=\"%s\" len=%zu%s", k, esc, len,
                   (take < len || used < sl) ? " truncated=1" : "");
    va_list ap;
    memset(&ap, 0, sizeof(ap));
    emit_v((int)cat, (int)lvl, sid, event, NULL, ap, fields, 0);
}

/* ---- the writer's side: files ------------------------------------------------------------------ */

static int pump_acquire(unsigned timeout_ms)
{
    unsigned waited = 0;
    for (;;) {
        if (!__atomic_exchange_n(&G.pump_busy, 1, __ATOMIC_ACQUIRE)) return 1;
        if (waited >= timeout_ms) return 0;
        if (G.p->sleep_ms) G.p->sleep_ms(1);
        waited++;
    }
}
static void pump_release(void) { __atomic_store_n(&G.pump_busy, 0, __ATOMIC_RELEASE); }

static void set_note(const char *s)
{
    int h = lk();
    set_str(G.note, sizeof(G.note), s);
    ul(h);
}

static void set_path(const char *path)
{
    int h = lk();
    set_str(G.path, sizeof(G.path), path);
    ul(h);
}

/* Write a log line the logger makes itself, straight to the file. */
static void write_self_line(int lvl, const char *event, const char *fields)
{
    if (!G.fh) return;
    char line[LINE_BUF];
    size_t n = dlog_format_line(line, sizeof(line), G.p->unix_ms(), G.p->utc_offset_min(), lvl,
                                (int)DLOG_GENERAL, 0, "log", event, fields);
    if (n && G.p->file_write(G.fh, line, n) == 0) G.cur_size += n;
}

static void build_levels_string(const DlogConfig *c, char *out, size_t cap)
{
    out[0] = '\0';
    size_t o = 0;
    for (int i = 0; i < (int)DLOG_CAT_COUNT; i++) {
        const char *ln = dlog_level_name(c->enabled ? (int)c->levels[i] : (int)DLOG_LVL_ERRORS);
        int w = snprintf(out + o, cap - o, "%s%s:%s", i ? "," : "", dlog_cat_name(i), ln ? ln : "?");
        if (w < 0 || (size_t)w >= cap - o) break;
        o += (size_t)w;
    }
}

static void fields_fmt(DlogOut *o, const char *fmt, ...) DLOG_PRINTF(2, 3);
static void fields_fmt(DlogOut *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    dlog_out_fmt(o, fmt, ap);
    va_end(ap);
}

/* The app.start line for the current configuration (lock held or startup). */
static void build_header_locked(void)
{
    char levels[200];
    build_levels_string(&G.cfg, levels, sizeof(levels));
    char line[LINE_BUF];
    size_t pl = dlog_make_prefix(line, sizeof(line), G.start_ms, G.start_off, (int)DLOG_INFO,
                                 (int)DLOG_GENERAL, 0, "ui");
    if (!pl) { G.header[0] = '\0'; return; }
    DlogOut o;
    dlog_out_init(&o, line, DLOG_LINE_MAX, pl, DLOG_OUT_RESERVE);
    dlog_out_event(&o, "app.start");
    fields_fmt(&o, "version=%s pid=%u exe=%s debug=%d levels=%s content=%d recorder=%d recorder_level=%s",
               G.version, G.pid, G.exe_path, G.cfg.enabled ? 1 : 0, levels, G.cfg.include_content ? 1 : 0,
               G.cfg.recorder ? 1 : 0, dlog_level_name((int)G.cfg.recorder_level));
    dlog_out_raw_fields(&o, G.extra);
    (void)dlog_out_finish(&o, 1);
    set_str(G.header, sizeof(G.header), line);
}

static void close_file(void)
{
    if (G.fh) {
        G.p->file_close(G.fh);
        G.fh = NULL;
    }
    __atomic_store_n(&G.file_open, 0, __ATOMIC_RELEASE);
}

static void run_retention(void)
{
    if (!G.dir_in_use[0] || !G.p->list_dir || !G.p->remove_file) return;
    enum { MAXF = 600 };
    DlogFileInfo *files = (DlogFileInfo *)malloc(sizeof(DlogFileInfo) * MAXF);
    unsigned char *state = (unsigned char *)calloc(MAXF, 1);
    if (!files || !state) { free(files); free(state); return; }
    int n = G.p->list_dir(G.dir_in_use, files, MAXF);
    if (n > 0) {
        int i;
        int guard = 0;
        while ((i = dlog_retention_next(files, (size_t)n, G.cur_name, G.total_cap, state)) >= 0 && guard++ < MAXF) {
            char path[DLOG_MAX_PATH];
            if (dlog_join(path, sizeof(path), G.dir_in_use, files[i].name) && G.p->remove_file(path) == 0)
                state[i] = 1;
            else
                state[i] = 2;
        }
        size_t cnt = 0;
        unsigned long long total = 0;
        for (int k = 0; k < n; k++) {
            if (state[k] == 1) continue;
            if (dlog_name_parse(files[k].name, NULL)) { total += files[k].size; cnt++; }
        }
        int h = lk();
        G.folder_bytes = total;
        G.folder_files = (unsigned)cnt;
        ul(h);
    }
    free(files);
    free(state);
}

static void join_or_empty(char *out, size_t cap, const char *dir, const char *name)
{
    if (!dlog_join(out, cap, dir, name)) out[0] = '\0';
}

/* Open the next file of this run, falling back as the spec says. Pump-private. */
static int open_next_file(void)
{
    if (G.have_retry && (int)(G.p->mono_ms() - G.retry_at) < 0) return 0;
    G.have_retry = 0;

    char cand[2][DLOG_MAX_PATH];
    int nc = 0;
    if (!G.using_fallback && G.dir_resolved[0]) set_str(cand[nc++], DLOG_MAX_PATH, G.dir_resolved);
    char fb[DLOG_MAX_PATH];
    if (G.p->local_appdata && dlog_fallback_dir(G.p->local_appdata(), fb, sizeof(fb))) {
        if (nc == 0 || strcmp(fb, cand[0]) != 0) set_str(cand[nc++], DLOG_MAX_PATH, fb);
    }

    unsigned part = G.part + 1;
    char name[96];
    (void)dlog_file_name(name, sizeof(name), G.stem, part);
    char first_err[DLOG_MAX_PATH + 160];
    first_err[0] = '\0';
    for (int i = 0; i < nc; i++) {
        char text[96];
        int err = G.p->mkdir_p(cand[i]);
        void *h = NULL;
        unsigned long long size = 0;
        char path[DLOG_MAX_PATH];
        join_or_empty(path, sizeof(path), cand[i], name);
        if (err == 0) err = G.p->file_open(path, &h, &size);
        if (err != 0) {
            os_err(err, text, sizeof(text));
            char msg[DLOG_MAX_PATH + 160];
            (void)snprintf(msg, sizeof(msg), "%s is not writable: %s", cand[i], text);
            if (!first_err[0]) set_str(first_err, sizeof(first_err), msg);
            set_note(first_err);
            continue;
        }
        /* opened */
        G.fh = h;
        G.cur_size = size;
        G.part = part;
        set_str(G.cur_name, sizeof(G.cur_name), name);
        set_str(G.dir_in_use, sizeof(G.dir_in_use), cand[i]);
        if (G.dir_resolved[0] && strcmp(cand[i], G.dir_resolved) != 0) G.using_fallback = 1;
        set_path(path);
        __atomic_store_n(&G.file_open, 1, __ATOMIC_RELEASE);

        if (!G.format_written) {
            char ft[8192];
            size_t fl = dlog_format_text(ft, sizeof(ft), G.version);
            char fpath[DLOG_MAX_PATH];
            join_or_empty(fpath, sizeof(fpath), cand[i], "FORMAT.txt");
            if (fpath[0] && fl) (void)G.p->file_write_all(fpath, ft, fl);
            G.format_written = 1;
        }
        if (G.prev_name[0]) {
            char f[200], v[130];
            (void)dlog_fmt_value(v, sizeof(v), G.prev_name);
            (void)snprintf(f, sizeof(f), "prev=%s", v);
            write_self_line((int)DLOG_INFO, "log.continue", f);
        }
        int h2 = lk();
        char header[1536];
        set_str(header, sizeof(header), G.header);
        ul(h2);
        size_t hl = strlen(header);
        if (hl && G.p->file_write(G.fh, header, hl) == 0) G.cur_size += hl;
        run_retention();
        if (i > 0 && first_err[0]) {
            emit_forced((int)DLOG_GENERAL, (int)DLOG_WARN, 0, "log.fallback", "dir=%s reason=%s using=%s",
                        G.dir_resolved, first_err, cand[i]);
        }
        return 1;
    }
    /* nothing worked: keep events in memory */
    G.mem_only = 1;
    __atomic_store_n(&G.mem_only, 1, __ATOMIC_RELEASE);
    emit_forced((int)DLOG_GENERAL, (int)DLOG_WARN, 0, "log.memory_only", "reason=%s", first_err[0] ? first_err : "no log folder");
    return 0;
}

static void rotate_file(void)
{
    set_str(G.prev_name, sizeof(G.prev_name), G.cur_name);
    close_file();
}

static void write_bytes(const char *buf, size_t n)
{
    if (__atomic_load_n(&G.mem_only, __ATOMIC_ACQUIRE)) return;
    if (!G.fh && !open_next_file()) return;
    size_t off = 0;
    int forced_one = 0;
    while (off < n) {
        size_t batch = 0;
        while (off + batch < n) {
            const char *start = buf + off + batch;
            const char *nl = memchr(start, '\n', n - off - batch);
            size_t ll = nl ? (size_t)(nl - start) + 1u : n - off - batch;
            int rot = (batch == 0) ? dlog_should_rotate(G.cur_size, ll, G.file_cap)
                                   : (G.cur_size + batch + ll > G.file_cap);
            if (rot && !(batch == 0 && forced_one)) break;
            batch += ll;
            if (forced_one) break;           /* just the one line into a fresh file */
        }
        if (batch == 0) {
            rotate_file();
            if (!open_next_file()) return;
            forced_one = 1;
            continue;
        }
        forced_one = 0;
        int err = G.p->file_write(G.fh, buf + off, batch);
        if (err != 0) {
            char text[96], msg[256];
            os_err(err, text, sizeof(text));
            (void)snprintf(msg, sizeof(msg), "write failed: %s", text);
            set_note(msg);
            close_file();
            G.retry_at = G.p->mono_ms() + 2000u;
            G.have_retry = 1;
            return;
        }
        G.cur_size += batch;
        off += batch;
    }
}

static void write_dropped_line(unsigned long long n)
{
    char line[LINE_BUF], f[48];
    (void)snprintf(f, sizeof(f), "n=%llu", n);
    size_t l = dlog_format_line(line, sizeof(line), G.p->unix_ms(), G.p->utc_offset_min(), (int)DLOG_WARN,
                                (int)DLOG_GENERAL, 0, "log", "dlog.dropped", f);
    if (l) write_bytes(line, l);
}

static void handle_dir_change(void)
{
    int h = lk();
    int changed = G.dir_changed;
    G.dir_changed = 0;
    char to[DLOG_MAX_PATH];
    set_str(to, sizeof(to), G.dir_resolved);
    ul(h);
    if (!changed) return;
    if (G.fh) {
        char v[DLOG_MAX_PATH + 8], f[DLOG_MAX_PATH + 16];
        (void)dlog_fmt_value(v, sizeof(v), to);
        (void)snprintf(f, sizeof(f), "to=%s", v);
        write_self_line((int)DLOG_INFO, "log.moved", f);
        rotate_file();
    }
    G.using_fallback = 0;
    G.dir_in_use[0] = '\0';
    if (G.mem_only) {
        G.mem_only = 0;
        __atomic_store_n(&G.mem_only, 0, __ATOMIC_RELEASE);
    }
    set_note("");
}

static void do_pump(void)
{
    dlog_tick();
    handle_dir_change();
    for (;;) {
        int h = lk();
        size_t n = ring_take_lines(&G.q, G.chunk, CHUNK_BYTES);
        unsigned long long dropped = G.dropped;
        G.dropped = 0;
        ul(h);
        if (n == 0 && dropped == 0) break;
        if (dropped) write_dropped_line(dropped);
        if (n) write_bytes(G.chunk, n);
        __atomic_add_fetch(&G.q_out, (unsigned long long)n, __ATOMIC_RELEASE);
    }
}

int dlog_pump(void)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return 0;
    if (!pump_acquire(0)) return 0;
    do_pump();
    pump_release();
    return 1;
}

void dlog_flush(void) { wake_writer(); }

void dlog_tick(void)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return;
    unsigned mono = G.p->mono_ms();
    int wake = 0;
    int h = lk();
    for (int c = 0; c < (int)DLOG_CAT_COUNT; c++) {
        Rep *r = &G.rep[c];
        if (r->valid && r->count && (unsigned)(mono - r->since) >= DLOG_REPEAT_MS)
            flush_repeat_locked(c, mono, &wake);
    }
    ul(h);
}

int dlog_flush_wait(unsigned ms)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return 1;
    if (!G.threaded) { (void)dlog_pump(); return 1; }
    unsigned long long target = __atomic_load_n(&G.q_in, __ATOMIC_ACQUIRE);
    unsigned start = G.p->mono_ms();
    wake_writer();
    while (__atomic_load_n(&G.q_out, __ATOMIC_ACQUIRE) < target) {
        if ((unsigned)(G.p->mono_ms() - start) >= ms) return 0;
        if (G.p->sleep_ms) G.p->sleep_ms(1);
        wake_writer();
    }
    return 1;
}

static void writer_main(void *arg)
{
    (void)arg;
    dlog_set_thread_tag("log");
    while (!__atomic_load_n(&G.stop, __ATOMIC_ACQUIRE)) {
        if (G.p->wait) G.p->wait(250);
        else if (G.p->sleep_ms) G.p->sleep_ms(250);
        (void)dlog_pump();
    }
}

void dlog_crash_note(unsigned long code, const void *addr)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return;
    G.crashing = 1;
    emit_forced((int)DLOG_GENERAL, (int)DLOG_ERROR, 0, "app.crash", "code=0x%08lx addr=%p", code, addr);
    do_pump();                       /* without the pump guard: the writer may be the thread that died */
}

/* ---- configuration --------------------------------------------------------------------------------- */

void dlog_config_defaults(DlogConfig *c)
{
    memset(c, 0, sizeof(*c));
    c->enabled = 0;
    for (int i = 0; i < (int)DLOG_CAT_COUNT; i++) c->levels[i] = (unsigned char)DLOG_DEFAULT_LEVEL;
    c->include_content = 0;
    c->screenshot = 0;
    c->recorder = 1;
    c->recorder_level = (unsigned char)DLOG_LVL_TRACE;
    set_str(c->dir, sizeof(c->dir), "logs");
    c->max_file_mb = 10;
    c->max_total_mb = 50;
}

void dlog_config_from_settings(const struct Settings *s, DlogConfig *c)
{
    dlog_config_defaults(c);
    if (!s) return;
    c->enabled = s->debug_enabled ? 1 : 0;
    for (int i = 0; i < (int)DLOG_CAT_COUNT; i++)
        if (s->debug_levels[i] < (unsigned char)DLOG_LVL_COUNT) c->levels[i] = s->debug_levels[i];
    c->include_content = s->debug_include_content ? 1 : 0;
    c->screenshot = s->debug_screenshot ? 1 : 0;
    c->recorder = s->debug_recorder ? 1 : 0;
    if (s->debug_recorder_level < (unsigned char)DLOG_LVL_COUNT) c->recorder_level = s->debug_recorder_level;
    set_str(c->dir, sizeof(c->dir), s->debug_log_dir[0] ? s->debug_log_dir : "logs");
    c->max_file_mb = s->debug_max_file_mb;
    c->max_total_mb = s->debug_max_total_mb;
}

static void compute_levels(const DlogConfig *c, unsigned char *file, unsigned char *cap)
{
    for (int i = 0; i < (int)DLOG_CAT_COUNT; i++) {
        int fl = c->enabled ? (int)c->levels[i] : (int)DLOG_LVL_ERRORS;
        int fm = dlog_level_max_event(fl);
        int cm = fm;
        if (c->enabled && c->recorder && fl != (int)DLOG_LVL_OFF) {
            int rm = dlog_level_max_event((int)c->recorder_level);
            if (rm > cm) cm = rm;
        }
        file[i] = (unsigned char)fm;
        cap[i] = (unsigned char)cm;
    }
}

void dlog_apply(const DlogConfig *in)
{
    if (!in || !__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return;
    DlogConfig c = *in;
    for (int i = 0; i < (int)DLOG_CAT_COUNT; i++)
        if (c.levels[i] >= (unsigned char)DLOG_LVL_COUNT) c.levels[i] = (unsigned char)DLOG_DEFAULT_LEVEL;
    if (c.recorder_level >= (unsigned char)DLOG_LVL_COUNT) c.recorder_level = (unsigned char)DLOG_LVL_TRACE;
    if (c.max_file_mb < 1) c.max_file_mb = 1;
    if (c.max_file_mb > 100) c.max_file_mb = 100;
    if (c.max_total_mb < 5) c.max_total_mb = 5;
    if (c.max_total_mb > 5000) c.max_total_mb = 5000;
    c.enabled = c.enabled ? 1 : 0;
    c.dir[sizeof(c.dir) - 1] = '\0';

    char resolved[DLOG_MAX_PATH];
    if (!dlog_resolve_dir(c.dir, G.exe_dir, resolved, sizeof(resolved))) resolved[0] = '\0';

    unsigned char fl[DLOG_CAT_COUNT], cp[DLOG_CAT_COUNT];
    compute_levels(&c, fl, cp);

    int file_open = __atomic_load_n(&G.file_open, __ATOMIC_ACQUIRE);
    char levels[200];
    build_levels_string(&c, levels, sizeof(levels));

    int h = lk();
    if (strcmp(resolved, G.dir_resolved) != 0) G.dir_changed = 1;
    set_str(G.dir_resolved, sizeof(G.dir_resolved), resolved);
    G.cfg = c;
    G.file_cap = dlog_file_cap_bytes(c.enabled, c.max_file_mb);
    G.total_cap = dlog_total_cap_bytes(c.max_total_mb);
    for (int i = 0; i < (int)DLOG_CAT_COUNT; i++) {
        __atomic_store_n(&g_file_max[i], fl[i], __ATOMIC_RELAXED);
        __atomic_store_n(&g_dlog_capture[i], cp[i], __ATOMIC_RELAXED);
    }
    __atomic_store_n(&g_content, c.enabled && c.include_content ? 1 : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_screenshot, c.screenshot ? 1 : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_enabled, c.enabled, __ATOMIC_RELAXED);
    build_header_locked();
    ul(h);

    if (file_open) {
        emit_forced((int)DLOG_GENERAL, (int)DLOG_INFO, 0, "dlog.config",
                    "enabled=%d levels=%s content=%d recorder=%d recorder_level=%s dir=%s max_file_mb=%d max_total_mb=%d",
                    c.enabled, levels, c.include_content ? 1 : 0, c.recorder ? 1 : 0,
                    dlog_level_name((int)c.recorder_level), resolved, c.max_file_mb, c.max_total_mb);
    }
}

void dlog_configure(const struct Settings *s)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return;
    DlogConfig c;
    dlog_config_from_settings(s, &c);
    dlog_apply(&c);
    /* the AI API key is a known secret: scrub it from content */
    const char *key = s ? s->ai_api_key : "";
    if (g_api_key[0] && strcmp(g_api_key, key) != 0) {
        dlog_secret_remove(g_api_key);
        secure_zero(g_api_key, sizeof(g_api_key));
    }
    if (key[0] && dlog_secret_add(key)) set_str(g_api_key, sizeof(g_api_key), key);
}

/* ---- lifecycle ------------------------------------------------------------------------------------ */

int dlog_start_with(const DlogPlatform *p, const DlogStartInfo *info)
{
    if (!p || !p->lock || !p->unlock || !p->unix_ms || !p->utc_offset_min || !p->mono_ms ||
        !p->mkdir_p || !p->file_open || !p->file_write || !p->file_close || !p->file_write_all ||
        !p->list_dir || !p->remove_file)
        return 0;
    if (__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return 0;

    memset(&G, 0, sizeof(G));
    G.p = p;
    size_t qb = (info && info->queue_bytes) ? info->queue_bytes : DLOG_QUEUE_BYTES;
    size_t rb = (info && info->recorder_bytes) ? info->recorder_bytes : DLOG_RECORDER_BYTES;
    if (qb < 8192) qb = 8192;
    if (rb < 8192) rb = 8192;
    if (!ring_init(&G.q, qb)) return 0;
    if (!ring_init(&G.rec, rb)) { ring_free(&G.q); return 0; }
    G.chunk = (char *)malloc(CHUNK_BYTES);
    if (!G.chunk) { ring_free(&G.q); ring_free(&G.rec); return 0; }
    G.q_reserve = qb / 8;

    if (info) {
        set_str(G.exe_dir, sizeof(G.exe_dir), info->exe_dir);
        set_str(G.exe_path, sizeof(G.exe_path), info->exe_path);
        set_str(G.version, sizeof(G.version), info->version ? info->version : "unknown");
        set_str(G.extra, sizeof(G.extra), info->extra);
        G.pid = info->pid;
    } else {
        set_str(G.version, sizeof(G.version), "unknown");
    }
    G.start_ms = p->unix_ms();
    G.start_off = p->utc_offset_min();
    (void)dlog_stem_name(G.stem, sizeof(G.stem), G.start_ms, G.start_off, G.pid);

    dlog_config_defaults(&G.cfg);
    if (!dlog_resolve_dir(G.cfg.dir, G.exe_dir, G.dir_resolved, sizeof(G.dir_resolved))) G.dir_resolved[0] = '\0';
    G.file_cap = dlog_file_cap_bytes(0, G.cfg.max_file_mb);
    G.total_cap = dlog_total_cap_bytes(G.cfg.max_total_mb);
    {
        unsigned char fl[DLOG_CAT_COUNT], cp[DLOG_CAT_COUNT];
        compute_levels(&G.cfg, fl, cp);
        for (int i = 0; i < (int)DLOG_CAT_COUNT; i++) {
            g_file_max[i] = fl[i];
            g_dlog_capture[i] = cp[i];
        }
    }
    g_content = 0;
    g_screenshot = 0;
    g_enabled = 0;
    build_header_locked();
    dlog_set_thread_tag("ui");

    __atomic_store_n(&G.started, 1, __ATOMIC_RELEASE);
    if (p->thread_start && p->thread_start(writer_main, NULL)) G.threaded = 1;
    return 1;
}

void dlog_shutdown(void)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return;
    if (G.threaded) {
        __atomic_store_n(&G.stop, 1, __ATOMIC_RELEASE);
        wake_writer();
        if (G.p->thread_join) G.p->thread_join();
        G.threaded = 0;
    }
    /* flush every pending repeat, then drain */
    {
        int wake = 0;
        unsigned mono = G.p->mono_ms();
        int h = lk();
        for (int c = 0; c < (int)DLOG_CAT_COUNT; c++) flush_repeat_locked(c, mono, &wake);
        ul(h);
    }
    (void)pump_acquire(1000);
    do_pump();
    close_file();
    pump_release();
    __atomic_store_n(&G.started, 0, __ATOMIC_RELEASE);
    {   /* barrier: any emitter inside the lock finishes before the rings go */
        int h = lk();
        ul(h);
    }
    for (int i = 0; i < (int)DLOG_CAT_COUNT; i++) {
        g_dlog_capture[i] = 0;
        g_file_max[i] = 0;
    }
    g_content = g_screenshot = g_enabled = 0;
    dlog_secret_clear();
    secure_zero(g_api_key, sizeof(g_api_key));
    ring_free(&G.q);
    ring_free(&G.rec);
    free(G.chunk);
    memset(&G, 0, sizeof(G));
}

/* ---- status, recorder -------------------------------------------------------------------------------- */

void dlog_status(DlogStatus *st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return;
    int h = lk();
    st->started = 1;
    st->enabled = G.cfg.enabled;
    st->file_open = G.fh != NULL || __atomic_load_n(&G.file_open, __ATOMIC_RELAXED);
    st->mem_only = G.mem_only;
    set_str(st->dir, sizeof(st->dir), G.dir_in_use[0] ? G.dir_in_use : G.dir_resolved);
    set_str(st->path, sizeof(st->path), st->file_open ? G.path : "");
    set_str(st->note, sizeof(st->note), G.note);
    set_str(st->stem, sizeof(st->stem), G.stem);
    st->dropped_total = G.dropped_total;
    st->issues = G.issues;
    ul(h);
}

void dlog_folder_usage(unsigned long long *bytes, unsigned *files)
{
    unsigned long long b = 0;
    unsigned f = 0;
    if (__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) {
        DlogStatus st;
        dlog_status(&st);
        if (st.dir[0] && G.p->list_dir) {
            enum { MAXF = 600 };
            DlogFileInfo *fi = (DlogFileInfo *)malloc(sizeof(DlogFileInfo) * MAXF);
            if (fi) {
                int n = G.p->list_dir(st.dir, fi, MAXF);
                size_t cnt = 0;
                if (n > 0) b = dlog_files_total(fi, (size_t)n, &cnt);
                f = (unsigned)cnt;
                free(fi);
            }
        }
    }
    if (bytes) *bytes = b;
    if (files) *files = f;
}

size_t dlog_recorder_size(void)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return 0;
    int h = lk();
    size_t n = G.rec.used;
    ul(h);
    return n;
}

size_t dlog_recorder_copy(char *buf, size_t cap)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return 0;
    int h = lk();
    size_t used = G.rec.used;
    size_t out = 0;
    if (buf && cap > 0) {
        size_t start = (used > cap) ? used - cap : 0;
        while (start > 0 && start < used && ring_at(&G.rec, start - 1) != '\n') start++;
        for (size_t i = start; i < used; i++) buf[out++] = ring_at(&G.rec, i);
    } else {
        out = used;
    }
    ul(h);
    return out;
}

/* ---- issue marks ------------------------------------------------------------------------------------- */

int dlog_issue_mark(const DlogSnapshot *snap, int want_screenshot, DlogIssue *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return 0;

    /* make sure there is somewhere to write before the mark line names it */
    if (pump_acquire(1000)) {
        if (!G.fh && !G.mem_only) (void)open_next_file();
        pump_release();
    }
    int mem_only = __atomic_load_n(&G.mem_only, __ATOMIC_ACQUIRE);

    int h = lk();
    unsigned n = ++G.issues;
    char dir[DLOG_MAX_PATH];
    set_str(dir, sizeof(dir), G.dir_in_use);
    ul(h);
    int have_dir = !mem_only && dir[0];

    char logname[96], pngname[96];
    (void)dlog_issue_file_name(logname, sizeof(logname), G.stem, n, "log");
    (void)dlog_issue_file_name(pngname, sizeof(pngname), G.stem, n, "png");
    emit_forced((int)DLOG_GENERAL, (int)DLOG_ISSUE, 0, "issue.mark", "n=%u recorder=%s screenshot=%s", n,
                have_dir ? logname : "none", (have_dir && want_screenshot) ? pngname : "none");

    if (snap) {
        DlogSnapLine lines[8 + DLOG_SNAP_MAX_SESSIONS];
        size_t cnt = dlog_snapshot_format(snap, lines, sizeof(lines) / sizeof(lines[0]));
        for (size_t i = 0; i < cnt; i++) {
            va_list ap;
            memset(&ap, 0, sizeof(ap));
            emit_v((int)DLOG_GENERAL, (int)DLOG_ISSUE, lines[i].sid, lines[i].event, NULL, ap, lines[i].fields, 1);
        }
    }

    int ok = 0;
    char recpath[DLOG_MAX_PATH];
    recpath[0] = '\0';
    if (have_dir) {
        size_t rs = dlog_recorder_size();
        char header[1536];
        h = lk();
        set_str(header, sizeof(header), G.header);
        ul(h);
        size_t hl = strlen(header);
        char *blob = (char *)malloc(hl + rs + 16);
        if (blob) {
            memcpy(blob, header, hl);
            size_t got = dlog_recorder_copy(blob + hl, rs + 8);
            join_or_empty(recpath, sizeof(recpath), dir, logname);
            if (recpath[0] && G.p->file_write_all(recpath, blob, hl + got) == 0) ok = 1;
            free(blob);
        }
    }
    (void)dlog_flush_wait(500);

    if (out) {
        out->n = n;
        out->ok = ok;
        if (ok) set_str(out->recorder_path, sizeof(out->recorder_path), recpath);
        if (ok && want_screenshot) join_or_empty(out->screenshot_path, sizeof(out->screenshot_path), dir, pngname);
    }
    return 1;
}

/* ---- the system platform ------------------------------------------------------------------------------ */

int dlog_start(const DlogStartInfo *info)
{
    const DlogPlatform *p = dlog_platform_system();
    if (!p) return 0;
    int ok = dlog_start_with(p, info);
    if (ok) dlog_sys_install_crash_filter();
    return ok;
}
