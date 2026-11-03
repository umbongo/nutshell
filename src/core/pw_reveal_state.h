/* src/core/pw_reveal_state.h -- hold-to-reveal state machine for the eye
 * button on password fields. Pure logic, no Win32: the UI feeds it events
 * and shows the text in the clear only while `revealed` is 1. */
#ifndef NUTSHELL_PW_REVEAL_STATE_H
#define NUTSHELL_PW_REVEAL_STATE_H

typedef enum {
    PWR_PRESS,        /* mouse/key down on the button */
    PWR_RELEASE,      /* mouse/key up */
    PWR_LEAVE,        /* pointer left the button while held */
    PWR_ENTER,        /* pointer came back */
    PWR_CAPTURE_LOST, /* mouse capture taken away */
    PWR_DEACTIVATE,   /* window lost activation */
    PWR_EMPTIED,      /* the field became empty */
    PWR_DESTROY       /* the control is going away */
} PwRevealEvent;

typedef struct {
    int pressed;
    int inside;
    int revealed;
} PwRevealState;

void pw_reveal_init(PwRevealState *s);

/* Returns 1 when s->revealed changed, else 0. NULL s returns 0. */
int pw_reveal_feed(PwRevealState *s, PwRevealEvent ev, int field_has_text);

#endif
