/* src/core/chat_activity.c */
#include "chat_activity.h"
#include <stdio.h>
#include <string.h>

void chat_activity_init(ActivityState *state)
{
    memset(state, 0, sizeof(*state));
    state->phase = ACTIVITY_IDLE;
    state->health = HEALTH_GREEN;
}

void chat_activity_set_phase(ActivityState *state, ActivityPhase phase, float current_time)
{
    state->phase = phase;
    state->phase_start_time = current_time;
    if (phase == ACTIVITY_PROCESSING) {
        state->last_token_time = current_time;
        state->health = HEALTH_GREEN;
        state->connection_lost = 0;
    }
}

void chat_activity_token(ActivityState *state, float current_time)
{
    state->last_token_time = current_time;
    state->health = HEALTH_GREEN;
}

void chat_activity_tick(ActivityState *state, float current_time)
{
    if (state->phase == ACTIVITY_IDLE) return;
    if (state->connection_lost) { state->health = HEALTH_RED; return; }

    float elapsed = current_time - state->last_token_time;
    if (elapsed >= 30.0f)
        state->health = HEALTH_RED;
    else if (elapsed >= 10.0f)
        state->health = HEALTH_YELLOW;
    else
        state->health = HEALTH_GREEN;
}

void chat_activity_set_exec(ActivityState *state, int current, int total)
{
    state->exec_current = current;
    state->exec_total = total;
}

void chat_activity_connection_lost(ActivityState *state)
{
    state->connection_lost = 1;
    state->health = HEALTH_RED;
}

void chat_activity_reset(ActivityState *state)
{
    chat_activity_init(state);
}

int chat_activity_format(const ActivityState *state, float current_time,
                         char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;

    if (state->connection_lost)
        return (int)snprintf(buf, buf_size, "Connection lost");

    if (state->health == HEALTH_RED && state->phase != ACTIVITY_IDLE) {
        return (int)snprintf(buf, buf_size, "Stalled - no response");
    }

    const char *slow = (state->health == HEALTH_YELLOW) ? " (slow)" : "";

    float phase_elapsed = current_time - state->phase_start_time;

    switch (state->phase) {
    case ACTIVITY_IDLE:
        return (int)snprintf(buf, buf_size, "Idle");
    case ACTIVITY_PROCESSING:
        return (int)snprintf(buf, buf_size, "Processing...%s", slow);
    case ACTIVITY_THINKING:
        if (phase_elapsed >= 1.0f)
            return (int)snprintf(buf, buf_size, "Thinking (%.1fs)%s",
                                 (double)phase_elapsed, slow);
        return (int)snprintf(buf, buf_size, "Thinking...%s", slow);
    case ACTIVITY_RESPONDING:
        return (int)snprintf(buf, buf_size, "Responding...%s", slow);
    case ACTIVITY_EXECUTING:
        return (int)snprintf(buf, buf_size, "Executing %d/%d...%s",
                             state->exec_current, state->exec_total, slow);
    case ACTIVITY_WAITING:
        return (int)snprintf(buf, buf_size, "Waiting for output...%s", slow);
    }
    return 0;
}
