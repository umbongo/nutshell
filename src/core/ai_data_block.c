#include "ai_data_block.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long g_delim_counter;

void ai_delim_make(AiRandBytesFn rb, void *ctx, char out[AI_DELIM_MAX])
{
    unsigned char b[8];
    static const char hex[] = "0123456789abcdef";
    size_t plen = strlen(AI_DELIM_PREFIX);

    if (!rb || rb(ctx, b, sizeof(b)) != 1) {
        unsigned long long c =
            __atomic_add_fetch(&g_delim_counter, 1, __ATOMIC_SEQ_CST);
        snprintf(out, AI_DELIM_MAX, "%s%016llx", AI_DELIM_PREFIX, c);
        return;
    }
    memcpy(out, AI_DELIM_PREFIX, plen);
    for (size_t i = 0; i < sizeof(b); i++) {
        out[plen + 2 * i]     = hex[b[i] >> 4];
        out[plen + 2 * i + 1] = hex[b[i] & 0x0F];
    }
    out[plen + 2 * sizeof(b)] = '\0';
}

typedef struct {
    char  *p;
    size_t len;
    size_t cap;
} DbBuf;

/* Room for `extra` more bytes plus the NUL; false on overflow or OOM. */
static int db_reserve(DbBuf *b, size_t extra)
{
    size_t need;
    if (extra > (size_t)-1 - b->len - 1) return 0;
    need = b->len + extra + 1;
    if (need > b->cap) {
        size_t ncap = b->cap ? b->cap : 64;
        char *np;
        while (ncap < need) {
            if (ncap > (size_t)-1 / 2) return 0;
            ncap *= 2;
        }
        np = (char *)realloc(b->p, ncap);
        if (!np) return 0;
        b->p = np;
        b->cap = ncap;
    }
    return 1;
}

static int db_put(DbBuf *b, const char *s, size_t n)
{
    if (!db_reserve(b, n)) return 0;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    return 1;
}

/* Case-insensitive ASCII match of lit at in[i], within len bytes. */
static int match_ci(const char *in, size_t len, size_t i, const char *lit)
{
    size_t n = strlen(lit);
    if (n > len - i) return 0;
    for (size_t k = 0; k < n; k++) {
        char c = in[i + k];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != lit[k]) return 0;
    }
    return 1;
}

/* Length of the line break at in[i] (0 if none). */
static size_t break_len(const char *in, size_t len, size_t i)
{
    unsigned char c = (unsigned char)in[i];
    if (c == '\n') return 1;
    if (c == '\r') return (i + 1 < len && in[i + 1] == '\n') ? 2 : 1;
    if (c == 0xE2 && i + 2 < len && (unsigned char)in[i + 1] == 0x80 &&
        ((unsigned char)in[i + 2] == 0xA8 || (unsigned char)in[i + 2] == 0xA9))
        return 3;
    if (c == 0xC2 && i + 1 < len && (unsigned char)in[i + 1] == 0x85) return 2;
    return 0;
}

char *ai_data_escape_dup(const char *in, size_t len, size_t *out_len)
{
    DbBuf b = {NULL, 0, 0};
    size_t i = 0;
    int line_start = 1;
    const size_t plen = strlen(AI_DELIM_PREFIX);

    if (!in) len = 0;
    if (!db_reserve(&b, len)) return NULL;

    while (i < len) {
        size_t bl = break_len(in, len, i);
        if (bl) {
            if (!db_put(&b, "\n", 1)) goto fail;
            i += bl;
            line_start = 1;
            continue;
        }
        if (line_start) {
            line_start = 0;
            if (plen <= len - i && memcmp(in + i, AI_DELIM_PREFIX, plen) == 0 &&
                !db_put(&b, " ", 1))
                goto fail;
        }
        if (in[i] == '[') {
            if (match_ci(in, len, i, "[exec]")) {
                if (!db_put(&b, "[EXEC ]", 7)) goto fail;
                i += 6;
                continue;
            }
            if (match_ci(in, len, i, "[/exec]")) {
                if (!db_put(&b, "[/EXEC ]", 8)) goto fail;
                i += 7;
                continue;
            }
        }
        if (!db_put(&b, in + i, 1)) goto fail;
        i++;
    }
    b.p[b.len] = '\0';
    if (out_len) *out_len = b.len;
    return b.p;

fail:
    free(b.p);
    return NULL;
}
