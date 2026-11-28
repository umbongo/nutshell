#include "test_framework.h"
#include "pty_size.h"

/* Nothing sent yet: the first valid size is sent. */
int test_pty_size_first_send(void)
{
    TEST_BEGIN();
    PtySizeState st;
    pty_size_init(&st);
    ASSERT_EQ(pty_size_needs_send(&st, 80, 24), 1);
    TEST_END();
}

/* The size requested with the pty is recorded: adopting at the same size
 * sends nothing. */
int test_pty_size_unchanged_sends_nothing(void)
{
    TEST_BEGIN();
    PtySizeState st;
    pty_size_init(&st);
    pty_size_sent(&st, 93, 25);
    ASSERT_EQ(pty_size_needs_send(&st, 93, 25), 0);
    TEST_END();
}

/* A resize during the connect: the grid is now 125x49, the pty asked for
 * 93x25, so adoption sends exactly one resize; the same size again, and a
 * later repeat, send nothing; a later different size sends. */
int test_pty_size_changed_during_connect(void)
{
    TEST_BEGIN();
    PtySizeState st;
    pty_size_init(&st);
    pty_size_sent(&st, 93, 25);                       /* pty request */
    ASSERT_EQ(pty_size_needs_send(&st, 125, 49), 1);  /* adoption */
    pty_size_sent(&st, 125, 49);
    ASSERT_EQ(pty_size_needs_send(&st, 125, 49), 0);  /* later, same */
    ASSERT_EQ(pty_size_needs_send(&st, 100, 30), 1);  /* later, different */
    pty_size_sent(&st, 100, 30);
    ASSERT_EQ(pty_size_needs_send(&st, 100, 30), 0);
    TEST_END();
}

/* One dimension differing is enough. */
int test_pty_size_one_dimension_differs(void)
{
    TEST_BEGIN();
    PtySizeState st;
    pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    ASSERT_EQ(pty_size_needs_send(&st, 81, 24), 1);
    ASSERT_EQ(pty_size_needs_send(&st, 80, 25), 1);
    TEST_END();
}

/* A failed send is not recorded, so the size is offered again. */
int test_pty_size_not_recorded_until_sent(void)
{
    TEST_BEGIN();
    PtySizeState st;
    pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    ASSERT_EQ(pty_size_needs_send(&st, 120, 40), 1);
    ASSERT_EQ(pty_size_needs_send(&st, 120, 40), 1);
    TEST_END();
}

/* Invalid sizes are never sent and never recorded; NULL is tolerated. */
int test_pty_size_invalid_and_null(void)
{
    TEST_BEGIN();
    PtySizeState st;
    pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    ASSERT_EQ(pty_size_needs_send(&st, 0, 24), 0);
    ASSERT_EQ(pty_size_needs_send(&st, 80, 0), 0);
    ASSERT_EQ(pty_size_needs_send(&st, -1, -1), 0);
    pty_size_sent(&st, 0, 0);
    ASSERT_EQ(st.cols, 80);
    ASSERT_EQ(st.rows, 24);
    ASSERT_EQ(pty_size_needs_send(NULL, 80, 24), 1);
    ASSERT_EQ(pty_size_needs_send(NULL, 0, 0), 0);
    pty_size_init(NULL);
    pty_size_sent(NULL, 80, 24);
    TEST_END();
}

/* A scripted transport for pty_size_apply: returns the next code in `script`
 * and records the size each request asked for. */
#define PS_AGAIN (-37)
typedef struct { int script[8]; int n; int cols[8]; int rows[8]; } PsScript;

static int ps_send(void *ctx, int cols, int rows)
{
    PsScript *p = (PsScript *)ctx;
    int i = p->n++;
    p->cols[i] = cols;
    p->rows[i] = rows;
    return p->script[i];
}

/* EAGAIN records nothing; the same size wanted again repeats that request and
 * records it once it returns 0. */
int test_pty_size_apply_again_then_same_size(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { PS_AGAIN, 0 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(st.cols, 80);               /* not recorded */
    ASSERT_EQ(st.pending_cols, 100);
    ASSERT_EQ(st.pending_rows, 30);
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &p, PS_AGAIN), 0);
    ASSERT_EQ(p.n, 2);                    /* one repeat, no extra request */
    ASSERT_EQ(p.cols[1], 100);
    ASSERT_EQ(st.cols, 100);
    ASSERT_EQ(st.rows, 30);
    ASSERT_EQ(st.pending_cols, 0);
    TEST_END();
}

/* EAGAIN, then a different size wanted: the pending one is repeated first,
 * then the wanted one is sent; the recorded size is the wanted one. */
int test_pty_size_apply_again_then_different_size(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { PS_AGAIN, 0, 0 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(pty_size_apply(&st, 120, 40, ps_send, &p, PS_AGAIN), 0);
    ASSERT_EQ(p.n, 3);
    ASSERT_EQ(p.cols[1], 100);            /* the pending request first */
    ASSERT_EQ(p.cols[2], 120);            /* then the size now wanted */
    ASSERT_EQ(p.rows[2], 40);
    ASSERT_EQ(st.cols, 120);
    ASSERT_EQ(st.rows, 40);
    ASSERT_EQ(st.pending_cols, 0);
    TEST_END();
}

/* EAGAIN twice: still pending, nothing recorded, and the wanted size is not
 * sent yet. */
int test_pty_size_apply_again_twice(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { PS_AGAIN, PS_AGAIN, 0 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(pty_size_apply(&st, 120, 40, ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(p.n, 2);
    ASSERT_EQ(p.cols[1], 100);            /* the repeat, not the new size */
    ASSERT_EQ(st.cols, 80);
    ASSERT_EQ(st.pending_cols, 100);
    ASSERT_EQ(pty_size_apply(&st, 120, 40, ps_send, &p, PS_AGAIN), 0);
    ASSERT_EQ(st.cols, 120);
    TEST_END();
}

/* A hard error records nothing and leaves the last good size; a hard error on
 * a pending repeat drops the pending size. */
int test_pty_size_apply_hard_error(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { -5 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &p, PS_AGAIN), -5);
    ASSERT_EQ(st.cols, 80);
    ASSERT_EQ(st.pending_cols, 0);        /* not an EAGAIN: nothing pending */

    PsScript q = { { PS_AGAIN, -5, 0 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &q, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &q, PS_AGAIN), -5);
    ASSERT_EQ(st.cols, 80);
    ASSERT_EQ(st.pending_cols, 0);
    ASSERT_EQ(q.n, 2);                    /* the error ended this call */
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &q, PS_AGAIN), 0);
    ASSERT_EQ(st.cols, 100);              /* the next call sends it afresh */
    TEST_END();
}

/* Nothing pending and an unchanged or invalid size: no request at all. */
int test_pty_size_apply_no_request_when_unchanged(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { 0 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 80, 24, ps_send, &p, PS_AGAIN), 0);
    ASSERT_EQ(pty_size_apply(&st, 0, 0, ps_send, &p, PS_AGAIN), 0);
    ASSERT_EQ(p.n, 0);
    ASSERT_EQ(pty_size_apply(NULL, 80, 24, ps_send, &p, PS_AGAIN), -1);
    ASSERT_EQ(pty_size_apply(&st, 90, 24, NULL, &p, PS_AGAIN), -1);
    TEST_END();
}

/* The retry the write path and the poll make: apply() with the pending size
 * itself. EAGAIN, then the retry completes: one more send, size recorded,
 * pending cleared. */
int test_pty_size_retry_completes(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { PS_AGAIN, 0 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(pty_size_pending(&st), 1);
    ASSERT_EQ(pty_size_apply(&st, st.pending_cols, st.pending_rows,
                             ps_send, &p, PS_AGAIN), 0);
    ASSERT_EQ(p.n, 2);                    /* exactly one further send */
    ASSERT_EQ(st.cols, 100);
    ASSERT_EQ(st.rows, 30);
    ASSERT_EQ(pty_size_pending(&st), 0);
    TEST_END();
}

/* EAGAIN, then the retry gets EAGAIN again: still pending, nothing recorded. */
int test_pty_size_retry_again_again(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { PS_AGAIN, PS_AGAIN }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(pty_size_apply(&st, st.pending_cols, st.pending_rows,
                             ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(p.n, 2);
    ASSERT_EQ(st.cols, 80);
    ASSERT_EQ(st.rows, 24);
    ASSERT_EQ(pty_size_pending(&st), 1);
    ASSERT_EQ(st.pending_cols, 100);
    TEST_END();
}

/* A retry when nothing is pending sends nothing. */
int test_pty_size_retry_nothing_pending(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { 0 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_pending(&st), 0);
    ASSERT_EQ(pty_size_apply(&st, st.pending_cols, st.pending_rows,
                             ps_send, &p, PS_AGAIN), 0);
    ASSERT_EQ(p.n, 0);
    ASSERT_EQ(st.cols, 80);
    TEST_END();
}

/* EAGAIN at one size, the grid then changes, the retry call names the new
 * size: the old packet completes first, the new size goes second. */
int test_pty_size_retry_then_grid_changed(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { PS_AGAIN, 0, 0 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(pty_size_apply(&st, 120, 40, ps_send, &p, PS_AGAIN), 0);
    ASSERT_EQ(p.n, 3);
    ASSERT_EQ(p.cols[1], 100);
    ASSERT_EQ(p.cols[2], 120);
    ASSERT_EQ(st.cols, 120);
    ASSERT_EQ(st.rows, 40);
    ASSERT_EQ(pty_size_pending(&st), 0);
    TEST_END();
}

/* A size wanted while an older request is still in flight is not lost: the
 * call that asks for it gets "try again" from the pending one, and asking
 * again (the poll does, every tick, with the terminal's size) finishes the
 * old request and then sends the wanted size. */
int test_pty_size_wanted_survives_second_again(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    pty_size_sent(&st, 80, 24);
    PsScript p = { { PS_AGAIN, PS_AGAIN, 0, 0 }, 0, {0}, {0} };
    ASSERT_EQ(pty_size_apply(&st, 100, 30, ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(pty_size_apply(&st, 120, 40, ps_send, &p, PS_AGAIN), PS_AGAIN);
    ASSERT_EQ(p.n, 2);
    ASSERT_EQ(st.cols, 80);
    ASSERT_EQ(pty_size_pending(&st), 1);
    ASSERT_EQ(st.pending_cols, 100);
    ASSERT_EQ(pty_size_apply(&st, 120, 40, ps_send, &p, PS_AGAIN), 0);
    ASSERT_EQ(p.n, 4);
    ASSERT_EQ(p.cols[2], 100);
    ASSERT_EQ(p.cols[3], 120);
    ASSERT_EQ(st.cols, 120);
    ASSERT_EQ(st.rows, 40);
    ASSERT_EQ(pty_size_pending(&st), 0);
    TEST_END();
}

/* The pending rule itself: both dimensions positive; NULL is none. */
int test_pty_size_pending_rule(void)
{
    TEST_BEGIN();
    PtySizeState st; pty_size_init(&st);
    ASSERT_EQ(pty_size_pending(&st), 0);
    ASSERT_EQ(pty_size_pending(NULL), 0);
    st.pending_cols = 100;
    ASSERT_EQ(pty_size_pending(&st), 0);
    st.pending_rows = 30;
    ASSERT_EQ(pty_size_pending(&st), 1);
    TEST_END();
}
