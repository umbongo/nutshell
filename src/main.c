#include <winsock2.h>  /* must precede windows.h */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/cli_args.h"
#include "core/cli_sink.h"
#include "core/dlog.h"
#include "core/text_lines.h"
#include "config/config.h"
#include "ui/ui.h"
#include "ui/icons.h"
#include "ui/resource.h"

/* The debug log (src/core/dlog.c). Started here, before any window, with
 * debugging off: errors and warnings from the very first line are kept in a
 * "logs" folder beside the exe, which is created lazily at the first line that
 * passes the filter (so --help / --version / --list never create it). The window
 * applies the saved settings with dlog_configure() once the config is loaded;
 * dlog_shutdown() at exit drains the writer. This replaces %TEMP%\nutshell.log. */
static void start_debug_log(void)
{
    char exe[MAX_PATH];
    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (n == 0 || n >= sizeof(exe)) {
        exe[0] = '\0';
        dir[0] = '\0';
    } else {
        (void)snprintf(dir, sizeof(dir), "%s", exe);
        char *slash = strrchr(dir, '\\');
        if (slash) *slash = '\0';
        else dir[0] = '\0';
    }
    DlogStartInfo si;
    memset(&si, 0, sizeof(si));
    si.exe_dir = dir;
    si.exe_path = exe;
    si.version = APP_VERSION;
    si.pid = (unsigned)GetCurrentProcessId();
    /* The display facts known before any window exists. The theme is only
     * known once the config is read; the window logs it with the window's own
     * DPI in the app.env event. */
    char extra[96];
    int sys_dpi = 96;
    HDC sdc = GetDC(NULL);
    if (sdc) {
        sys_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
        ReleaseDC(NULL, sdc);
    }
    (void)snprintf(extra, sizeof(extra), "monitors=%d system_dpi=%d",
                   GetSystemMetrics(SM_CMONITORS), sys_dpi);
    si.extra = extra;
    (void)dlog_start(&si);
}

/* cli_sink.h mirrors these so the portable decision can be tested; keep
 * them in step with winbase.h. */
_Static_assert(CLI_FT_UNKNOWN == FILE_TYPE_UNKNOWN, "cli_sink: FILE_TYPE_UNKNOWN");
_Static_assert(CLI_FT_DISK == FILE_TYPE_DISK, "cli_sink: FILE_TYPE_DISK");
_Static_assert(CLI_FT_CHAR == FILE_TYPE_CHAR, "cli_sink: FILE_TYPE_CHAR");
_Static_assert(CLI_FT_PIPE == FILE_TYPE_PIPE, "cli_sink: FILE_TYPE_PIPE");

/* WriteFile as a cli_write_all() writer. A pipe whose reader has exited
 * ("| findstr /m") is reported CLOSED so the caller stops quietly instead
 * of dumping the text on the console. */
static CliIoStatus cli_writefile(void *ctx, const char *buf, size_t len,
                                 size_t *written)
{
    HANDLE h = (HANDLE)ctx;
    DWORD chunk = (len > 0x7FFFFFFFu) ? 0x7FFFFFFFu : (DWORD)len;
    DWORD wrote = 0;
    *written = 0;
    if (!WriteFile(h, buf, chunk, &wrote, NULL)) {
        DWORD err = GetLastError();
        return (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA)
                   ? CLI_IO_CLOSED : CLI_IO_ERROR;
    }
    *written = wrote;
    return CLI_IO_OK;
}

/* One inherited standard handle, classified for cli_sink_for(). */
typedef struct {
    HANDLE h;
    int valid;
    unsigned type;
    int is_console;
} CliStdHandle;

static CliStdHandle cli_std_handle(DWORD which)
{
    CliStdHandle sh;
    sh.h = GetStdHandle(which);
    sh.valid = (sh.h != NULL && sh.h != INVALID_HANDLE_VALUE);
    sh.type = sh.valid ? (unsigned)GetFileType(sh.h) : CLI_FT_UNKNOWN;
    DWORD mode = 0;
    sh.is_console = (sh.valid && sh.type == CLI_FT_CHAR &&
                     GetConsoleMode(sh.h, &mode));
    return sh;
}

/* Try the inherited handle for `which`; 1 if the text went there (fully,
 * or as far as it could before a closed pipe or a write error, which must
 * not be followed by a second copy elsewhere), 0 if it is not a redirect
 * or nothing could be written. */
static int cli_try_handle(DWORD which, const char *text)
{
    CliStdHandle sh = cli_std_handle(which);
    if (cli_sink_for(sh.valid, sh.type, sh.is_console) != CLI_SINK_HANDLE) {
        return 0;
    }
    return cli_write_all(cli_writefile, sh.h, text) != CLI_WRITE_NONE;
}

/* Print to the stream the shell redirected (a pipe, a file, NUL), else to
 * the parent console when launched from a terminal, else to a MessageBox
 * when there is none (double-click, Run dialog). A GUI process is not
 * waited for by an interactive cmd or PowerShell, so the console path
 * prints after the next prompt; a pipe's reader does wait, so
 * "nutshell.exe --help | more" prints in order, and a file redirect at
 * least receives the text. src/core/cli_sink.h has the full story. Errors
 * follow stderr, everything else stdout, so "2>&1" and a plain "| more"
 * both behave like any console tool. */
static void cli_output(const char *text, const char *title, int is_error)
{
    /* Attach before classifying: GetConsoleMode() on an inherited console
     * handle can fail while this process is not attached to that
     * console, which would make the console look like a NUL device. */
    BOOL attached = AttachConsole(ATTACH_PARENT_PROCESS);
    if (cli_try_handle(is_error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE,
                       text)) {
        if (attached) {
            FreeConsole();
        }
        return;
    }
    if (attached) {
        if (freopen("CONOUT$", "w", stdout) != NULL) {
            fputc('\n', stdout);
            fputs(text, stdout);
            fflush(stdout);
        }
        FreeConsole();
        return;
    }
    /* No console and no usable stderr: a parent that redirected only
     * stdout still gets the error there rather than a blocking dialog. */
    if (is_error && cli_try_handle(STD_OUTPUT_HANDLE, text)) {
        return;
    }
    MessageBoxA(NULL, text, title,
                (UINT)(MB_OK | (is_error ? MB_ICONERROR
                                         : MB_ICONINFORMATION)));
}

/* Directory containing the running exe ("" on failure). */
static void exe_dir(char *buf, size_t n)
{
    buf[0] = '\0';
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (len == 0 || len >= sizeof(path)) {
        return;
    }
    char *slash = strrchr(path, '\\');
    if (!slash) {
        return;
    }
    *slash = '\0';
    (void)snprintf(buf, n, "%s", path);
}

/* -l / --list: print saved sessions without starting the UI. */
static int run_list(void)
{
    char dir[MAX_PATH];
    char cfg_path[MAX_PATH];
    exe_dir(dir, sizeof(dir));
    if (dir[0] != '\0') {
        (void)snprintf(cfg_path, sizeof(cfg_path), "%s\\" CONFIG_FILENAME, dir);
    } else {
        /* Could not determine the exe's own directory: fall back to the
         * same absolute per-user location the running app would use
         * (window.c), never a bare relative CONFIG_FILENAME -- that would
         * silently read from wherever this process's current working
         * directory happens to be. */
        char local_appdata[MAX_PATH];
        DWORD la_len = GetEnvironmentVariableA("LOCALAPPDATA", local_appdata,
                                                (DWORD)sizeof(local_appdata));
        if (la_len == 0 || la_len >= sizeof(local_appdata) ||
            !config_fallback_path(local_appdata, cfg_path, sizeof(cfg_path))) {
            cli_output("Could not find a folder to store " CONFIG_FILENAME " in.\n",
                       "Nutshell Sessions", 1);
            return 2;
        }
    }

    if (GetFileAttributesA(cfg_path) == INVALID_FILE_ATTRIBUTES) {
        cli_output("No saved sessions.\n", "Nutshell Sessions", 0);
        return 0;
    }

    Config *cfg = config_load(cfg_path);
    if (!cfg) {
        cli_output("Could not parse " CONFIG_FILENAME ".\n",
                   "Nutshell Sessions", 1);
        return 2;
    }

    size_t n = vec_size(&cfg->profiles);
    if (n == 0) {
        cli_output("No saved sessions.\n", "Nutshell Sessions", 0);
        config_free(cfg);
        return 0;
    }

    /* name-or-host label, hyphen, host: one line per profile. ASCII on
     * purpose: PowerShell decodes native output with the OEM code page by
     * default, so an em-dash came out as mojibake in a pipe or a file. */
    size_t cap = n * 560u + 32u;
    char *out = malloc(cap);
    if (!out) {
        config_free(cfg);
        return 2;
    }
    size_t pos = 0;
    (void)snprintf(out, cap, "Saved sessions:\n");
    pos = strlen(out);
    for (size_t i = 0; i < n; i++) {
        const Profile *pr = (const Profile *)vec_get(&cfg->profiles, i);
        const char *label = (pr->name[0] != '\0') ? pr->name : pr->host;
        int wrote = snprintf(out + pos, cap - pos, "  %s - %s\n",
                             label, pr->host);
        if (wrote < 0 || (size_t)wrote >= cap - pos) {
            break;
        }
        pos += (size_t)wrote;
    }
    cli_output(out, "Nutshell Sessions", 0);
    free(out);
    config_free(cfg);
    return 0;
}

/* Convert the process command line to a UTF-8 argv. Returns argc; *argv_out
 * must be freed with free_utf8_argv. On failure returns 0 with *argv_out
 * NULL (treated as "no arguments"). */
static int build_utf8_argv(char ***argv_out)
{
    *argv_out = NULL;
    int argc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv || argc <= 0) {
        if (wargv) LocalFree(wargv);
        return 0;
    }
    char **argv = calloc((size_t)argc, sizeof(char *));
    if (!argv) {
        LocalFree(wargv);
        return 0;
    }
    for (int i = 0; i < argc; i++) {
        int need = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                       NULL, 0, NULL, NULL);
        if (need <= 0) need = 1;
        argv[i] = calloc((size_t)need, 1u);
        if (argv[i]) {
            (void)WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                      argv[i], need, NULL, NULL);
        } else {
            argv[i] = calloc(1u, 1u);  /* empty string fallback */
        }
    }
    LocalFree(wargv);
    *argv_out = argv;
    return argc;
}

static void free_utf8_argv(int argc, char **argv)
{
    if (!argv) return;
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    /* DLL search-order hardening: the very first thing this process does.
     * SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32) makes every
     * later LoadLibraryEx call that does not pass its own flags search only
     * System32 (plus any directory explicitly added with
     * AddDllDirectory, of which there are none), never the application
     * directory or the current directory. SetDllDirectoryW(L"") on top of
     * that removes the current directory from the search path used by the
     * classic LoadLibrary/CreateProcess-without-lpApplicationName lookup,
     * which SetDefaultDllDirectories does not touch. Between the two, a
     * malicious DLL planted next to nutshell.exe or in whatever directory it
     * happened to be launched from cannot be picked up ahead of the real,
     * System32 one by any LoadLibrary* call made from here on. Static
     * imports the loader resolves before this line ever runs, and any
     * LoadLibrary* call that explicitly passes its own search flags, are
     * unaffected -- see ai_chat.c's Riched20.dll/Msftedit.dll load and
     * dwm_util.c for the two places that do. */
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetDllDirectoryW(L"");

    char **argv = NULL;
    int argc = build_utf8_argv(&argv);
    CliOptions opts;
    cli_parse(argc, argv, &opts);
    free_utf8_argv(argc, argv);

    switch (opts.action) {
        case CLI_ERROR: {
            char err_text[600];
            (void)snprintf(err_text, sizeof(err_text), "%s\n\n%s",
                           opts.error, cli_usage_text());
            cli_output(err_text, "Nutshell — Usage", 1);
            return 2;
        }
        case CLI_HELP:
            cli_output(cli_usage_text(), "Nutshell — Usage", 0);
            return 0;
        case CLI_VERSION:
            cli_output("Nutshell " APP_VERSION "\n", "Nutshell", 0);
            return 0;
        case CLI_LIST:
            return run_list();
        default:
            break;  /* CLI_RUN / CLI_RUN_NO_CONNECT / connect actions */
    }

    ui_set_startup_action(opts.action, opts.arg, opts.demo_state, opts.theme);

    start_debug_log();

    /* I-2: initialise WSA once for the process lifetime */
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    ns_icons_init();
    ui_init(hInstance);
    ui_run();
    ns_icons_shutdown();

    WSACleanup();
    dlog_shutdown();
    return 0;
}
