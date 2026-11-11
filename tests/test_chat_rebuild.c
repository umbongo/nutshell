/* tests/test_chat_rebuild.c -- the rules of the rebuilt AI display
 * (src/core/chat_rebuild.c). Expected strings are literals. */
#include "test_framework.h"
#include "chat_rebuild.h"
#include "ai_agentic.h"
#include "ui_demo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The list as one string, a line per item: a letter for its kind and its text
 * (a command row's command): U user, A reply text, K command, S status,
 * C tool call, R tool result. */
static void dump(const ChatMsgList *list, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = '\0';
    for (const ChatMsgItem *it = list->head; it; it = it->next) {
        char k = '?';
        const char *text = it->text ? it->text : "";
        switch (it->type) {
        case CHAT_ITEM_USER:        k = 'U'; break;
        case CHAT_ITEM_AI_TEXT:     k = 'A'; break;
        case CHAT_ITEM_COMMAND:     k = 'K'; text = it->u.cmd.command; break;
        case CHAT_ITEM_STATUS:      k = 'S'; break;
        case CHAT_ITEM_TOOL_CALL:   k = 'C'; break;
        case CHAT_ITEM_TOOL_RESULT: k = 'R'; break;
        }
        int w = snprintf(out + n, cap - n, "%c:%s\n", k, text ? text : "");
        if (w < 0 || (size_t)w >= cap - n) return;
        n += (size_t)w;
    }
}

/* ---- chat_rebuild_call_text ---- */

int test_rebuild_call_text_variants(void) {
    TEST_BEGIN();
    AiToolCall c;
    char out[256];
    memset(&c, 0, sizeof(c));

    snprintf(c.name, sizeof(c.name), "web_search");
    snprintf(c.input_json, sizeof(c.input_json), "{\"query\":\"x\"}");
    ASSERT_TRUE(chat_rebuild_call_text(&c, out, sizeof(out)) > 0);
    ASSERT_STR_EQ(out, "using tool: web_search");

    snprintf(c.name, sizeof(c.name), "web_fetch");
    snprintf(c.input_json, sizeof(c.input_json),
             "{\"url\":\"https://example.com/a\"}");
    chat_rebuild_call_text(&c, out, sizeof(out));
    ASSERT_STR_EQ(out, "using tool: web_fetch - https://example.com/a");

    snprintf(c.name, sizeof(c.name), "run_command");
    snprintf(c.input_json, sizeof(c.input_json),
             "{\"command\":\"ls -la /tmp\",\"note\":\"look\"}");
    chat_rebuild_call_text(&c, out, sizeof(out));
    ASSERT_STR_EQ(out, "using tool: run_command - ls -la /tmp");

    /* No usable command: the plain row. */
    snprintf(c.input_json, sizeof(c.input_json), "{}");
    chat_rebuild_call_text(&c, out, sizeof(out));
    ASSERT_STR_EQ(out, "using tool: run_command");

    /* A command that does not fit is cut on a UTF-8 boundary. */
    snprintf(c.input_json, sizeof(c.input_json),
             "{\"command\":\"echo \xC3\xA9\xC3\xA9\xC3\xA9\"}");
    char small[22];                /* "using tool: run_command - " is 26 */
    ASSERT_EQ((int)chat_rebuild_call_text(&c, small, sizeof(small)), 21);
    ASSERT_STR_EQ(small, "using tool: run_comma");
    char mid[34];                  /* 26 + "echo " + 2 bytes of the first e-acute */
    chat_rebuild_call_text(&c, mid, sizeof(mid));
    ASSERT_STR_EQ(mid, "using tool: run_command - echo \xC3\xA9");

    /* NULL arguments. */
    ASSERT_EQ((int)chat_rebuild_call_text(NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "");
    ASSERT_EQ((int)chat_rebuild_call_text(&c, NULL, 10), 0);
    ASSERT_EQ((int)chat_rebuild_call_text(&c, out, 0), 0);
    TEST_END();
}

int test_rebuild_is_cut(void) {
    TEST_BEGIN();
    ASSERT_EQ(chat_rebuild_is_cut("", 0), 0);
    ASSERT_EQ(chat_rebuild_is_cut(NULL, 0), 0);
    ASSERT_EQ(chat_rebuild_is_cut("ran", 0), 0);
    ASSERT_EQ(chat_rebuild_is_cut("ran", 5), 1);
    ASSERT_EQ(chat_rebuild_is_cut("[truncated by nutshell at 65536 bytes]", 0), 1);
    ASSERT_EQ(chat_rebuild_is_cut("fetched\n[truncated by nutshell at 65536 bytes]", 0), 1);
    ASSERT_EQ(chat_rebuild_is_cut("[truncated by nutshell at 5 bytes]", 0), 0);
    TEST_END();
}

/* ---- chat_rebuild_part_rows ---- */

int test_rebuild_part_other_tool_note_and_preview(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    char out[1024];
    chat_rebuild_part_rows(&list, "web_fetch", "fetched 2 pages", "hello", 5, 0, 0);
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "S:fetched 2 pages\nR:hello\n");
    chat_msg_list_clear(&list);

    /* No note: only the preview. */
    chat_rebuild_part_rows(&list, "web_search", "", "results", 7, 0, 0);
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "R:results\n");
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_rebuild_part_long_content_is_not_a_cut(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    char content[301];
    memset(content, 'x', 300);
    content[300] = '\0';
    chat_rebuild_part_rows(&list, "web_fetch", "", content, 300, 0, 0);
    ASSERT_EQ(list.count, 1);              /* the preview only: no cut row */
    ASSERT_EQ(list.head->type, CHAT_ITEM_TOOL_RESULT);
    ASSERT_EQ((int)strlen(list.head->text), 200);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_rebuild_part_cut_rows(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    char out[1024];

    /* A recorded count says how much. */
    chat_rebuild_part_rows(&list, "web_fetch", "", "abc", 3, 1234, 0);
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "S:[web_fetch output cut: 1234 bytes omitted]\nR:abc\n");
    chat_msg_list_clear(&list);

    /* No count, but the note holds the cap's line: it names the cap. The
     * note's own newline reads as a space in its row. */
    chat_rebuild_part_rows(&list, "web_fetch",
                           "fetched\n[truncated by nutshell at 65536 bytes]",
                           "abc", 3, 0, 0);
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out,
        "S:fetched [truncated by nutshell at 65536 bytes]\n"
        "S:[web_fetch output truncated at 64 KiB]\n"
        "R:abc\n");
    chat_msg_list_clear(&list);

    /* No count and no cap line: no cut row, whatever the note says. */
    chat_rebuild_part_rows(&list, "web_fetch", "cut short by the site", "abc",
                           3, 0, 0);
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "S:cut short by the site\nR:abc\n");
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_rebuild_part_preview_cut_on_utf8_boundary(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    char content[260];
    memset(content, 'a', 199);
    memcpy(content + 199, "\xC3\xA9\xC3\xA9", 4);   /* bytes 199-202 */
    content[203] = '\0';
    chat_rebuild_part_rows(&list, "web_fetch", "", content, 203, 0, 0);
    ASSERT_EQ(list.count, 1);
    ASSERT_EQ((int)strlen(list.head->text), 199);    /* never half an e-acute */
    ASSERT_EQ((unsigned char)list.head->text[198], 'a');
    chat_msg_list_clear(&list);

    /* Exactly 200 bytes of ASCII: kept whole. */
    memset(content, 'b', 200);
    content[200] = '\0';
    chat_rebuild_part_rows(&list, "web_fetch", "", content, 200, 0, 0);
    ASSERT_EQ((int)strlen(list.head->text), 200);
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_rebuild_part_run_command_has_no_preview(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    char out[1024];
    chat_rebuild_part_rows(&list, "run_command",
                           "ran; output cut: 40 bytes omitted from the middle",
                           "head tail", 9, 40, 0);
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out,
        "S:ran; output cut: 40 bytes omitted from the middle\n"
        "S:[run_command output cut: 40 bytes omitted]\n");
    chat_msg_list_clear(&list);

    chat_rebuild_part_rows(&list, "run_command", "ran", "out", 3, 0, 0);
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "S:ran\n");
    chat_msg_list_clear(&list);
    TEST_END();
}

int test_rebuild_part_empty_covered_and_null(void) {
    TEST_BEGIN();
    ChatMsgList list;
    chat_msg_list_init(&list);
    /* Nothing to say: no rows (the old rebuild drew an empty one). */
    chat_rebuild_part_rows(&list, "web_search", "", NULL, 0, 0, 0);
    chat_rebuild_part_rows(&list, "web_search", NULL, "", 0, 0, 0);
    ASSERT_EQ(list.count, 0);
    /* A covered run_command result: the card says it all. */
    chat_rebuild_part_rows(&list, "run_command", "ran", "out", 3, 9, 1);
    ASSERT_EQ(list.count, 0);
    /* NULL list, NULL name. */
    chat_rebuild_part_rows(NULL, "web_fetch", "n", "c", 1, 0, 0);
    chat_rebuild_part_rows(&list, NULL, "n", "c", 1, 0, 0);
    ASSERT_EQ(list.count, 2);
    chat_msg_list_clear(&list);
    TEST_END();
}

/* ---- chat_rebuild_replay ---- */

static void conv_start(AiConversation *conv)
{
    ai_conv_init(conv, "m");
    ai_conv_add(conv, AI_ROLE_SYSTEM, "SYSTEM PROMPT");
}

static void set_call(AiToolCall *c, const char *id, const char *name,
                     const char *input)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->id, sizeof(c->id), "%s", id);
    snprintf(c->name, sizeof(c->name), "%s", name);
    snprintf(c->input_json, sizeof(c->input_json), "%s", input);
}

static void set_result(AiToolResult *r, const char *id, const char *note,
                       const char *content, size_t omitted)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->tool_use_id, sizeof(r->tool_use_id), "%s", id);
    snprintf(r->tool_note, sizeof(r->tool_note), "%s", note);
    if (content) {
        r->content = strdup(content);
        r->content_len = strlen(content);
    }
    r->omitted_bytes = omitted;
}

static void free_results(AiToolResult *r, int n)
{
    for (int i = 0; i < n; i++) free(r[i].content);
}

/* Add an assistant tool turn and its results turn; returns the turn's index. */
static int add_tool_round(AiConversation *conv, const char *text,
                          const AiToolCall *calls, AiToolResult *res, int n)
{
    agentic_add_assistant_tool_msg(conv, text, calls, n);
    int idx = conv->msg_count - 1;
    if (res) agentic_add_tool_results(conv, conv->messages[idx].tool_calls, res, n);
    return idx;
}

int test_rebuild_replay_plain_turns(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "hello");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "hi there");
    ai_conv_add(&conv, AI_ROLE_USER, "bye");
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "U:hello\nA:hi there\nU:bye\n");
    chat_msg_list_clear(&list);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_thinking_and_empty_reply(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "q");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "answer");
    char *thinking[AI_MAX_MESSAGES];
    memset(thinking, 0, sizeof(thinking));
    char why[] = "because";
    thinking[2] = why;
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, thinking, NULL, NULL);
    ASSERT_EQ(list.count, 2);
    ASSERT_EQ(list.tail->type, CHAT_ITEM_AI_TEXT);
    ASSERT_STR_EQ(list.tail->u.ai.thinking_text, "because");
    ASSERT_EQ(list.tail->u.ai.thinking_complete, 1);
    chat_msg_list_clear(&list);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_web_rounds_interleave_call_and_result(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "look it up");
    AiToolCall calls[2];
    set_call(&calls[0], "t1", "web_search", "{\"query\":\"q\"}");
    set_call(&calls[1], "t2", "web_fetch", "{\"url\":\"https://example.com/a\"}");
    AiToolResult res[2];
    set_result(&res[0], "t1", "", "three hits", 0);
    set_result(&res[1], "t2", "fetched", "page text", 0);
    add_tool_round(&conv, "", calls, res, 2);
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "done");
    free_results(res, 2);

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, NULL);
    char out[1024];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out,
        "U:look it up\n"
        "C:using tool: web_search\n"
        "R:three hits\n"
        "C:using tool: web_fetch - https://example.com/a\n"
        "S:fetched\n"
        "R:page text\n"
        "A:done\n");
    chat_msg_list_clear(&list);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_results_matched_by_id_not_position(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "q");
    AiToolCall calls[2];
    set_call(&calls[0], "t1", "web_search", "{}");
    set_call(&calls[1], "t2", "web_fetch", "{}");
    AiToolResult res[2];
    set_result(&res[0], "t2", "", "second", 0);    /* answers arrive reordered */
    set_result(&res[1], "t1", "", "first", 0);
    add_tool_round(&conv, "", calls, res, 2);
    free_results(res, 2);

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out,
        "U:q\n"
        "C:using tool: web_search\nR:first\n"
        "C:using tool: web_fetch\nR:second\n");
    chat_msg_list_clear(&list);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_runcmd_without_record_shows_row_and_note(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "list files");
    AiToolCall call;
    set_call(&call, "r1", "run_command", "{\"command\":\"ls -la\"}");
    AiToolResult res;
    set_result(&res, "r1", "ran", "total 0\nfile", 0);
    add_tool_round(&conv, "", &call, &res, 1);
    free_results(&res, 1);

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    /* The row, its note, and no preview of the output. */
    ASSERT_STR_EQ(out, "U:list files\nC:using tool: run_command - ls -la\nS:ran\n");
    chat_msg_list_clear(&list);
    ai_conv_reset(&conv);
    TEST_END();
}

/* A record of one run_command reply's batch, keyed to message `idx`. */
static void record_batch(CmdRunLog *log, const AiConversation *conv, int idx,
                         const char *cmd, int status)
{
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);
    chat_approval_add(&b->q, cmd, CMD_PLATFORM_LINUX);
    b->q.entries[0].status = (ApprovalStatus)status;
    b->reply_msg = idx;
    const AiMessage *m = &conv->messages[idx];
    cmd_run_log_record(log, b,
        cmd_run_log_reply_hash(ai_msg_content(m), m->content_len,
                               m->tool_calls, m->n_tool_calls), 0);
    cmd_batch_set_free(&set);
}

int test_rebuild_replay_runcmd_with_record_is_shown_once(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "list files");
    AiToolCall call;
    set_call(&call, "r1", "run_command", "{\"command\":\"ls -la\"}");
    AiToolResult res;
    set_result(&res, "r1", "ran", "total 0", 0);
    int idx = add_tool_round(&conv, "", &call, &res, 1);
    free_results(&res, 1);
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "there they are");

    CmdRunLog log;
    cmd_run_log_init(&log);
    record_batch(&log, &conv, idx, "ls -la", APPROVE_COMPLETED);
    ASSERT_EQ(log.count, 1);

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, &log, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    /* Only the replayed command row: no "using tool" row, no note, no
     * preview. */
    ASSERT_STR_EQ(out, "U:list files\nK:ls -la\nA:there they are\n");
    ASSERT_EQ(list.head->next->u.cmd.settled, 1);
    ASSERT_EQ(list.head->next->u.cmd.batch, -1);
    chat_msg_list_clear(&list);
    cmd_run_log_free(&log);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_stale_record_does_not_hide_the_call(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "list files");
    AiToolCall call;
    set_call(&call, "r1", "run_command", "{\"command\":\"ls -la\"}");
    AiToolResult res;
    set_result(&res, "r1", "ran", NULL, 0);
    int idx = add_tool_round(&conv, "", &call, &res, 1);
    free_results(&res, 1);

    /* A record keyed to the same index by a reply whose call had another id
     * (Retry popped it, a new reply landed here): it must not match. */
    AiToolCall old;
    set_call(&old, "OLD", "run_command", "{\"command\":\"ls -la\"}");
    CmdRunLog log;
    cmd_run_log_init(&log);
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);
    chat_approval_add(&b->q, "ls -la", CMD_PLATFORM_LINUX);
    b->q.entries[0].status = APPROVE_COMPLETED;
    b->reply_msg = idx;
    cmd_run_log_record(&log, b, cmd_run_log_reply_hash("", 0, &old, 1), 0);
    cmd_batch_set_free(&set);
    ASSERT_EQ(log.count, 1);

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, &log, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "U:list files\nC:using tool: run_command - ls -la\nS:ran\n");
    chat_msg_list_clear(&list);
    cmd_run_log_free(&log);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_runcmd_with_live_batch_is_shown_once(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "run it");
    AiToolCall call;
    set_call(&call, "r1", "run_command", "{\"command\":\"uptime\"}");
    int idx = add_tool_round(&conv, "", &call, NULL, 1);   /* waiting on its card */

    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);
    chat_approval_add(&b->q, "uptime", CMD_PLATFORM_LINUX);
    b->q.entries[0].status = APPROVE_PENDING;
    const AiMessage *m = &conv.messages[idx];
    b->reply_msg = idx;
    b->reply_hash = cmd_run_log_reply_hash(ai_msg_content(m), m->content_len,
                                           m->tool_calls, m->n_tool_calls);

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, &set);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "U:run it\nK:uptime\n");   /* the card, once, last */
    ASSERT_EQ(list.tail->u.cmd.batch, b->id);
    chat_msg_list_clear(&list);
    cmd_batch_set_free(&set);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_live_batch_of_another_reply_keeps_the_row(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "run it");
    AiToolCall call;
    set_call(&call, "r1", "run_command", "{\"command\":\"uptime\"}");
    add_tool_round(&conv, "", &call, NULL, 1);

    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);
    chat_approval_add(&b->q, "df", CMD_PLATFORM_LINUX);
    b->reply_msg = 2;                 /* the right index, a different reply */
    b->reply_hash = 12345u;
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, &set);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "U:run it\nC:using tool: run_command - uptime\nK:df\n");
    chat_msg_list_clear(&list);
    cmd_batch_set_free(&set);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_status_record_follows_the_reply(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "go");
    AiToolCall call;
    set_call(&call, "r1", "run_command", "{\"command\":\"ls\"}");
    AiToolResult res;
    set_result(&res, "r1", "not run (the batch finished without it)", NULL, 0);
    int idx = add_tool_round(&conv, "", &call, &res, 1);
    free_results(&res, 1);
    const AiMessage *m = &conv.messages[idx];

    CmdRunLog log;
    cmd_run_log_init(&log);
    const char *texts[1] = { "[reply too large: results reduced]" };
    ASSERT_EQ(cmd_run_log_record_status(&log, idx,
        cmd_run_log_reply_hash(ai_msg_content(m), m->content_len, m->tool_calls,
                               m->n_tool_calls), texts, 1), 0);

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, &log, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    /* A status-only record shows no command, so the call keeps its row; the
     * status row comes after the results, as live. */
    ASSERT_STR_EQ(out,
        "U:go\n"
        "C:using tool: run_command - ls\n"
        "S:not run (the batch finished without it)\n"
        "S:[reply too large: results reduced]\n");
    chat_msg_list_clear(&list);
    cmd_run_log_free(&log);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_invalid_call_in_a_covered_reply_keeps_row_and_note(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "go");
    AiToolCall calls[2];
    set_call(&calls[0], "r1", "run_command", "{\"command\":\"ls\"}");
    set_call(&calls[1], "r2", "run_command", "{}");
    AiToolResult res[2];
    set_result(&res[0], "r1", "ran", "a", 0);
    set_result(&res[1], "r2", "run_command needs a \"command\" string", NULL, 0);
    int idx = add_tool_round(&conv, "", calls, res, 2);
    free_results(res, 2);
    CmdRunLog log;
    cmd_run_log_init(&log);
    record_batch(&log, &conv, idx, "ls", APPROVE_COMPLETED);

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, &log, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out,
        "U:go\n"
        "C:using tool: run_command\n"
        "S:run_command needs a \"command\" string\n"
        "K:ls\n");
    chat_msg_list_clear(&list);
    cmd_run_log_free(&log);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_exec_text_in_a_tool_round_is_split(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "check");
    /* A hidden tab's reply: text with an [EXEC] marker in a round that also
     * made a web tool call; its batch ended and was recorded. */
    AiToolCall call;
    set_call(&call, "t1", "web_search", "{}");
    AiToolResult res;
    set_result(&res, "t1", "", "hits", 0);
    int idx = add_tool_round(&conv,
        "Checking.\n[EXEC]uname -a[/EXEC]\nthen more text", &call, &res, 1);
    free_results(&res, 1);
    CmdRunLog log;
    cmd_run_log_init(&log);
    record_batch(&log, &conv, idx, "uname -a", APPROVE_COMPLETED);

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, &log, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out,
        "U:check\n"
        "C:using tool: web_search\n"
        "R:hits\n"
        "A:Checking.\n\n"
        "K:uname -a\n");
    chat_msg_list_clear(&list);
    cmd_run_log_free(&log);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_exec_text_with_live_card_is_split(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "check");
    ai_conv_add(&conv, AI_ROLE_ASSISTANT, "Running.\n[EXEC]df -h[/EXEC]");
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b = cmd_batch_add(&set, NULL, NULL);
    chat_approval_add(&b->q, "df -h", CMD_PLATFORM_LINUX);
    b->q.entries[0].status = APPROVE_PENDING;
    b->reply_msg = 2;
    b->reply_hash = cmd_run_log_turn_hash("Running.\n[EXEC]df -h[/EXEC]",
                                          strlen("Running.\n[EXEC]df -h[/EXEC]"));
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, &set);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "U:check\nA:Running.\n\nK:df -h\n");
    chat_msg_list_clear(&list);
    cmd_batch_set_free(&set);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_plain_reply_text_is_kept_whole_with_a_record(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "q");
    AiToolCall call;
    set_call(&call, "r1", "run_command", "{\"command\":\"ls\"}");
    int idx = add_tool_round(&conv, "I will list them.", &call, NULL, 1);
    CmdRunLog log;
    cmd_run_log_init(&log);
    record_batch(&log, &conv, idx, "ls", APPROVE_COMPLETED);
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, &log, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "U:q\nA:I will list them.\nK:ls\n");
    chat_msg_list_clear(&list);
    cmd_run_log_free(&log);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_partless_tool_turn(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "q");
    AiToolCall call;
    set_call(&call, "t1", "web_fetch", "{\"url\":\"https://example.com/\"}");
    agentic_add_assistant_tool_msg(&conv, "", &call, 1);
    /* The older shape: one answer in the message's own fields. */
    ai_conv_add(&conv, AI_ROLE_TOOL, "old style output");
    AiMessage *t = &conv.messages[conv.msg_count - 1];
    snprintf(t->tool_call_id, sizeof(t->tool_call_id), "t1");
    snprintf(t->tool_name, sizeof(t->tool_name), "web_fetch");
    snprintf(t->tool_note, sizeof(t->tool_note), "fetched");

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out,
        "U:q\n"
        "C:using tool: web_fetch - https://example.com/\n"
        "S:fetched\n"
        "R:old style output\n");
    chat_msg_list_clear(&list);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_orphan_tool_turn_and_empty_part(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "q");
    /* A results turn with no assistant turn above it: its parts still draw. */
    AiToolCall call;
    set_call(&call, "t9", "web_search", "{}");
    AiToolResult res;
    set_result(&res, "t9", "no key set", NULL, 0);
    agentic_add_assistant_tool_msg(&conv, "", &call, 1);
    agentic_add_tool_results(&conv, conv.messages[2].tool_calls, &res, 1);
    free_results(&res, 1);
    /* Remove the assistant turn: the results turn moves down to its place. */
    AiMessage keep = conv.messages[3];
    ai_msg_free(&conv.messages[2]);
    conv.messages[2] = keep;
    conv.msg_count = 3;
    memset(&conv.messages[3], 0, sizeof(conv.messages[3]));

    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, NULL);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "U:q\nS:no key set\n");
    chat_msg_list_clear(&list);
    ai_conv_reset(&conv);
    TEST_END();
}

int test_rebuild_replay_live_cards_come_last_in_order(void) {
    TEST_BEGIN();
    AiConversation conv;
    conv_start(&conv);
    ai_conv_add(&conv, AI_ROLE_USER, "q");
    CmdBatchSet set;
    cmd_batch_set_init(&set);
    CmdBatch *b1 = cmd_batch_add(&set, NULL, NULL);
    chat_approval_add(&b1->q, "one", CMD_PLATFORM_LINUX);
    CmdBatch *b2 = cmd_batch_add(&set, NULL, NULL);
    chat_approval_add(&b2->q, "two", CMD_PLATFORM_LINUX);
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, &set);
    char out[512];
    dump(&list, out, sizeof(out));
    ASSERT_STR_EQ(out, "U:q\nK:one\nK:two\n");
    ASSERT_EQ(list.tail->u.cmd.batch, b2->id);
    chat_msg_list_clear(&list);
    cmd_batch_set_free(&set);
    ai_conv_reset(&conv);

    /* An empty conversation and NULL arguments change nothing. */
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, NULL, NULL, NULL, NULL);
    chat_rebuild_replay(NULL, &conv, NULL, NULL, NULL);
    ASSERT_EQ(list.count, 0);
    TEST_END();
}

/* The --ui-demo=tool state replays with the rows of a real cut. */
int test_rebuild_replay_demo_tool_state_shows_the_cut(void) {
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    ai_conv_init(&conv, "m");
    ASSERT_EQ(ui_demo_build("tool", &conv, &approval, &approval2, NULL, 0), 0);
    ChatMsgList list;
    chat_msg_list_init(&list);
    chat_rebuild_replay(&list, &conv, NULL, NULL, NULL);
    char out[2048];
    dump(&list, out, sizeof(out));
    ASSERT_TRUE(strstr(out, "C:using tool: web_search\n"
                            "S:[truncated by nutshell at 65536 bytes]\n"
                            "S:[web_search output truncated at 64 KiB]\n"
                            "R:Rate limit dashboard") == out + strlen("U:Why are we hitting API rate limits again? Look it up.\n"));
    chat_msg_list_clear(&list);
    ai_conv_reset(&conv);
    TEST_END();
}
