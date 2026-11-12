#ifndef NUTSHELL_SETTINGS_HELP_H
#define NUTSHELL_SETTINGS_HELP_H

#include <stddef.h>

/*
 * settings_help -- the Settings dialog's help table, as a portable
 * (Win32-free) module: for every control the label the dialog shows, the
 * heading and detailed hover text, the default and the numeric range. The
 * dialog takes each label from here and shows `text` as the control's tooltip;
 * the native tests check the defaults and ranges against the real ones.
 * Every string is ASCII (the dialog's tooltip is ANSI).
 */

typedef enum {
    SH_NONE = -1,
    SH_COLOUR_SCHEME, SH_TERMINAL_FONT, SH_FONT_SIZE, SH_AI_FONT,      /* Appearance */
    SH_SCROLLBACK, SH_PASTE_DELAY, SH_PASTE_CONFIRM,                   /* Terminal */
    SH_LOG_DIR, SH_LOG_FORMAT,                                         /* Logging */
    SH_SSH_IDLE,                                                       /* SSH */
    SH_SESSION_MGR_STARTUP, SH_AUTOCONNECT, SH_AUTOCONNECT_SESSION,    /* Startup */
    SH_AI_PROVIDER, SH_AI_KEY, SH_AI_MODEL, SH_AI_REFRESH, SH_AI_BASE_URL, /* Provider */
    SH_AI_RUNCMD_MODE,
    SH_AI_CONTEXT_LINES, SH_AI_SYSTEM_NOTES, SH_AI_MARKDOWN,           /* Behaviour */
    SH_AI_THINKING_MODE,
    SH_AI_POLICY_ALLOWED, SH_AI_POLICY_AUTO, SH_AI_TOOL_TIMEOUT, SH_AI_TOOL_ROUNDS,
    SH_AI_WEB_FETCH, SH_AI_SEARCH, SH_AI_SEARCH_URL, SH_AI_MAX_RESULTS, /* Web Access */
    SH_DBG_ENABLE, SH_DBG_LEVEL_RENDER, SH_DBG_LEVEL_TERM, SH_DBG_LEVEL_AI, /* Debugging */
    SH_DBG_LEVEL_SSH, SH_DBG_LEVEL_SHELL, SH_DBG_LEVEL_GENERAL,
    SH_DBG_RECORDER, SH_DBG_SCREENSHOT, SH_DBG_CONTENT, SH_DBG_FOLDER,
    SH_DBG_BROWSE, SH_DBG_OPEN, SH_DBG_MAX_FILE, SH_DBG_MAX_TOTAL, SH_DBG_USAGE,
    SH_DBG_CURRENT,
    SH_COUNT
} SettingsHelpId;

typedef enum { SH_KIND_SETTING, SH_KIND_ACTION, SH_KIND_INFO } SettingsHelpKind;

/* SettingsHelp.flags */
#define SH_F_ZERO_MEANS_NEVER   1u  /* 0 is also valid and means "never" */
#define SH_F_THOUSANDS          2u  /* numbers are written with thousands separators */
#define SH_F_KEEP_OUT_OF_RANGE  4u  /* an out-of-range entry is not saved */

typedef struct {
    SettingsHelpId   id;
    const char      *name;          /* "SH_COLOUR_SCHEME", via #id in the table macro */
    int              page;          /* SETTINGS_PAGE_* */
    SettingsHelpKind kind;
    const char      *label;         /* exactly as the dialog shows it */
    const char      *title;         /* tip heading when the label is a fragment, else NULL */
    const char      *text;          /* heading "\n\n" body ["\n" "Default: ..."] */
    const char      *default_text;  /* what follows "Default: ", NULL for actions/info */
    int              min, max;      /* numeric range, or 0, 0 */
    unsigned         flags;         /* SH_F_ZERO_MEANS_NEVER, SH_F_THOUSANDS, SH_F_KEEP_OUT_OF_RANGE */
} SettingsHelp;

#define SETTINGS_HELP_TEXT_MAX 700  /* bytes of `text`, terminator excluded */

const SettingsHelp *settings_help_get(int id);          /* NULL out of range */
const SettingsHelp *settings_help_table(size_t *count); /* dialog order */

#endif /* NUTSHELL_SETTINGS_HELP_H */
