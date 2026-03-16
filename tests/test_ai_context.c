#include "test_framework.h"
#include "ai_prompt.h"
#include <string.h>
#include <stdlib.h>

/* --- ai_word_count tests --- */

int test_ai_word_count_basic(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_word_count("hello world"), 2);
    ASSERT_EQ(ai_word_count("one two three"), 3);
    ASSERT_EQ(ai_word_count("single"), 1);
    TEST_END();
}

int test_ai_word_count_multispace(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_word_count("  hello   world  "), 2);
    ASSERT_EQ(ai_word_count("\t\ttab\t\tseparated\t"), 2);
    TEST_END();
}

int test_ai_word_count_empty(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_word_count(""), 0);
    ASSERT_EQ(ai_word_count("   "), 0);
    ASSERT_EQ(ai_word_count("\n\t "), 0);
    TEST_END();
}

int test_ai_word_count_null(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_word_count(NULL), 0);
    TEST_END();
}

int test_ai_word_count_newlines(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_word_count("one\ntwo\nthree"), 3);
    ASSERT_EQ(ai_word_count("line one\r\nline two"), 4);
    TEST_END();
}

/* --- ai_model_context_limit tests --- */

int test_ai_model_context_limit_known(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_model_context_limit("deepseek-chat"), 64000);
    ASSERT_EQ(ai_model_context_limit("deepseek-coder"), 128000);
    ASSERT_EQ(ai_model_context_limit("deepseek-reasoner"), 64000);
    ASSERT_EQ(ai_model_context_limit("gpt-4o"), 128000);
    ASSERT_EQ(ai_model_context_limit("gpt-4o-mini"), 128000);
    ASSERT_EQ(ai_model_context_limit("gpt-3.5-turbo"), 16000);
    ASSERT_EQ(ai_model_context_limit("claude-sonnet-4-6"), 200000);
    ASSERT_EQ(ai_model_context_limit("claude-opus-4-6"), 200000);
    ASSERT_EQ(ai_model_context_limit("kimi-k2"), 128000);
    ASSERT_EQ(ai_model_context_limit("moonshot-v1-8k"), 8000);
    ASSERT_EQ(ai_model_context_limit("moonshot-v1-128k"), 128000);
    TEST_END();
}

int test_ai_model_context_limit_unknown(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_model_context_limit("random-model-xyz"), 0);
    ASSERT_EQ(ai_model_context_limit(""), 0);
    TEST_END();
}

int test_ai_model_context_limit_null(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_model_context_limit(NULL), 0);
    TEST_END();
}

/* --- ai_context_estimate_tokens tests --- */

int test_ai_context_estimate_tokens(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    /* 20 chars / 4 = 5 tokens */
    ai_conv_add(&conv, AI_ROLE_USER, "12345678901234567890");
    ASSERT_EQ(ai_context_estimate_tokens(&conv), 5);
    /* Add another 40 chars -> total 60/4 = 15 */
    ai_conv_add(&conv, AI_ROLE_ASSISTANT,
                "1234567890123456789012345678901234567890");
    ASSERT_EQ(ai_context_estimate_tokens(&conv), 15);
    TEST_END();
}

int test_ai_context_estimate_tokens_empty(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ASSERT_EQ(ai_context_estimate_tokens(&conv), 0);
    TEST_END();
}

int test_ai_context_estimate_tokens_null(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_context_estimate_tokens(NULL), 0);
    TEST_END();
}

/* --- ai_conv_compact tests --- */

int test_ai_conv_compact_basic(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    /* msg[0] = system */
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "system prompt");
    /* 5 exchanges = 10 messages (indices 1-10) */
    for (int i = 0; i < 5; i++) {
        char ubuf[32], abuf[32];
        snprintf(ubuf, sizeof(ubuf), "user_%d", i);
        snprintf(abuf, sizeof(abuf), "asst_%d", i);
        ai_conv_add(&conv, AI_ROLE_USER, ubuf);
        ai_conv_add(&conv, AI_ROLE_ASSISTANT, abuf);
    }
    ASSERT_EQ(conv.msg_count, 11); /* 1 system + 10 */

    /* keep_recent=3 -> keep last 6 msgs + system = 7 total, remove 4 */
    int removed = ai_conv_compact(&conv, 3);
    ASSERT_EQ(removed, 4);
    ASSERT_EQ(conv.msg_count, 7);
    /* System prompt preserved */
    ASSERT_STR_EQ(conv.messages[0].content, "system prompt");
    /* Last 3 exchanges preserved (user_2..user_4, asst_2..asst_4) */
    ASSERT_STR_EQ(conv.messages[1].content, "user_2");
    ASSERT_STR_EQ(conv.messages[2].content, "asst_2");
    ASSERT_STR_EQ(conv.messages[5].content, "user_4");
    ASSERT_STR_EQ(conv.messages[6].content, "asst_4");
    TEST_END();
}

int test_ai_conv_compact_too_few(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&conv, AI_ROLE_USER, "hi");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "hello");
    /* 3 messages, keep_recent=3 -> need 1+6=7, only have 3, no compaction */
    int removed = ai_conv_compact(&conv, 3);
    ASSERT_EQ(removed, 0);
    ASSERT_EQ(conv.msg_count, 3);
    TEST_END();
}

int test_ai_conv_compact_null(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_conv_compact(NULL, 3), 0);
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ASSERT_EQ(ai_conv_compact(&conv, 0), 0);
    ASSERT_EQ(ai_conv_compact(&conv, -1), 0);
    TEST_END();
}

int test_ai_conv_compact_preserves_system(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "important system prompt");
    /* Add 8 exchanges = 16 msgs */
    for (int i = 0; i < 8; i++) {
        ai_conv_add(&conv, AI_ROLE_USER, "u");
        ai_conv_add(&conv, AI_ROLE_ASSISTANT, "a");
    }
    ASSERT_EQ(conv.msg_count, 17);
    int removed = ai_conv_compact(&conv, 2);
    ASSERT_TRUE(removed > 0);
    /* System prompt is always msg[0] */
    ASSERT_EQ((int)conv.messages[0].role, (int)AI_ROLE_SYSTEM);
    ASSERT_STR_EQ(conv.messages[0].content, "important system prompt");
    /* Should have 1 system + 4 kept = 5 */
    ASSERT_EQ(conv.msg_count, 5);
    TEST_END();
}

int test_ai_conv_compact_boundary(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    /* Exactly keep_recent*2+1 messages (boundary: 1 system + 2 pairs = 5) */
    ai_conv_add(&conv, AI_ROLE_USER, "u0");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "a0");
    ai_conv_add(&conv, AI_ROLE_USER, "u1");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "a1");
    ASSERT_EQ(conv.msg_count, 5); /* 1 + 4 = 5, keep_recent=2 needs 1+4=5 */
    /* At boundary: nothing to remove */
    int removed = ai_conv_compact(&conv, 2);
    ASSERT_EQ(removed, 0);
    ASSERT_EQ(conv.msg_count, 5);

    /* Add one more pair to go over boundary */
    ai_conv_add(&conv, AI_ROLE_USER, "u2");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "a2");
    ASSERT_EQ(conv.msg_count, 7);
    removed = ai_conv_compact(&conv, 2);
    ASSERT_EQ(removed, 2);
    ASSERT_EQ(conv.msg_count, 5);
    ASSERT_STR_EQ(conv.messages[0].content, "sys");
    ASSERT_STR_EQ(conv.messages[1].content, "u1");
    TEST_END();
}

int test_ai_conv_compact_system_only(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    /* Only system message — nothing to compact */
    int removed = ai_conv_compact(&conv, 1);
    ASSERT_EQ(removed, 0);
    ASSERT_EQ(conv.msg_count, 1);
    TEST_END();
}

/* --- ai_build_system_prompt with notes tests --- */

int test_ai_system_prompt_with_notes(void) {
    TEST_BEGIN();
    char buf[4096];
    ai_build_system_prompt(buf, sizeof(buf), "$ ls\nfile.txt",
                           "Ubuntu 22.04 production DB server",
                           "Be concise. Use sudo sparingly.");
    /* Base prompt present */
    ASSERT_TRUE(strstr(buf, "AI assistant") != NULL);
    /* System notes present */
    ASSERT_TRUE(strstr(buf, "Be concise. Use sudo sparingly.") != NULL);
    ASSERT_TRUE(strstr(buf, "system-wide instructions") != NULL);
    /* Session notes present */
    ASSERT_TRUE(strstr(buf, "Ubuntu 22.04 production DB server") != NULL);
    ASSERT_TRUE(strstr(buf, "About this server") != NULL);
    /* Terminal text present */
    ASSERT_TRUE(strstr(buf, "$ ls\nfile.txt") != NULL);
    TEST_END();
}

int test_ai_system_prompt_notes_null(void) {
    TEST_BEGIN();
    char buf[4096];
    /* NULL notes should work fine */
    ai_build_system_prompt(buf, sizeof(buf), "$ whoami", NULL, NULL);
    ASSERT_TRUE(strstr(buf, "AI assistant") != NULL);
    ASSERT_TRUE(strstr(buf, "$ whoami") != NULL);
    /* No notes sections */
    ASSERT_TRUE(strstr(buf, "system-wide") == NULL);
    ASSERT_TRUE(strstr(buf, "About this server") == NULL);

    /* Empty string notes should also be skipped */
    ai_build_system_prompt(buf, sizeof(buf), NULL, "", "");
    ASSERT_TRUE(strstr(buf, "AI assistant") != NULL);
    ASSERT_TRUE(strstr(buf, "system-wide") == NULL);
    TEST_END();
}
