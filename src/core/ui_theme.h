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

#endif /* NUTSHELL_UI_THEME_H */
