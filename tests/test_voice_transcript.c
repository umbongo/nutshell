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
    ASSERT_EQ(from, 7U);   /* " I se" is shared; 'a' vs 'e' is at 2 + 5 */
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

/* I1: committed items are registered in commit order, so a completion that
 * arrives before any delta (and out of order) still lands in commit order */
int test_vt_register_commit_order(void)
{
    TEST_BEGIN();
    Box x; x.b[0] = 0;
    VoiceTranscript t; vt_init(&t, 0, 0);
    ASSERT_EQ(vt_register(&t, "A"), 0);
    ASSERT_EQ(vt_register(&t, "B"), 0);
    ASSERT_EQ(vt_register(&t, "A"), 0);          /* idempotent */
    ASSERT_EQ(t.nseg, 2);
    vt_complete(&t, "B", "Second sentence.");
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "Second sentence.");
    vt_complete(&t, "A", "First sentence.");
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "First sentence. Second sentence.");
    ASSERT_EQ(vt_all_done(&t), 1);
    ASSERT_EQ(vt_register(&t, "A"), 0);          /* finished: ignored */
    ASSERT_EQ(t.nseg, 0);
    ASSERT_EQ(vt_register(&t, NULL), -1);
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

int test_vt_invalid_utf8(void)
{
    TEST_BEGIN();
    static const char *bad[] = {
        "\xF0\x9F\x98",        /* truncated 4-byte sequence: 3 bytes */
        "\x80",                  /* stray continuation */
        "\xC0\xAF",             /* overlong '/' */
        "\xED\xA0\x80",        /* CESU surrogate D800 */
        "\xF4\x90\x80\x80",   /* above U+10FFFF */
    };
    static const int nrep[] = { 3, 1, 2, 3, 4 };
    for (size_t k = 0; k < sizeof bad / sizeof *bad; k++) {
        Box x; x.b[0] = 0;
        VoiceTranscript t; vt_init(&t, 0, 0);
        char in[32];
        snprintf(in, sizeof in, "a%sb", bad[k]);
        ASSERT_EQ(vt_delta(&t, "i1", in, 0), 0);
        step(&t, &x);
        char want[64] = "a";
        for (int j = 0; j < nrep[k]; j++) strcat(want, "\xEF\xBF\xBD");
        strcat(want, "b");
        ASSERT_STR_EQ(x.b, want);
        /* every UTF-16 offset is exact: a revision after it lands right */
        vt_delta(&t, "i1", " \xF0\x9F\x98\x80 z", 0);
        step(&t, &x);
        vt_complete(&t, "i1", "A\xEF\xBF\xBD");   /* differs at the first unit */
        size_t from = step(&t, &x);
        ASSERT_EQ(from, 0U);
        ASSERT_STR_EQ(x.b, "A\xEF\xBF\xBD");
        ASSERT_EQ(vt_utf16_len(want, strlen(want)), 2U + (size_t)nrep[k]);
        vt_free(&t);
    }
    /* offsets after sanitised text: the change sits after the U+FFFD run */
    Box y; y.b[0] = 0;
    VoiceTranscript u; vt_init(&u, 0, 0);
    vt_delta(&u, "i1", "\x80\xC0\xAF q", 0);
    step(&u, &y);
    vt_complete(&u, "i1", "\x80\xC0\xAF r");
    size_t from = step(&u, &y);
    ASSERT_EQ(from, 4U);   /* three U+FFFD + space */
    ASSERT_STR_EQ(y.b, "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD r");
    vt_free(&u);
    /* replace_all sanitises too */
    VoiceTranscript w; vt_init(&w, 0, 0);
    vt_replace_all(&w, "x\x80");
    char *s = vt_text(&w);
    ASSERT_STR_EQ(s, "x\xEF\xBF\xBD");
    free(s);
    vt_free(&w);
    TEST_END();
}

int test_vt_finished_ids_ignored(void)
{
    TEST_BEGIN();
    Box x; x.b[0] = 0;
    VoiceTranscript t; vt_init(&t, 0, 0);
    vt_delta(&t, "i1", "one", 0);
    vt_complete(&t, "i1", "One.");
    step(&t, &x);
    ASSERT_EQ(vt_complete(&t, "i1", "Again"), 0);
    ASSERT_EQ(vt_delta(&t, "i1", " late", 0), 0);
    ASSERT_EQ(step(&t, &x), 4U);
    ASSERT_STR_EQ(x.b, "One.");
    ASSERT_EQ(t.nseg, 0);
    ASSERT_EQ(vt_all_done(&t), 1);
    /* done but not yet folded (i2 open in front of it) */
    vt_delta(&t, "i2", "two", 0);
    vt_delta(&t, "i3", "three", 0);
    vt_complete(&t, "i3", "Three.");
    vt_delta(&t, "i3", " late", 0);
    vt_complete(&t, "i3", "Other");
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "One. two Three.");
    vt_complete(&t, "i2", "Two.");
    step(&t, &x);
    ASSERT_STR_EQ(x.b, "One. Two. Three.");
    ASSERT_EQ(vt_all_done(&t), 1);
    vt_free(&t);

    /* an id folded by table overflow is ignored afterwards */
    VoiceTranscript o; vt_init(&o, 0, 0);
    for (int i = 0; i < 33; i++) {
        char id[16], txt[16];
        snprintf(id, sizeof id, "item%d", i);
        snprintf(txt, sizeof txt, "w%d", i);
        vt_delta(&o, id, txt, 0);
    }
    ASSERT_EQ(vt_delta(&o, "item0", "dup", 0), 0);
    ASSERT_EQ(vt_complete(&o, "item0", "dup"), 0);
    ASSERT_EQ(o.nseg, VT_SEG_MAX);
    char *s = vt_text(&o);
    ASSERT_NULL(strstr(s, "dup"));
    free(s);
    vt_free(&o);
    TEST_END();
}

/* vt_rendered: the whole text the last render left after the draft (separator,
 * committed, open segments), whatever `from` the render reported. */
int test_vt_rendered_full_text(void)
{
    TEST_BEGIN();
    VoiceTranscript t; vt_init(&t, 4, 1);
    ASSERT_STR_EQ(vt_rendered(&t), "");
    vt_delta(&t, "i1", "hello", 0);
    free(vt_render(&t, NULL));
    ASSERT_STR_EQ(vt_rendered(&t), " hello");
    vt_delta(&t, "i1", " world", 0);
    size_t from = 0;
    char *r = vt_render(&t, &from);
    ASSERT_STR_EQ(r, " world");               /* only the changed tail ... */
    ASSERT_STR_EQ(vt_rendered(&t), " hello world");   /* ... but the whole text here */
    free(r);
    vt_complete(&t, "i1", "Hello world.");
    free(vt_render(&t, NULL));
    ASSERT_STR_EQ(vt_rendered(&t), " Hello world.");
    ASSERT_STR_EQ(vt_rendered(NULL), "");
    vt_free(&t);
    TEST_END();
}
