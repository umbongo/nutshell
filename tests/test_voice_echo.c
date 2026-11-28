#include "test_framework.h"
#include "voice_echo.h"
#include <stdio.h>
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

/* ---- a cut-in turn's transcript is always checked (late, and short fragments) ---- */

int test_vclass_cutin_late_transcript_checked(void)
{
    TEST_BEGIN();
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    r.spoken = REPLY;
    r.playing = 0;
    r.spoken_end_at = 10000;
    /* 60 s after the speech stopped: outside the 2 s window */
    ASSERT_EQ(voice_turn_classify("the C drive has 120 GB free", &r, 70000), VTC_TEXT);
    r.cut_in = 1;
    ASSERT_EQ(voice_turn_classify("the C drive has 120 GB free", &r, 70000), VTC_ECHO);
    /* a late transcript that does not match is the user's */
    ASSERT_EQ(voice_turn_classify("open the logs and show me the disk usage", &r, 70000), VTC_TEXT);
    /* 80% is still kept */
    ASSERT_EQ(voice_turn_classify("check the d drive", &r, 70000), VTC_TEXT);
    TEST_END();
}

int test_vclass_cutin_fragments(void)
{
    TEST_BEGIN();
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    r.spoken = REPLY;
    r.cut_in = 1;
    r.spoken_end_at = 10000;
    r.recent = "The C drive has 120 GB free and the D drive";
    /* two adjacent words from the recent text: echo, case ignored */
    ASSERT_EQ(voice_turn_classify("120 GB", &r, 70000), VTC_ECHO);
    ASSERT_EQ(voice_turn_classify("Drive has", &r, 70000), VTC_ECHO);
    /* two words that are not in it, or in the wrong order: the user's */
    ASSERT_EQ(voice_turn_classify("stop please", &r, 70000), VTC_TEXT);
    ASSERT_EQ(voice_turn_classify("free has", &r, 70000), VTC_TEXT);
    ASSERT_EQ(voice_turn_classify("120 stop", &r, 70000), VTC_TEXT);
    /* a word of the reply that was not said in the recent text */
    ASSERT_EQ(voice_turn_classify("healthy", &r, 70000), VTC_TEXT);
    ASSERT_EQ(voice_turn_classify("everything healthy", &r, 70000), VTC_TEXT);
    /* one word: the user's unless the recent text contains it */
    ASSERT_EQ(voice_turn_classify("stop", &r, 70000), VTC_TEXT);
    ASSERT_EQ(voice_turn_classify("drive", &r, 70000), VTC_ECHO);
    r.recent = "and then restart";
    ASSERT_EQ(voice_turn_classify("restart", &r, 70000), VTC_ECHO);
    /* no recent text, or not a cut-in: nothing to compare with */
    r.recent = NULL;
    ASSERT_EQ(voice_turn_classify("drive", &r, 70000), VTC_TEXT);
    r.recent = "The C drive";
    r.cut_in = 0;
    ASSERT_EQ(voice_turn_classify("drive", &r, 70000), VTC_TEXT);
    TEST_END();
}

int test_vclass_cutin_fragment_helper(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_echo_fragment("a b c", "b c"), 1);
    ASSERT_EQ(voice_echo_fragment("a b c", "a c"), 0);       /* in order, not adjacent: kept */
    ASSERT_EQ(voice_echo_fragment("a b c", "c a"), 0);
    ASSERT_EQ(voice_echo_fragment("a b c", "b b"), 0);
    ASSERT_EQ(voice_echo_fragment("a b b", "b b"), 1);
    ASSERT_EQ(voice_echo_fragment("a b c d", "c d"), 1);
    ASSERT_EQ(voice_echo_fragment("a b c d", "b d"), 0);
    ASSERT_EQ(voice_echo_fragment("a b c", "a b c"), 0);     /* 3 words: the 90% rule's business */
    ASSERT_EQ(voice_echo_fragment("a b c", ""), 0);
    ASSERT_EQ(voice_echo_fragment("a b c", "  ,. "), 0);
    ASSERT_EQ(voice_echo_fragment("", "a"), 0);
    ASSERT_EQ(voice_echo_fragment(NULL, "a"), 0);
    ASSERT_EQ(voice_echo_fragment("a b c", NULL), 0);
    TEST_END();
}

int test_vtrail_recent_text(void)
{
    TEST_BEGIN();
    const char *spoken = "one two three four five six seven eight";
    static const size_t off[8] = { 0, 4, 8, 14, 19, 24, 28, 34 };
    VoiceWordTrail t;
    char out[128];
    voice_trail_reset(&t);
    ASSERT_EQ((int)voice_trail_recent(&t, spoken, 4500, VOICE_ECHO_RECENT_MS, out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");
    for (int i = 0; i < 8; i++) voice_trail_note(&t, 1000ul + 500ul * (unsigned long)i, off[i]);
    /* cut as "eight" is reported (4500): the words reported from 1500 on */
    ASSERT_EQ((int)voice_trail_recent(&t, spoken, 4500, 3000, out, sizeof out),
              (int)strlen("two three four five six seven eight"));
    ASSERT_STR_EQ(out, "two three four five six seven eight");
    /* cut at 3000 (after "five"): the words from 1000 (3000 - 3000 included) to "five" */
    voice_trail_recent(&t, spoken, 3000, 3000, out, sizeof out);
    ASSERT_STR_EQ(out, "one two three four five");
    /* a shorter window */
    voice_trail_recent(&t, spoken, 4500, 1000, out, sizeof out);
    ASSERT_STR_EQ(out, "six seven eight");
    /* nothing reported yet at the cut */
    ASSERT_EQ((int)voice_trail_recent(&t, spoken, 500, VOICE_ECHO_RECENT_MS, out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");
    /* a cap smaller than the text: cut short, NUL-terminated */
    char small[8];
    voice_trail_recent(&t, spoken, 4500, 3000, small, sizeof small);
    ASSERT_STR_EQ(small, "eight");               /* the tail, from a word's start */
    TEST_END();
}

int test_vtrail_wraps_and_resets(void)
{
    TEST_BEGIN();
    char spoken[1200];
    char out[256];
    VoiceWordTrail t;
    size_t o = 0;
    for (int i = 0; i < 200; i++) o += (size_t)snprintf(spoken + o, sizeof spoken - o, "w%d ", i % 10);
    voice_trail_reset(&t);
    for (int i = 0; i < 200; i++) voice_trail_note(&t, 100ul * (unsigned long)i, (size_t)i * 3);
    ASSERT_TRUE(t.n <= VOICE_TRAIL_MAX);
    /* cut at 19900: the words reported from 16900 to 19900, w169 .. w199 (digit = i % 10) */
    voice_trail_recent(&t, spoken, 19900, 3000, out, sizeof out);
    ASSERT_EQ((int)strlen(out), 31 * 3 - 1);
    ASSERT_EQ(strncmp(out, "w9 w0 w1", 8), 0);
    voice_trail_reset(&t);
    ASSERT_EQ((int)voice_trail_recent(&t, spoken, 19900, VOICE_ECHO_RECENT_MS, out, sizeof out), 0);
    /* an offset past the text is ignored */
    voice_trail_note(&t, 1000, 99999);
    ASSERT_EQ((int)voice_trail_recent(&t, spoken, 1000, VOICE_ECHO_RECENT_MS, out, sizeof out), 0);
    TEST_END();
}

int test_vecho_bargein_words_are_never_fragments(void)
{
    TEST_BEGIN();
    static const char *const words[] = { "stop", "wait", "no", "yes", "cancel", "hold on",
                                         "pause", "quiet", "Stop!", "HOLD ON.", NULL };
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    r.spoken = REPLY;
    r.cut_in = 1;
    r.spoken_end_at = 10000;
    r.recent = "and then stop wait no yes cancel hold on pause quiet please";
    for (int i = 0; words[i]; i++) {
        ASSERT_EQ(voice_echo_fragment(r.recent, words[i]), 0);
        ASSERT_EQ(voice_turn_classify(words[i], &r, 70000), VTC_TEXT);
    }
    /* other words of that text are still fragments */
    ASSERT_EQ(voice_turn_classify("please", &r, 70000), VTC_ECHO);
    TEST_END();
}

int test_vclass_phrase_is_not_a_fragment(void)
{
    TEST_BEGIN();
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    r.spoken = REPLY;
    r.cut_in = 1;
    r.spoken_end_at = 10000;
    r.reply = "The disk is fine.";
    r.reply_at = 69000;
    r.recent = "shall I repeat that and carry on";
    ASSERT_EQ(voice_turn_classify("repeat that", &r, 70000), VTC_REPEAT);
    ASSERT_EQ(voice_turn_classify("Carry on", &r, 70000), VTC_CONTINUE);
    ASSERT_EQ(voice_turn_classify("please repeat that", &r, 70000), VTC_REPEAT);
    /* a phrase with no recent reply to act on is the user's text, not an echo */
    r.reply = NULL;
    ASSERT_EQ(voice_turn_classify("repeat that", &r, 70000), VTC_TEXT);
    TEST_END();
}

int test_vecho_fragment_two_words_must_be_adjacent(void)
{
    TEST_BEGIN();
    VoiceReplyCtx r;
    memset(&r, 0, sizeof r);
    r.spoken = REPLY;
    r.cut_in = 1;
    r.spoken_end_at = 10000;
    r.recent = "the C drive has 120 GB free";
    ASSERT_EQ(voice_turn_classify("drive has", &r, 70000), VTC_ECHO);       /* adjacent */
    ASSERT_EQ(voice_turn_classify("drive free", &r, 70000), VTC_TEXT);      /* in order, not adjacent */
    ASSERT_EQ(voice_turn_classify("c has", &r, 70000), VTC_TEXT);
    TEST_END();
}

int test_vtrail_recent_window_is_1500ms(void)
{
    TEST_BEGIN();
    const char *spoken = "alpha bravo charlie delta echo";
    static const size_t off[5] = { 0, 6, 12, 20, 26 };
    VoiceWordTrail t;
    char out[64];
    ASSERT_EQ(VOICE_ECHO_RECENT_MS, 1500);
    voice_trail_reset(&t);
    for (int i = 0; i < 5; i++) voice_trail_note(&t, 1000ul + 500ul * (unsigned long)i, off[i]);
    /* cut at 3000: "alpha" was said 2 s before and is not recent */
    voice_trail_recent(&t, spoken, 3000, VOICE_ECHO_RECENT_MS, out, sizeof out);
    ASSERT_STR_EQ(out, "bravo charlie delta echo");
    voice_trail_recent(&t, spoken, 2900, VOICE_ECHO_RECENT_MS, out, sizeof out);
    ASSERT_STR_EQ(out, "bravo charlie delta");
    TEST_END();
}

int test_vtrail_recent_keeps_the_tail_when_too_long(void)
{
    TEST_BEGIN();
    const char *spoken = "one two three four five six seven eight";
    static const size_t off[8] = { 0, 4, 8, 14, 19, 24, 28, 34 };
    VoiceWordTrail t;
    char out[16];
    voice_trail_reset(&t);
    for (int i = 0; i < 8; i++) voice_trail_note(&t, 1000ul + 100ul * (unsigned long)i, off[i]);
    /* 15 bytes fit: "six seven eight" is 15 chars, so the tail starts at a word boundary */
    ASSERT_EQ((int)voice_trail_recent(&t, spoken, 1700, 3000, out, 16), 15);
    ASSERT_STR_EQ(out, "six seven eight");
    ASSERT_EQ((int)voice_trail_recent(&t, spoken, 1700, 3000, out, 15), 11);   /* "six" does not fit whole */
    ASSERT_STR_EQ(out, "seven eight");
    ASSERT_EQ((int)voice_trail_recent(&t, spoken, 1700, 3000, out, 5), 0);     /* not even "eight" */
    ASSERT_STR_EQ(out, "");
    TEST_END();
}

int test_vtrail_recent_across_the_tick_wrap(void)
{
    TEST_BEGIN();
    const char *spoken = "one two three four five six seven eight";
    static const size_t off[8] = { 0, 4, 8, 14, 19, 24, 28, 34 };
    VoiceWordTrail t;
    char out[64];
    const unsigned long base = (unsigned long)-1000;   /* the counter wraps after two words */
    voice_trail_reset(&t);
    for (int i = 0; i < 8; i++) voice_trail_note(&t, base + 500ul * (unsigned long)i, off[i]);
    /* cut at 500 on the wrapped clock: the words from 1000 ms before, across the wrap */
    voice_trail_recent(&t, spoken, base + 1500ul, 1000, out, sizeof out);
    ASSERT_STR_EQ(out, "two three four");
    voice_trail_recent(&t, spoken, base + 500ul, 1000, out, sizeof out);
    ASSERT_STR_EQ(out, "one two");
    TEST_END();
}

int test_vtrail_heard_includes_the_start_and_a_second_of_lookahead(void)
{
    TEST_BEGIN();
    const char *spoken = "one two three four five six seven eight nine ten";
    static const size_t off[10] = { 0, 4, 8, 14, 19, 24, 28, 34, 40, 45 };
    VoiceWordTrail t;
    char out[128];
    voice_trail_reset(&t);
    /* the AI engine's first event is its second word: the speak's own start is noted by the glue */
    voice_trail_note(&t, 1000, off[0]);
    voice_trail_note(&t, 1500, off[2]);
    voice_trail_note(&t, 2000, off[3]);
    voice_trail_note(&t, 2500, off[4]);
    /* what the engine reported: the first word is there with the start noted */
    voice_trail_recent(&t, spoken, 2500, VOICE_ECHO_RECENT_MS, out, sizeof out);
    ASSERT_STR_EQ(out, "one two three four five");
    /* and about a second of words it has not yet reported (2.7 words a second: three) */
    ASSERT_EQ((int)voice_trail_heard(&t, spoken, 2500, out, sizeof out), (int)strlen("one two three four five six seven eight"));
    ASSERT_STR_EQ(out, "one two three four five six seven eight");
    /* never past the end of the text */
    ASSERT_EQ((int)voice_trail_heard(&t, "one two three four five six", 2500, out, sizeof out), 27);
    ASSERT_STR_EQ(out, "one two three four five six");
    /* a single note: three words a second assumed */
    voice_trail_reset(&t);
    voice_trail_note(&t, 1000, off[0]);
    voice_trail_heard(&t, spoken, 1000, out, sizeof out);
    ASSERT_STR_EQ(out, "one two three four");
    /* nothing reported: nothing heard */
    voice_trail_reset(&t);
    ASSERT_EQ((int)voice_trail_heard(&t, spoken, 2500, out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");
    TEST_END();
}
