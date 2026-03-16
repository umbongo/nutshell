#ifndef NUTSHELL_AI_CHAT_H
#define NUTSHELL_AI_CHAT_H

#ifdef _WIN32
#include <windows.h>
#include "term.h"
#include "ssh_channel.h"
#include "ai_prompt.h"

/* Initialize the AI assist window class. Call once at startup. */
void ai_chat_init(HINSTANCE hInstance);

/* Show the AI assist window. If already open, brings to front.
 * api_key, provider, custom_url, custom_model, font_name, colour_scheme copied.
 * paste_delay_ms: inter-command delay when executing batched commands.
 * session_notes, system_notes: optional AI context notes (may be NULL).
 * initial_state: per-session AI state (may be NULL for fresh conversation). */
HWND ai_chat_show(HWND parent, const char *api_key, const char *provider,
                  const char *custom_url, const char *custom_model,
                  int paste_delay_ms, const char *font_name,
                  const char *colour_scheme,
                  const char *session_notes, const char *system_notes,
                  AiSessionState *initial_state,
                  const char *session_name);

/* Switch the AI chat to a different session's conversation.
 * Saves the current conversation, loads the new one, rebuilds the display.
 * If busy with an API call, the switch is deferred until the call completes.
 * session_name: profile name shown in the chat header (may be NULL). */
void ai_chat_switch_session(HWND hwnd,
                            AiSessionState *new_state,
                            Terminal *term, SSHChannel *channel,
                            const char *session_notes,
                            const char *system_notes,
                            const char *session_name);

/* Notify the chat window that a session is being closed.
 * Clears any internal pointers to the dying session's state. */
void ai_chat_notify_session_closed(HWND hwnd, AiSessionState *state);

/* Update the active session's terminal/channel pointers (without switching conversation). */
void ai_chat_set_session(HWND hwnd, Terminal *term, SSHChannel *channel);

/* Update the API key, provider, and custom URL/model (e.g. after settings change). */
void ai_chat_update_key(HWND hwnd, const char *api_key, const char *provider,
                        const char *custom_url, const char *custom_model);

/* Update session-specific and system-wide AI notes.
 * Either may be NULL to leave unchanged. */
void ai_chat_update_notes(HWND hwnd, const char *session_notes,
                          const char *system_notes);

/* Update the colour scheme / theme of an open AI chat window. */
void ai_chat_set_theme(HWND hwnd, const char *colour_scheme);

/* Close and destroy the AI assist window. */
void ai_chat_close(HWND hwnd);

#endif /* _WIN32 */
#endif /* NUTSHELL_AI_CHAT_H */
