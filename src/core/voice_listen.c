#include "voice_listen.h"
#include "secure_zero.h"
#include <string.h>

#define CI_DUCK_WINDOWS 10      /* 200 ms */
#define CI_CUT_WINDOWS 20       /* 400 ms */
#define CI_GAP_MAX 2            /* non-speech windows allowed between cut-in windows */
#define CI_ABANDON_WINDOWS 15   /* 300 ms without speech */
#define AEC_EXTRA_DB 6.0
#define HD_FLOOR_MARGIN 12.0
#define HD_ECHO_MARGIN 10.0
#define HD_UP_DB 0.5
#define HD_DOWN_DB 0.1
#define HD_MIN_DB (-55.0)

static void ev(const VoiceListenSink *k, VoiceListenEvent e)
{
    if (k && k->event) k->event(k->u, e);
}

static void au(const VoiceListenSink *k, const int16_t *s)
{
    if (k && k->audio) k->audio(k->u, s, VVAD_WIN);
}

static void ring_clear(VoiceListen *l)
{
    secure_zero(l->ring, sizeof l->ring);
    l->ring_n = 0;
    l->ring_head = 0;
}

static void ring_push(VoiceListen *l, const int16_t *w)
{
    int idx;
    if (l->ring_n < VL_PREROLL) {
        idx = (l->ring_head + l->ring_n) % VL_PREROLL;
        l->ring_n++;
    } else {
        idx = l->ring_head;
        l->ring_head = (l->ring_head + 1) % VL_PREROLL;
    }
    memcpy(l->ring[idx], w, sizeof l->ring[idx]);
}

static void ring_flush(VoiceListen *l, const VoiceListenSink *k)
{
    for (int i = 0; i < l->ring_n; i++) au(k, l->ring[(l->ring_head + i) % VL_PREROLL]);
    ring_clear(l);
}

static void ci_reset(VoiceListen *l)
{
    l->ci_speech = l->ci_gap = l->ci_total = 0;
    l->ci_started = l->ci_ducked = 0;
}

static void apply_mode(VoiceListen *l)
{
    voice_vad_set_extra_db(&l->vad, l->mode == VLM_SPEAK_AEC ? AEC_EXTRA_DB : 0.0);
    voice_vad_freeze_floor(&l->vad, l->mode == VLM_SPEAK_AEC || l->mode == VLM_SPEAK_HD);
}

void voice_listen_init(VoiceListen *l, int pause_ms)
{
    memset(l, 0, sizeof *l);
    voice_vad_init(&l->vad);
    voice_ep_init(&l->ep, pause_ms);
    l->mode = VLM_HOLD;
    apply_mode(l);
}

void voice_listen_set_mode(VoiceListen *l, VoiceListenMode m)
{
    if (!l) return;
    l->mode = m;
    voice_ep_reset(&l->ep);
    ring_clear(l);
    ci_reset(l);
    l->hd_win = 0;
    l->echo_db = voice_vad_floor_db(&l->vad);
    apply_mode(l);
}

/* Window w becomes a turn the endpointer carries on (cut-in stage 2). */
static void enter_listen_turn(VoiceListen *l)
{
    l->mode = VLM_LISTEN;
    apply_mode(l);
    l->ep.in_turn = 1;
    l->ep.since_speech = l->ci_gap;
    l->ep.speech_windows = l->ci_speech;
    l->ep.turn_windows = l->ci_total;
    l->ep.burst_speech = l->ci_speech;
    ci_reset(l);
}

static void win_listen(VoiceListen *l, const int16_t *w, double level, const VoiceListenSink *k)
{
    VoiceVadOut o = voice_vad_window(&l->vad, level);
    int was = voice_ep_in_turn(&l->ep);
    VoiceEpOut r = voice_ep_window(&l->ep, o);
    if (!was) {
        ring_push(l, w);
        if (r == VEP_START) {
            ev(k, VLN_TURN_START);
            ring_flush(l, k);
        }
        return;
    }
    au(k, w);
    if (r == VEP_END) ev(k, VLN_TURN_END);
    else if (r == VEP_CAP) ev(k, VLN_TURN_CAP);
    else if (r == VEP_ABANDON) ev(k, VLN_TURN_ABANDON);
}

/* Shared cut-in counting. speech: this window is cut-in speech; credit: windows
 * it stands for on the first one. hd: no turn until the cut. Returns 1 when the
 * mode has become LISTEN (the window was handled). */
static void win_cutin(VoiceListen *l, const int16_t *w, int speech, int credit, int reset,
                      int hd, const VoiceListenSink *k)
{
    if (!l->ci_started && !l->ci_ducked) {
        if (!hd) ring_push(l, w);
        if (reset) { ci_reset(l); return; }
    }
    if (speech) {
        if (l->ci_speech == 0) l->ci_total = credit;
        else l->ci_total++;
        l->ci_speech += l->ci_speech == 0 ? credit : 1;
        l->ci_gap = 0;
    } else if (l->ci_speech > 0) {
        l->ci_gap++;
        l->ci_total++;
    }

    if (l->ci_ducked) {
        if (!hd && l->ci_started) au(k, w);
        if (reset || l->ci_gap >= CI_ABANDON_WINDOWS) {
            ev(k, VLN_UNDUCK);
            if (!hd) ev(k, VLN_TURN_ABANDON);
            ci_reset(l);
            ring_clear(l);
            return;
        }
        if (l->ci_speech >= CI_CUT_WINDOWS) {
            ev(k, VLN_CUT);
            if (hd) {
                ev(k, VLN_TURN_START);
                au(k, w);
            }
            enter_listen_turn(l);
        }
        return;
    }
    if (l->ci_speech == 0) return;
    if (l->ci_gap > CI_GAP_MAX) { ci_reset(l); return; }
    if (l->ci_speech >= CI_DUCK_WINDOWS) {
        l->ci_ducked = 1;
        ev(k, VLN_DUCK);
        if (!hd) {
            l->ci_started = 1;
            ev(k, VLN_TURN_START);
            ring_flush(l, k);
        }
    }
}

static void win_aec(VoiceListen *l, const int16_t *w, double level, const VoiceListenSink *k)
{
    VoiceVadOut o = voice_vad_window(&l->vad, level);
    win_cutin(l, w, o == VV_SPEECH, VVAD_START_NEED, o == VV_NOISE_RESET, 0, k);
}

static void win_hd(VoiceListen *l, const int16_t *w, double level, const VoiceListenSink *k)
{
    double thr = voice_vad_floor_db(&l->vad) + HD_FLOOR_MARGIN;
    int cand;
    if (l->hd_win < VL_HD_PRIME) {
        l->hd_win++;
        if (level > l->echo_db) l->echo_db = level;
    }
    if (l->echo_db + HD_ECHO_MARGIN > thr) thr = l->echo_db + HD_ECHO_MARGIN;
    cand = level >= thr && level >= HD_MIN_DB;
    if (!cand) {
        if (level > l->echo_db) {
            l->echo_db += level - l->echo_db > HD_UP_DB ? HD_UP_DB : level - l->echo_db;
        } else {
            l->echo_db -= l->echo_db - level > HD_DOWN_DB ? HD_DOWN_DB : l->echo_db - level;
        }
    }
    win_cutin(l, w, cand, 1, 0, 1, k);
}

static void process(VoiceListen *l, const int16_t *w, const VoiceListenSink *k)
{
    double level = voice_window_db(w, VVAD_WIN);
    switch (l->mode) {
    case VLM_HOLD:
        (void)voice_vad_window(&l->vad, level);
        break;
    case VLM_LISTEN:
        win_listen(l, w, level, k);
        break;
    case VLM_SPEAK_AEC:
        win_aec(l, w, level, k);
        break;
    case VLM_SPEAK_HD:
        win_hd(l, w, level, k);
        break;
    }
}

void voice_listen_feed(VoiceListen *l, const int16_t *s, size_t n, const VoiceListenSink *k)
{
    if (!l || !s) return;
    while (n > 0) {
        if (l->part_n == 0 && n >= VVAD_WIN) {
            process(l, s, k);
            s += VVAD_WIN;
            n -= VVAD_WIN;
            continue;
        }
        size_t take = VVAD_WIN - l->part_n;
        if (take > n) take = n;
        memcpy(l->part + l->part_n, s, take * sizeof *s);
        l->part_n += take;
        s += take;
        n -= take;
        if (l->part_n == VVAD_WIN) {
            process(l, l->part, k);
            secure_zero(l->part, sizeof l->part);
            l->part_n = 0;
        }
    }
}

void voice_listen_wipe(VoiceListen *l)
{
    if (!l) return;
    ring_clear(l);
    secure_zero(l->part, sizeof l->part);
    l->part_n = 0;
}
