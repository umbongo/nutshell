#ifndef NUTSHELL_PROFILE_HELP_H
#define NUTSHELL_PROFILE_HELP_H

/*
 * profile_help -- the hover / F1 help of the session profile editor (the
 * Session Manager's form), as a portable table: one short text for every
 * field the user can edit. The dialog (src/ui/session_manager.c) maps its
 * control ids to these ids and shows `text` in the shared tip window, the way
 * the Settings dialog shows settings_help.c. Every string is plain ASCII (the
 * tip is ANSI) and written for a person, not a manual.
 */

typedef enum {
    PH_KIND, PH_NAME, PH_HOST, PH_PORT, PH_USER, PH_AUTH, PH_PLATFORM,
    PH_PASSWORD, PH_KEY, PH_BROWSE, PH_SHELL, PH_AI_NOTES,
    PH_COUNT
} ProfileHelpId;

typedef struct {
    ProfileHelpId id;
    const char   *name;    /* "PH_HOST" */
    const char   *label;   /* the field as the dialog names it, e.g. "Host" */
    const char   *text;    /* what the field does and what an empty field means */
} ProfileHelp;

/* The longest text, in bytes (terminator excluded): the tip is 360 px wide at
 * the dialog's size, so this is about eight lines. */
#define PROFILE_HELP_TEXT_MAX 360

const ProfileHelp *profile_help_get(int id);   /* NULL out of range */

#endif
