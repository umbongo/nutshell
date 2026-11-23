/* voice_stt_win.c -- the speech-to-text upload worker. */

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voice_stt_win.h"
#include "ai_http.h"
#include "ai_netcancel.h"
#include "dlog.h"
#include "secure_zero.h"
#include "voice_gate.h"
#include "voice_stt.h"

#define STT_MAX_JOBS 8

typedef struct SttJob {
    HWND            notify;
    HANDLE          thread;
    AiNetCancel     nc;
    SttTarget       target;
    char            secret[CFG_STR_MAX];   /* the copied bearer token, wiped at the end */
    unsigned char  *wav;
    size_t          wav_len;
    unsigned        token;
} SttJob;

static SttJob *g_jobs[STT_MAX_JOBS];   /* UI thread only */

static void put_text(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src ? src : "");
}

/* RtlGenRandom (advapi32, already imported by the app): BCryptGenRandom would
 * add a bcrypt.dll import the exe does not have. The boundary only has to be
 * unguessable enough not to occur in the body. */
BOOLEAN WINAPI SystemFunction036(PVOID buf, ULONG len);

/* 32 hex characters of randomness as hex into out; 0 on failure. */
static int random_boundary(char *out, size_t cap)
{
    unsigned char r[16];
    static const char hex[] = "0123456789abcdef";
    const char *prefix = "NutshellBoundary";
    size_t pl = strlen(prefix);
    if (cap < pl + 33) return 0;
    if (!SystemFunction036(r, (ULONG)sizeof(r))) return 0;
    memcpy(out, prefix, pl);
    for (size_t i = 0; i < sizeof(r); i++) {
        out[pl + 2 * i]     = hex[r[i] >> 4];
        out[pl + 2 * i + 1] = hex[r[i] & 15];
    }
    out[pl + 32] = '\0';
    return 1;
}

static void post_result(SttJob *j, VoiceSttResult *r)
{
    r->token = j->token;
    if (!PostMessage(j->notify, WM_VOICE_STT, 0, (LPARAM)r))
        free(r);   /* the window is gone */
}

static void finish_fail(SttJob *j, const char *why)
{
    VoiceSttResult *r = (VoiceSttResult *)calloc(1, sizeof(*r));
    if (!r) return;
    r->ok = 0;
    put_text(r->err, sizeof(r->err), why);
    post_result(j, r);
}

static DWORD WINAPI stt_thread(LPVOID arg)
{
    SttJob *j = (SttJob *)arg;
    ULONGLONG t0 = GetTickCount64();
    char *body = NULL;
    size_t body_len = 0;
    char ctype[160];
    char boundary[64] = "";
    int status = 0;
    size_t resp_len = 0;

    if (!voice_gate_on()) {
        finish_fail(j, "Voice input is turned off.");
        goto done;
    }
    if (ai_nc_cancelled(&j->nc)) {
        finish_fail(j, "Cancelled");
        goto done;
    }

    if (j->target.shape == STT_SHAPE_GEMINI_CHAT) {
        body = voice_stt_gemini_body(j->wav, j->wav_len, j->target.model,
                                     j->target.language, &body_len);
        snprintf(ctype, sizeof(ctype), "application/json");
    } else {
        if (!random_boundary(boundary, sizeof(boundary))) {
            finish_fail(j, "Speech-to-text could not start (no random source).");
            goto done;
        }
        body = voice_stt_multipart(boundary, j->wav, j->wav_len, j->target.model,
                                   j->target.language, &body_len);
        snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", boundary);
    }
    /* The recording is in the body now: wipe it at once. */
    secure_zero(j->wav, j->wav_len);
    free(j->wav);
    j->wav = NULL;
    if (!body) {
        finish_fail(j, "Speech-to-text could not build the request.");
        goto done;
    }

    char auth[CFG_STR_MAX + 32];
    const char *headers[2] = { NULL, NULL };
    if (j->secret[0]) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", j->secret);
        headers[0] = auth;
    }

    /* Checked again right before sending: the gate may have gone off. */
    if (!voice_gate_on() || ai_nc_cancelled(&j->nc)) {
        secure_zero(auth, j->secret[0] ? sizeof(auth) : 0);
        finish_fail(j, ai_nc_cancelled(&j->nc) ? "Cancelled" : "Voice input is turned off.");
        goto done;
    }

    AiHttpResponse resp;
    int rc = ai_http_post_ct(j->target.url, headers, ctype, body, body_len, &resp, &j->nc);
    if (j->secret[0]) secure_zero(auth, sizeof(auth));
    secure_zero(body, body_len);
    free(body);
    body = NULL;
    status = resp.status_code;
    resp_len = resp.body_len;

    VoiceSttResult *r = (VoiceSttResult *)calloc(1, sizeof(*r));
    if (!r) { ai_http_response_free(&resp); goto done; }
    if (rc != 0 || resp.status_code < 200 || resp.status_code > 299) {
        if (rc != 0 && ai_nc_cancelled(&j->nc))
            put_text(r->err, sizeof(r->err), "Cancelled");
        else
            voice_stt_http_error(rc != 0 ? 0 : resp.status_code, resp.error,
                                 r->err, sizeof(r->err));
    } else if (voice_stt_parse(j->target.shape, resp.body ? resp.body : "", resp.body_len,
                               r->text, sizeof(r->text), r->err, sizeof(r->err))) {
        r->ok = 1;
        r->err[0] = '\0';
    }
    ai_http_response_free(&resp);
    DLOG(DLOG_AI, DLOG_INFO, 0, "stt.done",
         "shape=%d http=%d ok=%d sent_bytes=%zu reply_bytes=%zu ms=%llu",
         (int)j->target.shape, status, r->ok, body_len, resp_len,
         (unsigned long long)(GetTickCount64() - t0));
    if (r->ok)
        DLOG_CONTENT(DLOG_AI, DLOG_INFO, 0, "stt.text", "text", r->text, strlen(r->text));
    post_result(j, r);

done:
    if (body) { secure_zero(body, body_len); free(body); }
    if (j->wav) { secure_zero(j->wav, j->wav_len); free(j->wav); j->wav = NULL; }
    secure_zero(j->secret, sizeof(j->secret));
    return 0;
}

/* Free finished jobs' records. */
static void reap(void)
{
    for (int i = 0; i < STT_MAX_JOBS; i++) {
        SttJob *j = g_jobs[i];
        if (j && WaitForSingleObject(j->thread, 0) == WAIT_OBJECT_0) {
            CloseHandle(j->thread);
            free(j);
            g_jobs[i] = NULL;
        }
    }
}

static void drop_wav(unsigned char *wav, size_t n)
{
    if (!wav) return;
    secure_zero(wav, n);
    free(wav);
}

int voice_stt_submit(HWND notify, const Settings *s, unsigned char *wav, size_t wav_len,
                     unsigned token, char *err, size_t errcap)
{
    if (err && errcap) err[0] = '\0';
    if (!voice_gate_on()) {
        put_text(err, errcap, "Voice input is turned off.");
        drop_wav(wav, wav_len);
        return 0;
    }
    SttTarget t;
    if (!s || !voice_stt_resolve(s, &t, err, errcap)) {
        if (!s) put_text(err, errcap, "No settings.");
        drop_wav(wav, wav_len);
        return 0;
    }
    reap();
    int slot = -1;
    for (int i = 0; i < STT_MAX_JOBS; i++)
        if (!g_jobs[i]) { slot = i; break; }
    SttJob *j = slot >= 0 ? (SttJob *)calloc(1, sizeof(*j)) : NULL;
    if (!j) {
        put_text(err, errcap, slot < 0 ? "Speech-to-text is busy. Try again shortly."
                                       : "Out of memory.");
        drop_wav(wav, wav_len);
        return 0;
    }
    j->notify = notify;
    ai_nc_init(&j->nc);
    j->target = t;
    put_text(j->secret, sizeof(j->secret), t.key_from_chat ? s->ai_api_key : s->stt_api_key);
    j->wav = wav;
    j->wav_len = wav_len;
    j->token = token;
    j->thread = CreateThread(NULL, 0, stt_thread, j, 0, NULL);
    if (!j->thread) {
        put_text(err, errcap, "Speech-to-text could not start a thread.");
        secure_zero(j->secret, sizeof(j->secret));
        drop_wav(wav, wav_len);
        free(j);
        return 0;
    }
    g_jobs[slot] = j;
    return 1;
}

void voice_stt_cancel_all(void)
{
    HANDLE h[STT_MAX_JOBS];
    int idx[STT_MAX_JOBS];
    int n = 0;
    for (int i = 0; i < STT_MAX_JOBS; i++) {
        if (!g_jobs[i]) continue;
        ai_nc_fire(&g_jobs[i]->nc);
        h[n] = g_jobs[i]->thread;
        idx[n++] = i;
    }
    if (n == 0) return;
    /* The workers only PostMessage, so a plain wait cannot deadlock on us. */
    (void)WaitForMultipleObjects((DWORD)n, h, TRUE, 3000);
    for (int k = 0; k < n; k++) {
        SttJob *j = g_jobs[idx[k]];
        if (WaitForSingleObject(j->thread, 0) == WAIT_OBJECT_0) {
            CloseHandle(j->thread);
            free(j);
        } else {
            CloseHandle(j->thread);   /* stuck: detached; its record is left to it */
        }
        g_jobs[idx[k]] = NULL;
    }
}
