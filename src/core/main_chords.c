#include "main_chords.h"
#include "app_font.h"
#include "ns_type.h"

MainChord main_chord_classify(unsigned vk, int shift, int ctrl, int alt, int ai_on)
{
    /* F11 alone; with any modifier it goes to the shell. */
    if (vk == MCHORD_VK_F11)
        return (!shift && !ctrl && !alt) ? MCHORD_FULLSCREEN : MCHORD_NONE;

    /* Every other chord is Ctrl with Alt up (AltGr reports both). */
    if (!ctrl || alt) return MCHORD_NONE;

    if (shift) {
        switch (vk) {
        case 'T':             return MCHORD_NEW_TAB;
        case 'W':             return MCHORD_CLOSE_TAB;
        case 'M':             return MCHORD_MARK_ISSUE;
        case MCHORD_VK_SPACE: return ai_on ? MCHORD_AI_PANEL : MCHORD_NONE;
        case MCHORD_VK_TAB:   return MCHORD_TAB_PREV;
        default:              return MCHORD_NONE;
        }
    }
    if (vk == MCHORD_VK_TAB) return MCHORD_TAB_NEXT;
    if (vk == '0')           return MCHORD_ZOOM_RESET;
    return MCHORD_NONE;
}

int main_chord_forwarded_from_panel(MainChord c)
{
    return c != MCHORD_NONE && c != MCHORD_ZOOM_RESET;
}

int tab_chord_target(int count, int current, int delta)
{
    if (count < 2 || (delta != 1 && delta != -1)) return -1;
    if (current < 0 || current >= count) return delta > 0 ? 0 : count - 1;
    return (current + delta + count) % count;
}

int zoom_reset_size(int ai_panel)
{
    return ai_panel ? ns_type_font(FONT_BODY)->size_pt : APP_FONT_DEFAULT_SIZE;
}
