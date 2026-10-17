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
    char     body[DLOG_LINE_MAX];       /* the repeat key (volatile values masked), not the line */
    unsigned since;          /* mono ms of the last line written for this run */
    long long last_ms;
    int      last_off;
    char     thread[8];
} Rep;

/* An issue mark's recorder dump, handed to the writer thread. The blob is the
 * header plus a snapshot of the recorder; the writer owns it while state is 1. */
#define JOB_SLOTS 2
typedef struct {
    int    state;            /* 0 free, 1 queued, 2 done ok, 3 done failed (guarded by the lock) */
    char  *blob;
    size_t len;
    char   name[96];
    char   dir[DLOG_MAX_PATH];      /* the folder the writer wrote it to */
} DumpJob;

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
    DumpJob jobs[JOB_SLOTS];

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
    size_t chunk_len, chunk_off;          /* a drained chunk not yet fully written */
    unsigned long long pending_dropped;   /* a dlog.dropped line still to write */

    /* shared status, written under the lock */
    char path[DLOG_MAX_PATH];
    char note[256];
    unsigned issues;
    unsigned long long folder_bytes;
    unsigned folder_files;
} G;

static _Thread_local char t_tag[8];

static char g_api_key[256];

/* A writer that did not stop within the join timeout still points into G, so G
 * and the rings are left alone (leaked on purpose) and a new start is refused
 * until that writer has really exited. */
static int g_leaked;
static int g_writer_exited;

/* ---- per-stream content state ------------------------------------------------------------ */

/* Terminal reads of one session arrive in arbitrary pieces. Per (session, stream)
 * we keep the OSC 52 state and the tail of the previous read that could be the
 * start of a known secret. Keyed by the exact session id, 16 entries per
 * thread, least recently used evicted. */
#define STREAM_SLOTS 16
#define SECRET_TAIL  256
typedef struct {
    unsigned     sid;
    int          stream;       /* 0 = free, 1 = term.rx, 2 = term.tx */
    unsigned     stamp;
    DlogEscState esc;
    size_t       held_len;
    char         held[SECRET_TAIL];
} StreamState;
static _Thread_local StreamState t_streams[STREAM_SLOTS];
static _Thread_local unsigned t_stream_clock;
static _Thread_local unsigned t_secret_epoch_seen;

/* A secret's registration ended: its prefix must not stay in memory (see dlog_internal.h). */
void dlog_streams_wipe_tails(void)
{
    for (int i = 0; i < STREAM_SLOTS; i++) {
        secure_zero(t_streams[i].held, sizeof(t_streams[i].held));
        t_streams[i].held_len = 0;
    }
    t_secret_epoch_seen = dlog_secret_epoch();
}

size_t dlog_streams_held_nonzero(void)
{
    size_t n = 0;
    for (int i = 0; i < STREAM_SLOTS; i++)
        for (size_t k = 0; k < sizeof(t_streams[i].held); k++)
            if (t_streams[i].held[k]) n++;
    return n;
}

static StreamState *stream_state(unsigned sid, int stream)
{
    if (t_secret_epoch_seen != dlog_secret_epoch())     /* another thread ended a secret */
        dlog_streams_wipe_tails();
    for (int i = 0; i < STREAM_SLOTS; i++) {
        StreamState *s = &t_streams[i];
        if (s->stream == stream && s->sid == sid) { s->stamp = ++t_stream_clock; return s; }
    }
    StreamState *victim = &t_streams[0];
    for (int i = 0; i < STREAM_SLOTS; i++) {
        StreamState *s = &t_streams[i];
        if (s->stream == 0) { victim = s; break; }
        if ((int)(s->stamp - victim->stamp) < 0) victim = s;
    }
    memset(victim, 0, sizeof(*victim));
    victim->sid = sid;
    victim->stream = stream;
    victim->stamp = ++t_stream_clock;
    dlog_esc_init(&victim->esc);
    return victim;
}

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

static void teardown(void);

static void wake_writer(void)
{
    const DlogPlatform *p = G.p;
    if (p && p->wake) p->wake();
}

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
    const DlogPlatform *pf = G.p;          /* read once: shutdown keeps it set, but never re-read it */
    if (!pf) return;
    if (cat < 0 || cat >= (int)DLOG_CAT_COUNT) return;
    int fmax = (int)__atomic_load_n(&g_file_max[cat], __ATOMIC_RELAXED);
    int cmax = (int)__atomic_load_n(&g_dlog_capture[cat], __ATOMIC_RELAXED);
    int to_file = force || lvl <= fmax;
    int to_rec = force || lvl <= cmax;
    if (to_file && __atomic_load_n(&G.mem_only, __ATOMIC_RELAXED)) to_file = 0;
    if (!to_file && !to_rec) return;

    char line[LINE_BUF];
    long long ms = pf->unix_ms();
    int off = pf->utc_offset_min();
    unsigned mono = pf->mono_ms();
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

    /* what "identical to the previous line" means: the body with volatile values masked */
    int collapsible = !force && lvl != (int)DLOG_ISSUE;
    char rkey[DLOG_LINE_MAX];
    size_t rkey_len = 0;
    if (collapsible) rkey_len = dlog_repeat_key(rkey, sizeof(rkey), line + pl, body_len);

    int wake = force ? 1 : 0;                 /* the logger's own lines must not wait for the next tick */
    int held = lk();
    if (G.started) {
        Rep *r = &G.rep[cat];
        if (collapsible && r->valid && r->sid == sid && r->lvl == lvl && r->body_len == rkey_len &&
            memcmp(r->body, rkey, rkey_len) == 0) {
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
                r->body_len = rkey_len;
                memcpy(r->body, rkey, rkey_len);
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

    /* A stream (terminal reads) keeps its OSC 52 state and the tail of a possible secret between
     * calls, per exact session id; anything else is judged on its own. */
    int stream = 0;
    if (event && strcmp(event, "term.rx") == 0) stream = 1;
    else if (event && strcmp(event, "term.tx") == 0) stream = 2;
    StreamState fresh;
    StreamState *ss = &fresh;
    if (stream) {
        ss = stream_state(sid, stream);
    } else {
        memset(&fresh, 0, sizeof(fresh));
        dlog_esc_init(&fresh.esc);
    }

    /* Every byte goes through the secret scrub and the OSC 52 state machine; only what fits is
     * written (about 4 KB of escaped text), the rest is counted in len= and marked truncated=1. */
    enum { SLICE = 1024 };
    const unsigned char *u = (const unsigned char *)data;
    char in[SLICE + SECRET_TAIL];
    char scrubbed[(SLICE + SECRET_TAIL) * 4 + 8];       /* a 3-byte secret becomes 10 bytes */
    char esc[4000];
    size_t used = 0;
    int truncated = 0;
    esc[0] = '\0';
    size_t pos = 0;
    while (pos < len) {
        size_t take = len - pos;
        if (take > SLICE) take = SLICE;
        int last = (pos + take >= len);
        if (!last) {            /* do not cut a UTF-8 sequence between slices */
            size_t back = 0;
            while (back < 3 && back < take && (u[pos + take - 1 - back] & 0xC0u) == 0x80u) back++;
            if (back < take) {
                unsigned char lead = u[pos + take - 1 - back];
                size_t need = lead >= 0xF0u ? 4u : lead >= 0xE0u ? 3u : lead >= 0xC0u ? 2u : 0u;
                if (need > back + 1) take -= back + 1;
            }
        }
        size_t hl = ss->held_len;
        memcpy(in, ss->held, hl);
        memcpy(in + hl, u + pos, take);
        size_t total = hl + take;
        size_t consumed = 0;
        size_t sl = dlog_scrub_stream(scrubbed, sizeof(scrubbed), in, total, (last && !stream) ? 1 : 0, &consumed);
        size_t carry = total - consumed;
        if (carry > SECRET_TAIL - 1) carry = SECRET_TAIL - 1;
        memcpy(ss->held, in + consumed, carry);
        ss->held_len = carry;
        size_t got = 0;
        used += dlog_escape_bytes(&ss->esc, scrubbed, sl, esc + used, sizeof(esc) - used, &got);
        if (got < sl) truncated = 1;
        pos += take;
    }
    char k[48];
    set_str(k, sizeof(k), (key && key[0]) ? key : "data");
    for (char *c = k; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_')) *c = '_';
    char fields[4200];
    (void)snprintf(fields, sizeof(fields), "%s=\"%s\" len=%zu%s", k, esc, len, truncated ? " truncated=1" : "");
    va_list ap;
    memset(&ap, 0, sizeof(ap));
    emit_v((int)cat, (int)lvl, sid, event, NULL, ap, fields, 0);
}

/* ---- the writer's side: files ------------------------------------------------------------------ */

/* Take the pump, waiting at most timeout_ms of REAL time. The deadline is the platform's
 * monotonic clock: counting Sleep(1) calls is not one (each can take ~15 ms). The spin
 * count caps it too, so a platform whose clock stands still cannot hang the caller. */
static int pump_acquire(unsigned timeout_ms)
{
    unsigned start = G.p->mono_ms();
    unsigned spins = 0;
    for (;;) {
        if (!__atomic_exchange_n(&G.pump_busy, 1, __ATOMIC_ACQUIRE)) return 1;
        if ((unsigned)(G.p->mono_ms() - start) >= timeout_ms || spins >= timeout_ms) return 0;
        if (G.p->sleep_ms) G.p->sleep_ms(1);
        spins++;
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

/* dir_in_use is written only by the pump but read by other threads under the lock: write it there too. */
static void set_dir_in_use(const char *dir)
{
    int h = lk();
    set_str(G.dir_in_use, sizeof(G.dir_in_use), dir);
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
    if (G.crashing) return;                     /* allocates and lists: never inside the crash filter */
    if (!G.dir_in_use[0] || !G.p->list_dir || !G.p->remove_file) return;
    unsigned long long total_cap = __atomic_load_n(&G.total_cap, __ATOMIC_RELAXED);
    enum { MAXF = 600 };
    DlogFileInfo *files = (DlogFileInfo *)malloc(sizeof(DlogFileInfo) * MAXF);
    unsigned char *state = (unsigned char *)calloc(MAXF, 1);
    if (!files || !state) { free(files); free(state); return; }
    int n = G.p->list_dir(G.dir_in_use, files, MAXF);
    if (n > 0) {
        int i;
        int guard = 0;
        while ((i = dlog_retention_next(files, (size_t)n, G.cur_name, total_cap, state)) >= 0 && guard++ < MAXF) {
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
    if (G.crashing) return 0;                   /* never creates folders inside the crash filter */
    if (G.have_retry && (int)(G.p->mono_ms() - G.retry_at) < 0) return 0;
    G.have_retry = 0;

    char resolved[DLOG_MAX_PATH];               /* written by dlog_apply under the lock: copy it there */
    {
        int h0 = lk();
        set_str(resolved, sizeof(resolved), G.dir_resolved);
        ul(h0);
    }
    char cand[2][DLOG_MAX_PATH];
    int nc = 0;
    if (!G.using_fallback && resolved[0]) set_str(cand[nc++], DLOG_MAX_PATH, resolved);
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
        set_dir_in_use(cand[i]);
        if (resolved[0] && strcmp(cand[i], resolved) != 0) G.using_fallback = 1;
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
                        resolved, first_err, cand[i]);
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

/* Lines that must be given up (no folder is writable): counted, never silent. */
static void count_lost(const char *buf, size_t n)
{
    unsigned long long lines = 0;
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '\n') lines++;
    if (!lines) return;
    int h = lk();
    G.dropped_total += lines;
    ul(h);
}

/* Nothing more can be written now. Memory-only: the rest is lost for good (counted) and
 * counts as consumed. Otherwise (a reopen is pending): not consumed, the caller keeps it. */
static size_t give_up(const char *buf, size_t n, size_t off)
{
    if (__atomic_load_n(&G.mem_only, __ATOMIC_ACQUIRE)) {
        count_lost(buf + off, n - off);
        return n;
    }
    return off;
}

/* The crash path: one plain open of the file the run was writing, no folder creation, no
 * retention, no listing, no allocation -- the filter may run while another thread holds the
 * heap lock. Pump-private fields are read without the lock for the same reason. */
static int crash_open(void)
{
    const char *dir = G.dir_in_use[0] ? G.dir_in_use : G.dir_resolved;
    if (!dir[0]) return 0;
    char name[96];
    if (G.cur_name[0]) set_str(name, sizeof(name), G.cur_name);
    else (void)dlog_file_name(name, sizeof(name), G.stem, G.part + 1);
    char path[DLOG_MAX_PATH];
    if (!dlog_join(path, sizeof(path), dir, name)) return 0;
    void *h = NULL;
    unsigned long long size = 0;
    if (G.p->file_open(path, &h, &size) != 0) return 0;
    G.fh = h;
    G.cur_size = size;
    return 1;
}

/* Writes whole lines of buf (n bytes). Returns the bytes consumed, always on a line boundary:
 * less than n means the rest could not be written yet and must be offered again. */
static size_t write_bytes(const char *buf, size_t n)
{
    if (__atomic_load_n(&G.mem_only, __ATOMIC_ACQUIRE)) { count_lost(buf, n); return n; }
    if (G.crashing) {
        if (!G.fh && !crash_open()) return 0;
        return G.p->file_write(G.fh, buf, n) == 0 ? n : 0;
    }
    if (!G.fh && !open_next_file()) return give_up(buf, n, 0);
    size_t off = 0;
    int forced_one = 0;
    while (off < n) {
        size_t batch = 0;
        while (off + batch < n) {
            const char *start = buf + off + batch;
            const char *nl = memchr(start, '\n', n - off - batch);
            size_t ll = nl ? (size_t)(nl - start) + 1u : n - off - batch;
            int rot = (batch == 0) ? dlog_should_rotate(G.cur_size, ll, __atomic_load_n(&G.file_cap, __ATOMIC_RELAXED))
                                   : (G.cur_size + batch + ll > __atomic_load_n(&G.file_cap, __ATOMIC_RELAXED));
            if (rot && !(batch == 0 && forced_one)) break;
            batch += ll;
            if (forced_one) break;           /* just the one line into a fresh file */
        }
        if (batch == 0) {
            rotate_file();
            if (!open_next_file()) return give_up(buf, n, off);
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
            return off;                      /* the failed batch stays queued for the reopened file */
        }
        G.cur_size += batch;
        off += batch;
    }
    return n;
}

/* 1 when the line is written (or pointless because nothing is written any more). */
static int write_dropped_line(unsigned long long n)
{
    if (__atomic_load_n(&G.mem_only, __ATOMIC_ACQUIRE)) return 1;
    char line[LINE_BUF], f[48];
    (void)snprintf(f, sizeof(f), "n=%llu", n);
    size_t l = dlog_format_line(line, sizeof(line), G.p->unix_ms(), G.p->utc_offset_min(), (int)DLOG_WARN,
                                (int)DLOG_GENERAL, 0, "log", "dlog.dropped", f);
    if (!l) return 1;
    return write_bytes(line, l) == l;
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
    set_dir_in_use("");
    if (G.mem_only) {
        G.mem_only = 0;
        __atomic_store_n(&G.mem_only, 0, __ATOMIC_RELEASE);
    }
    set_note("");
}

/* Issue-mark dumps handed over by dlog_issue_mark(): open the folder if need be (retention and
 * FORMAT.txt included), write <stem>.issue<n>.log, publish the result. Pump-private. */
static void run_jobs(void)
{
    for (int i = 0; i < JOB_SLOTS; i++) {
        int h = lk();
        DumpJob *j = &G.jobs[i];
        int queued = (j->state == 1);
        char *blob = j->blob;
        size_t len = j->len;
        char name[96];
        set_str(name, sizeof(name), j->name);
        ul(h);
        if (!queued) continue;
        if (!G.fh && !__atomic_load_n(&G.mem_only, __ATOMIC_ACQUIRE)) (void)open_next_file();
        int mem_only = __atomic_load_n(&G.mem_only, __ATOMIC_ACQUIRE);
        if (!mem_only && !G.dir_in_use[0]) continue;       /* a reopen is pending: stay queued */
        int ok = 0;
        char dir[DLOG_MAX_PATH];
        dir[0] = '\0';
        if (!mem_only) {
            char path[DLOG_MAX_PATH];
            set_str(dir, sizeof(dir), G.dir_in_use);
            join_or_empty(path, sizeof(path), dir, name);
            if (path[0] && G.p->file_write_all(path, blob, len) == 0) ok = 1;
        }
        free(blob);
        if (!ok)        /* the issue.mark line named the file before the writer knew it could not be made */
            emit_forced((int)DLOG_GENERAL, (int)DLOG_WARN, 0, "issue.dump_failed", "name=%s reason=%s", name,
                        mem_only ? "no writable log folder" : "write failed");
        h = lk();
        j->blob = NULL;
        j->len = 0;
        set_str(j->dir, sizeof(j->dir), dir);
        j->state = ok ? 2 : 3;
        ul(h);
    }
}

static void do_pump(void)
{
    dlog_tick();
    if (!G.crashing) handle_dir_change();
    for (;;) {
        if (G.chunk_off >= G.chunk_len) {
            G.chunk_len = G.chunk_off = 0;
            int h = lk();
            size_t n = ring_take_lines(&G.q, G.chunk, CHUNK_BYTES);
            G.pending_dropped += G.dropped;
            G.dropped = 0;
            ul(h);
            G.chunk_len = n;
            if (n == 0 && G.pending_dropped == 0) break;
        }
        if (G.chunk_off < G.chunk_len) {
            size_t w = write_bytes(G.chunk + G.chunk_off, G.chunk_len - G.chunk_off);
            G.chunk_off += w;
            __atomic_add_fetch(&G.q_out, (unsigned long long)w, __ATOMIC_RELEASE);
            if (G.chunk_off < G.chunk_len) break;           /* kept as is; offered again by the next pump */
        }
        /* the dropped notice goes out after the lines that were queued before it */
        if (G.pending_dropped) {
            if (!write_dropped_line(G.pending_dropped)) break;
            G.pending_dropped = 0;
        }
    }
    if (!G.crashing) run_jobs();
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

/* True when the writer has nothing to do until somebody wakes it: debugging is off and nothing is
 * queued, pending or waiting for a tick. Every path that creates work wakes the writer. */
static int writer_idle(void)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return 0;
    if (__atomic_load_n(&g_enabled, __ATOMIC_RELAXED)) return 0;
    int h = lk();
    int idle = G.q.used == 0 && G.chunk_off >= G.chunk_len && G.pending_dropped == 0 && !G.dir_changed;
    for (int c = 0; idle && c < (int)DLOG_CAT_COUNT; c++)
        if (G.rep[c].valid && G.rep[c].count) idle = 0;
    for (int i = 0; idle && i < JOB_SLOTS; i++)
        if (G.jobs[i].state == 1) idle = 0;
    ul(h);
    return idle;
}

unsigned dlog_writer_wait_ms(void) { return writer_idle() ? DLOG_WAIT_FOREVER : 250u; }

static void writer_main(void *arg)
{
    (void)arg;
    dlog_set_thread_tag("log");
    while (!__atomic_load_n(&G.stop, __ATOMIC_ACQUIRE)) {
        unsigned ms = dlog_writer_wait_ms();
        if (G.p->wait) G.p->wait(ms);
        else if (G.p->sleep_ms) G.p->sleep_ms(250);
        (void)dlog_pump();
    }
    __atomic_store_n(&g_writer_exited, 1, __ATOMIC_RELEASE);
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
    __atomic_store_n(&G.file_cap, dlog_file_cap_bytes(c.enabled, c.max_file_mb), __ATOMIC_RELAXED);
    __atomic_store_n(&G.total_cap, dlog_total_cap_bytes(c.max_total_mb), __ATOMIC_RELAXED);
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
    wake_writer();          /* an idle writer must notice a changed folder or level */
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
    if (key[0] && strcmp(g_api_key, key) != 0 && dlog_secret_add(key)) set_str(g_api_key, sizeof(g_api_key), key);
}

/* ---- lifecycle ------------------------------------------------------------------------------------ */

int dlog_start_with(const DlogPlatform *p, const DlogStartInfo *info)
{
    if (!p || !p->lock || !p->unlock || !p->unix_ms || !p->utc_offset_min || !p->mono_ms ||
        !p->mkdir_p || !p->file_open || !p->file_write || !p->file_close || !p->file_write_all ||
        !p->list_dir || !p->remove_file)
        return 0;
    if (__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return 0;
    if (g_leaked) {                      /* a previous shutdown left its writer running */
        if (!__atomic_load_n(&g_writer_exited, __ATOMIC_ACQUIRE)) return 0;
        teardown();
        g_leaked = 0;
    }

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
    __atomic_store_n(&g_writer_exited, 0, __ATOMIC_RELEASE);
    if (p->thread_start && p->thread_start(writer_main, NULL)) G.threaded = 1;
    return 1;
}

/* Release everything the running logger owns. The platform pointer stays: late emitters read it. */
static void teardown(void)
{
    const DlogPlatform *p = G.p;
    close_file();
    for (int i = 0; i < JOB_SLOTS; i++) free(G.jobs[i].blob);
    ring_free(&G.q);
    ring_free(&G.rec);
    free(G.chunk);
    memset(&G, 0, sizeof(G));
    G.p = p;
}

void dlog_shutdown(void)
{
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return;
    int joined = 1;
    if (G.threaded) {
        __atomic_store_n(&G.stop, 1, __ATOMIC_RELEASE);
        wake_writer();
        if (G.p->thread_join) joined = G.p->thread_join() ? 1 : 0;
        G.threaded = 0;
    }
    if (!joined) {
        /* The writer is still running (stuck in an I/O call). It points into G and the rings, so
         * they are leaked on purpose; the logger is marked stopped and a restart waits for the
         * writer to exit. */
        __atomic_store_n(&G.started, 0, __ATOMIC_RELEASE);
        g_leaked = 1;
        for (int i = 0; i < (int)DLOG_CAT_COUNT; i++) {
            g_dlog_capture[i] = 0;
            g_file_max[i] = 0;
        }
        g_content = g_screenshot = g_enabled = 0;
        dlog_secret_clear();
        secure_zero(g_api_key, sizeof(g_api_key));
        return;
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
    teardown();
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
    st->file_open = __atomic_load_n(&G.file_open, __ATOMIC_RELAXED);
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

/* Snapshot header + recorder and hand them to the writer as a dump job. Returns the job slot, or
 * -1 (no memory / both slots still busy). Cheap: one allocation and one copy, no I/O. */
static int queue_dump(const char *logname)
{
    char header[1536];
    int h = lk();
    set_str(header, sizeof(header), G.header);
    ul(h);
    size_t hl = strlen(header);
    size_t rs = dlog_recorder_size();
    char *blob = (char *)malloc(hl + rs + 16);
    if (!blob) return -1;
    memcpy(blob, header, hl);
    size_t got = dlog_recorder_copy(blob + hl, rs + 8);
    int slot = -1;
    h = lk();
    for (int i = 0; i < JOB_SLOTS; i++) {
        if (G.jobs[i].state == 1) continue;
        DumpJob *j = &G.jobs[i];
        j->blob = blob;
        j->len = hl + got;
        set_str(j->name, sizeof(j->name), logname);
        j->dir[0] = '\0';
        j->state = 1;
        slot = i;
        break;
    }
    ul(h);
    if (slot < 0) free(blob);
    return slot;
}

/* Wait until the writer has drained what was queued before `target` and finished the dump, at
 * most `ms` of real time (a clock that stands still is capped by the spin count). */
static void wait_issue(int slot, unsigned long long target, unsigned ms)
{
    if (!G.threaded) { (void)dlog_pump(); return; }
    unsigned start = G.p->mono_ms();
    unsigned spins = 0;
    for (;;) {
        int done = __atomic_load_n(&G.q_out, __ATOMIC_ACQUIRE) >= target;
        if (done && slot >= 0) {
            int h = lk();
            done = G.jobs[slot].state != 1;
            ul(h);
        }
        if (done) return;
        if ((unsigned)(G.p->mono_ms() - start) >= ms || spins >= ms) return;
        wake_writer();
        if (G.p->sleep_ms) G.p->sleep_ms(1);
        spins++;
    }
}

int dlog_issue_mark(const DlogSnapshot *snap, int want_screenshot, DlogIssue *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!__atomic_load_n(&G.started, __ATOMIC_ACQUIRE)) return 0;

    int mem_only = __atomic_load_n(&G.mem_only, __ATOMIC_ACQUIRE);
    int h = lk();
    unsigned n = ++G.issues;
    char dir[DLOG_MAX_PATH];                    /* where the dump is expected; the writer has the last word */
    set_str(dir, sizeof(dir), G.dir_in_use[0] ? G.dir_in_use : G.dir_resolved);
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

    /* the dump is taken now (it ends with the lines above) and written by the writer thread */
    int slot = have_dir ? queue_dump(logname) : -1;
    const int dump_failed = (have_dir && slot < 0);
    unsigned long long target = __atomic_load_n(&G.q_in, __ATOMIC_ACQUIRE);
    wake_writer();
    wait_issue(slot, target, 500);

    int state = 0;
    if (slot >= 0) {
        h = lk();
        state = G.jobs[slot].state;
        if (state == 2) set_str(dir, sizeof(dir), G.jobs[slot].dir);
        ul(h);
    }
    int ok = (slot >= 0 && state != 3);
    if (dump_failed)       /* never silent: both dump job slots were still busy (or no memory) */
        emit_forced((int)DLOG_GENERAL, (int)DLOG_WARN, 0, "issue.dump_failed", "n=%u reason=\"writer busy\"", n);
    if (out) {
        out->n = n;
        out->ok = ok;
        out->dump_failed = dump_failed;
        out->pending = (ok && state == 1) ? 1 : 0;
        if (ok) join_or_empty(out->recorder_path, sizeof(out->recorder_path), dir, logname);
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
