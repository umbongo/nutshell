/* Tests for src/core/write_stats.h: the bookkeeping behind issue.term's
 * last_write_rc / write_fails / since_key_write_ms and the term.write_fail WARN. */
#include "test_framework.h"
#include "write_stats.h"

#include <stddef.h>

int test_write_is_failure_classifies_rc(void)
{
    TEST_BEGIN();
    ASSERT_EQ(write_is_failure(1, 1), 0);
    ASSERT_EQ(write_is_failure(10, 10), 0);
    ASSERT_EQ(write_is_failure(11, 10), 0);      /* more than asked is not a failure */
    ASSERT_EQ(write_is_failure(0, 1), 1);        /* nothing written */
    ASSERT_EQ(write_is_failure(3, 10), 1);       /* short */
    ASSERT_EQ(write_is_failure(-1, 1), 1);
    ASSERT_EQ(write_is_failure(-37, 0), 1);      /* negative is always a failure */
    ASSERT_EQ(write_is_failure(0, 0), 0);
    TEST_END();
}

int test_write_stats_note_records_rc_tick_and_counts_failures(void)
{
    TEST_BEGIN();
    WriteStats w;
    write_stats_init(&w);
    ASSERT_EQ(w.fail_count, 0u);
    ASSERT_EQ(write_stats_note(&w, 5, 5, 1000ul), 0);
    ASSERT_EQ(w.last_rc, 5L);
    ASSERT_EQ(w.last_write_tick, 1000ul);
    ASSERT_EQ(w.fail_count, 0u);
    ASSERT_EQ(write_stats_note(&w, -1, 5, 2000ul), 1);
    ASSERT_EQ(write_stats_note(&w, 2, 5, 3000ul), 1);
    ASSERT_EQ(w.last_rc, 2L);
    ASSERT_EQ(w.last_write_tick, 3000ul);
    ASSERT_EQ(w.fail_count, 2u);
    ASSERT_EQ(write_stats_note(NULL, 1, 1, 1ul), 0);   /* NULL is harmless */
    TEST_END();
}

int test_write_agg_counts_one_failure_per_call_not_per_byte(void)
{
    TEST_BEGIN();
    WriteStats w;
    write_stats_init(&w);
    WriteAgg a;
    write_agg_init(&a);
    for (int i = 0; i < 100; i++) write_agg_add(&a, 1, 1);
    ASSERT_EQ(write_stats_note_agg(&w, &a, 50ul), 0);
    ASSERT_EQ(w.fail_count, 0u);
    ASSERT_EQ(w.last_rc, 1L);

    write_agg_init(&a);
    write_agg_add(&a, 1, 1);
    write_agg_add(&a, -7, 1);        /* the first failure's rc is kept */
    write_agg_add(&a, 0, 1);
    write_agg_add(&a, -9, 1);
    ASSERT_EQ(write_stats_note_agg(&w, &a, 60ul), 1);
    ASSERT_EQ(w.fail_count, 1u);     /* three bad bytes, one call, one failure */
    ASSERT_EQ(w.last_rc, -7L);
    ASSERT_EQ(w.last_write_tick, 60ul);
    TEST_END();
}

int test_write_agg_empty_is_clean(void)
{
    TEST_BEGIN();
    WriteStats w;
    write_stats_init(&w);
    WriteAgg a;
    write_agg_init(&a);              /* e.g. an all-CR chunk that wrote nothing */
    ASSERT_EQ(write_stats_note_agg(&w, &a, 5ul), 0);
    ASSERT_EQ(w.fail_count, 0u);
    ASSERT_EQ(write_stats_note_agg(NULL, &a, 5ul), 0);
    ASSERT_EQ(write_stats_note_agg(&w, NULL, 5ul), 0);
    TEST_END();
}

int test_write_ms_since_never_is_minus_one(void)
{
    TEST_BEGIN();
    ASSERT_EQ(write_ms_since(5000ul, 0ul), -1L);
    ASSERT_EQ(write_ms_since(5000ul, 4000ul), 1000L);
    ASSERT_EQ(write_ms_since(5000ul, 5000ul), 0L);
    /* tick counter wrapped between then and now */
    ASSERT_EQ(write_ms_since(10ul, 0xFFFFFFF0ul), 26L);
    TEST_END();
}
