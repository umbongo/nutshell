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

/* Non-zero once fired. NULL counts as not cancelled (a tool run without a
 * token). */
int ai_nc_cancelled(const AiNetCancel *c);

#endif /* NUTSHELL_AI_NETCANCEL_H */
