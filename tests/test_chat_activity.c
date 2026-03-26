/* tests/test_chat_activity.c */
#include "test_framework.h"
#include "chat_activity.h"
#include <string.h>

int test_activity_init(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_IDLE);
    ASSERT_EQ((int)s.health, (int)HEALTH_GREEN);
    ASSERT_EQ(s.connection_lost, 0);
    TEST_END();
}

int test_activity_phase_transition(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 1.0f);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_PROCESSING);
    chat_activity_set_phase(&s, ACTIVITY_THINKING, 2.0f);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_THINKING);
    chat_activity_set_phase(&s, ACTIVITY_RESPONDING, 5.0f);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_RESPONDING);
    TEST_END();
}

int test_activity_skip_thinking(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 1.0f);
    chat_activity_set_phase(&s, ACTIVITY_RESPONDING, 3.0f);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_RESPONDING);
    TEST_END();
}

int test_activity_health_green(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 1.0f);
    chat_activity_tick(&s, 5.0f);
    ASSERT_EQ((int)s.health, (int)HEALTH_GREEN);
    TEST_END();
}

int test_activity_health_yellow(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 1.0f);
    chat_activity_tick(&s, 15.0f);
    ASSERT_EQ((int)s.health, (int)HEALTH_YELLOW);
    TEST_END();
}

int test_activity_health_red(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 1.0f);
    chat_activity_tick(&s, 35.0f);
    ASSERT_EQ((int)s.health, (int)HEALTH_RED);
    TEST_END();
}

int test_activity_health_boundary_10s(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 0.0f);
    chat_activity_tick(&s, 10.0f);
    ASSERT_EQ((int)s.health, (int)HEALTH_YELLOW);
    TEST_END();
}

int test_activity_health_boundary_30s(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 0.0f);
    chat_activity_tick(&s, 30.0f);
    ASSERT_EQ((int)s.health, (int)HEALTH_RED);
    TEST_END();
}

int test_activity_token_resets_health(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_token(&s, 0.0f);
    chat_activity_tick(&s, 20.0f);
    ASSERT_EQ((int)s.health, (int)HEALTH_YELLOW);
    chat_activity_token(&s, 20.0f);
    chat_activity_tick(&s, 22.0f);
    ASSERT_EQ((int)s.health, (int)HEALTH_GREEN);
    TEST_END();
}

int test_activity_connection_lost(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    chat_activity_connection_lost(&s);
    ASSERT_EQ(s.connection_lost, 1);
    ASSERT_EQ((int)s.health, (int)HEALTH_RED);
    TEST_END();
}

int test_activity_exec_progress(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_EXECUTING, 0.0f);
    chat_activity_set_exec(&s, 2, 5);
    ASSERT_EQ(s.exec_current, 2);
    ASSERT_EQ(s.exec_total, 5);
    TEST_END();
}

int test_activity_format_processing(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_PROCESSING, 0.0f);
    char buf[128];
    int n = chat_activity_format(&s, 1.0f, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "Processing") != NULL);
    TEST_END();
}

int test_activity_format_executing(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_EXECUTING, 0.0f);
    chat_activity_set_exec(&s, 2, 5);
    char buf[128];
    chat_activity_format(&s, 1.0f, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "2") != NULL);
    ASSERT_TRUE(strstr(buf, "5") != NULL);
    TEST_END();
}

int test_activity_format_stalled(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_RESPONDING, 0.0f);
    chat_activity_token(&s, 0.0f);
    chat_activity_tick(&s, 45.0f);
    char buf[128];
    chat_activity_format(&s, 1.0f, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Stalled") != NULL || strstr(buf, "stalled") != NULL);
    TEST_END();
}

int test_activity_reset(void) {
    TEST_BEGIN();
    ActivityState s;
    chat_activity_init(&s);
    chat_activity_set_phase(&s, ACTIVITY_RESPONDING, 0.0f);
    chat_activity_token(&s, 5.0f);
    chat_activity_reset(&s);
    ASSERT_EQ((int)s.phase, (int)ACTIVITY_IDLE);
    ASSERT_EQ((int)s.health, (int)HEALTH_GREEN);
    ASSERT_EQ(s.connection_lost, 0);
    TEST_END();
}
