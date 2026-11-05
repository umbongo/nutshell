/* src/core/ssh_auth_plan.h */
#ifndef NUTSHELL_SSH_AUTH_PLAN_H
#define NUTSHELL_SSH_AUTH_PLAN_H

#include <stddef.h>

/*
 * ssh_auth_plan -- which SSH authentication methods to try, and in what
 * order. Pure and portable (no windows.h, no libssh2): the connect thread
 * asks the server for its method list (libssh2_userauth_list), parses it
 * here, and then loops on ssh_auth_next() until it returns SSH_STEP_DONE.
 * See docs/superpowers/specs/2026-11-05-keyboard-interactive-design.md,
 * sections 1 and 2.
 */

#define SSH_AM_PUBLICKEY 0x1u
#define SSH_AM_PASSWORD  0x2u
#define SSH_AM_KBDINT    0x4u
#define SSH_AM_OTHER     0x8u      /* any method name this module does not know */

#define SSH_AUTH_DISPLAY_MAX  128  /* size of the display string buffer */
#define SSH_AUTH_MAX_ATTEMPTS 4    /* libssh2 auth calls per connection */

#define SSH_RC_AUTH_FAILED    (-18) /* LIBSSH2_ERROR_AUTHENTICATION_FAILED */
#define SSH_RC_PK_UNVERIFIED  (-19) /* LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED */

/* Parses a comma-separated method list (the libssh2_userauth_list reply)
 * into an SSH_AM_* bit set. Spaces, empty items and duplicates are
 * tolerated. `display`, when non-NULL and cap > 0, receives the names
 * (printable ASCII only), joined by ", ", cut on an item boundary so it
 * fits both cap - 1 and SSH_AUTH_DISPLAY_MAX - 1 bytes; always terminated.
 * A NULL list gives 0 and an empty display. */
unsigned ssh_auth_methods_parse(const char *list, char *display, size_t cap);

typedef enum {
    SSH_STEP_DONE = 0,
    SSH_STEP_PUBLICKEY,
    SSH_STEP_KBDINT,
    SSH_STEP_PASSWORD_SAVED,
    SSH_STEP_KBDINT_RETRY,
    SSH_STEP_PASSWORD_ASK
} SshAuthStep;

typedef struct {
    int is_key, has_saved;     /* has_saved: password profile with a non-empty saved password */
    unsigned offered;          /* SSH_AM_* */
    unsigned tried;            /* 1u << step for each step run */
    int saved_used;            /* the saved password has been sent */
    int kbd_sent_saved;        /* the KBDINT attempt sent it */
    int attempts, requeried;
} SshAuthPlan;

/* `saved` is the profile's saved password (or passphrase, for a key
 * profile); only its non-emptiness is kept. */
void ssh_auth_plan_init(SshAuthPlan *p, int is_key, const char *saved, unsigned offered);

/* The next step to run, or SSH_STEP_DONE (nothing left, or the attempt cap). */
SshAuthStep ssh_auth_next(const SshAuthPlan *p);

/* Records that `step` ran and counts one attempt. `saved_sent` says the
 * saved password went out in it. */
void ssh_auth_plan_record(SshAuthPlan *p, SshAuthStep step, int saved_sent);

/* Counts one extra libssh2 call that is not a plan step (the passphrase retry). */
void ssh_auth_plan_count_retry(SshAuthPlan *p);

/* The saved password if `step` may send it, else NULL. Never for a key
 * profile, never twice, never an empty one. */
const char *ssh_auth_saved_for(const SshAuthPlan *p, SshAuthStep step, const char *saved);

/* libssh2 last-errno values that mean the socket is gone: -7, -13, -43. */
int ssh_auth_transport_dead(int last_errno);

/* 1 when an attempt's result lets the plan go on: rc -18 or -19, no timeout,
 * transport alive. */
int ssh_auth_may_continue(int rc, int timed_out, int last_errno);

/* 1 when the method list should be asked for again: may_continue, no step
 * left, not yet re-queried, attempts to spare. */
int ssh_auth_should_requery(const SshAuthPlan *p, int rc, int timed_out, int last_errno);

/* Installs the re-queried method set and marks the one re-query used. */
void ssh_auth_plan_requeried(SshAuthPlan *p, unsigned offered);

/* The final failure message, e.g. "Authentication failed for alice@host.
 * The server offers: publickey, keyboard-interactive." `display` (may be
 * NULL or "") is the parser's display string; `extra` (may be NULL or "")
 * is appended after one space. Returns the length written, or 0 with buf
 * set to "" when cap is too small. */
size_t ssh_auth_fail_text(const char *user, const char *host, int is_key, unsigned offered,
                          const char *display, const char *extra, char *buf, size_t cap);

#endif /* NUTSHELL_SSH_AUTH_PLAN_H */
