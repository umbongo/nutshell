/* tests/test_markdown_render.c
 *
 * Tests for the markdown parser (markdown.h) — edge cases not covered by
 * test_ai_chat.c.  md_render_text / md_measure_text require a Win32 HDC and
 * are compiled only for Windows, so rendering itself is verified visually;
 * only the portable parser is tested here.
 */

#include "test_framework.h"
#include "../src/ui/markdown.h"
#include <string.h>

/* ---- md_classify_line: edge cases ---- */

/* Heading with no trailing space — just "#" followed by end of string */
int test_md_classify_hash_only(void) {
    TEST_BEGIN();
    MdLineInfo info = md_classify_line("#", 0);
    /* "#" alone with no content after is still a heading (level=1, no content) */
    ASSERT_EQ(info.type, MD_LINE_HEADING);
    ASSERT_EQ(info.heading_level, 1);
    TEST_END();
}

/* Deep heading (#### = 4 hashes) should fall back to PARAGRAPH — parser only
 * handles up to H3. */
int test_md_classify_h4_is_paragraph(void) {
    TEST_BEGIN();
    MdLineInfo info = md_classify_line("#### Deep", 0);
    /* heading_level > 3 falls through as paragraph */
    ASSERT_EQ(info.type, MD_LINE_PARAGRAPH);
    TEST_END();
}

/* Inside code block, even markdown-looking lines must stay as MD_LINE_CODE */
int test_md_classify_inside_code_block(void) {
    TEST_BEGIN();
    /* These would normally be classified differently, but in_code_block = 1 */
    MdLineInfo info;
    info = md_classify_line("# looks like a heading", 1);
    ASSERT_EQ(info.type, MD_LINE_CODE);

    info = md_classify_line("---", 1);
    ASSERT_EQ(info.type, MD_LINE_CODE);

    info = md_classify_line("- list item", 1);
    ASSERT_EQ(info.type, MD_LINE_CODE);

    info = md_classify_line("> blockquote", 1);
    ASSERT_EQ(info.type, MD_LINE_CODE);
    TEST_END();
}

/* Closing code fence inside a code block is still CODE_FENCE */
int test_md_classify_closing_fence(void) {
    TEST_BEGIN();
    MdLineInfo info = md_classify_line("```", 1);
    ASSERT_EQ(info.type, MD_LINE_CODE_FENCE);
    TEST_END();
}

/* "---" is HRULE, "- text" is ULIST (disambiguation) */
int test_md_classify_dash_disambiguation(void) {
    TEST_BEGIN();
    MdLineInfo hr = md_classify_line("---", 0);
    ASSERT_EQ(hr.type, MD_LINE_HRULE);

    MdLineInfo ul = md_classify_line("- text", 0);
    ASSERT_EQ(ul.type, MD_LINE_ULIST);
    TEST_END();
}

/* "***" is HRULE, "* text" is ULIST */
int test_md_classify_star_disambiguation(void) {
    TEST_BEGIN();
    MdLineInfo hr = md_classify_line("***", 0);
    ASSERT_EQ(hr.type, MD_LINE_HRULE);

    MdLineInfo ul = md_classify_line("* text", 0);
    ASSERT_EQ(ul.type, MD_LINE_ULIST);
    TEST_END();
}

/* Ordered list with multi-digit number */
int test_md_classify_olist_multidigit(void) {
    TEST_BEGIN();
    MdLineInfo info = md_classify_line("99. item", 0);
    ASSERT_EQ(info.type, MD_LINE_OLIST);
    ASSERT_EQ(info.content_offset, 4); /* "99. " = 4 bytes */
    TEST_END();
}

/* Blockquote: ">" alone (no space, no text following) counts as blockquote */
int test_md_classify_blockquote_no_space(void) {
    TEST_BEGIN();
    /* "> " (with space) → blockquote with content_offset=2 */
    MdLineInfo info = md_classify_line("> ", 0);
    ASSERT_EQ(info.type, MD_LINE_BLOCKQUOTE);
    ASSERT_EQ(info.content_offset, 2);
    /* ">" alone (null terminator) → blockquote with content_offset=1 */
    info = md_classify_line(">", 0);
    ASSERT_EQ(info.type, MD_LINE_BLOCKQUOTE);
    ASSERT_EQ(info.content_offset, 1);
    /* ">text" (no space) → NOT a blockquote per this parser (falls through to paragraph) */
    info = md_classify_line(">text", 0);
    ASSERT_EQ(info.type, MD_LINE_PARAGRAPH);
    TEST_END();
}

/* Table line (starts with |) */
int test_md_classify_table_line(void) {
    TEST_BEGIN();
    MdLineInfo info = md_classify_line("| A | B | C |", 0);
    ASSERT_EQ(info.type, MD_LINE_TABLE);
    TEST_END();
}

/* Empty string → EMPTY */
int test_md_classify_empty_string(void) {
    TEST_BEGIN();
    MdLineInfo info = md_classify_line("", 0);
    ASSERT_EQ(info.type, MD_LINE_EMPTY);
    TEST_END();
}

/* NULL → EMPTY */
int test_md_classify_null_line(void) {
    TEST_BEGIN();
    MdLineInfo info = md_classify_line(NULL, 0);
    ASSERT_EQ(info.type, MD_LINE_EMPTY);
    TEST_END();
}

/* Plain text → PARAGRAPH */
int test_md_classify_plain_text(void) {
    TEST_BEGIN();
    MdLineInfo info = md_classify_line("Hello, world!", 0);
    ASSERT_EQ(info.type, MD_LINE_PARAGRAPH);
    TEST_END();
}

/* Text starting with a pipe inside code block → CODE, not TABLE */
int test_md_classify_table_in_code_block(void) {
    TEST_BEGIN();
    MdLineInfo info = md_classify_line("| col |", 1);
    ASSERT_EQ(info.type, MD_LINE_CODE);
    TEST_END();
}

/* ---- md_parse_inline: edge cases ---- */

/* NULL inputs return 0 */
int test_md_inline_null_inputs(void) {
    TEST_BEGIN();
    MdSpan spans[MD_MAX_SPANS];
    ASSERT_EQ(md_parse_inline(NULL, 10, spans), 0);
    ASSERT_EQ(md_parse_inline("text", 0, spans), 0);
    ASSERT_EQ(md_parse_inline("text", -1, spans), 0);
    ASSERT_EQ(md_parse_inline("text", 4, NULL), 0);
    TEST_END();
}

/* Backtick with no closing — treated as plain text */
int test_md_inline_unclosed_backtick(void) {
    TEST_BEGIN();
    MdSpan spans[MD_MAX_SPANS];
    const char *line = "hello `world";
    int n = md_parse_inline(line, (int)strlen(line), spans);
    /* Should produce at least one TEXT span covering the whole string */
    ASSERT_TRUE(n >= 1);
    /* No CODE spans should be emitted */
    int code_found = 0;
    for (int i = 0; i < n; i++) {
        if (spans[i].type == MD_SPAN_CODE) code_found = 1;
    }
    ASSERT_EQ(code_found, 0);
    TEST_END();
}

/* Strikethrough with no closing — treated as plain text (falls through) */
int test_md_inline_unclosed_strikethrough(void) {
    TEST_BEGIN();
    MdSpan spans[MD_MAX_SPANS];
    const char *line = "hello ~~world";
    int n = md_parse_inline(line, (int)strlen(line), spans);
    ASSERT_TRUE(n >= 1);
    int st_found = 0;
    for (int i = 0; i < n; i++) {
        if (spans[i].type == MD_SPAN_STRIKETHROUGH) st_found = 1;
    }
    ASSERT_EQ(st_found, 0);
    TEST_END();
}

/* Plain text with no markdown markers → single TEXT span */
int test_md_inline_no_markers(void) {
    TEST_BEGIN();
    MdSpan spans[MD_MAX_SPANS];
    const char *line = "Just plain text with no markers";
    int n = md_parse_inline(line, (int)strlen(line), spans);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(spans[0].type, MD_SPAN_TEXT);
    ASSERT_EQ(spans[0].start, 0);
    ASSERT_EQ(spans[0].end, (int)strlen(line));
    TEST_END();
}

/* Inline code: content offsets are correct */
int test_md_inline_code_offsets(void) {
    TEST_BEGIN();
    MdSpan spans[MD_MAX_SPANS];
    const char *line = "`hello`";
    int n = md_parse_inline(line, (int)strlen(line), spans);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(spans[0].type, MD_SPAN_CODE);
    /* start should skip the opening `, end should be before the closing ` */
    ASSERT_EQ(spans[0].start, 1);
    ASSERT_EQ(spans[0].end, 6);
    TEST_END();
}

/* Bold-italic (***text***) produces BOLD_ITALIC span */
int test_md_inline_bold_italic_span(void) {
    TEST_BEGIN();
    MdSpan spans[MD_MAX_SPANS];
    const char *line = "***bold-italic***";
    int n = md_parse_inline(line, (int)strlen(line), spans);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(spans[0].type, MD_SPAN_BOLD_ITALIC);
    /* Content is "bold-italic" = 11 chars; starts at offset 3 */
    ASSERT_EQ(spans[0].start, 3);
    ASSERT_EQ(spans[0].end, 14);
    TEST_END();
}

/* Strikethrough offsets: ~~text~~ → start=2, end before closing ~~ */
int test_md_inline_strikethrough_offsets(void) {
    TEST_BEGIN();
    MdSpan spans[MD_MAX_SPANS];
    const char *line = "~~struck~~";
    int n = md_parse_inline(line, (int)strlen(line), spans);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(spans[0].type, MD_SPAN_STRIKETHROUGH);
    ASSERT_EQ(spans[0].start, 2);
    ASSERT_EQ(spans[0].end, 8); /* "struck" = 6 chars starting at 2 → end=8 */
    TEST_END();
}

/* Mixed: text + bold + text in sequence */
int test_md_inline_text_bold_text(void) {
    TEST_BEGIN();
    MdSpan spans[MD_MAX_SPANS];
    const char *line = "before **bold** after";
    int n = md_parse_inline(line, (int)strlen(line), spans);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(spans[0].type, MD_SPAN_TEXT);   /* "before " */
    ASSERT_EQ(spans[1].type, MD_SPAN_BOLD);   /* "bold" */
    ASSERT_EQ(spans[2].type, MD_SPAN_TEXT);   /* " after" */
    TEST_END();
}

/* Italic span: correct content range */
int test_md_inline_italic_offsets(void) {
    TEST_BEGIN();
    MdSpan spans[MD_MAX_SPANS];
    const char *line = "*italic*";
    int n = md_parse_inline(line, (int)strlen(line), spans);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(spans[0].type, MD_SPAN_ITALIC);
    ASSERT_EQ(spans[0].start, 1);
    ASSERT_EQ(spans[0].end, 7); /* "italic" = 6 chars */
    TEST_END();
}

/* ---- md_is_table_separator: edge cases ---- */

/* Minimal separator */
int test_md_table_sep_minimal(void) {
    TEST_BEGIN();
    ASSERT_TRUE(md_is_table_separator("|---|"));
    TEST_END();
}

/* With alignment colons */
int test_md_table_sep_colons(void) {
    TEST_BEGIN();
    ASSERT_TRUE(md_is_table_separator("|:---|"));
    ASSERT_TRUE(md_is_table_separator("|---:|"));
    ASSERT_TRUE(md_is_table_separator("|:---:|"));
    TEST_END();
}

/* Not a separator (contains text) */
int test_md_table_sep_not_separator(void) {
    TEST_BEGIN();
    ASSERT_FALSE(md_is_table_separator("| text |"));
    ASSERT_FALSE(md_is_table_separator("| 123 |"));
    TEST_END();
}

/* NULL input → not a separator */
int test_md_table_sep_null(void) {
    TEST_BEGIN();
    ASSERT_FALSE(md_is_table_separator(NULL));
    TEST_END();
}
