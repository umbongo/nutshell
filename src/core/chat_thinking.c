/* src/core/chat_thinking.c */
#include "chat_thinking.h"

void chat_thinking_init(ThinkingState *state)
{
    state->phase = THINK_IDLE;
    state->elapsed_sec = 0.0f;
    state->start_time = 0.0f;
    state->collapsed = 1;
}

void chat_thinking_token(ThinkingState *state, float current_time)
{
    if (state->phase == THINK_IDLE) {
        state->start_time = current_time;
        state->phase = THINK_STREAMING;
    }
    state->elapsed_sec = current_time - state->start_time;
}

void chat_thinking_complete(ThinkingState *state, float current_time)
{
    if (state->phase == THINK_STREAMING) {
        state->elapsed_sec = current_time - state->start_time;
    }
    state->phase = THINK_COMPLETE;
}

int chat_thinking_toggle(ThinkingState *state)
{
    state->collapsed = !state->collapsed;
    return state->collapsed;
}

void chat_thinking_tick(ThinkingState *state, float current_time)
{
    if (state->phase == THINK_STREAMING) {
        state->elapsed_sec = current_time - state->start_time;
    }
}

void chat_thinking_reset(ThinkingState *state)
{
    chat_thinking_init(state);
}
