#ifndef NUTSHELL_AI_CHAT_TESTABLE_H
#define NUTSHELL_AI_CHAT_TESTABLE_H

/* Pure-logic helper: determine whether the AI chat window's channel reference
 * needs updating after a connection completes.
 *
 * Returns 1 if ai_chat_set_session() should be called, 0 otherwise.
 *
 * Parameters are opaque pointers so this compiles without Win32 types. */
static inline int ai_chat_should_update_channel(int chat_open,
                                                 const void *chat_channel,
                                                 const void *session_channel,
                                                 int session_is_active)
{
    (void)chat_channel; /* update even if already set (reconnect) */
    if (!chat_open) return 0;
    if (!session_channel) return 0;
    if (!session_is_active) return 0;
    return 1;
}

#endif /* NUTSHELL_AI_CHAT_TESTABLE_H */
