#include "test_framework.h"
#include "voice_lang.h"
#include <string.h>

int test_voice_lang_table_order(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const VoiceLang *t = voice_lang_table(&n);
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(n, (size_t)19);
    ASSERT_STR_EQ(t[0].label, "Auto-detect");
    ASSERT_STR_EQ(t[0].id, "");
    ASSERT_STR_EQ(t[0].iso, "");
    ASSERT_STR_EQ(t[1].id, "en-GB");
    ASSERT_STR_EQ(t[1].label, "English (UK)");
    ASSERT_STR_EQ(t[1].iso, "en");
    ASSERT_STR_EQ(t[2].id, "en-US");
    ASSERT_STR_EQ(t[2].label, "English (US)");
    ASSERT_STR_EQ(t[2].iso, "en");
    ASSERT_STR_EQ(t[3].id, "ar");
    ASSERT_STR_EQ(t[n - 1].id, "uk");
    for (size_t i = 3; i < n; i++) ASSERT_STR_EQ(t[i].id, t[i].iso);
    ASSERT_STR_EQ(VOICE_LANG_DEFAULT, "en-GB");
    TEST_END();
}

int test_voice_lang_find(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_lang_find("en-GB"), 1);
    ASSERT_TRUE(voice_lang_find("fr") >= 3);
    ASSERT_EQ(voice_lang_find("en"), -1);
    ASSERT_EQ(voice_lang_find("pt-BR"), -1);
    ASSERT_EQ(voice_lang_find(""), 0);
    ASSERT_EQ(voice_lang_find(NULL), -1);
    TEST_END();
}

static int parses(const char *in, const char *want)
{
    char code[8];
    int r = voice_lang_parse_other(in, code, sizeof code);
    if (!want) return r == 0 && code[0] == '\0';
    return r == 1 && strcmp(code, want) == 0;
}

int test_voice_lang_parse_other(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(parses("cy", "cy"));
    ASSERT_TRUE(parses(" CY ", "cy"));
    ASSERT_TRUE(parses("Welsh", "cy"));
    ASSERT_TRUE(parses("welsh", "cy"));
    ASSERT_TRUE(parses("  WELSH  ", "cy"));
    ASSERT_TRUE(parses("scottish gaelic", "gd"));
    ASSERT_TRUE(parses("mi", "mi"));
    ASSERT_TRUE(parses("Maori", "mi"));
    ASSERT_TRUE(parses("M\xC4\x81ori", "mi"));
    ASSERT_TRUE(parses("ga", "ga"));
    ASSERT_TRUE(parses("Irish", "ga"));
    ASSERT_TRUE(parses("fr", "fr"));
    ASSERT_TRUE(parses("xx", NULL));
    ASSERT_TRUE(parses("", NULL));
    ASSERT_TRUE(parses("   ", NULL));
    ASSERT_TRUE(parses("en-GB", NULL));
    ASSERT_TRUE(parses("English-ish", NULL));
    ASSERT_TRUE(parses("c", NULL));
    ASSERT_TRUE(parses("cyy", NULL));
    char big[301];
    memset(big, 'a', 300);
    big[300] = '\0';
    ASSERT_TRUE(parses(big, NULL));
    char code[8] = "zz";
    ASSERT_EQ(voice_lang_parse_other(NULL, code, sizeof code), 0);
    ASSERT_STR_EQ(code, "");
    ASSERT_EQ(voice_lang_parse_other("cy", NULL, 0), 0);
    char tiny[2];
    ASSERT_EQ(voice_lang_parse_other("cy", tiny, sizeof tiny), 0);
    TEST_END();
}

int test_voice_lang_name(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_lang_name("cy"), "Welsh");
    ASSERT_STR_EQ(voice_lang_name("en"), "English");
    ASSERT_NULL(voice_lang_name("zz"));
    ASSERT_NULL(voice_lang_name("CY"));
    ASSERT_NULL(voice_lang_name(""));
    ASSERT_NULL(voice_lang_name(NULL));
    TEST_END();
}

int test_voice_lang_other_hint(void)
{
    TEST_BEGIN();
    char h[256];
    ASSERT_EQ(voice_lang_other_hint("", h, sizeof h), 0);
    ASSERT_STR_EQ(h, "Type a two-letter ISO 639-1 language code such as cy for Welsh, "
                     "or the language's English name such as Welsh.");
    ASSERT_EQ(voice_lang_other_hint("welsh", h, sizeof h), 0);
    ASSERT_STR_EQ(h, "Welsh (cy)");
    ASSERT_EQ(voice_lang_other_hint(" CY", h, sizeof h), 0);
    ASSERT_STR_EQ(h, "Welsh (cy)");
    ASSERT_EQ(voice_lang_other_hint("xx", h, sizeof h), 1);
    ASSERT_STR_EQ(h, "Not recognised. Use a two-letter language code such as cy, or the "
                     "language's English name such as Welsh. It will not be saved.");
    char one[1] = { 'x' };
    (void)voice_lang_other_hint("xx", one, sizeof one);
    ASSERT_EQ(one[0], '\0');
    char four[4];
    (void)voice_lang_other_hint("welsh", four, sizeof four);
    ASSERT_STR_EQ(four, "Wel");
    ASSERT_TRUE(strstr(VOICE_LANG_OTHER_CUE, "cy or Welsh") != NULL);
    TEST_END();
}

static int iso_is(const char *id, const char *want)
{
    char o[16];
    voice_lang_iso(id, o, sizeof o);
    return strcmp(o, want) == 0;
}

int test_voice_lang_iso(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(iso_is("en-GB", "en"));
    ASSERT_TRUE(iso_is("en-US", "en"));
    ASSERT_TRUE(iso_is("pt-BR", "pt"));
    ASSERT_TRUE(iso_is("EN_au", "en"));
    ASSERT_TRUE(iso_is("en", "en"));
    ASSERT_TRUE(iso_is("cy", "cy"));
    ASSERT_TRUE(iso_is("", ""));
    ASSERT_TRUE(iso_is(NULL, ""));
    char two[3];
    voice_lang_iso("zh-Hant", two, sizeof two);
    ASSERT_STR_EQ(two, "zh");
    TEST_END();
}

int test_voice_lang_hint(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_lang_spelling_hint("en-GB"),
                  "Use British English spelling, for example colour, organise, centre and travelled.");
    ASSERT_NULL(voice_lang_spelling_hint("en-US"));
    ASSERT_NULL(voice_lang_spelling_hint("en"));
    ASSERT_NULL(voice_lang_spelling_hint(""));
    ASSERT_NULL(voice_lang_spelling_hint(NULL));
    TEST_END();
}

int test_voice_lang_combo_index(void)
{
    TEST_BEGIN();
    size_t n = 0;
    (void)voice_lang_table(&n);
    ASSERT_EQ(voice_lang_combo_index("en-GB"), 1);
    ASSERT_EQ(voice_lang_combo_index(""), 0);
    ASSERT_EQ(voice_lang_combo_index(NULL), 0);
    ASSERT_EQ(voice_lang_combo_index("fr"), 6);
    /* not listed (a custom code, or a legacy raw value): the Other item */
    ASSERT_EQ(voice_lang_combo_index("cy"), (int)n);
    ASSERT_EQ(voice_lang_combo_index("pt-BR"), (int)n);
    TEST_END();
}

int test_voice_lang_save_value(void)
{
    TEST_BEGIN();
    size_t n = 0;
    (void)voice_lang_table(&n);
    char out[32];
    /* a listed item stores its id, whatever the Other box holds */
    voice_lang_save_value(2, "cy", "fr", out, sizeof out);
    ASSERT_STR_EQ(out, "en-US");
    voice_lang_save_value(0, "", "fr", out, sizeof out);
    ASSERT_STR_EQ(out, "");
    /* Other with a recognised code or name stores the lower-case code */
    voice_lang_save_value((int)n, "CY", "fr", out, sizeof out);
    ASSERT_STR_EQ(out, "cy");
    voice_lang_save_value((int)n, " Welsh ", "fr", out, sizeof out);
    ASSERT_STR_EQ(out, "cy");
    /* an unrecognised Other keeps the previously saved language */
    voice_lang_save_value((int)n, "xyz", "fr", out, sizeof out);
    ASSERT_STR_EQ(out, "fr");
    voice_lang_save_value((int)n, "", "de", out, sizeof out);
    ASSERT_STR_EQ(out, "de");
    /* an untouched legacy raw value is written back unchanged */
    voice_lang_save_value((int)n, "pt-BR", "pt-BR", out, sizeof out);
    ASSERT_STR_EQ(out, "pt-BR");
    /* a bad selection index keeps the previous value */
    voice_lang_save_value((int)n, NULL, "es", out, sizeof out);   /* Other, no text */
    ASSERT_STR_EQ(out, "es");
    voice_lang_save_value(-1, "cy", "es", out, sizeof out);
    ASSERT_STR_EQ(out, "es");
    voice_lang_save_value((int)n + 1, "cy", NULL, out, sizeof out);
    ASSERT_STR_EQ(out, "");
    TEST_END();
}

int test_voice_lang_other_hint_loaded(void)
{
    TEST_BEGIN();
    char h[200];
    /* the untouched legacy value: neutral, says it is kept */
    ASSERT_EQ(voice_lang_other_hint_loaded("pt-BR", "pt-BR", h, sizeof h), 0);
    ASSERT_TRUE(strstr(h, "Saved as pt-BR.") == h);
    /* edited to something unrecognised: the ordinary error */
    ASSERT_EQ(voice_lang_other_hint_loaded("pt-B", "pt-BR", h, sizeof h), 1);
    ASSERT_TRUE(strstr(h, "Not recognised") == h);
    /* a recognised loaded value, or no loaded value: the ordinary hint */
    ASSERT_EQ(voice_lang_other_hint_loaded("cy", "cy", h, sizeof h), 0);
    ASSERT_STR_EQ(h, "Welsh (cy)");
    ASSERT_EQ(voice_lang_other_hint_loaded("xyz", "", h, sizeof h), 1);
    ASSERT_EQ(voice_lang_other_hint_loaded("xyz", NULL, h, sizeof h), 1);
    TEST_END();
}
