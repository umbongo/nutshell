/* tests/test_chat_copy.c -- the visible text of a selection (spec
 * 2026-10-18 section 2): chat_copy_visible() per line kind, the [EXEC]
 * segment walk, and the join across items. */
#include "test_framework.h"
#include "chat_copy.h"
#include "text_runs.h"
#include <stdlib.h>
#include <string.h>

#define MDX (CHAT_COPY_MD | CHAT_COPY_EXEC)

/* chat_copy_visible over [s, e) of t, compared with want. */
static int copy_is(const char *t, int s, int e, int flags, const char *want)
{
    char *out = NULL;
    int n = chat_copy_visible(t, (int)strlen(t), s, e, flags, &out);
    int ok = out && n == (int)strlen(want) && strcmp(out, want) == 0;
    if (!ok) printf("  got \"%s\" want \"%s\"\n", out ? out : "(null)", want);
    free(out);
    return ok;
}

/* Byte offset of the first occurrence of `needle` in t. */
static int at(const char *t, const char *needle)
{
    const char *p = strstr(t, needle);
    return p ? (int)(p - t) : -1;
}

static const char GOLD_SRC[] =
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
    "Done.";

static const char GOLD_COPY[] =
    "Disk Usage\n"
    "Intro with code and em.\n"
    "\n"
    "- one\n"
    "- two\n"
    "1. a\n"
    "2. b\n"
    "\n"
    "quote bold\n"
    "\n"
    "A\tB \\| C\n"
    "x\ty\n"
    "\n"
    "cd a && make\n"
    "Done.";

int test_chat_copy_whole_fixture_matches_golden(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(copy_is(GOLD_SRC, 0, (int)strlen(GOLD_SRC), MDX, GOLD_COPY));
    TEST_END();
}

int test_chat_copy_prose_inside_one_line_drops_markers(void)
{
    TEST_BEGIN();
    const char *t = "Intro with `code` and **bold** text";
    /* from "with" to the end of "bold": the backticks and ** never copy */
    ASSERT_TRUE(copy_is(t, at(t, "with"), at(t, "** text"), MDX,
                        "with code and bold"));
    /* starting on a marker byte copies from the next visible byte */
    ASSERT_TRUE(copy_is(t, at(t, "`code"), at(t, "` and"), MDX, "code"));
    TEST_END();
}

int test_chat_copy_across_line_break_and_empty_line(void)
{
    TEST_BEGIN();
    const char *t = "p1\n\np2";
    ASSERT_TRUE(copy_is(t, 0, 6, MDX, "p1\n\np2"));
    ASSERT_TRUE(copy_is(t, 1, 5, MDX, "1\n\np"));
    ASSERT_TRUE(copy_is(t, 0, 2, MDX, "p1"));       /* ends before the '\n' */
    ASSERT_TRUE(copy_is(t, 0, 3, MDX, "p1\n"));
    TEST_END();
}

int test_chat_copy_bullet_marker_follows_its_bytes(void)
{
    TEST_BEGIN();
    /* Review Focus 1: the "- " marker copies when the range covers one of
     * its bytes, not when the range starts at the line */
    const char *t = "a\n- b";
    ASSERT_TRUE(copy_is(t, 0, 2, MDX, "a\n"));      /* line 2 untouched */
    ASSERT_TRUE(copy_is(t, 0, 3, MDX, "a\n- "));    /* covers the marker */
    ASSERT_TRUE(copy_is(t, 3, 5, MDX, "- b"));      /* its last byte */
    ASSERT_TRUE(copy_is(t, 4, 5, MDX, "b"));        /* a word: no marker */
    ASSERT_TRUE(copy_is(t, 0, 5, MDX, "a\n- b"));
    TEST_END();
}

int test_chat_copy_ordered_list_copies_the_displayed_number(void)
{
    TEST_BEGIN();
    const char *t = "1. a\n1. b\n7. c";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "1. a\n2. b\n3. c"));
    /* item 2 alone, marker included, is numbered 2; a word in it alone
     * carries no number */
    ASSERT_TRUE(copy_is(t, at(t, "1. b"), at(t, "b") + 1, MDX, "2. b"));
    ASSERT_TRUE(copy_is(t, at(t, "b"), at(t, "b") + 1, MDX, "b"));
    /* a paragraph or blank line restarts the count, as the painter does */
    ASSERT_TRUE(copy_is("1. a\n\n1. b", 0, 10, MDX, "1. a\n\n1. b"));
    TEST_END();
}

int test_chat_copy_fence_lines_copy_nothing(void)
{
    TEST_BEGIN();
    const char *t = "x\n```\ny  z\n```\nw";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "x\ny  z\nw"));
    /* a range inside the code keeps its spaces verbatim */
    ASSERT_TRUE(copy_is(t, at(t, "  z"), at(t, "z") + 1, MDX, "  z"));
    TEST_END();
}

int test_chat_copy_heading_quote_and_rule(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(copy_is("## **Note**", 0, 11, MDX, "Note"));
    ASSERT_TRUE(copy_is("> said *so*", 0, 11, MDX, "said so"));
    ASSERT_TRUE(copy_is("a\n---\nb", 0, 7, MDX, "a\n\nb"));
    TEST_END();
}

int test_chat_copy_table_escaped_pipe_and_column_cap(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(copy_is("| a \\| b | c |", 0, 14, MDX, "a \\| b\tc"));
    /* 18 cells and no alignment row: the painter draws 16 */
    const char *wide =
        "|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|";
    ASSERT_TRUE(copy_is(wide, 0, (int)strlen(wide), MDX,
                        "1\t2\t3\t4\t5\t6\t7\t8\t9\t10\t11\t12\t13\t14\t15\t16"));
    TEST_END();
}

int test_chat_copy_table_columns_follow_the_alignment_row(void)
{
    TEST_BEGIN();
    /* Review Focus 5: two alignment cells -> the painter draws two columns,
     * header and body alike; a short body row draws an empty last cell. */
    const char *t = "| a | b | c |\n|---|---|\n| x | y | z |\n| q |";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "a\tb\nx\ty\nq\t"));
    TEST_END();
}

int test_chat_copy_table_range_from_the_end_of_a_short_row(void)
{
    TEST_BEGIN();
    /* A three-column table whose body row has one cell. A range that begins
     * at the end of that row reaches its missing cells' position but covers
     * none of them, so it must not open with a stray tab. */
    const char *t = "| a | b | c |\n|---|---|---|\n| only |\n| x | y | z |";
    int from = at(t, "| only |") + (int)strlen("| only |");
    char *out = NULL;
    int n = chat_copy_visible(t, (int)strlen(t), from, (int)strlen(t), MDX,
                              &out);
    ASSERT_NOT_NULL(out);
    if (out) {
        ASSERT_STR_EQ(out, "\nx\ty\tz");
        ASSERT_EQ(n, (int)strlen("\nx\ty\tz"));
    }
    free(out);
    TEST_END();
}

int test_chat_copy_exec_segment_and_state_reset(void)
{
    TEST_BEGIN();
    const char *t = "Run this:\n\n[EXEC]ls -la[/EXEC]\nDone.";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "Run this:\nls -la\n\nDone."));
    /* the fence opened before the command does not survive it: after the
     * segment the parser starts afresh, so the second ``` opens a new block */
    const char *r = "```bash\n[EXEC]ls[/EXEC]\n```\nDone.";
    ASSERT_TRUE(copy_is(r, 0, (int)strlen(r), MDX, "ls\n\nDone."));
    /* a range inside the command */
    ASSERT_TRUE(copy_is(t, at(t, "-la"), at(t, "[/EXEC]"), MDX, "-la"));
    TEST_END();
}

int test_chat_copy_ordered_list_restarts_after_exec(void)
{
    TEST_BEGIN();
    /* the [EXEC] segment reset restarts the list numbering */
    const char *t = "1. a\n[EXEC]x[/EXEC]\n1. b";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), MDX, "1. a\nx\n\n1. b"));
    TEST_END();
}

int test_chat_copy_leading_whitespace_is_not_copied(void)
{
    TEST_BEGIN();
    /* Review Focus 4: render_inline_spans drops whitespace at the start of
     * a line's content, so the copy does too (markdown only). */
    ASSERT_TRUE(copy_is("-  x\n   para **b**", 0, 18, MDX, "- x\npara b"));
    ASSERT_TRUE(copy_is("**  a**", 0, 7, MDX, "a"));
    ASSERT_TRUE(copy_is("   para", 0, 7, 0, "   para"));       /* plain keeps */
    TEST_END();
}

int test_chat_copy_plain_passes_bytes_through(void)
{
    TEST_BEGIN();
    const char *t = "**a**\n- b\n```";
    ASSERT_TRUE(copy_is(t, 0, (int)strlen(t), 0, t));
    /* markdown off on a reply: the [EXEC] split still applies */
    const char *r = "**x**\n[EXEC]ls[/EXEC]";
    ASSERT_TRUE(copy_is(r, 0, (int)strlen(r), CHAT_COPY_EXEC, "**x**\nls"));
    TEST_END();
}

int test_chat_copy_widens_a_cut_character(void)
{
    TEST_BEGIN();
    const char *t = "x\xC3\xA9y";        /* x e-acute y */
    ASSERT_TRUE(copy_is(t, 2, 3, 0, "\xC3\xA9"));
    ASSERT_TRUE(copy_is(t, 1, 2, 0, "\xC3\xA9"));
    ASSERT_TRUE(copy_is(t, 2, 4, MDX, "\xC3\xA9y"));
    TEST_END();
}

int test_chat_copy_empty_and_bad_arguments(void)
{
    TEST_BEGIN();
    char *out = (char *)1;
    int n = chat_copy_visible("abc", 3, 2, 2, MDX, &out);
    ASSERT_EQ(n, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "");
    free(out);
    n = chat_copy_visible(NULL, 3, 0, 3, MDX, &out);
    ASSERT_EQ(n, -1);
    ASSERT_NULL(out);
    n = chat_copy_visible("abc", 3, 0, 3, MDX, NULL);
    ASSERT_EQ(n, -1);
    /* a range wholly inside a fence line copies nothing */
    ASSERT_TRUE(copy_is("```\nx\n```", 0, 3, MDX, ""));
    TEST_END();
}

int test_chat_sel_cmp_and_part_range(void)
{
    TEST_BEGIN();
    ChatSelPos a = { 3, TR_PART_THINKING, 9 }, b = { 3, TR_PART_MAIN, 2 };
    ASSERT_TRUE(chat_sel_cmp(a, b) < 0);           /* Thinking reads first */
    ASSERT_TRUE(chat_sel_cmp(b, a) > 0);
    ChatSelPos c = { 4, TR_PART_THINKING, 0 };
    ASSERT_TRUE(chat_sel_cmp(b, c) < 0);
    ASSERT_EQ(chat_sel_cmp(a, a), 0);
    int s = -1, e = -1;
    int r = chat_sel_part_range(a, b, 3, TR_PART_THINKING, 20, &s, &e);
    ASSERT_TRUE(r);
    ASSERT_EQ(s, 9); ASSERT_EQ(e, 20);
    r = chat_sel_part_range(a, b, 3, TR_PART_MAIN, 50, &s, &e);
    ASSERT_TRUE(r);
    ASSERT_EQ(s, 0); ASSERT_EQ(e, 2);
    r = chat_sel_part_range(a, b, 4, TR_PART_MAIN, 50, &s, &e);
    ASSERT_FALSE(r);
    r = chat_sel_part_range(a, b, 2, TR_PART_MAIN, 50, &s, &e);
    ASSERT_FALSE(r);
    /* an end past the part's length is clamped */
    ChatSelPos far = { 3, TR_PART_MAIN, 999 };
    r = chat_sel_part_range(a, far, 3, TR_PART_MAIN, 50, &s, &e);
    ASSERT_TRUE(r);
    ASSERT_EQ(e, 50);
    TEST_END();
}

/* user, AI reply with Thinking, a command card, a status line */
static void build_list(ChatMsgList *l, int thinking_open)
{
    chat_msg_list_init(l);
    chat_msg_append(l, CHAT_ITEM_USER, "hello world");
    ChatMsgItem *ai = chat_msg_append(l, CHAT_ITEM_AI_TEXT,
                                      "**Hi** there\n[EXEC]ls[/EXEC]");
    chat_msg_set_thinking(ai, "think");
    ai->u.ai.thinking_collapsed = !thinking_open;
    ChatMsgItem *cmd = chat_msg_append(l, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(cmd, "rm -rf /tmp/x", CMD_WRITE, 0);
    chat_msg_append(l, CHAT_ITEM_STATUS, "done **raw**");
}

int test_chat_copy_selection_joins_items_and_skips_cards(void)
{
    TEST_BEGIN();
    ChatMsgList l;
    build_list(&l, 0);
    int u = l.head->id, s = l.tail->id;
    ChatSelPos a = { u, TR_PART_MAIN, 6 }, b = { s, TR_PART_MAIN, 6 };
    char *out = NULL;
    size_t n = chat_copy_selection(&l, b, a, 1, &out);   /* either order */
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "world\n\nHi there\nls\n\ndone *");
    ASSERT_EQ(n, strlen(out));
    free(out);
    /* markdown off: the reply's markers stay, but the [EXEC] split still
     * applies (the command's "ls" is copied without its markers) */
    out = NULL;
    n = chat_copy_selection(&l, a, b, 0, &out);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "world\n\n**Hi** there\nls\n\ndone *");
    ASSERT_EQ(n, strlen(out));
    free(out);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_copy_selection_takes_open_thinking_only(void)
{
    TEST_BEGIN();
    ChatMsgList l;
    build_list(&l, 1);
    int u = l.head->id, ai = l.head->next->id;
    ChatSelPos a = { u, TR_PART_MAIN, 6 }, b = { ai, TR_PART_MAIN, 2 };
    char *out = NULL;
    size_t n = chat_copy_selection(&l, a, b, 1, &out);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "world\n\nthink");   /* "**" copies nothing */
    ASSERT_EQ(n, strlen(out));
    free(out);
    out = NULL;
    l.head->next->u.ai.thinking_collapsed = 1;
    ChatSelPos b2 = { ai, TR_PART_MAIN, 6 };
    n = chat_copy_selection(&l, a, b2, 1, &out);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "world\n\nHi");
    ASSERT_EQ(n, strlen(out));
    free(out);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_copy_selection_clamps_and_drops_vanished_ids(void)
{
    TEST_BEGIN();
    /* Review Focus 2: a reply that was replaced by a shorter text keeps a
     * selection whose end now lies past its length -- clamp, never read
     * past the text; an id that no longer resolves copies nothing. */
    ChatMsgList l;
    build_list(&l, 0);
    ChatMsgItem *ai = l.head->next;
    ChatSelPos a = { ai->id, TR_PART_MAIN, 0 }, b = { ai->id, TR_PART_MAIN, 500 };
    char *out = NULL;
    chat_msg_set_text(ai, "ok");
    size_t n = chat_copy_selection(&l, a, b, 1, &out);
    ASSERT_EQ(n, 2);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "ok");
    free(out);
    out = NULL;
    ChatSelPos gone = { 9999, TR_PART_MAIN, 0 };
    n = chat_copy_selection(&l, a, gone, 1, &out);
    ASSERT_EQ(n, 0);
    ASSERT_NULL(out);
    n = chat_copy_selection(&l, a, a, 1, &out);   /* empty selection */
    ASSERT_EQ(n, 0);
    ASSERT_NULL(out);
    chat_msg_list_clear(&l);
    TEST_END();
}

int test_chat_sel_all_spans_text_items(void)
{
    TEST_BEGIN();
    ChatMsgList l;
    build_list(&l, 1);
    ChatSelPos a, b;
    int got = chat_sel_all(&l, &a, &b);
    ASSERT_TRUE(got);
    ASSERT_EQ(a.msg_id, l.head->id);
    ASSERT_EQ(a.part, TR_PART_MAIN);
    ASSERT_EQ(a.off, 0);
    ASSERT_EQ(b.msg_id, l.tail->id);          /* the status line, not the card */
    ASSERT_EQ(b.off, (int)strlen("done **raw**"));
    char *out = NULL;
    size_t n = chat_copy_selection(&l, a, b, 1, &out);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out,
        "hello world\n\nthink\n\nHi there\nls\n\ndone **raw**");
    ASSERT_EQ(n, strlen(out));
    free(out);
    chat_msg_list_clear(&l);
    ChatMsgList empty;
    chat_msg_list_init(&empty);
    got = chat_sel_all(&empty, &a, &b);
    ASSERT_FALSE(got);
    TEST_END();
}
