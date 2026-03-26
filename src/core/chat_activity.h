/* src/core/chat_activity.h */
#ifndef NUTSHELL_CHAT_ACTIVITY_H
#define NUTSHELL_CHAT_ACTIVITY_H

#include <stddef.h>

typedef enum {
    ACTIVITY_IDLE,
    ACTIVITY_PROCESSING,
    ACTIVITY_THINKING,
    ACTIVITY_RESPONDING,
    ACTIVITY_EXECUTING,
    ACTIVITY_WAITING
} ActivityPhase;

typedef enum {
    HEALTH_GREEN,    /* 0-10s since last token */
    HEALTH_YELLOW,   /* 10-30s */
    HEALTH_RED,      /* 30s+ or connection lost */
} HealthStatus;

typedef struct {
    ActivityPhase phase;
    HealthStatus health;
    float last_token_time;   /* timestamp of last received token */
    float phase_start_time;  /* when current phase started */
    int exec_current;        /* current command index (1-based) */
    int exec_total;          /* total commands to execute */
    int connection_lost;     /* 1 if connection detected lost */
} ActivityState;

/* Initialize activity state to idle. */
void chat_activity_init(ActivityState *state);

/* Set phase. current_time is monotonic seconds. */
void chat_activity_set_phase(ActivityState *state, ActivityPhase phase, float current_time);

/* Record a token arrival. Resets health timer. */
void chat_activity_token(ActivityState *state, float current_time);

/* Heartbeat tick: update health status based on elapsed time since last token.
 * Call every ~1 second. */
void chat_activity_tick(ActivityState *state, float current_time);

/* Set execution progress (for ACTIVITY_EXECUTING phase). */
void chat_activity_set_exec(ActivityState *state, int current, int total);

/* Mark connection as lost. */
void chat_activity_connection_lost(ActivityState *state);

/* Reset to idle. */
void chat_activity_reset(ActivityState *state);

/* Format inline status text into buf. current_time is monotonic seconds
 * (used to compute elapsed time for thinking phase). Returns bytes written. */
int chat_activity_format(const ActivityState *state, float current_time,
                         char *buf, size_t buf_size);

#endif /* NUTSHELL_CHAT_ACTIVITY_H */
