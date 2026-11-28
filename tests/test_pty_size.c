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
