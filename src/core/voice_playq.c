#include "voice_playq.h"

#include <stdlib.h>

#include "secure_zero.h"

#define ENDED_MAX 16

typedef struct Blk {
    struct Blk *next;
    unsigned reply_id, chunk;
    int16_t *samples;
    size_t n, read;
    int is_end;
} Blk;

struct VoicePlayQ {
    Blk *head, *tail;
    int has_min;
    unsigned min_reply;
    uint32_t gain_q16;
    uint64_t queued;
    unsigned ended[ENDED_MAX];   /* ring of reached end markers */
    int ended_first, ended_count;
};

static int before(unsigned a, unsigned b) { return (int)(a - b) < 0; }

static void blk_free(Blk *b)
{
    if (b->samples) {
        secure_zero(b->samples, b->n * sizeof(int16_t));
        free(b->samples);
    }
    free(b);
}

static int stale(const VoicePlayQ *q, unsigned reply_id)
{
    return q->has_min && before(reply_id, q->min_reply);
}

static void append(VoicePlayQ *q, Blk *b)
{
    if (q->tail) q->tail->next = b; else q->head = b;
    q->tail = b;
}

static void clear(VoicePlayQ *q)
{
    while (q->head) {
        Blk *b = q->head;
        q->head = b->next;
        blk_free(b);
    }
    q->tail = NULL;
    q->queued = 0;
    q->ended_first = q->ended_count = 0;
}

VoicePlayQ *voice_playq_new(void)
{
    VoicePlayQ *q = (VoicePlayQ *)calloc(1, sizeof *q);
    if (q) q->gain_q16 = 65536u;
    return q;
}

void voice_playq_free(VoicePlayQ *q)
{
    if (!q) return;
    clear(q);
    free(q);
}

int voice_playq_push(VoicePlayQ *q, unsigned reply_id, unsigned chunk,
                     int16_t *s, size_t n)
{
    Blk *b;
    if (!s) return 0;
    if (!q || n == 0 || stale(q, reply_id) || !(b = (Blk *)calloc(1, sizeof *b))) {
        if (n) secure_zero(s, n * sizeof(int16_t));
        free(s);
        return 0;
    }
    b->reply_id = reply_id;
    b->chunk = chunk;
    b->samples = s;
    b->n = n;
    append(q, b);
    q->queued += n;
    return 1;
}

void voice_playq_end(VoicePlayQ *q, unsigned reply_id)
{
    Blk *b;
    if (!q || stale(q, reply_id)) return;
    b = (Blk *)calloc(1, sizeof *b);
    if (!b) return;
    b->reply_id = reply_id;
    b->is_end = 1;
    append(q, b);
}

void voice_playq_flush(VoicePlayQ *q, unsigned min_reply)
{
    if (!q) return;
    clear(q);
    q->has_min = 1;
    q->min_reply = min_reply;
}

void voice_playq_set_gain(VoicePlayQ *q, int percent)
{
    if (!q) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    q->gain_q16 = (uint32_t)percent * 65536u / 100u;
}

static void note_end(VoicePlayQ *q, unsigned id)
{
    if (q->ended_count == ENDED_MAX) {   /* full: drop the oldest */
        q->ended_first = (q->ended_first + 1) % ENDED_MAX;
        q->ended_count--;
    }
    q->ended[(q->ended_first + q->ended_count) % ENDED_MAX] = id;
    q->ended_count++;
}

/* Pop end markers sitting at the head. */
static void pass_ends(VoicePlayQ *q)
{
    while (q->head && q->head->is_end) {
        Blk *b = q->head;
        q->head = b->next;
        if (!q->head) q->tail = NULL;
        note_end(q, b->reply_id);
        blk_free(b);
    }
}

size_t voice_playq_read(VoicePlayQ *q, int16_t *out, size_t frames, VoicePlayPos *pos)
{
    size_t got = 0;
    if (!q) return 0;
    if (!out) frames = 0;
    pass_ends(q);
    while (got < frames && q->head) {
        Blk *b = q->head;
        size_t take = b->n - b->read, i;
        if (take > frames - got) take = frames - got;
        for (i = 0; i < take; i++) {
            int64_t v = ((int64_t)b->samples[b->read + i] * (int64_t)q->gain_q16) >> 16;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[got + i] = (int16_t)v;
        }
        b->read += take;
        got += take;
        q->queued -= take;
        if (pos) {
            pos->reply_id = b->reply_id;
            pos->chunk = b->chunk;
            pos->frames_in_chunk = b->read;
        }
        if (b->read == b->n) {
            q->head = b->next;
            if (!q->head) q->tail = NULL;
            blk_free(b);
        }
        pass_ends(q);
    }
    return got;
}

int voice_playq_take_end(VoicePlayQ *q, unsigned *reply_id)
{
    if (!q || q->ended_count == 0) return 0;
    if (reply_id) *reply_id = q->ended[q->ended_first];
    q->ended_first = (q->ended_first + 1) % ENDED_MAX;
    q->ended_count--;
    return 1;
}

uint64_t voice_playq_queued_frames(const VoicePlayQ *q)
{
    return q ? q->queued : 0;
}
