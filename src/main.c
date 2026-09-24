#include <winsock2.h>  /* must precede windows.h */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/cli_args.h"
#include "core/logger.h"
#include "config/config.h"
#include "ui/ui.h"
#include "ui/icons.h"
#include "ui/resource.h"

/* The app-wide diagnostic log (src/core/logger.c) was wired into every
 * error path (window.c's WM_CREATE/RegisterClass failures, and -- since
 * 2026-09-25 -- dispatch_tick()'s prompt-gated command dispatcher, see
 * ai_chat.c) but log_init() was never actually called anywhere in
 * production, only in tests/test_logger.c: every LOG_* call reached only
 * stderr, invisible for a GUI-subsystem (-mwindows) process with no
 * console. Point it at %TEMP%\nutshell.log unconditionally, at
 * LOG_LEVEL_INFO -- no setting gates this, unlike the per-session
 * debug_terminal log (Settings > Debug), so it is there for a bug report
 * without the user having to change anything first. Same file, directory
 * and "no setting required" shape as REDRAW_DEBUG's
 * %TEMP%\nutshell_redraw.log (src/ui/redraw_log.h), which is opt-in at
 * build time instead. */
static void init_app_log(void)
{
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(tmp), tmp);
    if (n == 0 || n >= sizeof(tmp)) {
        log_init(NULL, LOG_LEVEL_INFO);
        return;
    }
    char path[MAX_PATH * 2];
    (void)snprintf(path, sizeof(path), "%snutshell.log", tmp);
    log_init(path, LOG_LEVEL_INFO);
}

/* Print to the parent console when launched from a terminal; fall back
 * to a MessageBox when there is none (double-click, Run dialog). */
static void cli_output(const char *text, const char *title, int is_error)
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (freopen("CONOUT$", "w", stdout) != NULL) {
            fputc('\n', stdout);
            fputs(text, stdout);
            fflush(stdout);
        }
        FreeConsole();
    } else {
        MessageBoxA(NULL, text, title,
                    (UINT)(MB_OK | (is_error ? MB_ICONERROR
                                             : MB_ICONINFORMATION)));
    }
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

    /* name-or-host label, em-dash, host: one line per profile */
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
        int wrote = snprintf(out + pos, cap - pos, "  %s — %s\n",
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

    init_app_log();

    /* I-2: initialise WSA once for the process lifetime */
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    ns_icons_init();
    ui_init(hInstance);
    ui_run();
    ns_icons_shutdown();

    WSACleanup();
    log_close();
    return 0;
}
