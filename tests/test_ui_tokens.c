#include "test_framework.h"
#include "ui_theme.h"
#include "theme.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <dirent.h>

/* ===========================================================================
 * ui_theme_resolve() / ThemeTokens tests (Design-System Foundation, task 2)
 * ===========================================================================
 *
 * Two themes are dark (Onyx Synapse=0, Sage & Sand=2), two are light
 * (Onyx Light=1, Moss & Mist=3).  See src/core/ui_theme.c for the exact
 * intent-colour values and why each was tuned the way it was.
 */

#define MIN_CONTRAST 4.5

/* ---- Contrast ------------------------------------------------------------- */

/* text_main and text_dim must read at >= 4.5:1 against bg_primary,
 * bg_secondary and raised (bg_secondary shifted one UI_THEME_STEP
 * lighter/darker) in EVERY theme.  UI_THEME_STEP is now a fixed CIE L*
 * delta (6 units) rather than a relative-luminance delta, which keeps the
 * "raised" jump visually comparable across dark and light themes and lets
 * text_dim clear 4.5:1 on both polarities -- each theme's text_dim was
 * nudged just enough for this in src/core/ui_theme.c (see the comments
 * there for the before/after numbers). */
int test_ui_tokens_text_contrast_on_backgrounds(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);

        ASSERT_TRUE(theme_contrast(tok.text_main, tok.bg_primary.base) >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.text_dim,  tok.bg_primary.base) >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.text_main, tok.bg_secondary.base) >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.text_dim,  tok.bg_secondary.base) >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.text_main, tok.raised.base) >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.text_dim,  tok.raised.base) >= MIN_CONTRAST);
    }
    TEST_END();
}

/* Each surface's resolved `label` -- whichever of white, bg_primary or
 * text_main has the best contrast against `base` (ui_theme_resolve's
 * resolve_label()) -- must read at >= 4.5:1 on accent and every intent
 * colour, in all four themes.  Each light theme now has its own accent
 * (spec section 1: Onyx Light 0x0A5FD6, Moss & Mist keeps 0x84A98C-family)
 * and Moss & Mist's accent was lightened slightly so its label clears the
 * bar too (see src/core/ui_theme.c). */
int test_ui_tokens_intent_label_contrast(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);

        ASSERT_TRUE(theme_contrast(tok.accent.label,  tok.accent.base)  >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.success.label, tok.success.base) >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.warning.label, tok.warning.base) >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.danger.label,  tok.danger.base)  >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.info.label,    tok.info.base)    >= MIN_CONTRAST);
        ASSERT_TRUE(theme_contrast(tok.link.label,    tok.link.base)    >= MIN_CONTRAST);
    }
    TEST_END();
}

/* label is always one of the three documented candidates. */
int test_ui_tokens_label_is_one_of_the_three_candidates(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);
        ThemeSurface surfaces[] = { tok.bg_primary, tok.bg_secondary, tok.raised,
                                     tok.accent, tok.success, tok.warning,
                                     tok.danger, tok.info, tok.link };
        for (size_t k = 0; k < sizeof(surfaces) / sizeof(surfaces[0]); k++) {
            unsigned int lbl = surfaces[k].label;
            ASSERT_TRUE(lbl == 0xFFFFFFu || lbl == base->bg_primary || lbl == base->text_main);
        }
    }
    TEST_END();
}

int test_ui_tokens_onyx_light_accent_contrast(void)
{
    TEST_BEGIN();
    const ThemeColors *base = ui_theme_get(ui_theme_find("Onyx Light"));
    ASSERT_EQ((int)base->accent, (int)0x0A5FD6);
    ThemeTokens tok;
    ui_theme_resolve(base, &tok);
    ASSERT_TRUE(theme_contrast(tok.accent.label, tok.accent.base) >= MIN_CONTRAST);
    TEST_END();
}

/* Links must be readable and, on light themes, visibly darker than the
 * theme's own accent (spec section 1). */
int test_ui_tokens_link_darker_than_accent_on_light_themes(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);
        if (!tok.is_dark) {
            ASSERT_TRUE(theme_luminance(tok.link.base) < theme_luminance(tok.accent.base));
        }
    }
    TEST_END();
}

/* ---- Derivation ------------------------------------------------------------ */

static int surface_derivation_ok(ThemeSurface s, unsigned int bg_primary, int step_dir)
{
    double lstar_base = theme_lstar(s.base);
    double lstar_hover = theme_lstar(s.hover);
    double lstar_pressed = theme_lstar(s.pressed);
    double lbase = theme_luminance(s.base);
    double ldisabled = theme_luminance(s.disabled);
    double lbg = theme_luminance(bg_primary);

    double dl_hover = lstar_hover - lstar_base;
    double dl_pressed = lstar_pressed - lstar_base;

    /* Direction: hover/pressed move the same way as the theme's polarity
     * (lighten on dark, darken on light) -- i.e. step_dir times the delta
     * must be >= 0. */
    if (dl_hover * step_dir < -1e-9) return 0;
    if (dl_pressed * step_dir < -1e-9) return 0;

    /* Magnitude: at least half of one L* step. */
    if (fabs(dl_hover) < UI_THEME_STEP / 2.0 - 1e-9) return 0;
    if (fabs(dl_pressed) < UI_THEME_STEP / 2.0 - 1e-9) return 0;

    /* disabled is closer to bg_primary than base is (or equal, for
     * bg_primary's own surface, where base == bg_primary already). */
    if (fabs(ldisabled - lbg) > fabs(lbase - lbg) + 1e-9) return 0;

    /* Never clip to pure black/white on any channel. */
    unsigned int vals[3] = { s.hover, s.pressed, s.disabled };
    for (int k = 0; k < 3; k++) {
        unsigned int r = (vals[k] >> 16) & 0xFFu;
        unsigned int g = (vals[k] >> 8) & 0xFFu;
        unsigned int b = vals[k] & 0xFFu;
        if (r == 0 || g == 0 || b == 0) return 0;
        if (r == 255 || g == 255 || b == 255) return 0;
    }
    return 1;
}

int test_ui_tokens_derivation_all_surfaces(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);
        int step_dir = tok.is_dark ? 1 : -1;

        ASSERT_TRUE(surface_derivation_ok(tok.bg_primary, tok.bg_primary.base, step_dir));
        ASSERT_TRUE(surface_derivation_ok(tok.bg_secondary, tok.bg_primary.base, step_dir));
        ASSERT_TRUE(surface_derivation_ok(tok.raised, tok.bg_primary.base, step_dir));
        ASSERT_TRUE(surface_derivation_ok(tok.accent, tok.bg_primary.base, step_dir));
        ASSERT_TRUE(surface_derivation_ok(tok.success, tok.bg_primary.base, step_dir));
        ASSERT_TRUE(surface_derivation_ok(tok.warning, tok.bg_primary.base, step_dir));
        ASSERT_TRUE(surface_derivation_ok(tok.danger, tok.bg_primary.base, step_dir));
        ASSERT_TRUE(surface_derivation_ok(tok.info, tok.bg_primary.base, step_dir));
        ASSERT_TRUE(surface_derivation_ok(tok.link, tok.bg_primary.base, step_dir));
    }
    TEST_END();
}

int test_ui_tokens_raised_derived_from_bg_secondary(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);
        int step_dir = tok.is_dark ? 1 : -1;
        unsigned int expect = theme_shift_lightness(base->bg_secondary,
                                                      step_dir * UI_THEME_STEP);
        ASSERT_EQ((int)tok.raised.base, (int)expect);
    }
    TEST_END();
}

int test_ui_tokens_focus_is_accent_by_default(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);
        ASSERT_EQ((int)tok.focus, (int)base->accent);
    }
    TEST_END();
}

int test_ui_tokens_text_disabled_blend(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);
        unsigned int expect = theme_blend(base->text_main, base->text_dim, 0.5);
        ASSERT_EQ((int)tok.text_disabled, (int)expect);
    }
    TEST_END();
}

/* ---- Overrides -------------------------------------------------------------- */

int test_ui_tokens_override_raised_honoured(void)
{
    TEST_BEGIN();
    ThemeColors c = *ui_theme_get(0);
    c.overrides.raised = 0x123456;
    ThemeTokens tok;
    ui_theme_resolve(&c, &tok);
    ASSERT_EQ((int)tok.raised.base, (int)0x123456);
    TEST_END();
}

int test_ui_tokens_override_focus_honoured(void)
{
    TEST_BEGIN();
    ThemeColors c = *ui_theme_get(1);
    c.overrides.focus = 0xABCDEF;
    ThemeTokens tok;
    ui_theme_resolve(&c, &tok);
    ASSERT_EQ((int)tok.focus, (int)0xABCDEF);
    TEST_END();
}

int test_ui_tokens_no_override_still_derives(void)
{
    TEST_BEGIN();
    /* Zero overrides (the normal case) must still derive raised/focus,
     * not leave them at zero. */
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ASSERT_EQ((int)base->overrides.raised, 0);
        ASSERT_EQ((int)base->overrides.focus, 0);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);
        ASSERT_TRUE(tok.raised.base != 0);
        ASSERT_TRUE(tok.focus != 0);
    }
    TEST_END();
}

/* ---- Chat block + is_dark --------------------------------------------------- */

int test_ui_tokens_chat_copied_unchanged(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);
        ASSERT_EQ(memcmp(&tok.chat, &base->chat, sizeof(ThemeChatColors)), 0);
    }
    TEST_END();
}

int test_ui_tokens_is_dark_matches_theme_is_dark(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);
        ASSERT_EQ(tok.is_dark, theme_is_dark(base->bg_primary));
    }
    /* Known polarities: 0 and 2 dark, 1 and 3 light. */
    ThemeTokens t0, t1, t2, t3;
    ui_theme_resolve(ui_theme_get(0), &t0);
    ui_theme_resolve(ui_theme_get(1), &t1);
    ui_theme_resolve(ui_theme_get(2), &t2);
    ui_theme_resolve(ui_theme_get(3), &t3);
    ASSERT_TRUE(t0.is_dark);
    ASSERT_FALSE(t1.is_dark);
    ASSERT_TRUE(t2.is_dark);
    ASSERT_FALSE(t3.is_dark);
    TEST_END();
}

int test_ui_tokens_resolve_null_safe(void)
{
    TEST_BEGIN();
    ThemeTokens tok;
    memset(&tok, 0xAA, sizeof(tok));
    ui_theme_resolve(NULL, &tok);      /* must not crash */
    ui_theme_resolve(ui_theme_get(0), NULL); /* must not crash */
    TEST_END();
}

/* ===========================================================================
 * Colour gate + scale gate (ratchets) -- see docs/superpowers/plans/
 * 2026-09-07-design-system-foundation.md, Task 2, and section 6 of the
 * design spec.  Both scan src/ui/*.c relative to the repo root, which is
 * where `make test` runs from.
 * ===========================================================================
 */

/* ns_draw.c (renamed from ui_draw.c in Task 4) is the shared drawing
 * module and is excluded here: it is allowed to call RGB() directly. */
#define COLOUR_GATE_BASELINE 79
#define SCALE_GATE_BASELINE  61

static char *read_whole_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

static int count_occurrences(const char *hay, const char *needle)
{
    int count = 0;
    size_t nlen = strlen(needle);
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;
    }
    return count;
}

static int scale_line_matches(const char *line)
{
    if (strstr(line, "#define S(")) return 1;
    if (strstr(line, "#define CLV_SCALE")) return 1;
    if (strstr(line, "MulDiv(") && strstr(line, "96)")) return 1;
    return 0;
}

static int count_scale_lines(const char *content)
{
    int count = 0;
    char linebuf[4096];
    const char *p = content;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof(linebuf)) len = sizeof(linebuf) - 1;
        memcpy(linebuf, p, len);
        linebuf[len] = '\0';
        if (scale_line_matches(linebuf)) count++;
        if (!nl) break;
        p = nl + 1;
    }
    return count;
}

/* Scans src/ui/*.c and reports both gate counts via out params.
 * Returns 0 on success (directory found and scanned), -1 if src/ui could
 * not be opened (e.g. wrong working directory) so callers can skip
 * rather than false-fail. */
static int scan_ui_gates(int *out_colour, int *out_scale)
{
    DIR *d = opendir("src/ui");
    if (!d) return -1;

    int colour = 0, scale = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        size_t nlen = strlen(name);
        if (nlen < 3 || strcmp(name + nlen - 2, ".c") != 0)
            continue;

        char path[600];
        snprintf(path, sizeof(path), "src/ui/%s", name);
        char *content = read_whole_file(path);
        if (!content) continue;

        int is_draw_module = (strcmp(name, "ns_draw.c") == 0);
        if (!is_draw_module)
            colour += count_occurrences(content, "RGB(");

        scale += count_scale_lines(content);

        free(content);
    }
    closedir(d);

    *out_colour = colour;
    *out_scale = scale;
    return 0;
}

int test_ui_tokens_colour_gate(void)
{
    TEST_BEGIN();
    int colour = 0, scale = 0;
    if (scan_ui_gates(&colour, &scale) != 0) {
        printf("  [colour gate] src/ui not found from cwd -- skipping\n");
        TEST_END();
    }
    printf("  [colour gate] RGB( outside ns_draw.c in src/ui/*.c: %d (baseline %d)\n",
           colour, COLOUR_GATE_BASELINE);
    ASSERT_TRUE(colour <= COLOUR_GATE_BASELINE);
    TEST_END();
}

int test_ui_tokens_scale_gate(void)
{
    TEST_BEGIN();
    int colour = 0, scale = 0;
    if (scan_ui_gates(&colour, &scale) != 0) {
        printf("  [scale gate] src/ui not found from cwd -- skipping\n");
        TEST_END();
    }
    printf("  [scale gate] #define S(/#define CLV_SCALE/MulDiv(...96) lines in src/ui/*.c: %d (baseline %d)\n",
           scale, SCALE_GATE_BASELINE);
    ASSERT_TRUE(scale <= SCALE_GATE_BASELINE);
    TEST_END();
}
