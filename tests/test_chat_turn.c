/* tests/test_chat_turn.c -- which row carries a turn's Read aloud button,
 * and the turn's speakable text (src/core/chat_turn.c). */
#include "test_framework.h"
#include "chat_msg.h"
#include "chat_turn.h"
#include <stdlib.h>
#include <string.h>

static ChatMsgItem *U(ChatMsgList *l, const char *t) { return chat_msg_append(l, CHAT_ITEM_USER, t); }
static ChatMsgItem *A(ChatMsgList *l, const char *t) { return chat_msg_append(l, CHAT_ITEM_AI_TEXT, t); }
static ChatMsgItem *S(ChatMsgList *l, const char *t) { return chat_msg_append(l, CHAT_ITEM_STATUS, t); }
static ChatMsgItem *C(ChatMsgList *l, int settled, int run)
{
    ChatMsgItem *it = chat_msg_append(l, CHAT_ITEM_COMMAND, "");
    if (!it) return NULL;
    chat_msg_set_command(it, "ip link", CMD_READ, 0);
    it->u.cmd.approved = 1;
    it->u.cmd.settled = settled;
    it->u.cmd.run = run;
    return it;
}

int test_chat_turn_plain(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    ChatMsgItem *u = U(&l, "Why is eth0 down?");
    ChatMsgItem *a = A(&l, "The peer stopped sending keepalives.");
    ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 1);
    ASSERT_EQ(chat_turn_carries_button(&l, u->id, 0), 0);
    a->dirty = 0;
    ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 1);
    ASSERT_EQ(a->u.ai.turn_btn, 1);
    ASSERT_EQ(a->dirty, 1);
    ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 0);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_tools_three_rows(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    U(&l, "Why?");
    ChatMsgItem *a1 = A(&l, "Let me check the interface:");
    C(&l, 1, CHAT_RUN_DONE);
    ChatMsgItem *a2 = A(&l, "The counters show drops.");
    C(&l, 1, CHAT_RUN_DONE);
    ChatMsgItem *a3 = A(&l, "Check the cable first.");
    ASSERT_EQ(chat_turn_carries_button(&l, a1->id, 0), 0);
    ASSERT_EQ(chat_turn_carries_button(&l, a2->id, 0), 0);
    ASSERT_EQ(chat_turn_carries_button(&l, a3->id, 0), 1);
    char *t = chat_turn_text(&l, a3->id, 0);
    ASSERT_NOT_NULL(t);
    if (t) ASSERT_STR_EQ(t, "Let me check the interface. The counters show drops. "
                            "Check the cable first.");
    free(t);
    ASSERT_NULL(chat_turn_text(&l, a1->id, 0));
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_still_streaming(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    U(&l, "First?");
    ChatMsgItem *a0 = A(&l, "First answer.");
    U(&l, "Second?");
    ChatMsgItem *a1 = A(&l, "Partial");
    ASSERT_EQ(chat_turn_carries_button(&l, a1->id, 1), 0);
    ASSERT_EQ(chat_turn_carries_button(&l, a0->id, 1), 1);
    ASSERT_EQ(chat_turn_carries_button(&l, a1->id, 0), 1);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_awaiting_approval(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    U(&l, "Fix it");
    ChatMsgItem *a = A(&l, "I would run this:");
    ChatMsgItem *c = C(&l, 0, CHAT_RUN_NONE);
    c->u.cmd.approved = -1;
    ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 0);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_running_command_no_button(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    U(&l, "Fix it");
    ChatMsgItem *a = A(&l, "Running it now.");
    C(&l, 1, CHAT_RUN_RUNNING);
    ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 0);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_no_text(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    U(&l, "One");
    S(&l, "Error: HTTP 503");
    U(&l, "Two");
    ChatMsgItem *blank = A(&l, "   \n");
    U(&l, "Three");
    ChatMsgItem *think = A(&l, "");
    chat_msg_set_thinking(think, "hm");
    ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 0);
    ASSERT_EQ(chat_turn_is_text(blank), 0);
    ASSERT_EQ(chat_turn_is_text(think), 0);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_last_turn(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    U(&l, "U1");
    ChatMsgItem *a1 = A(&l, "One.");
    U(&l, "U2");
    ChatMsgItem *a2 = A(&l, "Two.");
    ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 2);
    ASSERT_EQ(a1->u.ai.turn_btn + a2->u.ai.turn_btn, 2);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_stopped(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    U(&l, "Look");
    ChatMsgItem *a = A(&l, "Let me look.");
    C(&l, 1, CHAT_RUN_ABORTED);
    S(&l, "Stream cancelled by user.");
    ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 1);
    char *t = chat_turn_text(&l, a->id, 0);
    ASSERT_NOT_NULL(t);
    if (t) ASSERT_STR_EQ(t, "Let me look.");
    free(t);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_status_after_reply_keeps_button(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    U(&l, "Hi");
    ChatMsgItem *a = A(&l, "Done.");
    S(&l, "Could not read the reply aloud: the Windows voice did not start.");
    ASSERT_EQ(chat_turn_carries_button(&l, a->id, 0), 1);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_refresh_off_clears(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    U(&l, "Hi");
    ChatMsgItem *a = A(&l, "Done.");
    ChatMsgItem *c = C(&l, 1, CHAT_RUN_DONE);
    ASSERT_EQ(chat_turn_refresh(&l, 1, 0), 1);
    ASSERT_EQ(chat_turn_refresh(&l, 0, 0), 1);
    ASSERT_EQ(a->u.ai.turn_btn, 0);
    ASSERT_EQ(chat_turn_refresh(&l, 0, 0), 0);
    ASSERT_EQ(c->u.cmd.approved, 1);
    ASSERT_EQ(c->u.cmd.settled, 1);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_turn_text_join(void)
{
    TEST_BEGIN();
    ChatMsgList l; chat_msg_list_init(&l);
    ChatMsgItem *u = U(&l, "Hi");
    A(&l, "Let me check:");
    C(&l, 1, CHAT_RUN_DONE);
    A(&l, "Done");
    C(&l, 1, CHAT_RUN_DONE);
    ChatMsgItem *a = A(&l, "Really?");
    char *t = chat_turn_text(&l, a->id, 0);
    ASSERT_NOT_NULL(t);
    if (t) ASSERT_STR_EQ(t, "Let me check. Done. Really?");
    free(t);
    ASSERT_NULL(chat_turn_text(&l, 9999, 0));
    ASSERT_NULL(chat_turn_text(&l, u->id, 0));
    chat_msg_list_clear(&l);
    TEST_END();
}
