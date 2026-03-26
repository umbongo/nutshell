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
