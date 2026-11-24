#include "test_framework.h"
#include "voice_transcript.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A simulated edit box: UTF-8 bytes, edited with UTF-16 offsets like
 * EM_SETSEL(from, -1) + EM_REPLACESEL. */
typedef struct { char b[2048]; } Box;

static size_t u16_to_byte(const char *s, size_t u16)
{
    size_t i = 0, u = 0, n = strlen(s);
    while (i < n && u < u16) {
        unsigned char c = (unsigned char)s[i];
        u += (c >= 0xF0) ? 2 : 1;
        i++;
        while (i < n && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    }
    return i;
}

static void box_apply(Box *x, size_t from_u16, const char *text)
{
    size_t at = u16_to_byte(x->b, from_u16);
    strcpy(x->b + at, text);
}

/* Render and apply; returns the from offset, or (size_t)-1 on failure. */
static size_t step(VoiceTranscript *t, Box *x)
{
    size_t from = 0;
    char *r = vt_render(t, &from);
    if (!r) return (size_t)-1;
    box_apply(x, from, r);
    free(r);
    return from;
}

/* The box tail after the draft. */
static const char *tail(const Box *x, const char *draft) { return x->b + strlen(draft); }

int test_vt_draft_sep(void)
{
    TEST_BEGIN();
    {
        VoiceTranscript t; vt_init(&t, 2, 1);
        size_t from = 99;
        ASSERT_EQ(vt_delta(&t, "i1", "there", 0), 0);
        char *r = vt_render(&t, &from);
        ASSERT_EQ(from, 2U);
        ASSERT_STR_EQ(r, " there");
        free(r);
        vt_free(&t);
    }
    {
        VoiceTranscript t; vt_init(&t, 3, 0);
        size_t from = 99;
        vt_delta(&t, "i1", "there", 0);
        char *r = vt_render(&t, &from);
        ASSERT_EQ(from, 3U);
        ASSERT_STR_EQ(r, "there");
        free(r);
        vt_free(&t);
    }
    {
        VoiceTranscript t; vt_init(&t, 0, 0);
        size_t from = 99;
        vt_delta(&t, "i1", " there", 0);   /* leading space dropped on an empty box */
        char *r = vt_render(&t, &from);
        ASSERT_EQ(from, 0U);
        ASSERT_STR_EQ(r, "there");
        free(r);
        vt_free(&t);
    }
    {
        VoiceTranscript t; vt_init(&t, 2, 1);
        size_t from = 99;
        char *r = vt_render(&t, &from);   /* nothing yet: no dangling separator */
        ASSERT_STR_EQ(r, "");
        free(r);
        vt_free(&t);
    }
    TEST_END();
}

int test_vt_openai_revision(void)
{
    TEST_BEGIN();
    Box x; strcpy(x.b, "hi");
    VoiceTranscript t; vt_init(&t, 2, 1);
    vt_delta(&t, "i1", "I sea", 0);
    step(&t, &x);
    vt_delta(&t, "i1", " the", 0);
    step(&t, &x);
    ASSERT_STR_EQ(tail(&x, "hi"), " I sea the");
    vt_complete(&t, "i1", "I see the boat.");
    size_t from = step(&t, &x);
    ASSERT_STR_EQ(tail(&x, "hi"), " I see the boat.");
    ASSERT_TRUE(from <= 2U + strlen(" I see"));   /* "sea"/"see" differ at the 'a' */
    size_t from2 = step(&t, &x);
    ASSERT_EQ(from2, 2U + strlen(" I see the boat."));
    ASSERT_EQ(t.stable_u16, strlen(" I see the boat."));
    ASSERT_EQ(t.shown_u16, strlen(" I see the boat."));
    ASSERT_EQ(t.nseg, 0);
    vt_free(&t);
    TEST_END();
}

int test_vt_openai_interleaved(void)
{
    TEST_BEGIN();
    Box x; x.b[0] = 0;
    VoiceTranscript t; vt_init(&t, 0, 0);
    vt_delta(&t, "i1", "one", 0);
    step(&t, &x);
    vt_delta(&t, "i2", "two", 0);
    step(&t, &x);
    vt_delta(&t, "i1", " more", 0);
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "one more two");
    vt_complete(&t, "i2", "Two.");
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "one more Two.");
    ASSERT_EQ(t.clen, 0U);          /* i1 still open: i2 not folded */
    ASSERT_EQ(t.nseg, 2);
    ASSERT_EQ(vt_all_done(&t), 0);
    vt_complete(&t, "i1", "One more.");
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "One more. Two.");
    ASSERT_EQ(t.nseg, 0);
    ASSERT_EQ(vt_all_done(&t), 1);
    char *s = vt_text(&t);
    ASSERT_STR_EQ(s, "One more. Two.");
    free(s);
    vt_free(&t);
    TEST_END();
}

int test_vt_mistral_append(void)
{
    TEST_BEGIN();
    Box x; strcpy(x.b, "A ");
    VoiceTranscript t; vt_init(&t, 2, 0);
    vt_delta(&t, "", "Hello", 1);
    step(&t, &x);
    vt_delta(&t, "", " world", 1);
    step(&t, &x);
    vt_delta(&t, "", ".", 1);
    step(&t, &x);
    ASSERT_STR_EQ(tail(&x, "A "), "Hello world.");
    vt_replace_all(&t, "Hello, world.");
    size_t from = step(&t, &x);
    ASSERT_STR_EQ(tail(&x, "A "), "Hello, world.");   /* the invariant */
    ASSERT_TRUE(from >= 2U && from <= 2U + 5U);        /* common prefix "Hello" kept */
    ASSERT_EQ(vt_all_done(&t), 1);
    /* a shorter replacement deletes the surplus */
    vt_replace_all(&t, "Hi");
    step(&t, &x);
    ASSERT_STR_EQ(tail(&x, "A "), "Hi");
    vt_free(&t);
    TEST_END();
}

int test_vt_utf16_offsets(void)
{
    TEST_BEGIN();
    const char *draft = "Caf\xC3\xA9 \xF0\x9F\x98\x80";   /* Café + space + U+1F600 */
    ASSERT_EQ(vt_utf16_len(draft, strlen(draft)), 7U);
    ASSERT_EQ(vt_utf16_len("\xC3\xA9", 2), 1U);
    ASSERT_EQ(vt_utf16_len("\xF0\x9F\x98\x80", 4), 2U);
    ASSERT_EQ(vt_utf16_len("\xE2\x82\xAC", 3), 1U);
    Box x; strcpy(x.b, draft);
    VoiceTranscript t; vt_init(&t, 7, 1);
    vt_delta(&t, "i1", "ok", 0);
    size_t from = 0;
    char *r = vt_render(&t, &from);
    ASSERT_EQ(from, 7U);
    ASSERT_STR_EQ(r, " ok");
    box_apply(&x, from, r);
    free(r);
    /* non-ASCII inside the live text, then a revision after it */
    vt_delta(&t, "i1", " caf\xC3\xA9 \xF0\x9F\x98\x80 x", 0);
    step(&t, &x);
    vt_complete(&t, "i1", "ok caf\xC3\xA9 \xF0\x9F\x98\x80 y");
    from = step(&t, &x);
    ASSERT_STR_EQ(x.b, "Caf\xC3\xA9 \xF0\x9F\x98\x80 ok caf\xC3\xA9 \xF0\x9F\x98\x80 y");
    /* from counts the emoji as two units: 7 draft + " ok café " (9) + 2 */
    ASSERT_EQ(from, 7U + 9U + 2U + 1U);
    vt_free(&t);

    /* a change that begins in the middle of a multibyte character backs up
     * to its start: U+00E9 (C3 A9) becomes U+00E8 (C3 A8) */
    Box y; y.b[0] = 0;
    VoiceTranscript u; vt_init(&u, 0, 0);
    vt_delta(&u, "i1", "a\xC3\xA9", 0);
    step(&u, &y);
    vt_complete(&u, "i1", "a\xC3\xA8");
    from = step(&u, &y);
    ASSERT_STR_EQ(y.b, "a\xC3\xA8");
    ASSERT_EQ(from, 1U);
    vt_free(&u);
    TEST_END();
}

int test_vt_newlines(void)
{
    TEST_BEGIN();
    Box x; x.b[0] = 0;
    VoiceTranscript t; vt_init(&t, 0, 0);
    vt_delta(&t, "i1", "a\r\nb", 0);
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "a  b");
    vt_complete(&t, "i1", "c\nd");
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "c d");
    vt_free(&t);
    TEST_END();
}

int test_vt_join_rules(void)
{
    TEST_BEGIN();
    Box x; x.b[0] = 0;
    VoiceTranscript t; vt_init(&t, 0, 0);
    vt_complete(&t, "i1", "One.");
    vt_delta(&t, "i2", "two", 0);
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "One. two");           /* space inserted between pieces */
    vt_complete(&t, "i2", " three");          /* whitespace at the join: none added */
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "One. three");
    vt_free(&t);
    TEST_END();
}

int test_vt_seg_overflow(void)
{
    TEST_BEGIN();
    VoiceTranscript t; vt_init(&t, 0, 0);
    for (int i = 0; i < 33; i++) {
        char id[16], txt[16];
        snprintf(id, sizeof id, "item%d", i);
        snprintf(txt, sizeof txt, "w%d", i);
        ASSERT_EQ(vt_delta(&t, id, txt, 0), 0);
    }
    ASSERT_EQ(t.nseg, VT_SEG_MAX);
    char *s = vt_text(&t);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(strstr(s, "w0"));
    ASSERT_NOT_NULL(strstr(s, "w32"));
    ASSERT_NULL(strstr(strstr(s, "w32") + 1, "w32"));   /* once */
    ASSERT_TRUE(strncmp(s, "w0 w1 w2", 8) == 0);
    free(s);
    vt_free(&t);
    TEST_END();
}

int test_vt_free_wipes(void)
{
    TEST_BEGIN();
    VoiceTranscript t; vt_init(&t, 0, 0);
    vt_complete(&t, "i1", "secret");
    vt_delta(&t, "i2", "more", 0);
    char *r = vt_render(&t, &(size_t){0});
    free(r);
    vt_free(&t);
    ASSERT_NULL(t.committed);
    ASSERT_NULL(t.shown);
    ASSERT_EQ(t.clen, 0U);
    ASSERT_EQ(t.slen, 0U);
    ASSERT_EQ(t.nseg, 0);
    int bad = 0;
    for (int i = 0; i < VT_SEG_MAX; i++) if (t.seg[i].text || t.seg[i].tlen) bad++;
    ASSERT_EQ(bad, 0);
    TEST_END();
}
