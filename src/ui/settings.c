#include "settings_dlg.h"
#include "resource.h"
#include "dpi_util.h"
#include "app_font.h"
#include "ui_theme.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "edit_scroll.h"
#include "ai_prompt.h"
#include "ai_http.h"
#include "json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <commctrl.h>

/* ---- Control IDs -------------------------------------------------------- */

#define IDC_FONT_COMBO      1001
#define IDC_FONTSIZE_COMBO  1002
#define IDC_SCROLLBACK_EDIT 1003
#define IDC_PASTEDELAY_EDIT 1004
#define IDC_LOG_DIR_EDIT    1010
#define IDC_LOG_FMT_EDIT    1011
#define IDC_AI_PROVIDER_COMBO 1013
#define IDC_AI_KEY_EDIT     1014
#define IDC_AI_CUSTOM_URL   1016
#define IDC_AI_CUSTOM_MODEL 1017
#define IDC_AI_REFRESH      1019
#define IDC_SCHEME_COMBO    1020
#define IDC_AI_SYSTEM_NOTES 1021
#define IDC_AI_FONT_COMBO   1022
#define WM_AI_MODELS_DONE   (WM_USER + 200)
#define IDT_SYSNOTES_SCROLL 51  /* timer for AI instructions scroll sync */

static const char *SETTINGS_CLASS = "Nutshell_Settings";

/* ---- Font list ---------------------------------------------------------- */

/* 10 SSH-compatible monospace fonts.
 * CBS_DROPDOWNLIST prevents free-text entry — only these may be chosen. */
static const char * const k_fonts[] = {
    "Cascadia Code",
    "Consolas",
    "Cascadia Mono",
    "Courier New",
    "Inter",
    "Lucida Console",
    "Lucida Sans Typewriter",
    "Fira Code",
    "JetBrains Mono",
    "Source Code Pro",
    "Hack",
};
#define NUM_FONTS ((int)(sizeof(k_fonts) / sizeof(k_fonts[0])))

/* ---- Discrete font sizes ------------------------------------------------ */

/* Use canonical table from app_font.h (no local copy). */
#define NUM_FONT_SIZES APP_FONT_NUM_SIZES

/* ---- AI provider list --------------------------------------------------- */

static const char * const k_ai_providers[] = {
    "anthropic",
    "openai",
    "gemini",
    "moonshot",
    "deepseek",
    "custom",
};
#define NUM_AI_PROVIDERS ((int)(sizeof(k_ai_providers) / sizeof(k_ai_providers[0])))

/* ---- Font availability check -------------------------------------------- */

/* EnumFontFamiliesExA callback: sets *(int*)lParam = 1 if the font exists. */
static int CALLBACK font_exists_cb(const LOGFONTA *lf, const TEXTMETRICA *tm,
                                    DWORD type, LPARAM lParam)
{
    (void)lf; (void)tm; (void)type;
    *(int *)lParam = 1;
    return 0; /* stop enumeration after first match */
}

/* Return non-zero if a font family is installed on this system. */
static int font_is_installed(const char *face_name)
{
    int found = 0;
    HDC hdc = GetDC(NULL);
    LOGFONTA lf;
    memset(&lf, 0, sizeof(lf));
    strncpy(lf.lfFaceName, face_name, LF_FACESIZE - 1);
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExA(hdc, &lf, (FONTENUMPROCA)font_exists_cb,
                        (LPARAM)&found, 0);
    ReleaseDC(NULL, hdc);
    return found;
}

/* ---- Dialog state ------------------------------------------------------- */

typedef struct {
    Config  *cfg;
    HWND     hTooltip;
    HFONT    hDlgFont;   /* MS Shell Dlg 8pt — applied to all child controls */
    const ThemeColors *theme;
    HBRUSH   hBrBgPrimary;
    HBRUSH   hBrBgSecondary;
    int      dpi;
    HWND     hSysNotesScroll; /* custom scrollbar for AI instructions */
    int      sys_notes_line_h; /* cached line height in px */
} SettingsDlgData;

/* ---- Layout helpers ----------------------------------------------------- */

static HWND make_label(HWND parent, const char *text, int x, int y, int w, int dpi)
{
    int lh = MulDiv(18, dpi, 96);
    return CreateWindow("STATIC", text,
        WS_VISIBLE | WS_CHILD | SS_RIGHT,
        x, y + lh / 6, w, lh, parent, NULL, NULL, NULL);
}

static HWND make_edit(HWND parent, const char *text,
                      int x, int y, int w, HMENU id, int dpi)
{
    int eh = MulDiv(22, dpi, 96);
    HWND h = CreateWindow("EDIT", text,
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        x, y + 1, w, eh, parent, id, NULL, NULL);
    SendMessage(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELPARAM(3, 3));
    return h;
}

/* Drop-down list combo.  drop_h = total window height including dropdown. */
static HWND make_combo(HWND parent, int x, int y, int w, int drop_h, HMENU id)
{
    return CreateWindow("COMBOBOX", "",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        x, y, w, drop_h, parent, id, NULL, NULL);
}

/* EnumChildWindows callback: send WM_SETFONT to every child control. */
static BOOL CALLBACK SetFontProc(HWND hChild, LPARAM lParam)
{
    SendMessage(hChild, WM_SETFONT, (WPARAM)(HFONT)lParam, (LPARAM)TRUE);
    return TRUE;
}

/* ---- Fetch models thread ------------------------------------------------ */

typedef struct {
    HWND  hwnd;        /* settings window to post result to */
    char  url[512];    /* models endpoint URL */
    char  headers[3][300]; /* raw HTTP headers (NULL-terminated array) */
    char *result;      /* heap-allocated newline-separated model IDs, or error */
} FetchModelsCtx;

static DWORD WINAPI fetch_models_thread(LPVOID param)
{
    FetchModelsCtx *ctx = (FetchModelsCtx *)param;

    /* Build NULL-terminated header pointer array */
    const char *hdrs[4] = {NULL, NULL, NULL, NULL};
    for (int i = 0; i < 3 && ctx->headers[i][0]; i++)
        hdrs[i] = ctx->headers[i];

    AiHttpResponse resp;
    int rc = ai_http_get(ctx->url, hdrs[0] ? hdrs : NULL, &resp);

    if (rc != 0 || resp.status_code < 200 || resp.status_code >= 300) {
        char err[512];
        if (resp.error[0])
            snprintf(err, sizeof(err), "Error: %s", resp.error);
        else
            snprintf(err, sizeof(err), "Error: HTTP %d", resp.status_code);
        ctx->result = _strdup(err);
        ai_http_response_free(&resp);
        PostMessage(ctx->hwnd, WM_AI_MODELS_DONE, 0, (LPARAM)ctx);
        return 0;
    }

    /* Parse JSON: {"data": [{"id": "model-name"}, ...]} */
    JsonNode *root = json_parse(resp.body);
    ai_http_response_free(&resp);

    if (!root) {
        ctx->result = _strdup("Error: invalid JSON response");
        PostMessage(ctx->hwnd, WM_AI_MODELS_DONE, 0, (LPARAM)ctx);
        return 0;
    }

    JsonNode *data = json_obj_get(root, "data");
    if (!data || data->type != JSON_ARRAY) {
        json_free(root);
        ctx->result = _strdup("Error: no 'data' array in response");
        PostMessage(ctx->hwnd, WM_AI_MODELS_DONE, 0, (LPARAM)ctx);
        return 0;
    }

    /* Build newline-separated list of model IDs */
    size_t cap = 4096;
    char *buf = malloc(cap);
    if (!buf) {
        json_free(root);
        ctx->result = _strdup("Error: out of memory");
        PostMessage(ctx->hwnd, WM_AI_MODELS_DONE, 0, (LPARAM)ctx);
        return 0;
    }
    size_t pos = 0;
    size_t count = vec_size(&data->as.arr);
    for (size_t i = 0; i < count; i++) {
        JsonNode *item = (JsonNode *)vec_get(&data->as.arr, i);
        if (!item || item->type != JSON_OBJECT) continue;
        const char *id = json_obj_str(item, "id");
        if (!id || !id[0]) continue;
        size_t len = strlen(id);
        if (pos + len + 2 > cap) {
            cap = (pos + len + 2) * 2;
            char *nb = realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
        memcpy(buf + pos, id, len);
        pos += len;
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    json_free(root);

    ctx->result = buf;
    PostMessage(ctx->hwnd, WM_AI_MODELS_DONE, 0, (LPARAM)ctx);
    return 0;
}

/* Sync AI instructions edit scroll state to custom scrollbar. */
static void sys_notes_sync_scroll(HWND hwnd, SettingsDlgData *d)
{
    HWND hEdit = GetDlgItem(hwnd, IDC_AI_SYSTEM_NOTES);
    csb_sync_edit(hEdit, d->hSysNotesScroll, d->sys_notes_line_h);
}

/* ---- Window procedure --------------------------------------------------- */

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam)
{
    SettingsDlgData *d = (SettingsDlgData *)(LONG_PTR)
                         GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
        SettingsDlgData *nd = (SettingsDlgData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)nd);

        /* Get per-monitor DPI for layout scaling */
        nd->dpi = get_window_dpi(hwnd);
        #define S(px) MulDiv((px), nd->dpi, 96)

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;

        /* Column geometry — DPI-scaled */
        int lx = S(10);    /* label x       */
        int lw = S(120);   /* label width    */
        int ex = S(135);   /* control x      */
        int ew = S(200);   /* default edit w */

        int y = S(10);  /* current row y position */
        int rh = S(28); /* row height */

        /* Row 1: Terminal Font */
        make_label(hwnd, "Terminal Font:", lx, y, lw, nd->dpi);
        {
            HWND hCombo = make_combo(hwnd, ex, y, ew, S(200), (HMENU)IDC_FONT_COMBO);
            int sel = 0, idx = 0;
            for (int i = 0; i < NUM_FONTS; i++) {
                if (!font_is_installed(k_fonts[i])) continue;
                SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)k_fonts[i]);
                if (_stricmp(nd->cfg->settings.font, k_fonts[i]) == 0)
                    sel = idx;
                idx++;
            }
            SendMessage(hCombo, CB_SETCURSEL, (WPARAM)sel, 0);
        }
        y += rh;

        /* Row 2: AI Assist Font */
        make_label(hwnd, "AI Assist Font:", lx, y, lw, nd->dpi);
        {
            HWND hCombo = make_combo(hwnd, ex, y, ew, S(200), (HMENU)IDC_AI_FONT_COMBO);
            int sel = 0, idx = 0;
            for (int i = 0; i < NUM_FONTS; i++) {
                if (!font_is_installed(k_fonts[i])) continue;
                SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)k_fonts[i]);
                if (_stricmp(nd->cfg->settings.ai_font, k_fonts[i]) == 0)
                    sel = idx;
                idx++;
            }
            SendMessage(hCombo, CB_SETCURSEL, (WPARAM)sel, 0);
        }
        y += rh;

        /* Row 3: Font size */
        make_label(hwnd, "Font Size:", lx, y, lw, nd->dpi);
        {
            HWND hSz = make_combo(hwnd, ex, y, S(80), S(180), (HMENU)IDC_FONTSIZE_COMBO);
            int sel = 0;
            for (int i = 0; i < NUM_FONT_SIZES; i++) {
                char buf[8];
                (void)snprintf(buf, sizeof(buf), "%d", k_app_font_sizes[i]);
                SendMessage(hSz, CB_ADDSTRING, 0, (LPARAM)buf);
                if (k_app_font_sizes[i] == nd->cfg->settings.font_size)
                    sel = i;
            }
            SendMessage(hSz, CB_SETCURSEL, (WPARAM)sel, 0);
        }
        y += rh;

        /* Row 3: Scrollback lines */
        {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d",
                           nd->cfg->settings.scrollback_lines);
            make_label(hwnd, "Scrollback Lines:", lx, y, lw, nd->dpi);
            make_edit(hwnd, buf, ex, y, S(80), (HMENU)IDC_SCROLLBACK_EDIT, nd->dpi);
        }
        y += rh;

        /* Row 4: Paste delay */
        {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d",
                           nd->cfg->settings.paste_delay_ms);
            make_label(hwnd, "Paste Delay (ms):", lx, y, lw, nd->dpi);
            make_edit(hwnd, buf, ex, y, S(80), (HMENU)IDC_PASTEDELAY_EDIT, nd->dpi);
        }
        y += rh;

        /* Row 5: Colour Scheme */
        make_label(hwnd, "Colour Scheme:", lx, y, lw, nd->dpi);
        {
            HWND hScheme = make_combo(hwnd, ex, y, ew, S(150), (HMENU)IDC_SCHEME_COMBO);
            int sel = 0;
            for (int i = 0; i < NUM_UI_THEMES; i++) {
                SendMessage(hScheme, CB_ADDSTRING, 0,
                            (LPARAM)ui_theme_name(i));
                if (_stricmp(nd->cfg->settings.colour_scheme,
                             ui_theme_name(i)) == 0)
                    sel = i;
            }
            SendMessage(hScheme, CB_SETCURSEL, (WPARAM)sel, 0);
        }
        y += rh;

        /* Row 6: Log directory */
        make_label(hwnd, "Log Directory:", lx, y, lw, nd->dpi);
        make_edit(hwnd, nd->cfg->settings.log_dir,
                  ex, y, ew, (HMENU)IDC_LOG_DIR_EDIT, nd->dpi);
        y += rh;

        /* Row 6: Log name format */
        make_label(hwnd, "Log Name Format:", lx, y, lw, nd->dpi);
        make_edit(hwnd, nd->cfg->settings.log_format,
                  ex, y, ew, (HMENU)IDC_LOG_FMT_EDIT, nd->dpi);

        /* Tooltip on the log format edit: list strftime specifiers */
        nd->hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | TTS_BALLOON,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        if (nd->hTooltip) {
            TOOLINFO ti;
            memset(&ti, 0, sizeof(ti));
            ti.cbSize   = sizeof(TOOLINFO);
            ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd     = hwnd;
            ti.uId      = (UINT_PTR)GetDlgItem(hwnd, IDC_LOG_FMT_EDIT);
            ti.lpszText = "%Y  4-digit year (e.g. 2026)\r\n"
                          "%m  month (01-12)\r\n"
                          "%d  day   (01-31)\r\n"
                          "%H  hour  (00-23)\r\n"
                          "%M  minute (00-59)\r\n"
                          "%S  second (00-59)\r\n"
                          "Example: session-%Y%m%d_%H%M%S";
            SendMessage(nd->hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)300);
        }

        /* Row 7: AI API Key (masked) — between log format and AI provider */
        y += rh;
        make_label(hwnd, "AI API Key:", lx, y, lw, nd->dpi);
        {
            HWND hKey = CreateWindow("EDIT",
                nd->cfg->settings.ai_api_key,
                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                ex, y + 1, ew, S(22), hwnd, (HMENU)IDC_AI_KEY_EDIT, NULL, NULL);
            SendMessage(hKey, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                        MAKELPARAM(3, 3));
        }

        /* Row 8: AI Provider */
        y += rh + S(5); /* small gap before AI section */
        int is_custom = (_stricmp(nd->cfg->settings.ai_provider, "custom") == 0);
        make_label(hwnd, "AI Provider:", lx, y, lw, nd->dpi);
        {
            HWND hAi = make_combo(hwnd, ex, y, ew, S(150), (HMENU)IDC_AI_PROVIDER_COMBO);
            int sel = 0;
            for (int i = 0; i < NUM_AI_PROVIDERS; i++) {
                SendMessage(hAi, CB_ADDSTRING, 0, (LPARAM)k_ai_providers[i]);
                if (_stricmp(nd->cfg->settings.ai_provider,
                             k_ai_providers[i]) == 0)
                    sel = i;
            }
            SendMessage(hAi, CB_SETCURSEL, (WPARAM)sel, 0);
        }
        y += rh;

        /* Row 9: AI Model (combo + refresh button) */
        {
            int model_w = ew - S(30); /* leave room for refresh button */
            make_label(hwnd, "AI Model:", lx, y, lw, nd->dpi);
            HWND hModel = CreateWindow("COMBOBOX", "",
                WS_VISIBLE | WS_CHILD | CBS_DROPDOWN | CBS_HASSTRINGS | WS_VSCROLL,
                ex, y, model_w, S(180), hwnd, (HMENU)IDC_AI_CUSTOM_MODEL, NULL, NULL);
            /* Refresh button (Unicode ↻) — owner-drawn for theme */
            CreateWindowW(L"BUTTON", L"\x21BB",
                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                ex + model_w + S(5), y, S(25), S(22), hwnd, (HMENU)IDC_AI_REFRESH, NULL, NULL);
            /* Show saved model if one was previously chosen; otherwise leave
             * empty until the user presses refresh */
            const char *cur_model = nd->cfg->settings.ai_custom_model;
            if (cur_model && cur_model[0])
                SetWindowText(hModel, cur_model);
        }
        y += rh;

        /* Row 10: AI Base URL (only for custom provider) */
        {
            HWND hUrlLabel = make_label(hwnd, "AI Base URL:", lx, y, lw, nd->dpi);
            HWND hUrl = CreateWindow("EDIT",
                nd->cfg->settings.ai_custom_url,
                WS_CHILD | WS_BORDER | ES_AUTOHSCROLL |
                (is_custom ? WS_VISIBLE : 0),
                ex, y + 1, ew, S(22), hwnd, (HMENU)IDC_AI_CUSTOM_URL, NULL, NULL);
            SendMessage(hUrl, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                        MAKELPARAM(3, 3));
            if (!is_custom) ShowWindow(hUrlLabel, SW_HIDE);
        }
        y += rh;

        /* Row 11: AI System Instructions (multiline, two-line label) */
        {
            int lbl_h = MulDiv(30, nd->dpi, 96); /* two lines */
            CreateWindow("STATIC", "System Wide\r\nAI Instructions:",
                WS_VISIBLE | WS_CHILD | SS_RIGHT,
                S(2), y, ex - S(6), lbl_h, hwnd, NULL, NULL, NULL);
        }
        {
            int edit_w = ew - CSB_WIDTH;
            int edit_h = S(132);
            HWND hSysNotes = CreateWindow("EDIT",
                nd->cfg->settings.ai_system_notes,
                WS_VISIBLE | WS_CHILD | WS_BORDER |
                ES_MULTILINE | ES_AUTOVSCROLL,
                ex, y + 1, edit_w, edit_h, hwnd, (HMENU)IDC_AI_SYSTEM_NOTES, NULL, NULL);
            SendMessage(hSysNotes, EM_SETLIMITTEXT, (WPARAM)2559, 0);
            SendMessage(hSysNotes, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                        MAKELPARAM(3, 3));
            SendMessageW(hSysNotes, EM_SETCUEBANNER, 0,
                         (LPARAM)L"System-wide AI instructions (max 400 words)");
        }

        /* Action buttons (owner-drawn for theme) */
        CreateWindow("BUTTON", "Save",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw / 2 - S(80), ch - S(65), S(75), S(25), hwnd, (HMENU)IDOK, NULL, NULL);
        CreateWindow("BUTTON", "Cancel",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw / 2 + S(5), ch - S(65), S(75), S(25), hwnd, (HMENU)IDCANCEL, NULL, NULL);

        /* Version / copyright footer */
        CreateWindow("STATIC", "Nutshell v" APP_VERSION,
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, ch - S(35), cw, S(16), hwnd, NULL, NULL, NULL);
        CreateWindow("STATIC", "Copyright (C) 2026 Thomas Sulkiewicz",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, ch - S(19), cw, S(16), hwnd, NULL, NULL, NULL);

        #undef S

        /* Apply Inter UI font to all child controls */
        {
            int h = -MulDiv(APP_FONT_UI_SIZE, nd->dpi, 72);
            nd->hDlgFont = CreateFont(
                h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                APP_FONT_UI_FACE);
            if (nd->hDlgFont)
                EnumChildWindows(hwnd, SetFontProc, (LPARAM)nd->hDlgFont);
        }

        /* Theme: look up from config, create brushes, apply title bar + borders */
        {
            int idx = ui_theme_find(nd->cfg->settings.colour_scheme);
            nd->theme = ui_theme_get(idx);
            nd->hBrBgPrimary   = CreateSolidBrush(theme_cr(nd->theme->bg_primary));
            nd->hBrBgSecondary = CreateSolidBrush(theme_cr(nd->theme->bg_secondary));
            themed_apply_title_bar(hwnd, nd->theme);
            themed_apply_borders(hwnd, nd->theme);
        }

        /* Custom scrollbar for AI instructions multiline edit */
        {
            HWND hEdit = GetDlgItem(hwnd, IDC_AI_SYSTEM_NOTES);
            if (hEdit) {
                /* Measure line height from applied font */
                HDC hdc = GetDC(hEdit);
                HGDIOBJ old = SelectObject(hdc, (HGDIOBJ)nd->hDlgFont);
                TEXTMETRIC tm;
                GetTextMetrics(hdc, &tm);
                nd->sys_notes_line_h = tm.tmHeight + tm.tmExternalLeading;
                if (nd->sys_notes_line_h < 1) nd->sys_notes_line_h = 16;
                SelectObject(hdc, old);
                ReleaseDC(hEdit, hdc);

                /* Position scrollbar on right edge of edit */
                RECT erc;
                GetWindowRect(hEdit, &erc);
                POINT pt = { erc.right, erc.top };
                ScreenToClient(hwnd, &pt);
                int eh = erc.bottom - erc.top;

                csb_register(GetModuleHandle(NULL));
                nd->hSysNotesScroll = csb_create(hwnd, pt.x, pt.y,
                                                 CSB_WIDTH, eh, nd->theme,
                                                 GetModuleHandle(NULL));
                SetTimer(hwnd, IDT_SYSNOTES_SCROLL, 50, NULL);
            }
        }

        return 0;
    }

    case WM_AI_MODELS_DONE: {
        FetchModelsCtx *ctx = (FetchModelsCtx *)lParam;
        if (ctx && ctx->result) {
            /* Re-enable the refresh button */
            EnableWindow(GetDlgItem(hwnd, IDC_AI_REFRESH), TRUE);

            if (strncmp(ctx->result, "Error:", 6) == 0) {
                MessageBox(hwnd, ctx->result, "Refresh Models",
                           MB_OK | MB_ICONWARNING);
            } else {
                /* Parse newline-separated model IDs into the combo */
                HWND hCombo = GetDlgItem(hwnd, IDC_AI_CUSTOM_MODEL);
                /* Remember current text */
                char cur[256] = {0};
                GetWindowText(hCombo, cur, (int)sizeof(cur));

                SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
                char *line = ctx->result;
                int added = 0;
                while (*line) {
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = '\0';
                    if (line[0]) {
                        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)line);
                        added++;
                    }
                    if (!nl) break;
                    line = nl + 1;
                }
                /* Try to re-select previous model, or first item */
                int idx = (int)SendMessage(hCombo, CB_FINDSTRINGEXACT,
                                           (WPARAM)-1, (LPARAM)cur);
                SendMessage(hCombo, CB_SETCURSEL,
                            (WPARAM)(idx >= 0 ? idx : 0), 0);

                if (added == 0)
                    MessageBox(hwnd, "No models found.", "Refresh Models",
                               MB_OK | MB_ICONINFORMATION);
            }
            free(ctx->result);
        }
        free(ctx);
        return 0;
    }

    case WM_VSCROLL:
        /* Custom scrollbar for AI instructions */
        if (d && d->hSysNotesScroll && (HWND)lParam == d->hSysNotesScroll) {
            WORD code = LOWORD(wParam);
            HWND hEdit = GetDlgItem(hwnd, IDC_AI_SYSTEM_NOTES);
            int first = (int)SendMessage(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
            int delta = 0;
            switch (code) {
            case SB_LINEUP:    delta = -1; break;
            case SB_LINEDOWN:  delta =  1; break;
            case SB_PAGEUP:    delta = -3; break;
            case SB_PAGEDOWN:  delta =  3; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                delta = edit_scroll_line_delta(
                    csb_get_trackpos(d->hSysNotesScroll), first);
                break;
            case SB_TOP:       delta = -first; break;
            case SB_BOTTOM:    delta = 99999;  break;
            }
            if (delta != 0)
                SendMessage(hEdit, EM_LINESCROLL, 0, (LPARAM)delta);
            sys_notes_sync_scroll(hwnd, d);
            return 0;
        }
        break;

    case WM_MOUSEWHEEL:
        if (d) {
            HWND hEdit = GetDlgItem(hwnd, IDC_AI_SYSTEM_NOTES);
            if (hEdit) {
                int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
                int scroll = edit_scroll_wheel_delta(zdelta, WHEEL_DELTA, 3);
                SendMessage(hEdit, EM_LINESCROLL, 0, (LPARAM)scroll);
                sys_notes_sync_scroll(hwnd, d);
            }
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == IDT_SYSNOTES_SCROLL && d) {
            sys_notes_sync_scroll(hwnd, d);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        if (d && d->theme) {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, d->hBrBgPrimary);
            return 1;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_primary));
            return (LRESULT)d->hBrBgPrimary;
        }
        break;

    case WM_CTLCOLOREDIT:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_secondary));
            return (LRESULT)d->hBrBgSecondary;
        }
        break;

    case WM_CTLCOLORLISTBOX:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_secondary));
            return (LRESULT)d->hBrBgSecondary;
        }
        break;

    case WM_DRAWITEM:
        if (d && d->theme) {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            int is_primary = ((int)dis->CtlID == IDOK);
            draw_themed_button(dis, d->theme, is_primary);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case IDC_AI_PROVIDER_COMBO:
            /* Show/hide custom URL field; repopulate model combo */
            if (HIWORD(wParam) == CBN_SELCHANGE && d) {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_AI_PROVIDER_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_AI_PROVIDERS) {
                    int cust = (_stricmp(k_ai_providers[sel], "custom") == 0);
                    int sw = cust ? SW_SHOW : SW_HIDE;
                    ShowWindow(GetDlgItem(hwnd, IDC_AI_CUSTOM_URL), sw);
                    HWND hUrlLbl = FindWindowEx(hwnd, NULL, "STATIC", "AI Base URL:");
                    if (hUrlLbl) ShowWindow(hUrlLbl, sw);
                    /* Clear model — user must press refresh to populate */
                    HWND hMdl = GetDlgItem(hwnd, IDC_AI_CUSTOM_MODEL);
                    SendMessage(hMdl, CB_RESETCONTENT, 0, 0);
                    SetWindowText(hMdl, "");
                }
            }
            break;

        case IDC_AI_REFRESH:
            /* Fetch available models from the provider's API */
            if (d) {
                /* Get current provider */
                int psel = (int)SendDlgItemMessage(hwnd, IDC_AI_PROVIDER_COMBO,
                                                   CB_GETCURSEL, 0, 0);
                const char *prov = (psel >= 0 && psel < NUM_AI_PROVIDERS)
                                   ? k_ai_providers[psel] : "";
                const char *models_url = ai_provider_models_url(prov);

                /* For custom provider, derive models URL from base URL */
                char custom_models_url[512] = {0};
                if (!models_url) {
                    char base[256];
                    GetDlgItemText(hwnd, IDC_AI_CUSTOM_URL, base, (int)sizeof(base));
                    if (base[0]) {
                        /* Strip /chat/completions or similar, append /models */
                        char *slash = strrchr(base, '/');
                        if (slash && slash != base) {
                            /* Try to find /v1/ or similar prefix */
                            char *v1 = strstr(base, "/v1/");
                            if (v1)
                                snprintf(custom_models_url, sizeof(custom_models_url),
                                         "%.*s/v1/models", (int)(v1 - base), base);
                            else
                                snprintf(custom_models_url, sizeof(custom_models_url),
                                         "%.*s/models", (int)(slash - base), base);
                        }
                        models_url = custom_models_url;
                    }
                }

                if (!models_url || !models_url[0]) {
                    MessageBox(hwnd, "Cannot determine models URL for this provider.",
                               "Refresh Models", MB_OK | MB_ICONWARNING);
                    break;
                }

                /* Get API key */
                char api_key[256];
                GetDlgItemText(hwnd, IDC_AI_KEY_EDIT, api_key, (int)sizeof(api_key));
                if (!api_key[0]) {
                    MessageBox(hwnd, "Enter an API key first.",
                               "Refresh Models", MB_OK | MB_ICONWARNING);
                    break;
                }

                /* Launch background thread */
                FetchModelsCtx *ctx = calloc(1, sizeof(FetchModelsCtx));
                if (ctx) {
                    ctx->hwnd = hwnd;
                    snprintf(ctx->url, sizeof(ctx->url), "%s", models_url);

                    /* Build provider-appropriate auth headers:
                     * Anthropic uses x-api-key + anthropic-version,
                     * all others use Authorization: Bearer */
                    if (_stricmp(prov, "anthropic") == 0) {
                        snprintf(ctx->headers[0], sizeof(ctx->headers[0]),
                                 "x-api-key: %s", api_key);
                        snprintf(ctx->headers[1], sizeof(ctx->headers[1]),
                                 "anthropic-version: 2023-06-01");
                    } else {
                        snprintf(ctx->headers[0], sizeof(ctx->headers[0]),
                                 "Authorization: Bearer %s", api_key);
                    }

                    HANDLE ht = CreateThread(NULL, 0, fetch_models_thread,
                                             ctx, 0, NULL);
                    if (ht) {
                        CloseHandle(ht);
                        /* Disable button while fetching */
                        EnableWindow(GetDlgItem(hwnd, IDC_AI_REFRESH), FALSE);
                    } else {
                        free(ctx);
                    }
                }
            }
            break;

        case IDOK: {
            if (!d) { DestroyWindow(hwnd); break; }
            Settings *s = &d->cfg->settings;

            /* Terminal font from combo */
            {
                char buf[CFG_STR_MAX];
                GetDlgItemText(hwnd, IDC_FONT_COMBO, buf, (int)sizeof(buf));
                if (buf[0])
                    snprintf(s->font, sizeof(s->font), "%s", buf);
            }

            /* AI Assist font from combo */
            {
                char buf[CFG_STR_MAX];
                GetDlgItemText(hwnd, IDC_AI_FONT_COMBO, buf, (int)sizeof(buf));
                if (buf[0])
                    snprintf(s->ai_font, sizeof(s->ai_font), "%s", buf);
            }

            /* Font size from discrete combo */
            {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_FONTSIZE_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_FONT_SIZES)
                    s->font_size = k_app_font_sizes[sel];
            }

            /* Numeric fields; keep existing value on parse failure */
            BOOL ok;
            UINT v;

            v = GetDlgItemInt(hwnd, IDC_SCROLLBACK_EDIT, &ok, FALSE);
            if (ok) s->scrollback_lines = (int)v;

            v = GetDlgItemInt(hwnd, IDC_PASTEDELAY_EDIT, &ok, FALSE);
            if (ok) s->paste_delay_ms = (int)v;

            /* Log directory & format */
            GetDlgItemText(hwnd, IDC_LOG_DIR_EDIT,
                           s->log_dir, (int)sizeof(s->log_dir));
            GetDlgItemText(hwnd, IDC_LOG_FMT_EDIT,
                           s->log_format, (int)sizeof(s->log_format));

            /* AI provider from combo */
            {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_AI_PROVIDER_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_AI_PROVIDERS) {
                    strncpy(s->ai_provider, k_ai_providers[sel],
                            sizeof(s->ai_provider) - 1);
                    s->ai_provider[sizeof(s->ai_provider) - 1] = '\0';
                }
            }

            /* AI API key */
            GetDlgItemText(hwnd, IDC_AI_KEY_EDIT,
                           s->ai_api_key, (int)sizeof(s->ai_api_key));

            /* AI custom URL */
            GetDlgItemText(hwnd, IDC_AI_CUSTOM_URL,
                           s->ai_custom_url, (int)sizeof(s->ai_custom_url));

            /* AI model — read combo text (works for both selection and free-text) */
            GetDlgItemText(hwnd, IDC_AI_CUSTOM_MODEL,
                           s->ai_custom_model, (int)sizeof(s->ai_custom_model));

            /* Colour scheme from combo */
            {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_SCHEME_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_UI_THEMES) {
                    strncpy(s->colour_scheme, ui_theme_name(sel),
                            sizeof(s->colour_scheme) - 1);
                    s->colour_scheme[sizeof(s->colour_scheme) - 1] = '\0';
                }
            }

            /* AI system-wide instructions */
            GetDlgItemText(hwnd, IDC_AI_SYSTEM_NOTES,
                           s->ai_system_notes, (int)sizeof(s->ai_system_notes));

            /* Clamp out-of-range values before persisting */
            settings_validate(s);
            config_save(d->cfg, CONFIG_FILENAME);
            DestroyWindow(hwnd);
            break;
        }

        case IDCANCEL:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        if (d) {
            KillTimer(hwnd, IDT_SYSNOTES_SCROLL);
            if (d->hTooltip) DestroyWindow(d->hTooltip);
            if (d->hDlgFont) DeleteObject(d->hDlgFont);
            if (d->hBrBgPrimary)   DeleteObject(d->hBrBgPrimary);
            if (d->hBrBgSecondary) DeleteObject(d->hBrBgSecondary);
            free(d);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)NULL);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ---- Public API --------------------------------------------------------- */

void settings_dlg_show(HWND parent, Config *cfg)
{
    if (!cfg) return;

    /* Allocate dialog state — freed in WM_DESTROY */
    SettingsDlgData *d = (SettingsDlgData *)calloc(1u, sizeof(SettingsDlgData));
    if (!d) return;
    d->cfg = cfg;

    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = SETTINGS_CLASS;
    RegisterClassEx(&wc);

    /* Scale window size for DPI */
    int pdpi = get_window_dpi(parent);

    HWND hwnd = CreateWindowEx(
        0, SETTINGS_CLASS, "Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        MulDiv(400, pdpi, 96), MulDiv(714, pdpi, 96),
        parent, NULL, GetModuleHandle(NULL), d);

    if (hwnd) {
        EnableWindow(parent, FALSE);
        MSG msg;
        while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        EnableWindow(parent, TRUE);
        SetFocus(parent);
    } else {
        free(d);
    }
}

#endif
