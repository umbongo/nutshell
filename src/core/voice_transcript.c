#include "voice_transcript.h"
#include "secure_zero.h"

#include <stdlib.h>
#include <string.h>

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

size_t vt_utf16_len(const char *utf8, size_t n)
{
    size_t u = 0;
    if (!utf8) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)utf8[i];
        if ((c & 0xC0) == 0x80) continue;   /* continuation */
        u += (c >= 0xF0) ? 2 : 1;
    }
    return u;
}

/* Grow *buf (len bytes used) to hold len + extra + 1; wipes the old block. */
static int buf_reserve(char **buf, size_t len, size_t *cap, size_t extra)
{
    size_t need = len + extra + 1;
    if (*buf && need <= *cap) return 0;
    size_t nc = *cap ? *cap : 64;
    while (nc < need) nc *= 2;
    char *nb = (char *)malloc(nc);
    if (!nb) return -1;
    if (*buf) {
        memcpy(nb, *buf, len);
        secure_zero(*buf, *cap);
        free(*buf);
    }
    nb[len] = '\0';
    *buf = nb;
    *cap = nc;
    return 0;
}

static void buf_free(char **buf, size_t *len, size_t *cap)
{
    if (*buf) { secure_zero(*buf, *cap); free(*buf); }
    *buf = NULL;
    *len = 0;
    *cap = 0;
}

/* Length of the well-formed UTF-8 sequence at s[i] (n bytes available), or 0
 * when the byte is not the start of one (overlong, surrogate, > U+10FFFF,
 * truncated, stray continuation). */
static size_t utf8_seq_len(const unsigned char *s, size_t i, size_t n)
{
    unsigned char c = s[i];
    size_t left = n - i;
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF)
        return (left >= 2 && (s[i + 1] & 0xC0) == 0x80) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        if (left < 3) return 0;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c == 0xE0) lo = 0xA0;
        if (c == 0xED) hi = 0x9F;
        if (s[i + 1] < lo || s[i + 1] > hi) return 0;
        return (s[i + 2] & 0xC0) == 0x80 ? 3 : 0;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (left < 4) return 0;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c == 0xF0) lo = 0x90;
        if (c == 0xF4) hi = 0x8F;
        if (s[i + 1] < lo || s[i + 1] > hi) return 0;
        return ((s[i + 2] & 0xC0) == 0x80 && (s[i + 3] & 0xC0) == 0x80) ? 4 : 0;
    }
    return 0;
}

/* Append provider text, sanitised: CR and LF become spaces and every byte
 * that is not part of a well-formed UTF-8 sequence becomes U+FFFD, so what
 * the buffer holds is valid UTF-8 and its UTF-16 count is exact. */
static int buf_append_norm(char **buf, size_t *len, size_t *cap, const char *str, size_t n)
{
    const unsigned char *s = (const unsigned char *)str;
    if (buf_reserve(buf, *len, cap, n * 3)) return -1;
    size_t o = *len;
    for (size_t i = 0; i < n;) {
        size_t k = utf8_seq_len(s, i, n);
        if (k == 0) {
            (*buf)[o++] = (char)0xEF;
            (*buf)[o++] = (char)0xBF;
            (*buf)[o++] = (char)0xBD;
            i++;
        } else if (k == 1 && (s[i] == '\r' || s[i] == '\n')) {
            (*buf)[o++] = ' ';
            i++;
        } else {
            memcpy(*buf + o, s + i, k);
            o += k;
            i += k;
        }
    }
    *len = o;
    (*buf)[o] = '\0';
    return 0;
}

/* Append a piece to a joined buffer: one space when neither side has
 * whitespace at the join.  Empty pieces are skipped. */
static int join_piece(char **buf, size_t *len, size_t *cap, const char *s, size_t n)
{
    if (n == 0) return 0;
    if (*len > 0 && !is_ws((*buf)[*len - 1]) && !is_ws(s[0])) {
        if (buf_reserve(buf, *len, cap, 1)) return -1;
        (*buf)[(*len)++] = ' ';
        (*buf)[*len] = '\0';
    }
    return buf_append_norm(buf, len, cap, s, n);
}

void vt_init(VoiceTranscript *t, size_t draft_u16, int need_sep)
{
    memset(t, 0, sizeof *t);
    t->draft_u16 = draft_u16;
    t->need_sep = need_sep ? 1 : 0;
}

static void seg_free(VtSeg *s)
{
    buf_free(&s->text, &s->tlen, &s->tcap);
    secure_zero(s->id, sizeof s->id);
    s->done = 0;
}

/* Remember an id that has finished so later events for it are ignored. */
static void remember_finished(VoiceTranscript *t, const char *id)
{
    char *slot = t->fin[t->fin_next];
    secure_zero(slot, VT_ID_MAX);
    strncpy(slot, id, VT_ID_MAX - 1);
    t->fin_next = (t->fin_next + 1) % VT_FIN_MAX;
    if (t->nfin < VT_FIN_MAX) t->nfin++;
}

static int is_finished(const VoiceTranscript *t, const char *id)
{
    for (int i = 0; i < t->nfin; i++)
        if (strncmp(t->fin[i], id, VT_ID_MAX - 1) == 0) return 1;
    return 0;
}

/* Fold seg[0] into committed (as final) and close the gap. */
static int fold_first(VoiceTranscript *t)
{
    VtSeg *s = &t->seg[0];
    remember_finished(t, s->id);
    int rc = join_piece(&t->committed, &t->clen, &t->ccap, s->text ? s->text : "", s->tlen);
    seg_free(s);
    for (int i = 1; i < t->nseg; i++) {
        t->seg[i - 1] = t->seg[i];
    }
    memset(&t->seg[t->nseg - 1], 0, sizeof t->seg[0]);
    t->nseg--;
    return rc;
}

static int fold_leading_done(VoiceTranscript *t)
{
    while (t->nseg > 0 && t->seg[0].done)
        if (fold_first(t)) return -1;
    return 0;
}

/* 0: *out is the segment (found or added); 1: the id is finished, ignore the
 * event; -1: out of memory. */
static int seg_get(VoiceTranscript *t, const char *id, VtSeg **out)
{
    for (int i = 0; i < t->nseg; i++)
        if (strcmp(t->seg[i].id, id) == 0) {
            if (t->seg[i].done) return 1;
            *out = &t->seg[i];
            return 0;
        }
    if (is_finished(t, id)) return 1;
    if (t->nseg == VT_SEG_MAX && fold_first(t)) return -1;
    VtSeg *s = &t->seg[t->nseg++];
    memset(s, 0, sizeof *s);
    strncpy(s->id, id, VT_ID_MAX - 1);
    *out = s;
    return 0;
}

int vt_delta(VoiceTranscript *t, const char *seg, const char *text, int append_only)
{
    if (!t || !text) return -1;
    if (!seg) seg = "";
    VtSeg *s = NULL;
    int g = seg_get(t, seg, &s);
    if (g) return g < 0 ? -1 : 0;
    (void)append_only;   /* both modes accumulate; the provider's final
                            event (complete / replace_all) is what replaces */
    return buf_append_norm(&s->text, &s->tlen, &s->tcap, text, strlen(text));
}

int vt_complete(VoiceTranscript *t, const char *seg, const char *final_text)
{
    if (!t) return -1;
    if (!seg) seg = "";
    VtSeg *s = NULL;
    int g = seg_get(t, seg, &s);
    if (g) return g < 0 ? -1 : 0;
    if (final_text) {
        buf_free(&s->text, &s->tlen, &s->tcap);
        if (buf_append_norm(&s->text, &s->tlen, &s->tcap, final_text, strlen(final_text)))
            return -1;
    }
    s->done = 1;
    return fold_leading_done(t);
}

int vt_replace_all(VoiceTranscript *t, const char *text)
{
    if (!t || !text) return -1;
    for (int i = 0; i < t->nseg; i++) seg_free(&t->seg[i]);
    t->nseg = 0;
    buf_free(&t->committed, &t->clen, &t->ccap);
    return buf_append_norm(&t->committed, &t->clen, &t->ccap, text, strlen(text));
}

/* Joined body: committed then open segments.  Heap, NUL-terminated. */
static char *body_build(const VoiceTranscript *t, size_t *outlen)
{
    char *b = NULL;
    size_t len = 0, cap = 0;
    if (buf_reserve(&b, 0, &cap, 0)) return NULL;
    int bad = 0;
    if (t->clen && join_piece(&b, &len, &cap, t->committed, t->clen)) bad = 1;
    for (int i = 0; !bad && i < t->nseg; i++)
        if (t->seg[i].tlen && join_piece(&b, &len, &cap, t->seg[i].text, t->seg[i].tlen))
            bad = 1;
    if (bad) { secure_zero(b, cap); free(b); return NULL; }
    *outlen = len;
    return b;
}

char *vt_text(const VoiceTranscript *t)
{
    size_t n = 0;
    return t ? body_build(t, &n) : NULL;
}

int vt_all_done(const VoiceTranscript *t)
{
    for (int i = 0; i < t->nseg; i++)
        if (!t->seg[i].done) return 0;
    return 1;
}

char *vt_render(VoiceTranscript *t, size_t *from_u16)
{
    size_t blen = 0;
    char *body = body_build(t, &blen);
    if (!body) return NULL;

    /* Leading whitespace of the live text is dropped when a separator is
     * added (it supplies the space) or when the box was empty. */
    size_t skip = 0;
    if (t->need_sep || t->draft_u16 == 0)
        while (skip < blen && is_ws(body[skip])) skip++;
    size_t rest = blen - skip;
    int sep = (t->need_sep && rest > 0) ? 1 : 0;

    char *nw = (char *)malloc(rest + (size_t)sep + 1);
    if (!nw) { secure_zero(body, blen + 1); free(body); return NULL; }
    if (sep) nw[0] = ' ';
    memcpy(nw + sep, body + skip, rest);
    size_t nlen = rest + (size_t)sep;
    nw[nlen] = '\0';
    secure_zero(body, blen + 1);
    free(body);

    size_t p = 0;
    size_t lim = nlen < t->slen ? nlen : t->slen;
    while (p < lim && nw[p] == t->shown[p]) p++;
    /* back up to a character boundary in both strings */
    while (p > 0 && ((p < nlen && ((unsigned char)nw[p] & 0xC0) == 0x80) ||
                     (p < t->slen && ((unsigned char)t->shown[p] & 0xC0) == 0x80)))
        p--;

    char *out = (char *)malloc(nlen - p + 1);
    if (!out) { secure_zero(nw, nlen + 1); free(nw); return NULL; }
    memcpy(out, nw + p, nlen - p + 1);

    t->stable_u16 = vt_utf16_len(nw, p);
    t->shown_u16 = vt_utf16_len(nw, nlen);
    if (from_u16) *from_u16 = t->draft_u16 + t->stable_u16;

    /* nw becomes the new "shown" buffer; the old one is wiped */
    if (t->shown) { secure_zero(t->shown, t->scap); free(t->shown); }
    t->shown = nw;
    t->slen = nlen;
    t->scap = nlen + 1;
    return out;
}

void vt_free(VoiceTranscript *t)
{
    if (!t) return;
    for (int i = 0; i < VT_SEG_MAX; i++) seg_free(&t->seg[i]);
    buf_free(&t->committed, &t->clen, &t->ccap);
    buf_free(&t->shown, &t->slen, &t->scap);
    secure_zero(t->fin, sizeof t->fin);
    t->nfin = t->fin_next = 0;
    t->nseg = 0;
    t->shown_u16 = t->stable_u16 = 0;
}
