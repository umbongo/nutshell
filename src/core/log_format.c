#include "log_format.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

int log_format_filename(const char *name, const char *dir,
                        char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;

    const char *d = (dir && dir[0]) ? dir : ".";
    const char *n = (name && name[0]) ? name : "session";

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

    /* Get current timestamp */
    time_t now = time(NULL);
    const struct tm *t = localtime(&now);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", t);

    int written = snprintf(buf, buf_size, "%s/%s-%s.log", d, safe, ts);
    if (written < 0 || (size_t)written >= buf_size) {
        /* Truncated or error */
        buf[buf_size - 1] = '\0';
        return (int)(strlen(buf));
    }
    return written;
}
