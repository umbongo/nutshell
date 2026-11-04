/* src/core/chat_exec.c -- see chat_exec.h. */
#include "chat_exec.h"
#include "exec_scan.h"
#include <string.h>

static const char EXEC_OPEN[]  = "[EXEC]";
static const char EXEC_CLOSE[] = "[/EXEC]";
#define EXEC_OPEN_LEN  ((int)sizeof(EXEC_OPEN) - 1)
#define EXEC_CLOSE_LEN ((int)sizeof(EXEC_CLOSE) - 1)

/* First occurrence of marker m (mlen bytes) in text[from, len), or -1. */
static int find_marker(const char *text, int from, int len,
                       const char *m, int mlen)
{
    for (int i = from; i + mlen <= len; i++)
        if (text[i] == m[0] && memcmp(text + i, m, (size_t)mlen) == 0)
            return i;
    return -1;
}

/* The next live marker at or after p: its "[EXEC]" at *open and its
 * "[/EXEC]" at *close, or *close == -1 for an opener with no closer. A marker
 * the shared scanner calls quoted (inside a fence or an inline code span) is
 * not live: it stays in the prose, where the renderer draws it as code.
 * Returns 0 when no live marker is left. */
static int next_live_marker(const char *text, int len, int p,
                            int *open, int *close)
{
    /* Most replies hold no marker at all: no scan, no allocation. */
    if (find_marker(text, p, len, EXEC_OPEN, EXEC_OPEN_LEN) < 0) return 0;

    ExecScan sc;
    ExecMark m;
    exec_scan_init(&sc, text, (size_t)len);
    sc.pos = text + p;

    int found = 0;
    int tail = p;                 /* past the last marker the scan returned */
    while (exec_scan_next(&sc, &m)) {
        tail = (int)(m.end - text) + EXEC_CLOSE_LEN;
        if (m.quoted) continue;
        *open = (int)(m.marker - text);
        *close = (int)(m.end - text);
        found = 1;
        break;
    }
    if (!found) {
        /* Openers left after the last pair have no closer: the first one
         * that is not quoted is an unterminated command. */
        int from = tail;
        for (;;) {
            int o = find_marker(text, from, len, EXEC_OPEN, EXEC_OPEN_LEN);
            if (o < 0) break;
            if (!exec_scan_quoted(&sc, text + o)) {
                *open = o;
                *close = -1;
                found = 1;
                break;
            }
            from = o + EXEC_OPEN_LEN;
        }
    }
    exec_scan_free(&sc);
    return found;
}

int chat_exec_split(const char *text, int len, int *pos, ChatSeg *out)
{
    if (!text || !pos || !out || len <= 0) return 0;
    int p = *pos;
    if (p < 0 || p >= len) return 0;

    int open = 0, close = -1;
    if (!next_live_marker(text, len, p, &open, &close)) {
        /* the rest is prose, untrimmed */
        out->kind = CHAT_SEG_PROSE;
        out->start = p;
        out->end = len;
        *pos = len;
        return 1;
    }

    /* Prose before the marker, trailing line ends dropped. */
    int trim = open;
    while (trim > p && (text[trim - 1] == '\n' || text[trim - 1] == '\r'))
        trim--;
    if (trim > p) {
        out->kind = CHAT_SEG_PROSE;
        out->start = p;
        out->end = trim;
        *pos = open;
        return 1;
    }

    /* Nothing left before the marker: the command segment. */
    out->kind = CHAT_SEG_EXEC;
    if (close < 0) {              /* unterminated: marker and all */
        out->start = open;
        out->end = len;
        *pos = len;
    } else {
        out->start = open + EXEC_OPEN_LEN;
        out->end = close;
        *pos = close + EXEC_CLOSE_LEN;
    }
    return 1;
}
