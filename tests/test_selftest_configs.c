/* The selftest configs (the three files under tests/selftest) and the integration harness's
 * New-NutshellTestEnv (tests/integration/NutshellIT.psm1) must be complete: a
 * missing key shows the modal upgrade notice on every launch, which hangs
 * `make selftest-wine` and the harness. A new key in the loader's table must
 * be added to all four, and this test says so. */
#include "test_framework.h"
#include "config.h"
#include "voice_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = (char *)malloc(1 << 20);
    if (!b) { fclose(f); return NULL; }
    size_t n = fread(b, 1, (1 << 20) - 1, f);
    b[n] = '\0';
    fclose(f);
    return b;
}

static int ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* `key` as a whole word followed by " =" (PowerShell). */
static int has_ps_key(const char *t, const char *key)
{
    size_t kl = strlen(key);
    for (const char *p = t; (p = strstr(p, key)) != NULL; p += kl) {
        int before = p == t || !ident(p[-1]);
        if (before && strncmp(p + kl, " =", 2) == 0) return 1;
    }
    return 0;
}

static int has_json_key(const char *t, const char *key)
{
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    return strstr(t, pat) != NULL;
}

int test_selftest_configs_complete(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const ConfigSettingKey *keys = config_setting_keys(&n);
    static const char *const files[] = {
        "tests/selftest/nutshell-ai-off.config",
        "tests/selftest/nutshell-ai-on.config",
        "tests/selftest/nutshell-voice-on.config",
        "tests/selftest/nutshell-voice-conv.config",
    };
    for (size_t f = 0; f < sizeof files / sizeof files[0]; f++) {
        char *t = slurp(files[f]);
        ASSERT_NOT_NULL(t);
        if (!t) continue;
        for (size_t i = 0; i < n; i++) {
            int has = has_json_key(t, keys[i].key);
            if (!has) printf("  %s lacks \"%s\"\n", files[f], keys[i].key);
            ASSERT_TRUE(has);
        }
        /* the internal marker: without it the loader runs the voice migrations */
        ASSERT_TRUE(strstr(t, "\"voice_defaults_rev\": 3") != NULL);
        free(t);
    }
    char *ps_all = slurp("tests/integration/NutshellIT.psm1");
    ASSERT_NOT_NULL(ps_all);
    /* only New-NutshellTestEnv counts: the same word elsewhere in the module
     * must not satisfy the check */
    char *ps = ps_all ? strstr(ps_all, "function New-NutshellTestEnv") : NULL;
    ASSERT_NOT_NULL(ps);
    if (ps) {
        char *next = strstr(ps + 8, "\nfunction ");
        if (next) *next = '\0';
        for (size_t i = 0; i < n; i++) {
            int has = has_ps_key(ps, keys[i].key);
            if (!has) printf("  NutshellIT.psm1 lacks %s\n", keys[i].key);
            ASSERT_TRUE(has);
        }
        ASSERT_TRUE(has_ps_key(ps, "voice_defaults_rev"));
    }
    free(ps_all);
    TEST_END();
}

/* The conversation selftest config really parses to voice on + Conversation
 * (a typo in "voice_mode" would fall back to Dictation and the `conv` and
 * `sapi` modes would silently test the wrong thing). The voice-on config is
 * the control: it stays Dictation. */
int test_selftest_conv_config_mode(void)
{
    TEST_BEGIN();
    static const struct { const char *src; int mode; } cases[] = {
        { "tests/selftest/nutshell-voice-conv.config", VOICE_MODE_CONVERSATION },
        { "tests/selftest/nutshell-voice-on.config",   VOICE_MODE_DICTATION },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char *t = slurp(cases[i].src);
        ASSERT_NOT_NULL(t);
        if (!t) continue;
        /* load a copy: a load may re-save the file */
        char tmp[256];
        snprintf(tmp, sizeof tmp, "%s/selftest_conv_%zu.config", TEST_TMP_DIR, i);
        FILE *f = test_fopen_private(tmp);
        ASSERT_NOT_NULL(f);
        if (f) { fwrite(t, 1, strlen(t), f); fclose(f); }
        free(t);
        Config *c = config_load(tmp);
        ASSERT_NOT_NULL(c);
        if (c) {
            ASSERT_TRUE(c->settings.voice_enabled);
            ASSERT_TRUE(c->settings.ai_enabled);
            ASSERT_EQ(c->settings.voice_mode, cases[i].mode);
            config_free(c);
        }
        remove(tmp);
    }
    TEST_END();
}
