#include "test_framework.h"
#include "ai_gate.h"

/* The gate is process-global; each test leaves it off and unoverridden. */
static void gate_reset(void)
{
    ai_gate_set_enabled(0);
    ai_gate_set_demo_override(0);
}

int test_ai_gate_default_off(void)
{
    TEST_BEGIN();
    gate_reset();   /* its own starting state, not test order */
    ASSERT_EQ(ai_gate_on(), 0);
    TEST_END();
}

int test_ai_gate_enabled(void)
{
    TEST_BEGIN();
    ai_gate_set_enabled(1);
    ASSERT_EQ(ai_gate_on(), 1);
    ai_gate_set_enabled(0);
    ASSERT_EQ(ai_gate_on(), 0);
    /* Any non-zero value counts as on and reads back as 1. */
    ai_gate_set_enabled(7);
    ASSERT_EQ(ai_gate_on(), 1);
    gate_reset();
    TEST_END();
}

int test_ai_gate_override_only(void)
{
    TEST_BEGIN();
    ai_gate_set_demo_override(1);
    ASSERT_EQ(ai_gate_on(), 1);
    ai_gate_set_demo_override(0);
    ASSERT_EQ(ai_gate_on(), 0);
    gate_reset();
    TEST_END();
}

int test_ai_gate_override_does_not_change_enabled(void)
{
    TEST_BEGIN();
    ai_gate_set_enabled(1);
    ai_gate_set_demo_override(1);
    /* Dropping the override leaves the stored choice standing... */
    ai_gate_set_demo_override(0);
    ASSERT_EQ(ai_gate_on(), 1);
    /* ...and setting enabled off while overridden leaves the override. */
    ai_gate_set_demo_override(1);
    ai_gate_set_enabled(0);
    ASSERT_EQ(ai_gate_on(), 1);
    ai_gate_set_demo_override(0);
    ASSERT_EQ(ai_gate_on(), 0);
    gate_reset();
    TEST_END();
}
