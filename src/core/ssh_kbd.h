#ifndef NUTSHELL_SSH_KBD_H
#define NUTSHELL_SSH_KBD_H

/* The portable half of SSH keyboard-interactive authentication (spec
 * docs/superpowers/specs/2026-11-05-keyboard-interactive-design.md, section 4).
 *
 * ssh_session.c holds a thin LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC that passes
 * its arguments straight to ssh_kbd_round(); everything that decides anything
 * lives here, behind injected hooks, so the native suite can run it without a
 * socket: ask (the user), set_timeout, abort_io and now. */

#include <libssh2.h>
#include <stddef.h>

#include "conn_prompt.h"

/* libssh2's arrays pass straight through. */
typedef LIBSSH2_USERAUTH_KBDINT_PROMPT   SshKbdPromptIn;
typedef LIBSSH2_USERAUTH_KBDINT_RESPONSE SshKbdResponseOut;

#define SSH_KBD_MAX_PROMPTS 8      /* prompts handled per round; the rest get "" */
#define SSH_KBD_MAX_ROUNDS  16     /* rounds per attempt; the 17th aborts */
#define SSH_KBD_NAME_MAX    96
#define SSH_KBD_TEXT_MAX    2048   /* instruction and text area */
#define SSH_KBD_PROMPT_MAX  2048   /* one prompt; the tail is kept on overflow */
#define SSH_KBD_ANSWER_MAX  256    /* ConnUiRequest.passphrase */

/* One prompt as shown to the user. The strings are sanitised and live only
 * until ask() returns. */
typedef struct {
    const char *name;    /* sanitised, single line, "" when none */
    const char *text;    /* zero-prompt instruction + instruction + prompt lines but the last; "" when none */
    const char *label;   /* the prompt's last line, single line */
    int echo, offer_save;
} SshKbdAsk;

typedef struct SshKbdCtx {
    /* set by window.c */
    /* Shows one prompt. Writes the typed answer, NUL-terminated, into
     * answer[cap] and the "remember it" tick into *save. Returns 1 for OK, 0
     * for Cancel (or a window close that cancelled the request). */
    int  (*ask)(void *ui_user, const SshKbdAsk *q, char *answer, size_t cap, int *save);
    void *ui_user;
    const char *saved;        /* ssh_auth_saved_for(); NULL = no auto-answer */
    int  is_key;
    long setup_limit_ms;      /* ssh_setup_timeout_ms() */
    ConnSaveCandidate *save;  /* may be NULL */
    unsigned log_id;
    /* set by ssh_auth_kbdint() */
    void (*set_timeout)(void *io_user, long ms);
    void (*abort_io)(void *io_user);
    long long (*now)(void *io_user);   /* seconds, like time() */
    void *io_user;
    /* per attempt; reset by ssh_kbd_attempt() */
    long long entry;
    long limit_in_force_ms;   /* SSH_KBD_ANSWER_WAIT_MS after an answered round, else setup_limit_ms */
    int  rounds;              /* rounds seen */
    int  prompts;             /* prompts received, handled or not */
    int  auto_answered;       /* answered with the saved password */
    int  user_answered;       /* ask() returned OK (an empty answer counts) */
    int  saved_sent, cancelled, too_many, aborted;
    char zero_instruction[SSH_KBD_TEXT_MAX];   /* last zero-prompt round's text not yet shown */
    char answer[SSH_KBD_ANSWER_MAX];           /* the buffer handed to ask; zero when a round returns */
} SshKbdCtx;

/* One keyboard-interactive round: every argument is libssh2's, none of the
 * strings NUL-terminated (NULL with length 0 is empty). Fills responses[0..
 * num_prompts-1], each a fresh malloc(len + 1) holding the answer and a NUL
 * with .length = len, which libssh2 frees with free(). Ends with
 * set_timeout(ssh_kbd_rearm_ms(...)), or with abort_io() on a cancel or the
 * round cap. */
void ssh_kbd_round(SshKbdCtx *ctx, const char *name, int name_len, const char *instruction,
                   int instruction_len, int num_prompts, const SshKbdPromptIn *prompts,
                   SshKbdResponseOut *responses);

/* Set-call-clear: resets the per-attempt state, records the entry time, stores
 * ctx in *slot (the session's kbd_ctx), makes call(arg), restores the timeout
 * to the setup limit in force and sets *slot back to NULL. Returns call's
 * result, or -1 without calling when slot, ctx or call is NULL. */
int  ssh_kbd_attempt(void **slot, SshKbdCtx *ctx, int (*call)(void *arg), void *arg);

#endif /* NUTSHELL_SSH_KBD_H */
