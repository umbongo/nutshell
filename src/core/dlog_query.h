/* src/core/dlog_query.h -- reading the debug log back: the engine behind the
 * AI Assist tool `read_debug_log` (and tests). It filters complete lines from
 * the run's log files (or the in-memory recorder when there is no file), keeps
 * the newest that fit, and caps the output so the result stays cheap in the
 * conversation (the maintainer's AI key has limited credit).
 *
 * The tool contract (name, schema, limits) lives here so the UI only wires it
 * up: register DLOG_TOOL_NAME with dlog_tool_description() and
 * dlog_tool_schema(), run dlog_query_from_json() on the model's arguments and
 * dlog_read_log() on the worker thread, show dlog_query_describe() in the
 * "using tool:" row and the first output line (the `# ...` header) as the
 * result row. TOOL_SAFE: read-only, no approval.
 *
 * Threads: dlog_read_log() may run on any thread (it flushes the writer and
 * reads files opened with full sharing). A file being appended to is read only
 * up to its last complete line.
 */
#ifndef NUTSHELL_CORE_DLOG_QUERY_H
#define NUTSHELL_CORE_DLOG_QUERY_H

#include <stddef.h>
#include "dlog_files.h"

#define DLOG_TOOL_NAME              "read_debug_log"
#define DLOG_TOOL_MAX_CALLS_PER_MSG 4        /* per user message (agentic_check_rate_limit) */
#define DLOG_QUERY_OUT_MAX          16384    /* the whole result, header included */
#define DLOG_QUERY_LINE_MAX         512      /* a longer line is cut with "+N bytes" */
#define DLOG_QUERY_DEFAULT_MINUTES  30
#define DLOG_QUERY_DEFAULT_LINES    200

typedef struct {
    int       since_minutes;   /* 0 = no time limit; from JSON: 1-1440, default 30 */
    int       category;        /* DlogCat, -1 = any */
    int       min_level;       /* most verbose DlogEvLevel shown (1 error .. 5 trace); 0 = no limit */
    char      contains[132];   /* case-insensitive substring of the line, "" = none */
    unsigned  session;         /* sid, 0 = any */
    int       around_issue;    /* -1 = off; 0 = the latest issue mark; n = issue n */
    int       summary;         /* counts only, no lines */
    int       max_lines;       /* 0 = DLOG_QUERY_DEFAULT_LINES; clamped to 1..1000 */
    long long now_ms;          /* reference time for since_minutes; 0 = the platform clock (or "no limit" without one) */
} DlogQuery;

void dlog_query_defaults(DlogQuery *q);

/* The model's tool arguments. NULL, "" and "{}" mean the defaults (the last 30
 * minutes, 200 lines). An unknown key is ignored; a wrong type, an unknown
 * category or level, or a bad session is refused with a short message in
 * err. Numbers are clamped. Returns 1 or 0. */
int  dlog_query_from_json(const char *json, DlogQuery *q, char *err, size_t err_cap);

/* "render, last 30 min", "summary", "around issue 2", ... for the chat row. */
void dlog_query_describe(const DlogQuery *q, char *out, size_t cap);

/* Tool metadata for registration. */
const char *dlog_tool_description(void);
const char *dlog_tool_schema(void);        /* the JSON "parameters" object */

/* Where lines come from. list() fills the folder's entries (names like
 * nutshell-...); read() returns bytes at an offset (0 at the end, -1 on error). */
typedef struct {
    int  (*list)(void *ctx, DlogFileInfo *out, size_t cap);
    long (*read)(void *ctx, const char *name, unsigned long long off, void *buf, size_t cap);
    void *ctx;
} DlogQueryIO;

/* Run a query over files. The result is NUL-terminated text: a `# ...` header
 * line, then (unless summary) the matching lines oldest first, at most
 * min(cap - 1, DLOG_QUERY_OUT_MAX) bytes, keeping the newest lines that fit.
 * Returns the length. */
size_t dlog_query_run(const DlogQuery *q, const DlogQueryIO *io, char *out, size_t cap);

/* The same over one in-memory text (the recorder, or a fixture). Only complete
 * lines (ending in LF) are used. */
size_t dlog_query_text(const DlogQuery *q, const char *text, size_t len, char *out, size_t cap);

/* The live log: dlog_flush_wait(200), then the folder in use through the
 * platform; with no file yet, the recorder. */
size_t dlog_read_log(const DlogQuery *q, char *out, size_t cap);

/* One parsed line prefix (exposed for tests). */
typedef struct {
    long long ms;
    int       ev_level;      /* DlogEvLevel */
    int       cat;           /* DlogCat */
    unsigned  sid;
    const char *event;       /* points into the line */
    size_t    event_len;
} DlogLineInfo;
int  dlog_line_parse(const char *line, size_t len, DlogLineInfo *li);

#endif
