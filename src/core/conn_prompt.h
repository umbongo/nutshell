/* src/core/conn_prompt.h */
#ifndef NUTSHELL_CONN_PROMPT_H
#define NUTSHELL_CONN_PROMPT_H

/*
 * conn_prompt -- the ownership protocol behind one connect-thread UI
 * prompt (the host-key decision, the key passphrase prompt). Mirrors
 * src/core/worker_life.h's split: pure, portable (no windows.h), GCC
 * atomic builtins, tested natively in tests/test_conn_prompt.c; the Win32
 * event/PostMessage/dialog live in src/ui/window.c.
 *
 * A ConnPromptSlot lives on the ConnJob (one per job -- connection_thread
 * never has more than one prompt in flight at a time, but does ask more
 * than once sequentially: a host-key decision, then possibly a passphrase
 * prompt, both from local `ConnUiRequest req` variables at the same call
 * site in the same worker thread, which a compiler is entirely free to
 * place at the very same stack address once the first has returned). The
 * worker publishes its pending request's address into the slot as an
 * opaque void* token (this module never dereferences it) and gets back a
 * generation number; later, exactly one of two callers claims it:
 *
 *   - the UI thread, once it has an answer (conn_prompt_slot_claim(),
 *     which only succeeds if the slot still holds that exact token *and*
 *     is still on the generation the caller was given);
 *   - the cancellation path (a tab closing, the window going away, a new
 *     connection replacing this one), which doesn't have a specific
 *     token or generation in hand -- it just wants to stop whatever is
 *     pending right now (conn_prompt_slot_cancel_and_claim()).
 *
 * The generation number is what closes the ABA hole address reuse would
 * otherwise open: a WM_CONN_PROMPT message carries the token *and* the
 * generation it was posted for (packed into wParam alongside the job id
 * in window.c, never re-read from the token's own memory -- see
 * conn_prompt_slot_publish()'s doc for why). If that message is somehow
 * still in flight after its request was long since resolved and a *new*
 * request for the same job happens to reuse the exact same address, the
 * generation carried by the stale message no longer matches the slot's
 * current one, so holds()/claim() correctly refuse it even though the
 * pointer alone would have matched.
 *
 * Claiming is a compare-and-swap (by identity+generation, or
 * unconditional): only the caller that performs the swap may touch the
 * request's memory afterward. That is what makes it safe for the UI
 * thread to be holding a raw pointer to a ConnJob or a ConnUiRequest
 * across something as slow as a modal dialog -- the window (and the job,
 * and the request) can be torn down by a nested cancellation while that
 * dialog's own message loop is pumping, so the UI thread must always
 * re-derive both from scratch (by job id, then conn_prompt_slot_holds())
 * before ever dereferencing them again, and must only actually write
 * through them once conn_prompt_slot_claim() has confirmed it won the
 * claim.
 *
 * Ordering matters for the sticky `cancelled` flag: it must be set
 * *before* the pending request (if any) is claimed, or a worker that
 * publishes a fresh request in the gap between those two steps could
 * check conn_prompt_slot_cancelled(), see it still false, and commit to
 * posting and waiting with nobody left to ever answer it.
 * conn_prompt_slot_cancel_and_claim() does both steps, in the correct
 * order, as one function specifically so that ordering is itself under
 * test here rather than trusted to every call site getting it right (see
 * tests/test_conn_prompt.c, which walks the dangerous interleavings step
 * by step) -- window.c's conn_ui_request_cancel() calls it, then does the
 * Win32 part (writing the answer, SetEvent) on whatever it gets back.
 * The worker's publish-before-check ordering (see
 * conn_prompt_slot_publish()'s doc) is the other half of the same
 * invariant and is enforced the same way, by construction of the two
 * functions' contracts rather than by convention at the call site.
 */

typedef struct {
    void *   volatile holder;    /* NULL, or the currently published request */
    unsigned volatile seq;       /* bumped on every publish(); see the module doc */
    int      volatile cancelled; /* sticky; set once, by the UI thread */
} ConnPromptSlot;

void conn_prompt_slot_init(ConnPromptSlot *s);

/* Worker thread: publish `req` as the slot's pending request, overwriting
 * whatever was there, and return the generation number this publish was
 * assigned (never 0 -- see conn_prompt_slot_holds()/claim()). The caller
 * must carry that number alongside `req` for as long as it needs to refer
 * back to this exact publish (e.g. packed into a posted message's
 * wParam) -- never by reading a `seq` field back out of the request
 * struct itself later, since if the address gets reused by a later
 * publish, the memory at that address no longer reflects what it held
 * at the time of *this* publish.
 *
 * Must be called *before* checking conn_prompt_slot_cancelled(): a
 * cancellation that already happened (the cancel flag already set,
 * conn_prompt_slot_cancel_and_claim()'s claim half already run and found
 * nothing since nothing was published yet) must still be visible to the
 * check that follows a publish, or the worker commits to PostMessage+wait
 * with nobody left to ever answer it. Publishing first guarantees that
 * whichever of {this check, a cancellation that runs later} happens
 * second observes the resolution. */
unsigned conn_prompt_slot_publish(ConnPromptSlot *s, void *req);

/* NULL reads as cancelled (fail closed). */
int conn_prompt_slot_cancelled(const ConnPromptSlot *s);

/* Cancellation path: mark the slot cancelled (sticky -- never cleared),
 * then claim whatever it currently holds, if anything, clearing it.
 * Returns the claimed token, or NULL if nothing was pending. See the
 * module doc for why "cancel, then claim" must happen as this one
 * function rather than as two separate calls a caller might reorder. */
void *conn_prompt_slot_cancel_and_claim(ConnPromptSlot *s);

/* Non-destructive: does the slot currently hold exactly `req` on
 * generation `seq`? For validating that a request is still live before
 * touching its memory, without claiming it -- claiming here would stop a
 * concurrent cancellation from being able to interrupt it while, say, a
 * dialog built from it is still on screen. */
int conn_prompt_slot_holds(const ConnPromptSlot *s, const void *req, unsigned seq);

/* Answered path: claim the slot only if it currently holds exactly `req`
 * on generation `seq`, clearing it. Returns 1 (claimed -- the caller now
 * owns `req` exclusively) or 0 (the slot held something else -- a
 * different generation's request that happens to share the same address,
 * a cancellation that already claimed it, or nothing at all -- the caller
 * must not touch `req`, or anything found in the slot, at all in that
 * case; in particular it must never drop a request that turns out not to
 * be the one it expected). */
int conn_prompt_slot_claim(ConnPromptSlot *s, void *req, unsigned seq);

#endif /* NUTSHELL_CONN_PROMPT_H */
