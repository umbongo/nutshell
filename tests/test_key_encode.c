/* tests/test_key_encode.c */
#include "test_framework.h"
#include "key_encode.h"
#include <string.h>

/* Compares out[0..len) against an expected byte string given as a normal
 * C string literal (the expected bytes never contain an embedded NUL in
 * this file, so strlen() on the literal is safe and lets callers write
 * "\x1b[A" instead of listing individual bytes). */
static int bytes_eq(const char *out, size_t len, const char *expect)
{
    size_t elen = strlen(expect);
    if (len != elen) return 0;
    return memcmp(out, expect, elen) == 0;
}

/* ---- Arrows --------------------------------------------------------- */

int test_key_arrows_unmodified_normal(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;

    n = key_encode(NSK_UP, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[A"));
    n = key_encode(NSK_DOWN, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[B"));
    n = key_encode(NSK_RIGHT, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[C"));
    n = key_encode(NSK_LEFT, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[D"));
    TEST_END();
}

int test_key_arrows_unmodified_app_mode(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;

    n = key_encode(NSK_UP, 0, 0, NSK_FLAG_APP_CURSOR, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOA"));
    n = key_encode(NSK_DOWN, 0, 0, NSK_FLAG_APP_CURSOR, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOB"));
    n = key_encode(NSK_RIGHT, 0, 0, NSK_FLAG_APP_CURSOR, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOC"));
    n = key_encode(NSK_LEFT, 0, 0, NSK_FLAG_APP_CURSOR, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOD"));
    TEST_END();
}

int test_key_arrows_all_modifiers_normal(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;
    static const char letters[4] = { 'A', 'B', 'C', 'D' };
    static const NsKey keys[4] = { NSK_UP, NSK_DOWN, NSK_RIGHT, NSK_LEFT };
    unsigned int mods;
    int k;

    for (k = 0; k < 4; k++) {
        for (mods = 1; mods <= 7; mods++) {
            char expect[16];
            snprintf(expect, sizeof(expect), "\x1b[1;%u%c", mods + 1, letters[k]);
            n = key_encode(keys[k], 0, mods, 0, out, sizeof(out));
            ASSERT_TRUE(bytes_eq(out, n, expect));
        }
    }
    TEST_END();
}

int test_key_arrows_all_modifiers_app_mode_same_as_normal(void)
{
    TEST_BEGIN();
    char out_normal[KEY_ENCODE_MAX];
    char out_app[KEY_ENCODE_MAX];
    size_t n1, n2;
    unsigned int mods;

    for (mods = 1; mods <= 7; mods++) {
        n1 = key_encode(NSK_UP, 0, mods, 0, out_normal, sizeof(out_normal));
        n2 = key_encode(NSK_UP, 0, mods, NSK_FLAG_APP_CURSOR, out_app, sizeof(out_app));
        ASSERT_EQ((int)n1, (int)n2);
        ASSERT_TRUE(n1 == n2 && memcmp(out_normal, out_app, n1) == 0);
    }
    TEST_END();
}

/* ---- Home / End ------------------------------------------------------ */

int test_key_home_end_normal_and_app(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;

    n = key_encode(NSK_HOME, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[H"));
    n = key_encode(NSK_END, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[F"));
    n = key_encode(NSK_HOME, 0, 0, NSK_FLAG_APP_CURSOR, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOH"));
    n = key_encode(NSK_END, 0, 0, NSK_FLAG_APP_CURSOR, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOF"));
    TEST_END();
}

int test_key_home_end_modified(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;
    unsigned int mods;

    for (mods = 1; mods <= 7; mods++) {
        char expect_h[16], expect_f[16];
        snprintf(expect_h, sizeof(expect_h), "\x1b[1;%uH", mods + 1);
        snprintf(expect_f, sizeof(expect_f), "\x1b[1;%uF", mods + 1);
        n = key_encode(NSK_HOME, 0, mods, 0, out, sizeof(out));
        ASSERT_TRUE(bytes_eq(out, n, expect_h));
        n = key_encode(NSK_END, 0, mods, NSK_FLAG_APP_CURSOR, out, sizeof(out));
        ASSERT_TRUE(bytes_eq(out, n, expect_f));
    }
    TEST_END();
}

/* ---- Insert/Delete/PgUp/PgDn ------------------------------------------ */

int test_key_ins_del_pgup_pgdn_unmodified(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;

    n = key_encode(NSK_INSERT, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[2~"));
    n = key_encode(NSK_DELETE, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[3~"));
    n = key_encode(NSK_PGUP, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[5~"));
    n = key_encode(NSK_PGDN, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[6~"));
    TEST_END();
}

int test_key_ins_del_pgup_pgdn_all_modifiers(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;
    static const int codes[4] = { 2, 3, 5, 6 };
    static const NsKey keys[4] = { NSK_INSERT, NSK_DELETE, NSK_PGUP, NSK_PGDN };
    unsigned int mods;
    int k;

    for (k = 0; k < 4; k++) {
        for (mods = 1; mods <= 7; mods++) {
            char expect[16];
            snprintf(expect, sizeof(expect), "\x1b[%d;%u~", codes[k], mods + 1);
            n = key_encode(keys[k], 0, mods, 0, out, sizeof(out));
            ASSERT_TRUE(bytes_eq(out, n, expect));
        }
    }
    TEST_END();
}

/* ---- F1-F4 (SS3) ------------------------------------------------------ */

int test_key_f1_f4_unmodified(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;

    n = key_encode(NSK_F1, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOP"));
    n = key_encode(NSK_F2, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOQ"));
    n = key_encode(NSK_F3, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOR"));
    n = key_encode(NSK_F4, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1bOS"));
    TEST_END();
}

int test_key_f1_f4_modified(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;
    static const char letters[4] = { 'P', 'Q', 'R', 'S' };
    static const NsKey keys[4] = { NSK_F1, NSK_F2, NSK_F3, NSK_F4 };
    unsigned int mods;
    int k;

    for (k = 0; k < 4; k++) {
        for (mods = 1; mods <= 7; mods++) {
            char expect[16];
            snprintf(expect, sizeof(expect), "\x1b[1;%u%c", mods + 1, letters[k]);
            n = key_encode(keys[k], 0, mods, 0, out, sizeof(out));
            ASSERT_TRUE(bytes_eq(out, n, expect));
        }
    }
    TEST_END();
}

/* ---- F5-F12 ------------------------------------------------------------ */

int test_key_f5_f12_unmodified(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;
    static const int codes[8] = { 15, 17, 18, 19, 20, 21, 23, 24 };
    static const NsKey keys[8] = {
        NSK_F5, NSK_F6, NSK_F7, NSK_F8, NSK_F9, NSK_F10, NSK_F11, NSK_F12
    };
    int k;

    for (k = 0; k < 8; k++) {
        char expect[16];
        snprintf(expect, sizeof(expect), "\x1b[%d~", codes[k]);
        n = key_encode(keys[k], 0, 0, 0, out, sizeof(out));
        ASSERT_TRUE(bytes_eq(out, n, expect));
    }
    TEST_END();
}

int test_key_f5_f12_all_modifiers(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;
    static const int codes[8] = { 15, 17, 18, 19, 20, 21, 23, 24 };
    static const NsKey keys[8] = {
        NSK_F5, NSK_F6, NSK_F7, NSK_F8, NSK_F9, NSK_F10, NSK_F11, NSK_F12
    };
    unsigned int mods;
    int k;

    for (k = 0; k < 8; k++) {
        for (mods = 1; mods <= 7; mods++) {
            char expect[16];
            snprintf(expect, sizeof(expect), "\x1b[%d;%u~", codes[k], mods + 1);
            n = key_encode(keys[k], 0, mods, 0, out, sizeof(out));
            ASSERT_TRUE(bytes_eq(out, n, expect));
        }
    }
    TEST_END();
}

int test_key_f12_ctrl_shift_literal(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    /* Ctrl+Shift -> mods = SHIFT|CTRL = 5, m = 6 */
    size_t n = key_encode(NSK_F12, 0, NSK_MOD_SHIFT | NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[24;6~"));
    TEST_END();
}

int test_key_f5_ctrl_literal(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_F5, 0, NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[15;5~"));
    TEST_END();
}

/* ---- Tab ---------------------------------------------------------------- */

int test_key_tab_plain(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_TAB, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x09"));
    TEST_END();
}

int test_key_tab_shift(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_TAB, 0, NSK_MOD_SHIFT, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[Z"));
    TEST_END();
}

int test_key_tab_alt(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_TAB, 0, NSK_MOD_ALT, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b\x09"));
    TEST_END();
}

int test_key_tab_alt_shift(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    /* Alt still prefixes ESC even when combined with the Shift form. */
    size_t n = key_encode(NSK_TAB, 0, NSK_MOD_ALT | NSK_MOD_SHIFT, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b\x1b[Z"));
    TEST_END();
}

int test_key_tab_ctrl_ignored(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_TAB, 0, NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x09"));
    TEST_END();
}

/* ---- Backspace ------------------------------------------------------- */

int test_key_backspace_ssh(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_BACKSPACE, 0, 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x08"));
    TEST_END();
}

int test_key_backspace_local(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_BACKSPACE, 0, 0, NSK_FLAG_LOCAL, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x7f"));
    TEST_END();
}

int test_key_backspace_alt_ssh(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_BACKSPACE, 0, NSK_MOD_ALT, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b\x08"));
    TEST_END();
}

int test_key_backspace_alt_local(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_BACKSPACE, 0, NSK_MOD_ALT, NSK_FLAG_LOCAL, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b\x7f"));
    TEST_END();
}

int test_key_backspace_ctrl_shift_ignored(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_BACKSPACE, 0, NSK_MOD_CTRL | NSK_MOD_SHIFT, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x08"));
    n = key_encode(NSK_BACKSPACE, 0, NSK_MOD_CTRL | NSK_MOD_SHIFT, NSK_FLAG_LOCAL, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x7f"));
    TEST_END();
}

/* ---- NSK_CHAR: plain / shift ------------------------------------------- */

int test_key_char_plain_printable(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_CHAR, (unsigned char)'a', 0, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "a"));
    TEST_END();
}

int test_key_char_shift_ignored(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    /* Layout already applied Shift; the encoder must not touch it. */
    size_t n = key_encode(NSK_CHAR, (unsigned char)'A', NSK_MOD_SHIFT, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "A"));
    TEST_END();
}

/* ---- NSK_CHAR: Ctrl mapping --------------------------------------------- */

int test_key_ctrl_space(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_CHAR, (unsigned char)' ', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE(out[0] == 0x00);
    TEST_END();
}

int test_key_ctrl_letters(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;

    n = key_encode(NSK_CHAR, (unsigned char)'a', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == ('a' & 0x1F));

    n = key_encode(NSK_CHAR, (unsigned char)'z', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == ('z' & 0x1F));

    n = key_encode(NSK_CHAR, (unsigned char)'@', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x00);

    n = key_encode(NSK_CHAR, (unsigned char)'[', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x1B);

    n = key_encode(NSK_CHAR, (unsigned char)'\\', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x1C);

    n = key_encode(NSK_CHAR, (unsigned char)']', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x1D);

    n = key_encode(NSK_CHAR, (unsigned char)'^', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x1E);

    n = key_encode(NSK_CHAR, (unsigned char)'_', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x1F);

    /* '?' is 0x3F, below '@' (0x40): the range check excludes it, so it
     * passes through unchanged. */
    n = key_encode(NSK_CHAR, (unsigned char)'?', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == (unsigned char)'?');

    TEST_END();
}

int test_key_ctrl_digit_specials(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;

    n = key_encode(NSK_CHAR, (unsigned char)'2', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x00);

    n = key_encode(NSK_CHAR, (unsigned char)'3', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x1B);

    n = key_encode(NSK_CHAR, (unsigned char)'7', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x1F);

    n = key_encode(NSK_CHAR, (unsigned char)'8', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x7F);

    n = key_encode(NSK_CHAR, (unsigned char)'/', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x1F);

    TEST_END();
}

int test_key_ctrl_already_control_byte_unchanged(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    /* WM_CHAR can deliver an already-mapped control byte for Ctrl+letter;
     * the mapping must not re-map it (0x17 is outside '@'..'~' and not one
     * of the digit/slash specials). */
    size_t n = key_encode(NSK_CHAR, (unsigned char)0x17, NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == 0x17);
    TEST_END();
}

int test_key_ctrl_equals_and_minus_unchanged(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n;

    n = key_encode(NSK_CHAR, (unsigned char)'=', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == (unsigned char)'=');

    n = key_encode(NSK_CHAR, (unsigned char)'-', NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE((unsigned char)out[0] == (unsigned char)'-');

    TEST_END();
}

/* ---- NSK_CHAR: Alt and Ctrl+Alt ----------------------------------------- */

int test_key_alt_lowercase(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_CHAR, (unsigned char)'f', NSK_MOD_ALT, 0, out, sizeof(out));
    /* "\x1b" "f" (not "\x1bf"): a hex escape greedily eats following hex
     * digits, and 'f' is one -- "\x1bf" alone is the single truncated byte
     * 0x1bf, not ESC followed by 'f'. */
    ASSERT_TRUE(bytes_eq(out, n, "\x1b" "f"));
    TEST_END();
}

int test_key_alt_uppercase(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_CHAR, (unsigned char)'F', NSK_MOD_ALT, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b" "F"));
    TEST_END();
}

int test_key_ctrl_alt_f(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_CHAR, (unsigned char)'f', NSK_MOD_ALT | NSK_MOD_CTRL, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 2);
    ASSERT_TRUE((unsigned char)out[0] == 0x1B && (unsigned char)out[1] == 0x06);
    TEST_END();
}

/* ---- Negative / edge cases ---------------------------------------------- */

int test_key_none_returns_zero(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    memset(out, 0x5A, sizeof(out));
    size_t n = key_encode(NSK_NONE, 0, 0, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 0);
    ASSERT_TRUE((unsigned char)out[0] == 0x5A);
    TEST_END();
}

int test_key_count_returns_zero(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    memset(out, 0x5A, sizeof(out));
    size_t n = key_encode(NSK_COUNT, 0, 0, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 0);
    ASSERT_TRUE((unsigned char)out[0] == 0x5A);
    TEST_END();
}

int test_key_out_of_range_returns_zero(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    memset(out, 0x5A, sizeof(out));
    size_t n = key_encode((NsKey)((int)NSK_COUNT + 5), 0, 0, 0, out, sizeof(out));
    ASSERT_EQ((int)n, 0);
    ASSERT_TRUE((unsigned char)out[0] == 0x5A);
    TEST_END();
}

int test_key_null_out_returns_zero(void)
{
    TEST_BEGIN();
    size_t n = key_encode(NSK_UP, 0, 0, 0, NULL, KEY_ENCODE_MAX);
    ASSERT_EQ((int)n, 0);
    TEST_END();
}

int test_key_size_too_small_returns_zero_and_untouched(void)
{
    TEST_BEGIN();
    char out[8];
    size_t n;

    /* F12 with a modifier needs 7 bytes ("\x1b[24;6~"); give it 3. */
    memset(out, 0x5A, sizeof(out));
    n = key_encode(NSK_F12, 0, NSK_MOD_SHIFT | NSK_MOD_CTRL, 0, out, 3);
    ASSERT_EQ((int)n, 0);
    ASSERT_TRUE((unsigned char)out[0] == 0x5A);

    /* A plain char needs 1 byte; size 0 must refuse it. */
    memset(out, 0x5A, sizeof(out));
    n = key_encode(NSK_CHAR, (unsigned char)'a', 0, 0, out, 0);
    ASSERT_EQ((int)n, 0);
    ASSERT_TRUE((unsigned char)out[0] == 0x5A);

    TEST_END();
}

int test_key_exact_fit_size_succeeds(void)
{
    TEST_BEGIN();
    char out[7];
    size_t n = key_encode(NSK_F12, 0, NSK_MOD_SHIFT | NSK_MOD_CTRL, 0, out, 7);
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[24;6~"));
    TEST_END();
}

int test_key_unknown_mod_bits_ignored(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    /* Bits above NSK_MOD_CTRL (0x4) must be ignored: 0xF0 set alongside
     * plain (no real modifier) should behave like mods == 0. */
    size_t n = key_encode(NSK_UP, 0, 0xF0u, 0, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x1b[A"));
    TEST_END();
}

int test_key_unknown_flag_bits_ignored(void)
{
    TEST_BEGIN();
    char out[KEY_ENCODE_MAX];
    size_t n = key_encode(NSK_BACKSPACE, 0, 0, 0xF0u, out, sizeof(out));
    ASSERT_TRUE(bytes_eq(out, n, "\x08"));
    TEST_END();
}
