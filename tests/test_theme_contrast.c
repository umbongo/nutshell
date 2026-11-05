/* tests/test_theme_contrast.c -- every theme's text is readable on every
 * surface it is painted on.
 *
 * One table (k_pairs) lists each (text colour, surface colour, floor) that
 * src/ui actually paints, resolved through ui_theme_resolve() for all
 * NUM_UI_THEMES themes, and each pair is asserted against its floor with
 * theme_contrast() (the WCAG ratio).  A failure names the theme, the two
 * tokens and the ratio to two decimals.
 *
 * One owner per pair: what tests/test_ui_tokens.c already holds at 4.5
 * (text_main and text_dim on bg_primary, bg_secondary and raised; each
 * surface label on its base; text_dim_label on text_dim; the markdown role
 * colours on bg_primary) is not repeated here.  The terminal's ANSI palette
 * floors (tests/test_term_colors.c, tests/test_ui_theme.c) and the selection
 * highlight's visibility (tests/test_sel_contrast.c) are not repeated either.
 *
 * Every pair meets its floor on every theme, with one exception, listed in
 * k_exempt with its measured ratio: the unselected checkbox stroke (`border`
 * on `chat.cmd_bg`), which is faint by design (see k_exempt).  The list is a
 * ratchet: the measured ratio may not drop more than RATIO_SLACK below the
 * recorded one, and an exemption whose pair now meets its floor fails the
 * suite, so a fix has to delete its line.  The list is also counted
 * (EXPECTED_EXEMPTIONS), so it cannot grow unnoticed.  Never lower a floor or
 * add an exemption to get a new change through: a pair that fails on a
 * shipped theme is a finding to fix in the tokens, not a row to list.
 */
#include "test_framework.h"
#include "ui_theme.h"
#include "theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Floors --------------------------------------------------------------
 *
 * FLOOR_TEXT 4.5 (WCAG 1.4.3, AA, normal-size text): any text that carries
 *   information -- text_main, text_dim (it is painted as readable captions,
 *   held rows, hints), the `.label` of a surface on its .base/.hover/
 *   .pressed, intents painted as text (warning, link, the markdown roles),
 *   the chat colours and the activity indicators.  The UI draws these at
 *   11-14 px, never "large".
 * FLOOR_DIM 3.0: text that is dimmed on purpose and not the only carrier of
 *   its meaning -- text_disabled (a policy stop above the ceiling) and the
 *   accelerator column of the selected menu row.
 * FLOOR_NONTEXT 3.0 (WCAG 1.4.11): a glyph or stroke that identifies a
 *   control or state against the surface it sits on (the send/stop icon, the
 *   empty-state glyph, the hot checkbox outline).  Controls that carry a text
 *   label are judged by their text, not by their outline, and decorative
 *   separators are not tested.
 * Not tested: a disabled button's label.  WCAG 1.4.3 exempts inactive
 *   controls, and a row for it would only assert the 50% blend in
 *   ns_draw_button, not a readability promise. */
#define FLOOR_TEXT    4.5
#define FLOOR_DIM     3.0
#define FLOOR_NONTEXT 3.0

/* How far below its recorded ratio an exempt pair may measure before the
 * ratchet fails (recorded ratios are rounded to two decimals). */
#define RATIO_SLACK 0.01

/* ---- The one exemption ---------------------------------------------------
 *
 * The unselected checkbox stroke of an approval-card row (`border` on
 * `chat.cmd_bg`, chat_listview.c) reads 1.18-1.49:1 on the four themes, under
 * the 3.0 non-text floor, and stays so on purpose: the checkbox is identified
 * by its label (the command text beside it) and its states carry the
 * meaning: the hot stroke is accent.base (a row in the table below, at 3.0)
 * and the checked state is a success fill with a tick.  A resting stroke
 * strong enough to reach 3.0 would make every unticked row look like a
 * control in use.
 *
 * The entry is kept, and counted, so the exception cannot silently spread to
 * another pair: EXPECTED_EXEMPTIONS must equal the number of entries below,
 * and each entry matches a row on one theme by name.  Each entry is
 * { theme name, text token, surface token, measured ratio }. */
typedef struct {
    const char *theme, *fg, *bg;
    double      ratio;
} Exemption;

static const Exemption k_exempt[] = {
    /* Onyx Synapse */
    { "Onyx Synapse", "border", "chat.cmd_bg", 1.19 },
    /* Onyx Light */
    { "Onyx Light", "border", "chat.cmd_bg", 1.18 },
    /* Sage & Sand */
    { "Sage & Sand", "border", "chat.cmd_bg", 1.49 },
    /* Moss & Mist */
    { "Moss & Mist", "border", "chat.cmd_bg", 1.20 },
};
#define EXPECTED_EXEMPTIONS 4

/* ---- Blends --------------------------------------------------------------
 *
 * Packed 0x00RRGGBB, per channel, truncating, exactly as src/ui computes
 * them.  ablend() is rgb_alpha() (src/ui/ns_draw.c) -- fg * a + bg * (1 - a)
 * in float -- copied from tests/test_sel_contrast.c, whose own copy is
 * static; the integer blend is the pressed send/stop button's, written inline
 * in ai_chat.c. */
static unsigned int ablend(unsigned int fg, unsigned int bg, float a)
{
    unsigned int out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float f = (float)((fg >> sh) & 0xFF), b = (float)((bg >> sh) & 0xFF);
        out |= ((unsigned int)(f * a + b * (1.0f - a)) & 0xFF) << sh;
    }
    return out;
}

/* c * 4 / 5 per channel: the pressed send/stop button. */
static unsigned int darken_pressed(unsigned int c)
{
    unsigned int out = 0;
    for (int sh = 16; sh >= 0; sh -= 8)
        out |= ((((c >> sh) & 0xFF) * 4 / 5) & 0xFF) << sh;
    return out;
}

/* ---- Colour selectors ----------------------------------------------------
 *
 * A selector picks one colour out of a resolved token set.  Derived colours
 * are computed from the same tokens exactly as src/ui computes them. */
typedef unsigned int (*ColourFn)(const ThemeTokens *t);
typedef struct {
    const char *name;
    ColourFn    fn;
} Sel;

#define SEL(id, label, expr) \
    static unsigned int id##_f(const ThemeTokens *t) { (void)t; return (expr); } \
    static const Sel id = { label, id##_f }

/* plain tokens */
SEL(s_white,          "white",               0xFFFFFFu);
SEL(s_text_main,      "text_main",           t->text_main);
SEL(s_text_dim,       "text_dim",            t->text_dim);
SEL(s_text_disabled,  "text_disabled",       t->text_disabled);
SEL(s_border,         "border",              t->border);
SEL(s_term_fg,        "terminal_fg",         t->terminal_fg);
SEL(s_term_bg,        "terminal_bg",         t->terminal_bg);
/* surfaces: base / hover / pressed / label */
SEL(s_bgp_base,       "bg_primary.base",     t->bg_primary.base);
SEL(s_bgs_base,       "bg_secondary.base",   t->bg_secondary.base);
SEL(s_bgs_hover,      "bg_secondary.hover",  t->bg_secondary.hover);
SEL(s_bgs_pressed,    "bg_secondary.pressed", t->bg_secondary.pressed);
SEL(s_bgs_label,      "bg_secondary.label",  t->bg_secondary.label);
SEL(s_raised_base,    "raised.base",         t->raised.base);
SEL(s_raised_hover,   "raised.hover",        t->raised.hover);
SEL(s_raised_pressed, "raised.pressed",      t->raised.pressed);
SEL(s_raised_label,   "raised.label",        t->raised.label);
SEL(s_acc_base,       "accent.base",         t->accent.base);
SEL(s_acc_hover,      "accent.hover",        t->accent.hover);
SEL(s_acc_pressed,    "accent.pressed",      t->accent.pressed);
SEL(s_acc_label,      "accent.label",        t->accent.label);
SEL(s_ok_hover,       "success.hover",       t->success.hover);
SEL(s_ok_pressed,     "success.pressed",     t->success.pressed);
SEL(s_ok_label,       "success.label",       t->success.label);
SEL(s_warn_base,      "warning.base",        t->warning.base);
SEL(s_warn_hover,     "warning.hover",       t->warning.hover);
SEL(s_warn_label,     "warning.label",       t->warning.label);
SEL(s_err_hover,      "danger.hover",        t->danger.hover);
SEL(s_err_label,      "danger.label",        t->danger.label);
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
SEL(s_c_send,         "chat.send_btn",       t->chat.send_btn);
SEL(s_c_stop,         "chat.stop_btn",       t->chat.stop_btn);
SEL(s_c_send_down,    "chat.send_btn (pressed)", darken_pressed(t->chat.send_btn));
SEL(s_c_stop_down,    "chat.stop_btn (pressed)", darken_pressed(t->chat.stop_btn));

/* derived: the policy control (ns_draw_policy) fills a lit stop with the
 * intent over bg_secondary at 18% (30% hot) when AUTO is off. */
SEL(s_tint_ok,        "tint18(success on bg_secondary)",
    ablend(t->success.base, t->bg_secondary.base, 0.18f));
SEL(s_tint_warn,      "tint18(warning on bg_secondary)",
    ablend(t->warning.base, t->bg_secondary.base, 0.18f));
SEL(s_tint_err,       "tint18(danger on bg_secondary)",
    ablend(t->danger.base, t->bg_secondary.base, 0.18f));
SEL(s_tint_info,      "tint18(info on bg_secondary)",
    ablend(t->info.base, t->bg_secondary.base, 0.18f));
SEL(s_hot_ok,         "tint30(success on bg_secondary)",
    ablend(t->success.base, t->bg_secondary.base, 0.30f));
SEL(s_hot_warn,       "tint30(warning on bg_secondary)",
    ablend(t->warning.base, t->bg_secondary.base, 0.30f));
SEL(s_hot_err,        "tint30(danger on bg_secondary)",
    ablend(t->danger.base, t->bg_secondary.base, 0.30f));
SEL(s_hot_info,       "tint30(info on bg_secondary)",
    ablend(t->info.base, t->bg_secondary.base, 0.30f));
/* derived: the dim chip is text_dim at UI_DIM_TINT_ALPHA (12%, the table
 * header's alpha) over its panel (chat_listview.c). */
SEL(s_chip_on_bgp,    "tint12(text_dim on bg_primary)",
    ablend(t->text_dim, t->bg_primary.base, UI_DIM_TINT_ALPHA));
SEL(s_chip_on_cmd,    "tint12(text_dim on chat.cmd_bg)",
    ablend(t->text_dim, t->chat.cmd_bg, UI_DIM_TINT_ALPHA));
/* derived: a markdown table's header fill (md_render.c). */
SEL(s_hdr_fill,       "table header fill(text_dim 12% on bg_primary)",
    ablend(t->text_dim, t->bg_primary.base, 0.12f));
/* derived: selected text -- the selection hue (ui_theme_sel_hue) at 30% over
 * the surface (md_render.c MD_SEL_ALPHA). */
SEL(s_sel_on_bgp,     "sel30(bg_primary)",
    ablend(ui_theme_sel_hue(t, 0), t->bg_primary.base, 0.30f));
SEL(s_sel_on_bgs,     "sel30(bg_secondary)",
    ablend(ui_theme_sel_hue(t, 0), t->bg_secondary.base, 0.30f));
SEL(s_sel_on_cmd,     "sel30(chat.cmd_bg)",
    ablend(ui_theme_sel_hue(t, 0), t->chat.cmd_bg, 0.30f));
SEL(s_sel_on_user,    "sel30(chat.user_bubble)",
    ablend(ui_theme_sel_hue(t, 1), t->chat.user_bubble, 0.30f));
/* derived: the menu's accelerator on the selected row: label at 75% over
 * the accent (window.c WM_DRAWITEM). */
SEL(s_accel_sel,      "accelerator(accent.label at 75%)",
    ablend(t->accent.label, t->accent.base, 0.75f));
/* derived: markdown roles (md_render.c md_role_clr): the resolved tokens,
 * ui_theme_md_role() -- the link/info hue walked to 4.7:1 on every surface of
 * its role, or text_main when no shade reaches it. */
SEL(s_md_structure,   "md structure(link role)", t->md_structure);
SEL(s_md_code,        "md code(info role)",      t->md_code);

/* ---- The table -----------------------------------------------------------
 *
 * Each row is a pair src/ui paints (the site is in the comment).  A (fg, bg)
 * pair may not repeat (test_theme_contrast_table_is_well_formed). */
typedef struct {
    const Sel *fg, *bg;
    double     floor;
} Pair;

static const Pair k_pairs[] = {
    /* -- text_main (4.5) -- */
    { &s_text_main, &s_bgs_hover,    FLOOR_TEXT },  /* hovered tab */
    { &s_text_main, &s_raised_hover, FLOOR_TEXT },  /* hot suggestion chip */
    { &s_text_main, &s_c_cmd_bg,     FLOOR_TEXT },  /* approval card header, Deny all, hot cmd text */
    /* tab "L" glyph, logging on, is success.label on success.base: held at 4.5
     * by tests/test_ui_tokens.c, so it is not a row here. */
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

    /* -- surface labels on hover/pressed (4.5; base is test_ui_tokens.c's) -- */
    { &s_acc_label,    &s_acc_hover,     FLOOR_TEXT },  /* OK / Copy buttons, menu selection */
    { &s_acc_label,    &s_acc_pressed,   FLOOR_TEXT },
    { &s_raised_label, &s_raised_base,   FLOOR_TEXT },  /* Cancel button */
    { &s_raised_label, &s_raised_hover,  FLOOR_TEXT },
    { &s_raised_label, &s_raised_pressed, FLOOR_TEXT },
    { &s_bgs_label,    &s_bgs_base,      FLOOR_TEXT },  /* Close button */
    { &s_bgs_label,    &s_bgs_hover,     FLOOR_TEXT },
    { &s_bgs_label,    &s_bgs_pressed,   FLOOR_TEXT },
    { &s_ok_label,     &s_ok_hover,      FLOOR_TEXT },  /* Run button, policy */
    { &s_ok_label,     &s_ok_pressed,    FLOOR_TEXT },
    { &s_warn_label,   &s_warn_hover,    FLOOR_TEXT },  /* policy */
    { &s_err_label,    &s_err_hover,     FLOOR_TEXT },
    { &s_info_label,   &s_info_hover,    FLOOR_TEXT },
    { &s_bgp_base,     &s_acc_base,      FLOOR_TEXT },  /* settings nav / session list, selected row */

    /* -- chat block (4.5) -- */
    { &s_c_user_text,  &s_c_user_bubble, FLOOR_TEXT },  /* user message bubble */
    { &s_c_user_text,  &s_sel_on_user,   FLOOR_TEXT },  /* selected text in the bubble */
    { &s_c_cmd_text,   &s_c_cmd_bg,      FLOOR_TEXT },  /* command rows, detail dialog edit */
    { &s_c_ai_accent,  &s_bgp_base,      FLOOR_TEXT },  /* "AI" name next to the avatar */
    { &s_c_status,     &s_bgp_base,      FLOOR_TEXT },  /* status messages in the chat */

    /* -- intents painted as text (4.5) -- */
    { &s_warn_base,    &s_bgp_base,      FLOOR_TEXT },  /* detail dialog reason, settings URL warning */
    { &s_warn_base,    &s_c_cmd_bg,      FLOOR_TEXT },  /* approval row reason */
    { &s_link_base,    &s_bgp_base,      FLOOR_TEXT },  /* [Retry] */
    { &s_md_code,      &s_c_cmd_bg,      FLOOR_TEXT },  /* markdown inline code */
    { &s_md_code,      &s_sel_on_cmd,    FLOOR_TEXT },  /* selected inline code */
    { &s_md_structure, &s_sel_on_bgp,    FLOOR_TEXT },  /* selected heading / list marker */
    { &s_md_structure, &s_hdr_fill,      FLOOR_TEXT },  /* table header text */

    /* -- activity indicators painted as text (4.5): the AI header's activity
     *    word and the chat activity line (steady colour; only the dot pulses) -- */
    { &s_c_ind_green,  &s_bgp_base,      FLOOR_TEXT },  /* activity word and line */
    { &s_c_ind_yellow, &s_bgp_base,      FLOOR_TEXT },
    { &s_c_ind_red,    &s_bgp_base,      FLOOR_TEXT },

    /* -- text_dim painted as readable text (4.5; its plain surfaces are
     *    test_ui_tokens.c's) -- */
    { &s_text_dim,     &s_bgs_hover,     FLOOR_TEXT },  /* hot AUTO button */
    { &s_text_dim,     &s_c_cmd_bg,      FLOOR_TEXT },  /* held rows, held reason */
    { &s_text_dim,     &s_chip_on_bgp,   FLOOR_TEXT },  /* dim chip on a settled command */
    { &s_text_dim,     &s_chip_on_cmd,   FLOOR_TEXT },  /* "quoted" chip on a live row */
    { &s_text_dim,     &s_sel_on_bgs,    FLOOR_TEXT },  /* selected Thinking body */
    { &s_text_dim,     &s_sel_on_bgp,    FLOOR_TEXT },  /* selected blockquote */

    /* -- dimmed on purpose (3.0) -- */
    { &s_text_disabled, &s_bgs_base,     FLOOR_DIM },   /* policy stop above the ceiling */
    { &s_text_disabled, &s_bgs_hover,    FLOOR_DIM },   /* the same, hot */
    { &s_accel_sel,    &s_acc_base,      FLOOR_DIM },   /* menu accelerator on the selected row */

    /* -- glyphs and strokes that identify a control (non-text, 3.0) -- */
    { &s_white,        &s_c_send,        FLOOR_NONTEXT }, /* send icon (ai_chat.c) */
    { &s_white,        &s_c_send_down,   FLOOR_NONTEXT }, /* pressed */
    { &s_white,        &s_c_stop,        FLOOR_NONTEXT }, /* stop icon */
    { &s_white,        &s_c_stop_down,   FLOOR_NONTEXT }, /* pressed */
    { &s_acc_base,     &s_bgp_base,      FLOOR_NONTEXT }, /* empty-state glyph */
    { &s_acc_base,     &s_c_cmd_bg,      FLOOR_NONTEXT }, /* checkbox stroke, hot */
    { &s_border,       &s_c_cmd_bg,      FLOOR_NONTEXT }, /* checkbox stroke, unselected */
};
#define N_PAIRS ((int)(sizeof(k_pairs) / sizeof(k_pairs[0])))
#define N_EXEMPT ((int)(sizeof(k_exempt) / sizeof(k_exempt[0])))

/* ---- The audit ------------------------------------------------------------ */

typedef struct {
    int checked;   /* theme x pair evaluations */
    int failed;    /* below the floor and not exempt */
    int stale;     /* exempt but now at or above the floor */
    int dropped;   /* exempt and measuring below its recorded ratio */
    int exempt;    /* exempt and still failing, at its recorded ratio or up */
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

/* Diagnostics are printed only under the test that asserts on them:
 * PRINT_GATE for failed/stale/dropped, PRINT_EXEMPT for orphans. */
#define PRINT_GATE   1
#define PRINT_EXEMPT 2

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

            if (ex < 0) {
                if (ratio < p->floor) {
                    a->failed++;
                    if (print & PRINT_GATE)
                        printf("  %s: %s on %s reads %.2f:1, needs %.2f\n",
                               base->name, p->fg->name, p->bg->name, ratio,
                               p->floor);
                }
            } else if (ratio >= p->floor) {
                a->stale++;
                if (print & PRINT_GATE)
                    printf("  %s: %s on %s now reads %.2f:1 (floor %.2f); "
                           "remove its exemption\n", base->name,
                           p->fg->name, p->bg->name, ratio, p->floor);
            } else if (ratio < k_exempt[ex].ratio - RATIO_SLACK) {
                a->dropped++;
                if (print & PRINT_GATE)
                    printf("  %s: %s on %s fell to %.2f:1 from the recorded "
                           "%.2f:1\n", base->name, p->fg->name, p->bg->name,
                           ratio, k_exempt[ex].ratio);
            } else {
                a->exempt++;
            }
        }
    }
    for (int i = 0; i < N_EXEMPT; i++) {
        if (!seen[i]) {
            a->orphan++;
            if (print & PRINT_EXEMPT)
                printf("  exemption %d (%s: %s on %s) matches no row\n", i,
                       k_exempt[i].theme ? k_exempt[i].theme : "(null)",
                       k_exempt[i].fg ? k_exempt[i].fg : "(null)",
                       k_exempt[i].bg ? k_exempt[i].bg : "(null)");
        }
    }
}

/* Every theme x pair meets its floor, bar the exemptions, and no exempt
 * pair has got worse or has been fixed without its line being deleted. */
int test_theme_contrast_every_pair_meets_its_floor(void)
{
    TEST_BEGIN();
    Audit a;
    audit_run(&a, PRINT_GATE);
    ASSERT_EQ(a.checked, NUM_UI_THEMES * N_PAIRS);
    ASSERT_EQ(a.failed, 0);
    ASSERT_EQ(a.stale, 0);
    ASSERT_EQ(a.dropped, 0);
    TEST_END();
}

/* The exemption list is exact: its size is the declared count, and every
 * entry names a real theme and row. */
int test_theme_contrast_exemptions_are_exact(void)
{
    TEST_BEGIN();
    Audit a;
    audit_run(&a, PRINT_EXEMPT);
    if (N_EXEMPT != EXPECTED_EXEMPTIONS)
        printf("  %d exemptions listed, EXPECTED_EXEMPTIONS is %d\n",
               N_EXEMPT, EXPECTED_EXEMPTIONS);
    ASSERT_EQ(N_EXEMPT, EXPECTED_EXEMPTIONS);
    ASSERT_EQ(a.orphan, 0);
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
        ASSERT_TRUE(fabs(p->floor - FLOOR_TEXT) < 1e-9 ||
                    fabs(p->floor - FLOOR_DIM) < 1e-9 ||
                    fabs(p->floor - FLOOR_NONTEXT) < 1e-9);
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
