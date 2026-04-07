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

int test_ai_provider_url_moonshot(void) {
    TEST_BEGIN();
    const char *url = ai_provider_url("moonshot");
    ASSERT_NOT_NULL(url);
    ASSERT_TRUE(strstr(url, "moonshot.ai") != NULL);
    TEST_END();
}

int test_ai_provider_model_moonshot(void) {
    TEST_BEGIN();
    const char *model = ai_provider_model("moonshot");
    ASSERT_NOT_NULL(model);
    ASSERT_STR_EQ(model, "kimi-k2");
    TEST_END();
}

int test_ai_provider_models_deepseek(void) {
    TEST_BEGIN();
    const char * const *models = ai_provider_models("deepseek");
    ASSERT_NOT_NULL(models);
    ASSERT_STR_EQ(models[0], "deepseek-chat");
    /* Ensure list is NULL-terminated */
    int count = 0;
    while (models[count]) count++;
    ASSERT_TRUE(count >= 2);
    TEST_END();
}

int test_ai_provider_models_moonshot(void) {
    TEST_BEGIN();
    const char * const *models = ai_provider_models("moonshot");
    ASSERT_NOT_NULL(models);
    ASSERT_STR_EQ(models[0], "kimi-k2");
    int count = 0;
    while (models[count]) count++;
    ASSERT_EQ(count, 4);
    TEST_END();
}

int test_ai_provider_models_unknown(void) {
    TEST_BEGIN();
    ASSERT_NULL(ai_provider_models("unknown"));
    ASSERT_NULL(ai_provider_models("custom"));
    ASSERT_NULL(ai_provider_models(NULL));
    TEST_END();
}

int test_ai_provider_models_url_known(void) {
    TEST_BEGIN();
    const char *url;
    url = ai_provider_models_url("deepseek");
    ASSERT_NOT_NULL(url);
    ASSERT_TRUE(strstr(url, "deepseek.com") != NULL);
    ASSERT_TRUE(strstr(url, "models") != NULL);
    url = ai_provider_models_url("openai");
    ASSERT_NOT_NULL(url);
    ASSERT_TRUE(strstr(url, "openai.com") != NULL);
    url = ai_provider_models_url("anthropic");
    ASSERT_NOT_NULL(url);
    ASSERT_TRUE(strstr(url, "anthropic.com") != NULL);
    url = ai_provider_models_url("moonshot");
    ASSERT_NOT_NULL(url);
    ASSERT_TRUE(strstr(url, "moonshot.ai") != NULL);
    TEST_END();
}

int test_ai_provider_models_url_unknown(void) {
    TEST_BEGIN();
    ASSERT_NULL(ai_provider_models_url("unknown"));
    ASSERT_NULL(ai_provider_models_url("custom"));
    ASSERT_NULL(ai_provider_models_url(NULL));
    TEST_END();
}

int test_ai_system_prompt_with_terminal(void) {
    TEST_BEGIN();
    char buf[2048];
    ai_build_system_prompt(buf, sizeof(buf), "user@host:~$ ls\nfile.txt", NULL, NULL);
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

int test_ai_extract_commands_multiple(void) {
    TEST_BEGIN();
    const char *resp =
        "I'll check the system.\n"
        "1. Disk usage:\n[EXEC]df -h[/EXEC]\n"
        "2. Memory:\n[EXEC]free -m[/EXEC]\n"
        "3. Uptime:\n[EXEC]uptime[/EXEC]";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 3);
    ASSERT_STR_EQ(cmds[0], "df -h");
    ASSERT_STR_EQ(cmds[1], "free -m");
    ASSERT_STR_EQ(cmds[2], "uptime");
    TEST_END();
}

int test_ai_extract_commands_single(void) {
    TEST_BEGIN();
    const char *resp = "Let me check.\n[EXEC]ls -la[/EXEC]";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(cmds[0], "ls -la");
    TEST_END();
}

int test_ai_extract_commands_none(void) {
    TEST_BEGIN();
    const char *resp = "No commands needed.";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 0);
    TEST_END();
}

int test_ai_extract_commands_skip_empty(void) {
    TEST_BEGIN();
    /* Empty [EXEC][/EXEC] should be skipped */
    const char *resp = "[EXEC][/EXEC]\n[EXEC]ls[/EXEC]";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(cmds[0], "ls");
    TEST_END();
}

int test_ai_extract_commands_max_limit(void) {
    TEST_BEGIN();
    /* Only extract up to max_cmds */
    const char *resp = "[EXEC]a[/EXEC][EXEC]b[/EXEC][EXEC]c[/EXEC]";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 2);
    ASSERT_EQ(n, 2);
    ASSERT_STR_EQ(cmds[0], "a");
    ASSERT_STR_EQ(cmds[1], "b");
    TEST_END();
}

int test_ai_extract_commands_null(void) {
    TEST_BEGIN();
    char cmds[16][1024];
    ASSERT_EQ(ai_extract_commands(NULL, cmds, 16), 0);
    ASSERT_EQ(ai_extract_commands("test", NULL, 16), 0);
    ASSERT_EQ(ai_extract_commands("test", cmds, 0), 0);
    TEST_END();
}

int test_ai_system_prompt_multi_command(void) {
    TEST_BEGIN();
    char buf[4096];
    ai_build_system_prompt(buf, sizeof(buf), NULL, NULL, NULL);
    /* System prompt should mention multiple commands */
    ASSERT_TRUE(strstr(buf, "multiple commands") != NULL);
    TEST_END();
}

int test_ai_extract_commands_with_text_between(void) {
    TEST_BEGIN();
    /* Commands interspersed with explanatory text */
    const char *resp =
        "Here's my plan:\n\n"
        "1. First, create the file\n"
        "[EXEC]touch test1.txt[/EXEC]\n\n"
        "2. Add content to it\n"
        "[EXEC]echo \"this is a test\" > test1.txt[/EXEC]\n\n"
        "3. Display the file\n"
        "[EXEC]cat test1.txt[/EXEC]\n\n"
        "4. Finally, clean up\n"
        "[EXEC]rm test1.txt[/EXEC]";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 4);
    ASSERT_STR_EQ(cmds[0], "touch test1.txt");
    ASSERT_STR_EQ(cmds[1], "echo \"this is a test\" > test1.txt");
    ASSERT_STR_EQ(cmds[2], "cat test1.txt");
    ASSERT_STR_EQ(cmds[3], "rm test1.txt");
    TEST_END();
}

int test_ai_extract_commands_partial_markers(void) {
    TEST_BEGIN();
    /* Second command has no closing tag — only first is extracted */
    const char *resp = "[EXEC]ls[/EXEC] then [EXEC]pwd";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(cmds[0], "ls");
    TEST_END();
}

int test_ai_extract_commands_adjacent(void) {
    TEST_BEGIN();
    /* Commands with no whitespace between them */
    const char *resp = "[EXEC]cmd1[/EXEC][EXEC]cmd2[/EXEC][EXEC]cmd3[/EXEC]";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 3);
    ASSERT_STR_EQ(cmds[0], "cmd1");
    ASSERT_STR_EQ(cmds[1], "cmd2");
    ASSERT_STR_EQ(cmds[2], "cmd3");
    TEST_END();
}

int test_ai_extract_commands_with_pipes(void) {
    TEST_BEGIN();
    /* Commands containing pipes, redirects, and special chars */
    const char *resp =
        "[EXEC]ps aux | grep nginx[/EXEC]\n"
        "[EXEC]cat /etc/passwd | wc -l[/EXEC]\n"
        "[EXEC]ls -la > /tmp/out.txt 2>&1[/EXEC]";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 3);
    ASSERT_STR_EQ(cmds[0], "ps aux | grep nginx");
    ASSERT_STR_EQ(cmds[1], "cat /etc/passwd | wc -l");
    ASSERT_STR_EQ(cmds[2], "ls -la > /tmp/out.txt 2>&1");
    TEST_END();
}

int test_ai_extract_commands_single_matches_old(void) {
    TEST_BEGIN();
    /* ai_extract_command (singular) and ai_extract_commands (plural)
     * should agree on the first command */
    const char *resp =
        "Step 1:\n[EXEC]df -h[/EXEC]\n"
        "Step 2:\n[EXEC]free -m[/EXEC]";
    char cmd_single[1024];
    ASSERT_EQ(ai_extract_command(resp, cmd_single, sizeof(cmd_single)), 1);

    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 2);
    /* First command from both must match */
    ASSERT_STR_EQ(cmd_single, cmds[0]);
    TEST_END();
}

int test_ai_extract_commands_newlines_in_command(void) {
    TEST_BEGIN();
    /* A command that spans multiple lines (e.g., heredoc) */
    const char *resp = "[EXEC]cat <<EOF\nhello\nworld\nEOF[/EXEC]";
    char cmds[16][1024];
    int n = ai_extract_commands(resp, cmds, 16);
    ASSERT_EQ(n, 1);
    ASSERT_STR_EQ(cmds[0], "cat <<EOF\nhello\nworld\nEOF");
    TEST_END();
}

int test_ai_system_prompt_all_commands_instruction(void) {
    TEST_BEGIN();
    char buf[4096];
    ai_build_system_prompt(buf, sizeof(buf), NULL, NULL, NULL);
    /* Must instruct to include ALL commands */
    ASSERT_TRUE(strstr(buf, "ALL commands") != NULL);
    /* Must discourage splitting across responses */
    ASSERT_TRUE(strstr(buf, "Do NOT split") != NULL);
    /* Must have multi-command example with 3 [EXEC] blocks */
    ASSERT_TRUE(strstr(buf, "[EXEC]df -h[/EXEC]") != NULL);
    ASSERT_TRUE(strstr(buf, "[EXEC]free -m[/EXEC]") != NULL);
    ASSERT_TRUE(strstr(buf, "[EXEC]uptime[/EXEC]") != NULL);
    TEST_END();
}

int test_ai_system_prompt_no_partial_plan(void) {
    TEST_BEGIN();
    char buf[4096];
    ai_build_system_prompt(buf, sizeof(buf), NULL, NULL, NULL);
    /* Must discourage "let's start with" partial plans */
    ASSERT_TRUE(strstr(buf, "Never say") != NULL);
    ASSERT_TRUE(strstr(buf, "first step") != NULL);
    TEST_END();
}

int test_ai_system_prompt_no_terminal(void) {
    TEST_BEGIN();
    char buf[2048];
    ai_build_system_prompt(buf, sizeof(buf), NULL, NULL, NULL);
    ASSERT_TRUE(strstr(buf, "SSH terminal") != NULL);
    /* Without terminal text, the "Current terminal output:" section is absent */
    ASSERT_TRUE(strstr(buf, "Current terminal output") == NULL);
    TEST_END();
}

/* --- ai_input_key_action tests --- */

int test_ai_input_enter_sends(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_input_key_action(1, 0), AI_INPUT_SEND);
    TEST_END();
}

int test_ai_input_shift_enter_newline(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_input_key_action(1, 1), AI_INPUT_NEWLINE);
    TEST_END();
}

int test_ai_input_other_key_passthrough(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_input_key_action(0, 0), AI_INPUT_PASSTHROUGH);
    TEST_END();
}

int test_ai_input_other_key_with_shift_passthrough(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_input_key_action(0, 1), AI_INPUT_PASSTHROUGH);
    TEST_END();
}

/* --- ai_conv_reset tests --- */

int test_ai_conv_reset_clears_messages(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "deepseek-chat");
    ai_conv_add(&conv, AI_ROLE_USER, "hello");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "hi there");
    ASSERT_EQ(conv.msg_count, 2);
    ai_conv_reset(&conv);
    ASSERT_EQ(conv.msg_count, 0);
    TEST_END();
}

int test_ai_conv_reset_preserves_model(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "gpt-4o");
    ai_conv_add(&conv, AI_ROLE_USER, "test");
    ai_conv_reset(&conv);
    ASSERT_STR_EQ(conv.model, "gpt-4o");
    TEST_END();
}

int test_ai_conv_reset_allows_reuse(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test-model");
    /* Fill some messages */
    for (int i = 0; i < 10; i++)
        ai_conv_add(&conv, AI_ROLE_USER, "msg");
    ASSERT_EQ(conv.msg_count, 10);
    ai_conv_reset(&conv);
    ASSERT_EQ(conv.msg_count, 0);
    /* Should be able to add messages again */
    ASSERT_EQ(ai_conv_add(&conv, AI_ROLE_USER, "new msg"), 0);
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_STR_EQ(conv.messages[0].content, "new msg");
    TEST_END();
}

int test_ai_conv_reset_null_safety(void) {
    TEST_BEGIN();
    /* Should not crash */
    ai_conv_reset(NULL);
    TEST_END();
}

/* --- ai_command_is_readonly tests --- */

int test_ai_cmd_readonly_ls(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("ls -la /tmp"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_cat(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("cat /etc/passwd"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_grep(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("grep -r pattern /home"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_find(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("find / -name '*.log'"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_ps(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("ps aux"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_df(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("df -h"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_pipe_readonly(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("ps aux | grep nginx"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_echo_no_redirect(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("echo hello world"), 1);
    TEST_END();
}

int test_ai_cmd_write_rm(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("rm -rf /tmp/junk"), 0);
    TEST_END();
}

int test_ai_cmd_write_mv(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("mv file1 file2"), 0);
    TEST_END();
}

int test_ai_cmd_write_cp(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("cp src dst"), 0);
    TEST_END();
}

int test_ai_cmd_write_mkdir(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("mkdir -p /tmp/newdir"), 0);
    TEST_END();
}

int test_ai_cmd_write_touch(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("touch newfile.txt"), 0);
    TEST_END();
}

int test_ai_cmd_write_chmod(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("chmod 755 script.sh"), 0);
    TEST_END();
}

int test_ai_cmd_write_redirect(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("echo hello > file.txt"), 0);
    TEST_END();
}

int test_ai_cmd_write_append_redirect(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("echo hello >> file.txt"), 0);
    TEST_END();
}

int test_ai_cmd_write_sudo(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("sudo rm -rf /"), 0);
    TEST_END();
}

int test_ai_cmd_write_tee(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("cat file | tee output.txt"), 0);
    TEST_END();
}

int test_ai_cmd_write_sed_inplace(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("sed -i 's/old/new/' file"), 0);
    TEST_END();
}

int test_ai_cmd_write_vim(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("vim file.txt"), 0);
    TEST_END();
}

int test_ai_cmd_write_nano(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("nano file.txt"), 0);
    TEST_END();
}

int test_ai_cmd_write_apt(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("apt install curl"), 0);
    TEST_END();
}

int test_ai_cmd_write_kill(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("kill -9 1234"), 0);
    TEST_END();
}

int test_ai_cmd_write_pipe_to_write(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("cat file | sed -i 's/a/b/' f"), 0);
    TEST_END();
}

int test_ai_cmd_readonly_null(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly(NULL), 1);
    TEST_END();
}

int test_ai_cmd_readonly_empty(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly(""), 1);
    TEST_END();
}

int test_ai_cmd_readonly_leading_spaces(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("  ls -la"), 1);
    ASSERT_EQ(ai_command_is_readonly("  rm file"), 0);
    TEST_END();
}

int test_ai_cmd_write_dd(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("dd if=/dev/zero of=/tmp/file"), 0);
    TEST_END();
}

int test_ai_cmd_write_chown(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("chown root:root file"), 0);
    TEST_END();
}

int test_ai_cmd_write_reboot(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("reboot"), 0);
    TEST_END();
}

/* --- ai_command_is_readonly: harmless redirect tests --- */

int test_ai_cmd_readonly_stderr_devnull(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("find / -name '*.log' 2>/dev/null"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_stderr_devnull_space(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("find / -name '*.log' 2> /dev/null"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_stdout_devnull(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("ls > /dev/null"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_stderr_to_stdout(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("ls 2>&1"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_ampersand_devnull(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("ls &>/dev/null"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_find_exec_devnull_pipe(void) {
    TEST_BEGIN();
    /* The exact command from the user's bug report */
    ASSERT_EQ(ai_command_is_readonly(
        "find ~ -type f -exec du -h {} + 2>/dev/null | sort -rh | head -20"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_du_devnull(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("du -sh /* 2>/dev/null | sort -rh"), 1);
    TEST_END();
}

int test_ai_cmd_write_redirect_to_file(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("echo hello > file.txt"), 0);
    TEST_END();
}

int test_ai_cmd_write_append_to_file(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("echo hello >> file.txt"), 0);
    TEST_END();
}

int test_ai_cmd_write_stderr_to_file(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("cmd 2> errors.log"), 0);
    TEST_END();
}

/* --- ai_command_is_readonly: network device commands --- */
/* Note: ai_command_is_readonly() now delegates to cmd_classify() with
 * CMD_PLATFORM_LINUX. Network-device-specific commands (configure, write,
 * commit, reload, rollback, erase, execute) are handled by their
 * respective platform classifiers, not the Linux classifier. On Linux
 * these are unknown commands and correctly return readonly/safe. */

int test_ai_cmd_write_configure_terminal(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("configure terminal"), 1);
    TEST_END();
}

int test_ai_cmd_write_conf_t(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("conf t"), 1);
    TEST_END();
}

int test_ai_cmd_write_configure_paloalto(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("configure"), 1);
    TEST_END();
}

int test_ai_cmd_write_write_memory(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("write memory"), 1);
    TEST_END();
}

int test_ai_cmd_write_commit(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("commit"), 1);
    TEST_END();
}

int test_ai_cmd_write_reload(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("reload"), 1);
    TEST_END();
}

int test_ai_cmd_write_rollback(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("rollback"), 1);
    TEST_END();
}

int test_ai_cmd_write_erase(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("erase startup-config"), 1);
    TEST_END();
}

int test_ai_cmd_write_execute(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("execute reboot"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_show_running(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("show running-config"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_show_interfaces(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("show interfaces"), 1);
    TEST_END();
}

int test_ai_cmd_readonly_display_version(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_command_is_readonly("display version"), 1);
    TEST_END();
}

/* --- ai_build_confirm_text tests --- */

int test_ai_confirm_text_single(void) {
    TEST_BEGIN();
    char cmds[1][1024] = {"ls -la"};
    char buf[512];
    size_t n = ai_build_confirm_text(cmds, 1, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "1 command") != NULL);
    ASSERT_TRUE(strstr(buf, "  1. ls -la") != NULL);
    ASSERT_TRUE(strstr(buf, "Allow") != NULL);
    TEST_END();
}

int test_ai_confirm_text_multiple(void) {
    TEST_BEGIN();
    char cmds[3][1024] = {"df -h", "free -m", "uptime"};
    char buf[512];
    size_t n = ai_build_confirm_text(cmds, 3, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "3 commands") != NULL);
    ASSERT_TRUE(strstr(buf, "  1. df -h") != NULL);
    ASSERT_TRUE(strstr(buf, "  2. free -m") != NULL);
    ASSERT_TRUE(strstr(buf, "  3. uptime") != NULL);
    TEST_END();
}

int test_ai_confirm_text_null(void) {
    TEST_BEGIN();
    char buf[512];
    ASSERT_EQ((int)ai_build_confirm_text(NULL, 1, buf, sizeof(buf)), 0);
    char cmds[1][1024] = {"ls"};
    ASSERT_EQ((int)ai_build_confirm_text(cmds, 0, buf, sizeof(buf)), 0);
    ASSERT_EQ((int)ai_build_confirm_text(cmds, 1, NULL, sizeof(buf)), 0);
    ASSERT_EQ((int)ai_build_confirm_text(cmds, 1, buf, 0), 0);
    TEST_END();
}

int test_ai_confirm_text_overflow(void) {
    TEST_BEGIN();
    char cmds[1][1024] = {"ls -la"};
    char buf[10]; /* too small */
    size_t n = ai_build_confirm_text(cmds, 1, buf, sizeof(buf));
    /* Should either return 0 (error) or truncate gracefully */
    ASSERT_TRUE(n == 0 || strlen(buf) < sizeof(buf));
    TEST_END();
}

int test_ai_confirm_text_numbering(void) {
    TEST_BEGIN();
    /* Verify sequential numbering with 5 commands */
    char cmds[5][1024] = {"a", "b", "c", "d", "e"};
    char buf[512];
    size_t n = ai_build_confirm_text(cmds, 5, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "  1. a") != NULL);
    ASSERT_TRUE(strstr(buf, "  5. e") != NULL);
    ASSERT_TRUE(strstr(buf, "5 commands") != NULL);
    TEST_END();
}

/* --- ai_parse_response_ex tests (thinking/reasoning content) --- */

int test_ai_parse_response_ex_no_thinking(void) {
    TEST_BEGIN();
    const char *json =
        "{\"choices\":[{\"message\":{\"content\":\"Hello from AI\"}}]}";
    char content[256], thinking[256];
    ASSERT_EQ(ai_parse_response_ex(json, content, sizeof(content),
                                    thinking, sizeof(thinking)), 0);
    ASSERT_STR_EQ(content, "Hello from AI");
    ASSERT_STR_EQ(thinking, "");
    TEST_END();
}

int test_ai_parse_response_ex_with_reasoning(void) {
    TEST_BEGIN();
    /* DeepSeek reasoner format: reasoning_content alongside content */
    const char *json =
        "{\"choices\":[{\"message\":{"
        "\"reasoning_content\":\"Let me think about this...\","
        "\"content\":\"The answer is 42.\"}}]}";
    char content[256], thinking[256];
    ASSERT_EQ(ai_parse_response_ex(json, content, sizeof(content),
                                    thinking, sizeof(thinking)), 0);
    ASSERT_STR_EQ(content, "The answer is 42.");
    ASSERT_STR_EQ(thinking, "Let me think about this...");
    TEST_END();
}

int test_ai_parse_response_ex_null_thinking_buf(void) {
    TEST_BEGIN();
    /* If thinking_out is NULL, should still work (just ignore thinking) */
    const char *json =
        "{\"choices\":[{\"message\":{"
        "\"reasoning_content\":\"thinking...\","
        "\"content\":\"answer\"}}]}";
    char content[256];
    ASSERT_EQ(ai_parse_response_ex(json, content, sizeof(content),
                                    NULL, 0), 0);
    ASSERT_STR_EQ(content, "answer");
    TEST_END();
}

/* --- ai_build_request_body_ex stream tests --- */

int test_ai_build_body_stream_true(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test-model");
    ai_conv_add(&conv, AI_ROLE_USER, "hello");
    char buf[4096];
    size_t n = ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 1);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\"stream\":true") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream\":false") == NULL);
    TEST_END();
}

int test_ai_build_body_stream_false(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test-model");
    ai_conv_add(&conv, AI_ROLE_USER, "hello");
    char buf[4096];
    size_t n = ai_build_request_body_ex(&conv, NULL, buf, sizeof(buf), 0);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\"stream\":false") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream\":true") == NULL);
    TEST_END();
}

int test_ai_build_body_ex_matches_original(void) {
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "gpt-4o");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "You are helpful.");
    ai_conv_add(&conv, AI_ROLE_USER, "Hello");
    char buf1[4096], buf2[4096];
    size_t n1 = ai_build_request_body(&conv, buf1, sizeof(buf1));
    size_t n2 = ai_build_request_body_ex(&conv, NULL, buf2, sizeof(buf2), 0);
    ASSERT_EQ((int)n1, (int)n2);
    ASSERT_STR_EQ(buf1, buf2);
    TEST_END();
}

/* --- ai_parse_stream_chunk tests --- */

int test_ai_parse_stream_chunk_content(void) {
    TEST_BEGIN();
    const char *json = "{\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}";
    char content[256], thinking[256];
    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking));
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(content, "Hello");
    ASSERT_STR_EQ(thinking, "");
    TEST_END();
}

int test_ai_parse_stream_chunk_thinking(void) {
    TEST_BEGIN();
    const char *json = "{\"choices\":[{\"delta\":{\"reasoning_content\":\"Let me think...\"}}]}";
    char content[256], thinking[256];
    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking));
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(content, "");
    ASSERT_STR_EQ(thinking, "Let me think...");
    TEST_END();
}

int test_ai_parse_stream_chunk_both(void) {
    TEST_BEGIN();
    const char *json = "{\"choices\":[{\"delta\":{\"reasoning_content\":\"hmm\",\"content\":\"ok\"}}]}";
    char content[256], thinking[256];
    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking));
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(content, "ok");
    ASSERT_STR_EQ(thinking, "hmm");
    TEST_END();
}

int test_ai_parse_stream_chunk_done(void) {
    TEST_BEGIN();
    int rc = ai_parse_stream_chunk("[DONE]", NULL, 0, NULL, 0);
    ASSERT_EQ(rc, 1);
    TEST_END();
}

int test_ai_parse_stream_chunk_role_only(void) {
    TEST_BEGIN();
    /* First chunk often has just the role, no content */
    const char *json = "{\"choices\":[{\"delta\":{\"role\":\"assistant\"}}]}";
    char content[256], thinking[256];
    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking));
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(content, "");
    ASSERT_STR_EQ(thinking, "");
    TEST_END();
}

int test_ai_parse_stream_chunk_empty_delta(void) {
    TEST_BEGIN();
    const char *json = "{\"choices\":[{\"delta\":{}}]}";
    char content[256], thinking[256];
    int rc = ai_parse_stream_chunk(json, content, sizeof(content),
                                   thinking, sizeof(thinking));
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(content, "");
    ASSERT_STR_EQ(thinking, "");
    TEST_END();
}

int test_ai_parse_stream_chunk_null(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_parse_stream_chunk(NULL, NULL, 0, NULL, 0), -1);
    TEST_END();
}

int test_ai_parse_stream_chunk_malformed(void) {
    TEST_BEGIN();
    ASSERT_EQ(ai_parse_stream_chunk("not json", NULL, 0, NULL, 0), -1);
    ASSERT_EQ(ai_parse_stream_chunk("{}", NULL, 0, NULL, 0), -1);
    TEST_END();
}

int test_ai_parse_response_ex_empty_reasoning(void) {
    TEST_BEGIN();
    const char *json =
        "{\"choices\":[{\"message\":{"
        "\"reasoning_content\":\"\","
        "\"content\":\"direct answer\"}}]}";
    char content[256], thinking[256];
    ASSERT_EQ(ai_parse_response_ex(json, content, sizeof(content),
                                    thinking, sizeof(thinking)), 0);
    ASSERT_STR_EQ(content, "direct answer");
    ASSERT_STR_EQ(thinking, "");
    TEST_END();
}

/* --- AiSessionState tests --- */

int test_session_state_init(void) {
    TEST_BEGIN();
    AiSessionState state;
    memset(&state, 0, sizeof(state));
    ASSERT_EQ(state.valid, 0);
    ASSERT_EQ(state.conv.msg_count, 0);
    TEST_END();
}

int test_session_save_restore(void) {
    TEST_BEGIN();
    /* Create a conversation with messages */
    AiConversation conv;
    ai_conv_init(&conv, "test-model");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "system prompt");
    ai_conv_add(&conv, AI_ROLE_USER, "hello");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "hi there");

    /* Save to session state */
    AiSessionState state;
    memset(&state, 0, sizeof(state));
    memcpy(&state.conv, &conv, sizeof(AiConversation));
    state.valid = 1;

    /* Start a fresh working conv */
    AiConversation work;
    ai_conv_init(&work, "test-model");
    ASSERT_EQ(work.msg_count, 0);

    /* Restore from session state */
    memcpy(&work, &state.conv, sizeof(AiConversation));
    ASSERT_EQ(work.msg_count, 3);
    ASSERT_STR_EQ(work.messages[1].content, "hello");
    ASSERT_STR_EQ(work.messages[2].content, "hi there");
    ASSERT_STR_EQ(work.model, "test-model");
    TEST_END();
}

int test_session_conv_independence(void) {
    TEST_BEGIN();
    /* Two separate session states */
    AiSessionState s1, s2;
    memset(&s1, 0, sizeof(s1));
    memset(&s2, 0, sizeof(s2));

    /* Populate state 1 */
    ai_conv_init(&s1.conv, "model-a");
    ai_conv_add(&s1.conv, AI_ROLE_USER, "msg for server A");
    s1.valid = 1;

    /* Populate state 2 */
    ai_conv_init(&s2.conv, "model-b");
    ai_conv_add(&s2.conv, AI_ROLE_USER, "msg for server B");
    ai_conv_add(&s2.conv, AI_ROLE_ASSISTANT, "reply from B");
    s2.valid = 1;

    /* Simulate: load s1 into working conv */
    AiConversation work;
    memcpy(&work, &s1.conv, sizeof(AiConversation));
    ASSERT_EQ(work.msg_count, 1);
    ASSERT_STR_EQ(work.messages[0].content, "msg for server A");

    /* Save back to s1, load s2 */
    memcpy(&s1.conv, &work, sizeof(AiConversation));
    memcpy(&work, &s2.conv, sizeof(AiConversation));
    ASSERT_EQ(work.msg_count, 2);
    ASSERT_STR_EQ(work.messages[0].content, "msg for server B");

    /* Verify s1 wasn't corrupted */
    ASSERT_EQ(s1.conv.msg_count, 1);
    ASSERT_STR_EQ(s1.conv.messages[0].content, "msg for server A");
    TEST_END();
}

int test_session_new_chat_reset(void) {
    TEST_BEGIN();
    AiSessionState state;
    memset(&state, 0, sizeof(state));
    ai_conv_init(&state.conv, "test");
    ai_conv_add(&state.conv, AI_ROLE_USER, "old message");
    state.valid = 1;

    /* Simulate New Chat: reset both working conv and state */
    AiConversation work;
    memcpy(&work, &state.conv, sizeof(AiConversation));
    ASSERT_EQ(work.msg_count, 1);

    ai_conv_reset(&work);
    ai_conv_reset(&state.conv);
    state.valid = 1;  /* Keep valid so switching back shows empty, not stale */

    ASSERT_EQ(work.msg_count, 0);
    ASSERT_EQ(state.conv.msg_count, 0);
    ASSERT_EQ(state.valid, 1);
    /* Model preserved after reset */
    ASSERT_STR_EQ(work.model, "test");
    TEST_END();
}

/* ---- ai_cmd_progress_text / ai_cmd_waiting_text tests ---- */

int test_cmd_progress_single(void)
{
    TEST_BEGIN();
    char buf[64];
    int n = ai_cmd_progress_text(1, 1, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, "(executing 1/1.)");
    TEST_END();
}

int test_cmd_progress_multi(void)
{
    TEST_BEGIN();
    char buf[64];
    ai_cmd_progress_text(2, 4, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "(executing 2/4.)");
    TEST_END();
}

int test_cmd_progress_last(void)
{
    TEST_BEGIN();
    char buf[64];
    ai_cmd_progress_text(4, 4, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "(executing 4/4.)");
    TEST_END();
}

int test_cmd_progress_null_buf(void)
{
    TEST_BEGIN();
    int n = ai_cmd_progress_text(1, 1, NULL, 0);
    ASSERT_EQ(n, 0);
    TEST_END();
}

int test_cmd_progress_small_buf(void)
{
    TEST_BEGIN();
    char buf[8];
    int n = ai_cmd_progress_text(1, 1, buf, sizeof(buf));
    /* Should be truncated but not overflow */
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strlen(buf) < 8);
    TEST_END();
}

int test_cmd_waiting_text(void)
{
    TEST_BEGIN();
    char buf[64];
    int n = ai_cmd_waiting_text(buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, "(waiting for output.)");
    TEST_END();
}

int test_cmd_waiting_null_buf(void)
{
    TEST_BEGIN();
    int n = ai_cmd_waiting_text(NULL, 0);
    ASSERT_EQ(n, 0);
    TEST_END();
}

/* ---- ai_build_save_text tests ---- */

int test_save_text_empty_conv(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    char buf[4096];
    size_t n = ai_build_save_text(&conv, NULL, 0, buf, sizeof(buf));
    /* Empty conversation still produces header */
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "AI Assist") != NULL);
    TEST_END();
}

int test_save_text_user_message(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "system prompt");
    ai_conv_add(&conv, AI_ROLE_USER, "hello world");
    char buf[4096];
    size_t n = ai_build_save_text(&conv, NULL, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "--- You ---") != NULL);
    ASSERT_TRUE(strstr(buf, "hello world") != NULL);
    TEST_END();
}

int test_save_text_assistant_message(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "system prompt");
    ai_conv_add(&conv, AI_ROLE_USER, "hi");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "hello back");
    char buf[4096];
    size_t n = ai_build_save_text(&conv, NULL, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "--- AI ---") != NULL);
    ASSERT_TRUE(strstr(buf, "hello back") != NULL);
    TEST_END();
}

int test_save_text_with_thinking(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&conv, AI_ROLE_USER, "q");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "answer");
    /* thinking_history[2] corresponds to msg index 2 (the assistant msg) */
    char *thinking[AI_MAX_MESSAGES] = {0};
    thinking[2] = "let me reason about this";
    char buf[4096];
    size_t n = ai_build_save_text(&conv, thinking, 1, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "--- Thinking ---") != NULL);
    ASSERT_TRUE(strstr(buf, "let me reason about this") != NULL);
    TEST_END();
}

int test_save_text_thinking_hidden(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&conv, AI_ROLE_USER, "q");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "answer");
    char *thinking[AI_MAX_MESSAGES] = {0};
    thinking[2] = "secret reasoning";
    char buf[4096];
    /* show_thinking=0 — thinking should NOT appear */
    size_t n = ai_build_save_text(&conv, thinking, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "--- Thinking ---") == NULL);
    ASSERT_TRUE(strstr(buf, "secret reasoning") == NULL);
    ASSERT_TRUE(strstr(buf, "answer") != NULL);
    TEST_END();
}

int test_save_text_skips_system(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "SECRET_SYSTEM_PROMPT");
    ai_conv_add(&conv, AI_ROLE_USER, "visible");
    char buf[4096];
    size_t n = ai_build_save_text(&conv, NULL, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* System prompt should not appear in save output */
    ASSERT_TRUE(strstr(buf, "SECRET_SYSTEM_PROMPT") == NULL);
    ASSERT_TRUE(strstr(buf, "visible") != NULL);
    TEST_END();
}

int test_save_text_null_safety(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_build_save_text(NULL, NULL, 0, NULL, 0), 0);
    char buf[64];
    ASSERT_EQ(ai_build_save_text(NULL, NULL, 0, buf, sizeof(buf)), 0);
    TEST_END();
}

int test_save_text_multi_exchange(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ai_conv_init(&conv, "test");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&conv, AI_ROLE_USER, "first question");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "first answer");
    ai_conv_add(&conv, AI_ROLE_USER, "second question");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "second answer");
    char buf[4096];
    size_t n = ai_build_save_text(&conv, NULL, 0, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* Both exchanges present */
    ASSERT_TRUE(strstr(buf, "first question") != NULL);
    ASSERT_TRUE(strstr(buf, "first answer") != NULL);
    ASSERT_TRUE(strstr(buf, "second question") != NULL);
    ASSERT_TRUE(strstr(buf, "second answer") != NULL);
    /* "You" appears twice, "AI" appears twice */
    char *p = strstr(buf, "--- You ---");
    ASSERT_NOT_NULL(p);
    p = strstr(p + 1, "--- You ---");
    ASSERT_NOT_NULL(p);
    TEST_END();
}

/* ---- Session switch / AiSessionState isolation tests ---- */

int test_session_state_init_not_valid(void)
{
    TEST_BEGIN();
    /* A zeroed AiSessionState should not be valid */
    AiSessionState state;
    memset(&state, 0, sizeof(state));
    ASSERT_EQ(state.valid, 0);
    TEST_END();
}

int test_session_state_save_restore(void)
{
    TEST_BEGIN();
    /* Simulate save: copy conv to state, mark valid */
    AiSessionState state;
    memset(&state, 0, sizeof(state));

    AiConversation conv;
    ai_conv_init(&conv, "test-model");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&conv, AI_ROLE_USER, "hello");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "hi back");

    memcpy(&state.conv, &conv, sizeof(AiConversation));
    state.valid = 1;

    /* Simulate restore: copy state.conv to a fresh conv */
    AiConversation restored;
    memcpy(&restored, &state.conv, sizeof(AiConversation));
    ASSERT_EQ(restored.msg_count, 3);
    ASSERT_STR_EQ(restored.model, "test-model");
    ASSERT_STR_EQ(restored.messages[1].content, "hello");
    ASSERT_STR_EQ(restored.messages[2].content, "hi back");
    TEST_END();
}

int test_session_state_two_sessions_isolated(void)
{
    TEST_BEGIN();
    /* Two session states must be completely independent */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    ai_conv_init(&state_a.conv, "model-a");
    ai_conv_add(&state_a.conv, AI_ROLE_SYSTEM, "sys-a");
    ai_conv_add(&state_a.conv, AI_ROLE_USER, "question-a");
    state_a.valid = 1;

    ai_conv_init(&state_b.conv, "model-b");
    ai_conv_add(&state_b.conv, AI_ROLE_SYSTEM, "sys-b");
    ai_conv_add(&state_b.conv, AI_ROLE_USER, "question-b");
    state_b.valid = 1;

    /* Adding to one doesn't affect the other */
    ai_conv_add(&state_a.conv, AI_ROLE_ASSISTANT, "answer-a");
    ASSERT_EQ(state_a.conv.msg_count, 3);
    ASSERT_EQ(state_b.conv.msg_count, 2);
    ASSERT_STR_EQ(state_a.conv.messages[2].content, "answer-a");
    TEST_END();
}

int test_session_state_switch_preserves_old(void)
{
    TEST_BEGIN();
    /* Simulates the session switch flow:
     * 1. Working conv has session A's data
     * 2. Save working conv to state A
     * 3. Load state B into working conv
     * 4. State A must still have its original data intact */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    /* Set up state B with existing conversation */
    ai_conv_init(&state_b.conv, "model-b");
    ai_conv_add(&state_b.conv, AI_ROLE_SYSTEM, "sys-b");
    ai_conv_add(&state_b.conv, AI_ROLE_USER, "q-b");
    ai_conv_add(&state_b.conv, AI_ROLE_ASSISTANT, "a-b");
    state_b.valid = 1;

    /* Working conv starts as session A */
    AiConversation working;
    ai_conv_init(&working, "model-a");
    ai_conv_add(&working, AI_ROLE_SYSTEM, "sys-a");
    ai_conv_add(&working, AI_ROLE_USER, "q-a");

    /* Step 2: save working to state A */
    memcpy(&state_a.conv, &working, sizeof(AiConversation));
    state_a.valid = 1;

    /* Step 3: load state B */
    memcpy(&working, &state_b.conv, sizeof(AiConversation));

    /* Step 4: verify state A is untouched */
    ASSERT_EQ(state_a.conv.msg_count, 2);
    ASSERT_STR_EQ(state_a.conv.messages[1].content, "q-a");

    /* And working now has B's data */
    ASSERT_EQ(working.msg_count, 3);
    ASSERT_STR_EQ(working.messages[2].content, "a-b");
    TEST_END();
}

int test_session_state_busy_commit_to_original(void)
{
    TEST_BEGIN();
    /* Simulates the mid-stream switch scenario:
     * 1. Thread starts on session A (busy_state = &state_a)
     * 2. User switches to session B (active_state = &state_b)
     * 3. Thread finishes: writes to busy_state->conv, not working conv
     * 4. Verify state A got the response, state B/working didn't */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    /* Session A has a pending question */
    ai_conv_init(&state_a.conv, "model-a");
    ai_conv_add(&state_a.conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&state_a.conv, AI_ROLE_USER, "q-a");
    state_a.valid = 1;

    /* Session B is idle */
    ai_conv_init(&state_b.conv, "model-b");
    ai_conv_add(&state_b.conv, AI_ROLE_SYSTEM, "sys");
    state_b.valid = 1;

    /* Simulate: busy_state = &state_a, user switches, working = state_b */
    AiSessionState *busy_state = &state_a;
    AiConversation working;
    memcpy(&working, &state_b.conv, sizeof(AiConversation));

    /* Thread finishes: active != busy, so commit to busy_state */
    ai_conv_add(&busy_state->conv, AI_ROLE_ASSISTANT, "response-for-a");

    /* Verify: state A got the response */
    ASSERT_EQ(state_a.conv.msg_count, 3);
    ASSERT_STR_EQ(state_a.conv.messages[2].content, "response-for-a");

    /* State B and working are untouched */
    ASSERT_EQ(state_b.conv.msg_count, 1);
    ASSERT_EQ(working.msg_count, 1);
    TEST_END();
}

int test_session_state_switch_back_has_response(void)
{
    TEST_BEGIN();
    /* After the thread commits to busy_state, switching back should
     * load the conversation with the response included */
    AiSessionState state_a;
    memset(&state_a, 0, sizeof(state_a));

    ai_conv_init(&state_a.conv, "model");
    ai_conv_add(&state_a.conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&state_a.conv, AI_ROLE_USER, "question");
    ai_conv_add(&state_a.conv, AI_ROLE_ASSISTANT, "answer");
    state_a.valid = 1;

    /* Simulate switch back: load state_a into working */
    AiConversation working;
    memcpy(&working, &state_a.conv, sizeof(AiConversation));

    ASSERT_EQ(working.msg_count, 3);
    ASSERT_STR_EQ(working.messages[2].content, "answer");
    TEST_END();
}

int test_session_state_reset_clears_valid(void)
{
    TEST_BEGIN();
    /* After ai_conv_reset, the conversation is empty but the
     * valid flag must be managed separately (caller sets it) */
    AiSessionState state;
    memset(&state, 0, sizeof(state));

    ai_conv_init(&state.conv, "model");
    ai_conv_add(&state.conv, AI_ROLE_USER, "test");
    state.valid = 1;

    ai_conv_reset(&state.conv);
    ASSERT_EQ(state.conv.msg_count, 0);
    /* valid stays 1 — caller must manage it */
    ASSERT_EQ(state.valid, 1);
    /* Model preserved after reset */
    ASSERT_STR_EQ(state.conv.model, "model");
    TEST_END();
}

/* ---- New Chat isolation tests ---- */

int test_new_chat_only_resets_active_session(void)
{
    TEST_BEGIN();
    /* Simulate: two sessions, "New Chat" on session A.
     * Session B must be completely unaffected. */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    /* Both sessions have conversations */
    ai_conv_init(&state_a.conv, "model");
    ai_conv_add(&state_a.conv, AI_ROLE_SYSTEM, "sys-a");
    ai_conv_add(&state_a.conv, AI_ROLE_USER, "q-a");
    ai_conv_add(&state_a.conv, AI_ROLE_ASSISTANT, "a-a");
    state_a.valid = 1;

    ai_conv_init(&state_b.conv, "model");
    ai_conv_add(&state_b.conv, AI_ROLE_SYSTEM, "sys-b");
    ai_conv_add(&state_b.conv, AI_ROLE_USER, "q-b");
    ai_conv_add(&state_b.conv, AI_ROLE_ASSISTANT, "a-b");
    state_b.valid = 1;

    /* Working conv mirrors session A (the active one) */
    AiConversation working;
    memcpy(&working, &state_a.conv, sizeof(AiConversation));

    /* New Chat: reset working + active_state (A) */
    ai_conv_reset(&working);
    ai_conv_reset(&state_a.conv);
    state_a.valid = 1;

    /* Verify session A is cleared */
    ASSERT_EQ(working.msg_count, 0);
    ASSERT_EQ(state_a.conv.msg_count, 0);

    /* Verify session B is completely untouched */
    ASSERT_EQ(state_b.conv.msg_count, 3);
    ASSERT_STR_EQ(state_b.conv.messages[0].content, "sys-b");
    ASSERT_STR_EQ(state_b.conv.messages[1].content, "q-b");
    ASSERT_STR_EQ(state_b.conv.messages[2].content, "a-b");
    ASSERT_EQ(state_b.valid, 1);
    TEST_END();
}

int test_new_chat_then_switch_preserves_other(void)
{
    TEST_BEGIN();
    /* New Chat on A, then switch to B: B should load fully intact */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    ai_conv_init(&state_a.conv, "model");
    ai_conv_add(&state_a.conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&state_a.conv, AI_ROLE_USER, "q-a");
    state_a.valid = 1;

    ai_conv_init(&state_b.conv, "model");
    ai_conv_add(&state_b.conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&state_b.conv, AI_ROLE_USER, "q-b");
    ai_conv_add(&state_b.conv, AI_ROLE_ASSISTANT, "a-b");
    state_b.valid = 1;

    /* Working = A initially */
    AiConversation working;
    memcpy(&working, &state_a.conv, sizeof(AiConversation));

    /* New Chat on A */
    ai_conv_reset(&working);
    ai_conv_reset(&state_a.conv);
    state_a.valid = 1;

    /* Switch to B: save working to state_a (already empty), load state_b */
    memcpy(&state_a.conv, &working, sizeof(AiConversation));
    memcpy(&working, &state_b.conv, sizeof(AiConversation));

    /* B is loaded correctly */
    ASSERT_EQ(working.msg_count, 3);
    ASSERT_STR_EQ(working.messages[1].content, "q-b");
    ASSERT_STR_EQ(working.messages[2].content, "a-b");

    /* A stays empty */
    ASSERT_EQ(state_a.conv.msg_count, 0);
    TEST_END();
}

int test_new_chat_then_switch_back_stays_empty(void)
{
    TEST_BEGIN();
    /* New Chat on A, switch to B, switch back to A: A should still be empty */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    ai_conv_init(&state_a.conv, "model");
    ai_conv_add(&state_a.conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&state_a.conv, AI_ROLE_USER, "q-a");
    state_a.valid = 1;

    ai_conv_init(&state_b.conv, "model");
    ai_conv_add(&state_b.conv, AI_ROLE_SYSTEM, "sys");
    state_b.valid = 1;

    AiConversation working;
    memcpy(&working, &state_a.conv, sizeof(AiConversation));

    /* New Chat on A */
    ai_conv_reset(&working);
    ai_conv_reset(&state_a.conv);
    state_a.valid = 1;

    /* Switch to B */
    memcpy(&state_a.conv, &working, sizeof(AiConversation));
    memcpy(&working, &state_b.conv, sizeof(AiConversation));

    /* Switch back to A */
    memcpy(&state_b.conv, &working, sizeof(AiConversation));
    memcpy(&working, &state_a.conv, sizeof(AiConversation));

    /* A is still empty */
    ASSERT_EQ(working.msg_count, 0);
    ASSERT_STR_EQ(working.model, "model");
    TEST_END();
}

int test_new_chat_preserves_model(void)
{
    TEST_BEGIN();
    /* New Chat should preserve the model name */
    AiSessionState state;
    memset(&state, 0, sizeof(state));

    ai_conv_init(&state.conv, "deepseek-reasoner");
    ai_conv_add(&state.conv, AI_ROLE_SYSTEM, "sys");
    ai_conv_add(&state.conv, AI_ROLE_USER, "question");
    state.valid = 1;

    AiConversation working;
    memcpy(&working, &state.conv, sizeof(AiConversation));

    ai_conv_reset(&working);
    ai_conv_reset(&state.conv);

    ASSERT_STR_EQ(working.model, "deepseek-reasoner");
    ASSERT_STR_EQ(state.conv.model, "deepseek-reasoner");
    ASSERT_EQ(working.msg_count, 0);
    TEST_END();
}

int test_new_chat_multiple_sessions_all_isolated(void)
{
    TEST_BEGIN();
    /* Three sessions: New Chat on the middle one.
     * First and third must be completely untouched. */
    AiSessionState states[3];
    memset(states, 0, sizeof(states));

    for (int i = 0; i < 3; i++) {
        char model[32];
        snprintf(model, sizeof(model), "model-%d", i);
        ai_conv_init(&states[i].conv, model);
        ai_conv_add(&states[i].conv, AI_ROLE_SYSTEM, "sys");

        char msg[32];
        snprintf(msg, sizeof(msg), "question-%d", i);
        ai_conv_add(&states[i].conv, AI_ROLE_USER, msg);

        snprintf(msg, sizeof(msg), "answer-%d", i);
        ai_conv_add(&states[i].conv, AI_ROLE_ASSISTANT, msg);
        states[i].valid = 1;
    }

    /* New Chat on session 1 (the middle one) */
    ai_conv_reset(&states[1].conv);
    states[1].valid = 1;

    /* Session 0: untouched */
    ASSERT_EQ(states[0].conv.msg_count, 3);
    ASSERT_STR_EQ(states[0].conv.messages[1].content, "question-0");
    ASSERT_STR_EQ(states[0].conv.messages[2].content, "answer-0");

    /* Session 1: cleared */
    ASSERT_EQ(states[1].conv.msg_count, 0);
    ASSERT_STR_EQ(states[1].conv.model, "model-1");

    /* Session 2: untouched */
    ASSERT_EQ(states[2].conv.msg_count, 3);
    ASSERT_STR_EQ(states[2].conv.messages[1].content, "question-2");
    ASSERT_STR_EQ(states[2].conv.messages[2].content, "answer-2");
    TEST_END();
}

/* --- Inline Command Approval tests --- */

int test_inline_approval_confirm_text_has_newlines(void)
{
    TEST_BEGIN();
    /* Confirm text must contain newlines for inline display in RichEdit */
    char cmds[2][1024] = {"ls -la ~", "df -h"};
    char buf[512];
    size_t n = ai_build_confirm_text(cmds, 2, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    /* Each command on its own line */
    ASSERT_TRUE(strstr(buf, "\n  1. ls -la ~\n") != NULL);
    ASSERT_TRUE(strstr(buf, "\n  2. df -h\n") != NULL);
    /* Ends with Allow? for inline prompt */
    char *last_line = strrchr(buf, '\n');
    ASSERT_TRUE(last_line != NULL);
    ASSERT_TRUE(strstr(last_line, "Allow?") != NULL);
    TEST_END();
}

int test_inline_approval_confirm_text_no_modal_title(void)
{
    TEST_BEGIN();
    /* The confirm text must NOT contain "Execute Commands" title —
     * that was the old modal dialog title. The text should be
     * self-describing with "wants to execute" phrasing. */
    char cmds[1][1024] = {"uptime"};
    char buf[512];
    size_t n = ai_build_confirm_text(cmds, 1, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "wants to execute") != NULL);
    /* Should NOT contain the old dialog title string */
    ASSERT_TRUE(strstr(buf, "Execute Commands") == NULL);
    TEST_END();
}

int test_inline_approval_queued_cmds_preserved(void)
{
    TEST_BEGIN();
    /* Simulate the approval flow: commands are extracted and queued.
     * Verify they survive being copied into the queue array. */
    char cmds[3][1024] = {"ls -la", "ps aux", "free -m"};
    char queued[16][1024];
    int ncmds = 3;
    memcpy(queued, cmds, (size_t)ncmds * sizeof(cmds[0]));

    /* All commands must survive the copy */
    ASSERT_STR_EQ(queued[0], "ls -la");
    ASSERT_STR_EQ(queued[1], "ps aux");
    ASSERT_STR_EQ(queued[2], "free -m");
    TEST_END();
}

int test_inline_approval_deny_clears_queue(void)
{
    TEST_BEGIN();
    /* Simulate deny: queued_count and queued_next reset to 0 */
    char queued[16][1024];
    int queued_count = 3;
    int queued_next = 0;
    int pending_approval = 1;

    snprintf(queued[0], sizeof(queued[0]), "rm -rf /");
    snprintf(queued[1], sizeof(queued[1]), "shutdown");
    snprintf(queued[2], sizeof(queued[2]), "reboot");

    /* User clicks Deny */
    pending_approval = 0;
    queued_count = 0;
    queued_next = 0;

    ASSERT_EQ(pending_approval, 0);
    ASSERT_EQ(queued_count, 0);
    ASSERT_EQ(queued_next, 0);
    TEST_END();
}

int test_inline_approval_allow_starts_execution(void)
{
    TEST_BEGIN();
    /* Simulate allow: pending clears, queued_next advances */
    int pending_approval = 1;
    int queued_count = 3;
    int queued_next = 0;

    /* User clicks Allow — first command executed */
    pending_approval = 0;
    queued_next = 1;

    ASSERT_EQ(pending_approval, 0);
    ASSERT_EQ(queued_next, 1);
    ASSERT_EQ(queued_count, 3);

    /* Remaining commands execute */
    for (int i = queued_next; i < queued_count; i++)
        queued_next = i + 1;
    ASSERT_EQ(queued_next, queued_count);
    TEST_END();
}

int test_inline_approval_blocks_send(void)
{
    TEST_BEGIN();
    /* When pending_approval is set, send_user_message should be blocked.
     * Simulate the guard condition. */
    int busy = 0;
    int pending_approval = 1;

    /* Guard: if (!d || d->busy || d->pending_approval) return; */
    int should_block = (busy || pending_approval);
    ASSERT_EQ(should_block, 1);

    /* After approval is resolved, sending should work */
    pending_approval = 0;
    should_block = (busy || pending_approval);
    ASSERT_EQ(should_block, 0);
    TEST_END();
}

int test_inline_approval_blocks_new_chat(void)
{
    TEST_BEGIN();
    /* New Chat should be blocked while pending approval */
    int busy = 0;
    int pending_approval = 1;

    /* Guard: if (d && !d->busy && !d->pending_approval) */
    int can_new_chat = (!busy && !pending_approval);
    ASSERT_EQ(can_new_chat, 0);

    /* After resolving approval */
    pending_approval = 0;
    can_new_chat = (!busy && !pending_approval);
    ASSERT_EQ(can_new_chat, 1);
    TEST_END();
}

int test_inline_approval_readonly_filter_then_approve(void)
{
    TEST_BEGIN();
    /* When permit_write is off, write commands are filtered out
     * before the approval prompt. Only read-only commands remain. */
    char cmds[4][1024] = {"ls -la", "rm file.txt", "cat log", "mkdir foo"};
    int ncmds = 4;

    /* Filter: keep only readonly */
    char filtered[16][1024];
    int nfiltered = 0;
    for (int i = 0; i < ncmds; i++) {
        if (ai_command_is_readonly(cmds[i])) {
            memcpy(filtered[nfiltered], cmds[i], sizeof(filtered[0]));
            nfiltered++;
        }
    }

    ASSERT_EQ(nfiltered, 2);
    ASSERT_STR_EQ(filtered[0], "ls -la");
    ASSERT_STR_EQ(filtered[1], "cat log");

    /* Confirm text should only show the 2 remaining commands */
    char buf[512];
    size_t n = ai_build_confirm_text(filtered, nfiltered, buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "2 commands") != NULL);
    ASSERT_TRUE(strstr(buf, "rm") == NULL);
    ASSERT_TRUE(strstr(buf, "mkdir") == NULL);
    TEST_END();
}

int test_inline_approval_switch_saves_to_session(void)
{
    TEST_BEGIN();
    /* Switching sessions saves pending approval to AiSessionState
     * via heap-allocated command array, then restores on switch back. */
    AiSessionState *state = calloc(1, sizeof(AiSessionState));
    ASSERT_TRUE(state != NULL);

    /* Simulate saving: pending approval with 2 commands */
    state->pending_approval = 1;
    state->pending_cmd_count = 2;
    state->pending_cmds = malloc(2 * 1024);
    ASSERT_TRUE(state->pending_cmds != NULL);
    snprintf(state->pending_cmds[0], 1024, "ls -la");
    snprintf(state->pending_cmds[1], 1024, "df -h");

    /* Verify state persists */
    ASSERT_EQ(state->pending_approval, 1);
    ASSERT_EQ(state->pending_cmd_count, 2);
    ASSERT_STR_EQ(state->pending_cmds[0], "ls -la");
    ASSERT_STR_EQ(state->pending_cmds[1], "df -h");

    /* Simulate restoring to local queue */
    char queued[16][1024];
    memcpy(queued, state->pending_cmds,
           (size_t)state->pending_cmd_count * sizeof(queued[0]));
    ASSERT_STR_EQ(queued[0], "ls -la");
    ASSERT_STR_EQ(queued[1], "df -h");

    /* Cleanup */
    free(state->pending_cmds);
    free(state);
    TEST_END();
}

int test_inline_approval_switch_clears_on_allow(void)
{
    TEST_BEGIN();
    /* After Allow, session state's pending fields are cleared */
    AiSessionState *state = calloc(1, sizeof(AiSessionState));
    ASSERT_TRUE(state != NULL);
    state->pending_approval = 1;
    state->pending_cmd_count = 1;
    state->pending_cmds = malloc(1024);
    ASSERT_TRUE(state->pending_cmds != NULL);
    snprintf(state->pending_cmds[0], 1024, "uptime");

    /* Simulate Allow click */
    state->pending_approval = 0;
    free(state->pending_cmds);
    state->pending_cmds = NULL;
    state->pending_cmd_count = 0;

    ASSERT_EQ(state->pending_approval, 0);
    ASSERT_EQ(state->pending_cmd_count, 0);
    ASSERT_TRUE(state->pending_cmds == NULL);
    free(state);
    TEST_END();
}

int test_inline_approval_deferred_extract_on_switch(void)
{
    TEST_BEGIN();
    /* When AI response arrives while user is on a different session,
     * commands must still be extracted and saved to the original
     * session's state for deferred approval. */
    AiSessionState *bs = calloc(1, sizeof(AiSessionState));
    ASSERT_TRUE(bs != NULL);

    /* AI response text with a command block */
    const char *response =
        "Here is the command:\n[EXEC]ping -c 3 server[/EXEC]\n";

    /* Extract commands (same as the switched-away WM_AI_RESPONSE path) */
    char cmds[16][1024];
    int ncmds = ai_extract_commands(response, cmds, 16);
    ASSERT_EQ(ncmds, 1);
    ASSERT_STR_EQ(cmds[0], "ping -c 3 server");

    /* Save to session state (simulates the deferred save) */
    if (ncmds > 0) {
        free(bs->pending_cmds);
        size_t sz = (size_t)ncmds * sizeof(cmds[0]);
        bs->pending_cmds = malloc(sz);
        ASSERT_TRUE(bs->pending_cmds != NULL);
        memcpy(bs->pending_cmds, cmds, sz);
        bs->pending_cmd_count = ncmds;
        bs->pending_approval = 1;
    }

    /* Verify the command survives in session state */
    ASSERT_EQ(bs->pending_approval, 1);
    ASSERT_EQ(bs->pending_cmd_count, 1);
    ASSERT_STR_EQ(bs->pending_cmds[0], "ping -c 3 server");

    free(bs->pending_cmds);
    free(bs);
    TEST_END();
}

int test_inline_approval_deferred_no_commands(void)
{
    TEST_BEGIN();
    /* If AI response has no commands, no approval should be saved */
    AiSessionState *bs = calloc(1, sizeof(AiSessionState));
    ASSERT_TRUE(bs != NULL);

    const char *response = "The server looks fine, no action needed.";
    char cmds[16][1024];
    int ncmds = ai_extract_commands(response, cmds, 16);
    ASSERT_EQ(ncmds, 0);

    /* No commands → no pending approval */
    ASSERT_EQ(bs->pending_approval, 0);
    ASSERT_TRUE(bs->pending_cmds == NULL);
    free(bs);
    TEST_END();
}

int test_inline_approval_two_sessions_independent(void)
{
    TEST_BEGIN();
    /* Two sessions with independent pending approval states.
     * Allowing one must not affect the other. */
    AiSessionState *sa = calloc(1, sizeof(AiSessionState));
    AiSessionState *sb = calloc(1, sizeof(AiSessionState));
    ASSERT_TRUE(sa != NULL);
    ASSERT_TRUE(sb != NULL);

    /* Session A: pending approval with "ls -la" */
    sa->pending_approval = 1;
    sa->pending_cmd_count = 1;
    sa->pending_cmds = malloc(1024);
    ASSERT_TRUE(sa->pending_cmds != NULL);
    snprintf(sa->pending_cmds[0], 1024, "ls -la");

    /* Session B: pending approval with "df -h" */
    sb->pending_approval = 1;
    sb->pending_cmd_count = 1;
    sb->pending_cmds = malloc(1024);
    ASSERT_TRUE(sb->pending_cmds != NULL);
    snprintf(sb->pending_cmds[0], 1024, "df -h");

    /* Allow on session B */
    sb->pending_approval = 0;
    free(sb->pending_cmds);
    sb->pending_cmds = NULL;
    sb->pending_cmd_count = 0;

    /* Session A must be completely unaffected */
    ASSERT_EQ(sa->pending_approval, 1);
    ASSERT_EQ(sa->pending_cmd_count, 1);
    ASSERT_STR_EQ(sa->pending_cmds[0], "ls -la");

    free(sa->pending_cmds);
    free(sa);
    free(sb);
    TEST_END();
}

int test_inline_approval_deny_one_keep_other(void)
{
    TEST_BEGIN();
    /* Denying on one session must not affect the other */
    AiSessionState *sa = calloc(1, sizeof(AiSessionState));
    AiSessionState *sb = calloc(1, sizeof(AiSessionState));
    ASSERT_TRUE(sa != NULL);
    ASSERT_TRUE(sb != NULL);

    sa->pending_approval = 1;
    sa->pending_cmd_count = 2;
    sa->pending_cmds = malloc(2 * 1024);
    ASSERT_TRUE(sa->pending_cmds != NULL);
    snprintf(sa->pending_cmds[0], 1024, "rm file");
    snprintf(sa->pending_cmds[1], 1024, "rmdir dir");

    sb->pending_approval = 1;
    sb->pending_cmd_count = 1;
    sb->pending_cmds = malloc(1024);
    ASSERT_TRUE(sb->pending_cmds != NULL);
    snprintf(sb->pending_cmds[0], 1024, "uptime");

    /* Deny on A */
    sa->pending_approval = 0;
    free(sa->pending_cmds);
    sa->pending_cmds = NULL;
    sa->pending_cmd_count = 0;

    /* B still pending */
    ASSERT_EQ(sb->pending_approval, 1);
    ASSERT_EQ(sb->pending_cmd_count, 1);
    ASSERT_STR_EQ(sb->pending_cmds[0], "uptime");

    free(sb->pending_cmds);
    free(sa);
    free(sb);
    TEST_END();
}

int test_inline_approval_save_restore_roundtrip(void)
{
    TEST_BEGIN();
    /* Full roundtrip: save working state → session, switch away,
     * switch back → restore from session, verify commands intact. */
    AiSessionState *state = calloc(1, sizeof(AiSessionState));
    ASSERT_TRUE(state != NULL);

    /* Working state: 3 pending commands */
    char working_cmds[16][1024];
    int working_count = 3;
    int working_approval = 1;
    snprintf(working_cmds[0], 1024, "ps aux");
    snprintf(working_cmds[1], 1024, "free -m");
    snprintf(working_cmds[2], 1024, "df -h");

    /* Step 1: save to session (simulates switching away) */
    state->pending_approval = working_approval;
    state->pending_cmd_count = 0;
    if (working_approval && working_count > 0) {
        size_t sz = (size_t)working_count * sizeof(working_cmds[0]);
        state->pending_cmds = malloc(sz);
        ASSERT_TRUE(state->pending_cmds != NULL);
        memcpy(state->pending_cmds, working_cmds, sz);
        state->pending_cmd_count = working_count;
    }

    /* Step 2: clear working state (simulates loading other session) */
    working_approval = 0;
    working_count = 0;
    memset(working_cmds, 0, sizeof(working_cmds));

    /* Step 3: restore from session (simulates switching back) */
    if (state->pending_approval && state->pending_cmds &&
        state->pending_cmd_count > 0) {
        int nc = state->pending_cmd_count;
        if (nc > 16) nc = 16;
        memcpy(working_cmds, state->pending_cmds,
               (size_t)nc * sizeof(working_cmds[0]));
        working_count = nc;
        working_approval = 1;
    }

    /* Verify full roundtrip */
    ASSERT_EQ(working_approval, 1);
    ASSERT_EQ(working_count, 3);
    ASSERT_STR_EQ(working_cmds[0], "ps aux");
    ASSERT_STR_EQ(working_cmds[1], "free -m");
    ASSERT_STR_EQ(working_cmds[2], "df -h");

    free(state->pending_cmds);
    free(state);
    TEST_END();
}

int test_inline_approval_deferred_multi_commands(void)
{
    TEST_BEGIN();
    /* Deferred extraction with multiple commands */
    AiSessionState *bs = calloc(1, sizeof(AiSessionState));
    ASSERT_TRUE(bs != NULL);

    const char *response =
        "Run these:\n"
        "[EXEC]ls -la ~[/EXEC]\n"
        "[EXEC]ss -tlnp[/EXEC]\n"
        "[EXEC]ps aux --sort=-%mem | head -20[/EXEC]\n";

    char cmds[16][1024];
    int ncmds = ai_extract_commands(response, cmds, 16);
    ASSERT_TRUE(ncmds >= 3);

    size_t sz = (size_t)ncmds * sizeof(cmds[0]);
    bs->pending_cmds = malloc(sz);
    ASSERT_TRUE(bs->pending_cmds != NULL);
    memcpy(bs->pending_cmds, cmds, sz);
    bs->pending_cmd_count = ncmds;
    bs->pending_approval = 1;

    /* All commands survive heap storage */
    ASSERT_STR_EQ(bs->pending_cmds[0], "ls -la ~");
    ASSERT_STR_EQ(bs->pending_cmds[1], "ss -tlnp");
    ASSERT_TRUE(strstr(bs->pending_cmds[2], "ps aux") != NULL);

    /* Confirm text can be rebuilt from stored commands */
    char confirm[4096];
    size_t clen = ai_build_confirm_text(
        bs->pending_cmds, bs->pending_cmd_count,
        confirm, sizeof(confirm));
    ASSERT_TRUE(clen > 0);
    ASSERT_TRUE(strstr(confirm, "ls -la ~") != NULL);
    ASSERT_TRUE(strstr(confirm, "ss -tlnp") != NULL);

    free(bs->pending_cmds);
    free(bs);
    TEST_END();
}

/* ---- Per-session busy/stream state tests (concurrent AI support) ---- */

int test_session_state_busy_init_zero(void)
{
    TEST_BEGIN();
    AiSessionState state;
    memset(&state, 0, sizeof(state));
    ASSERT_EQ(state.busy, 0);
    ASSERT_NULL(state.stream_content);
    ASSERT_NULL(state.stream_thinking);
    ASSERT_EQ((int)state.stream_content_len, 0);
    ASSERT_EQ((int)state.stream_thinking_len, 0);
    ASSERT_EQ(state.stream_phase, 0);
    TEST_END();
}

int test_session_state_busy_independent(void)
{
    TEST_BEGIN();
    /* Two sessions: one busy, one idle — must be independent */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    state_a.busy = 1;
    state_a.stream_content = (char *)calloc(1, AI_MSG_MAX);
    ASSERT_NOT_NULL(state_a.stream_content);
    memcpy(state_a.stream_content, "partial response A", 18);
    state_a.stream_content_len = 18;
    state_a.stream_content[18] = '\0';
    state_a.stream_phase = 2;

    /* B is idle — should be completely unaffected */
    ASSERT_EQ(state_b.busy, 0);
    ASSERT_NULL(state_b.stream_content);
    ASSERT_EQ((int)state_b.stream_content_len, 0);
    ASSERT_EQ(state_b.stream_phase, 0);

    /* A is still busy with its data */
    ASSERT_EQ(state_a.busy, 1);
    ASSERT_EQ((int)state_a.stream_content_len, 18);
    ASSERT_STR_EQ(state_a.stream_content, "partial response A");

    free(state_a.stream_content);
    TEST_END();
}

int test_session_state_concurrent_busy(void)
{
    TEST_BEGIN();
    /* Both sessions can be busy at the same time */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    ai_conv_init(&state_a.conv, "model-a");
    ai_conv_add(&state_a.conv, AI_ROLE_SYSTEM, "sys-a");
    ai_conv_add(&state_a.conv, AI_ROLE_USER, "question-a");
    state_a.valid = 1;
    state_a.busy = 1;

    ai_conv_init(&state_b.conv, "model-b");
    ai_conv_add(&state_b.conv, AI_ROLE_SYSTEM, "sys-b");
    ai_conv_add(&state_b.conv, AI_ROLE_USER, "question-b");
    state_b.valid = 1;
    state_b.busy = 1;

    /* Both busy */
    ASSERT_EQ(state_a.busy, 1);
    ASSERT_EQ(state_b.busy, 1);

    /* Simulate thread A completing: writes response and clears busy */
    ai_conv_add(&state_a.conv, AI_ROLE_ASSISTANT, "answer-a");
    state_a.busy = 0;

    /* A is done, B is still busy */
    ASSERT_EQ(state_a.busy, 0);
    ASSERT_EQ(state_b.busy, 1);
    ASSERT_EQ(state_a.conv.msg_count, 3);
    ASSERT_STR_EQ(state_a.conv.messages[2].content, "answer-a");

    /* B hasn't changed */
    ASSERT_EQ(state_b.conv.msg_count, 2);

    /* Simulate thread B completing */
    ai_conv_add(&state_b.conv, AI_ROLE_ASSISTANT, "answer-b");
    state_b.busy = 0;

    ASSERT_EQ(state_b.busy, 0);
    ASSERT_EQ(state_b.conv.msg_count, 3);
    ASSERT_STR_EQ(state_b.conv.messages[2].content, "answer-b");
    TEST_END();
}

int test_session_state_stream_buffer_alloc_free(void)
{
    TEST_BEGIN();
    /* Simulate stream start: allocate buffers */
    AiSessionState state;
    memset(&state, 0, sizeof(state));

    state.stream_content = (char *)calloc(1, AI_MSG_MAX);
    state.stream_thinking = (char *)calloc(1, AI_MSG_MAX);
    ASSERT_NOT_NULL(state.stream_content);
    ASSERT_NOT_NULL(state.stream_thinking);
    state.busy = 1;
    state.stream_phase = 0;

    /* Simulate accumulation */
    const char *chunk1 = "Hello ";
    const char *chunk2 = "World";
    size_t len1 = strlen(chunk1);
    size_t len2 = strlen(chunk2);
    memcpy(state.stream_content, chunk1, len1);
    state.stream_content_len = len1;
    memcpy(state.stream_content + state.stream_content_len, chunk2, len2);
    state.stream_content_len += len2;
    state.stream_content[state.stream_content_len] = '\0';
    state.stream_phase = 2;

    ASSERT_STR_EQ(state.stream_content, "Hello World");
    ASSERT_EQ((int)state.stream_content_len, 11);

    /* Simulate stream end: free buffers */
    free(state.stream_content);
    state.stream_content = NULL;
    state.stream_content_len = 0;
    free(state.stream_thinking);
    state.stream_thinking = NULL;
    state.stream_thinking_len = 0;
    state.stream_phase = 0;
    state.busy = 0;

    ASSERT_NULL(state.stream_content);
    ASSERT_NULL(state.stream_thinking);
    ASSERT_EQ(state.busy, 0);
    TEST_END();
}

int test_session_state_stream_thinking_accumulation(void)
{
    TEST_BEGIN();
    AiSessionState state;
    memset(&state, 0, sizeof(state));

    state.stream_thinking = (char *)calloc(1, AI_MSG_MAX);
    ASSERT_NOT_NULL(state.stream_thinking);
    state.busy = 1;

    /* Accumulate thinking chunks */
    const char *t1 = "Let me think...";
    const char *t2 = " Actually, ";
    const char *t3 = "the answer is 42.";
    size_t tlen = 0;

    memcpy(state.stream_thinking + tlen, t1, strlen(t1));
    tlen += strlen(t1);
    memcpy(state.stream_thinking + tlen, t2, strlen(t2));
    tlen += strlen(t2);
    memcpy(state.stream_thinking + tlen, t3, strlen(t3));
    tlen += strlen(t3);
    state.stream_thinking[tlen] = '\0';
    state.stream_thinking_len = tlen;
    state.stream_phase = 1;

    ASSERT_STR_EQ(state.stream_thinking,
                   "Let me think... Actually, the answer is 42.");
    ASSERT_EQ(state.stream_phase, 1);

    free(state.stream_thinking);
    TEST_END();
}

int test_session_state_cleanup_on_close(void)
{
    TEST_BEGIN();
    /* Simulate session close while busy: buffers must be freed */
    AiSessionState state;
    memset(&state, 0, sizeof(state));

    state.busy = 1;
    state.stream_content = (char *)calloc(1, AI_MSG_MAX);
    state.stream_thinking = (char *)calloc(1, AI_MSG_MAX);
    state.pending_cmds = (char (*)[1024])malloc(2 * 1024);
    state.pending_cmd_count = 2;
    state.pending_approval = 1;

    /* Simulate cleanup (mirrors ai_chat_notify_session_closed) */
    free(state.pending_cmds);
    state.pending_cmds = NULL;
    free(state.stream_content);
    state.stream_content = NULL;
    free(state.stream_thinking);
    state.stream_thinking = NULL;
    state.busy = 0;

    ASSERT_NULL(state.pending_cmds);
    ASSERT_NULL(state.stream_content);
    ASSERT_NULL(state.stream_thinking);
    ASSERT_EQ(state.busy, 0);
    TEST_END();
}

int test_session_state_switch_while_both_busy(void)
{
    TEST_BEGIN();
    /* Simulate: A is busy streaming, switch to B, start B streaming.
     * Both should have independent busy state and stream buffers. */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    /* A starts streaming */
    ai_conv_init(&state_a.conv, "model");
    ai_conv_add(&state_a.conv, AI_ROLE_USER, "question-a");
    state_a.valid = 1;
    state_a.busy = 1;
    state_a.stream_content = (char *)calloc(1, AI_MSG_MAX);
    memcpy(state_a.stream_content, "partial-a", 9);
    state_a.stream_content_len = 9;
    state_a.stream_content[9] = '\0';
    state_a.stream_phase = 2;

    /* Switch to B: A's data is preserved, B starts fresh */
    ai_conv_init(&state_b.conv, "model");
    ai_conv_add(&state_b.conv, AI_ROLE_USER, "question-b");
    state_b.valid = 1;
    state_b.busy = 1;
    state_b.stream_content = (char *)calloc(1, AI_MSG_MAX);
    memcpy(state_b.stream_content, "partial-b", 9);
    state_b.stream_content_len = 9;
    state_b.stream_content[9] = '\0';
    state_b.stream_phase = 2;

    /* Both independently streaming */
    ASSERT_EQ(state_a.busy, 1);
    ASSERT_EQ(state_b.busy, 1);
    ASSERT_STR_EQ(state_a.stream_content, "partial-a");
    ASSERT_STR_EQ(state_b.stream_content, "partial-b");

    /* A's thread finishes — only A is affected */
    ai_conv_add(&state_a.conv, AI_ROLE_ASSISTANT, "answer-a");
    state_a.busy = 0;
    free(state_a.stream_content);
    state_a.stream_content = NULL;
    state_a.stream_content_len = 0;
    state_a.stream_phase = 0;

    ASSERT_EQ(state_a.busy, 0);
    ASSERT_EQ(state_b.busy, 1);
    ASSERT_NULL(state_a.stream_content);
    ASSERT_STR_EQ(state_b.stream_content, "partial-b");

    /* B's thread finishes */
    ai_conv_add(&state_b.conv, AI_ROLE_ASSISTANT, "answer-b");
    state_b.busy = 0;
    free(state_b.stream_content);
    state_b.stream_content = NULL;

    ASSERT_EQ(state_b.busy, 0);
    ASSERT_STR_EQ(state_a.conv.messages[1].content, "answer-a");
    ASSERT_STR_EQ(state_b.conv.messages[1].content, "answer-b");
    TEST_END();
}

int test_session_state_switch_back_restores_stream(void)
{
    TEST_BEGIN();
    /* Switching back to a busy session should allow display of accumulated
     * stream content from the per-session buffers */
    AiSessionState state_a;
    memset(&state_a, 0, sizeof(state_a));

    ai_conv_init(&state_a.conv, "model");
    ai_conv_add(&state_a.conv, AI_ROLE_USER, "question");
    state_a.valid = 1;
    state_a.busy = 1;

    /* Simulate stream accumulation while user is on another tab */
    state_a.stream_content = (char *)calloc(1, AI_MSG_MAX);
    state_a.stream_thinking = (char *)calloc(1, AI_MSG_MAX);
    memcpy(state_a.stream_content, "accumulated content", 19);
    state_a.stream_content_len = 19;
    state_a.stream_content[19] = '\0';
    memcpy(state_a.stream_thinking, "some thinking", 13);
    state_a.stream_thinking_len = 13;
    state_a.stream_thinking[13] = '\0';
    state_a.stream_phase = 2;

    /* When switching back, the display can restore from session buffers */
    ASSERT_EQ(state_a.busy, 1);
    ASSERT_STR_EQ(state_a.stream_content, "accumulated content");
    ASSERT_STR_EQ(state_a.stream_thinking, "some thinking");
    ASSERT_EQ(state_a.stream_phase, 2);
    ASSERT_EQ((int)state_a.stream_content_len, 19);
    ASSERT_EQ((int)state_a.stream_thinking_len, 13);

    free(state_a.stream_content);
    free(state_a.stream_thinking);
    TEST_END();
}

int test_session_state_pending_cmds_per_session(void)
{
    TEST_BEGIN();
    /* When a non-active session finishes with commands,
     * they should be stored in that session's pending state */
    AiSessionState state_a, state_b;
    memset(&state_a, 0, sizeof(state_a));
    memset(&state_b, 0, sizeof(state_b));

    /* Session A gets commands while user views B */
    char cmds[2][1024];
    snprintf(cmds[0], sizeof(cmds[0]), "ls -la");
    snprintf(cmds[1], sizeof(cmds[1]), "df -h");

    state_a.pending_cmds = (char (*)[1024])malloc(2 * sizeof(cmds[0]));
    ASSERT_NOT_NULL(state_a.pending_cmds);
    memcpy(state_a.pending_cmds, cmds, 2 * sizeof(cmds[0]));
    state_a.pending_cmd_count = 2;
    state_a.pending_approval = 1;

    /* Session B has no pending commands */
    ASSERT_NULL(state_b.pending_cmds);
    ASSERT_EQ(state_b.pending_approval, 0);

    /* A has its commands ready */
    ASSERT_EQ(state_a.pending_approval, 1);
    ASSERT_EQ(state_a.pending_cmd_count, 2);
    ASSERT_STR_EQ(state_a.pending_cmds[0], "ls -la");
    ASSERT_STR_EQ(state_a.pending_cmds[1], "df -h");

    free(state_a.pending_cmds);
    TEST_END();
}
