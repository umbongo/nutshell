/* tests/test_conn_prompt.c -- src/core/conn_prompt.h: the ownership
 * protocol behind the connect-thread's host-key and passphrase prompts
 * (src/ui/window.c). No thread is spawned here -- each test drives the
 * worker's and the UI's calls by hand, in whichever order a real race
 * could deliver them, so the dangerous interleavings (especially the one
 * that used to hang the worker forever -- see
 * test_conn_prompt_cancel_before_publish_is_still_caught below -- and the
 * ABA one -- see test_conn_prompt_stale_seq_is_refused_by_a_newer_request
 * below) are exercised deterministically instead of hoping a real race
 * reproduces them. */
#include "test_framework.h"
#include "conn_prompt.h"

int test_conn_prompt_slot_starts_empty_and_not_cancelled(void)
{
    TEST_BEGIN();
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int token;
    ASSERT_FALSE(conn_prompt_slot_holds(&s, &token, 0));
    ASSERT_FALSE(conn_prompt_slot_holds(&s, &token, 1));
    ASSERT_FALSE(conn_prompt_slot_cancelled(&s));
    ASSERT_NULL(conn_prompt_slot_cancel_and_claim(&s));
    TEST_END();
}

int test_conn_prompt_publish_then_claim_by_identity(void)
{
    TEST_BEGIN();
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int token;
    unsigned seq = conn_prompt_slot_publish(&s, &token);
    ASSERT_TRUE(conn_prompt_slot_holds(&s, &token, seq));
    ASSERT_EQ(conn_prompt_slot_claim(&s, &token, seq), 1);
    ASSERT_FALSE(conn_prompt_slot_holds(&s, &token, seq));
    /* Claimed once: a second claim of the same token+seq finds nothing. */
    ASSERT_EQ(conn_prompt_slot_claim(&s, &token, seq), 0);
    TEST_END();
}

int test_conn_prompt_publish_then_cancel_and_claim(void)
{
    TEST_BEGIN();
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int token;
    conn_prompt_slot_publish(&s, &token);
    ASSERT_TRUE(conn_prompt_slot_cancel_and_claim(&s) == &token);
    ASSERT_TRUE(conn_prompt_slot_cancelled(&s));
    /* Second call finds nothing left -- idempotently safe. */
    ASSERT_NULL(conn_prompt_slot_cancel_and_claim(&s));
    TEST_END();
}

int test_conn_prompt_claim_wrong_identity_does_not_drop_real_request(void)
{
    TEST_BEGIN();
    /* Regression for the bug where the UI thread's post-dialog code
     * unconditionally swapped the slot to NULL and only *afterward*
     * checked whether it got the request it expected -- if it got a
     * *different* one (should never happen in practice, since a job
     * never has two requests in flight, but the protocol must not rely
     * on that to stay safe), that foreign request was silently dropped
     * on the floor with nobody left to signal its owner. claim() by
     * identity must instead leave an unmatched holder untouched. */
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int real_token, wrong_token;
    unsigned seq = conn_prompt_slot_publish(&s, &real_token);
    ASSERT_EQ(conn_prompt_slot_claim(&s, &wrong_token, seq), 0);
    /* The real token is still there, unharmed, and claimable. */
    ASSERT_TRUE(conn_prompt_slot_holds(&s, &real_token, seq));
    ASSERT_EQ(conn_prompt_slot_claim(&s, &real_token, seq), 1);
    TEST_END();
}

int test_conn_prompt_cancel_is_sticky(void)
{
    TEST_BEGIN();
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    conn_prompt_slot_cancel_and_claim(&s);
    conn_prompt_slot_cancel_and_claim(&s);
    ASSERT_TRUE(conn_prompt_slot_cancelled(&s));
    TEST_END();
}

int test_conn_prompt_cancel_before_publish_is_still_caught(void)
{
    TEST_BEGIN();
    /* This is the interleaving that used to hang the worker forever:
     *   UI:     cancel_and_claim() (nothing published yet -- a no-op,
     *           but the cancel flag is now set)
     *   worker: publish(req)
     *   worker: checks cancelled() -- must see it, or it commits to
     *           PostMessage+WaitForSingleObject with nobody left to ever
     *           signal it (the UI already made its one attempt to claim
     *           and moved on).
     * conn_prompt_slot_cancel_and_claim() sets the sticky `cancelled`
     * flag *before* it claims whatever is pending -- see its doc -- so
     * even though the claim above finds nothing, the flag is already
     * visible to whatever the worker checks right after it publishes. */
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int token;

    ASSERT_NULL(conn_prompt_slot_cancel_and_claim(&s));   /* nothing published yet */

    /* Worker, unaware of the above, proceeds anyway. */
    unsigned seq = conn_prompt_slot_publish(&s, &token);
    ASSERT_TRUE(conn_prompt_slot_cancelled(&s));   /* must see it */
    /* So it self-resolves instead of posting and waiting. */
    ASSERT_EQ(conn_prompt_slot_claim(&s, &token, seq), 1);
    TEST_END();
}

int test_conn_prompt_publish_then_cancel_ui_wins_claim(void)
{
    TEST_BEGIN();
    /* The opposite interleaving: the worker publishes first, then the UI
     * cancels. The UI's cancel_and_claim() must get it (so it can signal
     * the worker itself); the worker's own attempt to reclaim what it
     * published must then correctly find nothing left. */
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int token;

    unsigned seq = conn_prompt_slot_publish(&s, &token);
    void *claimed = conn_prompt_slot_cancel_and_claim(&s);
    ASSERT_TRUE(claimed == &token);

    /* Worker's post-publish check: it sees the cancellation too, but its
     * own claim attempt is a no-op -- the UI already owns the request. */
    ASSERT_TRUE(conn_prompt_slot_cancelled(&s));
    ASSERT_EQ(conn_prompt_slot_claim(&s, &token, seq), 0);
    TEST_END();
}

int test_conn_prompt_answered_path_no_cancellation(void)
{
    TEST_BEGIN();
    /* The common case: worker publishes, UI (after showing its dialog)
     * claims it by identity+generation to deliver the answer, no
     * cancellation involved. */
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int token;
    unsigned seq = conn_prompt_slot_publish(&s, &token);
    ASSERT_FALSE(conn_prompt_slot_cancelled(&s));
    ASSERT_EQ(conn_prompt_slot_claim(&s, &token, seq), 1);
    TEST_END();
}

int test_conn_prompt_claim_after_claim_by_identity_finds_nothing(void)
{
    TEST_BEGIN();
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int token;
    unsigned seq = conn_prompt_slot_publish(&s, &token);
    ASSERT_EQ(conn_prompt_slot_claim(&s, &token, seq), 1);
    /* A cancellation racing in just after the UI resolved it (e.g. the
     * window is torn down microseconds after the dialog was answered)
     * must not find (and must not disturb) anything. */
    ASSERT_NULL(conn_prompt_slot_cancel_and_claim(&s));
    TEST_END();
}

int test_conn_prompt_stale_seq_is_refused_by_a_newer_request(void)
{
    TEST_BEGIN();
    /* The ABA scenario: connection_thread's host-key and passphrase
     * requests are sequential `ConnUiRequest req` locals at the same call
     * site in conn_ui_ask(), so the compiler is entirely free to place
     * the second at the exact same stack address as the first once it
     * has returned. A WM_CONN_PROMPT message carries a generation number
     * alongside the address specifically so a message that -- somehow --
     * still names the first request cannot be mistaken for the second
     * one just because the address matches. */
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int token;   /* same address stands in for both "generations" */

    unsigned seq1 = conn_prompt_slot_publish(&s, &token);
    ASSERT_EQ(conn_prompt_slot_claim(&s, &token, seq1), 1);   /* first fully resolved */

    unsigned seq2 = conn_prompt_slot_publish(&s, &token);     /* address reused */
    ASSERT_TRUE(seq2 != seq1);

    /* A stale message still carrying seq1 must not be able to see or
     * claim the new, unrelated request living at the same address. */
    ASSERT_FALSE(conn_prompt_slot_holds(&s, &token, seq1));
    ASSERT_EQ(conn_prompt_slot_claim(&s, &token, seq1), 0);

    /* The real, current request is unharmed and claimable by its correct
     * generation. */
    ASSERT_TRUE(conn_prompt_slot_holds(&s, &token, seq2));
    ASSERT_EQ(conn_prompt_slot_claim(&s, &token, seq2), 1);
    TEST_END();
}

int test_conn_prompt_seq_is_never_zero(void)
{
    TEST_BEGIN();
    /* 0 is reserved as "no publish happened yet" (conn_prompt_slot_init()
     * leaves the slot at generation 0 with nothing published) -- a real
     * publish must never hand out that value, or a caller that forgot to
     * check for a live holder could accidentally treat an empty slot as
     * matching an uninitialized/zeroed seq variable. */
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    int token;
    ASSERT_TRUE(conn_prompt_slot_publish(&s, &token) != 0);
    TEST_END();
}

int test_conn_prompt_null_is_safe(void)
{
    TEST_BEGIN();
    int token;
    conn_prompt_slot_init(NULL);
    conn_prompt_slot_publish(NULL, &token);
    ASSERT_TRUE(conn_prompt_slot_cancelled(NULL));   /* fail closed */
    ASSERT_FALSE(conn_prompt_slot_holds(NULL, &token, 1));
    ASSERT_NULL(conn_prompt_slot_cancel_and_claim(NULL));
    ASSERT_EQ(conn_prompt_slot_claim(NULL, &token, 1), 0);
    ConnPromptSlot s;
    conn_prompt_slot_init(&s);
    ASSERT_EQ(conn_prompt_slot_claim(&s, NULL, 1), 0);
    TEST_END();
}
