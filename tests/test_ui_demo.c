#include "test_framework.h"
#include "ui_demo.h"
#include <string.h>

/* ===========================================================================
 * ui_demo tests (Design-System Foundation, task 9) -- see
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

int test_ui_demo_states_lists_seven_ending_in_all(void)
{
    TEST_BEGIN();
    int count = 0;
    const char *const *states = ui_demo_states(&count);
    ASSERT_NOT_NULL(states);
    ASSERT_EQ(count, 7);
    ASSERT_STR_EQ(states[count - 1], "all");
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
    ApprovalQueue approval;
    char term[64];
    ASSERT_EQ(ui_demo_build("bogus", &conv, &approval, term, sizeof(term)), -1);
    TEST_END();
}

int test_ui_demo_build_null_outputs_safe(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ui_demo_build("chat", NULL, NULL, NULL, 0), 0);
    TEST_END();
}

int test_ui_demo_build_chat_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval;
    ASSERT_EQ(ui_demo_build("chat", &conv, &approval, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3); /* system, user, assistant */
    ASSERT_EQ((int)conv.messages[0].role, (int)AI_ROLE_SYSTEM);
    ASSERT_EQ((int)conv.messages[1].role, (int)AI_ROLE_USER);
    ASSERT_EQ((int)conv.messages[2].role, (int)AI_ROLE_ASSISTANT);
    ASSERT_EQ(approval.count, 0);
    TEST_END();
}

int test_ui_demo_build_approval_counts_and_statuses(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval;
    ASSERT_EQ(ui_demo_build("approval", &conv, &approval, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3);
    ASSERT_EQ(approval.count, 4);
    ASSERT_EQ(count_status(&approval, APPROVE_PENDING), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_APPROVED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_DENIED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_BLOCKED), 1);
    TEST_END();
}

int test_ui_demo_build_executing_counts_and_statuses(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval;
    ASSERT_EQ(ui_demo_build("executing", &conv, &approval, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3);
    ASSERT_EQ(approval.count, 2);
    ASSERT_EQ(count_status(&approval, APPROVE_COMPLETED), 1);
    ASSERT_EQ(count_status(&approval, APPROVE_EXECUTING), 1);
    TEST_END();
}

int test_ui_demo_build_tool_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval;
    ASSERT_EQ(ui_demo_build("tool", &conv, &approval, NULL, 0), 0);
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
    ApprovalQueue approval;
    ASSERT_EQ(ui_demo_build("error", &conv, &approval, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 3); /* system, two user turns, no reply */
    ASSERT_EQ(approval.count, 0);
    TEST_END();
}

int test_ui_demo_build_empty_counts(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval;
    ASSERT_EQ(ui_demo_build("empty", &conv, &approval, NULL, 0), 0);
    ASSERT_EQ(conv.msg_count, 1); /* system prompt only -- always skipped */
    ASSERT_EQ(approval.count, 0);
    TEST_END();
}

int test_ui_demo_build_all_is_union(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval;
    ASSERT_EQ(ui_demo_build("all", &conv, &approval, NULL, 0), 0);
    /* 1 shared system + (chat 2) + (approval 2) + (executing 2) + (tool 4)
     * + (error 2) = 13 conversation messages. */
    ASSERT_EQ(conv.msg_count, 13);
    /* approval(4) + executing(2) = 6 approval entries. */
    ASSERT_EQ(approval.count, 6);
    TEST_END();
}

int test_ui_demo_term_text_ends_with_prompt(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval;
    char term[4096];
    ASSERT_EQ(ui_demo_build("chat", &conv, &approval, term, sizeof(term)), 0);
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
