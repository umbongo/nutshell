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
 *
 * Cases 4-6 record what the special-keys spec (2026-09-23, section 9) left
 * unverified: ConPTY's mapping of xterm key bytes to console key events,
 * whether ConPTY forwards ?1049h and ?1h from full-screen programs (Windows's
 * own edit.exe -- there being no busybox sidecar any more, 2026-09-24 local
 * shell hardening), and what the loaded keyboard layouts make of Ctrl+Space,
 * Ctrl+Shift+Space and Ctrl+Alt+F.
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

/* A tiny real LocalShellProbe -- just enough of local_shell_probe.c's real
 * Win32 callbacks (that file itself isn't in WIN_TEST_SRCS) for
 * local_shell_resolve_bare()'s bare-name search (System32, the Windows
 * directory, PATH) to work against the real machine these wintest cases
 * already run real processes on. No registry_string: nothing here resolves
 * a bare name that would need it. */
static int probe_exists(void *ctx, const char *path)
{
    (void)ctx;
    if (!path || !path[0]) return 0;
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES &&
            !(attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

static int probe_env(void *ctx, const char *name, char *out, size_t out_size)
{
    (void)ctx;
    if (!name || !out || out_size == 0u) return 0;
    out[0] = '\0';
    DWORD n = GetEnvironmentVariableA(name, out, (DWORD)out_size);
    if (n == 0u || n >= (DWORD)out_size) { out[0] = '\0'; return 0; }
    return out[0] ? 1 : 0;
}

/* A custom-command spec, resolved exactly the way window.c's
 * start_local_shell() resolves one -- local_shell_resolve() to split
 * `command` into spec->exe/dir/command, then local_shell_resolve_bare()
 * to turn a bare name (e.g. "cmd.exe") into the absolute path
 * local_pty_open() now requires (M1: an empty spec->exe is a refusal, not
 * a NULL-lpApplicationName fallback) -- rather than leaving spec->exe
 * empty, which every caller in production always resolves first. */
static void spec_for(LocalShellSpec *spec, const char *command)
{
    LocalShellProbe probe;
    memset(&probe, 0, sizeof(probe));
    probe.exists = probe_exists;
    probe.env    = probe_env;

    memset(spec, 0, sizeof(*spec));
    (void)local_shell_resolve(command, &probe, spec);
    (void)local_shell_resolve_bare(spec, &probe);

    /* fill_env() inside the calls above already added TERM (and NUTSHELL);
     * these cases only ever look at the emulator's own output, not the
     * child's environment, so that's harmless -- kept only for the log
     * line a failed resolve prints via spec->error. */
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

/* ---- 4-7. special keys: what ConPTY and the layout do with them ---------
 *
 * docs/superpowers/specs/2026-09-23-special-keys-design.md section 9 lists
 * what was not verified during research. These cases record it; apart from
 * "the child ran", they assert nothing about the answers, which are facts
 * about this Windows build to be read in the output. */

/* Directory of this executable, with a trailing backslash. */
static int self_dir(char *out, size_t out_size)
{
    char self[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, self, (DWORD)sizeof(self));
    if (n == 0u || n >= sizeof(self)) return 0;
    char *slash = strrchr(self, '\\');
    if (!slash) return 0;
    slash[1] = '\0';
    (void)snprintf(out, out_size, "%s", self);
    return 1;
}

static int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void drain(LocalPty *pty, Terminal *term, FILE *dbg, int ms)
{
    for (int waited = 0; waited < ms; waited += 25) {
        (void)local_pty_poll(pty, term, NULL, dbg);
        Sleep(25);
    }
}

static int wait_for_screen(LocalPty *pty, Terminal *term, const char *needle,
                           int ms)
{
    static char screen[16384];
    for (int waited = 0; waited < ms; waited += 50) {
        (void)local_pty_poll(pty, term, NULL, NULL);
        screen_text(term, screen, sizeof(screen));
        if (strstr(screen, needle)) return 1;
        Sleep(50);
    }
    return 0;
}

/* The key dump child: `test_runner.exe --dump-keys` (tests/win_runner.c)
 * lands here. It reads raw console input records -- what a console program
 * such as Edit sees -- and prints one line per key-down, so the parent,
 * which writes xterm bytes into the pseudo-console, sees how ConPTY's input
 * parser turned each one into a key event. 'Q' ends it. */
int nspty_key_dump_main(void)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    (void)GetConsoleMode(in, &mode);
    /* No processed input (Ctrl+C is a key), no line or echo input, and no
     * VT input: ConPTY must translate the bytes into key events. */
    (void)SetConsoleMode(in, ENABLE_EXTENDED_FLAGS);
    printf("KEYDUMP READY (input mode was 0x%04lX)\r\n", (unsigned long)mode);
    fflush(stdout);
    ULONGLONG deadline = GetTickCount64() + 30000u;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(in, 200u) != WAIT_OBJECT_0) continue;
        INPUT_RECORD rec[16];
        DWORD n = 0;
        if (!ReadConsoleInputW(in, rec, 16u, &n)) break;
        for (DWORD i = 0; i < n; i++) {
            if (rec[i].EventType != KEY_EVENT) continue;
            const KEY_EVENT_RECORD *k = &rec[i].Event.KeyEvent;
            if (!k->bKeyDown) continue;
            unsigned int ch = (unsigned int)k->uChar.UnicodeChar;
            if (ch == (unsigned int)'Q') {
                printf("KEYDUMP END\r\n");
                fflush(stdout);
                return 0;
            }
            DWORD st = k->dwControlKeyState;
            printf("K vk=%02X ch=%04X mods=%s%s%s%s\r\n",
                   (unsigned int)k->wVirtualKeyCode, ch,
                   (st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) ? "C" : "",
                   (st & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))   ? "A" : "",
                   (st & SHIFT_PRESSED)                             ? "S" : "",
                   (st & ENHANCED_KEY)                              ? "+ext" : "");
            fflush(stdout);
        }
    }
    printf("KEYDUMP TIMEOUT\r\n");
    fflush(stdout);
    return 0;
}

typedef struct { const char *label; const char *bytes; size_t len; } KeyInput;

static const KeyInput KEY_INPUTS[] = {
    { "0x7F (DEL)",        "\x7f",            1 },
    { "0x08 (BS)",         "\x08",            1 },
    { "0x00 (NUL)",        "\0",              1 },
    { "ESC f",             "\x1b" "f",        2 },
    { "CSI 1;5A",          "\x1b[1;5A",       6 },
    { "CSI Z",             "\x1b[Z",          3 },
    { "SS3 P",             "\x1bOP",          3 },
    { "CSI 1;5P",          "\x1b[1;5P",       6 },
    { "CSI 21~ (F10)",     "\x1b[21~",        5 },
    { "CSI 23;2~ (S-F11)", "\x1b[23;2~",      7 },
    { "CSI 5~ (PgUp)",     "\x1b[5~",         4 },
    { "CSI 1;3D (A-Left)", "\x1b[1;3D",       6 },
    { "SS3 H (app Home)",  "\x1bOH",          3 },
    { "ESC 0x06 (C-A-f)",  "\x1b\x06",        2 },
    { "CR (Enter)",        "\r",              1 },
    { "lone ESC",          "\x1b",            1 },
};
#define KEY_INPUT_COUNT (sizeof(KEY_INPUTS) / sizeof(KEY_INPUTS[0]))

/* 4. xterm bytes in, console key events out. */
int test_local_pty_records_key_mapping(void)
{
    TEST_BEGIN();

    char dir[MAX_PATH];
    ASSERT_TRUE(self_dir(dir, sizeof(dir)));
    char cmd[MAX_PATH * 2];
    char self[MAX_PATH];
    (void)GetModuleFileNameA(NULL, self, (DWORD)sizeof(self));
    (void)snprintf(cmd, sizeof(cmd), "\"%s\" --dump-keys", self);

    LocalShellSpec spec;
    spec_for(&spec, cmd);
    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open(&spec, 100, 60, err, sizeof(err));
    if (!pty) {
        RPT("  [skip] local_pty_open failed: %s\n", err);
        ASSERT_TRUE(err[0] != '\0');
        TEST_END();
    }
    Terminal *term = term_init(60, 100, 500);
    ASSERT_NOT_NULL(term);

    int ready = wait_for_screen(pty, term, "KEYDUMP READY", 5000);
    ASSERT_TRUE(ready);

    /* Each input is preceded by a "." marker (unshifted on US and UK
     * layouts, so no Shift events around it), so the events it produced can
     * be told apart; the lone ESC gets 600 ms to show ConPTY's timeout. */
    for (size_t i = 0; i < KEY_INPUT_COUNT && ready; i++) {
        (void)local_pty_write(pty, ".", 1);
        drain(pty, term, NULL, 150);
        (void)local_pty_write(pty, KEY_INPUTS[i].bytes, KEY_INPUTS[i].len);
        drain(pty, term, NULL, (i + 1 == KEY_INPUT_COUNT) ? 600 : 250);
    }
    (void)local_pty_write(pty, ".Q", 2);
    (void)wait_for_screen(pty, term, "KEYDUMP END", 3000);

    static char screen[16384];
    screen_text(term, screen, sizeof(screen));

    /* Group the key lines by marker and print them against their input. */
    RPT("  [conpty key mapping] input bytes -> console key-down events\n");
    int group = -1;
    char *line = screen;
    while (line && *line) {
        char *eol = strpbrk(line, "\r\n");
        if (eol) *eol = '\0';
        if (strncmp(line, "K ", 2) == 0) {
            if (strstr(line, "ch=002E")) {          /* a marker */
                group++;
                RPT("%s    %-18s :", group > 0 ? "\n" : "",
                    group < (int)KEY_INPUT_COUNT ? KEY_INPUTS[group].label
                                                 : "(end marker)");
            } else {
                RPT(" [%s]", line + 2);
            }
        } else if (strstr(line, "KEYDUMP")) {
            RPT("%s    %s", group >= 0 ? "\n" : "", line);
            if (group < 0) RPT("\n");
        }
        if (!eol) break;
        line = eol + 1;
        while (*line == '\r' || *line == '\n') line++;
    }
    RPT("\n");

    local_pty_close(pty);
    term_free(term);
    TEST_END();
}

/* Run a full-screen console program under the pseudo-console and record
 * whether Nutshell's emulator is told about the alternate screen (?1049h)
 * and application cursor keys (?1h) -- whether ConPTY forwards them. */
static void record_fullscreen(const char *label, const char *command,
                              const char *quit_bytes, size_t quit_len)
{
    LocalShellSpec spec;
    spec_for(&spec, command);
    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open(&spec, 100, 30, err, sizeof(err));
    if (!pty) {
        RPT("  [%s] local_pty_open failed: %s\n", label, err);
        return;
    }
    Terminal *term = term_init(30, 100, 200);
    if (!term) { local_pty_close(pty); return; }

    char dbg_path[MAX_PATH * 2], tmp_dir[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    if (n == 0u || n >= sizeof(tmp_dir)) tmp_dir[0] = '\0';
    (void)snprintf(dbg_path, sizeof(dbg_path), "%snutshell_wintest_fs.txt", tmp_dir);
    FILE *dbg = fopen(dbg_path, "w");

    drain(pty, term, dbg, 2500);
    bool alt_on = term->alt_screen_active;
    bool app_on = term->app_cursor_keys;

    (void)local_pty_write(pty, quit_bytes, quit_len);
    drain(pty, term, dbg, 1000);
    bool alt_after = term->alt_screen_active;
    if (dbg) fclose(dbg);

    static char raw[65536];
    size_t got = 0;
    FILE *f = fopen(dbg_path, "rb");
    if (f) {
        got = fread(raw, 1u, sizeof(raw) - 1u, f);
        fclose(f);
    }
    raw[got] = '\0';
    DeleteFileA(dbg_path);

    RPT("  [%s] while running: alt_screen_active=%s app_cursor_keys=%s; "
        "after quit: alt_screen_active=%s\n", label,
        alt_on ? "YES" : "no", app_on ? "YES" : "no", alt_after ? "YES" : "no");
    RPT("  [%s] raw output (%u bytes) contains: ?1049h %s, ?1049l %s, ?1h %s, "
        "?1l %s, ?47h %s, ?1047h %s, ?9001h %s, ?1004h %s\n", label,
        (unsigned)got,
        strstr(raw, "ESC[?1049h") ? "YES" : "no",
        strstr(raw, "ESC[?1049l") ? "YES" : "no",
        strstr(raw, "ESC[?1h")    ? "YES" : "no",
        strstr(raw, "ESC[?1l")    ? "YES" : "no",
        strstr(raw, "ESC[?47h")   ? "YES" : "no",
        strstr(raw, "ESC[?1047h") ? "YES" : "no",
        strstr(raw, "ESC[?9001h") ? "YES" : "no",
        strstr(raw, "ESC[?1004h") ? "YES" : "no");
    /* The first few hundred bytes, for eyeballing the mode sets. */
    print_escaped(label, raw, got < 400u ? got : 400u);

    local_pty_close(pty);
    term_free(term);
}

/* 6. Does ConPTY forward ?1049h / ?1h? Windows's own edit.exe. */
int test_local_pty_records_fullscreen_modes(void)
{
    TEST_BEGIN();

    char cmd[MAX_PATH * 3];
    char sysdir[MAX_PATH], edit[MAX_PATH * 2];
    UINT sn = GetSystemDirectoryA(sysdir, (UINT)sizeof(sysdir));
    if (sn > 0u && sn < sizeof(sysdir)) {
        (void)snprintf(edit, sizeof(edit), "%s\\edit.exe", sysdir);
        if (file_exists(edit)) {
            (void)snprintf(cmd, sizeof(cmd), "\"%s\"", edit);
            record_fullscreen("edit.exe", cmd, "\x11", 1);   /* Ctrl+Q quits */
        } else {
            RPT("  [skip] edit.exe: no %s\n", edit);
        }
    }
    TEST_END();
}

/* ---- 7. does the dispatcher's readiness gate ever recover after a
 * PowerShell command errors, sent with no Ctrl+E Ctrl+U prefix? ----------
 *
 * Regression (2026-09-25, reported against ai-dispatch-shell-prefix
 * f5bbfa8): a real local PowerShell tab, single-command batch
 * `Get-Item C:\this-path-does-not-exist-12345`. PowerShell prints the
 * ItemNotFoundException and returns to its prompt, but the AI panel never
 * settles -- "running" forever, no continue message. Mirrors
 * dispatch_tick()'s own readiness gate (write_seq quiet for
 * PROMPT_QUIET_MS, then term_at_prompt()) against a real pseudo-console,
 * the same way execute_command() writes for a no-prefix shell: the
 * command text, then a bare "\r", with no "\x05\x15" ahead of it. */
int test_local_pty_powershell_settles_after_command_error(void)
{
    TEST_BEGIN();

    LocalShellSpec spec;
    spec_for(&spec, "powershell.exe -NoLogo -NoExit");

    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open(&spec, 100, 30, err, sizeof(err));
    if (!pty) {
        RPT("  [skip] local_pty_open failed: %s\n", err);
        ASSERT_TRUE(err[0] != '\0');
        TEST_END();
    }
    ASSERT_NOT_NULL(pty);

    Terminal *term = term_init(30, 100, 500);
    ASSERT_NOT_NULL(term);

    /* Drain the banner and let the first prompt settle, using the same
     * "quiet since the last write_seq change, then term_at_prompt()"
     * shape dispatch_tick() itself uses (PROMPT_QUIET_MS is 400 ms in
     * ai_chat.c) -- bounded generously since a first PowerShell launch on
     * a cold profile can be slow. */
    unsigned long last_seq = term->write_seq;
    ULONGLONG last_change = GetTickCount64();
    ULONGLONG deadline = GetTickCount64() + 15000u;
    while (GetTickCount64() < deadline) {
        (void)local_pty_poll(pty, term, NULL, NULL);
        if (term->write_seq != last_seq) {
            last_seq = term->write_seq;
            last_change = GetTickCount64();
        }
        if (GetTickCount64() - last_change >= 400u && term_at_prompt(term))
            break;
        Sleep(20);
    }
    char screen[8192];
    screen_text(term, screen, sizeof(screen));
    RPT("  [initial prompt]\n%s", screen);
    RPT("  [cursor row=%d col=%d alt_screen=%d]\n",
        term->cursor.row, term->cursor.col, (int)term->alt_screen_active);
    if (!term_at_prompt(term)) {
        local_pty_close(pty);
        term_free(term);
        RPT("  [skip] initial PowerShell prompt never settled -- nothing "
            "to regress-test here\n");
        TEST_END();
    }

    /* The command execute_command() would send for a no-prefix shell:
     * the text, then Enter, nothing ahead of it. */
    static const char CMD[] = "Get-Item C:\\this-path-does-not-exist-12345";
    ASSERT_TRUE(local_pty_write(pty, CMD, sizeof(CMD) - 1) >= 0);
    ASSERT_TRUE(local_pty_write(pty, "\r", 1) >= 0);

    /* dispatch_tick()'s own readiness gate: quiet for 400 ms since the
     * last write_seq change, and term_at_prompt() true. Bounded at 10 s --
     * production's own ambiguous-prompt cancel is 5 s, so anything not
     * settled by 10 s here is the "never settles" bug, not a slow host. */
    last_seq = term->write_seq;
    last_change = GetTickCount64();
    int settled = 0;
    int seq_changes = 0;
    deadline = GetTickCount64() + 10000u;
    while (GetTickCount64() < deadline) {
        (void)local_pty_poll(pty, term, NULL, NULL);
        if (term->write_seq != last_seq) {
            last_seq = term->write_seq;
            last_change = GetTickCount64();
            seq_changes++;
        }
        if (GetTickCount64() - last_change >= 400u && term_at_prompt(term)) {
            settled = 1;
            break;
        }
        Sleep(20);
    }

    screen_text(term, screen, sizeof(screen));
    RPT("  [after error -- settled=%d, write_seq changes after send=%d]\n%s",
        settled, seq_changes, screen);
    RPT("  [cursor row=%d col=%d alt_screen=%d write_seq=%lu]\n",
        term->cursor.row, term->cursor.col, (int)term->alt_screen_active,
        term->write_seq);

    ASSERT_TRUE(strstr(screen, "ItemNotFoundException") != NULL ||
                strstr(screen, "does not exist") != NULL);
    ASSERT_TRUE(settled);

    /* Guard against the other suspect the regression report named: ConPTY
     * spontaneously nudging write_seq while genuinely idle at the prompt
     * (a repaint/blink) would make "quiet" never hold in the live app even
     * though this harness just saw it settle once. It does not, on this
     * host -- kept as a real assertion, not just a printed observation. */
    {
        unsigned long idle_seq = term->write_seq;
        int idle_changes = 0;
        ULONGLONG idle_deadline = GetTickCount64() + 3000u;
        while (GetTickCount64() < idle_deadline) {
            (void)local_pty_poll(pty, term, NULL, NULL);
            if (term->write_seq != idle_seq) {
                idle_seq = term->write_seq;
                idle_changes++;
            }
            Sleep(20);
        }
        RPT("  [idle 3s at prompt] write_seq changes=%d, final write_seq=%lu\n",
            idle_changes, term->write_seq);
        ASSERT_EQ(idle_changes, 0);
    }

    local_pty_close(pty);
    term_free(term);
    TEST_END();
}

/* 7. What character does the layout make of Ctrl+Space, Ctrl+Shift+Space
 * and Ctrl+Alt+F, for every layout loaded in this session? (What WM_CHAR
 * the window gets; ToUnicodeEx with flag 0x4 leaves the keyboard state
 * alone.) */
int test_key_layout_records_ctrl_space(void)
{
    TEST_BEGIN();

    HKL layouts[16];
    int count = GetKeyboardLayoutList(16, layouts);
    ASSERT_TRUE(count > 0);
    static const struct { const char *label; UINT vk; BYTE mods[3]; } PROBES[] = {
        { "Ctrl+Space",       VK_SPACE, { VK_CONTROL, 0, 0 } },
        { "Ctrl+Shift+Space", VK_SPACE, { VK_CONTROL, VK_SHIFT, 0 } },
        { "Ctrl+Alt+F",       'F',      { VK_CONTROL, VK_MENU, 0 } },
    };
    for (int li = 0; li < count; li++) {
        RPT("  [layout %08lX]", (unsigned long)(ULONG_PTR)layouts[li]);
        for (size_t p = 0; p < sizeof(PROBES) / sizeof(PROBES[0]); p++) {
            BYTE ks[256];
            memset(ks, 0, sizeof(ks));
            for (int m = 0; m < 3 && PROBES[p].mods[m]; m++)
                ks[PROBES[p].mods[m]] = 0x80;
            UINT scan = MapVirtualKeyExA(PROBES[p].vk, 0 /* MAPVK_VK_TO_VSC */,
                                         layouts[li]);
            WCHAR buf[8];
            int rc = ToUnicodeEx(PROBES[p].vk, scan, ks, buf, 8, 0x4u,
                                 layouts[li]);
            if (rc > 0)
                RPT("  %s -> 0x%04X", PROBES[p].label, (unsigned int)buf[0]);
            else
                RPT("  %s -> none (rc %d)", PROBES[p].label, rc);
        }
        RPT("\n");
    }
    TEST_END();
}

#endif /* _WIN32 */
