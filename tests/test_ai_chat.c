#include "test_framework.h"
#include <string.h>
#include <stdlib.h>

/*
 * Regression tests for the AI chat indicator removal bug.
 *
 * RichEdit stores line breaks as a single \r internally, but
 * GetWindowTextLength() returns sizes where \r\n counts as 2 chars.
 * EM_SETSEL uses the internal (1-char) positions.
 *
 * Bug: using GetWindowTextLength for indicator_pos causes the selection
 * to start too far into the text — leaving partial indicator text like
 * "(think" visible instead of removing the full "(thinking...)".
 *
 * Fix: use EM_EXGETSEL (which returns internal positions) instead.
 *
 * These tests simulate the mismatch in pure C so they run on Linux.
 */

/* Simulate RichEdit internal length: each \r\n pair counts as 1 char */
static int richedit_internal_len(const char *text)
{
    int len = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\r' && *(p + 1) == '\n') {
            len++;  /* \r\n = 1 internal char */
            p++;    /* skip the \n */
        } else {
            len++;
        }
    }
    return len;
}

/* Simulate GetWindowTextLength: counts raw bytes (each \r\n = 2 chars) */
static int getwindowtextlength(const char *text)
{
    return (int)strlen(text);
}

/* Simulate EM_SETSEL + EM_REPLACESEL: remove text from 'start' to 'end'
 * using internal (RichEdit) positions.  Returns the result string.
 * Caller must free. */
static char *richedit_remove(const char *text, int start, int end)
{
    /* Convert internal positions back to byte offsets */
    int byte_start = 0, byte_end = 0;
    int ipos = 0;
    const char *p = text;

    /* Find byte offset for 'start' */
    while (*p && ipos < start) {
        if (*p == '\r' && *(p + 1) == '\n') {
            byte_start += 2;
            p += 2;
        } else {
            byte_start++;
            p++;
        }
        ipos++;
    }

    /* Find byte offset for 'end' — clamp to text end */
    byte_end = byte_start;
    while (*p && ipos < end) {
        if (*p == '\r' && *(p + 1) == '\n') {
            byte_end += 2;
            p += 2;
        } else {
            byte_end++;
            p++;
        }
        ipos++;
    }
    /* Clamp: if end exceeds text, extend to real end */
    if (ipos < end)
        byte_end = (int)strlen(text);

    /* Build result: text[0..byte_start) + text[byte_end..] */
    int total = (int)strlen(text);
    int result_len = byte_start + (total - byte_end);
    char *result = (char *)malloc((size_t)result_len + 1);
    memcpy(result, text, (size_t)byte_start);
    memcpy(result + byte_start, text + byte_end,
           (size_t)(total - byte_end));
    result[result_len] = '\0';
    return result;
}

/* ---- The actual bug scenario ---- */

/*
 * Simulate the full chat flow:
 *   1. Display has multi-line text (intro + user message)
 *   2. Record indicator_pos
 *   3. Append "(thinking...)"
 *   4. On response, remove from indicator_pos to end
 *   5. Verify indicator is fully removed
 */

int test_indicator_removal_with_newlines(void) {
    TEST_BEGIN();

    /* Simulated RichEdit content before indicator is appended.
     * Uses \r\n like Win32 RichEdit returns via WM_GETTEXT. */
    const char *before =
        "AI Chat - Type a message.\r\n"
        "---\r\n"
        "\r\n"
        "--- You ---\r\n"
        "hi\r\n";

    /* The correct position (internal) for where indicator starts */
    int correct_pos = richedit_internal_len(before);

    /* The BUGGY position (GetWindowTextLength) */
    int buggy_pos = getwindowtextlength(before);

    /* Append the indicator */
    const char *indicator = "\r\n(thinking...)";
    size_t full_len = strlen(before) + strlen(indicator);
    char *full_text = (char *)malloc(full_len + 1);
    strcpy(full_text, before);
    strcat(full_text, indicator);

    int end_internal = richedit_internal_len(full_text);

    /* --- Test the FIXED approach (internal position) --- */
    char *after_fix = richedit_remove(full_text, correct_pos, end_internal);
    /* Should be exactly the text before the indicator */
    ASSERT_STR_EQ(after_fix, before);
    /* Must NOT contain any part of "thinking" */
    ASSERT_NULL(strstr(after_fix, "think"));
    ASSERT_NULL(strstr(after_fix, "("));
    free(after_fix);

    /* --- Test the BUGGY approach (GetWindowTextLength position) --- */
    char *after_bug = richedit_remove(full_text, buggy_pos, end_internal);
    /* The buggy version leaves residue because buggy_pos > correct_pos.
     * The discrepancy equals the number of \r\n pairs in 'before', so
     * that many chars from the indicator leak into the output. */
    ASSERT_TRUE(buggy_pos > correct_pos);
    /* Result should be LONGER than 'before' — residue left behind */
    ASSERT_TRUE(strlen(after_bug) > strlen(before));
    /* The leaked chars include the start of "(thinking..." */
    ASSERT_NOT_NULL(strstr(after_bug, "(th"));
    free(after_bug);

    free(full_text);
    TEST_END();
}

/* More newlines = bigger offset error */
int test_indicator_offset_grows_with_newlines(void) {
    TEST_BEGIN();

    /* With 10 \r\n pairs, the offset error should be exactly 10 */
    const char *text =
        "line1\r\n"
        "line2\r\n"
        "line3\r\n"
        "line4\r\n"
        "line5\r\n"
        "line6\r\n"
        "line7\r\n"
        "line8\r\n"
        "line9\r\n"
        "line10\r\n";

    int internal = richedit_internal_len(text);
    int external = getwindowtextlength(text);

    /* Each \r\n adds 1 to the discrepancy */
    ASSERT_EQ(external - internal, 10);

    TEST_END();
}

/* Continuing indicator has the same issue */
int test_continuing_indicator_removal(void) {
    TEST_BEGIN();

    const char *before =
        "AI Chat\r\n"
        "---\r\n"
        "--- You ---\r\n"
        "do something\r\n"
        "--- AI ---\r\n"
        "OK I'll help.\r\n"
        "--- Commands ---\r\n"
        "  $ ls\r\n";

    const char *indicator = "\r\n(continuing...)";
    size_t full_len = strlen(before) + strlen(indicator);
    char *full_text = (char *)malloc(full_len + 1);
    strcpy(full_text, before);
    strcat(full_text, indicator);

    int correct_pos = richedit_internal_len(before);
    int end_internal = richedit_internal_len(full_text);

    char *after = richedit_remove(full_text, correct_pos, end_internal);
    ASSERT_STR_EQ(after, before);
    ASSERT_NULL(strstr(after, "continu"));
    free(after);

    free(full_text);
    TEST_END();
}

/* No newlines = no discrepancy (sanity check) */
int test_indicator_no_newlines_no_discrepancy(void) {
    TEST_BEGIN();

    const char *before = "Hello world";
    int internal = richedit_internal_len(before);
    int external = getwindowtextlength(before);
    ASSERT_EQ(internal, external);

    TEST_END();
}

/* Edge case: indicator right after single newline */
int test_indicator_after_single_newline(void) {
    TEST_BEGIN();

    const char *before = "text\r\n";
    const char *indicator = "\r\n(thinking...)";

    size_t full_len = strlen(before) + strlen(indicator);
    char *full_text = (char *)malloc(full_len + 1);
    strcpy(full_text, before);
    strcat(full_text, indicator);

    int correct_pos = richedit_internal_len(before);
    int end_internal = richedit_internal_len(full_text);

    char *after = richedit_remove(full_text, correct_pos, end_internal);
    ASSERT_STR_EQ(after, before);
    ASSERT_NULL(strstr(after, "think"));
    free(after);

    /* With buggy pos: discrepancy of 1 */
    int buggy_pos = getwindowtextlength(before);
    ASSERT_EQ(buggy_pos - correct_pos, 1);
    char *after_bug = richedit_remove(full_text, buggy_pos, end_internal);
    /* One char of residue */
    ASSERT_TRUE(strlen(after_bug) > strlen(before));
    free(after_bug);

    free(full_text);
    TEST_END();
}

/* ---- UTF-8 display regression tests ----
 *
 * Bug: RichEdit20A interprets UTF-8 bytes as Windows-1252, so emoji
 * like ✅ (U+2705, UTF-8: E2 9C 85) appear as "âœ…" (three separate
 * Windows-1252 characters: â=0xE2, œ=0x9C, …=0x85).
 *
 * Fix: switched to RichEdit20W and convert UTF-8→UTF-16 via
 * MultiByteToWideChar before display.  The tests below verify the
 * data pipeline keeps UTF-8 intact up to the display boundary.
 */

/* Replicate the \n → \r\n conversion from format_ai_text() to verify
 * that multi-byte UTF-8 sequences are not broken by the byte-level
 * processing.  Only ASCII bytes (< 0x80) are acted on; continuation
 * bytes (0x80–0xBF) and lead bytes (0xC0–0xF4) pass through. */
static char *mock_format_ai_text(const char *raw)
{
    if (!raw) return NULL;
    size_t raw_len = strlen(raw);
    size_t alloc = raw_len * 2 + 64;
    char *out = (char *)malloc(alloc);
    if (!out) return NULL;

    size_t oi = 0;
    for (const char *p = raw; *p && oi + 4 < alloc; p++) {
        if (*p == '\n') {
            if (oi > 0 && out[oi - 1] != '\r')
                out[oi++] = '\r';
            out[oi++] = '\n';
        } else {
            out[oi++] = *p;
        }
    }
    out[oi] = '\0';
    return out;
}

int test_utf8_emoji_survives_format(void) {
    TEST_BEGIN();
    /* ✅ = U+2705 = UTF-8 bytes E2 9C 85 */
    const char *input = "1. \xe2\x9c\x85 Done\n2. \xe2\x9c\x85 Also done\n";
    char *formatted = mock_format_ai_text(input);
    ASSERT_NOT_NULL(formatted);

    /* The ✅ bytes must appear intact — not split or modified */
    ASSERT_NOT_NULL(strstr(formatted, "\xe2\x9c\x85 Done"));
    ASSERT_NOT_NULL(strstr(formatted, "\xe2\x9c\x85 Also done"));

    /* \n should become \r\n, but UTF-8 bytes must not be touched */
    ASSERT_NOT_NULL(strstr(formatted, "\r\n2."));
    ASSERT_EQ((int)strlen(formatted),
              (int)strlen(input) + 2); /* +2 for two \n→\r\n conversions */
    free(formatted);
    TEST_END();
}

int test_utf8_multibyte_not_split(void) {
    TEST_BEGIN();
    /* Test multiple multi-byte sequences:
     *   é (U+00E9) = C3 A9           (2-byte)
     *   → (U+2192) = E2 86 92        (3-byte)
     *   🔧 (U+1F527) = F0 9F 94 A7   (4-byte) */
    const char *input = "\xc3\xa9\n\xe2\x86\x92\n\xf0\x9f\x94\xa7";
    char *formatted = mock_format_ai_text(input);
    ASSERT_NOT_NULL(formatted);

    /* Each UTF-8 sequence must survive intact */
    ASSERT_NOT_NULL(strstr(formatted, "\xc3\xa9"));         /* é */
    ASSERT_NOT_NULL(strstr(formatted, "\xe2\x86\x92"));     /* → */
    ASSERT_NOT_NULL(strstr(formatted, "\xf0\x9f\x94\xa7")); /* 🔧 */

    free(formatted);
    TEST_END();
}

int test_utf8_ansi_misinterpretation_pattern(void) {
    TEST_BEGIN();
    /* Document the exact bug: when UTF-8 bytes for ✅ (E2 9C 85)
     * are interpreted as Windows-1252, they become three separate
     * characters: â (0xE2), œ (0x9C), … (0x85).
     *
     * This test verifies the bytes ARE a valid 3-byte UTF-8 sequence
     * and NOT three independent characters. */
    const char *checkmark = "\xe2\x9c\x85";

    /* Valid UTF-8: lead byte E2 = 1110xxxx (3-byte sequence) */
    ASSERT_TRUE(((unsigned char)checkmark[0] & 0xF0) == 0xE0);
    /* Continuation bytes: 10xxxxxx */
    ASSERT_TRUE(((unsigned char)checkmark[1] & 0xC0) == 0x80);
    ASSERT_TRUE(((unsigned char)checkmark[2] & 0xC0) == 0x80);
    /* Total: exactly 3 bytes for one codepoint */
    ASSERT_EQ((int)strlen(checkmark), 3);

    /* Verify format processing doesn't break the sequence */
    const char *input = "Step 1: \xe2\x9c\x85 Complete";
    char *formatted = mock_format_ai_text(input);
    ASSERT_NOT_NULL(formatted);
    /* The 3-byte sequence must remain contiguous */
    char *pos = strstr(formatted, "\xe2\x9c\x85");
    ASSERT_NOT_NULL(pos);
    ASSERT_EQ(pos[3], ' '); /* space after ✅, not a mangled byte */
    free(formatted);
    TEST_END();
}

int test_utf8_exec_block_preserves_multibyte(void) {
    TEST_BEGIN();
    /* Verify that [EXEC]...[/EXEC] replacement (which also processes
     * byte-by-byte) doesn't break UTF-8 inside commands.
     * Simulate the byte-copy loop from format_ai_text's EXEC handler. */
    const char *cmd_with_utf8 = "echo '\xc3\xa9l\xc3\xa8ve'";
    char out[256];
    size_t oi = 0;
    for (const char *c = cmd_with_utf8; *c && oi + 4 < sizeof(out); c++) {
        if (*c == '\n') {
            out[oi++] = '\r'; out[oi++] = '\n';
        } else {
            out[oi++] = *c;
        }
    }
    out[oi] = '\0';

    /* UTF-8 must survive: "élève" */
    ASSERT_STR_EQ(out, cmd_with_utf8); /* no \n, so identical */
    ASSERT_NOT_NULL(strstr(out, "\xc3\xa9"));  /* é */
    ASSERT_NOT_NULL(strstr(out, "\xc3\xa8"));  /* è */
    TEST_END();
}
