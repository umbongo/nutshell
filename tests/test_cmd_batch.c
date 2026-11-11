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
    /* NULL defaults leaves the new batch's policy at cmd_policy_default() */
    CmdPolicy def = cmd_policy_default();
    ASSERT_EQ(a->q.policy.allowed, def.allowed);
    ASSERT_EQ(a->q.policy.unattended, def.unattended);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_add_copies_defaults(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    ApprovalQueue defaults;
    chat_approval_init(&defaults);
    /* A non-default policy -- Write with Auto-approve on -- must carry over
     * from the session into a freshly added batch. */
    cmd_policy_set_allowed(&defaults.policy, CMD_WRITE);
    cmd_policy_set_auto(&defaults.policy, 1);
    CmdBatch *a = cmd_batch_add(&set, &defaults, NULL);
    ASSERT_NOT_NULL(a);
    ASSERT_EQ(a->q.policy.allowed, CMD_WRITE);
    ASSERT_EQ(a->q.policy.unattended, CMD_WRITE);

    /* cmd_batch_add() clamps what it copies, so a split default (only a
     * test can make one) arrives with the button off (spec 2026-10-18 s2). */
    cmd_policy_set_unattended(&defaults.policy, CMD_READ);
    CmdBatch *b = cmd_batch_add(&set, &defaults, NULL);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ(b->q.policy.allowed, CMD_WRITE);
    ASSERT_EQ(b->q.policy.unattended, POLICY_NONE);
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
    int b_id = b->id;
    cmd_batch_remove(&set, b_id);
    ASSERT_EQ(set.count, 2);
    ASSERT_NULL(cmd_batch_find(&set, b_id));
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
    chat_approval_add(&a->q, "echo a", CMD_PLATFORM_LINUX);
    chat_approval_approve(&a->q, 0);
    chat_approval_add(&b->q, "echo b", CMD_PLATFORM_LINUX);

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
    chat_approval_add(&a->q, "echo a", CMD_PLATFORM_LINUX);
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
    chat_approval_add(&a->q, "echo a", CMD_PLATFORM_LINUX);
    chat_approval_add(&b->q, "echo b", CMD_PLATFORM_LINUX);
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
    chat_approval_add(&a->q, "echo a", CMD_PLATFORM_LINUX);
    ASSERT_NULL(cmd_batch_next_runnable(&set));
    cmd_batch_set_free(&set);
    TEST_END();
}

/* user_acted defaults to 0 (from cmd_batch_add's calloc) and is a plain
 * settable field the Run/Approve click paths set. */
int test_cmd_batch_add_user_acted_zero(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *a = cmd_batch_add(&set, NULL, NULL);
    ASSERT_NOT_NULL(a);
    ASSERT_EQ(a->user_acted, 0);
    a->user_acted = 1;
    ASSERT_EQ(a->user_acted, 1);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_skips_tool_batches(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *t = cmd_batch_add(&set, NULL, NULL);
    CmdBatch *c = cmd_batch_add(&set, NULL, NULL);
    ASSERT_NOT_NULL(t);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(cmd_batch_is_tool(t), 0);
    t->tool_stream_id = 5;
    t->tool_gen = 2;
    ASSERT_EQ(cmd_batch_is_tool(t), 1);
    ASSERT_EQ(cmd_batch_is_tool(c), 0);
    ASSERT_EQ(cmd_batch_is_tool(NULL), 0);
    ASSERT_EQ(chat_approval_add_tool(&t->q, "web_fetch: https://a.test/", "u",
                                     CMD_UNKNOWN), 0);
    ASSERT_EQ(chat_approval_add_session(&c->q, "ls -la", CMD_PLATFORM_LINUX, 0), 0);
    c->q.entries[0].status = APPROVE_PENDING;

    /* the tool batch needs the user, but is not the one the harness acts on */
    ASSERT_EQ(chat_approval_needs_user(&t->q), 1);
    ASSERT_TRUE(cmd_batch_first_pending(&set) == c);

    /* an approved tool row is not runnable */
    ASSERT_EQ(chat_approval_approve(&t->q, 0), 0);
    ASSERT_NULL(cmd_batch_next_runnable(&set));
    ASSERT_EQ(chat_approval_approve(&c->q, 0), 0);
    ASSERT_TRUE(cmd_batch_next_runnable(&set) == c);

    /* with only the tool batch left, nothing is pending or runnable */
    cmd_batch_remove(&set, c->id);
    ASSERT_NULL(cmd_batch_first_pending(&set));
    ASSERT_NULL(cmd_batch_next_runnable(&set));
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_cmd_batch_full(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    ASSERT_EQ(cmd_batch_full(&set), 0);
    for (int i = 0; i < CMD_BATCH_MAX - 1; i++)
        ASSERT_NOT_NULL(cmd_batch_add(&set, NULL, NULL));
    ASSERT_EQ(set.count, CMD_BATCH_MAX - 1);
    ASSERT_EQ(cmd_batch_full(&set), 0);
    ASSERT_NOT_NULL(cmd_batch_add(&set, NULL, NULL));
    ASSERT_EQ(cmd_batch_full(&set), 1);
    ASSERT_EQ(cmd_batch_full(NULL), 1);
    cmd_batch_set_free(&set);
    TEST_END();
}

/* reply_msg is the producing reply's index, set by the caller; a batch that
 * was never given one says so (-1) instead of pointing at message 0. */
int test_cmd_batch_reply_msg_defaults_unset(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ(b->reply_msg, -1);
    b->reply_msg = 6;
    ASSERT_EQ(cmd_batch_find(&set, b->id)->reply_msg, 6);
    cmd_batch_set_free(&set);
    TEST_END();
}
