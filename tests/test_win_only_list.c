/* Every "_win.c" file under tests/ must be named in the Makefile's WIN_ONLY_TESTS.
 *
 * Those files compile only into the Wine/Windows harness (WIN_TEST_SRCS).
 * One missing from WIN_ONLY_TESTS is picked up by the wildcard of the native
 * test build, which on a Windows host then tries to compile and link it
 * (missed in PR #93 and PR #106). This reads the Makefile and the tests
 * directory and names each file that is on disk and not in the list.
 *
 * Like the README check in test_shortcuts.c, it runs from the repository
 * root; from anywhere else it skips, except under CI (checks.yml sets
 * CI: true), where an unreadable file fails. */
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define WOL_MAX_FILES 256
#define WOL_NAME_MAX  128

/* Whole file, as bytes; NULL when it cannot be read. */
static char *wol_read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1u, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static int wol_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Collect the words of the WIN_ONLY_TESTS assignment (continued lines joined
 * by a trailing backslash, LF or CRLF) into names. Returns the count. */
static size_t wol_parse_list(const char *mk, char names[][WOL_NAME_MAX], size_t cap)
{
    const char *key = "WIN_ONLY_TESTS";
    size_t n = 0u;
    const char *p = mk;
    for (;;) {
        p = strstr(p, key);
        if (!p) return 0u;
        /* the assignment: at a line start, followed by optional blanks and '=' */
        int at_start = (p == mk || p[-1] == '\n');
        const char *q = p + strlen(key);
        while (*q == ' ' || *q == '\t') q++;
        if (at_start && *q == '=') { p = q + 1; break; }
        p += strlen(key);
    }
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (*p == '\\' && (p[1] == '\n' || (p[1] == '\r' && p[2] == '\n'))) {
            p += (p[1] == '\r') ? 3 : 2;      /* continuation: go on */
            continue;
        }
        if (*p == '\r' || *p == '\n') break;  /* end of the assignment */
        const char *s = p;
        while (*p && !wol_is_space(*p)) p++;
        size_t len = (size_t)(p - s);
        if (len > 0u && len < WOL_NAME_MAX && n < cap) {
            memcpy(names[n], s, len);
            names[n][len] = '\0';
            n++;
        }
    }
    return n;
}

static int wol_in_list(char names[][WOL_NAME_MAX], size_t n, const char *path)
{
    for (size_t i = 0u; i < n; i++)
        if (strcmp(names[i], path) == 0) return 1;
    return 0;
}

int test_win_only_list_every_win_file_is_listed(void)
{
    TEST_BEGIN();
    char *mk = wol_read_whole("Makefile");
    DIR *d = opendir("tests");
    if (!mk || !d) {
        const char *ci = getenv("CI");
        if (ci && ci[0]) {
            printf("  Makefile or tests/ not readable from the working directory under CI\n");
            ASSERT_TRUE(0);
        } else {
            printf("  [win_only_list] Makefile or tests/ not readable from cwd -- skipping\n");
        }
        if (d) closedir(d);
        free(mk);
        TEST_END();
    }

    static char names[WOL_MAX_FILES][WOL_NAME_MAX];
    size_t n = wol_parse_list(mk, names, WOL_MAX_FILES);
    free(mk);

    /* the parser itself: a list that parsed to nothing would pass vacuously */
    ASSERT_TRUE(n >= 10u);
    ASSERT_TRUE(wol_in_list(names, n, "tests/win_runner.c"));

    struct dirent *de;
    size_t win_files = 0u;
    while ((de = readdir(d)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len < 7u || strcmp(de->d_name + len - 6u, "_win.c") != 0) continue;
        char path[WOL_NAME_MAX];
        if (snprintf(path, sizeof path, "tests/%s", de->d_name) >= (int)sizeof path) continue;
        win_files++;
        if (!wol_in_list(names, n, path)) {
            printf("  %s is not in WIN_ONLY_TESTS in the Makefile\n", path);
            ASSERT_TRUE(0);
        }
    }
    closedir(d);
    ASSERT_TRUE(win_files > 0u);
    TEST_END();
}
