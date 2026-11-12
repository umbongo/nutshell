/* tests/test_chat_msg.c */
#include "test_framework.h"
#include "chat_msg.h"
#include "tool_decision.h"
#include "cmd_batch.h"
#include <string.h>
#include <stdlib.h>

int test_chat_msg_list_init(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ASSERT_NULL(list.head);
    ASSERT_NULL(list.tail);
    ASSERT_EQ(list.count, 0);
    TEST_END();
}

int test_chat_msg_append_user(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_USER, "hello");
    ASSERT_NOT_NULL(item);
    ASSERT_EQ((int)item->type, (int)CHAT_ITEM_USER);
    ASSERT_STR_EQ(item->text, "hello");
    ASSERT_EQ(item->dirty, 1);
    ASSERT_EQ(list.count, 1);
    ASSERT_TRUE(list.head == item);
    ASSERT_TRUE(list.tail == item);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_append_order(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "first");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "second");
    ChatMsgItem *c = chat_msg_append(&list, CHAT_ITEM_STATUS, "third");
    ASSERT_EQ(list.count, 3);
    ASSERT_TRUE(list.head == a);
    ASSERT_TRUE(list.tail == c);
    ASSERT_TRUE(a->next == b);
    ASSERT_TRUE(b->next == c);
    ASSERT_TRUE(c->prev == b);
    ASSERT_TRUE(b->prev == a);
    ASSERT_NULL(a->prev);
    ASSERT_NULL(c->next);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_remove_middle(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_USER, "b");
    ChatMsgItem *c = chat_msg_append(&list, CHAT_ITEM_USER, "c");
    chat_msg_remove(&list, b);
    ASSERT_EQ(list.count, 2);
    ASSERT_TRUE(a->next == c);
    ASSERT_TRUE(c->prev == a);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_remove_head(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_USER, "b");
    chat_msg_remove(&list, a);
    ASSERT_EQ(list.count, 1);
    ASSERT_TRUE(list.head == b);
    ASSERT_NULL(b->prev);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_remove_tail(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_USER, "b");
    chat_msg_remove(&list, b);
    ASSERT_EQ(list.count, 1);
    ASSERT_TRUE(list.tail == a);
    ASSERT_NULL(a->next);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_remove_only(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    chat_msg_remove(&list, a);
    ASSERT_EQ(list.count, 0);
    ASSERT_NULL(list.head);
    ASSERT_NULL(list.tail);
    TEST_END();
}

int test_chat_msg_set_text(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_USER, "old");
    item->dirty = 0;
    ASSERT_EQ(chat_msg_set_text(item, "new text"), 0);
    ASSERT_STR_EQ(item->text, "new text");
    ASSERT_EQ(item->dirty, 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_command(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ASSERT_EQ(chat_msg_set_command(item, "ls -la", CMD_READ, 0), 0);
    ASSERT_STR_EQ(item->u.cmd.command, "ls -la");
    ASSERT_EQ((int)item->u.cmd.safety, (int)CMD_READ);
    ASSERT_EQ(item->u.cmd.approved, -1); /* pending */
    ASSERT_EQ(item->u.cmd.blocked, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_thinking(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "response");
    ASSERT_EQ(chat_msg_set_thinking(item, "I think..."), 0);
    ASSERT_STR_EQ(item->u.ai.thinking_text, "I think...");
    ASSERT_EQ(item->u.ai.thinking_collapsed, 1); /* default collapsed */
    chat_msg_list_clear(&list);
    TEST_END();
}

/* Thinking disclosure: a user collapse must stick (see ai_chat.c's
 * WM_AI_STREAM handler). thinking_user_set is the flag that records a
 * manual open/close; a freshly created AI item must start with it unset,
 * and chat_msg_set_thinking() -- called on every streamed thinking chunk
 * -- must never touch it, or a mid-stream collapse would be silently
 * undone by the very next chunk. */
int test_chat_msg_new_ai_item_thinking_user_set_is_zero(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "response");
    ASSERT_NOT_NULL(item);
    ASSERT_EQ(item->u.ai.thinking_user_set, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_thinking_leaves_user_set_alone(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "response");
    item->u.ai.thinking_user_set = 1;
    ASSERT_EQ(chat_msg_set_thinking(item, "more thinking..."), 0);
    ASSERT_EQ(item->u.ai.thinking_user_set, 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* Thinking disclosure overlap fix: recalc_layout() only remeasures items
 * that are dirty (or unmeasured). If chat_msg_set_thinking() replaced the
 * text without marking the item dirty, an item already measured at its
 * pre-thinking (shorter) height would keep that stale height forever,
 * and later items would paint over the newly-opened Thinking block. */
int test_chat_msg_set_thinking_marks_dirty(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "response");
    /* Simulate the item having already been measured once (dirty cleared,
     * as recalc_layout does after Pass 1) before thinking text arrives. */
    item->dirty = 0;
    ASSERT_EQ(chat_msg_set_thinking(item, "I think..."), 0);
    ASSERT_EQ(item->dirty, 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_empty_text(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_USER, "");
    ASSERT_NOT_NULL(item);
    ASSERT_STR_EQ(item->text, "");
    ASSERT_EQ(item->text_len, (size_t)0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_null_text(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_STATUS, NULL);
    ASSERT_NOT_NULL(item);
    ASSERT_NOT_NULL(item->text); /* should be "" */
    ASSERT_EQ(item->text_len, (size_t)0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_unique_ids(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_USER, "b");
    ASSERT_TRUE(a->id != b->id);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_command_too_long(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    /* Build a command > 1023 bytes */
    char long_cmd[2048];
    memset(long_cmd, 'x', sizeof(long_cmd) - 1);
    long_cmd[sizeof(long_cmd) - 1] = '\0';
    /* Should reject (return non-zero) */
    ASSERT_TRUE(chat_msg_set_command(item, long_cmd, CMD_READ, 0) != 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_list_clear(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    for (int i = 0; i < 50; i++)
        chat_msg_append(&list, CHAT_ITEM_USER, "msg");
    ASSERT_EQ(list.count, 50);
    chat_msg_list_clear(&list);
    ASSERT_EQ(list.count, 0);
    ASSERT_NULL(list.head);
    ASSERT_NULL(list.tail);
    TEST_END();
}

/* ── Error-status-after-AI scenarios ────────────────────────────────
 * When an HTTP error occurs during streaming, a STATUS item is appended
 * after the AI item.  The status item must be the tail and marked dirty
 * so the UI layer knows to scroll it into view. */

int test_chat_msg_error_status_after_ai_is_tail(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_msg_append(&list, CHAT_ITEM_USER, "hello");
    chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "");
    ChatMsgItem *status = chat_msg_append(&list, CHAT_ITEM_STATUS,
                                          "HTTP 429: streaming request failed");
    ASSERT_NOT_NULL(status);
    ASSERT_TRUE(list.tail == status);
    ASSERT_EQ((int)status->type, (int)CHAT_ITEM_STATUS);
    ASSERT_STR_EQ(status->text, "HTTP 429: streaming request failed");
    ASSERT_EQ(status->dirty, 1);
    ASSERT_EQ(list.count, 3);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_error_status_dirty_flag(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_msg_append(&list, CHAT_ITEM_USER, "test");
    ChatMsgItem *ai = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "partial");
    /* Simulate the AI item having been measured (dirty cleared) */
    ai->dirty = 0;
    ChatMsgItem *err = chat_msg_append(&list, CHAT_ITEM_STATUS,
                                       "HTTP 500: streaming request failed");
    /* The new status item must be dirty regardless of prior items */
    ASSERT_EQ(err->dirty, 1);
    /* The AI item before it should be untouched */
    ASSERT_EQ(ai->dirty, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_multiple_error_statuses(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_msg_append(&list, CHAT_ITEM_USER, "try again");
    chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "");
    chat_msg_append(&list, CHAT_ITEM_STATUS,
                    "HTTP 429: streaming request failed");
    /* User retries, another error */
    chat_msg_append(&list, CHAT_ITEM_USER, "try again");
    chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "");
    ChatMsgItem *err2 = chat_msg_append(&list, CHAT_ITEM_STATUS,
                                         "HTTP 429: streaming request failed");
    ASSERT_TRUE(list.tail == err2);
    ASSERT_EQ(err2->dirty, 1);
    ASSERT_EQ(list.count, 6);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* ── Batch id helpers (pending command batches) ─────────────────────── */

int test_chat_msg_set_batch(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(item, "ls", CMD_READ, 0);
    ASSERT_EQ(item->u.cmd.batch, 0);
    chat_msg_set_batch(item, 7);
    ASSERT_EQ(item->u.cmd.batch, 7);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_batch_noop_on_non_command(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_USER, "hi");
    chat_msg_set_batch(item, 7);   /* must not crash or touch anything */
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_command_does_not_touch_batch(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_batch(item, 3);
    chat_msg_set_command(item, "ls", CMD_READ, 0);
    ASSERT_EQ(item->u.cmd.batch, 3);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* Two interleaved batches: batch 1's commands, then batch 2's commands.
 * batch_index/first/settle must only ever see the requested batch. */
int test_chat_msg_batch_index_first_settle_two_batches(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);

    ChatMsgItem *a1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(a1, "echo a1", CMD_READ, 0);
    chat_msg_set_batch(a1, 1);
    ChatMsgItem *a2 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(a2, "echo a2", CMD_READ, 0);
    chat_msg_set_batch(a2, 1);

    chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "in between");

    ChatMsgItem *b1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(b1, "echo b1", CMD_READ, 0);
    chat_msg_set_batch(b1, 2);
    ChatMsgItem *b2 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(b2, "echo b2", CMD_READ, 0);
    chat_msg_set_batch(b2, 2);
    ChatMsgItem *b3 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(b3, "echo b3", CMD_READ, 0);
    chat_msg_set_batch(b3, 2);

    /* batch_index: position within its own batch, ignoring the other */
    ASSERT_EQ(chat_msg_batch_index(&list, a1), 0);
    ASSERT_EQ(chat_msg_batch_index(&list, a2), 1);
    ASSERT_EQ(chat_msg_batch_index(&list, b1), 0);
    ASSERT_EQ(chat_msg_batch_index(&list, b2), 1);
    ASSERT_EQ(chat_msg_batch_index(&list, b3), 2);

    /* batch_first: first unsettled item of each batch */
    ASSERT_TRUE(chat_msg_batch_first(&list, 1) == a1);
    ASSERT_TRUE(chat_msg_batch_first(&list, 2) == b1);
    ASSERT_NULL(chat_msg_batch_first(&list, 3));

    /* settle batch 1 only: its items settle, batch 2 untouched */
    a1->dirty = 0; a2->dirty = 0;
    int n = chat_msg_batch_settle(&list, 1);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(a1->u.cmd.settled, 1);
    ASSERT_EQ(a2->u.cmd.settled, 1);
    ASSERT_EQ(a1->dirty, 1);
    ASSERT_EQ(a2->dirty, 1);
    ASSERT_EQ(b1->u.cmd.settled, 0);
    ASSERT_EQ(b2->u.cmd.settled, 0);
    ASSERT_EQ(b3->u.cmd.settled, 0);

    /* Now a1/a2 are settled: batch_index reports -1, batch_first(1) is NULL */
    ASSERT_EQ(chat_msg_batch_index(&list, a1), -1);
    ASSERT_NULL(chat_msg_batch_first(&list, 1));
    /* batch 2 unaffected */
    ASSERT_EQ(chat_msg_batch_index(&list, b2), 1);

    /* settle batch 2 */
    int n2 = chat_msg_batch_settle(&list, 2);
    ASSERT_EQ(n2, 3);
    ASSERT_EQ(b1->u.cmd.settled, 1);
    ASSERT_EQ(b2->u.cmd.settled, 1);
    ASSERT_EQ(b3->u.cmd.settled, 1);

    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_batch_index_non_command_is_negative_one(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *user = chat_msg_append(&list, CHAT_ITEM_USER, "hi");
    ASSERT_EQ(chat_msg_batch_index(&list, user), -1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_batch_settle_unknown_batch_returns_zero(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(a1, "echo a1", CMD_READ, 0);
    chat_msg_set_batch(a1, 1);
    ASSERT_EQ(chat_msg_batch_settle(&list, 99), 0);
    ASSERT_EQ(a1->u.cmd.settled, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* ── chat_cmd_label() ────────────────────────────────────────────────
 * Pure label + intent for a command card's status chip. Table order from
 * the dispatch-states design spec, section 2. */

int test_chat_cmd_label_blocked_is_held(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_DIM;
    /* blocked wins regardless of approved/run */
    ASSERT_STR_EQ(chat_cmd_label(1, 1, 0, CHAT_RUN_DONE, &intent), "held");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_WARNING);
    ASSERT_STR_EQ(chat_cmd_label(-1, 1, 0, CHAT_RUN_NONE, &intent), "held");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_WARNING);
    TEST_END();
}

int test_chat_cmd_label_denied(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_WARNING;
    /* approved == 0 wins over any run value */
    ASSERT_STR_EQ(chat_cmd_label(0, 0, 0, CHAT_RUN_RUNNING, &intent), "denied");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_DIM);
    TEST_END();
}

int test_chat_cmd_label_skipped(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_WARNING;
    ASSERT_STR_EQ(chat_cmd_label(-1, 0, 0, CHAT_RUN_NONE, &intent), "skipped");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_DIM);
    TEST_END();
}

int test_chat_cmd_label_queued(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_WARNING;
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, CHAT_RUN_NONE, &intent), "queued");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_DIM);
    TEST_END();
}

int test_chat_cmd_label_running(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_DIM;
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, CHAT_RUN_RUNNING, &intent), "running");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_INFO);
    TEST_END();
}

int test_chat_cmd_label_done(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_DIM;
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, CHAT_RUN_DONE, &intent), "ran");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_SUCCESS);
    TEST_END();
}

int test_chat_cmd_label_aborted(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_DIM;
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, CHAT_RUN_ABORTED, &intent), "not run");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_WARNING);
    TEST_END();
}

int test_chat_cmd_label_unexpected_run_falls_back_to_queued(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_WARNING;
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, 999, &intent), "queued");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_DIM);
    TEST_END();
}

int test_chat_cmd_label_null_intent_ok(void) {
    TEST_BEGIN();
    /* Must not crash, and must still return the right label. */
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, CHAT_RUN_RUNNING, NULL), "running");
    ASSERT_STR_EQ(chat_cmd_label(1, 1, 0, CHAT_RUN_NONE, NULL), "held");
    TEST_END();
}

/* ── chat_msg_batch_sync_run() ───────────────────────────────────────
 * Syncs u.cmd.run from a batch's ApprovalQueue, in list order paired with
 * q->entries[i]. */

int test_chat_msg_batch_sync_run_basic(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_READ, 0);
    chat_msg_set_batch(c0, 5);
    ChatMsgItem *c1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c1, "pwd", CMD_READ, 0);
    chat_msg_set_batch(c1, 5);
    ChatMsgItem *c2 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c2, "whoami", CMD_READ, 0);
    chat_msg_set_batch(c2, 5);
    c0->dirty = 0; c1->dirty = 0; c2->dirty = 0;

    ApprovalQueue q;
    chat_approval_init(&q);
    ASSERT_TRUE(chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX) == 0);
    ASSERT_TRUE(chat_approval_add(&q, "pwd", CMD_PLATFORM_LINUX) == 1);
    ASSERT_TRUE(chat_approval_add(&q, "whoami", CMD_PLATFORM_LINUX) == 2);
    ASSERT_EQ(chat_approval_approve(&q, 0), 0);
    ASSERT_EQ(chat_approval_approve(&q, 1), 0);
    ASSERT_EQ(chat_approval_approve(&q, 2), 0);
    chat_approval_set_completed(&q, 0);
    chat_approval_set_executing(&q, 1);
    /* entry 2 stays APPROVED, never sent */

    int n = chat_msg_batch_sync_run(&list, 5, &q, 0);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(c0->u.cmd.run, CHAT_RUN_DONE);
    ASSERT_EQ(c1->u.cmd.run, CHAT_RUN_RUNNING);
    ASSERT_EQ(c2->u.cmd.run, CHAT_RUN_NONE);
    ASSERT_EQ(c0->dirty, 1);
    ASSERT_EQ(c1->dirty, 1);
    ASSERT_EQ(c2->dirty, 0); /* unchanged: was already CHAT_RUN_NONE */

    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_batch_sync_run_batch_ending(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_READ, 0);
    chat_msg_set_batch(c0, 9);
    ChatMsgItem *c1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c1, "pwd", CMD_READ, 0);
    chat_msg_set_batch(c1, 9);
    ChatMsgItem *c2 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c2, "whoami", CMD_READ, 0);
    chat_msg_set_batch(c2, 9);

    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "pwd", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "whoami", CMD_PLATFORM_LINUX);
    chat_approval_approve(&q, 0);
    chat_approval_approve(&q, 1);
    chat_approval_approve(&q, 2);
    chat_approval_set_completed(&q, 0);
    chat_approval_set_executing(&q, 1);
    /* entry 2 stays APPROVED, never sent, and the batch is stopping */

    int n = chat_msg_batch_sync_run(&list, 9, &q, 1 /* batch_ending */);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(c0->u.cmd.run, CHAT_RUN_DONE);     /* COMPLETED unaffected by batch_ending */
    ASSERT_EQ(c1->u.cmd.run, CHAT_RUN_UNFINISHED); /* EXECUTING + ending (decision 21) */
    ASSERT_EQ(c2->u.cmd.run, CHAT_RUN_ABORTED);  /* APPROVED + ending -> ABORTED */

    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_batch_sync_run_settled_items_still_synced(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_READ, 0);
    chat_msg_set_batch(c0, 4);
    ChatMsgItem *c1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c1, "pwd", CMD_READ, 0);
    chat_msg_set_batch(c1, 4);

    /* The panel already settled this batch's rows (rendered inline), while
     * the dispatcher is still running them. */
    ASSERT_EQ(chat_msg_batch_settle(&list, 4), 2);
    ASSERT_EQ(c0->u.cmd.settled, 1);
    ASSERT_EQ(c1->u.cmd.settled, 1);

    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "pwd", CMD_PLATFORM_LINUX);
    chat_approval_approve(&q, 0);
    chat_approval_approve(&q, 1);
    chat_approval_set_executing(&q, 0);
    chat_approval_set_completed(&q, 1);

    int n = chat_msg_batch_sync_run(&list, 4, &q, 0);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(c0->u.cmd.run, CHAT_RUN_RUNNING);
    ASSERT_EQ(c1->u.cmd.run, CHAT_RUN_DONE);

    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_batch_sync_run_unrelated_batch_untouched(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* Batch 1 and batch 2 interleaved in the list. */
    ChatMsgItem *a0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(a0, "echo a0", CMD_READ, 0);
    chat_msg_set_batch(a0, 1);
    ChatMsgItem *b0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(b0, "echo b0", CMD_READ, 0);
    chat_msg_set_batch(b0, 2);
    ChatMsgItem *a1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(a1, "echo a1", CMD_READ, 0);
    chat_msg_set_batch(a1, 1);
    ChatMsgItem *b1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(b1, "echo b1", CMD_READ, 0);
    chat_msg_set_batch(b1, 2);
    b0->dirty = 0; b1->dirty = 0;

    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "echo a0", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "echo a1", CMD_PLATFORM_LINUX);
    chat_approval_approve(&q, 0);
    chat_approval_approve(&q, 1);
    chat_approval_set_executing(&q, 0);
    chat_approval_set_completed(&q, 1);

    int n = chat_msg_batch_sync_run(&list, 1, &q, 0);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(a0->u.cmd.run, CHAT_RUN_RUNNING);
    ASSERT_EQ(a1->u.cmd.run, CHAT_RUN_DONE);
    /* batch 2's items are untouched */
    ASSERT_EQ(b0->u.cmd.run, CHAT_RUN_NONE);
    ASSERT_EQ(b1->u.cmd.run, CHAT_RUN_NONE);
    ASSERT_EQ(b0->dirty, 0);
    ASSERT_EQ(b1->dirty, 0);

    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_batch_sync_run_null_list_returns_zero(void) {
    TEST_BEGIN();
    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    ASSERT_EQ(chat_msg_batch_sync_run(NULL, 1, &q, 0), 0);
    TEST_END();
}

int test_chat_msg_batch_sync_run_null_queue_returns_zero(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_READ, 0);
    chat_msg_set_batch(c0, 1);
    ASSERT_EQ(chat_msg_batch_sync_run(&list, 1, NULL, 0), 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_batch_sync_run_empty_queue_leaves_items_untouched(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_READ, 0);
    chat_msg_set_batch(c0, 1);
    c0->dirty = 0;

    ApprovalQueue q;
    chat_approval_init(&q); /* count == 0 */

    int n = chat_msg_batch_sync_run(&list, 1, &q, 0);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(c0->u.cmd.run, CHAT_RUN_NONE);
    ASSERT_EQ(c0->dirty, 0);

    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_status_after_empty_ai(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_msg_append(&list, CHAT_ITEM_USER, "hello");
    /* AI item with no content yet (stream never started) */
    ChatMsgItem *ai = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "");
    ASSERT_STR_EQ(ai->text, "");
    ChatMsgItem *status = chat_msg_append(&list, CHAT_ITEM_STATUS,
                                          "Error: connection timeout");
    ASSERT_TRUE(list.tail == status);
    ASSERT_EQ(status->dirty, 1);
    ASSERT_EQ(list.count, 3);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* --- per-command note --- */

int test_chat_msg_cmd_note_defaults_to_null(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ASSERT_NOT_NULL(item);
    ASSERT_NULL(item->u.cmd.note);
    chat_msg_set_command(item, "ls", CMD_READ, 0);
    ASSERT_NULL(item->u.cmd.note);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_note_copies_and_replaces(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *item = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(item, "ls", CMD_READ, 0);
    char src[] = "Lists files";
    ASSERT_EQ(chat_msg_set_note(item, src), 0);
    ASSERT_NOT_NULL(item->u.cmd.note);
    ASSERT_TRUE(item->u.cmd.note != src);
    src[0] = 'X';
    ASSERT_STR_EQ(item->u.cmd.note, "Lists files");
    ASSERT_EQ(chat_msg_set_note(item, "Second"), 0);
    ASSERT_STR_EQ(item->u.cmd.note, "Second");
    /* set_command keeps the note. */
    chat_msg_set_command(item, "pwd", CMD_READ, 0);
    ASSERT_STR_EQ(item->u.cmd.note, "Second");
    /* NULL clears it. */
    ASSERT_EQ(chat_msg_set_note(item, NULL), 0);
    ASSERT_NULL(item->u.cmd.note);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_note_rejects_non_command_and_null_item(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *user = chat_msg_append(&list, CHAT_ITEM_USER, "hi");
    ASSERT_TRUE(chat_msg_set_note(user, "x") != 0);
    ASSERT_TRUE(chat_msg_set_note(NULL, "x") != 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_note_freed_with_item(void) {
    TEST_BEGIN();
    /* Exercised under a leak/ASan run; here we prove remove and clear both
     * accept an item carrying a note without touching freed memory. */
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(a, "ls", CMD_READ, 0);
    chat_msg_set_command(b, "pwd", CMD_READ, 0);
    chat_msg_set_note(a, "note a");
    chat_msg_set_note(b, "note b");
    chat_msg_remove(&list, a);
    ASSERT_EQ(list.count, 1);
    ASSERT_STR_EQ(list.head->u.cmd.note, "note b");
    chat_msg_list_clear(&list);
    ASSERT_EQ(list.count, 0);
    TEST_END();
}

/* --- utf8_cut_at --- */

int test_utf8_cut_at_short_string_untouched(void) {
    TEST_BEGIN();
    char s[16] = "hello";
    ASSERT_EQ((int)utf8_cut_at(s, sizeof(s)), 5);
    ASSERT_STR_EQ(s, "hello");
    /* Exactly max-1 bytes fits. */
    char t[6] = "hello";
    ASSERT_EQ((int)utf8_cut_at(t, sizeof(t)), 5);
    ASSERT_STR_EQ(t, "hello");
    TEST_END();
}

int test_utf8_cut_at_ascii_truncates_to_max_minus_one(void) {
    TEST_BEGIN();
    char s[32] = "abcdefghij";
    ASSERT_EQ((int)utf8_cut_at(s, 5), 4);
    ASSERT_STR_EQ(s, "abcd");
    TEST_END();
}

int test_utf8_cut_at_does_not_split_multibyte(void) {
    TEST_BEGIN();
    /* "ab" + U+00E9 (2 bytes) + "cd": limit 4 -> 3 usable bytes: the é would
     * need bytes 2..3 so it fits only if max-1 >= 4. */
    char s[32] = "ab\xC3\xA9" "cd";
    ASSERT_EQ((int)utf8_cut_at(s, 4), 2);
    ASSERT_STR_EQ(s, "ab");
    char t[32] = "ab\xC3\xA9" "cd";
    ASSERT_EQ((int)utf8_cut_at(t, 5), 4);
    ASSERT_STR_EQ(t, "ab\xC3\xA9");
    /* 3-byte and 4-byte characters. */
    char u[32] = "a\xE2\x82\xAC" "z";          /* a EUR z */
    ASSERT_EQ((int)utf8_cut_at(u, 4), 1);     /* 3 usable: EUR needs 3 after a */
    ASSERT_STR_EQ(u, "a");
    char v[32] = "\xF0\x9F\x98\x80" "x";       /* emoji x */
    ASSERT_EQ((int)utf8_cut_at(v, 4), 0);
    ASSERT_STR_EQ(v, "");
    char w[32] = "\xF0\x9F\x98\x80" "x";
    ASSERT_EQ((int)utf8_cut_at(w, 5), 4);
    ASSERT_STR_EQ(w, "\xF0\x9F\x98\x80");
    TEST_END();
}

int test_utf8_cut_at_degenerate_args(void) {
    TEST_BEGIN();
    char s[8] = "abc";
    ASSERT_EQ((int)utf8_cut_at(s, 0), 0);
    ASSERT_STR_EQ(s, "abc");           /* nothing may be written */
    ASSERT_EQ((int)utf8_cut_at(s, 1), 0);
    ASSERT_STR_EQ(s, "");
    ASSERT_EQ((int)utf8_cut_at(NULL, 8), 0);
    TEST_END();
}

/* --- selection support: runs on the item, lookup by id (spec 2026-10-18) --- */

int test_chat_msg_find_by_id(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = chat_msg_append(&list, CHAT_ITEM_USER, "a");
    ChatMsgItem *b = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "b");
    ASSERT_TRUE(chat_msg_find(&list, a->id) == a);
    ASSERT_TRUE(chat_msg_find(&list, b->id) == b);
    int gone = a->id;
    chat_msg_remove(&list, a);
    ASSERT_NULL(chat_msg_find(&list, gone));
    ASSERT_NULL(chat_msg_find(NULL, 1));
    /* ids are never reused, even after a clear */
    int b_id = b->id;
    chat_msg_list_clear(&list);
    ChatMsgItem *c = chat_msg_append(&list, CHAT_ITEM_USER, "c");
    ASSERT_TRUE(c->id > b_id);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_item_starts_without_runs_and_frees_them(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *ai = chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "hello");
    ASSERT_EQ(ai->runs.n, 0);
    ASSERT_EQ(ai->pinned_runs.n, 0);
    ASSERT_EQ(ai->runs.cap, 0);
    ASSERT_NULL(ai->runs.v);
    ASSERT_NULL(ai->pinned_runs.v);
    ASSERT_EQ(ai->painted_height, 0);
    ASSERT_EQ(ai->u.ai.think_cache_len, -1);
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.w = 10; t.h = 10; t.end = 5; t.part = TR_PART_MAIN;
    int cum[2] = { 0, 10 }, cb[2] = { 0, 5 };
    int r1 = text_runs_append(&ai->runs, &t, cum, cb, 2);
    int r2 = text_runs_append(&ai->pinned_runs, &t, cum, cb, 2);
    ASSERT_EQ(r1, 0);
    ASSERT_EQ(r2, 0);
    /* Both sets own memory now; removing the item must release it (the
     * item's storage goes with it, so the freed fields are not observable
     * here -- a leak check of this test is the proof, see the task report). */
    ASSERT_NOT_NULL(ai->runs.v);
    ASSERT_NOT_NULL(ai->pinned_runs.v);
    ASSERT_NOT_NULL(ai->runs.cum);
    ASSERT_NOT_NULL(ai->pinned_runs.cum);
    chat_msg_remove(&list, ai);      /* frees both run sets */
    ASSERT_EQ(list.count, 0);
    ASSERT_NULL(list.head);

    /* Non-AI items start without runs too, and clear frees every item's. */
    ChatMsgItem *u = chat_msg_append(&list, CHAT_ITEM_USER, "u");
    ASSERT_EQ(u->runs.n, 0);
    int r3 = text_runs_append(&u->runs, &t, cum, cb, 2);
    ASSERT_EQ(r3, 0);
    chat_msg_list_clear(&list);
    ASSERT_EQ(list.count, 0);
    TEST_END();
}

int test_chat_cmd_label_quoted(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_INFO;
    ASSERT_STR_EQ(chat_cmd_label(-1, 1, 1, CHAT_RUN_NONE, &intent), "quoted, not run");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_DIM);
    intent = CHAT_CMD_INTENT_INFO;
    ASSERT_STR_EQ(chat_cmd_label(1, 1, 1, CHAT_RUN_DONE, &intent), "quoted, not run");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_DIM);
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 1, CHAT_RUN_RUNNING, NULL), "quoted, not run");
    TEST_END();
}

int test_chat_card_header_text(void) {
    TEST_BEGIN();
    char buf[96];
    ASSERT_EQ((int)chat_card_header_text(NULL, 3, 0, 0, buf, sizeof(buf)), 10);
    ASSERT_STR_EQ(buf, "3 commands");
    ASSERT_EQ((int)chat_card_header_text(NULL, 1, 0, 0, buf, sizeof(buf)), 9);
    ASSERT_STR_EQ(buf, "1 command");
    chat_card_header_text(NULL, 3, 1, 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "3 commands \xC2\xB7 1 held");
    chat_card_header_text(NULL, 3, 0, 2, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "3 commands \xC2\xB7 2 quoted");
    int n = (int)chat_card_header_text(NULL, 4, 1, 2, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "4 commands \xC2\xB7 1 held \xC2\xB7 2 quoted");
    ASSERT_EQ(n, (int)strlen(buf));
    /* does not fit: 0, and buf still a valid string */
    memset(buf, 'x', sizeof(buf));
    ASSERT_EQ((int)chat_card_header_text(NULL, 3, 0, 0, buf, 8), 0);
    ASSERT_EQ((int)buf[7], 0);
    ASSERT_EQ((int)strlen(buf) < 8, 1);
    ASSERT_EQ((int)chat_card_header_text(NULL, 3, 0, 0, NULL, 8), 0);
    ASSERT_EQ((int)chat_card_header_text(NULL, 3, 0, 0, buf, 0), 0);
    TEST_END();
}

int test_chat_msg_set_quoted(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *it = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ASSERT_NOT_NULL(it);
    ASSERT_EQ(it->u.cmd.quoted, 0);
    chat_msg_set_quoted(it, 1);
    ASSERT_EQ(it->u.cmd.quoted, 1);
    chat_msg_set_quoted(it, 0);
    ASSERT_EQ(it->u.cmd.quoted, 0);
    chat_msg_set_quoted(NULL, 1);   /* safe */
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_set_reason(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *it = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ASSERT_NOT_NULL(it);
    /* a fresh command item has no reason */
    ASSERT_STR_EQ(it->u.cmd.reason, "");

    chat_msg_set_reason(it, "reads credentials");
    ASSERT_STR_EQ(it->u.cmd.reason, "reads credentials");

    /* a new reason replaces the old; the empty string and NULL clear it */
    chat_msg_set_reason(it, "ab");
    ASSERT_STR_EQ(it->u.cmd.reason, "ab");
    chat_msg_set_reason(it, "");
    ASSERT_STR_EQ(it->u.cmd.reason, "");
    chat_msg_set_reason(it, "reads credentials");
    chat_msg_set_reason(it, NULL);
    ASSERT_STR_EQ(it->u.cmd.reason, "");

    /* re-setting the command (a reused item) drops a stale reason */
    chat_msg_set_reason(it, "reads credentials");
    ASSERT_EQ(chat_msg_set_command(it, "ls", CMD_READ, 0), 0);
    ASSERT_STR_EQ(it->u.cmd.reason, "");

    /* 100 ASCII bytes: cut to CMD_REASON_MAX - 1, NUL-terminated */
    char longr[101];
    memset(longr, 'x', 100);
    longr[100] = '\0';
    chat_msg_set_reason(it, longr);
    ASSERT_EQ((int)strlen(it->u.cmd.reason), CMD_REASON_MAX - 1);
    ASSERT_EQ((int)it->u.cmd.reason[CMD_REASON_MAX - 1], 0);

    /* a 2-byte character straddling the limit is dropped whole */
    char mb[101];
    memset(mb, 'x', CMD_REASON_MAX - 2);       /* 62 bytes */
    mb[CMD_REASON_MAX - 2] = '\xC3';           /* bytes 62 and 63 */
    mb[CMD_REASON_MAX - 1] = '\xA9';
    memset(mb + CMD_REASON_MAX, 'y', 10);
    mb[CMD_REASON_MAX + 10] = '\0';
    chat_msg_set_reason(it, mb);
    ASSERT_EQ((int)strlen(it->u.cmd.reason), CMD_REASON_MAX - 2);
    ASSERT_EQ((int)(unsigned char)it->u.cmd.reason[CMD_REASON_MAX - 3], 'x');

    /* NULL item and non-command items are no-ops */
    chat_msg_set_reason(NULL, "reads credentials");
    ChatMsgItem *user = chat_msg_append(&list, CHAT_ITEM_USER, "hi");
    ASSERT_NOT_NULL(user);
    chat_msg_set_reason(user, "reads credentials");
    ASSERT_STR_EQ(user->text, "hi");
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_card_header_noun(void) {
    TEST_BEGIN();
    char buf[96];
    ASSERT_EQ((int)chat_card_header_text("tool call", 1, 0, 0, buf, sizeof(buf)), 11);
    ASSERT_STR_EQ(buf, "1 tool call");
    ASSERT_EQ((int)chat_card_header_text("tool call", 2, 0, 0, buf, sizeof(buf)), 12);
    ASSERT_STR_EQ(buf, "2 tool calls");
    chat_card_header_text(NULL, 3, 1, 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "3 commands \xC2\xB7 1 held");
    ASSERT_EQ((int)chat_card_header_text(NULL, 3, 0, 0, buf, 8), 0);
    ASSERT_EQ((int)chat_card_header_text("tool call", 3, 0, 0, buf, 8), 0);
    ASSERT_EQ((int)buf[0], 0);
    TEST_END();
}

int test_chat_msg_set_tool_and_outcome(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *cmd = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ChatMsgItem *usr = chat_msg_append(&list, CHAT_ITEM_USER, "hi");
    ASSERT_NOT_NULL(cmd);
    ASSERT_NOT_NULL(usr);
    ASSERT_EQ(cmd->u.cmd.tool, 0);
    ASSERT_EQ(cmd->u.cmd.tool_outcome, (int)TOOL_DECISION_NONE);

    chat_msg_set_tool(cmd, 1);
    ASSERT_EQ(cmd->u.cmd.tool, 1);
    ASSERT_EQ(cmd->u.cmd.tool_outcome, (int)TOOL_DECISION_PENDING);

    chat_msg_set_tool_outcome(cmd, (int)TOOL_DECISION_APPROVED);
    ASSERT_EQ(cmd->u.cmd.tool_outcome, (int)TOOL_DECISION_APPROVED);

    /* clearing the flag leaves the outcome alone */
    chat_msg_set_tool(cmd, 0);
    ASSERT_EQ(cmd->u.cmd.tool, 0);
    ASSERT_EQ(cmd->u.cmd.tool_outcome, (int)TOOL_DECISION_APPROVED);

    /* NULL-safe, and a no-op on a non-command item */
    chat_msg_set_tool(NULL, 1);
    chat_msg_set_tool_outcome(NULL, 3);
    char before[sizeof(*usr)];
    memcpy(before, usr, sizeof(*usr));
    chat_msg_set_tool(usr, 1);
    chat_msg_set_tool_outcome(usr, (int)TOOL_DECISION_RUN);
    ASSERT_EQ(memcmp(before, usr, sizeof(*usr)), 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_msg_apply_ceiling_skips_tool(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *tool = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ChatMsgItem *held = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ChatMsgItem *quo  = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ChatMsgItem *open = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    ChatMsgItem *usr  = chat_msg_append(&list, CHAT_ITEM_USER, "hi");
    ASSERT_NOT_NULL(tool);
    ASSERT_NOT_NULL(held);
    ASSERT_NOT_NULL(quo);
    ASSERT_NOT_NULL(open);
    ASSERT_NOT_NULL(usr);

    /* A tool row: unblocked and undecided, then held and undecided. */
    chat_msg_set_tool(tool, 1);
    tool->u.cmd.safety = CMD_UNKNOWN;
    tool->u.cmd.blocked = 0;
    tool->u.cmd.approved = -1;
    ChatMsgItem tool_before = *tool;
    chat_msg_apply_ceiling(tool, CMD_READ, CMD_CRITICAL);   /* raise */
    ASSERT_EQ(memcmp(&tool_before, tool, sizeof(*tool)), 0);
    chat_msg_apply_ceiling(tool, CMD_CRITICAL, CMD_READ);   /* lower */
    ASSERT_EQ(memcmp(&tool_before, tool, sizeof(*tool)), 0);
    tool->u.cmd.blocked = 1;
    tool_before = *tool;
    chat_msg_apply_ceiling(tool, CMD_READ, CMD_CRITICAL);   /* raise */
    ASSERT_EQ(memcmp(&tool_before, tool, sizeof(*tool)), 0);
    ASSERT_EQ(tool->u.cmd.blocked, 1);

    /* A held command row is released when the ceiling reaches it. */
    held->u.cmd.safety = CMD_WRITE;
    held->u.cmd.blocked = 1;
    held->u.cmd.approved = 0;
    held->u.cmd.selected = 0;
    chat_msg_apply_ceiling(held, CMD_READ, CMD_WRITE);
    ASSERT_EQ(held->u.cmd.blocked, 0);
    ASSERT_EQ(held->u.cmd.approved, -1);
    ASSERT_EQ(held->u.cmd.selected, 1);

    /* ... but not while the ceiling stays below it. */
    held->u.cmd.safety = CMD_CRITICAL;
    held->u.cmd.blocked = 1;
    held->u.cmd.approved = 0;
    held->u.cmd.selected = 0;
    chat_msg_apply_ceiling(held, CMD_READ, CMD_WRITE);
    ASSERT_EQ(held->u.cmd.blocked, 1);
    ASSERT_EQ(held->u.cmd.approved, 0);
    ASSERT_EQ(held->u.cmd.selected, 0);

    /* A quoted row is never released. */
    quo->u.cmd.safety = CMD_READ;
    quo->u.cmd.quoted = 1;
    quo->u.cmd.blocked = 1;
    chat_msg_apply_ceiling(quo, CMD_READ, CMD_CRITICAL);
    ASSERT_EQ(quo->u.cmd.blocked, 1);

    /* Lowering holds an unsettled, undecided row above the new ceiling. */
    open->u.cmd.safety = CMD_WRITE;
    open->u.cmd.approved = -1;
    open->u.cmd.settled = 0;
    chat_msg_apply_ceiling(open, CMD_CRITICAL, CMD_READ);
    ASSERT_EQ(open->u.cmd.blocked, 1);

    /* ... not a settled one, a decided one, or one within the ceiling. */
    open->u.cmd.blocked = 0;
    open->u.cmd.settled = 1;
    chat_msg_apply_ceiling(open, CMD_CRITICAL, CMD_READ);
    ASSERT_EQ(open->u.cmd.blocked, 0);
    open->u.cmd.settled = 0;
    open->u.cmd.approved = 1;
    chat_msg_apply_ceiling(open, CMD_CRITICAL, CMD_READ);
    ASSERT_EQ(open->u.cmd.blocked, 0);
    open->u.cmd.approved = -1;
    chat_msg_apply_ceiling(open, CMD_CRITICAL, CMD_WRITE);
    ASSERT_EQ(open->u.cmd.blocked, 0);

    /* An unchanged ceiling touches nothing. */
    chat_msg_apply_ceiling(open, CMD_WRITE, CMD_WRITE);
    ASSERT_EQ(open->u.cmd.blocked, 0);

    /* Non-command items and NULL are left alone. */
    char before[sizeof(*usr)];
    memcpy(before, usr, sizeof(*usr));
    chat_msg_apply_ceiling(usr, CMD_READ, CMD_CRITICAL);
    chat_msg_apply_ceiling(usr, CMD_CRITICAL, CMD_READ);
    ASSERT_EQ(memcmp(before, usr, sizeof(*usr)), 0);
    chat_msg_apply_ceiling(NULL, CMD_READ, CMD_CRITICAL);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* ---- The command list: "unfinished" and the summary line (spec 2026-11-05
 * run-command tool, decision 21) ---- */

int test_chat_cmd_label_unfinished(void) {
    TEST_BEGIN();
    ChatCmdIntent intent = CHAT_CMD_INTENT_DIM;
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, CHAT_RUN_UNFINISHED, &intent),
                  "unfinished");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_WARNING);
    /* A new value after ABORTED: the existing ones keep theirs. */
    ASSERT_TRUE(CHAT_RUN_UNFINISHED > CHAT_RUN_ABORTED);
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, CHAT_RUN_ABORTED, &intent), "not run");
    /* The priority order still holds above it. */
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 1, CHAT_RUN_UNFINISHED, NULL),
                  "quoted, not run");
    ASSERT_STR_EQ(chat_cmd_label(1, 1, 0, CHAT_RUN_UNFINISHED, NULL), "held");
    ASSERT_STR_EQ(chat_cmd_label(0, 0, 0, CHAT_RUN_UNFINISHED, NULL), "denied");
    TEST_END();
}

int test_chat_msg_batch_sync_run_executing_ending(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *c0 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c0, "ls", CMD_READ, 0);
    chat_msg_set_batch(c0, 9);
    ChatMsgItem *c1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(c1, "pwd", CMD_READ, 0);
    chat_msg_set_batch(c1, 9);

    ApprovalQueue q;
    chat_approval_init(&q);
    chat_approval_add(&q, "ls", CMD_PLATFORM_LINUX);
    chat_approval_add(&q, "pwd", CMD_PLATFORM_LINUX);
    chat_approval_approve(&q, 0);
    chat_approval_approve(&q, 1);
    chat_approval_set_executing(&q, 0);
    /* entry 1 stays APPROVED, never sent */

    ASSERT_EQ(chat_msg_batch_sync_run(&list, 9, &q, 0), 2);
    ASSERT_EQ(c0->u.cmd.run, CHAT_RUN_RUNNING);
    ASSERT_EQ(c1->u.cmd.run, CHAT_RUN_NONE);

    ASSERT_EQ(chat_msg_batch_sync_run(&list, 9, &q, 1), 2);
    ASSERT_EQ(c0->u.cmd.run, CHAT_RUN_UNFINISHED);  /* EXECUTING + ending */
    ASSERT_EQ(c1->u.cmd.run, CHAT_RUN_ABORTED);     /* APPROVED + ending */
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, c0->u.cmd.run, NULL), "unfinished");
    ASSERT_STR_EQ(chat_cmd_label(1, 0, 0, c1->u.cmd.run, NULL), "not run");

    chat_msg_list_clear(&list);
    TEST_END();
}

/* One settled row per state in batch `batch`. */
static ChatMsgItem *add_row(ChatMsgList *list, int batch, int approved,
                            int blocked, int quoted, int run, int settled)
{
    ChatMsgItem *it = chat_msg_append(list, CHAT_ITEM_COMMAND, "");
    if (!it) return NULL;
    chat_msg_set_command(it, "cmd", CMD_READ, blocked);
    chat_msg_set_batch(it, batch);
    chat_msg_set_quoted(it, quoted);
    it->u.cmd.approved = approved;
    it->u.cmd.run = run;
    it->u.cmd.settled = settled;
    return it;
}

/* One of every state: ran, running, unfinished, queued, awaiting, held (on
 * the card), not run, denied, skipped, quoted. */
static void add_every_state(ChatMsgList *list, int batch)
{
    add_row(list, batch,  1, 0, 0, CHAT_RUN_DONE,       1);
    add_row(list, batch,  1, 0, 0, CHAT_RUN_RUNNING,    1);
    add_row(list, batch,  1, 0, 0, CHAT_RUN_UNFINISHED, 1);
    add_row(list, batch,  1, 0, 0, CHAT_RUN_NONE,       1);
    add_row(list, batch, -1, 0, 0, CHAT_RUN_NONE,       0);   /* on the card */
    add_row(list, batch, -1, 1, 0, CHAT_RUN_NONE,       0);   /* on the card, held */
    add_row(list, batch,  1, 0, 0, CHAT_RUN_ABORTED,    1);
    add_row(list, batch,  0, 0, 0, CHAT_RUN_NONE,       1);
    add_row(list, batch, -1, 0, 0, CHAT_RUN_NONE,       1);   /* settled, skipped */
    add_row(list, batch, -1, 1, 1, CHAT_RUN_NONE,       1);   /* quoted */
}

int test_chat_batch_summary_order(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    add_every_state(&list, 5);

    ChatBatchCounts c;
    ASSERT_EQ(chat_msg_batch_counts(&list, 5, &c), 10);
    ASSERT_EQ(c.n, 10);
    ASSERT_EQ(c.ran, 1);
    ASSERT_EQ(c.running, 1);
    ASSERT_EQ(c.unfinished, 1);
    ASSERT_EQ(c.queued, 1);
    ASSERT_EQ(c.awaiting, 1);
    ASSERT_EQ(c.held, 1);
    ASSERT_EQ(c.not_run, 1);
    ASSERT_EQ(c.denied, 1);
    ASSERT_EQ(c.skipped, 1);
    ASSERT_EQ(c.quoted, 1);
    ASSERT_EQ(c.decided, 8);   /* the settled rows: all but awaiting and held */
    char buf[256];
    int n = chat_batch_summary_text(&c, buf, sizeof(buf));
    ASSERT_STR_EQ(buf,
        "10 commands: 1 ran, 1 running, 1 unfinished, 1 queued, "
        "1 awaiting approval, 1 held, 1 not run, 1 denied, 1 skipped, "
        "1 quoted");
    ASSERT_EQ(n, (int)strlen(buf));
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_batch_summary_zero_omitted(void) {
    TEST_BEGIN();
    ChatBatchCounts c;
    memset(&c, 0, sizeof(c));
    c.n = 3; c.ran = 2; c.queued = 1; c.decided = 3;
    char buf[64];
    int n = chat_batch_summary_text(&c, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "3 commands: 2 ran, 1 queued");
    ASSERT_EQ(n, (int)strlen(buf));
    TEST_END();
}

int test_chat_batch_summary_no_line(void) {
    TEST_BEGIN();
    char buf[64];
    ChatBatchCounts c;
    memset(&c, 0, sizeof(c));

    /* One command, decided: its chip says it all. */
    c.n = 1; c.ran = 1; c.decided = 1;
    strcpy(buf, "stale");
    ASSERT_EQ(chat_batch_summary_text(&c, buf, sizeof(buf)), 0);
    ASSERT_STR_EQ(buf, "");

    /* Three rows, all still on the card: the card header counts them. */
    memset(&c, 0, sizeof(c));
    c.n = 3; c.awaiting = 2; c.held = 1; c.decided = 0;
    strcpy(buf, "stale");
    ASSERT_EQ(chat_batch_summary_text(&c, buf, sizeof(buf)), 0);
    ASSERT_STR_EQ(buf, "");

    /* Nothing at all. */
    memset(&c, 0, sizeof(c));
    strcpy(buf, "stale");
    ASSERT_EQ(chat_batch_summary_text(&c, buf, sizeof(buf)), 0);
    ASSERT_STR_EQ(buf, "");

    /* NULL counts, NULL buffer, zero capacity. */
    ASSERT_EQ(chat_batch_summary_text(NULL, buf, sizeof(buf)), 0);
    c.n = 2; c.ran = 2; c.decided = 2;
    ASSERT_EQ(chat_batch_summary_text(&c, NULL, 64), 0);
    ASSERT_EQ(chat_batch_summary_text(&c, buf, 0), 0);
    TEST_END();
}

int test_chat_batch_summary_head(void) {
    TEST_BEGIN();
    char buf[64];
    ChatBatchCounts c;
    memset(&c, 0, sizeof(c));
    c.n = 2; c.ran = 1; c.denied = 1; c.decided = 2;
    ASSERT_TRUE(chat_batch_summary_text(&c, buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strncmp(buf, "2 commands: ", 12) == 0);
    ASSERT_STR_EQ(buf, "2 commands: 1 ran, 1 denied");

    /* The singular never appears, whatever the counts say. */
    for (int n = 0; n <= 1; n++) {
        memset(&c, 0, sizeof(c));
        c.n = n; c.ran = n; c.decided = n;
        strcpy(buf, "stale");
        ASSERT_EQ(chat_batch_summary_text(&c, buf, sizeof(buf)), 0);
        ASSERT_TRUE(strstr(buf, "1 command") == NULL);
    }
    TEST_END();
}

int test_chat_batch_summary_cap(void) {
    TEST_BEGIN();
    ChatBatchCounts c;
    memset(&c, 0, sizeof(c));
    c.n = 3; c.ran = 2; c.queued = 1; c.decided = 3;
    const char *want = "3 commands: 2 ran, 1 queued";
    size_t len = strlen(want);

    char buf[64];
    strcpy(buf, "stale");
    ASSERT_EQ(chat_batch_summary_text(&c, buf, 10), 0);
    ASSERT_STR_EQ(buf, "");

    /* Exact fit: len + 1 bytes holds it, len does not. */
    char exact[28];
    ASSERT_EQ(sizeof(exact), len + 1);
    ASSERT_EQ(chat_batch_summary_text(&c, exact, sizeof(exact)), (int)len);
    ASSERT_STR_EQ(exact, want);
    strcpy(buf, "stale");
    ASSERT_EQ(chat_batch_summary_text(&c, buf, len), 0);
    ASSERT_STR_EQ(buf, "");
    TEST_END();
}

int test_chat_msg_batch_counts_rows(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* Batch 3: five rows, one still on the card; batch 4: two rows. A
     * non-command item and a row of no batch sit between them. */
    add_row(&list, 3, 1, 0, 0, CHAT_RUN_DONE,    1);
    add_row(&list, 3, 1, 0, 0, CHAT_RUN_DONE,    1);
    chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "text");
    add_row(&list, 4, 0, 0, 0, CHAT_RUN_NONE,    1);
    add_row(&list, 3, 1, 0, 0, CHAT_RUN_RUNNING, 1);
    add_row(&list, 3, -1, 1, 0, CHAT_RUN_NONE,   0);   /* on the card, held */
    add_row(&list, 4, 1, 0, 0, CHAT_RUN_NONE,    1);
    add_row(&list, 3, -1, 0, 0, CHAT_RUN_NONE,   0);   /* on the card */
    add_row(&list, 0, 1, 0, 0, CHAT_RUN_DONE,    1);

    ChatBatchCounts c3, c4, c9;
    ASSERT_EQ(chat_msg_batch_counts(&list, 3, &c3), 5);
    ASSERT_EQ(c3.n, 5);
    ASSERT_EQ(c3.ran, 2);
    ASSERT_EQ(c3.running, 1);
    ASSERT_EQ(c3.held, 1);
    ASSERT_EQ(c3.awaiting, 1);
    ASSERT_EQ(c3.decided, 3);                 /* rows off the card */
    ASSERT_EQ(c3.denied + c3.queued + c3.quoted + c3.skipped + c3.not_run +
              c3.unfinished, 0);

    ASSERT_EQ(chat_msg_batch_counts(&list, 4, &c4), 2);
    ASSERT_EQ(c4.n, 2);
    ASSERT_EQ(c4.denied, 1);
    ASSERT_EQ(c4.queued, 1);
    ASSERT_EQ(c4.decided, 2);

    /* An id no row carries, a NULL list, a NULL out. */
    memset(&c9, 0x5A, sizeof(c9));
    ASSERT_EQ(chat_msg_batch_counts(&list, 99, &c9), 0);
    ASSERT_EQ(c9.n, 0);
    ASSERT_EQ(c9.ran + c9.decided, 0);
    ASSERT_EQ(chat_msg_batch_counts(NULL, 3, &c9), 0);
    ASSERT_EQ(chat_msg_batch_counts(&list, 3, NULL), 0);

    /* The summary of each batch, from its own counts only. */
    char buf[128];
    ASSERT_TRUE(chat_batch_summary_text(&c3, buf, sizeof(buf)) > 0);
    ASSERT_STR_EQ(buf, "5 commands: 2 ran, 1 running, 1 awaiting approval, 1 held");
    ASSERT_TRUE(chat_batch_summary_text(&c4, buf, sizeof(buf)) > 0);
    ASSERT_STR_EQ(buf, "2 commands: 1 queued, 1 denied");

    /* Each row's tally agrees with its chip. */
    int ran = 0;
    for (ChatMsgItem *it = list.head; it; it = it->next)
        if (it->type == CHAT_ITEM_COMMAND && it->u.cmd.batch == 3 &&
            strcmp(chat_cmd_label(it->u.cmd.approved, it->u.cmd.blocked,
                                  it->u.cmd.quoted, it->u.cmd.run, NULL),
                   "ran") == 0)
            ran++;
    ASSERT_EQ(ran, c3.ran);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* ---- chat_msg_append_batch_items: the rebuild's replay of a live batch
 * (moved from src/ui/ai_chat_view.c; Part A's review items 2, R60) ---- */

/* A batch of `n` commands ("c0", "c1", ...) in `set`, every row PENDING. */
static CmdBatch *replay_batch(CmdBatchSet *set, int n)
{
    CmdBatch *b = cmd_batch_add(set, NULL, NULL);
    if (!b) return NULL;
    for (int i = 0; i < n; i++) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "c%d", i);
        if (chat_approval_add(&b->q, cmd, CMD_PLATFORM_LINUX) < 0) return NULL;
        b->q.entries[i].status = APPROVE_PENDING;
    }
    return b;
}

int test_chat_batch_items_settled_survives_ceiling_raise(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = replay_batch(&set, 3);
    ASSERT_NOT_NULL(b);
    /* Settled with one denied, one still held by the ceiling, one ran. */
    b->q.entries[0].status = APPROVE_DENIED;
    b->q.entries[1].status = APPROVE_BLOCKED;
    b->q.entries[2].status = APPROVE_COMPLETED;
    b->settled = 1;

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_msg_append_batch_items(&list, b);
    ASSERT_EQ(list.count, 3);
    for (ChatMsgItem *it = list.head; it; it = it->next) {
        ASSERT_EQ(it->u.cmd.settled, 1);
        ASSERT_EQ(it->u.cmd.batch, b->id);
    }
    ASSERT_EQ(list.head->u.cmd.approved, 0);
    ASSERT_EQ(list.head->next->u.cmd.blocked, 1);
    chat_msg_list_clear(&list);

    /* The ceiling is raised: the held row turns PENDING. The batch is still
     * settled, so the rebuild still draws every row inline. */
    b->q.entries[1].status = APPROVE_PENDING;
    chat_msg_append_batch_items(&list, b);
    ASSERT_EQ(list.count, 3);
    for (ChatMsgItem *it = list.head; it; it = it->next)
        ASSERT_EQ(it->u.cmd.settled, 1);
    ASSERT_EQ(list.head->next->u.cmd.blocked, 0);
    ASSERT_EQ(list.head->next->u.cmd.approved, -1);
    chat_msg_list_clear(&list);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_chat_batch_items_card_survives_ceiling_drop(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = replay_batch(&set, 2);
    ASSERT_NOT_NULL(b);
    /* The ceiling dropped below both rows: every row BLOCKED, card still up
     * (never settled). Not mistaken for a decided batch. */
    b->q.entries[0].status = APPROVE_BLOCKED;
    b->q.entries[1].status = APPROVE_BLOCKED;

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_msg_append_batch_items(&list, b);
    ASSERT_EQ(list.count, 2);
    for (ChatMsgItem *it = list.head; it; it = it->next) {
        ASSERT_EQ(it->u.cmd.settled, 0);
        ASSERT_EQ(it->u.cmd.blocked, 1);
        ASSERT_EQ(it->u.cmd.approved, -1);
    }
    ChatBatchCounts c;
    ASSERT_EQ(chat_msg_batch_counts(&list, b->id, &c), 2);
    ASSERT_EQ(c.held, 2);
    ASSERT_EQ(c.decided, 0);
    char line[64];
    ASSERT_EQ(chat_batch_summary_text(&c, line, sizeof(line)), 0);
    chat_msg_list_clear(&list);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_chat_batch_items_half_decided_card(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = replay_batch(&set, 3);
    ASSERT_NOT_NULL(b);
    /* One row approved, one denied, one still asking: the card is up. */
    b->q.entries[0].status = APPROVE_APPROVED;
    b->q.entries[1].status = APPROVE_DENIED;
    b->q.entries[2].status = APPROVE_PENDING;

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_msg_append_batch_items(&list, b);
    ASSERT_EQ(list.count, 3);
    ChatMsgItem *r0 = list.head, *r1 = r0->next, *r2 = r1->next;
    ASSERT_EQ(r0->u.cmd.approved, 1);
    ASSERT_EQ(r1->u.cmd.approved, 0);
    ASSERT_EQ(r2->u.cmd.approved, -1);
    ASSERT_EQ(r0->u.cmd.settled, 0);       /* decided rows stay on the card */
    ASSERT_EQ(r1->u.cmd.settled, 0);
    ASSERT_EQ(r2->u.cmd.settled, 0);
    ASSERT_STR_EQ(r0->u.cmd.command, "c0");
    ASSERT_STR_EQ(r2->u.cmd.command, "c2");
    ChatBatchCounts c;
    ASSERT_EQ(chat_msg_batch_counts(&list, b->id, &c), 3);
    ASSERT_EQ(c.decided, 0);               /* no summary line yet */
    chat_msg_list_clear(&list);
    cmd_batch_set_free(&set);
    TEST_END();
}

int test_chat_batch_items_run_state_and_note(void) {
    TEST_BEGIN();
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = replay_batch(&set, 2);
    ASSERT_NOT_NULL(b);
    b->q.entries[0].status = APPROVE_COMPLETED;
    b->q.entries[1].status = APPROVE_EXECUTING;
    chat_approval_set_note(&b->q, 0, "list the files");
    b->settled = 1;

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_msg_append_batch_items(&list, b);
    ASSERT_EQ(list.count, 2);
    ASSERT_EQ(list.head->u.cmd.run, CHAT_RUN_DONE);
    ASSERT_EQ(list.head->next->u.cmd.run, CHAT_RUN_RUNNING);
    ASSERT_STR_EQ(list.head->u.cmd.note, "list the files");
    /* NULL arguments change nothing. */
    chat_msg_append_batch_items(NULL, b);
    chat_msg_append_batch_items(&list, NULL);
    ASSERT_EQ(list.count, 2);
    chat_msg_list_clear(&list);
    cmd_batch_set_free(&set);
    TEST_END();
}

/* --- Folding a finished command card to one row ------------------------- */

static ChatBatchCounts fold_counts(int ran, int running, int unfinished,
                                   int queued, int awaiting, int held,
                                   int not_run, int denied, int skipped,
                                   int quoted, int decided)
{
    ChatBatchCounts c;
    memset(&c, 0, sizeof(c));
    c.ran = ran; c.running = running; c.unfinished = unfinished;
    c.queued = queued; c.awaiting = awaiting; c.held = held;
    c.not_run = not_run; c.denied = denied; c.skipped = skipped;
    c.quoted = quoted; c.decided = decided;
    c.n = ran + running + unfinished + queued + awaiting + held + not_run +
          denied + skipped + quoted;
    return c;
}

int test_chat_batch_is_final_states(void) {
    TEST_BEGIN();
    /* ran + denied + not run, all left the card: final */
    ChatBatchCounts c = fold_counts(1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 3);
    ASSERT_EQ(chat_batch_is_final(&c), 1);
    /* one command, ran */
    c = fold_counts(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
    ASSERT_EQ(chat_batch_is_final(&c), 1);
    /* one row still on the card (decided < n) */
    c = fold_counts(2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 2);
    ASSERT_EQ(chat_batch_is_final(&c), 0);
    /* running, queued: not final even when every row is settled */
    c = fold_counts(1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2);
    ASSERT_EQ(chat_batch_is_final(&c), 0);
    c = fold_counts(1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 2);
    ASSERT_EQ(chat_batch_is_final(&c), 0);
    /* nothing at all, NULL */
    c = fold_counts(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    ASSERT_EQ(chat_batch_is_final(&c), 0);
    ASSERT_EQ(chat_batch_is_final(NULL), 0);
    TEST_END();
}

int test_chat_batch_fold_text_plural_singular(void) {
    TEST_BEGIN();
    char label[32], sum[96];
    int warn = -1;
    ChatBatchCounts c = fold_counts(3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3);
    ASSERT_EQ(chat_batch_fold_text(&c, label, sizeof(label), sum, sizeof(sum),
                                   &warn), 1);
    ASSERT_STR_EQ(label, "3 commands");
    ASSERT_STR_EQ(sum, "\xC2\xB7 3 ran");
    ASSERT_EQ(warn, 0);

    c = fold_counts(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
    ASSERT_EQ(chat_batch_fold_text(&c, label, sizeof(label), sum, sizeof(sum),
                                   NULL), 1);
    ASSERT_STR_EQ(label, "1 command");
    ASSERT_STR_EQ(sum, "\xC2\xB7 1 ran");

    c = fold_counts(1, 0, 1, 0, 0, 0, 2, 0, 0, 0, 4);
    ASSERT_EQ(chat_batch_fold_text(&c, label, sizeof(label), sum, sizeof(sum),
                                   &warn), 1);
    ASSERT_STR_EQ(label, "4 commands");
    ASSERT_STR_EQ(sum, "\xC2\xB7 1 ran, 1 unfinished, 2 not run");
    TEST_END();
}

int test_chat_batch_fold_text_warn(void) {
    TEST_BEGIN();
    char label[32], sum[96];
    int warn = 0;
    /* unfinished, not run, held each warn */
    ChatBatchCounts c = fold_counts(1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2);
    ASSERT_EQ(chat_batch_fold_text(&c, label, sizeof(label), sum, sizeof(sum), &warn), 1);
    ASSERT_EQ(warn, 1);
    c = fold_counts(1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2);
    ASSERT_EQ(chat_batch_fold_text(&c, label, sizeof(label), sum, sizeof(sum), &warn), 1);
    ASSERT_EQ(warn, 1);
    c = fold_counts(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    c.decided = 2;   /* a settled held row */
    ASSERT_EQ(chat_batch_fold_text(&c, label, sizeof(label), sum, sizeof(sum), &warn), 1);
    ASSERT_EQ(warn, 1);
    /* ran and denied alone do not */
    c = fold_counts(2, 0, 0, 0, 0, 0, 0, 1, 0, 0, 3);
    ASSERT_EQ(chat_batch_fold_text(&c, label, sizeof(label), sum, sizeof(sum), &warn), 1);
    ASSERT_EQ(warn, 0);
    ASSERT_STR_EQ(sum, "\xC2\xB7 2 ran, 1 denied");
    TEST_END();
}

int test_chat_batch_fold_text_not_final_or_small_cap(void) {
    TEST_BEGIN();
    char label[32], sum[96];
    ChatBatchCounts c = fold_counts(1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2);
    strcpy(label, "x"); strcpy(sum, "x");
    ASSERT_EQ(chat_batch_fold_text(&c, label, sizeof(label), sum, sizeof(sum), NULL), 0);
    ASSERT_STR_EQ(label, "");
    ASSERT_STR_EQ(sum, "");

    c = fold_counts(3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3);
    char tiny[5];
    strcpy(label, "x"); strcpy(sum, "x");
    ASSERT_EQ(chat_batch_fold_text(&c, tiny, sizeof(tiny), sum, sizeof(sum), NULL), 0);
    ASSERT_STR_EQ(tiny, "");
    ASSERT_STR_EQ(sum, "");
    strcpy(label, "x");
    ASSERT_EQ(chat_batch_fold_text(&c, label, sizeof(label), tiny, sizeof(tiny), NULL), 0);
    ASSERT_STR_EQ(label, "");
    ASSERT_STR_EQ(tiny, "");
    ASSERT_EQ(chat_batch_fold_text(&c, NULL, 0, sum, sizeof(sum), NULL), 0);
    ASSERT_EQ(chat_batch_fold_text(NULL, label, sizeof(label), sum, sizeof(sum), NULL), 0);
    TEST_END();
}

int test_chat_fold_refresh_closes_final_group(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = add_row(&list, 4, 1, 0, 0, CHAT_RUN_DONE,    1);
    ChatMsgItem *b = add_row(&list, 4, 0, 0, 0, CHAT_RUN_NONE,    1);
    ChatMsgItem *c = add_row(&list, 4, 1, 0, 0, CHAT_RUN_ABORTED, 1);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 3);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_CLOSED);
    ASSERT_EQ(a->u.cmd.fold_hidden, 0);
    ASSERT_EQ(b->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(b->u.cmd.fold_hidden, 1);
    ASSERT_EQ(c->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(c->u.cmd.fold_hidden, 1);
    /* a second call changes nothing */
    ASSERT_EQ(chat_msg_fold_refresh(&list), 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_single_command_folds(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = add_row(&list, 7, 1, 0, 0, CHAT_RUN_DONE, 1);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 1);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_CLOSED);
    ASSERT_EQ(a->u.cmd.fold_hidden, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_pending_stays_whole(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* a settled ran row, one still on the card, one running, one queued */
    ChatMsgItem *r0 = add_row(&list, 3, 1, 0, 0, CHAT_RUN_DONE,    1);
    ChatMsgItem *r1 = add_row(&list, 3, -1, 0, 0, CHAT_RUN_NONE,   0);
    ChatMsgItem *r2 = add_row(&list, 3, 1, 0, 0, CHAT_RUN_RUNNING, 1);
    ChatMsgItem *r3 = add_row(&list, 3, 1, 0, 0, CHAT_RUN_NONE,    1);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 0);
    ChatMsgItem *rows[4] = { r0, r1, r2, r3 };
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(rows[i]->u.cmd.fold, CHAT_FOLD_NONE);
        ASSERT_EQ(rows[i]->u.cmd.fold_hidden, 0);
    }
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_unfolds_when_work_returns(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = add_row(&list, 3, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *b = add_row(&list, 3, 1, 0, 0, CHAT_RUN_DONE, 1);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 2);
    ASSERT_EQ(b->u.cmd.fold_hidden, 1);
    b->u.cmd.run = CHAT_RUN_RUNNING;
    ASSERT_EQ(chat_msg_fold_refresh(&list), 2);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(b->u.cmd.fold_hidden, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_open_choice(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = add_row(&list, 2, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *b = add_row(&list, 2, 1, 0, 0, CHAT_RUN_DONE, 1);
    chat_msg_fold_refresh(&list);
    ASSERT_EQ(chat_msg_fold_toggle(a), 1);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 2);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_OPEN);
    ASSERT_EQ(a->u.cmd.fold_hidden, 0);
    ASSERT_EQ(b->u.cmd.fold_hidden, 0);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 0);
    /* a second toggle folds again */
    ASSERT_EQ(chat_msg_fold_toggle(a), 0);
    chat_msg_fold_refresh(&list);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_CLOSED);
    ASSERT_EQ(b->u.cmd.fold_hidden, 1);
    /* a toggle on a row that heads no fold does nothing */
    ASSERT_EQ(chat_msg_fold_toggle(b), 0);
    ASSERT_EQ(b->u.cmd.fold_open, 0);
    ASSERT_EQ(chat_msg_fold_toggle(NULL), 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_skips_tools_and_batch_zero(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *z = add_row(&list, 0, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *t = add_row(&list, 6, 1, 0, 0, CHAT_RUN_DONE, 1);
    chat_msg_set_tool(t, 1);
    ChatMsgItem *m1 = add_row(&list, 8, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *m2 = add_row(&list, 8, 1, 0, 0, CHAT_RUN_DONE, 1);
    chat_msg_set_tool(m2, 1);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 0);
    ASSERT_EQ(z->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(t->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(m1->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(m2->u.cmd.fold_hidden, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_adjacent_batches_separate(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a1 = add_row(&list, 1, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *a2 = add_row(&list, 1, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *b1 = add_row(&list, 2, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *b2 = add_row(&list, 2, 1, 0, 0, CHAT_RUN_RUNNING, 1);
    chat_msg_fold_refresh(&list);
    ASSERT_EQ(a1->u.cmd.fold, CHAT_FOLD_CLOSED);
    ASSERT_EQ(a2->u.cmd.fold_hidden, 1);
    ASSERT_EQ(b1->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(b1->u.cmd.fold_hidden, 0);
    ASSERT_EQ(b2->u.cmd.fold_hidden, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_split_batch_stays_whole(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* a batch's rows with prose between them are not one group: never fold */
    ChatMsgItem *a = add_row(&list, 5, 1, 0, 0, CHAT_RUN_DONE, 1);
    chat_msg_append(&list, CHAT_ITEM_STATUS, "between");
    ChatMsgItem *b = add_row(&list, 5, 1, 0, 0, CHAT_RUN_DONE, 1);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 0);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(b->u.cmd.fold_hidden, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_held_row_on_card_never_folds(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = add_row(&list, 3, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *b = add_row(&list, 3, 1, 0, 0, CHAT_RUN_DONE, 1);
    /* a held row still on the card: blocked, not approved, not settled */
    ChatMsgItem *h = add_row(&list, 3, 0, 1, 0, CHAT_RUN_NONE, 0);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 0);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(b->u.cmd.fold_hidden, 0);
    ASSERT_EQ(h->u.cmd.fold_hidden, 0);
    /* the held row sitting first changes nothing either */
    chat_msg_list_clear(&list);
    chat_msg_list_init(&list);
    h = add_row(&list, 3, 0, 1, 0, CHAT_RUN_NONE, 0);
    a = add_row(&list, 3, 1, 0, 0, CHAT_RUN_DONE, 1);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 0);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_NONE);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_rebuilt_negative_batch_folds(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = add_row(&list, -2, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *b = add_row(&list, -2, 1, 0, 0, CHAT_RUN_UNFINISHED, 1);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 2);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_CLOSED);
    ASSERT_EQ(b->u.cmd.fold_hidden, 1);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_refresh_open_choice_survives_unfold_refold(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = add_row(&list, 2, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *b = add_row(&list, 2, 1, 0, 0, CHAT_RUN_RUNNING, 1);
    /* b still runs: no fold; the user's (earlier) open choice is kept */
    a->u.cmd.fold_open = 1;
    chat_msg_fold_refresh(&list);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_NONE);
    ASSERT_EQ(a->u.cmd.fold_open, 1);
    /* b finishes: the group comes back open, not closed */
    b->u.cmd.run = CHAT_RUN_DONE;
    ASSERT_EQ(chat_msg_fold_refresh(&list), 1);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_OPEN);
    ASSERT_EQ(b->u.cmd.fold_hidden, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_chat_fold_toggle_on_hidden_member_is_noop(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = add_row(&list, 2, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *b = add_row(&list, 2, 1, 0, 0, CHAT_RUN_DONE, 1);
    chat_msg_fold_refresh(&list);
    ASSERT_EQ(b->u.cmd.fold_hidden, 1);
    ASSERT_EQ(chat_msg_fold_toggle(b), 0);
    ASSERT_EQ(b->u.cmd.fold_open, 0);
    ASSERT_EQ(chat_msg_fold_refresh(&list), 0);
    ASSERT_EQ(a->u.cmd.fold, CHAT_FOLD_CLOSED);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* A fold change marks every row of the group dirty (their heights and the
 * last row's summary line change); a refresh that changes nothing leaves
 * the flags alone, so layout remeasures only what a fold touched. */
int test_chat_fold_refresh_marks_changed_group_dirty(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    ChatMsgItem *a = add_row(&list, 1, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *b = add_row(&list, 1, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *c = add_row(&list, 1, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *o = add_row(&list, 7, 1, 0, 0, CHAT_RUN_DONE, 1);
    ChatMsgItem *p = add_row(&list, 7, 1, 0, 0, CHAT_RUN_RUNNING, 1);
    a->dirty = b->dirty = c->dirty = o->dirty = p->dirty = 0;
    chat_msg_fold_refresh(&list);
    ASSERT_EQ(a->dirty, 1);
    ASSERT_EQ(b->dirty, 1);
    ASSERT_EQ(c->dirty, 1);
    ASSERT_EQ(o->dirty, 0);   /* batch 7 did not change */
    ASSERT_EQ(p->dirty, 0);
    a->dirty = b->dirty = c->dirty = 0;
    ASSERT_EQ(chat_msg_fold_refresh(&list), 0);
    ASSERT_EQ(a->dirty, 0);
    /* opening it: NONE members' heights and the last row's summary change,
     * even though only the head's fold value moves from CLOSED to OPEN */
    chat_msg_fold_toggle(a);
    chat_msg_fold_refresh(&list);
    ASSERT_EQ(a->dirty, 1);
    ASSERT_EQ(b->dirty, 1);
    ASSERT_EQ(c->dirty, 1);
    ASSERT_EQ(o->dirty, 0);
    chat_msg_list_clear(&list);
    TEST_END();
}
