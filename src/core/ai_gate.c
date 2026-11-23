/* src/core/ai_gate.c -- see ai_gate.h */
#include "ai_gate.h"

static int s_enabled;
static int s_override;

void ai_gate_set_enabled(int on)
{
    __atomic_store_n(&s_enabled, on ? 1 : 0, __ATOMIC_SEQ_CST);
}

void ai_gate_set_demo_override(int on)
{
    __atomic_store_n(&s_override, on ? 1 : 0, __ATOMIC_SEQ_CST);
}

int ai_gate_on(void)
{
    return __atomic_load_n(&s_enabled, __ATOMIC_SEQ_CST)
        || __atomic_load_n(&s_override, __ATOMIC_SEQ_CST);
}
