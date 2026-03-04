#include "test_framework.h"
#include "snap.h"

/* Shared constants for all tests */
#define TAB  32   /* tab-strip height */
#define NCW  16   /* non-client horizontal (border L+R) */
#define NCH  39   /* non-client vertical (title bar + borders) */
#define CW    8   /* character cell width */
#define CH   16   /* character cell height */

/* =========================================================================
 * snap_calc — positive tests
 * ========================================================================= */

/* Client area is already an exact multiple: window stays the same size. */
int test_snap_calc_exact_fit(void)
{
    TEST_BEGIN();
    /* 80 cols × 24 rows exactly */
    int cols, rows, win_w, win_h;
    snap_calc(80*CW, 24*CH + TAB, CW, CH, NCW, NCH, TAB,
              &cols, &rows, &win_w, &win_h);
    ASSERT_EQ(cols,  80);
    ASSERT_EQ(rows,  24);
    ASSERT_EQ(win_w, 80*CW + NCW);
    ASSERT_EQ(win_h, 24*CH + TAB + NCH);
    TEST_END();
}

/* Extra pixels on the right: cols floors down, gutter is absorbed. */
int test_snap_calc_right_gutter(void)
{
    TEST_BEGIN();
    int cols, rows, win_w, win_h;
    snap_calc(80*CW + 5, 24*CH + TAB, CW, CH, NCW, NCH, TAB,
              &cols, &rows, &win_w, &win_h);
    ASSERT_EQ(cols,  80);          /* 5 spare pixels dropped */
    ASSERT_EQ(rows,  24);
    ASSERT_EQ(win_w, 80*CW + NCW); /* snapped width is smaller */
    TEST_END();
}

/* Extra pixels at the bottom: rows floors down. */
int test_snap_calc_bottom_gutter(void)
{
    TEST_BEGIN();
    int cols, rows, win_w, win_h;
    snap_calc(80*CW, 24*CH + TAB + 7, CW, CH, NCW, NCH, TAB,
              &cols, &rows, &win_w, &win_h);
    ASSERT_EQ(rows,  24);
    ASSERT_EQ(win_h, 24*CH + TAB + NCH); /* 7 spare pixels dropped */
    TEST_END();
}

/* Gutter on both sides simultaneously. */
int test_snap_calc_both_gutters(void)
{
    TEST_BEGIN();
    int cols, rows, win_w, win_h;
    snap_calc(80*CW + 5, 24*CH + TAB + 7, CW, CH, NCW, NCH, TAB,
              &cols, &rows, &win_w, &win_h);
    ASSERT_EQ(cols,  80);
    ASSERT_EQ(rows,  24);
    ASSERT_EQ(win_w, 80*CW + NCW);
    ASSERT_EQ(win_h, 24*CH + TAB + NCH);
    TEST_END();
}

/* Non-power-of-two cell size (e.g. 11pt font → 9×18 px cells). */
int test_snap_calc_odd_cell_size(void)
{
    TEST_BEGIN();
    int cols, rows, win_w, win_h;
    /* 71 cols × 24 rows with 9-wide cells, with a 5-px right gutter */
    snap_calc(71*9 + 5, 24*18 + TAB + 3, 9, 18, NCW, NCH, TAB,
              &cols, &rows, &win_w, &win_h);
    ASSERT_EQ(cols,  71);
    ASSERT_EQ(rows,  24);
    ASSERT_EQ(win_w, 71*9 + NCW);
    ASSERT_EQ(win_h, 24*18 + TAB + NCH);
    TEST_END();
}

/* After zoom-in (larger cells) the snapped result has fewer cols/rows. */
int test_snap_calc_zoom_in(void)
{
    TEST_BEGIN();
    /* Window sized for 80×24 at 8×16; after zoom-in to 10×20 cells */
    int orig_client_w = 80 * CW;          /* 640 */
    int orig_client_h = 24 * CH + TAB;    /* 416 */
    int cols, rows;
    snap_calc(orig_client_w, orig_client_h, 10, 20, NCW, NCH, TAB,
              &cols, &rows, NULL, NULL);
    ASSERT_EQ(cols, 640 / 10);            /* 64 */
    ASSERT_EQ(rows, (416 - TAB) / 20);   /* 19 */
    TEST_END();
}

/* After zoom-out (smaller cells) the snapped result has more cols/rows. */
int test_snap_calc_zoom_out(void)
{
    TEST_BEGIN();
    int orig_client_w = 80 * CW;
    int orig_client_h = 24 * CH + TAB;
    int cols, rows;
    snap_calc(orig_client_w, orig_client_h, 6, 12, NCW, NCH, TAB,
              &cols, &rows, NULL, NULL);
    ASSERT_EQ(cols, 640 / 6);             /* 106 */
    ASSERT_EQ(rows, (416 - TAB) / 12);   /* 32 */
    TEST_END();
}

/* NULL output pointers must not crash. */
int test_snap_calc_null_outputs(void)
{
    TEST_BEGIN();
    /* Should not crash: all out-ptrs are NULL */
    snap_calc(640, 416, CW, CH, NCW, NCH, TAB, NULL, NULL, NULL, NULL);
    ASSERT_TRUE(1); /* reached without crash */
    TEST_END();
}

/* =========================================================================
 * snap_calc — negative / clamp tests
 * ========================================================================= */

/* client_w < char_w: cols clamps to 1. */
int test_snap_calc_min_cols_clamp(void)
{
    TEST_BEGIN();
    int cols, win_w;
    snap_calc(3, 24*CH + TAB, CW, CH, NCW, NCH, TAB,
              &cols, NULL, &win_w, NULL);
    ASSERT_EQ(cols,  1);
    ASSERT_EQ(win_w, 1*CW + NCW);
    TEST_END();
}

/* term_h < char_h: rows clamps to 1 (tiny window). */
int test_snap_calc_min_rows_clamp(void)
{
    TEST_BEGIN();
    /* client_h just 1 pixel taller than the tab strip */
    int rows, win_h;
    snap_calc(80*CW, TAB + 1, CW, CH, NCW, NCH, TAB,
              NULL, &rows, NULL, &win_h);
    ASSERT_EQ(rows,  1);
    ASSERT_EQ(win_h, 1*CH + TAB + NCH);
    TEST_END();
}

/* Negative term_h (window smaller than tab strip): clamps to one row. */
int test_snap_calc_negative_term_h(void)
{
    TEST_BEGIN();
    int rows;
    snap_calc(80*CW, TAB - 10, CW, CH, NCW, NCH, TAB,
              NULL, &rows, NULL, NULL);
    ASSERT_EQ(rows, 1);
    TEST_END();
}

/* Zero client_w: cols clamps to 1. */
int test_snap_calc_zero_client_w(void)
{
    TEST_BEGIN();
    int cols;
    snap_calc(0, 24*CH + TAB, CW, CH, NCW, NCH, TAB,
              &cols, NULL, NULL, NULL);
    ASSERT_EQ(cols, 1);
    TEST_END();
}

/* =========================================================================
 * snap_adjust — positive tests (right / bottom edges fixed by default)
 * ========================================================================= */

/* SNAP_EDGE_BOTTOMRIGHT: left and top are anchors; right and bottom move. */
int test_snap_adjust_bottom_right(void)
{
    TEST_BEGIN();
    int l=100, t=50, r=907, b=659;
    snap_adjust(&l, &t, &r, &b, 800, 600, SNAP_EDGE_BOTTOMRIGHT);
    ASSERT_EQ(l, 100);          /* left anchor unchanged */
    ASSERT_EQ(t, 50);           /* top anchor unchanged */
    ASSERT_EQ(r, 100 + 800);    /* right snapped */
    ASSERT_EQ(b, 50  + 600);    /* bottom snapped */
    TEST_END();
}

/* SNAP_EDGE_TOPLEFT: right and bottom are anchors; left and top move. */
int test_snap_adjust_top_left(void)
{
    TEST_BEGIN();
    int l=100, t=50, r=907, b=659;
    snap_adjust(&l, &t, &r, &b, 800, 600, SNAP_EDGE_TOPLEFT);
    ASSERT_EQ(r, 907);          /* right anchor unchanged */
    ASSERT_EQ(b, 659);          /* bottom anchor unchanged */
    ASSERT_EQ(l, 907 - 800);    /* left snapped */
    ASSERT_EQ(t, 659 - 600);    /* top snapped */
    TEST_END();
}

/* SNAP_EDGE_LEFT: right anchor; left moves. Bottom moves (default vertical). */
int test_snap_adjust_left(void)
{
    TEST_BEGIN();
    int l=100, t=50, r=907, b=659;
    snap_adjust(&l, &t, &r, &b, 800, 600, SNAP_EDGE_LEFT);
    ASSERT_EQ(r, 907);
    ASSERT_EQ(t, 50);
    ASSERT_EQ(l, 907 - 800);
    ASSERT_EQ(b, 50 + 600);
    TEST_END();
}

/* SNAP_EDGE_RIGHT: left anchor; right moves. */
int test_snap_adjust_right(void)
{
    TEST_BEGIN();
    int l=100, t=50, r=907, b=659;
    snap_adjust(&l, &t, &r, &b, 800, 600, SNAP_EDGE_RIGHT);
    ASSERT_EQ(l, 100);
    ASSERT_EQ(t, 50);
    ASSERT_EQ(r, 100 + 800);
    ASSERT_EQ(b, 50 + 600);
    TEST_END();
}

/* SNAP_EDGE_TOP: bottom anchor; top moves. Right moves (default horizontal). */
int test_snap_adjust_top(void)
{
    TEST_BEGIN();
    int l=100, t=50, r=907, b=659;
    snap_adjust(&l, &t, &r, &b, 800, 600, SNAP_EDGE_TOP);
    ASSERT_EQ(l, 100);
    ASSERT_EQ(b, 659);
    ASSERT_EQ(r, 100 + 800);
    ASSERT_EQ(t, 659 - 600);
    TEST_END();
}

/* SNAP_EDGE_BOTTOM: top anchor; bottom moves. */
int test_snap_adjust_bottom(void)
{
    TEST_BEGIN();
    int l=100, t=50, r=907, b=659;
    snap_adjust(&l, &t, &r, &b, 800, 600, SNAP_EDGE_BOTTOM);
    ASSERT_EQ(l, 100);
    ASSERT_EQ(t, 50);
    ASSERT_EQ(r, 100 + 800);
    ASSERT_EQ(b, 50 + 600);
    TEST_END();
}

/* SNAP_EDGE_TOPRIGHT: bottom anchor; top moves. Left anchor; right moves. */
int test_snap_adjust_top_right(void)
{
    TEST_BEGIN();
    int l=100, t=50, r=907, b=659;
    snap_adjust(&l, &t, &r, &b, 800, 600, SNAP_EDGE_TOPRIGHT);
    ASSERT_EQ(l, 100);
    ASSERT_EQ(b, 659);
    ASSERT_EQ(r, 100 + 800);
    ASSERT_EQ(t, 659 - 600);
    TEST_END();
}

/* SNAP_EDGE_BOTTOMLEFT: right anchor; left moves. Top anchor; bottom moves. */
int test_snap_adjust_bottom_left(void)
{
    TEST_BEGIN();
    int l=100, t=50, r=907, b=659;
    snap_adjust(&l, &t, &r, &b, 800, 600, SNAP_EDGE_BOTTOMLEFT);
    ASSERT_EQ(r, 907);
    ASSERT_EQ(t, 50);
    ASSERT_EQ(l, 907 - 800);
    ASSERT_EQ(b, 50 + 600);
    TEST_END();
}

/* =========================================================================
 * snap_adjust — already-snapped window: coordinates are unchanged.
 * ========================================================================= */

int test_snap_adjust_no_change_when_exact(void)
{
    TEST_BEGIN();
    /* Window is already perfectly sized; snap should produce the same rect. */
    int l=100, t=50, r=900, b=650;   /* 800×600 exactly */
    snap_adjust(&l, &t, &r, &b, 800, 600, SNAP_EDGE_BOTTOMRIGHT);
    ASSERT_EQ(l, 100);
    ASSERT_EQ(t, 50);
    ASSERT_EQ(r, 900);
    ASSERT_EQ(b, 650);
    TEST_END();
}

/* =========================================================================
 * Integration: snap_calc → snap_adjust round-trip
 * ========================================================================= */

/* Simulate a WM_SIZING BOTTOMRIGHT drag that needs snapping. */
int test_snap_roundtrip_bottomright(void)
{
    TEST_BEGIN();
    /* Proposed window size from the OS during drag */
    int l=0, t=0, r=80*CW + NCW + 5, b=24*CH + TAB + NCH + 7;
    int client_w = (r - l) - NCW;
    int client_h = (b - t) - NCH;

    int snapped_w, snapped_h;
    snap_calc(client_w, client_h, CW, CH, NCW, NCH, TAB,
              NULL, NULL, &snapped_w, &snapped_h);
    snap_adjust(&l, &t, &r, &b, snapped_w, snapped_h, SNAP_EDGE_BOTTOMRIGHT);

    /* Right and bottom should now exactly fit 80×24 cells */
    ASSERT_EQ(r, 80*CW + NCW);
    ASSERT_EQ(b, 24*CH + TAB + NCH);
    /* Left and top anchors are unchanged */
    ASSERT_EQ(l, 0);
    ASSERT_EQ(t, 0);
    TEST_END();
}

/* Same but dragging the TOPLEFT corner. */
int test_snap_roundtrip_topleft(void)
{
    TEST_BEGIN();
    int l=5, t=7, r=80*CW + NCW + 5, b=24*CH + TAB + NCH + 7;
    int client_w = (r - l) - NCW;
    int client_h = (b - t) - NCH;

    int snapped_w, snapped_h;
    snap_calc(client_w, client_h, CW, CH, NCW, NCH, TAB,
              NULL, NULL, &snapped_w, &snapped_h);
    snap_adjust(&l, &t, &r, &b, snapped_w, snapped_h, SNAP_EDGE_TOPLEFT);

    /* Right and bottom anchors unchanged; left and top adjusted */
    ASSERT_EQ(r, 80*CW + NCW + 5);
    ASSERT_EQ(b, 24*CH + TAB + NCH + 7);
    ASSERT_EQ(r - l, snapped_w);
    ASSERT_EQ(b - t, snapped_h);
    TEST_END();
}
