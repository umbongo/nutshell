#include "voice_vad.h"
#include "voice_config.h"
#include <math.h>

/* Numbers: see the header comment (spec 5.3). */
#define VVAD_MARGIN_DB 12.0
#define VVAD_ABS_MIN_DB (-55.0)
#define VVAD_RISE_DB 0.05
#define VVAD_START_OF 8
#define VVAD_END_WINDOWS 15
#define VVAD_STATIONARY_SPAN_DB 4.0
#define VEP_CAP_WINDOWS 6000
#define VVAD_DIGITAL_SILENCE_DB (-85.0)
#define VVAD_FLOOR_MIN_DB (-75.0)
#define VVAD_IMPULSE_PEAK_WINDOWS 2
#define VVAD_IMPULSE_RISE_DB 3.0
#define VVAD_IMPULSE_TOP_DB 1.0   /* a flat top: the peak moves only on a rise this big */
#define VVAD_IMPULSE_DECAY_DB 6.0

double voice_window_db(const int16_t *s, size_t n)
{
    if (!s || n == 0) return -90.0;
    double acc = 0.0;
    for (size_t i = 0; i < n; i++) acc += (double)s[i] * (double)s[i];
    double rms = sqrt(acc / (double)n);
    if (rms <= 0.0) return -90.0;
    double db = 20.0 * log10(rms / 32768.0);
    return db < -90.0 ? -90.0 : db;
}

void voice_vad_init(VoiceVad *v)
{
    if (!v) return;
    for (int i = 0; i < VVAD_RING; i++) v->ring[i] = 0.0;
    v->extra_db = 0.0;
    v->hist = 0;
    v->ring_pos = 0;
    v->burst_windows = 0;
    v->nc_run = 0;
    v->in_burst = 0;
    v->frozen = 0;
    v->started = 0;
    for (int i = 0; i < 8; i++) v->lev[i] = -90.0;
    v->imp_pos = 0;
    v->imp_peak_pos = 0;
    v->imp_rose = 0;
    v->imp_peak = v->imp_prev = v->imp_last_cand = -90.0;
    v->floor_db = VVAD_FLOOR_MIN_DB;
}

void voice_vad_set_extra_db(VoiceVad *v, double extra_db) { if (v) v->extra_db = extra_db; }
void voice_vad_freeze_floor(VoiceVad *v, int frozen) { if (v) v->frozen = frozen ? 1 : 0; }
double voice_vad_floor_db(const VoiceVad *v) { return v ? v->floor_db : -90.0; }

static int popcount8(unsigned x)
{
    int n = 0;
    for (int i = 0; i < VVAD_START_OF; i++) n += (int)((x >> i) & 1u);
    return n;
}

static double clamp_floor(double db) { return db < VVAD_FLOOR_MIN_DB ? VVAD_FLOOR_MIN_DB : db; }

static void imp_feed(VoiceVad *v, double level, int cand)
{
    if (v->imp_pos == 0) {
        v->imp_peak = level;
        v->imp_peak_pos = 0;
        v->imp_rose = 0;
        v->imp_last_cand = level;
    } else {
        if (level > v->imp_peak + VVAD_IMPULSE_TOP_DB) { v->imp_peak = level; v->imp_peak_pos = v->imp_pos; }
        if (level >= v->imp_prev + VVAD_IMPULSE_RISE_DB) v->imp_rose = 1;
    }
    if (cand) v->imp_last_cand = level;
    v->imp_prev = level;
    v->imp_pos++;
}

static int imp_is_impulse(const VoiceVad *v)
{
    return v->imp_peak_pos < VVAD_IMPULSE_PEAK_WINDOWS && !v->imp_rose
        && v->imp_peak - v->imp_last_cand >= VVAD_IMPULSE_DECAY_DB;
}

VoiceVadOut voice_vad_window(VoiceVad *v, double level_db)
{
    if (!v) return VV_SILENCE;
    int quiet = level_db <= VVAD_DIGITAL_SILENCE_DB;
    for (int i = 0; i < 7; i++) v->lev[i] = v->lev[i + 1];
    v->lev[7] = level_db;
    if (!v->started) {
        if (quiet) { v->hist = (v->hist << 1) & 0xFFu; return VV_SILENCE; }
        v->started = 1;
        v->floor_db = clamp_floor(level_db);
    }
    int cand = !quiet && level_db >= v->floor_db + VVAD_MARGIN_DB + v->extra_db
            && level_db >= VVAD_ABS_MIN_DB;

    if (!v->frozen && !quiet) {
        if (level_db < v->floor_db) v->floor_db = clamp_floor(level_db);
        else if (!cand && !v->in_burst) {
            double up = v->floor_db + VVAD_RISE_DB;
            v->floor_db = level_db < up ? level_db : up;
        }
    }

    v->hist = ((v->hist << 1) | (cand ? 1u : 0u)) & 0xFFu;

    if (!v->in_burst) {
        if (popcount8(v->hist) >= VVAD_START_NEED) {
            v->in_burst = 1;
            v->burst_windows = 0;
            v->nc_run = 0;
            v->ring_pos = 0;
            v->imp_pos = 0;
            {
                int f = 0;   /* oldest candidate among the last 8 windows */
                while (f < 7 && !((v->hist >> (7 - f)) & 1u)) f++;
                for (int i = f; i < 7; i++)
                    imp_feed(v, v->lev[i], (int)((v->hist >> (7 - i)) & 1u));
            }
            /* fall through: this window is the first of the burst */
        } else {
            return VV_SILENCE;
        }
    }

    imp_feed(v, level_db, cand);
    v->ring[v->ring_pos] = level_db;
    v->ring_pos = (v->ring_pos + 1) % VVAD_RING;
    v->burst_windows++;
    v->nc_run = cand ? 0 : v->nc_run + 1;

    if (v->burst_windows >= VVAD_RING) {
        double lo = v->ring[0], hi = v->ring[0], sum = 0.0;
        for (int i = 0; i < VVAD_RING; i++) {
            if (v->ring[i] < lo) lo = v->ring[i];
            if (v->ring[i] > hi) hi = v->ring[i];
            sum += v->ring[i];
        }
        if (hi - lo < VVAD_STATIONARY_SPAN_DB) {
            v->floor_db = sum / (double)VVAD_RING;
            v->in_burst = 0;
            v->hist = 0;
            return VV_NOISE_RESET;
        }
    }

    if (v->nc_run >= VVAD_END_WINDOWS) {
        v->in_burst = 0;
        v->hist = 0;
        return imp_is_impulse(v) ? VV_IMPULSE : VV_SILENCE;
    }
    return cand ? VV_SPEECH : VV_SILENCE;
}

void voice_ep_init(VoiceEndpoint *e, int pause_ms)
{
    if (!e) return;
    if (pause_ms < VOICE_PAUSE_MIN_MS) pause_ms = VOICE_PAUSE_MIN_MS;
    if (pause_ms > VOICE_PAUSE_MAX_MS) pause_ms = VOICE_PAUSE_MAX_MS;
    e->pause_windows = pause_ms / 20;
    voice_ep_reset(e);
}

void voice_ep_reset(VoiceEndpoint *e)
{
    if (!e) return;
    e->in_turn = 0;
    e->since_speech = 0;
    e->speech_windows = 0;
    e->turn_windows = 0;
    e->burst_speech = 0;
}

int voice_ep_in_turn(const VoiceEndpoint *e) { return e && e->in_turn; }

VoiceEpOut voice_ep_window(VoiceEndpoint *e, VoiceVadOut w)
{
    if (!e) return VEP_NONE;
    if (!e->in_turn) {
        if (w != VV_SPEECH) return VEP_NONE;
        e->in_turn = 1;
        e->since_speech = 0;
        e->turn_windows = 0;
        e->speech_windows = VVAD_START_NEED;   /* the windows that opened the burst */
        e->burst_speech = VVAD_START_NEED;
        return VEP_START;
    }
    e->turn_windows++;
    if (w == VV_NOISE_RESET) { e->in_turn = 0; return VEP_ABANDON; }
    if (w == VV_SPEECH) {
        /* a burst ends after 15 non-candidates, so a speech window after a gap
         * that long opens a new burst */
        e->burst_speech = e->since_speech >= VVAD_END_WINDOWS ? 1 : e->burst_speech + 1;
        e->speech_windows++;
        e->since_speech = 0;
    } else {
        e->since_speech++;
        if (w == VV_IMPULSE) {   /* the burst that just ended was an impulse: not speech */
            e->speech_windows -= e->burst_speech;
            if (e->speech_windows < 0) e->speech_windows = 0;
            e->burst_speech = 0;
        }
    }
    if (e->turn_windows >= VEP_CAP_WINDOWS) { e->in_turn = 0; return VEP_CAP; }
    if (e->since_speech >= e->pause_windows) {
        e->in_turn = 0;
        return e->speech_windows * 20 >= VOICE_SHORT_TAP_MS ? VEP_END : VEP_ABANDON;
    }
    return VEP_NONE;
}
