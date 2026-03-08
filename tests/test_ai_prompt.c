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
    ai_build_system_prompt(buf, sizeof(buf), NULL);
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
    ai_build_system_prompt(buf, sizeof(buf), NULL);
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
    ai_build_system_prompt(buf, sizeof(buf), NULL);
    /* Must discourage "let's start with" partial plans */
    ASSERT_TRUE(strstr(buf, "Never say") != NULL);
    ASSERT_TRUE(strstr(buf, "first step") != NULL);
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
