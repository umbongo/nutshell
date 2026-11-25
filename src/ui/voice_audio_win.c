#include "voice_audio_win.h"

#define COBJMACROS
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <audiosessiontypes.h>
#include <propidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dll_record.h"
#include "secure_zero.h"
#include "voice_config.h"
#include "voice_gate.h"
#include "voice_live_win.h"

/* The GUIDs and the property key are defined here, as copies: linking
 * libuuid/mmdevapi for them would add an import the exe must not have. */
static const GUID VA_CLSID_MMDeviceEnumerator =
    {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID VA_IID_IMMDeviceEnumerator =
    {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID VA_IID_IAudioClient =
    {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID VA_IID_IAudioCaptureClient =
    {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
static const GUID VA_IID_IMMNotificationClient =
    {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
static const GUID VA_IID_IUnknown =
    {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
/* Conversation capture (echo cancellation, ducking opt-out). Sources, all checked 2026-11-25:
 * IID_IAudioClient2, IID_IAudioSessionManager2, IID_IAudioSessionControl2: the mingw-w64
 * audioclient.h / audiopolicy.h in this toolchain (IAudioClient2, IAudioSessionControl2) and
 * the Windows SDK's (IAudioSessionManager2, 77AA99A0-...).
 * IID_IAudioEffectsManager 4460B3AE-4B44-4527-8676-7548A8ACD260 and
 * IID_IAcousticEchoCancellationControl F4AE25B5-AAA3-437D-B6B3-DBBE2D0E9549, with their vtable
 * orders and AUDIO_EFFECT: the Windows SDK audioclient.h as mirrored by the windows-rs 0.58.0
 * metadata (crates/libs/windows/src/Windows/Win32/Media/Audio/mod.rs); method names and the
 * Windows 11 (build 22000 / 22621) availability from learn.microsoft.com
 * (nn-audioclient-iaudioeffectsmanager, nn-audioclient-iacousticechocancellationcontrol).
 * AUDIO_EFFECT_TYPE_ACOUSTIC_ECHO_CANCELLATION 6f64adbe-8211-11e2-8c70-2c27d7f001fa: the "CLSIDs
 * for System Effects" table on learn.microsoft.com (audio-signal-processing-modes, KSMedia.h).
 * A wrong GUID fails safe: GetService gives E_NOINTERFACE and the conversation runs without
 * echo cancellation. */
static const GUID VA_IID_IAudioClient2 =
    {0x726778CD, 0xF60A, 0x4EDA, {0x82, 0xDE, 0xE4, 0x76, 0x10, 0xCD, 0x78, 0xAA}};
static const GUID VA_IID_IAudioSessionManager2 =
    {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
static const GUID VA_IID_IAudioSessionControl2 =
    {0xBFB7FF88, 0x7239, 0x4FC9, {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};
static const GUID VA_IID_IAudioEffectsManager =
    {0x4460B3AE, 0x4B44, 0x4527, {0x86, 0x76, 0x75, 0x48, 0xA8, 0xAC, 0xD2, 0x60}};
static const GUID VA_IID_IAcousticEchoCancellationControl =
    {0xF4AE25B5, 0xAAA3, 0x437D, {0xB6, 0xB3, 0xDB, 0xBE, 0x2D, 0x0E, 0x95, 0x49}};
static const GUID VA_EFFECT_AEC =
    {0x6F64ADBE, 0x8211, 0x11E2, {0x8C, 0x70, 0x2C, 0x27, 0xD7, 0xF0, 0x01, 0xFA}};

#ifndef __IAudioEffectsManager_INTERFACE_DEFINED__
#define __IAudioEffectsManager_INTERFACE_DEFINED__
typedef enum AUDIO_EFFECT_STATE { AUDIO_EFFECT_STATE_OFF = 0, AUDIO_EFFECT_STATE_ON = 1 } AUDIO_EFFECT_STATE;
typedef struct AUDIO_EFFECT { GUID id; BOOL canSetState; AUDIO_EFFECT_STATE state; } AUDIO_EFFECT;
typedef struct IAudioEffectsManager IAudioEffectsManager;
typedef struct IAudioEffectsManagerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IAudioEffectsManager *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IAudioEffectsManager *);
    ULONG (STDMETHODCALLTYPE *Release)(IAudioEffectsManager *);
    HRESULT (STDMETHODCALLTYPE *RegisterAudioEffectsChangedNotificationCallback)(IAudioEffectsManager *, void *);
    HRESULT (STDMETHODCALLTYPE *UnregisterAudioEffectsChangedNotificationCallback)(IAudioEffectsManager *, void *);
    HRESULT (STDMETHODCALLTYPE *GetAudioEffects)(IAudioEffectsManager *, AUDIO_EFFECT **, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *SetAudioEffectState)(IAudioEffectsManager *, GUID, AUDIO_EFFECT_STATE);
} IAudioEffectsManagerVtbl;
struct IAudioEffectsManager { const IAudioEffectsManagerVtbl *lpVtbl; };
#endif

#ifndef __IAcousticEchoCancellationControl_INTERFACE_DEFINED__
#define __IAcousticEchoCancellationControl_INTERFACE_DEFINED__
typedef struct IAcousticEchoCancellationControl IAcousticEchoCancellationControl;
typedef struct IAcousticEchoCancellationControlVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IAcousticEchoCancellationControl *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IAcousticEchoCancellationControl *);
    ULONG (STDMETHODCALLTYPE *Release)(IAcousticEchoCancellationControl *);
    HRESULT (STDMETHODCALLTYPE *SetEchoCancellationRenderEndpoint)(IAcousticEchoCancellationControl *, PCWSTR);
} IAcousticEchoCancellationControlVtbl;
struct IAcousticEchoCancellationControl { const IAcousticEchoCancellationControlVtbl *lpVtbl; };
#endif

/* PKEY_Device_FriendlyName */
static const PROPERTYKEY VA_PKEY_FriendlyName =
    {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

#define VA_CAP_SAMPLES  ((size_t)VOICE_CAP_MS * WAV_RATE / 1000u)
/* cap_ms in 16 kHz samples; <= 0 is the default cap. */
static size_t va_cap_samples(int cap_ms)
{
    return cap_ms > 0 ? (size_t)cap_ms * WAV_RATE / 1000u : VA_CAP_SAMPLES;
}
#define VA_STALL_MS     2000u
#define VA_LEVEL_MS     50u

typedef enum { VC_ENUM = 1, VC_BEGIN, VC_END, VC_WATCH, VC_SET_GAIN,
               VC_CONV_BEGIN, VC_CONV_MODE, VC_CONV_ATTACH, VC_CONV_END, VC_CONV_END_TURN,
               VC_TEST_CONV_OPEN, VC_TEST_CONV_INGEST } VaCmdKind;
typedef struct {
    VaCmdKind kind;
    char      id[256];
    unsigned  token;
    int       flag;      /* test_only / keep / on */
    int       gain_db;   /* VC_BEGIN, VC_SET_GAIN */
    int       cap_ms;    /* VC_BEGIN: this capture's length cap, <= 0 = VOICE_CAP_MS */
    int       pause_ms;  /* VC_CONV_BEGIN, VC_TEST_CONV_OPEN: the endpoint pause */
    int       live;      /* VC_BEGIN: feed the live session */
    unsigned  token2;    /* VC_CONV_ATTACH: the live session's token */
    int16_t  *data;      /* VC_TEST_CONV_INGEST: owned by the command, wiped and freed when run */
    size_t    n;
} VaCmd;

/* Everything one audio thread uses lives in its own context, so a thread
 * that outlives voice_audio_stop() (wedged in a driver call) can never touch
 * the next thread's state. The thread and the owner free it between them. */
typedef struct VaCtx VaCtx;
typedef struct { const IMMNotificationClientVtbl *lpVtbl; VaCtx *ctx; } VaNotify;
struct VaCtx {
    HANDLE           thread, stop, wake, ready;
    HWND             notify;
    CRITICAL_SECTION qcs;
    VaCmd           *q;          /* grown by va_push under qcs */
    int              qn, qcap;
    int              hold;       /* under qcs: test seam, the thread pops nothing */
    VaNotify         nc;
    int              exited;     /* under g_cs: the thread is done with ctx */
    int              orphaned;   /* under g_cs: stop() gave up waiting */
};

static VaCtx           *g_ctx;
static CRITICAL_SECTION g_cs;
static int              g_cs_ready;
static int              g_zombies;   /* orphaned threads still running; under g_cs */

static void va_ctx_free(VaCtx *c)
{
    if (c->thread) CloseHandle(c->thread);
    if (c->stop) CloseHandle(c->stop);
    if (c->wake) CloseHandle(c->wake);
    if (c->ready) CloseHandle(c->ready);
    DeleteCriticalSection(&c->qcs);
    for (int i = 0; i < c->qn; i++)          /* commands the thread never ran */
        if (c->q[i].data) { secure_zero(c->q[i].data, c->q[i].n * sizeof(int16_t)); free(c->q[i].data); }
    free(c->q);
    free(c);
}

/* ---- posting ---------------------------------------------------------- */

static void va_post(VaCtx *c, VoiceAudioMsg m, LPARAM lp)
{
    HWND h = c->notify;
    if (!h || !PostMessage(h, WM_VOICE_AUDIO, (WPARAM)m, lp)) {
        if (m == VAM_DEVICES) voice_audio_free_devices((VoiceDeviceList *)lp);
        if (m == VAM_CAPTURED) voice_audio_free_capture((VoiceCapture *)lp);
        if (m == VAM_CONV_TURN) voice_audio_free_capture((VoiceCapture *)lp);   /* the turn's audio: wiped */
    }
}

/* token: the push-to-talk recording the error belongs to, 0 for none. */
static void va_post_err(VaCtx *c, VoiceAudioErr e, unsigned token)
{
    va_post(c, VAM_ERROR, VOICE_ERR_PACK(e, token));   /* nothing to allocate or free */
}

/* ---- the notification client (a C vtable; callbacks only PostMessage) --- */

static HRESULT STDMETHODCALLTYPE nc_qi(IMMNotificationClient *self, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &VA_IID_IUnknown) ||
        IsEqualGUID(riid, &VA_IID_IMMNotificationClient)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE nc_addref(IMMNotificationClient *self) { (void)self; return 2; }
static ULONG STDMETHODCALLTYPE nc_release(IMMNotificationClient *self) { (void)self; return 1; }
static void nc_changed(IMMNotificationClient *self)
{ va_post(((VaNotify *)self)->ctx, VAM_DEVICES_CHANGED, 0); }
static HRESULT STDMETHODCALLTYPE nc_state(IMMNotificationClient *s, LPCWSTR id, DWORD st)
{ (void)id; (void)st; nc_changed(s); return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_added(IMMNotificationClient *s, LPCWSTR id)
{ (void)id; nc_changed(s); return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_removed(IMMNotificationClient *s, LPCWSTR id)
{ (void)id; nc_changed(s); return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_default(IMMNotificationClient *s, EDataFlow f, ERole r, LPCWSTR id)
{ (void)f; (void)r; (void)id; nc_changed(s); return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_prop(IMMNotificationClient *s, LPCWSTR id, const PROPERTYKEY key)
{ (void)s; (void)id; (void)key; return S_OK; }

static const IMMNotificationClientVtbl g_nc_vtbl = {
    nc_qi, nc_addref, nc_release, nc_state, nc_added, nc_removed, nc_default, nc_prop
};

/* ---- audio-thread state ---------------------------------------------- */

typedef struct {
    VaCtx               *ctx;
    IMMDeviceEnumerator *enm;
    int                  watching;
    /* a capture in progress */
    IAudioClient        *client;
    IAudioCaptureClient *cap;
    HANDLE               buf_ev;
    PcmBuf               pcm;
    unsigned             token;
    int                  test_only;
    int                  live;         /* feed voice_live_feed (never with test_only) */
    int                  used_default;
    ULONGLONG            t_start, t_last_ev, t_last_level;
    uint32_t             gain_q16;     /* applied to every new sample */
    int                  level_peak;   /* raw 0..32768, max since the last post */
    int                  raw_nonzero;  /* any non-zero sample seen before the gain */
    int                  active;
    struct VaConv       *conv;         /* a conversation capture (or the test seam's state) */
} VaState;

#ifdef NS_VOICE_WAV_SEAM
/* ---- the harness build's microphone (make harness-exe, never the release exe) ------------
 * --voice-wav=<file> (16 kHz mono 16-bit PCM) replaces WASAPI for every capture, dictation and
 * conversation: a 20 ms waitable timer stands in for the capture event (it is st->buf_ev, so the
 * thread loop, the stall check and the close all work unchanged), and va_drain feeds one 20 ms
 * chunk per tick from the file, then silence. The file is consumed once per process, so a
 * conversation that reopens its capture after Working does not replay the phrase. While a
 * capture is open "<file>.capture" holds "open", else "closed": the harness reads it to see that
 * no microphone is open while a tool card waits. */
static void va_ingest(VaState *st, const int16_t *src, size_t frames);
__attribute__((used)) static const char g_seam_marker[] = "ns-voice-wav-seam";
#define VA_SEAM_CHUNK 320u                 /* 20 ms at 16 kHz */
static char     g_seam_path[MAX_PATH];
static int16_t *g_seam_pcm;
static size_t   g_seam_n, g_seam_pos;
static int      g_seam_loaded, g_seam_open;
static ULONGLONG g_seam_t0, g_seam_sent;

static void va_seam_state(int open)
{
    char p[MAX_PATH + 16];
    (void)snprintf(p, sizeof(p), "%s.capture", g_seam_path);
    FILE *f = fopen(p, "w");
    if (f) { fputs(open ? "open\n" : "closed\n", f); fclose(f); }
    g_seam_open = open;
}

static uint32_t va_seam_u32(const unsigned char *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* Reads the file once; any fault leaves an empty clip (silence), never a crash. */
static void va_seam_load(void)
{
    if (g_seam_loaded) return;
    g_seam_loaded = 1;
    FILE *f = fopen(g_seam_path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = (len >= 44 && len < 64L * 1024 * 1024) ? (unsigned char *)malloc((size_t)len) : NULL;
    if (b && fread(b, 1, (size_t)len, f) == (size_t)len && memcmp(b, "RIFF", 4) == 0 &&
        memcmp(b + 8, "WAVE", 4) == 0) {
        size_t off = 12, ulen = (size_t)len;
        int fmt_ok = 0;
        while (off + 8 <= ulen) {
            size_t cl = va_seam_u32(b + off + 4);
            const unsigned char *cd = b + off + 8;
            if (cl > ulen - off - 8) cl = ulen - off - 8;   /* a streamed header's size: take what is there */
            if (memcmp(b + off, "fmt ", 4) == 0 && cl >= 16) {
                fmt_ok = cd[0] == 1 && cd[1] == 0 && cd[2] == 1 && cd[3] == 0 &&
                         va_seam_u32(cd + 4) == (uint32_t)WAV_RATE && cd[14] == 16 && cd[15] == 0;
            } else if (memcmp(b + off, "data", 4) == 0 && fmt_ok) {
                size_t n = cl / 2;
                g_seam_pcm = (int16_t *)malloc((n ? n : 1) * sizeof(int16_t));
                if (g_seam_pcm) {
                    for (size_t i = 0; i < n; i++)
                        g_seam_pcm[i] = (int16_t)(uint16_t)(cd[2 * i] | cd[2 * i + 1] << 8);
                    g_seam_n = n;
                }
                break;
            }
            off += 8 + cl + (cl & 1u);
        }
    }
    free(b);
    fclose(f);
}

void voice_audio_seam_set_wav(const char *path)
{
    free(g_seam_pcm);
    g_seam_pcm = NULL;
    g_seam_n = g_seam_pos = 0;
    g_seam_loaded = 0;
    (void)snprintf(g_seam_path, sizeof(g_seam_path), "%s", path ? path : "");
}

void voice_audio_seam_note_header(const char *text)
{
    char p[MAX_PATH + 16];
    if (!g_seam_path[0] || !text) return;
    (void)snprintf(p, sizeof(p), "%s.header", g_seam_path);
    FILE *f = fopen(p, "a");
    if (f) { fprintf(f, "%s\n", text); fclose(f); }
}

/* The seam's va_open_endpoint: the timer in place of the capture event; no device, no echo. */
static int va_seam_open(VaState *st, unsigned token)
{
    va_seam_load();
    HANDLE tm = CreateWaitableTimerA(NULL, FALSE, NULL);
    LARGE_INTEGER due;
    due.QuadPart = -200000;                /* 20 ms, relative, in 100 ns */
    if (!tm || !SetWaitableTimer(tm, &due, 20, NULL, NULL, FALSE)) {
        if (tm) CloseHandle(tm);
        va_post_err(st->ctx, VAE_OPEN_FAILED, token);
        return 0;
    }
    st->buf_ev = tm;
    st->used_default = 0;
    g_seam_t0 = GetTickCount64();
    g_seam_sent = 0;
    va_seam_state(1);
    return 1;
}

/* The seam's va_drain: every 20 ms chunk that is due, from the file, then zeros. */
static int va_seam_drain(VaState *st)
{
    ULONGLONG due = (GetTickCount64() - g_seam_t0) / 20u;
    int budget = 50;                       /* a stalled thread catches up in bounded bursts */
    while (g_seam_sent < due && budget-- > 0 && st->active) {
        int16_t chunk[VA_SEAM_CHUNK];
        memset(chunk, 0, sizeof(chunk));
        size_t n = g_seam_n > g_seam_pos ? g_seam_n - g_seam_pos : 0;
        if (n > VA_SEAM_CHUNK) n = VA_SEAM_CHUNK;
        if (n > 0) memcpy(chunk, g_seam_pcm + g_seam_pos, n * sizeof(int16_t));
        g_seam_pos += n;
        g_seam_sent++;
        va_ingest(st, chunk, VA_SEAM_CHUNK);
        secure_zero(chunk, sizeof(chunk));
    }
    return 1;
}
#endif /* NS_VOICE_WAV_SEAM */

/* The communications-category extras of a conversation capture; every object is released
 * when the capture closes (before the audio client). */
typedef struct {
    IAudioEffectsManager *fx;
    IAcousticEchoCancellationControl *aec;
    IAudioSessionManager2 *sm;
    IAudioSessionControl2 *duck;
} VaComms;

typedef struct VaConv {
    VoiceListen listen;
    VaComms     com;
    PcmBuf      turn;          /* this turn's audio; empty between turns */
    int         in_turn;
    int         live;          /* a live session is attached ... */
    unsigned    live_token;    /* ... under this token */
    int         echo;          /* 0 none, 1 on, 2 turned on by us */
} VaConv;

/* A turn is at most 2 minutes (the listener caps it) plus the pre-roll it replays. */
#define VA_CONV_TURN_SAMPLES ((size_t)120 * WAV_RATE + (size_t)VL_PREROLL * VVAD_WIN)

static void va_comms_release(VaComms *c)
{
    if (c->duck) { IAudioSessionControl2_Release(c->duck); c->duck = NULL; }
    if (c->sm) { IAudioSessionManager2_Release(c->sm); c->sm = NULL; }
    if (c->aec) { c->aec->lpVtbl->Release(c->aec); c->aec = NULL; }
    if (c->fx) { c->fx->lpVtbl->Release(c->fx); c->fx = NULL; }
}

static void va_close_client(VaState *st)
{
    if (st->client) IAudioClient_Stop(st->client);
    if (st->conv) va_comms_release(&st->conv->com);
    if (st->cap) { IAudioCaptureClient_Release(st->cap); st->cap = NULL; }
    if (st->client) { IAudioClient_Release(st->client); st->client = NULL; }
    if (st->buf_ev) { CloseHandle(st->buf_ev); st->buf_ev = NULL; }
#ifdef NS_VOICE_WAV_SEAM
    if (g_seam_open) va_seam_state(0);
#endif
}

/* The live feed, or the test seam's stand-in (NULL in product code). */
static void (*g_test_feed)(unsigned, const int16_t *, size_t);
static void (*g_test_feed_end)(unsigned);

static void va_live_feed(unsigned token, const int16_t *s, size_t n)
{
    if (g_test_feed) g_test_feed(token, s, n);
    else voice_live_feed(token, s, n);
}

static void va_live_end(unsigned token)
{
    if (g_test_feed_end) g_test_feed_end(token);
    else voice_live_feed_end(token);
}

/* ---- conversation capture helpers (audio thread) ----------------------- */

static void va_conv_wipe_turn(VaConv *cv)
{
    pcm_free(&cv->turn);                 /* secure wipe, free, reset */
    cv->in_turn = 0;
    cv->live = 0;                        /* the UI closes the live session itself, as for a discard */
}

/* Frees the conversation state: the audio is user content, wiped first. */
static void va_conv_drop(VaState *st)
{
    VaConv *cv = st->conv;
    if (!cv) return;
    st->conv = NULL;
    voice_listen_wipe(&cv->listen);
    pcm_free(&cv->turn);
    secure_zero(cv, sizeof(*cv));
    free(cv);
}

static void va_conv_post_event(VaState *st, VoiceListenEvent e)
{
    va_post(st->ctx, VAM_CONV_EVENT, VOICE_CONV_PACK((unsigned)e, st->token));
}

/* The finished turn (TURN_END / TURN_CAP): the live session is ended after the last samples,
 * then the capture is posted; ownership of the audio moves to the receiver. */
static void va_conv_turn_done(VaState *st)
{
    VaConv *cv = st->conv;
    if (!cv->in_turn) return;
    if (cv->live) { va_live_end(cv->live_token); cv->live = 0; }
    VoiceCapture *c = (VoiceCapture *)calloc(1, sizeof(*c));
    if (!c) {
        va_conv_wipe_turn(cv);
        va_post_err(st->ctx, VAE_NO_MEMORY, st->token);
        return;
    }
    c->pcm = cv->turn;                   /* ownership moves */
    memset(&cv->turn, 0, sizeof(cv->turn));
    cv->in_turn = 0;
    c->verdict = pcm_classify(c->pcm.s, c->pcm.n);
    c->used_default = st->used_default;
    c->token = st->token;
    va_post(st->ctx, VAM_CONV_TURN, (LPARAM)c);
}

static void va_conv_sink_audio(void *u, const int16_t *s, size_t n)
{
    VaState *st = (VaState *)u;
    VaConv *cv = st->conv;
    if (!cv || !cv->in_turn) return;
    size_t before = cv->turn.n;
    size_t took = pcm_append(&cv->turn, s, n);
    if (took > 0 && cv->live) va_live_feed(cv->live_token, cv->turn.s + before, took);
}

static void va_conv_sink_event(void *u, VoiceListenEvent e)
{
    VaState *st = (VaState *)u;
    VaConv *cv = st->conv;
    if (!cv) return;
    switch (e) {
    case VLN_TURN_START:
        pcm_free(&cv->turn);
        pcm_init(&cv->turn, VA_CONV_TURN_SAMPLES);
        cv->in_turn = 1;
        break;
    case VLN_TURN_END:
    case VLN_TURN_CAP:
        va_conv_post_event(st, e);
        va_conv_turn_done(st);
        return;
    case VLN_TURN_ABANDON:
        va_conv_wipe_turn(cv);
        break;
    default:
        break;
    }
    va_conv_post_event(st, e);
}

/* The one place conversation samples enter: the capture feed (and the test seam's) -- gain,
 * the mic pulse's peak, then the listener. The sink does the rest. */
static void va_conv_ingest(VaState *st, const int16_t *src, size_t frames)
{
    VaConv *cv = st->conv;
    VoiceListenSink k = { va_conv_sink_audio, va_conv_sink_event, st };
    int16_t buf[1600];
    while (cv && frames > 0) {
        size_t n = frames > 1600 ? 1600 : frames;
        memcpy(buf, src, n * sizeof(int16_t));
        pcm_apply_gain_q16(buf, n, st->gain_q16);
        int pk = (int)(pcm_peak(buf, n) * 32768.0 + 0.5);
        if (pk > st->level_peak) st->level_peak = pk;
        voice_listen_feed(&cv->listen, buf, n, &k);
        secure_zero(buf, n * sizeof(int16_t));
        src += n;
        frames -= n;
        cv = st->conv;
    }
}

/* Ends the capture: the client is stopped and released first (so the
 * device is closed between recordings), then the result is posted. */
static void va_finish(VaState *st, int keep)
{
    if (st->conv) {                      /* a conversation posts no capture: its turns went already */
        va_close_client(st);
        st->active = 0;
        va_conv_drop(st);
        return;
    }
    if (!st->active) return;
    va_close_client(st);
    st->active = 0;
    /* After the last samples, before the result: the session flushes its gate and queues
     * the end (a discard too: the UI closes the session itself). */
    if (st->live) { va_live_end(st->token); st->live = 0; }
    if (keep && !st->test_only) {
        VoiceCapture *c = (VoiceCapture *)calloc(1, sizeof(*c));
        if (!c) {
            pcm_free(&st->pcm);
            va_post_err(st->ctx, VAE_NO_MEMORY, st->token);
            return;
        }
        c->pcm = st->pcm;                    /* ownership moves */
        memset(&st->pcm, 0, sizeof(st->pcm));
        c->verdict = pcm_classify_raw(c->pcm.s, c->pcm.n, st->raw_nonzero);
        c->used_default = st->used_default;
        c->token = st->token;
        va_post(st->ctx, VAM_CAPTURED, (LPARAM)c);
    } else {
        pcm_free(&st->pcm);
    }
}

static void va_fail(VaState *st, VoiceAudioErr e)
{
    unsigned token = st->token;
    va_finish(st, 0);
    va_post_err(st->ctx, e, token);
}

static int va_utf8_to_wide(const char *u, wchar_t *w, int cap)
{
    return MultiByteToWideChar(CP_UTF8, 0, u, -1, w, cap) > 0;
}

/* Activate, initialise and start a client on dev. 1 and the three outputs
 * set, or 0 with nothing left open. */
/* The default render endpoint's id (for the echo reference) and the ducking opt-out for our
 * own speech's session (spec 7.3). Both are best effort. */
static void va_comms_render(IMMDeviceEnumerator *enm, VaComms *cm, int echo_ctl)
{
    IMMDevice *rd = NULL;
    if (!enm || FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enm, eRender, eConsole, &rd)) || !rd)
        return;
    LPWSTR id = NULL;
    if (echo_ctl && cm->aec && SUCCEEDED(IMMDevice_GetId(rd, &id)) && id)
        cm->aec->lpVtbl->SetEchoCancellationRenderEndpoint(cm->aec, id);
    if (id) CoTaskMemFree(id);
    if (SUCCEEDED(IMMDevice_Activate(rd, &VA_IID_IAudioSessionManager2, CLSCTX_ALL, NULL,
                                     (void **)&cm->sm)) && cm->sm) {
        IAudioSessionControl *sc = NULL;
        if (SUCCEEDED(IAudioSessionManager2_GetAudioSessionControl(cm->sm, NULL, 0, &sc)) && sc) {
            if (FAILED(IAudioSessionControl_QueryInterface(sc, &VA_IID_IAudioSessionControl2,
                                                           (void **)&cm->duck)))
                cm->duck = NULL;
            else
                IAudioSessionControl2_SetDuckingPreference(cm->duck, TRUE);
            IAudioSessionControl_Release(sc);
        }
    }
    IMMDevice_Release(rd);
}

/* After Initialize: the echo-cancellation effect and its render reference. Returns the
 * VAM_CONV_OPENED value: 0 none, 1 on, 2 turned on by us. */
static int va_comms_aec(IMMDeviceEnumerator *enm, IAudioClient *cl, VaComms *cm)
{
    int echo = 0;
    if (SUCCEEDED(IAudioClient_GetService(cl, &VA_IID_IAudioEffectsManager, (void **)&cm->fx)) && cm->fx) {
        AUDIO_EFFECT *fx = NULL;
        UINT32 nfx = 0;
        if (SUCCEEDED(cm->fx->lpVtbl->GetAudioEffects(cm->fx, &fx, &nfx)) && fx) {
            for (UINT32 i = 0; i < nfx; i++) {
                if (!IsEqualGUID(&fx[i].id, &VA_EFFECT_AEC)) continue;
                if (fx[i].state == AUDIO_EFFECT_STATE_ON) echo = 1;
                else if (fx[i].canSetState &&
                         SUCCEEDED(cm->fx->lpVtbl->SetAudioEffectState(cm->fx, fx[i].id,
                                                                       AUDIO_EFFECT_STATE_ON)))
                    echo = 2;
                break;
            }
            CoTaskMemFree(fx);
        }
    }
    int ctl = SUCCEEDED(IAudioClient_GetService(cl, &VA_IID_IAcousticEchoCancellationControl,
                                                (void **)&cm->aec)) && cm->aec;
    va_comms_render(enm, cm, ctl);
    return echo;
}

/* cm: a conversation capture's extras (NULL for dictation): the communications category before
 * Initialize, the echo-cancellation setup after it; *echo gets the VAM_CONV_OPENED value. */
static int va_open_dev(IMMDeviceEnumerator *enm, IMMDevice *dev, IAudioClient **out_cl,
                       IAudioCaptureClient **out_cap, HANDLE *out_ev, int *denied,
                       VaComms *cm, int *echo)
{
    IAudioClient *cl = NULL;
    IAudioCaptureClient *cap = NULL;
    HANDLE ev = NULL;
    HRESULT hr = IMMDevice_Activate(dev, &VA_IID_IAudioClient, CLSCTX_ALL, NULL,
                                    (void **)&cl);
    if (FAILED(hr) || !cl) {
        if (hr == E_ACCESSDENIED) *denied = 1;
        return 0;
    }

    if (cm) {                               /* failure is not fatal: no echo cancellation */
        IAudioClient2 *c2 = NULL;
        if (SUCCEEDED(IAudioClient_QueryInterface(cl, &VA_IID_IAudioClient2, (void **)&c2)) && c2) {
            AudioClientProperties props;
            memset(&props, 0, sizeof(props));
            props.cbSize = (UINT32)sizeof(props);
            props.bIsOffload = FALSE;
            props.eCategory = AudioCategory_Communications;
            props.Options = AUDCLNT_STREAMOPTIONS_NONE;
            IAudioClient2_SetClientProperties(c2, &props);
            IAudioClient2_Release(c2);
        }
    }
    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = WAV_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 2;
    wfx.nAvgBytesPerSec = WAV_RATE * 2u;
    hr = IAudioClient_Initialize(cl, AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            2000000, 0, &wfx, NULL);
    ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (SUCCEEDED(hr) && ev) hr = IAudioClient_SetEventHandle(cl, ev);
    else if (SUCCEEDED(hr)) hr = E_FAIL;   /* no event: keep a real failure (E_ACCESSDENIED) as it is */
    if (SUCCEEDED(hr))
        hr = IAudioClient_GetService(cl, &VA_IID_IAudioCaptureClient, (void **)&cap);
    if (SUCCEEDED(hr) && cm && echo) *echo = va_comms_aec(enm, cl, cm);
    if (SUCCEEDED(hr)) hr = IAudioClient_Start(cl);
    if (FAILED(hr) || !cap) {
        if (cm) va_comms_release(cm);
        if (hr == E_ACCESSDENIED) *denied = 1;
        if (cap) IAudioCaptureClient_Release(cap);
        IAudioClient_Release(cl);
        if (ev) CloseHandle(ev);
        return 0;
    }
    *out_cl = cl;
    *out_cap = cap;
    *out_ev = ev;
    return 1;
}

/* Opens the chosen endpoint (else the default capture one) into st->client/cap/buf_ev and sets
 * used_default; posts the error for `token` and returns 0 when nothing opened. cm: see
 * va_open_dev. */
static int va_open_endpoint(VaState *st, const char *id, unsigned token, VaComms *cm, int *echo)
{
#ifdef NS_VOICE_WAV_SEAM
    if (g_seam_path[0]) { (void)cm; (void)echo; (void)id; return va_seam_open(st, token); }
#endif
    if (!st->enm) { va_post_err(st->ctx, VAE_NO_DEVICE, token); return 0; }

    IAudioClient *cl = NULL;
    IAudioCaptureClient *cap = NULL;
    HANDLE ev = NULL;
    int ok = 0, denied = 0;
    st->used_default = 0;
    if (id[0]) {
        IMMDevice *dev = NULL;
        wchar_t wid[256];
        if (va_utf8_to_wide(id, wid, 256) &&
            SUCCEEDED(IMMDeviceEnumerator_GetDevice(st->enm, wid, &dev)) && dev) {
            DWORD state = 0;
            if (SUCCEEDED(IMMDevice_GetState(dev, &state)) && state == DEVICE_STATE_ACTIVE)
                ok = va_open_dev(st->enm, dev, &cl, &cap, &ev, &denied, cm, echo);
            IMMDevice_Release(dev);
        }
        /* Not found, not active or would not open: Windows default; the
         * stored ID is never touched. */
        if (!ok) { st->used_default = 1; denied = 0; }
    }
    if (!ok) {
        IMMDevice *dev = NULL;
        if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(st->enm, eCapture,
                                                               eConsole, &dev)) || !dev) {
            va_post_err(st->ctx, VAE_NO_DEVICE, token);
            return 0;
        }
        ok = va_open_dev(st->enm, dev, &cl, &cap, &ev, &denied, cm, echo);
        IMMDevice_Release(dev);
        if (!ok) {
            va_post_err(st->ctx, denied ? VAE_ACCESS_DENIED : VAE_OPEN_FAILED, token);
            return 0;
        }
    }
    st->client = cl;
    st->cap = cap;
    st->buf_ev = ev;
    return 1;
}

/* A command that is not the conversation's pre-empts it: the capture is closed and the UI is
 * told under the conversation's token (VAE_CONV_ENDED), so it never believes one is open. */
static void va_conv_preempted(VaState *st)
{
    unsigned token = st->token;
    va_finish(st, 0);
    va_post_err(st->ctx, VAE_CONV_ENDED, token);
}

static void va_begin(VaState *st, const VaCmd *c)
{
    if (st->conv) va_conv_preempted(st);
    else if (st->active) va_finish(st, 0);
    if (!va_open_endpoint(st, c->id, c->token, NULL, NULL)) return;
    pcm_init(&st->pcm, va_cap_samples(c->cap_ms));
    st->token = c->token;
    st->test_only = c->flag;
    st->live = c->live && !c->flag;
    st->active = 1;
    st->t_start = st->t_last_ev = st->t_last_level = GetTickCount64();
    st->level_peak = 0;
    st->raw_nonzero = 0;
    st->gain_q16 = pcm_gain_q16(c->gain_db);
}

/* ---- conversation capture commands ------------------------------------ */

/* A fresh conversation state (no device yet); NULL on OOM (error posted). */
static VaConv *va_conv_new(VaState *st, unsigned token, int pause_ms, int gain_db)
{
    if (st->conv) va_conv_preempted(st);
    else if (st->active) va_finish(st, 0);
    VaConv *cv = (VaConv *)calloc(1, sizeof(*cv));
    if (!cv) { va_post_err(st->ctx, VAE_NO_MEMORY, token); return NULL; }
    voice_listen_init(&cv->listen, voice_pause_clamp(pause_ms));
    voice_listen_set_mode(&cv->listen, VLM_LISTEN);   /* a capture starts listening; the glue moves it */
    st->token = token;
    st->test_only = 0;
    st->live = 0;
    st->level_peak = 0;
    st->raw_nonzero = 0;
    st->gain_q16 = pcm_gain_q16(gain_db);
    return cv;
}

static void va_conv_begin(VaState *st, const VaCmd *c)
{
    VaConv *cv = va_conv_new(st, c->token, c->pause_ms, c->gain_db);
    if (!cv) return;
    int echo = 0;
    st->conv = cv;                       /* va_finish / va_close_client release cv->com */
    if (!va_open_endpoint(st, c->id, c->token, &cv->com, &echo)) {
        va_conv_drop(st);
        return;
    }
    cv->echo = echo;
    st->active = 1;
    st->t_start = st->t_last_ev = st->t_last_level = GetTickCount64();
    va_post(st->ctx, VAM_CONV_OPENED, VOICE_CONV_PACK((unsigned)echo, c->token));
}

static int va_conv_is(const VaState *st, unsigned token)
{
    return st->conv && (st->token & VOICE_TOKEN_MASK) == (token & VOICE_TOKEN_MASK);
}

static void va_conv_set_mode(VaState *st, unsigned token, VoiceListenMode m)
{
    if (!va_conv_is(st, token)) return;
    VaConv *cv = st->conv;
    /* A cut-in has already moved the listener to LISTEN with the turn carrying on: the
     * controller's mode command for the same mode must not wipe that turn. */
    if (m == VLM_LISTEN && cv->listen.mode == VLM_LISTEN && voice_ep_in_turn(&cv->listen.ep))
        return;
    const int wiped = cv->in_turn;
    voice_listen_set_mode(&cv->listen, m);         /* abandons a turn in progress, silently */
    va_conv_wipe_turn(cv);                         /* so the audio held for it goes too */
    if (wiped) va_conv_post_event(st, VLN_TURN_ABANDON);   /* the controller must not wait for it */
}

static void va_conv_attach(VaState *st, unsigned token, unsigned live_token)
{
    if (!va_conv_is(st, token)) return;
    VaConv *cv = st->conv;
    cv->live = 1;
    cv->live_token = live_token;
    if (cv->in_turn && cv->turn.n > 0)     /* the turn so far, once; later windows follow as they come */
        va_live_feed(live_token, cv->turn.s, cv->turn.n);
}

static void va_conv_end(VaState *st, unsigned token)
{
    if (va_conv_is(st, token)) va_finish(st, 0);
}

/* The turn ends now, exactly as the endpointer's pause would end it: TURN_END, the live
 * session's end after the last samples, the audio posted. The listener is then reset so it
 * makes no second end for the same turn. A stale token does nothing; with no turn open the controller is told VLN_TURN_ABANDON. */
static void va_conv_end_turn(VaState *st, unsigned token)
{
    if (!va_conv_is(st, token)) return;
    if (!st->conv->in_turn) {            /* nothing to end: the controller must not wait for audio */
        va_conv_post_event(st, VLN_TURN_ABANDON);
        return;
    }
    va_conv_post_event(st, VLN_TURN_END);
    va_conv_turn_done(st);
    voice_listen_set_mode(&st->conv->listen, st->conv->listen.mode);   /* out of the turn */
}

/* The one place a gain command takes effect, for the thread and the seam. */
static void va_set_gain(VaState *st, int gain_db)
{
    st->gain_q16 = pcm_gain_q16(gain_db);
}

/* Takes freshly captured samples: appended, then the software gain applied to
 * exactly the new ones, so the meter, pcm_classify and pcm_to_wav all see the
 * gained signal. A Settings Test only measures: it folds the peak into the
 * pending level, then wipes the samples, so it never accumulates audio. */
static void va_ingest(VaState *st, const int16_t *src, size_t frames)
{
    if (st->conv) { va_conv_ingest(st, src, frames); return; }
    size_t before = st->pcm.n;
    size_t take = pcm_append(&st->pcm, src, frames);
    if (take == 0) return;
    int16_t *fresh = st->pcm.s + before;
    /* Decided on the raw samples: at -12 dB a quiet mic's +-1 noise becomes 0. */
    if (!st->raw_nonzero && pcm_any_nonzero(fresh, take)) st->raw_nonzero = 1;
    pcm_apply_gain_q16(fresh, take, st->gain_q16);
    if (st->live) va_live_feed(st->token, fresh, take);
    if (st->test_only) {
        int pk = (int)(pcm_peak(fresh, take) * 32768.0 + 0.5);
        if (pk > st->level_peak) st->level_peak = pk;
        secure_zero(fresh, take * sizeof(int16_t));
        st->pcm.n = before;
    }
}

/* Drains every packet. Returns 0 on a device error. */
static int va_drain(VaState *st)
{
#ifdef NS_VOICE_WAV_SEAM
    if (g_seam_path[0] && !st->cap) return va_seam_drain(st);
#endif
    for (;;) {
        UINT32 frames = 0;
        HRESULT hr = IAudioCaptureClient_GetNextPacketSize(st->cap, &frames);
        if (FAILED(hr)) return 0;
        if (frames == 0) return 1;
        BYTE *data = NULL;
        DWORD flags = 0;
        hr = IAudioCaptureClient_GetBuffer(st->cap, &data, &frames, &flags, NULL, NULL);
        if (FAILED(hr)) return 0;
        if (frames > 0) {
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
                int16_t zeros[480];
                memset(zeros, 0, sizeof(zeros));
                UINT32 left = frames;
                while (left > 0) {
                    UINT32 n = left > 480 ? 480 : left;
                    va_ingest(st, zeros, n);
                    left -= n;
                }
            } else {
                va_ingest(st, (const int16_t *)data, frames);
            }
        }
        IAudioCaptureClient_ReleaseBuffer(st->cap, frames);
    }
}

static void va_level(VaState *st, ULONGLONG now)
{
    if ((!st->test_only && !st->conv) || now - st->t_last_level < VA_LEVEL_MS) return;
    st->t_last_level = now;
    int pk = st->level_peak > 32768 ? 32768 : st->level_peak;
    st->level_peak = 0;
    va_post(st->ctx, VAM_LEVEL, (LPARAM)pk);
}

/* An enumeration failure is VAE_COM_FAILED, never a capture error, so the
 * Settings Test can tell it from its own result. */
static void va_enumerate(VaState *st)
{
    if (!st->enm) { va_post_err(st->ctx, VAE_COM_FAILED, 0); return; }
    IMMDeviceCollection *coll = NULL;
    if (FAILED(IMMDeviceEnumerator_EnumAudioEndpoints(st->enm, eCapture,
                                                      DEVICE_STATE_ACTIVE, &coll)) || !coll) {
        va_post_err(st->ctx, VAE_COM_FAILED, 0);
        return;
    }
    VoiceDeviceList *l = (VoiceDeviceList *)calloc(1, sizeof(*l));
    if (!l) { IMMDeviceCollection_Release(coll); va_post_err(st->ctx, VAE_NO_MEMORY, 0); return; }
    UINT cnt = 0;
    IMMDeviceCollection_GetCount(coll, &cnt);
    for (UINT i = 0; i < cnt && l->n < 32; i++) {
        IMMDevice *d = NULL;
        if (FAILED(IMMDeviceCollection_Item(coll, i, &d)) || !d) continue;
        LPWSTR id = NULL;
        if (SUCCEEDED(IMMDevice_GetId(d, &id)) && id && wcslen(id) < 256) {
            wcscpy(l->dev[l->n].id, id);
            snprintf(l->dev[l->n].name, sizeof(l->dev[l->n].name), "%s", "Microphone");
            IPropertyStore *ps = NULL;
            if (SUCCEEDED(IMMDevice_OpenPropertyStore(d, STGM_READ, &ps)) && ps) {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                if (SUCCEEDED(IPropertyStore_GetValue(ps, &VA_PKEY_FriendlyName, &pv)) &&
                    pv.vt == VT_LPWSTR && pv.pwszVal)
                    WideCharToMultiByte(CP_UTF8, 0, pv.pwszVal, -1, l->dev[l->n].name,
                                        (int)sizeof(l->dev[l->n].name), NULL, NULL);
                PropVariantClear(&pv);
                IPropertyStore_Release(ps);
            }
            l->dev[l->n].name[sizeof(l->dev[l->n].name) - 1] = '\0';
            l->n++;
        }
        if (id) CoTaskMemFree(id);
        IMMDevice_Release(d);
    }
    IMMDeviceCollection_Release(coll);
    va_post(st->ctx, VAM_DEVICES, (LPARAM)l);
}

static void va_watch(VaState *st, int on)
{
    if (!st->enm) return;
    IMMNotificationClient *nc = (IMMNotificationClient *)&st->ctx->nc;
    if (on && !st->watching) {
        if (SUCCEEDED(IMMDeviceEnumerator_RegisterEndpointNotificationCallback(st->enm, nc)))
            st->watching = 1;
    } else if (!on && st->watching) {
        IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(st->enm, nc);
        st->watching = 0;
    }
}

static int va_pop(VaCtx *cx, VaCmd *out)
{
    int got = 0;
    EnterCriticalSection(&cx->qcs);
    if (cx->qn > 0 && !cx->hold) {
        *out = cx->q[0];
        memmove(&cx->q[0], &cx->q[1], (size_t)(cx->qn - 1) * sizeof(VaCmd));
        cx->qn--;
        got = 1;
    }
    LeaveCriticalSection(&cx->qcs);
    return got;
}

static DWORD WINAPI va_thread(LPVOID arg)
{
    VaCtx *cx = (VaCtx *)arg;
    VaState st;
    memset(&st, 0, sizeof(st));
    st.ctx = cx;
    int com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    if (!com) {
        va_post_err(cx, VAE_COM_FAILED, 0);
    } else {
        dll_record_note("mmdevapi.dll");   /* the only place the enumerator is created */
        if (FAILED(CoCreateInstance(&VA_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                    &VA_IID_IMMDeviceEnumerator, (void **)&st.enm)))
            { st.enm = NULL; va_post_err(cx, VAE_COM_FAILED, 0); }
    }
    va_post(cx, VAM_READY, 0);
    SetEvent(cx->ready);

    for (;;) {
        HANDLE h[3] = { cx->stop, cx->wake, st.buf_ev };
        DWORD nh = st.active ? 3u : 2u;
        DWORD w = WaitForMultipleObjects(nh, h, FALSE, st.active ? 100u : INFINITE);
        if (w == WAIT_OBJECT_0) break;
        if (w != WAIT_OBJECT_0 + 1 && w != WAIT_OBJECT_0 + 2 && w != WAIT_TIMEOUT)
            break;                      /* WAIT_FAILED or anything unexpected: leave */
        VaCmd c;
        while (va_pop(cx, &c)) {
            switch (c.kind) {
            case VC_ENUM:  va_enumerate(&st); break;
            case VC_BEGIN: va_begin(&st, &c); break;
            case VC_END:
                if (st.conv) va_conv_preempted(&st);   /* a dictation end is not the conversation's */
                else va_finish(&st, c.flag);
                break;
            case VC_WATCH: va_watch(&st, c.flag); break;
            case VC_SET_GAIN: va_set_gain(&st, c.gain_db); break;
            case VC_CONV_BEGIN:  va_conv_begin(&st, &c); break;
            case VC_CONV_MODE:   va_conv_set_mode(&st, c.token, (VoiceListenMode)c.flag); break;
            case VC_CONV_ATTACH: va_conv_attach(&st, c.token, c.token2); break;
            case VC_CONV_END:    va_conv_end(&st, c.token); break;
            case VC_CONV_END_TURN: va_conv_end_turn(&st, c.token); break;
            case VC_TEST_CONV_OPEN:
                st.conv = va_conv_new(&st, c.token, c.pause_ms, 0);
                break;
            case VC_TEST_CONV_INGEST:
                if (va_conv_is(&st, c.token) && c.data) va_ingest(&st, c.data, c.n);
                break;
            }
            if (c.data) { secure_zero(c.data, c.n * sizeof(int16_t)); free(c.data); }
        }
        if (st.active) {
            ULONGLONG now = GetTickCount64();
            if (w == WAIT_OBJECT_0 + 2) st.t_last_ev = now;
            if (!va_drain(&st)) { va_fail(&st, VAE_DEVICE_LOST); continue; }
            /* The cap ends a push-to-talk capture here, so the device never
             * stays open past it whatever the UI does (a Test runs until stopped). */
            if (!st.test_only && !st.conv && st.token != 0 && pcm_full(&st.pcm)) {
                va_finish(&st, 1);
                continue;
            }
            va_level(&st, now);
            if (now - st.t_last_ev > VA_STALL_MS) { va_fail(&st, VAE_DEVICE_LOST); continue; }
        }
    }

    va_finish(&st, 0);              /* closes a conversation too (device, objects, turn audio) */
    va_watch(&st, 0);               /* unregistered here, never inside a callback */
    if (st.enm) IMMDeviceEnumerator_Release(st.enm);
    if (com) CoUninitialize();

    /* Hand the context back: free it here when stop() gave up on us. */
    EnterCriticalSection(&g_cs);
    cx->exited = 1;
    if (cx->orphaned) {
        g_zombies--;
        LeaveCriticalSection(&g_cs);
        va_ctx_free(cx);
    } else {
        LeaveCriticalSection(&g_cs);
    }
    return 0;
}

/* ---- public API (UI thread) ------------------------------------------ */

int voice_audio_start(HWND notify)
{
    if (!g_cs_ready) { InitializeCriticalSection(&g_cs); g_cs_ready = 1; }
    if (g_ctx) return 1;
    if (!voice_gate_on()) return 0;     /* voice off: no thread, no mmdevapi.dll */
    EnterCriticalSection(&g_cs);
    int zombie = g_zombies;
    LeaveCriticalSection(&g_cs);
    if (zombie) return 0;               /* a wedged thread is still out there */

    VaCtx *cx = (VaCtx *)calloc(1, sizeof(*cx));
    if (!cx) return 0;
    InitializeCriticalSection(&cx->qcs);
    cx->notify = notify;
    cx->nc.lpVtbl = &g_nc_vtbl;
    cx->nc.ctx = cx;
    cx->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    cx->wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    cx->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (cx->stop && cx->wake && cx->ready)
        cx->thread = CreateThread(NULL, 0, va_thread, cx, 0, NULL);
    if (!cx->thread) {
        va_ctx_free(cx);
        return 0;
    }
    g_ctx = cx;
    return 1;
}

void voice_audio_stop(void)
{
    VaCtx *cx = g_ctx;
    if (!cx) return;
    g_ctx = NULL;
    SetEvent(cx->stop);
    int joined = (WaitForSingleObject(cx->thread, 3000) == WAIT_OBJECT_0);
    EnterCriticalSection(&g_cs);
    if (joined || cx->exited) {
        LeaveCriticalSection(&g_cs);
        va_ctx_free(cx);
        return;
    }
    /* Wedged in a driver call: the thread owns its context from here and
     * frees it when it finally exits; start() refuses until then. */
    cx->notify = NULL;
    cx->orphaned = 1;
    g_zombies++;
    LeaveCriticalSection(&g_cs);
}

int voice_audio_wait_ready(DWORD ms)
{
    if (!g_ctx) return 0;
    return WaitForSingleObject(g_ctx->ready, ms) == WAIT_OBJECT_0;
}

static void va_push(VaCmdKind k, const char *id, unsigned token, int flag, int gain_db,
                    int cap_ms, int live)
{
    VaCtx *cx = g_ctx;
    if (!cx) return;
    EnterCriticalSection(&cx->qcs);
    /* An enumeration repeats nothing a queued one will not do, and only the
     * last watch state matters: both are coalesced. Begin and end are never
     * dropped: the queue grows for them. */
    int done = 0;
    if (k == VC_ENUM || k == VC_WATCH || k == VC_SET_GAIN) {
        /* A gain folds only into a gain queued after the last begin: one
         * ahead of it would be overwritten by that begin's own gain. */
        int from = 0;
        if (k == VC_SET_GAIN)
            for (int i = 0; i < cx->qn; i++)
                if (cx->q[i].kind == VC_BEGIN) from = i + 1;
        for (int i = from; i < cx->qn; i++) {
            if (cx->q[i].kind != k) continue;
            if (k == VC_WATCH) cx->q[i].flag = flag;
            if (k == VC_SET_GAIN) cx->q[i].gain_db = gain_db;
            done = 1;
            break;
        }
    }
    if (!done && cx->qn >= cx->qcap) {
        int ncap = cx->qcap ? cx->qcap * 2 : VOICE_AUDIO_QUEUE_MIN;
        VaCmd *nq = (VaCmd *)realloc(cx->q, (size_t)ncap * sizeof(VaCmd));
        if (nq) { cx->q = nq; cx->qcap = ncap; }
    }
    if (!done && cx->qn < cx->qcap) {
        VaCmd *c = &cx->q[cx->qn++];
        memset(c, 0, sizeof(*c));
        c->kind = k;
        if (id) snprintf(c->id, sizeof(c->id), "%s", id);
        c->token = token;
        c->flag = flag;
        c->gain_db = gain_db;
        c->cap_ms = cap_ms;
        c->live = live;
    }
    LeaveCriticalSection(&cx->qcs);
    SetEvent(cx->wake);
}

/* TEST-ONLY seam (wintest), never called by product code: while held, the audio thread leaves every queued command alone
 * (it still honours stop); releasing wakes it to drain them. */
void voice_audio_test_hold(int on)
{
    VaCtx *cx = g_ctx;
    if (!cx) return;
    EnterCriticalSection(&cx->qcs);
    cx->hold = on ? 1 : 0;
    LeaveCriticalSection(&cx->qcs);
    SetEvent(cx->wake);
}

/* TEST-ONLY seam (wintest), never called by product code: the queue's current capacity (0 without a thread). */
int voice_audio_queue_cap(void)
{
    VaCtx *cx = g_ctx;
    if (!cx) return 0;
    EnterCriticalSection(&cx->qcs);
    int n = cx->qcap;
    LeaveCriticalSection(&cx->qcs);
    return n;
}

void voice_audio_enumerate(void) { va_push(VC_ENUM, NULL, 0, 0, 0, 0, 0); }
void voice_audio_capture_begin(const char *id, unsigned token, int test_only, int gain_db,
                               int cap_ms, int live)
{ va_push(VC_BEGIN, id ? id : "", token, test_only ? 1 : 0, gain_db, cap_ms, live ? 1 : 0); }
void voice_audio_capture_end(int keep) { va_push(VC_END, NULL, 0, keep ? 1 : 0, 0, 0, 0); }
void voice_audio_watch_devices(int on) { va_push(VC_WATCH, NULL, 0, on ? 1 : 0, 0, 0, 0); }
void voice_audio_set_gain(int gain_db) { va_push(VC_SET_GAIN, NULL, 0, 0, gain_db, 0, 0); }

/* TEST-ONLY seam (wintest), never called by product code: how many gain
 * commands are queued (coalescing keeps it at most 1) and the last value. */
int voice_audio_test_pending_gain(int *last_db)
{
    VaCtx *cx = g_ctx;
    int n = 0;
    if (!cx) return 0;
    EnterCriticalSection(&cx->qcs);
    for (int i = 0; i < cx->qn; i++)
        if (cx->q[i].kind == VC_SET_GAIN) { n++; if (last_db) *last_db = cx->q[i].gain_db; }
    LeaveCriticalSection(&cx->qcs);
    return n;
}

/* TEST-ONLY seam (wintest), never called by product code: runs the audio
 * thread's ingest path on a private capture state, no device or thread. `s`
 * is fed twice, the gain set to gain_db first and gain2_db before the second
 * feed (the live change). Fills peak[2] (the level each feed contributes,
 * 0..32768), *pcm_len (samples kept) and *verdict (pcm_classify of what is
 * kept). */
void voice_audio_test_ingest(int test_only, int gain_db, int gain2_db,
                             const int16_t *s, size_t n, int peak[2],
                             size_t *pcm_len, int *verdict)
{
    VaState st;
    memset(&st, 0, sizeof(st));
    pcm_init(&st.pcm, VA_CAP_SAMPLES);
    st.test_only = test_only;
    st.gain_q16 = pcm_gain_q16(gain_db);
    va_ingest(&st, s, n);
    peak[0] = st.level_peak;
    st.level_peak = 0;
    va_set_gain(&st, gain2_db);   /* as a VC_SET_GAIN on the running capture */
    va_ingest(&st, s, n);
    peak[1] = st.level_peak;
    *pcm_len = st.pcm.n;
    *verdict = (int)pcm_classify_raw(st.pcm.s, st.pcm.n, st.raw_nonzero);
    pcm_free(&st.pcm);
}

/* TEST-ONLY seam (wintest), never called by product code. */
void voice_audio_test_live_hooks(void (*feed)(unsigned, const int16_t *, size_t),
                                 void (*end)(unsigned))
{
    g_test_feed = feed;
    g_test_feed_end = end;
}

/* TEST-ONLY seam (wintest), never called by product code: one capture's ingest and
 * finish on a private state, no device or thread. */
void voice_audio_test_ingest_live(int live, unsigned token, int gain_db, int cap_ms,
                                  const int16_t *s, size_t n)
{
    VaState st;
    memset(&st, 0, sizeof(st));
    pcm_init(&st.pcm, va_cap_samples(cap_ms));
    st.token = token;
    st.live = live ? 1 : 0;
    st.gain_q16 = pcm_gain_q16(gain_db);
    st.active = 1;
    va_ingest(&st, s, n);
    va_finish(&st, 0);          /* discards the samples; the live end still fires */
}

/* Appends a command as it is (no coalescing); the queue grows, nothing is dropped. Returns 0
 * with no thread or no memory; the caller then still owns c->data. */
static int va_push_raw(const VaCmd *c)
{
    VaCtx *cx = g_ctx;
    int ok = 0;
    if (!cx) return 0;
    EnterCriticalSection(&cx->qcs);
    if (cx->qn >= cx->qcap) {
        int ncap = cx->qcap ? cx->qcap * 2 : VOICE_AUDIO_QUEUE_MIN;
        VaCmd *nq = (VaCmd *)realloc(cx->q, (size_t)ncap * sizeof(VaCmd));
        if (nq) { cx->q = nq; cx->qcap = ncap; }
    }
    if (cx->qn < cx->qcap) { cx->q[cx->qn++] = *c; ok = 1; }
    LeaveCriticalSection(&cx->qcs);
    if (ok) SetEvent(cx->wake);
    return ok;
}

int voice_audio_conv_begin(const char *id, unsigned token, double gain_db, int pause_ms)
{
    VaCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = VC_CONV_BEGIN;
    snprintf(c.id, sizeof(c.id), "%s", id ? id : "");
    c.token = token;
    /* clamp the double first: a huge or NaN value must not reach the int conversion */
    if (!(gain_db >= (double)VOICE_GAIN_MIN_DB)) gain_db = (double)VOICE_GAIN_MIN_DB;
    if (gain_db > (double)VOICE_GAIN_MAX_DB) gain_db = (double)VOICE_GAIN_MAX_DB;
    c.gain_db = voice_gain_clamp((int)lround(gain_db));
    c.pause_ms = pause_ms;
    return va_push_raw(&c);
}

void voice_audio_conv_mode(unsigned token, VoiceListenMode m)
{
    VaCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = VC_CONV_MODE;
    c.token = token;
    c.flag = (int)m;
    va_push_raw(&c);
}

void voice_audio_conv_attach_live(unsigned token, unsigned live_token)
{
    VaCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = VC_CONV_ATTACH;
    c.token = token;
    c.token2 = live_token;
    va_push_raw(&c);
}

void voice_audio_conv_end_turn(unsigned token)
{
    VaCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = VC_CONV_END_TURN;
    c.token = token;
    va_push_raw(&c);
}

void voice_audio_conv_end(unsigned token)
{
    VaCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = VC_CONV_END;
    c.token = token;
    va_push_raw(&c);
}

/* TEST-ONLY seams (wintest), never called by product code: a conversation state with no
 * device on the audio thread, and samples fed to it through the capture feed. */
void voice_audio_test_conv_open(unsigned token, int pause_ms)
{
    VaCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = VC_TEST_CONV_OPEN;
    c.token = token;
    c.pause_ms = pause_ms;
    va_push_raw(&c);
}

void voice_audio_test_conv_ingest(unsigned token, const int16_t *s, size_t n)
{
    VaCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = VC_TEST_CONV_INGEST;
    c.token = token;
    c.n = n;
    c.data = (int16_t *)malloc(n * sizeof(int16_t));
    if (!c.data) return;
    memcpy(c.data, s, n * sizeof(int16_t));
    if (!va_push_raw(&c)) { secure_zero(c.data, n * sizeof(int16_t)); free(c.data); }
}

void voice_audio_free_devices(VoiceDeviceList *l) { free(l); }

void voice_audio_free_capture(VoiceCapture *c)
{
    if (!c) return;
    pcm_free(&c->pcm);
    free(c);
}
