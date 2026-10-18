/* src/core/settings_debug.c -- see settings_debug.h. */
#include "settings_debug.h"

#include <stdio.h>
#include <string.h>

const char *settings_debug_level_label(int level)
{
    switch (level) {
        case DLOG_LVL_OFF:    return "Off";
        case DLOG_LVL_ERRORS: return "Errors";
        case DLOG_LVL_INFO:   return "Info";
        case DLOG_LVL_DEBUG:  return "Debug";
        case DLOG_LVL_TRACE:  return "Trace";
        default:              return NULL;
    }
}

const char *settings_debug_cat_label(int cat)
{
    switch (cat) {
        case DLOG_RENDER:  return "Rendering";
        case DLOG_TERM:    return "Terminal";
        case DLOG_AI:      return "AI Assist";
        case DLOG_SSH:     return "SSH connection";
        case DLOG_SHELL:   return "Local shell";
        case DLOG_GENERAL: return "General";
        default:           return NULL;
    }
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int settings_debug_parse_mb(const char *text, int lo, int hi, int current)
{
    if (!text) return clamp_int(current, lo, hi);
    while (*text == ' ' || *text == '\t') text++;
    if (*text < '0' || *text > '9') return clamp_int(current, lo, hi);

    long long v = 0;
    const char *p = text;
    while (*p >= '0' && *p <= '9') {
        if (v <= 100000000000LL) v = v * 10 + (*p - '0');   /* saturates: far beyond any hi */
        p++;
    }
    const char *tail = p;
    while (*tail == ' ' || *tail == '\t') tail++;
    if (*tail != '\0') return clamp_int(current, lo, hi);

    if (v > (long long)hi) return hi;
    if (v < (long long)lo) return lo;
    return (int)v;
}

/* Copy s into out (cap bytes incl. NUL), cut to fit. Returns the length. */
static size_t put(char *out, size_t cap, const char *s)
{
    if (!out || cap == 0) return 0;
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = '\0';
    return n;
}

size_t settings_debug_usage_text(char *out, size_t cap,
                                 unsigned long long bytes, unsigned files)
{
    if (!out || cap == 0) return 0;
    if (files == 0) return put(out, cap, "no log files yet");

    char tmp[96];
    const unsigned long long mb = 1048576ULL;
    if (bytes < mb) {
        (void)snprintf(tmp, sizeof tmp, "using under 1 MB in %u file%s",
                       files, files == 1u ? "" : "s");
    } else {
        unsigned long long whole = (bytes + mb / 2ULL) / mb;
        (void)snprintf(tmp, sizeof tmp, "using %llu MB in %u file%s",
                       whole, files, files == 1u ? "" : "s");
    }
    return put(out, cap, tmp);
}

size_t settings_debug_current_text(char *out, size_t cap, const DlogStatus *st)
{
    if (!out || cap == 0) return 0;
    char tmp[2 * DLOG_MAX_PATH + 320];

    if (!st) {
        (void)snprintf(tmp, sizeof tmp, "none yet, created at the first error");
    } else if (st->mem_only) {
        if (st->note[0])
            (void)snprintf(tmp, sizeof tmp, "logging to memory only (%s)", st->note);
        else
            (void)snprintf(tmp, sizeof tmp, "logging to memory only (no log folder is writable)");
    } else if (st->path[0]) {
        if (st->note[0])
            (void)snprintf(tmp, sizeof tmp, "%s  (%s)", st->path, st->note);
        else
            (void)snprintf(tmp, sizeof tmp, "%s", st->path);
    } else if (st->enabled) {
        (void)snprintf(tmp, sizeof tmp, "none yet, created at the first event");
    } else {
        (void)snprintf(tmp, sizeof tmp, "none yet, created at the first error");
    }
    return put(out, cap, tmp);
}
