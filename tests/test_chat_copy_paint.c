/* tests/test_chat_copy_paint.c -- the two walkers must agree (spec
 * 2026-10-18 section 7): the runs a model of the paint pass records carry
 * the bytes of the words it drew (base-offset arithmetic at every level:
 * [EXEC] segment, line, content offset, table cell), and per source line
 * the painted words equal chat_copy_visible() over that line. */
#include "test_framework.h"
#include "fake_md_painter.h"
#include "chat_copy.h"
#include <stdlib.h>
#include <string.h>

static const char *const FIXTURES[] = {
    /* every line kind */
    "## Disk **Usage**\n"
    "Intro with `code` and *em*.\n"
    "\n"
    "- one\n"
    "- two\n"
    "1. a\n"
    "1. b\n"
    "\n"
    "> quote **bold**\n"
    "---\n"
    "| A | B \\| C |\n"
    "|---|---|\n"
    "| x | y |\n"
    "\n"
    "```sh\n"
    "cd a && make\n"
    "```\n"
    "Done.",
    /* [EXEC] segments between markdown, a heading, a list and a table */
    "Here:\n\n[EXEC]df -h /[/EXEC]\n## After **it**\n1. a\n1. b\n"
    "[EXEC]  indented cmd[/EXEC]\n- x `y` z\n| h1 | h2 |\n|:-|-:|\n"
    "| a \\| b | c |\n| only |\n\n> q *e*",
    /* the parser state resets across a segment */
    "```bash\n[EXEC]ls[/EXEC]\n```\nDone.",
    /* Review Focus 4: whitespace at the start of the content */
    "-  x\n   para **b**\n**  a**\n1.   n",
    /* more cells than the painter draws, a ragged block */
    "|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|\n| a | b | c |\n"
    "|---|---|\n| x | y | z |",
    /* multi-byte text */
    "Gr\xC3\xBC\xC3\x9F" "e **\xC3\xA9t\xC3\xA9**\n- \xC3\xBCn\xC3\xAF "
    "\xF0\x9F\x98\x80 done",
};
#define N_FIXTURES ((int)(sizeof(FIXTURES) / sizeof(FIXTURES[0])))

static FakePaint *paint(const char *t, int markdown)
{
    FakePaint *fp = (FakePaint *)calloc(1, sizeof(FakePaint));
    if (fp) fake_paint(t, (int)strlen(t), markdown, fp);
    return fp;
}

int test_paint_runs_carry_the_bytes_they_draw(void)
{
    TEST_BEGIN();
    for (int f = 0; f < N_FIXTURES; f++) {
        const char *t = FIXTURES[f];
        int len = (int)strlen(t);
        FakePaint *fp = paint(t, 1);
        ASSERT_NOT_NULL(fp);
        if (!fp) continue;
        ASSERT_EQ(fp->offset_errors, 0);
        for (int i = 0; i < fp->runs.n; i++) {
            const TextRun *r = &fp->runs.v[i];
            ASSERT_TRUE(r->start >= 0 && r->start <= r->end && r->end <= len);
            /* no run ever holds a marker byte sequence */
            if (!r->synthetic[0] && r->end > r->start) {
                ASSERT_TRUE(memchr(t + r->start, '\n',
                                   (size_t)(r->end - r->start)) == NULL);
            }
        }
        fake_paint_free(fp);
        free(fp);
    }
    TEST_END();
}

static int lines_agree(const char *t, int markdown)
{
    int ok = 1;
    int len = (int)strlen(t);
    int flags = CHAT_COPY_EXEC | (markdown ? CHAT_COPY_MD : 0);
    FakePaint *fp = paint(t, markdown);
    if (!fp) return 0;
    for (int i = 0; i < fp->n_lines; i++) {
        char painted[512];
        fake_line_text(fp, t, i, painted, (int)sizeof(painted));
        char *copied = NULL;
        chat_copy_visible(t, len, fp->lines[i].ls, fp->lines[i].le, flags,
                          &copied);
        if (!copied || strcmp(painted, copied) != 0) {
            printf("  line %d [%d,%d): painted \"%s\" copied \"%s\"\n", i,
                   fp->lines[i].ls, fp->lines[i].le, painted,
                   copied ? copied : "(null)");
            ok = 0;
        }
        free(copied);
    }
    fake_paint_free(fp);
    free(fp);
    return ok;
}

int test_painted_words_equal_copied_text(void)
{
    TEST_BEGIN();
    for (int f = 0; f < N_FIXTURES; f++) {
        int agree = lines_agree(FIXTURES[f], 1);
        ASSERT_TRUE(agree);
    }
    TEST_END();
}

int test_painted_words_equal_copied_text_markdown_off(void)
{
    TEST_BEGIN();
    for (int f = 0; f < N_FIXTURES; f++) {
        int agree = lines_agree(FIXTURES[f], 0);
        ASSERT_TRUE(agree);
    }
    TEST_END();
}

int test_paint_synthetic_runs_cover_their_marker(void)
{
    TEST_BEGIN();
    const char *t = "1. a\n1. b";
    FakePaint *fp = paint(t, 1);
    ASSERT_NOT_NULL(fp);
    if (fp) {
        ASSERT_STR_EQ(fp->runs.v[0].synthetic, "1. ");
        ASSERT_EQ(fp->runs.v[0].start, 0);
        ASSERT_EQ(fp->runs.v[0].end, 3);
        ASSERT_STR_EQ(fp->runs.v[2].synthetic, "2. ");
        ASSERT_EQ(fp->runs.v[2].start, 5);
        fake_paint_free(fp);
        free(fp);
    }
    TEST_END();
}
