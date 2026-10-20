#include "test_framework.h"
#include "shortcuts.h"
#include "resource.h"
#include <stdlib.h>
#include <string.h>

/* The seven sections, in the order the sheet and README show them (spec
 * section 1). */
static const char *const WANT_SECTIONS[] = {
    "Sessions and tabs",
    "Copy, paste and selection",
    "Scrolling and zoom",
    "Keys the terminal sends to the shell",
    "AI Assist panel",
    "Window and help",
    "Mouse"
};
#define WANT_N (sizeof(WANT_SECTIONS) / sizeof(WANT_SECTIONS[0]))

int test_shortcuts_every_row_has_section_chord_action(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    ASSERT_NOT_NULL(t);
    ASSERT_TRUE(n >= 40u);
    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(t[i].section && t[i].section[0]);
        ASSERT_TRUE(t[i].chord && t[i].chord[0]);
        ASSERT_TRUE(t[i].action && t[i].action[0]);
        ASSERT_NOT_NULL(t[i].condition);           /* "" when none, never NULL */
    }
    TEST_END();
}

int test_shortcuts_sections_contiguous_and_in_fixed_order(void)
{
    TEST_BEGIN();
    const char *got[16];
    size_t ns = shortcuts_sections(got, 16u);
    ASSERT_EQ(ns, WANT_N);
    for (size_t i = 0; i < ns && i < WANT_N; i++)
        ASSERT_STR_EQ(got[i], WANT_SECTIONS[i]);

    /* Contiguous: walking the table, the section index never goes back. */
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    size_t at = 0;
    for (size_t i = 0; i < n; i++) {
        while (at < WANT_N && strcmp(t[i].section, WANT_SECTIONS[at]) != 0) at++;
        ASSERT_TRUE(at < WANT_N);                  /* a section out of order or unknown */
    }
    /* A cap smaller than the count still reports the full count. */
    ASSERT_EQ(shortcuts_sections(NULL, 0u), WANT_N);
    TEST_END();
}

/* The i-th ", "-separated alternative of `chord` into out (cap bytes);
 * 0 when there is no i-th one. */
static int chord_alt(const char *chord, int idx, char *out, size_t cap)
{
    const char *p = chord;
    for (int k = 0; k < idx; k++) {
        p = strstr(p, ", ");
        if (!p) return 0;
        p += 2;
    }
    const char *end = strstr(p, ", ");
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= cap) len = cap - 1u;
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

/* No single chord appears twice with the same condition in one section,
 * whether it stands alone or is one of a row's alternatives. */
int test_shortcuts_no_chord_and_condition_repeat_within_a_section(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    char a[64], b[64];
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i; j < n; j++) {
            if (strcmp(t[i].section, t[j].section) != 0) continue;
            if (strcmp(t[i].condition, t[j].condition) != 0) continue;
            for (int x = 0; chord_alt(t[i].chord, x, a, sizeof(a)); x++) {
                for (int y = 0; chord_alt(t[j].chord, y, b, sizeof(b)); y++) {
                    if (i == j && x == y) continue;
                    if (strcmp(a, b) == 0) {
                        printf("  duplicate: [%s] %s \"%s\"\n",
                               t[i].section, a, t[i].condition);
                        ASSERT_TRUE(0);
                    }
                }
            }
        }
    }
    TEST_END();
}

/* Chord grammar: alternatives only as ", " between non-empty chords; never
 * " / "; a menu-label row holds exactly one chord; a menu path is flagged
 * as one. */
int test_shortcuts_chord_grammar(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (size_t i = 0; i < n; i++) {
        const char *c = t[i].chord;
        ASSERT_TRUE(strstr(c, " / ") == NULL);
        ASSERT_TRUE(strstr(c, ",,") == NULL);
        for (const char *p = strchr(c, ','); p; p = strchr(p + 1, ','))
            ASSERT_TRUE(p[1] == ' ' && p[2] != '\0' && p[2] != ' ' && p != c);
        if (t[i].menu_id != 0) ASSERT_TRUE(strchr(c, ',') == NULL);
        int is_path = strstr(c, " > ") != NULL;
        ASSERT_EQ(is_path, (t[i].flags & SC_MENU_PATH) ? 1 : 0);
    }
    TEST_END();
}

/* No field may break a markdown table row: no '|', no newline, no
 * leading or trailing space. */
int test_shortcuts_fields_are_markdown_safe(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (size_t i = 0; i < n; i++) {
        const char *f[4] = { t[i].section, t[i].chord, t[i].action, t[i].condition };
        for (int k = 0; k < 4; k++) {
            size_t len = strlen(f[k]);
            ASSERT_TRUE(strpbrk(f[k], "|\r\n\t*`") == NULL);
            if (len > 0) {
                ASSERT_TRUE(f[k][0] != ' ');
                ASSERT_TRUE(f[k][len - 1] != ' ');
            }
        }
    }
    TEST_END();
}

int test_shortcuts_menu_ids_unique_and_flagged(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    size_t with_id = 0;
    for (size_t i = 0; i < n; i++) {
        ASSERT_EQ(t[i].menu_id != 0 ? 1 : 0, (t[i].flags & SC_MENU_ACCEL) ? 1 : 0);
        if (t[i].menu_id == 0) continue;
        with_id++;
        for (size_t j = i + 1; j < n; j++)
            ASSERT_TRUE(t[j].menu_id != t[i].menu_id);
    }
    ASSERT_EQ(with_id, (size_t)7);
    TEST_END();
}

int test_shortcuts_menu_accel_for_the_seven_menu_ids(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_FILE_NEW_SESSION), "Ctrl+Shift+T");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_FILE_CLOSE_TAB),   "Ctrl+Shift+W");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_EDIT_COPY),        "Ctrl+Shift+C");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_EDIT_PASTE),       "Ctrl+V");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_VIEW_AI_CHAT),     "Ctrl+Shift+Space");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_VIEW_FULLSCREEN),  "F11");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_HELP_MARK_ISSUE),  "Ctrl+Shift+M");
    /* Items with no accelerator, an unknown id and 0 all give "". */
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_FILE_EXIT),        "");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_EDIT_SELECT_ALL),  "");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_HELP_SHORTCUTS),   "");
    ASSERT_STR_EQ(shortcuts_menu_accel(IDM_SENDKEY_CTRL_V),   "");
    ASSERT_STR_EQ(shortcuts_menu_accel(0),                    "");
    ASSERT_STR_EQ(shortcuts_menu_accel(-1),                   "");
    TEST_END();
}

/* Each accelerator fits MenuItemData.accel (char[32] in window.c). */
int test_shortcuts_menu_accels_fit_the_menu_buffer(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (size_t i = 0; i < n; i++)
        if (t[i].menu_id != 0) ASSERT_TRUE(strlen(t[i].chord) < 32u);
    TEST_END();
}

int test_shortcuts_readme_block_shape(void)
{
    TEST_BEGIN();
    size_t need = shortcuts_readme_block(NULL, 0u);
    ASSERT_TRUE(need > 1000u);
    char *buf = (char *)malloc(need + 1u);
    ASSERT_NOT_NULL(buf);
    if (buf) {
        ASSERT_EQ(shortcuts_readme_block(buf, need + 1u), need);
        ASSERT_EQ(strlen(buf), need);
        static const char HEAD[] =
            "\n\n#### Sessions and tabs\n\n| Shortcut | Action | Note |\n| --- | --- | --- |\n";
        ASSERT_TRUE(strncmp(buf, HEAD, sizeof(HEAD) - 1u) == 0);
        ASSERT_TRUE(strstr(buf, "| **Ctrl+Shift+T** | New session (opens the Session Manager) | from the terminal, not the AI panel |\n") != NULL);
        ASSERT_TRUE(strstr(buf, "| **Alt+F4** | Exit | |\n") != NULL);
        ASSERT_TRUE(strchr(buf, '\r') == NULL);
        ASSERT_TRUE(need >= 2u && buf[need - 1] == '\n' && buf[need - 2] == '\n');
        free(buf);
    }
    TEST_END();
}

/* A short buffer gets a NUL-terminated prefix and the full length back. */
int test_shortcuts_readme_block_truncates_safely(void)
{
    TEST_BEGIN();
    size_t need = shortcuts_readme_block(NULL, 0u);
    char small[16];
    memset(small, 'x', sizeof(small));
    ASSERT_EQ(shortcuts_readme_block(small, sizeof(small)), need);
    ASSERT_EQ(strlen(small), sizeof(small) - 1u);
    char one[1] = { 'x' };
    ASSERT_EQ(shortcuts_readme_block(one, 1u), need);
    ASSERT_EQ(one[0], '\0');
    TEST_END();
}
