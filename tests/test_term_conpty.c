#include "test_framework.h"
#include "term.h"
#include "term_extract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * Recorded ConPTY byte streams through the emulator
 * (docs/superpowers/specs/2026-09-22-local-shell-design.md section 8).
 *
 * A local session's bytes come from Windows's pseudo-console, not from an
 * SSH server, and they are not the same bytes: ConPTY re-renders whatever
 * the child wrote into its own screen updates, opens with
 * ESC[?9001h ESC[?1004h (win32-input-mode and focus-event reporting, both of
 * which handle_private_mode discards), clears with ESC[2J ESC[H, and sets
 * the window title with an OSC 0 string. Nothing else in the suite feeds
 * the emulator that dialect.
 *
 * The fixture is a real recording made on the maintainer's Windows box on
 * 2026-09-22: Git for Windows bash 5 -- which is what local_shell_resolve()
 * picks on that host, there being no busybox sidecar -- running `ls -l` on a
 * four-entry directory, captured straight off the pseudo-console's pipe.
 * It is a fixture, not a golden master of the emulator: what is asserted is
 * that the *content* survives the dialect.
 *
 * Runs natively, on any host: it is a file of bytes and term_process().
 * ===========================================================================
 */

#define CONPTY_LS_FIXTURE "tests/fixtures/conpty_ls_gitbash.bin"

/* The fixture is read relative to the repo root, which is where `make test`
 * runs from. Returns NULL (and the test skips) if it is not there, the same
 * courtesy test_ui_tokens.c's gates extend to a wrong working directory. */
static char *read_fixture(const char *path, size_t *len_out)
{
    *len_out = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1u, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    *len_out = got;
    return buf;
}

/* The recorded `ls -l` lands on the screen: the total line, every entry with
 * its mode and size, and nothing from the ConPTY control traffic. */
int test_term_conpty_ls_screen_contents(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *bytes = read_fixture(CONPTY_LS_FIXTURE, &len);
    if (!bytes) {
        printf("  [conpty fixture] %s not readable from cwd -- skipping\n",
               CONPTY_LS_FIXTURE);
        TEST_END();
    }
    ASSERT_TRUE(len > 100u);

    Terminal *term = term_init(30, 100, 500);
    ASSERT_NOT_NULL(term);

    /* Fed in two uneven chunks, because the real transport delivers the
     * stream in whatever pieces ReadFile happens to return -- an escape
     * sequence split across a chunk boundary must still parse. */
    size_t first = len / 3u;
    term_process(term, bytes, first);
    term_process(term, bytes + first, len - first);

    char screen[8192];
    size_t n = term_extract_last_n(term, 40, screen, sizeof(screen));
    ASSERT_TRUE(n > 0u);

    ASSERT_TRUE(strstr(screen, "total 3") != NULL);
    ASSERT_TRUE(strstr(screen, "alpha.txt") != NULL);
    ASSERT_TRUE(strstr(screen, "bravo.txt") != NULL);
    ASSERT_TRUE(strstr(screen, "run.sh") != NULL);
    ASSERT_TRUE(strstr(screen, "subdir") != NULL);

    /* Modes and sizes, so this is the real listing and not just the names
     * appearing somewhere. */
    ASSERT_TRUE(strstr(screen, "-rw-r--r--") != NULL);
    ASSERT_TRUE(strstr(screen, "-rwxr-xr-x") != NULL);
    ASSERT_TRUE(strstr(screen, "drwxr-xr-x") != NULL);

    /* No control traffic leaked through as text: no raw ESC, and neither
     * the OSC title string nor a private-mode number on the screen. */
    ASSERT_TRUE(strchr(screen, 0x1B) == NULL);
    ASSERT_TRUE(strstr(screen, "?9001") == NULL);
    ASSERT_TRUE(strstr(screen, "?1004") == NULL);
    ASSERT_TRUE(strstr(screen, "bash.exe") == NULL);

    term_free(term);
    free(bytes);
    TEST_END();
}

/* Byte-at-a-time is the worst case for the state machine and must give the
 * same screen as the two-chunk feed above. */
int test_term_conpty_ls_byte_at_a_time_matches(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *bytes = read_fixture(CONPTY_LS_FIXTURE, &len);
    if (!bytes) {
        printf("  [conpty fixture] %s not readable from cwd -- skipping\n",
               CONPTY_LS_FIXTURE);
        TEST_END();
    }

    Terminal *whole = term_init(30, 100, 500);
    Terminal *drip  = term_init(30, 100, 500);
    ASSERT_NOT_NULL(whole);
    ASSERT_NOT_NULL(drip);

    term_process(whole, bytes, len);
    for (size_t i = 0; i < len; i++) term_process(drip, bytes + i, 1u);

    char a[8192], b[8192];
    size_t na = term_extract_last_n(whole, 40, a, sizeof(a));
    size_t nb = term_extract_last_n(drip,  40, b, sizeof(b));
    ASSERT_TRUE(na > 0u);
    ASSERT_EQ((int)na, (int)nb);
    ASSERT_STR_EQ(a, b);

    term_free(whole);
    term_free(drip);
    free(bytes);
    TEST_END();
}

/* The opening bytes on their own: ConPTY's two private modes are consumed,
 * the screen is cleared, and the cursor goes home -- so a local session
 * starts on a blank screen rather than showing the sequence as text.
 * Recorded on the same host: ESC[?9001h ESC[?1004h ESC[?25l ESC[2J ESC[m
 * ESC[H. No DSR (ESC[6n) or DA (ESC[c / ESC[>c) query appears anywhere in
 * the stream, which is why poll() has no write-back reply hook: the spec
 * made that hook conditional on this recording showing one. */
int test_term_conpty_opening_sequence_is_consumed(void)
{
    TEST_BEGIN();
    static const char OPEN[] =
        "\x1b[?9001h\x1b[?1004h\x1b[?25l\x1b[2J\x1b[m\x1b[H";

    Terminal *term = term_init(24, 80, 200);
    ASSERT_NOT_NULL(term);
    term_process(term, "stale text from before", 22);
    term_process(term, OPEN, sizeof(OPEN) - 1u);

    char screen[4096];
    (void)term_extract_last_n(term, 30, screen, sizeof(screen));
    /* ESC[2J cleared what was there, and nothing was printed as text. */
    ASSERT_TRUE(strstr(screen, "stale text") == NULL);
    ASSERT_TRUE(strstr(screen, "9001") == NULL);
    ASSERT_TRUE(strstr(screen, "1004") == NULL);
    ASSERT_TRUE(strchr(screen, 0x1B) == NULL);
    ASSERT_EQ(term->cursor.row, 0);
    ASSERT_EQ(term->cursor.col, 0);

    /* The fixture itself contains no terminal query. */
    size_t len = 0;
    char *bytes = read_fixture(CONPTY_LS_FIXTURE, &len);
    if (bytes) {
        for (size_t i = 0; i + 3u < len; i++) {
            if (bytes[i] != 0x1B || bytes[i + 1] != '[') continue;
            ASSERT_TRUE(!(bytes[i + 2] == '6' && bytes[i + 3] == 'n'));
            ASSERT_TRUE(bytes[i + 2] != 'c');
            ASSERT_TRUE(!(bytes[i + 2] == '>' && bytes[i + 3] == 'c'));
        }
        free(bytes);
    }

    term_free(term);
    TEST_END();
}
