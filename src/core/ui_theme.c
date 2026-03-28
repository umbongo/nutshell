#include "ui_theme.h"
#include <string.h>

static const ThemeColors k_themes[NUM_UI_THEMES] = {
    {
        "Onyx Synapse",
        0x121212, /* bg_primary  */
        0x1E1E1E, /* bg_secondary */
        0x007AFF, /* accent       */
        0xE0E0E0, /* text_main    */
        0x888888, /* text_dim     */
        0x2A2A2A, /* border       */
        0xE0E0E0, /* terminal_fg  */
        0x121212, /* terminal_bg  */
        .chat = {
            0x007AFF, /* user_bubble — accent blue */
            0xFFFFFF, /* user_text — white on blue */
            0x007AFF, /* ai_accent — blue */
            0x1A1A2E, /* cmd_bg — darker than bg */
            0x2A2A3E, /* cmd_border */
            0xC0C0C0, /* cmd_text — monospace light */
            0x007AFF, /* thinking_border — accent */
            0x888888, /* thinking_text — dim */
            0x666666, /* status_text — dimmer */
            0x34C759, /* indicator_green */
            0xFFCC00, /* indicator_yellow */
            0xFF3B30, /* indicator_red */
            0x6BAAFF, /* send_btn — soft periwinkle */
            0xFF6B6B, /* stop_btn — soft coral red */
        },
    },
    {
        "Onyx Light",
        0xF5F5F7, /* bg_primary  */
        0xFFFFFF, /* bg_secondary */
        0x007AFF, /* accent       */
        0x1D1D1F, /* text_main    */
        0x86868B, /* text_dim     */
        0xDCDCE0, /* border       */
        0x1D1D1F, /* terminal_fg  */
        0xF5F5F7, /* terminal_bg  */
        .chat = {
            0x007AFF, 0xFFFFFF, 0x007AFF,
            0xEEEEF2, 0xDCDCE0, 0x333333,
            0x007AFF, 0x86868B, 0xAAAAAA,
            0x34C759, 0xFFCC00, 0xFF3B30,
            0x94C4FF, /* send_btn — light sky blue */
            0xF4A0A0, /* stop_btn — rose pink */
        },
    },
    {
        "Sage & Sand",
        0x2B2D24, /* bg_primary  */
        0x353730, /* bg_secondary */
        0xA3B18A, /* accent       */
        0xEAE7DC, /* text_main    */
        0xA09E93, /* text_dim     */
        0x3F4138, /* border       */
        0xEAE7DC, /* terminal_fg  */
        0x2B2D24, /* terminal_bg  */
        .chat = {
            0xA3B18A, 0x1A1A14, 0xA3B18A,
            0x232520, 0x3F4138, 0xD4D1C4,
            0xA3B18A, 0xA09E93, 0x7A7868,
            0x8FBC6A, 0xD4AA4A, 0xC75A3A,
            0x8A9EB1, /* send_btn — dusty steel blue */
            0xC4756B, /* stop_btn — earthy terracotta */
        },
    },
    {
        "Moss & Mist",
        0xF1F3F0, /* bg_primary  */
        0xFFFFFF, /* bg_secondary */
        0x84A98C, /* accent       */
        0x354F52, /* text_main    */
        0x6B8A8D, /* text_dim     */
        0xD5D8D3, /* border       */
        0x354F52, /* terminal_fg  */
        0xF1F3F0, /* terminal_bg  */
        .chat = {
            0x84A98C, 0xFFFFFF, 0x84A98C,
            0xE8EBE6, 0xD5D8D3, 0x354F52,
            0x84A98C, 0x6B8A8D, 0x9AAFB1,
            0x52B788, 0xE9C46A, 0xE76F51,
            0x8CAAB8, /* send_btn — misty blue */
            0xD4908A, /* stop_btn — dusty rose */
        },
    },
};

const ThemeColors *ui_theme_get(int index)
{
    if (index < 0 || index >= NUM_UI_THEMES)
        return &k_themes[0];
    return &k_themes[index];
}

int ui_theme_find(const char *name)
{
    if (!name) return 0;
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        if (strcmp(k_themes[i].name, name) == 0)
            return i;
    }
    return 0;
}

const char *ui_theme_name(int index)
{
    return ui_theme_get(index)->name;
}
