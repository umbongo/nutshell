/* tests/test_theme_contrast.c -- every theme's text is readable on every
 * surface it is painted on.
 *
 * One table (k_pairs) lists each (text colour, surface colour, floor) that
 * src/ui actually paints, resolved through ui_theme_resolve() for all
 * NUM_UI_THEMES themes, and each pair is asserted against its floor with
 * theme_contrast() (the WCAG ratio).  A failure names the theme, the two
 * tokens and the ratio to two decimals.
 *
 * Not duplicated here: the terminal's ANSI palette floors (see
 * docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md,
 * tests/test_term_colors.c) and the selection highlight's visibility
 * (tests/test_sel_contrast.c).
 *
 * A pair that fails on a shipped theme is a finding, not something to fix
 * here: it is listed in k_exempt, with its ratio, and marked KNOWN at its row.
 * The list is counted (EXPECTED_EXEMPTIONS), and an exemption whose pair now
 * passes fails the suite, so a fix has to delete its line.  Never lower a
 * floor or add an exemption to get a new change through.
 */
#include "test_framework.h"
#include "ui_theme.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

/* ---- Floors --------------------------------------------------------------
 *
 * FLOOR_TEXT 4.5 (WCAG 1.4.3, AA, normal-size text): body text and every
 *   text that carries information.  Applies to text_main, the `.label` of a
 *   surface on its .base/.hover/.pressed, intents painted as text
 *   (warning/link and the markdown roles), the chat colours and the activity
 *   indicators used as text.  The UI draws these at 11-14 px, never "large".
 * FLOOR_DIM 3.0: deliberately de-emphasised text -- text_dim, text_disabled,
 *   a disabled button's label, an accelerator column.  WCAG exempts disabled
 *   controls and places no number on "secondary" text; 3:1 is the floor below
 *   which such text stops being legible at all.  (tests/test_ui_tokens.c
 *   already holds text_dim to 4.5 on bg_primary, bg_secondary and raised.)
 * FLOOR_NONTEXT 3.0 (WCAG 1.4.11): the stroke that identifies a control
 *   (unselected checkbox, Deny all, segmented/policy outline) against the
 *   surface it sits on.  Decorative separators are not tested. */
#define FLOOR_TEXT    4.5
#define FLOOR_DIM     3.0
#define FLOOR_NONTEXT 3.0

/* ---- Known failures (findings) ------------------------------------------
 *
 * Counted: EXPECTED_EXEMPTIONS must equal the number of lines below.  Each
 * line is { theme name, text token, surface token } with its measured ratio
 * beside it; the pair's row in k_pairs carries a KNOWN comment too.  The list
 * is empty once every theme passes. */
typedef struct {
    const char *theme, *fg, *bg;
} Exemption;

static const Exemption k_exempt[] = {
    /* Onyx Synapse */
    { "Onyx Synapse", "text_main", "success.base" }, /* 3.42 */
    { "Onyx Synapse", "success.label", "success.hover" }, /* 3.66 */
    { "Onyx Synapse", "success.label", "success.pressed" }, /* 2.98 */
    { "Onyx Synapse", "warning.label", "warning.hover" }, /* 3.65 */
    { "Onyx Synapse", "danger.label", "danger.hover" }, /* 3.65 */
    { "Onyx Synapse", "info.label", "info.hover" }, /* 3.66 */
    { "Onyx Synapse", "chat.user_text", "sel30(chat.user_text on chat.user_bubble)" }, /* 3.14 */
    { "Onyx Synapse", "chat.status_text", "bg_primary.base" }, /* 3.26 */
    { "Onyx Synapse", "warning.base", "bg_primary.base" }, /* 4.16 */
    { "Onyx Synapse", "warning.base", "chat.cmd_bg" }, /* 3.78 */
    { "Onyx Synapse", "link.base", "bg_primary.base" }, /* 4.16 */
    { "Onyx Synapse", "md code(info role)", "chat.cmd_bg" }, /* 4.35 */
    { "Onyx Synapse", "activity text(indicator_red)", "bg_primary.base" }, /* 3.41 */
    { "Onyx Synapse", "disabled label(accent)", "accent.disabled" }, /* 1.55 */
    { "Onyx Synapse", "border", "bg_secondary.base" }, /* 1.16 */
    { "Onyx Synapse", "border", "chat.cmd_bg" }, /* 1.19 */
    /* Onyx Light */
    { "Onyx Light", "text_main", "success.base" }, /* 3.71 */
    { "Onyx Light", "warning.label", "warning.hover" }, /* 4.18 */
    { "Onyx Light", "chat.user_text", "chat.user_bubble" }, /* 4.02 */
    { "Onyx Light", "chat.user_text", "sel30(chat.user_text on chat.user_bubble)" }, /* 2.65 */
    { "Onyx Light", "chat.ai_accent", "bg_primary.base" }, /* 3.69 */
    { "Onyx Light", "chat.status_text", "bg_primary.base" }, /* 2.13 */
    { "Onyx Light", "warning.base", "bg_primary.base" }, /* 2.99 */
    { "Onyx Light", "warning.base", "chat.cmd_bg" }, /* 2.81 */
    { "Onyx Light", "chat.indicator_green", "bg_primary.base" }, /* 2.04 */
    { "Onyx Light", "chat.indicator_yellow", "bg_primary.base" }, /* 1.39 */
    { "Onyx Light", "chat.indicator_red", "bg_primary.base" }, /* 3.26 */
    { "Onyx Light", "activity text(indicator_green)", "bg_primary.base" }, /* 1.73 */
    { "Onyx Light", "activity text(indicator_yellow)", "bg_primary.base" }, /* 1.30 */
    { "Onyx Light", "activity text(indicator_red)", "bg_primary.base" }, /* 2.60 */
    { "Onyx Light", "disabled label(accent)", "accent.disabled" }, /* 1.59 */
    { "Onyx Light", "disabled label(success)", "success.disabled" }, /* 1.48 */
    { "Onyx Light", "border", "bg_secondary.base" }, /* 1.37 */
    { "Onyx Light", "border", "chat.cmd_bg" }, /* 1.18 */
    /* Sage & Sand */
    { "Sage & Sand", "text_main", "success.base" }, /* 3.67 */
    { "Sage & Sand", "success.label", "success.hover" }, /* 3.66 */
    { "Sage & Sand", "success.label", "success.pressed" }, /* 2.99 */
    { "Sage & Sand", "warning.label", "warning.hover" }, /* 3.67 */
    { "Sage & Sand", "danger.label", "danger.hover" }, /* 3.65 */
    { "Sage & Sand", "info.label", "info.hover" }, /* 3.64 */
    { "Sage & Sand", "chat.user_text", "sel30(chat.user_text on chat.user_bubble)" }, /* 4.43 */
    { "Sage & Sand", "chat.status_text", "bg_primary.base" }, /* 3.14 */
    { "Sage & Sand", "warning.base", "bg_primary.base" }, /* 3.08 */
    { "Sage & Sand", "warning.base", "chat.cmd_bg" }, /* 3.41 */
    { "Sage & Sand", "link.base", "bg_primary.base" }, /* 3.10 */
    { "Sage & Sand", "chat.indicator_red", "bg_primary.base" }, /* 3.29 */
    { "Sage & Sand", "activity text(indicator_green)", "bg_primary.base" }, /* 4.26 */
    { "Sage & Sand", "activity text(indicator_yellow)", "bg_primary.base" }, /* 4.31 */
    { "Sage & Sand", "activity text(indicator_red)", "bg_primary.base" }, /* 2.44 */
    { "Sage & Sand", "disabled label(accent)", "accent.disabled" }, /* 1.63 */
    { "Sage & Sand", "border", "bg_secondary.base" }, /* 1.16 */
    { "Sage & Sand", "border", "chat.cmd_bg" }, /* 1.49 */
    /* Moss & Mist */
    { "Moss & Mist", "text_main", "success.base" }, /* 1.94 */
    { "Moss & Mist", "accent.label", "accent.hover" }, /* 3.81 */
    { "Moss & Mist", "accent.label", "accent.pressed" }, /* 3.16 */
    { "Moss & Mist", "bg_primary.base", "accent.base" }, /* 1.72 */
    { "Moss & Mist", "chat.user_text", "chat.user_bubble" }, /* 2.61 */
    { "Moss & Mist", "chat.user_text", "sel30(chat.user_text on chat.user_bubble)" }, /* 1.89 */
    { "Moss & Mist", "chat.ai_accent", "bg_primary.base" }, /* 2.34 */
    { "Moss & Mist", "chat.status_text", "bg_primary.base" }, /* 2.06 */
    { "Moss & Mist", "warning.base", "bg_primary.base" }, /* 4.05 */
    { "Moss & Mist", "warning.base", "chat.cmd_bg" }, /* 3.75 */
    { "Moss & Mist", "chat.indicator_green", "bg_primary.base" }, /* 2.22 */
    { "Moss & Mist", "chat.indicator_yellow", "bg_primary.base" }, /* 1.50 */
    { "Moss & Mist", "chat.indicator_red", "bg_primary.base" }, /* 2.77 */
    { "Moss & Mist", "activity text(indicator_green)", "bg_primary.base" }, /* 1.80 */
    { "Moss & Mist", "activity text(indicator_yellow)", "bg_primary.base" }, /* 1.35 */
    { "Moss & Mist", "activity text(indicator_red)", "bg_primary.base" }, /* 2.15 */
    { "Moss & Mist", "disabled label(accent)", "accent.disabled" }, /* 2.19 */
    { "Moss & Mist", "disabled label(raised)", "raised.disabled" }, /* 2.38 */
    { "Moss & Mist", "disabled label(bg_secondary)", "bg_secondary.disabled" }, /* 2.44 */
    { "Moss & Mist", "disabled label(success)", "success.disabled" }, /* 1.48 */
    { "Moss & Mist", "accelerator(accent.label at 75%)", "accent.base" }, /* 2.95 */
    { "Moss & Mist", "border", "bg_secondary.base" }, /* 1.44 */
    { "Moss & Mist", "border", "chat.cmd_bg" }, /* 1.20 */
};
#define EXPECTED_EXEMPTIONS 75

/* ---- Colour selectors ----------------------------------------------------
 *
 * A selector picks one colour out of a resolved token set.  Derived colours
 * (the policy control's tints, a chip's background, a disabled label) are
 * computed from the same tokens exactly as src/ui computes them; theme_blend
 * (a, b, t) is rgb_alpha (a, b, 1 - t). */
typedef unsigned int (*ColourFn)(const ThemeTokens *t);
typedef struct {
    const char *name;
    ColourFn    fn;
} Sel;

#define SEL(id, label, expr) \
    static unsigned int id##_f(const ThemeTokens *t) { return (expr); } \
    static const Sel id = { label, id##_f }

/* plain tokens */
SEL(s_text_main,      "text_main",           t->text_main);
SEL(s_text_dim,       "text_dim",            t->text_dim);
SEL(s_text_disabled,  "text_disabled",       t->text_disabled);
SEL(s_text_dim_label, "text_dim_label",      t->text_dim_label);
SEL(s_border,         "border",              t->border);
SEL(s_term_fg,        "terminal_fg",         t->terminal_fg);
SEL(s_term_bg,        "terminal_bg",         t->terminal_bg);
/* surfaces: base / hover / pressed / disabled / label */
SEL(s_bgp_base,       "bg_primary.base",     t->bg_primary.base);
SEL(s_bgs_base,       "bg_secondary.base",   t->bg_secondary.base);
SEL(s_bgs_hover,      "bg_secondary.hover",  t->bg_secondary.hover);
SEL(s_bgs_pressed,    "bg_secondary.pressed", t->bg_secondary.pressed);
SEL(s_bgs_disabled,   "bg_secondary.disabled", t->bg_secondary.disabled);
SEL(s_bgs_label,      "bg_secondary.label",  t->bg_secondary.label);
SEL(s_raised_base,    "raised.base",         t->raised.base);
SEL(s_raised_hover,   "raised.hover",        t->raised.hover);
SEL(s_raised_pressed, "raised.pressed",      t->raised.pressed);
SEL(s_raised_disabled, "raised.disabled",    t->raised.disabled);
SEL(s_raised_label,   "raised.label",        t->raised.label);
SEL(s_acc_base,       "accent.base",         t->accent.base);
SEL(s_acc_hover,      "accent.hover",        t->accent.hover);
SEL(s_acc_pressed,    "accent.pressed",      t->accent.pressed);
SEL(s_acc_disabled,   "accent.disabled",     t->accent.disabled);
SEL(s_acc_label,      "accent.label",        t->accent.label);
SEL(s_ok_base,        "success.base",        t->success.base);
SEL(s_ok_hover,       "success.hover",       t->success.hover);
SEL(s_ok_pressed,     "success.pressed",     t->success.pressed);
SEL(s_ok_disabled,    "success.disabled",    t->success.disabled);
SEL(s_ok_label,       "success.label",       t->success.label);
SEL(s_warn_base,      "warning.base",        t->warning.base);
SEL(s_warn_hover,     "warning.hover",       t->warning.hover);
SEL(s_warn_label,     "warning.label",       t->warning.label);
SEL(s_err_base,       "danger.base",         t->danger.base);
SEL(s_err_hover,      "danger.hover",        t->danger.hover);
SEL(s_err_label,      "danger.label",        t->danger.label);
SEL(s_info_base,      "info.base",           t->info.base);
SEL(s_info_hover,     "info.hover",          t->info.hover);
SEL(s_info_label,     "info.label",          t->info.label);
SEL(s_link_base,      "link.base",           t->link.base);
/* the chat block */
SEL(s_c_user_bubble,  "chat.user_bubble",    t->chat.user_bubble);
SEL(s_c_user_text,    "chat.user_text",      t->chat.user_text);
SEL(s_c_ai_accent,    "chat.ai_accent",      t->chat.ai_accent);
SEL(s_c_cmd_bg,       "chat.cmd_bg",         t->chat.cmd_bg);
SEL(s_c_cmd_text,     "chat.cmd_text",       t->chat.cmd_text);
SEL(s_c_status,       "chat.status_text",    t->chat.status_text);
SEL(s_c_ind_green,    "chat.indicator_green", t->chat.indicator_green);
SEL(s_c_ind_yellow,   "chat.indicator_yellow", t->chat.indicator_yellow);
SEL(s_c_ind_red,      "chat.indicator_red",  t->chat.indicator_red);

/* derived: the policy control (ns_draw_policy) fills a lit stop with the
 * intent over bg_secondary at 18% (0.30 hot) when AUTO is off. */
SEL(s_tint_ok,        "tint18(success on bg_secondary)",
    theme_blend(t->success.base, t->bg_secondary.base, 0.82));
SEL(s_tint_warn,      "tint18(warning on bg_secondary)",
    theme_blend(t->warning.base, t->bg_secondary.base, 0.82));
SEL(s_tint_err,       "tint18(danger on bg_secondary)",
    theme_blend(t->danger.base, t->bg_secondary.base, 0.82));
SEL(s_tint_info,      "tint18(info on bg_secondary)",
    theme_blend(t->info.base, t->bg_secondary.base, 0.82));
SEL(s_hot_ok,         "tint30(success on bg_secondary)",
    theme_blend(t->success.base, t->bg_secondary.base, 0.70));
SEL(s_hot_warn,       "tint30(warning on bg_secondary)",
    theme_blend(t->warning.base, t->bg_secondary.base, 0.70));
SEL(s_hot_err,        "tint30(danger on bg_secondary)",
    theme_blend(t->danger.base, t->bg_secondary.base, 0.70));
SEL(s_hot_info,       "tint30(info on bg_secondary)",
    theme_blend(t->info.base, t->bg_secondary.base, 0.70));
/* derived: the dim chip (settled command's "quoted"/dim state) is text_dim
 * at 18% over its panel, with text_dim as its text. */
SEL(s_chip_on_bgp,    "tint18(text_dim on bg_primary)",
    theme_blend(t->text_dim, t->bg_primary.base, 0.82));
SEL(s_chip_on_cmd,    "tint18(text_dim on chat.cmd_bg)",
    theme_blend(t->text_dim, t->chat.cmd_bg, 0.82));
/* derived: selected text -- the focus hue at 30% over the surface. */
SEL(s_sel_on_bgp,     "sel30(focus on bg_primary)",
    theme_blend(t->focus, t->bg_primary.base, 0.70));
SEL(s_sel_on_cmd,     "sel30(focus on chat.cmd_bg)",
    theme_blend(t->focus, t->chat.cmd_bg, 0.70));
SEL(s_sel_on_user,    "sel30(chat.user_text on chat.user_bubble)",
    theme_blend(t->chat.user_text, t->chat.user_bubble, 0.70));
/* derived: a disabled button's label is its label blended 50% into its
 * disabled fill (ns_draw_button). */
SEL(s_dlabel_acc,     "disabled label(accent)",
    theme_blend(t->accent.label, t->accent.disabled, 0.5));
SEL(s_dlabel_raised,  "disabled label(raised)",
    theme_blend(t->raised.label, t->raised.disabled, 0.5));
SEL(s_dlabel_bgs,     "disabled label(bg_secondary)",
    theme_blend(t->bg_secondary.label, t->bg_secondary.disabled, 0.5));
SEL(s_dlabel_ok,      "disabled label(success)",
    theme_blend(t->success.label, t->success.disabled, 0.5));
/* derived: the menu's accelerator on the selected row: label at 75% over
 * the accent (window.c WM_DRAWITEM). */
SEL(s_accel_sel,      "accelerator(accent.label at 75%)",
    theme_blend(t->accent.label, t->accent.base, 0.25));
/* derived: markdown roles (md_render.c md_role_clr): link/info hue moved to
 * 4.5:1 on bg_primary, or text_main when no shade reaches it. */
SEL(s_md_structure,   "md structure(link role)",
    theme_role_color(t->link.base, t->bg_primary.base, t->text_main));
SEL(s_md_code,        "md code(info role)",
    theme_role_color(t->info.base, t->bg_primary.base, t->text_main));
/* derived: the chat activity line is 3/4 indicator + 1/4 bg_primary. */
SEL(s_act_green,      "activity text(indicator_green)",
    theme_blend(t->chat.indicator_green, t->bg_primary.base, 0.25));
SEL(s_act_yellow,     "activity text(indicator_yellow)",
    theme_blend(t->chat.indicator_yellow, t->bg_primary.base, 0.25));
SEL(s_act_red,        "activity text(indicator_red)",
    theme_blend(t->chat.indicator_red, t->bg_primary.base, 0.25));

/* ---- The table -----------------------------------------------------------
 *
 * Each row is a pair src/ui paints (the site is in the comment).  Rows with
 * the same fg and bg may not repeat (test_theme_contrast_table_is_well_formed). */
typedef struct {
    const Sel *fg, *bg;
    double     floor;
} Pair;

static const Pair k_pairs[] = {
    /* -- text_main (4.5) -- */
    { &s_text_main, &s_bgp_base,     FLOOR_TEXT },  /* windows, dialogs, chat, active tab */
    { &s_text_main, &s_bgs_base,     FLOOR_TEXT },  /* inactive tab, menu, edits, lists */
    { &s_text_main, &s_bgs_hover,    FLOOR_TEXT },  /* hovered tab, hot policy stop */
    { &s_text_main, &s_raised_base,  FLOOR_TEXT },  /* selected segment, suggestion chip */
    { &s_text_main, &s_raised_hover, FLOOR_TEXT },  /* hot suggestion chip */
    { &s_text_main, &s_c_cmd_bg,     FLOOR_TEXT },  /* approval card header, Deny all, hot cmd text */
    { &s_text_main, &s_term_bg,      FLOOR_TEXT },  /* terminal default background */
    /* KNOWN: Onyx Synapse 3.42; Onyx Light 3.71; Sage & Sand 3.67; Moss & Mist 1.94 */
    { &s_text_main, &s_ok_base,      FLOOR_TEXT },  /* tab "L" glyph, logging on */
    { &s_text_main, &s_border,       FLOOR_TEXT },  /* tab "L" glyph, logging off */
    { &s_term_fg,   &s_term_bg,      FLOOR_TEXT },  /* terminal default text */
    { &s_text_main, &s_tint_ok,      FLOOR_TEXT },  /* policy stop, AUTO off, lit */
    { &s_text_main, &s_tint_warn,    FLOOR_TEXT },
    { &s_text_main, &s_tint_err,     FLOOR_TEXT },
    { &s_text_main, &s_tint_info,    FLOOR_TEXT },
    { &s_text_main, &s_hot_ok,       FLOOR_TEXT },  /* the same, hot */
    { &s_text_main, &s_hot_warn,     FLOOR_TEXT },
    { &s_text_main, &s_hot_err,      FLOOR_TEXT },
    { &s_text_main, &s_hot_info,     FLOOR_TEXT },
    { &s_text_main, &s_sel_on_bgp,   FLOOR_TEXT },  /* selected AI text */
    { &s_text_main, &s_sel_on_cmd,   FLOOR_TEXT },  /* selected text in code */

    /* -- surface labels, rest/hover/pressed (4.5) -- */
    { &s_acc_label,    &s_acc_base,      FLOOR_TEXT },  /* OK / Copy / Retry-style buttons, menu selection */
    /* KNOWN: Moss & Mist 3.81 */
    { &s_acc_label,    &s_acc_hover,     FLOOR_TEXT },
    /* KNOWN: Moss & Mist 3.16 */
    { &s_acc_label,    &s_acc_pressed,   FLOOR_TEXT },
    { &s_raised_label, &s_raised_base,   FLOOR_TEXT },  /* Cancel button */
    { &s_raised_label, &s_raised_hover,  FLOOR_TEXT },
    { &s_raised_label, &s_raised_pressed, FLOOR_TEXT },
    { &s_bgs_label,    &s_bgs_base,      FLOOR_TEXT },  /* Close button */
    { &s_bgs_label,    &s_bgs_hover,     FLOOR_TEXT },
    { &s_bgs_label,    &s_bgs_pressed,   FLOOR_TEXT },
    { &s_ok_label,     &s_ok_base,       FLOOR_TEXT },  /* Run button, checkbox tick, policy, chips */
    /* KNOWN: Onyx Synapse 3.66; Sage & Sand 3.66 */
    { &s_ok_label,     &s_ok_hover,      FLOOR_TEXT },
    /* KNOWN: Onyx Synapse 2.98; Sage & Sand 2.99 */
    { &s_ok_label,     &s_ok_pressed,    FLOOR_TEXT },
    { &s_warn_label,   &s_warn_base,     FLOOR_TEXT },  /* WRITE chip, policy, selected warning segment */
    /* KNOWN: Onyx Synapse 3.65; Onyx Light 4.18; Sage & Sand 3.67 */
    { &s_warn_label,   &s_warn_hover,    FLOOR_TEXT },
    { &s_err_label,    &s_err_base,      FLOOR_TEXT },  /* CRITICAL chip, policy */
    /* KNOWN: Onyx Synapse 3.65; Sage & Sand 3.65 */
    { &s_err_label,    &s_err_hover,     FLOOR_TEXT },
    { &s_info_label,   &s_info_base,     FLOOR_TEXT },  /* UNKNOWN chip, policy */
    /* KNOWN: Onyx Synapse 3.66; Sage & Sand 3.64 */
    { &s_info_label,   &s_info_hover,    FLOOR_TEXT },
    { &s_text_dim_label, &s_text_dim,    FLOOR_TEXT },  /* READ chip */
    /* KNOWN: Moss & Mist 1.72 */
    { &s_bgp_base,     &s_acc_base,      FLOOR_TEXT },  /* settings nav / session list, selected row */

    /* -- chat block (4.5) -- */
    /* KNOWN: Onyx Light 4.02; Moss & Mist 2.61 */
    { &s_c_user_text,  &s_c_user_bubble, FLOOR_TEXT },  /* user message bubble */
    /* KNOWN: Onyx Synapse 3.14; Onyx Light 2.65; Sage & Sand 4.43; Moss & Mist 1.89 */
    { &s_c_user_text,  &s_sel_on_user,   FLOOR_TEXT },  /* selected text in the bubble */
    { &s_c_cmd_text,   &s_c_cmd_bg,      FLOOR_TEXT },  /* command rows, detail dialog edit */
    /* KNOWN: Onyx Light 3.69; Moss & Mist 2.34 */
    { &s_c_ai_accent,  &s_bgp_base,      FLOOR_TEXT },  /* "AI" name next to the avatar */
    /* KNOWN: Onyx Synapse 3.26; Onyx Light 2.13; Sage & Sand 3.14; Moss & Mist 2.06 */
    { &s_c_status,     &s_bgp_base,      FLOOR_TEXT },  /* status messages in the chat */

    /* -- intents painted as text (4.5) -- */
    /* KNOWN: Onyx Synapse 4.16; Onyx Light 2.99; Sage & Sand 3.08; Moss & Mist 4.05 */
    { &s_warn_base,    &s_bgp_base,      FLOOR_TEXT },  /* detail dialog reason, settings URL warning */
    /* KNOWN: Onyx Synapse 3.78; Onyx Light 2.81; Sage & Sand 3.41; Moss & Mist 3.75 */
    { &s_warn_base,    &s_c_cmd_bg,      FLOOR_TEXT },  /* approval row reason */
    /* KNOWN: Onyx Synapse 4.16; Sage & Sand 3.10 */
    { &s_link_base,    &s_bgp_base,      FLOOR_TEXT },  /* [Retry] */
    { &s_md_structure, &s_bgp_base,      FLOOR_TEXT },  /* markdown headings, list markers */
    /* KNOWN: Onyx Synapse 4.35 */
    { &s_md_code,      &s_c_cmd_bg,      FLOOR_TEXT },  /* markdown inline code */

    /* -- activity indicators painted as text (4.5) -- */
    /* KNOWN: Onyx Light 2.04; Moss & Mist 2.22 */
    { &s_c_ind_green,  &s_bgp_base,      FLOOR_TEXT },  /* AI header activity word */
    /* KNOWN: Onyx Light 1.39; Moss & Mist 1.50 */
    { &s_c_ind_yellow, &s_bgp_base,      FLOOR_TEXT },
    /* KNOWN: Onyx Light 3.26; Sage & Sand 3.29; Moss & Mist 2.77 */
    { &s_c_ind_red,    &s_bgp_base,      FLOOR_TEXT },
    /* KNOWN: Onyx Light 1.73; Sage & Sand 4.26; Moss & Mist 1.80 */
    { &s_act_green,    &s_bgp_base,      FLOOR_TEXT },  /* activity line in the chat */
    /* KNOWN: Onyx Light 1.30; Sage & Sand 4.31; Moss & Mist 1.35 */
    { &s_act_yellow,   &s_bgp_base,      FLOOR_TEXT },
    /* KNOWN: Onyx Synapse 3.41; Onyx Light 2.60; Sage & Sand 2.44; Moss & Mist 2.15 */
    { &s_act_red,      &s_bgp_base,      FLOOR_TEXT },

    /* -- de-emphasised text (3.0) -- */
    { &s_text_dim,     &s_bgp_base,      FLOOR_DIM },   /* captions, about, nav headers */
    { &s_text_dim,     &s_bgs_base,      FLOOR_DIM },   /* unselected segment, AUTO off, menu accelerator */
    { &s_text_dim,     &s_bgs_hover,     FLOOR_DIM },   /* hovered unselected segment / AUTO */
    { &s_text_dim,     &s_raised_base,   FLOOR_DIM },   /* model chip in the AI header */
    { &s_text_dim,     &s_c_cmd_bg,      FLOOR_DIM },   /* held rows, held reason */
    { &s_text_dim,     &s_chip_on_bgp,   FLOOR_DIM },   /* dim chip on a settled command */
    { &s_text_dim,     &s_chip_on_cmd,   FLOOR_DIM },   /* "quoted" chip on a live row */
    { &s_text_disabled, &s_bgs_base,     FLOOR_DIM },   /* policy stop above the ceiling */
    { &s_text_disabled, &s_bgs_hover,    FLOOR_DIM },   /* the same, hot */
    /* KNOWN: Onyx Synapse 1.55; Onyx Light 1.59; Sage & Sand 1.63; Moss & Mist 2.19 */
    { &s_dlabel_acc,   &s_acc_disabled,  FLOOR_DIM },   /* disabled OK / Copy */
    /* KNOWN: Moss & Mist 2.38 */
    { &s_dlabel_raised, &s_raised_disabled, FLOOR_DIM },/* disabled Cancel */
    /* KNOWN: Moss & Mist 2.44 */
    { &s_dlabel_bgs,   &s_bgs_disabled,  FLOOR_DIM },   /* disabled Close */
    /* KNOWN: Onyx Light 1.48; Moss & Mist 1.48 */
    { &s_dlabel_ok,    &s_ok_disabled,   FLOOR_DIM },   /* disabled Run */
    /* KNOWN: Moss & Mist 2.95 */
    { &s_accel_sel,    &s_acc_base,      FLOOR_DIM },   /* menu accelerator on the selected row */

    /* -- strokes that identify a control (non-text, 3.0) -- */
    /* KNOWN: Onyx Synapse 1.16; Onyx Light 1.37; Sage & Sand 1.16; Moss & Mist 1.44 */
    { &s_border,       &s_bgs_base,      FLOOR_NONTEXT }, /* segmented/policy/AUTO outline */
    /* KNOWN: Onyx Synapse 1.19; Onyx Light 1.18; Sage & Sand 1.49; Moss & Mist 1.20 */
    { &s_border,       &s_c_cmd_bg,      FLOOR_NONTEXT }, /* unselected checkbox, Deny all */
};
#define N_PAIRS ((int)(sizeof(k_pairs) / sizeof(k_pairs[0])))
#define N_EXEMPT ((int)(sizeof(k_exempt) / sizeof(k_exempt[0])))

/* ---- The audit ------------------------------------------------------------ */

typedef struct {
    int checked;   /* theme x pair evaluations */
    int failed;    /* below the floor and not exempt */
    int stale;     /* exempt but now at or above the floor */
    int exempt;    /* below the floor and exempt */
    int orphan;    /* exemptions that matched no theme/pair at all */
} Audit;

static int exempt_for(const char *theme, const Pair *p)
{
    for (int i = 0; i < N_EXEMPT; i++) {
        if (k_exempt[i].theme && strcmp(k_exempt[i].theme, theme) == 0 &&
            strcmp(k_exempt[i].fg, p->fg->name) == 0 &&
            strcmp(k_exempt[i].bg, p->bg->name) == 0)
            return i;
    }
    return -1;
}

static void audit_run(Audit *a, int print)
{
    int seen[N_EXEMPT > 0 ? N_EXEMPT : 1];
    memset(seen, 0, sizeof(seen));
    memset(a, 0, sizeof(*a));

    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        ThemeTokens tok;
        ui_theme_resolve(base, &tok);

        for (int k = 0; k < N_PAIRS; k++) {
            const Pair *p = &k_pairs[k];
            double ratio = theme_contrast(p->fg->fn(&tok), p->bg->fn(&tok));
            int ex = exempt_for(base->name, p);
            a->checked++;
            if (ex >= 0) seen[ex] = 1;

            if (ratio >= p->floor) {
                if (ex >= 0) {
                    a->stale++;
                    if (print)
                        printf("  %s: %s on %s now reads %.2f:1 (floor %.2f); "
                               "remove its exemption\n", base->name,
                               p->fg->name, p->bg->name, ratio, p->floor);
                }
            } else if (ex >= 0) {
                a->exempt++;
            } else {
                a->failed++;
                if (print)
                    printf("  %s: %s on %s reads %.2f:1, needs %.2f\n",
                           base->name, p->fg->name, p->bg->name, ratio,
                           p->floor);
            }
        }
    }
    for (int i = 0; i < N_EXEMPT; i++) {
        if (!seen[i]) {
            a->orphan++;
            if (print)
                printf("  exemption %d (%s: %s on %s) matches no row\n", i,
                       k_exempt[i].theme ? k_exempt[i].theme : "(null)",
                       k_exempt[i].fg ? k_exempt[i].fg : "(null)",
                       k_exempt[i].bg ? k_exempt[i].bg : "(null)");
        }
    }
}

/* Every theme x pair meets its floor, bar the counted exemptions. */
int test_theme_contrast_every_pair_meets_its_floor(void)
{
    TEST_BEGIN();
    Audit a;
    audit_run(&a, 1);
    ASSERT_EQ(a.checked, NUM_UI_THEMES * N_PAIRS);
    ASSERT_EQ(a.failed, 0);
    ASSERT_EQ(a.stale, 0);
    TEST_END();
}

/* The exemption list is exact: its size is the declared count, every entry
 * names a real theme and row, and every entry still fails its floor. */
int test_theme_contrast_exemptions_are_exact(void)
{
    TEST_BEGIN();
    Audit a;
    audit_run(&a, 0);
    ASSERT_EQ(N_EXEMPT, EXPECTED_EXEMPTIONS);
    ASSERT_EQ(a.orphan, 0);
    ASSERT_EQ(a.stale, 0);
    ASSERT_EQ(a.exempt, EXPECTED_EXEMPTIONS);
    TEST_END();
}

/* The table itself: a floor is one of the three named ones, no (fg, bg)
 * repeats, and a pair is never a colour on itself. */
int test_theme_contrast_table_is_well_formed(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(N_PAIRS > 0);
    for (int i = 0; i < N_PAIRS; i++) {
        const Pair *p = &k_pairs[i];
        ASSERT_TRUE(p->floor == FLOOR_TEXT || p->floor == FLOOR_DIM ||
                    p->floor == FLOOR_NONTEXT);
        ASSERT_TRUE(strcmp(p->fg->name, p->bg->name) != 0);
        for (int j = i + 1; j < N_PAIRS; j++) {
            const Pair *q = &k_pairs[j];
            if (p->fg == q->fg && p->bg == q->bg)
                printf("  duplicate row: %s on %s\n", p->fg->name, p->bg->name);
            ASSERT_FALSE(p->fg == q->fg && p->bg == q->bg);
        }
    }
    TEST_END();
}
