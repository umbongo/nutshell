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
        .accent       = 0x5C88C4, /* calmed from 0x007AFF: that was a fully
                                    * saturated signal blue (R=0x00, B=0xFF)
                                    * and the glare was chroma, not lightness.
                                    * Same hue (215 deg), about half the
                                    * chroma (S 53% vs 100%), L* 56.0 vs 53.1
                                    * so it is not brighter. Contrast 5.15:1
                                    * on bg_primary (was 4.66) and 4.58:1 on
                                    * bg_secondary (was 4.15, below AA), so
                                    * accent-filled buttons now clear 4.5:1
                                    * against the panels they sit on. The
                                    * label resolves to bg_primary at 5.15:1. */
        .text_main    = 0xE0E0E0,
        .text_dim     = 0x919191, /* lightened from 0x888888: text_dim on raised
                                    * (bg_secondary + 1 L* step) was 4.05:1 */
        .border       = 0x2A2A2A,
        .terminal_fg  = 0xE0E0E0,
        .terminal_bg  = 0x121212,
        .terminal_min_contrast = 0.0, /* dark: today's palette, byte for byte */
        .success      = 0x21883B, /* darkened from indicator_green 0x34C759 */
        .warning      = 0xA58308, /* indicator_yellow 0xFFCC00 darkened 28 L*: 5.22 on bg_primary */
        .danger       = 0xE03229, /* darkened from indicator_red 0xFF3B30 */
        .info         = 0x8969A8, /* darkened from the old [EXEC] purple 0xB48CDC */
        .link         = 0x5D82BB, /* +4 L* so it reads as text: 4.80 on bg_primary */
        .chat = {
            0x5C88C4, /* user_bubble — accent blue */
            0x121212, /* user_text — the accent surface's resolved label
                        * (bg_primary) at 5.15:1; the old white was 4.02:1
                        * on 0x007AFF and would be 3.64:1 here, and the AI
                        * avatar next to it already draws its glyph with
                        * ns_tokens()->accent.label over the same fill */
            0x5C88C4, /* ai_accent — accent */
            0x1A1A2E, /* cmd_bg — darker than bg */
            0x2A2A3E, /* cmd_border */
            0xC0C0C0, /* cmd_text — monospace light */
            0x5C88C4, /* thinking_border — accent */
            0x888888, /* thinking_text — dim */
            0x828282, /* status_text — dimmer; 4.87:1 on bg_primary (was 0x666666) */
            0x34C759, /* indicator_green */
            0xFFCC00, /* indicator_yellow */
            0xFF3B30, /* indicator_red */
            0x4D76AE, /* send_btn — the accent taken down ~7 L* (to 49.0).
                        * ai_chat.c fills this and draws the white paper-
                        * plane glyph over it, so it has to stay dark enough
                        * for white: 4.65:1 here, where the old periwinkle
                        * 0x6BAAFF (L* 68.8) managed only 2.38:1 and was the
                        * brightest blue in the window. */
            0xEA6262, /* stop_btn — soft coral red */
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
        .terminal_min_contrast = 4.5, /* indexed text floored to WCAG AA */
        .success      = 0x1C883B,
        .warning      = 0x886208, /* darkened 15 L* to read as text; white label 5.53:1 */
        .danger       = 0xD32F2F,
        .info         = 0x7B4FA3,
        .link         = 0x0847A6, /* darker than accent, per spec */
        .chat = {
            0x0869DD, 0xFFFFFF, 0x0869DD,
            0xEEEEF2, 0xDCDCE0, 0x333333,
            0x007AFF, 0x86868B, 0x6C6C6C,
            0x1D7E35, 0x866A08, 0xCD2D24,
            0x6C90BD, /* send_btn — light sky blue */
            0xBF7D7D, /* stop_btn — rose pink */
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
        .terminal_min_contrast = 0.0, /* dark: today's palette, byte for byte */
        .success      = 0x607F46, /* darkened from indicator_green 0x8FBC6A */
        .warning      = 0xB7933F, /* indicator_yellow 0xD4AA4A darkened 9 L*: 4.83 on bg_primary */
        .danger       = 0xC05738, /* darkened from indicator_red 0xC75A3A */
        .info         = 0x896B9D,
        .link         = 0x8A9BA4, /* +13 L* so it reads as text: 4.86 on bg_primary */
        .chat = {
            0xA3B18A, 0x1A1A14, 0xA3B18A,
            0x232520, 0x3F4138, 0xD4D1C4,
            0xA3B18A, 0xA09E93, 0x98978D,
            0x8FBC6A, 0xD4AA4A, 0xD08375,
            0x7E91A2, /* send_btn — dusty steel blue */
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
        .terminal_min_contrast = 4.5, /* indexed text floored to WCAG AA */
        .success      = 0x2C8654, /* darkened from indicator_green 0x52B788 */
        .warning      = 0x845F08, /* indicator_yellow 0xE9C46A darkened 7 L* more; white label 5.80:1 */
        .danger       = 0xC1440E, /* darkened from indicator_red 0xE76F51 */
        .info         = 0x7A5C99,
        .link         = 0x3D6D7A, /* darker than accent, per spec */
        .chat = {
            0x84A98C, 0xFFFFFF, 0x84A98C,
            0xE8EBE6, 0xD5D8D3, 0x354F52,
            0x84A98C, 0x6B8A8D, 0x5F6D6E,
            0x347858, 0x7C6735, 0xA94F39,
            0x78929E, /* send_btn — misty blue */
            0xBC7F7A, /* stop_btn — dusty rose */
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
 * `bg_primary` is the surface `disabled` blends 50% toward and one of the
 * three `label` candidates; `text_main` is the other.
 *
 * The direction hover/pressed step is `step_dir`: +1 lightens, -1 darkens.
 * With `away_from_label` set (the filled surfaces: accent and the five
 * intents) `step_dir` is ignored: the label is resolved first and the states
 * step AWAY from it -- lighten when the label is darker than the base,
 * darken otherwise. The label is the best of three candidates on `base` and
 * every step moves away from it, so its contrast on hover and pressed is
 * never below its contrast on base. Neutral surfaces keep the theme polarity
 * (+1 on dark themes, -1 on light). */
static void resolve_surface(unsigned int base, int step_dir, int away_from_label,
                             unsigned int bg_primary, unsigned int text_main,
                             ThemeSurface *out)
{
    out->label = resolve_label(base, bg_primary, text_main);
    if (away_from_label)
        step_dir = theme_lstar(out->label) < theme_lstar(base) ? 1 : -1;
    out->base     = base;
    out->hover    = theme_shift_lightness(base, step_dir * UI_THEME_STEP);
    out->pressed  = theme_shift_lightness(base, step_dir * 2.0 * UI_THEME_STEP);
    out->disabled = theme_blend(base, bg_primary, 0.5);
}

/* The selection hue anywhere but the user bubble, from the focus token and
 * the theme polarity.  The one place that decides it: ui_theme_sel_hue() and
 * ui_theme_md_role() both read it, so the markdown walk follows any change. */
static unsigned int sel_hue_off_bubble(unsigned int focus, int is_dark)
{
    (void)is_dark;
    return focus;
}

/* rgb_alpha() of src/ui/ns_draw.c, byte for byte: fg * a + bg * (1 - a) per
 * channel in float, truncated.  Kept here so the portable tests see it. */
static unsigned int tint(unsigned int fg, unsigned int bg, float a)
{
    unsigned int out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float f = (float)((fg >> sh) & 0xFFu), b = (float)((bg >> sh) & 0xFFu);
        out |= ((unsigned int)(f * a + b * (1.0f - a)) & 0xFFu) << sh;
    }
    return out;
}

unsigned int ui_theme_md_role(const ThemeColors *c, UiMdRole role)
{
    if (!c) return 0;

    unsigned int hue = role == UI_MD_CODE ? c->info : c->link;
    unsigned int focus = c->overrides.focus ? c->overrides.focus : c->accent;
    int is_dark = theme_is_dark(c->bg_primary);
    unsigned int sel = sel_hue_off_bubble(focus, is_dark);

    unsigned int surf[3];
    surf[0] = c->bg_primary;
    if (role == UI_MD_CODE) {
        surf[1] = c->chat.cmd_bg;
        surf[2] = tint(sel, c->chat.cmd_bg, UI_SEL_ALPHA);
    } else {
        surf[1] = tint(c->text_dim, c->bg_primary, UI_DIM_TINT_ALPHA);
        surf[2] = tint(sel, c->bg_primary, UI_SEL_ALPHA);
    }

    double dir = is_dark ? 2.0 : -2.0;
    unsigned int cur = hue;
    for (int step = 0; step <= 50; step++) {
        if (step > 0) cur = theme_shift_lightness(cur, dir);
        if (theme_contrast(cur, surf[0]) >= UI_TEXT_MIN_RATIO &&
            theme_contrast(cur, surf[1]) >= UI_TEXT_MIN_RATIO &&
            theme_contrast(cur, surf[2]) >= UI_TEXT_MIN_RATIO)
            return cur;
    }
    return c->text_main;
}

void ui_theme_resolve(const ThemeColors *base, ThemeTokens *out)
{
    if (!base || !out) return;

    int is_dark = theme_is_dark(base->bg_primary);
    int step_dir = is_dark ? 1 : -1;

    out->is_dark = is_dark;

    resolve_surface(base->bg_primary, step_dir, 0, base->bg_primary, base->text_main, &out->bg_primary);
    resolve_surface(base->bg_secondary, step_dir, 0, base->bg_primary, base->text_main, &out->bg_secondary);
    resolve_surface(base->accent, step_dir, 1, base->bg_primary, base->text_main, &out->accent);
    resolve_surface(base->success, step_dir, 1, base->bg_primary, base->text_main, &out->success);
    resolve_surface(base->warning, step_dir, 1, base->bg_primary, base->text_main, &out->warning);
    resolve_surface(base->danger, step_dir, 1, base->bg_primary, base->text_main, &out->danger);
    resolve_surface(base->info, step_dir, 1, base->bg_primary, base->text_main, &out->info);
    resolve_surface(base->link, step_dir, 0, base->bg_primary, base->text_main, &out->link);

    /* raised: bg_secondary shifted one step, unless overridden */
    unsigned int raised_base = base->overrides.raised
        ? base->overrides.raised
        : theme_shift_lightness(base->bg_secondary, step_dir * UI_THEME_STEP);
    resolve_surface(raised_base, step_dir, 0, base->bg_primary, base->text_main, &out->raised);

    out->text_main = base->text_main;
    out->text_dim  = base->text_dim;
    out->text_disabled = theme_blend(base->text_main, base->text_dim, 0.5);
    out->text_dim_label = resolve_label(base->text_dim, base->bg_primary, base->text_main);
    out->border = base->border;
    out->focus  = base->overrides.focus ? base->overrides.focus : base->accent;
    out->md_structure = ui_theme_md_role(base, UI_MD_STRUCTURE);
    out->md_code      = ui_theme_md_role(base, UI_MD_CODE);

    out->terminal_fg = base->terminal_fg;
    out->terminal_bg = base->terminal_bg;
    term_palette_build(&out->term, base->terminal_fg, base->terminal_bg,
                       base->terminal_min_contrast);

    out->chat = base->chat;
}

unsigned int ui_theme_sel_hue(const ThemeTokens *t, int on_user_bubble)
{
    if (!t) return 0;
    return on_user_bubble ? t->chat.user_text : sel_hue_off_bubble(t->focus, t->is_dark);
}
