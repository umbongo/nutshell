/* src/core/conn_prompt.c -- see conn_prompt.h */
#include "conn_prompt.h"
#include "secure_zero.h"
#include <stddef.h>
#include <string.h>

void conn_prompt_slot_init(ConnPromptSlot *s)
{
    if (!s) return;
    __atomic_store_n(&s->holder, (void *)NULL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&s->seq, 0u, __ATOMIC_SEQ_CST);
    __atomic_store_n(&s->cancelled, 0, __ATOMIC_SEQ_CST);
}

unsigned conn_prompt_slot_publish(ConnPromptSlot *s, void *req)
{
    if (!s) return 0;
    /* Bump the generation first, then publish the token under it: a
     * reader can only ever observe (new seq, old/NULL holder) or (new
     * seq, new holder) in between these two stores, never (new holder,
     * old seq) -- either is safe for holds()/claim() (both require an
     * exact seq match before trusting `holder` at all), but this order
     * means a reader that sees the new seq and a stale holder simply
     * fails the identity check rather than the generation check, which
     * keeps the failure mode uniform. */
    unsigned seq = __atomic_add_fetch(&s->seq, 1u, __ATOMIC_SEQ_CST);
    __atomic_store_n(&s->holder, req, __ATOMIC_SEQ_CST);
    return seq;
}

int conn_prompt_slot_cancelled(const ConnPromptSlot *s)
{
    if (!s) return 1;
    return __atomic_load_n(&s->cancelled, __ATOMIC_SEQ_CST) ? 1 : 0;
}

void *conn_prompt_slot_cancel_and_claim(ConnPromptSlot *s)
{
    if (!s) return NULL;
    __atomic_store_n(&s->cancelled, 1, __ATOMIC_SEQ_CST);
    return __atomic_exchange_n(&s->holder, (void *)NULL, __ATOMIC_ACQ_REL);
}

int conn_prompt_slot_holds(const ConnPromptSlot *s, const void *req, unsigned seq)
{
    if (!s) return 0;
    if (__atomic_load_n(&s->seq, __ATOMIC_SEQ_CST) != seq) return 0;
    return __atomic_load_n(&s->holder, __ATOMIC_SEQ_CST) == req;
}

int conn_prompt_slot_claim(ConnPromptSlot *s, void *req, unsigned seq)
{
    if (!s || !req) return 0;
    /* Checked before the CAS, not folded into it: the slot's `seq` and
     * `holder` are two separate atomics, and the only writer that could
     * ever change either of them for this job's slot -- another
     * publish() -- runs exclusively on the worker thread that is, at the
     * time any caller reaches this function, blocked waiting on the very
     * request this claim is trying to resolve. It cannot call publish()
     * again until that wait ends, and the wait cannot end until this
     * claim (or a cancellation) finishes -- so no publish() can land
     * between this check and the CAS below. */
    if (__atomic_load_n(&s->seq, __ATOMIC_SEQ_CST) != seq) return 0;
    void *expected = req;
    return __atomic_compare_exchange_n(&s->holder, &expected, (void *)NULL,
                                        0 /* not weak */,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)
           ? 1 : 0;
}

int conn_prompt_wants_password(int auth_is_key, const char *saved_password)
{
    if (auth_is_key) return 0;
    return (!saved_password || saved_password[0] == '\0') ? 1 : 0;
}

int conn_ui_kind_has_secret(ConnUiKind kind)
{
    return (kind == CONN_UI_PASSPHRASE || kind == CONN_UI_PASSWORD ||
            kind == CONN_UI_KBDINT) ? 1 : 0;
}

void conn_save_clear(ConnSaveCandidate *c)
{
    if (!c) return;
    secure_zero(c->pw, sizeof c->pw);
    c->ticked = 0;
}

void conn_save_note(ConnSaveCandidate *c, const char *typed, int ticked)
{
    if (!c) return;
    conn_save_clear(c);
    if (ticked && typed) {
        size_t n = strlen(typed);
        if (n > CONN_SAVE_MAX - 1) n = CONN_SAVE_MAX - 1;
        memcpy(c->pw, typed, n);
        c->pw[n] = '\0';
        c->ticked = 1;
    }
}
