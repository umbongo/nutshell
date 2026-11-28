#include "test_framework.h"
#include "profile_help.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

int test_profile_help_table_matches_enum(void) {
    TEST_BEGIN();
    for (int i = 0; i < (int)PH_COUNT; i++) {
        const ProfileHelp *e = profile_help_get(i);
        ASSERT_NOT_NULL(e);
        if (!e) continue;
        ASSERT_EQ((int)e->id, i);
        ASSERT_NOT_NULL(e->name);
        ASSERT_TRUE(e->name && strncmp(e->name, "PH_", 3) == 0);
    }
    ASSERT_NULL(profile_help_get(-1));
    ASSERT_NULL(profile_help_get((int)PH_COUNT));
    TEST_END();
}

int test_profile_help_texts_shape(void) {
    TEST_BEGIN();
    for (int i = 0; i < (int)PH_COUNT; i++) {
        const ProfileHelp *e = profile_help_get(i);
        ASSERT_NOT_NULL(e);
        if (!e) continue;
        ASSERT_TRUE(e->label && e->label[0]);
        ASSERT_TRUE(e->text && e->text[0]);
        size_t len = e->text ? strlen(e->text) : 0;
        if (len > PROFILE_HELP_TEXT_MAX)
            printf("  %s is %zu bytes (max %d)\n", e->name, len, PROFILE_HELP_TEXT_MAX);
        ASSERT_TRUE(len <= PROFILE_HELP_TEXT_MAX);
        ASSERT_TRUE(len >= 20);   /* says something */
        /* Plain ASCII, no tabs, no stray line breaks (the tip wraps itself),
         * ends like a sentence, no leading or trailing space. */
        for (size_t k = 0; k < len; k++) {
            unsigned char ch = (unsigned char)e->text[k];
            ASSERT_TRUE(ch >= 0x20 && ch < 0x7F);
        }
        ASSERT_TRUE(e->text[0] != ' ' && e->text[len - 1] == '.');
        /* No real host, address or key material in an example. */
        ASSERT_TRUE(strstr(e->text, "BEGIN ") == NULL);
        ASSERT_TRUE(strstr(e->text, "://") == NULL);
        ASSERT_TRUE(strstr(e->text, "ssh-rsa") == NULL);
    }
    TEST_END();
}

int test_profile_help_fields_say_what_empty_means(void) {
    TEST_BEGIN();
    /* The fields that may be left empty (or that cannot) say so. */
    static const ProfileHelpId k_must_say[] = {
        PH_NAME, PH_HOST, PH_PORT, PH_USER, PH_PASSWORD, PH_KEY, PH_AI_NOTES
    };
    for (size_t i = 0; i < sizeof(k_must_say) / sizeof(k_must_say[0]); i++) {
        const ProfileHelp *e = profile_help_get((int)k_must_say[i]);
        ASSERT_NOT_NULL(e);
        if (!e) continue;
        int says = strstr(e->text, "empty") != NULL || strstr(e->text, "blank") != NULL;
        if (!says) printf("  %s does not say what an empty field does\n", e->name);
        ASSERT_TRUE(says);
    }
    TEST_END();
}

/* The wording the review asked for, word for word where it was given. */
int test_profile_help_review_wording(void) {
    TEST_BEGIN();
    const ProfileHelp *e;
    e = profile_help_get((int)PH_NAME);
    ASSERT_STR_EQ(e->text,
        "The name shown in the list of saved sessions. If you leave it empty, "
        "the list shows the host name instead; a local shell session has no "
        "host, so give it a name.");
    e = profile_help_get((int)PH_USER);
    ASSERT_STR_EQ(e->text,
        "The account to log in as. SSH needs one: Nutshell does not ask for "
        "it, so with this empty the server refuses the login. For Telnet, "
        "Nutshell types it at the login prompt; leave it empty to type it "
        "yourself.");
    e = profile_help_get((int)PH_KEY);
    ASSERT_STR_EQ(e->text,
        "The path of your private key file, used when Auth is SSH Key. If you "
        "leave it empty, the key login cannot work, so give a path or choose "
        "Password.");
    e = profile_help_get((int)PH_PLATFORM);
    ASSERT_TRUE(strstr(e->text, "Auto-detect") != NULL);
    ASSERT_TRUE(strstr(e->text, "Automatic") == NULL);
    e = profile_help_get((int)PH_PASSWORD);
    ASSERT_TRUE(strstr(e->text,
        " With Telnet and no user either, you type both at the server's "
        "prompts.") != NULL);
    e = profile_help_get((int)PH_AUTH);
    ASSERT_TRUE(strstr(e->text, "Telnet") == NULL);
    e = profile_help_get((int)PH_SHELL);
    ASSERT_TRUE(strncmp(e->text, "Automatic (the first choice) picks one "
                        "installed on this PC:", 60) == 0);
    ASSERT_TRUE(strstr(e->text, "empty") == NULL);
    ASSERT_TRUE(strstr(e->text, "PowerShell 7, Windows PowerShell, Git Bash, "
                       "MSYS2 bash or cmd, in that order") != NULL);
    TEST_END();
}

/* Every field the form lets the user edit has a help entry, and the dialog
 * maps it: the editable controls named in resource.rc's Session Manager
 * dialog are looked up in session_manager.c's help map. */
int test_profile_help_every_editable_field_is_mapped(void) {
    TEST_BEGIN();
    char *rc = read_whole("src/ui/resource.rc");
    char *src = read_whole("src/ui/session_manager.c");
    if (!rc || !src) {
        const char *ci = getenv("CI");
        if (ci && ci[0]) {
            printf("  resource.rc or session_manager.c not readable under CI\n");
            ASSERT_TRUE(0);
        } else {
            printf("  [profile_help] sources not readable from cwd -- skipping\n");
        }
        free(rc); free(src);
        TEST_END();
    }
    char *dlg = strstr(rc, "IDD_SESSION_MANAGER DIALOGEX");
    ASSERT_NOT_NULL(dlg);
    char *end = dlg ? strstr(dlg, "\nEND") : NULL;
    ASSERT_NOT_NULL(end);
    int editable = 0;
    if (dlg && end) {
        *end = '\0';
        for (char *line = dlg; line && *line; ) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            while (*line == ' ' || *line == '\t') line++;
            int is_field = strncmp(line, "EDITTEXT", 8) == 0 ||
                           strncmp(line, "COMBOBOX", 8) == 0;
            int is_browse = strstr(line, "IDC_BTN_BROWSE_KEY") != NULL;
            if (is_field || is_browse) {
                char id[64] = {0};
                const char *p = strstr(line, "IDC_");
                size_t k = 0;
                while (p && k < sizeof(id) - 1 && (isalnum((unsigned char)p[k]) || p[k] == '_')) {
                    id[k] = p[k];
                    k++;
                }
                char want[96];
                (void)snprintf(want, sizeof(want), "%s, PH_", id);
                int mapped = id[0] && strstr(src, want) != NULL;
                if (!mapped) printf("  %s has no help mapping in session_manager.c\n", id);
                ASSERT_TRUE(mapped);
                editable++;
            }
            line = nl ? nl + 1 : NULL;
        }
    }
    /* Type, Name, Host, Port, User, Auth, Platform, Password, Key, Browse,
     * Shell, AI notes: one per help entry. */
    ASSERT_EQ(editable, (int)PH_COUNT);
    free(rc);
    free(src);
    TEST_END();
}
