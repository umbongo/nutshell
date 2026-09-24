/* tests/test_ai_stream.c -- ownership and lifetime of AI conversations,
 * streams and worker jobs (src/core/ai_stream.h, src/core/worker_life.h,
 * ai_conv_take()/ai_conv_copy() in src/core/ai_prompt.h).
 *
 * The worker thread never runs here: each test drives the two sides of the
 * handshake (UI and worker) by hand, in the order a real race could deliver
 * them, so every interleaving is deterministic. */
#include "test_framework.h"
#include "ai_prompt.h"
#include "ai_stream.h"
#include "worker_life.h"
#include <stdlib.h>
#include <string.h>

/* A system prompt larger than AI_MSG_MAX lives in content_overflow -- the
 * shape of the default system prompt that the old shallow ai_conv_move()
 * freed twice. */
static char *big_text(size_t n, char fill)
{
    char *s = malloc(n + 1);
    if (!s) return NULL;
    memset(s, fill, n);
    s[n] = '\0';
    return s;
}

static void build_overflow_conv(AiConversation *c)
{
    ai_conv_init(c, "model-x");
    char *sys = big_text(AI_MSG_MAX + 100, 'S');
    ai_conv_add(c, AI_ROLE_SYSTEM, sys);
    free(sys);
    ai_conv_add(c, AI_ROLE_USER, "hello");
    c->messages[1].attachment = calloc(1, sizeof(AiAttachment));
    c->messages[1].attachment->base64_url = strdup("data:image/png;base64,AAAA");
    ai_conv_add(c, AI_ROLE_ASSISTANT, "hi");
    c->messages[2].tool_calls = calloc(1, sizeof(AiToolCall));
    c->messages[2].n_tool_calls = 1;
    strcpy(c->messages[2].tool_calls[0].name, "web_search");
}

/* ---- ai_conv_take ------------------------------------------------------ */

int test_ai_conv_take_moves_overflow_and_clears_source(void)
{
    TEST_BEGIN();
    AiConversation *src = calloc(1, sizeof(*src));
    AiConversation *dst = calloc(1, sizeof(*dst));
    build_overflow_conv(src);
    ai_conv_init(dst, "other");
    char *ovf = src->messages[0].content_overflow;
    AiAttachment *att = src->messages[1].attachment;
    AiToolCall *tc = src->messages[2].tool_calls;
    ASSERT_NOT_NULL(ovf);

    ai_conv_take(dst, src);

    ASSERT_EQ(dst->msg_count, 3);
    ASSERT_TRUE(dst->messages[0].content_overflow == ovf);
    ASSERT_TRUE(dst->messages[1].attachment == att);
    ASSERT_TRUE(dst->messages[2].tool_calls == tc);
    ASSERT_STR_EQ(dst->model, "model-x");
    /* The source owns nothing any more, but keeps its model. */
    ASSERT_EQ(src->msg_count, 0);
    ASSERT_NULL(src->messages[0].content_overflow);
    ASSERT_NULL(src->messages[1].attachment);
    ASSERT_NULL(src->messages[2].tool_calls);
    ASSERT_STR_EQ(src->model, "model-x");

    ai_conv_reset(src);   /* would double-free under the old shallow move */
    ai_conv_reset(dst);
    free(src);
    free(dst);
    TEST_END();
}

int test_ai_conv_take_frees_previous_destination(void)
{
    TEST_BEGIN();
    AiConversation *src = calloc(1, sizeof(*src));
    AiConversation *dst = calloc(1, sizeof(*dst));
    ai_conv_init(src, "m");
    ai_conv_add(src, AI_ROLE_USER, "new");
    build_overflow_conv(dst);          /* dst holds heap buffers already */

    ai_conv_take(dst, src);            /* must free them, not leak or keep */

    ASSERT_EQ(dst->msg_count, 1);
    ASSERT_NULL(dst->messages[0].content_overflow);
    ASSERT_STR_EQ(ai_msg_content(&dst->messages[0]), "new");
    /* Slots past msg_count carry no stale owners. */
    ASSERT_NULL(dst->messages[1].attachment);
    ASSERT_NULL(dst->messages[2].tool_calls);
    ai_conv_reset(dst);
    ai_conv_reset(src);
    free(src);
    free(dst);
    TEST_END();
}

int test_ai_conv_take_self_and_null_are_noops(void)
{
    TEST_BEGIN();
    AiConversation *c = calloc(1, sizeof(*c));
    build_overflow_conv(c);
    char *ovf = c->messages[0].content_overflow;
    ai_conv_take(c, c);
    ASSERT_EQ(c->msg_count, 3);
    ASSERT_TRUE(c->messages[0].content_overflow == ovf);
    ai_conv_take(NULL, c);
    ai_conv_take(c, NULL);
    ASSERT_EQ(c->msg_count, 3);
    ai_conv_reset(c);
    free(c);
    TEST_END();
}

/* The reported sequence: default settings put the system prompt in an
 * overflow buffer; switching tabs away and back, then sending, freed it
 * twice. Model the panel's working copy, two sessions and the send-time
 * copy handed to the stream, repeating the switch several times. */
int test_ai_conv_take_repeated_switch_then_send(void)
{
    TEST_BEGIN();
    AiConversation *panel = calloc(1, sizeof(*panel));
    AiConversation *sess_a = calloc(1, sizeof(*sess_a));
    AiConversation *sess_b = calloc(1, sizeof(*sess_b));
    AiConversation *stream = calloc(1, sizeof(*stream));
    build_overflow_conv(panel);        /* session A is displayed */
    ai_conv_init(sess_a, "model-x");
    ai_conv_init(sess_b, "model-x");
    ai_conv_add(sess_b, AI_ROLE_USER, "b says hi");

    for (int round = 0; round < 5; round++) {
        /* A -> B */
        ai_conv_take(sess_a, panel);
        ai_conv_take(panel, sess_b);
        /* B -> A */
        ai_conv_take(sess_b, panel);
        ai_conv_take(panel, sess_a);
        /* No buffer is owned twice. */
        ASSERT_NOT_NULL(panel->messages[0].content_overflow);
        ASSERT_EQ(sess_a->msg_count, 0);
        ASSERT_NULL(sess_a->messages[0].content_overflow);
        ASSERT_EQ(sess_b->msg_count, 1);
    }

    /* Send: the stream gets its own deep copy. */
    ASSERT_EQ(ai_conv_copy(stream, panel), 0);
    ASSERT_TRUE(stream->messages[0].content_overflow !=
                panel->messages[0].content_overflow);
    ASSERT_TRUE(stream->messages[1].attachment != panel->messages[1].attachment);
    ASSERT_TRUE(stream->messages[2].tool_calls != panel->messages[2].tool_calls);
    ASSERT_STR_EQ(ai_msg_content(&stream->messages[0]),
                  ai_msg_content(&panel->messages[0]));
    ASSERT_STR_EQ(stream->messages[1].attachment->base64_url,
                  panel->messages[1].attachment->base64_url);
    ASSERT_STR_EQ(stream->messages[2].tool_calls[0].name, "web_search");

    /* Everything frees independently. */
    ai_conv_reset(stream);
    ai_conv_reset(panel);
    ai_conv_reset(sess_a);
    ai_conv_reset(sess_b);
    free(panel); free(sess_a); free(sess_b); free(stream);
    TEST_END();
}

int test_ai_conv_copy_replaces_destination(void)
{
    TEST_BEGIN();
    AiConversation *src = calloc(1, sizeof(*src));
    AiConversation *dst = calloc(1, sizeof(*dst));
    ai_conv_init(src, "m1");
    ai_conv_add(src, AI_ROLE_USER, "only");
    build_overflow_conv(dst);
    ASSERT_EQ(ai_conv_copy(dst, src), 0);
    ASSERT_EQ(dst->msg_count, 1);
    ASSERT_STR_EQ(dst->model, "m1");
    ASSERT_NULL(dst->messages[0].content_overflow);
    ASSERT_NULL(dst->messages[1].attachment);
    ASSERT_EQ(ai_conv_copy(dst, NULL), -1);
    ASSERT_EQ(ai_conv_copy(dst, dst), 0);   /* self-copy is a no-op */
    ASSERT_EQ(dst->msg_count, 1);
    ai_conv_reset(src);
    ai_conv_reset(dst);
    free(src); free(dst);
    TEST_END();
}

/* ---- worker_life ------------------------------------------------------- */

int test_worker_life_last_release_frees(void)
{
    TEST_BEGIN();
    WorkerLife l;
    worker_life_init(&l);
    ASSERT_EQ(worker_life_refs(&l), 2);
    ASSERT_FALSE(worker_life_cancelled(&l));
    ASSERT_EQ(worker_life_release(&l), 0);   /* worker finishes first */
    ASSERT_EQ(worker_life_release(&l), 1);   /* UI lets go last: it frees */
    TEST_END();
}

int test_worker_life_cancel_is_sticky(void)
{
    TEST_BEGIN();
    WorkerLife l;
    worker_life_init(&l);
    worker_life_cancel(&l);
    worker_life_cancel(&l);
    ASSERT_TRUE(worker_life_cancelled(&l));
    ASSERT_EQ(worker_life_release(&l), 0);   /* UI orphans it */
    ASSERT_TRUE(worker_life_cancelled(&l));
    ASSERT_EQ(worker_life_release(&l), 1);   /* the worker frees it */
    TEST_END();
}

int test_worker_life_ids_unique_nonzero(void)
{
    TEST_BEGIN();
    unsigned a = worker_life_next_id();
    unsigned b = worker_life_next_id();
    unsigned c = worker_life_next_id();
    ASSERT_TRUE(a != 0 && b != 0 && c != 0);
    ASSERT_TRUE(a != b && b != c && a != c);
    TEST_END();
}

/* ---- AiStream + AiStreamTable ------------------------------------------ */

int test_ai_stream_finish_before_close(void)
{
    TEST_BEGIN();
    int live0 = ai_stream_live_count();
    AiStreamTable t;
    ai_stream_table_init(&t);
    int owner_a = 0;
    AiStream *s = ai_stream_new();
    ASSERT_NOT_NULL(s);
    unsigned id = s->id;
    ASSERT_EQ(ai_stream_table_add(&t, s, &owner_a), 0);
    ASSERT_EQ(ai_stream_live_count(), live0 + 1);

    /* Worker posts its done message, then drops its reference. */
    ai_stream_release(s);
    ASSERT_EQ(ai_stream_live_count(), live0 + 1);   /* UI still holds it */

    /* UI handles the done message: the id still resolves to its owner. */
    void *owner = NULL;
    ASSERT_TRUE(ai_stream_table_find(&t, id, &owner) == s);
    ASSERT_TRUE(owner == &owner_a);
    ASSERT_EQ(ai_stream_table_finish(&t, id), 1);
    ASSERT_EQ(ai_stream_live_count(), live0);
    ASSERT_NULL(ai_stream_table_find(&t, id, NULL));

    /* Then the tab closes: nothing left to abort. */
    ASSERT_EQ(ai_stream_table_abort_owner(&t, &owner_a), 0);
    TEST_END();
}

int test_ai_stream_close_before_finish(void)
{
    TEST_BEGIN();
    int live0 = ai_stream_live_count();
    AiStreamTable t;
    ai_stream_table_init(&t);
    int owner_a = 0;
    AiStream *s = ai_stream_new();
    ASSERT_NOT_NULL(s);
    unsigned id = s->id;
    ai_stream_table_add(&t, s, &owner_a);

    /* Tab closes mid-reply: the stream is aborted and orphaned. */
    ASSERT_EQ(ai_stream_table_abort_owner(&t, &owner_a), 1);
    ASSERT_TRUE(ai_stream_aborted(s));      /* worker still holds it */
    ASSERT_EQ(ai_stream_live_count(), live0 + 1);
    ASSERT_NULL(ai_stream_table_find(&t, id, NULL));

    /* Its late done message finds nothing, so it touches no session. */
    ASSERT_EQ(ai_stream_table_finish(&t, id), 0);

    /* Worker notices the abort and drops the last reference itself. */
    ai_stream_release(s);
    ASSERT_EQ(ai_stream_live_count(), live0);
    TEST_END();
}

int test_ai_stream_double_close(void)
{
    TEST_BEGIN();
    int live0 = ai_stream_live_count();
    AiStreamTable t;
    ai_stream_table_init(&t);
    int owner_a = 0;
    AiStream *s = ai_stream_new();
    ai_stream_table_add(&t, s, &owner_a);
    ASSERT_EQ(ai_stream_table_abort_owner(&t, &owner_a), 1);
    ASSERT_EQ(ai_stream_table_abort_owner(&t, &owner_a), 0);
    ASSERT_EQ(ai_stream_table_abort_all(&t), 0);
    ASSERT_EQ(ai_stream_live_count(), live0 + 1);   /* not over-released */
    ai_stream_release(s);
    ASSERT_EQ(ai_stream_live_count(), live0);
    TEST_END();
}

int test_ai_stream_abort_then_late_chunk(void)
{
    TEST_BEGIN();
    AiStreamTable t;
    ai_stream_table_init(&t);
    int owner_a = 0;
    AiStream *s = ai_stream_new();
    unsigned id = s->id;
    ai_stream_table_add(&t, s, &owner_a);
    /* Stop pressed: the UI orphans the stream... */
    ai_stream_table_abort_owner(&t, &owner_a);
    /* ...and a chunk the worker had already posted arrives afterwards. */
    void *owner = &owner_a;
    ASSERT_NULL(ai_stream_table_find(&t, id, &owner));
    ASSERT_NULL(owner);
    ai_stream_release(s);
    TEST_END();
}

int test_ai_stream_stale_generation(void)
{
    TEST_BEGIN();
    int live0 = ai_stream_live_count();
    AiStreamTable t;
    ai_stream_table_init(&t);
    int owner_a = 0;
    AiStream *s1 = ai_stream_new();
    AiStream *s2 = ai_stream_new();
    unsigned id1 = s1->id, id2 = s2->id;
    ASSERT_TRUE(id1 != id2);
    ai_stream_table_add(&t, s1, &owner_a);
    /* Retry / resend in the same session: the new stream replaces the old
     * one, which is aborted and orphaned. */
    ai_stream_table_add(&t, s2, &owner_a);
    ASSERT_TRUE(ai_stream_aborted(s1));
    ASSERT_FALSE(ai_stream_aborted(s2));
    ASSERT_NULL(ai_stream_table_find(&t, id1, NULL));
    ASSERT_TRUE(ai_stream_table_find(&t, id2, NULL) == s2);
    ASSERT_TRUE(ai_stream_table_find_owner(&t, &owner_a) == s2);
    ASSERT_EQ(ai_stream_table_finish(&t, id1), 0);  /* stale done: ignored */
    ai_stream_release(s1);                           /* old worker exits */
    ai_stream_release(s2);
    ASSERT_EQ(ai_stream_table_finish(&t, id2), 1);
    ASSERT_EQ(ai_stream_live_count(), live0);
    TEST_END();
}

int test_ai_stream_abort_is_per_owner(void)
{
    TEST_BEGIN();
    AiStreamTable t;
    ai_stream_table_init(&t);
    int owner_a = 0, owner_b = 0;
    AiStream *a = ai_stream_new();
    AiStream *b = ai_stream_new();
    ai_stream_table_add(&t, a, &owner_a);
    ai_stream_table_add(&t, b, &owner_b);
    /* Stopping tab A's reply leaves tab B's running. */
    ASSERT_EQ(ai_stream_table_abort_owner(&t, &owner_a), 1);
    ASSERT_TRUE(ai_stream_aborted(a));
    ASSERT_FALSE(ai_stream_aborted(b));
    void *owner = NULL;
    ASSERT_TRUE(ai_stream_table_find(&t, b->id, &owner) == b);
    ASSERT_TRUE(owner == &owner_b);
    ai_stream_release(a);
    ai_stream_release(b);
    ai_stream_table_finish(&t, b->id);
    TEST_END();
}

int test_ai_stream_abort_all_on_panel_close(void)
{
    TEST_BEGIN();
    int live0 = ai_stream_live_count();
    AiStreamTable t;
    ai_stream_table_init(&t);
    int owners[3] = {0, 0, 0};
    AiStream *s[3];
    for (int i = 0; i < 3; i++) {
        s[i] = ai_stream_new();
        ai_stream_table_add(&t, s[i], &owners[i]);
    }
    ASSERT_TRUE(ai_stream_table_owner_at(&t, 0) != NULL);
    ASSERT_NULL(ai_stream_table_owner_at(&t, 3));
    ASSERT_EQ(ai_stream_table_abort_all(&t), 3);
    ASSERT_EQ(t.count, 0);
    for (int i = 0; i < 3; i++) ASSERT_TRUE(ai_stream_aborted(s[i]));
    ASSERT_EQ(ai_stream_live_count(), live0 + 3);   /* workers still out */
    for (int i = 0; i < 3; i++) ai_stream_release(s[i]);
    ASSERT_EQ(ai_stream_live_count(), live0);
    TEST_END();
}

int test_ai_stream_table_full_and_bad_args(void)
{
    TEST_BEGIN();
    AiStreamTable t;
    ai_stream_table_init(&t);
    static int owners[AI_STREAM_TABLE_MAX + 1];
    AiStream *all[AI_STREAM_TABLE_MAX];
    for (int i = 0; i < AI_STREAM_TABLE_MAX; i++) {
        all[i] = ai_stream_new();
        ASSERT_EQ(ai_stream_table_add(&t, all[i], &owners[i]), 0);
    }
    AiStream *extra = ai_stream_new();
    ASSERT_EQ(ai_stream_table_add(&t, extra, &owners[AI_STREAM_TABLE_MAX]), -1);
    ASSERT_FALSE(ai_stream_aborted(extra));   /* refused, not touched */
    ai_stream_discard(extra);
    ASSERT_EQ(ai_stream_table_add(&t, NULL, &owners[0]), -1);
    ASSERT_EQ(ai_stream_table_add(NULL, NULL, NULL), -1);
    ASSERT_NULL(ai_stream_table_find(&t, 0, NULL));
    ASSERT_EQ(ai_stream_table_abort_all(&t), AI_STREAM_TABLE_MAX);
    for (int i = 0; i < AI_STREAM_TABLE_MAX; i++) ai_stream_release(all[i]);
    TEST_END();
}

static int dummy_exec(const char *in, void *data, volatile int *cancel,
                      char **out, size_t *out_len, int *trunc)
{
    (void)in; (void)data; (void)cancel; (void)out; (void)out_len; (void)trunc;
    return 0;
}

int test_ai_stream_copy_tools_rebinds_context(void)
{
    TEST_BEGIN();
    AiToolRegistry reg;
    ai_tools_init(&reg);
    WebSearchContext search;
    WebFetchContext fetch;
    memset(&search, 0, sizeof(search));
    memset(&fetch, 0, sizeof(fetch));
    strcpy(search.search_provider, "duckduckgo-api");
    fetch.timeout_ms = 1234;
    AiToolDef def;
    memset(&def, 0, sizeof(def));
    strcpy(def.name, "web_search");
    def.execute = dummy_exec;
    def.tool_data = &search;
    ai_tools_register(&reg, &def);
    strcpy(def.name, "web_fetch");
    def.tool_data = &fetch;
    ai_tools_register(&reg, &def);

    AiStream *s = ai_stream_new();
    ai_stream_copy_tools(s, &reg, 7u, &search, &fetch);
    ASSERT_EQ(s->tools.count, 2);
    ASSERT_EQ(s->tools_gen, 7u);
    ASSERT_TRUE(s->tools.tools[0].tool_data == &s->search);
    ASSERT_TRUE(s->tools.tools[1].tool_data == &s->fetch);
    ASSERT_STR_EQ(s->search.search_provider, "duckduckgo-api");
    ASSERT_EQ(s->fetch.timeout_ms, 1234);

    /* A settings change after launch does not reach the running stream. */
    strcpy(search.search_provider, "custom");
    reg.count = 0;
    ASSERT_STR_EQ(s->search.search_provider, "duckduckgo-api");
    ASSERT_EQ(s->tools.count, 2);

    ai_stream_discard(s);
    TEST_END();
}

int test_ai_stream_release_frees_conversation(void)
{
    TEST_BEGIN();
    int live0 = ai_stream_live_count();
    AiStream *s = ai_stream_new();
    build_overflow_conv(&s->conv);
    strcpy(s->api_key, "secret");
    ai_stream_orphan(s);          /* UI side */
    ASSERT_EQ(ai_stream_live_count(), live0 + 1);
    ai_stream_release(s);         /* worker side: frees conv buffers too */
    ASSERT_EQ(ai_stream_live_count(), live0);
    ai_stream_release(NULL);
    ai_stream_orphan(NULL);
    ai_stream_discard(NULL);
    TEST_END();
}

/* A thread whose final message never reached the panel (allocation or
 * PostMessage failure) must not leave its session busy for ever: the panel
 * finds it through ai_stream_table_lost_owner(). */
int test_ai_stream_lost_final_message_is_found(void)
{
    TEST_BEGIN();
    AiStreamTable t;
    ai_stream_table_init(&t);
    int owner_a = 0, owner_b = 0, owner_c = 0;
    AiStream *a = ai_stream_new();   /* still running */
    AiStream *b = ai_stream_new();   /* finished, final message posted */
    AiStream *c = ai_stream_new();   /* finished, final message lost */
    ai_stream_table_add(&t, a, &owner_a);
    ai_stream_table_add(&t, b, &owner_b);
    ai_stream_table_add(&t, c, &owner_c);
    ASSERT_NULL(ai_stream_table_lost_owner(&t));

    ai_stream_note_delivered(b);
    ai_stream_mark_finished(b);
    ai_stream_release(b);
    ASSERT_NULL(ai_stream_table_lost_owner(&t));   /* its message is coming */

    ai_stream_mark_finished(c);
    ai_stream_release(c);
    ASSERT_TRUE(ai_stream_table_lost_owner(&t) == &owner_c);
    ASSERT_EQ(ai_stream_table_abort_owner(&t, &owner_c), 1);
    ASSERT_NULL(ai_stream_table_lost_owner(&t));

    ai_stream_table_finish(&t, b->id);
    ai_stream_release(a);
    ai_stream_table_abort_all(&t);
    ai_stream_mark_finished(NULL);
    ai_stream_note_delivered(NULL);
    ASSERT_NULL(ai_stream_table_lost_owner(NULL));
    TEST_END();
}
