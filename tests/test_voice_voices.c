#include "test_framework.h"
#include "voice_voices.h"
#include <stdio.h>
#include <string.h>

static VoiceInfo mk(const char *name, const char *lang, int female, int onecore)
{
    VoiceInfo v;
    memset(&v, 0, sizeof v);
    snprintf(v.id, sizeof v.id, "%s\\%s", onecore ? "OC" : "SAPI", name);
    snprintf(v.name, sizeof v.name, "%s", name);
    snprintf(v.lang, sizeof v.lang, "%s", lang);
    v.female = female;
    v.onecore = onecore;
    return v;
}

int test_vvoices_chain_known_names_first(void)
{
    TEST_BEGIN();
    VoiceInfo v[] = {
        mk("Microsoft George Desktop", "809;9", 0, 0),
        mk("Microsoft Zira Desktop", "409;9", 1, 0),
        mk("Microsoft Susan Desktop", "809;9", 1, 0),
        mk("Microsoft Hazel Desktop", "809;9", 1, 0),
    };
    ASSERT_EQ(voice_choose_auto(v, 4), 3);
    /* Susan beats an unnamed en-GB woman */
    VoiceInfo w[] = {
        mk("Microsoft Abigail", "809;9", 1, 0),
        mk("Microsoft Susan Desktop", "809;9", 1, 0),
    };
    ASSERT_EQ(voice_choose_auto(w, 2), 1);
    /* a male Hazel-named voice does not count */
    VoiceInfo x[] = { mk("Hazel Man", "809", 0, 0), mk("Other", "809", 1, 0) };
    ASSERT_EQ(voice_choose_auto(x, 2), 1);
    TEST_END();
}

int test_vvoices_other_en_gb_female(void)
{
    TEST_BEGIN();
    VoiceInfo v[] = {
        mk("Microsoft Zira Desktop", "409;9", 1, 0),
        mk("Microsoft Ada", "809;9", 1, 0),
        mk("Microsoft Beth", "809;9", 1, 0),
    };
    ASSERT_EQ(voice_choose_auto(v, 3), 1);
    TEST_END();
}

int test_vvoices_any_english_female(void)
{
    TEST_BEGIN();
    VoiceInfo v[] = {
        mk("Microsoft George Desktop", "809;9", 0, 0),
        mk("Microsoft Zira Desktop", "409;9", 1, 0),
    };
    ASSERT_EQ(voice_choose_auto(v, 2), 1);
    /* a non-English woman is not enough */
    VoiceInfo w[] = { mk("Microsoft Hedda", "407;7", 1, 0), mk("George", "809", 0, 0) };
    ASSERT_EQ(voice_choose_auto(w, 2), 1);
    TEST_END();
}

int test_vvoices_any_en_gb(void)
{
    TEST_BEGIN();
    VoiceInfo v[] = {
        mk("Microsoft Hedda Desktop", "407;7", 1, 0),
        mk("Microsoft George Desktop", "809;9", 0, 0),
        mk("Microsoft David Desktop", "809;9", 0, 0),
    };
    ASSERT_EQ(voice_choose_auto(v, 3), 1);
    TEST_END();
}

int test_vvoices_none(void)
{
    TEST_BEGIN();
    VoiceInfo v[] = {
        mk("Microsoft Hedda Desktop", "407;7", 1, 0),
        mk("Microsoft David Desktop", "409;9", 0, 0),
    };
    ASSERT_EQ(voice_choose_auto(v, 2), -1);
    ASSERT_EQ(voice_choose_auto(v, 0), -1);
    ASSERT_EQ(voice_choose_auto(NULL, 0), -1);
    TEST_END();
}

int test_vvoices_dedupe_desktop_wins(void)
{
    TEST_BEGIN();
    char lc[64];
    VoiceInfo lo = mk("Microsoft Hazel DESKTOP", "809", 1, 0);
    voice_display_name(&lo, lc, sizeof lc);
    ASSERT_STR_EQ(lc, "Microsoft Hazel");
    VoiceInfo v[] = {
        mk("Microsoft Hazel", "809;9", 1, 1),          /* OneCore twin comes first */
        mk("Microsoft Zira Desktop", "409;9", 1, 0),
        mk("Microsoft Hazel Desktop", "809;9", 1, 0),  /* SAPI desktop, same display name */
        mk("MICROSOFT ZIRA", "409;9", 1, 1),
        mk("Microsoft Mark", "409;9", 0, 1),
    };
    size_t n = voice_list_dedupe(v, 5);
    ASSERT_EQ(n, 3u);
    ASSERT_STR_EQ(v[0].name, "Microsoft Hazel Desktop");
    ASSERT_EQ(v[0].onecore, 0);
    ASSERT_STR_EQ(v[1].name, "Microsoft Zira Desktop");
    ASSERT_STR_EQ(v[2].name, "Microsoft Mark");
    char disp[64];
    voice_display_name(&v[0], disp, sizeof disp);
    ASSERT_STR_EQ(disp, "Microsoft Hazel");
    voice_display_name(&v[2], disp, 8);
    ASSERT_STR_EQ(disp, "Microso");
    ASSERT_EQ(voice_list_dedupe(v, 0), 0u);
    TEST_END();
}

int test_vvoices_saved_gone(void)
{
    TEST_BEGIN();
    VoiceInfo v[] = { mk("A", "809", 1, 0), mk("B", "809", 1, 0) };
    ASSERT_EQ(voice_find_saved(v, 2, v[1].id), 1);
    ASSERT_EQ(voice_find_saved(v, 2, "SAPI\\Gone"), -1);
    ASSERT_EQ(voice_find_saved(v, 2, ""), -1);
    ASSERT_EQ(voice_find_saved(v, 2, NULL), -1);
    TEST_END();
}

int test_vvoices_lcid_parsing(void)
{
    TEST_BEGIN();
    VoiceInfo a = mk("a", "809;9", 0, 0);
    VoiceInfo b = mk("b", "409", 0, 0);
    VoiceInfo c = mk("c", "", 0, 0);
    VoiceInfo d = mk("d", "zz", 0, 0);
    VoiceInfo e = mk("e", "407;809", 0, 0);
    ASSERT_EQ(voice_info_lcid_primary(&a), 0x809);
    ASSERT_EQ(voice_info_lcid_primary(&b), 0x409);
    ASSERT_EQ(voice_info_lcid_primary(&c), 0);
    ASSERT_EQ(voice_info_lcid_primary(&d), 0);
    ASSERT_EQ(voice_info_is_en_gb(&a), 1);
    ASSERT_EQ(voice_info_is_en_gb(&b), 0);
    ASSERT_EQ(voice_info_is_en_gb(&c), 0);
    ASSERT_EQ(voice_info_is_en_gb(&e), 1);
    ASSERT_EQ(voice_info_is_english(&a), 1);
    ASSERT_EQ(voice_info_is_english(&b), 1);
    ASSERT_EQ(voice_info_is_english(&c), 0);
    ASSERT_EQ(voice_info_is_english(&d), 0);
    ASSERT_EQ(voice_info_is_english(&e), 1);
    VoiceInfo f = mk("f", "407;7", 0, 0);
    ASSERT_EQ(voice_info_is_english(&f), 0);
    ASSERT_EQ(voice_info_lcid_primary(NULL), 0);
    TEST_END();
}

int test_vvoices_notice_due(void)
{
    TEST_BEGIN();
    VoiceInfo v[] = {
        mk("Microsoft Hazel Desktop", "809;9", 1, 0),
        mk("Microsoft Zira Desktop", "409;9", 1, 0),
        mk("Microsoft George Desktop", "809;9", 0, 0),
    };
    ASSERT_EQ(voice_notice_due(v, 3, 0, 1, 0), 0);   /* Hazel */
    ASSERT_EQ(voice_notice_due(v, 3, 1, 1, 0), 1);   /* Zira */
    ASSERT_EQ(voice_notice_due(v, 3, 2, 1, 0), 1);   /* a man, even en-GB */
    ASSERT_EQ(voice_notice_due(v, 3, 1, 1, 1), 0);   /* already shown */
    ASSERT_EQ(voice_notice_due(v, 3, 1, 0, 0), 0);   /* a chosen voice, not Automatic */
    ASSERT_EQ(voice_notice_due(v, 3, -1, 1, 0), 1);  /* system default */
    ASSERT_EQ(voice_notice_due(v, 3, -1, 0, 0), 0);
    TEST_END();
}
