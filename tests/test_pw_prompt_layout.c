/* Native tests for the password dialog's layout (src/core/pw_prompt_layout.c)
 * and the LF to CRLF helper for its text area. */
#include "test_framework.h"
#include "pw_prompt_layout.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

/* The formula as it stood in src/ui/pw_prompt.c before the move, spelled out
 * independently so a change to the moved code shows up here. */
static void expected_today(int dpi, int has_detail, int has_save, PwLayout *E)
{
    E->pad  = ns_scale(SP_MD, dpi);
    E->gap  = ns_scale(SP_SM, dpi);
    E->ctlh = ns_scale(SZ_CTRL_H, dpi);
    E->fw   = ns_scale(264, dpi);
    E->bw   = ns_scale(SZ_BTN_MIN_W, dpi);
    int y = E->pad;
    E->y_label = y;
    y += E->ctlh + E->gap;
    E->y_detail = y;
    if (has_detail) y += E->ctlh + E->gap;
    E->y_edit = y;
    y += E->ctlh;
    E->y_save = y + E->gap;
    y += has_save ? E->gap + E->ctlh + E->pad : E->pad;
    E->y_btn = y;
    E->client_h = y + E->ctlh + E->pad;
}

int test_pw_layout_today_unchanged(void)
{
    TEST_BEGIN();
    const int dpis[] = { 96, 192 };
    for (size_t d = 0; d < sizeof(dpis) / sizeof(dpis[0]); d++) {
        for (int det = 0; det <= 1; det++) {
            for (int sv = 0; sv <= 1; sv++) {
                PwLayout E, L;
                expected_today(dpis[d], det, sv, &E);
                pw_layout(dpis[d], det, 0, sv, &L);
                ASSERT_EQ(L.pad, E.pad);
                ASSERT_EQ(L.gap, E.gap);
                ASSERT_EQ(L.ctlh, E.ctlh);
                ASSERT_EQ(L.fw, E.fw);
                ASSERT_EQ(L.bw, E.bw);
                ASSERT_EQ(L.y_label, E.y_label);
                ASSERT_EQ(L.y_detail, E.y_detail);
                ASSERT_EQ(L.y_edit, E.y_edit);
                ASSERT_EQ(L.y_save, E.y_save);
                ASSERT_EQ(L.y_btn, E.y_btn);
                ASSERT_EQ(L.client_h, E.client_h);
                ASSERT_EQ(L.texth, 0);
                ASSERT_EQ(L.y_text, L.pad);
            }
        }
    }
    PwLayout L;
    pw_layout(96, 0, 0, 0, &L);
    ASSERT_EQ(L.client_h, 128);
    pw_layout(96, 1, 0, 1, &L);
    ASSERT_EQ(L.client_h, 128 + L.ctlh + L.gap + L.gap + L.ctlh);
    TEST_END();
}

int test_pw_layout_text_row(void)
{
    TEST_BEGIN();
    const int dpis[] = { 96, 192 };
    for (size_t d = 0; d < sizeof(dpis) / sizeof(dpis[0]); d++) {
        for (int det = 0; det <= 1; det++) {
            for (int sv = 0; sv <= 1; sv++) {
                PwLayout base, T;
                pw_layout(dpis[d], det, 0, sv, &base);
                pw_layout(dpis[d], det, 1, sv, &T);
                ASSERT_EQ(T.texth, 4 * T.ctlh);
                ASSERT_EQ(T.y_text, T.pad);
                ASSERT_EQ(T.y_label, T.pad + T.texth + T.gap);
                ASSERT_EQ(T.client_h, base.client_h + T.texth + T.gap);
                /* every row below the text area moves down by the same amount */
                int shift = T.texth + T.gap;
                ASSERT_EQ(T.y_detail, base.y_detail + shift);
                ASSERT_EQ(T.y_edit, base.y_edit + shift);
                ASSERT_EQ(T.y_save, base.y_save + shift);
                ASSERT_EQ(T.y_btn, base.y_btn + shift);
                /* widths do not depend on the rows */
                ASSERT_EQ(T.fw, base.fw);
                ASSERT_EQ(T.bw, base.bw);
            }
        }
    }
    TEST_END();
}

int test_pw_text_crlf(void)
{
    TEST_BEGIN();
    char out[32];
    size_t n;

    n = pw_text_crlf("a\nb", out, sizeof out);
    ASSERT_STR_EQ(out, "a\r\nb");
    ASSERT_EQ(n, 4);

    n = pw_text_crlf("a\r\nb", out, sizeof out);
    ASSERT_STR_EQ(out, "a\r\nb");
    ASSERT_EQ(n, 4);

    n = pw_text_crlf("\n\n", out, sizeof out);
    ASSERT_STR_EQ(out, "\r\n\r\n");
    ASSERT_EQ(n, 4);

    n = pw_text_crlf(NULL, out, sizeof out);
    ASSERT_STR_EQ(out, "");
    ASSERT_EQ(n, 0);

    n = pw_text_crlf("", out, sizeof out);
    ASSERT_STR_EQ(out, "");
    ASSERT_EQ(n, 0);

    /* cap 4 holds three characters and the NUL: "a\r\n" fits, "b" does not */
    n = pw_text_crlf("a\nb", out, 4);
    ASSERT_STR_EQ(out, "a\r\n");
    ASSERT_EQ(n, 3);

    /* cap 3: "a" fits, the pair "\r\n" would not, so no lone "\r" */
    n = pw_text_crlf("a\nb", out, 3);
    ASSERT_STR_EQ(out, "a");
    ASSERT_EQ(n, 1);

    /* cap 1 writes just the NUL; cap 0 writes nothing */
    memset(out, 'x', sizeof out);
    n = pw_text_crlf("abc", out, 1);
    ASSERT_EQ(out[0], '\0');
    ASSERT_EQ(n, 0);
    memset(out, 'x', sizeof out);
    n = pw_text_crlf("abc", out, 0);
    ASSERT_EQ(out[0], 'x');
    ASSERT_EQ(n, 0);

    /* a lone CR is kept as is */
    n = pw_text_crlf("a\rb", out, sizeof out);
    ASSERT_STR_EQ(out, "a\rb");
    ASSERT_EQ(n, 3);

    /* a trailing LF after text, and a CRLF followed by LF */
    n = pw_text_crlf("a\r\n\nb\n", out, sizeof out);
    ASSERT_STR_EQ(out, "a\r\n\r\nb\r\n");
    ASSERT_EQ(n, 8);
    TEST_END();
}
