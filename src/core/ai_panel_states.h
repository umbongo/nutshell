#ifndef NUTSHELL_AI_PANEL_STATES_H
#define NUTSHELL_AI_PANEL_STATES_H

/*
 * ai_panel_states — the copy for the AI Assist panel's three "nothing to
 * show yet" states (empty thread, no API key, no connected session),
 * rendered by chat_listview when the thread has no items. A small,
 * testable core table so the copy itself (not just its layout) is
 * checked. See
 * docs/superpowers/specs/2026-09-07-ai-assist-panel-design.md
 * "Empty and blocked states (frame C)".
 */

#include <stddef.h>

typedef enum {
    AI_STATE_EMPTY,
    AI_STATE_NO_KEY,
    AI_STATE_NO_SESSION,
    AI_STATE_COUNT
} AiPanelStateId;

typedef struct {
    const char *title;
    const char *body_fmt;      /* AI_STATE_EMPTY's has a %s for the line count */
    const char *action_label;  /* NULL when the state has no button */
    int has_suggestions;       /* 1 for AI_STATE_EMPTY's three suggestion chips */
} AiPanelState;

/* Returns the state's static copy, or NULL for an out-of-range id. Never
 * crashes. */
const AiPanelState *ai_panel_state(AiPanelStateId id);

/* Formats the state's body into `buf`. For AI_STATE_EMPTY, `context_lines`
 * is substituted with thousands separators (e.g. 1000 -> "1,000"); other
 * states ignore `context_lines` and copy their body verbatim. Returns the
 * formatted length (like ai_thinking_summary: the length of what actually
 * ended up in `buf`). buf/cap==0 or an out-of-range id is a no-op
 * returning 0 (buf is left untouched when it's NULL). */
int ai_panel_state_body(AiPanelStateId id, int context_lines, char *buf, size_t cap);

/* The three suggestion chips for AI_STATE_EMPTY ("What's using disk?" etc).
 * Always exactly 3 non-empty strings; *count is set to 3. */
const char *const *ai_panel_suggestions(int *count);

#endif /* NUTSHELL_AI_PANEL_STATES_H */
