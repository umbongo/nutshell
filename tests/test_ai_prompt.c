#include "test_framework.h"
#include "ai_prompt.h"
#include <string.h>
#include <stdlib.h>

int test_ai_conv_init(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "deepseek-chat");
    ASSERT_STR_EQ(conv.model, "deepseek-chat");
    ASSERT_EQ(conv.msg_count, 0);
    TEST_END();
}

int test_ai_conv_add_user(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, "hello"), 0);
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ((int)conv.messages[0].role, (int)AI_ROLE_USER);
    ASSERT_STR_EQ(conv.messages[0].content, "hello");
    TEST_END();
}

int test_ai_conv_add_full(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    for (int i = 0; i < AI_MAX_MESSAGES; i++)
        ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, "msg"), 0);
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, "overflow"), -1);
    TEST_END();
}

int test_ai_conv_null_safety(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_conv_add(NULL, AI_ROLE_USER, "hello"), -1);
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, NULL), -1);
    TEST_END();
}

int test_ai_build_body_basic(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "deepseek-chat");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "You are helpful.");
    ai_conv_add(&conv, AI_ROLE_USER, "Hello");

    char buf[4096];
    size_t n = ai_build_request_body(&conv, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\"model\":\"deepseek-chat\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"role\":\"system\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"role\":\"user\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream\":false") != NULL);
    ASSERT_TRUE(strstr(buf, "You are helpful.") != NULL);
    ASSERT_TRUE(strstr(buf, "Hello") != NULL);
    TEST_END();
}

int test_ai_build_body_escapes(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_USER, "line1\nline2\ttab \"quoted\"");

    char buf[4096];
    size_t n = ai_build_request_body(&conv, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\\n") != NULL);
    ASSERT_TRUE(strstr(buf, "\\t") != NULL);
    ASSERT_TRUE(strstr(buf, "\\\"quoted\\\"") != NULL);
    /* Raw newline should NOT appear in JSON output */
    ASSERT_TRUE(strchr(buf, '\n') == NULL);
    TEST_END();
}

int test_ai_build_body_overflow(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_USER, "hello");

    char buf[10]; /* way too small */
    size_t n = ai_build_request_body(&conv, buf, sizeof(buf));
    ASSERT_EQ((int)n, 0);
    TEST_END();
}

int test_ai_build_body_null(void) {
    TEST_BEGIN();
    char buf[256];
    ASSERT_EQ((int)ai_build_request_body(NULL, buf, sizeof(buf)), 0);
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ASSERT_EQ((int)ai_build_request_body(&conv, NULL, 256), 0);
    ASSERT_EQ((int)ai_build_request_body(&conv, buf, 0), 0);
    TEST_END();
}

int test_ai_parse_response_basic(void) {
    TEST_BEGIN();
    const char *json =
        "{\"choices\":[{\"message\":{\"content\":\"Hello from AI\"}}]}";

    char content[256];
    ASSERT_EQ(ai_parse_response(json, content, sizeof(content)), 0);
    ASSERT_STR_EQ(content, "Hello from AI");
    TEST_END();
}

int test_ai_parse_response_empty_choices(void) {
    TEST_BEGIN();
    const char *json = "{\"choices\":[]}";
    char content[256];
    ASSERT_EQ(ai_parse_response(json, content, sizeof(content)), -1);
    TEST_END();
}

int test_ai_parse_response_malformed(void) {
    TEST_BEGIN();
    char content[256];
    ASSERT_EQ(ai_parse_response("not json", content, sizeof(content)), -1);
    ASSERT_EQ(ai_parse_response("{}", content, sizeof(content)), -1);
    ASSERT_EQ(ai_parse_response(NULL, content, sizeof(content)), -1);
    TEST_END();
}

int test_ai_extract_command_found(void) {
    TEST_BEGIN();
    const char *resp = "I'll list the files.\n[EXEC]ls -la[/EXEC]";
    char cmd[256];
    ASSERT_EQ(ai_extract_command(resp, cmd, sizeof(cmd)), 1);
    ASSERT_STR_EQ(cmd, "ls -la");
    TEST_END();
}

int test_ai_extract_command_none(void) {
    TEST_BEGIN();
    const char *resp = "I don't need to run any commands.";
    char cmd[256];
    ASSERT_EQ(ai_extract_command(resp, cmd, sizeof(cmd)), 0);
    TEST_END();
}

int test_ai_extract_command_no_end(void) {
    TEST_BEGIN();
    const char *resp = "[EXEC]ls -la";
    char cmd[256];
    ASSERT_EQ(ai_extract_command(resp, cmd, sizeof(cmd)), 0);
    TEST_END();
}

int test_ai_extract_command_empty(void) {
    TEST_BEGIN();
    const char *resp = "[EXEC][/EXEC]";
    char cmd[256];
    ASSERT_EQ(ai_extract_command(resp, cmd, sizeof(cmd)), 0);
    TEST_END();
}

int test_ai_extract_command_null(void) {
    TEST_BEGIN();
    char cmd[256];
    ASSERT_EQ(ai_extract_command(NULL, cmd, sizeof(cmd)), 0);
    ASSERT_EQ(ai_extract_command("test", NULL, 10), 0);
    ASSERT_EQ(ai_extract_command("test", cmd, 0), 0);
    TEST_END();
}

int test_ai_provider_url_deepseek(void) {
    TEST_BEGIN();
    const char *url = ai_provider_url("deepseek");
    ASSERT_NOT_NULL(url);
    ASSERT_TRUE(strstr(url, "deepseek.com") != NULL);
    TEST_END();
}

int test_ai_provider_url_openai(void) {
    TEST_BEGIN();
    const char *url = ai_provider_url("openai");
    ASSERT_NOT_NULL(url);
    ASSERT_TRUE(strstr(url, "openai.com") != NULL);
    TEST_END();
}

int test_ai_provider_url_unknown(void) {
    TEST_BEGIN();
    ASSERT_NULL(ai_provider_url("unknown"));
    ASSERT_NULL(ai_provider_url(NULL));
    TEST_END();
}

int test_ai_provider_model_deepseek(void) {
    TEST_BEGIN();
    const char *model = ai_provider_model("deepseek");
    ASSERT_NOT_NULL(model);
    ASSERT_STR_EQ(model, "deepseek-chat");
    TEST_END();
}

int test_ai_system_prompt_with_terminal(void) {
    TEST_BEGIN();
    char buf[2048];
    ai_build_system_prompt(buf, sizeof(buf), "user@host:~$ ls\nfile.txt");
    ASSERT_TRUE(strstr(buf, "SSH terminal") != NULL);
    ASSERT_TRUE(strstr(buf, "[EXEC]") != NULL);
    ASSERT_TRUE(strstr(buf, "user@host") != NULL);
    ASSERT_TRUE(strstr(buf, "file.txt") != NULL);
    TEST_END();
}

int test_ai_extract_command_with_context(void) {
    TEST_BEGIN();
    /* Realistic AI response with explanation before the command */
    const char *resp =
        "I'll check the disk usage for you.\n\n"
        "[EXEC]df -h[/EXEC]\n\n"
        "This will show all mounted filesystems.";
    char cmd[256];
    ASSERT_EQ(ai_extract_command(resp, cmd, sizeof(cmd)), 1);
    ASSERT_STR_EQ(cmd, "df -h");
    TEST_END();
}

int test_ai_extract_command_multiword(void) {
    TEST_BEGIN();
    /* Command with pipes and arguments */
    const char *resp = "Let me find large files.\n[EXEC]find / -size +100M -type f 2>/dev/null[/EXEC]";
    char cmd[256];
    ASSERT_EQ(ai_extract_command(resp, cmd, sizeof(cmd)), 1);
    ASSERT_STR_EQ(cmd, "find / -size +100M -type f 2>/dev/null");
    TEST_END();
}

int test_ai_extract_command_truncated(void) {
    TEST_BEGIN();
    /* Command too long for output buffer — truncated but still found */
    const char *resp = "[EXEC]echo hello world[/EXEC]";
    char cmd[8]; /* only room for 7 chars + NUL */
    ASSERT_EQ(ai_extract_command(resp, cmd, sizeof(cmd)), 1);
    ASSERT_STR_EQ(cmd, "echo he"); /* truncated to fit */
    TEST_END();
}

int test_ai_system_prompt_no_terminal(void) {
    TEST_BEGIN();
    char buf[2048];
    ai_build_system_prompt(buf, sizeof(buf), NULL);
    ASSERT_TRUE(strstr(buf, "SSH terminal") != NULL);
    /* Without terminal text, the "Current terminal output:" section is absent */
    ASSERT_TRUE(strstr(buf, "Current terminal output") == NULL);
    TEST_END();
}
