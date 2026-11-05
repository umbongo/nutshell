#include "ssh_kbd.h"

#include <stdlib.h>
#include <string.h>

#include "dlog.h"
#include "secure_zero.h"
#include "ssh_auth_plan.h"
#include "ssh_timeout.h"

/* Room for the three parts of an ask's text area (the pending zero-prompt
 * instruction, the round's instruction, the prompt's earlier lines), each at
 * most SSH_KBD_TEXT_MAX - 1 bytes, and two LF separators. */
#define JOIN_MAX (3 * SSH_KBD_TEXT_MAX)

/* Appends `part` to buf, after an LF when buf already holds something. */
static void join_part(char *buf, size_t *n, const char *part)
{
    size_t pl = strlen(part);
    if (pl == 0) return;
    if (*n) buf[(*n)++] = '\n';
    memcpy(buf + *n, part, pl);
    *n += pl;
    buf[*n] = '\0';
}

/* Fills one libssh2 response with a fresh malloc(len + 1) copy of s (a NUL
 * after the answer; `.length` is len). libssh2 frees it with its default
 * allocator, which is free(). Never leaves NULL for a real string, even an
 * empty one. If malloc fails the response is text = NULL, length 0, which
 * libssh2 tolerates (it sends an empty string), and the round carries on:
 * running out of memory must not take authentication down with an abort.
 * Returns 1 when the copy was made. */
static int set_response(SshKbdResponseOut *r, const char *s, size_t len)
{
    char *m = (char *)malloc(len + 1);
    if (!m) {
        r->text = NULL;
        r->length = 0;
        return 0;
    }
    if (len) memcpy(m, s, len);
    m[len] = '\0';
    r->text = m;
    r->length = (unsigned)len;
    return 1;
}

/* A prompt cut by the 2047-byte limit keeps its tail, since the last line is
 * the label: "..." and as many of the last bytes as fit, starting on a UTF-8
 * boundary. Returns the length written to out (cap SSH_KBD_PROMPT_MAX). */
static size_t tail_prompt(const unsigned char *raw, size_t len, char *out)
{
    size_t keep = SSH_KBD_PROMPT_MAX - 1 - 3;
    const unsigned char *t = raw + len - keep;
    while (keep > 0 && (*t & 0xC0) == 0x80) { t++; keep--; }
    memcpy(out, "...", 3);
    return 3 + ssh_kbd_sanitize((const char *)t, keep, out + 3, SSH_KBD_PROMPT_MAX - 3, 0);
}

void ssh_kbd_round(SshKbdCtx *ctx, const char *name, int name_len, const char *instruction,
                   int instruction_len, int num_prompts, const SshKbdPromptIn *prompts,
                   SshKbdResponseOut *responses)
{
    if (!ctx) return;

    char nm[SSH_KBD_NAME_MAX];
    char instr[SSH_KBD_TEXT_MAX];
    char full[SSH_KBD_PROMPT_MAX];     /* the sanitised prompt, several lines */
    char flat[SSH_KBD_PROMPT_MAX];     /* the same on one line, for the matcher */
    char joined[JOIN_MAX];
    char text[SSH_KBD_TEXT_MAX];
    unsigned char registered[SSH_KBD_MAX_PROMPTS] = { 0 };

    int zero_round = (num_prompts <= 0 || !prompts || !responses);
    int n_prompts = zero_round ? 0 : num_prompts;

    ctx->rounds++;
    ctx->prompts += n_prompts;
    if (n_prompts > SSH_KBD_MAX_PROMPTS) ctx->too_many = 1;

    /* abort_io runs at most once per attempt, from a cancel or from here. */
    int io_aborted = ctx->cancelled || ctx->aborted;
    int capped = ctx->rounds > SSH_KBD_MAX_ROUNDS;
    if (capped) {
        ctx->aborted = 1;
        if (!io_aborted && ctx->abort_io) ctx->abort_io(ctx->io_user);
    }

    ssh_kbd_sanitize(name && name_len > 0 ? name : NULL, name && name_len > 0 ? (size_t)name_len : 0,
                     nm, sizeof nm, 1);
    size_t instr_len = ssh_kbd_sanitize(instruction && instruction_len > 0 ? instruction : NULL,
                                        instruction && instruction_len > 0 ? (size_t)instruction_len : 0,
                                        instr, sizeof instr, 0);
    if (instr_len)
        DLOG_CONTENT(DLOG_SSH, DLOG_DEBUG, ctx->log_id, "connect.auth.instruction", "text",
                     instr, instr_len);

    if (zero_round && !capped && instr_len) {
        /* shown in the next prompt's text area, or appended to the final error;
         * a later zero-prompt round with nothing to say keeps what was not shown */
        memcpy(ctx->zero_instruction, instr, instr_len + 1);
    }

    int answered_now = 0;                       /* an ask() returned OK this round */
    for (int i = 0; i < n_prompts; i++) {
        SshKbdResponseOut *resp = &responses[i];

        if (i >= SSH_KBD_MAX_PROMPTS || capped || ctx->cancelled) {
            set_response(resp, "", 0);
            continue;
        }

        const unsigned char *raw = prompts[i].text;
        size_t rawlen = raw ? prompts[i].length : 0;
        int echo = prompts[i].echo != 0;
        int truncated = rawlen > SSH_KBD_PROMPT_MAX - 1;

        size_t flen;
        if (truncated)
            flen = tail_prompt(raw, rawlen, full);
        else
            flen = ssh_kbd_sanitize((const char *)raw, rawlen, full, sizeof full, 0);
        DLOG_CONTENT(DLOG_SSH, DLOG_DEBUG, ctx->log_id, "connect.auth.prompt", "text", full, flen);

        /* The matcher sees the whole sanitised prompt on one line (spec section
         * 4: "accepts the sanitised text"); the user is shown its last line. */
        int is_pw = 0;
        if (!echo && !truncated) {
            ssh_kbd_sanitize((const char *)raw, rawlen, flat, sizeof flat, 1);
            is_pw = ssh_kbd_prompt_is_password(flat);
        }

        if (ctx->saved && ctx->saved[0] && !ctx->saved_sent && !echo && !truncated && is_pw) {
            set_response(resp, ctx->saved, strlen(ctx->saved));
            ctx->saved_sent = 1;
            ctx->auto_answered++;
            continue;
        }

        /* text area: the pending zero-prompt instruction, the instruction, the
         * prompt's lines but the last; label: the last line */
        char *nl = strrchr(full, '\n');
        const char *label = full;
        if (nl) { *nl = '\0'; label = nl + 1; }
        size_t jn = 0;
        joined[0] = '\0';
        join_part(joined, &jn, ctx->zero_instruction);
        join_part(joined, &jn, instr);
        if (nl) join_part(joined, &jn, full);
        ssh_kbd_sanitize(joined, jn, text, sizeof text, 0);

        SshKbdAsk q;
        q.name = nm;
        q.text = text;
        q.label = label;
        q.echo = echo;
        q.offer_save = !ctx->is_key && !echo && !truncated && is_pw;

        int save = 0;
        secure_zero(ctx->answer, sizeof ctx->answer);
        int ok = ctx->ask ? ctx->ask(ctx->ui_user, &q, ctx->answer, sizeof ctx->answer, &save) : 0;
        ctx->zero_instruction[0] = '\0';        /* shown now */
        ctx->answer[sizeof ctx->answer - 1] = '\0';

        if (!ok) {
            ctx->cancelled = 1;
            secure_zero(ctx->answer, sizeof ctx->answer);
            set_response(resp, "", 0);
            if (!io_aborted && ctx->abort_io) ctx->abort_io(ctx->io_user);
            io_aborted = 1;
            continue;
        }

        size_t alen = strlen(ctx->answer);
        answered_now = 1;
        ctx->user_answered++;
        if (q.offer_save) conn_save_note(ctx->save, ctx->answer, save);

        int made = set_response(resp, ctx->answer, alen);
        /* A non-echo answer is a known secret for the rest of the round, so a
         * later prompt's logged text cannot leak it; released below. If the copy
         * failed there is no text to release it by, so it is never registered. */
        if (made && !echo && alen >= 3 && dlog_secret_add(ctx->answer))
            registered[i] = 1;
        secure_zero(ctx->answer, sizeof ctx->answer);
    }

    for (int i = 0; i < SSH_KBD_MAX_PROMPTS && i < n_prompts; i++)
        if (registered[i]) dlog_secret_remove(responses[i].text);
    secure_zero(ctx->answer, sizeof ctx->answer);

    if (ctx->aborted) return;                   /* the socket is shut down: no re-arm */

    long limit = answered_now ? SSH_KBD_ANSWER_WAIT_MS : ctx->setup_limit_ms;
    long long now = ctx->now ? ctx->now(ctx->io_user) : ctx->entry;
    ctx->limit_in_force_ms = limit;
    if (ctx->set_timeout)
        ctx->set_timeout(ctx->io_user, ssh_kbd_rearm_ms(ctx->entry, now, limit));
}

int ssh_kbd_attempt(void **slot, SshKbdCtx *ctx, int (*call)(void *arg), void *arg)
{
    if (!slot || !ctx || !call) return -1;

    ctx->rounds = ctx->prompts = ctx->auto_answered = ctx->user_answered = 0;
    ctx->saved_sent = ctx->cancelled = ctx->too_many = ctx->aborted = 0;
    ctx->zero_instruction[0] = '\0';
    secure_zero(ctx->answer, sizeof ctx->answer);
    ctx->entry = ctx->now ? ctx->now(ctx->io_user) : 0;
    ctx->limit_in_force_ms = ctx->setup_limit_ms;

    *slot = ctx;
    int rc = call(arg);
    if (ctx->set_timeout) ctx->set_timeout(ctx->io_user, ctx->setup_limit_ms);
    *slot = NULL;
    secure_zero(ctx->answer, sizeof ctx->answer);
    return rc;
}
