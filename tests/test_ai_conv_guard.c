#include "test_framework.h"
#include "ai_conv_guard.h"
#include "ai_agentic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers --------------------------------------------------------------- */

static AiConversation *new_conv(void)
{
    AiConversation *c = (AiConversation *)calloc(1, sizeof(*c));
    if (c) {
        ai_conv_init(c, "test-model");
        ai_conv_add(c, AI_ROLE_SYSTEM, "SYS");
        ai_conv_add(c, AI_ROLE_USER, "go");
    }
    return c;
}

static void free_conv(AiConversation *c)
{
    if (!c) return;
    ai_conv_reset(c);
    free(c);
}

/* An assistant turn with n calls, ids "a", "b", "c" ... */
static int add_asst(AiConversation *c, int n, AiToolCall *calls)
{
    memset(calls, 0, (size_t)n * sizeof(AiToolCall));
    for (int i = 0; i < n; i++) {
        snprintf(calls[i].id, sizeof(calls[i].id), "%c", 'a' + i);
        snprintf(calls[i].name, sizeof(calls[i].name), "run_command");
        snprintf(calls[i].input_json, sizeof(calls[i].input_json),
                 "{\"command\":\"ls\"}");
    }
    return agentic_add_assistant_tool_msg(c, "checking", calls, n);
}

/* A results turn answering calls[first .. first+n). */
static int add_results(AiConversation *c, const AiToolCall *calls, int first,
                       int n)
{
    AiToolResult r[8];
    char text[] = "out";
    memset(r, 0, sizeof(r));
    for (int i = 0; i < n; i++) {
        snprintf(r[i].tool_use_id, sizeof(r[i].tool_use_id), "%s",
                 calls[first + i].id);
        r[i].content = text;
        r[i].content_len = 3;
    }
    return agentic_add_tool_results(c, &calls[first], r, n);
}

static int body_ok(const AiConversation *c, const char *provider)
{
    static char buf[65536];
    return ai_build_request_body_frame(c, NULL, NULL, NULL, buf, sizeof(buf),
                                       0, provider) > 0;
}

/* ---- the pairing invariant ------------------------------------------------- */

int test_pairs_plain_ok(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    ASSERT_NOT_NULL(c);
    if (c) {
        ai_conv_add(c, AI_ROLE_ASSISTANT, "hello");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        ASSERT_EQ(ai_conv_tool_calls_open(c), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(NULL), 0);
    }
    free_conv(c);
    TEST_END();
}

int test_pairs_tool_round_ok(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(ai_conv_tool_calls_open(c), 1);
        ASSERT_EQ(add_results(c, calls, 0, 2), 0);
        ASSERT_EQ(ai_conv_tool_calls_open(c), 0);
        ai_conv_add(c, AI_ROLE_USER, "note");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        /* answers in the other order are still one answer each */
        ai_conv_reset(c);
        ai_conv_add(c, AI_ROLE_SYSTEM, "SYS");
        ai_conv_add(c, AI_ROLE_USER, "go");
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        AiToolCall rev[2];
        rev[0] = calls[1];
        rev[1] = calls[0];
        ASSERT_EQ(add_results(c, rev, 0, 2), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
    }
    free_conv(c);
    TEST_END();
}

/* A conversation stored the way it was before parts: one parts-less results
 * message per call. Each stands for one part with its own id. */
int test_pairs_legacy_tool_messages_ok(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        for (int i = 0; i < 2; i++) {
            ai_conv_add(c, AI_ROLE_TOOL, "legacy");
            AiMessage *t = &c->messages[c->msg_count - 1];
            snprintf(t->tool_call_id, sizeof(t->tool_call_id), "%s", calls[i].id);
        }
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        ASSERT_EQ(ai_conv_tool_calls_open(c), 0);
        ai_conv_add(c, AI_ROLE_ASSISTANT, "done");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        /* one legacy message of two calls is open */
        ai_conv_reset(c);
        ai_conv_add(c, AI_ROLE_USER, "go");
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ai_conv_add(c, AI_ROLE_TOOL, "legacy");
        AiMessage *t = &c->messages[c->msg_count - 1];
        snprintf(t->tool_call_id, sizeof(t->tool_call_id), "a");
        ASSERT_EQ(ai_conv_tool_calls_open(c), 1);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
    }
    free_conv(c);
    TEST_END();
}

int test_pairs_note_between_fails(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[1];
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(add_asst(c, 1, calls), 0);
        ai_conv_add(c, AI_ROLE_USER, "policy raised");
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
    }
    free_conv(c);
    TEST_END();
}

int test_pairs_exec_continue_between_fails(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[1];
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(add_asst(c, 1, calls), 0);
        ai_conv_add(c, AI_ROLE_USER,
                    "The commands above have been executed. Output follows.");
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
    }
    free_conv(c);
    TEST_END();
}

int test_pairs_missing_results_fails(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ai_conv_add(c, AI_ROLE_ASSISTANT, "text");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
        /* the open check looks only at the tail */
        ASSERT_EQ(ai_conv_tool_calls_open(c), 0);
    }
    free_conv(c);
    TEST_END();
}

int test_pairs_partial_results_fails(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        /* K-1 of K, then a user turn */
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ASSERT_EQ(ai_conv_tool_calls_open(c), 1);
        ai_conv_add(c, AI_ROLE_USER, "next");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);

        /* two results turns of one part each */
        ai_conv_reset(c);
        ai_conv_add(c, AI_ROLE_USER, "go");
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ASSERT_EQ(add_results(c, calls, 1, 1), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);

        /* one call answered twice */
        ai_conv_reset(c);
        ai_conv_add(c, AI_ROLE_USER, "go");
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(add_results(c, calls, 0, 2), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        /* the store pairs by position; written past it, the second part
         * answers "a" again and "b" is left unanswered */
        snprintf(c->messages[c->msg_count - 1].parts[1].tool_call_id,
                 sizeof(c->messages[c->msg_count - 1].parts[1].tool_call_id), "a");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
    }
    free_conv(c);
    TEST_END();
}

int test_pairs_wrong_id_fails(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[1], other[1];
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(add_asst(c, 1, calls), 0);
        memset(other, 0, sizeof(other));
        snprintf(other[0].id, sizeof(other[0].id), "b");
        ASSERT_EQ(add_results(c, other, 0, 1), 0);
        /* the store pairs by the stored turn's ids, so the answer carries "a" */
        ASSERT_STR_EQ(c->messages[c->msg_count - 1].parts[0].tool_call_id, "a");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        /* an answer with the wrong id (written past the store) is refused */
        snprintf(c->messages[c->msg_count - 1].parts[0].tool_call_id,
                 sizeof(c->messages[c->msg_count - 1].parts[0].tool_call_id), "b");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
    }
    free_conv(c);
    TEST_END();
}

int test_pairs_orphan_result_fails(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[1];
    ASSERT_NOT_NULL(c);
    if (c) {
        memset(calls, 0, sizeof(calls));
        snprintf(calls[0].id, sizeof(calls[0].id), "a");
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
        /* a results turn after a closed round, with no tool_use of its own */
        ai_conv_reset(c);
        ai_conv_add(c, AI_ROLE_USER, "go");
        ASSERT_EQ(add_asst(c, 1, calls), 0);
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ai_conv_add(c, AI_ROLE_ASSISTANT, "ok");
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
    }
    free_conv(c);
    TEST_END();
}

int test_pairs_open_at_end_fails(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[1];
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(add_asst(c, 1, calls), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
        ASSERT_EQ(ai_conv_tool_calls_open(c), 1);
        ASSERT_EQ(ai_conv_tool_calls_open(NULL), 0);
    }
    free_conv(c);
    TEST_END();
}

/* ---- the note door --------------------------------------------------------- */

int test_conv_note_queued_while_open(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiNoteQueue q;
    AiToolCall calls[1];
    memset(&q, 0, sizeof(q));
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(add_asst(c, 1, calls), 0);
        int before = c->msg_count;
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 0, "policy raised"), 1);
        ASSERT_EQ(c->msg_count, before);
        ASSERT_EQ(q.count, 1);
        /* flush does nothing while open */
        ASSERT_EQ(ai_conv_flush_notes(c, &q), 0);
        ASSERT_EQ(q.count, 1);
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ASSERT_EQ(ai_conv_flush_notes(c, &q), 1);
        ASSERT_EQ(q.count, 0);
        AiMessage *last = &c->messages[c->msg_count - 1];
        ASSERT_EQ((int)last->role, (int)AI_ROLE_USER);
        ASSERT_STR_EQ(last->content, "policy raised");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
    }
    free_conv(c);
    TEST_END();
}

int test_conv_note_deferred_while_busy(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiNoteQueue q;
    memset(&q, 0, sizeof(q));
    ASSERT_NOT_NULL(c);
    if (c) {
        int before = c->msg_count;
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 1, "first"), 1);
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 1, "second"), 1);
        ASSERT_EQ(c->msg_count, before);
        ASSERT_EQ(q.count, 2);
        ASSERT_EQ(ai_conv_flush_notes(c, &q), 2);
        ASSERT_EQ(c->msg_count, before + 2);
        ASSERT_STR_EQ(c->messages[before].content, "first");
        ASSERT_STR_EQ(c->messages[before + 1].content, "second");
        ASSERT_EQ(q.count, 0);
        /* closed and not busy: appended at once */
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 0, "direct"), 0);
        ASSERT_EQ(c->msg_count, before + 3);
        ASSERT_EQ(q.count, 0);
        /* a queued note is never overtaken by a later direct one */
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 1, "queued"), 1);
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 0, "later"), 0);
        ASSERT_EQ(q.count, 0);
        ASSERT_STR_EQ(c->messages[c->msg_count - 2].content, "queued");
        ASSERT_STR_EQ(c->messages[c->msg_count - 1].content, "later");
        /* NULL queue: appended when it can be, refused when it cannot queue */
        ASSERT_EQ(ai_conv_add_user_note(c, NULL, 0, "no queue"), 0);
        ASSERT_EQ(ai_conv_add_user_note(c, NULL, 1, "no queue"), -1);
    }
    free_conv(c);
    TEST_END();
}

int test_conv_note_refusals(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiNoteQueue q;
    memset(&q, 0, sizeof(q));
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 0, NULL), -1);
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 0, ""), -1);
        ASSERT_EQ(ai_conv_add_user_note(NULL, &q, 0, "x"), -1);
        for (int i = 0; i < AI_NOTE_QUEUE_MAX; i++)
            ASSERT_EQ(ai_conv_add_user_note(c, &q, 1, "n"), 1);
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 1, "fifth"), -1);
        ASSERT_EQ(q.count, AI_NOTE_QUEUE_MAX);

        /* a full conversation refuses a direct note */
        ai_conv_reset(c);
        while (c->msg_count < AI_MAX_MESSAGES)
            ai_conv_add(c, AI_ROLE_USER, "fill");
        AiNoteQueue q2;
        memset(&q2, 0, sizeof(q2));
        ASSERT_EQ(ai_conv_add_user_note(c, &q2, 0, "no room"), -1);
        ASSERT_EQ(c->msg_count, AI_MAX_MESSAGES);
        ASSERT_EQ(q2.count, 0);
        /* queued notes that no longer fit stay queued (review of B1-B11) */
        ASSERT_EQ(ai_conv_add_user_note(c, &q2, 1, "queued"), 1);
        ASSERT_EQ(ai_conv_flush_notes(c, &q2), 0);
        ASSERT_EQ(q2.count, 1);
        ASSERT_STR_EQ(q2.text[0], "queued");
        ASSERT_EQ(c->msg_count, AI_MAX_MESSAGES);
    }
    free_conv(c);
    TEST_END();
}

/* 1 when s is valid UTF-8 */
static int utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3
              : (*p >> 3) == 30 ? 4 : 0;
        if (!n) return 0;
        for (int i = 1; i < n; i++)
            if ((p[i] & 0xC0) != 0x80) return 0;
        p += n;
    }
    return 1;
}

int test_conv_note_long_text_cut(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiNoteQueue q;
    static char text[6000];
    memset(&q, 0, sizeof(q));
    ASSERT_NOT_NULL(c);
    if (c) {
        /* 4095 bytes fit whole */
        memset(text, 'x', 4095);
        text[4095] = '\0';
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 1, text), 1);
        ASSERT_EQ(strlen(q.text[0]), 4095);
        ASSERT_STR_EQ(q.text[0], text);

        /* 5000 bytes are cut, with the marker, still fitting a slot */
        memset(text, 'y', 5000);
        text[5000] = '\0';
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 1, text), 1);
        size_t n = strlen(q.text[1]);
        ASSERT_TRUE(n < (size_t)AI_NOTE_TEXT_MAX);
        const char *marker = " [\xE2\x80\xA6 cut]";
        ASSERT_TRUE(n >= strlen(marker));
        ASSERT_STR_EQ(q.text[1] + n - strlen(marker), marker);
        ASSERT_EQ(q.text[1][0], 'y');

        /* a multi-byte character straddling the cut is dropped whole */
        for (size_t i = 0; i + 3 <= 5000; i += 3)
            memcpy(text + i, "\xE2\x82\xAC", 3);   /* the euro sign */
        text[4998] = 'z'; text[4999] = 'z';
        text[5000] = '\0';
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 1, text), 1);
        ASSERT_TRUE(utf8_valid(q.text[2]));
        n = strlen(q.text[2]);
        ASSERT_STR_EQ(q.text[2] + n - strlen(marker), marker);

        /* the direct path cuts the same way */
        AiNoteQueue q2;
        memset(&q2, 0, sizeof(q2));
        memset(text, 'q', 5000);
        text[5000] = '\0';
        ASSERT_EQ(ai_conv_add_user_note(c, &q2, 0, text), 0);
        const AiMessage *m = &c->messages[c->msg_count - 1];
        ASSERT_TRUE(m->content_len < (size_t)AI_NOTE_TEXT_MAX);
        ASSERT_TRUE(strstr(m->content, "cut]") != NULL);
    }
    free_conv(c);
    TEST_END();
}

int test_conv_room_for_send(void)
{
    TEST_BEGIN();
    AiConversation *c = (AiConversation *)calloc(1, sizeof(*c));
    ASSERT_NOT_NULL(c);
    if (c) {
        ai_conv_init(c, "m");
        while (c->msg_count < 62) ai_conv_add(c, AI_ROLE_USER, "x");
        ASSERT_EQ(ai_conv_room_for_send(c, 0, NULL), 1);
        ASSERT_EQ(ai_conv_room_for_send(c, 1, NULL), 0);
        ai_conv_add(c, AI_ROLE_USER, "x");
        ASSERT_EQ(c->msg_count, 63);
        ASSERT_EQ(ai_conv_room_for_send(c, 0, NULL), 0);
        ASSERT_EQ(ai_conv_room_for_send(NULL, 0, NULL), 0);
        /* the typed "stop, don't do that" is refused, nothing stored: the
         * caller's check is the only gate, and it says no */
        if (ai_conv_room_for_send(c, 0, NULL))
            ai_conv_add(c, AI_ROLE_USER, "stop, don't do that");
        ASSERT_EQ(c->msg_count, 63);
    }
    free_conv(c);
    TEST_END();
}

/* ---- dropping an open turn -------------------------------------------------- */

int test_conv_drop_open_tool_turn(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(ai_conv_drop_open_tool_turn(c), 0);   /* nothing open */
        ASSERT_EQ(c->msg_count, 2);
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(ai_conv_drop_open_tool_turn(c), 1);
        ASSERT_EQ(c->msg_count, 2);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        /* partial results go with it */
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ASSERT_EQ(ai_conv_drop_open_tool_turn(c), 1);
        ASSERT_EQ(c->msg_count, 2);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        /* a closed round stays */
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(add_results(c, calls, 0, 2), 0);
        ASSERT_EQ(ai_conv_drop_open_tool_turn(c), 0);
        ASSERT_EQ(c->msg_count, 4);
        ASSERT_EQ(ai_conv_drop_open_tool_turn(NULL), 0);
    }
    free_conv(c);
    TEST_END();
}

/* An allocator that never succeeds. */
static void *no_alloc(size_t n)
{
    (void)n;
    return NULL;
}

/* Spec 25: when not even the notes-only array can be had, the open assistant
 * turn is dropped, so the conversation stays well-formed (and sendable). */
int test_agentic_results_oom_drops_open_turn(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        char text[] = "out";
        AiToolResult r[2];
        memset(r, 0, sizeof(r));
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        for (int i = 0; i < 2; i++) {
            snprintf(r[i].tool_use_id, sizeof(r[i].tool_use_id), "%s", calls[i].id);
            r[i].content = text;
            r[i].content_len = 3;
        }
        agentic_set_alloc_for_test(no_alloc);
        ASSERT_EQ(agentic_add_tool_results(c, calls, r, 2), -2);
        agentic_set_alloc_for_test(NULL);
        ASSERT_EQ(c->msg_count, 2);   /* the assistant turn is gone */
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        ASSERT_EQ(body_ok(c, "anthropic"), 1);
    }
    free_conv(c);
    TEST_END();
}

/* ---- the body builders ------------------------------------------------------ */

int test_body_refuses_malformed(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[1];
    ASSERT_NOT_NULL(c);
    if (c) {
        char buf[16384];
        ASSERT_EQ(add_asst(c, 1, calls), 0);
        ai_conv_add(c, AI_ROLE_USER, "policy raised");
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ASSERT_EQ(body_ok(c, "anthropic"), 0);
        ASSERT_EQ(body_ok(c, "openai"), 0);
        ASSERT_EQ(body_ok(c, "moonshot"), 0);
        ASSERT_EQ(body_ok(c, NULL), 0);
        ASSERT_EQ(ai_build_request_body(c, buf, sizeof(buf)), 0);
        ASSERT_EQ(ai_build_request_body_ex(c, NULL, buf, sizeof(buf), 0,
                                           "anthropic"), 0);
        ASSERT_EQ(ai_build_request_body_tools(c, NULL, "[]", buf, sizeof(buf), 0,
                                              "openai"), 0);
        ASSERT_EQ(ai_build_request_body_frame(c, NULL, "[]", NULL, buf,
                                              sizeof(buf), 0, "anthropic"), 0);

        /* the same conversation, repaired, builds */
        ai_conv_reset(c);
        ai_conv_add(c, AI_ROLE_SYSTEM, "SYS");
        ai_conv_add(c, AI_ROLE_USER, "go");
        ASSERT_EQ(add_asst(c, 1, calls), 0);
        ASSERT_EQ(add_results(c, calls, 0, 1), 0);
        ai_conv_add(c, AI_ROLE_USER, "policy raised");
        ASSERT_EQ(body_ok(c, "anthropic"), 1);
        ASSERT_EQ(body_ok(c, "openai"), 1);
    }
    free_conv(c);
    TEST_END();
}

/* ---- review of B1-B11, 2026-11-11: room counts queued notes ------------------ */

/* c with `n` messages, the last an open run_command turn with one call. */
static void conv_open_at(AiConversation *c, int n, AiToolCall *calls)
{
    while (c->msg_count < n - 1) ai_conv_add(c, AI_ROLE_USER, "x");
    add_asst(c, 1, calls);
}

static void queue_notes(AiNoteQueue *q, int n)
{
    memset(q, 0, sizeof(*q));
    for (int i = 0; i < n; i++) {
        snprintf(q->text[i], sizeof(q->text[i]), "note%d", i + 1);
        q->count++;
    }
}

/* Review Focus 1's own sequence: 61 messages, an open run_command turn, two
 * notes queued. The typed finish stores the results (62) and the flush two
 * notes (64); the typed message then has no room. The check must say so. */
int test_conv_room_counts_queued_notes(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[1];
    AiNoteQueue q;
    ASSERT_NOT_NULL(c);
    if (c) {
        conv_open_at(c, 61, calls);
        ASSERT_EQ(c->msg_count, 61);
        queue_notes(&q, 2);
        ASSERT_EQ(ai_conv_room_for_send(c, 1, NULL), 1);   /* 61 + 3 <= 64 */
        ASSERT_EQ(ai_conv_room_for_send(c, 1, &q), 0);     /* + 2 notes */
        q.count = 0;
        ASSERT_EQ(ai_conv_room_for_send(c, 1, &q), 1);     /* an empty queue */
        queue_notes(&q, 2);
        /* no batch open: 60 + 2 + 2 = 64 fits, 61 + 2 + 2 does not */
        ai_conv_reset(c);
        while (c->msg_count < 60) ai_conv_add(c, AI_ROLE_USER, "x");
        ASSERT_EQ(ai_conv_room_for_send(c, 0, &q), 1);
        ai_conv_add(c, AI_ROLE_USER, "x");
        ASSERT_EQ(ai_conv_room_for_send(c, 0, &q), 0);
        ASSERT_EQ(ai_conv_room_for_send(c, 0, NULL), 1);
    }
    free_conv(c);
    TEST_END();
}

/* SETTLED: the results plus the flushed notes must not take the slot §4's +3
 * keeps for the reply. */
int test_agentic_room_counts_queued_notes(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiNoteQueue q;
    ASSERT_NOT_NULL(c);
    if (c) {
        queue_notes(&q, 2);
        while (c->msg_count < 59) ai_conv_add(c, AI_ROLE_USER, "x");
        ASSERT_EQ(agentic_room_for_tool_round(c, NULL), 1);   /* 59 + 3 */
        ASSERT_EQ(agentic_room_for_tool_round(c, &q), 1);     /* 59 + 3 + 2 = 64 */
        ai_conv_add(c, AI_ROLE_USER, "x");
        ASSERT_EQ(agentic_room_for_tool_round(c, NULL), 1);   /* 60 + 3 */
        ASSERT_EQ(agentic_room_for_tool_round(c, &q), 0);     /* 65 */
        queue_notes(&q, 0);
        ASSERT_EQ(agentic_room_for_tool_round(c, &q), 1);
        ASSERT_EQ(agentic_room_for_tool_round(NULL, &q), 0);
    }
    free_conv(c);
    TEST_END();
}

/* A note that cannot be stored stays queued, in order; nothing is dropped. */
int test_conv_flush_keeps_what_does_not_fit(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiNoteQueue q;
    ASSERT_NOT_NULL(c);
    if (c) {
        while (c->msg_count < AI_MAX_MESSAGES - 2) ai_conv_add(c, AI_ROLE_USER, "x");
        queue_notes(&q, 4);
        ASSERT_EQ(ai_conv_flush_notes(c, &q), 2);
        ASSERT_EQ(c->msg_count, AI_MAX_MESSAGES);
        ASSERT_STR_EQ(c->messages[AI_MAX_MESSAGES - 2].content, "note1");
        ASSERT_STR_EQ(c->messages[AI_MAX_MESSAGES - 1].content, "note2");
        ASSERT_EQ(q.count, 2);
        ASSERT_STR_EQ(q.text[0], "note3");
        ASSERT_STR_EQ(q.text[1], "note4");
        /* nothing more fits: nothing more moves */
        ASSERT_EQ(ai_conv_flush_notes(c, &q), 0);
        ASSERT_EQ(q.count, 2);
        /* a new note queues behind them rather than overtaking */
        ai_conv_reset(c);
        ai_conv_add(c, AI_ROLE_USER, "x");
        while (c->msg_count < AI_MAX_MESSAGES) ai_conv_add(c, AI_ROLE_USER, "x");
        ASSERT_EQ(ai_conv_add_user_note(c, &q, 0, "later"), 1);
        ASSERT_EQ(q.count, 3);
        ASSERT_STR_EQ(q.text[2], "later");
    }
    free_conv(c);
    TEST_END();
}

/* ---- made-up ids, pairing by the stored turn ---------------------------------- */

static void set_call(AiToolCall *c, const char *id, const char *name)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->id, sizeof(c->id), "%s", id);
    snprintf(c->name, sizeof(c->name), "%s", name);
    snprintf(c->input_json, sizeof(c->input_json), "{\"query\":\"q\"}");
}

/* a result for each call, content "out", ids as the caller (wrongly) has them */
static int results_for(AiConversation *c, const AiToolCall *calls, int n)
{
    AiToolResult r[4];
    char text[] = "out";
    memset(r, 0, sizeof(r));
    for (int i = 0; i < n; i++) {
        snprintf(r[i].tool_use_id, sizeof(r[i].tool_use_id), "%s", calls[i].id);
        r[i].content = text;
        r[i].content_len = 3;
    }
    return agentic_add_tool_results(c, calls, r, n);
}

int test_ids_empty_get_made_up(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        set_call(&calls[0], "", "web_search");
        set_call(&calls[1], "", "web_search");
        ASSERT_EQ(agentic_add_assistant_tool_msg(c, "x", calls, 2), 0);
        const AiMessage *a = &c->messages[c->msg_count - 1];
        ASSERT_STR_EQ(a->tool_calls[0].id, "call_1");
        ASSERT_STR_EQ(a->tool_calls[1].id, "call_2");
        /* the caller's calls are untouched */
        ASSERT_STR_EQ(calls[0].id, "");
        ASSERT_EQ(results_for(c, calls, 2), 0);
        const AiMessage *t = &c->messages[c->msg_count - 1];
        ASSERT_STR_EQ(t->parts[0].tool_call_id, "call_1");
        ASSERT_STR_EQ(t->parts[1].tool_call_id, "call_2");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        ASSERT_EQ(body_ok(c, "openai"), 1);
        ASSERT_EQ(body_ok(c, "anthropic"), 1);

        /* a later turn picks a number no earlier id uses */
        set_call(&calls[0], "", "web_search");
        ASSERT_EQ(agentic_add_assistant_tool_msg(c, "y", calls, 1), 0);
        ASSERT_STR_EQ(c->messages[c->msg_count - 1].tool_calls[0].id, "call_3");
        ASSERT_EQ(results_for(c, calls, 1), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);

        /* the provider's own id "call_4" is never reused for a made-up one */
        set_call(&calls[0], "call_4", "web_search");
        set_call(&calls[1], "", "web_search");
        ASSERT_EQ(agentic_add_assistant_tool_msg(c, "z", calls, 2), 0);
        ASSERT_STR_EQ(c->messages[c->msg_count - 1].tool_calls[0].id, "call_4");
        ASSERT_STR_EQ(c->messages[c->msg_count - 1].tool_calls[1].id, "call_5");
        ASSERT_EQ(results_for(c, calls, 2), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
    }
    free_conv(c);
    TEST_END();
}

int test_ids_repeated_get_made_up(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        set_call(&calls[0], "a", "web_search");
        set_call(&calls[1], "a", "web_fetch");
        ASSERT_EQ(agentic_add_assistant_tool_msg(c, "x", calls, 2), 0);
        const AiMessage *a = &c->messages[c->msg_count - 1];
        ASSERT_STR_EQ(a->tool_calls[0].id, "a");
        ASSERT_STR_EQ(a->tool_calls[1].id, "call_1");
        ASSERT_STR_EQ(a->tool_calls[1].name, "web_fetch");
        /* the results are paired by the stored ids, in call order */
        AiToolResult r[2];
        char text[] = "out";
        memset(r, 0, sizeof(r));
        snprintf(r[0].tool_use_id, sizeof(r[0].tool_use_id), "a");
        snprintf(r[1].tool_use_id, sizeof(r[1].tool_use_id), "a");
        r[0].content = r[1].content = text;
        r[0].content_len = r[1].content_len = 3;
        ASSERT_EQ(agentic_add_tool_results(c, calls, r, 2), 0);
        const AiMessage *t = &c->messages[c->msg_count - 1];
        ASSERT_STR_EQ(t->parts[0].tool_call_id, "a");
        ASSERT_STR_EQ(t->parts[1].tool_call_id, "call_1");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        ASSERT_EQ(body_ok(c, "openai"), 1);
    }
    free_conv(c);
    TEST_END();
}

/* calls a,a answered a,b is refused, so is a,a answered a,a: a stored turn with
 * a repeated id never passes, whichever way it got there. */
int test_pairs_repeated_ids_fail(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ(add_asst(c, 2, calls), 0);            /* ids a, b */
        ASSERT_EQ(add_results(c, calls, 0, 2), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        /* the turn now says a,a; its answer still says a,b */
        AiMessage *a = &c->messages[c->msg_count - 2];
        snprintf(a->tool_calls[1].id, sizeof(a->tool_calls[1].id), "a");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
        /* answered a,a */
        AiMessage *t = &c->messages[c->msg_count - 1];
        snprintf(t->parts[1].tool_call_id, sizeof(t->parts[1].tool_call_id), "a");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 0);
        ASSERT_EQ(body_ok(c, "openai"), 0);
    }
    free_conv(c);
    TEST_END();
}

/* A turn whose ids are unique and non-empty is stored byte for byte, and the
 * answer takes the stored ids, not the caller's. */
int test_ids_unique_stored_unchanged(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        set_call(&calls[0], "toolu_01", "web_search");
        set_call(&calls[1], "toolu_02", "web_fetch");
        ASSERT_EQ(agentic_add_assistant_tool_msg(c, "x", calls, 2), 0);
        const AiMessage *a = &c->messages[c->msg_count - 1];
        ASSERT_EQ(memcmp(a->tool_calls, calls, sizeof(calls)), 0);
        AiToolCall caller[2];
        set_call(&caller[0], "zzz", "web_search");
        set_call(&caller[1], "yyy", "web_fetch");
        ASSERT_EQ(results_for(c, caller, 2), 0);
        const AiMessage *t = &c->messages[c->msg_count - 1];
        ASSERT_STR_EQ(t->parts[0].tool_call_id, "toolu_01");
        ASSERT_STR_EQ(t->parts[1].tool_call_id, "toolu_02");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
    }
    free_conv(c);
    TEST_END();
}

/* ---- R37: what agentic_add_tool_results returns -------------------------------- */

static int g_n, g_fail_at;
static void *fail_nth_alloc(size_t n)
{
    g_n++;
    return g_n == g_fail_at ? NULL : malloc(n);
}

int test_agentic_results_return_values(void)
{
    TEST_BEGIN();
    AiConversation *c = new_conv();
    AiToolCall calls[2];
    ASSERT_NOT_NULL(c);
    if (c) {
        /* 0: stored */
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(add_results(c, calls, 0, 2), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
        ASSERT_EQ(c->messages[c->msg_count - 1].parts[0].content[0], 'o');

        /* 1: the parts array comes, a content copy does not: notes only */
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        g_n = 0; g_fail_at = 2;
        agentic_set_alloc_for_test(fail_nth_alloc);
        ASSERT_EQ(add_results(c, calls, 0, 2), 1);
        agentic_set_alloc_for_test(NULL);
        ASSERT_NULL(c->messages[c->msg_count - 1].parts[0].content);
        ASSERT_STR_EQ(c->messages[c->msg_count - 1].parts[0].tool_note,
                      "output dropped: Nutshell ran out of memory");
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);

        /* -2: not even the notes-only array: dropped, well-formed */
        int before = c->msg_count;
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(c->msg_count, before + 1);
        agentic_set_alloc_for_test(no_alloc);
        ASSERT_EQ(add_results(c, calls, 0, 2), -2);
        agentic_set_alloc_for_test(NULL);
        ASSERT_EQ(c->msg_count, before);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);

        /* -1: the conversation is full: nothing stored, the turn dropped */
        ai_conv_reset(c);
        while (c->msg_count < AI_MAX_MESSAGES - 1) ai_conv_add(c, AI_ROLE_USER, "x");
        ASSERT_EQ(add_asst(c, 2, calls), 0);
        ASSERT_EQ(c->msg_count, AI_MAX_MESSAGES);
        ASSERT_EQ(ai_conv_tool_calls_open(c), 1);
        ASSERT_EQ(add_results(c, calls, 0, 2), -1);
        ASSERT_EQ(c->msg_count, AI_MAX_MESSAGES - 1);
        ASSERT_EQ(ai_conv_tool_calls_open(c), 0);
        ASSERT_EQ(ai_conv_tool_pairs_well_formed(c), 1);
    }
    free_conv(c);
    TEST_END();
}
