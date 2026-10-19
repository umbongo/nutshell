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
#include "dlog.h"
#include "conpty_expect.h"
#include <stdio.h>
#include <stdlib.h>
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
        int rc = local_pty_poll(pty, term, NULL);
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
        (void)local_pty_poll(pty, term, NULL);
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
        (void)local_pty_poll(pty, term, NULL);
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

static void drain(LocalPty *pty, Terminal *term, int ms)
{
    for (int waited = 0; waited < ms; waited += 25) {
        (void)local_pty_poll(pty, term, NULL);
        Sleep(25);
    }
}

static int wait_for_screen(LocalPty *pty, Terminal *term, const char *needle,
                           int ms)
{
    static char screen[16384];
    for (int waited = 0; waited < ms; waited += 50) {
        (void)local_pty_poll(pty, term, NULL);
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
        drain(pty, term, 150);
        (void)local_pty_write(pty, KEY_INPUTS[i].bytes, KEY_INPUTS[i].len);
        drain(pty, term, (i + 1 == KEY_INPUT_COUNT) ? 600 : 250);
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

    /* The raw byte stream comes from the debug log's recorder: the poll
     * writes every chunk it feeds the emulator as a term.rx event (Trace,
     * "Include terminal content" on), with ESC shown as \e. The files go to
     * the temp folder. */
    char tmp_dir[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    if (n == 0u || n >= sizeof(tmp_dir)) tmp_dir[0] = '\0';
    {
        DlogStartInfo si;
        memset(&si, 0, sizeof(si));
        si.exe_dir = tmp_dir;
        si.exe_path = "test_runner.exe";
        si.version = "wintest";
        si.pid = (unsigned)GetCurrentProcessId();
        (void)dlog_start(&si);
        DlogConfig cfg;
        dlog_config_defaults(&cfg);
        cfg.enabled = 1;
        cfg.levels[DLOG_TERM] = DLOG_LVL_TRACE;
        cfg.include_content = 1;
        cfg.recorder = 1;
        cfg.recorder_level = DLOG_LVL_TRACE;
        (void)snprintf(cfg.dir, sizeof(cfg.dir), "%.200snutshell_wintest_logs", tmp_dir);
        dlog_apply(&cfg);
    }

    drain(pty, term, 2500);
    bool alt_on = term->alt_screen_active;
    bool app_on = term->app_cursor_keys;

    (void)local_pty_write(pty, quit_bytes, quit_len);
    drain(pty, term, 1000);
    bool alt_after = term->alt_screen_active;

    static char raw[262144];
    size_t got = dlog_recorder_copy(raw, sizeof(raw) - 1u);
    raw[got] = '\0';
    dlog_shutdown();

    RPT("  [%s] while running: alt_screen_active=%s app_cursor_keys=%s; "
        "after quit: alt_screen_active=%s\n", label,
        alt_on ? "YES" : "no", app_on ? "YES" : "no", alt_after ? "YES" : "no");
    RPT("  [%s] raw output (%u bytes) contains: ?1049h %s, ?1049l %s, ?1h %s, "
        "?1l %s, ?47h %s, ?1047h %s, ?9001h %s, ?1004h %s\n", label,
        (unsigned)got,
        strstr(raw, "\\e[?1049h") ? "YES" : "no",
        strstr(raw, "\\e[?1049l") ? "YES" : "no",
        strstr(raw, "\\e[?1h")    ? "YES" : "no",
        strstr(raw, "\\e[?1l")    ? "YES" : "no",
        strstr(raw, "\\e[?47h")   ? "YES" : "no",
        strstr(raw, "\\e[?1047h") ? "YES" : "no",
        strstr(raw, "\\e[?9001h") ? "YES" : "no",
        strstr(raw, "\\e[?1004h") ? "YES" : "no");
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
        (void)local_pty_poll(pty, term, NULL);
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
        (void)local_pty_poll(pty, term, NULL);
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
            (void)local_pty_poll(pty, term, NULL);
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

/* ---- 8. CreatePseudoConsole flags and the raw tap ---------------------
 *
 * Proves two things only: local_pty_open_ex() accepts each of the two flag
 * values (0 and PSEUDOCONSOLE_RESIZE_QUIRK) -- or the refusal is reported
 * with a reason and passes, as a missing ConPTY does in case 1 -- and the
 * raw tap sees the bytes poll() feeds the emulator. It does not prove that
 * conhost honours the flag; the console probe measures that. */
int test_local_pty_open_ex_passes_pcon_flags(void)
{
    TEST_BEGIN();

    char tmp_dir[MAX_PATH];
    DWORD tn = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    int tmp_ok = (tn > 0u && tn < sizeof(tmp_dir));
    if (!tmp_ok) RPT("  GetTempPathA failed or overflowed (%lu)\n", (unsigned long)tn);
    ASSERT_TRUE(tmp_ok);
    char tap_path[MAX_PATH * 2];
    (void)snprintf(tap_path, sizeof(tap_path), "%snsprobe_tap_check.bin",
                   tmp_dir);

    static const unsigned long FLAGS[2] = { 0ul, PSEUDOCONSOLE_RESIZE_QUIRK };
    for (int f = 0; f < 2; f++) {
        LocalShellSpec spec;
        spec_for(&spec, "cmd.exe /c echo nutshell-flags");
        char err[512];
        err[0] = '\0';
        LocalPty *pty = local_pty_open_ex(&spec, 80, 24, 0u, FLAGS[f],
                                          err, sizeof(err));
        if (!pty) {
            RPT("  [skip] flags 0x%lx: local_pty_open_ex failed: %s\n",
                FLAGS[f], err);
            int has_reason = (err[0] != '\0');
            if (!has_reason)
                RPT("  flags 0x%lx: the open failed with no reason\n", FLAGS[f]);
            ASSERT_TRUE(has_reason);
            continue;
        }
        FILE *tap = fopen(tap_path, "w+b");
        if (!tap) {
            RPT("  flags 0x%lx: cannot create the tap file %s\n", FLAGS[f],
                tap_path);
            local_pty_close(pty);
            ASSERT_NOT_NULL(tap);       /* records the failure; does not return */
            continue;
        }
        local_pty_set_raw_tap(pty, tap);

        Terminal *term = term_init(24, 80, 200);
        if (!term) {
            RPT("  flags 0x%lx: term_init failed\n", FLAGS[f]);
            local_pty_close(pty);
            fclose(tap);
            (void)DeleteFileA(tap_path);
            ASSERT_NOT_NULL(term);      /* records the failure; does not return */
            continue;
        }
        int seen = wait_for_screen(pty, term, "nutshell-flags", 5000);
        local_pty_close(pty);

        static char raw[65536];
        (void)fseek(tap, 0L, SEEK_SET);
        size_t got = fread(raw, 1u, sizeof(raw) - 1u, tap);
        raw[got] = '\0';
        fclose(tap);
        (void)DeleteFileA(tap_path);
        int in_tap = contains(raw, got, "nutshell-flags");
        RPT("  [flags 0x%lx] echo %s; tap holds %u bytes%s\n", FLAGS[f],
            seen ? "seen" : "NOT seen", (unsigned)got,
            in_tap ? "" : " (marker NOT in the tap)");
        term_free(term);
        ASSERT_TRUE(seen);
        ASSERT_TRUE(in_tap);
    }
    TEST_END();
}


/* ---- 9-10. the console probe (resize spec section 0) -------------------
 *
 * docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md, section
 * 0 and Tests, "Probe" and "Fixtures". `test_runner.exe --console-probe
 * <scenario> <dumpfile>` (tests/win_runner.c) runs as the pseudo-console's
 * own process: it writes a scenario from tests/conpty_expect.c to its
 * console, puts the cursor where the scenario says, appends dump 0 to the
 * dump file, and then on each 'D' appends the next dump -- every buffer
 * row (GetConsoleScreenBufferInfo, ReadConsoleOutputCharacterW) and the
 * cursor -- and on 'Q' exits. Nothing is typed as a command.
 *
 * Dump format, one block per dump, rows trimmed of trailing spaces:
 *   DUMP <n>
 *   size=<X>x<Y> window=<left>,<top>,<right>,<bottom> cursor=<row>,<col>
 *   R<row, 4 digits>|<text>|        (one per buffer row)
 *   END <n>
 */

static void probe_write(HANDLE out, const char *text)
{
    DWORD wrote = 0;
    (void)WriteConsoleA(out, text, (DWORD)strlen(text), &wrote, NULL);
}

static void probe_dump(HANDLE out, const char *path, int n)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if (!GetConsoleScreenBufferInfo(out, &bi)) return;
    FILE *f = fopen(path, "ab");
    if (!f) return;
    fprintf(f, "DUMP %d\n", n);
    fprintf(f, "size=%dx%d window=%d,%d,%d,%d cursor=%d,%d\n",
            (int)bi.dwSize.X, (int)bi.dwSize.Y,
            (int)bi.srWindow.Left, (int)bi.srWindow.Top,
            (int)bi.srWindow.Right, (int)bi.srWindow.Bottom,
            (int)bi.dwCursorPosition.Y, (int)bi.dwCursorPosition.X);
    /* Every buffer row while the buffer is small (under ConPTY it is the
     * viewport); only the window's rows if it is not. */
    int first = 0;
    int last = (int)bi.dwSize.Y - 1;
    if (bi.dwSize.Y > 200) {
        first = (int)bi.srWindow.Top;
        last = (int)bi.srWindow.Bottom;
    }
    int width = (int)bi.dwSize.X;
    if (width > 512) width = 512;
    for (int y = first; y <= last; y++) {
        WCHAR wrow[512];
        char row[513];
        DWORD got = 0;
        COORD at;
        at.X = 0;
        at.Y = (SHORT)y;
        if (!ReadConsoleOutputCharacterW(out, wrow, (DWORD)width, at, &got))
            got = 0;
        int k = 0;
        for (DWORD i = 0; i < got && k < 512; i++) {
            WCHAR w = wrow[i];
            row[k++] = (w >= 0x20 && w < 0x7F) ? (char)w : (w == 0 ? ' ' : '?');
        }
        while (k > 0 && row[k - 1] == ' ') k--;
        row[k] = '\0';
        fprintf(f, "R%04d|%s|\n", y, row);
    }
    fprintf(f, "END %d\n", n);
    fclose(f);
}

/* The probe child: `test_runner.exe --console-probe <scenario> <dumpfile>`
 * lands here (tests/win_runner.c). Returns 0 on 'Q', 2 for an unknown
 * scenario, 3 when no 'Q' came within a minute. */
int nsprobe_console_main(const char *scenario, const char *dump_path)
{
    const CpxScenario *s = cpx_scenario_find(scenario);
    if (!s || !dump_path) return 2;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE in  = GetStdHandle(STD_INPUT_HANDLE);
    /* No processed, line or echo input: 'D' and 'Q' are plain keys. */
    (void)SetConsoleMode(in, ENABLE_EXTENDED_FLAGS);

    for (int i = 0; i < s->line_count; i++) {
        if (i > 0) probe_write(out, "\r\n");
        probe_write(out, s->lines[i]);
    }
    /* The cursor where the scenario puts it -- for shrink_below, back up
     * above the content written below it. */
    CpxView v;
    if (cpx_initial_view(s, &v) == 0) {
        CONSOLE_SCREEN_BUFFER_INFO bi;
        if (GetConsoleScreenBufferInfo(out, &bi)) {
            COORD at;
            at.X = (SHORT)v.cursor_col;
            at.Y = (SHORT)(bi.srWindow.Top + v.cursor_row);
            (void)SetConsoleCursorPosition(out, at);
        }
    }

    int dumps = 0;
    probe_dump(out, dump_path, dumps++);   /* dump 0: the ready signal */

    ULONGLONG deadline = GetTickCount64() + 60000u;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(in, 200u) != WAIT_OBJECT_0) continue;
        INPUT_RECORD rec[16];
        DWORD n = 0;
        if (!ReadConsoleInputW(in, rec, 16u, &n)) break;
        for (DWORD i = 0; i < n; i++) {
            if (rec[i].EventType != KEY_EVENT) continue;
            if (!rec[i].Event.KeyEvent.bKeyDown) continue;
            WCHAR ch = rec[i].Event.KeyEvent.uChar.UnicodeChar;
            if (ch == L'D') probe_dump(out, dump_path, dumps++);
            else if (ch == L'Q') return 0;
        }
    }
    return 3;
}

/* Dump n from the probe's file: the window's rows into view, the cursor
 * relative to the window's top. Returns 1 once that dump is complete. */
static int probe_read_dump(const char *path, int n, CpxView *view)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char head[32];
    (void)snprintf(head, sizeof(head), "DUMP %d", n);
    char line[600];
    int in_dump = 0, done = 0;
    int bx = 0, by = 0, wl = 0, wt = 0, wr = 0, wb = 0, cy = 0, cx = 0;
    memset(view, 0, sizeof(*view));
    while (fgets(line, (int)sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!in_dump) {
            if (strcmp(line, head) == 0) in_dump = 1;
            continue;
        }
        if (strncmp(line, "size=", 5) == 0) {
            if (sscanf(line, "size=%dx%d window=%d,%d,%d,%d cursor=%d,%d",
                       &bx, &by, &wl, &wt, &wr, &wb, &cy, &cx) != 8) break;
            view->rows = wb - wt + 1;
            view->cols = wr - wl + 1;
            view->cursor_row = cy - wt;
            view->cursor_col = cx;
            continue;
        }
        if (line[0] == 'R') {
            char *bar = strchr(line, '|');
            char *end = strrchr(line, '|');
            if (!bar || end == bar) continue;
            *end = '\0';
            int vr = atoi(line + 1) - wt;
            if (vr >= 0 && vr < view->rows && vr < CPX_MAX_ROWS)
                (void)snprintf(view->text[vr], sizeof(view->text[vr]),
                               "%.80s", bar + 1);
            continue;
        }
        if (strncmp(line, "END ", 4) == 0) {
            done = (atoi(line + 4) == n);
            break;
        }
    }
    fclose(f);
    (void)bx;
    (void)by;
    return done;
}

static int probe_wait_dump(const char *path, int n, CpxView *view, int ms)
{
    for (int waited = 0; waited <= ms; waited += 50) {
        if (probe_read_dump(path, n, view)) return 1;
        Sleep(50);
    }
    return 0;
}

/* The probe child's command line, for spec_for(). */
static void probe_command(char *out, size_t out_size, const char *scenario,
                          const char *dump_path)
{
    char self[MAX_PATH];
    (void)GetModuleFileNameA(NULL, self, (DWORD)sizeof(self));
    (void)snprintf(out, out_size, "\"%s\" --console-probe %s \"%s\"", self,
                   scenario, dump_path);
}

/* The model's screen in the same shape as a dump, so the two compare. */
static void model_view(const Terminal *term, CpxView *v)
{
    memset(v, 0, sizeof(*v));
    v->rows = term->rows < CPX_MAX_ROWS ? term->rows : CPX_MAX_ROWS;
    v->cols = term->cols;
    v->cursor_row = term->cursor.row;
    v->cursor_col = term->cursor.col;
    for (int r = 0; r < v->rows; r++) {
        const TermRow *row = term->lines[term_screen_to_phys(term, r)];
        int n = row->len < term->cols ? row->len : term->cols;
        if (n > CPX_MAX_COLS) n = CPX_MAX_COLS;
        int k = 0;
        for (int c = 0; c < n; c++) {
            uint32_t cp = row->cells[c].codepoint;
            v->text[r][k++] = (cp >= 0x20u && cp < 0x7Fu) ? (char)cp
                            : (cp == 0u ? ' ' : '?');
        }
        while (k > 0 && v->text[r][k - 1] == ' ') k--;
        v->text[r][k] = '\0';
    }
}

/* Print two views side by side; returns cpx_view_diff(). */
static int report_views(const char *who, const char *label,
                        const CpxView *want, const CpxView *got)
{
    int diff = cpx_view_diff(want, got);
    RPT("  [%s] %s: %s (%d difference%s)\n", who, label,
        diff ? "DISAGREES" : "AGREES", diff, diff == 1 ? "" : "s");
    int rows = want->rows > got->rows ? want->rows : got->rows;
    if (rows > CPX_MAX_ROWS) rows = CPX_MAX_ROWS;
    for (int r = 0; r < rows; r++) {
        const char *a = (r < want->rows) ? want->text[r] : "";
        const char *b = (r < got->rows) ? got->text[r] : "";
        RPT("    %c %2d want |%s| got |%s|\n", strcmp(a, b) ? '*' : ' ', r,
            a, b);
    }
    RPT("    %c cursor want %d,%d got %d,%d; size want %dx%d got %dx%d\n",
        (want->cursor_row != got->cursor_row ||
         want->cursor_col != got->cursor_col) ? '*' : ' ',
        want->cursor_row, want->cursor_col, got->cursor_row, got->cursor_col,
        want->cols, want->rows, got->cols, got->rows);
    return diff;
}

/* 9. The probe child runs under a pseudo-console, writes its scenario, and
 * its dump 0 reads back at the size it was opened at, with the text the
 * scenario wrote. */
int test_local_pty_console_probe_dumps(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("grow");
    if (!s) RPT("  [probe] scenario \"grow\" not found\n");
    ASSERT_NOT_NULL(s);
    if (!s) TEST_END();

    char tmp_dir[MAX_PATH];
    DWORD tn = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    int tmp_ok = (tn > 0u && tn < sizeof(tmp_dir));
    if (!tmp_ok) RPT("  [probe] GetTempPathA failed\n");
    ASSERT_TRUE(tmp_ok);
    if (!tmp_ok) TEST_END();
    char dump_path[MAX_PATH * 2];
    (void)snprintf(dump_path, sizeof(dump_path), "%snsprobe_smoke.txt",
                   tmp_dir);
    (void)DeleteFileA(dump_path);

    char cmd[MAX_PATH * 6];
    probe_command(cmd, sizeof(cmd), s->name, dump_path);
    LocalShellSpec spec;
    spec_for(&spec, cmd);
    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open_ex(&spec, s->old_cols, s->old_rows, 0u,
                                      0ul, err, sizeof(err));
    if (!pty) {
        RPT("  [skip] local_pty_open_ex failed: %s\n", err);
        ASSERT_TRUE(err[0] != '\0');
        TEST_END();
    }
    Terminal *term = term_init(s->old_rows, s->old_cols, 500);
    if (!term) {
        RPT("  [probe] term_init failed\n");
        ASSERT_NOT_NULL(term);
        (void)local_pty_write(pty, "Q", 1);
        local_pty_close(pty);
        (void)DeleteFileA(dump_path);
        TEST_END();
    }

    CpxView got, want, model;
    int ok = probe_wait_dump(dump_path, 0, &got, 10000);
    if (!ok) RPT("  [probe] dump 0 never appeared in %s\n", dump_path);
    ASSERT_TRUE(ok);
    drain(pty, term, 800);
    int have_want = cpx_initial_view(s, &want);
    if (have_want != 0) RPT("  [probe] cpx_initial_view failed (%d)\n", have_want);
    ASSERT_EQ(have_want, 0);
    if (ok) {
        if (got.rows != s->old_rows || got.cols != s->old_cols)
            RPT("  [probe] dump 0 is %dx%d, opened at %dx%d\n", got.cols,
                got.rows, s->old_cols, s->old_rows);
        ASSERT_EQ(got.rows, s->old_rows);
        ASSERT_EQ(got.cols, s->old_cols);
        int found = 0;
        for (int r = 0; r < got.rows; r++)
            if (strcmp(got.text[r], "<20> alpha") == 0) found = 1;
        if (!found) RPT("  [probe] dump 0 has no row \"<20> alpha\"\n");
        ASSERT_TRUE(found);
        if (have_want == 0)
            (void)report_views("smoke", "dump 0: conhost vs scenario layout",
                               &want, &got);
        model_view(term, &model);
        (void)report_views("smoke", "dump 0: conhost vs model", &model, &got);
    }

    (void)local_pty_write(pty, "Q", 1);
    drain(pty, term, 300);
    local_pty_close(pty);
    term_free(term);
    (void)DeleteFileA(dump_path);
    TEST_END();
}

/* ---- 10. resize parity: conhost's own buffer against the spec's rule ----
 *
 * For each scenario, opened once with flags 0 (what local_pty_open() passes
 * today) and once with PSEUDOCONSOLE_RESIZE_QUIRK:
 *   - drain the scenario into a Terminal at the old size;
 *   - term_resize() and local_pty_resize() to the new size;
 *   - snapshot the model BEFORE draining ConPTY's post-resize bytes, and
 *     check the resize kept every scenario line in scrollback plus screen
 *     as often as before (exactly once, on Windows);
 *   - send 'D' and compare conhost's dump with the spec's section 1.3
 *     prediction (tests/conpty_expect.c) and with the model snapshot;
 *   - drain and print the raw post-resize bytes.
 * Section 1 (top mode) is not built yet, so the model is today's
 * bottom-anchored one: what it is compared against is the rule, computed
 * from the scenario text. The comparisons are the report, printed and not
 * asserted -- like cases 4-7, they are facts about this Windows build.
 *
 * NSPROBE_RECORD=1 in the environment (on Windows, never under Wine) also
 * writes tests/fixtures/conpty_resize_<scenario>.bin from the flags-0 run:
 * every byte poll() drained, with \e]9999;resize=COLSxROWS\a where the
 * resize was made. */

/* Wine's ntdll exports wine_get_version; Windows's does not. Under Wine
 * the probe's conhost results are Wine's, so they are printed, never taken
 * as the report, and no fixture is recorded from them. */
static int running_under_wine(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    if (!nt) return 0;
    FARPROC fn = GetProcAddress(nt, "wine_get_version");
    return fn ? 1 : 0;
}

static int count_in(const char *hay, const char *needle)
{
    int count = 0;
    size_t nl = strlen(needle);
    if (nl == 0u) return 0;
    for (const char *p = strstr(hay, needle); p; p = strstr(p + nl, needle))
        count++;
    return count;
}

/* Every logical line the model holds, scrollback and screen, one per text
 * line (wrapped rows joined). term_logical_rows(), not lines_count: right
 * after a resize the screen's rows are real before they are committed. */
static void model_history(const Terminal *term, char *out, size_t out_size)
{
    size_t n = 0u;
    int total = term_logical_rows(term);
    for (int i = 0; i < total; i++) {
        const TermRow *row =
            term->lines[(term->lines_start + i) % term->lines_capacity];
        if (i > 0 && !row->wrapped && n + 1u < out_size) out[n++] = '\n';
        int len = row->len < term->cols ? row->len : term->cols;
        for (int c = 0; c < len && n + 1u < out_size; c++) {
            uint32_t cp = row->cells[c].codepoint;
            out[n++] = (cp >= 0x20u && cp < 0x7Fu) ? (char)cp : ' ';
        }
    }
    out[n] = '\0';
}

static void count_tags(const CpxScenario *s, const Terminal *term,
                       int counts[CPX_MAX_LINES])
{
    static char hist[65536];
    model_history(term, hist, sizeof(hist));
    for (int i = 0; i < s->line_count; i++) {
        char tag[16];
        counts[i] = (cpx_tag(s->lines[i], tag, sizeof(tag)) > 0u)
                  ? count_in(hist, tag) : -1;
    }
}

/* Copy the whole tap file to path (the fixture). Returns 1 on success, 0 on
 * any short read or write. */
static int copy_tap(FILE *tap, const char *path)
{
    char buf[8192];
    if (fflush(tap) != 0 || fseek(tap, 0L, SEEK_SET) != 0) return 0;
    FILE *out = fopen(path, "wb");
    if (!out) return 0;
    int ok = 1;
    size_t got;
    while ((got = fread(buf, 1u, sizeof(buf), tap)) > 0u) {
        if (fwrite(buf, 1u, got, out) != got) { ok = 0; break; }
    }
    if (ferror(tap)) ok = 0;
    if (fclose(out) != 0) ok = 0;
    (void)fseek(tap, 0L, SEEK_END);
    return ok;
}

/* One scenario under one flag set. Returns 0, or 1 when a check failed. */
static int probe_scenario(const CpxScenario *s, unsigned long flags,
                          int on_wine, int record)
{
    int _tf_local_fail = 0;
    char who[64];
    (void)snprintf(who, sizeof(who), "%s/%s", s->name,
                   flags ? "quirk" : "flags0");

    char tmp_dir[MAX_PATH];
    DWORD tn = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    if (tn == 0u || tn >= sizeof(tmp_dir)) {
        RPT("  [%s] no temp directory\n", who);
        return 1;
    }
    /* Names unique per run: a stale dump from an aborted earlier run (an
     * orphaned child still writing) can never be mistaken for this run's. */
    char dump_path[MAX_PATH * 2], tap_path[MAX_PATH * 2];
    unsigned long pid = (unsigned long)GetCurrentProcessId();
    (void)snprintf(dump_path, sizeof(dump_path), "%snsprobe_%s_%lu_%lu.txt",
                   tmp_dir, s->name, flags, pid);
    (void)snprintf(tap_path, sizeof(tap_path), "%snsprobe_%s_%lu_%lu.bin",
                   tmp_dir, s->name, flags, pid);
    (void)DeleteFileA(dump_path);

    char cmd[MAX_PATH * 6];
    probe_command(cmd, sizeof(cmd), s->name, dump_path);
    LocalShellSpec spec;
    spec_for(&spec, cmd);
    char err[512];
    err[0] = '\0';
    LocalPty *pty = local_pty_open_ex(&spec, s->old_cols, s->old_rows, 0u,
                                      flags, err, sizeof(err));
    if (!pty) {
        RPT("  [%s] [skip] local_pty_open_ex failed: %s\n", who, err);
        ASSERT_TRUE(err[0] != '\0');
        return _tf_local_fail;
    }
    FILE *tap = fopen(tap_path, "w+b");
    if (tap) local_pty_set_raw_tap(pty, tap);
    else RPT("  [%s] could not create the raw tap %s\n", who, tap_path);
    if (!tap && record && flags == 0ul) {
        /* The recording cannot be made: that is a failure, not a skip. */
        local_pty_close(pty);
        (void)DeleteFileA(dump_path);
        return 1;
    }
    Terminal *term = term_init(s->old_rows, s->old_cols, 500);
    if (!term) {
        RPT("  [%s] term_init failed\n", who);
        local_pty_close(pty);
        if (tap) { fclose(tap); (void)DeleteFileA(tap_path); }
        (void)DeleteFileA(dump_path);
        return 1;
    }

    /* The scenario, drained at the old size. */
    static CpxView want0, got0, want1, got1, model1, model2;
    int ok0 = probe_wait_dump(dump_path, 0, &got0, 10000);
    if (!ok0) RPT("  [%s] dump 0 never appeared in %s\n", who, dump_path);
    ASSERT_TRUE(ok0);
    drain(pty, term, 800);
    if (ok0 && cpx_initial_view(s, &want0) == 0)
        (void)report_views(who, "before the resize: conhost vs scenario layout",
                           &want0, &got0);
    int before[CPX_MAX_LINES], after[CPX_MAX_LINES];
    count_tags(s, term, before);

    /* The resize, with the in-band marker where it was made. */
    long repaint_at = -1L;
    if (tap) {
        char mark[64];
        int mn = snprintf(mark, sizeof(mark), "\x1b]9999;resize=%dx%d\x07",
                          s->new_cols, s->new_rows);
        if (mn > 0) (void)fwrite(mark, 1u, (size_t)mn, tap);
        (void)fflush(tap);
        repaint_at = ftell(tap);
    }
    term_resize(term, s->new_rows, s->new_cols);
    int resized = local_pty_resize(pty, s->new_cols, s->new_rows);
    if (resized != 0)
        RPT("  [%s] local_pty_resize returned %d%s\n", who, resized,
            on_wine ? " (Wine's ResizePseudoConsole fails)" : "");
    if (!on_wine) ASSERT_EQ(resized, 0);

    /* The model before ConPTY's post-resize bytes reach it. */
    model_view(term, &model1);
    count_tags(s, term, after);
    for (int i = 0; i < s->line_count; i++) {
        if (before[i] < 0) continue;
        int bad = (after[i] != before[i]) || (!on_wine && after[i] != 1);
        if (bad)
            RPT("  [%s] line %d (%s): %d before the resize, %d after\n", who,
                i, s->lines[i], before[i], after[i]);
        ASSERT_EQ(after[i], before[i]);
        if (!on_wine) ASSERT_EQ(after[i], 1);
    }

    /* conhost's buffer after the resize: retried until the dump shows the
     * new size, since ResizePseudoConsole and the 'D' key travel different
     * pipes. */
    int ok1 = 0, n = 1;
    for (int attempt = 0; attempt < 5 && !ok1; attempt++) {
        Sleep(300);
        (void)local_pty_write(pty, "D", 1);
        if (!probe_wait_dump(dump_path, n, &got1, 3000)) break;
        n++;
        ok1 = (got1.rows == s->new_rows && got1.cols == s->new_cols);
    }
    if (!ok1) {
        RPT("  [%s] conhost's dump never showed %dx%d (last read: %dx%d)%s\n",
            who, s->new_cols, s->new_rows, got1.cols, got1.rows,
            on_wine ? " -- Wine does not resize its conhost; nothing to "
                      "compare" : "");
        if (!on_wine) ASSERT_TRUE(ok1);
    } else {
        int rule = -1;
        if (cpx_expected_after(s, &want1) == 0)
            rule = report_views(who,
                "after the resize: conhost vs spec section 1.3 (top-anchored)",
                &want1, &got1);
        int model = report_views(who,
            "after the resize: conhost vs today's model before its repaint",
            &model1, &got1);
        RPT("  [%s] VERDICT rule=%s model=%s\n", who,
            rule == 0 ? "AGREES" : (rule < 0 ? "N/A" : "DISAGREES"),
            model == 0 ? "AGREES" : "DISAGREES");
    }

    /* ConPTY's post-resize bytes: is there a full repaint, and are
     * soft-wrapped rows sent without \r\n? */
    drain(pty, term, 1000);
    if (tap && repaint_at >= 0L) {
        static char raw[65536];
        (void)fflush(tap);
        long end = ftell(tap);
        size_t want_len = (end > repaint_at) ? (size_t)(end - repaint_at) : 0u;
        if (want_len > sizeof(raw)) want_len = sizeof(raw);
        size_t got = 0u;
        if (fseek(tap, repaint_at, SEEK_SET) == 0)
            got = fread(raw, 1u, want_len, tap);
        (void)fseek(tap, 0L, SEEK_END);
        char label[96];
        (void)snprintf(label, sizeof(label), "%s post-resize bytes", who);
        print_escaped(label, raw, got);
    }
    if (ok1) {
        model_view(term, &model2);
        (void)report_views(who, "after the repaint: conhost vs the model",
                           &model2, &got1);
    }

    (void)local_pty_write(pty, "Q", 1);
    drain(pty, term, 300);
    local_pty_close(pty);

    if (tap) {
        if (record && flags == 0ul) {
            char fixture[128];
            (void)snprintf(fixture, sizeof(fixture),
                           "tests/fixtures/conpty_resize_%s.bin", s->name);
            int wrote = copy_tap(tap, fixture);
            RPT("  [%s] fixture %s: %s\n", who, fixture,
                wrote ? "written" : "NOT written");
            ASSERT_TRUE(wrote);
        }
        fclose(tap);
        (void)DeleteFileA(tap_path);
    }
    (void)DeleteFileA(dump_path);
    term_free(term);
    return _tf_local_fail;
}

int test_local_pty_resize_parity(void)
{
    TEST_BEGIN();
    int on_wine = running_under_wine();
    char rec[8];
    DWORD rn = GetEnvironmentVariableA("NSPROBE_RECORD", rec,
                                       (DWORD)sizeof(rec));
    int record = (rn > 0u && rn < sizeof(rec) && rec[0] == '1') ? 1 : 0;
    if (on_wine) {
        RPT("  [host] Wine: the conhost results below are Wine's, not "
            "Windows's -- not the report; no fixture is recorded\n");
        record = 0;
    }
    static const unsigned long FLAGS[2] = { 0ul, PSEUDOCONSOLE_RESIZE_QUIRK };
    for (int i = 0; i < cpx_scenario_count(); i++) {
        for (int f = 0; f < 2; f++) {
            /* One call: ASSERT_EQ evaluates its operands again to print. */
            int rc = probe_scenario(cpx_scenario_at(i), FLAGS[f], on_wine,
                                    record);
            if (rc != 0)
                RPT("  [%s/%s] a check failed (see above)\n",
                    cpx_scenario_at(i)->name, f ? "quirk" : "flags0");
            ASSERT_EQ(rc, 0);
        }
    }
    TEST_END();
}

#endif /* _WIN32 */
