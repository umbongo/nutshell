/* tests/test_response_split.c — Tests for ai_response_split()
 *
 * ai_response_split() separates an AI response into:
 *   pre_cmd:  text BEFORE the first [EXEC] marker (shown immediately)
 *   post_cmd: text AFTER the last [/EXEC] marker (deferred until after execution)
 *
 * This ensures summaries/analysis are not displayed before commands run.
 */
#include "test_framework.h"
#include "ai_prompt.h"
#include <string.h>

/* No commands — full text goes to pre_cmd, post_cmd empty */
int test_split_no_commands(void) {
    TEST_BEGIN();
    char pre[1024], post[1024];
    int n = ai_response_split("Just some explanation text.", pre, sizeof(pre),
                              post, sizeof(post));
    ASSERT_EQ(n, 0);
    ASSERT_STR_EQ(pre, "Just some explanation text.");
    ASSERT_STR_EQ(post, "");
    TEST_END();
}

/* Single command with text before and after */
int test_split_single_command_with_summary(void) {
    TEST_BEGIN();
    char pre[1024], post[1024];
    const char *response =
        "Let me check the file:\n"
        "[EXEC]cat /etc/hosts[/EXEC]\n"
        "## Summary\n"
        "The file has been checked successfully.";
    int n = ai_response_split(response, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(pre, "Let me check the file:\n");
    ASSERT_STR_EQ(post, "\n## Summary\nThe file has been checked successfully.");
    TEST_END();
}

/* Multiple commands with summary after last [/EXEC] */
int test_split_multiple_commands_with_summary(void) {
    TEST_BEGIN();
    char pre[2048], post[2048];
    const char *response =
        "I'll run these commands:\n"
        "1. Check disk\n[EXEC]df -h[/EXEC]\n"
        "2. Check memory\n[EXEC]free -m[/EXEC]\n"
        "## Summary\n"
        "I have checked disk and memory usage.";
    int n = ai_response_split(response, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 2);
    /* Pre = everything before first [EXEC] */
    ASSERT_STR_EQ(pre, "I'll run these commands:\n1. Check disk\n");
    ASSERT_STR_EQ(post, "\n## Summary\nI have checked disk and memory usage.");
    TEST_END();
}

/* Commands only, no surrounding text */
int test_split_commands_only(void) {
    TEST_BEGIN();
    char pre[1024], post[1024];
    const char *response = "[EXEC]ls[/EXEC]";
    int n = ai_response_split(response, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(pre, "");
    ASSERT_STR_EQ(post, "");
    TEST_END();
}

/* Text before commands only, nothing after */
int test_split_no_post_text(void) {
    TEST_BEGIN();
    char pre[1024], post[1024];
    const char *response =
        "Running the command:\n"
        "[EXEC]uptime[/EXEC]";
    int n = ai_response_split(response, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(pre, "Running the command:\n");
    ASSERT_STR_EQ(post, "");
    TEST_END();
}

/* NULL inputs should not crash */
int test_split_null_response(void) {
    TEST_BEGIN();
    char pre[64], post[64];
    int n = ai_response_split(NULL, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 0);
    ASSERT_STR_EQ(pre, "");
    ASSERT_STR_EQ(post, "");
    TEST_END();
}

int test_split_null_outputs(void) {
    TEST_BEGIN();
    int n = ai_response_split("hello [EXEC]ls[/EXEC] world", NULL, 0, NULL, 0);
    ASSERT_EQ(n, 1);
    TEST_END();
}

/* Empty response */
int test_split_empty_response(void) {
    TEST_BEGIN();
    char pre[64], post[64];
    int n = ai_response_split("", pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 0);
    ASSERT_STR_EQ(pre, "");
    ASSERT_STR_EQ(post, "");
    TEST_END();
}

/* Text between commands is NOT included in pre or post —
 * only text before first [EXEC] and after last [/EXEC] */
int test_split_inter_command_text_excluded(void) {
    TEST_BEGIN();
    char pre[1024], post[1024];
    const char *response =
        "Step 1:\n"
        "[EXEC]ls[/EXEC]\n"
        "Now step 2:\n"
        "[EXEC]pwd[/EXEC]\n"
        "All done.";
    int n = ai_response_split(response, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 2);
    ASSERT_STR_EQ(pre, "Step 1:\n");
    ASSERT_STR_EQ(post, "\nAll done.");
    TEST_END();
}

/* Whitespace-only post text is still captured */
int test_split_whitespace_post(void) {
    TEST_BEGIN();
    char pre[1024], post[1024];
    const char *response = "Check:\n[EXEC]ls[/EXEC]\n\n";
    int n = ai_response_split(response, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(pre, "Check:\n");
    ASSERT_STR_EQ(post, "\n\n");
    TEST_END();
}

/* Pre buffer too small — truncates safely */
int test_split_pre_truncated(void) {
    TEST_BEGIN();
    char pre[10], post[1024];
    const char *response = "A long explanation here\n[EXEC]ls[/EXEC]\nDone.";
    int n = ai_response_split(response, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 1);
    /* Should be truncated to fit within 10 bytes (9 chars + NUL) */
    ASSERT_EQ((int)strlen(pre) < 10, 1);
    ASSERT_STR_EQ(post, "\nDone.");
    TEST_END();
}

/* Post buffer too small — truncates safely */
int test_split_post_truncated(void) {
    TEST_BEGIN();
    char pre[1024], post[10];
    const char *response = "Hi\n[EXEC]ls[/EXEC]\nA very long summary here";
    int n = ai_response_split(response, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(pre, "Hi\n");
    ASSERT_EQ((int)strlen(post) < 10, 1);
    TEST_END();
}

/* Unclosed [EXEC] — treated as no valid commands */
int test_split_unclosed_exec(void) {
    TEST_BEGIN();
    char pre[1024], post[1024];
    const char *response = "Text before\n[EXEC]ls\nSummary after";
    int n = ai_response_split(response, pre, sizeof(pre), post, sizeof(post));
    ASSERT_EQ(n, 0);
    ASSERT_STR_EQ(pre, "Text before\n[EXEC]ls\nSummary after");
    ASSERT_STR_EQ(post, "");
    TEST_END();
}
