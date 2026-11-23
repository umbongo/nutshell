#ifndef NUTSHELL_AI_GATE_H
#define NUTSHELL_AI_GATE_H

/*
 * ai_gate -- the runtime "is the AI assistant on" flag that worker threads
 * (WinHTTP), the Help > Keyboard Shortcuts sheet and the tab strip read.
 * Portable, no windows.h. Settings.ai_enabled is the stored choice; the
 * window mirrors it here with ai_gate_set_enabled(). The --ui-demo override
 * turns the gate on without ever touching Settings. All three functions are
 * safe to call from any thread.
 */

void ai_gate_set_enabled(int on);
void ai_gate_set_demo_override(int on);

/* enabled || demo override. Off until the window sets one. */
int ai_gate_on(void);

#endif /* NUTSHELL_AI_GATE_H */
