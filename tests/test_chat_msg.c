/* tests/test_chat_msg.c */
#include "test_framework.h"
#include "chat_msg.h"
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
    ASSERT_EQ(chat_msg_set_command(item, "ls -la", CMD_SAFE, 0), 0);
    ASSERT_STR_EQ(item->u.cmd.command, "ls -la");
    ASSERT_EQ((int)item->u.cmd.safety, (int)CMD_SAFE);
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
    ASSERT_TRUE(chat_msg_set_command(item, long_cmd, CMD_SAFE, 0) != 0);
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
    chat_msg_set_command(item, "ls", CMD_SAFE, 0);
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
    chat_msg_set_command(item, "ls", CMD_SAFE, 0);
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
    chat_msg_set_command(a1, "echo a1", CMD_SAFE, 0);
    chat_msg_set_batch(a1, 1);
    ChatMsgItem *a2 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(a2, "echo a2", CMD_SAFE, 0);
    chat_msg_set_batch(a2, 1);

    chat_msg_append(&list, CHAT_ITEM_AI_TEXT, "in between");

    ChatMsgItem *b1 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(b1, "echo b1", CMD_SAFE, 0);
    chat_msg_set_batch(b1, 2);
    ChatMsgItem *b2 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(b2, "echo b2", CMD_SAFE, 0);
    chat_msg_set_batch(b2, 2);
    ChatMsgItem *b3 = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(b3, "echo b3", CMD_SAFE, 0);
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
    chat_msg_set_command(a1, "echo a1", CMD_SAFE, 0);
    chat_msg_set_batch(a1, 1);
    ASSERT_EQ(chat_msg_batch_settle(&list, 99), 0);
    ASSERT_EQ(a1->u.cmd.settled, 0);
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
