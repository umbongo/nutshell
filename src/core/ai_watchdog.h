/* src/core/ai_watchdog.h */
#ifndef NUTSHELL_AI_WATCHDOG_H
#define NUTSHELL_AI_WATCHDOG_H

#include "ai_netcancel.h"

/*
 * ai_watchdog -- closes the request handle armed on a token when one hop's
 * time is up, or when the caller's cancel flag is raised, so a blocking
 * WinHTTP call (a read from a server that trickles bytes) returns at once.
 *
 *   worker                                  watchdog thread
 *   ------                                  ---------------
 *   ai_nc_arm(nc, req, close)
 *   wd = ai_wd_start(nc, ms, cancel)  --->  waits for the deadline, the flag
 *   ... blocking calls ...                  or a stop; then ai_nc_expire(nc)
 *   fired = ai_wd_stop(wd)  (joins)
 *   ai_nc_disarm(nc): 1 close the handle yourself, 0 the watchdog (or a fire)
 *                     already did
 *
 * The close goes through the token, so the handle is closed exactly once
 * whoever gets there first (this thread, the UI's ai_nc_fire, or the worker's
 * own disarm). ai_wd_stop() joins the thread: none is left running after it
 * returns, and none can touch a later hop's handle. Portable (pthreads off
 * Windows, Win32 threads on it); tested natively (tests/test_ai_watchdog.c).
 */

typedef struct AiWatchdog AiWatchdog;

/* Start watching `nc` for `ms` milliseconds (at least 1). `cancel` may be
 * NULL; when it reads non-zero the handle is closed too (checked every
 * ~25 ms). Returns NULL when the thread could not start: the caller goes on
 * without a watchdog, as before. */
AiWatchdog *ai_wd_start(AiNetCancel *nc, unsigned long ms, volatile int *cancel);

/* Stop and join the thread, free it. Returns 1 when it closed the handle
 * (deadline or cancel flag), 0 when it did not. NULL is harmless (returns 0). */
int ai_wd_stop(AiWatchdog *wd);

#endif /* NUTSHELL_AI_WATCHDOG_H */
