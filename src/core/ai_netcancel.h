/* src/core/ai_netcancel.h */
#ifndef NUTSHELL_AI_NETCANCEL_H
#define NUTSHELL_AI_NETCANCEL_H

#include <stdint.h>

/*
 * ai_netcancel -- real cancellation of one blocking network request.
 *
 * A worker thread sits in a blocking WinHTTP call; a polled flag cannot wake
 * it. The token holds the one handle that call is blocked on. The worker
 * arms it after opening the request and disarms it before closing the
 * request itself; any other thread (the UI) fires it, which closes the
 * handle at once and makes the blocked call return with an error.
 *
 *   worker                          UI
 *   ------                          --
 *   ai_nc_arm(h)  (-1: cancelled,   ai_nc_fire()
 *                  worker closes h)   sets `cancelled`, takes h under the
 *   ... blocking calls ...             lock, closes it after unlocking
 *   ai_nc_disarm()  1: still mine,
 *                      close h
 *                   0: fire took it
 *
 * Exactly one side closes the handle. The worker keeps ownership of its
 * session and connect handles; only the request handle is armed. After any
 * failed call the worker consults ai_nc_cancelled() before touching the
 * handle again, because a closed handle value may be reused by the system.
 * That window (a check that passes, then a fire, then the next call) is a few
 * instructions wide and accepted; the call then fails on the closed handle.
 *
 * A disarm that returns 0 waits until fire's close callback has returned, so
 * the worker never closes the connect/session handles while the request is
 * still being closed on another thread.
 *
 * The token is sticky: once fired it refuses every later arm. It may be
 * armed and disarmed many times before that (one request per redirect hop).
 *
 * Pure, portable, no windows.h -- tested natively (tests/test_ai_netcancel.c).
 */

typedef void (*AiNcCloseFn)(uintptr_t h);

typedef struct {
    int         lock;        /* spin lock, held only to move the fields below */
    int         cancelled;   /* set once by fire, never cleared */
    uintptr_t   h;           /* the armed handle, 0 when none */
    AiNcCloseFn close;       /* how to close it */
    int         closing;     /* fire's close callback is running (outside the lock) */
    int         expired;     /* set by expire, cleared by the next arm (one hop's deadline) */
} AiNetCancel;

void ai_nc_init(AiNetCancel *c);

/* Worker: register request handle `h`. Returns 0, or -1 when the token was
 * already fired (nothing is registered; the worker closes `h` itself). */
int ai_nc_arm(AiNetCancel *c, uintptr_t h, AiNcCloseFn close);

/* Worker: take the handle back before closing it. Returns 1 when it is still
 * the worker's to close, 0 when fire has already taken (and closed) it or
 * nothing was armed. */
int ai_nc_disarm(AiNetCancel *c);

/* Any thread: mark the token cancelled and close the armed handle, if any.
 * Idempotent; the handle is closed at most once, outside the lock. */
void ai_nc_fire(AiNetCancel *c);

/* Any thread: close the armed handle, if any, exactly like ai_nc_fire() but
 * WITHOUT cancelling the token: a later arm still succeeds. For a deadline of
 * one request (ai_watchdog.h), not a cancel of the whole job. Returns 1 when it
 * closed a handle, 0 when nothing was armed. */
int ai_nc_expire(AiNetCancel *c);

/* Non-zero once ai_nc_expire() has run since the last arm: the hop's request
 * handle was closed (or was about to be) by the deadline, so the worker must
 * make no further call on it. Cleared by ai_nc_arm() for the next hop. */
int ai_nc_expired(const AiNetCancel *c);

/* Non-zero when the token was fired or expired: the armed handle is gone.
 * The one predicate a worker checks before each use of its request handle. */
int ai_nc_ended(const AiNetCancel *c);

/* Non-zero once fired. NULL counts as not cancelled (a tool run without a
 * token). */
int ai_nc_cancelled(const AiNetCancel *c);

#endif /* NUTSHELL_AI_NETCANCEL_H */
