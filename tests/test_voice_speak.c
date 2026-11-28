#include "test_framework.h"
#include "voice_speak.h"
#include "markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Speak text and compare with `want`; frees the result. */
static int speak_eq(const char *md, const char *want)
{
    char *t = voice_speak_text(md);
    int ok = t && strcmp(t, want) == 0;
    free(t);
    return ok;
}

int test_vspeak_strips_markdown(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("# Title\n\nSome **bold** and *italic* and `code` text.\n",
                         "Title. Some bold and italic and code text."));
    ASSERT_TRUE(speak_eq("- one\n- two.\n1. three\n> quoted words\n",
                         "one. two. three. quoted words"));
    ASSERT_TRUE(speak_eq("snake_case stays", "snake_case stays"));
    TEST_END();
}

int test_vspeak_code_block_once(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("Before.\n```c\nint x;\nint y;\n```\nBetween.\n```\nls\n```\nAfter.",
                         "Before. The code is on screen. Between. The code is on screen. After."));
    /* unterminated fence runs to the end */
    ASSERT_TRUE(speak_eq("Run:\n```\nls\nstill code", "Run: The code is on screen."));
    /* indented block, once */
    ASSERT_TRUE(speak_eq("Try this:\n\n    ls -l\n    pwd\n\nDone.",
                         "Try this: The code is on screen. Done."));
    TEST_END();
}

int test_vspeak_table_once(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("Disks:\n| a | b |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\nEnd.",
                         "Disks: The table is on screen. End."));
    TEST_END();
}

int test_vspeak_link_text_no_url(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("See [the docs](https://x) now.", "See the docs now."));
    ASSERT_TRUE(speak_eq("Go to https://example.com/a?b=1 today.", "Go to today."));
    ASSERT_TRUE(speak_eq("Or http://x.org.", "Or"));
    TEST_END();
}

int test_vspeak_thinking_and_exec_removed(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("<think>hmm\nsecret</think>Hello there.", "Hello there."));
    ASSERT_TRUE(speak_eq("Running [EXEC]ls -l[/EXEC] now.", "Running now."));
    TEST_END();
}

int test_vspeak_only_code(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("```\nrm -rf x\n```", "The code is on screen."));
    TEST_END();
}

int test_vspeak_empty(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("", ""));
    ASSERT_TRUE(speak_eq("  \n\t \r\n ", ""));
    ASSERT_TRUE(speak_eq("<think>x</think>", ""));
    TEST_END();
}

int test_vspeak_chunks_first_200(void)
{
    TEST_BEGIN();
    char text[4000];
    text[0] = '\0';
    for (int i = 0; i < 60; i++) strcat(text, "This is a sentence of moderate length. ");
    size_t st[64], ln[64];
    size_t n = voice_speak_chunks(text, 0, st, ln, 64);
    ASSERT_TRUE(n >= 3);
    ASSERT_TRUE(ln[0] <= VOICE_SPEAK_FIRST_CHUNK);
    ASSERT_TRUE(ln[0] > 150);
    ASSERT_EQ(text[st[0] + ln[0] - 1], '.');
    for (size_t i = 1; i < n; i++) {
        ASSERT_TRUE(ln[i] <= VOICE_SPEAK_CHUNK_MAX);
        ASSERT_EQ(text[st[i] + ln[i] - 1], '.');
    }
    ASSERT_TRUE(ln[1] > 900);
    TEST_END();
}

int test_vspeak_chunk_from_offset(void)
{
    TEST_BEGIN();
    const char *text = "One. Two. Three. Four.";
    size_t st[8], ln[8];
    size_t n = voice_speak_chunks(text, 5, st, ln, 8);
    ASSERT_EQ(n, 1u);
    ASSERT_EQ(st[0], 5u);
    ASSERT_EQ(ln[0], strlen(text) - 5);
    ASSERT_EQ(voice_speak_chunks(text, strlen(text), st, ln, 8), 0u);
    /* a leading space at from is skipped */
    n = voice_speak_chunks(text, 4, st, ln, 8);
    ASSERT_EQ(st[0], 5u);
    TEST_END();
}

int test_vspeak_decimal_not_sentence_end(void)
{
    TEST_BEGIN();
    const char *text = "It is 1.5 GB free. Next.";
    size_t st[4], ln[4];
    /* a limit smaller than the text would split at sentence ends only */
    size_t n = voice_speak_chunks(text, 0, st, ln, 4);
    ASSERT_EQ(n, 1u);
    char long_text[1400];
    strcpy(long_text, "Version 2.5 is out and it is fine.");
    while (strlen(long_text) < 260) strcat(long_text, " Version 2.5 is out and it is fine.");
    n = voice_speak_chunks(long_text, 0, st, ln, 4);
    ASSERT_TRUE(n >= 2);
    ASSERT_TRUE(memcmp(long_text + st[0] + ln[0] - 8, "is fine.", 8) == 0);
    TEST_END();
}

int test_vspeak_question(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_speak_ends_with_question("Shall I delete them?"), 1);
    ASSERT_EQ(voice_speak_ends_with_question("Shall I delete them?  \n"), 1);
    ASSERT_EQ(voice_speak_ends_with_question("Done."), 0);
    ASSERT_EQ(voice_speak_ends_with_question(""), 0);
    ASSERT_EQ(voice_speak_ends_with_question(NULL), 0);
    TEST_END();
}

static int on_boundary(const char *t, size_t i)
{
    return ((unsigned char)t[i] & 0xC0) != 0x80;
}

int test_vspeak_utf8_boundaries(void)
{
    TEST_BEGIN();
    /* no spaces: forces the split-at-limit path through multibyte chars */
    char text[3000];
    text[0] = '\0';
    const char *units[] = { "\xC3\xA9", "\xE2\x80\x9C", "\xF0\x9F\x98\x80", "\xC2\xA0", "a" };
    size_t o = 0;
    for (int i = 0; i < 700; i++) {
        const char *u = units[i % 5];
        size_t l = strlen(u);
        memcpy(text + o, u, l);
        o += l;
    }
    text[o] = '\0';
    size_t st[16], ln[16];
    size_t n = voice_speak_chunks(text, 0, st, ln, 16);
    ASSERT_TRUE(n >= 2);
    size_t covered = 0;
    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(on_boundary(text, st[i]));
        ASSERT_TRUE(on_boundary(text, st[i] + ln[i]));
        ASSERT_TRUE(ln[i] <= (i == 0 ? VOICE_SPEAK_FIRST_CHUNK : VOICE_SPEAK_CHUNK_MAX));
        covered += ln[i];
    }
    ASSERT_EQ(covered, o);
    /* the speakable text keeps the characters whole; NBSP becomes a space */
    char *t = voice_speak_text("caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D\xC2\xA0ok \xF0\x9F\x98\x80");
    ASSERT_NOT_NULL(t);
    ASSERT_STR_EQ(t, "caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D ok \xF0\x9F\x98\x80");
    free(t);
    TEST_END();
}

int test_vspeak_long_reply(void)
{
    TEST_BEGIN();
    size_t cap = 70000;
    char *md = (char *)malloc(cap);
    ASSERT_NOT_NULL(md);
    size_t o = 0;
    while (o < 60000) {
        o += (size_t)snprintf(md + o, cap - o,
            "Here is **some** prose with a [link](http://a.b/c) and `code`. It goes on [[[ and on.\n"
            "- an item without a stop\n```\nint main(void) { return 0; }\n```\n"
            "| a | b |\n|---|---|\n| 1 | 2 |\n\n    indented code\n\n");
    }
    char *t = voice_speak_text(md);
    ASSERT_NOT_NULL(t);
    size_t tl = strlen(t);
    ASSERT_TRUE(tl > 1000);
    size_t *st = (size_t *)malloc(sizeof(size_t) * 4096);
    size_t *ln = (size_t *)malloc(sizeof(size_t) * 4096);
    size_t n = voice_speak_chunks(t, 0, st, ln, 4096);
    ASSERT_TRUE(n > 10);
    size_t covered = 0;
    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(ln[i] <= VOICE_SPEAK_CHUNK_MAX);
        covered += ln[i];
    }
    ASSERT_TRUE(covered <= tl);
    free(st); free(ln); free(t); free(md);

    /* Time must grow linearly with the reply. Speak 40 KB and 640 KB of the
     * same prose (16x) and compare CPU time: linear gives about 16x, a
     * quadratic pass about 256x. The bound of 60x (and a 1 ms floor on the
     * small run, so timer noise cannot fail it) tells them apart. Best of
     * three runs each. */
    static const size_t sizes[2] = { 40000u, 640000u };
    double best[2] = { 0.0, 0.0 };
    for (int k = 0; k < 2; k++) {
        size_t mcap = sizes[k] + 512u;
        char *big = (char *)malloc(mcap);
        ASSERT_NOT_NULL(big);
        size_t bo = 0;
        while (bo < sizes[k]) {
            bo += (size_t)snprintf(big + bo, mcap - bo,
                "Here is **some** prose with a [link](http://a.b/c) and `code`. It goes on [[[ and on.\n"
                "- an item without a stop\n```\nint main(void) { return 0; }\n```\n"
                "| a | b |\n|---|---|\n| 1 | 2 |\n\n    indented code\n\n");
        }
        size_t ccap = bo / 4u + 64u;
        size_t *cs = (size_t *)malloc(sizeof(size_t) * ccap);
        size_t *cl = (size_t *)malloc(sizeof(size_t) * ccap);
        ASSERT_NOT_NULL(cs);
        ASSERT_NOT_NULL(cl);
        for (int rep = 0; rep < 3; rep++) {
            clock_t c0 = clock();
            char *tt = voice_speak_text(big);
            ASSERT_NOT_NULL(tt);
            size_t cn = voice_speak_chunks(tt, 0, cs, cl, ccap);
            ASSERT_TRUE(cn > 0);
            double dt = (double)(clock() - c0) / (double)CLOCKS_PER_SEC;
            free(tt);
            if (rep == 0 || dt < best[k]) best[k] = dt;
        }
        free(cs); free(cl); free(big);
    }
    double base = best[0] < 0.001 ? 0.001 : best[0];
    printf("  [vspeak] 40 KB %.4f s, 640 KB %.4f s\n", best[0], best[1]);
    ASSERT_TRUE(best[1] < 60.0 * base);
    TEST_END();
}

int test_vspeak_indented_fence(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("Run:\n  ```bash\n  ls\n    deeper\n  ```\nDone.",
                         "Run: The code is on screen. Done."));
    TEST_END();
}

/* A fence indented four or more spaces (or a tab) under a list item, with no blank line
 * above it, is still a fence: the code is not read aloud. */
int test_vspeak_deep_indented_fence(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("1. Run this:\n    ```bash\n    sudo rm -rf /x\n    ```\nDone.",
                         "Run this: The code is on screen. Done."));
    ASSERT_TRUE(speak_eq("Run:\n\t```sh\n\tsudo reboot\n\t```\nDone.",
                         "Run: The code is on screen. Done."));
    TEST_END();
}

/* A fence that opens on the list marker's own line. */
int test_vspeak_fence_after_list_marker(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("- ```bash\n  sudo rm -rf /x\n  ```\nDone.",
                         "The code is on screen. Done."));
    ASSERT_TRUE(speak_eq("1. ```\n   secret\n   ```\nDone.", "The code is on screen. Done."));
    TEST_END();
}

int test_vspeak_tilde_fence(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("A.\n~~~\nrm -rf x\n```\n~~~\nB.", "A. The code is on screen. B."));
    /* a different-char fence does not close a tilde fence; a backtick fence is closed, as on screen,
     * by any column-0 line starting with three backticks */
    ASSERT_TRUE(speak_eq("~~~\nx\n```\ny\n~~~\nok.", "The code is on screen. ok."));
    ASSERT_TRUE(speak_eq("````\nx\n```\ny\n````\nok.", "The code is on screen. y The code is on screen."));
    TEST_END();
}

int test_vspeak_unclosed_fence(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("Look:\n```sh\nls\nmore\n\nstill code", "Look: The code is on screen."));
    TEST_END();
}

int test_vspeak_pipeless_table(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("Data:\na | b\n--|--\n1 | 2\n3 | 4\nEnd.", "Data: The table is on screen. End."));
    ASSERT_TRUE(speak_eq("a | b\n---\n1 | 2\n", "The table is on screen."));
    /* a rule under plain prose is not a table */
    ASSERT_TRUE(speak_eq("Just text\n---\nmore", "Just text more"));
    TEST_END();
}

int test_vspeak_fence_in_quote(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("> ```\n> ls\n> ```\nAfter.", "The code is on screen. After."));
    ASSERT_TRUE(speak_eq("> | a | b |\n> |---|---|\n> | 1 | 2 |\nEnd.", "The table is on screen. End."));
    TEST_END();
}

/* A line that starts with a one-line triple-backtick span (a backtick after the run, so no info
 * string) is inline code in a list item or an indented line, not a fence: the reply after it is
 * still read. A real fence is unchanged, and a tilde fence may hold a backtick in its info. */
int test_vspeak_one_line_triple_backtick_span_is_not_a_fence(void)
{
    TEST_BEGIN();
    char *r = voice_speak_text("- ```x``` is the span.\nThen more prose here.\n");
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r != NULL && strstr(r, "Then more prose here.") != NULL);
    ASSERT_TRUE(r != NULL && strstr(r, "The code is on screen") == NULL);
    free(r);
    r = voice_speak_text("Intro.\n    ```x``` indented.\nAfter it.\n");
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r != NULL && strstr(r, "After it.") != NULL);
    free(r);
    r = voice_speak_text("1. ```a`b``` item\nTail text.\n");
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r != NULL && strstr(r, "Tail text.") != NULL);
    free(r);
    /* real fences still hide their code: with an info string, bare, in a list, tilde */
    ASSERT_TRUE(speak_eq("- ```bash\n  rm x\n  ```\nDone.", "The code is on screen. Done."));
    ASSERT_TRUE(speak_eq("```\nrm x\n```\nDone.", "The code is on screen. Done."));
    ASSERT_TRUE(speak_eq("~~~a`b\nrm x\n~~~\nDone.", "The code is on screen. Done."));
    TEST_END();
}

/* In column 0 the speech follows the chat renderer: any line starting with three backticks
 * toggles a code block, whatever follows the run. */
int test_vspeak_column0_fence_follows_renderer(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("```x``` y\nrm -rf build\n```\nDone, that is all.",
                         "The code is on screen. Done, that is all."));
    ASSERT_TRUE(speak_eq("```bash `cmd`\nSECRETCODE\n```\nAfter.", "The code is on screen. After."));
    /* a closing line with a trailing backtick closes on screen, so the prose after it is read */
    ASSERT_TRUE(speak_eq("```\nrm x\n``` `\nstill prose\n```\nDone.",
                         "The code is on screen. still prose The code is on screen."));
    TEST_END();
}

/* Pin the speech code to md_classify_line: a first line that the renderer calls a fence hides
 * the line after it; any other line leaves it spoken. */
int test_vspeak_column0_fence_agrees_with_classifier(void)
{
    TEST_BEGIN();
    static const char *lines[] = {
        "```", "```c", "```x``` y", "``` `", "``", "`` `", "` ``` `", "text ```", "````", "```\t", NULL
    };
    for (int i = 0; lines[i]; i++) {
        int fence = md_classify_line(lines[i], 0).type == MD_LINE_CODE_FENCE;
        char in[64];
        snprintf(in, sizeof in, "%s\nSECRETLINE", lines[i]);
        char *r = voice_speak_text(in);
        ASSERT_NOT_NULL(r);
        int hidden = r && strstr(r, "SECRETLINE") == NULL;
        ASSERT_EQ(hidden, fence);
        free(r);
    }
    TEST_END();
}

int test_vspeak_unterminated_think(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("Hello. <think>secret plan", "Hello."));
    TEST_END();
}

int test_vspeak_unterminated_exec(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("Running. [EXEC]rm -rf /", "Running."));
    TEST_END();
}

int test_vspeak_minor_links_and_questions(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(speak_eq("See [x](https://en.wikipedia.org/wiki/X_(y)) now.", "See x now."));
    ASSERT_TRUE(speak_eq("Visit www.example.com today.", "Visit today."));
    ASSERT_EQ(voice_speak_ends_with_question("Really?\xE2\x80\x9D"), 1);
    ASSERT_EQ(voice_speak_ends_with_question("Really?\xE2\x80\x99" " "), 1);
    TEST_END();
}
