#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>

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

/* test_settings.c */
int test_settings_validate_defaults(void);
int test_settings_validate_font_size_low(void);
int test_settings_validate_font_size_high(void);
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

    /* Settings Validation */
    failed += test_settings_validate_defaults();
    failed += test_settings_validate_font_size_low();
    failed += test_settings_validate_font_size_high();
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
    failed += test_term_utf8();

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

    printf("\nTests Run: %d, Failed: %d\n", _tf_run, _tf_failed);
    return failed > 0;
}