/* tests/test_tool_decision.c -- the tool gate's decision rule: which tool
 * calls run at once and which ask first. Spec
 * docs/superpowers/specs/2026-11-05-trust-boundary-design.md decisions 13-14. */
#include "test_framework.h"
#include "tool_decision.h"
#include "ai_tools.h"
#include "cmd_policy.h"
#include <stdio.h>
#include <string.h>

/* The 60 s default, in ms: what a card waits when Settings is untouched. */
#define TD_TO 60000ULL

typedef struct {
    const char *name;
    CmdPolicy policy;
} GatePolicyCase;

static CmdPolicy auto_at(int level)
{
    CmdPolicy p = cmd_policy_default();
    cmd_policy_set_allowed(&p, level);
    cmd_policy_set_auto(&p, 1);
    return p;
}

int test_tool_gate_matrix(void)
{
    TEST_BEGIN();

    CmdPolicy ceiling_crit = cmd_policy_default();
    cmd_policy_set_allowed(&ceiling_crit, CMD_CRITICAL);   /* Auto stays off */
    CmdPolicy none_both = { CMD_READ, POLICY_NONE };
    none_both.unattended = POLICY_NONE;

    GatePolicyCase cases[] = {
        { "default{Read,off}",        cmd_policy_default() },
        { "auto@Read",                auto_at(CMD_READ) },
        { "auto@Unknown",             auto_at(CMD_UNKNOWN) },
        { "auto@Write",               auto_at(CMD_WRITE) },
        { "ceiling Critical, auto off", ceiling_crit },
        { "POLICY_NONE/none",         none_both },
    };
    const ToolSafety levels[] = { TOOL_SAFE, TOOL_UNKNOWN, TOOL_WRITE,
                                  TOOL_CRITICAL, (ToolSafety)9 };
    const char *level_names[] = { "SAFE", "UNKNOWN", "WRITE", "CRITICAL", "9" };

    for (size_t li = 0; li < sizeof(levels) / sizeof(levels[0]); li++) {
        for (size_t pi = 0; pi < sizeof(cases) / sizeof(cases[0]); pi++) {
            for (int attended = 0; attended <= 1; attended++) {
                ToolGate got = tool_gate_decide(levels[li], cases[pi].policy, attended);
                ToolGate want = TOOL_GATE_ASK;
                if (levels[li] == TOOL_SAFE) {
                    want = TOOL_GATE_RUN;
                } else if (levels[li] == TOOL_UNKNOWN) {
                    int auto_ok = (pi == 2 || pi == 3);   /* auto @Unknown, @Write */
                    want = (auto_ok && !attended) ? TOOL_GATE_RUN : TOOL_GATE_ASK;
                }
                if (got != want) {
                    printf("  safety %s, policy %s, attended %d: got %d, want %d\n",
                           level_names[li], cases[pi].name, attended,
                           (int)got, (int)want);
                }
                ASSERT_EQ((int)got, (int)want);
            }
        }
    }
    TEST_END();
}

int test_tool_gate_levels_follow_cmd_safety_order(void)
{
    TEST_BEGIN();
    ASSERT_EQ((int)TOOL_SAFE, (int)CMD_READ);
    ASSERT_EQ((int)TOOL_UNKNOWN, (int)CMD_UNKNOWN);
    ASSERT_EQ((int)TOOL_WRITE, (int)CMD_WRITE);
    ASSERT_EQ((int)TOOL_CRITICAL, (int)CMD_CRITICAL);
    TEST_END();
}

int test_tool_registrations(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    DebugLogToolContext ctx;
    ai_tools_init(&reg);
    ai_debug_tool_ctx_init(&ctx);
    ASSERT_EQ(ai_debug_tool_register(&reg, &ctx), 0);
    const AiToolDef *t = ai_tools_find(&reg, "read_debug_log");
    ASSERT_NOT_NULL(t);
    if (t) ASSERT_EQ((int)t->safety, (int)TOOL_SAFE);
    TEST_END();
}

/* ---- the decision slot (decisions 15, 16, 20) ---- */

int test_tool_decision_publish_gen_nonzero_and_fresh(void)
{
    TEST_BEGIN();
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g1 = tool_decision_publish(&s);
    ASSERT_TRUE(g1 != 0);
    ASSERT_EQ((int)tool_decision_poll(&s, g1, 0, 0, TD_TO), (int)TOOL_DECISION_PENDING);
    unsigned g2 = tool_decision_publish(&s);
    ASSERT_TRUE(g2 != 0);
    ASSERT_TRUE(g1 != g2);
    ASSERT_EQ((int)tool_decision_poll(&s, g2, 0, 0, TD_TO), (int)TOOL_DECISION_PENDING);
    TEST_END();
}

int test_tool_decision_publish_skips_zero_on_wrap(void)
{
    TEST_BEGIN();
    ToolDecisionSlot s;
    tool_decision_init(&s);
    s.word = (0x1FFFFFFFu << 3) | (unsigned)TOOL_DECISION_APPROVED;   /* the last generation */
    unsigned g = tool_decision_publish(&s);
    ASSERT_EQ((int)g, 1);                                            /* 0 is skipped */
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 0, TD_TO), (int)TOOL_DECISION_PENDING);
    ASSERT_EQ((int)tool_decision_poll(&s, 0, 0, 0, TD_TO), (int)TOOL_DECISION_NONE);
    ASSERT_EQ((int)tool_decision_publish(NULL), 0);
    TEST_END();
}

int test_tool_decision_each_outcome(void)
{
    TEST_BEGIN();
    static const ToolDecision outcomes[] = {
        TOOL_DECISION_RUN, TOOL_DECISION_APPROVED, TOOL_DECISION_NOT_APPROVED,
        TOOL_DECISION_TOO_MANY, TOOL_DECISION_CANCELLED
    };
    for (size_t i = 0; i < sizeof(outcomes) / sizeof(outcomes[0]); i++) {
        ToolDecisionSlot s;
        tool_decision_init(&s);
        unsigned g = tool_decision_publish(&s);
        ASSERT_EQ((int)tool_decision_answer(&s, g, outcomes[i]), (int)outcomes[i]);
        ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 0, TD_TO), (int)outcomes[i]);
    }
    TEST_END();
}

int test_tool_decision_second_answer_loses(void)
{
    TEST_BEGIN();
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_answer(&s, g, TOOL_DECISION_APPROVED), (int)TOOL_DECISION_APPROVED);
    ASSERT_EQ((int)tool_decision_answer(&s, g, TOOL_DECISION_NOT_APPROVED), (int)TOOL_DECISION_APPROVED);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 0, TD_TO), (int)TOOL_DECISION_APPROVED);
    TEST_END();
}

int test_tool_decision_stale_gen(void)
{
    TEST_BEGIN();
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g1 = tool_decision_publish(&s);
    unsigned g2 = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_answer(&s, g1, TOOL_DECISION_APPROVED), (int)TOOL_DECISION_NONE);
    ASSERT_EQ((int)tool_decision_poll(&s, g2, 0, 0, TD_TO), (int)TOOL_DECISION_PENDING);
    ASSERT_EQ((int)tool_decision_poll(&s, g1, 1, 600000, TD_TO), (int)TOOL_DECISION_NONE);   /* a stale poll settles nothing */
    ASSERT_EQ((int)tool_decision_poll(&s, g2, 0, 0, TD_TO), (int)TOOL_DECISION_PENDING);
    TEST_END();
}

int test_tool_decision_invalid_answer(void)
{
    TEST_BEGIN();
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_answer(&s, g, TOOL_DECISION_PENDING), (int)TOOL_DECISION_NONE);
    ASSERT_EQ((int)tool_decision_answer(&s, g, TOOL_DECISION_EXPIRED), (int)TOOL_DECISION_NONE);
    ASSERT_EQ((int)tool_decision_answer(&s, g, TOOL_DECISION_NONE), (int)TOOL_DECISION_NONE);
    ASSERT_EQ((int)tool_decision_answer(&s, g, (ToolDecision)99), (int)TOOL_DECISION_NONE);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 0, TD_TO), (int)TOOL_DECISION_PENDING);
    ASSERT_EQ((int)tool_decision_answer(NULL, g, TOOL_DECISION_RUN), (int)TOOL_DECISION_NONE);
    TEST_END();
}

int test_tool_decision_aborted_poll(void)
{
    TEST_BEGIN();
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 1, 0, TD_TO), (int)TOOL_DECISION_CANCELLED);
    ASSERT_EQ((int)tool_decision_answer(&s, g, TOOL_DECISION_APPROVED), (int)TOOL_DECISION_CANCELLED);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 0, TD_TO), (int)TOOL_DECISION_CANCELLED);
    TEST_END();
}

int test_tool_decision_abort_before_publish(void)
{
    TEST_BEGIN();
    /* The stream was aborted before the thread got here: the abort flag is
     * already true when it publishes, and its first poll sees it. */
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 1, 0, TD_TO), (int)TOOL_DECISION_CANCELLED);
    TEST_END();
}

int test_tool_decision_expiry(void)
{
    TEST_BEGIN();
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 59999, 60000), (int)TOOL_DECISION_PENDING);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 60000, 60000), (int)TOOL_DECISION_EXPIRED);
    ASSERT_EQ((int)tool_decision_answer(&s, g, TOOL_DECISION_APPROVED), (int)TOOL_DECISION_EXPIRED);
    ASSERT_EQ((int)AI_TOOL_DECIDE_POLL_MS, 250);
    TEST_END();
}

int test_tool_decision_expiry_follows_timeout(void)
{
    TEST_BEGIN();
    /* The same wait is pending under a longer timeout and expired under a
     * shorter one: the slot's expiry is the value passed, not a constant. */
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 20000, 120000), (int)TOOL_DECISION_PENDING);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 119999, 120000), (int)TOOL_DECISION_PENDING);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 120000, 120000), (int)TOOL_DECISION_EXPIRED);

    unsigned g2 = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_poll(&s, g2, 0, 20000, 15000), (int)TOOL_DECISION_EXPIRED);

    /* A timeout of 0 or past the ceiling is clamped, never "wait forever"
     * or "expire at once". */
    unsigned g3 = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_poll(&s, g3, 0, 0, 0), (int)TOOL_DECISION_PENDING);
    ASSERT_EQ((int)tool_decision_poll(&s, g3, 0, 14999, 0), (int)TOOL_DECISION_PENDING);
    ASSERT_EQ((int)tool_decision_poll(&s, g3, 0, 15000, 0), (int)TOOL_DECISION_EXPIRED);
    unsigned g4 = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_poll(&s, g4, 0, 3599999, 99999999ULL), (int)TOOL_DECISION_PENDING);
    ASSERT_EQ((int)tool_decision_poll(&s, g4, 0, 3600000, 99999999ULL), (int)TOOL_DECISION_EXPIRED);
    TEST_END();
}

int test_tool_decision_timeout_ms(void)
{
    TEST_BEGIN();
    ASSERT_EQ(AI_TOOL_TIMEOUT_SEC_MIN, 15);
    ASSERT_EQ(AI_TOOL_TIMEOUT_SEC_MAX, 3600);
    ASSERT_EQ(AI_TOOL_TIMEOUT_SEC_DEFAULT, 60);
    ASSERT_EQ((int)tool_decision_timeout_ms(AI_TOOL_TIMEOUT_SEC_DEFAULT), 60000);
    ASSERT_EQ((int)tool_decision_timeout_ms(15), 15000);
    ASSERT_EQ((int)tool_decision_timeout_ms(3600), 3600000);
    ASSERT_EQ((int)tool_decision_timeout_ms(14), 15000);
    ASSERT_EQ((int)tool_decision_timeout_ms(0), 15000);
    ASSERT_EQ((int)tool_decision_timeout_ms(-7), 15000);
    ASSERT_EQ((int)tool_decision_timeout_ms(3601), 3600000);
    ASSERT_EQ((int)tool_decision_timeout_ms(2000000000), 3600000);
    TEST_END();
}

int test_tool_decision_late_answer_reports_winner(void)
{
    TEST_BEGIN();
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g1 = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_poll(&s, g1, 0, 600000, TD_TO), (int)TOOL_DECISION_EXPIRED);
    ToolDecision r = tool_decision_answer(&s, g1, TOOL_DECISION_APPROVED);
    ASSERT_EQ((int)r, (int)TOOL_DECISION_EXPIRED);
    ASSERT_TRUE(strcmp(tool_decision_row_label(r), "approved") != 0);

    unsigned g2 = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_poll(&s, g2, 1, 0, TD_TO), (int)TOOL_DECISION_CANCELLED);
    r = tool_decision_answer(&s, g2, TOOL_DECISION_APPROVED);
    ASSERT_EQ((int)r, (int)TOOL_DECISION_CANCELLED);
    ASSERT_TRUE(strcmp(tool_decision_row_label(r), "approved") != 0);
    TEST_END();
}

int test_tool_decision_answer_before_remove_order(void)
{
    TEST_BEGIN();
    /* The sweep answers CANCELLED first; the removal that follows answers
     * NOT_APPROVED, which must not override it. */
    ToolDecisionSlot s;
    tool_decision_init(&s);
    unsigned g = tool_decision_publish(&s);
    ASSERT_EQ((int)tool_decision_answer(&s, g, TOOL_DECISION_CANCELLED), (int)TOOL_DECISION_CANCELLED);
    ASSERT_EQ((int)tool_decision_answer(&s, g, TOOL_DECISION_NOT_APPROVED), (int)TOOL_DECISION_CANCELLED);
    ASSERT_EQ((int)tool_decision_poll(&s, g, 0, 0, TD_TO), (int)TOOL_DECISION_CANCELLED);
    TEST_END();
}

int test_tool_decision_notes(void)
{
    TEST_BEGIN();
    ASSERT_NULL(tool_decision_note(TOOL_DECISION_RUN));
    ASSERT_NULL(tool_decision_note(TOOL_DECISION_APPROVED));
    ASSERT_STR_EQ(tool_decision_note(TOOL_DECISION_TOO_MANY),
                  "The user did not approve this tool call (too many pending cards).");
    ASSERT_STR_EQ(tool_decision_note(TOOL_DECISION_NOT_APPROVED), "The user did not approve this tool call.");
    ASSERT_STR_EQ(tool_decision_note(TOOL_DECISION_EXPIRED), "The user did not approve this tool call.");
    ASSERT_STR_EQ(tool_decision_note(TOOL_DECISION_CANCELLED), "The user did not approve this tool call.");
    ASSERT_STR_EQ(tool_decision_note(TOOL_DECISION_NONE), "The user did not approve this tool call.");
    ASSERT_STR_EQ(tool_decision_note(TOOL_DECISION_PENDING), "The user did not approve this tool call.");
    TEST_END();
}

int test_tool_decision_row_labels(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(tool_decision_row_label(TOOL_DECISION_PENDING), "waiting");
    ASSERT_STR_EQ(tool_decision_row_label(TOOL_DECISION_APPROVED), "approved");
    ASSERT_STR_EQ(tool_decision_row_label(TOOL_DECISION_NOT_APPROVED), "not approved");
    ASSERT_STR_EQ(tool_decision_row_label(TOOL_DECISION_TOO_MANY), "not approved");
    ASSERT_STR_EQ(tool_decision_row_label(TOOL_DECISION_EXPIRED), "expired, not run");
    ASSERT_STR_EQ(tool_decision_row_label(TOOL_DECISION_CANCELLED), "cancelled, not run");
    ASSERT_STR_EQ(tool_decision_row_label(TOOL_DECISION_NONE), "not run");
    ASSERT_STR_EQ(tool_decision_row_label(TOOL_DECISION_RUN), "not run");
    TEST_END();
}
