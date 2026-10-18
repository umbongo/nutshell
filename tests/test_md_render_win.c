/* tests/test_md_render_win.c -- make wintest: the renderer behind character
 * selection on a memory DC, no window needed (spec docs/superpowers/specs/
 * 2026-10-18-ai-panel-text-selection-design.md sections 3 and 6). The
 * measure pass and the paint pass give the same height; a paint with a run
 * context records runs whose boundaries are monotonic and whose bytes are
 * the words drawn; a code line is one run with its "&&" intact; a bullet is
 * a synthetic run over its marker; MD_MODE_PLAIN breaks a word wider than
 * the line. */

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"
#include "md_render.h"
#include "text_runs.h"
#include "ui_theme.h"

static const char MD_FIX[] =
    "## Disk **Usage**\n"
    "Here's what I found on **web-01** with `du`:\n"
    "\n"
    "- Root partition: 42% used\n"
    "1. first\n"
    "| Mount | Used |\n"
    "|---|--:|\n"
    "| / | 42% |\n"
    "```bash\n"
    "cd /var/log && ls -la\n"
    "```\n"
    "Gr\xC3\xBC\xC3\x9F" "e \xF0\x9F\x98\x80 done.";

static const char PLAIN_FIX[] =
    "  indented line kept\n"
    "averyveryveryverylongwordthatcannotfitonthenarrowline end";

static HFONT mk(int weight, const char *face)
{
    return CreateFontA(-13, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, face);
}

static int runs_sane(const TextRuns *r, const char *text, int len)
{
    int bad = 0;
    for (int i = 0; i < r->n; i++) {
        const TextRun *t = &r->v[i];
        if (t->start < 0 || t->end > len || t->start > t->end) bad++;
        for (int k = 1; k < t->n_cum; k++)
            if (r->cum[t->first_cum + k] < r->cum[t->first_cum + k - 1]) bad++;
        if (!t->synthetic[0] && t->end > t->start &&
            memchr(text + t->start, '\n', (size_t)(t->end - t->start)))
            bad++;
    }
    return bad;
}

int test_md_render_win_measure_equals_paint_and_runs(void)
{
    TEST_BEGIN();
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 800, 1200);
    HGDIOBJ oldb = SelectObject(dc, bmp);
    HFONT f = mk(FW_NORMAL, "Segoe UI"), m = mk(FW_NORMAL, "Consolas"),
          b = mk(FW_BOLD, "Segoe UI");
    const ThemeColors *th = ui_theme_get(0);

    for (int w = 160; w <= 640; w += 480) {
        TextRuns runs;
        text_runs_init(&runs);
        MdRunCtx rc;
        memset(&rc, 0, sizeof(rc));
        rc.runs = &runs;
        rc.part = TR_PART_MAIN;
        rc.sel_start = 0;
        rc.sel_end = (int)strlen(MD_FIX);
        int hm = md_render_ex(dc, MD_FIX, 0, 0, w, f, m, b, th,
                              MD_MODE_MARKDOWN, 0, 0, NULL, NULL);
        int hp = md_render_ex(dc, MD_FIX, 10, 20, w, f, m, b, th,
                              MD_MODE_MARKDOWN, 1, 0, NULL, &rc);
        ASSERT_EQ(hm, hp);
        ASSERT_TRUE(runs.n > 0);
        ASSERT_EQ(runs_sane(&runs, MD_FIX, (int)strlen(MD_FIX)), 0);
        int code = 0, bullet = 0;
        for (int i = 0; i < runs.n; i++) {
            const TextRun *t = &runs.v[i];
            if ((t->flags & TR_FLAG_CODE) &&
                memcmp(MD_FIX + t->start, "cd /var/log && ls -la", 21) == 0 &&
                t->end - t->start == 21)
                code = 1;
            if (strcmp(t->synthetic, "- ") == 0 &&
                memcmp(MD_FIX + t->start, "- ", 2) == 0 && t->end - t->start == 2)
                bullet = 1;
        }
        ASSERT_TRUE(code);
        ASSERT_TRUE(bullet);
        text_runs_free(&runs);

        TextRuns pr;
        text_runs_init(&pr);
        MdRunCtx prc;
        memset(&prc, 0, sizeof(prc));
        prc.runs = &pr;
        prc.part = TR_PART_MAIN;
        int pm = md_render_ex(dc, PLAIN_FIX, 0, 0, w, f, f, f, th,
                              MD_MODE_PLAIN, 0, 0, NULL, NULL);
        int pp = md_render_ex(dc, PLAIN_FIX, 0, 0, w, f, f, f, th,
                              MD_MODE_PLAIN, 1, 0, NULL, &prc);
        ASSERT_EQ(pm, pp);
        ASSERT_EQ(runs_sane(&pr, PLAIN_FIX, (int)strlen(PLAIN_FIX)), 0);
        /* the leading spaces are a run of their own, kept */
        ASSERT_TRUE(pr.n > 1);
        if (pr.n > 1) {
            const TextRun *lead = &pr.v[0];
            ASSERT_EQ(lead->start, 0);
            ASSERT_TRUE(lead->end >= 2);
            ASSERT_TRUE(memcmp(PLAIN_FIX + lead->start, "  ", 2) == 0);
            ASSERT_EQ(lead->x, 0);
            ASSERT_TRUE(lead->w > 0);
            /* the first word starts after the whitespace's width */
            ASSERT_TRUE(pr.v[1].x >= lead->x + lead->w);
        }
        /* no run is wider than the line */
        for (int i = 0; i < pr.n; i++) ASSERT_TRUE(pr.v[i].w <= w);
        text_runs_free(&pr);
    }

    SelectObject(dc, oldb);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    DeleteObject(f); DeleteObject(m); DeleteObject(b);
    TEST_END();
}

#endif /* _WIN32 */
