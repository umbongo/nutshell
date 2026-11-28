#include "test_framework.h"
#include "term_underline.h"

int test_term_underline_typical_cell(void) {
    TEST_BEGIN();
    int top, th;
    term_underline_geom(16, 13, 96, &top, &th);
    ASSERT_EQ(th, 1);
    ASSERT_EQ(top, 14);            /* one row into a 3 px descent */
    term_underline_geom(32, 26, 192, &top, &th);
    ASSERT_EQ(th, 2);              /* thickness follows the DPI */
    ASSERT_EQ(top, 28);
    TEST_END();
}

int test_term_underline_stays_inside_cell(void) {
    TEST_BEGIN();
    int dpis[] = {0, 96, 120, 144, 192, 288, 384};
    for (int h = 1; h <= 200; h++) {
        for (int a = -3; a <= h + 3; a++) {
            for (unsigned d = 0; d < sizeof dpis / sizeof dpis[0]; d++) {
                int top, th;
                term_underline_geom(h, a, dpis[d], &top, &th);
                ASSERT_TRUE(th >= 1);
                ASSERT_TRUE(top >= 0);
                ASSERT_TRUE(top + th <= h);
            }
        }
    }
    TEST_END();
}

int test_term_underline_degenerate_cells(void) {
    TEST_BEGIN();
    int top = 99, th = 99;
    term_underline_geom(1, 1, 96, &top, &th);     /* smallest cell */
    ASSERT_EQ(top, 0);
    ASSERT_EQ(th, 1);
    term_underline_geom(0, 0, 96, &top, &th);     /* no cell: nothing */
    ASSERT_EQ(top, 0);
    ASSERT_EQ(th, 0);
    term_underline_geom(-5, 3, 96, &top, &th);
    ASSERT_EQ(th, 0);
    term_underline_geom(10, 8, 96, NULL, NULL);   /* NULL outputs are fine */
    term_underline_geom(2, 2, 400, &top, &th);    /* thicker than the cell */
    ASSERT_EQ(top, 0);
    ASSERT_EQ(th, 2);
    TEST_END();
}
