#include "cheatsheet_layout.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

static int clamp0(int v) { return v < 0 ? 0 : v; }
static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

int cheatsheet_scroll_clamp(int offset, int page_h, int view_h)
{
    int max_off = page_h - view_h;
    if (max_off < 0) max_off = 0;
    if (offset < 0) return 0;
    return offset > max_off ? max_off : offset;
}

void cheatsheet_place(int owner_x, int owner_y, int owner_w, int owner_h,
                      int win_w, int win_h,
                      int work_x, int work_y, int work_w, int work_h,
                      int *out_x, int *out_y)
{
    int x = owner_x + (owner_w - win_w) / 2;
    int y = owner_y + (owner_h - win_h) / 2;
    if (x + win_w > work_x + work_w) x = work_x + work_w - win_w;
    if (y + win_h > work_y + work_h) y = work_y + work_h - win_h;
    if (x < work_x) x = work_x;
    if (y < work_y) y = work_y;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

void cheatsheet_layout_compute(CsLayout *out, const CsInput *in_raw)
{
    if (!out || !in_raw) return;
    memset(out, 0, sizeof(*out));

    CsInput in = *in_raw;
    int dpi = in.dpi > 0 ? in.dpi : 96;
    in.chord_w = clamp0(in.chord_w);
    in.text_w = clamp0(in.text_w);
    in.heading_w = clamp0(in.heading_w);
    in.body_h = clamp0(in.body_h);
    in.caption_h = clamp0(in.caption_h);
    in.heading_h = clamp0(in.heading_h);
    in.scrollbar_w = clamp0(in.scrollbar_w);
    in.work_w = clamp0(in.work_w);
    in.work_h = clamp0(in.work_h);
    in.nc_w = clamp0(in.nc_w);
    in.nc_h = clamp0(in.nc_h);
    int ns = imin(imax(in.n_sections, 0), CS_MAX_SECTIONS);

    /* Row counts, clamped so the running total never passes CS_MAX_ROWS. */
    int rows_of[CS_MAX_SECTIONS];
    int first_of[CS_MAX_SECTIONS];
    int total = 0;
    for (int s = 0; s < ns; s++) {
        int r = clamp0(in.section_rows[s]);
        if (total + r > CS_MAX_ROWS) r = CS_MAX_ROWS - total;
        first_of[s] = total;
        rows_of[s] = r;
        total += r;
    }
    out->n_sections = ns;
    out->n_rows = total;

    const int pad        = ns_scale(SP_LG, dpi);   /* page padding */
    const int sec_gap    = ns_scale(SP_XL, dpi);   /* between sections */
    const int col_gap    = ns_scale(SP_XL, dpi);   /* between the two columns */
    const int row_gap    = ns_scale(SP_XS, dpi);   /* between rows */
    const int gutter     = ns_scale(SP_LG, dpi);   /* chord column -> text column */
    const int rule_gap   = ns_scale(SP_XS, dpi);   /* heading text -> rule */
    const int rule_h     = STROKE_HAIRLINE;        /* strokes are not scaled */
    const int after_rule = ns_scale(SP_SM, dpi);   /* rule -> first row */
    const int btn_w      = ns_scale(SZ_BTN_MIN_W, dpi);
    const int btn_h      = ns_scale(SZ_CTRL_H, dpi);
    const int btn_top    = ns_scale(SP_SM, dpi);   /* view -> button */
    const int btn_area   = btn_top + btn_h + pad;  /* below the view */

    const int col_w = imax(in.chord_w + gutter + in.text_w, in.heading_w);
    out->col_w = col_w;

    const int avail_w = clamp0(in.work_w - in.nc_w);
    const int avail_h = clamp0(in.work_h - in.nc_h);

    /* Two columns when they fit beside each other with the scrollbar's
     * width held back, so the choice never depends on whether it scrolls. */
    const int two_w = 2 * pad + 2 * col_w + col_gap + in.scrollbar_w;
    const int cols = (ns > CS_LEFT_SECTIONS && two_w <= avail_w) ? 2 : 1;
    out->columns = cols;

    /* Place every section in page coordinates. */
    int col_y[2] = { pad, pad };
    int placed[2] = { 0, 0 };
    for (int s = 0; s < ns; s++) {
        int c = (cols == 2 && s >= CS_LEFT_SECTIONS) ? 1 : 0;
        int x = pad + c * (col_w + col_gap);
        int y = col_y[c];
        if (placed[c]) y += sec_gap;            /* not before a column's first */
        placed[c] = 1;

        out->heading_col[s] = c;
        out->heading[s] = (CsRect){ x, y, col_w, in.heading_h };
        y += in.heading_h + rule_gap;
        out->rule[s] = (CsRect){ x, y, col_w, rule_h };
        y += rule_h + after_rule;

        for (int r = 0; r < rows_of[s]; r++) {
            int i = first_of[s] + r;
            int cond_h = in.has_cond[i] ? in.caption_h : 0;
            int text_x = x + in.chord_w + gutter;
            int text_w = col_w - in.chord_w - gutter;
            out->row_col[i] = c;
            out->row[i]    = (CsRect){ x, y, col_w, in.body_h + cond_h };
            out->chord[i]  = (CsRect){ x, y, in.chord_w, in.body_h };
            out->action[i] = (CsRect){ text_x, y, text_w, in.body_h };
            out->cond[i]   = (CsRect){ text_x, y + in.body_h, text_w, cond_h };
            y += in.body_h + cond_h;
            if (r + 1 < rows_of[s]) y += row_gap;
        }
        col_y[c] = y;
    }

    const int content_bottom = imax(col_y[0], col_y[1]);
    out->page_h = content_bottom + pad;

    int page_w = 2 * pad + cols * col_w + (cols - 1) * col_gap;
    int scrolls = out->page_h + btn_area > avail_h;
    int view_h = scrolls ? avail_h - btn_area : out->page_h;
    /* A work area too short for even one line still gets one line. */
    if (view_h < in.body_h) view_h = in.body_h;
    if (view_h < 1) view_h = 1;
    if (view_h >= out->page_h) { view_h = out->page_h; scrolls = 0; }
    out->scrolls = scrolls;

    int client_w = page_w + (scrolls ? in.scrollbar_w : 0);
    /* A work area narrower than one column: the window keeps to the work
     * area and the text column is clipped -- no horizontal scrolling, no
     * ellipsis (spec section 1). */
    if (avail_w > 0 && client_w > avail_w) client_w = avail_w;
    if (client_w < btn_w + 2 * pad) client_w = btn_w + 2 * pad;
    out->client_w = client_w;
    out->client_h = view_h + btn_area;

    int view_w = client_w - (scrolls ? in.scrollbar_w : 0);
    out->view = (CsRect){ 0, 0, view_w, view_h };
    if (scrolls)
        out->scrollbar = (CsRect){ view_w, 0, in.scrollbar_w, view_h };
    out->button = (CsRect){ (client_w - btn_w) / 2, view_h + btn_top, btn_w, btn_h };
}

void cheatsheet_input_from_table(CsInput *in, const Shortcut *t, size_t n,
                                 CsMeasureFn measure, void *ctx)
{
    if (!in || !t || !measure) return;
    memset(in->section_rows, 0, sizeof(in->section_rows));
    memset(in->has_cond, 0, sizeof(in->has_cond));
    in->n_sections = 0;
    in->chord_w = 0;
    in->text_w = 0;
    in->heading_w = 0;

    int s = -1;
    for (size_t i = 0; i < n && i < (size_t)CS_MAX_ROWS; i++) {
        if (i == 0 || strcmp(t[i].section, t[i - 1].section) != 0) {
            if (s + 1 >= CS_MAX_SECTIONS) break;
            s++;
            in->heading_w = imax(in->heading_w,
                                 measure(ctx, FONT_HEADING, t[i].section));
        }
        in->section_rows[s]++;
        in->has_cond[i] = t[i].condition[0] ? 1 : 0;
        in->chord_w = imax(in->chord_w, measure(ctx, FONT_BODY, t[i].chord));
        in->text_w = imax(in->text_w, measure(ctx, FONT_BODY, t[i].action));
        if (t[i].condition[0])
            in->text_w = imax(in->text_w,
                              measure(ctx, FONT_CAPTION, t[i].condition));
    }
    in->n_sections = s + 1;
}
