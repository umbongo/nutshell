#include "test_framework.h"
#include "chat_cursor.h"

/* Which cursor the AI chat list shows for a hit-test result (spec
 * 2026-10-18-ai-panel-text-selection-design.md section 8: an I-beam over
 * text). Every hit kind the list view reports, with and without a drag. */
int test_chat_cursor_every_hit_kind(void)
{
    TEST_BEGIN();
    /* No drag: text is an I-beam, a widget a hand, anything else the arrow. */
    ASSERT_EQ((int)chat_cursor_for_hit(CHAT_HIT_NONE, 0), (int)CHAT_CURSOR_ARROW);
    ASSERT_EQ((int)chat_cursor_for_hit(CHAT_HIT_TEXT, 0), (int)CHAT_CURSOR_IBEAM);
    ASSERT_EQ((int)chat_cursor_for_hit(CHAT_HIT_ACTION, 0), (int)CHAT_CURSOR_HAND);
    /* A drag in progress keeps the I-beam wherever the pointer strays: the
     * selection is still being made. */
    ASSERT_EQ((int)chat_cursor_for_hit(CHAT_HIT_NONE, 1), (int)CHAT_CURSOR_IBEAM);
    ASSERT_EQ((int)chat_cursor_for_hit(CHAT_HIT_TEXT, 1), (int)CHAT_CURSOR_IBEAM);
    ASSERT_EQ((int)chat_cursor_for_hit(CHAT_HIT_ACTION, 1), (int)CHAT_CURSOR_IBEAM);
    TEST_END();
}

/* A value the list view never reports is the plain cursor, and the kinds are
 * exactly the three above. */
int test_chat_cursor_unknown_kind_is_arrow(void)
{
    TEST_BEGIN();
    ASSERT_EQ((int)chat_cursor_for_hit((ChatHitKind)99, 0), (int)CHAT_CURSOR_ARROW);
    ASSERT_EQ((int)chat_cursor_for_hit((ChatHitKind)-1, 0), (int)CHAT_CURSOR_ARROW);
    ASSERT_EQ((int)CHAT_HIT_KIND_COUNT, 3);
    TEST_END();
}
