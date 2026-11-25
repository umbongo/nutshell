/* voice_live_win.c -- the live transcription session. See the header. */

#include <winsock2.h>
#include "voice_live_win.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai_netcancel.h"
#include "json_scan.h"
#include "dlog.h"
#include "secure_zero.h"
#include "voice_gate.h"
#include "voice_ledger.h"
#include "voice_ptt.h"
#include "voice_resample.h"
#include "voice_sgate.h"
#include "voice_ws_win.h"
#include "wav.h"

#define VL_MAX_SESS        8
#define VL_MAX_CHUNK       2400u                       /* 100 ms at 24 kHz */
#define VL_QUEUE_MAX       (24000u * 180u)             /* samples queued ahead of the sender */
#define VL_READY_TIMEOUT_MS 10000u                     /* session start to session.updated */
#define VL_ABORT_AFTER_MS   5000u                      /* closed this long ago and still running: abort */
#define VL_DETACH_AFTER_MS  5000u                      /* aborted this long ago and still running: detach */
#define VL_UNKNOWN_LOG_MAX  20u                        /* unknown event types logged per session */

typedef enum { QI_AUDIO = 1, QI_GATE, QI_END } QKind;
typedef struct { QKind kind; int16_t *s; size_t n; } QItem;

typedef struct LiveSess {
    volatile LONG    refs;          /* the owner, the receiver and the sender */
    unsigned         token;
    HWND             notify;
    VoiceLiveTarget  t;
    char            *key;           /* the receiver's: wiped after the handshake */
    AiNetCancel      nc;
    VoiceWs *volatile ws;           /* published by the receiver after the upgrade */
    HANDLE           th_recv, th_send;      /* the owner's, closed at reap */
    HANDLE           ev_feed;       /* auto: the queue has something */
    HANDLE           ev_cancel;     /* manual: voice_live_close */
    HANDLE           ev_session;    /* manual: session_msg is ready */
    HANDLE           ev_ready;      /* manual: session.updated arrived */
    HANDLE           ev_over;       /* manual: the session ended (final, closed, failed) */
    CRITICAL_SECTION cs;            /* the feed: queue, gate, resampler */
    CRITICAL_SECTION post_cs;       /* cancel flag and PostMessage are one step */
    QItem           *q;
    size_t           qn, qcap, qsamples;
    VoiceSgate       gate;
    VoiceResampler   rs;
    int              end_queued;    /* under cs */
    int              overflowed;    /* under cs: the queue filled; post_end runs once after unlock */
    char            *session_msg;   /* under cs */
    size_t           session_len;
    volatile LONG    cancel;        /* under post_cs when set */
    volatile LONG    ended;         /* a final or a closed message was posted */
    volatile LONG    end_sent;
    volatile LONG    commits_sent;  /* OpenAI commits the sender sent */
    volatile LONG    finals;        /* OpenAI completed + failed events posted */
    int              closed;        /* UI thread: voice_live_close was called */
    ULONGLONG        started_at;    /* GetTickCount64 at voice_live_start */
    ULONGLONG        closed_at;     /* UI thread: when voice_live_close ran */
    ULONGLONG        aborted_at;    /* UI thread: when reap aborted it (0 = not yet) */
    int              detached;      /* UI thread: reap gave up joining; the threads free it */
    volatile LONG    exited;        /* threads that posted their exit wake-up */
} LiveSess;

static INIT_ONCE        g_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_cs;       /* guards g_cur against the audio thread */
static LiveSess        *g_cur;      /* the session the audio thread feeds */
static LiveSess        *g_list[VL_MAX_SESS];   /* UI thread */
static volatile LONG    g_detached;            /* detached sessions whose threads still run */

static BOOL CALLBACK init_once_cb(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    return TRUE;
}

static void init_globals(void)
{
    InitOnceExecuteOnce(&g_once, init_once_cb, NULL, NULL);
}

static int tok_eq(unsigned a, unsigned b)
{
    return (a & VOICE_TOKEN_MASK) == (b & VOICE_TOKEN_MASK);
}

void voice_live_msg_free(VoiceLiveMsg *m)
{
    if (!m) return;
    voice_live_event_free(&m->ev);
    free(m);
}

/* ---- the session's memory ------------------------------------------------ */

static void q_clear(LiveSess *s)
{
    for (size_t i = 0; i < s->qn; i++) {
        if (s->q[i].s) { secure_zero(s->q[i].s, s->q[i].n * sizeof(int16_t)); free(s->q[i].s); }
    }
    s->qn = 0;
    s->qsamples = 0;
}

static void sess_free(LiveSess *s)
{
    if (s->detached) InterlockedDecrement(&g_detached);
    voice_ws_free(s->ws);
    if (s->key) { secure_zero(s->key, strlen(s->key)); free(s->key); }
    if (s->session_msg) { secure_zero(s->session_msg, s->session_len); free(s->session_msg); }
    q_clear(s);
    free(s->q);
    voice_sgate_reset(&s->gate);
    secure_zero(&s->rs, sizeof(s->rs));
    DeleteCriticalSection(&s->cs);
    DeleteCriticalSection(&s->post_cs);
    if (s->ev_feed) CloseHandle(s->ev_feed);
    if (s->ev_cancel) CloseHandle(s->ev_cancel);
    if (s->ev_session) CloseHandle(s->ev_session);
    if (s->ev_ready) CloseHandle(s->ev_ready);
    if (s->ev_over) CloseHandle(s->ev_over);
    free(s);
}

static void sess_unref(LiveSess *s)
{
    if (InterlockedDecrement(&s->refs) == 0) sess_free(s);
}

/* ---- the feed (audio thread, under s->cs) --------------------------------- */

static int q_push(LiveSess *s, QKind kind, int16_t *buf, size_t n)
{
    if (s->qn >= s->qcap) {
        size_t nc = s->qcap ? s->qcap * 2 : 64;
        QItem *nq = (QItem *)realloc(s->q, nc * sizeof(QItem));
        if (!nq) return 0;
        s->q = nq;
        s->qcap = nc;
    }
    s->q[s->qn].kind = kind;
    s->q[s->qn].s = buf;
    s->q[s->qn].n = n;
    s->qn++;
    s->qsamples += n;
    return 1;
}

static void sink_audio(void *u, const int16_t *x, size_t n)
{
    LiveSess *s = (LiveSess *)u;
    if (n == 0) return;
    if (s->qsamples + n > VL_QUEUE_MAX) {    /* the sender is not keeping up: the link failed */
        s->overflowed = 1;                   /* never a silent drop: the recording falls back to batch */
        return;
    }
    size_t cap = s->t.rate == 24000u ? voice_rs_out_max(n) : n;
    int16_t *out = (int16_t *)malloc(cap * sizeof(int16_t));
    if (!out) return;
    size_t m;
    if (s->t.rate == 24000u) m = voice_rs_16_to_24(&s->rs, x, n, out, cap);
    else { memcpy(out, x, n * sizeof(int16_t)); m = n; }
    if (m == 0 || !q_push(s, QI_AUDIO, out, m)) {
        secure_zero(out, cap * sizeof(int16_t));
        free(out);
    }
}

static void sink_closed(void *u)
{
    LiveSess *s = (LiveSess *)u;
    q_push(s, QI_GATE, NULL, 0);
    voice_rs_init(&s->rs);       /* the next utterance starts from clean filter history */
}

static void fail_overflow(LiveSess *s);

/* The current session if `token` is its, with g_cs held; NULL (g_cs released) otherwise. */
static LiveSess *feed_lock(unsigned token)
{
    init_globals();
    EnterCriticalSection(&g_cs);
    LiveSess *s = g_cur;
    if (s && tok_eq(s->token, token) && !s->ended) return s;
    LeaveCriticalSection(&g_cs);
    return NULL;
}

void voice_live_feed(unsigned token, const int16_t *x, size_t n)
{
    if (!x || n == 0) return;
    LiveSess *s = feed_lock(token);
    if (!s) return;
    VoiceSgateSink k = { sink_audio, sink_closed, s };
    EnterCriticalSection(&s->cs);
    if (!s->end_queued) voice_sgate_feed(&s->gate, x, n, &k);
    int any = s->qn > 0;
    int over = s->overflowed;
    LeaveCriticalSection(&s->cs);
    if (over) fail_overflow(s);
    if (any) SetEvent(s->ev_feed);
    LeaveCriticalSection(&g_cs);
}

void voice_live_feed_end(unsigned token)
{
    LiveSess *s = feed_lock(token);
    if (!s) return;
    VoiceSgateSink k = { sink_audio, sink_closed, s };
    EnterCriticalSection(&s->cs);
    if (!s->end_queued) {
        voice_sgate_flush(&s->gate, &k);
        s->end_queued = q_push(s, QI_END, NULL, 0);
    }
    int over = s->overflowed;
    LeaveCriticalSection(&s->cs);
    if (over) fail_overflow(s);
    SetEvent(s->ev_feed);
    LeaveCriticalSection(&g_cs);
}

size_t voice_live_test_queued(void)
{
    init_globals();
    size_t r = 0;
    EnterCriticalSection(&g_cs);
    LiveSess *s = g_cur;
    if (s) {
        EnterCriticalSection(&s->cs);
        r = s->qsamples + s->qn;
        LeaveCriticalSection(&s->cs);
    }
    LeaveCriticalSection(&g_cs);
    return r;
}

/* ---- posting to the UI ----------------------------------------------------- */

/* Posts `*ev` (ownership moves) unless the session was closed; the flag and the
 * post are one step against voice_live_close. */
static void post_event(LiveSess *s, VoiceLiveEvent *ev, int http_status)
{
    VoiceLiveMsg *m = (VoiceLiveMsg *)calloc(1, sizeof(*m));
    int posted = 0;
    if (m) {
        m->token = s->token;
        m->http_status = http_status;
        m->ev = *ev;
        memset(ev, 0, sizeof(*ev));
        EnterCriticalSection(&s->post_cs);
        if (!s->cancel && s->notify)
            posted = PostMessage(s->notify, WM_VOICE_LIVE, 0, (LPARAM)m) ? 1 : 0;
        LeaveCriticalSection(&s->post_cs);
        if (!posted) voice_live_msg_free(m);
    } else {
        voice_live_event_free(ev);
    }
}

/* The session's one ending message: a final (VLE_DONE, text moved from *text) or VLE_CLOSED.
 * Only the first ending posts; it wakes the sender. */
static void post_end(LiveSess *s, VoiceLiveEvKind kind, char **text, const char *item,
                     int http_status)
{
    if (InterlockedCompareExchange(&s->ended, 1, 0) == 0) {
        VoiceLiveEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.kind = kind;
        if (item) snprintf(ev.item, sizeof(ev.item), "%s", item);
        if (text && *text) { ev.text = *text; *text = NULL; }
        post_event(s, &ev, http_status);
        DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_end", "kind=%d", (int)kind);
    }
    SetEvent(s->ev_over);
}

/* The feed queue overflowed (3 minutes unsent): the link failed, once. The sender stops
 * (ev_over) and the glue falls the recording back to batch. */
static void fail_overflow(LiveSess *s)
{
    DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_overflow", "queue full");
    post_end(s, VLE_CLOSED, NULL, NULL, 0);
}

/* A thread's last act: a NULL-payload wake-up so the UI thread reaps the finished session
 * now, not at the next recording. Posted even after voice_live_close. */
static void post_wake(LiveSess *s)
{
    InterlockedIncrement(&s->exited);
    if (s->notify) PostMessage(s->notify, WM_VOICE_LIVE, 0, 0);
}

/* OpenAI: final once the end marker went out and every commit completed or failed. */
static void try_final(LiveSess *s)
{
    if (s->t.prov != VLP_OPENAI || !s->end_sent) return;
    if (s->finals < s->commits_sent) return;
    post_end(s, VLE_DONE, NULL, NULL, 0);
}

/* ---- the receiver ----------------------------------------------------------- */

static void wipe_free(char *m, size_t n)
{
    if (m) { secure_zero(m, n); free(m); }
}

/* An event the parser ignored or rejected: its type only (never content), the first
 * VL_UNKNOWN_LOG_MAX per session. */
static void log_unknown(const char *m, size_t n, int parsed, unsigned *count)
{
    char *ty = NULL;
    size_t tl = 0;
    char safe[48];
    size_t k = 0;
    if (*count >= VL_UNKNOWN_LOG_MAX) return;
    (*count)++;
    if (json_scan_string(m, n, 1, "type", 64, &ty, &tl) && ty) {
        for (size_t i = 0; i < tl && k + 1 < sizeof(safe); i++) {
            char c = ty[i];
            int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '.' || c == '_' || c == '-';
            safe[k++] = ok ? c : '?';
        }
        secure_zero(ty, tl);
        free(ty);
    }
    safe[k] = '\0';
    DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_unknown", "type=%s parsed=%d", k ? safe : "-", parsed);
}

static DWORD WINAPI recv_thread(LPVOID p)
{
    LiveSess *s = (LiveSess *)p;
    dlog_set_thread_tag("live");
    char err[128] = "";
    int st = 0;
    VoiceWs *ws = NULL;

    size_t hl = strlen(s->key) + 32;
    char *hdr = (char *)malloc(hl);
    if (hdr) {
        snprintf(hdr, hl, "Authorization: Bearer %s", s->key);
        const char *hdrs[2] = { hdr, NULL };
        ws = voice_ws_connect(s->t.host, s->t.port, s->t.secure, s->t.path, hdrs, &s->nc,
                              &st, err, sizeof(err));
        secure_zero(hdr, hl);
        free(hdr);
    } else {
        snprintf(err, sizeof(err), "out of memory");
    }
    secure_zero(s->key, strlen(s->key));          /* the key is not needed again */
    free(s->key);
    s->key = NULL;

    if (!ws) {
        DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_connect_failed", "status=%d", st);
        char *why = _strdup(err);
        post_end(s, VLE_CLOSED, &why, NULL, st);
        free(why);
        goto out;
    }
    InterlockedExchangePointer((PVOID volatile *)&s->ws, ws);
    if (s->cancel) voice_ws_abort(ws);             /* closed while connecting */

    int created = 0, ready = 0;
    char seen[64][64];                  /* item ids already counted (a ring): only this thread */
    unsigned nseen = 0, nunknown = 0;
    memset(seen, 0, sizeof(seen));
    for (;;) {
        char *m = NULL;
        size_t n = 0;
        int cl = 0;
        if (voice_ws_recv(ws, &m, &n, &cl) != 0 || cl) {
            wipe_free(m, n);
            DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_recv_end", "close=%d", cl);
            break;
        }
        if (s->cancel || s->ended) { wipe_free(m, n); break; }
        VoiceLiveEvent ev;
        int ok = voice_live_parse(s->t.prov, m, n, &ev);
        if (!ok || ev.kind == VLE_IGNORE) {
            log_unknown(m, n, ok, &nunknown);
            wipe_free(m, n);
            voice_live_event_free(&ev);
            continue;
        }
        wipe_free(m, n);
        DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_event", "kind=%d", (int)ev.kind);

        switch (ev.kind) {
        case VLE_CREATED:
            if (!created) {
                size_t len = 0;
                char *msg = voice_live_msg_session(&s->t, &len);
                created = 1;
                if (!msg) {
                    char *why = _strdup("could not build the session message");
                    post_end(s, VLE_CLOSED, &why, NULL, 0);
                    free(why);
                    voice_live_event_free(&ev);
                    goto done;
                }
                EnterCriticalSection(&s->cs);
                s->session_msg = msg;
                s->session_len = len;
                LeaveCriticalSection(&s->cs);
                SetEvent(s->ev_session);
            }
            voice_live_event_free(&ev);
            break;
        case VLE_READY:
            if (!ready) {
                ready = 1;
                post_event(s, &ev, 0);
                SetEvent(s->ev_ready);
            }
            voice_live_event_free(&ev);
            break;
        case VLE_ERROR:        /* not fatal: only the socket closing is a link failure */
            post_event(s, &ev, 0);
            voice_live_event_free(&ev);
            break;
        case VLE_DONE:
            if (InterlockedCompareExchange(&s->ended, 1, 0) == 0) post_event(s, &ev, 0);
            SetEvent(s->ev_over);
            voice_live_event_free(&ev);
            goto done;
        case VLE_COMPLETED:
        case VLE_ITEM_FAILED: {
            char item[sizeof(ev.item)];
            snprintf(item, sizeof(item), "%s", ev.item);     /* post_event moves ev */
            post_event(s, &ev, 0);
            {   /* a duplicate or spurious completion never counts twice */
                int dup = 0;
                for (unsigned k = 0; k < nseen && k < 64; k++)
                    if (strcmp(seen[k], item) == 0) { dup = 1; break; }
                if (!dup) {
                    snprintf(seen[nseen % 64], sizeof(seen[0]), "%s", item);
                    nseen++;
                    InterlockedIncrement(&s->finals);   /* after the post: the final comes last */
                    try_final(s);
                }
            }
            voice_live_event_free(&ev);
            break;
        }
        default:
            post_event(s, &ev, 0);
            voice_live_event_free(&ev);
            break;
        }
        if (s->ended) break;
    }
done:
    if (!s->ended) post_end(s, VLE_CLOSED, NULL, NULL, 0);
out:
    SetEvent(s->ev_over);
    post_wake(s);
    sess_unref(s);
    return 0;
}

/* ---- the sender ------------------------------------------------------------- */

typedef struct {
    LiveSess *s;
    int16_t   acc[VL_MAX_CHUNK];
    size_t    nacc, chunk;
    int       uncommitted;       /* audio sent since the last commit or flush */
    size_t    unc_n;             /* samples sent since the last commit or flush */
    int       failed;
} Sender;

static int send_json(Sender *x, char *msg, size_t len)
{
    if (!msg) { x->failed = 1; return -1; }
    int rc = voice_ws_send_text(x->s->ws, msg, len);
    secure_zero(msg, len);
    free(msg);
    if (rc != 0) x->failed = 1;
    return rc;
}

static void send_acc(Sender *x)
{
    if (x->nacc == 0 || x->failed) return;
    size_t len = 0;
    char *msg = voice_live_msg_audio(&x->s->t, x->acc, x->nacc, &len);
    size_t n = x->nacc;
    x->nacc = 0;
    secure_zero(x->acc, sizeof(x->acc));
    if (send_json(x, msg, len) == 0) {
        x->uncommitted = 1;
        x->unc_n += n;
        DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_audio", "samples=%zu", n);
    }
}

static void send_gate_closed(Sender *x, int is_end)
{
    LiveSess *s = x->s;
    size_t len = 0;
    if (s->t.prov == VLP_OPENAI) {
        /* Under 100 ms appended: skip (and do not count) the commit; the audio stays in the
         * provider's buffer for the next one, or is simply never committed. */
        if (x->uncommitted && voice_live_commit_due(x->unc_n, s->t.rate) && !x->failed) {
            InterlockedIncrement(&s->commits_sent);   /* counted before it can complete */
            char *msg = voice_live_msg_gate_closed(&s->t, &len);
            if (send_json(x, msg, len) == 0) { x->uncommitted = 0; x->unc_n = 0; }
            DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_commit", "end=%d", is_end);
        }
    } else if (!x->failed) {
        char *msg = voice_live_msg_gate_closed(&s->t, &len);
        if (send_json(x, msg, len) == 0) { x->uncommitted = 0; x->unc_n = 0; }
        DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_flush", "end=%d", is_end);
    }
}

static void handle_item(Sender *x, QItem *it)
{
    LiveSess *s = x->s;
    switch (it->kind) {
    case QI_AUDIO:
        for (size_t i = 0; i < it->n && !x->failed;) {
            size_t take = x->chunk - x->nacc;
            if (take > it->n - i) take = it->n - i;
            memcpy(x->acc + x->nacc, it->s + i, take * sizeof(int16_t));
            x->nacc += take;
            i += take;
            if (x->nacc == x->chunk) send_acc(x);
        }
        break;
    case QI_GATE:
        send_acc(x);
        send_gate_closed(x, 0);
        break;
    case QI_END:
        send_acc(x);
        if (s->t.prov == VLP_MISTRAL) {
            size_t len = 0;
            send_gate_closed(x, 1);                  /* flush, then end */
            if (!x->failed) {
                char *msg = voice_live_msg_end(&s->t, &len);
                send_json(x, msg, len);
            }
        } else {
            send_gate_closed(x, 1);                  /* a commit only when uncommitted */
        }
        if (!x->failed) {
            InterlockedExchange(&s->end_sent, 1);
            try_final(s);
        }
        break;
    }
    if (it->s) { secure_zero(it->s, it->n * sizeof(int16_t)); free(it->s); it->s = NULL; }
}

/* Returns 0 to stop (cancel, over, or a failed send). */
static int drain(Sender *x)
{
    LiveSess *s = x->s;
    QItem *items;
    size_t n;
    EnterCriticalSection(&s->cs);
    items = s->q;
    n = s->qn;
    s->q = NULL;
    s->qn = s->qcap = s->qsamples = 0;
    LeaveCriticalSection(&s->cs);
    size_t i = 0;
    for (; i < n; i++) {
        if (!x->failed && !s->cancel) handle_item(x, &items[i]);
        else if (items[i].s) { secure_zero(items[i].s, items[i].n * sizeof(int16_t)); free(items[i].s); }
    }
    free(items);
    return !x->failed && !s->cancel;
}

/* 0: stop (cancelled or ended); 1: the event fired; -1: `until` (GetTickCount64, 0 = never)
 * passed first. */
static int wait_until(LiveSess *s, HANDLE ev, ULONGLONG until)
{
    HANDLE h[3] = { s->ev_cancel, s->ev_over, ev };
    DWORD ms = INFINITE;
    if (until) {
        ULONGLONG now = GetTickCount64();
        ms = until > now ? (DWORD)(until - now) : 0;
    }
    DWORD r = WaitForMultipleObjects(3, h, FALSE, ms);
    if (r == WAIT_TIMEOUT) return -1;
    return r == WAIT_OBJECT_0 + 2;
}

static int wait_for(LiveSess *s, HANDLE ev)
{
    return wait_until(s, ev, 0) == 1;
}

static DWORD WINAPI send_thread(LPVOID p)
{
    LiveSess *s = (LiveSess *)p;
    dlog_set_thread_tag("live");
    Sender *x = (Sender *)calloc(1, sizeof(*x));
    if (!x) { post_end(s, VLE_CLOSED, NULL, NULL, 0); goto close; }
    x->s = s;
    x->chunk = (size_t)s->t.rate * VOICE_LIVE_CHUNK_MS / 1000u;
    if (x->chunk > VL_MAX_CHUNK) x->chunk = VL_MAX_CHUNK;

    /* The session message goes first, from here (the receiver only reads). */
    ULONGLONG ready_by = s->started_at + VL_READY_TIMEOUT_MS;
    int w = wait_until(s, s->ev_session, ready_by);
    if (w != 1) {
        if (w < 0) goto ready_timeout;
        goto close;
    }
    EnterCriticalSection(&s->cs);
    char *sm = s->session_msg;
    size_t sl = s->session_len;
    s->session_msg = NULL;
    LeaveCriticalSection(&s->cs);
    if (send_json(x, sm, sl) != 0) { post_end(s, VLE_CLOSED, NULL, NULL, 0); goto close; }

    /* Nothing else until the provider says it is ready. */
    w = wait_until(s, s->ev_ready, ready_by);
    if (w != 1) {
        if (w < 0) goto ready_timeout;
        goto close;
    }
    for (;;) {
        if (!wait_for(s, s->ev_feed)) break;
        if (!drain(x)) {
            if (x->failed && !s->cancel) post_end(s, VLE_CLOSED, NULL, NULL, 0);
            break;
        }
    }
    goto close;
ready_timeout:        /* no session.updated 10 s after the start: the link failed */
    DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_ready_timeout", "after_ms=%u", VL_READY_TIMEOUT_MS);
    post_end(s, VLE_CLOSED, NULL, NULL, 0);
close:
    /* The polite close ends the receiver's pending receive; if it fails, the handle goes. */
    {
        VoiceWs *ws = s->ws;
        if (ws) {
            DWORD rc = voice_ws_shutdown(ws);
            if (rc != NO_ERROR) voice_ws_abort(ws);
        } else {
            ai_nc_fire(&s->nc);                  /* still in the handshake */
        }
    }
    if (x) { secure_zero(x, sizeof(*x)); free(x); }
    SetEvent(s->ev_over);
    post_wake(s);
    sess_unref(s);
    return 0;
}

/* ---- the UI thread's API ----------------------------------------------------- */

static void set_err(char *err, size_t cap, const char *m)
{
    if (err && cap) snprintf(err, cap, "%s", m);
}

static void list_remove(LiveSess *s)
{
    for (int i = 0; i < VL_MAX_SESS; i++) if (g_list[i] == s) g_list[i] = NULL;
}

int voice_live_start(HWND notify, const VoiceLiveTarget *t, const char *key, unsigned token,
                     char *err, size_t errcap)
{
    init_globals();
    if (err && errcap) err[0] = '\0';
    if (!voice_gate_on()) {     /* one gate: nothing starts while voice is off */
        set_err(err, errcap, "Voice input is turned off.");
        return 0;
    }
    if (!notify || !t || !key || !key[0] || (t->prov != VLP_OPENAI && t->prov != VLP_MISTRAL)) {
        set_err(err, errcap, "live session: bad arguments");
        return 0;
    }
    if (g_cur) voice_live_close(g_cur->token);       /* one live session at a time */
    int slot = -1;
    for (int tries = 0; tries < 2 && slot < 0; tries++) {
        for (int i = 0; i < VL_MAX_SESS; i++) if (!g_list[i]) { slot = i; break; }
        if (slot < 0) voice_live_reap(0);
    }
    if (slot < 0) { set_err(err, errcap, "live session: too many sessions still closing"); return 0; }

    LiveSess *s = (LiveSess *)calloc(1, sizeof(*s));
    if (!s) { set_err(err, errcap, "out of memory"); return 0; }
    s->token = token;
    s->started_at = GetTickCount64();
    s->notify = notify;
    s->t = *t;
    s->key = _strdup(key);
    ai_nc_init(&s->nc);
    InitializeCriticalSection(&s->cs);
    InitializeCriticalSection(&s->post_cs);
    s->ev_feed = CreateEventA(NULL, FALSE, FALSE, NULL);
    s->ev_cancel = CreateEventA(NULL, TRUE, FALSE, NULL);
    s->ev_session = CreateEventA(NULL, TRUE, FALSE, NULL);
    s->ev_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    s->ev_over = CreateEventA(NULL, TRUE, FALSE, NULL);
    voice_sgate_init(&s->gate, t->gate_rms, t->hangover_ms);
    voice_rs_init(&s->rs);
    s->refs = 3;
    if (!s->key || !s->ev_feed || !s->ev_cancel || !s->ev_session || !s->ev_ready || !s->ev_over) {
        set_err(err, errcap, "out of memory");
        s->refs = 0;
        sess_free(s);
        return 0;
    }

    /* Both threads are made suspended, so a failed second one leaves nothing running. */
    s->th_recv = CreateThread(NULL, 0, recv_thread, s, CREATE_SUSPENDED, NULL);
    s->th_send = s->th_recv ? CreateThread(NULL, 0, send_thread, s, CREATE_SUSPENDED, NULL) : NULL;
    if (!s->th_recv || !s->th_send) {
        if (s->th_recv) { TerminateThread(s->th_recv, 0); CloseHandle(s->th_recv); }
        set_err(err, errcap, "could not start the live threads");
        s->th_recv = s->th_send = NULL;
        s->refs = 0;
        sess_free(s);
        return 0;
    }

    voice_ledger_acquire(VRES_LIVE_SESSION);
    voice_ledger_acquire(VRES_LIVE_THREAD);
    voice_ledger_acquire(VRES_LIVE_THREAD);
    g_list[slot] = s;
    EnterCriticalSection(&g_cs);
    g_cur = s;
    LeaveCriticalSection(&g_cs);
    DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_start", "prov=%d", (int)t->prov);
    ResumeThread(s->th_recv);
    ResumeThread(s->th_send);
    return 1;
}

void voice_live_close(unsigned token)
{
    init_globals();
    EnterCriticalSection(&g_cs);
    LiveSess *s = g_cur;
    if (!s || !tok_eq(s->token, token)) { LeaveCriticalSection(&g_cs); return; }
    g_cur = NULL;
    LeaveCriticalSection(&g_cs);
    EnterCriticalSection(&s->post_cs);
    InterlockedExchange(&s->cancel, 1);              /* nothing is posted after this */
    LeaveCriticalSection(&s->post_cs);
    SetEvent(s->ev_cancel);                          /* the sender does the close */
    s->closed = 1;
    s->closed_at = GetTickCount64();
    voice_ledger_release(VRES_LIVE_SESSION);
    DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_close", "ended=%d", (int)s->ended);
}

int voice_live_test_detached_count(void)
{
    return (int)g_detached;
}

/* Wake whatever the session's threads are blocked on. */
static void abort_blocked(LiveSess *s)
{
    VoiceWs *ws = s->ws;
    if (ws) voice_ws_abort(ws);
    else ai_nc_fire(&s->nc);
}

void voice_live_reap(DWORD wait_ms)
{
    init_globals();
    if (wait_ms > 0 && g_cur) voice_live_close(g_cur->token);
    ULONGLONG deadline = GetTickCount64() + wait_ms;
    for (int i = 0; i < VL_MAX_SESS; i++) {
        LiveSess *s = g_list[i];
        if (!s || !s->closed) continue;
        ULONGLONG now = GetTickCount64();
        DWORD left = (wait_ms > 0 && deadline > now) ? (DWORD)(deadline - now) : 0;
        /* Both threads posted their exit wake-up: they are returning, a short wait joins. */
        if (left == 0 && s->exited >= 2) left = 1000;
        HANDLE h[2] = { s->th_recv, s->th_send };
        DWORD r = WaitForMultipleObjects(2, h, TRUE, left);
        if (r != WAIT_OBJECT_0) {
            if (wait_ms == 0) {
                /* Closed long ago and still running: abort what is blocked, then give it a
                 * further bounded wait, then detach (a later reap decides each step). */
                if (!s->aborted_at && now - s->closed_at >= VL_ABORT_AFTER_MS) {
                    abort_blocked(s);
                    s->aborted_at = now;
                    DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_abort", "late");
                    continue;
                }
                if (!s->aborted_at || now - s->aborted_at < VL_DETACH_AFTER_MS) continue;
            }
            /* Detach: abort what is blocked; the threads free the session when they end. */
            abort_blocked(s);
            s->detached = 1;
            InterlockedIncrement(&g_detached);
            DLOG(DLOG_AI, DLOG_INFO, 0, "voice.live_detach", "wait_ms=%lu", (unsigned long)wait_ms);
        }
        CloseHandle(s->th_recv);
        CloseHandle(s->th_send);
        s->th_recv = s->th_send = NULL;
        voice_ledger_release(VRES_LIVE_THREAD);
        voice_ledger_release(VRES_LIVE_THREAD);
        g_list[i] = NULL;
        list_remove(s);
        sess_unref(s);                               /* the owner's reference */
    }
}
