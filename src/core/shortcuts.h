#ifndef NUTSHELL_SHORTCUTS_H
#define NUTSHELL_SHORTCUTS_H

/*
 * shortcuts — the one table of every keyboard and mouse shortcut Nutshell
 * has (spec docs/superpowers/specs/2026-10-20-keyboard-cheat-sheet-design.md
 * section 2.1). Portable, no windows.h: the cheat sheet (src/ui/
 * cheatsheet_dlg.c) draws from it, the menu builder (create_app_menu() in
 * src/ui/window.c) takes every accelerator label from it, and README.md's
 * "Keyboard Shortcuts" block is generated from it and compared verbatim by
 * tests/test_shortcuts.c.
 *
 * A chord is a key combination written as the key is named on a US layout
 * ("Ctrl+=", "Shift+PgUp"). Alternatives that share an action and condition
 * sit in one row, separated by ", " ("Ctrl+Shift+V, Shift+Insert"); a row
 * with a non-zero menu_id holds exactly one chord, because it is that menu
 * item's label. The only placeholders are the literal words "key",
 * "letter" and "Arrows". A row flagged SC_MENU_PATH names a menu item
 * ("Edit > Select All") in the chord column instead.
 */

#include <stddef.h>

enum {
    SC_MENU_PATH      = 1u << 0, /* chord is a menu path, not keys */
    SC_SELECTION_SPEC = 1u << 1, /* true once the PuTTY-selection spec landed */
    SC_MENU_ACCEL     = 1u << 2  /* menu_id's label shows this chord */
};

typedef struct {
    const char *section;    /* "Sessions and tabs", ... */
    const char *chord;      /* "Ctrl+Shift+T", "Esc, Enter", or a menu path "Edit > Select All" */
    const char *action;     /* "New session (opens the Session Manager)" */
    const char *condition;  /* "" or "on the alternate screen (vim, less): sent to the program" */
    unsigned    flags;      /* SC_* */
    int         menu_id;    /* IDM_* whose menu label carries this chord, or 0 */
} Shortcut;

/* The rows in display order; sections are contiguous. Never NULL. */
const Shortcut *shortcuts_table(size_t *count);

/* The distinct section names in display order. Writes at most `cap` of
 * them to `out` (which may be NULL when cap is 0) and returns how many
 * there are. */
size_t shortcuts_sections(const char **out, size_t cap);

/* The chord the menu item `menu_id` shows as its accelerator, or "" when
 * no row names that id. Never NULL. */
const char *shortcuts_menu_accel(int menu_id);

/* The README.md block that sits between SHORTCUTS_README_BEGIN and
 * SHORTCUTS_README_END: one GitHub-flavoured markdown table per section,
 * LF line endings. Writes at most cap-1 bytes plus a NUL (snprintf
 * semantics; buf may be NULL when cap is 0) and returns the full length,
 * not counting the NUL. */
size_t shortcuts_readme_block(char *buf, size_t cap);

#define SHORTCUTS_README_BEGIN "<!-- shortcuts:begin -->"
#define SHORTCUTS_README_END   "<!-- shortcuts:end -->"

#endif /* NUTSHELL_SHORTCUTS_H */
