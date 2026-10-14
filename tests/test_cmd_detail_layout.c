#include "test_framework.h"
#include "cmd_detail_layout.h"
#include "ns_scale.h"
#include "ns_type.h"

/* ===========================================================================
 * cmd_detail_layout -- the command details popup's geometry (spec
 * 2026-10-13-command-detail-and-copy-design.md section 2).
 * ===========================================================================
 */

static int cd_overlap(NsRect a, NsRect b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return 0;
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

static int cd_inside(NsRect r, int cw, int ch)
{
    return r.x >= 0 && r.y >= 0 && r.x + r.w <= cw && r.y + r.h <= ch;
}

static int cd_nonneg(NsRect r)
{
    return r.x >= 0 && r.y >= 0 && r.w >= 0 && r.h >= 0;
}

static int cd_all_nonneg(const CmdDetailLayout *l)
{
    return cd_nonneg(l->label) && cd_nonneg(l->note) && cd_nonneg(l->tag) &&
           cd_nonneg(l->command) && cd_nonneg(l->copy_btn) &&
           cd_nonneg(l->close_btn);
}

#define CD_W 544
#define CD_H 321

int test_cmd_detail_layout_all_rects_inside_client(void)
{
    TEST_BEGIN();
    CmdDetailLayout l;
    cmd_detail_layout(CD_W, CD_H, 96, 60, 64, &l);
    ASSERT_TRUE(cd_inside(l.label, CD_W, CD_H));
    ASSERT_TRUE(cd_inside(l.note, CD_W, CD_H));
    ASSERT_TRUE(cd_inside(l.tag, CD_W, CD_H));
    ASSERT_TRUE(cd_inside(l.command, CD_W, CD_H));
    ASSERT_TRUE(cd_inside(l.copy_btn, CD_W, CD_H));
    ASSERT_TRUE(cd_inside(l.close_btn, CD_W, CD_H));
    TEST_END();
}

int test_cmd_detail_layout_no_overlaps_and_vertical_order(void)
{
    TEST_BEGIN();
    CmdDetailLayout l;
    cmd_detail_layout(CD_W, CD_H, 96, 60, 64, &l);
    const NsRect *r[6] = { &l.label, &l.note, &l.tag, &l.command,
                           &l.copy_btn, &l.close_btn };
    for (int i = 0; i < 6; i++) {
        ASSERT_TRUE(r[i]->w > 0);
        ASSERT_TRUE(r[i]->h > 0);
        for (int j = i + 1; j < 6; j++) ASSERT_TRUE(!cd_overlap(*r[i], *r[j]));
    }
    ASSERT_TRUE(l.label.y + l.label.h <= l.note.y);
    ASSERT_TRUE(l.note.y + l.note.h <= l.tag.y);
    ASSERT_TRUE(l.tag.y + l.tag.h <= l.command.y);
    ASSERT_TRUE(l.command.y + l.command.h <= l.copy_btn.y);
    TEST_END();
}

int test_cmd_detail_layout_96_exact_positions(void)
{
    TEST_BEGIN();
    CmdDetailLayout l;
    cmd_detail_layout(CD_W, CD_H, 96, 60, 64, &l);
    /* SP_LG margin; caption line SZ_ICON; SP_XS to the note; SP_SM gaps. */
    ASSERT_EQ(l.label.x, SP_LG);
    ASSERT_EQ(l.label.y, SP_LG);
    ASSERT_EQ(l.label.w, CD_W - 2 * SP_LG);
    ASSERT_EQ(l.label.h, SZ_ICON);
    ASSERT_EQ(l.note.y, SP_LG + SZ_ICON + SP_XS);
    ASSERT_EQ(l.note.h, 60);
    ASSERT_EQ(l.tag.y, l.note.y + 60 + SP_SM);
    ASSERT_EQ(l.tag.w, 64);
    ASSERT_EQ(l.tag.h, SZ_TAG_H);
    ASSERT_EQ(l.command.y, l.tag.y + SZ_TAG_H + SP_SM);
    ASSERT_EQ(l.command.x, SP_LG);
    ASSERT_EQ(l.command.w, CD_W - 2 * SP_LG);
    /* Command box ends SP_SM above the button row. */
    ASSERT_EQ(l.command.y + l.command.h + SP_SM, l.copy_btn.y);
    TEST_END();
}

int test_cmd_detail_layout_command_box_shrinks_as_note_grows(void)
{
    TEST_BEGIN();
    CmdDetailLayout a, b, c;
    cmd_detail_layout(CD_W, CD_H, 96, 20, 64, &a);
    cmd_detail_layout(CD_W, CD_H, 96, 60, 64, &b);
    cmd_detail_layout(CD_W, CD_H, 96, 100, 64, &c);
    ASSERT_TRUE(a.command.h > b.command.h);
    ASSERT_TRUE(b.command.h > c.command.h);
    ASSERT_EQ(a.command.h - b.command.h, 40);
    ASSERT_EQ(b.command.h - c.command.h, 40);
    /* The buttons do not move. */
    ASSERT_EQ(a.close_btn.y, c.close_btn.y);
    TEST_END();
}

int test_cmd_detail_layout_oversized_note_is_clipped_not_negative(void)
{
    TEST_BEGIN();
    CmdDetailLayout l;
    cmd_detail_layout(CD_W, CD_H, 96, 100000, 64, &l);
    ASSERT_EQ(l.command.h, 0);
    ASSERT_TRUE(l.note.h < CD_H);
    /* The tag and the command box still sit above the buttons. */
    ASSERT_TRUE(l.tag.y + l.tag.h <= l.copy_btn.y);
    ASSERT_TRUE(!cd_overlap(l.note, l.tag));
    ASSERT_TRUE(cd_inside(l.note, CD_W, CD_H));
    TEST_END();
}

int test_cmd_detail_layout_buttons_right_aligned(void)
{
    TEST_BEGIN();
    CmdDetailLayout l;
    cmd_detail_layout(CD_W, CD_H, 96, 40, 64, &l);
    ASSERT_EQ(l.close_btn.w, SZ_BTN_MIN_W);
    ASSERT_EQ(l.close_btn.h, SZ_CTRL_H);
    ASSERT_EQ(l.copy_btn.w, SZ_BTN_MIN_W);
    ASSERT_EQ(l.copy_btn.h, SZ_CTRL_H);
    ASSERT_EQ(l.close_btn.x + l.close_btn.w, CD_W - SP_LG);
    ASSERT_EQ(l.copy_btn.x + l.copy_btn.w + SP_SM, l.close_btn.x);
    ASSERT_EQ(l.copy_btn.y, l.close_btn.y);
    ASSERT_EQ(l.close_btn.y + l.close_btn.h + SP_LG, CD_H);
    /* Right edges track a wider client. */
    CmdDetailLayout w;
    cmd_detail_layout(CD_W + 100, CD_H, 96, 40, 64, &w);
    ASSERT_EQ(w.close_btn.x - l.close_btn.x, 100);
    TEST_END();
}

int test_cmd_detail_layout_192_is_double_96(void)
{
    TEST_BEGIN();
    CmdDetailLayout a, b;
    cmd_detail_layout(400, 300, 96, 40, 64, &a);
    cmd_detail_layout(800, 600, 192, 80, 128, &b);
    const NsRect *ra[6] = { &a.label, &a.note, &a.tag, &a.command,
                            &a.copy_btn, &a.close_btn };
    const NsRect *rb[6] = { &b.label, &b.note, &b.tag, &b.command,
                            &b.copy_btn, &b.close_btn };
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(rb[i]->x, ra[i]->x * 2);
        ASSERT_EQ(rb[i]->y, ra[i]->y * 2);
        ASSERT_EQ(rb[i]->w, ra[i]->w * 2);
        ASSERT_EQ(rb[i]->h, ra[i]->h * 2);
    }
    TEST_END();
}

int test_cmd_detail_layout_dpi_le_zero_is_96(void)
{
    TEST_BEGIN();
    CmdDetailLayout a, z, n;
    cmd_detail_layout(CD_W, CD_H, 96, 40, 64, &a);
    cmd_detail_layout(CD_W, CD_H, 0, 40, 64, &z);
    cmd_detail_layout(CD_W, CD_H, -5, 40, 64, &n);
    ASSERT_EQ(z.command.h, a.command.h);
    ASSERT_EQ(n.close_btn.x, a.close_btn.x);
    TEST_END();
}

int test_cmd_detail_layout_tag_wider_than_content_is_clamped(void)
{
    TEST_BEGIN();
    CmdDetailLayout l;
    cmd_detail_layout(CD_W, CD_H, 96, 40, 100000, &l);
    ASSERT_EQ(l.tag.w, CD_W - 2 * SP_LG);
    ASSERT_TRUE(cd_inside(l.tag, CD_W, CD_H));
    TEST_END();
}

int test_cmd_detail_layout_never_negative_at_tiny_sizes(void)
{
    TEST_BEGIN();
    const int dpis[3] = { 96, 192, 0 };
    const int notes[4] = { 0, 20, 500, -7 };
    const int tags[3] = { 0, 64, 900 };
    for (int d = 0; d < 3; d++)
        for (int cw = -4; cw <= 80; cw += 4)
            for (int ch = -4; ch <= 200; ch += 4)
                for (int n = 0; n < 4; n++)
                    for (int t = 0; t < 3; t++) {
                        CmdDetailLayout l;
                        cmd_detail_layout(cw, ch, dpis[d], notes[n], tags[t], &l);
                        ASSERT_TRUE(cd_all_nonneg(&l));
                    }
    TEST_END();
}

int test_cmd_detail_layout_inside_client_for_all_adequate_sizes(void)
{
    TEST_BEGIN();
    for (int dpi = 96; dpi <= 192; dpi += 96) {
        for (int cw = 360 * dpi / 96; cw <= 900; cw += 37) {
            for (int ch = 300 * dpi / 96; ch <= 800; ch += 41) {
                CmdDetailLayout l;
                cmd_detail_layout(cw, ch, dpi, ns_scale(60, dpi),
                                  ns_scale(64, dpi), &l);
                ASSERT_TRUE(cd_inside(l.label, cw, ch));
                ASSERT_TRUE(cd_inside(l.note, cw, ch));
                ASSERT_TRUE(cd_inside(l.tag, cw, ch));
                ASSERT_TRUE(cd_inside(l.command, cw, ch));
                ASSERT_TRUE(cd_inside(l.copy_btn, cw, ch));
                ASSERT_TRUE(cd_inside(l.close_btn, cw, ch));
                ASSERT_TRUE(!cd_overlap(l.tag, l.command));
                ASSERT_TRUE(!cd_overlap(l.command, l.copy_btn));
                ASSERT_TRUE(!cd_overlap(l.command, l.close_btn));
                ASSERT_TRUE(!cd_overlap(l.copy_btn, l.close_btn));
            }
        }
    }
    TEST_END();
}

int test_cmd_detail_layout_null_out_is_safe(void)
{
    TEST_BEGIN();
    cmd_detail_layout(CD_W, CD_H, 96, 40, 64, NULL);
    ASSERT_TRUE(1);
    TEST_END();
}
