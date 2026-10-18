/* tests/test_dlog_redact.c -- the final-line secret scanner (ported from the
 * second implementation), the pure line formatter, and the folder
 * relativiser used by the Browse button. */
#include "test_framework.h"
#include "dlog.h"
#include <stdio.h>
#include <string.h>

static void red(const char *in, char *out, size_t cap)
{
    size_t n = strlen(in);
    memcpy(out, in, n + 1);
    size_t m = dlog_redact_line(out, n, cap);
    out[m] = '\0';
}

int test_dlog_redact_line_key_values(void)
{
    TEST_BEGIN();
    char o[512];
    red("x password=hunter2 y", o, sizeof(o));
    ASSERT_STR_EQ(o, "x password=[REDACTED] y");
    red("a passphrase=abc secret=def token=ghi api_key=jkl apikey=mno", o, sizeof(o));
    ASSERT_STR_EQ(o, "a passphrase=[REDACTED] secret=[REDACTED] token=[REDACTED] api_key=[REDACTED] apikey=[REDACTED]");
    red("authorization=Basic private_key=xyz pass=1", o, sizeof(o));
    ASSERT_STR_EQ(o, "authorization=[REDACTED] private_key=[REDACTED] pass=[REDACTED]");
    TEST_END();
}

int test_dlog_redact_line_key_is_case_insensitive_and_whole_word(void)
{
    TEST_BEGIN();
    char o[512];
    red("PassWord=abc API_KEY=def", o, sizeof(o));
    ASSERT_STR_EQ(o, "PassWord=[REDACTED] API_KEY=[REDACTED]");
    red("keystroke_ms=5 token_count=7 passwords=2 password_hint=x mypassword=1", o, sizeof(o));
    ASSERT_STR_EQ(o, "keystroke_ms=5 token_count=7 passwords=2 password_hint=x mypassword=1");
    TEST_END();
}

int test_dlog_redact_line_quoted_values_and_empty_values(void)
{
    TEST_BEGIN();
    char o[512];
    red("e password=\"two words\" next=1", o, sizeof(o));
    ASSERT_STR_EQ(o, "e password=[REDACTED] next=1");
    red("e password=\"say \\\"hi\\\" now\" next=1", o, sizeof(o));
    ASSERT_STR_EQ(o, "e password=[REDACTED] next=1");
    red("e password= next=1", o, sizeof(o));
    ASSERT_STR_EQ(o, "e password= next=1");                   /* nothing to hide */
    TEST_END();
}

int test_dlog_redact_line_key_inside_a_quoted_content_value(void)
{
    TEST_BEGIN();
    char o[512];
    red("term.rx data=\"login: x password=abc123 ok\" len=30", o, sizeof(o));
    ASSERT_TRUE(strstr(o, "abc123") == NULL);
    ASSERT_TRUE(strstr(o, "password=[REDACTED]") != NULL);
    TEST_END();
}

int test_dlog_redact_line_bearer_and_sk_keys(void)
{
    TEST_BEGIN();
    char o[512];
    red("hdr Authorization: Bearer abc.DEF-123_xyz== tail", o, sizeof(o));
    ASSERT_TRUE(strstr(o, "abc.DEF") == NULL);
    ASSERT_TRUE(strstr(o, "Bearer [REDACTED] tail") != NULL);
    red("key sk-abcdefghijklmnop1234 and sk-short", o, sizeof(o));
    ASSERT_TRUE(strstr(o, "abcdefghijklmnop") == NULL);
    ASSERT_TRUE(strstr(o, "sk-short") != NULL);               /* too short to be a key */
    red("lower bearer tok123 x", o, sizeof(o));
    ASSERT_TRUE(strstr(o, "tok123") == NULL);
    TEST_END();
}

int test_dlog_redact_line_encrypted_blobs_and_pem(void)
{
    TEST_BEGIN();
    char o[1024];
    red("cfg blob=$dpapi$v1$AAAABBBBCCCC== end", o, sizeof(o));
    ASSERT_TRUE(strstr(o, "AAAABBBB") == NULL);
    ASSERT_TRUE(strstr(o, " end") != NULL);
    red("cfg blob=$aes256gcm$v1$ZZZZ end", o, sizeof(o));
    ASSERT_TRUE(strstr(o, "ZZZZ") == NULL);
    red("k -----BEGIN OPENSSH PRIVATE KEY----- abcdef -----END OPENSSH PRIVATE KEY----- tail", o, sizeof(o));
    ASSERT_TRUE(strstr(o, "abcdef") == NULL);
    ASSERT_TRUE(strstr(o, "tail") != NULL);
    red("k -----BEGIN PRIVATE KEY----- no end here", o, sizeof(o));
    ASSERT_TRUE(strstr(o, "no end here") == NULL);
    red("pub -----BEGIN PUBLIC KEY----- fine", o, sizeof(o));
    ASSERT_TRUE(strstr(o, "fine") != NULL);                   /* only private keys */
    TEST_END();
}

int test_dlog_redact_line_keeps_the_newline_and_is_idempotent(void)
{
    TEST_BEGIN();
    char o[512];
    char line[] = "e password=abc\n";
    size_t m = dlog_redact_line(line, strlen(line), sizeof(line) + 40);
    ASSERT_EQ(m, strlen("e password=[REDACTED]\n"));
    ASSERT_STR_EQ(line, "e password=[REDACTED]\n");
    size_t m2 = dlog_redact_line(line, m, sizeof(line) + 40);
    ASSERT_EQ(m2, m);
    red("nothing to hide here", o, sizeof(o));
    ASSERT_STR_EQ(o, "nothing to hide here");
    ASSERT_EQ(dlog_redact_line(NULL, 5, 10), (size_t)5);
    ASSERT_EQ(dlog_redact_line(o, 0, 10), (size_t)0);
    TEST_END();
}

int test_dlog_redact_line_growth_is_capped_and_still_a_line(void)
{
    TEST_BEGIN();
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "e a=1 password=x password=y password=z\n");
    size_t n = strlen(buf);
    size_t m = dlog_redact_line(buf, n, 40);                  /* the redacted form does not fit in 40 */
    ASSERT_TRUE(m < 40);
    ASSERT_EQ(buf[m - 1], '\n');
    ASSERT_EQ(buf[m], '\0');
    TEST_END();
}

/* ---- dlog_format_line --------------------------------------------------------------- */

static long long t_ms(void)
{
    DlogTm tm = { 2026, 10, 17, 13, 3, 22, 418 };
    return dlog_unix_ms_from_civil(&tm, 0);
}

int test_dlog_format_line_exact(void)
{
    TEST_BEGIN();
    char b[512];
    size_t n = dlog_format_line(b, sizeof(b), t_ms(), 60, DLOG_TRACE, DLOG_RENDER, 2, "ui", "poll.cooldown_skip", "poll_rc=1 dirty_rows=73");
    ASSERT_STR_EQ(b, "2026-10-17T14:03:22.418+01:00 TRACE render s2 ui poll.cooldown_skip poll_rc=1 dirty_rows=73\n");
    ASSERT_EQ(n, strlen(b));
    n = dlog_format_line(b, sizeof(b), t_ms(), -330, DLOG_WARN, DLOG_SHELL, 0, "pty", "child.exit", "");
    ASSERT_STR_EQ(b, "2026-10-17T07:33:22.418-05:30 WARN  shell - pty child.exit\n");
    n = dlog_format_line(b, sizeof(b), t_ms(), 0, DLOG_ISSUE, DLOG_GENERAL, 0, NULL, NULL, NULL);
    ASSERT_STR_EQ(b, "2026-10-17T13:03:22.418+00:00 ISSUE general - ? -\n");
    TEST_END();
}

int test_dlog_format_line_never_splits_on_control_bytes(void)
{
    TEST_BEGIN();
    char b[512];
    (void)dlog_format_line(b, sizeof(b), t_ms(), 0, DLOG_INFO, DLOG_AI, 0, "ai", "e", "msg=a\nb\rc\x01" "d");
    ASSERT_EQ(strchr(b, '\n'), b + strlen(b) - 1);         /* only the final LF */
    ASSERT_TRUE(strstr(b, "a\\x0ab\\x0dc\\x01d") != NULL);
    TEST_END();
}

int test_dlog_format_line_cuts_long_fields_with_truncated_marker(void)
{
    TEST_BEGIN();
    static char fields[9000];
    memset(fields, 'f', sizeof(fields) - 1);
    memcpy(fields, "k=", 2);
    fields[sizeof(fields) - 1] = '\0';
    static char b[9000];
    size_t n = dlog_format_line(b, sizeof(b), t_ms(), 0, DLOG_INFO, DLOG_AI, 1, "ai", "e", fields);
    ASSERT_TRUE(n <= (size_t)DLOG_LINE_MAX);
    ASSERT_TRUE(n > (size_t)DLOG_LINE_MAX - 40);
    ASSERT_STR_EQ(b + n - 13, " truncated=1\n");
    ASSERT_EQ(b[n], '\0');
    TEST_END();
}

int test_dlog_format_line_cut_keeps_escapes_and_utf8_whole(void)
{
    TEST_BEGIN();
    static char fields[9000];
    size_t o = 0;
    memcpy(fields, "k=\"", 3);
    o = 3;
    while (o + 8 < sizeof(fields)) { fields[o++] = (char)0xE2; fields[o++] = (char)0x82; fields[o++] = (char)0xAC; fields[o++] = '\\'; fields[o++] = 'x'; fields[o++] = '0'; fields[o++] = 'a'; }
    fields[o] = '\0';
    static char b[9000];
    size_t n = dlog_format_line(b, sizeof(b), t_ms(), 0, DLOG_INFO, DLOG_AI, 1, "ai", "e", fields);
    ASSERT_TRUE(n <= (size_t)DLOG_LINE_MAX);
    const char *p = strstr(b, " e k=\"") + 6;
    int ok = 1;
    while (*p && !(p[0] == '"' && p[1] == ' ' && p[2] == 't')) {
        if ((unsigned char)p[0] == 0xE2) { if ((unsigned char)p[1] != 0x82 || (unsigned char)p[2] != 0xAC) ok = 0; p += 3; }
        else if (p[0] == '\\') { if (strncmp(p, "\\x0a", 4) != 0) ok = 0; p += 4; }
        else { ok = 0; p++; }
    }
    ASSERT_TRUE(ok);
    ASSERT_TRUE(strstr(b, "\" truncated=1\n") != NULL);       /* the quote was closed */
    TEST_END();
}

int test_dlog_format_line_small_buffers(void)
{
    TEST_BEGIN();
    char b[16];
    ASSERT_EQ(dlog_format_line(b, sizeof(b), t_ms(), 0, DLOG_INFO, DLOG_AI, 1, "ai", "e", "k=1"), (size_t)0);
    ASSERT_EQ(b[0], '\0');
    ASSERT_EQ(dlog_format_line(NULL, 100, t_ms(), 0, DLOG_INFO, DLOG_AI, 1, "ai", "e", "k=1"), (size_t)0);
    ASSERT_EQ(dlog_format_line(b, 1, t_ms(), 0, DLOG_INFO, DLOG_AI, 1, "ai", "e", "k=1"), (size_t)0);
    char exact[96];
    size_t n = dlog_format_line(exact, sizeof(exact), t_ms(), 0, DLOG_INFO, DLOG_AI, 1, "ai", "e", "k=1");
    ASSERT_TRUE(n > 0 && n < sizeof(exact));
    char tight[64];
    n = dlog_format_line(tight, sizeof(tight), t_ms(), 0, DLOG_INFO, DLOG_AI, 1, "ai", "e", "k=1");
    ASSERT_TRUE(n == 0 || (n < sizeof(tight) && tight[n - 1] == '\n'));
    TEST_END();
}

/* ---- dlog_relativize_dir ----------------------------------------------------------------- */

int test_dlog_relativize_dir_inside_the_exe_dir(void)
{
    TEST_BEGIN();
    char o[260];
    ASSERT_TRUE(dlog_relativize_dir("C:\\Tools\\nutshell\\logs", "C:\\Tools\\nutshell", o, sizeof(o)));
    ASSERT_STR_EQ(o, "logs");
    ASSERT_TRUE(dlog_relativize_dir("C:\\Tools\\nutshell\\diag\\run1\\", "C:\\Tools\\nutshell\\", o, sizeof(o)));
    ASSERT_STR_EQ(o, "diag\\run1");
    ASSERT_TRUE(dlog_relativize_dir("C:\\Tools\\nutshell", "C:\\Tools\\nutshell", o, sizeof(o)));
    ASSERT_STR_EQ(o, ".");
    ASSERT_TRUE(dlog_relativize_dir("c:/tools/NUTSHELL/logs", "C:\\Tools\\nutshell", o, sizeof(o)));
    ASSERT_STR_EQ(o, "logs");                                  /* case and separators do not matter */
    TEST_END();
}

int test_dlog_relativize_dir_outside_is_copied(void)
{
    TEST_BEGIN();
    char o[260];
    ASSERT_TRUE(dlog_relativize_dir("D:\\logs", "C:\\Tools\\nutshell", o, sizeof(o)));
    ASSERT_STR_EQ(o, "D:\\logs");
    ASSERT_TRUE(dlog_relativize_dir("C:\\Tools\\nutshell2\\logs", "C:\\Tools\\nutshell", o, sizeof(o)));
    ASSERT_STR_EQ(o, "C:\\Tools\\nutshell2\\logs");           /* a sibling that shares a prefix */
    ASSERT_TRUE(dlog_relativize_dir("C:\\Tools\\nutshell\\logs", NULL, o, sizeof(o)));
    ASSERT_STR_EQ(o, "C:\\Tools\\nutshell\\logs");
    ASSERT_TRUE(dlog_relativize_dir("C:\\Tools\\nutshell\\logs", "", o, sizeof(o)));
    ASSERT_STR_EQ(o, "C:\\Tools\\nutshell\\logs");
    TEST_END();
}

int test_dlog_relativize_dir_overflow_and_null(void)
{
    TEST_BEGIN();
    char o[8];
    ASSERT_FALSE(dlog_relativize_dir("D:\\a-long-folder-name", "C:\\x", o, sizeof(o)));
    ASSERT_FALSE(dlog_relativize_dir(NULL, "C:\\x", o, sizeof(o)));
    ASSERT_FALSE(dlog_relativize_dir("D:\\a", "C:\\x", NULL, 8));
    ASSERT_FALSE(dlog_relativize_dir("D:\\a", "C:\\x", o, 0));
    TEST_END();
}
