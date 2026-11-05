/* tests/test_ssh_auth_plan.c -- src/core/ssh_auth_plan.h: the server's
 * auth-method list parser, the pure step plan and the failure text. */
#include "test_framework.h"
#include "ssh_auth_plan.h"

#include <string.h>

#define PK  SSH_AM_PUBLICKEY
#define PW  SSH_AM_PASSWORD
#define KBD SSH_AM_KBDINT

/* Walks the plan to DONE as if every attempt returned -18. A KBDINT step
 * sends the saved password when `kbd_sends` is set. Returns the step count. */
static int walk(SshAuthPlan *p, SshAuthStep *out, int max, int kbd_sends)
{
    int n = 0;
    for (;;) {
        SshAuthStep s = ssh_auth_next(p);
        if (s == SSH_STEP_DONE) break;
        if (n >= max) return -1;
        out[n++] = s;
        ssh_auth_plan_record(p, s,
            s == SSH_STEP_PASSWORD_SAVED || (s == SSH_STEP_KBDINT && kbd_sends));
    }
    return n;
}

static int expect_walk(int is_key, const char *saved, unsigned offered,
                       int kbd_sends, const SshAuthStep *want, int nwant)
{
    SshAuthPlan p;
    SshAuthStep got[16];
    ssh_auth_plan_init(&p, is_key, saved, offered);
    int n = walk(&p, got, 16, kbd_sends);
    if (n != nwant) return 0;
    for (int i = 0; i < n; i++)
        if (got[i] != want[i]) return 0;
    return 1;
}

int test_ssh_auth_parse(void)
{
    TEST_BEGIN();
    char d[SSH_AUTH_DISPLAY_MAX];

    memset(d, 'x', sizeof d);
    ASSERT_EQ(ssh_auth_methods_parse(NULL, d, sizeof d), 0u);
    ASSERT_STR_EQ(d, "");
    ASSERT_EQ(ssh_auth_methods_parse("", d, sizeof d), 0u);
    ASSERT_STR_EQ(d, "");
    ASSERT_EQ(ssh_auth_methods_parse("publickey", d, sizeof d), PK);
    ASSERT_STR_EQ(d, "publickey");
    ASSERT_EQ(ssh_auth_methods_parse("publickey,password,keyboard-interactive",
                                     d, sizeof d), PK | PW | KBD);
    ASSERT_STR_EQ(d, "publickey, password, keyboard-interactive");
    ASSERT_EQ(ssh_auth_methods_parse(" password , ,password,,", d, sizeof d), PW);
    ASSERT_STR_EQ(d, "password");
    ASSERT_EQ(ssh_auth_methods_parse("hostbased,gssapi-with-mic", d, sizeof d),
              SSH_AM_OTHER);
    ASSERT_STR_EQ(d, "hostbased, gssapi-with-mic");

    /* A control byte inside a name: unknown, and never echoed. */
    ASSERT_EQ(ssh_auth_methods_parse("pass\x1bword", d, sizeof d), SSH_AM_OTHER);
    ASSERT_TRUE(strchr(d, '\x1b') == NULL);
    ASSERT_STR_EQ(d, "password");

    /* A NULL display is allowed; only the bit set comes back. */
    ASSERT_EQ(ssh_auth_methods_parse("password,keyboard-interactive", NULL, 0),
              PW | KBD);

    /* A 300-byte list is cut on an item boundary under the cap. */
    char longlist[400];
    size_t off = 0;
    for (int i = 0; off < 300; i++)
        off += (size_t)snprintf(longlist + off, sizeof longlist - off,
                                "%smethod%03d", i ? "," : "", i);
    ASSERT_TRUE(strlen(longlist) >= 300);
    memset(d, 'x', sizeof d);
    unsigned bits = ssh_auth_methods_parse(longlist, d, SSH_AUTH_DISPLAY_MAX);
    ASSERT_EQ(bits, SSH_AM_OTHER);
    ASSERT_TRUE(strlen(d) <= 127);
    ASSERT_TRUE(strlen(d) > 100);
    ASSERT_TRUE(d[strlen(d) - 1] != ',' && d[strlen(d) - 1] != ' ');
    ASSERT_TRUE(strncmp(d + strlen(d) - 9, "method", 6) == 0);

    /* A tiny buffer is still NUL-terminated. */
    char small[8];
    memset(small, 'x', sizeof small);
    ssh_auth_methods_parse("publickey,password", small, sizeof small);
    ASSERT_TRUE(memchr(small, '\0', sizeof small) != NULL);
    ASSERT_STR_EQ(small, "");
    TEST_END();
}

int test_ssh_auth_sequence_table(void)
{
    TEST_BEGIN();
    /* key profile */
    { SshAuthStep w[] = { SSH_STEP_PUBLICKEY };
      ASSERT_TRUE(expect_walk(1, "pp", PK, 0, w, 1)); }
    { SshAuthStep w[] = { SSH_STEP_PUBLICKEY, SSH_STEP_KBDINT };
      ASSERT_TRUE(expect_walk(1, "pp", PK | KBD, 0, w, 2)); }
    { SshAuthStep w[] = { SSH_STEP_PUBLICKEY, SSH_STEP_KBDINT };
      ASSERT_TRUE(expect_walk(1, "pp", PK | PW | KBD, 0, w, 2)); }
    { SshAuthStep w[] = { SSH_STEP_KBDINT };
      ASSERT_TRUE(expect_walk(1, "pp", KBD, 0, w, 1)); }
    ASSERT_TRUE(expect_walk(1, "pp", PW, 0, NULL, 0));

    /* password profile, saved password: password method first */
    { SshAuthStep w[] = { SSH_STEP_PASSWORD_SAVED, SSH_STEP_KBDINT, SSH_STEP_PASSWORD_ASK };
      ASSERT_TRUE(expect_walk(0, "pw", PW | KBD, 0, w, 3));
      ASSERT_TRUE(expect_walk(0, "pw", PK | PW | KBD, 0, w, 3)); }
    { SshAuthStep w[] = { SSH_STEP_PASSWORD_SAVED, SSH_STEP_PASSWORD_ASK };
      ASSERT_TRUE(expect_walk(0, "pw", PW, 0, w, 2)); }
    { SshAuthStep w[] = { SSH_STEP_KBDINT, SSH_STEP_KBDINT_RETRY };
      ASSERT_TRUE(expect_walk(0, "pw", KBD, 1, w, 2)); }
    { SshAuthStep w[] = { SSH_STEP_KBDINT };
      ASSERT_TRUE(expect_walk(0, "pw", KBD, 0, w, 1)); }

    /* password profile, no saved password: keyboard-interactive first */
    { SshAuthStep w[] = { SSH_STEP_KBDINT, SSH_STEP_PASSWORD_ASK };
      ASSERT_TRUE(expect_walk(0, NULL, PW | KBD, 0, w, 2)); }
    { SshAuthStep w[] = { SSH_STEP_PASSWORD_ASK };
      ASSERT_TRUE(expect_walk(0, NULL, PW, 0, w, 1));
      ASSERT_TRUE(expect_walk(0, "", PW, 0, w, 1)); }
    { SshAuthStep w[] = { SSH_STEP_KBDINT };
      ASSERT_TRUE(expect_walk(0, NULL, KBD, 0, w, 1)); }

    /* nothing usable on offer */
    ASSERT_TRUE(expect_walk(0, "pw", 0, 0, NULL, 0));
    ASSERT_TRUE(expect_walk(1, "pp", 0, 0, NULL, 0));
    ASSERT_TRUE(expect_walk(0, "pw", SSH_AM_OTHER, 0, NULL, 0));
    ASSERT_TRUE(expect_walk(0, "pw", PK, 0, NULL, 0));
    TEST_END();
}

int test_ssh_auth_attempt_cap(void)
{
    TEST_BEGIN();
    SshAuthPlan p;
    ssh_auth_plan_init(&p, 1, "pp", PK | KBD);
    p.attempts = SSH_AUTH_MAX_ATTEMPTS;
    ASSERT_EQ(ssh_auth_next(&p), SSH_STEP_DONE);
    ssh_auth_plan_init(&p, 0, "pw", PW | KBD);
    p.attempts = SSH_AUTH_MAX_ATTEMPTS;
    ASSERT_EQ(ssh_auth_next(&p), SSH_STEP_DONE);
    ssh_auth_plan_init(&p, 0, NULL, PW | KBD);
    p.attempts = SSH_AUTH_MAX_ATTEMPTS;
    ASSERT_EQ(ssh_auth_next(&p), SSH_STEP_DONE);

    ssh_auth_plan_init(&p, 1, "pp", PK | KBD);
    ASSERT_EQ(p.attempts, 0);
    ssh_auth_plan_record(&p, SSH_STEP_PUBLICKEY, 0);
    ASSERT_EQ(p.attempts, 1);
    ssh_auth_plan_count_retry(&p);
    ASSERT_EQ(p.attempts, 2);
    ASSERT_EQ(ssh_auth_next(&p), SSH_STEP_KBDINT);
    ssh_auth_plan_record(&p, SSH_STEP_KBDINT, 0);
    ASSERT_EQ(p.attempts, 3);

    /* The passphrase retries can use up the cap before the next step. */
    ssh_auth_plan_init(&p, 1, "pp", PK | KBD);
    ssh_auth_plan_record(&p, SSH_STEP_PUBLICKEY, 0);
    ssh_auth_plan_count_retry(&p);
    ssh_auth_plan_count_retry(&p);
    ssh_auth_plan_count_retry(&p);
    ASSERT_EQ(ssh_auth_next(&p), SSH_STEP_DONE);
    TEST_END();
}

int test_ssh_auth_saved_for(void)
{
    TEST_BEGIN();
    SshAuthPlan p;

    ssh_auth_plan_init(&p, 0, "secret", PW | KBD);
    ASSERT_STR_EQ(ssh_auth_saved_for(&p, SSH_STEP_PASSWORD_SAVED, "secret"), "secret");
    ssh_auth_plan_record(&p, SSH_STEP_PASSWORD_SAVED, 1);
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_KBDINT, "secret"));
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_PASSWORD_ASK, "secret"));

    ssh_auth_plan_init(&p, 0, "secret", KBD);
    ASSERT_STR_EQ(ssh_auth_saved_for(&p, SSH_STEP_KBDINT, "secret"), "secret");
    ssh_auth_plan_record(&p, SSH_STEP_KBDINT, 1);
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_KBDINT_RETRY, "secret"));
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_PASSWORD_ASK, "secret"));

    /* A KBDINT that did not send it leaves it available. */
    ssh_auth_plan_init(&p, 0, "secret", PW | KBD);
    ssh_auth_plan_record(&p, SSH_STEP_KBDINT, 0);
    ASSERT_STR_EQ(ssh_auth_saved_for(&p, SSH_STEP_PASSWORD_SAVED, "secret"), "secret");

    /* A key profile's password field is a passphrase: never an answer. */
    ssh_auth_plan_init(&p, 1, "passphrase", PK | PW | KBD);
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_PUBLICKEY, "passphrase"));
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_KBDINT, "passphrase"));
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_PASSWORD_SAVED, "passphrase"));
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_PASSWORD_ASK, "passphrase"));

    /* An empty saved password is none. */
    ssh_auth_plan_init(&p, 0, "", PW | KBD);
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_KBDINT, ""));
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_PASSWORD_SAVED, ""));
    ssh_auth_plan_init(&p, 0, NULL, PW | KBD);
    ASSERT_NULL(ssh_auth_saved_for(&p, SSH_STEP_KBDINT, NULL));
    TEST_END();
}

int test_ssh_auth_transport_dead(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ssh_auth_transport_dead(-7), 1);
    ASSERT_EQ(ssh_auth_transport_dead(-13), 1);
    ASSERT_EQ(ssh_auth_transport_dead(-43), 1);
    ASSERT_EQ(ssh_auth_transport_dead(0), 0);
    ASSERT_EQ(ssh_auth_transport_dead(-9), 0);
    ASSERT_EQ(ssh_auth_transport_dead(-16), 0);
    ASSERT_EQ(ssh_auth_transport_dead(-18), 0);
    ASSERT_EQ(ssh_auth_transport_dead(-19), 0);
    ASSERT_EQ(ssh_auth_transport_dead(-48), 0);
    TEST_END();
}

int test_ssh_auth_may_continue(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ssh_auth_may_continue(-18, 0, -18), 1);
    ASSERT_EQ(ssh_auth_may_continue(-19, 0, -19), 1);
    ASSERT_EQ(ssh_auth_may_continue(-18, 1, -9), 0);
    ASSERT_EQ(ssh_auth_may_continue(-18, 0, -43), 0);
    ASSERT_EQ(ssh_auth_may_continue(-18, 0, -7), 0);
    ASSERT_EQ(ssh_auth_may_continue(-18, 0, -13), 0);
    ASSERT_EQ(ssh_auth_may_continue(-16, 0, -16), 0);
    ASSERT_EQ(ssh_auth_may_continue(-48, 0, -48), 0);
    ASSERT_EQ(ssh_auth_may_continue(-48, 0, -18), 0);
    ASSERT_EQ(ssh_auth_may_continue(-7, 0, -7), 0);
    ASSERT_EQ(ssh_auth_may_continue(-7, 0, -18), 0);
    ASSERT_EQ(ssh_auth_may_continue(-1, 0, -1), 0);
    ASSERT_EQ(ssh_auth_may_continue(0, 0, 0), 0);
    ASSERT_EQ(ssh_auth_may_continue(0, 0, -18), 0);
    TEST_END();
}

int test_ssh_auth_requery_rule(void)
{
    TEST_BEGIN();
    SshAuthPlan p;

    ssh_auth_plan_init(&p, 1, "pp", PK);
    ssh_auth_plan_record(&p, SSH_STEP_PUBLICKEY, 0);
    ASSERT_EQ(ssh_auth_should_requery(&p, -19, 0, -19), 1);
    ASSERT_EQ(ssh_auth_should_requery(&p, -18, 0, -18), 1);
    ssh_auth_plan_requeried(&p, KBD);
    ASSERT_EQ(p.requeried, 1);
    ASSERT_EQ(p.offered, KBD);
    ASSERT_EQ(ssh_auth_next(&p), SSH_STEP_KBDINT);
    ssh_auth_plan_record(&p, SSH_STEP_KBDINT, 0);
    ASSERT_EQ(ssh_auth_should_requery(&p, -18, 0, -18), 0);   /* once only */

    /* Steps are left: no re-query yet. */
    ssh_auth_plan_init(&p, 1, "pp", PK | KBD);
    ssh_auth_plan_record(&p, SSH_STEP_PUBLICKEY, 0);
    ASSERT_EQ(ssh_auth_should_requery(&p, -19, 0, -19), 0);

    /* Timeout, dead transport, a file error or the cap: never. */
    ssh_auth_plan_init(&p, 1, "pp", PK);
    ssh_auth_plan_record(&p, SSH_STEP_PUBLICKEY, 0);
    ASSERT_EQ(ssh_auth_should_requery(&p, -19, 1, -9), 0);
    ASSERT_EQ(ssh_auth_should_requery(&p, -19, 0, -43), 0);
    ASSERT_EQ(ssh_auth_should_requery(&p, -16, 0, -16), 0);
    ASSERT_EQ(ssh_auth_should_requery(&p, -48, 0, -48), 0);
    p.attempts = SSH_AUTH_MAX_ATTEMPTS;
    ASSERT_EQ(ssh_auth_should_requery(&p, -19, 0, -19), 0);
    TEST_END();
}

int test_ssh_auth_fail_text(void)
{
    TEST_BEGIN();
    char buf[512];
    size_t n;

    n = ssh_auth_fail_text("alice", "host", 0, PK | KBD,
                           "publickey, keyboard-interactive", NULL, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "Authentication failed for alice@host. "
                       "The server offers: publickey, keyboard-interactive.");
    ASSERT_EQ(n, strlen(buf));

    n = ssh_auth_fail_text("alice", "host", 1, PW, "password", NULL, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "Authentication failed for alice@host. "
                       "The server does not offer key authentication; it offers: password.");
    ASSERT_EQ(n, strlen(buf));

    /* A key profile whose server does offer publickey gets the plain clause. */
    ssh_auth_fail_text("alice", "host", 1, PK, "publickey", NULL, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "Authentication failed for alice@host. The server offers: publickey.");

    ssh_auth_fail_text("alice", "host", 0, PK | KBD, "publickey, keyboard-interactive",
                       "Too many authentication prompts.", buf, sizeof buf);
    ASSERT_STR_EQ(buf, "Authentication failed for alice@host. "
                       "The server offers: publickey, keyboard-interactive. "
                       "Too many authentication prompts.");

    ssh_auth_fail_text("alice", "host", 0, 0, "", NULL, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "Authentication failed for alice@host.");

    ssh_auth_fail_text("alice", "host", 0, 0, "", "Please enter your token.", buf, sizeof buf);
    ASSERT_STR_EQ(buf, "Authentication failed for alice@host. Please enter your token.");

    ssh_auth_fail_text("alice", "host", 0, 0, NULL, "", buf, sizeof buf);
    ASSERT_STR_EQ(buf, "Authentication failed for alice@host.");

    /* Too small a buffer: nothing, and a terminated empty string. */
    char tiny[16];
    memset(tiny, 'x', sizeof tiny);
    n = ssh_auth_fail_text("alice", "host", 0, PK | KBD,
                           "publickey, keyboard-interactive", NULL, tiny, sizeof tiny);
    ASSERT_EQ(n, 0u);
    ASSERT_STR_EQ(tiny, "");
    ASSERT_EQ(ssh_auth_fail_text("a", "b", 0, 0, "", NULL, buf, 0), 0u);
    TEST_END();
}

/* ---- the password-like matcher ---------------------------------------- */

#define PWQ(t) ssh_kbd_prompt_is_password(t)

int test_ssh_kbd_matcher_positive(void)
{
    TEST_BEGIN();
    ASSERT_EQ(PWQ("Password:"), 1);
    ASSERT_EQ(PWQ("password"), 1);
    ASSERT_EQ(PWQ("  PASSWORD:  "), 1);
    ASSERT_EQ(PWQ("user@host's password:"), 1);
    ASSERT_EQ(PWQ("alice@NEWCASTLE.AC.UK's Password:"), 1);
    ASSERT_EQ(PWQ("new@h's password:"), 1);     /* a one-letter host still strips */
    ASSERT_EQ(PWQ("SPINNAKER password:"), 1);
    ASSERT_EQ(PWQ("Password for alice:"), 1);
    ASSERT_EQ(PWQ("LDAP Password:"), 1);
    ASSERT_EQ(PWQ("Password for pin:"), 1);
    /* Several colons and spaces after the word are all dropped. */
    ASSERT_EQ(PWQ("Password: :  "), 1);
    /* The whole prompt joined to one line: earlier text, then the label. */
    ASSERT_EQ(PWQ("Acme login for alice Password:"), 1);
    ASSERT_EQ(PWQ("Welcome to host Password:"), 1);
    /* The current or old password is still the saved one. */
    ASSERT_EQ(PWQ("Current password:"), 1);
    ASSERT_EQ(PWQ("Old password:"), 1);
    TEST_END();
}

int test_ssh_kbd_matcher_negative(void)
{
    TEST_BEGIN();
    ASSERT_EQ(PWQ("PASSCODE:"), 0);
    ASSERT_EQ(PWQ("One-time password:"), 0);
    ASSERT_EQ(PWQ("New password:"), 0);
    ASSERT_EQ(PWQ("Retype new password:"), 0);
    ASSERT_EQ(PWQ("Verification code:"), 0);
    ASSERT_EQ(PWQ("OTP password:"), 0);
    ASSERT_EQ(PWQ("Token password:"), 0);
    ASSERT_EQ(PWQ("PIN password:"), 0);
    ASSERT_EQ(PWQ("Password expired, enter code:"), 0);
    ASSERT_EQ(PWQ("Username:"), 0);
    ASSERT_EQ(PWQ(""), 0);
    ASSERT_EQ(PWQ(NULL), 0);
    ASSERT_EQ(PWQ("passwords:"), 0);
    ASSERT_EQ(PWQ("xpassword:"), 0);
    /* Only separators, or a prefix and nothing else. */
    ASSERT_EQ(PWQ(":"), 0);
    ASSERT_EQ(PWQ("   "), 0);
    ASSERT_EQ(PWQ("alice@host's :"), 0);
    ASSERT_EQ(PWQ("for alice:"), 0);
    /* The whole prompt joined to one line: a "new" earlier in it still counts. */
    ASSERT_EQ(PWQ("Enter new account password:"), 0);
    ASSERT_EQ(PWQ("Your password has expired. New password:"), 0);
    /* Other one-time, second-factor and confirmation spellings. */
    ASSERT_EQ(PWQ("One time password:"), 0);
    ASSERT_EQ(PWQ("Onetime password:"), 0);
    ASSERT_EQ(PWQ("SMS password:"), 0);
    ASSERT_EQ(PWQ("Duo password:"), 0);
    ASSERT_EQ(PWQ("Temporary password:"), 0);
    ASSERT_EQ(PWQ("Confirm password:"), 0);
    ASSERT_EQ(PWQ("Repeat password:"), 0);
    ASSERT_EQ(PWQ("Verify password:"), 0);
    ASSERT_EQ(PWQ("MFA password:"), 0);
    ASSERT_EQ(PWQ("2FA password:"), 0);
    TEST_END();
}

int test_ssh_kbd_matcher_lookalikes(void)
{
    TEST_BEGIN();
    ASSERT_EQ(PWQ("Password (new):"), 0);
    ASSERT_EQ(PWQ("Passcode or password:"), 0);
    ASSERT_EQ(PWQ("Enter PIN password:"), 0);
    ASSERT_EQ(PWQ("Passcode or option (1-3):"), 0);
    ASSERT_EQ(PWQ("Password for new account:"), 0);
    /* "-" is a word boundary, so a hyphenated forbidden word still counts. */
    ASSERT_EQ(PWQ("pin-code password:"), 0);
    ASSERT_EQ(PWQ("One-time-password:"), 0);
    /* A forbidden word inside a longer word does not. */
    ASSERT_EQ(PWQ("Newton password:"), 1);
    ASSERT_EQ(PWQ("Opinion password:"), 1);
    TEST_END();
}

/* ---- the sanitiser ------------------------------------------------------ */

/* Sanitises a NUL-terminated literal with a roomy buffer; the result lives
 * in a static buffer until the next call. */
static const char *san_n(const char *in, size_t len, int single)
{
    static char out[4096];
    size_t n = ssh_kbd_sanitize(in, len, out, sizeof out, single);
    if (n != strlen(out)) return "<length mismatch>";
    return out;
}
static const char *san(const char *in) { return san_n(in, strlen(in), 0); }

int test_ssh_kbd_sanitize_escapes(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(san("a\x1b[31mb\x1b[0m"), "ab");
    ASSERT_STR_EQ(san("a\x1b]0;title\x07" "b"), "ab");
    ASSERT_STR_EQ(san("a\x1b]0;t\x1b\\" "b"), "ab");
    ASSERT_STR_EQ(san("a\x1b" "Zb"), "ab");
    ASSERT_STR_EQ(san("ab\x1b"), "ab");           /* lone trailing ESC */
    ASSERT_STR_EQ(san("\x01" "a\x7f"), "a");
    /* A CSI with parameters and intermediates, an unterminated one, and an
     * unterminated OSC swallow only their own bytes / the rest. */
    ASSERT_STR_EQ(san("a\x1b[1;2;3Hb"), "ab");
    ASSERT_STR_EQ(san("a\x1b[1;2"), "a");
    ASSERT_STR_EQ(san("a\x1b]0;never ends"), "a");
    /* ESC ESC [ m: the first ESC is lone, the second starts the CSI. */
    ASSERT_STR_EQ(san("a\x1b\x1b[0mb"), "ab");
    /* An embedded NUL is a control byte; the input is read for `len` bytes. */
    ASSERT_STR_EQ(san_n("a\0b", 3, 0), "ab");
    /* A string that is only escapes is empty. */
    ASSERT_STR_EQ(san("\x1b[2J\x1b[H"), "");
    TEST_END();
}

int test_ssh_kbd_sanitize_lines(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(san("a\r\nb\rc\n"), "a\nb\nc");
    ASSERT_STR_EQ(san("\n\n a  \n"), " a");
    ASSERT_STR_EQ(san("a\tb"), "a b");
    ASSERT_STR_EQ(san_n("a\nb", 3, 1), "a b");
    /* Inner empty lines stay; trailing blanks go on every line. */
    ASSERT_STR_EQ(san("a  \n\nb \t\n"), "a\n\nb");
    ASSERT_STR_EQ(san("a  b"), "a  b");
    ASSERT_STR_EQ(san("\r\n\r\n"), "");
    ASSERT_STR_EQ(san("   \n  "), "");
    /* Single line: an inner blank line becomes two spaces, the edges none. */
    ASSERT_STR_EQ(san_n("\na\n\nb\n", 6, 1), "a  b");
    /* NULL with len 0, and an empty string, give "". */
    ASSERT_STR_EQ(san_n(NULL, 0, 0), "");
    ASSERT_STR_EQ(san(""), "");
    /* `in` is not NUL-terminated: only `len` bytes are read. */
    ASSERT_STR_EQ(san_n("abcdef", 3, 0), "abc");
    /* cap 0 writes nothing and returns 0; cap 1 gives "". */
    char b[4] = {'x', 'x', 'x', 'x'};
    ASSERT_EQ(ssh_kbd_sanitize("abc", 3, b, 0, 0), 0u);
    ASSERT_EQ(b[0], 'x');
    ASSERT_EQ(ssh_kbd_sanitize("abc", 3, b, 1, 0), 0u);
    ASSERT_EQ(b[0], '\0');
    TEST_END();
}

int test_ssh_kbd_sanitize_utf8_and_cap(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(san("\xC3\xA9"), "\xC3\xA9");
    ASSERT_STR_EQ(san("\xC3("), "?(");
    ASSERT_STR_EQ(san("\xFF"), "?");
    ASSERT_STR_EQ(san("\xE2\x82\xAC"), "\xE2\x82\xAC");            /* euro sign */
    ASSERT_STR_EQ(san("\xF0\x9F\x98\x80"), "\xF0\x9F\x98\x80");     /* 4 bytes */
    ASSERT_STR_EQ(san("\xC0\x80"), "??");                           /* overlong */
    ASSERT_STR_EQ(san("\xED\xA0\x80"), "???");                      /* surrogate */
    ASSERT_STR_EQ(san("a\xE2\x82"), "a??");                         /* cut short */
    ASSERT_STR_EQ(san("\x80"), "?");                                /* stray tail */

    char out[2048];
    char big[5000];
    memset(big, 'a', sizeof big);
    size_t n = ssh_kbd_sanitize(big, sizeof big, out, sizeof out, 0);
    ASSERT_EQ(n, 2047u);
    ASSERT_EQ(strlen(out), 2047u);
    ASSERT_STR_EQ(out + 2044, "...");
    ASSERT_EQ(out[2043], 'a');

    /* Exactly cap - 1 bytes of content is not truncated. */
    n = ssh_kbd_sanitize(big, 2047, out, sizeof out, 0);
    ASSERT_EQ(n, 2047u);
    ASSERT_EQ(out[2046], 'a');
    ASSERT_EQ(out[2044], 'a');
    /* One byte more is. */
    n = ssh_kbd_sanitize(big, 2048, out, sizeof out, 0);
    ASSERT_EQ(n, 2047u);
    ASSERT_STR_EQ(out + 2044, "...");
    /* Trailing blanks past the cap are trimmed, not truncation. */
    char padded[3000];
    memset(padded, 'a', 2047);
    memset(padded + 2047, ' ', 500);
    n = ssh_kbd_sanitize(padded, 2547, out, sizeof out, 0);
    ASSERT_EQ(n, 2047u);
    ASSERT_EQ(out[2044], 'a');

    /* A cut that would land inside the euro sign leaves no partial sequence. */
    char eur[64];
    char in[64];
    memcpy(in, "aaaaaaaaaa" "\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC", 22);
    n = ssh_kbd_sanitize(in, 22, eur, 15, 0);      /* cut at byte 11: 2nd byte */
    ASSERT_STR_EQ(eur, "aaaaaaaaaa...");
    ASSERT_EQ(n, 13u);
    n = ssh_kbd_sanitize(in, 22, eur, 16, 0);      /* cut at byte 12: 3rd byte */
    ASSERT_STR_EQ(eur, "aaaaaaaaaa...");
    ASSERT_EQ(n, 13u);
    n = ssh_kbd_sanitize(in, 22, eur, 17, 0);      /* cut at byte 13: a boundary */
    ASSERT_STR_EQ(eur, "aaaaaaaaaa\xE2\x82\xAC...");
    ASSERT_EQ(n, 16u);

    /* A tiny cap still terminates; the marker shrinks to fit. */
    n = ssh_kbd_sanitize("abcdef", 6, eur, 3, 0);
    ASSERT_STR_EQ(eur, "..");
    ASSERT_EQ(n, 2u);
    n = ssh_kbd_sanitize("abcdef", 6, eur, 4, 0);
    ASSERT_STR_EQ(eur, "...");
    ASSERT_EQ(n, 3u);
    n = ssh_kbd_sanitize("abcdef", 6, eur, 5, 0);
    ASSERT_STR_EQ(eur, "a...");
    ASSERT_EQ(n, 4u);
    TEST_END();
}
