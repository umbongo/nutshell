#include "run_command.h"
#include "ai_data_block.h"
#include "json_parser.h"
#include "json_validate.h"
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
    /* a \u0000 escape ends the C string early: "rm -rf /home/u\u0000/build"
     * would run "rm -rf /home/u" (decision 2, review of B1-B11) */
    if (json_obj_get(root, "command")->str_len != len) {
        json_free(root);
        return RUNCMD_PARSE_NO_COMMAND;
    }
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
    /* a NUL in the note makes it unsafe text like any other control byte */
    if (nt && json_obj_get(root, "note")->str_len == strlen(nt)) {
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

RunCmdRound runcmd_round_plan(int room, int run_command,
                              const AiToolCall *calls, int n_calls)
{
    if (!room) return RUNCMD_ROUND_FULL;
    if (run_command && n_calls > 0 && runcmd_reply_has_call(calls, n_calls))
        return RUNCMD_ROUND_STOP;
    return RUNCMD_ROUND_LOOP;
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
    /* A capture armed again (Retry, a second look) starts clean: nothing of
     * the last take survives, and it can take once more. */
    cap_clear_excerpt(c);
    c->flags = 0;
    c->captured = 0;
    c->start_line = term_abs_cursor_line(t);
    c->start_reflow_seq = t->reflow_seq;
    c->start_region_seq = t->region_scroll_seq;
    c->start_erase_seq = t->erase_seq;
    c->start_alt = t->alt_screen_active ? 1 : 0;

    /* The prompt: the start row's text left of the cursor, untrimmed. One
     * that does not fit is not recorded (prompt_long): the take then leaves
     * the cursor row out, as it did before the prompt was recorded. */
    size_t n = term_extract_cursor_prefix(t, c->prompt, sizeof c->prompt);
    c->prompt_long = n > sizeof c->prompt - 1;
    c->prompt_len = c->prompt_long ? 0 : n;
    if (c->prompt_long) c->prompt[0] = '\0';
    c->started = 1;
}

/* The part of the cursor's row the command wrote: its text left of the
 * cursor, by these rules (C0, replacing ruling R49's prompt fallback): ending
 * with the recorded prompt, the part before it; equal to it, nothing;
 * otherwise all of it. A prompt that changed since the command started (a
 * counter, a cd, another host, a REPL's) is therefore the output's last line:
 * dropping it lost output that had no final newline and hid the change from
 * the model. Trailing blanks trimmed. Heap text with its length in *len_out
 * ("" length 0 when there is none); NULL only when the allocation fails. A
 * prompt that was too long to record (prompt_long) leaves the row out. */
static char *cursor_row_rest(const RunCmdCapture *c, const Terminal *t, size_t *len_out)
{
    *len_out = 0;
    size_t full = c->prompt_long ? 0 : term_extract_cursor_prefix(t, NULL, 0);
    char *row = (char *)g_alloc(full + 1);
    if (!row) return NULL;
    row[0] = '\0';
    if (full > 0) (void)term_extract_cursor_prefix(t, row, full + 1);

    size_t n = full;
    if (c->prompt_len > 0 && n >= c->prompt_len &&
        memcmp(row + n - c->prompt_len, c->prompt, c->prompt_len) == 0) {
        n -= c->prompt_len;
    }
    while (n > 0 && row[n - 1] == ' ') n--;
    row[n] = '\0';
    *len_out = n;
    return row;
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
    /* A clear (ED 2, or ED 0 straight after a CUP/HVP home): the numbering still
     * counts rows, but the rows from the start line on are no longer the
     * command's output. A cursor move is never read as one (spec section 10). */
    int redrawn = 0;
    if (!screen && t->erase_seq != c->start_erase_seq)
        screen = redrawn = 1;
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

        /* The cursor's row is not in the range: add what the command wrote on
         * it (output with no final newline, a progress line at a cancel). */
        if (text) {
            size_t rest_len = 0;
            char *rest = cursor_row_rest(c, t, &rest_len);
            if (!rest) {
                oom = 1;
            } else if (rest_len > 0) {
                char *joined = (char *)g_alloc(len + 1 + rest_len + 1);
                if (!joined) {
                    oom = 1;
                } else {
                    memcpy(joined, text, len);
                    size_t pos = len;
                    if (pos > 0) joined[pos++] = '\n';
                    memcpy(joined + pos, rest, rest_len);
                    pos += rest_len;
                    joined[pos] = '\0';
                    free(text);
                    text = joined;
                    len = pos;
                }
            }
            free(rest);
        }
    }

    if (oom) {
        /* Dropped, size unknown: no excerpt, no EMPTY (the signature B8 reads
         * as "output dropped"), omitted_bytes 0. */
        free(text);
        cap_clear_excerpt(c);
        c->flags = flags;
        return 1;
    }
    /* Nothing left: "no output on the screen". */
    if (!text || len == 0) flags |= RUNCMD_CAP_EMPTY;
    c->flags = flags;
    runcmd_cut(text, len, cap, c);
    free(text);
    return 1;
}

/* ---- Results, notes, fit, framing and aging (spec decisions 4, 11 to 13, 15) ---- */

int runcmd_cause_relaunches(RunCmdCause c)
{
    return c == RUNCMD_CAUSE_SETTLED || c == RUNCMD_CAUSE_TIMEOUT || c == RUNCMD_CAUSE_RETRY;
}

int runcmd_cause_appends(RunCmdCause c)
{
    return c != RUNCMD_CAUSE_NEW_CHAT && c != RUNCMD_CAUSE_SESSION_CLOSED;
}

RunCmdRetry runcmd_retry_plan(const AiConversation *conv, int has_runcmd_batch)
{
    if (has_runcmd_batch) return RUNCMD_RETRY_CANCEL_BATCH;
    if (conv && conv->msg_count > 0) {
        const AiMessage *last = &conv->messages[conv->msg_count - 1];
        if (last->role == AI_ROLE_ASSISTANT && last->n_tool_calls == 0)
            return RUNCMD_RETRY_POP_ASSISTANT;
    }
    return RUNCMD_RETRY_RELAUNCH;
}

RunCmdPathPlan runcmd_path_plan(RunCmdPath p)
{
    RunCmdPathPlan r = { RUNCMD_CAUSE_SETTLED, 0, 0, 0, 0 };
    switch (p) {
    case RUNCMD_PATH_TICK_END:       r.cause = RUNCMD_CAUSE_SETTLED;        break;
    case RUNCMD_PATH_DECIDED:        r.cause = RUNCMD_CAUSE_DECIDED;        break;
    case RUNCMD_PATH_TIMEOUT:        r.cause = RUNCMD_CAUSE_TIMEOUT;        break;
    case RUNCMD_PATH_STOP:           r.cause = RUNCMD_CAUSE_STOP;           break;
    case RUNCMD_PATH_SESSION_ENDED:  r.cause = RUNCMD_CAUSE_SESSION_ENDED;  break;
    case RUNCMD_PATH_TAB_SWITCH:     r.cause = RUNCMD_CAUSE_TAB_SWITCH;     break;
    case RUNCMD_PATH_RETRY:          r.cause = RUNCMD_CAUSE_RETRY;          break;
    case RUNCMD_PATH_TYPED:          r.cause = RUNCMD_CAUSE_TYPED;          break;
    case RUNCMD_PATH_NEW_CHAT:       r.cause = RUNCMD_CAUSE_NEW_CHAT;       break;
    case RUNCMD_PATH_SESSION_CLOSED: r.cause = RUNCMD_CAUSE_SESSION_CLOSED; break;
    case RUNCMD_PATH_PANEL_CLOSED:   r.cause = RUNCMD_CAUSE_PANEL_CLOSED;   break;
    default: return r;
    }
    r.append = runcmd_cause_appends(r.cause);
    r.relaunch = runcmd_cause_relaunches(r.cause);
    /* The terminal is still there except when the chat is cleared or the
     * session is gone (the results are not stored then either). */
    r.capture = (p != RUNCMD_PATH_NEW_CHAT && p != RUNCMD_PATH_SESSION_CLOSED);
    /* Today's [EXEC] path resets auto_rounds on Stop (ai_chat.c, the
     * IDC_CHAT_SEND handler's ai_rounds_user_message) and on a typed message
     * (ai_chat_send_user_message). New Chat calls neither and ai_conv_reset
     * leaves auto_rounds alone. */
    r.reset_rounds = (p == RUNCMD_PATH_STOP || p == RUNCMD_PATH_TYPED);
    return r;
}

#define NOTE_RAN            "ran"
#define NOTE_OTHER_TOOL \
    "not run: a reply that calls run_command may call no other tool; call it again in a reply of its own"
#define NOTE_STOP_REASON    "stopped by the user; do not run it again unless the user asks"
#define NOTE_SCREEN \
    "the screen changed while the command ran; this is the screen as it ended"
#define NOTE_SCROLLED       "the start of the output scrolled out of the terminal's history"
#define NOTE_NO_OUTPUT      "no output on the screen"
#define NOTE_NOT_CAPTURED   "output not captured"
#define NOTE_OOM_DROPPED    "output dropped: Nutshell ran out of memory"
#define NOTE_FIT_DROPPED    "output dropped: no room left in this conversation"
#define NOTE_CUT_FMT        "output cut: %zu bytes omitted from the middle"
#define NOTE_CUT_PRE        "output cut: "
#define NOTE_CUT_POST       " bytes omitted from the middle"

/* What a cause calls itself inside "(...)": the reason a command stopped or
 * never went. The whole-reply causes have none. */
static const char *cause_reason(RunCmdCause c)
{
    switch (c) {
    case RUNCMD_CAUSE_TIMEOUT:       return "the terminal did not show a prompt in time";
    case RUNCMD_CAUSE_RETRY:         return "the user retried";
    case RUNCMD_CAUSE_STOP:          return NOTE_STOP_REASON;
    case RUNCMD_CAUSE_TAB_SWITCH:    return "the user switched tabs";
    case RUNCMD_CAUSE_SESSION_ENDED: return "the session ended";
    case RUNCMD_CAUSE_TYPED:         return "the user sent a new message";
    case RUNCMD_CAUSE_NEW_CHAT:      return "the user started a new chat";
    case RUNCMD_CAUSE_SESSION_CLOSED: return "the session was closed";
    case RUNCMD_CAUSE_PANEL_CLOSED:  return "the AI panel was closed";
    case RUNCMD_CAUSE_EVICTED:       return "the batch was dropped";
    case RUNCMD_CAUSE_SETTLED:
    case RUNCMD_CAUSE_DECIDED:
    default:                         return "the batch finished";
    }
}

/* A note under construction: parts joined with "; ", never past cap - 1
 * bytes (a part that does not fit whole is left out, so no text is cut
 * mid-sentence). */
typedef struct { char *buf; size_t cap, len; } NoteBuf;

static void note_part(NoteBuf *nb, const char *text)
{
    size_t add = strlen(text) + (nb->len ? 2 : 0);
    if (nb->len + add >= nb->cap) return;
    if (nb->len) { memcpy(nb->buf + nb->len, "; ", 2); nb->len += 2; }
    memcpy(nb->buf + nb->len, text, strlen(text) + 1);
    nb->len += strlen(text);
}

/* pre, the number, post: a part with one count in it. */
static void note_part_n(NoteBuf *nb, const char *pre, size_t n, const char *post)
{
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s%zu%s", pre, n, post);
    note_part(nb, tmp);
}

/* The notes decision 10 adds for what was read back, then, when there is an
 * excerpt, the cut note. */
static void capture_notes(NoteBuf *nb, const RunCmdCapture *cap)
{
    /* the capture never ran (the call has no terminal to read): say so, so
     * a bare "ran" never reads as "ran with no output" (review of B1-B11) */
    if (!cap->captured) { note_part(nb, NOTE_NOT_CAPTURED); return; }
    int has_excerpt = cap->head || cap->tail;
    if (cap->flags & RUNCMD_CAP_SCREEN)       note_part(nb, NOTE_SCREEN);
    if (cap->flags & RUNCMD_CAP_SCROLLED_OUT) note_part(nb, NOTE_SCROLLED);
    if (!has_excerpt) {
        if (cap->flags & RUNCMD_CAP_EMPTY) note_part(nb, NOTE_NO_OUTPUT);
        else                               note_part(nb, NOTE_OOM_DROPPED);   /* ruling R31 */
    } else if (cap->omitted_bytes > 0) {
        note_part_n(nb, NOTE_CUT_PRE, cap->omitted_bytes, NOTE_CUT_POST);
    }
}

/* content = head then tail, from the capture. 0 on allocation failure (the
 * result is then left without an excerpt). */
static int excerpt_content(const RunCmdCapture *cap, AiToolResult *res)
{
    if (!cap->captured || (!cap->head && !cap->tail)) return 1;
    size_t hl = cap->head ? cap->head_len : 0, tl = cap->tail ? cap->tail_len : 0;
    if (hl + tl == 0) return 1;
    char *p = (char *)g_alloc(hl + tl + 1);
    if (!p) return 0;
    if (hl) memcpy(p, cap->head, hl);
    if (tl) memcpy(p + hl, cap->tail, tl);
    p[hl + tl] = '\0';
    res->content = p;
    res->content_len = hl + tl;
    if (cap->omitted_bytes > 0) {
        res->omit_at = hl;
        res->omitted_bytes = cap->omitted_bytes;
    }
    return 1;
}

/* The note, and whether the call ran with an excerpt, for one SLOT call. */
static void slot_note(NoteBuf *nb, const RunCmdCall *c, const ApprovalQueue *q,
                      RunCmdCause cause, int *with_excerpt)
{
    char tmp[240];
    *with_excerpt = 0;
    if (cause == RUNCMD_CAUSE_SET_FULL)     { note_part(nb, "not run (too many pending cards)"); return; }
    if (cause == RUNCMD_CAUSE_SESSION_GONE) { note_part(nb, "not run (session ended)"); return; }
    if (!q || cause == RUNCMD_CAUSE_NO_SLOT || c->entry < 0 || c->entry >= q->count) {
        note_part(nb, "not run (refused by the approval queue)");
        return;
    }
    const char *reason = cause_reason(cause);
    switch (q->entries[c->entry].status) {
    case APPROVE_COMPLETED:
        note_part(nb, NOTE_RAN);
        *with_excerpt = 1;
        break;
    case APPROVE_EXECUTING:
        if (cause == RUNCMD_CAUSE_RETRY)
            snprintf(tmp, sizeof(tmp), "ran; the panel stopped waiting; the command may still be running (%s)", reason);
        else
            snprintf(tmp, sizeof(tmp), "the panel stopped waiting; the command may still be running (%s)", reason);
        note_part(nb, tmp);
        *with_excerpt = 1;
        break;
    case APPROVE_APPROVED:
        if (cause == RUNCMD_CAUSE_STOP)       note_part(nb, "not run: " NOTE_STOP_REASON);
        else if (cause == RUNCMD_CAUSE_RETRY) note_part(nb, "not run (the user retried)");
        else {
            snprintf(tmp, sizeof(tmp), "cancelled before it was sent (%s)", reason);
            note_part(nb, tmp);
        }
        break;
    case APPROVE_DENIED:
        note_part(nb, "not run (denied)");
        break;
    case APPROVE_BLOCKED:
        if (cause == RUNCMD_CAUSE_TYPED)
            note_part(nb, "not run (the user sent a new message instead)");
        else
            note_part(nb, "not run (blocked by policy: above the command policy ceiling)");
        break;
    case APPROVE_PENDING:
    default:
        if (cause == RUNCMD_CAUSE_TYPED)        note_part(nb, "not run (the user sent a new message instead)");
        else if (cause == RUNCMD_CAUSE_RETRY)   note_part(nb, "not run (the user retried)");
        else if (cause == RUNCMD_CAUSE_STOP)    note_part(nb, "not run: " NOTE_STOP_REASON);
        else if (cause == RUNCMD_CAUSE_SETTLED || cause == RUNCMD_CAUSE_DECIDED)
            note_part(nb, "not run (the batch finished without it)");
        else                                    note_part(nb, "not run (never decided)");
        break;
    }
}

int runcmd_build_results(const RunCmdReply *r, const ApprovalQueue *q, RunCmdCause cause,
                         AiToolResult *out)
{
    if (!r || !out || r->n_calls <= 0 || !r->calls) return -1;
    memset(out, 0, (size_t)r->n_calls * sizeof(*out));
    for (int i = 0; i < r->n_calls; i++) {
        const RunCmdCall *c = &r->calls[i];
        AiToolResult *res = &out[i];
        NoteBuf nb = { res->tool_note, sizeof(res->tool_note), 0 };
        int with_excerpt = 0;
        snprintf(res->tool_use_id, sizeof(res->tool_use_id), "%s", c->tool_use_id);
        switch (c->kind) {
        case RUNCMD_KIND_OTHER_TOOL:
            if (c->fetch_off) {
                note_part(&nb, tool_fetch_off_note());
            } else {
                note_part(&nb, NOTE_OTHER_TOOL);
                res->is_error = 1;
            }
            break;
        case RUNCMD_KIND_INVALID:
            note_part(&nb, runcmd_parse_error(c->parse));
            res->is_error = 1;
            break;
        case RUNCMD_KIND_OVER_CAP:
            note_part_n(&nb, "not run (too many commands in one reply; at most ", (size_t)RUNCMD_SLOT_MAX, ")");
            break;
        case RUNCMD_KIND_SLOT:
        default:
            slot_note(&nb, c, q, cause, &with_excerpt);
            break;
        }
        if (with_excerpt) {
            /* an excerpt that cannot be copied is dropped with a note, never
             * the call left unanswered (spec decision 25) */
            if (excerpt_content(&c->cap, res)) capture_notes(&nb, &c->cap);
            else {
                RunCmdCapture lost = c->cap;
                lost.head = lost.tail = NULL;
                capture_notes(&nb, &lost);
            }
        }
    }
    return r->n_calls;
}

/* ---- The estimate, the budget and the fit ---- */

static size_t escaped_len(const char *s, size_t len)
{
    size_t n = 0;
    if (!s) return 0;
    for (size_t i = 0; i < len; i++) n += json_escaped_byte_len((unsigned char)s[i]);
    return n;
}

size_t runcmd_request_estimate(const AiConversation *conv, const char *provider,
                               size_t tools_json_len, size_t snapshot_len)
{
    size_t total = tools_json_len + 2 * snapshot_len;
    if (!conv) return total;
    int moonshot = provider && strcmp(provider, "moonshot") == 0;
    for (int i = 0; i < conv->msg_count; i++) {
        const AiMessage *m = &conv->messages[i];
        const char *content = ai_msg_content(m);
        total += escaped_len(content, strlen(content));
        total += escaped_len(m->tool_note, strlen(m->tool_note));
        for (int k = 0; m->parts && k < m->n_parts; k++) {
            const AiToolPart *p = &m->parts[k];
            /* the append that follows ages what is old enough: the stub is what goes out */
            size_t clen = runcmd_part_ages(conv, i, p, RUNCMD_AGE_ROUNDS - 1)
                              ? strlen(RUNCMD_AGED_STUB) : p->content_len;
            total += escaped_len(p->content, clen);
            total += escaped_len(p->tool_note, strlen(p->tool_note));
        }
        if (moonshot && m->role == AI_ROLE_ASSISTANT && m->reasoning)
            total += escaped_len(m->reasoning, strlen(m->reasoning));
    }
    return total + ai_tool_wire_bytes(conv, 1, RUNCMD_AGE_ROUNDS - 1);
}

size_t runcmd_budget(size_t body_max, int context_limit_tokens)
{
    size_t room = body_max;
    if (context_limit_tokens > 0) {
        size_t by_context = (size_t)context_limit_tokens * 3;
        if (by_context < room) room = by_context;
    }
    return room > RUNCMD_BODY_HEADROOM ? room - RUNCMD_BODY_HEADROOM : 0;
}

/* The span of the cut note ("output cut: N bytes omitted from the middle") in
 * a note: [*from, *to) with the digits at [*digits, *to - strlen(POST)). 1 when found. */
static int find_cut_note(const char *note, size_t *from, size_t *digits, size_t *to)
{
    const char *p = strstr(note, NOTE_CUT_PRE);
    if (!p) return 0;
    const char *d = p + strlen(NOTE_CUT_PRE);
    const char *e = d;
    while (*e >= '0' && *e <= '9') e++;
    if (e == d || strncmp(e, NOTE_CUT_POST, strlen(NOTE_CUT_POST)) != 0) return 0;
    *from = (size_t)(p - note);
    *digits = (size_t)(d - note);
    *to = (size_t)(e - note) + strlen(NOTE_CUT_POST);
    return 1;
}

/* Replace the cut note's count in step with omitted_bytes, or add the cut
 * note when there was none (an uncut result that has just been halved). */
static void note_set_cut(char *note, size_t cap, size_t omitted)
{
    char num[40];
    snprintf(num, sizeof(num), "%zu", omitted);
    size_t from, digits, to;
    char tail[AI_TOOL_PART_NOTE_MAX];
    if (find_cut_note(note, &from, &digits, &to)) {
        snprintf(tail, sizeof(tail), "%s", note + to);
        if (digits + strlen(num) + strlen(NOTE_CUT_POST) + strlen(tail) < cap)
            snprintf(note + digits, cap - digits, "%s%s%s", num, NOTE_CUT_POST, tail);
        return;
    }
    char add[96];
    snprintf(add, sizeof(add), NOTE_CUT_FMT, omitted);
    size_t len = strlen(note), sep = len ? 2 : 0;
    if (len + sep + strlen(add) < cap)
        snprintf(note + len, cap - len, "%s%s", sep ? "; " : "", add);
}

/* Drop the cut note and one "; " join beside it (the one before it, else the
 * one after), keeping the rest of the note as it is. Shared through
 * run_command.h (the aging, the fit and the notes-only fallback). */
void runcmd_note_drop_cut(char *note, size_t cap)
{
    size_t from, digits, to;
    if (!find_cut_note(note, &from, &digits, &to)) return;
    char rest[AI_TOOL_PART_NOTE_MAX];
    snprintf(rest, sizeof(rest), "%s", note + to);
    const char *r = rest;
    if (from >= 2 && note[from - 2] == ';' && note[from - 1] == ' ') from -= 2;
    else if (r[0] == ';' && r[1] == ' ') r += 2;
    snprintf(note + from, cap - from, "%s", r);
}

/* Drop the cut note (and the separator before it), then add the fit note. */
static void note_set_dropped(char *note, size_t cap)
{
    runcmd_note_drop_cut(note, cap);
    size_t len = strlen(note), sep = len ? 2 : 0;
    if (len + sep + strlen(NOTE_FIT_DROPPED) < cap)
        snprintf(note + len, cap - len, "%s%s", sep ? "; " : "", NOTE_FIT_DROPPED);
}

/* A UTF-8 continuation byte. */
static int is_cont(unsigned char c) { return (c & 0xC0u) == 0x80u; }

/* Halve one excerpt in place. */
static void halve_result(AiToolResult *r)
{
    size_t len = r->content_len;
    size_t hl = r->omitted_bytes > 0 ? (r->omit_at < len ? r->omit_at : len) : (len * 3) / 4;
    if (r->omitted_bytes == 0)
        while (hl > 0 && hl < len && is_cont((unsigned char)r->content[hl])) hl--;
    size_t tl = len - hl;
    size_t nh = hl / 2, nt = tl / 2;
    while (nh > 0 && nh < hl && is_cont((unsigned char)r->content[nh])) nh--;
    size_t tstart = hl + (tl - nt);                    /* the tail keeps its end */
    while (nt > 0 && is_cont((unsigned char)r->content[tstart])) { tstart++; nt--; }
    if (nt > 0) memmove(r->content + nh, r->content + tstart, nt);
    size_t nlen = nh + nt;
    r->omitted_bytes += len - nlen;
    r->omit_at = nh;
    r->content_len = nlen;
    r->content[nlen] = '\0';
}

size_t runcmd_result_bytes(const AiToolResult *r)
{
    if (!r) return 0;
    size_t clen = r->content ? r->content_len : 0;
    size_t nlen = strlen(r->tool_note);
    return escaped_len(r->content, clen) + escaped_len(r->tool_note, nlen) +
           ai_tool_frame_bytes(RUNCMD_TOOL_NAME, clen, r->omit_at, r->omitted_bytes,
                               nlen > 0, 1);
}

int runcmd_fit(AiToolResult *res, int n, size_t estimate, size_t budget)
{
    if (!res || n <= 0) return 0;
    int rounds = 0;
    for (;;) {
        size_t sum = estimate;
        int any = 0;
        for (int i = 0; i < n; i++) {
            sum += runcmd_result_bytes(&res[i]);
            if (res[i].content && res[i].content_len > 0) any = 1;
        }
        if (sum <= budget || !any) break;
        for (int i = 0; i < n; i++) {
            AiToolResult *r = &res[i];
            if (!r->content || r->content_len == 0) continue;
            halve_result(r);
            if (r->content_len == 0) {
                free(r->content);
                r->content = NULL;
                r->omit_at = 0;
                r->omitted_bytes = 0;
                note_set_dropped(r->tool_note, sizeof(r->tool_note));
            } else {
                note_set_cut(r->tool_note, sizeof(r->tool_note), r->omitted_bytes);
            }
        }
        rounds++;
    }
    return rounds;
}

/* ---- Framing ---- */

char *runcmd_frame_result(const AiToolPart *part, const char *delim, size_t *out_len)
{
    if (!part) return NULL;
    size_t nlen = strlen(part->tool_note);
    const char *content = part->content;
    size_t clen = content ? part->content_len : 0;
    char *out = NULL;
    size_t n = 0;

    if (clen == 0) {                                    /* no excerpt: the note alone */
        out = (char *)g_alloc(nlen + 1);
        if (!out) return NULL;
        memcpy(out, part->tool_note, nlen + 1);
        if (out_len) *out_len = nlen;
        return out;
    }

    char lead[160];
    if (delim) (void)ai_data_lead_tool(RUNCMD_TOOL_NAME, lead, sizeof(lead));

    if (part->omitted_bytes == 0) {                     /* uncut */
        if (delim) return ai_data_block_dup(lead, delim, content, clen, nlen ? part->tool_note : NULL, out_len);
        out = (char *)g_alloc(clen + 1 + nlen + 1);
        if (!out) return NULL;
        memcpy(out, content, clen);
        n = clen;
        if (nlen) { out[n++] = '\n'; memcpy(out + n, part->tool_note, nlen); n += nlen; }
        out[n] = '\0';
        if (out_len) *out_len = n;
        return out;
    }

    /* cut: lead, head block, marker, tail block, note, joined by newlines;
     * a side with nothing in it has no block, so neither is assumed */
    size_t hl = part->omit_at < clen ? part->omit_at : clen, tl = clen - hl;
    char marker[96];
    snprintf(marker, sizeof(marker), RUNCMD_OMIT_MARKER_FMT, part->omitted_bytes);
    char *eh = NULL, *et = NULL;
    size_t ehl = 0, etl = 0;
    if (delim) {
        if (hl) { eh = ai_data_escape_dup(content, hl, &ehl); if (!eh) return NULL; }
        if (tl) { et = ai_data_escape_dup(content + hl, tl, &etl);
                  if (!et) { free(eh); return NULL; } }
    }
    const char *seg[10];
    size_t sl[10];
    int ns = 0;
    size_t dl = delim ? strlen(delim) : 0;
#define SEG(p, l) do { seg[ns] = (p); sl[ns] = (l); ns++; } while (0)
    if (delim) SEG(lead, strlen(lead));
    if (hl) {
        if (delim) { SEG(delim, dl); SEG(eh, ehl); SEG(delim, dl); }
        else SEG(content, hl);
    }
    SEG(marker, strlen(marker));
    if (tl) {
        if (delim) { SEG(delim, dl); SEG(et, etl); SEG(delim, dl); }
        else SEG(content + hl, tl);
    }
    if (nlen) SEG(part->tool_note, nlen);
#undef SEG
    size_t total = 0;
    for (int i = 0; i < ns; i++) total += sl[i] + 1;    /* a newline after each, the last is the NUL */
    out = (char *)g_alloc(total);
    if (out) {
        size_t w = 0;
        for (int i = 0; i < ns; i++) {
            if (i) out[w++] = '\n';
            memcpy(out + w, seg[i], sl[i]);
            w += sl[i];
        }
        out[w] = '\0';
        if (out_len) *out_len = w;
    }
    free(eh);
    free(et);
    return out;
}

/* ---- Aging ---- */

/* A results turn that holds a run_command part: a command round. Web results
 * turns between command rounds are not rounds for aging (spec decision 11,
 * review of B1-B11). */
static int turn_is_command_round(const AiMessage *msg)
{
    if (msg->role != AI_ROLE_TOOL) return 0;
    for (int k = 0; msg->parts && k < msg->n_parts; k++)
        if (strcmp(msg->parts[k].tool_name, RUNCMD_TOOL_NAME) == 0) return 1;
    return 0;
}

int runcmd_part_ages(const AiConversation *conv, int m, const AiToolPart *p, int keep)
{
    if (!conv || !p || keep <= 0 || m < 0 || m >= conv->msg_count) return 0;
    if (strcmp(p->tool_name, RUNCMD_TOOL_NAME) != 0) return 0;
    if (!p->content || p->content_len < strlen(RUNCMD_AGED_STUB)) return 0;
    int later = 0;
    for (int j = m + 1; j < conv->msg_count; j++)
        if (turn_is_command_round(&conv->messages[j])) later++;
    return later >= keep;
}

int runcmd_age_results(AiConversation *conv)
{
    if (!conv) return 0;
    const size_t stub_len = strlen(RUNCMD_AGED_STUB);
    int aged = 0;
    for (int m = 0; m < conv->msg_count; m++) {
        AiMessage *msg = &conv->messages[m];
        if (msg->role != AI_ROLE_TOOL) continue;
        for (int k = 0; msg->parts && k < msg->n_parts; k++) {
            AiToolPart *p = &msg->parts[k];
            if (!runcmd_part_ages(conv, m, p, RUNCMD_AGE_ROUNDS)) continue;
            if (p->content_len == stub_len && memcmp(p->content, RUNCMD_AGED_STUB, stub_len) == 0)
                continue;                                /* already the stub */
            memcpy(p->content, RUNCMD_AGED_STUB, stub_len);
            if (p->content_len > stub_len) p->content[stub_len] = '\0';
            p->content_len = stub_len;
            p->omit_at = 0;
            p->omitted_bytes = 0;
            /* the stub says the output is gone: the cut note goes with it.
             * (A fit or out-of-memory note never sits beside content.) */
            runcmd_note_drop_cut(p->tool_note, sizeof(p->tool_note));
            aged++;
        }
    }
    return aged;
}
