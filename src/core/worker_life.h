/* src/core/worker_life.h */
#ifndef NUTSHELL_WORKER_LIFE_H
#define NUTSHELL_WORKER_LIFE_H

/*
 * worker_life -- the lifetime handshake between the UI thread and one
 * background worker (an AI stream, an SSH connection attempt).
 *
 * The object a worker runs on is owned jointly: one reference for the UI,
 * one for the worker. Neither side ever frees it out from under the other.
 *
 *   - The worker drops its reference when it is done, after posting its
 *     last message.
 *   - The UI drops its reference when it has consumed the result, or when
 *     the thing the result was for (a tab, the AI panel, the app) goes away
 *     first. In that case it cancels the worker first: the object is then
 *     "orphaned", and the worker frees it on its way out.
 *
 * Whichever side drops the last reference frees the object. Messages the
 * worker posts carry the object's id, never a pointer into UI state, and
 * the UI resolves the id against what it still holds before acting on it.
 *
 * Pure, portable, no windows.h -- tested natively (tests/test_ai_stream.c).
 */

typedef struct {
    volatile long refs;       /* owners still holding the object */
    volatile int  cancelled;  /* set once by the UI; read by the worker.
                               * An int so a tool's cancel_flag can point
                               * straight at it. */
} WorkerLife;

/* Two references: the UI's and the worker's. Not cancelled. */
void worker_life_init(WorkerLife *l);

/* Ask the worker to stop. Idempotent; never undone. */
void worker_life_cancel(WorkerLife *l);

/* Non-zero once worker_life_cancel() has been called. */
int worker_life_cancelled(const WorkerLife *l);

/* Drop one reference. Returns 1 to the caller that dropped the last one,
 * which must then free the object; 0 otherwise. Each side calls this
 * exactly once. */
int worker_life_release(WorkerLife *l);

/* Current reference count (tests and assertions only). */
int worker_life_refs(const WorkerLife *l);

/* A process-unique id for a new worker object: never 0, never repeated
 * (short of 2^32 allocations), safe to call from any thread. */
unsigned worker_life_next_id(void);

#endif /* NUTSHELL_WORKER_LIFE_H */
