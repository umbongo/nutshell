#include "log_format.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Documented default — used whenever fmt is NULL, empty, or invalid. */
#define LOG_FORMAT_DEFAULT "%Y-%m-%d_%H-%M-%S"

/* %-conversions this app supports in a log-name format string. */
#define LOG_FORMAT_ALLOWED_SPECS "YymdHMSjAaBbpZz%"

int log_format_validate(const char *fmt)
{
    if (!fmt || fmt[0] == '\0') return 0;

    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') continue;
        p++;
        if (*p == '\0') return 0; /* trailing bare '%' */
        if (!strchr(LOG_FORMAT_ALLOWED_SPECS, *p)) return 0;
    }
    return 1;
}

int log_format_filename(const char *name, const char *dir, const char *fmt,
                        const struct tm *t, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;

    const char *d = (dir && dir[0]) ? dir : ".";
    const char *n = (name && name[0]) ? name : "session";
    const char *f = log_format_validate(fmt) ? fmt : LOG_FORMAT_DEFAULT;

    /* Sanitise name into a temp buffer */
    char safe[64];
    size_t si = 0;
    for (size_t i = 0; n[i] && si + 1 < sizeof(safe); i++) {
        char c = n[i];
        if (c == ' ') {
            safe[si++] = '_';
        } else if (isalnum((unsigned char)c) || c == '-' || c == '.' || c == '_') {
            safe[si++] = c;
        }
        /* else: drop the character */
    }
    safe[si] = '\0';
    if (si == 0) {
        snprintf(safe, sizeof(safe), "session");
    }

    /* Render the timestamp portion. `f` is validated by log_format_validate()
     * (or is the fixed default), never a raw user format string passed
     * straight to a *printf-family function — safe despite not being a
     * literal here. */
    char ts[64];
    ts[0] = '\0';
    if (t) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
        size_t ts_len = strftime(ts, sizeof(ts), f, t);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (ts_len == 0) {
            ts[0] = '\0';
        }
    }

    int written = snprintf(buf, buf_size, "%s\\%s_%s.log", d, ts, safe);
    if (written < 0 || (size_t)written >= buf_size) {
        /* Truncated or error */
        buf[buf_size - 1] = '\0';
        return (int)(strlen(buf));
    }
    return written;
}
