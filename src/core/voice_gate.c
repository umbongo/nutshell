/* src/core/voice_gate.c -- see voice_gate.h */
#include "voice_gate.h"
#include "ai_gate.h"

static int s_enabled;
static int s_override;

void voice_gate_set_enabled(int on)
{
    __atomic_store_n(&s_enabled, on ? 1 : 0, __ATOMIC_SEQ_CST);
}

void voice_gate_set_demo_override(int on)
{
    __atomic_store_n(&s_override, on ? 1 : 0, __ATOMIC_SEQ_CST);
}

int voice_gate_on(void)
{
    int own = __atomic_load_n(&s_enabled, __ATOMIC_SEQ_CST)
           || __atomic_load_n(&s_override, __ATOMIC_SEQ_CST);
    return own && ai_gate_on();
}
