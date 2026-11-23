/* src/core/ai_netcancel.c -- see ai_netcancel.h */
#include "ai_netcancel.h"
#include <stddef.h>

static void nc_lock(AiNetCancel *c)
{
    while (__atomic_exchange_n(&c->lock, 1, __ATOMIC_ACQUIRE))
        while (__atomic_load_n(&c->lock, __ATOMIC_RELAXED)) { /* spin: held for a few stores only */ }
}

static void nc_unlock(AiNetCancel *c)
{
    __atomic_store_n(&c->lock, 0, __ATOMIC_RELEASE);
}

void ai_nc_init(AiNetCancel *c)
{
    if (!c) return;
    c->lock = 0;
    c->cancelled = 0;
    c->h = 0;
    c->close = NULL;
}

int ai_nc_arm(AiNetCancel *c, uintptr_t h, AiNcCloseFn close)
{
    if (!c) return 0;   /* no token: nothing to register, nothing cancelled */
    nc_lock(c);
    if (c->cancelled) {
        nc_unlock(c);
        return -1;
    }
    c->h = h;
    c->close = close;
    nc_unlock(c);
    return 0;
}

int ai_nc_disarm(AiNetCancel *c)
{
    if (!c) return 1;   /* no token: the handle was never shared */
    nc_lock(c);
    int mine = c->h != 0;
    c->h = 0;
    c->close = NULL;
    nc_unlock(c);
    return mine;
}

void ai_nc_fire(AiNetCancel *c)
{
    if (!c) return;
    nc_lock(c);
    c->cancelled = 1;
    uintptr_t   h = c->h;
    AiNcCloseFn close = c->close;
    c->h = 0;
    c->close = NULL;
    nc_unlock(c);
    if (h && close) close(h);
}

int ai_nc_cancelled(const AiNetCancel *c)
{
    if (!c) return 0;
    return __atomic_load_n(&c->cancelled, __ATOMIC_ACQUIRE) ? 1 : 0;
}
