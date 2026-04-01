#include "test_framework.h"
#include "profile.h"
#include "config.h"
#include "xmalloc.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * Profile struct and Config profile management tests
 *
 * session_manager.c is Win32 UI code (excluded from test builds),
 * so we test the underlying data layer it depends on.
 * ============================================================ */

int test_profile_struct(void) {
    TEST_BEGIN();
    Profile p;
    memset(&p, 0, sizeof(p));

    snprintf(p.name, sizeof(p.name), "Test Session");
    snprintf(p.host, sizeof(p.host), "127.0.0.1");
    p.port = 2222;
    p.auth_type = AUTH_KEY;

    ASSERT_EQ(strcmp(p.name, "Test Session"), 0);
    ASSERT_EQ(strcmp(p.host, "127.0.0.1"), 0);
    ASSERT_EQ(p.port, 2222);
    ASSERT_EQ((int)p.auth_type, (int)AUTH_KEY);

    TEST_END();
}

int test_profile_default_values(void)
{
    TEST_BEGIN();
    Profile *p = config_profile_new();
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->port, 22);
    ASSERT_EQ((int)p->auth_type, (int)AUTH_PASSWORD);
    ASSERT_EQ(p->name[0], '\0');
    ASSERT_EQ(p->host[0], '\0');
    ASSERT_EQ(p->username[0], '\0');
    ASSERT_EQ(p->password[0], '\0');
    ASSERT_EQ(p->key_path[0], '\0');
    config_profile_free(p);
    TEST_END();
}

int test_profile_free_null(void)
{
    TEST_BEGIN();
    /* must not crash */
    config_profile_free(NULL);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_profile_field_truncation(void)
{
    TEST_BEGIN();
    Profile p;
    memset(&p, 0, sizeof(p));
    /* Fill name with 300 chars — must truncate to 255 + NUL */
    char long_name[301];
    memset(long_name, 'A', 300);
    long_name[300] = '\0';
    snprintf(p.name, sizeof(p.name), "%s", long_name);
    ASSERT_EQ(strlen(p.name), 255u);
    ASSERT_EQ(p.name[255], '\0');
    TEST_END();
}

int test_profile_auth_types(void)
{
    TEST_BEGIN();
    ASSERT_EQ((int)AUTH_PASSWORD, 0);
    ASSERT_EQ((int)AUTH_KEY, 1);
    TEST_END();
}

int test_profile_ai_notes(void)
{
    TEST_BEGIN();
    Profile p;
    memset(&p, 0, sizeof(p));
    snprintf(p.ai_notes, sizeof(p.ai_notes), "This is a Cisco switch");
    ASSERT_STR_EQ(p.ai_notes, "This is a Cisco switch");
    ASSERT_TRUE(sizeof(p.ai_notes) == AI_NOTES_MAX);
    TEST_END();
}

int test_sessmgr_new_default(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    ASSERT_NOT_NULL(cfg);
    /* default settings populated */
    ASSERT_TRUE(cfg->settings.font[0] != '\0');
    ASSERT_EQ(cfg->settings.scrollback_lines, 10000);
    ASSERT_EQ(cfg->settings.paste_delay_ms, 350);
    /* no profiles initially */
    ASSERT_EQ((int)vec_size(&cfg->profiles), 0);
    config_free(cfg);
    TEST_END();
}

int test_sessmgr_add_profiles(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();

    Profile *p1 = config_profile_new();
    snprintf(p1->name, sizeof(p1->name), "Server 1");
    snprintf(p1->host, sizeof(p1->host), "10.0.0.1");
    vec_push(&cfg->profiles, p1);

    Profile *p2 = config_profile_new();
    snprintf(p2->name, sizeof(p2->name), "Server 2");
    snprintf(p2->host, sizeof(p2->host), "10.0.0.2");
    p2->port = 2222;
    vec_push(&cfg->profiles, p2);

    ASSERT_EQ((int)vec_size(&cfg->profiles), 2);

    Profile *got1 = (Profile *)vec_get(&cfg->profiles, 0);
    Profile *got2 = (Profile *)vec_get(&cfg->profiles, 1);
    ASSERT_STR_EQ(got1->name, "Server 1");
    ASSERT_STR_EQ(got2->host, "10.0.0.2");
    ASSERT_EQ(got2->port, 2222);

    config_free(cfg);
    TEST_END();
}

int test_sessmgr_free_null(void)
{
    TEST_BEGIN();
    config_free(NULL);
    ASSERT_TRUE(1);
    TEST_END();
}

int test_sessmgr_load_null_path(void)
{
    TEST_BEGIN();
    Config *cfg = config_load(NULL);
    ASSERT_NULL(cfg);
    TEST_END();
}

int test_sessmgr_load_nonexistent(void)
{
    TEST_BEGIN();
    Config *cfg = config_load("/tmp/nutshell_nonexistent_config_test.json");
    ASSERT_NULL(cfg);
    TEST_END();
}

int test_sessmgr_save_null(void)
{
    TEST_BEGIN();
    ASSERT_EQ(config_save(NULL, "/tmp/test.json"), -1);
    Config *cfg = config_new_default();
    ASSERT_EQ(config_save(cfg, NULL), -1);
    config_free(cfg);
    TEST_END();
}

int test_sessmgr_save_load_roundtrip(void)
{
    TEST_BEGIN();
    const char *path = "/tmp/nutshell_test_roundtrip.config";

    Config *cfg = config_new_default();
    snprintf(cfg->settings.font, sizeof(cfg->settings.font), "Consolas");
    cfg->settings.font_size = 14;
    cfg->settings.scrollback_lines = 5000;

    Profile *p = config_profile_new();
    snprintf(p->name, sizeof(p->name), "TestBox");
    snprintf(p->host, sizeof(p->host), "192.168.1.100");
    p->port = 8022;
    p->auth_type = AUTH_KEY;
    snprintf(p->key_path, sizeof(p->key_path), "/home/user/.ssh/id_rsa");
    snprintf(p->ai_notes, sizeof(p->ai_notes), "Ubuntu 22.04 server");
    vec_push(&cfg->profiles, p);

    int rc = config_save(cfg, path);
    ASSERT_EQ(rc, 0);
    config_free(cfg);

    /* Load back */
    Config *loaded = config_load(path);
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(loaded->settings.font, "Consolas");
    ASSERT_EQ(loaded->settings.font_size, 14);
    ASSERT_EQ(loaded->settings.scrollback_lines, 5000);

    ASSERT_EQ((int)vec_size(&loaded->profiles), 1);
    Profile *lp = (Profile *)vec_get(&loaded->profiles, 0);
    ASSERT_STR_EQ(lp->name, "TestBox");
    ASSERT_STR_EQ(lp->host, "192.168.1.100");
    ASSERT_EQ(lp->port, 8022);
    ASSERT_EQ((int)lp->auth_type, (int)AUTH_KEY);
    ASSERT_STR_EQ(lp->key_path, "/home/user/.ssh/id_rsa");
    ASSERT_STR_EQ(lp->ai_notes, "Ubuntu 22.04 server");

    config_free(loaded);
    remove(path);
    TEST_END();
}

int test_sessmgr_settings_validate(void)
{
    TEST_BEGIN();
    Settings s;
    memset(&s, 0, sizeof(s));
    /* out-of-range values */
    s.scrollback_lines = 50;    /* below min 100 */
    s.paste_delay_ms = 99999;   /* above max 5000 */

    settings_validate(&s);

    ASSERT_EQ(s.scrollback_lines, 100);
    ASSERT_EQ(s.paste_delay_ms, 5000);
    /* empty font should get default */
    ASSERT_TRUE(s.font[0] != '\0');
    TEST_END();
}

int test_sessmgr_multiple_profiles_roundtrip(void)
{
    TEST_BEGIN();
    const char *path = "/tmp/nutshell_test_multi_profile.config";

    Config *cfg = config_new_default();
    for (int i = 0; i < 10; i++) {
        Profile *p = config_profile_new();
        snprintf(p->name, sizeof(p->name), "Server-%d", i);
        snprintf(p->host, sizeof(p->host), "10.0.0.%d", i + 1);
        p->port = 22 + i;
        vec_push(&cfg->profiles, p);
    }

    ASSERT_EQ(config_save(cfg, path), 0);
    config_free(cfg);

    Config *loaded = config_load(path);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ((int)vec_size(&loaded->profiles), 10);

    Profile *p5 = (Profile *)vec_get(&loaded->profiles, 5);
    ASSERT_STR_EQ(p5->name, "Server-5");
    ASSERT_STR_EQ(p5->host, "10.0.0.6");
    ASSERT_EQ(p5->port, 27);

    config_free(loaded);
    remove(path);
    TEST_END();
}
