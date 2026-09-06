#ifndef NUTSHELL_AI_CHAT_H
#define NUTSHELL_AI_CHAT_H

#ifdef _WIN32
#include <windows.h>
#include "term.h"
#include "ssh_channel.h"
#include "ai_prompt.h"
#include "chat_approval.h"

/* Initialize the AI assist window class. Call once at startup. */
void ai_chat_init(HINSTANCE hInstance);

/* Show the AI assist window. If already open, brings to front.
 * api_key, provider, custom_url, custom_model, font_name, ai_font, colour_scheme copied.
 * paste_delay_ms: inter-command delay when executing batched commands.
 * session_notes, system_notes: optional AI context notes (may be NULL).
 * initial_state: per-session AI state (may be NULL for fresh conversation). */
HWND ai_chat_show(HWND parent, const char *api_key, const char *provider,
                  const char *custom_url, const char *custom_model,
                  int paste_delay_ms, const char *font_name,
                  const char *ai_font,
                  const char *colour_scheme,
                  const char *session_notes, const char *system_notes,
                  AiSessionState *initial_state,
                  const char *session_name,
                  int docked);

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

/* Update tool configuration (search provider, fetch enabled, etc.).
 * Call after settings change and on initial window creation.
 * search_provider: "none", "duckduckgo-api", "duckduckgo-html", or "custom".
 * search_url: custom endpoint URL (only used when search_provider="custom").
 * max_search_results: 1-20, 0 uses default (7).
 * web_fetch_enabled: 1 to enable, 0 to disable. */
void ai_chat_update_tools(HWND hwnd,
                          const char *search_provider,
                          const char *search_url,
                          int max_search_results,
                          int web_fetch_enabled);

/* Update session-specific and system-wide AI notes.
 * Either may be NULL to leave unchanged. */
void ai_chat_update_notes(HWND hwnd, const char *session_notes,
                          const char *system_notes);

/* Update the colour scheme / theme of an open AI chat window. */
void ai_chat_set_theme(HWND hwnd, const char *colour_scheme);

/* Re-fetch the chrome fonts this window keeps cached across paints
 * (currently just hFont, the FONT_BODY role for buttons/labels) from
 * ns_font(). Call after ns_font_flush() runs elsewhere (a font-setting
 * change or WM_DPICHANGED) so this window never draws with a stale,
 * already-deleted handle. A no-op if the window is not open. */
void ai_chat_refresh_fonts(HWND hwnd);

/* Enable or disable markdown rendering in the AI chat list view. */
void ai_chat_set_markdown(HWND hwnd, int enabled);

/* Set whether session Auto Approve also covers write/critical commands
 * (settings.ai_auto_approve_all). Survives chat_approval_reset(), so this
 * only needs to be called when the setting itself changes. */
void ai_chat_set_auto_approve_all(HWND hwnd, int enabled);

/* Set how many terminal lines are sent to the AI as context (1-50000). */
void ai_chat_set_context_lines(HWND hwnd, int lines);

/* --ui-demo only: layer the canned approval queue and small activity
 * flourishes onto a conversation already loaded via ai_chat_show()'s
 * initial_state or ai_chat_switch_session() (see ui_demo_build() in
 * src/core/ui_demo.h, which builds the conversation/approval queue this
 * expects).
 * state: the requested demo state name ("chat", "approval", "executing",
 *   "tool", "error", "empty", "all") -- drives the two flourishes plain
 *   AiConversation/ApprovalQueue data can't express on their own: the
 *   executing command's activity dot, and the HTTP-error [Retry] link.
 * approval: entries to render as command cards (copied); NULL/empty is
 *   fine for states with no commands.
 * No-op if hwnd isn't a live, open AI chat window. */
void ai_chat_apply_demo_extras(HWND hwnd, const char *state,
                               const ApprovalQueue *approval);

/* Close and destroy the AI assist window. */
void ai_chat_close(HWND hwnd);

/* Returns non-zero if the AI chat has saveable conversation content. */
int ai_chat_has_content(HWND hwnd);

/* Bump the active session's user-idle tick.  Called from any UI
 * surface (chat input, etc.) that should count as user activity. */
void session_mark_user_active(void);

#endif /* _WIN32 */
#endif /* NUTSHELL_AI_CHAT_H */
