#ifndef NUTSHELL_THINKING_MODE_H
#define NUTSHELL_THINKING_MODE_H

#include <stddef.h>
#include "ui_theme.h"

/* How a Thinking row shows the model's reasoning, and the saved preference
 * behind it.  Pure logic, native-tested; see
 * docs/superpowers/specs/2026-11-12-thinking-row-modes-design.md. */

typedef enum {
    THINK_MODE_COLLAPSED = 0,
    THINK_MODE_COMPACT   = 1,
    THINK_MODE_OPEN      = 2,
    THINK_MODE_EXTENDED  = 3   /* appended: the ints are never persisted */
} ThinkMode;

typedef enum {
    THINK_PREF_AUTO      = 0,
    THINK_PREF_COLLAPSED = 1,
    THINK_PREF_COMPACT   = 2,
    THINK_PREF_OPEN      = 3,
    THINK_PREF_EXTENDED  = 4
} ThinkPref;

/* The highest valid ThinkPref; the settings combo index equals the value. */
#define THINK_PREF_MAX THINK_PREF_EXTENDED

/* Lines the compact box shows (the last ones). */
#define THINK_COMPACT_LINES 4
/* Lines the extended box shows: compact's behaviour, a taller window. */
#define THINK_EXTENDED_LINES 25

/* collapsed -> compact -> extended -> open -> collapsed; out of range acts as collapsed. */
ThinkMode think_mode_next(ThinkMode m);

/* AUTO: extended while streaming, collapsed when idle.  Any other pref gives
 * its own mode in both states.  Out of range pref acts as AUTO. */
ThinkMode think_mode_effective(ThinkPref pref, int streaming);

/* The sticky pref a click stores.  Out of range gives THINK_PREF_COLLAPSED. */
ThinkPref think_pref_from_mode(ThinkMode m);

/* Config tokens "auto", "collapsed", "compact", "open", "extended".  Name of an out of
 * range pref is "auto"; NULL, unknown or wrong-case text parses as AUTO. */
const char *think_pref_name(ThinkPref p);
ThinkPref   think_pref_parse(const char *s);

/* Height cap of the body box: 0 collapsed, THINK_COMPACT_LINES * line_h
 * compact, THINK_EXTENDED_LINES * line_h extended, open_cap open. */
int think_mode_body_cap(ThinkMode m, int line_h, int open_cap);

/* 1 for the modes that show the last N lines and follow the stream (compact
 * and extended): no scrollbar, trailing blank lines trimmed.  0 otherwise. */
int think_mode_follows_tail(ThinkMode m);

/* 1 when the wheel over the mode's body scrolls the body itself (compact,
 * extended, open), 0 for collapsed (no body) and out-of-range modes. */
int think_mode_wheel_scrolls(ThinkMode m);

/* The box's scroll offset after its content or size changed (streamed text,
 * a mode change, a resize), by the chat list's stick-to-bottom rule
 * (stick_scroll_on_layout): stick (1 = the reader is at the bottom) follows
 * the tail, otherwise the offset is kept, clamped to the new range.  full_h
 * is the wrapped text height, vis_h the box's visible height.  Collapsed
 * (and an out-of-range mode) has no body: 0. */
int think_box_settle_scroll(ThinkMode m, int stick, int scroll_y, int full_h,
                            int vis_h);

/* Scroll offset that puts the last lines in view: max(0, full_h - vis_h). */
int think_compact_offset(int full_h, int vis_h);

/* Length of s[0..len) without trailing whitespace (space, tab, CR, LF, FF,
 * VT).  The compact box ends at the last visible text, not at blank lines.
 * NULL gives 0. */
size_t think_trimmed_len(const char *s, size_t len);

/* The label colour for the mode (each of the four modes has its own token; collapsed is the quietest, open the strongest).
 * An out of range mode picks the collapsed one. */
unsigned int think_mode_label_colour(const ThemeChatColors *chat, ThinkMode m);

#endif
