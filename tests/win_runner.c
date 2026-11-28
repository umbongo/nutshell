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
int _tf_skipped = 0;

/* test_icons.c */
int test_icons_all_render_non_empty(void);
int test_icons_render_at_multiple_sizes(void);
int test_icons_speaker_renders(void);

/* test_md_render_win.c */
int test_md_render_win_measure_equals_paint_and_runs(void);

/* test_chat_select_win.c */
int test_chat_select_win_runs_match_paint(void);
int test_chat_select_win_reason_in_row(void);
int test_chat_select_win_thinking_pref_modes(void);
int test_chat_select_win_thinking_compact_box(void);
int test_chat_select_win_thinking_extended_box(void);
int test_chat_select_win_thinking_auto_follows_complete(void);
int test_chat_select_win_thinking_click_cycles_and_anchors(void);
int test_chat_select_win_thinking_mode_change_holds_reader_place(void);
int test_chat_select_win_thinking_wheel_fits_goes_to_thread(void);
int test_chat_select_win_thinking_wheel_streaming_box_takes_it(void);
int test_chat_select_win_thinking_wheel_mode_change_place(void);
int test_chat_select_win_thinking_wheel_rows_keep_own_state(void);
int test_chat_select_win_mouse_and_keys(void);
int test_chat_select_win_query_word(void);
int test_chat_select_win_drag_ignores_buttonless_move(void);
int test_chat_select_win_short_list(void);
int test_chat_read_win_button_rows(void);
int test_chat_read_win_painted_equals_measured(void);
int test_chat_read_win_off_heights_unchanged(void);
int test_chat_read_win_tail_busy_hides_last(void);
int test_chat_read_win_click_posts_id(void);
int test_chat_read_win_reading_draws_stop(void);
int test_chat_read_win_button_moves_to_new_row(void);
int test_chat_read_win_off_paints_as_never_on(void);

/* test_ns_slider_win.c */
int test_ns_slider_win_keys_and_notify(void);

/* test_ns_draw_level_meter_win.c */
int test_ns_draw_level_meter_win_zones_fill_and_hold(void);

/* test_ns_check_win.c */
int test_ns_check_win_check_state_round_trips(void);
int test_ns_check_win_click_toggles_and_paints(void);
int test_ns_check_win_null_and_destroy_are_safe(void);

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
int test_pw_reveal_win_unlisted_message_that_empties_field_masks(void);
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
int test_notice_dlg_win_opens_with_class_caption_and_controls(void);
int test_notice_dlg_win_no_action_button_when_action_empty(void);
int test_notice_dlg_win_enter_on_ok_returns_zero_and_window_is_gone(void);
int test_notice_dlg_win_escape_returns_zero(void);
int test_notice_dlg_win_close_box_returns_zero(void);
int test_notice_dlg_win_enter_on_focused_action_returns_one(void);
int test_notice_dlg_win_tab_moves_ok_action_ok(void);
int test_notice_dlg_win_tab_with_only_ok_stays_on_ok(void);
int test_notice_dlg_win_owner_enable_state_is_restored(void);
int test_notice_dlg_win_child_owner_disables_root(void);
int test_notice_dlg_win_msg_shows_table_text(void);
int test_notice_dlg_win_msg_bad_id_shows_nothing(void);
int test_notice_dlg_win_wide_label_and_long_text_stay_inside(void);
int test_notice_dlg_win_minimized_owner_centres_on_normal_position(void);
int test_notice_dlg_win_close_hides_before_inner_loop_ends(void);
int test_notice_dlg_win_single_notice_stays_visible_until_owner_enabled(void);
int test_notice_dlg_win_held_enter_does_not_dismiss(void);
int test_ns_font_win_bold_is_cached_and_bold(void);

/* test_telnet_io_win.c */
int test_telnet_io_win_connect_ok_and_refused(void);
int test_telnet_io_win_resolve_fails(void);
int test_telnet_io_win_negotiates_and_logs_in(void);
int test_telnet_io_win_eof_reports_closed(void);
int test_telnet_io_win_serial_unique(void);
int test_winhttp_open_refuses_while_off(void);
int test_winhttp_open_opens_while_on(void);
int test_voice_audio_start_stop(void);
int test_voice_audio_start_refused_when_gate_off(void);
int test_voice_audio_double_start_is_one_thread(void);
int test_voice_audio_restart(void);
int test_voice_audio_enumerate_defined(void);
int test_voice_audio_capture_no_device_or_data(void);
int test_voice_audio_bad_id_falls_back(void);
int test_voice_audio_queue_keeps_begin_and_end(void);
int test_voice_audio_queue_grows_past_initial_capacity(void);
int test_voice_audio_never_started(void);
int test_voice_audio_test_capture_has_no_time_limit(void);
int test_voice_audio_test_capture_keeps_no_samples(void);
int test_voice_audio_ingest_applies_gain_live(void);
int test_voice_audio_ingest_quiet_mic_not_all_zero(void);
int test_voice_audio_set_gain_coalesces(void);
int test_voice_audio_set_gain_after_begin_is_kept(void);
int test_voice_audio_live_ingest_feeds_gained_samples(void);
int test_voice_audio_conv_begin_headless(void);
int test_voice_audio_conv_turn_posted(void);
int test_voice_audio_conv_attach_live_feeds(void);
int test_voice_audio_conv_hold_wipes(void);
int test_voice_audio_conv_mode_listen_keeps_cutin_turn(void);
int test_voice_audio_conv_preempted_is_reported(void);
int test_voice_ptt_posted_keys(void);
int test_voice_ptt_posted_right_shift(void);
int test_voice_stt_refused_when_gate_off(void);
int test_voice_stt_unresolvable_target(void);
int test_voice_stt_cancel_unreachable(void);
int test_voice_stt_cancel_all_idle(void);
int test_voice_stt_retry_after_400_drops_language(void);
int test_voice_stt_retry_twice_400_delivers_the_error(void);
int test_voice_stt_no_retry_without_language_or_other_status(void);
int test_voice_stt_cancel_closes_a_blocked_request(void);
int test_voice_stt_gate_off_before_send_sends_nothing(void);
int test_voice_stt_counts_one_open_per_upload(void);
int test_voice_stt_long_transcript_delivered_whole(void);
int test_voice_net_counts(void);
int test_stt_upload_no_redirect(void);
int test_stt_upload_policy_refused(void);
int test_stt_upload_plain_still_works(void);
int test_ai_stream_ex_wav_bytes(void);
int test_ai_stream_ex_sse(void);
int test_ai_stream_ex_statuses(void);
int test_ai_stream_ex_no_redirect(void);
int test_ai_stream_ex_cancel_hang(void);
int test_ai_stream_ex_null_session(void);
int test_ai_stream_ex_policy_refused(void);
int test_ai_stream_ex_bad_recv_timeout(void);
int test_ai_stream_ex_session_kept(void);
int test_voice_lock_register_balances(void);
int test_voice_lock_is_lock_event(void);
int test_voice_voices_read_hkcu(void);
int test_voice_tts_widen_map(void);
int test_voice_tts_gate_off(void);
int test_voice_tts_start_quit_wine(void);
int test_voice_tts_speak_events_wine(void);
int test_voice_tts_sample_quits_itself(void);
int test_voice_voices_read_long_names(void);
int test_voice_tts_exited_context_restarts(void);
int test_voice_tts_reap_by_generation(void);
int test_vconv_win_ends_ledger_zero(void);
int test_vconv_win_activate_and_enable_pause(void);
int test_vconv_win_full_turn(void);
int test_vconv_win_stale_reply_events_ignored(void);
int test_vconv_win_notice_once(void);
int test_vconv_win_off_mid_reply(void);
int test_vconv_win_events_after_their_moment_dropped(void);
int test_voice_audio_conv_end_turn(void);
int test_voice_render_null_plays_and_done(void);
int test_voice_render_flush_then_old_data(void);
int test_voice_render_gain(void);
int test_voice_render_preempt(void);
int test_voice_render_real_wine(void);
int test_voice_render_end_without_data_done(void);
int test_voice_render_pending_keeps_order(void);
int test_voice_ais_gate_off(void);
int test_voice_ais_fake_done(void);
int test_voice_ais_empty_chunk_fails(void);
int test_voice_ais_stop_then_speak_no_old_audio(void);
int test_voice_ais_gain_survives_open(void);
int test_voice_ais_reopen_after_preempt(void);
int test_voice_ais_preempt_while_playing(void);
int test_voice_ais_done_guard_waits_for_progress(void);
int test_voice_ais_dead_worker_never_pushes(void);
int test_voice_ais_resolve_error_at_once(void);
int test_voice_ais_sample_self_quits(void);
int test_voice_ais_lost_chunk_done_still_ends(void);
int test_voice_ais_partial_chunk_failure_offset(void);
int test_voice_ais_same_reply_id_respeak(void);
int test_voice_ais_chunk_cap_is_an_error(void);
int test_voice_ais_quit_all_two_contexts(void);
int test_voice_ais_custom_wav_done(void);
int test_voice_ais_custom_no_key(void);
int test_voice_ais_mistral_sse(void);
int test_voice_ais_mistral_capture(void);
int test_voice_ais_sse_wav_then_error_event(void);
int test_voice_ais_sse_error_fixture_is_format(void);
int test_voice_ais_sse_done_only(void);
int test_voice_ais_badrate_is_format(void);
int test_voice_ais_endless_audio_capped(void);
int test_voice_ais_request_time_capped(void);
int test_voice_ais_not_wav_is_format(void);
int test_voice_ais_http_302_redirect(void);
int test_voice_ais_endless_error_body_capped(void);
int test_voice_ais_second_header_fails(void);
int test_voice_ais_empty_body_fails(void);
int test_voice_ais_http_401(void);
int test_voice_ais_http_429(void);
int test_voice_ais_http_404_and_422(void);
int test_voice_ais_oversized_line_fails(void);
int test_voice_ais_mistral_no_done_fails(void);
int test_voice_ais_partial_frame_fails(void);
int test_voice_ais_quit_while_server_hangs(void);
int test_voice_audio_conv_end_then_same_token_open(void);
int test_voice_audio_close_cannot_be_lost(void);
int test_vconv_win_send_while_hearing_batch(void);
int test_vconv_win_send_while_hearing_live_link_fails_to_fallback(void);
int test_vconv_win_ending_wipes_audio_keeps_words(void);
int test_vconv_win_fallback_failed_note(void);
int test_vconv_win_draft_cue_follows_the_box(void);
int test_vread_win_ai_press_speaks(void);
int test_vread_win_windows_waits_ready(void);
int test_vread_win_gate_refuses(void);
int test_vread_win_other_tokens_ignored(void);
int test_vread_win_error_releases_then_fresh_start(void);
int test_vread_win_start_refused_shows_error(void);
int test_vread_win_late_error_of_first_keeps_second(void);
int test_vread_win_resolve_reason(void);
int test_vread_win_empty_text_does_nothing(void);
int test_vread_win_settings_change_releases(void);
int test_vread_win_mode_switch_releases(void);
int test_vread_win_sample_stop_is_not_error(void);
int test_vread_win_chat_change_stops(void);
int test_vread_win_recording_disables_view(void);
int test_vread_win_old_context_messages_dropped(void);
int test_vread_win_no_context_gen_refuses(void);
int test_vread_win_zero_context_gen_quits(void);
int test_vread_win_stale_messages_before_new_context(void);
int test_vread_win_set_ops_releases_held_context(void);
int test_vread_win_set_ops_while_reading_other_table(void);
int test_vread_win_exited_while_reading(void);
int test_vread_win_exited_while_idle(void);
int test_vread_win_set_recording_stops_reading(void);
int test_vread_win_apply_unchanged_keeps_reading(void);
int test_vread_win_apply_switches_off_release(void);
int test_vread_win_engine_change_between_presses(void);
int test_vread_win_escape_then_ready_speaks_nothing(void);
int test_vread_win_second_press_before_ready_speaks_second(void);
int test_vread_win_kept_context_reused(void);
int test_vconv_win_audio_lost_leaves_ending(void);
int test_vconv_win_changed_chat_sends_and_speaks_nothing(void);
int test_vconv_win_reply_after_duck_is_full_volume(void);
int test_vconv_win_tts_error_quits_the_context(void);
int test_vconv_win_panel_dialog_pauses(void);
int test_vconv_win_speak_mode_follows_new_capture(void);
int test_vconv_win_external_send(void);
int test_vconv_win_external_send_keeps_words(void);
int test_vconv_win_external_send_unducks(void);
int test_vconv_win_new_reply_resets_times(void);
int test_vconv_win_progress_keeps_long_reply(void);
int test_vconv_win_notice_kept_if_ended_first(void);
int test_vconv_win_end_never_dropped(void);
int test_vconv_win_progress_throttled(void);
int test_vconv_win_parked_end_wins(void);
int test_vconv_win_old_echo_not_matched_while_new_reply_loads(void);
int test_vconv_win_ai_engine_started(void);
int test_vconv_win_ai_fallback_rest_of_reply(void);
int test_vconv_win_ai_and_windows_fail(void);
int test_vconv_win_ai_stale_error_ignored(void);
int test_vconv_win_no_notice_with_ai(void);
int test_vconv_win_ai_passing_retries(void);
int test_vconv_win_ai_fatal_note_once(void);
int test_vconv_win_ai_fatal_resolve(void);
int test_vconv_win_ai_fatal_format(void);
int test_vconv_win_ai_context_quit_on_voice_off(void);
int test_vconv_win_ai_fallback_keeps_duck(void);
int test_vconv_win_ai_start_failure_waits_for_speech(void);
int test_vconv_win_ai_fatal_while_not_playing(void);
int test_voice_ws_gate_off(void);
int test_voice_ws_echo(void);
int test_voice_ws_fragmented(void);
int test_voice_ws_reject(void);
int test_voice_ws_shutdown_wakes_receiver(void);
int test_voice_ws_redirect_refused(void);
int test_voice_ws_cancel_during_handshake(void);
int test_voice_ws_send_and_receive_concurrently(void);
int test_voice_live_openai_flow(void);
int test_voice_live_mistral_flow(void);
int test_voice_live_close_mid_handshake(void);
int test_voice_live_server_drops(void);
int test_voice_live_stale_token_feed(void);
int test_voice_live_openai_final_waits_for_every_commit(void);
int test_voice_live_openai_zero_commits_final_at_once(void);
int test_voice_live_connect_refused_reports_status(void);
int test_voice_live_start_refused_while_voice_off(void);
int test_voice_live_ready_timeout(void);
int test_voice_live_queue_overflow_fails_link(void);
int test_voice_live_error_event_not_fatal(void);
int test_voice_live_short_utterance_not_committed(void);
int test_voice_live_exit_wakes_ui(void);
int test_ai_http_post_ct_rejects_bad_content_type(void);
int test_thread_join_all_finish(void);
int test_thread_join_is_bounded(void);
int test_thread_join_full_list_closes_handle(void);
int test_ai_nc_fire_wakes_blocked_winhttp_call(void);
int test_web_fetch_trickling_head_ends_at_hop_deadline(void);
int test_web_fetch_trickling_head_ends_on_cancel_flag(void);

/* test_settings_label_win.c */
int test_settings_label_win_labels_fit_column(void);

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
    failed += test_settings_label_win_labels_fit_column();
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
    failed += test_icons_speaker_renders();
    failed += test_md_render_win_measure_equals_paint_and_runs();
    failed += test_chat_select_win_runs_match_paint();
    failed += test_chat_select_win_reason_in_row();
    failed += test_chat_select_win_thinking_pref_modes();
    failed += test_chat_select_win_thinking_compact_box();
    failed += test_chat_select_win_thinking_extended_box();
    failed += test_chat_select_win_thinking_auto_follows_complete();
    failed += test_chat_select_win_thinking_click_cycles_and_anchors();
    failed += test_chat_select_win_thinking_mode_change_holds_reader_place();
    failed += test_chat_select_win_thinking_wheel_fits_goes_to_thread();
    failed += test_chat_select_win_thinking_wheel_streaming_box_takes_it();
    failed += test_chat_select_win_thinking_wheel_mode_change_place();
    failed += test_chat_select_win_thinking_wheel_rows_keep_own_state();
    failed += test_chat_select_win_mouse_and_keys();
    failed += test_chat_select_win_query_word();
    failed += test_chat_select_win_drag_ignores_buttonless_move();
    failed += test_chat_select_win_short_list();
    failed += test_chat_read_win_button_rows();
    failed += test_chat_read_win_painted_equals_measured();
    failed += test_chat_read_win_off_heights_unchanged();
    failed += test_chat_read_win_tail_busy_hides_last();
    failed += test_chat_read_win_click_posts_id();
    failed += test_chat_read_win_reading_draws_stop();
    failed += test_chat_read_win_button_moves_to_new_row();
    failed += test_chat_read_win_off_paints_as_never_on();
    InitCommonControls();
    failed += test_ns_check_win_check_state_round_trips();
    failed += test_ns_check_win_click_toggles_and_paints();
    failed += test_ns_check_win_null_and_destroy_are_safe();
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
    failed += test_pw_reveal_win_unlisted_message_that_empties_field_masks();
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
    failed += test_notice_dlg_win_opens_with_class_caption_and_controls();
    failed += test_notice_dlg_win_no_action_button_when_action_empty();
    failed += test_notice_dlg_win_enter_on_ok_returns_zero_and_window_is_gone();
    failed += test_notice_dlg_win_escape_returns_zero();
    failed += test_notice_dlg_win_close_box_returns_zero();
    failed += test_notice_dlg_win_enter_on_focused_action_returns_one();
    failed += test_notice_dlg_win_tab_moves_ok_action_ok();
    failed += test_notice_dlg_win_tab_with_only_ok_stays_on_ok();
    failed += test_notice_dlg_win_owner_enable_state_is_restored();
    failed += test_notice_dlg_win_child_owner_disables_root();
    failed += test_notice_dlg_win_msg_shows_table_text();
    failed += test_notice_dlg_win_msg_bad_id_shows_nothing();
    failed += test_notice_dlg_win_wide_label_and_long_text_stay_inside();
    failed += test_notice_dlg_win_minimized_owner_centres_on_normal_position();
    failed += test_notice_dlg_win_close_hides_before_inner_loop_ends();
    failed += test_notice_dlg_win_single_notice_stays_visible_until_owner_enabled();
    failed += test_notice_dlg_win_held_enter_does_not_dismiss();
    failed += test_ns_font_win_bold_is_cached_and_bold();
    failed += test_telnet_io_win_connect_ok_and_refused();
    failed += test_telnet_io_win_resolve_fails();
    failed += test_telnet_io_win_negotiates_and_logs_in();
    failed += test_telnet_io_win_eof_reports_closed();
    failed += test_telnet_io_win_serial_unique();
    failed += test_winhttp_open_refuses_while_off();
    failed += test_winhttp_open_opens_while_on();
    failed += test_voice_audio_start_stop();
    failed += test_voice_audio_start_refused_when_gate_off();
    failed += test_voice_audio_double_start_is_one_thread();
    failed += test_voice_audio_restart();
    failed += test_voice_audio_enumerate_defined();
    failed += test_voice_audio_capture_no_device_or_data();
    failed += test_voice_audio_bad_id_falls_back();
    failed += test_voice_audio_queue_keeps_begin_and_end();
    failed += test_voice_audio_queue_grows_past_initial_capacity();
    failed += test_voice_audio_never_started();
    failed += test_voice_audio_test_capture_has_no_time_limit();
    failed += test_voice_audio_test_capture_keeps_no_samples();
    failed += test_voice_audio_ingest_applies_gain_live();
    failed += test_voice_audio_ingest_quiet_mic_not_all_zero();
    failed += test_voice_audio_set_gain_coalesces();
    failed += test_voice_audio_set_gain_after_begin_is_kept();
    failed += test_voice_audio_live_ingest_feeds_gained_samples();
    failed += test_voice_audio_conv_begin_headless();
    failed += test_voice_audio_conv_turn_posted();
    failed += test_voice_audio_conv_attach_live_feeds();
    failed += test_voice_audio_conv_hold_wipes();
    failed += test_voice_audio_conv_mode_listen_keeps_cutin_turn();
    failed += test_voice_audio_conv_preempted_is_reported();
    failed += test_voice_ptt_posted_keys();
    failed += test_voice_ptt_posted_right_shift();
    failed += test_voice_stt_refused_when_gate_off();
    failed += test_voice_stt_unresolvable_target();
    failed += test_voice_stt_cancel_unreachable();
    failed += test_voice_stt_cancel_all_idle();
    failed += test_voice_stt_retry_after_400_drops_language();
    failed += test_voice_stt_retry_twice_400_delivers_the_error();
    failed += test_voice_stt_no_retry_without_language_or_other_status();
    failed += test_voice_stt_cancel_closes_a_blocked_request();
    failed += test_voice_stt_gate_off_before_send_sends_nothing();
    failed += test_voice_stt_counts_one_open_per_upload();
    failed += test_voice_stt_long_transcript_delivered_whole();
    failed += test_voice_net_counts();
    failed += test_stt_upload_no_redirect();
    failed += test_stt_upload_policy_refused();
    failed += test_stt_upload_plain_still_works();
    failed += test_ai_stream_ex_wav_bytes();
    failed += test_ai_stream_ex_sse();
    failed += test_ai_stream_ex_statuses();
    failed += test_ai_stream_ex_no_redirect();
    failed += test_ai_stream_ex_cancel_hang();
    failed += test_ai_stream_ex_null_session();
    failed += test_ai_stream_ex_policy_refused();
    failed += test_ai_stream_ex_bad_recv_timeout();
    failed += test_ai_stream_ex_session_kept();
    failed += test_voice_lock_register_balances();
    failed += test_voice_lock_is_lock_event();
    failed += test_voice_voices_read_hkcu();
    failed += test_voice_tts_widen_map();
    failed += test_voice_tts_gate_off();
    failed += test_voice_tts_start_quit_wine();
    failed += test_voice_tts_speak_events_wine();
    failed += test_voice_tts_sample_quits_itself();
    failed += test_voice_voices_read_long_names();
    failed += test_voice_tts_exited_context_restarts();
    failed += test_voice_tts_reap_by_generation();
    failed += test_vconv_win_ends_ledger_zero();
    failed += test_vconv_win_activate_and_enable_pause();
    failed += test_vconv_win_full_turn();
    failed += test_vconv_win_stale_reply_events_ignored();
    failed += test_vconv_win_notice_once();
    failed += test_vconv_win_off_mid_reply();
    failed += test_vconv_win_events_after_their_moment_dropped();
    failed += test_voice_audio_conv_end_turn();
    failed += test_vread_win_ai_press_speaks();
    failed += test_vread_win_windows_waits_ready();
    failed += test_vread_win_gate_refuses();
    failed += test_vread_win_other_tokens_ignored();
    failed += test_vread_win_error_releases_then_fresh_start();
    failed += test_vread_win_start_refused_shows_error();
    failed += test_vread_win_late_error_of_first_keeps_second();
    failed += test_vread_win_resolve_reason();
    failed += test_vread_win_empty_text_does_nothing();
    failed += test_vread_win_settings_change_releases();
    failed += test_vread_win_mode_switch_releases();
    failed += test_vread_win_sample_stop_is_not_error();
    failed += test_vread_win_chat_change_stops();
    failed += test_vread_win_recording_disables_view();
    failed += test_vread_win_old_context_messages_dropped();
    failed += test_vread_win_no_context_gen_refuses();
    failed += test_vread_win_zero_context_gen_quits();
    failed += test_vread_win_stale_messages_before_new_context();
    failed += test_vread_win_set_ops_releases_held_context();
    failed += test_vread_win_set_ops_while_reading_other_table();
    failed += test_vread_win_exited_while_reading();
    failed += test_vread_win_exited_while_idle();
    failed += test_vread_win_set_recording_stops_reading();
    failed += test_vread_win_apply_unchanged_keeps_reading();
    failed += test_vread_win_apply_switches_off_release();
    failed += test_vread_win_engine_change_between_presses();
    failed += test_vread_win_escape_then_ready_speaks_nothing();
    failed += test_vread_win_second_press_before_ready_speaks_second();
    failed += test_vread_win_kept_context_reused();
    failed += test_voice_render_null_plays_and_done();
    failed += test_voice_render_flush_then_old_data();
    failed += test_voice_render_gain();
    failed += test_voice_render_preempt();
    failed += test_voice_render_real_wine();
    failed += test_voice_audio_close_cannot_be_lost();
    failed += test_voice_render_end_without_data_done();
    failed += test_voice_render_pending_keeps_order();
    failed += test_voice_ais_gate_off();
    failed += test_voice_ais_fake_done();
    failed += test_voice_ais_empty_chunk_fails();
    failed += test_voice_ais_stop_then_speak_no_old_audio();
    failed += test_voice_ais_gain_survives_open();
    failed += test_voice_ais_reopen_after_preempt();
    failed += test_voice_ais_preempt_while_playing();
    failed += test_voice_ais_done_guard_waits_for_progress();
    failed += test_voice_ais_dead_worker_never_pushes();
    failed += test_voice_ais_resolve_error_at_once();
    failed += test_voice_ais_sample_self_quits();
    failed += test_voice_ais_lost_chunk_done_still_ends();
    failed += test_voice_ais_partial_chunk_failure_offset();
    failed += test_voice_ais_same_reply_id_respeak();
    failed += test_voice_ais_chunk_cap_is_an_error();
    failed += test_voice_ais_quit_all_two_contexts();
    failed += test_voice_ais_custom_wav_done();
    failed += test_voice_ais_custom_no_key();
    failed += test_voice_ais_mistral_sse();
    failed += test_voice_ais_mistral_capture();
    failed += test_voice_ais_sse_wav_then_error_event();
    failed += test_voice_ais_sse_error_fixture_is_format();
    failed += test_voice_ais_sse_done_only();
    failed += test_voice_ais_badrate_is_format();
    failed += test_voice_ais_endless_audio_capped();
    failed += test_voice_ais_request_time_capped();
    failed += test_voice_ais_not_wav_is_format();
    failed += test_voice_ais_http_302_redirect();
    failed += test_voice_ais_endless_error_body_capped();
    failed += test_voice_ais_second_header_fails();
    failed += test_voice_ais_empty_body_fails();
    failed += test_voice_ais_http_401();
    failed += test_voice_ais_http_429();
    failed += test_voice_ais_http_404_and_422();
    failed += test_voice_ais_oversized_line_fails();
    failed += test_voice_ais_mistral_no_done_fails();
    failed += test_voice_ais_partial_frame_fails();
    failed += test_voice_ais_quit_while_server_hangs();
    failed += test_voice_audio_conv_end_then_same_token_open();
    failed += test_vconv_win_send_while_hearing_batch();
    failed += test_vconv_win_send_while_hearing_live_link_fails_to_fallback();
    failed += test_vconv_win_ending_wipes_audio_keeps_words();
    failed += test_vconv_win_fallback_failed_note();
    failed += test_vconv_win_draft_cue_follows_the_box();
    failed += test_vconv_win_audio_lost_leaves_ending();
    failed += test_vconv_win_changed_chat_sends_and_speaks_nothing();
    failed += test_vconv_win_reply_after_duck_is_full_volume();
    failed += test_vconv_win_tts_error_quits_the_context();
    failed += test_vconv_win_panel_dialog_pauses();
    failed += test_vconv_win_speak_mode_follows_new_capture();
    failed += test_vconv_win_external_send();
    failed += test_vconv_win_external_send_keeps_words();
    failed += test_vconv_win_external_send_unducks();
    failed += test_vconv_win_new_reply_resets_times();
    failed += test_vconv_win_progress_keeps_long_reply();
    failed += test_vconv_win_notice_kept_if_ended_first();
    failed += test_vconv_win_end_never_dropped();
    failed += test_vconv_win_progress_throttled();
    failed += test_vconv_win_parked_end_wins();
    failed += test_vconv_win_old_echo_not_matched_while_new_reply_loads();
    failed += test_vconv_win_ai_engine_started();
    failed += test_vconv_win_ai_fallback_rest_of_reply();
    failed += test_vconv_win_ai_and_windows_fail();
    failed += test_vconv_win_ai_stale_error_ignored();
    failed += test_vconv_win_no_notice_with_ai();
    failed += test_vconv_win_ai_passing_retries();
    failed += test_vconv_win_ai_fatal_note_once();
    failed += test_vconv_win_ai_fatal_resolve();
    failed += test_vconv_win_ai_fatal_format();
    failed += test_vconv_win_ai_context_quit_on_voice_off();
    failed += test_vconv_win_ai_fallback_keeps_duck();
    failed += test_vconv_win_ai_start_failure_waits_for_speech();
    failed += test_vconv_win_ai_fatal_while_not_playing();
    failed += test_voice_ws_gate_off();
    failed += test_voice_ws_echo();
    failed += test_voice_ws_fragmented();
    failed += test_voice_ws_reject();
    failed += test_voice_ws_shutdown_wakes_receiver();
    failed += test_voice_ws_redirect_refused();
    failed += test_voice_ws_cancel_during_handshake();
    failed += test_voice_ws_send_and_receive_concurrently();
    failed += test_voice_live_openai_flow();
    failed += test_voice_live_mistral_flow();
    failed += test_voice_live_close_mid_handshake();
    failed += test_voice_live_server_drops();
    failed += test_voice_live_stale_token_feed();
    failed += test_voice_live_openai_final_waits_for_every_commit();
    failed += test_voice_live_openai_zero_commits_final_at_once();
    failed += test_voice_live_connect_refused_reports_status();
    failed += test_voice_live_start_refused_while_voice_off();
    failed += test_voice_live_ready_timeout();
    failed += test_voice_live_queue_overflow_fails_link();
    failed += test_voice_live_error_event_not_fatal();
    failed += test_voice_live_short_utterance_not_committed();
    failed += test_voice_live_exit_wakes_ui();
    failed += test_ai_http_post_ct_rejects_bad_content_type();
    failed += test_thread_join_all_finish();
    failed += test_thread_join_is_bounded();
    failed += test_thread_join_full_list_closes_handle();
    failed += test_ai_nc_fire_wakes_blocked_winhttp_call();
    failed += test_web_fetch_trickling_head_ends_at_hop_deadline();
    failed += test_web_fetch_trickling_head_ends_on_cancel_flag();
    failed += test_ns_slider_win_keys_and_notify();
    failed += test_ns_draw_level_meter_win_zones_fill_and_hold();
    failed += run_ns_tip_tests();

    printf("\n[pty] re-executing with a console of our own "
           "(see win_runner.c for why)\n");
    int pty_failed = spawn_pty_child();
    if (pty_failed < 0) {
        printf("[SKIP] ConPTY cases -- could not re-execute this harness\n");
        _tf_run += PTY_CASE_COUNT;               /* counted: each case is a skip */
        _tf_skipped += PTY_CASE_COUNT;
    } else {
        failed += pty_failed;
        _tf_run += PTY_CASE_COUNT;
        _tf_failed += pty_failed;
    }

    printf("\nTests Run: %d, Failed: %d, Skipped: %d\n", _tf_run, _tf_failed, _tf_skipped);
    return failed ? 1 : 0;
}

#endif /* _WIN32 */
