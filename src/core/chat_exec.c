/* src/core/chat_exec.c -- see chat_exec.h. */
#include "chat_exec.h"
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

int chat_exec_split(const char *text, int len, int *pos, ChatSeg *out)
{
    if (!text || !pos || !out || len <= 0) return 0;
    for (;;) {
        int p = *pos;
        if (p < 0 || p >= len) return 0;

        /* At a marker: the command segment. */
        if (p + EXEC_OPEN_LEN <= len &&
            memcmp(text + p, EXEC_OPEN, (size_t)EXEC_OPEN_LEN) == 0) {
            int cmd = p + EXEC_OPEN_LEN;
            int close = find_marker(text, cmd, len, EXEC_CLOSE, EXEC_CLOSE_LEN);
            out->kind = CHAT_SEG_EXEC;
            if (close < 0) {          /* unterminated: marker and all */
                out->start = p;
                out->end = len;
                *pos = len;
            } else {
                out->start = cmd;
                out->end = close;
                *pos = close + EXEC_CLOSE_LEN;
            }
            return 1;
        }

        int open = find_marker(text, p, len, EXEC_OPEN, EXEC_OPEN_LEN);
        if (open < 0) {               /* the rest is prose, untrimmed */
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
        *pos = open;
        if (trim > p) {
            out->kind = CHAT_SEG_PROSE;
            out->start = p;
            out->end = trim;
            return 1;
        }
        /* nothing left before the marker: loop to emit the command */
    }
}
