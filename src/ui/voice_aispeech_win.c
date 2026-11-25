#include <winsock2.h>
#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "voice_aispeech_win.h"
#include "voice_audio_win.h"
#include "voice_tts_win.h"
#include "voice_gate.h"
#include "voice_ledger.h"
#include "voice_ptt.h"
#include "voice_speak.h"
#include "secure_zero.h"

#define AIS_MAX_CTX      2
#define AIS_MAX_CHUNKS   2048
#define AIS_QUEUE_LIMIT_MS 20000
#define AIS_GEN_BASE     0x40000000u   /* apart from the SAPI engine's generations */
#define AIS_SAMPLE_TEXT  "Hello. This is how replies will sound."

/* One worker's state. Allocated by the UI thread, freed by the UI thread after the worker has
 * ended; the worker never frees it. Every field below `thread` that the worker and the UI both
 * touch is guarded by g_lock. */
typedef struct AisJob {
    struct AisJob *next;
    unsigned token, reply_id, serial;
    HWND notify;
    HANDLE thread;                 /* NULL once closed (joined or given up on) */
    char *text, *key;              /* the worker's copies, wiped at its end */
    size_t text_len;
    size_t *starts, *lens;
    size_t n;
    VoiceAisTarget target;
    VoiceAisFetch fetch;
    int dead, cancel, ended;       /* under g_lock */
    unsigned cur_chunk;            /* under g_lock */
    uint64_t chunk_frames;         /* frames pushed for cur_chunk, under g_lock */
    int push_failed;               /* render_data refused a block, under g_lock */
} AisJob;

typedef struct {
    HWND notify;
    unsigned token, gen;
    int slot;
    int auto_quit, holding, volume;
    /* the reply being spoken (UI side) */
    int speaking;
    unsigned reply_id;
    char *text;
    size_t text_len, from;
    size_t *starts, *lens;
    uint64_t *totals;
    unsigned char *complete;
    size_t n, chunks_done;
    int failed;
    size_t fail_chunk;
    long fail_hr;
    unsigned serial;               /* the current worker's */
    int worker_exited;
    int pos_valid;
    unsigned pos_chunk;
    uint64_t pos_frames;
    size_t last_off;
} AisCtx;

static AisCtx *g_ctx[AIS_MAX_CTX];        /* UI thread only */
static AisJob *g_jobs;                    /* UI thread only (the list itself) */
static CRITICAL_SECTION g_lock;
static int g_lock_ready;
static unsigned g_serial;
static unsigned g_gen = AIS_GEN_BASE;
static VoiceAisFetch g_fetch;

static void lock(void)   { EnterCriticalSection(&g_lock); }
static void unlock(void) { LeaveCriticalSection(&g_lock); }

void voice_ais_set_fetch(VoiceAisFetch f) { g_fetch = f; }

/* ---- small helpers ---- */

static AisCtx *ctx_find(unsigned token)
{
    for (int i = 0; i < AIS_MAX_CTX; i++)
        if (g_ctx[i] != NULL && g_ctx[i]->token == token) return g_ctx[i];
    return NULL;
}

static AisCtx *ctx_find_masked(unsigned token24)
{
    for (int i = 0; i < AIS_MAX_CTX; i++)
        if (g_ctx[i] != NULL && (g_ctx[i]->token & VOICE_TOKEN_MASK) == token24) return g_ctx[i];
    return NULL;
}

static void post_tts(const AisCtx *c, VoiceTtsKind kind, unsigned reply, size_t offset, long hr)
{
    if (c->notify == NULL) return;
    VoiceTtsMsg *m = (VoiceTtsMsg *)calloc(1, sizeof(*m));
    if (m == NULL) return;
    m->token = c->token;
    m->gen = c->gen;
    m->kind = kind;
    m->reply_id = reply;
    m->offset = offset;
    m->hr = hr;
    if (!PostMessageW(c->notify, WM_VOICE_TTS, 0, (LPARAM)m)) free(m);
}

static void kill_guard(const AisCtx *c)
{
    if (c->notify != NULL) KillTimer(c->notify, (UINT_PTR)(VOICE_AIS_TIMER_BASE + c->slot));
}

static void arm_guard(const AisCtx *c)
{
    if (c->notify != NULL)
        SetTimer(c->notify, (UINT_PTR)(VOICE_AIS_TIMER_BASE + c->slot), VOICE_AIS_DONE_GUARD_MS, NULL);
}

static void reply_clear(AisCtx *c)
{
    if (c->text != NULL) { secure_zero(c->text, c->text_len + 1); free(c->text); }
    free(c->starts); free(c->lens); free(c->totals); free(c->complete);
    c->text = NULL; c->starts = c->lens = NULL; c->totals = NULL; c->complete = NULL;
    c->text_len = c->n = c->chunks_done = 0;
    c->failed = 0; c->fail_chunk = 0; c->fail_hr = 0;
    c->worker_exited = 0; c->pos_valid = 0; c->pos_chunk = 0; c->pos_frames = 0;
    c->last_off = 0;
}

static void cancel_jobs(unsigned token)
{
    if (!g_lock_ready) return;
    lock();
    for (AisJob *j = g_jobs; j != NULL; j = j->next)
        if (j->token == token) j->cancel = 1;
    unlock();
}

/* The byte offset of a position in chunk `chunk`, `frames` in. */
static size_t pos_offset(const AisCtx *c, unsigned chunk, uint64_t frames)
{
    if (c->text == NULL || chunk >= c->n) return c->from;
    return voice_ais_offset(c->text, c->starts[chunk], c->lens[chunk], frames,
                            c->totals[chunk], c->complete[chunk]);
}

/* Where reading should resume: the played position, or, for a failure in chunk fail_chunk
 * (-1: none), the played position when it is inside that chunk, else the chunk's start. Never
 * before `from` or an offset already reported. */
static size_t cur_offset(const AisCtx *c, long fail_chunk)
{
    size_t off;
    if (!c->pos_valid) off = c->from;
    else if (fail_chunk >= 0 && (long)c->pos_chunk < fail_chunk && (size_t)fail_chunk < c->n)
        off = c->starts[fail_chunk];
    else off = pos_offset(c, c->pos_chunk, c->pos_frames);
    if (off < c->last_off) off = c->last_off;
    if (off < c->from) off = c->from;
    return off;
}

/* A VTT_DONE or VTT_ERROR ends the reply; a sample then quits itself. */
static void post_result(AisCtx *c, VoiceTtsKind kind, size_t offset, long hr)
{
    const unsigned token = c->token;
    const int auto_quit = c->auto_quit;
    post_tts(c, kind, c->reply_id, offset, hr);
    if (auto_quit) voice_ais_quit(token, 3000);
}

/* ---- jobs ---- */

static void job_free(AisJob *j)
{
    free(j->starts); free(j->lens);
    free(j);
}

static void job_unlink(AisJob *j)
{
    for (AisJob **pp = &g_jobs; *pp != NULL; pp = &(*pp)->next)
        if (*pp == j) { *pp = j->next; return; }
}

/* The worker has ended (or is about to return from its last statement): close, release, free. */
static void job_reap(AisJob *j, DWORD wait_ms)
{
    if (j->thread != NULL) {
        WaitForSingleObject(j->thread, wait_ms);
        CloseHandle(j->thread);
        j->thread = NULL;
    }
    voice_ledger_release(VRES_SPEECH_REQ);
    job_unlink(j);
    job_free(j);
}

/* Worker side: post a message while holding g_lock. */
static void wpost(const AisJob *j, VoiceAisKind kind, unsigned chunk, uint64_t frames, long hr)
{
    VoiceAisMsg *m = (VoiceAisMsg *)calloc(1, sizeof(*m));
    if (m == NULL) return;
    m->token = j->token;
    m->reply_id = j->reply_id;
    m->chunk = chunk;
    m->kind = kind;
    m->frames = frames;
    m->hr = hr;
    if (j->notify == NULL || !PostMessageW(j->notify, WM_VOICE_AIS, 0, (LPARAM)m)) free(m);
}

/* The push the fetch calls. Holds the lock across the dead check and the render push, so a
 * quit that has set the dead flag is never followed by a push. */
static int ais_push(void *job, const int16_t *s, size_t n)
{
    AisJob *j = (AisJob *)job;
    int ok = 0;
    if (j == NULL || (s == NULL && n > 0)) return 0;
    lock();
    if (!j->dead && !j->cancel) {
        if (n == 0) ok = 1;
        else {
            int16_t *buf = (int16_t *)malloc(n * sizeof(int16_t));
            if (buf != NULL) {
                memcpy(buf, s, n * sizeof(int16_t));
                if (voice_audio_render_data(j->token, j->reply_id, j->cur_chunk, buf, n) == 1) {
                    j->chunk_frames += n;
                    ok = 1;
                } else {
                    secure_zero(buf, n * sizeof(int16_t));
                    free(buf);
                    j->push_failed = 1;
                }
            } else {
                j->push_failed = 1;
            }
        }
    }
    unlock();
    return ok;
}

/* Wait below 20 s queued, in 50 ms slices; 0 when the job is dead or cancelled. */
static int wait_room(AisJob *j)
{
    for (;;) {
        lock();
        const int stop = j->dead || j->cancel;
        unlock();
        if (stop) return 0;
        if (voice_audio_render_queued_ms() < AIS_QUEUE_LIMIT_MS) return 1;
        Sleep(50);
    }
}

static DWORD WINAPI ais_worker(LPVOID arg)
{
    AisJob *j = (AisJob *)arg;
    int failed = 0, stopped = 0;

    for (size_t k = 0; k < j->n && !failed && !stopped; k++) {
        if (!wait_room(j)) { stopped = 1; break; }
        lock();
        j->cur_chunk = (unsigned)k;
        j->chunk_frames = 0;
        j->push_failed = 0;
        unlock();

        long hr = j->fetch != NULL
            ? j->fetch(j, &j->target, j->key != NULL ? j->key : "", j->text + j->starts[k], j->lens[k], ais_push)
            : VOICE_AIS_HR_NET;

        lock();
        if (j->dead || j->cancel) { unlock(); stopped = 1; break; }
        const uint64_t frames = j->chunk_frames;
        if (hr == 0 && j->push_failed) hr = VOICE_AIS_HR_DEVICE;
        if (hr == 0 && frames == 0) hr = VOICE_AIS_HR_STREAM;
        if (hr != 0) {
            /* FAILED first, then the end: the UI sees the failure before the end plays out. */
            wpost(j, VAI_FAILED, (unsigned)k, frames, hr);
            voice_audio_render_end(j->token, j->reply_id);
            failed = 1;
        } else {
            wpost(j, VAI_CHUNK_DONE, (unsigned)k, frames, 0);
        }
        unlock();
    }
    if (!failed && !stopped) {
        lock();
        if (!j->dead && !j->cancel) voice_audio_render_end(j->token, j->reply_id);
        unlock();
    }

    if (j->text != NULL) { secure_zero(j->text, j->text_len + 1); free(j->text); j->text = NULL; }
    if (j->key != NULL) { secure_zero(j->key, strlen(j->key) + 1); free(j->key); j->key = NULL; }
    lock();
    wpost(j, VAI_EXITED, 0, j->serial, 0);   /* the one post that ignores the flags */
    j->ended = 1;
    unlock();
    return 0;   /* j may be freed by the UI from here on: never touched again */
}

/* ---- contexts ---- */

int voice_ais_start(HWND notify, unsigned token)
{
    if (!voice_gate_on() || voice_audio_wait_ready(0) != 1) return 0;
    if (ctx_find(token) != NULL) return 1;
    int slot = -1;
    for (int i = 0; i < AIS_MAX_CTX; i++) if (g_ctx[i] == NULL) { slot = i; break; }
    if (slot < 0) return 0;
    if (!g_lock_ready) { InitializeCriticalSection(&g_lock); g_lock_ready = 1; }
    AisCtx *c = (AisCtx *)calloc(1, sizeof(*c));
    if (c == NULL) return 0;
    c->notify = notify;
    c->token = token;
    c->gen = ++g_gen;
    c->slot = slot;
    c->volume = 100;
    c->auto_quit = (token == VOICE_AIS_SAMPLE_TOKEN);
    g_ctx[slot] = c;
    return 1;
}

int voice_ais_running(void)
{
    int n = 0;
    for (int i = 0; i < AIS_MAX_CTX; i++) if (g_ctx[i] != NULL) n++;
    return n;
}

int voice_ais_owns(unsigned token) { return ctx_find(token) != NULL; }

void voice_ais_volume(unsigned token, int percent)
{
    AisCtx *c = ctx_find(token);
    if (c == NULL) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    c->volume = percent;
    if (c->holding) voice_audio_render_gain(token, percent);
}

void voice_ais_stop(unsigned token)
{
    AisCtx *c = ctx_find(token);
    if (c == NULL) return;
    cancel_jobs(token);
    if (c->holding) voice_audio_render_flush(token, c->reply_id + 1u);
    kill_guard(c);
    c->speaking = 0;
}

void voice_ais_speak(unsigned token, unsigned reply_id, const char *utf8, size_t from,
                     const VoiceAisTarget *t, const char *key)
{
    AisCtx *c = ctx_find(token);
    if (c == NULL || utf8 == NULL) return;

    cancel_jobs(token);                       /* the old worker's cancel */
    if (c->holding) voice_audio_render_flush(token, reply_id);
    kill_guard(c);
    c->speaking = 0;
    reply_clear(c);
    c->reply_id = reply_id;

    const size_t len = strlen(utf8);
    if (from > len) from = len;
    c->from = from;

    if (t == NULL || t->url[0] == '\0') {
        post_result(c, VTT_ERROR, from, VOICE_AIS_HR_RESOLVE);
        return;
    }

    char *text = (char *)malloc(len + 1);
    const size_t cap = len + 1 < AIS_MAX_CHUNKS ? len + 1 : AIS_MAX_CHUNKS;
    size_t *starts = (size_t *)calloc(cap, sizeof(size_t));
    size_t *lens = (size_t *)calloc(cap, sizeof(size_t));
    size_t n = 0;
    const int alloc_ok = text != NULL && starts != NULL && lens != NULL;
    if (alloc_ok) {
        memcpy(text, utf8, len + 1);
        n = voice_speak_chunks(text, from, starts, lens, cap);
    }
    if (!alloc_ok || n == 0) {
        if (text != NULL) { secure_zero(text, len + 1); free(text); }
        free(starts); free(lens);
        if (alloc_ok) post_result(c, VTT_DONE, from, 0);                  /* nothing speakable */
        else post_result(c, VTT_ERROR, from, VOICE_AIS_HR_DEVICE);        /* out of memory */
        return;
    }

    /* The render client, when this context does not hold it (R35). */
    if (!c->holding) {
        if (voice_audio_render_open(token) != 1) {
            secure_zero(text, len + 1); free(text); free(starts); free(lens);
            post_result(c, VTT_ERROR, from, VOICE_AIS_HR_DEVICE);
            return;
        }
        voice_ledger_acquire(VRES_RENDER);
        c->holding = 1;
        if (c->volume != 100) voice_audio_render_gain(token, c->volume);   /* after the open */
    }

    AisJob *j = (AisJob *)calloc(1, sizeof(*j));
    const size_t klen = key != NULL ? strlen(key) : 0;
    if (j != NULL) {
        j->key = (char *)malloc(klen + 1);
        j->text = (char *)malloc(len + 1);
        j->starts = (size_t *)malloc(n * sizeof(size_t));
        j->lens = (size_t *)malloc(n * sizeof(size_t));
    }
    if (j == NULL || j->key == NULL || j->text == NULL || j->starts == NULL || j->lens == NULL) {
        if (j != NULL) {
            if (j->key != NULL) { secure_zero(j->key, klen + 1); free(j->key); }
            if (j->text != NULL) { secure_zero(j->text, len + 1); free(j->text); }
            job_free(j);
        }
        secure_zero(text, len + 1); free(text); free(starts); free(lens);
        post_result(c, VTT_ERROR, from, VOICE_AIS_HR_DEVICE);
        return;
    }
    if (klen > 0) memcpy(j->key, key, klen);
    j->key[klen] = '\0';
    memcpy(j->text, utf8, len + 1);
    j->text_len = len;
    memcpy(j->starts, starts, n * sizeof(size_t));
    memcpy(j->lens, lens, n * sizeof(size_t));
    j->n = n;
    j->target = *t;
    j->fetch = g_fetch;
    j->token = token;
    j->reply_id = reply_id;
    j->notify = c->notify;
    j->serial = ++g_serial;

    c->text = text;
    c->text_len = len;
    c->starts = starts;
    c->lens = lens;
    c->n = n;
    c->totals = (uint64_t *)calloc(n, sizeof(uint64_t));
    c->complete = (unsigned char *)calloc(n, 1);
    if (c->totals == NULL || c->complete == NULL) {
        secure_zero(j->key, klen + 1); free(j->key);
        secure_zero(j->text, len + 1); free(j->text);
        job_free(j);
        reply_clear(c);
        post_result(c, VTT_ERROR, from, VOICE_AIS_HR_DEVICE);
        return;
    }
    c->serial = j->serial;

    /* Link first: the worker's EXITED must find the job. The thread starts holding no lock. */
    j->next = g_jobs;
    g_jobs = j;
    j->thread = CreateThread(NULL, 0, ais_worker, j, 0, NULL);
    if (j->thread == NULL) {
        job_unlink(j);
        secure_zero(j->key, klen + 1); free(j->key);
        secure_zero(j->text, len + 1); free(j->text);
        job_free(j);
        reply_clear(c);
        post_result(c, VTT_ERROR, from, VOICE_AIS_HR_NET);
        return;
    }
    voice_ledger_acquire(VRES_SPEECH_REQ);
    c->speaking = 1;
}

/* ---- quit (R36) ---- */

static void ctx_quit_one(AisCtx *c, unsigned wait_ms)
{
    const unsigned token = c->token;
    AisJob *j, *nx;
    const ULONGLONG deadline = GetTickCount64() + (ULONGLONG)wait_ms;

    g_ctx[c->slot] = NULL;
    kill_guard(c);

    lock();                                  /* dead before the join: no push can begin after */
    for (j = g_jobs; j != NULL; j = j->next)
        if (j->token == token) { j->dead = 1; j->cancel = 1; }
    unlock();

    for (j = g_jobs; j != NULL; j = nx) {
        nx = j->next;
        if (j->token != token) continue;
        const ULONGLONG now = GetTickCount64();
        const DWORD remain = deadline > now ? (DWORD)(deadline - now) : 0;
        if (j->thread != NULL && WaitForSingleObject(j->thread, remain) == WAIT_OBJECT_0)
            job_reap(j, 0);
        else if (j->thread != NULL) {
            /* detached: the count stays until its VAI_EXITED */
            CloseHandle(j->thread);
            j->thread = NULL;
        }
    }

    if (c->holding) {
        voice_audio_render_close(token);
        voice_ledger_release(VRES_RENDER);
    }
    reply_clear(c);
    free(c);
}

void voice_ais_quit(unsigned token, unsigned wait_ms)
{
    AisCtx *c = ctx_find(token);
    if (c == NULL) return;
    ctx_quit_one(c, wait_ms);
}

void voice_ais_quit_all(unsigned wait_ms)
{
    if (g_lock_ready) {
        lock();
        for (AisJob *j = g_jobs; j != NULL; j = j->next) { j->dead = 1; j->cancel = 1; }
        unlock();
    }
    for (int i = 0; i < AIS_MAX_CTX; i++)
        if (g_ctx[i] != NULL) ctx_quit_one(g_ctx[i], wait_ms);
}

int voice_ais_sample(HWND notify, const VoiceAisTarget *t, const char *key)
{
    if (!voice_ais_start(notify, VOICE_AIS_SAMPLE_TOKEN)) return 0;
    voice_ais_speak(VOICE_AIS_SAMPLE_TOKEN, 1, AIS_SAMPLE_TEXT, 0, t, key);
    return 1;
}

/* ---- the UI side of the messages ---- */

/* The reply is over: VTT_DONE when every chunk completed, VTT_ERROR for a failure. */
static void finish_reply(AisCtx *c)
{
    if (!c->speaking) return;
    c->speaking = 0;
    kill_guard(c);
    if (c->failed)
        post_result(c, VTT_ERROR, cur_offset(c, (long)c->fail_chunk), c->fail_hr);
    else if (c->chunks_done == c->n)
        post_result(c, VTT_DONE, 0, 0);
}

void voice_ais_on_audio(UINT kind, LPARAM lp)
{
    if (kind == VAM_PLAYED || kind == VAM_PLAY_DONE) {
        VoiceRenderMsg *m = (VoiceRenderMsg *)lp;
        if (m == NULL) return;
        AisCtx *c = ctx_find(m->token);
        if (c != NULL && c->speaking && m->reply_id == c->reply_id) {
            const int progress = !c->pos_valid || c->pos_chunk != m->chunk ||
                                 c->pos_frames != m->frames_in_chunk;
            if (kind == VAM_PLAYED || m->frames_in_chunk > 0 || !c->pos_valid) {
                c->pos_valid = 1;
                c->pos_chunk = m->chunk;
                c->pos_frames = m->frames_in_chunk;
            }
            if (kind == VAM_PLAYED) {
                if (progress && m->chunk < c->n) {
                    size_t off = cur_offset(c, -1);
                    c->last_off = off;
                    post_tts(c, VTT_WORD, c->reply_id, off, 0);
                }
                if (progress && c->worker_exited) arm_guard(c);   /* R30: re-armed on progress */
            } else {
                finish_reply(c);
            }
        }
        free(m);
        return;
    }
    if (kind == VAM_RENDER_ERROR) {
        AisCtx *c = ctx_find_masked(VOICE_ERR_TOKEN(lp));
        if (c == NULL || !c->holding) return;
        c->holding = 0;
        voice_ledger_release(VRES_RENDER);
        if (c->speaking) {            /* a passing failure at the current offset (R35) */
            cancel_jobs(c->token);
            c->speaking = 0;
            kill_guard(c);
            post_result(c, VTT_ERROR, cur_offset(c, -1), VOICE_AIS_HR_DEVICE);
        }
    }
}

void voice_ais_on_msg(VoiceAisMsg *m)
{
    if (m == NULL) return;
    AisCtx *c = ctx_find(m->token);
    if (m->kind == VAI_EXITED) {
        AisJob *j = g_jobs;
        while (j != NULL && j->serial != (unsigned)m->frames) j = j->next;
        if (j == NULL) return;                 /* joined by a quit already */
        const unsigned serial = j->serial;
        job_reap(j, 3000);
        if (c != NULL && c->serial == serial && c->speaking && c->reply_id == m->reply_id) {
            c->worker_exited = 1;
            arm_guard(c);                     /* R30: armed once the worker has finished */
        }
        return;
    }
    if (c == NULL || !c->speaking || m->reply_id != c->reply_id || m->chunk >= c->n) return;
    if (m->kind == VAI_CHUNK_DONE) {
        c->totals[m->chunk] = m->frames;
        c->complete[m->chunk] = 1;
        c->chunks_done++;
    } else if (m->kind == VAI_FAILED) {
        c->totals[m->chunk] = m->frames;
        c->complete[m->chunk] = 1;
        c->failed = 1;
        c->fail_chunk = m->chunk;
        c->fail_hr = m->hr;
    }
}

void voice_ais_on_timer(UINT_PTR id)
{
    if (id < VOICE_AIS_TIMER_BASE || id >= VOICE_AIS_TIMER_BASE + AIS_MAX_CTX) return;
    AisCtx *c = g_ctx[id - VOICE_AIS_TIMER_BASE];
    if (c == NULL) return;
    kill_guard(c);                            /* one-shot */
    if (c->speaking && c->worker_exited) finish_reply(c);
}
