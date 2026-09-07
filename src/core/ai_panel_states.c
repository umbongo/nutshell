#include "ai_panel_states.h"
#include <stdio.h>
#include <string.h>

static const AiPanelState g_states[AI_STATE_COUNT] = {
    [AI_STATE_EMPTY] = {
        "Ask about this session",
        "The assistant sees the last %s lines of your terminal and can run commands with your approval.",
        NULL,
        1
    },
    [AI_STATE_NO_KEY] = {
        "Add an API key to use the assistant",
        "Choose a provider and paste a key in Settings. Nothing is sent anywhere until you do.",
        "Open Settings",
        0
    },
    [AI_STATE_NO_SESSION] = {
        "Connect to a session first",
        "The assistant works on a live terminal. Open the Session Manager to connect.",
        "Open Session Manager",
        0
    },
};

const AiPanelState *ai_panel_state(AiPanelStateId id)
{
    if (id < 0 || id >= AI_STATE_COUNT) return NULL;
    return &g_states[id];
}

/* Formats a non-negative int with thousands separators into `out`
 * (e.g. 1000 -> "1,000", 500 -> "500"). Always NUL-terminates within
 * `cap`; `out`/`cap`==0 is a no-op. */
static void format_thousands(int n, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    if (n < 0) n = 0;

    char digits[16];
    int len = snprintf(digits, sizeof digits, "%d", n);
    if (len < 0) { out[0] = '\0'; return; }
    size_t ndigits = (size_t)len;

    char tmp[32];
    size_t ti = 0;
    for (size_t i = 0; i < ndigits && ti < sizeof(tmp) - 1; i++) {
        if (i > 0 && (ndigits - i) % 3 == 0) tmp[ti++] = ',';
        if (ti < sizeof(tmp) - 1) tmp[ti++] = digits[i];
    }
    tmp[ti] = '\0';

    snprintf(out, cap, "%s", tmp);
}

int ai_panel_state_body(AiPanelStateId id, int context_lines, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (id < 0 || id >= AI_STATE_COUNT) return 0;

    if (id == AI_STATE_EMPTY) {
        char n[32];
        format_thousands(context_lines, n, sizeof n);
        /* Literal format string (required to keep -Wformat-nonliteral
         * happy) with the pre-formatted "1,000"-style count substituted
         * via %s -- see CLAUDE.md's cross-compile pitfalls. */
        snprintf(buf, cap,
                 "The assistant sees the last %s lines of your terminal and can run commands with your approval.",
                 n);
    } else {
        const AiPanelState *st = ai_panel_state(id);
        snprintf(buf, cap, "%s", st->body_fmt);
    }
    return (int)strlen(buf);
}

static const char *const g_suggestions[3] = {
    "What's using disk?",
    "Why did that fail?",
    "Summarise the log"
};

const char *const *ai_panel_suggestions(int *count)
{
    if (count) *count = 3;
    return g_suggestions;
}
