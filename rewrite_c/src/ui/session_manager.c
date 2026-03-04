#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "session_manager.h"
#include "../config/profile.h"
#include "../config/config.h"
#include "../core/vector.h"
#include "resource.h"

/* ---- Internal state passed through GWLP_USERDATA ---- */

typedef struct {
    Config     *cfg;
    const char *config_path;
    Profile    *out_profile;
    int         edit_idx;   /* -1 = new entry; >= 0 = index in cfg->profiles */
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
