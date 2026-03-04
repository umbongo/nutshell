/* test_scrollbar.c
 * Tests for the scrollbar position arithmetic used in window.c.
 *
 * update_scrollbar() and WM_VSCROLL are Win32-only and cannot be linked in
 * the test build.  The three pure arithmetic formulas that drive them are
 * tested here as inline helpers that mirror the window.c logic exactly.
 *
 * Formula 1 — nPos (thumb position, top-down):
 *   nPos = (total > rows) ? max(0, total - rows - off) : 0
 *
 * Formula 2 — max_off (maximum scrollback_offset):
 *   max_off = min(max(0, total - rows), max_scrollback)
 *
 * Formula 3 — off from thumb (WM_VSCROLL SB_THUMBTRACK reverse):
 *   off = total - rows - nTrackPos
 */

#include "test_framework.h"

/* ---- helpers ------------------------------------------------------------ */

static int sb_npos(int total, int rows, int off)
{
    int n = (total > rows) ? (total - rows - off) : 0;
    return n < 0 ? 0 : n;
}

static int sb_max_off(int total, int rows, int max_scrollback)
{
    int m = (total > rows) ? (total - rows) : 0;
    return m > max_scrollback ? max_scrollback : m;
}

static int sb_off_from_npos(int total, int rows, int npos)
{
    int off = total - rows - npos;
    return off < 0 ? 0 : off;
}

/* ---- nPos formula ------------------------------------------------------- */

/* At the live bottom (off=0), nPos equals total - rows */
int test_sb_npos_at_bottom(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_npos(100, 24, 0), 76);
    TEST_END();
}

/* Scrolling back N lines decreases nPos by N */
int test_sb_npos_scrolled_back(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_npos(100, 24, 10), 66);
    TEST_END();
}

/* Fully scrolled to top: off = total - rows => nPos = 0 */
int test_sb_npos_at_top(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_npos(100, 24, 76), 0);
    TEST_END();
}

/* When total <= rows there is nothing to scroll: nPos is always 0 */
int test_sb_npos_no_content(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_npos(24, 24, 0), 0);
    ASSERT_EQ(sb_npos(10, 24, 0), 0);
    ASSERT_EQ(sb_npos(1,  24, 0), 0);
    TEST_END();
}

/* Over-scrolled off is clamped: nPos never goes negative */
int test_sb_npos_clamp_negative(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_npos(100, 24, 80), 0);  /* 100 - 24 - 80 = -4, clamp to 0 */
    TEST_END();
}

/* ---- max_off formula ---------------------------------------------------- */

/* Normal case: max_off = total - rows (well within max_scrollback) */
int test_sb_max_off_normal(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_max_off(100, 24, 3000), 76);
    TEST_END();
}

/* max_off is capped by the max_scrollback setting */
int test_sb_max_off_capped(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_max_off(4000, 24, 3000), 3000);
    TEST_END();
}

/* When total <= rows there is no scrollback */
int test_sb_max_off_no_scrollback(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_max_off(24, 24, 3000), 0);
    ASSERT_EQ(sb_max_off(1,  24, 3000), 0);
    TEST_END();
}

/* max_off = 0 when max_scrollback = 0 */
int test_sb_max_off_zero_setting(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_max_off(100, 24, 0), 0);
    TEST_END();
}

/* ---- thumb → offset (WM_VSCROLL SB_THUMBTRACK) -------------------------- */

/* Thumb at the bottom of its range => off = 0 (live view) */
int test_sb_offset_from_npos_bottom(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_off_from_npos(100, 24, 76), 0);
    TEST_END();
}

/* Thumb at the top of its range => off = total - rows */
int test_sb_offset_from_npos_top(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_off_from_npos(100, 24, 0), 76);
    TEST_END();
}

/* Thumb in the middle */
int test_sb_offset_from_npos_mid(void)
{
    TEST_BEGIN();
    ASSERT_EQ(sb_off_from_npos(100, 24, 38), 38);
    TEST_END();
}

/* Round-trip: off -> nPos -> off must be identity */
int test_sb_roundtrip(void)
{
    TEST_BEGIN();
    int total = 200, rows = 40, off = 30;
    int npos = sb_npos(total, rows, off);           /* 200 - 40 - 30 = 130 */
    int recovered = sb_off_from_npos(total, rows, npos);
    ASSERT_EQ(recovered, off);
    TEST_END();
}

/* Win64 rationale: nPos can exceed 65535, which HIWORD(wParam) in WM_VSCROLL
 * would silently truncate to 16 bits.  GetScrollInfo(SIF_TRACKPOS) returns
 * the full 32-bit value, so the formula works correctly for any buffer depth. */
int test_sb_npos_exceeds_word(void)
{
    TEST_BEGIN();
    /* total=70000, rows=24, off=0  =>  nPos=69976  (0xFFFF = 65535, so > WORD) */
    ASSERT_EQ(sb_npos(70000, 24, 0), 69976);
    /* Round-trip via GetScrollInfo-style nTrackPos must be exact */
    ASSERT_EQ(sb_off_from_npos(70000, 24, 69976), 0);
    /* Mid-point above 65535 */
    ASSERT_EQ(sb_npos(70000, 24, 10000), 59976);
    ASSERT_EQ(sb_off_from_npos(70000, 24, 59976), 10000);
    TEST_END();
}

/* Round-trip at the extremes */
int test_sb_roundtrip_extremes(void)
{
    TEST_BEGIN();
    int total = 500, rows = 50;

    /* Bottom */
    int npos_bot = sb_npos(total, rows, 0);
    ASSERT_EQ(sb_off_from_npos(total, rows, npos_bot), 0);

    /* Top */
    int npos_top = sb_npos(total, rows, 450);
    ASSERT_EQ(sb_off_from_npos(total, rows, npos_top), 450);
    TEST_END();
}
