#include "ns_motion.h"
#include <stddef.h>

double ns_ease(double t)
{
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

void ns_anim_start(NsAnim *a, unsigned long now_tick, int duration_ms)
{
    if (!a) return;
    a->start_tick = now_tick;
    a->duration_ms = duration_ms;
    a->done_reported = 0;
}

NsAnimStep ns_anim_progress(NsAnim *a, unsigned long now_tick, int reduced_motion)
{
    NsAnimStep step = { 1.0, 0 };
    if (!a) return step;

    if (reduced_motion) {
        step.t = 1.0;
        step.done = a->done_reported ? 0 : 1;
        a->done_reported = 1;
        return step;
    }

    /* Unsigned subtraction wraps correctly even if now_tick has wrapped
     * past ULONG_MAX relative to start_tick. */
    unsigned long elapsed = now_tick - a->start_tick;

    double t_raw;
    if (a->duration_ms <= 0) {
        t_raw = 1.0;
    } else {
        t_raw = (double)elapsed / (double)a->duration_ms;
    }
    if (t_raw < 0.0) t_raw = 0.0;
    if (t_raw > 1.0) t_raw = 1.0;

    step.t = ns_ease(t_raw);
    step.done = 0;
    if (t_raw >= 1.0 && !a->done_reported) {
        step.done = 1;
        a->done_reported = 1;
    }
    return step;
}

static int anim_list_find(const NsAnimList *l, int id)
{
    for (int i = 0; i < l->count; i++) {
        if (l->ids[i] == id) return i;
    }
    return -1;
}

int ns_anim_list_add(NsAnimList *l, int id, unsigned long now_tick, int duration_ms)
{
    if (!l) return -1;

    int idx = anim_list_find(l, id);
    if (idx >= 0) {
        ns_anim_start(&l->items[idx], now_tick, duration_ms);
        return idx;
    }

    if (l->count >= NS_ANIM_LIST_MAX) return -1;

    idx = l->count++;
    l->ids[idx] = id;
    ns_anim_start(&l->items[idx], now_tick, duration_ms);
    return idx;
}

int ns_anim_list_active(const NsAnimList *l)
{
    if (!l) return 0;
    return l->count;
}

int ns_anim_list_step(NsAnimList *l, unsigned long now_tick, int reduced_motion)
{
    if (!l) return 0;

    int i = 0;
    while (i < l->count) {
        NsAnimStep step = ns_anim_progress(&l->items[i], now_tick, reduced_motion);
        if (step.done) {
            int last = l->count - 1;
            l->items[i] = l->items[last];
            l->ids[i] = l->ids[last];
            l->count--;
            /* Re-check the same index (now holding the previous last item). */
            continue;
        }
        i++;
    }
    return l->count;
}

int ns_anim_list_get(const NsAnimList *l, int id, unsigned long now_tick,
                     int reduced_motion, NsAnimStep *out)
{
    NsAnimStep fallback = { 1.0, 0 };
    if (!out) return 0;
    *out = fallback;
    if (!l) return 0;

    int idx = anim_list_find(l, id);
    if (idx < 0) return 0;

    /* Progress on a copy: get() must not mutate the caller's list (it is
     * const), and step() already owns advancing/removing finished items. */
    NsAnim tmp = l->items[idx];
    *out = ns_anim_progress(&tmp, now_tick, reduced_motion);
    return 1;
}
