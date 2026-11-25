/* tests/test_ai_session_discard.c -- ai_session_discard(): what turning the
 * AI assistant off does to one session's AI state. */
#include "test_framework.h"
#include "ai_prompt.h"
#include "cmd_batch.h"
#include "cmd_run_log.h"
#include <stdlib.h>
#include <string.h>

static void fill_state(AiSessionState *s)
{
    memset(s, 0, sizeof(*s));
    cmd_batch_set_init(&s->batches);
    cmd_run_log_init(&s->run_log);
    ai_conv_init(&s->conv, "model-x");
    ai_conv_add(&s->conv, AI_ROLE_USER, "hello");
    ai_conv_add(&s->conv, AI_ROLE_ASSISTANT, "hi there");
    s->valid = 1;
    cmd_batch_add(&s->batches, NULL, NULL);
    cmd_batch_add(&s->batches, NULL, NULL);
    snprintf(s->notes.text[0], sizeof(s->notes.text[0]), "%s", "a note");
    s->notes.count = 1;
    s->busy = 1;
    s->stream_content = strdup("partial");
    s->stream_content_len = 7;
    s->stream_thinking = strdup("hmm");
    s->stream_thinking_len = 3;
    s->stream_phase = 2;
    s->platform = 2;
    s->platform_contradicted = 1;
    s->policy = cmd_policy_default();
    s->policy_seeded = 1;
    s->activity_phase = 3;
    s->auto_rounds = 2;
    s->auto_tool_rounds = 4;
    s->tool_round_stream = 9;
    s->tool_round_iter = 5;
    s->tool_round_base = 1;
    s->tool_round_counted = 1;
    s->tool_pause_shown = 1;
}

int test_ai_session_discard_clears_everything(void)
{
    TEST_BEGIN();
    AiSessionState s;
    fill_state(&s);
    s.last_turn_error = 1;
    ASSERT_EQ(s.batches.count, 2);
    ai_session_discard(&s);
    ASSERT_EQ(s.valid, 0);
    ASSERT_EQ(s.conv.msg_count, 0);
    ASSERT_EQ(s.batches.count, 0);
    ASSERT_EQ(s.run_log.count, 0);
    ASSERT_EQ(s.notes.count, 0);
    ASSERT_EQ((int)s.busy, 0);
    ASSERT_NULL(s.stream_content);
    ASSERT_NULL(s.stream_thinking);
    ASSERT_EQ((int)s.stream_content_len, 0);
    ASSERT_EQ((int)s.stream_thinking_len, 0);
    ASSERT_EQ(s.stream_phase, 0);
    ASSERT_EQ(s.activity_phase, 0);
    ASSERT_EQ(s.auto_rounds, 0);
    ASSERT_EQ(s.last_turn_error, 0);
    ASSERT_EQ(s.auto_tool_rounds, 0);
    ASSERT_EQ((int)s.tool_round_stream, 0);
    ASSERT_EQ(s.tool_round_iter, 0);
    ASSERT_EQ(s.tool_round_base, 0);
    ASSERT_EQ(s.tool_round_counted, 0);
    ASSERT_EQ(s.tool_pause_shown, 0);
    ASSERT_EQ(s.policy_seeded, 0);
    ASSERT_EQ(s.notes.text[0][0], '\0');
    ai_session_discard(&s);
    TEST_END();
}

int test_ai_session_discard_keeps_platform(void)
{
    TEST_BEGIN();
    AiSessionState s;
    fill_state(&s);
    ai_session_discard(&s);
    ASSERT_EQ(s.platform, 2);
    ASSERT_EQ(s.platform_contradicted, 1);
    TEST_END();
}

int test_ai_session_discard_twice_is_safe(void)
{
    TEST_BEGIN();
    AiSessionState s;
    fill_state(&s);
    ai_session_discard(&s);
    ai_session_discard(&s);
    ASSERT_EQ(s.valid, 0);
    ASSERT_EQ(s.batches.count, 0);
    ai_session_discard(NULL);   /* NULL-safe */
    /* usable again after a discard */
    ASSERT_TRUE(cmd_batch_add(&s.batches, NULL, NULL) != NULL);
    ASSERT_EQ(s.batches.count, 1);
    cmd_batch_set_free(&s.batches);
    TEST_END();
}
