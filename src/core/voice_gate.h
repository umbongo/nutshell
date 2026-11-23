#ifndef NUTSHELL_VOICE_GATE_H
#define NUTSHELL_VOICE_GATE_H

/*
 * voice_gate -- the runtime "is voice input on" flag, mirroring ai_gate.h.
 * Portable, no windows.h. Settings.voice_enabled is the stored choice; the
 * window mirrors it here. The --ui-demo override turns the gate on without
 * touching Settings. Voice needs the AI assistant, so the gate is on only
 * while ai_gate_on() is too. Safe to call from any thread.
 */

void voice_gate_set_enabled(int on);
void voice_gate_set_demo_override(int on);

/* (enabled || demo override) && ai_gate_on(). Off until the window sets one. */
int voice_gate_on(void);

#endif /* NUTSHELL_VOICE_GATE_H */
