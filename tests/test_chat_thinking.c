/* tests/test_chat_thinking.c */
#include "test_framework.h"
#include "chat_thinking.h"

int test_thinking_init(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    ASSERT_EQ((int)s.phase, (int)THINK_IDLE);
    ASSERT_EQ(s.collapsed, 1); /* default collapsed */
    TEST_END();
}

int test_thinking_first_token(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    ASSERT_EQ((int)s.phase, (int)THINK_STREAMING);
    ASSERT_TRUE(s.start_time >= 9.9f && s.start_time <= 10.1f);
    ASSERT_TRUE(s.elapsed_sec < 0.1f);
    TEST_END();
}

int test_thinking_elapsed_updates(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_tick(&s, 12.5f);
    ASSERT_TRUE(s.elapsed_sec >= 2.4f && s.elapsed_sec <= 2.6f);
    TEST_END();
}

int test_thinking_complete(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_complete(&s, 17.8f);
    ASSERT_EQ((int)s.phase, (int)THINK_COMPLETE);
    ASSERT_TRUE(s.elapsed_sec >= 7.7f && s.elapsed_sec <= 7.9f);
    TEST_END();
}

int test_thinking_toggle(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    ASSERT_EQ(s.collapsed, 1);
    int result = chat_thinking_toggle(&s);
    ASSERT_EQ(result, 0);  /* now expanded */
    ASSERT_EQ(s.collapsed, 0);
    result = chat_thinking_toggle(&s);
    ASSERT_EQ(result, 1);  /* collapsed again */
    TEST_END();
}

int test_thinking_toggle_during_streaming(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_toggle(&s); /* expand during streaming */
    ASSERT_EQ(s.collapsed, 0);
    ASSERT_EQ((int)s.phase, (int)THINK_STREAMING); /* still streaming */
    TEST_END();
}

int test_thinking_tick_idle_noop(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_tick(&s, 100.0f); /* should not crash */
    ASSERT_EQ((int)s.phase, (int)THINK_IDLE);
    ASSERT_TRUE(s.elapsed_sec < 0.01f);
    TEST_END();
}

int test_thinking_tick_complete_noop(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_complete(&s, 15.0f);
    float final_elapsed = s.elapsed_sec;
    chat_thinking_tick(&s, 100.0f); /* should not update elapsed after complete */
    ASSERT_TRUE(s.elapsed_sec >= final_elapsed - 0.01f && s.elapsed_sec <= final_elapsed + 0.01f);
    TEST_END();
}

int test_thinking_reset(void) {
    TEST_BEGIN();
    ThinkingState s;
    chat_thinking_init(&s);
    chat_thinking_token(&s, 10.0f);
    chat_thinking_reset(&s);
    ASSERT_EQ((int)s.phase, (int)THINK_IDLE);
    ASSERT_TRUE(s.elapsed_sec < 0.01f);
    TEST_END();
}
