/* tests/test_ssh_kbd.c -- the portable keyboard-interactive round and the
 * set-call-clear attempt wrapper (spec sections 4, 6 and 8).
 *
 * A fake records every ask, every set_timeout and every abort_io, and serves
 * scripted answers, ticks and a cancel. Prompts are built from byte arrays
 * with no NUL and garbage beyond `length`, like libssh2's. */
#include "test_framework.h"
#include "ssh_kbd.h"
#include "ssh_timeout.h"
#include "dlog.h"
#include <stdlib.h>
#include <string.h>

#define FK_MAX_ASKS 32
#define FK_MAX_TIMEOUTS 40

typedef struct {
    /* script */
    const char *ans[FK_MAX_ASKS];   /* NULL = the default answer */
    int  tick[FK_MAX_ASKS];
    int  nans;
    int  cancel_at;                 /* ask index that returns 0; -1 = none */
    const char *dflt;
    /* record */
    int  asks;
    char name[FK_MAX_ASKS][SSH_KBD_NAME_MAX];
    char text[FK_MAX_ASKS][SSH_KBD_TEXT_MAX + 8];
    char label[FK_MAX_ASKS][SSH_KBD_PROMPT_MAX + 8];
    int  echo[FK_MAX_ASKS];
    int  offer[FK_MAX_ASKS];
    int  ntimeouts;
    long timeouts[FK_MAX_TIMEOUTS];
    int  aborts;
    long long now;
    /* secret registration probe: filled when probe_next is set */
    int  probe;                     /* ask index at which to probe the scrub */
    int  probe_redacted;
} Fake;

static Fake F;

static int fk_ask(void *user, const SshKbdAsk *q, char *answer, size_t cap, int *save)
{
    Fake *f = (Fake *)user;
    int i = f->asks++;
    if (i < FK_MAX_ASKS) {
        snprintf(f->name[i], sizeof f->name[i], "%s", q->name ? q->name : "(null)");
        snprintf(f->text[i], sizeof f->text[i], "%s", q->text ? q->text : "(null)");
        snprintf(f->label[i], sizeof f->label[i], "%s", q->label ? q->label : "(null)");
        f->echo[i] = q->echo;
        f->offer[i] = q->offer_save;
    }
    if (f->probe == i) {
        char out[64];
        dlog_scrub_copy(out, sizeof out, "x hunter22 y", 12);
        f->probe_redacted = strstr(out, "[REDACTED]") != NULL;
    }
    const char *a = (i < f->nans && f->ans[i]) ? f->ans[i] : f->dflt;
    snprintf(answer, cap, "%s", a);          /* typed, even if then cancelled */
    if (f->cancel_at == i) return 0;
    *save = (i < FK_MAX_ASKS) ? f->tick[i] : 0;
    return 1;
}

static void fk_set_timeout(void *io, long ms)
{
    (void)io;
    if (F.ntimeouts < FK_MAX_TIMEOUTS) F.timeouts[F.ntimeouts] = ms;
    F.ntimeouts++;
}
static void fk_abort(void *io) { (void)io; F.aborts++; }
static long long fk_now(void *io) { (void)io; return F.now; }

static void fk_reset(SshKbdCtx *c)
{
    memset(&F, 0, sizeof F);
    F.cancel_at = -1;
    F.probe = -1;
    F.dflt = "";
    F.now = 130;
    memset(c, 0, sizeof *c);
    c->ask = fk_ask;
    c->ui_user = &F;
    c->setup_limit_ms = 15000;
    c->set_timeout = fk_set_timeout;
    c->abort_io = fk_abort;
    c->now = fk_now;
    c->entry = 100;
    c->limit_in_force_ms = 15000;
}

/* A prompt of `len` bytes of `text`, with garbage after it and no NUL. */
static void mk_prompt(SshKbdPromptIn *p, unsigned char *store, size_t store_cap,
                      const char *text, size_t len, int echo)
{
    memset(store, 'G', store_cap);
    if (len) memcpy(store, text, len);
    p->text = store;
    p->length = len;
    p->echo = (unsigned char)echo;
}

static void free_responses(SshKbdResponseOut *r, int n)
{
    for (int i = 0; i < n; i++) { free(r[i].text); r[i].text = NULL; }
}

static void fill_garbage(SshKbdResponseOut *r, int n)
{
    for (int i = 0; i < n; i++) { r[i].text = NULL; r[i].length = 99; }
}

/* `n` bytes: n-10 filler bytes, then "\nPassword:". */
static size_t long_prompt(unsigned char *buf, size_t n)
{
    memset(buf, 'a', n - 10);
    memcpy(buf + n - 10, "\nPassword:", 10);
    return n;
}

/* ---- tests ---------------------------------------------------------------- */

int test_ssh_kbd_copies_by_length(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p;
    SshKbdResponseOut r;
    unsigned char st[64], nm[32], ins[32];
    fk_reset(&c);
    F.ans[0] = "abc";
    F.nans = 1;
    memset(nm, 'G', sizeof nm);  memcpy(nm, "Login:XXXX", 10);
    memset(ins, 'G', sizeof ins); memcpy(ins, "Hello there!!!!", 15);
    mk_prompt(&p, st, sizeof st, "Password:XXXX", 9, 0);
    fill_garbage(&r, 1);
    ssh_kbd_round(&c, (const char *)nm, 6, (const char *)ins, 5, 1, &p, &r);
    ASSERT_EQ(F.asks, 1);
    ASSERT_STR_EQ(F.name[0], "Login:");
    ASSERT_STR_EQ(F.text[0], "Hello");
    ASSERT_STR_EQ(F.label[0], "Password:");
    ASSERT_NOT_NULL(r.text);
    ASSERT_EQ(r.length, 3u);
    if (r.text) { ASSERT_STR_EQ(r.text, "abc"); ASSERT_EQ(r.text[3], 0); }
    free_responses(&r, 1);

    /* libssh2 passes NULL for a zero-length string. */
    fk_reset(&c);
    p.text = NULL; p.length = 0; p.echo = 0;
    fill_garbage(&r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
    ASSERT_EQ(F.asks, 1);
    ASSERT_STR_EQ(F.name[0], "");
    ASSERT_STR_EQ(F.text[0], "");
    ASSERT_STR_EQ(F.label[0], "");
    ASSERT_NOT_NULL(r.text);
    ASSERT_EQ(r.length, 0u);
    free_responses(&r, 1);
    TEST_END();
}

int test_ssh_kbd_prompt_boundaries(void)
{
    TEST_BEGIN();
    static unsigned char buf[2200];
    SshKbdCtx c;
    SshKbdPromptIn p;
    SshKbdResponseOut r;

    /* 2047 bytes: the limit is "does not fit in 2048 with a NUL". */
    fk_reset(&c);
    c.saved = "s3cret";
    p.text = buf; p.echo = 0;
    p.length = long_prompt(buf, 2047);
    fill_garbage(&r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
    ASSERT_EQ(F.asks, 0);
    ASSERT_EQ(c.saved_sent, 1);
    ASSERT_NOT_NULL(r.text);
    if (r.text) ASSERT_STR_EQ(r.text, "s3cret");
    free_responses(&r, 1);

    /* 2048 and 2049: truncated, the tail is kept, never auto-answered. */
    for (size_t n = 2048; n <= 2049; n++) {
        fk_reset(&c);
        c.saved = "s3cret";
        p.length = long_prompt(buf, n);
        fill_garbage(&r, 1);
        ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
        ASSERT_EQ(F.asks, 1);
        ASSERT_EQ(c.saved_sent, 0);
        ASSERT_TRUE(strncmp(F.text[0], "...", 3) == 0);
        ASSERT_TRUE(strlen(F.text[0]) < SSH_KBD_PROMPT_MAX);
        ASSERT_STR_EQ(F.label[0], "Password:");
        ASSERT_EQ(F.offer[0], 0);
        ASSERT_NOT_NULL(r.text);
        free_responses(&r, 1);
    }
    TEST_END();
}

int test_ssh_kbd_saved_answers_one_prompt(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p[2];
    SshKbdResponseOut r[2];
    unsigned char st[2][32];

    fk_reset(&c);
    c.saved = "s3cret";
    mk_prompt(&p[0], st[0], sizeof st[0], "Password:", 9, 0);
    fill_garbage(r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, p, r);
    ASSERT_EQ(F.asks, 0);
    ASSERT_EQ(c.saved_sent, 1);
    ASSERT_EQ(c.auto_answered, 1);
    ASSERT_NOT_NULL(r[0].text);
    if (r[0].text) ASSERT_STR_EQ(r[0].text, "s3cret");
    ASSERT_EQ(r[0].length, 6u);
    free_responses(r, 1);

    fk_reset(&c);
    c.saved = "s3cret";
    F.ans[0] = "typed";                 /* the one ask is the second prompt's */
    F.nans = 1;
    mk_prompt(&p[0], st[0], sizeof st[0], "Password:", 9, 0);
    mk_prompt(&p[1], st[1], sizeof st[1], "Password:", 9, 0);
    fill_garbage(r, 2);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 2, p, r);
    ASSERT_EQ(F.asks, 1);
    ASSERT_NOT_NULL(r[0].text);
    ASSERT_NOT_NULL(r[1].text);
    if (r[0].text) ASSERT_STR_EQ(r[0].text, "s3cret");
    if (r[1].text) ASSERT_STR_EQ(r[1].text, "typed");
    free_responses(r, 2);
    TEST_END();
}

int test_ssh_kbd_saved_once_across_rounds(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p;
    SshKbdResponseOut r;
    unsigned char st[32];
    mk_prompt(&p, st, sizeof st, "Password:", 9, 0);

    fk_reset(&c);
    c.saved = "s3cret";
    fill_garbage(&r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
    ASSERT_EQ(F.asks, 0);
    free_responses(&r, 1);
    fill_garbage(&r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
    ASSERT_EQ(F.asks, 1);
    ASSERT_EQ(c.rounds, 2);
    free_responses(&r, 1);

    fk_reset(&c);                       /* no saved password: always asked */
    for (int i = 0; i < 2; i++) {
        fill_garbage(&r, 1);
        ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
        free_responses(&r, 1);
    }
    ASSERT_EQ(F.asks, 2);
    ASSERT_EQ(c.saved_sent, 0);
    TEST_END();
}

int test_ssh_kbd_never_auto(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p;
    SshKbdResponseOut r;
    unsigned char st[64];
    static const struct { const char *t; int echo; int is_key; const char *saved; } rows[] = {
        { "Password:",          1, 0, "s3cret" },   /* echo prompt */
        { "PASSCODE:",          0, 0, "s3cret" },
        { "Verification code:", 0, 0, "s3cret" },
        { "Password:",          0, 1, NULL     },   /* key profile: saved is NULL */
        { "Password:",          0, 0, ""       },   /* empty saved password */
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        fk_reset(&c);
        c.saved = rows[i].saved;
        c.is_key = rows[i].is_key;
        mk_prompt(&p, st, sizeof st, rows[i].t, strlen(rows[i].t), rows[i].echo);
        fill_garbage(&r, 1);
        ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
        ASSERT_EQ(F.asks, 1);
        ASSERT_EQ(c.saved_sent, 0);
        ASSERT_EQ(c.auto_answered, 0);
        free_responses(&r, 1);
    }
    TEST_END();
}

int test_ssh_kbd_offer_save(void)
{
    TEST_BEGIN();
    static unsigned char buf[2200];
    SshKbdCtx c;
    SshKbdPromptIn p[2];
    SshKbdResponseOut r[2];
    unsigned char st[2][64];
    ConnSaveCandidate sv;

    /* a password profile, an ordinary password prompt: offered */
    fk_reset(&c);
    mk_prompt(&p[0], st[0], sizeof st[0], "Password:", 9, 0);
    fill_garbage(r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, p, r);
    ASSERT_EQ(F.offer[0], 1);
    free_responses(r, 1);

    /* the whole sanitised prompt decides, not its last line */
    fk_reset(&c);
    mk_prompt(&p[0], st[0], sizeof st[0], "Enter the new one\nPassword:", 27, 0);
    fill_garbage(r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, p, r);
    ASSERT_STR_EQ(F.label[0], "Password:");
    ASSERT_STR_EQ(F.text[0], "Enter the new one");
    ASSERT_EQ(F.offer[0], 0);
    free_responses(r, 1);

    static const struct { const char *t; int echo; int is_key; } no[] = {
        { "Password:", 1, 0 }, { "PASSCODE:", 0, 0 }, { "Password:", 0, 1 },
    };
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++) {
        fk_reset(&c);
        c.is_key = no[i].is_key;
        mk_prompt(&p[0], st[0], sizeof st[0], no[i].t, strlen(no[i].t), no[i].echo);
        fill_garbage(r, 1);
        ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, p, r);
        ASSERT_EQ(F.asks, 1);
        ASSERT_EQ(F.offer[0], 0);
        free_responses(r, 1);
    }

    fk_reset(&c);                       /* truncated password prompt */
    p[0].text = buf; p[0].echo = 0;
    p[0].length = long_prompt(buf, 2048);
    fill_garbage(r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, p, r);
    ASSERT_EQ(F.offer[0], 0);
    free_responses(r, 1);

    /* the latest offered answer wins; an unticked one clears the candidate */
    memset(&sv, 0, sizeof sv);
    fk_reset(&c);
    c.save = &sv;
    F.ans[0] = "a"; F.tick[0] = 1;
    F.ans[1] = "b"; F.tick[1] = 0;
    F.nans = 2;
    mk_prompt(&p[0], st[0], sizeof st[0], "Password:", 9, 0);
    mk_prompt(&p[1], st[1], sizeof st[1], "Password:", 9, 0);
    fill_garbage(r, 2);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 2, p, r);
    ASSERT_EQ(sv.ticked, 0);
    {
        int clean = 1;
        for (size_t i = 0; i < sizeof sv.pw; i++) if (sv.pw[i]) clean = 0;
        ASSERT_TRUE(clean);
    }
    free_responses(r, 2);

    fk_reset(&c);
    c.save = &sv;
    F.ans[0] = "b"; F.tick[0] = 0;
    F.ans[1] = "c"; F.tick[1] = 1;
    F.nans = 2;
    fill_garbage(r, 2);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 2, p, r);
    ASSERT_EQ(sv.ticked, 1);
    ASSERT_STR_EQ(sv.pw, "c");
    free_responses(r, 2);

    /* a code prompt that is not offered leaves the candidate alone */
    fk_reset(&c);
    c.save = &sv;
    F.ans[0] = "123456"; F.tick[0] = 1;
    F.nans = 1;
    mk_prompt(&p[0], st[0], sizeof st[0], "PASSCODE:", 9, 0);
    fill_garbage(r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, p, r);
    ASSERT_EQ(sv.ticked, 1);
    ASSERT_STR_EQ(sv.pw, "c");
    free_responses(r, 1);
    conn_save_clear(&sv);
    TEST_END();
}

int test_ssh_kbd_zero_prompt_round(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p;
    SshKbdResponseOut r;
    unsigned char st[32];

    fk_reset(&c);
    ssh_kbd_round(&c, NULL, 0, "Use Duo", 7, 0, NULL, NULL);
    ASSERT_EQ(F.asks, 0);
    ASSERT_STR_EQ(c.zero_instruction, "Use Duo");
    ASSERT_EQ(c.rounds, 1);

    mk_prompt(&p, st, sizeof st, "Passcode:", 9, 0);
    fill_garbage(&r, 1);
    ssh_kbd_round(&c, NULL, 0, "Enter code", 10, 1, &p, &r);
    ASSERT_EQ(F.asks, 1);
    ASSERT_STR_EQ(F.text[0], "Use Duo\nEnter code");
    ASSERT_STR_EQ(c.zero_instruction, "");
    free_responses(&r, 1);

    fill_garbage(&r, 1);
    ssh_kbd_round(&c, NULL, 0, "Enter code", 10, 1, &p, &r);
    ASSERT_STR_EQ(F.text[1], "Enter code");
    free_responses(&r, 1);

    /* a zero-prompt round with an empty instruction keeps the one not yet shown */
    fk_reset(&c);
    ssh_kbd_round(&c, NULL, 0, "Use Duo", 7, 0, NULL, NULL);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 0, NULL, NULL);
    ASSERT_STR_EQ(c.zero_instruction, "Use Duo");
    TEST_END();
}

int test_ssh_kbd_too_many_prompts(void)
{
    TEST_BEGIN();
    static SshKbdPromptIn p[100];
    static SshKbdResponseOut r[100];
    unsigned char st[32];
    SshKbdCtx c;
    fk_reset(&c);
    F.dflt = "ans";
    mk_prompt(&p[0], st, sizeof st, "Password:", 9, 0);
    for (int i = 1; i < 100; i++) p[i] = p[0];
    fill_garbage(r, 100);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 100, p, r);
    ASSERT_EQ(F.asks, SSH_KBD_MAX_PROMPTS);
    ASSERT_EQ(c.too_many, 1);
    for (int i = 0; i < 100; i++) {
        ASSERT_NOT_NULL(r[i].text);
        ASSERT_EQ(r[i].length, i < SSH_KBD_MAX_PROMPTS ? 3u : 0u);
        if (r[i].text) ASSERT_EQ(r[i].text[r[i].length], 0);
    }
    free_responses(r, 100);

    fk_reset(&c);                       /* exactly 8: not too many */
    fill_garbage(r, 8);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 8, p, r);
    ASSERT_EQ(c.too_many, 0);
    ASSERT_EQ(F.asks, 8);
    free_responses(r, 8);
    TEST_END();
}

int test_ssh_kbd_round_cap(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p;
    SshKbdResponseOut r;
    unsigned char st[32];
    fk_reset(&c);
    F.dflt = "ans";
    mk_prompt(&p, st, sizeof st, "Passcode:", 9, 0);
    for (int i = 0; i < SSH_KBD_MAX_ROUNDS; i++) {
        fill_garbage(&r, 1);
        ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
        ASSERT_EQ(r.length, 3u);
        free_responses(&r, 1);
    }
    ASSERT_EQ(F.asks, SSH_KBD_MAX_ROUNDS);
    ASSERT_EQ(c.aborted, 0);
    ASSERT_EQ(F.aborts, 0);
    ASSERT_EQ(F.ntimeouts, SSH_KBD_MAX_ROUNDS);

    fill_garbage(&r, 1);                /* round 17 */
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
    ASSERT_EQ(F.asks, SSH_KBD_MAX_ROUNDS);
    ASSERT_NOT_NULL(r.text);
    ASSERT_EQ(r.length, 0u);
    ASSERT_EQ(c.aborted, 1);
    ASSERT_EQ(F.aborts, 1);
    ASSERT_EQ(F.ntimeouts, SSH_KBD_MAX_ROUNDS);      /* abort_io instead of a re-arm */
    free_responses(&r, 1);

    fill_garbage(&r, 1);                /* round 18: still nothing, no second abort */
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
    ASSERT_EQ(F.asks, SSH_KBD_MAX_ROUNDS);
    ASSERT_EQ(r.length, 0u);
    ASSERT_EQ(F.aborts, 1);
    free_responses(&r, 1);
    TEST_END();
}

int test_ssh_kbd_cancel(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p[3];
    SshKbdResponseOut r[3];
    unsigned char st[3][32];
    for (int i = 0; i < 3; i++) mk_prompt(&p[i], st[i], sizeof st[i], "Passcode:", 9, 0);

    fk_reset(&c);
    F.dflt = "ans";
    F.cancel_at = 1;
    fill_garbage(r, 3);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 3, p, r);
    ASSERT_EQ(F.asks, 2);
    ASSERT_EQ(c.cancelled, 1);
    ASSERT_EQ(F.aborts, 1);
    ASSERT_NOT_NULL(r[0].text);
    ASSERT_EQ(r[0].length, 3u);
    ASSERT_NOT_NULL(r[1].text);
    ASSERT_NOT_NULL(r[2].text);
    ASSERT_EQ(r[1].length, 0u);
    ASSERT_EQ(r[2].length, 0u);
    free_responses(r, 3);

    fill_garbage(r, 3);                 /* a later round asks nothing */
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 3, p, r);
    ASSERT_EQ(F.asks, 2);
    ASSERT_EQ(F.aborts, 1);
    for (int i = 0; i < 3; i++) { ASSERT_NOT_NULL(r[i].text); ASSERT_EQ(r[i].length, 0u); }
    free_responses(r, 3);

    /* the round cap after a cancel does not abort a second time */
    for (int i = 0; i < SSH_KBD_MAX_ROUNDS + 1; i++) {
        fill_garbage(r, 1);
        ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, p, r);
        free_responses(r, 1);
    }
    ASSERT_EQ(F.aborts, 1);

    /* cancelled before any answer: the round still ends with the setup limit */
    fk_reset(&c);
    F.cancel_at = 0;
    fill_garbage(r, 3);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 3, p, r);
    ASSERT_EQ(F.asks, 1);
    ASSERT_EQ(F.ntimeouts, 1);
    ASSERT_TRUE(F.timeouts[0] == 46000L);
    ASSERT_TRUE(c.limit_in_force_ms == 15000L);
    free_responses(r, 3);
    TEST_END();
}

int test_ssh_kbd_rearm_limits(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p;
    SshKbdResponseOut r;
    unsigned char st[32];
    mk_prompt(&p, st, sizeof st, "Passcode:", 9, 0);

    /* an answered round: 60 s */
    fk_reset(&c);
    F.dflt = "123456";
    fill_garbage(&r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
    ASSERT_EQ(F.ntimeouts, 1);
    ASSERT_TRUE(F.timeouts[0] == 91000L);
    ASSERT_TRUE(c.limit_in_force_ms == 60000L);
    free_responses(&r, 1);

    /* the user answered with an empty string: still an answer */
    fk_reset(&c);
    fill_garbage(&r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
    ASSERT_TRUE(F.timeouts[0] == 91000L);
    ASSERT_EQ(c.user_answered, 1);
    free_responses(&r, 1);

    /* a zero-prompt round: the setup limit */
    fk_reset(&c);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 0, NULL, NULL);
    ASSERT_EQ(F.ntimeouts, 1);
    ASSERT_TRUE(F.timeouts[0] == 30000L + 15000L + 1000L);
    ASSERT_TRUE(c.limit_in_force_ms == 15000L);

    /* every prompt auto-answered: the setup limit */
    fk_reset(&c);
    c.saved = "s3cret";
    mk_prompt(&p, st, sizeof st, "Password:", 9, 0);
    fill_garbage(&r, 1);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 1, &p, &r);
    ASSERT_EQ(F.asks, 0);
    ASSERT_TRUE(F.timeouts[0] == 46000L);
    ASSERT_TRUE(c.limit_in_force_ms == 15000L);
    free_responses(&r, 1);

    /* a test override of the setup limit */
    fk_reset(&c);
    c.setup_limit_ms = 300;
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 0, NULL, NULL);
    ASSERT_TRUE(F.timeouts[0] == 31300L);
    ASSERT_TRUE(c.limit_in_force_ms == 300L);
    TEST_END();
}

typedef struct {
    void **slot;
    SshKbdCtx *ctx;
    int calls, slot_ok, rounds_seen;
    int rc;
} AttemptProbe;

static int probe_call(void *arg)
{
    AttemptProbe *a = (AttemptProbe *)arg;
    a->calls++;
    a->slot_ok = (*a->slot == (void *)a->ctx);
    a->rounds_seen = a->ctx->rounds;
    return a->rc;
}

int test_ssh_kbd_attempt_restores_override(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    void *slot = NULL;
    AttemptProbe a;

    fk_reset(&c);
    F.now = 500;
    c.entry = 1; c.limit_in_force_ms = 1;
    c.rounds = 3; c.prompts = 4; c.auto_answered = 5; c.user_answered = 6;
    c.saved_sent = 1; c.cancelled = 1; c.too_many = 1; c.aborted = 1;
    strcpy(c.zero_instruction, "stale");
    memset(c.answer, 'x', sizeof c.answer);
    memset(&a, 0, sizeof a);
    a.slot = &slot; a.ctx = &c; a.rc = 42;
    ASSERT_EQ(ssh_kbd_attempt(&slot, &c, probe_call, &a), 42);
    ASSERT_EQ(a.calls, 1);
    ASSERT_EQ(a.slot_ok, 1);
    ASSERT_EQ(a.rounds_seen, 0);
    ASSERT_NULL(slot);
    ASSERT_TRUE(c.entry == 500);
    ASSERT_EQ(F.ntimeouts, 1);
    ASSERT_TRUE(F.timeouts[0] == 15000L);
    ASSERT_TRUE(c.limit_in_force_ms == 15000L);
    ASSERT_EQ(c.rounds + c.prompts + c.auto_answered + c.user_answered + c.saved_sent
              + c.cancelled + c.too_many + c.aborted, 0);
    ASSERT_STR_EQ(c.zero_instruction, "");
    {
        int clean = 1;
        for (size_t i = 0; i < sizeof c.answer; i++) if (c.answer[i]) clean = 0;
        ASSERT_TRUE(clean);
    }

    /* the override */
    fk_reset(&c);
    c.setup_limit_ms = 300;
    memset(&a, 0, sizeof a);
    a.slot = &slot; a.ctx = &c; a.rc = -7;
    ASSERT_EQ(ssh_kbd_attempt(&slot, &c, probe_call, &a), -7);
    ASSERT_TRUE(F.timeouts[F.ntimeouts - 1] == 300L);
    ASSERT_NULL(slot);

    /* NULL slot, ctx or call: refused, nothing made */
    fk_reset(&c);
    memset(&a, 0, sizeof a);
    a.slot = &slot; a.ctx = &c;
    ASSERT_EQ(ssh_kbd_attempt(NULL, &c, probe_call, &a), -1);
    ASSERT_EQ(ssh_kbd_attempt(&slot, NULL, probe_call, &a), -1);
    ASSERT_EQ(ssh_kbd_attempt(&slot, &c, NULL, &a), -1);
    ASSERT_EQ(a.calls, 0);
    ASSERT_EQ(F.ntimeouts, 0);
    ASSERT_NULL(slot);
    TEST_END();
}

static int all_zero(const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i]) return 0;
    return 1;
}

int test_ssh_kbd_wipes_answers(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p[2];
    SshKbdResponseOut r[2];
    unsigned char st[2][32];
    mk_prompt(&p[0], st[0], sizeof st[0], "Password:", 9, 0);
    mk_prompt(&p[1], st[1], sizeof st[1], "Passcode:", 9, 0);

    fk_reset(&c);
    c.saved = "SENTINEL2";
    F.dflt = "SENTINEL";
    fill_garbage(r, 2);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 2, p, r);
    ASSERT_EQ(F.asks, 1);
    ASSERT_TRUE(all_zero(c.answer, sizeof c.answer));
    ASSERT_NOT_NULL(r[0].text);
    ASSERT_NOT_NULL(r[1].text);
    if (r[0].text) ASSERT_STR_EQ(r[0].text, "SENTINEL2");
    if (r[1].text) ASSERT_STR_EQ(r[1].text, "SENTINEL");
    free_responses(r, 2);

    fk_reset(&c);                       /* typed, then cancelled */
    F.dflt = "SENTINEL";
    F.cancel_at = 0;
    fill_garbage(r, 2);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 2, p, r);
    ASSERT_EQ(c.cancelled, 1);
    ASSERT_TRUE(all_zero(c.answer, sizeof c.answer));
    ASSERT_EQ(r[0].length, 0u);
    free_responses(r, 2);
    TEST_END();
}

/* the stub's and the real header's macro both declare this shape */
static LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC(adapter)
{
    ssh_kbd_round((SshKbdCtx *)*abstract, name, name_len, instruction, instruction_len,
                  num_prompts, prompts, responses);
}

int test_ssh_kbd_adapter_signature(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p;
    SshKbdResponseOut r;
    unsigned char st[32];
    void *slot = &c;
    void **abstract = &slot;
    fk_reset(&c);
    F.dflt = "x1";
    mk_prompt(&p, st, sizeof st, "Passcode:", 9, 0);
    fill_garbage(&r, 1);
    adapter("n", 1, "i", 1, 1, &p, &r, abstract);
    ASSERT_EQ(F.asks, 1);
    ASSERT_NOT_NULL(r.text);
    ASSERT_EQ(r.length, 2u);
    free_responses(&r, 1);
    TEST_END();
}

int test_ssh_kbd_secret_registration(void)
{
    TEST_BEGIN();
    SshKbdCtx c;
    SshKbdPromptIn p[2];
    SshKbdResponseOut r[2];
    unsigned char st[2][32];
    char out[64];
    mk_prompt(&p[0], st[0], sizeof st[0], "Passcode:", 9, 0);
    mk_prompt(&p[1], st[1], sizeof st[1], "Passcode:", 9, 0);

    /* a typed non-echo answer is scrubbed from the next prompt's log, and
     * released when the round returns */
    fk_reset(&c);
    F.ans[0] = "hunter22"; F.nans = 1;
    F.probe = 1;
    fill_garbage(r, 2);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 2, p, r);
    ASSERT_EQ(F.probe_redacted, 1);
    dlog_scrub_copy(out, sizeof out, "x hunter22 y", 12);
    ASSERT_TRUE(strstr(out, "[REDACTED]") == NULL);
    free_responses(r, 2);

    /* an echo answer is not registered */
    fk_reset(&c);
    p[0].echo = 1;
    F.ans[0] = "hunter22"; F.nans = 1;
    F.probe = 1;
    fill_garbage(r, 2);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 2, p, r);
    ASSERT_EQ(F.probe_redacted, 0);
    free_responses(r, 2);

    /* a cancelled round releases what it registered */
    fk_reset(&c);
    p[0].echo = 0;
    F.ans[0] = "hunter22"; F.nans = 1;
    F.cancel_at = 1;
    fill_garbage(r, 2);
    ssh_kbd_round(&c, NULL, 0, NULL, 0, 2, p, r);
    dlog_scrub_copy(out, sizeof out, "x hunter22 y", 12);
    ASSERT_TRUE(strstr(out, "[REDACTED]") == NULL);
    free_responses(r, 2);
    TEST_END();
}
