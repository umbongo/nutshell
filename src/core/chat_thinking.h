/* src/core/chat_thinking.h */
#ifndef NUTSHELL_CHAT_THINKING_H
#define NUTSHELL_CHAT_THINKING_H

#include "chat_msg.h"

typedef enum {
    THINK_IDLE,       /* No active thinking */
    THINK_STREAMING,  /* Receiving thinking tokens */
    THINK_COMPLETE    /* Thinking finished */
} ThinkingPhase;

typedef struct {
    ThinkingPhase phase;
    float elapsed_sec;      /* Seconds since first thinking token */
    float start_time;       /* Timestamp (seconds) of first token */
    int collapsed;          /* Current collapse state */
} ThinkingState;

/* Initialize thinking state. */
void chat_thinking_init(ThinkingState *state);

/* Called when a thinking token arrives. current_time in seconds (monotonic).
 * Updates elapsed time and phase. */
void chat_thinking_token(ThinkingState *state, float current_time);

/* Called when thinking is complete. Finalizes elapsed time. */
void chat_thinking_complete(ThinkingState *state, float current_time);

/* Toggle collapsed state. Returns the new collapsed value (0 or 1). */
int chat_thinking_toggle(ThinkingState *state);

/* Update elapsed time display (call periodically during streaming).
 * current_time in seconds. */
void chat_thinking_tick(ThinkingState *state, float current_time);

/* Reset to idle state. */
void chat_thinking_reset(ThinkingState *state);

#endif /* NUTSHELL_CHAT_THINKING_H */
