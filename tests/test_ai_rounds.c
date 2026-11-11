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
    for (int i = 0; i < AI_TOOL_ROUNDS_DEFAULT; i++) {
        ASSERT_EQ(ai_tool_rounds_attended(&s, 1, i, AI_TOOL_ROUNDS_DEFAULT), 0);
        ai_tool_rounds_ran_unattended(&s, 1, i);
        ASSERT_EQ(s.auto_tool_rounds, i + 1);
    }
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 3, AI_TOOL_ROUNDS_DEFAULT), 1);
    ASSERT_EQ(s.auto_tool_rounds, AI_TOOL_ROUNDS_DEFAULT);
    TEST_END();
}

int test_tool_rounds_counts_once_per_round(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 0, AI_TOOL_ROUNDS_DEFAULT), 0);
    ai_tool_rounds_ran_unattended(&s, 1, 0);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 0, AI_TOOL_ROUNDS_DEFAULT), 0);
    ai_tool_rounds_ran_unattended(&s, 1, 0);
    ASSERT_EQ(s.auto_tool_rounds, 1);
    TEST_END();
}

int test_tool_rounds_key_includes_stream(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    ASSERT_EQ(ai_tool_rounds_attended(&s, 7, 0, AI_TOOL_ROUNDS_DEFAULT), 0);
    ai_tool_rounds_ran_unattended(&s, 7, 0);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 8, 0, AI_TOOL_ROUNDS_DEFAULT), 0);   /* same iter, new stream */
    ai_tool_rounds_ran_unattended(&s, 8, 0);
    ASSERT_EQ(s.auto_tool_rounds, 2);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 8, 1, AI_TOOL_ROUNDS_DEFAULT), 0);
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
    s.auto_tool_rounds = AI_TOOL_ROUNDS_DEFAULT;
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 3, AI_TOOL_ROUNDS_DEFAULT), 1);
    ai_tool_rounds_reset(&s);                          /* Run click on the first call */
    ASSERT_EQ(s.auto_tool_rounds, 0);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 3, AI_TOOL_ROUNDS_DEFAULT), 1);   /* 2nd call: pre-round count */
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 4, AI_TOOL_ROUNDS_DEFAULT), 0);   /* next round starts fresh */
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
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 0, AI_TOOL_ROUNDS_DEFAULT), 0);
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
    TEST_END();
}

int test_tool_rounds_null_safe(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_tool_rounds_attended(NULL, 1, 0, AI_TOOL_ROUNDS_DEFAULT), 0);
    ai_tool_rounds_ran_unattended(NULL, 1, 0);
    ai_tool_rounds_reset(NULL);
    ASSERT_EQ(ai_tool_rounds_take_pause_row(NULL), 0);
    TEST_END();
}

/* The cap is a parameter: with 2 the third round is attended; with 5 the
 * sixth is and the fifth is not. */
int test_tool_rounds_cap_follows_setting(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 2; i++) {
        ASSERT_EQ(ai_tool_rounds_attended(&s, 1, i, 2), 0);
        ai_tool_rounds_ran_unattended(&s, 1, i);
    }
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 2, 2), 1);

    AiSessionState t;
    memset(&t, 0, sizeof(t));
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(ai_tool_rounds_attended(&t, 1, i, 5), 0);
        ai_tool_rounds_ran_unattended(&t, 1, i);
    }
    ASSERT_EQ(t.auto_tool_rounds, 5);
    ASSERT_EQ(ai_tool_rounds_attended(&t, 1, 5, 5), 1);
    TEST_END();
}

/* A cap outside 1..20 is clamped in core: 0 and negatives act as 1, 99 as 20. */
int test_tool_rounds_cap_clamped(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_tool_rounds_clamp(0), 1);
    ASSERT_EQ(ai_tool_rounds_clamp(-5), 1);
    ASSERT_EQ(ai_tool_rounds_clamp(1), 1);
    ASSERT_EQ(ai_tool_rounds_clamp(3), 3);
    ASSERT_EQ(ai_tool_rounds_clamp(20), 20);
    ASSERT_EQ(ai_tool_rounds_clamp(21), 20);
    ASSERT_EQ(ai_tool_rounds_clamp(99999), 20);

    AiSessionState s;
    memset(&s, 0, sizeof(s));
    /* cap 0 acts as 1: the first round runs, the second asks */
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 0, 0), 0);
    ai_tool_rounds_ran_unattended(&s, 1, 0);
    ASSERT_EQ(ai_tool_rounds_attended(&s, 1, 1, 0), 1);
    /* cap 99 acts as 20: 19 counted rounds are still unattended */
    AiSessionState t;
    memset(&t, 0, sizeof(t));
    t.auto_tool_rounds = 19;
    ASSERT_EQ(ai_tool_rounds_attended(&t, 1, 0, 99), 0);
    AiSessionState u;
    memset(&u, 0, sizeof(u));
    u.auto_tool_rounds = 20;
    ASSERT_EQ(ai_tool_rounds_attended(&u, 1, 0, 99), 1);
    TEST_END();
}

/* The pause row carries the cap in force: 3 (default), 2, 5; a bad cap is
 * clamped like the gate; "rounds" stays plural for 1; tiny buffers are safe. */
int test_tool_rounds_pause_row_text(void) {
    TEST_BEGIN();
    char buf[96];
    ai_tool_pause_row(buf, sizeof(buf), 3);
    ASSERT_STR_EQ(buf, "[Auto-approve paused after 3 automatic tool rounds]");
    ai_tool_pause_row(buf, sizeof(buf), 2);
    ASSERT_STR_EQ(buf, "[Auto-approve paused after 2 automatic tool rounds]");
    ai_tool_pause_row(buf, sizeof(buf), 5);
    ASSERT_STR_EQ(buf, "[Auto-approve paused after 5 automatic tool rounds]");
    ai_tool_pause_row(buf, sizeof(buf), 20);
    ASSERT_STR_EQ(buf, "[Auto-approve paused after 20 automatic tool rounds]");
    ai_tool_pause_row(buf, sizeof(buf), 1);
    ASSERT_STR_EQ(buf, "[Auto-approve paused after 1 automatic tool rounds]");
    ai_tool_pause_row(buf, sizeof(buf), 0);
    ASSERT_STR_EQ(buf, "[Auto-approve paused after 1 automatic tool rounds]");
    ai_tool_pause_row(buf, sizeof(buf), 500);
    ASSERT_STR_EQ(buf, "[Auto-approve paused after 20 automatic tool rounds]");
    char small[8];
    ai_tool_pause_row(small, sizeof(small), 3);
    ASSERT_EQ((int)strlen(small), 7);
    ai_tool_pause_row(NULL, 0, 3);
    ai_tool_pause_row(buf, 0, 3);
    TEST_END();
}

/* A run_command batch relaunches through the same counter as [EXEC]
 * (run_command spec decision 14): three unattended rounds, then the fourth
 * batch is built attended. Pins existing behaviour; no code of its own. */
int test_ai_rounds_runcmd_fourth_attended(void) {
    TEST_BEGIN();
    AiSessionState s;
    memset(&s, 0, sizeof(s));
    CmdPolicy p = auto_policy(CMD_WRITE);
    ai_rounds_user_message(&s);
    for (int i = 0; i < AI_AUTO_ROUNDS_MAX; i++) {
        CmdPolicy q = ai_rounds_batch_policy(&s, p);
        ASSERT_EQ(q.unattended, p.unattended);     /* batches 1 to 3 keep the policy */
        ai_rounds_continue(&s, 1);
    }
    ASSERT_EQ(s.auto_rounds, 3);
    CmdPolicy fourth = ai_rounds_batch_policy(&s, p);
    ASSERT_EQ(fourth.unattended, POLICY_NONE);     /* the fourth is attended */
    ASSERT_EQ(fourth.allowed, p.allowed);
    ASSERT_EQ(ai_rounds_paused(&s, p), 1);
    /* a Stop or a typed message (reset_rounds) starts the count over */
    ai_rounds_user_message(&s);
    ASSERT_EQ(ai_rounds_batch_policy(&s, p).unattended, p.unattended);
    TEST_END();
}
