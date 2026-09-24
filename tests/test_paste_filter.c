#include "test_framework.h"
#include "paste_filter.h"
#include <stdlib.h>
#include <string.h>

/* ---- paste_filter_controls ---------------------------------------------- */

int test_paste_filter_no_controls_unchanged(void)
{
    TEST_BEGIN();
    char buf[16] = "abc def";
    size_t removed = 999;
    size_t n = paste_filter_controls(buf, strlen(buf), buf, &removed);
    ASSERT_EQ((int)n, 7);
    buf[n] = '\0';
    ASSERT_STR_EQ(buf, "abc def");
    ASSERT_EQ((int)removed, 0);
    TEST_END();
}

int test_paste_filter_removes_esc(void)
{
    TEST_BEGIN();
    /* A pasted "bracketed paste close" sequence -- exactly what bracketed
     * paste mode exists to prevent a hostile clipboard from injecting. */
    const char in[] = "abc\x1b[201~def";
    char out[32];
    size_t removed = 0;
    size_t n = paste_filter_controls(in, sizeof(in) - 1, out, &removed);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "abc[201~def");
    ASSERT_EQ((int)removed, 1);
    TEST_END();
}

int test_paste_filter_keeps_tab_lf_cr(void)
{
    TEST_BEGIN();
    const char in[] = "a\tb\nc\rd";
    char out[16];
    size_t removed = 999;
    size_t n = paste_filter_controls(in, sizeof(in) - 1, out, &removed);
    out[n] = '\0';
    ASSERT_EQ((int)n, (int)(sizeof(in) - 1));
    ASSERT_STR_EQ(out, in);
    ASSERT_EQ((int)removed, 0);
    TEST_END();
}

int test_paste_filter_removes_other_c0(void)
{
    TEST_BEGIN();
    /* SOH, BEL, VT, FF -- none of the three preserved controls */
    const char in[] = "a\x01\x07\x0b\x0c" "b";
    char out[16];
    size_t removed = 0;
    size_t n = paste_filter_controls(in, sizeof(in) - 1, out, &removed);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "ab");
    ASSERT_EQ((int)removed, 4);
    TEST_END();
}

int test_paste_filter_removes_del(void)
{
    TEST_BEGIN();
    const char in[] = "a\x7f" "b";
    char out[8];
    size_t removed = 0;
    size_t n = paste_filter_controls(in, sizeof(in) - 1, out, &removed);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "ab");
    ASSERT_EQ((int)removed, 1);
    TEST_END();
}

int test_paste_filter_removes_c1_control(void)
{
    TEST_BEGIN();
    /* U+0080 and U+009B (CSI), UTF-8 encoded as 0xC2 0x80 / 0xC2 0x9B */
    const char in[] = "a\xC2\x80" "b\xC2\x9B" "c";
    char out[16];
    size_t removed = 0;
    size_t n = paste_filter_controls(in, sizeof(in) - 1, out, &removed);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "abc");
    ASSERT_EQ((int)removed, 2);
    TEST_END();
}

int test_paste_filter_keeps_c2_a0_and_above(void)
{
    TEST_BEGIN();
    /* 0xC2 0xA0 = NBSP (U+00A0), immediately past the C1 range -- must
     * survive untouched, same for the rest of the 0xC2 0xA0-0xBF span. */
    const char in[] = "a\xC2\xA0" "b";
    char out[16];
    size_t removed = 999;
    size_t n = paste_filter_controls(in, sizeof(in) - 1, out, &removed);
    out[n] = '\0';
    ASSERT_STR_EQ(out, in);
    ASSERT_EQ((int)removed, 0);
    TEST_END();
}

int test_paste_filter_in_place_aliasing(void)
{
    TEST_BEGIN();
    /* out == in is the common case (window.c filters the clipboard copy
     * in place); every stripped byte must still compact correctly. */
    char buf[32] = "\x1b" "a" "\x1b" "b" "\x1b" "c";
    size_t removed = 0;
    size_t n = paste_filter_controls(buf, strlen(buf), buf, &removed);
    buf[n] = '\0';
    ASSERT_STR_EQ(buf, "abc");
    ASSERT_EQ((int)removed, 3);
    TEST_END();
}

int test_paste_filter_all_stripped_gives_empty(void)
{
    TEST_BEGIN();
    char buf[8] = "\x1b\x01\x7f";
    size_t removed = 0;
    size_t n = paste_filter_controls(buf, strlen(buf), buf, &removed);
    ASSERT_EQ((int)n, 0);
    ASSERT_EQ((int)removed, 3);
    TEST_END();
}

int test_paste_filter_empty_input(void)
{
    TEST_BEGIN();
    char out[4];
    size_t removed = 999;
    size_t n = paste_filter_controls("", 0, out, &removed);
    ASSERT_EQ((int)n, 0);
    ASSERT_EQ((int)removed, 0);
    TEST_END();
}

int test_paste_filter_null_input_safe(void)
{
    TEST_BEGIN();
    char out[4];
    size_t removed = 999;
    size_t n = paste_filter_controls(NULL, 5, out, &removed);
    ASSERT_EQ((int)n, 0);
    ASSERT_EQ((int)removed, 0);
    TEST_END();
}

int test_paste_filter_null_removed_out_safe(void)
{
    TEST_BEGIN();
    char buf[8] = "a\x1b" "b";
    size_t n = paste_filter_controls(buf, strlen(buf), buf, NULL);
    buf[n] = '\0';
    ASSERT_STR_EQ(buf, "ab");
    TEST_END();
}

/* Truncated C1 lead byte at the very end of the buffer: 0xC2 with nothing
 * after it is not a complete C1 sequence, so it must pass through as a
 * literal (invalid-UTF-8, but not this function's job to fix that). */
int test_paste_filter_trailing_lone_c2_passthrough(void)
{
    TEST_BEGIN();
    char buf[8] = "a\xC2";
    size_t removed = 999;
    size_t n = paste_filter_controls(buf, strlen(buf), buf, &removed);
    ASSERT_EQ((int)n, 2);
    ASSERT_TRUE((unsigned char)buf[1] == 0xC2);
    ASSERT_EQ((int)removed, 0);
    TEST_END();
}

/* ---- paste_visualize_controls -------------------------------------------- */

int test_paste_visualize_no_controls_unchanged(void)
{
    TEST_BEGIN();
    size_t removed = 999;
    char *vis = paste_visualize_controls("abc", 3, &removed);
    ASSERT_NOT_NULL(vis);
    ASSERT_STR_EQ(vis, "abc");
    ASSERT_EQ((int)removed, 0);
    free(vis);
    TEST_END();
}

int test_paste_visualize_esc_becomes_symbol(void)
{
    TEST_BEGIN();
    /* U+241B SYMBOL FOR ESCAPE = E2 90 9B in UTF-8 */
    size_t removed = 0;
    char *vis = paste_visualize_controls("a\x1b" "b", 3, &removed);
    ASSERT_NOT_NULL(vis);
    ASSERT_EQ((int)removed, 1);
    ASSERT_EQ(strlen(vis), (size_t)5); /* 'a' + 3-byte symbol + 'b' */
    ASSERT_TRUE((unsigned char)vis[1] == 0xE2);
    ASSERT_TRUE((unsigned char)vis[2] == 0x90);
    ASSERT_TRUE((unsigned char)vis[3] == 0x9B);
    ASSERT_EQ(vis[4], 'b');
    free(vis);
    TEST_END();
}

int test_paste_visualize_keeps_tab_lf_cr(void)
{
    TEST_BEGIN();
    const char in[] = "a\tb\nc\rd";
    size_t removed = 999;
    char *vis = paste_visualize_controls(in, sizeof(in) - 1, &removed);
    ASSERT_NOT_NULL(vis);
    ASSERT_STR_EQ(vis, in);
    ASSERT_EQ((int)removed, 0);
    free(vis);
    TEST_END();
}

int test_paste_visualize_matches_filter_count(void)
{
    TEST_BEGIN();
    const char in[] = "ab\x1b[201~cd\x07" "ef\xC2\x9B";
    size_t filter_removed = 0;
    char buf[64];
    memcpy(buf, in, sizeof(in));
    paste_filter_controls(buf, sizeof(in) - 1, buf, &filter_removed);

    size_t vis_removed = 0;
    char *vis = paste_visualize_controls(in, sizeof(in) - 1, &vis_removed);
    ASSERT_NOT_NULL(vis);
    ASSERT_EQ((int)vis_removed, (int)filter_removed);
    free(vis);
    TEST_END();
}

int test_paste_visualize_null_input_safe(void)
{
    TEST_BEGIN();
    size_t removed = 999;
    char *vis = paste_visualize_controls(NULL, 0, &removed);
    ASSERT_NULL(vis);
    ASSERT_EQ((int)removed, 0);
    TEST_END();
}

int test_paste_visualize_long_run_grows_buffer(void)
{
    TEST_BEGIN();
    /* Every byte a control: output (3x) is much longer than input --
     * exercises the realloc growth path. */
    char in[64];
    for (int i = 0; i < 64; i++) in[i] = '\x01';
    size_t removed = 0;
    char *vis = paste_visualize_controls(in, sizeof(in), &removed);
    ASSERT_NOT_NULL(vis);
    ASSERT_EQ((int)removed, 64);
    ASSERT_EQ(strlen(vis), (size_t)(64 * 3));
    free(vis);
    TEST_END();
}

/* Boundary case for the one-byte heap overflow fixed in
 * append_control_picture()/ensure_cap(): an input that is ENTIRELY controls
 * so the very last byte written by the loop lands exactly at the buffer's
 * capacity, with nothing left over for the NUL terminator, forcing the
 * growth path to run again right at the end. There is no allocation-failure
 * injection hook in this codebase to reproduce the overflow directly (the
 * bug was `buf[len] = '\0'` after a downsize realloc that could fail
 * without anyone checking the resulting capacity) -- this instead pins the
 * post-condition the fix guarantees unconditionally: the returned buffer is
 * always validly NUL-terminated at exactly `strlen()`, for a range of
 * lengths that walk right through where cap == len can occur. */
int test_paste_visualize_exact_capacity_boundary_terminates_safely(void)
{
    TEST_BEGIN();
    for (int n = 1; n <= 40; n++) {
        char *in = (char *)malloc((size_t)n);
        ASSERT_NOT_NULL(in);
        for (int i = 0; i < n; i++) in[i] = '\x01';   /* SOH: always stripped */

        size_t removed = 999;
        char *vis = paste_visualize_controls(in, (size_t)n, &removed);
        free(in);

        ASSERT_NOT_NULL(vis);
        ASSERT_EQ((int)removed, n);
        ASSERT_EQ(strlen(vis), (size_t)(n * 3));   /* 1 SOH -> 3-byte picture */
        free(vis);
    }
    TEST_END();
}

/* ---- Bidi override/isolate stripping (Trojan Source hardening) ---------- */

int test_paste_filter_removes_bidi_override(void)
{
    TEST_BEGIN();
    /* U+202E RIGHT-TO-LEFT OVERRIDE, UTF-8 E2 80 AE. */
    const char in[] = "a\xE2\x80\xAE" "b";
    char out[16];
    size_t removed = 0;
    size_t n = paste_filter_controls(in, sizeof(in) - 1, out, &removed);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "ab");
    ASSERT_EQ((int)removed, 1);
    TEST_END();
}

int test_paste_filter_removes_bidi_isolate(void)
{
    TEST_BEGIN();
    /* U+2066 LEFT-TO-RIGHT ISOLATE, UTF-8 E2 81 A6. */
    const char in[] = "a\xE2\x81\xA6" "b";
    char out[16];
    size_t removed = 0;
    size_t n = paste_filter_controls(in, sizeof(in) - 1, out, &removed);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "ab");
    ASSERT_EQ((int)removed, 1);
    TEST_END();
}

int test_paste_filter_keeps_bytes_just_outside_bidi_ranges(void)
{
    TEST_BEGIN();
    /* E2 80 A9 (U+2029 PARAGRAPH SEPARATOR, just below the LRE..RLO range)
     * and E2 81 AA (U+206A INHIBIT SYMMETRIC SWAPPING, just above the
     * LRI..PDI range) must both survive untouched. */
    const char in[] = "\xE2\x80\xA9" "\xE2\x81\xAA";
    char out[16];
    size_t removed = 999;
    size_t n = paste_filter_controls(in, sizeof(in) - 1, out, &removed);
    out[n] = '\0';
    ASSERT_STR_EQ(out, in);
    ASSERT_EQ((int)removed, 0);
    TEST_END();
}

int test_paste_visualize_bidi_override_becomes_tag(void)
{
    TEST_BEGIN();
    size_t removed = 0;
    char *vis = paste_visualize_controls("a\xE2\x80\xAE" "b", 5, &removed);
    ASSERT_NOT_NULL(vis);
    ASSERT_EQ((int)removed, 1);
    ASSERT_STR_EQ(vis, "a[RLO]b");
    free(vis);
    TEST_END();
}

int test_paste_visualize_bidi_isolate_becomes_tag(void)
{
    TEST_BEGIN();
    size_t removed = 0;
    char *vis = paste_visualize_controls("\xE2\x81\xA9" "x", 4, &removed);
    ASSERT_NOT_NULL(vis);
    ASSERT_EQ((int)removed, 1);
    ASSERT_STR_EQ(vis, "[PDI]x");
    free(vis);
    TEST_END();
}

/* ---- text_has_unsafe_command_char ---------------------------------------- */

int test_unsafe_cmd_char_plain_command_is_safe(void)
{
    TEST_BEGIN();
    ASSERT_EQ(text_has_unsafe_command_char("ls -la /tmp"), 0);
    TEST_END();
}

int test_unsafe_cmd_char_rejects_c0(void)
{
    TEST_BEGIN();
    ASSERT_EQ(text_has_unsafe_command_char("echo ok\nrm -rf ~"), 1);
    TEST_END();
}

int test_unsafe_cmd_char_rejects_tab(void)
{
    TEST_BEGIN();
    /* Unlike paste_filter_controls(), a command must be a single line: TAB
     * is rejected here even though it passes through a paste unchanged. */
    ASSERT_EQ(text_has_unsafe_command_char("echo\tok"), 1);
    TEST_END();
}

int test_unsafe_cmd_char_rejects_del(void)
{
    TEST_BEGIN();
    ASSERT_EQ(text_has_unsafe_command_char("echo ok\x7f"), 1);
    TEST_END();
}

int test_unsafe_cmd_char_rejects_c1(void)
{
    TEST_BEGIN();
    ASSERT_EQ(text_has_unsafe_command_char("echo ok\xC2\x9B"), 1);
    TEST_END();
}

int test_unsafe_cmd_char_keeps_c2_a0_and_above(void)
{
    TEST_BEGIN();
    /* NBSP (0xC2 0xA0) is not a C1 control -- must not be rejected. */
    ASSERT_EQ(text_has_unsafe_command_char("echo ok\xC2\xA0" "there"), 0);
    TEST_END();
}

int test_unsafe_cmd_char_rejects_bidi_override(void)
{
    TEST_BEGIN();
    ASSERT_EQ(text_has_unsafe_command_char("echo ok\xE2\x80\xAA"), 1);
    TEST_END();
}

int test_unsafe_cmd_char_rejects_bidi_isolate(void)
{
    TEST_BEGIN();
    ASSERT_EQ(text_has_unsafe_command_char("echo ok\xE2\x81\xA9"), 1);
    TEST_END();
}

int test_unsafe_cmd_char_null_safe(void)
{
    TEST_BEGIN();
    ASSERT_EQ(text_has_unsafe_command_char(NULL), 0);
    TEST_END();
}

int test_unsafe_cmd_char_trailing_lone_lead_bytes_safe(void)
{
    TEST_BEGIN();
    /* A truncated 0xC2 or 0xE2 lead byte at the very end of the string:
     * reading past the terminator must never happen, and neither is a
     * match (not this function's job to validate UTF-8). */
    ASSERT_EQ(text_has_unsafe_command_char("echo ok\xC2"), 0);
    ASSERT_EQ(text_has_unsafe_command_char("echo ok\xE2"), 0);
    ASSERT_EQ(text_has_unsafe_command_char("echo ok\xE2\x80"), 0);
    TEST_END();
}
