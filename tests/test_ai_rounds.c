/* tests/test_ai_rounds.c -- the per-session unattended-round counter
 * (guard-rails spec 2026-11-04 section 1, "The counter"). */
#include "test_framework.h"
#include "ai_prompt.h"
#include "cmd_policy.h"
#include <string.h>

static CmdPolicy auto_policy(int level)
{
    CmdPolicy p = cmd_policy_default();
    cmd_policy_set_allowed(&p, level);
    cmd_policy_set_auto(&p, 1);
    return p;
}

int test_ai_rounds_user_message_resets(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    s.auto_rounds = 5;
    ai_rounds_user_message(&s);
    ASSERT_EQ(s.auto_rounds, 0);
    TEST_END();
}

int test_ai_rounds_continue_unattended_counts(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(s.auto_rounds, 0);
    ai_rounds_continue(&s, 1);
    ASSERT_EQ(s.auto_rounds, 1);
    ai_rounds_continue(&s, 1);
    ASSERT_EQ(s.auto_rounds, 2);
    TEST_END();
}

int test_ai_rounds_continue_attended_resets(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    s.auto_rounds = 2;
    ai_rounds_continue(&s, 0);
    ASSERT_EQ(s.auto_rounds, 0);
    TEST_END();
}

int test_ai_rounds_three_rounds_then_paused(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    CmdPolicy p = auto_policy(CMD_WRITE);
    ai_rounds_user_message(&s);
    ai_rounds_continue(&s, 1);
    ai_rounds_continue(&s, 1);
    CmdPolicy q = ai_rounds_batch_policy(&s, p);
    ASSERT_EQ(q.allowed, p.allowed);
    ASSERT_EQ(q.unattended, p.unattended);
    ASSERT_EQ(ai_rounds_paused(&s, p), 0);
    ai_rounds_continue(&s, 1);
    ASSERT_EQ(s.auto_rounds, AI_AUTO_ROUNDS_MAX);
    q = ai_rounds_batch_policy(&s, p);
    ASSERT_EQ(q.unattended, POLICY_NONE);
    ASSERT_EQ(q.allowed, p.allowed);
    ASSERT_EQ(ai_rounds_paused(&s, p), 1);
    TEST_END();
}

int test_ai_rounds_auto_off_never_paused(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    CmdPolicy p = cmd_policy_default();
    cmd_policy_set_allowed(&p, CMD_WRITE);
    s.auto_rounds = AI_AUTO_ROUNDS_MAX;
    CmdPolicy q = ai_rounds_batch_policy(&s, p);
    ASSERT_EQ(q.allowed, p.allowed);
    ASSERT_EQ(q.unattended, p.unattended);
    ASSERT_EQ(ai_rounds_paused(&s, p), 0);
    TEST_END();
}

int test_ai_rounds_session_policy_untouched(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    s.policy = auto_policy(CMD_UNKNOWN);
    s.auto_rounds = AI_AUTO_ROUNDS_MAX;
    CmdPolicy before = s.policy;
    CmdPolicy q = ai_rounds_batch_policy(&s, s.policy);
    ASSERT_EQ(q.unattended, POLICY_NONE);
    ASSERT_EQ(s.policy.allowed, before.allowed);
    ASSERT_EQ(s.policy.unattended, before.unattended);
    TEST_END();
}

int test_ai_rounds_null_safe(void) {
    TEST_BEGIN();
    CmdPolicy p = auto_policy(CMD_READ);
    ai_rounds_user_message(NULL);
    ai_rounds_continue(NULL, 1);
    ai_rounds_continue(NULL, 0);
    CmdPolicy q = ai_rounds_batch_policy(NULL, p);
    ASSERT_EQ(q.allowed, p.allowed);
    ASSERT_EQ(q.unattended, p.unattended);
    ASSERT_EQ(ai_rounds_paused(NULL, p), 0);
    TEST_END();
}

int test_tool_rounds_three_then_attended(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < AI_AUTO_TOOL_ROUNDS_MAX; i++) {
        ASSERT_EQ(ai_tool_rounds_attended(&s, 1, i), 0);
        ai_tool_rounds_ran_unattended(&s, 1, i);
        ASSERT_EQ(s.auto_tool_rounds, i + 1);
    }
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 3), 1);
    ASSERT_EQ(s.auto_tool_rounds, AI_AUTO_TOOL_ROUNDS_MAX);
    TEST_END();
}

int test_tool_rounds_counts_once_per_round(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 0), 0);
    ai_tool_rounds_ran_unattended(&s, 1, 0);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 0), 0);
    ai_tool_rounds_ran_unattended(&s, 1, 0);
    ASSERT_EQ(s.auto_tool_rounds, 1);
    TEST_END();
}

int test_tool_rounds_key_includes_stream(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(ai_tool_rounds_attended(&s, 7, 0), 0);
    ai_tool_rounds_ran_unattended(&s, 7, 0);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 8, 0), 0);   /* same iter, new stream */
    ai_tool_rounds_ran_unattended(&s, 8, 0);
    ASSERT_EQ(s.auto_tool_rounds, 2);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 8, 1), 0);
    ai_tool_rounds_ran_unattended(&s, 8, 1);
    ai_tool_rounds_ran_unattended(&s, 8, 1);
    ASSERT_EQ(s.auto_tool_rounds, 3);
    /* a call that never asked first is not counted */
    ai_tool_rounds_ran_unattended(&s, 9, 0);
    ASSERT_EQ(s.auto_tool_rounds, 3);
    TEST_END();
}

int test_tool_rounds_reset_mid_round_still_asks(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    s.auto_tool_rounds = AI_AUTO_TOOL_ROUNDS_MAX;
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 3), 1);
    ai_tool_rounds_reset(&s);                          /* Run click on the first call */
    ASSERT_EQ(s.auto_tool_rounds, 0);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 3), 1);   /* 2nd call: pre-round count */
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 4), 0);   /* next round starts fresh */
    TEST_END();
}

int test_tool_rounds_command_counter_separate(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    ai_rounds_continue(&s, 1);
    ai_rounds_continue(&s, 1);
    ai_rounds_continue(&s, 1);
    ASSERT_EQ(s.auto_rounds, AI_AUTO_ROUNDS_MAX);
    ASSERT_EQ(s.auto_tool_rounds, 0);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 0), 0);
    ai_tool_rounds_ran_unattended(&s, 1, 0);
    ASSERT_EQ(s.auto_tool_rounds, 1);
    ASSERT_EQ(s.auto_rounds, AI_AUTO_ROUNDS_MAX);
    ai_tool_rounds_reset(&s);
    ASSERT_EQ(s.auto_rounds, AI_AUTO_ROUNDS_MAX);      /* reset leaves commands alone */
    TEST_END();
}

int test_tool_rounds_pause_row_once(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(ai_tool_rounds_take_pause_row(&s), 1);
    ASSERT_EQ(ai_tool_rounds_take_pause_row(&s), 0);
    ai_tool_rounds_reset(&s);
    ASSERT_EQ(ai_tool_rounds_take_pause_row(&s), 1);
    ASSERT_EQ(ai_tool_rounds_take_pause_row(&s), 0);
    ASSERT_STR_EQ(AI_TOOL_PAUSE_ROW,
                  "[Auto-approve paused after 3 automatic tool rounds]");
    TEST_END();
}

int test_tool_rounds_null_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_tool_rounds_attended(NULL, 1, 0), 0);
    ai_tool_rounds_ran_unattended(NULL, 1, 0);
    ai_tool_rounds_reset(NULL);
    ASSERT_EQ(ai_tool_rounds_take_pause_row(NULL), 0);
    TEST_END();
}
