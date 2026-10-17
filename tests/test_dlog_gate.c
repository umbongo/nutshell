/* tests/test_dlog_gate.c -- the source gate for the debug log (spec section 6,
 * layer 3), in the style of test_ui_tokens.c.
 *
 * No call that writes a log event -- DLOG(, DLOG_EV(, dlog_emit(,
 * dlog_emit_fields( -- may mention a secret-looking identifier anywhere in its
 * argument list, field names included: password, passphrase, api_key, apikey,
 * privkey, secret, passwd, bearer, or a name that is `token`, ends `_token`,
 * or starts `token_` + not a counter. Terminal, chat and command text must go
 * through DLOG_CONTENT(), which is not scanned (it is gated at run time and
 * scrubbed of known secrets). This is an exact allow-list, not a ratchet: a
 * line that really must name one of these is added to k_allow with a reason,
 * and the test fails if an allow-list entry no longer matches anything.
 *
 * The scanner is exercised on planted violations so a gate that silently
 * stopped matching would fail too.
 */
#include "test_framework.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* "relative/path.c:line" entries that are allowed to break the rule, with a
 * reason. Empty: nothing needs to. */
static const char *const k_allow[] = { NULL };

/* ---- the scanner --------------------------------------------------------------- */

static const char *const k_callers[] = { "DLOG_EV(", "DLOG(", "dlog_emit_fields(", "dlog_emit(" };

static int ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

static int starts_with_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
    return 1;
}

static int ends_with_ci(const char *s, size_t n, const char *suffix)
{
    size_t m = strlen(suffix);
    if (n < m) return 0;
    for (size_t i = 0; i < m; i++)
        if (tolower((unsigned char)s[n - m + i]) != tolower((unsigned char)suffix[i])) return 0;
    return 1;
}

/* 1 when the word (n chars) is secret-looking. */
static int word_is_secret(const char *w, size_t n)
{
    static const char *const contains[] = { "password", "passphrase", "api_key", "apikey", "privkey", "secret", "passwd", "bearer" };
    char low[128];
    if (n >= sizeof(low)) n = sizeof(low) - 1;
    for (size_t i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)w[i]);
    low[n] = '\0';
    for (size_t i = 0; i < sizeof(contains) / sizeof(contains[0]); i++)
        if (strstr(low, contains[i])) return 1;
    if (strcmp(low, "token") == 0) return 1;
    if (ends_with_ci(low, n, "_token")) return 1;
    if (starts_with_ci(low, "token_") && !strstr(low, "count") && !strstr(low, "budget") && !strstr(low, "total")) return 1;
    return 0;
}

/* Replace comments with spaces (newlines kept so line numbers hold); strings
 * and character literals are kept: field names live in format strings. */
static char *strip_comments(const char *src)
{
    size_t n = strlen(src);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    size_t i = 0;
    while (i < n) {
        if (src[i] == '/' && i + 1 < n && src[i + 1] == '*') {
            out[i] = out[i + 1] = ' ';
            i += 2;
            while (i < n && !(src[i] == '*' && i + 1 < n && src[i + 1] == '/')) { out[i] = (src[i] == '\n') ? '\n' : ' '; i++; }
            if (i < n) { out[i] = out[i + 1] = ' '; i += 2; }
        } else if (src[i] == '/' && i + 1 < n && src[i + 1] == '/') {
            while (i < n && src[i] != '\n') { out[i] = ' '; i++; }
        } else if (src[i] == '"' || src[i] == '\'') {
            char q = src[i];
            out[i] = src[i];
            i++;
            while (i < n && src[i] != q) {
                if (src[i] == '\\' && i + 1 < n) { out[i] = src[i]; i++; }
                out[i] = src[i];
                i++;
            }
            if (i < n) { out[i] = src[i]; i++; }
        } else {
            out[i] = src[i];
            i++;
        }
    }
    out[n] = '\0';
    return out;
}

/* The scan of one text. Returns the number of violations; `first` (optional)
 * gets "line N: word" for the first one. */
static int gate_scan(const char *src, int *first_line, char *first_word, size_t word_cap)
{
    char *t = strip_comments(src);
    if (!t) return 0;
    int violations = 0;
    size_t n = strlen(t);
    int line = 1;
    for (size_t i = 0; i < n; i++) {
        if (t[i] == '\n') { line++; continue; }
        if (t[i] == '"' || t[i] == '\'') {              /* skip literals while looking for calls */
            char q = t[i++];
            while (i < n && t[i] != q) { if (t[i] == '\\') i++; else if (t[i] == '\n') line++; i++; }
            continue;
        }
        if (i > 0 && ident_char(t[i - 1])) continue;
        for (size_t c = 0; c < sizeof(k_callers) / sizeof(k_callers[0]); c++) {
            size_t cl = strlen(k_callers[c]);
            if (strncmp(t + i, k_callers[c], cl) != 0) continue;
            /* DLOG( must not be the prefix of DLOG_CONTENT( : the entry includes the paren */
            size_t j = i + cl;
            int depth = 1;
            int start_line = line;
            int cur_line = line;
            while (j < n && depth > 0) {
                char ch = t[j];
                if (ch == '\n') { cur_line++; j++; continue; }
                if (ch == '(') { depth++; j++; continue; }
                if (ch == ')') { depth--; j++; continue; }
                if (ident_char(ch) && !isdigit((unsigned char)ch)) {
                    size_t k = j;
                    while (k < n && ident_char(t[k])) k++;
                    if (word_is_secret(t + j, k - j)) {
                        violations++;
                        if (violations == 1) {
                            if (first_line) *first_line = cur_line;
                            if (first_word && word_cap) {
                                size_t wl = k - j < word_cap - 1 ? k - j : word_cap - 1;
                                memcpy(first_word, t + j, wl);
                                first_word[wl] = '\0';
                            }
                        }
                    }
                    j = k;
                    continue;
                }
                if (ch == '"') {                         /* words inside the string: field names */
                    j++;
                    while (j < n && t[j] != '"') {
                        if (t[j] == '\\') { j += 2; continue; }
                        if (t[j] == '\n') cur_line++;
                        if (ident_char(t[j]) && !isdigit((unsigned char)t[j])) {
                            size_t k = j;
                            while (k < n && ident_char(t[k])) k++;
                            if (word_is_secret(t + j, k - j)) {
                                violations++;
                                if (violations == 1) {
                                    if (first_line) *first_line = cur_line;
                                    if (first_word && word_cap) {
                                        size_t wl = k - j < word_cap - 1 ? k - j : word_cap - 1;
                                        memcpy(first_word, t + j, wl);
                                        first_word[wl] = '\0';
                                    }
                                }
                            }
                            j = k;
                        } else {
                            j++;
                        }
                    }
                    if (j < n) j++;
                    continue;
                }
                j++;
            }
            (void)start_line;
            line = cur_line;
            i = j - 1;
            break;
        }
    }
    free(t);
    return violations;
}

/* ---- planted violations ------------------------------------------------------------ */

#define SCAN(text) gate_scan((text), NULL, NULL, 0)

int test_dlog_gate_flags_a_secret_field_name_in_the_format(void)
{
    TEST_BEGIN();
    ASSERT_EQ(SCAN("DLOG(DLOG_SSH, DLOG_INFO, 0, \"ssh.auth\", \"password=%s\", pw);"), 1);
    ASSERT_EQ(SCAN("DLOG(DLOG_SSH, DLOG_INFO, 0, \"ssh.auth\", \"user=%s passphrase=%s\", u, p);"), 1);
    ASSERT_EQ(SCAN("DLOG(DLOG_AI, DLOG_DEBUG, 0, \"ai.req\", \"api_key=%s\", k);"), 1);
    ASSERT_EQ(SCAN("DLOG(DLOG_AI, DLOG_DEBUG, 0, \"ai.req\", \"secret=%s\", k);"), 1);
    ASSERT_EQ(SCAN("DLOG(DLOG_AI, DLOG_DEBUG, 0, \"ai.req\", \"token=%s\", k);"), 1);
    ASSERT_EQ(SCAN("DLOG(DLOG_AI, DLOG_DEBUG, 0, \"ai.req\", \"auth=%s\", k);"), 0);   /* key-name redaction covers auth */
    TEST_END();
}

int test_dlog_gate_flags_a_secret_looking_argument_expression(void)
{
    TEST_BEGIN();
    ASSERT_EQ(SCAN("DLOG(DLOG_SSH, DLOG_INFO, 0, \"e\", \"n=%d\", (int)strlen(profile->password));"), 1);
    ASSERT_EQ(SCAN("DLOG(DLOG_AI, DLOG_INFO, 0, \"e\", \"n=%s\", settings->ai_api_key);"), 1);
    ASSERT_EQ(SCAN("dlog_emit(DLOG_AI, DLOG_INFO, 0, \"e\", \"n=%s\", s->access_token);"), 1);
    ASSERT_EQ(SCAN("dlog_emit_fields(DLOG_AI, DLOG_INFO, 0, \"e\", privkey);"), 1);
    ASSERT_EQ(SCAN("DLOG_EV(DLOG_SSH, DLOG_INFO, 0, \"ssh.password.prompt\");"), 1);
    TEST_END();
}

int test_dlog_gate_flags_across_lines_and_nested_parens(void)
{
    TEST_BEGIN();
    const char *src =
        "void f(void) {\n"
        "    DLOG(DLOG_SSH, DLOG_INFO, 0,\n"
        "         \"ssh.x\",\n"
        "         \"n=%d len=%d\",\n"
        "         compute(a, (b + c)),\n"
        "         strlen(pw_secret_copy));\n"
        "}\n";
    int line = 0;
    char word[64];
    ASSERT_EQ(gate_scan(src, &line, word, sizeof(word)), 1);
    ASSERT_EQ(line, 6);
    ASSERT_STR_EQ(word, "pw_secret_copy");
    TEST_END();
}

int test_dlog_gate_allows_safe_calls(void)
{
    TEST_BEGIN();
    ASSERT_EQ(SCAN("DLOG(DLOG_RENDER, DLOG_TRACE, sid, \"paint\", \"rows=%d tick=%u\", rows, tick);"), 0);
    ASSERT_EQ(SCAN("DLOG(DLOG_AI, DLOG_INFO, 0, \"ai.done\", \"tokens_in=%d tokens_out=%d\", a, b);"), 0);
    ASSERT_EQ(SCAN("DLOG(DLOG_AI, DLOG_INFO, 0, \"ai.done\", \"token_count=%d\", a);"), 0);
    ASSERT_EQ(SCAN("DLOG(DLOG_SSH, DLOG_INFO, 0, \"ssh.auth.result\", \"auth_method=%s ok=%d\", m, ok);"), 0);
    ASSERT_EQ(SCAN("DLOG(DLOG_TERM, DLOG_TRACE, 0, \"key\", \"name=%s mods=%s\", n, m);"), 0);
    ASSERT_EQ(SCAN("DLOG_EV(DLOG_GENERAL, DLOG_INFO, 0, \"app.start\");"), 0);
    TEST_END();
}

int test_dlog_gate_ignores_the_content_macro_comments_and_other_code(void)
{
    TEST_BEGIN();
    ASSERT_EQ(SCAN("DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, 0, \"term.rx\", \"data\", password_buffer, n);"), 0);
    ASSERT_EQ(SCAN("/* DLOG(x, y, z, \"e\", \"password=%s\", p); */ int a;"), 0);
    ASSERT_EQ(SCAN("// DLOG(x, y, z, \"e\", \"password=%s\", p);\nint a;"), 0);
    ASSERT_EQ(SCAN("strcpy(profile->password, \"x\"); const char *s = \"DLOG( password )\";"), 0);
    ASSERT_EQ(SCAN("static void my_DLOG(int password) { }"), 0);     /* a longer identifier, not a call */
    ASSERT_EQ(SCAN("MYDLOG(DLOG_AI, 1, 0, \"e\", \"password=%s\", p);"), 0);
    TEST_END();
}

int test_dlog_gate_counts_every_violation(void)
{
    TEST_BEGIN();
    const char *src =
        "DLOG(A, B, 0, \"e\", \"password=%s secret=%s\", p, s);\n"
        "DLOG(A, B, 0, \"e\", \"ok=%d\", 1);\n"
        "DLOG(A, B, 0, \"e\", \"x=%s\", api_key);\n";
    ASSERT_EQ(SCAN(src), 3);
    TEST_END();
}

/* ---- the real tree ---------------------------------------------------------------- */

static int allowed(const char *path, int line)
{
    char key[300];
    (void)snprintf(key, sizeof(key), "%s:%d", path, line);
    for (size_t i = 0; k_allow[i]; i++)
        if (strcmp(k_allow[i], key) == 0) return 1;
    return 0;
}

static int scan_dir(const char *dir, int *scanned)
{
    int bad = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t nl = strlen(ent->d_name);
        if (nl < 3 || (strcmp(ent->d_name + nl - 2, ".c") != 0 && strcmp(ent->d_name + nl - 2, ".h") != 0)) continue;
        char path[400];
        (void)snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = (char *)malloc((size_t)sz + 1);
        if (buf) {
            size_t got = fread(buf, 1, (size_t)sz, f);
            buf[got] = '\0';
            int line = 0;
            char word[64] = "";
            int v = gate_scan(buf, &line, word, sizeof(word));
            (*scanned)++;
            if (v > 0 && !allowed(path, line)) {
                printf("  %s:%d: a log call mentions \"%s\" (%d in file)\n", path, line, word, v);
                bad += v;
            }
            free(buf);
        }
        fclose(f);
    }
    closedir(d);
    return bad;
}

int test_dlog_gate_source_tree_has_no_secret_names_in_log_calls(void)
{
    TEST_BEGIN();
    static const char *const dirs[] = { "src", "src/core", "src/config", "src/crypto", "src/term", "src/ssh", "src/ui" };
    int scanned = 0, bad = 0;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) bad += scan_dir(dirs[i], &scanned);
    ASSERT_TRUE(scanned > 100);        /* it really looked at the tree (cwd is the repo root) */
    ASSERT_EQ(bad, 0);
    TEST_END();
}

int test_dlog_gate_allow_list_has_no_stale_entries(void)
{
    TEST_BEGIN();
    /* every k_allow entry must still point at a violation, else it is dead weight */
    for (size_t i = 0; k_allow[i]; i++) {
        char path[300];
        int line = 0;
        if (sscanf(k_allow[i], "%299[^:]:%d", path, &line) != 2) { ASSERT_TRUE(0); continue; }
        FILE *f = fopen(path, "rb");
        ASSERT_NOT_NULL(f);
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = (char *)malloc((size_t)sz + 1);
        if (buf) {
            size_t got = fread(buf, 1, (size_t)sz, f);
            buf[got] = '\0';
            int l2 = 0;
            ASSERT_TRUE(gate_scan(buf, &l2, NULL, 0) > 0);
            free(buf);
        }
        fclose(f);
    }
    TEST_END();
}
