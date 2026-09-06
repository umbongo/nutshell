#include "ui_theme.h"
#include "theme.h"
#include <string.h>

/* Intent colours are hand-picked per theme (Design-System Foundation,
 * spec section 1): success/warning/danger seed from each theme's chat
 * activity-dot colours, then all five (plus, for Onyx Light, the new
 * accent) are darkened or lightened just enough that the best of
 * { white, bg_primary, text_main } reads at >= 4.5:1 against them (that
 * best choice is what ThemeSurface.label resolves to) — see
 * tests/test_ui_tokens.c for the exact numbers this was tuned against.
 * text_dim in every theme was similarly nudged so it clears 4.5:1 against
 * `raised` (bg_secondary shifted one UI_THEME_STEP, see ui_theme_resolve). */
static const ThemeColors k_themes[NUM_UI_THEMES] = {
    {
        .name         = "Onyx Synapse",
        .bg_primary   = 0x121212,
        .bg_secondary = 0x1E1E1E,
        .accent       = 0x007AFF,
        .text_main    = 0xE0E0E0,
        .text_dim     = 0x919191, /* lightened from 0x888888: text_dim on raised
                                    * (bg_secondary + 1 L* step) was 4.05:1 */
        .border       = 0x2A2A2A,
        .terminal_fg  = 0xE0E0E0,
        .terminal_bg  = 0x121212,
        .success      = 0x21883B, /* darkened from indicator_green 0x34C759 */
        .warning      = 0x917308, /* darkened from indicator_yellow 0xFFCC00 */
        .danger       = 0xE03229, /* darkened from indicator_red 0xFF3B30 */
        .info         = 0x8969A8, /* darkened from the old [EXEC] purple 0xB48CDC */
        .link         = 0x4A78B7,
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
        .name         = "Onyx Light",
        .bg_primary   = 0xF5F5F7,
        .bg_secondary = 0xFFFFFF,
        .accent       = 0x0A5FD6, /* own accent, deep enough for 4.5:1 with white */
        .text_main    = 0x1D1D1F,
        .text_dim     = 0x6B6B70, /* darkened from 0x707075: text_dim on raised
                                    * (bg_secondary + 1 L* step) was 4.25:1 */
        .border       = 0xDCDCE0,
        .terminal_fg  = 0x1D1D1F,
        .terminal_bg  = 0xF5F5F7,
        .success      = 0x1C883B,
        .warning      = 0xB8860B, /* reads via text_main label (5.17:1), not white */
        .danger       = 0xD32F2F,
        .info         = 0x7B4FA3,
        .link         = 0x0847A6, /* darker than accent, per spec */
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
        .name         = "Sage & Sand",
        .bg_primary   = 0x2B2D24,
        .bg_secondary = 0x353730,
        .accent       = 0xA3B18A,
        .text_main    = 0xEAE7DC,
        .text_dim     = 0xB2B1A8, /* lightened from 0xA19F94: text_dim on raised
                                    * (bg_secondary + 1 L* step) was 3.70:1 */
        .border       = 0x3F4138,
        .terminal_fg  = 0xEAE7DC,
        .terminal_bg  = 0x2B2D24,
        .success      = 0x607F46, /* darkened from indicator_green 0x8FBC6A */
        .warning      = 0x90722F, /* darkened from indicator_yellow 0xD4AA4A */
        .danger       = 0xC05738, /* darkened from indicator_red 0xC75A3A */
        .info         = 0x896B9D,
        .link         = 0x5D7B89,
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
        .name         = "Moss & Mist",
        .bg_primary   = 0xF1F3F0,
        .bg_secondary = 0xFFFFFF,
        .accent       = 0xAAC1AE, /* lightened from 0x84A98C: best-label contrast
                                    * (text_main) was 3.36:1, below 4.5 */
        .text_main    = 0x354F52,
        .text_dim     = 0x577073, /* darkened from 0x597376: text_dim on raised
                                    * (bg_secondary + 1 L* step) was 4.37:1 */
        .border       = 0xD5D8D3,
        .terminal_fg  = 0x354F52,
        .terminal_bg  = 0xF1F3F0,
        .success      = 0x2C8654, /* darkened from indicator_green 0x52B788 */
        .warning      = 0x9A6F08, /* darkened from indicator_yellow 0xE9C46A */
        .danger       = 0xC1440E, /* darkened from indicator_red 0xE76F51 */
        .info         = 0x7A5C99,
        .link         = 0x3D6D7A, /* darker than accent, per spec */
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

/* Whichever of { white, bg_primary, text_main } has the highest contrast
 * against `base` -- the colour to draw as text/icon on that surface. */
static unsigned int resolve_label(unsigned int base, unsigned int bg_primary,
                                   unsigned int text_main)
{
    unsigned int best = 0xFFFFFFu;
    double best_c = theme_contrast(best, base);

    double c_bg = theme_contrast(bg_primary, base);
    if (c_bg > best_c) { best_c = c_bg; best = bg_primary; }

    double c_text = theme_contrast(text_main, base);
    if (c_text > best_c) { best_c = c_text; best = text_main; }

    return best;
}

/* Fill a ThemeSurface: base plus its hover/pressed/disabled/label states.
 * `step_dir` is +1 on dark themes (hover/pressed lighten), -1 on light
 * themes (hover/pressed darken); `bg_primary` is the surface `disabled`
 * blends 50% toward and one of the three `label` candidates; `text_main`
 * is the other. */
static void resolve_surface(unsigned int base, int step_dir,
                             unsigned int bg_primary, unsigned int text_main,
                             ThemeSurface *out)
{
    out->base     = base;
    out->hover    = theme_shift_lightness(base, step_dir * UI_THEME_STEP);
    out->pressed  = theme_shift_lightness(base, step_dir * 2.0 * UI_THEME_STEP);
    out->disabled = theme_blend(base, bg_primary, 0.5);
    out->label    = resolve_label(base, bg_primary, text_main);
}

void ui_theme_resolve(const ThemeColors *base, ThemeTokens *out)
{
    if (!base || !out) return;

    int is_dark = theme_is_dark(base->bg_primary);
    int step_dir = is_dark ? 1 : -1;

    out->is_dark = is_dark;

    resolve_surface(base->bg_primary, step_dir, base->bg_primary, base->text_main, &out->bg_primary);
    resolve_surface(base->bg_secondary, step_dir, base->bg_primary, base->text_main, &out->bg_secondary);
    resolve_surface(base->accent, step_dir, base->bg_primary, base->text_main, &out->accent);
    resolve_surface(base->success, step_dir, base->bg_primary, base->text_main, &out->success);
    resolve_surface(base->warning, step_dir, base->bg_primary, base->text_main, &out->warning);
    resolve_surface(base->danger, step_dir, base->bg_primary, base->text_main, &out->danger);
    resolve_surface(base->info, step_dir, base->bg_primary, base->text_main, &out->info);
    resolve_surface(base->link, step_dir, base->bg_primary, base->text_main, &out->link);

    /* raised: bg_secondary shifted one step, unless overridden */
    unsigned int raised_base = base->overrides.raised
        ? base->overrides.raised
        : theme_shift_lightness(base->bg_secondary, step_dir * UI_THEME_STEP);
    resolve_surface(raised_base, step_dir, base->bg_primary, base->text_main, &out->raised);

    out->text_main = base->text_main;
    out->text_dim  = base->text_dim;
    out->text_disabled = theme_blend(base->text_main, base->text_dim, 0.5);
    out->border = base->border;
    out->focus  = base->overrides.focus ? base->overrides.focus : base->accent;

    out->terminal_fg = base->terminal_fg;
    out->terminal_bg = base->terminal_bg;

    out->chat = base->chat;
}
