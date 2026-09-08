/* tests/test_md_table.c — src/core/md_table.c: markdown table row
 * splitting, separator-row alignment parsing, and the column-fit
 * shrink-widest-first algorithm. Pure/portable, no Win32 dependency.
 */

#include "test_framework.h"
#include "../src/core/md_table.h"
#include <string.h>

/* ---- md_table_split_row ---- */

int test_md_table_split_basic(void)
{
    TEST_BEGIN();
    MdTableCell cells[8];
    int n = md_table_split_row("| a | b |", cells, 8);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(cells[0].len, 1);
    ASSERT_TRUE(strncmp(cells[0].start, "a", 1) == 0);
    ASSERT_EQ(cells[1].len, 1);
    ASSERT_TRUE(strncmp(cells[1].start, "b", 1) == 0);
    TEST_END();
}

int test_md_table_split_no_outer_pipes(void)
{
    TEST_BEGIN();
    MdTableCell cells[8];
    int n = md_table_split_row("a | b", cells, 8);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(cells[0].len, 1);
    ASSERT_TRUE(strncmp(cells[0].start, "a", 1) == 0);
    ASSERT_EQ(cells[1].len, 1);
    ASSERT_TRUE(strncmp(cells[1].start, "b", 1) == 0);
    TEST_END();
}

int test_md_table_split_escaped_pipe(void)
{
    TEST_BEGIN();
    MdTableCell cells[8];
    int n = md_table_split_row("| a \\| b | c |", cells, 8);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(cells[0].len, 6);
    ASSERT_TRUE(strncmp(cells[0].start, "a \\| b", 6) == 0);
    ASSERT_EQ(cells[1].len, 1);
    ASSERT_TRUE(strncmp(cells[1].start, "c", 1) == 0);
    TEST_END();
}

int test_md_table_split_empty_cells(void)
{
    TEST_BEGIN();
    MdTableCell cells[8];
    int n = md_table_split_row("| a |  | c |", cells, 8);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(cells[0].len, 1);
    ASSERT_EQ(cells[1].len, 0);
    ASSERT_EQ(cells[2].len, 1);
    TEST_END();
}

int test_md_table_split_ragged_row(void)
{
    TEST_BEGIN();
    /* A body row with fewer columns than a 3-column header -- caller pads
     * the missing trailing columns; split just reports what it found. */
    MdTableCell cells[8];
    int n = md_table_split_row("| only | two |", cells, 8);
    ASSERT_EQ(n, 2);
    TEST_END();
}

int test_md_table_split_max_cells_caps_output(void)
{
    TEST_BEGIN();
    MdTableCell cells[2];
    int n = md_table_split_row("| a | b | c | d |", cells, 2);
    ASSERT_EQ(n, 2);
    ASSERT_TRUE(strncmp(cells[0].start, "a", 1) == 0);
    ASSERT_TRUE(strncmp(cells[1].start, "b", 1) == 0);
    TEST_END();
}

int test_md_table_split_null_and_empty(void)
{
    TEST_BEGIN();
    MdTableCell cells[8];
    ASSERT_EQ(md_table_split_row(NULL, cells, 8), 0);
    ASSERT_EQ(md_table_split_row("| a |", NULL, 8), 0);
    ASSERT_EQ(md_table_split_row("| a |", cells, 0), 0);
    TEST_END();
}

int test_md_table_split_single_empty_cell(void)
{
    TEST_BEGIN();
    MdTableCell cells[8];
    int n = md_table_split_row("||", cells, 8);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(cells[0].len, 0);
    TEST_END();
}

/* ---- md_table_alignments ---- */

int test_md_table_alignments_mixed(void)
{
    TEST_BEGIN();
    int align[8];
    int n = md_table_alignments("|:--|:-:|--:|---|", align, 8);
    ASSERT_EQ(n, 4);
    ASSERT_EQ(align[0], MD_ALIGN_LEFT);
    ASSERT_EQ(align[1], MD_ALIGN_CENTER);
    ASSERT_EQ(align[2], MD_ALIGN_RIGHT);
    ASSERT_EQ(align[3], MD_ALIGN_LEFT);
    TEST_END();
}

int test_md_table_alignments_not_a_separator(void)
{
    TEST_BEGIN();
    int align[8];
    int n = md_table_alignments("| a | b |", align, 8);
    ASSERT_EQ(n, 0);
    TEST_END();
}

int test_md_table_alignments_null_safe(void)
{
    TEST_BEGIN();
    int align[8];
    ASSERT_EQ(md_table_alignments(NULL, align, 8), 0);
    ASSERT_EQ(md_table_alignments("|---|", NULL, 8), 0);
    ASSERT_EQ(md_table_alignments("|---|", align, 0), 0);
    TEST_END();
}

/* ---- md_table_fit_columns ---- */

int test_md_table_fit_columns_fits_unchanged(void)
{
    TEST_BEGIN();
    int natural[3] = { 40, 30, 20 };
    int minimum[3] = { 10, 10, 10 };
    int out[3];
    md_table_fit_columns(natural, minimum, 3, 100, out);
    ASSERT_EQ(out[0], 40);
    ASSERT_EQ(out[1], 30);
    ASSERT_EQ(out[2], 20);
    TEST_END();
}

int test_md_table_fit_columns_one_wide_shrinks(void)
{
    TEST_BEGIN();
    int natural[3] = { 100, 50, 50 };
    int minimum[3] = { 20, 20, 20 };
    int out[3];
    md_table_fit_columns(natural, minimum, 3, 150, out);
    int total = out[0] + out[1] + out[2];
    ASSERT_EQ(total, 150);
    ASSERT_EQ(out[1], 50);
    ASSERT_EQ(out[2], 50);
    ASSERT_EQ(out[0], 50);
    TEST_END();
}

int test_md_table_fit_columns_two_wide_shrink_equal(void)
{
    TEST_BEGIN();
    int natural[3] = { 100, 100, 30 };
    int minimum[3] = { 10, 10, 10 };
    int out[3];
    md_table_fit_columns(natural, minimum, 3, 170, out);
    int total = out[0] + out[1] + out[2];
    ASSERT_EQ(total, 170);
    ASSERT_EQ(out[0], out[1]);
    ASSERT_EQ(out[2], 30);
    TEST_END();
}

int test_md_table_fit_columns_minimums_respected(void)
{
    TEST_BEGIN();
    /* Column 0's own minimum (80) is higher than the other columns'
     * natural widths, so it must floor there even while shrinking. */
    int natural[3] = { 200, 60, 60 };
    int minimum[3] = { 80, 20, 20 };
    int out[3];
    md_table_fit_columns(natural, minimum, 3, 150, out);
    int total = out[0] + out[1] + out[2];
    ASSERT_EQ(total, 150);
    ASSERT_EQ(out[0], 80);
    ASSERT_EQ(out[1], out[2]);
    ASSERT_TRUE(out[1] >= minimum[1]);
    TEST_END();
}

int test_md_table_fit_columns_overflow(void)
{
    TEST_BEGIN();
    /* Even the minimums don't fit -- out == minimum verbatim, table
     * overflows and the caller clips. */
    int natural[3] = { 500, 400, 300 };
    int minimum[3] = { 200, 200, 200 };
    int out[3];
    md_table_fit_columns(natural, minimum, 3, 100, out);
    ASSERT_EQ(out[0], 200);
    ASSERT_EQ(out[1], 200);
    ASSERT_EQ(out[2], 200);
    TEST_END();
}

int test_md_table_fit_columns_null_safe(void)
{
    TEST_BEGIN();
    int out[3] = { -1, -1, -1 };
    md_table_fit_columns(NULL, NULL, 3, 100, out);
    ASSERT_EQ(out[0], -1);  /* untouched: no-op on bad input */
    md_table_fit_columns(NULL, NULL, 0, 100, out);
    ASSERT_EQ(out[0], -1);
    TEST_END();
}
