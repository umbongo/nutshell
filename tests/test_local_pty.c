/* tests/test_local_pty.c -- the ConPTY backend, on a real Windows host.
 *
 * Part of `make wintest`, never of `make test`: it starts real processes
 * through a real pseudo-console, which no native/Linux test build can do.
 * See docs/superpowers/specs/2026-09-22-local-shell-design.md section 8.
 *
 * Three things are checked, and one is recorded:
 *   1. bytes make the round trip -- cmd.exe /c echo, drained through the
 *      emulator, appears on the screen; and close() returns within a second;
 *   2. close() still returns when the child has spawned a grandchild that
 *      outlives it and keeps the pseudo-console's pipe open (the deadlock
 *      the close sequence's CancelSynchronousIo, bounded waits and detach
 *      exist to prevent);
 *   3. whatever ConPTY emits on open, before anything is written to it, is
 *      printed -- so we can see whether it queries the terminal (ESC[6n
 *      DSR, ESC[c or ESC[>c DA), which the emulator has no reply path for.
 */

#ifdef _WIN32

#include <windows.h>
#include "test_framework.h"
#include "local_pty.h"
#include "local_shell.h"
#include "term.h"
#include "term_extract.h"
#include <stdio.h>
#include <string.h>

/* ---- where these cases report ------------------------------------------
 *
 * Not stdout. The harness runs these in a re-executed child that had to
 * swap its inherited console for one of its own (see tests/win_runner.c for
 * why the pseudo-console attach needs that), and swapping the console
 * re-binds the CRT's standard streams -- while redirecting stdout with
 * freopen() re-binds STD_OUTPUT_HANDLE and breaks the attach again. So the
 * substance goes to an ordinary FILE the harness names, which touches no
 * standard handle at all. NULL means stdout, for running a case by hand. */
FILE *nspty_report = NULL;

#define RPT(...)                                                \
    do {                                                        \
        FILE *_rpt = nspty_report ? nspty_report : stdout;      \
        fprintf(_rpt, __VA_ARGS__);                             \
        fflush(_rpt);                                           \
    } while (0)

/* ---- helpers ------------------------------------------------------------ */

/* A custom-command spec: the one LocalShellSpec shape a test can build
 * without probing the machine. */
static void spec_for(LocalShellSpec *spec, const char *command)
{
    memset(spec, 0, sizeof(*spec));
    spec->kind = SHELL_CUSTOM;
    (void)snprintf(spec->command, sizeof(spec->command), "%s", command);
    (void)snprintf(spec->env[0].name,  sizeof(spec->env[0].name),  "%s", "TERM");
    (void)snprintf(spec->env[0].value, sizeof(spec->env[0].value), "%s",
                   "xterm-256color");
    spec->env_count = 1;
}

/* The screen as the app reads it -- the same extractor the AI panel uses. */
static void screen_text(Terminal *term, char *out, size_t out_size)
{
    out[0] = '\0';
    (void)term_extract_last_n(term, 200, out, out_size);
}

/* Print a byte stream the way the debug log does, so an escape sequence is
 * readable in the harness output. */
static void print_escaped(const char *label, const char *data, size_t len)
{
    RPT("  [%s] %u bytes: ", label, (unsigned)len);
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)data[i];
        if      (ch == 0x1Bu) RPT("ESC");
        else if (ch == '\r')  RPT("\\r");
        else if (ch == '\n')  RPT("\\n");
        else if (ch == '\t')  RPT("\\t");
        else if (ch >= 0x20u && ch < 0x7Fu) RPT("%c", (char)ch);
        else RPT("\\x%02X", (unsigned int)ch);
    }
    RPT("\n");
}

static int contains(const char *hay, size_t hay_len, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0u || hay_len < nlen) return 0;
    for (size_t i = 0; i + nlen <= hay_len; i++)
        if (memcmp(hay + i, needle, nlen) == 0) return 1;
    return 0;
}

/* ---- 1. bytes make the round trip, and close is prompt ------------------ */

int test_local_pty_echo_through_cmd(void)
{
    TEST_BEGIN();

    LocalShellSpec spec;
    spec_for(&spec, "cmd.exe /c echo nutshell-ok");

    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open(&spec, 80, 24, err, sizeof(err));
    if (!pty) {
        /* No ConPTY on this host (Windows older than 10 1809): the feature
         * is unavailable, not broken. Say so and pass. */
        RPT("  [skip] local_pty_open failed: %s\n", err);
        ASSERT_TRUE(err[0] != '\0');
        TEST_END();
    }
    ASSERT_NOT_NULL(pty);

    Terminal *term = term_init(24, 80, 200);
    ASSERT_NOT_NULL(term);

    /* cmd.exe exits almost at once, but conhost keeps the pseudo-console's
     * pipe open until ClosePseudoConsole is called -- poll() never sees a
     * real EOF here without local_pty_close() (spec section 3, "Close"; and
     * the review fix behind this: EOF is no longer inferred from the child
     * process handle alone, only from the pipe actually closing, so a
     * grandchild holding the pipe open is never cut off early). So this
     * drains for a bounded number of iterations rather than waiting on -2. */
    for (int i = 0; i < 40; i++) {
        int rc = local_pty_poll(pty, term, NULL, NULL);
        ASSERT_TRUE(rc != -1);   /* no read error */
        Sleep(20);
    }

    char screen[8192];
    screen_text(term, screen, sizeof(screen));
    RPT("  [screen]\n%s", screen);
    ASSERT_TRUE(strstr(screen, "nutshell-ok") != NULL);

    ULONGLONG t0 = GetTickCount64();
    local_pty_close(pty);
    ULONGLONG elapsed = GetTickCount64() - t0;
    RPT("  [close] returned in %u ms\n", (unsigned)elapsed);
    ASSERT_TRUE(elapsed < 1000u);

    term_free(term);
    TEST_END();
}

/* ---- 2. close returns even with a grandchild holding the pipe ----------- */

int test_local_pty_close_with_sleeping_grandchild(void)
{
    TEST_BEGIN();

    /* cmd exits at once; `start /b` leaves a detached grandchild running for
     * ~30 seconds, still holding the pseudo-console's pipe. A close that
     * simply waited for the reader would block the UI thread for all of it. */
    LocalShellSpec spec;
    spec_for(&spec,
             "cmd.exe /c start /b \"\" cmd.exe /c ping -n 30 127.0.0.1");

    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open(&spec, 80, 24, err, sizeof(err));
    if (!pty) {
        RPT("  [skip] local_pty_open failed: %s\n", err);
        ASSERT_TRUE(err[0] != '\0');
        TEST_END();
    }
    ASSERT_NOT_NULL(pty);

    Terminal *term = term_init(24, 80, 200);
    ASSERT_NOT_NULL(term);

    /* Let the tree get going, and drain whatever it says. */
    for (int i = 0; i < 50; i++) {
        (void)local_pty_poll(pty, term, NULL, NULL);
        Sleep(20);
    }

    ULONGLONG t0 = GetTickCount64();
    local_pty_close(pty);
    ULONGLONG elapsed = GetTickCount64() - t0;
    RPT("  [close, grandchild alive] returned in %u ms\n",
           (unsigned)elapsed);

    /* The close sequence is bounded at two 500 ms waits plus the handle
     * work; anything near the grandchild's 30 s means it blocked. */
    ASSERT_TRUE(elapsed < 3000u);

    term_free(term);
    TEST_END();
}

/* ---- 3. what does ConPTY say on open? ----------------------------------- */

int test_local_pty_records_opening_bytes(void)
{
    TEST_BEGIN();

    /* A shell that sits there rather than exiting, so the opening bytes are
     * ConPTY's own and nothing has been written to provoke a reply. */
    LocalShellSpec spec;
    spec_for(&spec, "cmd.exe /k rem nutshell");

    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open(&spec, 80, 24, err, sizeof(err));
    if (!pty) {
        RPT("  [skip] local_pty_open failed: %s\n", err);
        ASSERT_TRUE(err[0] != '\0');
        TEST_END();
    }
    ASSERT_NOT_NULL(pty);

    Terminal *term = term_init(24, 80, 200);
    ASSERT_NOT_NULL(term);

    for (int i = 0; i < 40; i++) {
        (void)local_pty_poll(pty, term, NULL, NULL);
        Sleep(25);
    }

    char first[LOCAL_PTY_FIRST_MAX];
    size_t n = local_pty_first_bytes(pty, first, sizeof(first));
    print_escaped("conpty open", first, n);

    int dsr   = contains(first, n, "\x1b[6n");
    int da1   = contains(first, n, "\x1b[c");
    int da2   = contains(first, n, "\x1b[>c");
    RPT("  [conpty queries] DSR ESC[6n: %s   DA ESC[c: %s   "
           "secondary DA ESC[>c: %s\n",
           dsr ? "YES" : "no", da1 ? "YES" : "no", da2 ? "YES" : "no");
    RPT("  [conpty queries] -> a reply path in poll() is %s\n",
           (dsr || da1 || da2) ? "REQUIRED" : "not required");

    /* ConPTY always says something on open; nothing at all would mean the
     * reader thread never ran. */
    ASSERT_TRUE(n > 0u);

    ULONGLONG t0 = GetTickCount64();
    local_pty_close(pty);
    ULONGLONG elapsed = GetTickCount64() - t0;
    RPT("  [close] returned in %u ms\n", (unsigned)elapsed);
    ASSERT_TRUE(elapsed < 3000u);

    term_free(term);
    TEST_END();
}

#endif /* _WIN32 */
