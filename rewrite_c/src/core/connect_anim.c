#include "connect_anim.h"

int connect_anim_dots(unsigned long elapsed_ms,
                      unsigned long interval_ms,
                      int           max_dots)
{
    if (interval_ms == 0) return max_dots;
    int dots = (int)(elapsed_ms / interval_ms);
    if (dots > max_dots) dots = max_dots;
    return dots;
}

int connect_anim_text(int dots, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;

    const char *prefix = "Connecting";
    size_t prefix_len = 10; /* strlen("Connecting") */
    size_t written = 0;

    /* Copy prefix, truncating if buffer is too small */
    while (written < prefix_len && written + 1 < buf_size) {
        buf[written] = prefix[written];
        written++;
    }

    /* Append dots up to buffer limit */
    for (int i = 0; i < dots && written + 1 < buf_size; i++) {
        buf[written++] = '.';
    }

    buf[written] = '\0';
    return (int)written;
}
