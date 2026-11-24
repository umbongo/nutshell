#include "voice_voices.h"

#include <string.h>

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (lower(*a) != lower(*b)) return 0;
    return *a == *b;
}

static int icontains(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] && lower(hay[i]) == lower(needle[i])) i++;
        if (i == nl) return 1;
    }
    return 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Next LCID in a ';'-separated hex list; *pos advances. Returns -1 at the end
 * and 0 for a token that is not hex. */
static long next_lcid(const char *s, size_t *pos)
{
    while (s[*pos] == ';' || s[*pos] == ' ') (*pos)++;
    if (!s[*pos]) return -1;
    long val = 0;
    int ok = 1, digits = 0;
    while (s[*pos] && s[*pos] != ';') {
        int h = hexval(s[*pos]);
        if (h < 0 || digits >= 8) ok = 0;
        else val = val * 16 + h;
        digits++;
        (*pos)++;
    }
    return ok ? val : 0;
}

int voice_info_lcid_primary(const VoiceInfo *v)
{
    if (!v) return 0;
    size_t pos = 0;
    long l = next_lcid(v->lang, &pos);
    return l < 0 ? 0 : (int)l;
}

int voice_info_is_en_gb(const VoiceInfo *v)
{
    if (!v) return 0;
    size_t pos = 0;
    long l;
    while ((l = next_lcid(v->lang, &pos)) >= 0) if (l == 0x809) return 1;
    return 0;
}

int voice_info_is_english(const VoiceInfo *v)
{
    if (!v) return 0;
    size_t pos = 0;
    long l;
    while ((l = next_lcid(v->lang, &pos)) >= 0) if (l != 0 && (l & 0x3FF) == 0x09) return 1;
    return 0;
}

void voice_display_name(const VoiceInfo *v, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!v) return;
    const char *n = v->name;
    const char *d = strstr(n, " Desktop");
    size_t o = 0;
    for (size_t i = 0; n[i] && o + 1 < cap; i++) {
        if (d && n + i == d) { i += strlen(" Desktop") - 1; continue; }
        out[o++] = n[i];
    }
    out[o] = '\0';
}

static int same_display(const VoiceInfo *a, const VoiceInfo *b)
{
    char x[sizeof a->name], y[sizeof b->name];
    voice_display_name(a, x, sizeof x);
    voice_display_name(b, y, sizeof y);
    return ieq(x, y);
}

size_t voice_list_dedupe(VoiceInfo *v, size_t n)
{
    if (!v) return 0;
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        size_t dup = out;
        for (size_t j = 0; j < out; j++) if (same_display(&v[j], &v[i])) { dup = j; break; }
        if (dup == out) {
            if (out != i) v[out] = v[i];
            out++;
        } else if (v[dup].onecore && !v[i].onecore) {
            v[dup] = v[i];
        }
    }
    return out;
}

int voice_choose_auto(const VoiceInfo *v, size_t n)
{
    if (!v) return -1;
    static const char *const names[] = { "Hazel", "Susan" };
    for (size_t k = 0; k < 2; k++)
        for (size_t i = 0; i < n; i++)
            if (voice_info_is_en_gb(&v[i]) && v[i].female && icontains(v[i].name, names[k]))
                return (int)i;
    for (size_t i = 0; i < n; i++)
        if (voice_info_is_en_gb(&v[i]) && v[i].female) return (int)i;
    for (size_t i = 0; i < n; i++)
        if (voice_info_is_english(&v[i]) && v[i].female) return (int)i;
    for (size_t i = 0; i < n; i++)
        if (voice_info_is_en_gb(&v[i])) return (int)i;
    return -1;
}

int voice_find_saved(const VoiceInfo *v, size_t n, const char *id)
{
    if (!v || !id || !id[0]) return -1;
    for (size_t i = 0; i < n; i++) if (ieq(v[i].id, id)) return (int)i;
    return -1;
}

int voice_notice_due(const VoiceInfo *v, size_t n, int resolved, int using_auto,
                     int shown_this_launch)
{
    if (!using_auto || shown_this_launch) return 0;
    if (resolved < 0 || !v || (size_t)resolved >= n) return 1;
    return !(voice_info_is_en_gb(&v[resolved]) && v[resolved].female);
}
