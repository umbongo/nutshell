/* src/core/dlog_types.h -- the enums every dlog user shares.
 *
 * Kept tiny and free of other includes: config.h pulls this in for the
 * per-category level array in Settings. See dlog.h for the logger itself and
 * docs/superpowers/specs/2026-10-17-debug-logging-design.md for the design.
 */
#ifndef NUTSHELL_CORE_DLOG_TYPES_H
#define NUTSHELL_CORE_DLOG_TYPES_H

/* The six categories (spec section 1). Order is the order of the Settings
 * rows and of the "levels=" list in the log header. */
typedef enum {
    DLOG_RENDER = 0,   /* "render"  paint, invalidation, cooldown, scrollbar   */
    DLOG_TERM,         /* "term"    VT parser, buffer, alt screen, bytes       */
    DLOG_AI,           /* "ai"      requests, streams, tools, dispatcher       */
    DLOG_SSH,          /* "ssh"     connect stages, host key, auth, channel    */
    DLOG_SHELL,          /* "shell"     local shell resolver, ConPTY, child exit   */
    DLOG_GENERAL,      /* "general" startup, settings, menus, the logger       */
    DLOG_CAT_COUNT
} DlogCat;

/* A per-category setting, as stored in the config and shown in Settings.
 * Each level admits itself and everything above it (Errors admits ERROR and
 * WARN; Trace admits everything). */
typedef enum {
    DLOG_LVL_OFF = 0,
    DLOG_LVL_ERRORS,
    DLOG_LVL_INFO,
    DLOG_LVL_DEBUG,
    DLOG_LVL_TRACE,
    DLOG_LVL_COUNT
} DlogLevel;

/* The level of one event. Lower is more severe, so "event <= threshold"
 * is the whole filter. DLOG_ISSUE (0) passes every threshold, Off included:
 * it is only ever written by the issue marker and the logger itself. */
typedef enum {
    DLOG_ISSUE = 0,
    DLOG_ERROR = 1,
    DLOG_WARN  = 2,
    DLOG_INFO  = 3,
    DLOG_DEBUG = 4,
    DLOG_TRACE = 5
} DlogEvLevel;

#define DLOG_DEFAULT_LEVEL DLOG_LVL_INFO

/* Category <-> config/log word ("render", "term", "ai", "ssh", "shell",
 * "general"). Name lookup is case-insensitive; -1 when unknown or NULL. */
const char *dlog_cat_name(int cat);
int         dlog_cat_from_name(const char *name);

/* DlogLevel <-> config word ("off", "errors", "info", "debug", "trace").
 * Lookup is case-insensitive; -1 when unknown or NULL. */
const char *dlog_level_name(int level);
int         dlog_level_from_name(const char *name);

/* The highest event level a setting admits: Off 0, Errors 2 (ERROR, WARN),
 * Info 3, Debug 4, Trace 5. Out-of-range settings clamp. */
int         dlog_level_max_event(int level);

/* Event level <-> the padded 5-character tag in the file ("ERROR", "WARN ",
 * "INFO ", "DEBUG", "TRACE", "ISSUE") and the AI tool's words ("error",
 * "warn", "info", "debug", "trace"). The tag lookup trims trailing spaces;
 * -1 when unknown. */
const char *dlog_ev_level_tag(int ev);
int         dlog_ev_level_from_tag(const char *tag);
int         dlog_ev_level_from_word(const char *word);

#endif /* NUTSHELL_CORE_DLOG_TYPES_H */
