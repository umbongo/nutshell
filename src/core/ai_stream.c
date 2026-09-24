/* src/core/ai_stream.c -- see ai_stream.h */
#include "ai_stream.h"
#include "secure_zero.h"
#include <stdlib.h>
#include <string.h>

static volatile int g_live_streams = 0;

AiStream *ai_stream_new(void)
{
    AiStream *s = (AiStream *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->body = (char *)calloc(1, AI_BODY_MAX);
    if (!s->body) { free(s); return NULL; }
    worker_life_init(&s->life);
    s->id = worker_life_next_id();
    __atomic_add_fetch(&g_live_streams, 1, __ATOMIC_SEQ_CST);
    return s;
}

static void stream_free(AiStream *s)
{
    ai_conv_reset(&s->conv);
    secure_zero(s->api_key, sizeof(s->api_key));
    free(s->body);
    free(s);
    __atomic_sub_fetch(&g_live_streams, 1, __ATOMIC_SEQ_CST);
}

void ai_stream_discard(AiStream *s)
{
    if (s) stream_free(s);
}

void ai_stream_release(AiStream *s)
{
    if (s && worker_life_release(&s->life))
        stream_free(s);
}

void ai_stream_orphan(AiStream *s)
{
    if (!s) return;
    worker_life_cancel(&s->life);
    ai_stream_release(s);
}

int ai_stream_aborted(const AiStream *s)
{
    return s ? worker_life_cancelled(&s->life) : 1;
}

volatile int *ai_stream_abort_flag(AiStream *s)
{
    return s ? &s->life.cancelled : NULL;
}

void ai_stream_note_delivered(AiStream *s)
{
    if (s) __atomic_store_n(&s->delivered, 1, __ATOMIC_SEQ_CST);
}

void ai_stream_mark_finished(AiStream *s)
{
    if (s) __atomic_store_n(&s->finished, 1, __ATOMIC_SEQ_CST);
}

int ai_stream_live_count(void)
{
    return __atomic_load_n(&g_live_streams, __ATOMIC_SEQ_CST);
}

void ai_stream_copy_tools(AiStream *s, const AiToolRegistry *reg,
                          unsigned gen, const WebSearchContext *search,
                          const WebFetchContext *fetch)
{
    if (!s) return;
    ai_tools_init(&s->tools);
    s->tools_gen = gen;
    if (search) s->search = *search;
    if (fetch)  s->fetch  = *fetch;
    if (!reg) return;
    s->tools = *reg;
    for (int i = 0; i < s->tools.count && i < AI_TOOL_MAX; i++) {
        AiToolDef *t = &s->tools.tools[i];
        if (search && t->tool_data == (const void *)search)
            t->tool_data = &s->search;
        else if (fetch && t->tool_data == (const void *)fetch)
            t->tool_data = &s->fetch;
    }
}

/* ---- Table ------------------------------------------------------------ */

void ai_stream_table_init(AiStreamTable *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

static void table_remove_at(AiStreamTable *t, int i)
{
    for (int j = i; j < t->count - 1; j++)
        t->slot[j] = t->slot[j + 1];
    t->count--;
    t->slot[t->count].stream = NULL;
    t->slot[t->count].owner = NULL;
}

int ai_stream_table_add(AiStreamTable *t, AiStream *s, void *owner)
{
    if (!t || !s) return -1;
    /* Check for room before touching the owner's old stream: a refusal
     * must leave everything as it was. Replacing counts as room. */
    int replacing = ai_stream_table_find_owner(t, owner) != NULL;
    if (!replacing && t->count >= AI_STREAM_TABLE_MAX) return -1;
    ai_stream_table_abort_owner(t, owner);
    t->slot[t->count].stream = s;
    t->slot[t->count].owner = owner;
    t->count++;
    return 0;
}

AiStream *ai_stream_table_find(const AiStreamTable *t, unsigned id,
                               void **owner_out)
{
    if (owner_out) *owner_out = NULL;
    if (!t || id == 0u) return NULL;
    for (int i = 0; i < t->count; i++) {
        if (t->slot[i].stream && t->slot[i].stream->id == id) {
            if (owner_out) *owner_out = t->slot[i].owner;
            return t->slot[i].stream;
        }
    }
    return NULL;
}

AiStream *ai_stream_table_find_owner(const AiStreamTable *t,
                                     const void *owner)
{
    if (!t) return NULL;
    for (int i = 0; i < t->count; i++)
        if (t->slot[i].owner == owner) return t->slot[i].stream;
    return NULL;
}

int ai_stream_table_finish(AiStreamTable *t, unsigned id)
{
    if (!t || id == 0u) return 0;
    for (int i = 0; i < t->count; i++) {
        AiStream *s = t->slot[i].stream;
        if (s && s->id == id) {
            table_remove_at(t, i);
            ai_stream_release(s);
            return 1;
        }
    }
    return 0;
}

int ai_stream_table_abort_owner(AiStreamTable *t, const void *owner)
{
    if (!t) return 0;
    int n = 0;
    for (int i = 0; i < t->count; ) {
        if (t->slot[i].owner == owner) {
            AiStream *s = t->slot[i].stream;
            table_remove_at(t, i);
            ai_stream_orphan(s);
            n++;
        } else {
            i++;
        }
    }
    return n;
}

int ai_stream_table_abort_all(AiStreamTable *t)
{
    if (!t) return 0;
    int n = t->count;
    while (t->count > 0) {
        AiStream *s = t->slot[t->count - 1].stream;
        table_remove_at(t, t->count - 1);
        ai_stream_orphan(s);
    }
    return n;
}

void *ai_stream_table_lost_owner(const AiStreamTable *t)
{
    if (!t) return NULL;
    for (int i = 0; i < t->count; i++) {
        const AiStream *s = t->slot[i].stream;
        if (s && __atomic_load_n(&s->finished, __ATOMIC_SEQ_CST) &&
            !__atomic_load_n(&s->delivered, __ATOMIC_SEQ_CST))
            return t->slot[i].owner;
    }
    return NULL;
}

void *ai_stream_table_owner_at(const AiStreamTable *t, int i)
{
    if (!t || i < 0 || i >= t->count) return NULL;
    return t->slot[i].owner;
}
