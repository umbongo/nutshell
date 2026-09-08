/* tests/test_cmd_batch.c */
#include "test_framework.h"
#include "cmd_batch.h"
#include <string.h>

int test_cmd_batch_set_init_empty(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    ASSERT_EQ(set.count, 0);
    ASSERT_EQ(set.next_id, 1);
    TEST_END();
}

int test_cmd_batch_add_assigns_sequential_ids(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);
    CmdBatch *c = cmd_batch_add(&set, NULL, NULL);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(a->id, 1);
    ASSERT_EQ(b->id, 2);
    ASSERT_EQ(c->id, 3);
    ASSERT_EQ(set.count, 3);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_add_inits_queue(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    ASSERT_NOT_NULL(a);
    ASSERT_EQ(a->q.count, 0);
    ASSERT_EQ(a->q.auto_approve, 0);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_add_copies_defaults(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    ApprovalQueue defaults;
    chat_approval_init(&defaults);
    defaults.auto_approve = 1;
    defaults.auto_approve_level = AUTO_APPROVE_WRITE;
    CmdBatch *a = cmd_batch_add(&set, &defaults, NULL);
    ASSERT_NOT_NULL(a);
    ASSERT_EQ(a->q.auto_approve, 1);
    ASSERT_EQ((int)a->q.auto_approve_level, (int)AUTO_APPROVE_WRITE);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_find_present_and_absent(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    CmdBatch *found = cmd_batch_find(&set, a->id);
    ASSERT_TRUE(found == a);
    ASSERT_NULL(cmd_batch_find(&set, 999));
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_remove_middle(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);
    CmdBatch *c = cmd_batch_add(&set, NULL, NULL);
    cmd_batch_remove(&set, b->id);
    ASSERT_EQ(set.count, 2);
    ASSERT_NULL(cmd_batch_find(&set, b->id));
    ASSERT_TRUE(cmd_batch_find(&set, a->id) == a);
    ASSERT_TRUE(cmd_batch_find(&set, c->id) == c);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_remove_absent_is_noop(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    cmd_batch_add(&set, NULL, NULL);
    cmd_batch_remove(&set, 999);
    ASSERT_EQ(set.count, 1);
    cmd_batch_set_free(&set);
    TEST_END();
}

/* Adding a 9th batch (CMD_BATCH_MAX == 8) evicts the oldest, reports its
 * id, and every id assigned across the whole sequence stays unique. */
int test_cmd_batch_add_evicts_oldest_when_full(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *batches[9];
    int ids[9];
    for (int i = 0; i < 8; i++) {
        batches[i] = cmd_batch_add(&set, NULL, NULL);
        ASSERT_NOT_NULL(batches[i]);
        ids[i] = batches[i]->id;
    }
    ASSERT_EQ(set.count, CMD_BATCH_MAX);

    int evicted_id = -1;
    batches[8] = cmd_batch_add(&set, NULL, &evicted_id);
    ASSERT_NOT_NULL(batches[8]);
    ids[8] = batches[8]->id;

    /* First batch's id was evicted */
    ASSERT_EQ(evicted_id, ids[0]);
    ASSERT_EQ(set.count, CMD_BATCH_MAX);
    ASSERT_NULL(cmd_batch_find(&set, ids[0]));

    /* Every id issued (1..9) is unique */
    for (int i = 0; i < 9; i++)
        for (int j = i + 1; j < 9; j++)
            ASSERT_TRUE(ids[i] != ids[j]);

    /* The oldest surviving batch is now what was batches[1] */
    ASSERT_TRUE(cmd_batch_find(&set, ids[1]) == batches[1]);
    ASSERT_TRUE(set.b[0] == batches[1]);
    ASSERT_TRUE(set.b[CMD_BATCH_MAX - 1] == batches[8]);

    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_add_no_eviction_reports_zero(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    int evicted_id = -1;
    cmd_batch_add(&set, NULL, &evicted_id);
    ASSERT_EQ(evicted_id, 0);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_first_pending_oldest_needing_user(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);

    /* a fully decided (nothing pending); b still has a pending entry */
    chat_approval_add(&a->q, "echo a", CMD_PLATFORM_LINUX, 1);
    chat_approval_approve(&a->q, 0);
    chat_approval_add(&b->q, "echo b", CMD_PLATFORM_LINUX, 1);

    CmdBatch *pending = cmd_batch_first_pending(&set);
    ASSERT_TRUE(pending == b);

    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_first_pending_none(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    chat_approval_add(&a->q, "echo a", CMD_PLATFORM_LINUX, 1);
    chat_approval_approve(&a->q, 0);
    ASSERT_NULL(cmd_batch_first_pending(&set));
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_next_runnable_oldest_with_approved(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);

    /* a has nothing approved yet (still pending); b has an approved entry */
    chat_approval_add(&a->q, "echo a", CMD_PLATFORM_LINUX, 1);
    chat_approval_add(&b->q, "echo b", CMD_PLATFORM_LINUX, 1);
    chat_approval_approve(&b->q, 0);

    CmdBatch *runnable = cmd_batch_next_runnable(&set);
    ASSERT_TRUE(runnable == b);

    /* Once a also has an approved entry, a (older) wins */
    chat_approval_approve(&a->q, 0);
    runnable = cmd_batch_next_runnable(&set);
    ASSERT_TRUE(runnable == a);

    cmd_batch_set_free(&set);
    TEST_END();
}

/* conv_mark defaults to 0 (from cmd_batch_add's calloc) and is a plain
 * settable field the caller records the conversation length into. */
int test_cmd_batch_conv_mark_defaults_zero_and_settable(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    ASSERT_NOT_NULL(a);
    ASSERT_EQ(a->conv_mark, 0);
    a->conv_mark = 5;
    ASSERT_EQ(a->conv_mark, 5);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_next_runnable_none(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    chat_approval_add(&a->q, "echo a", CMD_PLATFORM_LINUX, 1);
    ASSERT_NULL(cmd_batch_next_runnable(&set));
    cmd_batch_set_free(&set);
    TEST_END();
}
