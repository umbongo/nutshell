/* libFuzzer target: run_command's input parser and the two tool-stream
 * accumulators (OpenAI and Anthropic format).
 *
 * First byte, low bit 0: the rest of the input is split into lines and each
 * line is fed as a raw stream chunk to both accumulators (the OpenAI one
 * through ai_stream_line_prepare, as the stream thread does). Bit 1 (value 2),
 * low bit 0: the rest, escaped, repeated 1..64 times, is the thought signature
 * of a Gemini call, so it passes the 8000 bytes a 4 KiB input cannot reach.
 * Low bit 1: the rest
 * is one text, escaped into a JSON string and sent as the arguments fragment
 * of one call, repeated 1..64 times so the sum passes AI_TOOL_INPUT_MAX, which
 * a 4 KiB input could not otherwise reach.
 *
 * After every chunk: no call's input is longer than AI_TOOL_INPUT_MAX - 1
 * bytes, and a call that was overflowed is still overflowed with its input
 * untouched (A1's sticky overflow). The whole input also goes to
 * runcmd_parse_input(), and, as one call's stored input, to
 * agentic_sanitize_tool_inputs(): an input it keeps passes json_validate (the
 * check every request body goes through), one it replaces is exactly "{}".
 * The calls the accumulators finish are sanitised the same way. A thought
 * signature, when one is kept, is non-empty, and sig_len is its strlen. */
#include "ai_agentic.h"
#include "ai_tools.h"
#include "json_validate.h"
#include "run_command.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_TRACK 8

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

typedef struct {
    int       count;
    int       overflowed[FUZZ_TRACK];
    char      input[FUZZ_TRACK][AI_TOOL_INPUT_MAX];
} Snap;

/* The sanitiser's invariant on one stored input. */
static void check_sanitised(const char *input)
{
    static AiToolCall c;
    memset(&c, 0, sizeof(c));
    snprintf(c.input_json, sizeof(c.input_json), "%s", input);
    int fixed = agentic_sanitize_tool_inputs(&c, 1);
    if (fixed == 0) {
        char err[96];
        if (!json_validate(c.input_json, strlen(c.input_json), err, sizeof(err))) abort();
    } else if (strcmp(c.input_json, "{}") != 0) {
        abort();
    }
}

static void snap_take(Snap *s, const AiToolStreamState *st)
{
    s->count = st->pending_tool_count < FUZZ_TRACK ? st->pending_tool_count : FUZZ_TRACK;
    for (int i = 0; i < s->count; i++) {
        s->overflowed[i] = st->pending_tool_calls[i].overflowed;
        memcpy(s->input[i], st->pending_tool_calls[i].input_json, AI_TOOL_INPUT_MAX);
    }
}

static void check_state(const Snap *before, const AiToolStreamState *st)
{
    for (int i = 0; i < st->pending_tool_count; i++) {
        const AiToolCall *c = &st->pending_tool_calls[i];
        if (memchr(c->input_json, '\0', AI_TOOL_INPUT_MAX) == NULL) abort();
    }
    if (st->tool_call_active) {
        if (memchr(st->active_tool_call.input_json, '\0', AI_TOOL_INPUT_MAX) == NULL)
            abort();
    }
    for (int i = 0; i < before->count && i < st->pending_tool_count; i++) {
        if (!before->overflowed[i]) continue;
        if (!st->pending_tool_calls[i].overflowed) abort();
        if (strcmp(st->pending_tool_calls[i].input_json, before->input[i]) != 0) abort();
    }
}

static void feed(int anthropic, AiToolStreamState *st, const char *chunk)
{
    static Snap snap;
    snap_take(&snap, st);
    if (anthropic) {
        (void)ai_parse_tool_stream_anthropic(chunk, st);
    } else {
        char *owned = NULL;
        const char *use = ai_stream_line_prepare(st, chunk, &owned);
        if (use) (void)ai_parse_tool_stream_openai(use, st);
        free(owned);
    }
    check_state(&snap, st);
    if ((st->sig == NULL) != (st->sig_len == 0)) abort();
    if (st->sig && strlen(st->sig) != st->sig_len) abort();
    for (int i = 0; i < st->pending_tool_count; i++)
        check_sanitised(st->pending_tool_calls[i].input_json);
}

/* data[0..n) as the body of a JSON string. */
static char *json_escape(const uint8_t *data, size_t n)
{
    char *out = (char *)malloc(n * 6 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = data[i];
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c < 0x20)         { o += (size_t)snprintf(out + o, 7, "\\u%04x", c); }
        else if (c >= 0x80)        { out[o++] = 'x'; }   /* keep the chunk valid UTF-8 */
        else                       { out[o++] = (char)c; }
    }
    out[o] = '\0';
    return out;
}

static void run_repeated(const uint8_t *data, size_t size)
{
    if (size < 2) return;
    int reps = 1 + (int)(data[1] % 64u);
    char *esc = json_escape(data + 2, size - 2);
    if (!esc) return;
    size_t cap = strlen(esc) + 256;
    char *chunk = (char *)malloc(cap);
    if (!chunk) { free(esc); return; }

    for (int anthropic = 0; anthropic < 2; anthropic++) {
        AiToolStreamState st;
        ai_tools_stream_init(&st);
        if (anthropic) {
            feed(1, &st, "{\"type\":\"content_block_start\",\"content_block\":"
                         "{\"type\":\"tool_use\",\"id\":\"t1\",\"name\":\"run_command\"}}");
            snprintf(chunk, cap, "{\"type\":\"content_block_delta\",\"delta\":"
                     "{\"type\":\"input_json_delta\",\"partial_json\":\"%s\"}}", esc);
        } else {
            feed(0, &st, "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
                         "\"id\":\"c1\",\"function\":{\"name\":\"run_command\"}}]}}]}");
            snprintf(chunk, cap, "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
                     "\"function\":{\"arguments\":\"%s\"}}]}}]}", esc);
        }
        for (int i = 0; i < reps; i++) feed(anthropic, &st, chunk);
        if (anthropic) feed(1, &st, "{\"type\":\"content_block_stop\"}");
        else feed(0, &st, "{\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}");
        ai_tools_stream_reset(&st);
    }
    free(chunk);
    free(esc);
}

/* The rest of the input, escaped and repeated, as one call's thought signature. */
static void run_signature(const uint8_t *data, size_t size)
{
    if (size < 3) return;
    int reps = 1 + (int)(data[1] % 64u);
    char *esc = json_escape(data + 2, size - 2);
    if (!esc) return;
    size_t el = strlen(esc);
    const char *head = "{\"choices\":[{\"delta\":{\"content\":\"t\",\"tool_calls\":["
        "{\"id\":\"c1\",\"function\":{\"name\":\"web_search\",\"arguments\":\"{}\"},"
        "\"extra_content\":{\"google\":{\"thought_signature\":\"";
    const char *tail = "\"}}}]}}]}";
    size_t cap = strlen(head) + el * (size_t)reps + strlen(tail) + 1;
    char *chunk = (char *)malloc(cap);
    if (!chunk) { free(esc); return; }
    size_t o = (size_t)snprintf(chunk, cap, "%s", head);
    for (int i = 0; i < reps; i++) { memcpy(chunk + o, esc, el); o += el; }
    memcpy(chunk + o, tail, strlen(tail) + 1);

    AiToolStreamState st;
    ai_tools_stream_init(&st);
    feed(0, &st, chunk);
    feed(0, &st, chunk);   /* a second one is dropped, never taken in place */
    ai_tools_stream_reset(&st);
    free(chunk);
    free(esc);
}

static void run_lines(const uint8_t *data, size_t size)
{
    char *s = (char *)malloc(size + 1);
    if (!s) return;
    memcpy(s, data, size);
    s[size] = '\0';

    for (int anthropic = 0; anthropic < 2; anthropic++) {
        AiToolStreamState st;
        ai_tools_stream_init(&st);
        char *line = s;
        while (line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            feed(anthropic, &st, line);
            line = nl ? nl + 1 : NULL;
        }
        ai_tools_stream_reset(&st);
        /* the lines were cut in place: restore them for the second format */
        memcpy(s, data, size);
    }
    free(s);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0) return 0;

    /* the parser reads everything after the mode byte as the call's arguments */
    char *text = (char *)malloc(size);
    if (!text) return 0;
    memcpy(text, data + 1, size - 1);
    text[size - 1] = '\0';
    {
        char command[RUNCMD_COMMAND_MAX + 1];
        char note[RUNCMD_NOTE_MAX];
        RunCmdParse p = runcmd_parse_input(text, command, note);
        if (strlen(command) > (size_t)RUNCMD_COMMAND_MAX) abort();
        if (strlen(note) >= (size_t)RUNCMD_NOTE_MAX) abort();
        if (p != RUNCMD_PARSE_OK && (command[0] || note[0])) abort();
        char out[32];
        (void)runcmd_describe(text, out, sizeof out);
        check_sanitised(text);
    }
    free(text);

    if (data[0] & 2u) run_signature(data, size);
    else if (data[0] & 1u) run_repeated(data, size);
    else run_lines(data + 1, size - 1);
    return 0;
}
