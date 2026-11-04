/* The message box: its subclass proc (Enter, Shift+Enter, paste, wheel,
 * zoom), the bitmap and text paste helpers and the scrollbar sync. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"

#ifdef _WIN32
#include <stdio.h>

/* PNG encoder CLSID: {557CF406-1A04-11D3-9A73-0000F81EF32E} */
static const GPCLSID CLSID_PNG = {
    0x557CF406, 0x1A04, 0x11D3,
    {0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E}
};

/* Convert a clipboard bitmap to a base64-encoded PNG AiAttachment.
 * Returns NULL if the clipboard does not contain a bitmap or on error. */
static AiAttachment *clipboard_to_png_attachment(HWND owner)
{
    if (!IsClipboardFormatAvailable(CF_BITMAP))
        return NULL;
    if (!OpenClipboard(owner))
        return NULL;

    HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (!hbm) { CloseClipboard(); return NULL; }

    BITMAP bm_info;
    GetObject(hbm, sizeof(bm_info), &bm_info);

    GpBitmap *gpbmp = NULL;
    if (GdipCreateBitmapFromHBITMAP(hbm, NULL, &gpbmp) != 0 || !gpbmp) {
        CloseClipboard();
        return NULL;
    }
    CloseClipboard();

    IStream *stm = SHCreateMemStream(NULL, 0);
    if (!stm) {
        GdipDisposeImage((GpImage *)gpbmp);
        return NULL;
    }

    if (GdipSaveImageToStream((GpImage *)gpbmp, stm, &CLSID_PNG, NULL) != 0) {
        stm->lpVtbl->Release(stm);
        GdipDisposeImage((GpImage *)gpbmp);
        return NULL;
    }
    GdipDisposeImage((GpImage *)gpbmp);

    STATSTG stat;
    stm->lpVtbl->Stat(stm, &stat, STATFLAG_NONAME);
    size_t png_len = (size_t)stat.cbSize.QuadPart;

    unsigned char *png_buf = (unsigned char *)malloc(png_len);
    if (!png_buf) { stm->lpVtbl->Release(stm); return NULL; }

    LARGE_INTEGER zero_pos = {{0}};
    stm->lpVtbl->Seek(stm, zero_pos, STREAM_SEEK_SET, NULL);
    ULONG bytes_read = 0;
    stm->lpVtbl->Read(stm, png_buf, (ULONG)png_len, &bytes_read);
    stm->lpVtbl->Release(stm);

    if (bytes_read != (ULONG)png_len) { free(png_buf); return NULL; }

    size_t b64_size = ((png_len + 2) / 3) * 4 + 1;
    char *b64 = (char *)malloc(b64_size);
    if (!b64) { free(png_buf); return NULL; }

    size_t b64_len = base64_encode(png_buf, png_len, b64, b64_size);
    free(png_buf);
    if (b64_len == 0) { free(b64); return NULL; }

    const char *prefix = "data:image/png;base64,";
    size_t prefix_len = strlen(prefix);
    char *url = (char *)malloc(prefix_len + b64_len + 1);
    if (!url) { free(b64); return NULL; }
    memcpy(url, prefix, prefix_len);
    memcpy(url + prefix_len, b64, b64_len + 1);
    free(b64);

    AiAttachment *att = (AiAttachment *)calloc(1, sizeof(*att));
    if (!att) { free(url); return NULL; }
    att->base64_url = url;
    att->width = bm_info.bmWidth;
    att->height = bm_info.bmHeight;
    return att;
}

/* Pastes the clipboard's CF_TEXT into the (ANSI) message box with every bare
 * LF turned into CRLF, which is the only line end a multiline EDIT breaks on
 * (the transcript copies lines joined with LF). Returns 1 when text was
 * inserted, 0 when there was none (the caller then lets the control paste). */
int ai_chat_input_paste_text(HWND hInput, HWND clip_owner)
{
    int done = 0;
    if (!hInput || !IsClipboardFormatAvailable(CF_TEXT)) return 0;
    if (!OpenClipboard(clip_owner)) return 0;
    HGLOBAL hg = GetClipboardData(CF_TEXT);
    if (hg) {
        const char *txt = (const char *)GlobalLock(hg);
        if (txt) {
            size_t need = text_lf_to_crlf(txt, NULL, 0);
            char *buf = (char *)malloc(need + 1);
            if (buf) {
                text_lf_to_crlf(txt, buf, need + 1);
                SendMessageA(hInput, EM_REPLACESEL, TRUE, (LPARAM)buf);
                free(buf);
                done = 1;
            }
            GlobalUnlock(hg);
        }
    }
    CloseClipboard();
    return done;
}

/* Subclass proc for the multiline input: Enter sends, Shift+Enter inserts newline */
LRESULT CALLBACK ai_chat_input_subclass_proc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData)
{
    if (msg == WM_KEYDOWN || msg == WM_CHAR) {
        session_mark_user_active();
    }
    if (msg == WM_PASTE) {
        HWND parent = GetParent(hwnd);
        AiChatData *pd = parent
            ? (AiChatData *)GetWindowLongPtr(parent, GWLP_USERDATA) : NULL;
        if (pd && IsClipboardFormatAvailable(CF_BITMAP)) {
            AiAttachment *att = clipboard_to_png_attachment(parent);
            if (att) {
                ai_attachment_free(&pd->pending_attachment);
                pd->pending_attachment = att;
                SendMessageA(hwnd, EM_REPLACESEL, TRUE,
                             (LPARAM)"[Image attached] ");
                return 0;
            }
        }
        /* Text: LF -> CRLF so pasted lines stay lines. Our own insert
         * replaces the control's, so it must not run a second time. */
        if (ai_chat_input_paste_text(hwnd, hwnd)) return 0;
        /* Fall through to the control's own paste */
    }
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        int shift = GetKeyState(VK_SHIFT) & 0x8000;
        AiInputAction action = ai_input_key_action(1, shift ? 1 : 0);
        if (action == AI_INPUT_SEND) {
            /* Trigger the send button */
            HWND parent = GetParent(hwnd);
            PostMessage(parent, WM_COMMAND,
                        MAKEWPARAM(IDC_CHAT_SEND, BN_CLICKED), 0);
            return 0; /* eat the Enter key */
        }
        /* AI_INPUT_NEWLINE: fall through to default (inserts newline) */
    }
    if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (wParam == VK_OEM_PLUS  || wParam == (WPARAM)'='
         || wParam == VK_OEM_MINUS || wParam == (WPARAM)'-') {
            HWND parent = GetParent(hwnd);
            if (parent) SendMessage(parent, msg, wParam, lParam);
            return 0;
        }
    }
    if (msg == WM_MOUSEWHEEL) {
        int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
        /* Ctrl+Wheel: forward to parent so ai_chat_apply_zoom runs */
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            HWND parent = GetParent(hwnd);
            if (parent) SendMessage(parent, msg, wParam, lParam);
            return 0;
        }
        /* Whether WM_MOUSEWHEEL targets the window under the cursor or the
         * window with keyboard focus is a per-machine setting
         * (HKCU\Control Panel\Desktop\MouseWheelRouting: 2 = under cursor,
         * the Windows 10/11 default; 0/1 = focus window). On the
         * maintainer's box it's 2, so this handler mostly only ever sees a
         * wheel message when the cursor really is over the input -- this
         * check is defence for the 0/1 case (and for any other host where
         * it differs), not the fix for the reported bug (that turned out
         * to be the box's own hit-test in ai_panel_layout.c -- see
         * thinking_wheel_over_box()/thinking_wheel_should_chain()). Route
         * an under-cursor wheel to whatever one of our own windows is
         * actually under it (same pattern settings.c uses for its
         * multiline notes field, generalised with WindowFromPoint instead
         * of a single hard-coded control): if that's this input box,
         * handle it below; if it's some other child of ours (typically
         * the chat list), hand it there directly; if the mouse is over
         * neither, hand off to the panel, whose own WM_MOUSEWHEEL default
         * forwards to the chat list anyway. GetWindowThreadProcessId scopes
         * this to our own thread's windows so a wheel over some unrelated
         * window (routed here only under setting 0/1, where it follows
         * focus regardless of the cursor) doesn't get redirected into it. */
        POINT wpt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hit = WindowFromPoint(wpt);
        if (hit && hit != hwnd &&
            GetWindowThreadProcessId(hit, NULL) == GetCurrentThreadId()) {
            return SendMessage(hit, msg, wParam, lParam);
        }
        if (!hit || hit != hwnd) {
            HWND panel = GetParent(hwnd);
            if (panel) return SendMessage(panel, msg, wParam, lParam);
            return 0;
        }
        int scroll = edit_scroll_wheel_delta(zdelta, WHEEL_DELTA, 3);
        SendMessage(hwnd, EM_LINESCROLL, 0, (LPARAM)scroll);
        /* Sync input scrollbar */
        HWND parent = GetParent(hwnd);
        if (parent) {
            AiChatData *d = (AiChatData *)GetWindowLongPtr(parent,
                                                           GWLP_USERDATA);
            if (d) ai_chat_input_sync_scroll(d);
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, ai_chat_input_subclass_proc, uIdSubclass);
    }
    (void)dwRefData;
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* Sync the input scrollbar with the EDIT control's scroll state */
void ai_chat_input_sync_scroll(AiChatData *d)
{
    if (!d || !d->hInputScrollbar) return;
    int lh = d->input_line_h > 0 ? d->input_line_h : 1;
    csb_sync_edit(d->hInput, d->hInputScrollbar, lh);
}

#endif /* _WIN32 */
