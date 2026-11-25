#include "test_framework.h"
#include "voice_echo.h"
#include <string.h>

static const char *REPLY =
    "I checked the machine. The C drive has 120 GB free and the D drive has 30 GB free. "
    "Everything else looks healthy. Shall I clean up the temp folder?";

int test_vecho_120gb_dropped(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_echo_match(REPLY, "120 GB free"), 1);
    ASSERT_EQ(voice_echo_match(REPLY, "the C drive has 120 GB free"), 1);
    TEST_END();
}

int test_vecho_80_percent_kept(void)
{
    TEST_BEGIN();
    /* 5 words, the last 4 ("check the d drive") are a run: 80% */
    ASSERT_EQ(voice_echo_match("Please check the D drive and then check the D drive",
                               "yes check the D drive"), 0);
    TEST_END();
}

int test_vecho_two_word_runs_never(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_echo_match(REPLY, "120 GB"), 0);
    ASSERT_EQ(voice_echo_match(REPLY, "healthy"), 0);
    ASSERT_EQ(voice_echo_match(REPLY, ""), 0);
    ASSERT_EQ(voice_echo_match(NULL, "one two three"), 0);
    ASSERT_EQ(voice_echo_match("one two three", NULL), 0);
    /* two 2-word runs never count */
    ASSERT_EQ(voice_echo_match("alpha beta x gamma delta y", "alpha beta gamma delta"), 0);
    TEST_END();
}

int test_vecho_two_runs_cover_90(void)
{
    TEST_BEGIN();
    /* transcript: 10 words; runs "one two three" and "red green blue yellow orange"
     * are 3 + 5 = 8 of 10: 80% kept; with a 6-word second run it is 9 of 10 */
    const char *spoken = "one two three then somewhere red green blue yellow orange violet end";
    ASSERT_EQ(voice_echo_match(spoken, "one two three zzz red green blue yellow orange violet"), 1);
    ASSERT_EQ(voice_echo_match(spoken, "one two three zzz qqq red green blue yellow orange"), 0);
    TEST_END();
}

int test_vecho_real_answer_kept(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_echo_match(REPLY, "yes please clean up the folder and the drive"), 0);
    ASSERT_EQ(voice_echo_match(REPLY, "no the D drive is fine thanks"), 0);
    TEST_END();
}

int test_vecho_whole_reply(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_echo_match(REPLY, "everything else looks healthy"), 1);
    ASSERT_EQ(voice_echo_match(REPLY, "Shall I clean up the temp folder"), 1);
    TEST_END();
}

int test_vecho_non_ascii_words(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_echo_match("caf\xC3\xA9 ouvert \xC3\xA0 9h", "caf\xC3\xA9 ouvert \xC3\xA0 9h"), 1);
    ASSERT_EQ(voice_echo_match("caf\xC3\xA9 ouvert \xC3\xA0 9h", "cafe ouvert a 9h"), 0);
    /* case and punctuation do not matter */
    ASSERT_EQ(voice_echo_match("Hello, WORLD of tests.", "hello world of tests"), 1);
    TEST_END();
}

int test_vphrase_each_phrase(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_phrase_match("repeat"), VPH_REPEAT);
    ASSERT_EQ(voice_phrase_match("Repeat that."), VPH_REPEAT);
    ASSERT_EQ(voice_phrase_match("say that again"), VPH_REPEAT);
    ASSERT_EQ(voice_phrase_match("again"), VPH_REPEAT);
    ASSERT_EQ(voice_phrase_match("What did you say?"), VPH_REPEAT);
    ASSERT_EQ(voice_phrase_match("continue"), VPH_CONTINUE);
    ASSERT_EQ(voice_phrase_match("carry on"), VPH_CONTINUE);
    ASSERT_EQ(voice_phrase_match("go on"), VPH_CONTINUE);
    ASSERT_EQ(voice_phrase_match("Keep going!"), VPH_CONTINUE);
    ASSERT_EQ(voice_phrase_match("repeat it"), VPH_NONE);
    ASSERT_EQ(voice_phrase_match(""), VPH_NONE);
    ASSERT_EQ(voice_phrase_match(NULL), VPH_NONE);
    TEST_END();
}

int test_vphrase_polite_forms(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_phrase_match("Could you please say that again?"), VPH_REPEAT);
    ASSERT_EQ(voice_phrase_match("carry on please"), VPH_CONTINUE);
    ASSERT_EQ(voice_phrase_match("please repeat that"), VPH_REPEAT);
    ASSERT_EQ(voice_phrase_match("can you repeat"), VPH_REPEAT);
    ASSERT_EQ(voice_phrase_match("could you keep going"), VPH_CONTINUE);
    ASSERT_EQ(voice_phrase_match("can you please go on please"), VPH_CONTINUE);
    ASSERT_EQ(voice_phrase_match("please"), VPH_NONE);
    ASSERT_EQ(voice_phrase_match("can you please"), VPH_NONE);
    TEST_END();
}

int test_vphrase_seven_words_never(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_phrase_match("could you please say that again please"), VPH_NONE); /* 7 words */
    ASSERT_EQ(voice_phrase_match("could you please say that again"), VPH_REPEAT);       /* 6 words */
    TEST_END();
}

int test_vphrase_inside_request_never(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_phrase_match("carry on with the backup"), VPH_NONE);
    ASSERT_EQ(voice_phrase_match("please do not repeat that"), VPH_NONE);
    TEST_END();
}

int test_vclass_echo_window(void)
{
    TEST_BEGIN();
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    r.spoken = "the C drive has 120 GB free";
    r.playing = 1;
    ASSERT_EQ(voice_turn_classify("120 GB free", &r, 50000), VTC_ECHO);
    r.playing = 0;
    r.spoken_end_at = 10000;
    ASSERT_EQ(voice_turn_classify("120 GB free", &r, 12000), VTC_ECHO);
    ASSERT_EQ(voice_turn_classify("120 GB free", &r, 12001), VTC_TEXT);
    /* inside the window but not an echo */
    ASSERT_EQ(voice_turn_classify("open the logs please", &r, 11000), VTC_TEXT);
    TEST_END();
}

int test_vclass_phrase_needs_recent_reply(void)
{
    TEST_BEGIN();
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    ASSERT_EQ(voice_turn_classify("repeat that", &r, 1000), VTC_TEXT);   /* no reply */
    ASSERT_EQ(voice_turn_classify("repeat that", NULL, 1000), VTC_TEXT);
    r.reply = "The disk is fine.";
    r.reply_at = 100000;
    ASSERT_EQ(voice_turn_classify("repeat that", &r, 100000 + 300000), VTC_REPEAT);
    ASSERT_EQ(voice_turn_classify("repeat that", &r, 100000 + 300001), VTC_TEXT);
    r.reply = "Shall I delete them?";
    ASSERT_EQ(voice_turn_classify("repeat that", &r, 100001), VTC_TEXT);
    TEST_END();
}

int test_vclass_continue_after_read_to_end(void)
{
    TEST_BEGIN();
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    r.reply = "Part one. Part two.";
    r.reply_at = 1000;
    ASSERT_EQ(voice_turn_classify("continue", &r, 2000), VTC_CONTINUE);
    r.read_to_end = 1;
    ASSERT_EQ(voice_turn_classify("continue", &r, 2000), VTC_TEXT);
    ASSERT_EQ(voice_turn_classify("repeat that", &r, 2000), VTC_REPEAT);
    TEST_END();
}

int test_vclass_empty(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_turn_classify("", NULL, 0), VTC_EMPTY);
    ASSERT_EQ(voice_turn_classify("  \t\n ", NULL, 0), VTC_EMPTY);
    ASSERT_EQ(voice_turn_classify(NULL, NULL, 0), VTC_EMPTY);
    ASSERT_EQ(voice_turn_classify("hello there", NULL, 0), VTC_TEXT);
    TEST_END();
}

int test_vecho_curly_apostrophe_nbsp_emdash(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_echo_match("Well I don\xE2\x80\x99" "t think the disk is full.", "i don't think the disk is full"), 1);
    ASSERT_EQ(voice_echo_match("the disk\xC2\xA0" "is\xE2\x80\x94" "full and healthy now", "the disk is full and healthy now"), 1);
    TEST_END();
}

/* The echo window survives the tick counter wrapping (32 bits on Windows). */
int test_vturn_echo_window_tick_wrap(void)
{
    TEST_BEGIN();
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    r.spoken = "the C drive has 120 GB free";
    r.spoken_end_at = (unsigned long)-500;
    ASSERT_EQ(voice_turn_classify("120 GB free", &r, 1000), VTC_ECHO);   /* 1500 ms after */
    ASSERT_EQ(voice_turn_classify("120 GB free", &r, 2600), VTC_TEXT);   /* 3100 ms after */
    TEST_END();
}

int test_vturn_phrase_age_tick_wrap(void)
{
    TEST_BEGIN();
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    r.reply = "The disk is fine.";
    r.reply_at = (unsigned long)-100000;
    ASSERT_EQ(voice_turn_classify("repeat that", &r, 100000), VTC_REPEAT);   /* 200 s after */
    ASSERT_EQ(voice_turn_classify("repeat that", &r, 250000), VTC_TEXT);     /* 350 s after */
    TEST_END();
}
