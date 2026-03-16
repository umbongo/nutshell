#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "session_manager.h"
#include "../core/app_font.h"
#include "../core/ui_theme.h"
#include "../core/edit_scroll.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "../config/profile.h"
#include "../config/config.h"
#include "../core/vector.h"
#include "resource.h"

#define IDT_AINOTES_SCROLL 50  /* timer ID for AI notes scroll sync */
#define IDT_LIST_SCROLL    51  /* timer ID for session list scroll sync */

/* ---- Internal state passed through GWLP_USERDATA ---- */

typedef struct {
    Config     *cfg;
    const char *config_path;
    Profile    *out_profile;
    int         edit_idx;   /* -1 = new entry; >= 0 = index in cfg->profiles */
    HFONT       hDlgFont;
    const ThemeColors *theme;
    HBRUSH      hBrBgPrimary;
    HBRUSH      hBrBgSecondary;
    HWND        hAiScrollbar; /* custom scrollbar for AI notes edit */
    int         ai_line_h;    /* cached line height in px for AI notes */
    HWND        hListScrollbar; /* custom scrollbar for session listbox */
} SessMgrState;

/* ---- Helpers ---- */

/* Rebuild the listbox from cfg->profiles.  Falls back to host if name is empty. */
static void list_rebuild(HWND hList, const Config *cfg)
{
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    size_t n = vec_size(&cfg->profiles);
    for (size_t i = 0; i < n; i++) {
        const Profile *pr = (const Profile *)vec_get(&cfg->profiles, i);
        const char *label = (pr->name[0] != '\0') ? pr->name : pr->host;
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)label);
    }
}

/* Clear all form fields and reset auth combo to Password. */
static void form_clear(HWND hwnd)
{
    SetDlgItemTextA(hwnd, IDC_EDIT_NAME,    "");
    SetDlgItemTextA(hwnd, IDC_EDIT_HOST,    "");
    SetDlgItemInt  (hwnd, IDC_EDIT_PORT,    22, FALSE);
    SetDlgItemTextA(hwnd, IDC_EDIT_USER,    "");
    SetDlgItemTextA(hwnd, IDC_EDIT_PASS,    "");
    SetDlgItemTextA(hwnd, IDC_EDIT_KEYPATH, "");
    SetDlgItemTextA(hwnd, IDC_EDIT_AI_NOTES, "");
    SendMessage(GetDlgItem(hwnd, IDC_COMBO_AUTH), CB_SETCURSEL, 0, 0);
}

/* Populate form fields from an existing profile. */
static void form_load(HWND hwnd, const Profile *pr)
{
    SetDlgItemTextA(hwnd, IDC_EDIT_NAME,    pr->name);
    SetDlgItemTextA(hwnd, IDC_EDIT_HOST,    pr->host);
    SetDlgItemInt  (hwnd, IDC_EDIT_PORT,    (UINT)pr->port, FALSE);
    SetDlgItemTextA(hwnd, IDC_EDIT_USER,    pr->username);
    SetDlgItemTextA(hwnd, IDC_EDIT_PASS,    pr->password);
    SetDlgItemTextA(hwnd, IDC_EDIT_KEYPATH, pr->key_path);
    SetDlgItemTextA(hwnd, IDC_EDIT_AI_NOTES, pr->ai_notes);
    SendMessage(GetDlgItem(hwnd, IDC_COMBO_AUTH), CB_SETCURSEL,
                pr->auth_type == AUTH_KEY ? 1 : 0, 0);
}

/* Show/hide Key path row and update Pass cue banner based on auth type. */
static void toggle_auth_fields(HWND hwnd)
{
    int  idx    = (int)SendMessage(GetDlgItem(hwnd, IDC_COMBO_AUTH),
                                   CB_GETCURSEL, 0, 0);
    BOOL is_key = (idx == 1);

    ShowWindow(GetDlgItem(hwnd, IDC_STATIC_KEY),      is_key ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_EDIT_KEYPATH),    is_key ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE_KEY),  is_key ? SW_SHOW : SW_HIDE);
    SendMessage(GetDlgItem(hwnd, IDC_EDIT_PASS), EM_SETCUEBANNER, 0,
                (LPARAM)(is_key ? L"Key passphrase (leave blank if none)"
                                : L"Password"));
}

/* Sync the AI notes edit control's scroll state to the custom scrollbar. */
static void ai_notes_sync_scroll(HWND hwnd, SessMgrState *st)
{
    HWND hEdit = GetDlgItem(hwnd, IDC_EDIT_AI_NOTES);
    csb_sync_edit(hEdit, st->hAiScrollbar, st->ai_line_h);
}

/* Sync the session listbox scroll state to its custom scrollbar. */
static void list_sync_scroll(HWND hwnd, SessMgrState *st)
{
    if (!st->hListScrollbar) return;
    HWND hList = GetDlgItem(hwnd, IDC_LIST_SESSIONS);
    if (!hList) return;

    int total = (int)SendMessage(hList, LB_GETCOUNT, 0, 0);
    int top   = (int)SendMessage(hList, LB_GETTOPINDEX, 0, 0);
    RECT rc;
    GetClientRect(hList, &rc);
    int item_h  = (int)SendMessage(hList, LB_GETITEMHEIGHT, 0, 0);
    int visible = (item_h > 0) ? ((rc.bottom - rc.top) / item_h) : total;

    if (total <= visible) {
        csb_set_range(st->hListScrollbar, 0, 0, 1);
        csb_set_pos(st->hListScrollbar, 0);
    } else {
        csb_set_range(st->hListScrollbar, 0, total - 1, visible);
        csb_set_pos(st->hListScrollbar, top);
    }
}

/*
 * Read form fields into *pr.
 * Returns 1 on success, 0 if host is empty (caller shows error).
 */
static int form_read(HWND hwnd, Profile *pr)
{
    GetDlgItemTextA(hwnd, IDC_EDIT_HOST, pr->host, sizeof(pr->host));
    if (pr->host[0] == '\0') {
        return 0;
    }
    GetDlgItemTextA(hwnd, IDC_EDIT_NAME,    pr->name,     sizeof(pr->name));
    GetDlgItemTextA(hwnd, IDC_EDIT_USER,    pr->username, sizeof(pr->username));
    GetDlgItemTextA(hwnd, IDC_EDIT_PASS,    pr->password, sizeof(pr->password));
    GetDlgItemTextA(hwnd, IDC_EDIT_KEYPATH, pr->key_path, sizeof(pr->key_path));
    GetDlgItemTextA(hwnd, IDC_EDIT_AI_NOTES, pr->ai_notes, sizeof(pr->ai_notes));

    int auth_idx  = (int)SendMessage(GetDlgItem(hwnd, IDC_COMBO_AUTH),
                                     CB_GETCURSEL, 0, 0);
    pr->auth_type = (auth_idx == 1) ? AUTH_KEY : AUTH_PASSWORD;

    BOOL ok;
    UINT port = GetDlgItemInt(hwnd, IDC_EDIT_PORT, &ok, FALSE);
    pr->port  = (ok && port >= 1u && port <= 65535u) ? (int)port : 22;

    return 1;
}

/* ---- Dialog procedure ---- */

static INT_PTR CALLBACK SessMgrDlgProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam)
{
    SessMgrState *st = (SessMgrState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {

    case WM_INITDIALOG: {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
        st = (SessMgrState *)lParam;

        HWND hCombo = GetDlgItem(hwnd, IDC_COMBO_AUTH);
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"Password");
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"SSH Key");
        SendMessage (hCombo, CB_SETCURSEL, 0, 0);

        /* Limit AI Notes to ~400 words (2559 chars) */
        SendDlgItemMessage(hwnd, IDC_EDIT_AI_NOTES, EM_SETLIMITTEXT, 2559, 0);
        SendMessage(GetDlgItem(hwnd, IDC_EDIT_AI_NOTES), EM_SETCUEBANNER, 0,
                    (LPARAM)L"Notes for AI about this server (max 400 words)");

        /* Apply configured font at UI size to all child controls */
        {
            HDC hdc = GetDC(hwnd);
            int h = -MulDiv(APP_FONT_UI_SIZE, GetDeviceCaps(hdc, LOGPIXELSY), 72);
            ReleaseDC(hwnd, hdc);
            st->hDlgFont = CreateFont(
                h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                st->cfg->settings.font);
            if (st->hDlgFont) {
                HWND hChild = NULL;
                while ((hChild = FindWindowEx(hwnd, hChild, NULL, NULL)) != NULL)
                    SendMessage(hChild, WM_SETFONT, (WPARAM)st->hDlgFont, TRUE);
            }
        }

        /* Theme: look up from config, create brushes, apply title bar + borders */
        {
            int idx = ui_theme_find(st->cfg->settings.colour_scheme);
            st->theme = ui_theme_get(idx);
            st->hBrBgPrimary   = CreateSolidBrush(theme_cr(st->theme->bg_primary));
            st->hBrBgSecondary = CreateSolidBrush(theme_cr(st->theme->bg_secondary));
            themed_apply_title_bar(hwnd, st->theme);
            themed_apply_borders(hwnd, st->theme);

            /* Convert buttons to owner-drawn */
            static const int btn_ids[] = {
                IDC_BTN_NEW, IDC_BTN_EDIT, IDC_BTN_DELETE,
                IDC_BTN_SAVE, IDC_BTN_BROWSE_KEY, IDOK, IDCANCEL
            };
            for (int i = 0; i < (int)(sizeof(btn_ids)/sizeof(btn_ids[0])); i++) {
                HWND hBtn = GetDlgItem(hwnd, btn_ids[i]);
                if (hBtn) {
                    LONG style = GetWindowLong(hBtn, GWL_STYLE);
                    style = (style & ~(BS_DEFPUSHBUTTON | BS_PUSHBUTTON)) | BS_OWNERDRAW;
                    SetWindowLong(hBtn, GWL_STYLE, style);
                    InvalidateRect(hBtn, NULL, TRUE);
                }
            }
        }

        /* Custom scrollbar for AI notes multiline edit */
        {
            HWND hEdit = GetDlgItem(hwnd, IDC_EDIT_AI_NOTES);
            if (hEdit) {
                /* Measure line height from the configured font */
                HDC hdc = GetDC(hEdit);
                HGDIOBJ old = SelectObject(hdc, (HGDIOBJ)st->hDlgFont);
                TEXTMETRIC tm;
                GetTextMetrics(hdc, &tm);
                st->ai_line_h = tm.tmHeight + tm.tmExternalLeading;
                if (st->ai_line_h < 1) st->ai_line_h = 16;
                SelectObject(hdc, old);
                ReleaseDC(hEdit, hdc);

                /* Position the scrollbar on the right edge of the edit */
                RECT erc;
                GetWindowRect(hEdit, &erc);
                POINT pt = { erc.right, erc.top };
                ScreenToClient(hwnd, &pt);
                int eh = erc.bottom - erc.top;

                csb_register(GetModuleHandle(NULL));
                st->hAiScrollbar = csb_create(hwnd, pt.x, pt.y,
                                              CSB_WIDTH, eh, st->theme,
                                              GetModuleHandle(NULL));

                /* Start a timer to sync scroll state (50ms) */
                SetTimer(hwnd, IDT_AINOTES_SCROLL, 50, NULL);
            }
        }

        /* Custom scrollbar for session listbox */
        {
            HWND hList = GetDlgItem(hwnd, IDC_LIST_SESSIONS);
            if (hList) {
                RECT lrc;
                GetWindowRect(hList, &lrc);
                POINT pt = { lrc.right, lrc.top };
                ScreenToClient(hwnd, &pt);
                int lh = lrc.bottom - lrc.top;

                csb_register(GetModuleHandle(NULL));
                st->hListScrollbar = csb_create(hwnd, pt.x, pt.y,
                                                CSB_WIDTH, lh, st->theme,
                                                GetModuleHandle(NULL));
                SetTimer(hwnd, IDT_LIST_SCROLL, 50, NULL);
            }
        }

        list_rebuild(GetDlgItem(hwnd, IDC_LIST_SESSIONS), st->cfg);
        form_clear(hwnd);
        toggle_auth_fields(hwnd);
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id  = LOWORD(wParam);
        WORD ntf = HIWORD(wParam);

        /* Auth combo changed */
        if (id == IDC_COMBO_AUTH && ntf == CBN_SELCHANGE) {
            toggle_auth_fields(hwnd);
            return TRUE;
        }

        /* --- List box notifications --- */
        if (id == IDC_LIST_SESSIONS) {
            HWND hList = GetDlgItem(hwnd, IDC_LIST_SESSIONS);

            if (ntf == LBN_SELCHANGE) {
                /* Single click: highlight + populate form */
                int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && (size_t)sel < vec_size(&st->cfg->profiles)) {
                    st->edit_idx = sel;
                    form_load(hwnd,
                        (const Profile *)vec_get(&st->cfg->profiles,
                                                 (size_t)sel));
                    toggle_auth_fields(hwnd);
                }
                return TRUE;
            }

            if (ntf == LBN_DBLCLK) {
                /* Double click: connect immediately */
                int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && (size_t)sel < vec_size(&st->cfg->profiles)) {
                    *st->out_profile =
                        *(const Profile *)vec_get(&st->cfg->profiles,
                                                  (size_t)sel);
                    EndDialog(hwnd, IDOK);
                }
                return TRUE;
            }
        }

        /* --- New: clear form and deselect list --- */
        if (id == IDC_BTN_NEW) {
            st->edit_idx = -1;
            SendMessage(GetDlgItem(hwnd, IDC_LIST_SESSIONS),
                        LB_SETCURSEL, (WPARAM)-1, 0);
            form_clear(hwnd);
            toggle_auth_fields(hwnd);
            SetFocus(GetDlgItem(hwnd, IDC_EDIT_NAME));
            return TRUE;
        }

        /* --- Edit: load selected profile into form --- */
        if (id == IDC_BTN_EDIT) {
            HWND hList = GetDlgItem(hwnd, IDC_LIST_SESSIONS);
            int  sel   = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel < 0) {
                MessageBoxA(hwnd, "Select a session first.", "Edit",
                            MB_ICONINFORMATION);
            } else {
                st->edit_idx = sel;
                form_load(hwnd,
                    (const Profile *)vec_get(&st->cfg->profiles,
                                             (size_t)sel));
                toggle_auth_fields(hwnd);
                SetFocus(GetDlgItem(hwnd, IDC_EDIT_NAME));
            }
            return TRUE;
        }

        /* --- Delete: remove selected profile --- */
        if (id == IDC_BTN_DELETE) {
            HWND hList = GetDlgItem(hwnd, IDC_LIST_SESSIONS);
            int  sel   = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel < 0) {
                MessageBoxA(hwnd, "Select a session to delete.", "Delete",
                            MB_ICONINFORMATION);
                return TRUE;
            }
            const Profile *pr = (const Profile *)vec_get(
                &st->cfg->profiles, (size_t)sel);
            char confirm[512];
            snprintf(confirm, sizeof(confirm), "Delete \"%s\"?",
                     pr->name[0] ? pr->name : pr->host);
            if (MessageBoxA(hwnd, confirm, "Delete Session",
                            MB_YESNO | MB_ICONWARNING) != IDYES) {
                return TRUE;
            }
            config_profile_free(
                (Profile *)vec_get(&st->cfg->profiles, (size_t)sel));
            vec_remove(&st->cfg->profiles, (size_t)sel);
            config_save(st->cfg, st->config_path);
            list_rebuild(hList, st->cfg);
            st->edit_idx = -1;
            form_clear(hwnd);
            toggle_auth_fields(hwnd);
            return TRUE;
        }

        /* --- Save: persist form to cfg->profiles --- */
        if (id == IDC_BTN_SAVE) {
            Profile tmp;
            memset(&tmp, 0, sizeof(tmp));
            if (!form_read(hwnd, &tmp)) {
                MessageBoxA(hwnd, "Please enter a hostname.", "Save",
                            MB_ICONWARNING);
                return TRUE;
            }
            HWND hList = GetDlgItem(hwnd, IDC_LIST_SESSIONS);
            if (st->edit_idx >= 0 &&
                (size_t)st->edit_idx < vec_size(&st->cfg->profiles)) {
                /* Update existing profile in place */
                Profile *pr = (Profile *)vec_get(&st->cfg->profiles,
                                                 (size_t)st->edit_idx);
                *pr = tmp;
            } else {
                /* Add new profile */
                Profile *pr = config_profile_new();
                *pr = tmp;
                vec_push(&st->cfg->profiles, pr);
                st->edit_idx = (int)vec_size(&st->cfg->profiles) - 1;
            }
            config_save(st->cfg, st->config_path);
            list_rebuild(hList, st->cfg);
            SendMessage(hList, LB_SETCURSEL, (WPARAM)st->edit_idx, 0);
            return TRUE;
        }

        /* --- Browse for key file --- */
        if (id == IDC_BTN_BROWSE_KEY) {
            char path[MAX_PATH];
            GetDlgItemTextA(hwnd, IDC_EDIT_KEYPATH, path, sizeof(path));
            OPENFILENAMEA ofn;
            memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = "Key files (*.pem;*.key;*.ppk)\0*.pem;*.key;*.ppk\0"
                              "All files (*.*)\0*.*\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn))
                SetDlgItemTextA(hwnd, IDC_EDIT_KEYPATH, path);
            return TRUE;
        }

        /* --- Connect: use current form values --- */
        if (id == IDOK) {
            Profile tmp;
            memset(&tmp, 0, sizeof(tmp));
            if (!form_read(hwnd, &tmp)) {
                MessageBoxA(hwnd, "Please enter a hostname.", "Connect",
                            MB_ICONWARNING);
                return TRUE;
            }
            *st->out_profile = tmp;
            EndDialog(hwnd, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_VSCROLL:
        /* Custom scrollbar for session listbox */
        if (st && st->hListScrollbar && (HWND)lParam == st->hListScrollbar) {
            WORD code = LOWORD(wParam);
            HWND hList = GetDlgItem(hwnd, IDC_LIST_SESSIONS);
            int top   = (int)SendMessage(hList, LB_GETTOPINDEX, 0, 0);
            int total = (int)SendMessage(hList, LB_GETCOUNT, 0, 0);
            RECT rc;
            GetClientRect(hList, &rc);
            int item_h  = (int)SendMessage(hList, LB_GETITEMHEIGHT, 0, 0);
            int visible = (item_h > 0) ? ((rc.bottom - rc.top) / item_h) : total;
            int new_top = top;
            switch (code) {
            case SB_LINEUP:    new_top = top - 1; break;
            case SB_LINEDOWN:  new_top = top + 1; break;
            case SB_PAGEUP:    new_top = top - visible; break;
            case SB_PAGEDOWN:  new_top = top + visible; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                new_top = csb_get_trackpos(st->hListScrollbar);
                break;
            case SB_TOP:       new_top = 0; break;
            case SB_BOTTOM:    new_top = total - visible; break;
            }
            if (new_top < 0) new_top = 0;
            if (new_top > total - visible) new_top = total - visible;
            SendMessage(hList, LB_SETTOPINDEX, (WPARAM)new_top, 0);
            list_sync_scroll(hwnd, st);
            return TRUE;
        }
        /* Custom scrollbar for AI notes */
        if (st && st->hAiScrollbar && (HWND)lParam == st->hAiScrollbar) {
            WORD code = LOWORD(wParam);
            HWND hEdit = GetDlgItem(hwnd, IDC_EDIT_AI_NOTES);
            int first = (int)SendMessage(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
            int delta = 0;
            switch (code) {
            case SB_LINEUP:    delta = -1; break;
            case SB_LINEDOWN:  delta =  1; break;
            case SB_PAGEUP:    delta = -edit_scroll_visible_lines(1, 1) * 3; break;
            case SB_PAGEDOWN:  delta =  edit_scroll_visible_lines(1, 1) * 3; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                delta = edit_scroll_line_delta(
                    csb_get_trackpos(st->hAiScrollbar), first);
                break;
            case SB_TOP:       delta = -first; break;
            case SB_BOTTOM:    delta = 99999;  break;
            }
            if (delta != 0)
                SendMessage(hEdit, EM_LINESCROLL, 0, (LPARAM)delta);
            ai_notes_sync_scroll(hwnd, st);
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wParam == IDT_AINOTES_SCROLL && st) {
            ai_notes_sync_scroll(hwnd, st);
            return TRUE;
        }
        if (wParam == IDT_LIST_SCROLL && st) {
            list_sync_scroll(hwnd, st);
            return TRUE;
        }
        break;

    case WM_DRAWITEM:
        if (st && st->theme) {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            int is_primary = ((int)dis->CtlID == IDOK);
            draw_themed_button(dis, st->theme, is_primary);
            return TRUE;
        }
        break;

    case WM_CTLCOLORDLG:
        if (st && st->theme) return (INT_PTR)st->hBrBgPrimary;
        break;

    case WM_CTLCOLORSTATIC:
        if (st && st->theme) {
            SetTextColor((HDC)wParam, theme_cr(st->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(st->theme->bg_primary));
            return (INT_PTR)st->hBrBgPrimary;
        }
        break;

    case WM_CTLCOLOREDIT:
        if (st && st->theme) {
            SetTextColor((HDC)wParam, theme_cr(st->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(st->theme->bg_secondary));
            return (INT_PTR)st->hBrBgSecondary;
        }
        break;

    case WM_CTLCOLORLISTBOX:
        if (st && st->theme) {
            SetTextColor((HDC)wParam, theme_cr(st->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(st->theme->bg_secondary));
            return (INT_PTR)st->hBrBgSecondary;
        }
        break;

    case WM_DESTROY:
        if (st) {
            KillTimer(hwnd, IDT_AINOTES_SCROLL);
            KillTimer(hwnd, IDT_LIST_SCROLL);
            if (st->hDlgFont)       DeleteObject(st->hDlgFont);
            if (st->hBrBgPrimary)   DeleteObject(st->hBrBgPrimary);
            if (st->hBrBgSecondary) DeleteObject(st->hBrBgSecondary);
        }
        break;
    }
    return FALSE;
}

/* ---- Public API ---- */

int SessionManager_Show(HINSTANCE hInstance, HWND parent,
                        Config *cfg, const char *config_path,
                        Profile *out_profile)
{
    SessMgrState st;
    st.cfg         = cfg;
    st.config_path = config_path;
    st.out_profile = out_profile;
    st.edit_idx    = -1;

    INT_PTR result = DialogBoxParam(hInstance,
                                    MAKEINTRESOURCE(IDD_SESSION_MANAGER),
                                    parent, SessMgrDlgProc, (LPARAM)&st);
    return (result == IDOK) ? 1 : 0;
}
