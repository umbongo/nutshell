/* src/core/ai_stream.h */
#ifndef NUTSHELL_AI_STREAM_H
#define NUTSHELL_AI_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include "ai_prompt.h"
#include "ai_tools.h"
#include "ai_tool_web_search.h"
#include "ai_tool_web_fetch.h"
#include "worker_life.h"

/*
 * ai_stream -- one AI reply in flight, and the panel's table of them.
 *
 * An AiStream carries everything the streaming thread reads or writes:
 * copies of the key, provider and URL, the request body, a snapshot of the
 * tool registry (with its own copies of the tools' context structs), and
 * its own deep copy of the conversation, which the agentic loop appends
 * tool calls and results to. The thread never touches the AI panel or a
 * session. It reports through posted messages that carry the stream's id;
 * the panel resolves the id in its AiStreamTable, and a message whose id
 * is gone (the reply was stopped, retried, its tab closed, or the panel
 * rebuilt) is dropped without touching anything.
 *
 * Lifetime is a WorkerLife: one reference for the panel (held by the
 * table), one for the thread. Stopping, closing a tab, undocking and
 * exiting abort the stream and orphan it; the thread notices the abort
 * flag, drops the last reference and frees the stream itself.
 *
 * Pure, portable, no windows.h -- tested natively (tests/test_ai_stream.c).
 */

typedef struct AiStream {
    WorkerLife life;
    unsigned   id;             /* process-unique, never 0 */
    volatile int finished;     /* set by the thread on its way out */
    volatile int delivered;    /* set by the thread once its final (done or
                                * error) message has been posted */
    uintptr_t  post_target;    /* where the thread posts (an HWND on Win32) */

    /* Inputs, copied at launch */
    char   api_key[256];
    char   provider[64];
    char   custom_url[256];
    char  *body;               /* AI_BODY_MAX bytes, rebuilt by the tool loop */
    size_t body_len;

    /* Tool snapshot: tools[].tool_data points at search/fetch below, never
     * at the panel's own copies, so a settings change or a closed panel
     * cannot reach a running tool. */
    int              has_tools;
    unsigned         tools_gen;   /* the panel's registry generation it came from */
    AiToolRegistry   tools;
    WebSearchContext search;
    WebFetchContext  fetch;
    char             tools_json[AI_TOOL_SCHEMA_MAX * AI_TOOL_MAX];

    /* The conversation this reply continues. The thread owns it while it
     * runs; once the panel has the stream's done message it may take it
     * with ai_conv_take(). */
    AiConversation conv;
} AiStream;

/* Allocate a stream with both references held, a fresh id and an empty
 * conversation. NULL on allocation failure. */
AiStream *ai_stream_new(void);

/* Free a stream that was never handed to a thread or a table. */
void ai_stream_discard(AiStream *s);

/* Drop one side's reference; the last one frees the stream (its api key is
 * wiped and its conversation freed). NULL is a no-op. */
void ai_stream_release(AiStream *s);

/* Abort, then drop the UI's reference: the thread frees it when it sees
 * the abort (or right now, if it has already finished). */
void ai_stream_orphan(AiStream *s);

/* Non-zero once the stream has been aborted. */
int ai_stream_aborted(const AiStream *s);

/* The abort flag, for APIs that poll a volatile int (ai_tool_execute). */
volatile int *ai_stream_abort_flag(AiStream *s);

/* Streams allocated and not yet freed, process-wide. The app waits a
 * bounded time for this to reach 0 on exit. */
int ai_stream_live_count(void);

/* Snapshot a tool registry into the stream. Any tool whose tool_data is
 * `search` or `fetch` is rebound to the stream's own copy of that context;
 * other tool_data pointers are kept as they are, so they must outlive the
 * stream (static data). Either context may be NULL. */
void ai_stream_copy_tools(AiStream *s, const AiToolRegistry *reg,
                          unsigned gen, const WebSearchContext *search,
                          const WebFetchContext *fetch);

/* Thread side: its final done/error message was posted successfully. */
void ai_stream_note_delivered(AiStream *s);

/* Thread side: called once on the way out, before its release. */
void ai_stream_mark_finished(AiStream *s);

/* ---- The panel's table of streams in flight (UI thread only) --------- */

#define AI_STREAM_TABLE_MAX 32

typedef struct {
    AiStream *stream;
    void     *owner;     /* the session this reply belongs to */
} AiStreamSlot;

typedef struct {
    AiStreamSlot slot[AI_STREAM_TABLE_MAX];
    int count;
} AiStreamTable;

void ai_stream_table_init(AiStreamTable *t);

/* Register a launched stream for `owner`; the table now holds the UI's
 * reference. A stream the owner already had is aborted and orphaned first
 * -- one reply per session, and a retry's late messages from the old
 * stream are stale. Returns 0, or -1 (table full or bad arguments: the
 * stream is left untouched and still belongs to the caller). */
int ai_stream_table_add(AiStreamTable *t, AiStream *s, void *owner);

/* Resolve a message's stream id. Returns the live stream and sets *owner
 * (if owner_out is non-NULL), or returns NULL and sets *owner to NULL when
 * the id is 0 or no longer registered -- the message is stale. */
AiStream *ai_stream_table_find(const AiStreamTable *t, unsigned id,
                               void **owner_out);

/* The stream currently running for `owner`, or NULL. */
AiStream *ai_stream_table_find_owner(const AiStreamTable *t,
                                     const void *owner);

/* The done message for `id` has been handled: unregister it and drop the
 * UI's reference. Returns 1 if it was registered, 0 if stale. */
int ai_stream_table_finish(AiStreamTable *t, unsigned id);

/* Abort and orphan every stream `owner` has (tab closed, Stop, New Chat).
 * Returns how many. */
int ai_stream_table_abort_owner(AiStreamTable *t, const void *owner);

/* Abort and orphan every stream (panel closed or undocked, app exit).
 * Returns how many. */
int ai_stream_table_abort_all(AiStreamTable *t);

/* The owner of a registered stream whose thread has finished without
 * delivering its final message (an allocation or PostMessage failure), or
 * NULL. Nothing else will ever end that session's reply, so the panel
 * polls this while streams are registered and resets such a session. */
void *ai_stream_table_lost_owner(const AiStreamTable *t);

/* The owner in slot i (0 <= i < count), or NULL -- lets the panel reset
 * each owner's busy state before abort_all. */
void *ai_stream_table_owner_at(const AiStreamTable *t, int i);

#endif /* NUTSHELL_AI_STREAM_H */
