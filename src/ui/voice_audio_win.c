#include "voice_audio_win.h"

#define COBJMACROS
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiosessiontypes.h>
#include <propidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef enum { VC_ENUM = 1, VC_BEGIN, VC_END, VC_WATCH, VC_SET_GAIN } VaCmdKind;
typedef struct {
    VaCmdKind kind;
    char      id[256];
    unsigned  token;
    int       flag;      /* test_only / keep / on */
    int       gain_db;   /* VC_BEGIN, VC_SET_GAIN */
    int       cap_ms;    /* VC_BEGIN: this capture's length cap, <= 0 = VOICE_CAP_MS */
    int       live;      /* VC_BEGIN: feed the live session */
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
} VaState;

static void va_close_client(VaState *st)
{
    if (st->client) IAudioClient_Stop(st->client);
    if (st->cap) { IAudioCaptureClient_Release(st->cap); st->cap = NULL; }
    if (st->client) { IAudioClient_Release(st->client); st->client = NULL; }
    if (st->buf_ev) { CloseHandle(st->buf_ev); st->buf_ev = NULL; }
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

/* Ends the capture: the client is stopped and released first (so the
 * device is closed between recordings), then the result is posted. */
static void va_finish(VaState *st, int keep)
{
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
static int va_open_dev(IMMDevice *dev, IAudioClient **out_cl,
                       IAudioCaptureClient **out_cap, HANDLE *out_ev, int *denied)
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
    if (SUCCEEDED(hr)) hr = IAudioClient_Start(cl);
    if (FAILED(hr) || !cap) {
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

static void va_begin(VaState *st, const VaCmd *c)
{
    if (st->active) va_finish(st, 0);
    if (!st->enm) { va_post_err(st->ctx, VAE_NO_DEVICE, c->token); return; }

    IAudioClient *cl = NULL;
    IAudioCaptureClient *cap = NULL;
    HANDLE ev = NULL;
    int ok = 0, denied = 0;
    st->used_default = 0;
    if (c->id[0]) {
        IMMDevice *dev = NULL;
        wchar_t wid[256];
        if (va_utf8_to_wide(c->id, wid, 256) &&
            SUCCEEDED(IMMDeviceEnumerator_GetDevice(st->enm, wid, &dev)) && dev) {
            DWORD state = 0;
            if (SUCCEEDED(IMMDevice_GetState(dev, &state)) && state == DEVICE_STATE_ACTIVE)
                ok = va_open_dev(dev, &cl, &cap, &ev, &denied);
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
            va_post_err(st->ctx, VAE_NO_DEVICE, c->token);
            return;
        }
        ok = va_open_dev(dev, &cl, &cap, &ev, &denied);
        IMMDevice_Release(dev);
        if (!ok) {
            va_post_err(st->ctx, denied ? VAE_ACCESS_DENIED : VAE_OPEN_FAILED, c->token);
            return;
        }
    }
    st->client = cl;
    st->cap = cap;
    st->buf_ev = ev;
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
    if (!st->test_only || now - st->t_last_level < VA_LEVEL_MS) return;
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
            case VC_END:   va_finish(&st, c.flag); break;
            case VC_WATCH: va_watch(&st, c.flag); break;
            case VC_SET_GAIN: va_set_gain(&st, c.gain_db); break;
            }
        }
        if (st.active) {
            ULONGLONG now = GetTickCount64();
            if (w == WAIT_OBJECT_0 + 2) st.t_last_ev = now;
            if (!va_drain(&st)) { va_fail(&st, VAE_DEVICE_LOST); continue; }
            /* The cap ends a push-to-talk capture here, so the device never
             * stays open past it whatever the UI does (a Test runs until stopped). */
            if (!st.test_only && st.token != 0 && pcm_full(&st.pcm)) {
                va_finish(&st, 1);
                continue;
            }
            va_level(&st, now);
            if (now - st.t_last_ev > VA_STALL_MS) { va_fail(&st, VAE_DEVICE_LOST); continue; }
        }
    }

    va_finish(&st, 0);
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

void voice_audio_free_devices(VoiceDeviceList *l) { free(l); }

void voice_audio_free_capture(VoiceCapture *c)
{
    if (!c) return;
    pcm_free(&c->pcm);
    free(c);
}
