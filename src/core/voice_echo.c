#include "voice_echo.h"

#include "voice_speak.h"
#include "secure_zero.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char  *text;   /* lower-cased words, each NUL-terminated */
    size_t tcap;   /* bytes allocated for text (wiped whole on free) */
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
    if (w->text) secure_zero(w->text, w->tcap);   /* the user's words */
    free(w->text); free(w->off); free(w->len);
    memset(w, 0, sizeof *w);
}

/* Copy of s with U+2019, U+00A0 and U+2014 turned into spaces, so a curly
 * apostrophe, a non-breaking space and an em dash separate words like their
 * ASCII forms. NULL on out of memory. */
static char *normalise(const char *s)
{
    size_t n = strlen(s), o = 0;
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xE2 && i + 2 < n && (unsigned char)s[i + 1] == 0x80
            && ((unsigned char)s[i + 2] == 0x99 || (unsigned char)s[i + 2] == 0x94)) {
            r[o++] = ' '; i += 3;
        } else if (c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0xA0) {
            r[o++] = ' '; i += 2;
        } else {
            r[o++] = s[i++];
        }
    }
    r[o] = '\0';
    return r;
}

/* 0 on out of memory. */
static int words_split(const char *orig, Words *w)
{
    memset(w, 0, sizeof *w);
    char *norm = normalise(orig);
    if (!norm) return 0;
    const char *s = norm;
    int ok = 0;
    size_t n = strlen(s), cnt = 0;
    for (size_t i = 0; i < n; i++)
        if (word_byte((unsigned char)s[i]) && (i == 0 || !word_byte((unsigned char)s[i - 1]))) cnt++;
    w->text = (char *)malloc(n + 1);
    w->tcap = w->text ? n + 1 : 0;
    w->off = (size_t *)malloc((cnt + 1) * sizeof(size_t));
    w->len = (size_t *)malloc((cnt + 1) * sizeof(size_t));
    if (!w->text || !w->off || !w->len) { words_free(w); secure_zero(norm, n + 1); free(norm); return 0; }
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
    ok = 1;
    secure_zero(norm, n + 1);
    free(norm);
    return ok;
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

/* The words a user says to take the floor: never taken for the assistant's own words, even
 * when the reply just said them ("stop" and "yes" are common in a terminal assistant). */
static const char *const BARGE_IN[] = {
    "stop", "wait", "no", "yes", "cancel", "hold on", "pause", "quiet", NULL
};

static int is_bargein(const Words *t)
{
    char all[32];
    size_t o = 0;
    int r;
    if (t->n < 1 || t->n > 2) return 0;
    for (size_t i = 0; i < t->n; i++) {
        if (i > 0) { if (o + 1 >= sizeof all) return 0; all[o++] = ' '; }
        if (o + t->len[i] >= sizeof all) return 0;
        memcpy(all + o, t->text + t->off[i], t->len[i]);
        o += t->len[i];
    }
    all[o] = '\0';
    r = 0;
    for (const char *const *b = BARGE_IN; *b; b++) if (strcmp(*b, all) == 0) r = 1;
    return r;
}

int voice_echo_fragment(const char *recent, const char *transcript)
{
    int result = 0;
    Words t, s;
    if (!recent || !transcript) return 0;
    if (!words_split(transcript, &t)) return 0;
    if (t.n < 1 || t.n > 2 || is_bargein(&t)) { words_free(&t); return 0; }
    if (!words_split(recent, &s)) { words_free(&t); return 0; }
    for (size_t j = 0; j + t.n <= s.n && !result; j++) {
        if (!word_eq(&t, 0, &s, j)) continue;
        if (t.n == 1 || word_eq(&t, 1, &s, j + 1)) result = 1;
    }
    words_free(&t); words_free(&s);
    return result;
}

int voice_echo_tail(const char *tail, const char *transcript)
{
    int result = 0;
    Words t, s;
    if (!tail || !transcript) return 0;
    if (!words_split(transcript, &t)) return 0;
    if (t.n < 1 || t.n > 2 || is_bargein(&t)) { words_free(&t); return 0; }
    if (!words_split(tail, &s)) { words_free(&t); return 0; }
    if (s.n >= t.n) {
        result = 1;
        for (size_t i = 0; i < t.n; i++)
            if (!word_eq(&t, i, &s, s.n - t.n + i)) result = 0;
    }
    words_free(&t); words_free(&s);
    return result;
}

void voice_trail_reset(VoiceWordTrail *t)
{
    if (t) memset(t, 0, sizeof *t);
}

void voice_trail_note(VoiceWordTrail *t, unsigned long at, size_t offset)
{
    int idx;
    if (!t) return;
    if (t->n < VOICE_TRAIL_MAX) {
        idx = (t->head + t->n) % VOICE_TRAIL_MAX;
        t->n++;
    } else {
        idx = t->head;
        t->head = (t->head + 1) % VOICE_TRAIL_MAX;
    }
    t->at[idx] = at;
    t->off[idx] = offset;
}

#define TRAIL_AHEAD_MS 1000
#define TRAIL_WORDS_PER_S 3

/* The end of the word that starts at or after `pos` (one word moved on), or slen. */
static size_t next_word_end(const char *spoken, size_t slen, size_t pos)
{
    while (pos < slen && !word_byte((unsigned char)spoken[pos])) pos++;
    while (pos < slen && word_byte((unsigned char)spoken[pos])) pos++;
    return pos;
}

static size_t trail_text(const VoiceWordTrail *t, const char *spoken, unsigned long cut_at,
                         unsigned long ms, unsigned long ahead_ms, char *out, size_t cap)
{
    size_t len = 0, from = 0, to = 0, slen;
    unsigned long t_first = 0, t_last = 0;
    int have = 0;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!t || !spoken) return 0;
    slen = strlen(spoken);
    {
        /* wrap-safe: the tick counter is 32 bits on Windows and wraps every 49 days */
        const unsigned long lo = cut_at - ms;
        for (int i = 0; i < t->n; i++) {
            const int k = (t->head + i) % VOICE_TRAIL_MAX;
            if (t->off[k] >= slen || (long)(t->at[k] - cut_at) > 0 || (long)(t->at[k] - lo) < 0) continue;
            if (!have) { from = t->off[k]; t_first = t->at[k]; have = 1; }
            to = t->off[k];
            t_last = t->at[k];
        }
    }
    if (!have) return 0;
    while (to < slen && word_byte((unsigned char)spoken[to])) to++;   /* the end of the last word */
    if (to < from) return 0;
    if (ahead_ms > 0) {
        /* words said per second in this span, then that many more as the ones not yet reported */
        const unsigned long span = t_last - t_first;
        size_t nw = 0, ahead;
        for (size_t i = from; i < to; i++)
            if (word_byte((unsigned char)spoken[i]) && (i == from || !word_byte((unsigned char)spoken[i - 1]))) nw++;
        if (span >= 500ul && nw >= 2)
            ahead = (size_t)(((unsigned long)(nw - 1) * ahead_ms + span / 2) / span);
        else
            ahead = (size_t)(TRAIL_WORDS_PER_S * ahead_ms / 1000ul);
        if (ahead < 1) ahead = 1;
        if (ahead > 4) ahead = 4;
        while (ahead-- > 0 && to < slen) to = next_word_end(spoken, slen, to);
    }
    len = to - from;
    if (len > cap - 1) {   /* keep the tail, the words nearest the cut, from a word's start */
        from = to - (cap - 1);
        if (from > 0 && word_byte((unsigned char)spoken[from]) && word_byte((unsigned char)spoken[from - 1]))
            while (from < to && word_byte((unsigned char)spoken[from])) from++;
        while (from < to && !word_byte((unsigned char)spoken[from])) from++;
        len = to - from;
    }
    memcpy(out, spoken + from, len);
    out[len] = '\0';
    return len;
}

size_t voice_trail_recent(const VoiceWordTrail *t, const char *spoken, unsigned long cut_at,
                          unsigned long ms, char *out, size_t cap)
{
    return trail_text(t, spoken, cut_at, ms, 0ul, out, cap);
}

size_t voice_trail_heard(const VoiceWordTrail *t, const char *spoken, unsigned long cut_at,
                         char *out, size_t cap)
{
    return trail_text(t, spoken, cut_at, VOICE_ECHO_RECENT_MS, TRAIL_AHEAD_MS, out, cap);
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
    char all[160], rest[160];
    all[0] = rest[0] = '\0';
    if (w.n >= 1 && w.n <= 6) {
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
    secure_zero(all, sizeof all);
    secure_zero(rest, sizeof rest);
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
        /* A cut-in turn is checked however late its transcript arrives (a batch transcription
         * can take longer than the window). */
        int window = r->cut_in || r->playing || (long)(now - r->spoken_end_at) <= VOICE_ECHO_AFTER_MS;
        if (window && voice_echo_match(r->spoken, transcript)) return VTC_ECHO;
        /* the conversation's own commands are never a fragment of the reply */
        if (voice_phrase_match(transcript) == VPH_NONE) {
            if (r->cut_in && voice_echo_fragment(r->recent, transcript)) return VTC_ECHO;
            /* a piece of the reply's end picked up as a turn as the speech finished, though no
             * cut-in began it: the turn's audio began before the speech ended (plus a grace),
             * however late its transcript arrives. A reply that asked something is answered, not
             * echoed. */
            if (!r->cut_in && !r->playing
                && (long)(r->turn_at - r->spoken_end_at) < VOICE_ECHO_TURN_GRACE_MS
                && !voice_speak_ends_with_question(r->spoken)
                && voice_echo_tail(r->tail, transcript)) return VTC_ECHO;
        }
    }
    if (r->reply && (long)(now - r->reply_at) <= VOICE_PHRASE_MAX_AGE_MS
        && !voice_speak_ends_with_question(r->reply)) {
        VoicePhrase ph = voice_phrase_match(transcript);
        if (ph == VPH_REPEAT) return VTC_REPEAT;
        if (ph == VPH_CONTINUE && !r->read_to_end) return VTC_CONTINUE;
    }
    return VTC_TEXT;
}
