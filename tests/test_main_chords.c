#include "test_framework.h"
#include "main_chords.h"
#include "app_font.h"
#include "ns_type.h"
#include "shortcuts.h"
#include <string.h>

/* classify(vk, shift, ctrl, alt, ai_on) */

int test_main_chords_tab_next_and_prev(void)
{
    TEST_BEGIN();
    ASSERT_EQ(main_chord_classify(MCHORD_VK_TAB, 0, 1, 0, 0), MCHORD_TAB_NEXT);
    ASSERT_EQ(main_chord_classify(MCHORD_VK_TAB, 1, 1, 0, 0), MCHORD_TAB_PREV);
    /* Plain Tab and Shift+Tab (back-tab) belong to the shell. */
    ASSERT_EQ(main_chord_classify(MCHORD_VK_TAB, 0, 0, 0, 0), MCHORD_NONE);
    ASSERT_EQ(main_chord_classify(MCHORD_VK_TAB, 1, 0, 0, 0), MCHORD_NONE);
    /* Alt+Tab and Ctrl+Alt+Tab are not ours. */
    ASSERT_EQ(main_chord_classify(MCHORD_VK_TAB, 0, 0, 1, 0), MCHORD_NONE);
    ASSERT_EQ(main_chord_classify(MCHORD_VK_TAB, 0, 1, 1, 0), MCHORD_NONE);
    TEST_END();
}

int test_main_chords_zoom_reset(void)
{
    TEST_BEGIN();
    ASSERT_EQ(main_chord_classify('0', 0, 1, 0, 0), MCHORD_ZOOM_RESET);
    ASSERT_EQ(main_chord_classify('0', 0, 0, 0, 0), MCHORD_NONE);   /* a plain 0 */
    ASSERT_EQ(main_chord_classify('0', 1, 1, 0, 0), MCHORD_NONE);
    ASSERT_EQ(main_chord_classify('0', 0, 1, 1, 0), MCHORD_NONE);   /* AltGr+0 */
    ASSERT_EQ(main_chord_classify('1', 0, 1, 0, 0), MCHORD_NONE);
    TEST_END();
}

int test_main_chords_existing_chords(void)
{
    TEST_BEGIN();
    ASSERT_EQ(main_chord_classify(MCHORD_VK_F11, 0, 0, 0, 0), MCHORD_FULLSCREEN);
    ASSERT_EQ(main_chord_classify(MCHORD_VK_F11, 1, 0, 0, 0), MCHORD_NONE);
    ASSERT_EQ(main_chord_classify(MCHORD_VK_F11, 0, 1, 0, 0), MCHORD_NONE);
    ASSERT_EQ(main_chord_classify('T', 1, 1, 0, 0), MCHORD_NEW_TAB);
    ASSERT_EQ(main_chord_classify('W', 1, 1, 0, 0), MCHORD_CLOSE_TAB);
    ASSERT_EQ(main_chord_classify('M', 1, 1, 0, 0), MCHORD_MARK_ISSUE);
    ASSERT_EQ(main_chord_classify('T', 0, 1, 0, 0), MCHORD_NONE);   /* Ctrl+T: the shell's */
    ASSERT_EQ(main_chord_classify('W', 0, 1, 0, 0), MCHORD_NONE);
    ASSERT_EQ(main_chord_classify('T', 1, 1, 1, 0), MCHORD_NONE);
    TEST_END();
}

int test_main_chords_ai_panel_needs_the_assistant_on(void)
{
    TEST_BEGIN();
    ASSERT_EQ(main_chord_classify(MCHORD_VK_SPACE, 1, 1, 0, 1), MCHORD_AI_PANEL);
    ASSERT_EQ(main_chord_classify(MCHORD_VK_SPACE, 1, 1, 0, 0), MCHORD_NONE);
    ASSERT_EQ(main_chord_classify(MCHORD_VK_SPACE, 0, 1, 0, 1), MCHORD_NONE);  /* Ctrl+Space: NUL */
    TEST_END();
}

int test_main_chords_forwarded_from_panel(void)
{
    TEST_BEGIN();
    ASSERT_EQ(main_chord_forwarded_from_panel(MCHORD_NONE), 0);
    ASSERT_EQ(main_chord_forwarded_from_panel(MCHORD_ZOOM_RESET), 0);
    ASSERT_EQ(main_chord_forwarded_from_panel(MCHORD_FULLSCREEN), 1);
    ASSERT_EQ(main_chord_forwarded_from_panel(MCHORD_NEW_TAB), 1);
    ASSERT_EQ(main_chord_forwarded_from_panel(MCHORD_CLOSE_TAB), 1);
    ASSERT_EQ(main_chord_forwarded_from_panel(MCHORD_AI_PANEL), 1);
    ASSERT_EQ(main_chord_forwarded_from_panel(MCHORD_MARK_ISSUE), 1);
    ASSERT_EQ(main_chord_forwarded_from_panel(MCHORD_TAB_NEXT), 1);
    ASSERT_EQ(main_chord_forwarded_from_panel(MCHORD_TAB_PREV), 1);
    TEST_END();
}

/* Typing in the AI input box is never a chord: letters, digits, Enter, Tab,
 * Space with at most Shift. */
int test_main_chords_typing_is_never_a_chord(void)
{
    TEST_BEGIN();
    for (unsigned vk = 0x08; vk < 0x100; vk++) {
        for (int shift = 0; shift <= 1; shift++) {
            MainChord c = main_chord_classify(vk, shift, 0, 0, 1);
            if (vk == MCHORD_VK_F11 && !shift) continue;   /* F11 alone is a chord */
            ASSERT_EQ(c, MCHORD_NONE);
        }
    }
    TEST_END();
}

int test_main_chords_tab_target_wraps_forward(void)
{
    TEST_BEGIN();
    ASSERT_EQ(tab_chord_target(3, 0, 1), 1);
    ASSERT_EQ(tab_chord_target(3, 1, 1), 2);
    ASSERT_EQ(tab_chord_target(3, 2, 1), 0);
    TEST_END();
}

int test_main_chords_tab_target_wraps_backward(void)
{
    TEST_BEGIN();
    ASSERT_EQ(tab_chord_target(3, 2, -1), 1);
    ASSERT_EQ(tab_chord_target(3, 1, -1), 0);
    ASSERT_EQ(tab_chord_target(3, 0, -1), 2);
    ASSERT_EQ(tab_chord_target(2, 0, 1), 1);
    ASSERT_EQ(tab_chord_target(2, 0, -1), 1);
    TEST_END();
}

int test_main_chords_tab_target_none_to_move_to(void)
{
    TEST_BEGIN();
    ASSERT_EQ(tab_chord_target(1, 0, 1), -1);    /* one tab: nothing happens */
    ASSERT_EQ(tab_chord_target(1, 0, -1), -1);
    ASSERT_EQ(tab_chord_target(0, -1, 1), -1);   /* no tabs */
    ASSERT_EQ(tab_chord_target(0, 0, -1), -1);
    ASSERT_EQ(tab_chord_target(-3, 0, 1), -1);
    ASSERT_EQ(tab_chord_target(3, 0, 0), -1);    /* not a step */
    ASSERT_EQ(tab_chord_target(3, 0, 2), -1);
    TEST_END();
}

int test_main_chords_tab_target_bad_current(void)
{
    TEST_BEGIN();
    ASSERT_EQ(tab_chord_target(3, -1, 1), 0);
    ASSERT_EQ(tab_chord_target(3, -1, -1), 2);
    ASSERT_EQ(tab_chord_target(3, 7, 1), 0);
    ASSERT_EQ(tab_chord_target(3, 7, -1), 2);
    TEST_END();
}

int test_main_chords_zoom_reset_sizes(void)
{
    TEST_BEGIN();
    ASSERT_EQ(zoom_reset_size(0), APP_FONT_DEFAULT_SIZE);
    ASSERT_EQ(zoom_reset_size(1), ns_type_font(FONT_BODY)->size_pt);
    /* Both are steps of the zoom table, so Ctrl+= and Ctrl+- land on them. */
    for (int p = 0; p <= 1; p++) {
        ASSERT_EQ(app_font_snap_size(zoom_reset_size(p)), zoom_reset_size(p));
    }
    TEST_END();
}

/* Each chord the code handles is in the shortcut table, so the sheet and
 * the README list it. */
int test_main_chords_are_in_the_shortcut_table(void)
{
    TEST_BEGIN();
    static const char *const want[] = {
        "Ctrl+Tab", "Ctrl+Shift+Tab", "Ctrl+0",
    };
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (size_t w = 0; w < sizeof(want) / sizeof(want[0]); w++) {
        int found = 0;
        for (size_t i = 0; i < n; i++)
            if (strstr(t[i].chord, want[w]) != NULL) found = 1;
        if (!found) printf("  missing from the table: %s\n", want[w]);
        ASSERT_TRUE(found);
    }
    TEST_END();
}

/* The editing keys of the AI input box and the list (Ctrl+A/C/V/X/Z, Ctrl and
 * Ctrl+Shift with any letter or digit but the chord ones, the zoom keys) are
 * never forwarded to the main window. */
int test_main_chords_panel_editing_keys_stay_in_the_panel(void)
{
    TEST_BEGIN();
    for (unsigned vk = 'A'; vk <= 'Z'; vk++) {
        for (int shift = 0; shift <= 1; shift++) {
            int chord_letter = shift && (vk == 'T' || vk == 'W' || vk == 'M');
            MainChord c = main_chord_classify(vk, shift, 1, 0, 1);
            ASSERT_EQ(main_chord_forwarded_from_panel(c), chord_letter ? 1 : 0);
        }
    }
    for (unsigned vk = '1'; vk <= '9'; vk++)
        ASSERT_EQ(main_chord_classify(vk, 0, 1, 0, 1), MCHORD_NONE);
    ASSERT_EQ(main_chord_classify(0xBB /* VK_OEM_PLUS */, 0, 1, 0, 1), MCHORD_NONE);
    ASSERT_EQ(main_chord_classify(0xBD /* VK_OEM_MINUS */, 0, 1, 0, 1), MCHORD_NONE);
    /* Enter, Backspace, arrows: never chords, with Ctrl or without. */
    static const unsigned keys[] = { 0x0D, 0x08, 0x25, 0x26, 0x27, 0x28, 0x2E, 0x1B };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        for (int ctrl = 0; ctrl <= 1; ctrl++)
            ASSERT_EQ(main_chord_classify(keys[i], 0, ctrl, 0, 1), MCHORD_NONE);
    TEST_END();
}
