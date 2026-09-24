#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "session_manager.h"
#include "../core/app_font.h"
#include "ns_font.h"
#include "../core/ns_scale.h"
#include "../core/ui_theme.h"
#include "../core/edit_scroll.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "icons.h"
#include "../config/profile.h"
#include "../config/config.h"
#include "../core/vector.h"
#include "../core/cmd_classify.h"
#include "local_shell.h"
#include "local_shell_probe.h"
#include "resource.h"
#include "dpi_util.h"
#include "secure_zero.h"

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

    /* Shell combo (IDC_EDIT_SHELL): row 0 is always "Automatic (...)",
     * stored value "" (index 0 of shell_cmd is unused/empty); rows 1..n
     * mirror local_shell_list_available(), stored value shell_cmd[i] is
     * that row's full quoted command line. Selecting a row copies the
     * stored value into the edit text (CBN_SELCHANGE below); the user can
     * still type over it freely. */
    char        shell_cmd[LOCAL_SHELL_CHOICE_MAX + 1][LOCAL_SHELL_CMD_MAX];
    int         shell_choice_count;
} SessMgrState;

/* ---- Helpers ---- */

/* Paint a single owner-drawn session row: theme bg, server icon, name. */
static void paint_session_row(LPDRAWITEMSTRUCT dis, const ThemeColors *theme)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int selected = (dis->itemState & ODS_SELECTED) != 0;
    int focus    = (dis->itemState & ODS_FOCUS) != 0;

    COLORREF bg = selected ? theme_cr(theme->accent)
                           : theme_cr(theme->bg_secondary);
    COLORREF fg = selected ? theme_cr(theme->bg_primary)
                           : theme_cr(theme->text_main);

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    int row_h = rc.bottom - rc.top;
    int pad   = 4;
    int icon_sz = row_h - pad * 2;
    if (icon_sz < 12) icon_sz = 12;

    RECT icon_rc = {
        rc.left + pad,
        rc.top + (row_h - icon_sz) / 2,
        rc.left + pad + icon_sz,
        rc.top + (row_h - icon_sz) / 2 + icon_sz
    };
    UINT dpi = (UINT)get_window_dpi(GetParent(dis->hwndItem));
    if (dpi == 0) dpi = 96;
    ns_icon_draw(hdc, NS_ICON_SERVER, &icon_rc, fg, dpi);

    if ((INT)dis->itemID >= 0) {
        char text[256];
        text[0] = '\0';
        SendMessageA(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);

        RECT text_rc = {
            icon_rc.right + pad,
            rc.top,
            rc.right - pad,
            rc.bottom
        };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);
        DrawTextA(hdc, text, -1, &text_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    if (focus) DrawFocusRect(hdc, &rc);
}

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
    SetDlgItemTextA(hwnd, IDC_EDIT_SHELL,   "");
    SetDlgItemTextA(hwnd, IDC_EDIT_AI_NOTES, "");
    SendMessage(GetDlgItem(hwnd, IDC_COMBO_AUTH), CB_SETCURSEL, 0, 0);
    SendMessage(GetDlgItem(hwnd, IDC_COMBO_PLATFORM), CB_SETCURSEL, 0, 0);
    SendMessage(GetDlgItem(hwnd, IDC_COMBO_KIND), CB_SETCURSEL, 0, 0);
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
    SetDlgItemTextA(hwnd, IDC_EDIT_SHELL,   pr->shell);
    SetDlgItemTextA(hwnd, IDC_EDIT_AI_NOTES, pr->ai_notes);
    SendMessage(GetDlgItem(hwnd, IDC_COMBO_AUTH), CB_SETCURSEL,
                pr->auth_type == AUTH_KEY ? 1 : 0, 0);
    SendMessage(GetDlgItem(hwnd, IDC_COMBO_KIND), CB_SETCURSEL,
                strcmp(pr->kind, "local") == 0 ? 1 : 0, 0);

    /* Select the dropdown row whose config token matches the profile's
     * platform string; fall back to row 0 (Auto-detect) for "auto", an
     * empty field (pre-existing profile struct with no platform saved
     * yet), or a token the current build doesn't recognise. */
    int plat_idx = 0;
    int plat_count = cmd_platform_choice_count();
    for (int i = 0; i < plat_count; i++) {
        const char *tok = cmd_platform_choice_name(i);
        if (tok && strcmp(tok, pr->platform) == 0) {
            plat_idx = i;
            break;
        }
    }
    SendMessage(GetDlgItem(hwnd, IDC_COMBO_PLATFORM), CB_SETCURSEL,
                (WPARAM)plat_idx, 0);
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

/* Show/hide the SSH connection rows vs. the Local shell command row based
 * on the Type combo's selection. Never leave focus in a control being
 * hidden. */
static void toggle_kind_fields(HWND hwnd)
{
    int  idx      = (int)SendMessage(GetDlgItem(hwnd, IDC_COMBO_KIND),
                                     CB_GETCURSEL, 0, 0);
    BOOL is_local = (idx == 1);

    static const int ssh_ids[] = {
        IDC_STATIC_HOST, IDC_EDIT_HOST, IDC_STATIC_PORT, IDC_EDIT_PORT,
        IDC_STATIC_USER, IDC_EDIT_USER, IDC_STATIC_AUTH, IDC_COMBO_AUTH,
        IDC_STATIC_PASS, IDC_EDIT_PASS, IDC_STATIC_KEY, IDC_EDIT_KEYPATH,
        IDC_BTN_BROWSE_KEY
    };
    static const int shell_ids[] = { IDC_STATIC_SHELL, IDC_EDIT_SHELL };

    const int *hide_ids = is_local ? ssh_ids   : shell_ids;
    int        hide_n   = is_local ? (int)(sizeof(ssh_ids)/sizeof(ssh_ids[0]))
                                    : (int)(sizeof(shell_ids)/sizeof(shell_ids[0]));

    HWND hFocus = GetFocus();
    for (int i = 0; i < hide_n; i++) {
        HWND hCtl = GetDlgItem(hwnd, hide_ids[i]);
        if (hCtl && hFocus == hCtl) {
            SetFocus(GetDlgItem(hwnd, IDC_EDIT_NAME));
            hFocus = NULL;
        }
    }

    if (is_local) {
        for (int i = 0; i < hide_n; i++)
            ShowWindow(GetDlgItem(hwnd, ssh_ids[i]), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_SHELL), SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_EDIT_SHELL),   SW_SHOW);
    } else {
        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_SHELL), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_EDIT_SHELL),   SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_HOST),  SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_EDIT_HOST),    SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_PORT),  SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_EDIT_PORT),    SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_USER),  SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_EDIT_USER),    SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_AUTH),  SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_COMBO_AUTH),   SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_PASS),  SW_SHOW);
        ShowWindow(GetDlgItem(hwnd, IDC_EDIT_PASS),    SW_SHOW);
        /* Key row's own visibility rule (key auth only) wins. */
        toggle_auth_fields(hwnd);
    }
}

/* ANSI (the display names and command lines local_shell.c builds are all
 * plain ASCII) -> wide, for EM_SETCUEBANNER, which takes nothing else. */
static void set_cue_banner(HWND ctrl, const char *text)
{
    if (!ctrl || !text) return;
    wchar_t wbuf[LOCAL_SHELL_CMD_MAX];
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0])));
    if (n <= 0) return;
    SendMessage(ctrl, EM_SETCUEBANNER, 0, (LPARAM)wbuf);
}

/* Populate the Shell combo: row 0 "Automatic (<whatever it resolves to
 * right now>)" (stored value ""), then one row per
 * local_shell_list_available() result (stored value: that row's full
 * command line). Called once, from WM_INITDIALOG. */
static void shell_combo_populate(HWND hwnd, SessMgrState *st)
{
    HWND hShell = GetDlgItem(hwnd, IDC_EDIT_SHELL);
    if (!hShell) return;

    LocalShellProbe probe;
    local_shell_fill_probe(&probe);

    LocalShellChoice choices[LOCAL_SHELL_CHOICE_MAX];
    int n = local_shell_list_available(&probe, choices, LOCAL_SHELL_CHOICE_MAX);
    if (n < 0) n = 0;
    if (n > LOCAL_SHELL_CHOICE_MAX) n = LOCAL_SHELL_CHOICE_MAX;

    char auto_label[96];
    if (n > 0) {
        snprintf(auto_label, sizeof(auto_label), "Automatic (%s)", choices[0].display);
    } else {
        snprintf(auto_label, sizeof(auto_label), "Automatic (no shell found)");
    }
    SendMessageA(hShell, CB_ADDSTRING, 0, (LPARAM)auto_label);
    st->shell_cmd[0][0] = '\0';

    for (int i = 0; i < n; i++) {
        SendMessageA(hShell, CB_ADDSTRING, 0, (LPARAM)choices[i].display);
        snprintf(st->shell_cmd[i + 1], sizeof(st->shell_cmd[i + 1]),
                "%s", choices[i].command);
    }
    st->shell_choice_count = n;

    SendMessage(hShell, CB_SETCURSEL, 0, 0);
    set_cue_banner(hShell, auto_label);
}

/* CBN_SELCHANGE on the Shell combo: row 0 (Automatic) clears the edit text
 * back to "" -- the cue banner (set once, at populate time) then shows
 * through -- any other row copies in that row's full command line, ready
 * to edit further. Out-of-range indexes (CB_ERR, or a stale selection from
 * before a repopulate) are left alone. */
static void shell_combo_selchange(HWND hwnd, SessMgrState *st)
{
    HWND hShell = GetDlgItem(hwnd, IDC_EDIT_SHELL);
    int idx = (int)SendMessage(hShell, CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx > st->shell_choice_count) return;
    SetDlgItemTextA(hwnd, IDC_EDIT_SHELL, st->shell_cmd[idx]);
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
        if (IsWindowVisible(st->hListScrollbar))
            ShowWindow(st->hListScrollbar, SW_HIDE);
    } else {
        csb_set_range(st->hListScrollbar, 0, total - 1, visible);
        csb_set_pos(st->hListScrollbar, top);
        if (!IsWindowVisible(st->hListScrollbar))
            ShowWindow(st->hListScrollbar, SW_SHOWNOACTIVATE);
    }
}

/*
 * Read form fields into *pr.
 * Returns 1 on success, 0 if host is empty (caller shows error).
 */
static int form_read(HWND hwnd, Profile *pr)
{
    int kind_idx  = (int)SendMessage(GetDlgItem(hwnd, IDC_COMBO_KIND),
                                     CB_GETCURSEL, 0, 0);
    BOOL is_local = (kind_idx == 1);

    GetDlgItemTextA(hwnd, IDC_EDIT_NAME, pr->name, sizeof(pr->name));

    if (is_local) {
        snprintf(pr->kind, sizeof(pr->kind), "%s", "local");
        GetDlgItemTextA(hwnd, IDC_EDIT_SHELL, pr->shell, sizeof(pr->shell));
        GetDlgItemTextA(hwnd, IDC_EDIT_AI_NOTES, pr->ai_notes, sizeof(pr->ai_notes));

        int plat_idx = (int)SendMessage(GetDlgItem(hwnd, IDC_COMBO_PLATFORM),
                                        CB_GETCURSEL, 0, 0);
        const char *plat_tok = cmd_platform_choice_name(plat_idx);
        snprintf(pr->platform, sizeof(pr->platform), "%s", plat_tok ? plat_tok : "auto");

        pr->port      = 22;
        pr->auth_type = AUTH_PASSWORD;
        return 1;
    }

    snprintf(pr->kind, sizeof(pr->kind), "%s", "ssh");
    pr->shell[0] = '\0';

    GetDlgItemTextA(hwnd, IDC_EDIT_HOST, pr->host, sizeof(pr->host));
    if (pr->host[0] == '\0') {
        return 0;
    }
    GetDlgItemTextA(hwnd, IDC_EDIT_USER,    pr->username, sizeof(pr->username));
    GetDlgItemTextA(hwnd, IDC_EDIT_PASS,    pr->password, sizeof(pr->password));
    GetDlgItemTextA(hwnd, IDC_EDIT_KEYPATH, pr->key_path, sizeof(pr->key_path));
    GetDlgItemTextA(hwnd, IDC_EDIT_AI_NOTES, pr->ai_notes, sizeof(pr->ai_notes));

    int auth_idx  = (int)SendMessage(GetDlgItem(hwnd, IDC_COMBO_AUTH),
                                     CB_GETCURSEL, 0, 0);
    pr->auth_type = (auth_idx == 1) ? AUTH_KEY : AUTH_PASSWORD;

    int plat_idx = (int)SendMessage(GetDlgItem(hwnd, IDC_COMBO_PLATFORM),
                                    CB_GETCURSEL, 0, 0);
    const char *plat_tok = cmd_platform_choice_name(plat_idx);
    snprintf(pr->platform, sizeof(pr->platform), "%s", plat_tok ? plat_tok : "auto");

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

        /* Session type: SSH (default) or Local shell */
        HWND hKind = GetDlgItem(hwnd, IDC_COMBO_KIND);
        SendMessageA(hKind, CB_ADDSTRING, 0, (LPARAM)"SSH");
        SendMessageA(hKind, CB_ADDSTRING, 0, (LPARAM)"Local shell");
        SendMessage (hKind, CB_SETCURSEL, 0, 0);

        shell_combo_populate(hwnd, st);

        /* Device platform: populated entirely from cmd_classify's table, so
         * adding a vendor later touches one place there and nothing here. */
        HWND hPlatform = GetDlgItem(hwnd, IDC_COMBO_PLATFORM);
        int plat_count = cmd_platform_choice_count();
        for (int i = 0; i < plat_count; i++) {
            const char *label = cmd_platform_choice_label(i);
            SendMessageA(hPlatform, CB_ADDSTRING, 0, (LPARAM)(label ? label : ""));
        }
        SendMessage(hPlatform, CB_SETCURSEL, 0, 0);

        /* Limit AI Notes to ~400 words (2559 chars) */
        SendDlgItemMessage(hwnd, IDC_EDIT_AI_NOTES, EM_SETLIMITTEXT, 2559, 0);
        SendMessage(GetDlgItem(hwnd, IDC_EDIT_AI_NOTES), EM_SETCUEBANNER, 0,
                    (LPARAM)L"Notes for AI about this server (max 400 words)");

        /* Match the password field's edit-box limit to Profile.password's
         * buffer size: form_read()'s GetDlgItemTextA() already truncates
         * safely at that size, but without this the box lets the user type
         * (and believe they saved) more than will ever actually be kept. */
        SendDlgItemMessage(hwnd, IDC_EDIT_PASS, EM_SETLIMITTEXT,
                            (WPARAM)(sizeof(((Profile *)0)->password) - 1u), 0);

        /* Theme: look up from config, create brushes, apply title bar + borders.
         * Must happen BEFORE font application, because WM_SETFONT with
         * fRedraw=TRUE triggers WM_CTLCOLOR* messages that need st->theme. */
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

        /* Apply configured font at UI size to all child controls.
         * Done after theme setup so WM_CTLCOLOR* repaints use correct colors. */
        {
            st->hDlgFont = ns_font(FONT_BODY, get_window_dpi(hwnd));
            if (st->hDlgFont) {
                HWND hChild = NULL;
                while ((hChild = FindWindowEx(hwnd, hChild, NULL, NULL)) != NULL)
                    SendMessage(hChild, WM_SETFONT, (WPARAM)st->hDlgFont, TRUE);
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
        toggle_kind_fields(hwnd);
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

        /* Session type combo changed */
        if (id == IDC_COMBO_KIND && ntf == CBN_SELCHANGE) {
            toggle_kind_fields(hwnd);
            return TRUE;
        }

        /* Shell combo: a row picked from the dropdown -- not the user
         * typing, which is CBN_EDITCHANGE and needs no help from here. */
        if (id == IDC_EDIT_SHELL && ntf == CBN_SELCHANGE) {
            shell_combo_selchange(hwnd, st);
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
                    toggle_kind_fields(hwnd);
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
            toggle_kind_fields(hwnd);
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
                toggle_kind_fields(hwnd);
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
            toggle_kind_fields(hwnd);
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
            int do_append = 0;
            if (st->edit_idx >= 0 &&
                (size_t)st->edit_idx < vec_size(&st->cfg->profiles)) {
                Profile *existing = (Profile *)vec_get(
                    &st->cfg->profiles, (size_t)st->edit_idx);
                if (strcmp(existing->name, tmp.name) != 0) {
                    char prompt[768];
                    snprintf(prompt, sizeof(prompt),
                        "The name has changed from \"%s\" to \"%s\".\n\n"
                        "Save as a new profile, or rename the existing one?\n\n"
                        "Yes  = save as a new profile\n"
                        "No   = rename the existing profile\n"
                        "Cancel = don't save",
                        existing->name[0] ? existing->name : "(unnamed)",
                        tmp.name[0]      ? tmp.name      : "(unnamed)");
                    int rc = MessageBoxA(hwnd, prompt, "Save Profile",
                                         MB_YESNOCANCEL | MB_ICONQUESTION);
                    if (rc == IDCANCEL) return TRUE;
                    do_append = (rc == IDYES);
                }
            } else {
                do_append = 1;
            }

            if (!do_append) {
                Profile *pr = (Profile *)vec_get(
                    &st->cfg->profiles, (size_t)st->edit_idx);
                /* form_read() never touches password_enc_preserved -- it
                 * isn't a form field -- so carry the existing profile's
                 * preserved blob (a password that could not be decrypted
                 * on this PC/user) forward. Otherwise every edit of this
                 * profile, even an unrelated field, would silently drop it.
                 * secret_prepare() (loader.c) ignores it the moment
                 * tmp.password is non-empty, so a real new password still
                 * always wins and replaces it. */
                memcpy(tmp.password_enc_preserved, pr->password_enc_preserved,
                       sizeof(tmp.password_enc_preserved));
                /* M-4: ... but if the user just typed a new password into
                 * the form, drop that carried-forward blob rather than
                 * leaving it sitting in tmp (and then pr) unused. Otherwise
                 * it lingers in memory, and a later save this same session
                 * that clears the password field again would resurrect the
                 * old foreign blob instead of writing "" like it should. */
                config_secret_drop_stale_preserved(tmp.password,
                    tmp.password_enc_preserved, sizeof(tmp.password_enc_preserved));
                *pr = tmp;
            } else {
                Profile *pr = config_profile_new();
                *pr = tmp;
                /* Duplicating a profile ("save as new", the Yes answer
                 * above): carry the source profile's preserved blob to the
                 * duplicate only when the password field was left
                 * untouched (still empty -- the box never shows an
                 * undecryptable blob's plaintext, so "untouched" and
                 * "empty" are the same thing here). If the user typed a
                 * new password for the duplicate, that new password always
                 * wins and there is nothing of the old blob worth keeping. */
                if (tmp.password[0] == '\0' && st->edit_idx >= 0 &&
                    (size_t)st->edit_idx < vec_size(&st->cfg->profiles)) {
                    const Profile *src = (const Profile *)vec_get(
                        &st->cfg->profiles, (size_t)st->edit_idx);
                    memcpy(pr->password_enc_preserved, src->password_enc_preserved,
                           sizeof(pr->password_enc_preserved));
                }
                vec_push(&st->cfg->profiles, pr);
                st->edit_idx = (int)vec_size(&st->cfg->profiles) - 1;
            }
            config_save(st->cfg, st->config_path);
            list_rebuild(hList, st->cfg);
            SendMessage(hList, LB_SETCURSEL, (WPARAM)st->edit_idx, 0);
            secure_zero(&tmp, sizeof(tmp));
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
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST
                              | OFN_NOCHANGEDIR;
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
            secure_zero(&tmp, sizeof(tmp));
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

    case WM_MOUSEWHEEL:
        if (st) {
            /* Forward wheel to AI notes edit */
            HWND hEdit = GetDlgItem(hwnd, IDC_EDIT_AI_NOTES);
            if (hEdit) {
                int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
                int scroll = edit_scroll_wheel_delta(zdelta, WHEEL_DELTA, 3);
                SendMessage(hEdit, EM_LINESCROLL, 0, (LPARAM)scroll);
                ai_notes_sync_scroll(hwnd, st);
            }
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
            if ((int)dis->CtlID == IDC_LIST_SESSIONS) {
                paint_session_row(dis, st->theme);
                return TRUE;
            }
            int is_primary = ((int)dis->CtlID == IDOK);
            draw_themed_button(dis, st->theme, is_primary);
            return TRUE;
        }
        break;

    case WM_MEASUREITEM:
        if ((int)wParam == IDC_LIST_SESSIONS) {
            LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lParam;
            int dpi = get_window_dpi(hwnd);
            if (dpi <= 0) dpi = 96;
            mis->itemHeight = (UINT)ns_scale(22, dpi);
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
            /* st->hDlgFont comes from the ns_font cache — owned there. */
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
    memset(&st, 0, sizeof(st));
    st.cfg         = cfg;
    st.config_path = config_path;
    st.out_profile = out_profile;
    st.edit_idx    = -1;

    INT_PTR result = DialogBoxParam(hInstance,
                                    MAKEINTRESOURCE(IDD_SESSION_MANAGER),
                                    parent, SessMgrDlgProc, (LPARAM)&st);
    return (result == IDOK) ? 1 : 0;
}
