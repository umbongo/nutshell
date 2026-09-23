#include "test_framework.h"
#include "ui_demo.h"
#include "chat_msg.h"
#include <string.h>

/* ===========================================================================
 * ui_demo tests (Design-System Foundation, task 9; extended for pending
 * command batches, docs/superpowers/specs/
 * 2026-09-09-pending-command-batches.md) -- see
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 5 ("Verification harness").
 * ===========================================================================
 */

static int count_status(const ApprovalQueue *q, ApprovalStatus status)
{
    int n = 0;
    for (int i = 0; i < q->count; i++)
        if (q->entries[i].status == status) n++;
    return n;
}

/* Eleven since v1.2.1 added "local" (the local-shell tab chrome demo,
 * spec 2026-09-22-local-shell-design.md section 5). "all" stays last and
 * still means the union of the panel states, which "local" is not part of:
 * it only swaps the terminal transcript. */
int test_ui_demo_states_list_ends_in_all(void)
{
    TEST_BEGIN();
    int count = 0;
    const char *const *states = ui_demo_states(&count);
    ASSERT_NOT_NULL(states);
    ASSERT_EQ(count, 11);
    ASSERT_STR_EQ(states[count - 1], "all");
    ASSERT_STR_EQ(states[count - 2], "local");
    TEST_END();
}

/* "local" is a real state, and it changes the terminal transcript without
 * adding anything to the conversation or the approval queues. */
int test_ui_demo_build_local_swaps_terminal_text_only(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char term_buf[8192];

    ASSERT_TRUE(ui_demo_state_valid("local"));
    ASSERT_EQ(ui_demo_build("local", &conv, &approval, &approval2,
                            term_buf, sizeof(term_buf)), 0);
    /* System message only -- no user/assistant turns, no commands. */
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ(approval2.count, 0);
    ASSERT_TRUE(strstr(term_buf, "busybox") != NULL);
    ASSERT_TRUE(strstr(term_buf, "web-01") == NULL);

    /* Every other state keeps the SSH transcript. */
    char ssh_buf[8192];
    ASSERT_EQ(ui_demo_build("chat", &conv, &approval, &approval2,
                            ssh_buf, sizeof(ssh_buf)), 0);
    ASSERT_TRUE(strstr(ssh_buf, "web-01") != NULL);
    ASSERT_EQ(ui_demo_build("all", &conv, &approval, &approval2,
                            ssh_buf, sizeof(ssh_buf)), 0);
    ASSERT_TRUE(strstr(ssh_buf, "web-01") != NULL);
    TEST_END();
}

int test_ui_demo_state_valid_accepts_every_listed_state(void)
{
    TEST_BEGIN();
    int count = 0;
    const char *const *states = ui_demo_states(&count);
    for (int i = 0; i < count; i++)
        ASSERT_TRUE(ui_demo_state_valid(states[i]));
    TEST_END();
}

int test_ui_demo_state_valid_rejects_unknown_and_null(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(!ui_demo_state_valid("bogus"));
    ASSERT_TRUE(!ui_demo_state_valid(""));
    ASSERT_TRUE(!ui_demo_state_valid(NULL));
    TEST_END();
}

int test_ui_demo_build_unknown_state_returns_error(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char term[64];
    ASSERT_EQ(ui_demo_build("bogus", &conv, &approval, &approval2, term, sizeof(term)), -1);
    TEST_END();
}

int test_ui_demo_build_null_outputs_safe(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ui_demo_build("chat", NULL, NULL, NULL, NULL, 0), 0);
    TEST_END();
}

int test_ui_demo_build_chat_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("chat", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3); /* system, user, assistant */
    ASSERT_EQ((int)conv.messages[0].role, (int)AI_ROLE_SYSTEM);
    ASSERT_EQ((int)conv.messages[1].role, (int)AI_ROLE_USER);
    ASSERT_EQ((int)conv.messages[2].role, (int)AI_ROLE_ASSISTANT);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ(approval2.count, 0);
    TEST_END();
}

int test_ui_demo_build_approval_counts_and_statuses(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("approval", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3);
    ASSERT_EQ(approval.count, 4);
    ASSERT_EQ(count_status(&approval, APPROVE_PENDING), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_APPROVED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_DENIED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_BLOCKED), 1);
    ASSERT_EQ(approval2.count, 0);
    TEST_END();
}

int test_ui_demo_build_executing_counts_and_statuses(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("executing", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3);
    ASSERT_EQ(approval.count, 2);
    ASSERT_EQ(count_status(&approval, APPROVE_COMPLETED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_EXECUTING), 1);
    ASSERT_EQ(approval2.count, 0);
    TEST_END();
}

/* The "executing" state replayed the way ai_chat.c's
 * append_batch_command_items() rebuilds a card -- one CHAT_ITEM_COMMAND per
 * queue entry, then chat_msg_batch_sync_run() from that same queue -- must
 * carry the live panel's run states: the COMPLETED command reads "ran", the
 * EXECUTING one "running". Before the 2026-09-23 dispatch-states change a
 * rebuild painted both of them "ran". */
int test_ui_demo_executing_rebuild_carries_run_states(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("executing", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(approval.count, 2);

    ChatMsgList list;
    chat_msg_list_init(&list);
    for (int i = 0; i < approval.count; i++) {
        ChatMsgItem *it = chat_msg_append(&list, CHAT_ITEM_COMMAND, "");
        ASSERT_NOT_NULL(it);
        chat_msg_set_command(it, approval.entries[i].command,
                             approval.entries[i].safety,
                             approval.entries[i].status == APPROVE_BLOCKED);
        chat_msg_set_batch(it, 7);
        /* Both are past the approval card: the replay settles them. */
        it->u.cmd.approved = 1;
        it->u.cmd.settled = 1;
    }
    ASSERT_EQ(chat_msg_batch_sync_run(&list, 7, &approval, 0), 2);

    ChatMsgItem *first = list.head;
    ASSERT_NOT_NULL(first);
    ChatMsgItem *second = first->next;
    ASSERT_NOT_NULL(second);
    ASSERT_EQ(first->u.cmd.run, CHAT_RUN_DONE);
    ASSERT_EQ(second->u.cmd.run, CHAT_RUN_RUNNING);

    ChatCmdIntent intent = CHAT_CMD_INTENT_DIM;
    ASSERT_STR_EQ(chat_cmd_label(first->u.cmd.approved, first->u.cmd.blocked,
                                 first->u.cmd.run, &intent), "ran");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_SUCCESS);
    ASSERT_STR_EQ(chat_cmd_label(second->u.cmd.approved, second->u.cmd.blocked,
                                 second->u.cmd.run, &intent), "running");
    ASSERT_EQ((int)intent, (int)CHAT_CMD_INTENT_INFO);

    chat_msg_list_clear(&list);
    TEST_END();
}

int test_ui_demo_build_tool_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("tool", &conv, &approval, &approval2, NULL, 0), 0);
    /* system, user, assistant(tool_use), tool result, assistant summary */
    ASSERT_EQ(conv.msg_count, 5);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ((int)conv.messages[2].role, (int)AI_ROLE_ASSISTANT);
    ASSERT_EQ(conv.messages[2].n_tool_calls, 1);
    ASSERT_STR_EQ(conv.messages[2].tool_calls[0].name, "web_search");
    ASSERT_EQ((int)conv.messages[3].role, (int)AI_ROLE_TOOL);
    ASSERT_STR_EQ(conv.messages[3].tool_name, "web_search");
    TEST_END();
}

int test_ui_demo_build_error_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("error", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3); /* system, two user turns, no reply */
    ASSERT_EQ(approval.count, 0);
    TEST_END();
}

int test_ui_demo_build_empty_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("empty", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 1); /* system prompt only -- always skipped */
    ASSERT_EQ(approval.count, 0);
    TEST_END();
}

int test_ui_demo_build_nokey_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("nokey", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 1); /* system prompt only -- always skipped */
    ASSERT_EQ(approval.count, 0);
    TEST_END();
}

int test_ui_demo_build_nosession_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("nosession", &conv, &approval, &approval2, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 1); /* system prompt only -- always skipped */
    ASSERT_EQ(approval.count, 0);
    TEST_END();
}

/* "batches": batch 1 (user, assistant, pending+blocked pair) then an
 * interleaving user turn then batch 2 (assistant, pending+blocked pair) --
 * see docs/superpowers/specs/2026-09-09-pending-command-batches.md. */
int test_ui_demo_build_batches_counts_and_statuses(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("batches", &conv, &approval, &approval2, NULL, 0), 0);
    /* system, user, assistant, user, assistant */
    ASSERT_EQ(conv.msg_count, 5);
    ASSERT_EQ((int)conv.messages[1].role, (int)AI_ROLE_USER);
    ASSERT_EQ((int)conv.messages[2].role, (int)AI_ROLE_ASSISTANT);
    ASSERT_EQ((int)conv.messages[3].role, (int)AI_ROLE_USER);
    ASSERT_EQ((int)conv.messages[4].role, (int)AI_ROLE_ASSISTANT);

    ASSERT_EQ(approval.count, 2);
    ASSERT_EQ(count_status(&approval, APPROVE_PENDING), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_BLOCKED), 1);

    ASSERT_EQ(approval2.count, 2);
    ASSERT_EQ(count_status(&approval2, APPROVE_PENDING), 1);
    ASSERT_EQ(count_status(&approval2, APPROVE_BLOCKED), 1);
    TEST_END();
}

int test_ui_demo_build_all_is_union(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ASSERT_EQ(ui_demo_build("all", &conv, &approval, &approval2, NULL, 0), 0);
    /* 1 shared system + (chat 2) + (approval 2) + (executing 2) + (tool 4)
     * + (error 2) + (batches 4) = 17 conversation messages. */
    ASSERT_EQ(conv.msg_count, 17);
    /* approval(4) + executing(2) + batches' first batch(2) = 8 entries. */
    ASSERT_EQ(approval.count, 8);
    /* batches' second batch. */
    ASSERT_EQ(approval2.count, 2);
    TEST_END();
}

int test_ui_demo_term_text_ends_with_prompt(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char term[4096];
    ASSERT_EQ(ui_demo_build("chat", &conv, &approval, &approval2, term, sizeof(term)), 0);
    size_t len = strlen(term);
    ASSERT_TRUE(len > 2);
    ASSERT_STR_EQ(term + len - 2, "$ ");
    TEST_END();
}

int test_ui_demo_thinking_text_non_empty(void)
{
    TEST_BEGIN();
    const char *t = ui_demo_thinking_text();
    ASSERT_NOT_NULL(t);
    ASSERT_TRUE(t[0] != '\0');
    TEST_END();
}
