#include "voice_echo.h"

#include "voice_speak.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char  *text;   /* lower-cased words, each NUL-terminated */
    size_t *off;   /* word offsets into text */
    size_t *len;
    size_t n;
} Words;

static int word_byte(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
}

static void words_free(Words *w)
{
    free(w->text); free(w->off); free(w->len);
    memset(w, 0, sizeof *w);
}

/* 0 on out of memory. */
static int words_split(const char *s, Words *w)
{
    memset(w, 0, sizeof *w);
    size_t n = strlen(s), cnt = 0;
    for (size_t i = 0; i < n; i++)
        if (word_byte((unsigned char)s[i]) && (i == 0 || !word_byte((unsigned char)s[i - 1]))) cnt++;
    w->text = (char *)malloc(n + 1);
    w->off = (size_t *)malloc((cnt + 1) * sizeof(size_t));
    w->len = (size_t *)malloc((cnt + 1) * sizeof(size_t));
    if (!w->text || !w->off || !w->len) { words_free(w); return 0; }
    size_t o = 0;
    for (size_t i = 0; i < n;) {
        if (!word_byte((unsigned char)s[i])) { i++; continue; }
        size_t st = o;
        while (i < n && word_byte((unsigned char)s[i])) {
            char c = s[i++];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            w->text[o++] = c;
        }
        w->off[w->n] = st;
        w->len[w->n] = o - st;
        w->n++;
    }
    w->text[o] = '\0';
    return 1;
}

static int word_eq(const Words *a, size_t i, const Words *b, size_t j)
{
    return a->len[i] == b->len[j] && memcmp(a->text + a->off[i], b->text + b->off[j], a->len[i]) == 0;
}

int voice_echo_match(const char *spoken, const char *transcript)
{
    if (!spoken || !transcript) return 0;
    Words t, s;
    if (!words_split(transcript, &t)) return 0;
    if (t.n < 3) { words_free(&t); return 0; }
    if (!words_split(spoken, &s)) { words_free(&t); return 0; }
    size_t m = t.n, k = s.n;
    size_t *prev = (size_t *)calloc(k + 1, sizeof(size_t));
    size_t *cur = (size_t *)calloc(k + 1, sizeof(size_t));
    size_t *best = (size_t *)calloc(m, sizeof(size_t));
    int result = 0;
    if (prev && cur && best) {
        for (size_t i = 0; i < m; i++) {
            for (size_t j = 0; j < k; j++)
                cur[j + 1] = word_eq(&t, i, &s, j) ? prev[j] + 1 : 0;
            cur[0] = 0;
            size_t b = 0;
            for (size_t j = 1; j <= k; j++) if (cur[j] > b) b = cur[j];
            best[i] = b;
            size_t *tmp = prev; prev = cur; cur = tmp;
        }
        size_t covered = 0, rem = 0;
        for (size_t i = m; i-- > 0;) {
            size_t b = best[i] >= 3 ? best[i] : 0;
            rem = rem > 0 ? rem - 1 : 0;
            if (b > rem) rem = b;
            if (rem > 0) covered++;
        }
        result = covered * 10 >= m * 9;
    }
    free(prev); free(cur); free(best);
    words_free(&t); words_free(&s);
    return result;
}

/* ---- phrases ---- */

static const char *const REPEAT_PHRASES[] = {
    "repeat", "repeat that", "say that again", "again", "what did you say", NULL
};
static const char *const CONTINUE_PHRASES[] = {
    "continue", "carry on", "go on", "keep going", NULL
};
static const char *const LEAD[] = {
    "can you please", "could you please", "can you", "could you", "please", NULL
};

/* Join words [from, to) with single spaces into out; 0 if it does not fit. */
static int join_words(const Words *w, size_t from, size_t to, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = from; i < to; i++) {
        if (i > from) { if (o + 1 >= cap) return 0; out[o++] = ' '; }
        if (o + w->len[i] >= cap) return 0;
        memcpy(out + o, w->text + w->off[i], w->len[i]);
        o += w->len[i];
    }
    out[o] = '\0';
    return 1;
}

static int in_list(const char *const *list, const char *s)
{
    for (; *list; list++) if (strcmp(*list, s) == 0) return 1;
    return 0;
}

VoicePhrase voice_phrase_match(const char *transcript)
{
    if (!transcript) return VPH_NONE;
    Words w;
    if (!words_split(transcript, &w)) return VPH_NONE;
    VoicePhrase res = VPH_NONE;
    if (w.n >= 1 && w.n <= 6) {
        char all[160], rest[160];
        if (join_words(&w, 0, w.n, all, sizeof all)) {
            const char *p = all;
            for (const char *const *l = LEAD; *l; l++) {
                size_t ll = strlen(*l);
                if (strncmp(all, *l, ll) == 0 && all[ll] == ' ') { p = all + ll + 1; break; }
            }
            size_t rl = strlen(p);
            if (rl >= sizeof rest) rl = sizeof rest - 1;
            memcpy(rest, p, rl);
            rest[rl] = '\0';
            size_t len = strlen(rest);
            if (len > 7 && strcmp(rest + len - 7, " please") == 0) rest[len - 7] = '\0';
            if (in_list(REPEAT_PHRASES, rest)) res = VPH_REPEAT;
            else if (in_list(CONTINUE_PHRASES, rest)) res = VPH_CONTINUE;
        }
    }
    words_free(&w);
    return res;
}

/* ---- classify ---- */

VoiceTurnClass voice_turn_classify(const char *transcript, const VoiceReplyCtx *r,
                                   unsigned long now)
{
    if (!transcript) return VTC_EMPTY;
    const char *p = transcript;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') p++;
    if (!*p) return VTC_EMPTY;
    if (!r) return VTC_TEXT;

    if (r->spoken) {
        int window = r->playing || now < r->spoken_end_at
                     || now - r->spoken_end_at <= VOICE_ECHO_AFTER_MS;
        if (window && voice_echo_match(r->spoken, transcript)) return VTC_ECHO;
    }
    if (r->reply && (now < r->reply_at || now - r->reply_at <= VOICE_PHRASE_MAX_AGE_MS)
        && !voice_speak_ends_with_question(r->reply)) {
        VoicePhrase ph = voice_phrase_match(transcript);
        if (ph == VPH_REPEAT) return VTC_REPEAT;
        if (ph == VPH_CONTINUE && !r->read_to_end) return VTC_CONTINUE;
    }
    return VTC_TEXT;
}
