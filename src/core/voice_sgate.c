#include "voice_sgate.h"

#include <string.h>

#include "secure_zero.h"
#include "wav.h"

void voice_sgate_reset(VoiceSgate *g)
{
    secure_zero(g->ring, sizeof g->ring);
    secure_zero(g->part, sizeof g->part);
    g->npart = 0;
    g->ring_head = 0;
    g->ring_count = 0;
    g->open = 0;
    g->speech_run = 0;
    g->silence_run = 0;
}

void voice_sgate_init(VoiceSgate *g, int threshold_rms, int hangover_ms)
{
    g->threshold_rms = threshold_rms;
    g->hangover_windows = (hangover_ms + 19) / 20;
    if (g->hangover_windows < 1) g->hangover_windows = 1;
    voice_sgate_reset(g);
}

int voice_sgate_open(const VoiceSgate *g) { return g->open; }

static void emit(const VoiceSgateSink *k, const int16_t *s, size_t n)
{
    if (k && k->audio && n) k->audio(k->u, s, n);
}

static void ring_push(VoiceSgate *g, const int16_t *w)
{
    size_t slot;
    if (g->ring_count < VSG_PREROLL_WINDOWS) {
        slot = (g->ring_head + g->ring_count) % VSG_PREROLL_WINDOWS;
        g->ring_count++;
    } else {
        slot = g->ring_head;
        g->ring_head = (g->ring_head + 1) % VSG_PREROLL_WINDOWS;
    }
    memcpy(g->ring[slot], w, sizeof g->ring[slot]);
}

static void ring_clear(VoiceSgate *g)
{
    secure_zero(g->ring, sizeof g->ring);
    g->ring_head = 0;
    g->ring_count = 0;
}

static void window(VoiceSgate *g, const int16_t *w, const VoiceSgateSink *k)
{
    int speech = pcm_rms(w, VSG_WIN) * 32768.0 >= (double)g->threshold_rms;

    if (!g->open) {
        ring_push(g, w);
        g->speech_run = speech ? g->speech_run + 1 : 0;
        if (g->speech_run >= VSG_OPEN_WINDOWS) {
            g->open = 1;
            g->silence_run = 0;
            g->speech_run = 0;
            for (size_t i = 0; i < g->ring_count; i++)
                emit(k, g->ring[(g->ring_head + i) % VSG_PREROLL_WINDOWS], VSG_WIN);
            ring_clear(g);
        }
        return;
    }

    emit(k, w, VSG_WIN);
    g->silence_run = speech ? 0 : g->silence_run + 1;
    if (g->silence_run >= g->hangover_windows) {
        g->open = 0;
        g->silence_run = 0;
        g->speech_run = 0;
        if (k && k->closed) k->closed(k->u);
    }
}

void voice_sgate_feed(VoiceSgate *g, const int16_t *s, size_t n, const VoiceSgateSink *k)
{
    while (n > 0) {
        if (g->npart == 0 && n >= VSG_WIN) {
            window(g, s, k);
            s += VSG_WIN;
            n -= VSG_WIN;
            continue;
        }
        size_t take = VSG_WIN - g->npart;
        if (take > n) take = n;
        memcpy(g->part + g->npart, s, take * sizeof *s);
        g->npart += take;
        s += take;
        n -= take;
        if (g->npart == VSG_WIN) {
            g->npart = 0;
            window(g, g->part, k);
        }
    }
}

void voice_sgate_flush(VoiceSgate *g, const VoiceSgateSink *k)
{
    int was_open = g->open;
    if (was_open) emit(k, g->part, g->npart);
    voice_sgate_reset(g);
    if (was_open && k && k->closed) k->closed(k->u);
}
