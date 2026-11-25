#include <winsock2.h>
#include <windows.h>
#include <initguid.h>
#define COBJMACROS
#include <sapi.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "voice_tts_win.h"
#include "voice_gate.h"
#include "voice_ledger.h"
#include "voice_speak.h"
#include "dll_record.h"
#include "secure_zero.h"

#define TTS_MAX_CTX 2
#define TTS_BATCH   256

typedef struct {
    size_t start;        /* UTF-8 offset of the chunk in the caller's text */
    size_t len;          /* UTF-8 length */
    wchar_t *wide;       /* the chunk as UTF-16, NUL-terminated */
    size_t *map;         /* map[i] = UTF-8 offset within the chunk of wide char i */
    size_t wlen;
    ULONG stream;        /* SAPI stream number, 0 until Speak accepted it */
    int done;
} TtsChunk;

typedef struct TtsCtx {
    HWND notify;
    unsigned token;
    char voice_id[256];
    HANDLE thread;
    HANDLE cmd_evt;               /* auto-reset: a command is waiting */
    HANDLE ready_evt;             /* manual-reset: READY or ERROR posted (selftest waits) */
    int auto_quit;                /* a Play sample: ends after DONE / ERROR */
    int started_ok;               /* written by the thread before ready_evt */

    CRITICAL_SECTION cs;          /* guards everything below */
    int cs_live;
    int quit, stop, exited, detached;
    int have_speak;
    unsigned speak_reply;
    char *speak_text;             /* owned copy, wiped on free */
    size_t speak_len, speak_from;
    int volume;                   /* -1 = no change pending */
} TtsCtx;

static TtsCtx *g_ctx[TTS_MAX_CTX];   /* UI thread only */

/* ---- UTF-8 -> UTF-16 with position map ---- */

size_t voice_tts_widen(const char *s, size_t len, wchar_t *w, size_t *map, size_t wcap)
{
    size_t i = 0, n = 0;
    while (i < len && n < wcap) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0xFFFD;
        size_t need = 1;
        if (c < 0x80) { cp = c; }
        else if (c >= 0xC2 && c <= 0xDF) { need = 2; cp = c & 0x1Fu; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 3; cp = c & 0x0Fu; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 4; cp = c & 0x07u; }
        if (need > 1) {
            int ok = i + need <= len;
            for (size_t k = 1; ok && k < need; k++) {
                const unsigned char cc = (unsigned char)s[i + k];
                if ((cc & 0xC0) != 0x80) ok = 0;
                else cp = (cp << 6) | (cc & 0x3Fu);
            }
            if (ok && ((need == 3 && cp < 0x800) || (need == 4 && (cp < 0x10000 || cp > 0x10FFFF))
                       || (cp >= 0xD800 && cp <= 0xDFFF)))
                ok = 0;
            if (!ok) { cp = 0xFFFD; need = 1; }
        }
        if (cp >= 0x10000) {
            if (n + 2 > wcap) break;
            cp -= 0x10000;
            map[n] = i; w[n++] = (wchar_t)(0xD800 + (cp >> 10));
            map[n] = i; w[n++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            map[n] = i; w[n++] = (wchar_t)cp;
        }
        i += need;
    }
    map[n] = i;
    return n;
}

/* ---- posting ---- */

static void post(const TtsCtx *c, VoiceTtsKind kind, unsigned reply, size_t offset, long hr)
{
    if (c->notify == NULL) return;
    VoiceTtsMsg *m = (VoiceTtsMsg *)calloc(1, sizeof(*m));
    if (m == NULL) return;
    m->token = c->token;
    m->kind = kind;
    m->reply_id = reply;
    m->offset = offset;
    m->hr = hr;
    if (!PostMessageW(c->notify, WM_VOICE_TTS, 0, (LPARAM)m)) free(m);
}

/* ---- the speech thread ---- */

static void chunks_free(TtsChunk *ch, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (ch[i].wide != NULL) secure_zero(ch[i].wide, (ch[i].wlen + 1) * sizeof(wchar_t));
        free(ch[i].wide);
        free(ch[i].map);
    }
    free(ch);
}

typedef struct {
    TtsChunk *ch;
    size_t n, remaining;
    unsigned reply;
} Reply;

static void reply_clear(Reply *r)
{
    chunks_free(r->ch, r->n);
    memset(r, 0, sizeof(*r));
}

static void purge(ISpVoice *v)
{
    (void)ISpVoice_Speak(v, NULL, SPF_PURGEBEFORESPEAK, NULL);
}

/* Queue every chunk of text[from..] on `v` as one reply. Posts VTT_DONE at once
 * for nothing speakable, VTT_ERROR when SAPI refuses. */
static void begin_reply(TtsCtx *c, ISpVoice *v, Reply *r, unsigned reply_id,
                        const char *text, size_t len, size_t from)
{
    size_t starts[TTS_BATCH], lens[TTS_BATCH];
    size_t cap = 0;
    reply_clear(r);
    r->reply = reply_id;
    if (from > len) from = len;

    size_t pos = from;
    for (;;) {
        const size_t got = voice_speak_chunks(text, pos, starts, lens, TTS_BATCH);
        if (got == 0) break;
        if (r->n + got > cap) {
            cap = (r->n + got) * 2;
            TtsChunk *nc = (TtsChunk *)realloc(r->ch, cap * sizeof(*nc));
            if (nc == NULL) { post(c, VTT_ERROR, reply_id, 0, (long)E_OUTOFMEMORY); reply_clear(r); return; }
            r->ch = nc;
        }
        for (size_t k = 0; k < got; k++) {
            TtsChunk *ck = &r->ch[r->n];
            memset(ck, 0, sizeof(*ck));
            ck->start = starts[k];
            ck->len = lens[k];
            ck->wide = (wchar_t *)calloc(ck->len + 1, sizeof(wchar_t));
            ck->map = (size_t *)calloc(ck->len + 1, sizeof(size_t));
            if (ck->wide == NULL || ck->map == NULL) {
                r->n++;
                post(c, VTT_ERROR, reply_id, 0, (long)E_OUTOFMEMORY);
                reply_clear(r);
                return;
            }
            ck->wlen = voice_tts_widen(text + ck->start, ck->len, ck->wide, ck->map, ck->len);
            r->n++;
        }
        pos = starts[got - 1] + lens[got - 1];
        if (got < TTS_BATCH) break;
    }
    if (r->n == 0) { post(c, VTT_DONE, reply_id, 0, 0); reply_clear(r); return; }

    for (size_t i = 0; i < r->n; i++) {
        ULONG stream = 0;
        const HRESULT hr = ISpVoice_Speak(v, r->ch[i].wide, SPF_ASYNC | SPF_IS_NOT_XML, &stream);
        if (FAILED(hr)) {
            purge(v);
            post(c, VTT_ERROR, reply_id, 0, (long)hr);
            reply_clear(r);
            return;
        }
        r->ch[i].stream = stream;
    }
    r->remaining = r->n;
}

/* Drain SAPI's event queue; returns 1 when the reply just finished. */
static int pump_events(TtsCtx *c, ISpVoice *v, Reply *r)
{
    int finished = 0;
    SPEVENT ev;
    ULONG got = 0;
    while (SUCCEEDED(ISpVoice_GetEvents(v, 1, &ev, &got)) && got == 1) {
        if (r->ch == NULL) continue;   /* a purged reply's late events */
        size_t i = 0;
        while (i < r->n && r->ch[i].stream != ev.ulStreamNum) i++;
        if (i == r->n) continue;
        TtsChunk *ck = &r->ch[i];
        if (ev.eEventId == SPEI_WORD_BOUNDARY) {
            const size_t wpos = (size_t)ev.lParam;
            if (wpos <= ck->wlen) post(c, VTT_WORD, r->reply, ck->start + ck->map[wpos], 0);
        } else if (ev.eEventId == SPEI_END_INPUT_STREAM && !ck->done) {
            ck->done = 1;
            if (r->remaining > 0 && --r->remaining == 0) {
                post(c, VTT_DONE, r->reply, 0, 0);
                reply_clear(r);
                finished = 1;
                break;   /* later events belong to nothing */
            }
        }
    }
    return finished;
}

static HRESULT select_voice(ISpVoice *v, const char *voice_id)
{
    wchar_t wid[256];
    if (voice_id == NULL || voice_id[0] == '\0') return S_OK;
    if (!MultiByteToWideChar(CP_UTF8, 0, voice_id, -1, wid, 256)) return E_INVALIDARG;
    ISpObjectToken *tok = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_SpObjectToken, NULL, CLSCTX_INPROC_SERVER, &IID_ISpObjectToken,
                                  (void **)&tok);
    if (FAILED(hr)) return hr;
    hr = ISpObjectToken_SetId(tok, NULL, wid, FALSE);
    if (SUCCEEDED(hr)) hr = ISpVoice_SetVoice(v, tok);
    ISpObjectToken_Release(tok);
    return hr;
}

static void ctx_destroy(TtsCtx *c)
{
    if (c->speak_text != NULL) {
        secure_zero(c->speak_text, c->speak_len + 1);
        free(c->speak_text);
    }
    if (c->thread != NULL) CloseHandle(c->thread);
    if (c->cmd_evt != NULL) CloseHandle(c->cmd_evt);
    if (c->ready_evt != NULL) CloseHandle(c->ready_evt);
    if (c->cs_live) DeleteCriticalSection(&c->cs);
    free(c);
}

static DWORD WINAPI tts_thread(LPVOID arg)
{
    TtsCtx *c = (TtsCtx *)arg;
    ISpVoice *v = NULL;
    Reply reply;
    memset(&reply, 0, sizeof(reply));

    const int com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    dll_record_note("sapi.dll");   /* the only place SpVoice is created */
    HRESULT hr = com ? CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_INPROC_SERVER, &IID_ISpVoice, (void **)&v)
                     : (HRESULT)E_FAIL;
    HANDLE notify_h = NULL;
    if (SUCCEEDED(hr)) {
        const ULONGLONG want = SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_END_INPUT_STREAM);
        hr = ISpVoice_SetInterest(v, want, want);
        if (SUCCEEDED(hr)) hr = ISpVoice_SetNotifyWin32Event(v);
        if (SUCCEEDED(hr)) notify_h = ISpVoice_GetNotifyEventHandle(v);
        if (SUCCEEDED(hr) && notify_h == NULL) hr = E_FAIL;
    }
    if (FAILED(hr)) {
        if (v != NULL) { ISpVoice_Release(v); v = NULL; }
        c->started_ok = 0;
        post(c, VTT_ERROR, 0, 0, (long)hr);
    } else {
        const HRESULT vh = select_voice(v, c->voice_id);   /* failure keeps the default */
        c->started_ok = 1;
        post(c, VTT_READY, 0, 0, (long)vh);
    }
    SetEvent(c->ready_evt);

    int running = (v != NULL);
    while (running) {
        HANDLE hs[2] = { c->cmd_evt, notify_h };
        const DWORD w = WaitForMultipleObjects(2, hs, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0) {
            int quit, stop, have, vol;
            unsigned rid = 0;
            char *text = NULL;
            size_t tlen = 0, from = 0;
            EnterCriticalSection(&c->cs);
            quit = c->quit; stop = c->stop; have = c->have_speak; vol = c->volume;
            c->stop = 0; c->have_speak = 0; c->volume = -1;
            if (have) {
                rid = c->speak_reply; text = c->speak_text; tlen = c->speak_len; from = c->speak_from;
                c->speak_text = NULL; c->speak_len = 0;
            }
            LeaveCriticalSection(&c->cs);

            if (vol >= 0) (void)ISpVoice_SetVolume(v, (USHORT)vol);
            if (quit) {
                purge(v);
                reply_clear(&reply);
                running = 0;
            } else if (have) {
                purge(v);
                reply_clear(&reply);
                begin_reply(c, v, &reply, rid, text, tlen, from);
                if (c->auto_quit && reply.ch == NULL) running = 0;   /* DONE or ERROR already posted */
            } else if (stop) {
                purge(v);
                reply_clear(&reply);
                if (c->auto_quit) running = 0;   /* a stopped Play sample is done */
            }
            if (text != NULL) { secure_zero(text, tlen + 1); free(text); }
        } else if (w == WAIT_OBJECT_0 + 1) {
            if (pump_events(c, v, &reply) && c->auto_quit) running = 0;
        } else {
            running = 0;
        }
    }

    reply_clear(&reply);
    if (v != NULL) ISpVoice_Release(v);
    if (com) CoUninitialize();
    post(c, VTT_EXITED, 0, 0, 0);

    EnterCriticalSection(&c->cs);
    c->exited = 1;
    const int detached = c->detached;
    LeaveCriticalSection(&c->cs);
    if (detached) ctx_destroy(c);   /* the UI gave up waiting: nobody else will */
    return 0;
}

/* ---- UI-thread API ---- */

static TtsCtx *find(unsigned token)
{
    for (int i = 0; i < TTS_MAX_CTX; i++)
        if (g_ctx[i] != NULL && g_ctx[i]->token == token) return g_ctx[i];
    return NULL;
}

int voice_tts_start(HWND notify, unsigned token, const char *voice_id)
{
    if (!voice_gate_on()) return 0;
    TtsCtx *old = find(token);
    if (old != NULL) {
        EnterCriticalSection(&old->cs);
        const int gone = old->exited;
        LeaveCriticalSection(&old->cs);
        if (!gone) return 1;
        voice_tts_quit(token, 1000);   /* reap the exited one, then start afresh */
    }
    int slot = -1;
    for (int i = 0; i < TTS_MAX_CTX; i++) if (g_ctx[i] == NULL) { slot = i; break; }
    if (slot < 0) return 0;

    TtsCtx *c = (TtsCtx *)calloc(1, sizeof(*c));
    if (c == NULL) return 0;
    c->notify = notify;
    c->token = token;
    c->volume = -1;
    c->auto_quit = (token == VOICE_TTS_SAMPLE_TOKEN);
    if (voice_id != NULL) {
        strncpy(c->voice_id, voice_id, sizeof(c->voice_id) - 1);
        c->voice_id[sizeof(c->voice_id) - 1] = '\0';
    }
    InitializeCriticalSection(&c->cs);
    c->cs_live = 1;
    c->cmd_evt = CreateEventW(NULL, FALSE, FALSE, NULL);
    c->ready_evt = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (c->cmd_evt == NULL || c->ready_evt == NULL) { ctx_destroy(c); return 0; }
    c->thread = CreateThread(NULL, 0, tts_thread, c, 0, NULL);
    if (c->thread == NULL) { ctx_destroy(c); return 0; }
    g_ctx[slot] = c;
    voice_ledger_acquire(VRES_SPVOICE);
    return 1;
}

void voice_tts_speak(unsigned token, unsigned reply_id, const char *utf8, size_t from)
{
    TtsCtx *c = find(token);
    if (c == NULL || utf8 == NULL) return;
    EnterCriticalSection(&c->cs);
    const int gone = c->exited;
    LeaveCriticalSection(&c->cs);
    if (gone) { post(c, VTT_ERROR, reply_id, 0, (long)E_FAIL); return; }   /* never vanish silently */
    const size_t len = strlen(utf8);
    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) return;
    memcpy(copy, utf8, len + 1);
    EnterCriticalSection(&c->cs);
    if (c->speak_text != NULL) { secure_zero(c->speak_text, c->speak_len + 1); free(c->speak_text); }
    c->speak_text = copy;
    c->speak_len = len;
    c->speak_from = from;
    c->speak_reply = reply_id;
    c->have_speak = 1;
    LeaveCriticalSection(&c->cs);
    SetEvent(c->cmd_evt);
}

void voice_tts_stop(unsigned token)
{
    TtsCtx *c = find(token);
    if (c == NULL) return;
    EnterCriticalSection(&c->cs);
    c->stop = 1;
    c->have_speak = 0;   /* a speak not yet taken is part of what is purged */
    if (c->speak_text != NULL) { secure_zero(c->speak_text, c->speak_len + 1); free(c->speak_text); c->speak_text = NULL; c->speak_len = 0; }
    LeaveCriticalSection(&c->cs);
    SetEvent(c->cmd_evt);
}

void voice_tts_volume(unsigned token, int percent)
{
    TtsCtx *c = find(token);
    if (c == NULL) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    EnterCriticalSection(&c->cs);
    c->volume = percent;
    LeaveCriticalSection(&c->cs);
    SetEvent(c->cmd_evt);
}

void voice_tts_quit(unsigned token, unsigned wait_ms)
{
    int slot = -1;
    for (int i = 0; i < TTS_MAX_CTX; i++)
        if (g_ctx[i] != NULL && g_ctx[i]->token == token) { slot = i; break; }
    if (slot < 0) return;
    TtsCtx *c = g_ctx[slot];
    g_ctx[slot] = NULL;

    EnterCriticalSection(&c->cs);
    c->quit = 1;
    LeaveCriticalSection(&c->cs);
    SetEvent(c->cmd_evt);

    (void)WaitForSingleObject(c->thread, wait_ms);
    EnterCriticalSection(&c->cs);
    const int exited = c->exited;
    if (!exited) c->detached = 1;   /* the thread frees itself on its way out */
    LeaveCriticalSection(&c->cs);
    voice_ledger_release(VRES_SPVOICE);
    if (exited) {
        (void)WaitForSingleObject(c->thread, INFINITE);   /* it is past its last CS use */
        ctx_destroy(c);
    }
}

int voice_tts_running(void)
{
    for (int i = 0; i < TTS_MAX_CTX; i++) if (g_ctx[i] != NULL) return 1;
    return 0;
}

int voice_tts_sample(HWND notify, const char *voice_id)
{
    if (!voice_tts_start(notify, VOICE_TTS_SAMPLE_TOKEN, voice_id)) return 0;
    voice_tts_speak(VOICE_TTS_SAMPLE_TOKEN, 1, "Hello. This is how replies will sound.", 0);
    return 1;
}

int voice_tts_selftest_hold(void)
{
    if (!voice_tts_start(NULL, VOICE_TTS_SELFTEST_TOKEN, NULL)) return 0;
    TtsCtx *c = find(VOICE_TTS_SELFTEST_TOKEN);
    (void)WaitForSingleObject(c->ready_evt, 5000);
    if (!c->started_ok) {
        voice_tts_quit(VOICE_TTS_SELFTEST_TOKEN, 3000);
        return 0;
    }
    return 1;
}

void voice_tts_selftest_release(void)
{
    voice_tts_quit(VOICE_TTS_SELFTEST_TOKEN, 3000);
}
