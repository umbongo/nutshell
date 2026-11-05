/* The status line: the policy control and its confirmation, the context
 * meter, the sash, and the geometry, hit-test, tooltips and paint behind them. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"

#ifdef _WIN32
#include <stdio.h>

/* Invalidate just the status line -- called whenever the policy or the
 * context numbers change. */
void ai_chat_invalidate_status_line(AiChatData *d)
{
    if (!d || !d->hwnd) return;
    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);
    RECT r = { l.status.x, l.status.y, l.status.x + l.status.w,
               l.status.y + l.status.h };
    InvalidateRect(d->hwnd, &r, FALSE);
    ai_chat_status_tools_update(d);
}

/* Move the session to `next` and bring every pending card into line with it.
 *
 * Raising the ceiling unblocks held rows in EVERY pending batch (not just
 * the newest) and tells the AI so, because it has a stale "blocked by
 * policy" message in its history and will otherwise keep advising the user
 * to change a setting they just changed. Lowering it re-blocks pending rows
 * the new ceiling no longer allows. Auto-approve only affects commands that
 * have not arrived yet, so turning it on releases nothing already waiting
 * (spec 2026-10-18 section 6: one click on the card's own Run button does
 * that); it shares this path so a single gesture that moves both the level
 * and the switch is applied once. */
static void apply_policy_change(AiChatData *d, CmdPolicy next)
{
    if (!d) return;
    cmd_policy_clamp(&next);
    CmdPolicy prev = d->approval_q.policy;
    if (next.allowed == prev.allowed && next.unattended == prev.unattended)
        return;
    /* The tip on screen describes the old policy; the next hover fetches
     * the new text (spec section 4). */
    if (d->hTooltip) SendMessage(d->hTooltip, TTM_POP, 0, 0);

    d->approval_q.policy = next;
    if (d->active_state) {
        d->active_state->policy = next;
        d->active_state->policy_seeded = 1;
        for (int bi = 0; bi < d->active_state->batches.count; bi++)
            d->active_state->batches.b[bi]->q.policy = next;
    }
    ai_chat_invalidate_status_line(d);

    if (next.allowed > prev.allowed) {
        if (d->active_state)
            for (int bi = 0; bi < d->active_state->batches.count; bi++)
                chat_approval_unblock_all(&d->active_state->batches.b[bi]->q);
        ChatMsgItem *it = d->msg_list.head;
        while (it) {
            if (it->type == CHAT_ITEM_COMMAND && it->u.cmd.blocked &&
                !it->u.cmd.quoted &&
                (int)it->u.cmd.safety <= next.allowed) {
                it->u.cmd.blocked = 0;
                it->u.cmd.approved = -1;
                it->u.cmd.selected = 1;  /* no longer held -- starts checked */
            }
            it = it->next;
        }
        if (d->conv.msg_count > 0) {
            char note[256];
            if (ai_build_policy_raised_note(next.allowed, note, sizeof(note)) > 0) {
                ai_conv_add(&d->conv, AI_ROLE_USER, note);
            }
        }
    } else if (next.allowed < prev.allowed) {
        if (d->active_state)
            for (int bi = 0; bi < d->active_state->batches.count; bi++)
                chat_approval_block_disallowed(&d->active_state->batches.b[bi]->q);
        ChatMsgItem *it = d->msg_list.head;
        while (it) {
            if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled &&
                it->u.cmd.approved == -1 &&
                (int)it->u.cmd.safety > next.allowed) {
                it->u.cmd.blocked = 1;
            }
            it = it->next;
        }
    }

    if (d->hChatList)
        chat_listview_invalidate(d->hChatList);
}

/* Every policy gesture goes through here: the cells, the Auto-approve
 * button, the cycle id 4005 and the harness's posted ids. Moving into
 * "Critical with Auto-approve on" asks first (spec 2026-10-18 section 2).
 * The box runs a modal loop, so: a second gesture while it is up is a
 * no-op, the tooltip is hidden first, and after Yes the change applies
 * only if the panel still exists, the same session is displayed and its
 * policy has not moved. Dispatch ticks and replies keep running during the
 * box; nothing here touches them. */
void ai_chat_request_policy_change(HWND hwnd, AiChatData *d, CmdPolicy next)
{
    if (!d || d->policy_confirm_open) return;
    CmdPolicy cur = d->approval_q.policy;
    if (!cmd_policy_needs_confirm(cur, next)) {
        apply_policy_change(d, next);
        return;
    }
    unsigned gen = d->session_gen;
    d->policy_confirm_open = 1;
    if (d->hTooltip) SendMessage(d->hTooltip, TTM_POP, 0, 0);
    int answer = MessageBoxA(hwnd, CMD_POLICY_CONFIRM_TEXT, "Auto-approve",
                             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    /* The panel may have been destroyed (closed, undocked) while the box was
     * up, freeing d: look it up again from the window. */
    if (!IsWindow(hwnd)) return;
    d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->policy_confirm_open = 0;
    if (answer != IDYES || d->session_gen != gen) return;
    if (d->approval_q.policy.allowed != cur.allowed ||
        d->approval_q.policy.unattended != cur.unattended) return;
    apply_policy_change(d, next);
}

/* Recompute the context meter/usage state and clear any busy-indicator
 * override text (see ai_chat_start_indicator()) so the status line goes back to
 * showing the normal used/limit numbers. The numbers themselves are
 * measured fresh from d->context_limit/actual_*_tokens/d->conv at paint
 * time (ai_chat_get_status_paint()) -- this just triggers that repaint. */
void ai_chat_update_context_bar(AiChatData *d)
{
    if (!d) return;
    d->context_label[0] = '\0';
    ai_chat_invalidate_status_line(d);
}

/* ── Status line: geometry, hit-test, tooltip, paint ──────────────────
 * The policy control is painted directly on the panel's own client area
 * (not child windows), so hover/click/tooltip all go through a hit-test
 * against a freshly measured AiStatusLayout + NsPolicyLayout rather than
 * window messages. See
 * docs/superpowers/specs/2026-09-11-status-policy-control-design.md
 * section 4.
 */

/* Format "<used> / <limit>" the same way the old boxed label did (e.g.
 * "1.2k / 200k"), just without the "Context: " prefix or "(N%)" suffix --
 * the meter bar shows the fraction visually now. */
static void ai_chat_format_meter_text(int tokens, int limit,
                                       char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    char tok_str[16], lim_str[16];
    if (tokens >= 1000)
        snprintf(tok_str, sizeof(tok_str), "%.1fk", tokens / 1000.0);
    else
        snprintf(tok_str, sizeof(tok_str), "%d", tokens);
    if (limit >= 1000)
        snprintf(lim_str, sizeof(lim_str), "%dk", limit / 1000);
    else
        snprintf(lim_str, sizeof(lim_str), "%d", limit);
    snprintf(buf, cap, "%s / %s", tok_str, lim_str);
}

/* Client rect to invalidate when a hover id gains or loses hover. */
static NsRect ai_chat_status_rect_for_id(const AiStatusPaint *sp, int id)
{
    NsRect z = { 0, 0, 0, 0 };
    if (id >= 0 && id < NS_POLICY_STOPS) return sp->pl.cell[id];
    if (id == STATUS_HIT_AUTO) return sp->sl.auto_btn;
    return z;
}

static int ai_chat_pt_in_rect(NsRect r, int x, int y)
{
    return r.w > 0 && r.h > 0 &&
           x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* Is (x, y), in client coordinates, over the composer's drag sash? */
int ai_chat_sash_hit(AiChatData *d, int x, int y)
{
    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);
    return ai_chat_pt_in_rect(l.sash, x, y);
}

/* The sash is a border-coloured hairline across its vertical centre. */
void ai_chat_paint_sash(AiChatData *d, HDC hdc)
{
    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);
    if (l.sash.w <= 0 || l.sash.h <= 0) return;
    ns_draw_separator(hdc, l.sash.x, l.sash.x + l.sash.w,
                      l.sash.y + l.sash.h / 2, theme_cr(ns_tokens()->border));
}

/* Measure the status line's variable-width text (segment labels,
 * auto-approve text, meter numbers) and lay it out via ai_status_layout().
 * Self-contained: borrows its own HDC when the caller doesn't have one
 * handy (hit-testing/tooltips), or reuses the caller's paint HDC. */
void ai_chat_get_status_paint(AiChatData *d, HDC hdc, AiStatusPaint *out)
{
    memset(out, 0, sizeof(*out));

    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);
    out->status.x = l.status.x; out->status.y = l.status.y;
    out->status.w = l.status.w; out->status.h = l.status.h;

    HDC own_hdc = hdc ? NULL : GetDC(d->hwnd);
    HDC use_hdc = hdc ? hdc : own_hdc;
    if (!use_hdc) return;

    HFONT font = ns_font(FONT_CAPTION, d->dpi);
    HGDIOBJ old = font ? SelectObject(use_hdc, font) : NULL;

    SIZE sz;
    int full_w[NS_POLICY_STOPS], short_w[NS_POLICY_STOPS];
    for (int k = 0; k < NS_POLICY_STOPS; k++) {
        const char *lbl = cmd_policy_stop_label(k);
        GetTextExtentPoint32A(use_hdc, lbl, (int)strlen(lbl), &sz);
        full_w[k] = sz.cx;
        const char *sh = cmd_policy_stop_short(k);
        GetTextExtentPoint32A(use_hdc, sh, (int)strlen(sh), &sz);
        short_w[k] = sz.cx;
    }
    AiStatusMeasure m;
    memset(&m, 0, sizeof(m));
    m.policy_full_w    = ns_policy_width(full_w, d->dpi);
    m.policy_compact_w = ns_policy_width(short_w, d->dpi);
    {
        const char *af = cmd_policy_auto_label(0);
        const char *ac = cmd_policy_auto_label(1);
        GetTextExtentPoint32A(use_hdc, af, (int)strlen(af), &sz);
        m.auto_full_w = sz.cx;
        GetTextExtentPoint32A(use_hdc, ac, (int)strlen(ac), &sz);
        m.auto_compact_w = sz.cx;
    }

    out->has_meter = d->context_limit > 0;
    if (out->has_meter) {
        int actual = d->actual_input_tokens + d->actual_output_tokens;
        int tokens = actual > 0 ? actual : ai_context_estimate_tokens_ex(&d->conv, d->last_snapshot_len);
        ai_chat_format_meter_text(tokens, d->context_limit,
                                  out->meter_text, sizeof(out->meter_text));
        GetTextExtentPoint32A(use_hdc, out->meter_text,
                              (int)strlen(out->meter_text), &sz);
        m.meter_text_w = sz.cx;
    }

    if (old) SelectObject(use_hdc, old);
    if (own_hdc) ReleaseDC(d->hwnd, own_hdc);

    ai_status_layout(out->status, d->dpi, &m, &out->sl);
    /* Cells are hit over the full row height; the pill is cut at the
     * layout's clip so a cell under the button neither paints nor hits. */
    ns_policy_layout(out->sl.policy, out->sl.compact ? short_w : full_w,
                     out->sl.policy_clip, out->status.h, d->dpi, &out->pl);
}

/* Hit-test a client point against the status line. Returns 0..3 for a
 * cell, STATUS_HIT_AUTO for the button, or -1. */
int ai_chat_status_hit(AiChatData *d, int x, int y)
{
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, NULL, &sp);
    int stop = -1;
    int what = ai_status_hit(&sp.sl, &sp.pl, x, y, &stop);
    if (what == HIT_POLICY_ALLOWED) return STATUS_HIT_CELL(stop);
    if (what == HIT_POLICY_AUTO) return STATUS_HIT_AUTO;
    return -1;
}

/* Client rect for a status-line hit id, for targeted invalidation. 0 when
 * the id is out of range or the element is zero-size. */
int ai_chat_status_rect(AiChatData *d, int id, RECT *out_rc)
{
    if (id < 0 || id >= STATUS_HIT_COUNT) return 0;
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, NULL, &sp);
    NsRect r = ai_chat_status_rect_for_id(&sp, id);
    if (r.w <= 0 || r.h <= 0) return 0;
    SetRect(out_rc, r.x, r.y, r.x + r.w, r.y + r.h);
    return 1;
}

/* Move each status-line tooltip tool onto its element. Called after every
 * ai_chat_relayout and every status-line invalidation (resize, DPI change, tier
 * change, policy or context change). A clipped cell gets an empty rect, so
 * it shows no tip. */
void ai_chat_status_tools_update(AiChatData *d)
{
    if (!d || !d->hTooltip || !d->hwnd) return;
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, NULL, &sp);
    for (int t = 0; t < STATUS_TOOL_COUNT; t++) {
        NsRect r = { 0, 0, 0, 0 };
        if (t < NS_POLICY_STOPS) {
            r = sp.pl.hit[t];
        } else if (t == STATUS_HIT_AUTO) {
            r = sp.sl.auto_btn;
        } else if (t == STATUS_TOOL_METER && sp.has_meter &&
                   (sp.sl.meter_bar.w > 0 || sp.sl.meter_text.w > 0)) {
            NsRect a = sp.sl.meter_bar.w > 0 ? sp.sl.meter_bar : sp.sl.meter_text;
            NsRect b = sp.sl.meter_text.w > 0 ? sp.sl.meter_text : sp.sl.meter_bar;
            r.x = a.x; r.y = a.y; r.w = (b.x + b.w) - a.x; r.h = a.h;
        }
        TOOLINFO ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.hwnd   = d->hwnd;
        ti.uId    = (UINT_PTR)(STATUS_TOOL_BASE + t);
        if (r.w > 0 && r.h > 0)
            SetRect(&ti.rect, r.x, r.y, r.x + r.w, r.y + r.h);
        else
            SetRectEmpty(&ti.rect);
        SendMessage(d->hTooltip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
    }
}

/* Paint the status line: bg_secondary fill with a border rule on top, the
 * policy control, and the context meter + used/limit numbers (or, while a
 * ai_chat_start_indicator() busy override is set, that text instead of the meter). */
void ai_chat_paint_status_line(AiChatData *d, HDC hdc)
{
    const ThemeTokens *tok = ns_tokens();
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, hdc, &sp);
    RECT status_rc = ai_chat_to_RECT(sp.status);

    HBRUSH bg_br = CreateSolidBrush(theme_cr(tok->bg_secondary.base));
    FillRect(hdc, &status_rc, bg_br);
    DeleteObject(bg_br);
    ns_draw_separator(hdc, status_rc.left, status_rc.right, status_rc.top,
                      theme_cr(tok->border));

    HFONT cap_font = ns_font(FONT_CAPTION, d->dpi);
    HGDIOBJ old_font = cap_font ? SelectObject(hdc, cap_font) : NULL;
    int old_bk = SetBkMode(hdc, TRANSPARENT);

    /* The policy pill and the Auto-approve button */
    {
        int hot = d->status_hover.hot_id;
        int hover_stop = (hot >= 0 && hot < NS_POLICY_STOPS) ? hot : -1;
        ns_draw_policy(hdc, &sp.pl, d->approval_q.policy, sp.sl.compact,
                       hover_stop, tok, cap_font, d->dpi);
        /* Tier 5 lets the button sit partly left of a row narrower than it
         * is: clip to the row so it never paints over the panel's edge. The
         * hit test keeps the layout's rect. */
        int saved_dc = SaveDC(hdc);
        IntersectClipRect(hdc, status_rc.left, status_rc.top,
                          status_rc.right, status_rc.bottom);
        ns_draw_auto_button(hdc, &sp.sl.auto_btn, d->approval_q.policy,
                            sp.sl.compact, hot == STATUS_HIT_AUTO, tok,
                            cap_font, d->dpi);
        if (saved_dc) RestoreDC(hdc, saved_dc);
    }

    /* Busy override, or the context meter + used/limit numbers. The busy
     * label uses the layout's label rect, which ends SP_SM short of the
     * button, so it can never paint over it (spec section 3). */
    if (d->context_label[0]) {
        if (sp.sl.label.w > 0) {
            RECT rc = ai_chat_to_RECT(sp.sl.label);
            SetTextColor(hdc, theme_cr(tok->text_main));
            DrawTextA(hdc, d->context_label, -1, &rc,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                      DT_END_ELLIPSIS);
        }
    } else if (sp.has_meter) {
        if (sp.sl.meter_bar.w > 0) {
            int actual = d->actual_input_tokens + d->actual_output_tokens;
            int tokens = actual > 0 ? actual : ai_context_estimate_tokens_ex(&d->conv, d->last_snapshot_len);
            double frac = d->context_limit > 0
                        ? (double)tokens / (double)d->context_limit : 0.0;
            ns_draw_meter(hdc, &sp.sl.meter_bar, frac, tok);
        }
        if (sp.sl.meter_text.w > 0) {
            RECT rc = ai_chat_to_RECT(sp.sl.meter_text);
            SetTextColor(hdc, theme_cr(tok->text_main));
            DrawTextA(hdc, sp.meter_text, -1, &rc,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    SetBkMode(hdc, old_bk);
    if (old_font) SelectObject(hdc, old_font);
}

#endif /* _WIN32 */
