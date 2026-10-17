/* src/core/dlog_types.c -- name tables for dlog_types.h */
#include "dlog_types.h"
#include <stddef.h>

static const char *const k_cat_names[DLOG_CAT_COUNT] = {
    "render", "term", "ai", "ssh", "shell", "general"
};

static const char *const k_level_names[DLOG_LVL_COUNT] = {
    "off", "errors", "info", "debug", "trace"
};

static const char *const k_ev_tags[6] = {
    "ISSUE", "ERROR", "WARN ", "INFO ", "DEBUG", "TRACE"
};

/* ASCII case-insensitive equality. */
static int ieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        if (ca == '\0') return 1;
    }
}

const char *dlog_cat_name(int cat)
{
    if (cat < 0 || cat >= (int)DLOG_CAT_COUNT) return NULL;
    return k_cat_names[cat];
}

int dlog_cat_from_name(const char *name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < (int)DLOG_CAT_COUNT; i++)
        if (ieq(name, k_cat_names[i])) return i;
    return -1;
}

const char *dlog_level_name(int level)
{
    if (level < 0 || level >= (int)DLOG_LVL_COUNT) return NULL;
    return k_level_names[level];
}

int dlog_level_from_name(const char *name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < (int)DLOG_LVL_COUNT; i++)
        if (ieq(name, k_level_names[i])) return i;
    return -1;
}

int dlog_level_max_event(int level)
{
    static const int k_max[DLOG_LVL_COUNT] = {
        0, DLOG_WARN, DLOG_INFO, DLOG_DEBUG, DLOG_TRACE
    };
    if (level < 0) return 0;
    if (level >= (int)DLOG_LVL_COUNT) return DLOG_TRACE;
    return k_max[level];
}

const char *dlog_ev_level_tag(int ev)
{
    if (ev < 0 || ev > (int)DLOG_TRACE) return NULL;
    return k_ev_tags[ev];
}

int dlog_ev_level_from_tag(const char *tag)
{
    if (!tag) return -1;
    for (int i = 0; i <= (int)DLOG_TRACE; i++) {
        const char *t = k_ev_tags[i];
        size_t j = 0;
        while (t[j] != '\0' && t[j] != ' ' && tag[j] == t[j]) j++;
        if ((t[j] == '\0' || t[j] == ' ') && (tag[j] == '\0' || tag[j] == ' ')) return i;
    }
    return -1;
}

int dlog_ev_level_from_word(const char *word)
{
    if (!word) return -1;
    static const char *const k_words[5] = { "error", "warn", "info", "debug", "trace" };
    for (int i = 0; i < 5; i++)
        if (ieq(word, k_words[i])) return i + 1;
    return -1;
}
