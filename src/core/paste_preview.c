#include "paste_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char **paste_format_lines(const char *raw, int *out_count)
{
    if (out_count) *out_count = 0;
    if (!raw || !out_count || *raw == '\0') return NULL;

    size_t len = strlen(raw);

    /* First pass: count lines */
    int count = 0;
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == '\n') count++;
    }
    /* If text doesn't end with \n, the last segment is still a line */
    if (len > 0 && raw[len - 1] != '\n') count++;

    if (count == 0) return NULL;

    char **lines = (char **)malloc((size_t)count * sizeof(char *));
    if (!lines) return NULL;

    /* Second pass: extract lines, stripping \r */
    int li = 0;
    const char *p = raw;
    while (*p && li < count) {
        const char *nl = strchr(p, '\n');
        size_t seg_len = nl ? (size_t)(nl - p) : strlen(p);

        /* Count \r in segment to determine clean length */
        size_t cr_count = 0;
        for (size_t i = 0; i < seg_len; i++) {
            if (p[i] == '\r') cr_count++;
        }

        size_t clean_len = seg_len - cr_count;
        char *line = (char *)malloc(clean_len + 1);
        if (!line) {
            paste_line_free(lines, li);
            return NULL;
        }

        size_t wi = 0;
        for (size_t i = 0; i < seg_len; i++) {
            if (p[i] != '\r') line[wi++] = p[i];
        }
        line[wi] = '\0';

        lines[li++] = line;
        p += seg_len + (nl ? 1 : 0);
    }

    *out_count = li;
    return lines;
}

void paste_line_free(char **lines, int count)
{
    if (!lines) return;
    for (int i = 0; i < count; i++)
        free(lines[i]);
    free(lines);
}

void paste_build_summary(int line_count, size_t char_count,
                         char *buf, size_t buf_sz)
{
    if (!buf || buf_sz == 0) return;
    (void)snprintf(buf, buf_sz, "Paste %d %s (%zu chars)?",
                   line_count, line_count == 1 ? "line" : "lines",
                   char_count);
}

void paste_clamp_size(int desired_w, int desired_h,
                      int screen_w, int screen_h,
                      int *out_w, int *out_h)
{
    /* Clamp to 90% of screen to keep buttons visible and
     * leave room for taskbar / window chrome */
    int max_w = (screen_w * 9) / 10;
    int max_h = (screen_h * 9) / 10;

    *out_w = desired_w > max_w ? max_w : desired_w;
    *out_h = desired_h > max_h ? max_h : desired_h;
}
