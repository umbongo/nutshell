/* The selftest configs (the three files under tests/selftest) and the integration harness's
 * New-NutshellTestEnv (tests/integration/NutshellIT.psm1) must be complete: a
 * missing key shows the modal upgrade notice on every launch, which hangs
 * `make selftest-wine` and the harness. A new key in the loader's table must
 * be added to all four, and this test says so. */
#include "test_framework.h"
#include "config.h"
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
    char *ps = slurp("tests/integration/NutshellIT.psm1");
    ASSERT_NOT_NULL(ps);
    if (ps) {
        for (size_t i = 0; i < n; i++) {
            int has = has_ps_key(ps, keys[i].key);
            if (!has) printf("  NutshellIT.psm1 lacks %s\n", keys[i].key);
            ASSERT_TRUE(has);
        }
        ASSERT_TRUE(has_ps_key(ps, "voice_defaults_rev"));
        free(ps);
    }
    TEST_END();
}
