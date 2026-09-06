#ifndef NUTSHELL_UI_THEME_H
#define NUTSHELL_UI_THEME_H

/* Onyx Synapse UI theme system — 4 curated colour schemes for all UI chrome.
 *
 * Colours are packed 0x00RRGGBB (same as theme.h and terminal emulator).
 * Pure C, no Win32 dependency — fully testable on Linux.
 */

#define NUM_UI_THEMES 4

typedef struct {
    unsigned int user_bubble;        /* User bubble background */
    unsigned int user_text;          /* User bubble text */
    unsigned int ai_accent;          /* AI avatar and name color */
    unsigned int cmd_bg;             /* Command block background */
    unsigned int cmd_border;         /* Command block border */
    unsigned int cmd_text;           /* Command text (monospace) */
    unsigned int thinking_border;    /* Thinking region left border */
    unsigned int thinking_text;      /* Thinking content text */
    unsigned int status_text;        /* Status message text */
    unsigned int indicator_green;    /* Healthy activity dot */
    unsigned int indicator_yellow;   /* Slow activity dot */
    unsigned int indicator_red;      /* Stalled activity dot */
    unsigned int send_btn;           /* Send button background (pastel blue) */
    unsigned int stop_btn;           /* Stop button background (pastel red) */
} ThemeChatColors;

/* Explicit per-theme overrides for a handful of derived tokens.  0 means
 * "derive it" (see ui_theme_resolve()).  Kept deliberately small; more
 * fields can be added here as later tasks need to override other derived
 * values. */
typedef struct {
    unsigned int raised; /* override for bg_secondary shifted one step */
    unsigned int focus;  /* override for accent-as-focus-ring colour */
} ThemeOverrides;

typedef struct {
    const char  *name;          /* "Onyx Synapse", "Onyx Light", etc. */
    unsigned int bg_primary;    /* Main window / dialog background */
    unsigned int bg_secondary;  /* Panels, inactive tabs, input fields */
    unsigned int accent;        /* Buttons, active indicators, links */
    unsigned int text_main;     /* Primary text */
    unsigned int text_dim;      /* Secondary / muted text */
    unsigned int border;        /* Subtle borders and separators */
    unsigned int terminal_fg;   /* Terminal default foreground */
    unsigned int terminal_bg;   /* Terminal default background */
    unsigned int success;       /* Intent: success / allow */
    unsigned int warning;       /* Intent: warning / write commands */
    unsigned int danger;        /* Intent: danger / deny / critical */
    unsigned int info;          /* Intent: informational (was [EXEC] purple) */
    unsigned int link;          /* Intent: hyperlink-style text */
    ThemeOverrides overrides;   /* 0 fields = derive (see ui_theme_resolve) */
    ThemeChatColors chat;       /* Chat panel colors */
} ThemeColors;

/* Get the built-in theme by index (0..NUM_UI_THEMES-1).
 * Out-of-range indices return theme 0.  Never returns NULL. */
const ThemeColors *ui_theme_get(int index);

/* Find a theme by name (case-sensitive).
 * Returns the index, or 0 if not found. */
int ui_theme_find(const char *name);

/* Get the display name for theme at index.
 * Out-of-range indices return theme 0's name. */
const char *ui_theme_name(int index);

/* ---- Resolved design tokens (Design-System Foundation, section 1) ------- */

/* Perceptual-lightness (CIE L*) delta of "one step" used by every derived
 * interaction state (hover, pressed, raised).  Pressed uses two steps.
 * A fixed relative-luminance delta would be a huge jump on a near-black
 * surface and invisible on a light one; 6 L* units read as the same change
 * on every theme. */
#define UI_THEME_STEP 6.0 /* L* units */

/* A colour plus its computed interaction states.  `base` is always the
 * source colour a ThemeSurface was derived from.  `label` is the text
 * colour to draw on `base`: whichever of white, bg_primary or text_main
 * has the highest contrast against it. */
typedef struct {
    unsigned int base, hover, pressed, disabled, label;
} ThemeSurface;

/* Fully resolved, ready-to-paint token set.  UI code should read only this
 * struct and never compute a colour itself. */
typedef struct {
    ThemeSurface bg_primary, bg_secondary, raised, accent;
    ThemeSurface success, warning, danger, info, link;
    unsigned int text_main, text_dim, text_disabled, border, focus;
    unsigned int terminal_fg, terminal_bg;
    ThemeChatColors chat;   /* copied through unchanged for the migration window */
    int is_dark;
} ThemeTokens;

/* Resolve `base` into a complete token set.  Call once, when the theme is
 * chosen or changed; `out` may then be read from freely by UI code. */
void ui_theme_resolve(const ThemeColors *base, ThemeTokens *out);

#endif /* NUTSHELL_UI_THEME_H */
