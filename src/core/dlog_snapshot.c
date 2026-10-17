/* src/core/dlog_snapshot.c -- see dlog_snapshot.h */
#include "dlog_snapshot.h"
#include "dlog.h"
#include <stdio.h>
#include <string.h>

size_t dlog_dirty_ranges(const unsigned char *flags, int n, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    size_t len = 0;
    int emitted = 0;
    int i = 0;
    while (flags && i < n) {
        if (!flags[i]) { i++; continue; }
        int start = i;
        while (i + 1 < n && flags[i + 1]) i++;
        int end = i;
        i++;
        char item[40];
        int w = (end > start) ? snprintf(item, sizeof(item), "%s%d-%d", emitted ? "," : "", start, end)
                              : snprintf(item, sizeof(item), "%s%d", emitted ? "," : "", start);
        if (w < 0) break;
        /* keep room for ",..." and the NUL */
        if (len + (size_t)w + 5u > cap) {
            if (len + 5u <= cap) {
                memcpy(out + len, ",...", 5);
                len += 4;
            }
            return len;
        }
        memcpy(out + len, item, (size_t)w + 1u);
        len += (size_t)w;
        emitted = 1;
    }
    if (!emitted) {
        if (cap < 5) { out[0] = '\0'; return 0; }
        memcpy(out, "none", 5);
        return 4;
    }
    return len;
}

/* A string value as logfmt, "-" for NULL. */
static void val(char *out, size_t cap, const char *s)
{
    (void)dlog_fmt_value(out, cap, s ? s : "-");
}

size_t dlog_snapshot_format(const DlogSnapshot *s, DlogSnapLine *out, size_t max)
{
    if (!s || !out || max == 0) return 0;
    size_t n = 0;

    if (n < max) {
        const DlogSnapWindow *w = &s->window;
        out[n].event = "issue.window";
        out[n].sid = 0;
        (void)snprintf(out[n].fields, sizeof(out[n].fields),
                       "client=%dx%d dpi=%d tab_height=%d left_margin=%d ai_docked=%d ai_width=%d "
                       "scrollbar=%d iconic=%d fullscreen=%d since_paint_ms=%ld cooldown_ms=%d focus=0x%llx",
                       w->client_w, w->client_h, w->dpi, w->tab_height, w->left_margin,
                       w->ai_docked, w->ai_width, w->scrollbar_visible, w->iconic, w->fullscreen,
                       w->ms_since_last_paint, w->cooldown_ms, w->focus_hwnd);
        n++;
    }

    if (s->has_term && n < max) {
        const DlogSnapTerm *t = &s->term;
        char kind[40], dirty[300];
        val(kind, sizeof(kind), t->kind);
        (void)dlog_dirty_ranges(t->dirty, t->dirty_n, dirty, sizeof(dirty));
        out[n].event = "issue.term";
        out[n].sid = t->sid;
        (void)snprintf(out[n].fields, sizeof(out[n].fields),
                       "kind=%s connected=%d grid=%dx%d lines_count=%d lines_start=%d lines_capacity=%d "
                       "scrollback_offset=%d cursor=%d,%d cursor_visible=%d alt=%d region=%d-%d "
                       "full_redraw=%d write_seq=%llu dirty=%s",
                       kind, t->connected, t->rows, t->cols, t->lines_count, t->lines_start,
                       t->lines_capacity, t->scrollback_offset, t->cursor_row, t->cursor_col,
                       t->cursor_visible, t->alt_screen, t->scroll_top, t->scroll_bottom,
                       t->full_redraw_needed, t->write_seq, dirty);
        n++;
    }

    if (n < max) {
        const DlogSnapRender *r = &s->render;
        out[n].event = "issue.render";
        out[n].sid = 0;
        (void)snprintf(out[n].fields, sizeof(out[n].fields),
                       "char=%dx%d dispbuf=%dx%d prev_cursor=%d,%d",
                       r->char_w, r->char_h, r->dispbuf_rows, r->dispbuf_cols,
                       r->prev_cursor_row, r->prev_cursor_col);
        n++;
    }

    int ns = s->nsessions;
    if (ns < 0) ns = 0;
    if (ns > DLOG_SNAP_MAX_SESSIONS) ns = DLOG_SNAP_MAX_SESSIONS;
    for (int i = 0; i < ns && n < max; i++) {
        const DlogSnapSession *x = &s->sessions[i];
        char kind[40];
        val(kind, sizeof(kind), x->kind);
        out[n].event = "issue.session";
        out[n].sid = x->sid;
        (void)snprintf(out[n].fields, sizeof(out[n].fields),
                       "kind=%s connected=%d dirty_rows=%d alt=%d",
                       kind, x->connected, x->dirty_rows, x->alt_screen);
        n++;
    }

    if (n < max) {
        const DlogSnapAi *a = &s->ai;
        char pol[80];
        val(pol, sizeof(pol), a->policy);
        out[n].event = "issue.ai";
        out[n].sid = 0;
        (void)snprintf(out[n].fields, sizeof(out[n].fields),
                       "panel=%d stream_live=%d dispatch_batch=%d policy=%s",
                       a->panel_visible, a->stream_live, a->dispatch_batch, pol);
        n++;
    }
    return n;
}
