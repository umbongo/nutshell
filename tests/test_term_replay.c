#include "test_framework.h"
#include "term.h"
#include "term_extract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * Real full-screen programs replayed through the emulator.
 *
 * Each fixture pair was recorded on 2026-11-28 on the maintainer's Linux box:
 * the program ran inside a private tmux server (`tmux -L <name> -f /dev/null`,
 * a fixed pane size, TERM=xterm-256color, LANG=C.UTF-8, an empty HOME) under
 * `script -qf`, which wrote every byte the program sent to its terminal to
 * term_replay_<program>_<cols>x<rows>.bin (script's "Script started" header
 * line removed). The program was driven by a fixed key script through
 * `tmux send-keys`, and while it was still running (before it could leave its
 * alternate screen) `tmux capture-pane -p` saved what tmux itself shows for the
 * same pane to the matching .screen file. The .bin is cut at exactly the byte
 * count the pane had received when the capture was taken.
 *
 *   less    less -- input.txt, a 120-line generated text file: two or three
 *           page-downs, three lines down, then a forward search ("item 665").
 *   vim     vim -u NONE -N -n --noplugin input.txt (same file): 5G, page
 *           down, a search, x, an insert of "NEW", Escape, ":set number".
 *   htop    htop -p 1 (one process, PID 1), updates paused with Z so the
 *           screen does not change under the capture. The CPU, memory,
 *           task and uptime figures are the recorder's live values; they are
 *           frozen in the pair, so the whole screen is compared.
 *
 * What is asserted: the emulator's visible rows equal tmux's rows, row by
 * row, with trailing spaces trimmed and trailing blank rows ignored on both
 * sides. Colours and attributes are not compared (capture-pane -p carries
 * none). The nano replay of PR #94 is a hand-built simulation in
 * test_scroll_region.c, not a recording; this is the recorded check.
 *
 * Runs natively on any host: bytes, a screen file and term_process().
 * ===========================================================================
 */

static char *replay_read(const char *path, size_t *len_out)
{
    *len_out = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1u, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    *len_out = got;
    return buf;
}

/* Cut `text` into rows in place: '\r' dropped, '\n' ends a row, trailing
 * spaces trimmed. Returns the row count with trailing blank rows ignored;
 * rows[] points into `text`. */
static int replay_rows(char *text, char **rows, int max_rows)
{
    int n = 0;
    char *w = text;
    char *start = text;
    for (char *r = text; ; r++) {
        if (*r == '\r') continue;
        if (*r == '\n' || *r == '\0') {
            char *end = w;
            while (end > start && end[-1] == ' ') end--;
            if (n < max_rows) rows[n++] = start;
            int last = (*r == '\0');
            *end = '\0';
            w = end + 1;
            start = w;
            if (last) break;
            continue;
        }
        *w++ = *r;
    }
    while (n > 0 && rows[n - 1][0] == '\0') n--;
    return n;
}

#define REPLAY_MAX_ROWS 64

/* Replays `name` into a rows x cols terminal and compares it with the
 * .screen file. Returns 0 on a match; otherwise the 1-based number of the
 * first differing row (printed), or -1 when a file could not be read (the
 * caller skips). */
static int replay_compare(const char *name, int rows, int cols)
{
    char bin_path[256], scr_path[256];
    snprintf(bin_path, sizeof(bin_path), "tests/fixtures/term_replay_%s.bin", name);
    snprintf(scr_path, sizeof(scr_path), "tests/fixtures/term_replay_%s.screen", name);
    size_t blen = 0, slen = 0;
    char *bytes = replay_read(bin_path, &blen);
    char *want = replay_read(scr_path, &slen);
    if (!bytes || !want) {
        printf("  [term replay] %s not readable from cwd -- skipping\n", name);
        free(bytes);
        free(want);
        return -1;
    }
    Terminal *term = term_init(rows, cols, 0);
    if (!term) { free(bytes); free(want); return 1; }

    /* Uneven chunks: an escape sequence split across two reads must parse. */
    size_t pos = 0, step = 509;
    while (pos < blen) {
        size_t n = blen - pos < step ? blen - pos : step;
        term_process(term, bytes + pos, n);
        pos += n;
        step = step == 509 ? 61 : 509;
    }

    size_t cap = (size_t)rows * (size_t)cols * 4u + 1024u;
    char *got = (char *)malloc(cap);
    int result = 1;
    if (got) {
        term_extract_visible(term, got, cap);
        char *grows[REPLAY_MAX_ROWS], *wrows[REPLAY_MAX_ROWS];
        int gn = replay_rows(got, grows, REPLAY_MAX_ROWS);
        int wn = replay_rows(want, wrows, REPLAY_MAX_ROWS);
        result = 0;
        int top = gn > wn ? gn : wn;
        for (int i = 0; i < top && result == 0; i++) {
            const char *g = i < gn ? grows[i] : "";
            const char *w = i < wn ? wrows[i] : "";
            if (strcmp(g, w) != 0) {
                result = i + 1;
                printf("  [term replay] %s row %d differs\n    tmux:     |%s|\n"
                       "    emulator: |%s|\n", name, i + 1, w, g);
            }
        }
        free(got);
    }
    term_free(term);
    free(bytes);
    free(want);
    return result;
}

#define REPLAY_TEST(fn, name, rows, cols) \
    int fn(void) \
    { \
        TEST_BEGIN(); \
        int r = replay_compare(name, rows, cols); \
        if (r < 0) { /* unreadable fixture: skip, except under CI */ \
            const char *ci = getenv("CI"); \
            if (ci && ci[0]) ASSERT_TRUE(0); \
        } else { \
            ASSERT_EQ(r, 0); \
        } \
        TEST_END(); \
    }

REPLAY_TEST(test_term_replay_less_80x24,  "less_80x24",  24, 80)
REPLAY_TEST(test_term_replay_less_120x40, "less_120x40", 40, 120)
REPLAY_TEST(test_term_replay_vim_80x24,   "vim_80x24",   24, 80)
REPLAY_TEST(test_term_replay_vim_120x40,  "vim_120x40",  40, 120)
REPLAY_TEST(test_term_replay_htop_80x24,  "htop_80x24",  24, 80)
REPLAY_TEST(test_term_replay_htop_120x40, "htop_120x40", 40, 120)
