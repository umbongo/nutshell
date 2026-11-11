/* tests/win_runner.c — Minimal Win32 test runner. Builds only under
 * MinGW (via `make wintest`), runs via Wine or on real Windows.
 * Covers tests that require GDI / GDI+ (icon renderer) or a real
 * pseudo-console and real child processes (the ConPTY backend). */

#ifdef _WIN32

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"

int _tf_failed = 0;
int _tf_run    = 0;

/* test_icons.c */
int test_icons_all_render_non_empty(void);
int test_icons_render_at_multiple_sizes(void);

/* test_md_render_win.c */
int test_md_render_win_measure_equals_paint_and_runs(void);

/* test_chat_select_win.c */
int test_chat_select_win_runs_match_paint(void);
int test_chat_select_win_reason_in_row(void);
int test_chat_select_win_mouse_and_keys(void);
int test_chat_select_win_query_word(void);
int test_chat_select_win_drag_ignores_buttonless_move(void);
int test_chat_select_win_short_list(void);

/* test_pw_reveal_win.c */
int test_pw_reveal_win_press_shows_release_restores_mask(void);
int test_pw_reveal_win_text_caret_selection_unchanged(void);
int test_pw_reveal_win_capture_lost_masks(void);
int test_pw_reveal_win_release_outside_masks(void);
int test_pw_reveal_win_drag_off_masks_drag_back_reveals(void);
int test_pw_reveal_win_mask_all_masks_descendants(void);
int test_pw_reveal_win_emptied_while_revealed_masks(void);
int test_pw_reveal_win_emptied_by_backspace(void);
int test_pw_reveal_win_emptied_by_clear(void);
int test_pw_reveal_win_emptied_by_replacesel(void);
int test_pw_reveal_win_emptied_by_delete_key(void);
int test_pw_reveal_win_replaced_text_masks(void);
int test_pw_reveal_win_eye_click_activates_and_no_context_menu(void);
int test_pw_reveal_win_setfont_keeps_margin(void);
int test_pw_reveal_win_empty_field_never_reveals(void);
int test_pw_reveal_win_copy_blocked_while_revealed(void);
int test_pw_reveal_win_eye_tracks_resize_and_margin(void);
int test_pw_reveal_win_destroy_while_revealed_is_clean(void);
int test_pw_prompt_win_activate_keeps_focus_on_field(void);
int test_pw_prompt_win_save_ticked_ok_returns_one(void);
int test_pw_prompt_win_save_unticked_ok_returns_zero(void);
int test_pw_prompt_win_save_ticked_cancel_returns_zero(void);

/* test_telnet_io_win.c */
int test_telnet_io_win_connect_ok_and_refused(void);
int test_telnet_io_win_resolve_fails(void);
int test_telnet_io_win_negotiates_and_logs_in(void);
int test_telnet_io_win_eof_reports_closed(void);
int test_telnet_io_win_serial_unique(void);

/* test_ns_tip_win.c */
int test_ns_tip_win_create_styles(void);
int test_ns_tip_win_notify_static_hits_client(void);
int test_ns_tip_win_add_keeps_styles(void);
int test_ns_tip_win_count_and_text(void);
int test_ns_tip_win_rect_tool_moves(void);

/* test_local_pty.c */
extern FILE *nspty_report;
int test_local_pty_echo_through_cmd(void);
int test_local_pty_close_with_sleeping_grandchild(void);
int test_local_pty_records_opening_bytes(void);
int test_local_pty_records_key_mapping(void);
int test_local_pty_records_fullscreen_modes(void);
int test_local_pty_powershell_settles_after_command_error(void);
int test_key_layout_records_ctrl_space(void);
int test_local_pty_open_ex_passes_pcon_flags(void);
int test_local_pty_console_probe_dumps(void);
int test_local_pty_resize_parity(void);
int nspty_key_dump_main(void);
int nsprobe_console_main(const char *scenario, const char *dump_path);

/* ---- why the ConPTY cases run in a re-executed child --------------------
 *
 * PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE only takes effect when the creating
 * process is not sitting in a console it inherited: a child then attaches to
 * that console in preference to the pseudo-console, and nothing reports an
 * error when it does -- CreateProcessW succeeds, the child's output goes to
 * the inherited console, and our pipe delivers only conhost's own 16
 * opening bytes.
 *
 * Measured on this host (2026-09-22), the same code each time, `cmd.exe /c
 * echo` through a pseudo-console:
 *
 *   creating process                                  bytes read
 *   ----------------------------------------------    ----------
 *   GUI subsystem, no console (nutshell.exe)              83  attached
 *   console subsystem, its own fresh console              83  attached
 *   console subsystem, inheriting MSYS2's terminal        16  NOT attached
 *   ... plus DETACHED_PROCESS (no console at all)          0  nothing runs
 *
 * The product is unaffected: nutshell.exe is `-mwindows`, a GUI process with
 * no console of its own -- the first row, and the reason the spec says so in
 * section 3. This harness is a console program normally started from an
 * MSYS2 shell, which is itself a pseudo-console: the third row. So it
 * re-executes itself once, and the child swaps the console it inherited for
 * a fresh hidden one of its own before opening any pseudo-console.
 *
 * The child's results come back in a file rather than on stdout, because
 * every way of redirecting stdout defeats the attach again: AllocConsole
 * re-binds the CRT's standard streams to the new console, and freopen() on
 * stdout re-binds STD_OUTPUT_HANDLE, which puts us back in row three. So the
 * cases write to a plain FILE (test_local_pty.c's nspty_report), which
 * touches no standard handle, and the parent prints it.
 */

#define PTY_CHILD_FLAG "--pty-child"
/* test_local_pty_records_key_mapping runs this executable again, under a
 * pseudo-console, as the program whose console input it records. */
#define KEY_DUMP_FLAG  "--dump-keys"
/* test_local_pty_console_probe_dumps (and the resize parity case) run this
 * executable again as the console program whose buffer they read back. */
#define CONSOLE_PROBE_FLAG "--console-probe"
#define PTY_CASE_COUNT 10

struct PtyCase { const char *name; int (*fn)(void); };

static int run_pty_cases(void)
{
    static const struct PtyCase CASES[] = {
        { "test_local_pty_echo_through_cmd", test_local_pty_echo_through_cmd },
        { "test_local_pty_close_with_sleeping_grandchild",
          test_local_pty_close_with_sleeping_grandchild },
        { "test_local_pty_records_opening_bytes",
          test_local_pty_records_opening_bytes },
        { "test_local_pty_records_key_mapping",
          test_local_pty_records_key_mapping },
        { "test_local_pty_records_fullscreen_modes",
          test_local_pty_records_fullscreen_modes },
        { "test_local_pty_powershell_settles_after_command_error",
          test_local_pty_powershell_settles_after_command_error },
        { "test_key_layout_records_ctrl_space",
          test_key_layout_records_ctrl_space },
        { "test_local_pty_open_ex_passes_pcon_flags",
          test_local_pty_open_ex_passes_pcon_flags },
        { "test_local_pty_console_probe_dumps",
          test_local_pty_console_probe_dumps },
        { "test_local_pty_resize_parity", test_local_pty_resize_parity },
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        fprintf(nspty_report, "[RUN ] %s\n", CASES[i].name);
        fflush(nspty_report);
        int rc = CASES[i].fn();
        failed += rc;
        fprintf(nspty_report, "[%s] %s\n", rc ? "FAIL" : "PASS", CASES[i].name);
        fflush(nspty_report);
    }
    return failed;
}

/* Spawn ourselves with a console of our own; print what the child said.
 * Returns the child's failure count, or -1 when it could not be run (the
 * caller then says so rather than failing the harness). */
static int spawn_pty_child(void)
{
    char self[MAX_PATH];
    if (GetModuleFileNameA(NULL, self, (DWORD)sizeof(self)) == 0u) return -1;

    static const char OUT_NAME[] = "nutshell_wintest_pty.txt";
    char out_path[MAX_PATH * 2];
    char tmp_dir[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(tmp_dir), tmp_dir);
    if (n == 0u || n >= sizeof(tmp_dir)) return -1;
    (void)snprintf(out_path, sizeof(out_path), "%s%s", tmp_dir, OUT_NAME);
    DeleteFileA(out_path);

    char cmdline[MAX_PATH * 4];
    int written = snprintf(cmdline, sizeof(cmdline),
                           "\"%s\" " PTY_CHILD_FLAG " \"%s\"", self, out_path);
    if (written < 0 || (size_t)written >= sizeof(cmdline)) return -1;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    CloseHandle(pi.hThread);

    /* 300 s: the resize parity case alone opens eight pseudo-consoles. */
    DWORD wait_rc = WaitForSingleObject(pi.hProcess, 300000u);
    DWORD code = 1u;
    if (wait_rc == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);

    FILE *f = fopen(out_path, "rb");
    if (!f) {
        printf("[pty] could not read the child's report at %s\n", out_path);
    } else {
        char buf[4096];
        size_t got;
        while ((got = fread(buf, 1u, sizeof(buf) - 1u, f)) > 0u) {
            buf[got] = '\0';
            fputs(buf, stdout);
        }
        fclose(f);
        DeleteFileA(out_path);
    }
    if (wait_rc != WAIT_OBJECT_0) return -1;
    return (int)code;
}

/* The ns_tip tests need comctl32 v6 (TTM_GETTEXT's buffer size, the V3
 * TOOLINFO size), as the exe gets from its manifest. Build an activation
 * context from the manifest, run the tests under it, and count one failure
 * when the manifest cannot be loaded rather than run them under v5. */
static int run_ns_tip_tests(void)
{
    ACTCTXA ac;
    memset(&ac, 0, sizeof(ac));
    ac.cbSize = sizeof(ac);
    ac.lpSource = "src\\ui\\nutshell.manifest";
    HANDLE ctx = CreateActCtxA(&ac);
    if (ctx == INVALID_HANDLE_VALUE) {
        printf("[FAIL] ns_tip: no activation context from "
               "src\\ui\\nutshell.manifest\n");
        _tf_failed += 1;
        return 1;
    }
    ULONG_PTR cookie = 0;
    if (!ActivateActCtx(ctx, &cookie)) {
        ReleaseActCtx(ctx);
        printf("[FAIL] ns_tip: ActivateActCtx failed\n");
        _tf_failed += 1;
        return 1;
    }
    int failed = 0;
    failed += test_ns_tip_win_create_styles();
    failed += test_ns_tip_win_notify_static_hits_client();
    failed += test_ns_tip_win_add_keeps_styles();
    failed += test_ns_tip_win_count_and_text();
    failed += test_ns_tip_win_rect_tool_moves();
    DeactivateActCtx(0, cookie);
    ReleaseActCtx(ctx);
    return failed;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], KEY_DUMP_FLAG) == 0)
        return nspty_key_dump_main();
    if (argc >= 4 && strcmp(argv[1], CONSOLE_PROBE_FLAG) == 0)
        return nsprobe_console_main(argv[2], argv[3]);
    if (argc >= 3 && strcmp(argv[1], PTY_CHILD_FLAG) == 0) {
        /* Re-executed half. Swap the inherited console for a fresh hidden
         * one, then open the report file -- in that order, and with fopen
         * rather than freopen(stdout), so no standard handle is touched
         * after the swap (see the note above). */
        FreeConsole();
        if (AllocConsole()) {
            HWND con = GetConsoleWindow();
            if (con) ShowWindow(con, SW_HIDE);
        }
        nspty_report = fopen(argv[2], "w");
        if (!nspty_report) return 99;
        int child_failed = run_pty_cases();
        fprintf(nspty_report, "\n[pty child] Tests Run: %d, Failed: %d\n",
                _tf_run, _tf_failed);
        fclose(nspty_report);
        return child_failed;
    }

    int failed = 0;
    failed += test_icons_all_render_non_empty();
    failed += test_icons_render_at_multiple_sizes();
    failed += test_md_render_win_measure_equals_paint_and_runs();
    failed += test_chat_select_win_runs_match_paint();
    failed += test_chat_select_win_reason_in_row();
    failed += test_chat_select_win_mouse_and_keys();
    failed += test_chat_select_win_query_word();
    failed += test_chat_select_win_drag_ignores_buttonless_move();
    failed += test_chat_select_win_short_list();
    InitCommonControls();
    failed += test_pw_reveal_win_press_shows_release_restores_mask();
    failed += test_pw_reveal_win_text_caret_selection_unchanged();
    failed += test_pw_reveal_win_capture_lost_masks();
    failed += test_pw_reveal_win_release_outside_masks();
    failed += test_pw_reveal_win_drag_off_masks_drag_back_reveals();
    failed += test_pw_reveal_win_mask_all_masks_descendants();
    failed += test_pw_reveal_win_emptied_while_revealed_masks();
    failed += test_pw_reveal_win_emptied_by_backspace();
    failed += test_pw_reveal_win_emptied_by_clear();
    failed += test_pw_reveal_win_emptied_by_replacesel();
    failed += test_pw_reveal_win_emptied_by_delete_key();
    failed += test_pw_reveal_win_replaced_text_masks();
    failed += test_pw_reveal_win_eye_click_activates_and_no_context_menu();
    failed += test_pw_reveal_win_setfont_keeps_margin();
    failed += test_pw_reveal_win_empty_field_never_reveals();
    failed += test_pw_reveal_win_copy_blocked_while_revealed();
    failed += test_pw_reveal_win_eye_tracks_resize_and_margin();
    failed += test_pw_reveal_win_destroy_while_revealed_is_clean();
    failed += test_pw_prompt_win_activate_keeps_focus_on_field();
    failed += test_pw_prompt_win_save_ticked_ok_returns_one();
    failed += test_pw_prompt_win_save_unticked_ok_returns_zero();
    failed += test_pw_prompt_win_save_ticked_cancel_returns_zero();
    failed += test_telnet_io_win_connect_ok_and_refused();
    failed += test_telnet_io_win_resolve_fails();
    failed += test_telnet_io_win_negotiates_and_logs_in();
    failed += test_telnet_io_win_eof_reports_closed();
    failed += test_telnet_io_win_serial_unique();
    failed += run_ns_tip_tests();

    printf("\n[pty] re-executing with a console of our own "
           "(see win_runner.c for why)\n");
    int pty_failed = spawn_pty_child();
    if (pty_failed < 0) {
        printf("[SKIP] ConPTY cases -- could not re-execute this harness\n");
    } else {
        failed += pty_failed;
        _tf_run += PTY_CASE_COUNT;
        _tf_failed += pty_failed;
    }

    printf("\nTests Run: %d, Failed: %d\n", _tf_run, _tf_failed);
    return failed ? 1 : 0;
}

#endif /* _WIN32 */
