#ifndef NUTSHELL_AI_CHAT_H
#define NUTSHELL_AI_CHAT_H

#ifdef _WIN32
#include <windows.h>
#include "term.h"
#include "ssh_channel.h"

/* Initialize the AI chat window class. Call once at startup. */
void ai_chat_init(HINSTANCE hInstance);

/* Show the AI chat window. If already open, brings to front.
 * api_key, provider, custom_url, custom_model are copied. */
HWND ai_chat_show(HWND parent, const char *api_key, const char *provider,
                  const char *custom_url, const char *custom_model);

/* Update the active session the AI interacts with. */
void ai_chat_set_session(HWND hwnd, Terminal *term, SSHChannel *channel);

/* Update the API key, provider, and custom URL/model (e.g. after settings change). */
void ai_chat_update_key(HWND hwnd, const char *api_key, const char *provider,
                        const char *custom_url, const char *custom_model);

/* Close and destroy the AI chat window. */
void ai_chat_close(HWND hwnd);

#endif /* _WIN32 */
#endif /* NUTSHELL_AI_CHAT_H */
