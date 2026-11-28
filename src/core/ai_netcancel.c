/* src/core/ai_netcancel.c -- see ai_netcancel.h */
#include "ai_netcancel.h"
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

/* Pause, and after 64 consecutive calls yield the thread so a waiter cannot
 * starve the holder of the lock (or the closing fire) on one core. */
static void nc_relax(unsigned *spins)
{
    if (++*spins >= 64u) {
        *spins = 0;
#ifdef _WIN32
        SwitchToThread();
#else
        sched_yield();
#endif
        return;
    }
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

static void nc_lock(AiNetCancel *c)
{
    unsigned spins = 0;
    while (__atomic_exchange_n(&c->lock, 1, __ATOMIC_ACQUIRE))
        while (__atomic_load_n(&c->lock, __ATOMIC_RELAXED)) nc_relax(&spins);   /* held for a few stores only */
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
    c->closing = 0;
    c->expired = 0;
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
    __atomic_store_n(&c->expired, 0, __ATOMIC_RELEASE);   /* a new hop starts clean */
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
    if (!mine) {
        /* fire took the handle: wait until its close has returned */
        unsigned spins = 0;
        while (__atomic_load_n(&c->closing, __ATOMIC_ACQUIRE)) nc_relax(&spins);
    }
    return mine;
}

void ai_nc_fire(AiNetCancel *c)
{
    if (!c) return;
    nc_lock(c);
    __atomic_store_n(&c->cancelled, 1, __ATOMIC_RELEASE);
    uintptr_t   h = c->h;
    AiNcCloseFn close = c->close;
    c->h = 0;
    c->close = NULL;
    if (h && close) __atomic_store_n(&c->closing, 1, __ATOMIC_RELEASE);
    nc_unlock(c);
    if (h && close) {
        close(h);
        __atomic_store_n(&c->closing, 0, __ATOMIC_RELEASE);
    }
}

int ai_nc_expire(AiNetCancel *c)
{
    if (!c) return 0;
    nc_lock(c);
    /* Set under the lock, before the close: the worker that reads it sees a
     * handle it must not touch again, closed or about to be. */
    __atomic_store_n(&c->expired, 1, __ATOMIC_RELEASE);
    uintptr_t   h = c->h;
    AiNcCloseFn close = c->close;
    c->h = 0;
    c->close = NULL;
    if (h && close) __atomic_store_n(&c->closing, 1, __ATOMIC_RELEASE);
    nc_unlock(c);
    if (!(h && close)) return 0;
    close(h);
    __atomic_store_n(&c->closing, 0, __ATOMIC_RELEASE);
    return 1;
}

int ai_nc_cancelled(const AiNetCancel *c)
{
    if (!c) return 0;
    return __atomic_load_n(&c->cancelled, __ATOMIC_ACQUIRE) ? 1 : 0;
}

int ai_nc_expired(const AiNetCancel *c)
{
    if (!c) return 0;
    return __atomic_load_n(&c->expired, __ATOMIC_ACQUIRE) ? 1 : 0;
}

int ai_nc_ended(const AiNetCancel *c)
{
    return ai_nc_cancelled(c) || ai_nc_expired(c);
}
