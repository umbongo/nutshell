/* The AI panel's side of voice input: the mic button (owner-drawn with
 * ns_draw primitives), the pulse, the cue painted in an empty input, the
 * status text and the delivery of a transcript into the box. voice_win.c
 * (the controller) calls the ai_chat_voice_* functions of ai_chat.h. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"

#ifdef _WIN32
#include "voice_config.h"
#include "voice_win.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ANIM_VOICE_PULSE = 0 };
#define VOICE_PULSE_MS 1000
#define MIC_SUBCLASS_ID 44

unsigned ai_chat_next_gen(void)
{
    static unsigned g;
    if (++g == 0) g = 1;
    return g;
}

static AiChatData *panel_data(HWND panel)
{
    if (!panel || !IsWindow(panel)) return NULL;
    return (AiChatData *)(LONG_PTR)GetWindowLongPtr(panel, GWLP_USERDATA);
}

static int busy_of(const AiChatData *d)
{
    if (ACTIVE_BUSY(d) || d->dispatch_active) return 1;
    if (d->active_state) {
        const CmdBatchSet *bs = &d->active_state->batches;
        for (int i = 0; i < bs->count; i++)
            if (!cmd_batch_finished(bs->b[i])) return 1;
    }
    return 0;
}

int ai_chat_voice_busy(HWND panel)
{
    const AiChatData *d = panel_data(panel);
    return d ? busy_of(d) : 1;
}

void ai_chat_voice_binding(HWND panel, VoiceBinding *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    const AiChatData *d = panel_data(panel);
    if (!d) return;
    out->session = d->active_state;
    out->session_gen = d->session_gen;
    out->conv_gen = d->conv_gen;
}

static wchar_t *utf8_to_wide(const char *u)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w && MultiByteToWideChar(CP_UTF8, 0, u, -1, w, n) <= 0) { free(w); return NULL; }
    return w;
}

static void wipe_free_wide(wchar_t *w)
{
    if (!w) return;
    secure_zero(w, (wcslen(w) + 1) * sizeof(wchar_t));
    free(w);
}

static void mic_tip_text(char *out, size_t cap)
{
    const Settings *s = ui_current_settings();
    snprintf(out, cap, "Talk to the assistant (%s)",
             voice_ptt_key_label(s ? s->voice_ptt_key : VOICE_PTT_RIGHT_SHIFT));
}

void ai_chat_voice_attach(HWND panel, int on)
{
    AiChatData *d = panel_data(panel);
    if (!d) return;
    char tip[96];
    mic_tip_text(tip, sizeof(tip));
    if (on) {
        if (!d->hMicBtn) {
            d->hMicBtn = CreateWindow("BUTTON", "",
                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                0, 0, 1, 1, panel, (HMENU)IDC_CHAT_MIC, NULL, NULL);
            if (!d->hMicBtn) return;
            ns_hover_init(&d->mic_hover);
            d->mic_hover_tracking = 0;
            SetWindowSubclass(d->hMicBtn, ai_chat_mic_subclass, MIC_SUBCLASS_ID, 0);
            ai_chat_add_tooltip(d->hTooltip, d->hMicBtn, tip);
            ai_chat_relayout(d);
        } else if (d->hTooltip) {
            TOOLINFO ti;
            memset(&ti, 0, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.hwnd = panel;
            ti.uId = (UINT_PTR)d->hMicBtn;
            ti.lpszText = tip;
            SendMessage(d->hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
        }
        ai_chat_update_panel_state(d);   /* the empty state's voice hint */
        return;
    }
    if (!d->hMicBtn) return;
    if (d->hTooltip) {
        TOOLINFO ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.hwnd = panel;
        ti.uId = (UINT_PTR)d->hMicBtn;
        SendMessage(d->hTooltip, TTM_DELTOOL, 0, (LPARAM)&ti);
    }
    DestroyWindow(d->hMicBtn);
    d->hMicBtn = NULL;
    d->voice_ui = AIV_IDLE;
    d->voice_cue[0] = '\0';
    ai_chat_header_invalidate(d);   /* the cue was shown in the header */
    d->anim.count = 0;   /* drop the pulse */
    if (d->hInput) InvalidateRect(d->hInput, NULL, TRUE);
    ai_chat_relayout(d);
    ai_chat_update_panel_state(d);   /* drops the voice hint */
}

void ai_chat_voice_set_ui(HWND panel, AiVoiceUi st, const char *cue_utf8)
{
    AiChatData *d = panel_data(panel);
    if (!d) return;
    d->voice_ui = st;
    snprintf(d->voice_cue, sizeof(d->voice_cue), "%s",
             (st != AIV_IDLE && cue_utf8) ? cue_utf8 : "");
    if (st == AIV_RECORDING && !ns_reduced_motion()) {
        ns_anim_list_add(&d->anim, ANIM_VOICE_PULSE, (unsigned long)GetTickCount(),
                         VOICE_PULSE_MS);
        SetTimer(panel, TIMER_MOTION, MOTION_TICK_MS, NULL);
    }
    if (d->hMicBtn) {
        EnableWindow(d->hMicBtn, st != AIV_TRANSCRIBING);
        InvalidateRect(d->hMicBtn, NULL, FALSE);
    }
    if (d->hInput) InvalidateRect(d->hInput, NULL, TRUE);
    ai_chat_header_invalidate(d);   /* the cue shows in the header too */
}

void ai_chat_voice_status(HWND panel, const char *text, int is_error)
{
    AiChatData *d = panel_data(panel);
    if (!d || !text) return;
    if (is_error) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, text);
        if (d->hChatList) {
            chat_listview_invalidate(d->hChatList);
            chat_listview_scroll_to_bottom(d->hChatList);
        }
        return;
    }
    snprintf(d->context_label, sizeof(d->context_label), "%s", text);
    snprintf(d->voice_status, sizeof(d->voice_status), "%s", d->context_label);
    ai_chat_header_invalidate(d);
    SetTimer(panel, TIMER_VOICE_STATUS, VOICE_STATUS_MS, NULL);
}

void ai_chat_voice_deliver(HWND panel, const char *transcript, int auto_send, int force_send)
{
    AiChatData *d = panel_data(panel);
    if (!d || !d->hInput || !transcript) return;
    char *cur = ai_chat_input_text_utf8(d->hInput);
    if (!cur) return;
    const size_t cur_len = strlen(cur);
    const size_t cap = cur_len + strlen(transcript) + 4;
    char *out = (char *)malloc(cap);
    if (!out) { secure_zero(cur, cur_len); free(cur); return; }

    VoiceDelivery dv = voice_deliver(cur, transcript, auto_send, out, cap);
    /* VD_SEND is the auto-send rule; force_send is the controller's "send now"
     * (the finish rule said so): words in the box, then the box goes. */
    const int send = (dv == VD_SEND) || (force_send && dv != VD_NOTHING);
    const char *busy_note = NULL;
    if (send && busy_of(d)) busy_note = "Assistant is busy: your words are in the box";
    if (dv == VD_NOTHING) {
        ai_chat_voice_status(panel, "Didn't catch that", 0);
    } else if (dv == VD_APPEND) {
        wchar_t *w = utf8_to_wide(out + cur_len);
        if (w) {
            int len = GetWindowTextLengthW(d->hInput);
            SendMessageW(d->hInput, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            SendMessageW(d->hInput, EM_REPLACESEL, TRUE, (LPARAM)w);
            wipe_free_wide(w);
        }
    } else {   /* VD_SEND or VD_FILL: the box was empty */
        wchar_t *w = utf8_to_wide(out);
        if (w) {
            SetWindowTextW(d->hInput, w);
            wipe_free_wide(w);
        }
        int len = GetWindowTextLengthW(d->hInput);
        SendMessageW(d->hInput, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    }
    if (dv != VD_NOTHING) {
        if (send && !busy_note) {
            /* Exactly the suggestion chips' path. */
            ai_chat_send_user_message(d);
        } else if (busy_note) {
            ai_chat_voice_status(panel, busy_note, 0);
        }
        SetFocus(d->hInput);
    }
    secure_zero(cur, cur_len);
    free(cur);
    secure_zero(out, cap);
    free(out);
}

size_t ai_chat_voice_live_begin(HWND panel, int *need_sep)
{
    if (need_sep) *need_sep = 0;
    AiChatData *d = panel_data(panel);
    if (!d || !d->hInput) return 0;
    int len = GetWindowTextLengthW(d->hInput);
    if (len <= 0) return 0;
    if (need_sep) {
        wchar_t *w = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
        if (w) {
            int got = GetWindowTextW(d->hInput, w, len + 1);
            if (got > 0) {
                const wchar_t last = w[got - 1];
                *need_sep = !(last == L' ' || last == L'\t' || last == L'\r' || last == L'\n');
            }
            secure_zero(w, ((size_t)len + 1) * sizeof(wchar_t));
            free(w);
        }
    }
    return (size_t)len;
}

void ai_chat_voice_live_replace(HWND panel, size_t from_u16, const char *text_utf8)
{
    AiChatData *d = panel_data(panel);
    if (!d || !d->hInput || !text_utf8) return;
    wchar_t *w = utf8_to_wide(text_utf8);
    if (!w) return;
    SendMessageW(d->hInput, EM_SETSEL, (WPARAM)from_u16, (LPARAM)-1);
    SendMessageW(d->hInput, EM_REPLACESEL, FALSE, (LPARAM)w);   /* no undo entry */
    wipe_free_wide(w);
    /* The caret is at the end after the replace; keep the newest words in view. */
    SendMessageW(d->hInput, EM_SCROLLCARET, 0, 0);
}

void ai_chat_voice_lock_typing(HWND panel, int on)
{
    AiChatData *d = panel_data(panel);
    if (d) d->voice_typing_locked = on ? 1 : 0;
}

int ai_chat_voice_send_box(HWND panel)
{
    AiChatData *d = panel_data(panel);
    if (!d) return 0;
    if (busy_of(d)) {
        ai_chat_voice_status(panel, "Assistant is busy: your words are in the box", 0);
        return 0;
    }
    ai_chat_send_user_message(d);
    if (d->hInput && IsWindowVisible(panel)) SetFocus(d->hInput);
    return 1;
}

void ai_chat_voice_timer(AiChatData *d, UINT_PTR id)
{
    if (!d) return;
    if (id == TIMER_VOICE_STATUS) {
        KillTimer(d->hwnd, TIMER_VOICE_STATUS);
        /* Only our own text: a busy indicator that replaced it stays. */
        if (d->voice_status[0] && strcmp(d->context_label, d->voice_status) == 0)
            ai_chat_update_context_bar(d);
        d->voice_status[0] = '\0';
        return;
    }
    if (id != TIMER_MOTION) return;
    int active = ns_anim_list_step(&d->anim, (unsigned long)GetTickCount(),
                                   ns_reduced_motion());
    if (d->voice_ui == AIV_RECORDING && !active && !ns_reduced_motion()) {
        ns_anim_list_add(&d->anim, ANIM_VOICE_PULSE, (unsigned long)GetTickCount(),
                         VOICE_PULSE_MS);   /* the 1 s cycle restarts */
        active = 1;
    }
    if (d->hMicBtn) InvalidateRect(d->hMicBtn, NULL, FALSE);
    if (!active) KillTimer(d->hwnd, TIMER_MOTION);
}

void ai_chat_voice_on_destroy(AiChatData *d)
{
    if (!d) return;
    KillTimer(d->hwnd, TIMER_MOTION);
    KillTimer(d->hwnd, TIMER_VOICE_STATUS);
    /* A recording the panel was bound to ends with it (live: the final text may
     * still land, nothing is sent). */
    d->voice_typing_locked = 0;
    if (d->voice_ui != AIV_IDLE) voice_on_deactivate();
}

void ai_chat_voice_busy_changed(AiChatData *d)
{
    if (d && d->hMicBtn) InvalidateRect(d->hMicBtn, NULL, FALSE);
}

/* ---- The painted pieces ---- */

void ai_chat_voice_paint_cue(AiChatData *d, HWND edit)
{
    if (!d || !d->voice_cue[0] || GetWindowTextLengthW(edit) > 0) return;
    wchar_t *w = utf8_to_wide(d->voice_cue);
    if (!w) return;
    HDC hdc = GetDC(edit);
    if (hdc) {
        RECT rc = {0, 0, 0, 0};
        SendMessage(edit, EM_GETRECT, 0, (LPARAM)&rc);
        HFONT f = (HFONT)SendMessage(edit, WM_GETFONT, 0, 0);
        HGDIOBJ old = f ? SelectObject(hdc, f) : NULL;
        int old_bk = SetBkMode(hdc, TRANSPARENT);
        COLORREF old_fg = SetTextColor(hdc, theme_cr(ns_tokens()->text_dim));
        DrawTextW(hdc, w, -1, &rc, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
        SetTextColor(hdc, old_fg);
        SetBkMode(hdc, old_bk);
        if (old) SelectObject(hdc, old);
        ReleaseDC(edit, hdc);
    }
    free(w);
}

/* A microphone outline: a capsule, the arc under it, a stem and a foot, built
 * from the ns_draw primitives at the button's centre. */
static void draw_mic_glyph(HDC hdc, const RECT *rc, COLORREF col, int dpi)
{
    const float cx = (float)(rc->left + rc->right) / 2.0f;
    const float cy = (float)(rc->top + rc->bottom) / 2.0f;
    const float u = (float)ns_scale(1, dpi);
    const int stroke = STROKE_RULE;

    RECT cap = { (int)(cx - 4.0f * u), (int)(cy - 9.0f * u),
                 (int)(cx + 4.0f * u), (int)(cy + 3.0f * u) };
    ns_draw_round_stroke(hdc, &cap, (int)(4.0f * u), col, stroke);

    /* The U under the capsule: the lower half of a circle. */
    float arc[2 * 13];
    const float r = 7.5f * u;
    const float acy = cy - 1.0f * u;
    for (int i = 0; i < 13; i++) {
        float a = 3.14159265f * (float)i / 12.0f;
        arc[2 * i]     = cx + r * cosf(a);
        arc[2 * i + 1] = acy + r * sinf(a);
    }
    ns_draw_polyline(hdc, arc, 13, col, stroke);

    float stem[4] = { cx, acy + r, cx, cy + 10.0f * u };
    ns_draw_polyline(hdc, stem, 2, col, stroke);
    float foot[4] = { cx - 4.0f * u, cy + 10.0f * u, cx + 4.0f * u, cy + 10.0f * u };
    ns_draw_polyline(hdc, foot, 2, col, stroke);
}

static void draw_dots(HDC hdc, const RECT *rc, COLORREF col, int dpi)
{
    const int d = ns_scale(SP_XS, dpi);
    const int gap = ns_scale(SP_XS, dpi);
    const int total = 3 * d + 2 * gap;
    int x = (rc->left + rc->right - total) / 2;
    const int y = (rc->top + rc->bottom - d) / 2;
    for (int i = 0; i < 3; i++) {
        RECT dot = { x, y, x + d, y + d };
        ns_draw_round_fill(hdc, &dot, d / 2, col, 255);
        x += d + gap;
    }
}

void ai_chat_voice_draw_mic(AiChatData *d, LPDRAWITEMSTRUCT dis)
{
    if (!d || !dis) return;
    const ThemeTokens *tok = ns_tokens();
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    const int pressed = (dis->itemState & ODS_SELECTED) != 0;
    const int busy = busy_of(d);
    const int hot = ns_hover_state_for(&d->mic_hover, 0) == 1;

    HBRUSH bg = CreateSolidBrush(theme_cr(tok->bg_primary.base));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    const int radius = ns_scale(R_CTRL, d->dpi);
    COLORREF fill, glyph;
    if (d->voice_ui == AIV_RECORDING) {
        float alpha = 1.0f;
        if (!ns_reduced_motion()) {
            NsAnimStep st = { 1.0, 0 };
            float t = 1.0f;
            if (ns_anim_list_get(&d->anim, ANIM_VOICE_PULSE, (unsigned long)GetTickCount(), 0, &st))
                t = (float)st.t;
            alpha = 0.45f + 0.55f * (t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f);
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;
        }
        fill = rgb_alpha(theme_cr(tok->danger.base), theme_cr(tok->bg_secondary.base), alpha);
        glyph = theme_cr(tok->danger.label);
    } else {
        unsigned int f = pressed ? tok->bg_secondary.pressed
                       : hot     ? tok->bg_secondary.hover
                                 : tok->bg_secondary.base;
        fill = theme_cr(f);
        glyph = theme_cr(busy ? tok->text_disabled
                              : (hot || pressed) ? tok->text_main : tok->text_dim);
    }
    ns_draw_round_fill(hdc, &rc, radius, fill, 255);
    ns_draw_round_stroke(hdc, &rc, radius, theme_cr(tok->border), STROKE_HAIRLINE);

    if (d->voice_ui == AIV_TRANSCRIBING)
        draw_dots(hdc, &rc, theme_cr(tok->text_dim), d->dpi);
    else
        draw_mic_glyph(hdc, &rc, glyph, d->dpi);
}

/* Hover and the click's focus: ns_hover tracks the one hit id (0) so only
 * this button repaints when it changes. */
LRESULT CALLBACK ai_chat_mic_subclass(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam, UINT_PTR uIdSubclass,
                                      DWORD_PTR dwRefData)
{
    (void)dwRefData;
    HWND panel = GetParent(hwnd);
    AiChatData *d = panel ? (AiChatData *)(LONG_PTR)GetWindowLongPtr(panel, GWLP_USERDATA)
                          : NULL;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (d) {
            if (!d->mic_hover_tracking) {
                TRACKMOUSEEVENT te = { sizeof(te), TME_LEAVE, hwnd, 0 };
                if (TrackMouseEvent(&te)) d->mic_hover_tracking = 1;
            }
            if (ns_hover_move(&d->mic_hover, 0).changed) InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        if (d) {
            d->mic_hover_tracking = 0;
            if (ns_hover_leave(&d->mic_hover).changed) InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ai_chat_mic_subclass, uIdSubclass);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

#endif /* _WIN32 */
