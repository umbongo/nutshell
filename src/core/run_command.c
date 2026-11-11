#include "run_command.h"
#include "json_parser.h"
#include "paste_filter.h"
#include "term_extract.h"
#include "tool_decision.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNCMD_DESCRIPTION \
    "Run one command in the user's terminal session (the remote host, or this " \
    "PC for a local shell). The user approves it on a card first. The result " \
    "is its output as the terminal shows it, or why it did not run. Call it " \
    "once per command, all in the same reply, and call no other tool in that " \
    "reply."

#define RUNCMD_SCHEMA \
    "{\"type\":\"object\",\"properties\":{" \
    "\"command\":{\"type\":\"string\",\"description\":" \
    "\"one single-line command, at most 1023 bytes\"}," \
    "\"note\":{\"type\":\"string\",\"description\":" \
    "\"one line for the user: what the command does and why\"}}," \
    "\"required\":[\"command\"]}"

#define RUNCMD_STUB_TEXT "run_command is handled by the panel"

/* The panel intercepts the call by name before the registry runs it. This is
 * the fail-safe for a missed intercept: the call has had its card, and then
 * it fails instead of running. The text is Nutshell's own (tool_note). */
static int runcmd_stub_execute(const char *input_json, void *tool_data,
                               volatile int *cancel_flag,
                               char **result_buf, size_t *result_len,
                               int *was_truncated)
{
    (void)input_json;
    (void)tool_data;
    (void)cancel_flag;
    if (!result_buf || !result_len || !was_truncated) return -1;
    *result_buf = NULL;
    *result_len = 0;
    *was_truncated = 0;
    size_t n = strlen(RUNCMD_STUB_TEXT);
    char *p = (char *)malloc(n + 1);
    if (p) {
        memcpy(p, RUNCMD_STUB_TEXT, n + 1);
        *result_buf = p;
        *result_len = n;
    }
    return -1;
}

void runcmd_tool_def(AiToolDef *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", RUNCMD_TOOL_NAME);
    snprintf(out->description, sizeof(out->description), "%s", RUNCMD_DESCRIPTION);
    snprintf(out->input_schema_json, sizeof(out->input_schema_json), "%s", RUNCMD_SCHEMA);
    out->safety = TOOL_CRITICAL;
    out->execute = runcmd_stub_execute;
    out->tool_data = NULL;
}

int runcmd_register(AiToolRegistry *reg)
{
    if (!reg) return -1;
    AiToolDef def;
    runcmd_tool_def(&def);
    return ai_tools_register(reg, &def);
}

static int runcmd_is_blank(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != '\t') return 0;
    return 1;
}

RunCmdParse runcmd_parse_input(const char *input_json,
                               char command[RUNCMD_COMMAND_MAX + 1],
                               char note[RUNCMD_NOTE_MAX])
{
    command[0] = '\0';
    note[0] = '\0';
    if (!input_json || !input_json[0]) return RUNCMD_PARSE_NO_COMMAND;

    JsonNode *root = json_parse(input_json);
    if (!root) return RUNCMD_PARSE_NO_COMMAND;
    if (root->type != JSON_OBJECT) {
        json_free(root);
        return RUNCMD_PARSE_NO_COMMAND;
    }

    const char *cmd = json_obj_str(root, "command");
    if (!cmd || runcmd_is_blank(cmd)) {
        json_free(root);
        return RUNCMD_PARSE_NO_COMMAND;
    }
    size_t len = strlen(cmd);
    if (len > (size_t)RUNCMD_COMMAND_MAX) {
        json_free(root);
        return RUNCMD_PARSE_TOO_LONG;
    }
    if (text_has_unsafe_command_char(cmd)) {
        json_free(root);
        return RUNCMD_PARSE_UNSAFE;
    }
    memcpy(command, cmd, len + 1);

    const char *nt = json_obj_str(root, "note");
    if (nt) {
        size_t n = strlen(nt);
        if (n > (size_t)RUNCMD_NOTE_MAX - 1) {
            n = (size_t)RUNCMD_NOTE_MAX - 1;
            /* nt[n] is the first byte dropped: a continuation byte there
             * means the cut fell inside a character, so back up to its start */
            while (n > 0 && ((unsigned char)nt[n] & 0xC0u) == 0x80u) n--;
        }
        memcpy(note, nt, n);
        note[n] = '\0';
        if (text_has_unsafe_command_char(note)) note[0] = '\0';
    }

    json_free(root);
    return RUNCMD_PARSE_OK;
}

const char *runcmd_parse_error(RunCmdParse p)
{
    switch (p) {
    case RUNCMD_PARSE_NO_COMMAND:
        return "run_command needs a \"command\" string";
    case RUNCMD_PARSE_TOO_LONG:
        return "command longer than 1023 bytes: break the work into smaller steps";
    case RUNCMD_PARSE_UNSAFE:
        return "command contains control or bidirectional formatting characters: "
               "send one single-line command";
    case RUNCMD_PARSE_OK:
    default:
        return "";
    }
}

int runcmd_describe(const char *input_json, char *out, size_t cap)
{
    char command[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    RunCmdParse p = runcmd_parse_input(input_json, command, note);

    if (out && cap > 0) {
        out[0] = '\0';
        if (p == RUNCMD_PARSE_OK) {
            size_t n = strlen(command);
            if (n > cap - 1) {
                n = cap - 1;
                while (n > 0 && ((unsigned char)command[n] & 0xC0u) == 0x80u) n--;
            }
            memcpy(out, command, n);
            out[n] = '\0';
        }
    }
    return p == RUNCMD_PARSE_OK ? 1 : 0;
}

/* ASCII case-insensitive substring test (strcasestr is not C11). */
static int contains_nocase(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i]) {
            char c = hay[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != needle[i]) break;
            i++;
        }
        if (i == nl) return 1;
    }
    return 0;
}

int runcmd_reject_hint_applies(int http_status, const char *error_text)
{
    if (http_status != 400 && http_status != 422) return 0;
    if (!error_text) return 0;
    return contains_nocase(error_text, "tool")
        || contains_nocase(error_text, "function")
        || contains_nocase(error_text, "reasoning_content");
}

const char *runcmd_reject_hint(void)
{
    return "[If this provider does not accept Nutshell's tool calls, set Settings > "
           "AI Assistant > Provider > Commands as tool calls to Off]";
}

/* ---- The planner ---- */

static void *(*g_alloc)(size_t) = malloc;

void runcmd_set_alloc_for_test(void *(*alloc)(size_t))
{
    g_alloc = alloc ? alloc : malloc;
}

RunCmdReply *runcmd_plan(const AiToolCall *calls, int n_calls,
                         int fetch_registered)
{
    if (!calls || n_calls <= 0) return NULL;

    RunCmdReply *r = g_alloc(sizeof(*r));
    if (!r) return NULL;
    r->calls = g_alloc((size_t)n_calls * sizeof(*r->calls));
    if (!r->calls) {
        free(r);
        return NULL;
    }
    memset(r->calls, 0, (size_t)n_calls * sizeof(*r->calls));
    r->n_calls = n_calls;
    r->n_slot = 0;

    for (int i = 0; i < n_calls; i++) {
        RunCmdCall *c = &r->calls[i];
        snprintf(c->tool_use_id, sizeof(c->tool_use_id), "%s", calls[i].id);
        snprintf(c->tool_name, sizeof(c->tool_name), "%s", calls[i].name);
        c->entry = -1;
        c->parse = RUNCMD_PARSE_OK;

        if (strncmp(calls[i].name, RUNCMD_TOOL_NAME, AI_TOOL_NAME_MAX) != 0) {
            c->kind = RUNCMD_KIND_OTHER_TOOL;
            c->fetch_off = tool_dispatch_rule(calls[i].name, fetch_registered)
                           == TOOL_DISPATCH_FETCH_OFF;
            continue;
        }
        c->parse = runcmd_parse_input(calls[i].input_json, c->command, c->note);
        if (c->parse != RUNCMD_PARSE_OK)
            c->kind = RUNCMD_KIND_INVALID;
        else if (r->n_slot >= RUNCMD_SLOT_MAX)
            c->kind = RUNCMD_KIND_OVER_CAP;
        else {
            c->kind = RUNCMD_KIND_SLOT;
            r->n_slot++;
        }
    }
    return r;
}

void runcmd_reply_free(RunCmdReply *r)
{
    if (!r) return;
    for (int i = 0; i < r->n_calls; i++) {
        free(r->calls[i].cap.head);
        free(r->calls[i].cap.tail);
    }
    free(r->calls);
    free(r);
}

RunCmdCall *runcmd_call_for_entry(RunCmdReply *r, int entry)
{
    if (!r || entry < 0) return NULL;
    for (int i = 0; i < r->n_calls; i++)
        if (r->calls[i].entry == entry) return &r->calls[i];
    return NULL;
}

int runcmd_reply_has_call(const AiToolCall *calls, int n_calls)
{
    if (!calls) return 0;
    for (int i = 0; i < n_calls; i++)
        if (strncmp(calls[i].name, RUNCMD_TOOL_NAME, AI_TOOL_NAME_MAX) == 0) return 1;
    return 0;
}

/* ---- Cap, cut and capture (spec decisions 10 and 11) ---- */

#define RUNCMD_CAP_MAX 16384u
#define RUNCMD_CAP_BUDGET 65536u

size_t runcmd_cap_bytes(int n_slot)
{
    if (n_slot <= 0) return RUNCMD_CAP_MAX;
    size_t share = RUNCMD_CAP_BUDGET / (size_t)n_slot;
    if (share > RUNCMD_CAP_MAX) share = RUNCMD_CAP_MAX;
    return share ? share : 1;
}

static void cap_clear_excerpt(RunCmdCapture *c)
{
    free(c->head);
    free(c->tail);
    c->head = c->tail = NULL;
    c->head_len = c->tail_len = 0;
    c->omitted_bytes = 0;
}

/* A NUL-terminated copy of text[0, n) through the planner's allocator. */
static char *cut_copy(const char *text, size_t n)
{
    char *p = (char *)g_alloc(n + 1);
    if (!p) return NULL;
    memcpy(p, text, n);
    p[n] = '\0';
    return p;
}

void runcmd_cut(const char *text, size_t len, size_t cap, RunCmdCapture *c)
{
    if (!c) return;
    cap_clear_excerpt(c);
    if (!text || len == 0) return;

    if (len <= cap) {
        c->head = cut_copy(text, len);
        if (!c->head) { c->omitted_bytes = len; return; }
        c->head_len = len;
        return;
    }

    size_t tail_want = cap / 4;
    size_t head_want = tail_want * 3 + (cap % 4) * 3 / 4;
    size_t head_len = head_want;
    while (head_len > 0 && ((unsigned char)text[head_len] & 0xC0u) == 0x80u)
        head_len--;
    size_t tail_start = len - tail_want;
    while (tail_start < len && ((unsigned char)text[tail_start] & 0xC0u) == 0x80u)
        tail_start++;
    size_t tail_len = len - tail_start;

    char *head = NULL, *tail = NULL;
    if (head_len > 0) {
        head = cut_copy(text, head_len);
        if (!head) { c->omitted_bytes = len; return; }
    }
    if (tail_len > 0) {
        tail = cut_copy(text + tail_start, tail_len);
        if (!tail) { free(head); c->omitted_bytes = len; return; }
    }
    c->head = head;
    c->head_len = head_len;
    c->tail = tail;
    c->tail_len = tail_len;
    c->omitted_bytes = len - head_len - tail_len;
}

void runcmd_capture_start(RunCmdCapture *c, const Terminal *t)
{
    if (!c || !t) return;
    c->start_line = term_abs_cursor_line(t);
    c->start_reflow_seq = t->reflow_seq;
    c->start_region_seq = t->region_scroll_seq;
    c->start_alt = t->alt_screen_active ? 1 : 0;
    c->started = 1;
}

int runcmd_capture_take(RunCmdCapture *c, const Terminal *t, const char *command,
                        size_t cap)
{
    if (!c || !t || !c->started || c->captured) return 0;
    c->captured = 1;

    unsigned flags = 0;
    size_t len = 0;
    char *text = NULL;
    uint64_t end = term_abs_cursor_line(t);

    int screen = c->start_alt || t->alt_screen_active ||
                 t->reflow_seq != c->start_reflow_seq ||
                 t->region_scroll_seq != c->start_region_seq;
    int redrawn = 0;
    if (!screen && end <= c->start_line) screen = redrawn = 1;

    /* A NULL text is an allocation failure, never "no output": the range
     * reader returns "" when nothing is left. The screen reader returns NULL
     * for a blank screen too, so a one-row probe (it fills a tiny buffer from
     * the same rows, writing at least a byte exactly when any row has
     * content) tells the two apart. */
    int oom = 0;
    if (screen) {
        flags |= RUNCMD_CAP_SCREEN;
        text = term_extract_last_n_dup(t, t->rows, &len);
        if (!text) {
            char probe[8];
            oom = term_extract_last_n(t, t->rows, probe, sizeof probe) > 0;
        }
        if (redrawn && (oom || (text && len > 0))) flags |= RUNCMD_CAP_REDRAWN;
    } else {
        unsigned range_flags = 0;
        text = term_extract_abs_range_dup(t, c->start_line, end, command, &len,
                                          &range_flags);
        if (!text) oom = 1;
        if (range_flags & TERM_RANGE_START_RECYCLED) flags |= RUNCMD_CAP_SCROLLED_OUT;
    }

    if (oom) {
        /* Dropped, size unknown: no excerpt, no EMPTY (the signature B8 reads
         * as "output dropped"), omitted_bytes 0. */
        cap_clear_excerpt(c);
        c->flags = flags;
        return 1;
    }
    if (!text || len == 0) flags |= RUNCMD_CAP_EMPTY;
    c->flags = flags;
    runcmd_cut(text, len, cap, c);
    free(text);
    return 1;
}
