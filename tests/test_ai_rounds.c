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
