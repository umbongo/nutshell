#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>

int _tf_failed = 0;
int _tf_run = 0;

/* Forward declarations from test files */
/* test_vector.c */
int test_vector_init(void);
int test_vector_push_and_get(void);
int test_vector_get_oob(void);
int test_vector_set(void);
int test_vector_remove(void);
int test_vector_remove_first(void);
int test_vector_remove_last(void);
int test_vector_remove_oob(void);
int test_vector_grows(void);
int test_vector_free_reuse(void);

/* test_string_utils.c */
int test_str_dup_basic(void);
int test_str_dup_empty(void);
int test_str_dup_null(void);
int test_str_cat_basic(void);
int test_str_cat_truncates(void);
int test_str_cat_full_buffer(void);
int test_str_cat_null_src(void);
int test_str_trim_both_ends(void);
int test_str_trim_leading(void);
int test_str_trim_trailing(void);
int test_str_trim_no_whitespace(void);
int test_str_trim_all_whitespace(void);
int test_str_trim_empty(void);
int test_str_starts_with_yes(void);
int test_str_starts_with_no(void);
int test_str_starts_with_null(void);
int test_str_ends_with_yes(void);
int test_str_ends_with_no(void);
int test_str_ends_with_null(void);
int test_ansi_strip_plain(void);
int test_ansi_strip_sgr_reset(void);
int test_ansi_strip_colour(void);
int test_ansi_strip_osc_title(void);
int test_ansi_strip_cr_removed(void);
int test_ansi_strip_empty(void);
int test_ansi_strip_null_dst(void);

/* test_logger.c */
int test_logger_init_stderr_only(void);
int test_logger_creates_file(void);
int test_logger_file_contains_message(void);
int test_logger_min_level_filters(void);
int test_logger_all_levels_no_crash(void);
int test_logger_close_without_file(void);

/* test_json.c */
int test_tok_empty(void);
int test_tok_null_src(void);
int test_tok_structural_chars(void);
int test_tok_string_basic(void);
int test_tok_string_escapes(void);
int test_tok_string_tab_escape(void);
int test_tok_number_integer(void);
int test_tok_number_float(void);
int test_tok_number_negative(void);
int test_tok_true(void);
int test_tok_false(void);
int test_tok_null(void);
int test_tok_whitespace_skipped(void);
int test_tok_invalid_char(void);
int test_tok_truncated_keyword(void);
int test_parse_null(void);
int test_parse_true(void);
int test_parse_false(void);
int test_parse_number(void);
int test_parse_number_float(void);
int test_parse_string(void);
int test_parse_empty_object(void);
int test_parse_simple_object(void);
int test_parse_object_number(void);
int test_parse_object_bool(void);
int test_parse_nested_object(void);
int test_parse_empty_array(void);
int test_parse_array(void);
int test_parse_array_of_objects(void);
int test_parse_null_src(void);
int test_parse_invalid_json(void);
int test_parse_missing_colon(void);
int test_parse_obj_get_missing_key(void);
int test_parse_obj_str_type_mismatch(void);
int test_parse_obj_num_fallback(void);
int test_json_free_null(void);

/* test_config.c */
int test_config_default_settings(void);
int test_config_profile_new_free(void);
int test_config_new_default_free(void);
int test_config_load_null_path(void);
int test_config_load_nonexistent(void);
int test_config_load_invalid_json(void);
int test_config_load_empty_object(void);
int test_config_roundtrip_settings(void);
int test_config_roundtrip_profile(void);
int test_config_roundtrip_multiple_profiles(void);
int test_config_save_null(void);
int test_config_default_ai_provider(void);
int test_config_default_ai_key_empty(void);
int test_config_roundtrip_ai_settings(void);
int test_config_ai_key_encrypted_on_disk(void);

/* test_session_manager.c */
int test_profile_struct(void);

/* test_tabs.c */
int test_tabmgr_init(void);
int test_tabmgr_add_first(void);
int test_tabmgr_add_multiple(void);
int test_tabmgr_switch(void);
int test_tabmgr_remove_middle(void);
int test_tabmgr_remove_last_tab(void);
int test_tabmgr_switch_invalid(void);
int test_tabmgr_remove_invalid(void);
int test_tabmgr_active_no_tabs(void);
int test_tabmgr_max_capacity(void);
int test_tabmgr_close_active_tab(void);
int test_tabmgr_reopen_no_id_collision(void);
int test_tabmgr_status_independence(void);
int test_tabmgr_find(void);
int test_tabmgr_navigate_right(void);
int test_tabmgr_navigate_left(void);
int test_tabmgr_navigate_wrap_right(void);
int test_tabmgr_navigate_wrap_left(void);
int test_tabmgr_navigate_single_tab(void);
int test_tabmgr_navigate_no_tabs(void);
int test_tabmgr_navigate_zero_delta(void);

/* test_tooltip.c */
int test_tooltip_format_3661(void);
int test_tooltip_format_59(void);
int test_tooltip_format_zero(void);
int test_tooltip_format_90(void);
int test_tooltip_connected(void);
int test_tooltip_disconnected(void);
int test_tooltip_with_log(void);
int test_tooltip_null_buf(void);
int test_tooltip_long_hostname(void);
int test_tooltip_name_first_line(void);
int test_tooltip_null_name(void);
int test_tooltip_empty_name(void);
int test_tooltip_logging_enabled(void);
int test_tooltip_logging_disabled(void);

/* test_paste_preview.c */
int test_paste_format_lines_single(void);
int test_paste_format_lines_multi(void);
int test_paste_format_lines_crlf(void);
int test_paste_format_lines_trailing_newline(void);
int test_paste_format_lines_no_trailing_newline(void);
int test_paste_format_lines_empty(void);
int test_paste_format_lines_null(void);
int test_paste_format_lines_blank_lines(void);
int test_paste_format_lines_long_line(void);
int test_paste_line_free_null(void);
int test_paste_build_summary_single(void);
int test_paste_build_summary_multi(void);
int test_paste_build_summary_null_buf(void);
int test_paste_build_summary_small_buf(void);

/* test_settings.c */
int test_settings_validate_defaults(void);
int test_settings_validate_font_size_low(void);
int test_settings_validate_font_size_high(void);
int test_settings_validate_font_size_snap_7(void);
int test_settings_validate_font_size_snap_9(void);
int test_settings_validate_font_size_snap_15(void);
int test_settings_validate_font_size_snap_19(void);
int test_settings_validate_scrollback_low(void);
int test_settings_validate_scrollback_high(void);
int test_settings_validate_paste_delay_neg(void);
int test_settings_validate_paste_delay_high(void);
int test_settings_validate_empty_font(void);
int test_settings_validate_null(void);
int test_settings_validate_via_load(void);

/* test_crypto.c */
int test_crypto_roundtrip_basic(void);
int test_crypto_roundtrip_empty(void);
int test_crypto_roundtrip_single_char(void);
int test_crypto_roundtrip_long(void);
int test_crypto_roundtrip_special_chars(void);
int test_crypto_nonce_unique(void);
int test_crypto_is_encrypted_yes(void);
int test_crypto_is_encrypted_no(void);
int test_crypto_wrong_key(void);
int test_crypto_tampered_ciphertext(void);
int test_crypto_truncated_blob(void);
int test_crypto_null_inputs(void);
int test_crypto_output_too_small(void);
int test_crypto_decrypt_not_encrypted(void);

#ifndef NO_SSH_LIBS
/* test_ssh.c */
int test_ssh_safety_checks(void);
int test_ssh_channel_safety(void);
int test_ssh_io_safety(void);
int test_ssh_pty_safety(void);
int test_pty_resize_dedup(void);
int test_pty_resize_initial_state(void);
int test_ssh_connect_fail(void);
int test_ssh_session_blocking(void);

/* test_knownhosts.c */
int test_kh_new_host_returns_new(void);
int test_kh_add_then_check_ok(void);
int test_kh_mismatch(void);
int test_kh_fingerprint_populated(void);
int test_kh_file_created_on_add(void);
int test_kh_multiple_hosts(void);
int test_kh_null_inputs(void);
int test_kh_key_rotation(void);

/* test_key_auth.c */
int test_key_auth_null_session(void);
int test_key_auth_not_connected(void);
int test_session_passphrase_initially_empty(void);
int test_session_passphrase_write_and_free(void);
#endif

/* test_term.c */
int test_term_buffer(void);
int test_term_parser(void);
int test_term_cursor_moves(void);
int test_term_extended_moves(void);
int test_term_sgr_flags(void);
int test_term_resize_basic(void);
int test_term_resize_reflow(void);
int test_term_resize_cursor_edge(void);
int test_term_utf8(void);

/* test_theme.c */
int test_theme_dark_background(void);
int test_theme_light_background(void);
int test_theme_mid_gray(void);
int test_theme_luminance_red(void);
int test_theme_luminance_green(void);
int test_theme_luminance_blue(void);
int test_theme_pure_black(void);
int test_theme_pure_white(void);

/* test_vt_sequences.c */
int test_vt_osc_title_0(void);
int test_vt_osc_title_2(void);
int test_vt_osc_st_terminator(void);
int test_vt_dectcem_show(void);
int test_vt_dectcem_hide(void);
int test_vt_alt_screen_enter(void);
int test_vt_alt_screen_exit(void);
int test_vt_app_cursor_enable(void);
int test_vt_app_cursor_disable(void);
int test_vt_insert_mode_enable(void);
int test_vt_insert_mode_disable(void);
int test_vt_osc_no_terminator(void);
int test_vt_unknown_private_mode(void);
int test_vt_osc_special_chars(void);
int test_vt_alt_screen_isolation(void);

/* test_connect_anim.c */
int test_anim_dots_zero_elapsed(void);
int test_anim_dots_just_under_interval(void);
int test_anim_dots_exactly_one_interval(void);
int test_anim_dots_multiple_intervals(void);
int test_anim_dots_clamped_to_max(void);
int test_anim_dots_large_interval(void);
int test_anim_dots_zero_interval(void);
int test_anim_dots_max_zero(void);
int test_anim_text_zero_dots(void);
int test_anim_text_one_dot(void);
int test_anim_text_three_dots(void);
int test_anim_text_buf_just_fits_prefix(void);
int test_anim_text_dots_truncated_by_buf(void);
int test_anim_text_null_buf(void);
int test_anim_text_zero_size(void);
int test_anim_text_small_buf_truncates_prefix(void);
int test_anim_text_single_byte_buf(void);

/* test_zoom.c */
int test_zoom_exact_fit(void);
int test_zoom_odd_cell_sizes(void);
int test_zoom_unit_cells(void);
int test_zoom_large_window(void);
int test_zoom_hgutter(void);
int test_zoom_vgutter(void);
int test_zoom_both_gutters(void);
int test_zoom_zero_char_w(void);
int test_zoom_zero_char_h(void);
int test_zoom_zero_client_w(void);
int test_zoom_zero_term_h(void);

/* test_snap.c */
int test_snap_calc_exact_fit(void);
int test_snap_calc_right_gutter(void);
int test_snap_calc_bottom_gutter(void);
int test_snap_calc_both_gutters(void);
int test_snap_calc_odd_cell_size(void);
int test_snap_calc_zoom_in(void);
int test_snap_calc_zoom_out(void);
int test_snap_calc_null_outputs(void);
int test_snap_calc_min_cols_clamp(void);
int test_snap_calc_min_rows_clamp(void);
int test_snap_calc_negative_term_h(void);
int test_snap_calc_zero_client_w(void);
int test_snap_adjust_bottom_right(void);
int test_snap_adjust_top_left(void);
int test_snap_adjust_left(void);
int test_snap_adjust_right(void);
int test_snap_adjust_top(void);
int test_snap_adjust_bottom(void);
int test_snap_adjust_top_right(void);
int test_snap_adjust_bottom_left(void);
int test_snap_adjust_no_change_when_exact(void);
int test_snap_roundtrip_bottomright(void);
int test_snap_roundtrip_topleft(void);

/* test_scrollbar.c */
int test_sb_npos_at_bottom(void);
int test_sb_npos_scrolled_back(void);
int test_sb_npos_at_top(void);
int test_sb_npos_no_content(void);
int test_sb_npos_clamp_negative(void);
int test_sb_max_off_normal(void);
int test_sb_max_off_capped(void);
int test_sb_max_off_no_scrollback(void);
int test_sb_max_off_zero_setting(void);
int test_sb_offset_from_npos_bottom(void);
int test_sb_offset_from_npos_top(void);
int test_sb_offset_from_npos_mid(void);
int test_sb_roundtrip(void);
int test_sb_roundtrip_extremes(void);
int test_sb_npos_exceeds_word(void);

/* test_log_format.c */
int test_logfmt_basic_name(void);
int test_logfmt_spaces_to_underscores(void);
int test_logfmt_empty_dir(void);
int test_logfmt_null_dir(void);
int test_logfmt_timestamp_format(void);
int test_logfmt_long_name(void);
int test_logfmt_null_name(void);
int test_logfmt_empty_name(void);
int test_logfmt_null_buf(void);
int test_logfmt_zero_bufsize(void);
int test_logfmt_special_chars(void);

/* test_dirty.c */
int test_dirty_init_all_dirty(void);
int test_dirty_clear_all(void);
int test_dirty_put_char(void);
int test_dirty_only_written_row(void);
int test_dirty_erase_display(void);
int test_dirty_erase_line(void);
int test_dirty_scroll(void);
int test_dirty_resize(void);
int test_dirty_null_safety(void);
int test_dirty_multi_write_clear(void);
int test_dirty_alt_screen(void);

/* test_color_consistency.c */
int test_cc_default_is_pure_light(void);
int test_cc_init_cells_color_default(void);
int test_cc_resize_preserves_color_default(void);
int test_cc_resize_shrink_grow(void);
int test_cc_scroll_new_row_color_default(void);
int test_cc_erase_display_color_default(void);
int test_cc_erase_line_color_default(void);
int test_cc_alt_screen_color_default(void);
int test_cc_sgr_reset_color_default(void);
int test_cc_config_roundtrip_pure_light(void);
int test_cc_repeated_resize(void);
int test_cc_scrollback_resize(void);
int test_cc_write_after_resize(void);

/* test_ai_http.c */
int test_ai_http_response_free_null(void);
int test_ai_http_response_free_allocated(void);
int test_ai_http_stub_conversation_flow(void);
int test_ai_http_stub_error_response(void);

/* test_ai_prompt.c */
int test_ai_conv_init(void);
int test_ai_conv_add_user(void);
int test_ai_conv_add_full(void);
int test_ai_conv_null_safety(void);
int test_ai_build_body_basic(void);
int test_ai_build_body_escapes(void);
int test_ai_build_body_overflow(void);
int test_ai_build_body_null(void);
int test_ai_parse_response_basic(void);
int test_ai_parse_response_empty_choices(void);
int test_ai_parse_response_malformed(void);
int test_ai_extract_command_found(void);
int test_ai_extract_command_none(void);
int test_ai_extract_command_no_end(void);
int test_ai_extract_command_empty(void);
int test_ai_extract_command_null(void);
int test_ai_provider_url_deepseek(void);
int test_ai_provider_url_openai(void);
int test_ai_provider_url_unknown(void);
int test_ai_provider_model_deepseek(void);
int test_ai_system_prompt_with_terminal(void);
int test_ai_system_prompt_no_terminal(void);

/* test_term_extract.c */
int test_extract_empty_term(void);
int test_extract_single_line(void);
int test_extract_multi_line(void);
int test_extract_trims_trailing_spaces(void);
int test_extract_utf8_codepoint(void);
int test_extract_buf_too_small(void);
int test_extract_null_safety(void);
int test_extract_last_n_basic(void);
int test_extract_last_n_with_scrollback(void);
int test_extract_last_n_exceeds_total(void);
int test_extract_last_n_zero(void);

/* test_color.c */
int test_color256_palette_ansi(void);
int test_color256_palette_cube(void);
int test_color256_palette_gray(void);
int test_color_256_fg(void);
int test_color_256_bg(void);
int test_color_rgb_fg(void);
int test_color_rgb_bg(void);
int test_color_sgr_reset(void);
int test_color_mixed_sgr(void);
int test_color_256_interleaved(void);
int test_color_256_missing_param(void);
int test_color_rgb_short_param(void);
int test_color_256_oob(void);
int test_color_rgb_black(void);
int test_color_rgb_white(void);
int test_color_sgr_default_fg(void);
int test_color_sgr_default_bg(void);

/* ---- Main ---------------------------------------------------------------- */

int main(void) {
    int failed = 0;

    printf("\n--- Core ---\n");
    failed += test_vector_init();
    failed += test_vector_push_and_get();
    failed += test_vector_get_oob();
    failed += test_vector_set();
    failed += test_vector_remove();
    failed += test_vector_remove_first();
    failed += test_vector_remove_last();
    failed += test_vector_remove_oob();
    failed += test_vector_grows();
    failed += test_vector_free_reuse();
    failed += test_str_dup_basic();
    failed += test_str_dup_empty();
    failed += test_str_dup_null();
    failed += test_str_cat_basic();
    failed += test_str_cat_truncates();
    failed += test_str_cat_full_buffer();
    failed += test_str_cat_null_src();
    failed += test_str_trim_both_ends();
    failed += test_str_trim_leading();
    failed += test_str_trim_trailing();
    failed += test_str_trim_no_whitespace();
    failed += test_str_trim_all_whitespace();
    failed += test_str_trim_empty();
    failed += test_str_starts_with_yes();
    failed += test_str_starts_with_no();
    failed += test_str_starts_with_null();
    failed += test_str_ends_with_yes();
    failed += test_str_ends_with_no();
    failed += test_str_ends_with_null();
    failed += test_ansi_strip_plain();
    failed += test_ansi_strip_sgr_reset();
    failed += test_ansi_strip_colour();
    failed += test_ansi_strip_osc_title();
    failed += test_ansi_strip_cr_removed();
    failed += test_ansi_strip_empty();
    failed += test_ansi_strip_null_dst();
    failed += test_logger_init_stderr_only();
    failed += test_logger_creates_file();
    failed += test_logger_file_contains_message();
    failed += test_logger_min_level_filters();
    failed += test_logger_all_levels_no_crash();
    failed += test_logger_close_without_file();

    printf("\n--- Config & UI ---\n");
    /* JSON Tokenizer */
    failed += test_tok_empty();
    failed += test_tok_null_src();
    failed += test_tok_structural_chars();
    failed += test_tok_string_basic();
    failed += test_tok_string_escapes();
    failed += test_tok_string_tab_escape();
    failed += test_tok_number_integer();
    failed += test_tok_number_float();
    failed += test_tok_number_negative();
    failed += test_tok_true();
    failed += test_tok_false();
    failed += test_tok_null();
    failed += test_tok_whitespace_skipped();
    failed += test_tok_invalid_char();
    failed += test_tok_truncated_keyword();

    /* JSON Parser */
    failed += test_parse_null();
    failed += test_parse_true();
    failed += test_parse_false();
    failed += test_parse_number();
    failed += test_parse_number_float();
    failed += test_parse_string();
    failed += test_parse_empty_object();
    failed += test_parse_simple_object();
    failed += test_parse_object_number();
    failed += test_parse_object_bool();
    failed += test_parse_nested_object();
    failed += test_parse_empty_array();
    failed += test_parse_array();
    failed += test_parse_array_of_objects();
    failed += test_parse_null_src();
    failed += test_parse_invalid_json();
    failed += test_parse_missing_colon();
    failed += test_parse_obj_get_missing_key();
    failed += test_parse_obj_str_type_mismatch();
    failed += test_parse_obj_num_fallback();
    failed += test_json_free_null();

    /* Config */
    failed += test_config_default_settings();
    failed += test_config_profile_new_free();
    failed += test_config_new_default_free();
    failed += test_config_load_null_path();
    failed += test_config_load_nonexistent();
    failed += test_config_load_invalid_json();
    failed += test_config_load_empty_object();
    failed += test_config_roundtrip_settings();
    failed += test_config_roundtrip_profile();
    failed += test_config_roundtrip_multiple_profiles();
    failed += test_config_save_null();
    failed += test_config_default_ai_provider();
    failed += test_config_default_ai_key_empty();
    failed += test_config_roundtrip_ai_settings();
    failed += test_config_ai_key_encrypted_on_disk();

    /* Session Manager */
    failed += test_profile_struct();

    /* Tab Manager */
    failed += test_tabmgr_init();
    failed += test_tabmgr_add_first();
    failed += test_tabmgr_add_multiple();
    failed += test_tabmgr_switch();
    failed += test_tabmgr_remove_middle();
    failed += test_tabmgr_remove_last_tab();
    failed += test_tabmgr_switch_invalid();
    failed += test_tabmgr_remove_invalid();
    failed += test_tabmgr_active_no_tabs();
    failed += test_tabmgr_max_capacity();
    failed += test_tabmgr_close_active_tab();
    failed += test_tabmgr_reopen_no_id_collision();
    failed += test_tabmgr_status_independence();
    failed += test_tabmgr_find();
    failed += test_tabmgr_navigate_right();
    failed += test_tabmgr_navigate_left();
    failed += test_tabmgr_navigate_wrap_right();
    failed += test_tabmgr_navigate_wrap_left();
    failed += test_tabmgr_navigate_single_tab();
    failed += test_tabmgr_navigate_no_tabs();
    failed += test_tabmgr_navigate_zero_delta();

    /* Tooltip */
    failed += test_tooltip_format_3661();
    failed += test_tooltip_format_59();
    failed += test_tooltip_format_zero();
    failed += test_tooltip_format_90();
    failed += test_tooltip_connected();
    failed += test_tooltip_disconnected();
    failed += test_tooltip_with_log();
    failed += test_tooltip_null_buf();
    failed += test_tooltip_long_hostname();
    failed += test_tooltip_name_first_line();
    failed += test_tooltip_null_name();
    failed += test_tooltip_empty_name();
    failed += test_tooltip_logging_enabled();
    failed += test_tooltip_logging_disabled();

    /* Paste Preview */
    printf("\n--- Paste Preview ---\n");
    failed += test_paste_format_lines_single();
    failed += test_paste_format_lines_multi();
    failed += test_paste_format_lines_crlf();
    failed += test_paste_format_lines_trailing_newline();
    failed += test_paste_format_lines_no_trailing_newline();
    failed += test_paste_format_lines_empty();
    failed += test_paste_format_lines_null();
    failed += test_paste_format_lines_blank_lines();
    failed += test_paste_format_lines_long_line();
    failed += test_paste_line_free_null();
    failed += test_paste_build_summary_single();
    failed += test_paste_build_summary_multi();
    failed += test_paste_build_summary_null_buf();
    failed += test_paste_build_summary_small_buf();

    /* Settings Validation */
    failed += test_settings_validate_defaults();
    failed += test_settings_validate_font_size_low();
    failed += test_settings_validate_font_size_high();
    failed += test_settings_validate_font_size_snap_7();
    failed += test_settings_validate_font_size_snap_9();
    failed += test_settings_validate_font_size_snap_15();
    failed += test_settings_validate_font_size_snap_19();
    failed += test_settings_validate_scrollback_low();
    failed += test_settings_validate_scrollback_high();
    failed += test_settings_validate_paste_delay_neg();
    failed += test_settings_validate_paste_delay_high();
    failed += test_settings_validate_empty_font();
    failed += test_settings_validate_null();
    failed += test_settings_validate_via_load();

    printf("\n--- Crypto ---\n");
    failed += test_crypto_roundtrip_basic();
    failed += test_crypto_roundtrip_empty();
    failed += test_crypto_roundtrip_single_char();
    failed += test_crypto_roundtrip_long();
    failed += test_crypto_roundtrip_special_chars();
    failed += test_crypto_nonce_unique();
    failed += test_crypto_is_encrypted_yes();
    failed += test_crypto_is_encrypted_no();
    failed += test_crypto_wrong_key();
    failed += test_crypto_tampered_ciphertext();
    failed += test_crypto_truncated_blob();
    failed += test_crypto_null_inputs();
    failed += test_crypto_output_too_small();
    failed += test_crypto_decrypt_not_encrypted();

#ifndef NO_SSH_LIBS
    printf("\n--- SSH ---\n");
    failed += test_ssh_safety_checks();
    failed += test_ssh_channel_safety();
    failed += test_ssh_io_safety();
    failed += test_ssh_pty_safety();
    failed += test_pty_resize_dedup();
    failed += test_pty_resize_initial_state();
    failed += test_ssh_connect_fail();
    failed += test_ssh_session_blocking();

    printf("\n--- Known Hosts ---\n");
    failed += test_kh_new_host_returns_new();
    failed += test_kh_add_then_check_ok();
    failed += test_kh_mismatch();
    failed += test_kh_fingerprint_populated();
    failed += test_kh_file_created_on_add();
    failed += test_kh_multiple_hosts();
    failed += test_kh_null_inputs();
    failed += test_kh_key_rotation();

    printf("\n--- Key Auth ---\n");
    failed += test_key_auth_null_session();
    failed += test_key_auth_not_connected();
    failed += test_session_passphrase_initially_empty();
    failed += test_session_passphrase_write_and_free();
#endif

    printf("\n--- Term ---\n");
    failed += test_term_buffer();
    failed += test_term_parser();
    failed += test_term_cursor_moves();
    failed += test_term_extended_moves();
    failed += test_term_sgr_flags();
    failed += test_term_resize_basic();
    failed += test_term_resize_reflow();
    failed += test_term_resize_cursor_edge();
    failed += test_term_utf8();

    printf("\n--- VT Sequences ---\n");
    failed += test_vt_osc_title_0();
    failed += test_vt_osc_title_2();
    failed += test_vt_osc_st_terminator();
    failed += test_vt_dectcem_show();
    failed += test_vt_dectcem_hide();
    failed += test_vt_alt_screen_enter();
    failed += test_vt_alt_screen_exit();
    failed += test_vt_app_cursor_enable();
    failed += test_vt_app_cursor_disable();
    failed += test_vt_insert_mode_enable();
    failed += test_vt_insert_mode_disable();
    failed += test_vt_osc_no_terminator();
    failed += test_vt_unknown_private_mode();
    failed += test_vt_osc_special_chars();
    failed += test_vt_alt_screen_isolation();

    printf("\n--- Theme ---\n");
    failed += test_theme_dark_background();
    failed += test_theme_light_background();
    failed += test_theme_mid_gray();
    failed += test_theme_luminance_red();
    failed += test_theme_luminance_green();
    failed += test_theme_luminance_blue();
    failed += test_theme_pure_black();
    failed += test_theme_pure_white();

    printf("\n--- Color Consistency ---\n");
    failed += test_cc_default_is_pure_light();
    failed += test_cc_init_cells_color_default();
    failed += test_cc_resize_preserves_color_default();
    failed += test_cc_resize_shrink_grow();
    failed += test_cc_scroll_new_row_color_default();
    failed += test_cc_erase_display_color_default();
    failed += test_cc_erase_line_color_default();
    failed += test_cc_alt_screen_color_default();
    failed += test_cc_sgr_reset_color_default();
    failed += test_cc_config_roundtrip_pure_light();
    failed += test_cc_repeated_resize();
    failed += test_cc_scrollback_resize();
    failed += test_cc_write_after_resize();

    printf("\n--- Dirty Tracking ---\n");
    failed += test_dirty_init_all_dirty();
    failed += test_dirty_clear_all();
    failed += test_dirty_put_char();
    failed += test_dirty_only_written_row();
    failed += test_dirty_erase_display();
    failed += test_dirty_erase_line();
    failed += test_dirty_scroll();
    failed += test_dirty_resize();
    failed += test_dirty_null_safety();
    failed += test_dirty_multi_write_clear();
    failed += test_dirty_alt_screen();

    printf("\n--- AI HTTP ---\n");
    failed += test_ai_http_response_free_null();
    failed += test_ai_http_response_free_allocated();
    failed += test_ai_http_stub_conversation_flow();
    failed += test_ai_http_stub_error_response();

    printf("\n--- AI Prompt ---\n");
    failed += test_ai_conv_init();
    failed += test_ai_conv_add_user();
    failed += test_ai_conv_add_full();
    failed += test_ai_conv_null_safety();
    failed += test_ai_build_body_basic();
    failed += test_ai_build_body_escapes();
    failed += test_ai_build_body_overflow();
    failed += test_ai_build_body_null();
    failed += test_ai_parse_response_basic();
    failed += test_ai_parse_response_empty_choices();
    failed += test_ai_parse_response_malformed();
    failed += test_ai_extract_command_found();
    failed += test_ai_extract_command_none();
    failed += test_ai_extract_command_no_end();
    failed += test_ai_extract_command_empty();
    failed += test_ai_extract_command_null();
    failed += test_ai_provider_url_deepseek();
    failed += test_ai_provider_url_openai();
    failed += test_ai_provider_url_unknown();
    failed += test_ai_provider_model_deepseek();
    failed += test_ai_system_prompt_with_terminal();
    failed += test_ai_system_prompt_no_terminal();

    printf("\n--- Term Extract ---\n");
    failed += test_extract_empty_term();
    failed += test_extract_single_line();
    failed += test_extract_multi_line();
    failed += test_extract_trims_trailing_spaces();
    failed += test_extract_utf8_codepoint();
    failed += test_extract_buf_too_small();
    failed += test_extract_null_safety();
    failed += test_extract_last_n_basic();
    failed += test_extract_last_n_with_scrollback();
    failed += test_extract_last_n_exceeds_total();
    failed += test_extract_last_n_zero();

    printf("\n--- Color ---\n");
    failed += test_color256_palette_ansi();
    failed += test_color256_palette_cube();
    failed += test_color256_palette_gray();
    failed += test_color_256_fg();
    failed += test_color_256_bg();
    failed += test_color_rgb_fg();
    failed += test_color_rgb_bg();
    failed += test_color_sgr_reset();
    failed += test_color_mixed_sgr();
    failed += test_color_256_interleaved();
    failed += test_color_256_missing_param();
    failed += test_color_rgb_short_param();
    failed += test_color_256_oob();
    failed += test_color_rgb_black();
    failed += test_color_rgb_white();
    failed += test_color_sgr_default_fg();
    failed += test_color_sgr_default_bg();

    printf("\n--- Connect Animation ---\n");
    failed += test_anim_dots_zero_elapsed();
    failed += test_anim_dots_just_under_interval();
    failed += test_anim_dots_exactly_one_interval();
    failed += test_anim_dots_multiple_intervals();
    failed += test_anim_dots_clamped_to_max();
    failed += test_anim_dots_large_interval();
    failed += test_anim_dots_zero_interval();
    failed += test_anim_dots_max_zero();
    failed += test_anim_text_zero_dots();
    failed += test_anim_text_one_dot();
    failed += test_anim_text_three_dots();
    failed += test_anim_text_buf_just_fits_prefix();
    failed += test_anim_text_dots_truncated_by_buf();
    failed += test_anim_text_null_buf();
    failed += test_anim_text_zero_size();
    failed += test_anim_text_small_buf_truncates_prefix();
    failed += test_anim_text_single_byte_buf();

    printf("\n--- Zoom ---\n");
    failed += test_zoom_exact_fit();
    failed += test_zoom_odd_cell_sizes();
    failed += test_zoom_unit_cells();
    failed += test_zoom_large_window();
    failed += test_zoom_hgutter();
    failed += test_zoom_vgutter();
    failed += test_zoom_both_gutters();
    failed += test_zoom_zero_char_w();
    failed += test_zoom_zero_char_h();
    failed += test_zoom_zero_client_w();
    failed += test_zoom_zero_term_h();

    printf("\n--- Snap ---\n");
    failed += test_snap_calc_exact_fit();
    failed += test_snap_calc_right_gutter();
    failed += test_snap_calc_bottom_gutter();
    failed += test_snap_calc_both_gutters();
    failed += test_snap_calc_odd_cell_size();
    failed += test_snap_calc_zoom_in();
    failed += test_snap_calc_zoom_out();
    failed += test_snap_calc_null_outputs();
    failed += test_snap_calc_min_cols_clamp();
    failed += test_snap_calc_min_rows_clamp();
    failed += test_snap_calc_negative_term_h();
    failed += test_snap_calc_zero_client_w();
    failed += test_snap_adjust_bottom_right();
    failed += test_snap_adjust_top_left();
    failed += test_snap_adjust_left();
    failed += test_snap_adjust_right();
    failed += test_snap_adjust_top();
    failed += test_snap_adjust_bottom();
    failed += test_snap_adjust_top_right();
    failed += test_snap_adjust_bottom_left();
    failed += test_snap_adjust_no_change_when_exact();
    failed += test_snap_roundtrip_bottomright();
    failed += test_snap_roundtrip_topleft();

    printf("\n--- Scrollbar ---\n");
    failed += test_sb_npos_at_bottom();
    failed += test_sb_npos_scrolled_back();
    failed += test_sb_npos_at_top();
    failed += test_sb_npos_no_content();
    failed += test_sb_npos_clamp_negative();
    failed += test_sb_max_off_normal();
    failed += test_sb_max_off_capped();
    failed += test_sb_max_off_no_scrollback();
    failed += test_sb_max_off_zero_setting();
    failed += test_sb_offset_from_npos_bottom();
    failed += test_sb_offset_from_npos_top();
    failed += test_sb_offset_from_npos_mid();
    failed += test_sb_roundtrip();
    failed += test_sb_roundtrip_extremes();
    failed += test_sb_npos_exceeds_word();

    printf("\n--- Log Format ---\n");
    failed += test_logfmt_basic_name();
    failed += test_logfmt_spaces_to_underscores();
    failed += test_logfmt_empty_dir();
    failed += test_logfmt_null_dir();
    failed += test_logfmt_timestamp_format();
    failed += test_logfmt_long_name();
    failed += test_logfmt_null_name();
    failed += test_logfmt_empty_name();
    failed += test_logfmt_null_buf();
    failed += test_logfmt_zero_bufsize();
    failed += test_logfmt_special_chars();

    printf("\nTests Run: %d, Failed: %d\n", _tf_run, _tf_failed);
    return failed > 0;
}