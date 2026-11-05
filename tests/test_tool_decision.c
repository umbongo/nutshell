/* tests/test_tool_decision.c -- the tool gate's decision rule: which tool
 * calls run at once and which ask first. Spec
 * docs/superpowers/specs/2026-11-05-trust-boundary-design.md decisions 13-14. */
#include "test_framework.h"
#include "tool_decision.h"
#include "ai_tools.h"
#include "cmd_policy.h"
#include <stdio.h>

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
