#include "settings_dlg.h"
#include "resource.h"
#include "dpi_util.h"
#include "app_font.h"
#include "ui_theme.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "edit_scroll.h"
#include "settings_layout.h"
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
#define IDC_SSH_IDLE_EDIT   1030
#define WM_AI_MODELS_DONE   (WM_USER + 200)
#define IDT_SYSNOTES_SCROLL 51  /* timer for AI instructions scroll sync */

static const char *SETTINGS_CLASS = "Nutshell_Settings";
static const char *PAGE_CLASS     = "Nutshell_SettingsPage";

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

/* ---- AI search provider list -------------------------------------------- */

typedef struct {
    const char *label;
    const char *value;
} SearchProviderEntry;

static const SearchProviderEntry k_search_providers[] = {
    { "None",                 "none"           },
    { "DuckDuckGo (API)",     "duckduckgo-api" },
    { "DuckDuckGo (HTML)",    "duckduckgo-html"},
    { "Custom",               "custom"         },
};
#define NUM_SEARCH_PROVIDERS \
    ((int)(sizeof(k_search_providers) / sizeof(k_search_providers[0])))

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

/* ---- Page control table --------------------------------------------------
 * Every control on every page is created once as a child of the page host
 * and positioned by relayout() (see below) from this table. See
 * docs/superpowers/specs/2026-08-29-settings-window-redesign-design.md. */

typedef struct {
    HWND hLabel;    /* right-aligned label, or NULL (checkboxes / statics) */
    HWND hCtrl;     /* the control itself */
    HWND hExtra;    /* optional second control on the same row (refresh button), or NULL */
    int  page;      /* SETTINGS_PAGE_* */
    int  ctrl_w;    /* 96-dpi width hint; 0 = stretch to the content pane */
    int  extra_w;   /* 96-dpi width reserved on the right (hExtra, or a
                      * dedicated scrollbar with no companion control) */
    int  rows;      /* minimum vertical row span (1 for a normal row) */
    int  stretch_v; /* 1 = absorb leftover vertical space on this page */
    int  visible;   /* conditional visibility (custom URL fields) */
    int  full_w;    /* 1 = no label column; control spans the whole pane */
} SettingsCtrl;

/* Raise this when a page outgrows it; add_ctrl refuses to overflow. */
#define MAX_SETTINGS_CTRLS 48

/* ---- Dialog state ------------------------------------------------------- */

typedef struct {
    Config  *cfg;
    HWND     hTooltip;
    HFONT    hDlgFont;   /* MS Shell Dlg 8pt — applied to all child controls */
    HFONT    hBoldFont;  /* same face, bold — nav headers + breadcrumb title */
    const ThemeColors *theme;
    HBRUSH   hBrBgPrimary;
    HBRUSH   hBrBgSecondary;
    int      dpi;
    HWND     hSysNotesScroll; /* custom scrollbar for AI instructions */
    int      sys_notes_line_h; /* cached line height in px */

    HWND     hNav;        /* category nav listbox (child of main window) */
    HWND     hPage;       /* page host (child of main window) */
    HWND     hBtnOK;
    HWND     hBtnCancel;
    HWND     hFooter;     /* "Nutshell vX.Y.Z" footer (child of main window) */
    HWND     hPageTitle;  /* breadcrumb title (child of page host) */
    HWND     hPageScroll; /* whole-page scrollbar (child of page host) */
    HWND     hSshHint;    /* dim wrapped SSH idle-timeout hint */
    HWND     hAboutBlurb; /* dim About tagline */
    HWND     hCtxHint;    /* dim wrapped AI max-context-lines hint */

    SettingsCtrl *ctrl_ai_base_url;   /* toggled by provider combo */
    SettingsCtrl *ctrl_ai_search_url; /* toggled by search-provider combo */

    int      cur_page; /* SETTINGS_PAGE_* currently shown */
    int      scroll;   /* current page scroll offset, px */

    SettingsCtrl ctrls[MAX_SETTINGS_CTRLS];
    int          n_ctrls;
} SettingsDlgData;

static void relayout(HWND hwnd, SettingsDlgData *d);
static void sys_notes_sync_scroll(SettingsDlgData *d);

/* ---- Layout helpers ----------------------------------------------------- */
/* Controls are created with placeholder geometry; relayout() positions them
 * for real once every control on every page exists. */

static HWND make_label2(HWND parent, const char *text)
{
    return CreateWindow("STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        0, 0, 10, 10, parent, NULL, NULL, NULL);
}

static HWND make_static2(HWND parent, const char *text)
{
    return CreateWindow("STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 10, 10, parent, NULL, NULL, NULL);
}

static HWND make_edit2(HWND parent, const char *text, HMENU id, DWORD extra_style)
{
    HWND h = CreateWindow("EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | extra_style,
        0, 0, 10, 10, parent, id, NULL, NULL);
    SendMessage(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELPARAM(3, 3));
    return h;
}

static HWND make_combo2(HWND parent, HMENU id, DWORD type_style)
{
    return CreateWindow("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_HASSTRINGS | WS_VSCROLL | type_style,
        0, 0, 10, 200, parent, id, NULL, NULL);
}

static HWND make_check2(HWND parent, const char *text, HMENU id)
{
    return CreateWindow("BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 10, 10, parent, id, NULL, NULL);
}

static SettingsCtrl *add_ctrl(SettingsDlgData *d, HWND lbl, HWND ctrl, HWND extra,
                               int page, int ctrl_w, int extra_w, int rows,
                               int stretch_v, int visible, int full_w)
{
    if (d->n_ctrls >= MAX_SETTINGS_CTRLS) return NULL;
    SettingsCtrl *c = &d->ctrls[d->n_ctrls++];
    c->hLabel    = lbl;
    c->hCtrl     = ctrl;
    c->hExtra    = extra;
    c->page      = page;
    c->ctrl_w    = ctrl_w;
    c->extra_w   = extra_w;
    c->rows      = rows;
    c->stretch_v = stretch_v;
    c->visible   = visible;
    c->full_w    = full_w;
    return c;
}

/* Combo boxes must keep a tall window rect at all times — that rect also
 * defines the drop-down list's extent, not just the closed box — so
 * relayout() never shrinks one down to a single row's height. */
static int is_combo_class(HWND h)
{
    char cls[32];
    if (!h) return 0;
    GetClassNameA(h, cls, (int)sizeof(cls));
    return _stricmp(cls, "ComboBox") == 0;
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

/* ---- Tooltip helpers ---------------------------------------------------- */

/* Add a tooltip to a single child control inside the dialog.
 * Reuses one shared tooltip window per dialog (TTM_ADDTOOL). */
static void add_tooltip(HWND tooltip_host, HWND tool, const char *text)
{
    if (!tooltip_host || !tool || !text) return;
    TOOLINFO ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize   = sizeof(TOOLINFO);
    ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd     = GetParent(tool);
    ti.uId      = (UINT_PTR)tool;
    ti.lpszText = (LPSTR)text;
    SendMessage(tooltip_host, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

typedef struct { int id; const char *text; } TooltipEntry;

static const TooltipEntry k_tooltips[] = {
    { IDC_FONT_COMBO,
      "Monospaced font used by the terminal display." },
    { IDC_AI_FONT_COMBO,
      "Font used by the AI chat panel (does not affect the terminal)." },
    { IDC_FONTSIZE_COMBO,
      "Terminal font size in points." },
    { IDC_SCROLLBACK_EDIT,
      "Number of lines kept in the scrollback buffer (100 - 50 000)." },
    { IDC_PASTEDELAY_EDIT,
      "Pause in ms between characters when pasting into the terminal "
      "(0 - 5 000). Higher values help slow remote shells keep up." },
    { IDC_PASTE_CONFIRM,
      "Show a preview and require confirmation before pasting into the "
      "terminal." },
    { IDC_SCHEME_COMBO,
      "Predefined colour scheme for the terminal. Foreground and "
      "background overrides apply on top of the scheme." },
    { IDC_LOG_DIR_EDIT,
      "Directory where session logs are written when logging is enabled." },
    { IDC_LOG_FMT_EDIT,
      "%Y  4-digit year (e.g. 2026)\r\n"
      "%m  month (01-12)\r\n"
      "%d  day   (01-31)\r\n"
      "%H  hour  (00-23)\r\n"
      "%M  minute (00-59)\r\n"
      "%S  second (00-59)\r\n"
      "Example: session-%Y%m%d_%H%M%S" },
    { IDC_DEBUG_TERMINAL,
      "Write raw terminal byte stream to a debug log "
      "(useful for debugging escape-sequence handling)." },
    { IDC_AI_KEY_EDIT,
      "API key for the chosen AI provider. Stored encrypted in "
      "nutshell.config." },
    { IDC_AI_PROVIDER_COMBO,
      "AI provider used by the chat panel." },
    { IDC_AI_CUSTOM_URL,
      "Custom AI API endpoint URL. Only used when provider is 'custom'." },
    { IDC_AI_CUSTOM_MODEL,
      "Custom model identifier. Only used when provider is 'custom'." },
    { IDC_AI_SYSTEM_NOTES,
      "Default system instructions for the AI. Profile-specific notes, "
      "if set, take precedence." },
    { IDC_AI_CONTEXT_LINES,
      "How many lines of terminal output are sent to the AI as context "
      "(1 - 50 000). Larger values give better context but cost more "
      "tokens per message." },
    { IDC_AI_SEARCH_COMBO,
      "Search backend used by AI tool calls. 'None' disables web search." },
    { IDC_AI_SEARCH_URL,
      "Custom search endpoint. Only used when search provider is 'custom'." },
    { IDC_AI_MAX_RESULTS,
      "Maximum number of search results returned to the AI per query "
      "(1 - 20)." },
    { IDC_AI_WEB_FETCH,
      "Allow the AI to fetch arbitrary URLs as a tool call." },
    { IDC_AI_MD_RENDER,
      "Render AI replies as formatted markdown. Turn off to see raw text." },
    { IDC_AI_AUTO_APPROVE_ALL,
      "When Auto Approve is on, also approve write and critical commands "
      "without a prompt. Off by default for safety." },
    { IDC_SSH_IDLE_EDIT,
      "Disconnect SSH sessions after this many minutes of no user "
      "activity. 0 = never disconnect on idle. Keystrokes, mouse-wheel "
      "scrolling, tab switches, and AI chat input all count as activity." },
    { IDC_AI_REFRESH,
      "Fetch the model list from the selected AI provider." },
    { IDC_SESSION_MGR_STARTUP,
      "Show the Session Manager when Nutshell starts, unless auto-connect "
      "fires or -nc is used." },
    { IDC_AUTOCONNECT_CHECK,
      "Automatically connect to the selected session when Nutshell "
      "starts. Command-line options override this; start with -nc to "
      "skip auto-connect once." },
    { IDC_AUTOCONNECT_COMBO,
      "The saved session to auto-connect to at startup." },
};
#define NUM_TOOLTIPS ((int)(sizeof(k_tooltips) / sizeof(k_tooltips[0])))

/* Sync AI instructions edit scroll state to custom scrollbar.
 * csb_sync_edit re-shows the scrollbar whenever the text overflows, so this
 * must do nothing unless the notes edit is the page on screen — the 50 ms
 * timer would otherwise float the notes scrollbar over an unrelated page. */
static void sys_notes_sync_scroll(SettingsDlgData *d)
{
    if (!d || !d->hSysNotesScroll) return;
    if (d->cur_page != SETTINGS_PAGE_AI_BEHAVIOUR) {
        ShowWindow(d->hSysNotesScroll, SW_HIDE);
        return;
    }
    HWND hEdit = GetDlgItem(d->hPage, IDC_AI_SYSTEM_NOTES);
    csb_sync_edit(hEdit, d->hSysNotesScroll, d->sys_notes_line_h);
}

/* ---- Nav owner-draw ------------------------------------------------------ */

static void draw_nav_item(SettingsDlgData *d, LPDRAWITEMSTRUCT dis)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    const ThemeColors *th = d->theme;

    HBRUSH hBg = CreateSolidBrush(theme_cr(th->bg_secondary));
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    if ((int)dis->itemID < 0) return;
    const SettingsNavEntry *e = settings_nav_at((int)dis->itemID);
    if (!e) return;

    int selected = ((dis->itemState & ODS_SELECTED) != 0) && !e->is_header;
    if (selected) {
        HBRUSH hSel = CreateSolidBrush(theme_cr(th->accent));
        FillRect(hdc, &rc, hSel);
        DeleteObject(hSel);
    }

    SettingsMetrics m;
    settings_metrics_init(&m, d->dpi);
    int indent = (e->depth > 0) ? settings_scale(14, d->dpi) : 0;

    SetBkMode(hdc, TRANSPARENT);
    unsigned int fg = selected ? th->bg_primary
                     : (e->is_header ? th->text_dim : th->text_main);
    SetTextColor(hdc, theme_cr(fg));

    HFONT useFont = e->is_header ? d->hBoldFont : d->hDlgFont;
    HGDIOBJ old = useFont ? SelectObject(hdc, useFont) : NULL;

    RECT trc = rc;
    trc.left += m.pad + indent;
    DrawTextA(hdc, e->label, -1, &trc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (old) SelectObject(hdc, old);
}

/* Step past a header in the direction of travel; on a dead end at either
 * edge of the list, fall back to searching the opposite direction so the
 * selection always lands on a real page. */
static int resolve_page_index(int start_idx, int dir)
{
    int n = settings_nav_count();
    int idx = start_idx;
    while (idx >= 0 && idx < n) {
        const SettingsNavEntry *e = settings_nav_at(idx);
        if (e && !e->is_header) return idx;
        idx += dir;
    }
    idx = start_idx;
    dir = -dir;
    while (idx >= 0 && idx < n) {
        const SettingsNavEntry *e = settings_nav_at(idx);
        if (e && !e->is_header) return idx;
        idx += dir;
    }
    int fp = settings_nav_first_page();
    return (fp >= 0) ? fp : 0;
}

/* ---- Relayout ------------------------------------------------------------
 * Single code path for resize, page switch, and scrolling. Positions every
 * control on the current page inside the page host, then repositions the
 * nav pane, page host, and button bar within the main window. */

static void relayout_page(SettingsDlgData *d, const SettingsMetrics *m,
                          int content_w, int content_h)
{
    /* Pass 1: how many rows does the current page need, and which control
     * (if any) absorbs leftover vertical space? */
    int total_rows = 1; /* the breadcrumb title row */
    SettingsCtrl *stretch = NULL;
    for (int i = 0; i < d->n_ctrls; i++) {
        SettingsCtrl *c = &d->ctrls[i];
        if (c->page != d->cur_page || !c->visible) continue;
        total_rows += c->rows;
        if (c->stretch_v) stretch = c;
    }

    int avail_rows = (content_h - 2 * m->pad) / m->row_h;
    int extra = avail_rows - total_rows;
    int extra_rows = (extra > 0 && stretch) ? extra : 0;

    int page_h = settings_page_height(total_rows + extra_rows, m);
    int scroll_max = settings_scroll_max(content_h, page_h);
    d->scroll = settings_scroll_clamp(d->scroll, scroll_max);

    int show_scroll = scroll_max > 0;
    int usable_w = content_w - (show_scroll ? CSB_WIDTH : 0);
    if (usable_w < 0) usable_w = 0;

    if (show_scroll) {
        int sx = content_w - CSB_WIDTH;
        SetWindowPos(d->hPageScroll, NULL, sx, 0, CSB_WIDTH, content_h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(d->hPageScroll, SW_SHOWNOACTIVATE);
        int pmax = page_h > 0 ? page_h - 1 : 0;
        csb_set_range(d->hPageScroll, 0, pmax, content_h);
        csb_set_pos(d->hPageScroll, d->scroll);
    } else if (d->hPageScroll) {
        ShowWindow(d->hPageScroll, SW_HIDE);
    }

    /* Breadcrumb title occupies row 0, spanning the full usable width. */
    if (d->hPageTitle) {
        SettingsRect lrc, crc;
        settings_row_rects(0, usable_w, 0, d->scroll, m, &lrc, &crc);
        int w = usable_w - 2 * m->pad;
        if (w < 0) w = 0;
        SetWindowPos(d->hPageTitle, NULL, m->pad, crc.y, w, crc.h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* Pass 2: place every control on the current page. */
    int row = 1;
    for (int i = 0; i < d->n_ctrls; i++) {
        SettingsCtrl *c = &d->ctrls[i];

        if (c->page != d->cur_page || !c->visible) {
            if (c->hLabel) ShowWindow(c->hLabel, SW_HIDE);
            if (c->hCtrl)  ShowWindow(c->hCtrl, SW_HIDE);
            if (c->hExtra) ShowWindow(c->hExtra, SW_HIDE);
            continue;
        }

        int eff_rows = c->rows + ((c == stretch) ? extra_rows : 0);
        int ctrl_w_scaled = (c->ctrl_w > 0) ? settings_scale(c->ctrl_w, m->dpi) : 0;

        SettingsRect lrc, crc;
        settings_row_rects(row, usable_w, ctrl_w_scaled, d->scroll, m, &lrc, &crc);

        if (c->full_w) {
            crc.x = m->pad;
            crc.w = usable_w - 2 * m->pad;
            if (crc.w < 0) crc.w = 0;
        }

        if (eff_rows > 1)
            crc.h = eff_rows * m->row_h - (m->row_h - m->ctrl_h);

        if (c->hExtra) {
            int extra_w_scaled = settings_scale(c->extra_w, m->dpi);
            int new_w = crc.w - extra_w_scaled - m->gap;
            if (new_w < 0) new_w = 0;
            SettingsRect erc = crc;
            erc.x = crc.x + new_w + m->gap;
            erc.w = extra_w_scaled;
            crc.w = new_w;
            SetWindowPos(c->hExtra, NULL, erc.x, erc.y, erc.w, erc.h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(c->hExtra, SW_SHOW);
        } else if (c->extra_w > 0) {
            /* Reserve space with no companion control (the AI notes csb,
             * positioned separately below once the edit's real rect is
             * known). */
            crc.w -= c->extra_w;
            if (crc.w < 0) crc.w = 0;
        }

        if (is_combo_class(c->hCtrl))
            crc.h = m->ctrl_h + settings_scale(200, m->dpi);

        if (c->hLabel) {
            SetWindowPos(c->hLabel, NULL, lrc.x, lrc.y, lrc.w, lrc.h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(c->hLabel, SW_SHOW);
        }
        SetWindowPos(c->hCtrl, NULL, crc.x, crc.y, crc.w, crc.h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(c->hCtrl, SW_SHOW);

        row += eff_rows;
    }

    /* Reposition the AI notes scrollbar flush against the notes edit. */
    {
        HWND hNotes = GetDlgItem(d->hPage, IDC_AI_SYSTEM_NOTES);
        if (hNotes && d->hSysNotesScroll &&
            d->cur_page == SETTINGS_PAGE_AI_BEHAVIOUR) {
            RECT erc;
            GetWindowRect(hNotes, &erc);
            POINT pt = { erc.right, erc.top };
            ScreenToClient(d->hPage, &pt);
            int eh = erc.bottom - erc.top;
            SetWindowPos(d->hSysNotesScroll, NULL, pt.x, pt.y, CSB_WIDTH, eh,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            sys_notes_sync_scroll(d);
        } else if (d->hSysNotesScroll) {
            ShowWindow(d->hSysNotesScroll, SW_HIDE);
        }
    }
}

static void relayout(HWND hwnd, SettingsDlgData *d)
{
    if (!d) return;

    RECT rc;
    GetClientRect(hwnd, &rc);

    SettingsMetrics m;
    settings_metrics_init(&m, d->dpi);

    SettingsRect nav, content, buttons;
    settings_layout_regions(rc.right, rc.bottom, &m, &nav, &content, &buttons);

    SetWindowPos(d->hNav, NULL, nav.x, nav.y, nav.w, nav.h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    /* The layout module tiles the panes edge to edge, so inset the page host
     * by one column to leave the divider between nav and content visible —
     * the main window paints it in WM_ERASEBKGND. */
    int page_w = content.w - 1;
    if (page_w < 0) page_w = 0;
    SetWindowPos(d->hPage, NULL, content.x + 1, content.y, page_w, content.h,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    int by = buttons.y + (buttons.h - m.btn_h) / 2;
    int cancel_x = buttons.x + buttons.w - m.pad - m.btn_w;
    SetWindowPos(d->hBtnCancel, NULL, cancel_x, by, m.btn_w, m.btn_h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    int ok_x = cancel_x - m.gap - m.btn_w;
    SetWindowPos(d->hBtnOK, NULL, ok_x, by, m.btn_w, m.btn_h,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    int fh = settings_scale(16, m.dpi);
    int fy = buttons.y + (buttons.h - fh) / 2;
    int fw = ok_x - m.gap - (buttons.x + m.pad);
    if (fw < 0) fw = 0;
    SetWindowPos(d->hFooter, NULL, buttons.x + m.pad, fy, fw, fh,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    relayout_page(d, &m, page_w, content.h);
}

/* ---- Page host window procedure ------------------------------------------
 * Every page control is a child of this window. It handles theme colours,
 * owner-draw for the refresh button, forwards WM_COMMAND to the main
 * window, and owns scrolling (both the AI notes edit and the whole page). */

static LRESULT CALLBACK SettingsPageProc(HWND hwnd, UINT umsg,
                                         WPARAM wParam, LPARAM lParam)
{
    SettingsDlgData *d = (SettingsDlgData *)(LONG_PTR)
                         GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (umsg) {
    case WM_CREATE: {
        LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

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
            HWND hCtl = (HWND)lParam;
            int dim = (hCtl == d->hSshHint || hCtl == d->hAboutBlurb ||
                       hCtl == d->hCtxHint);
            SetTextColor((HDC)wParam,
                         theme_cr(dim ? d->theme->text_dim : d->theme->text_main));
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
            if ((int)dis->CtlID == IDC_AI_REFRESH) {
                draw_themed_button(dis, d->theme, 0);
                return TRUE;
            }
        }
        break;

    case WM_COMMAND:
        return SendMessage(GetParent(hwnd), WM_COMMAND, wParam, lParam);

    case WM_VSCROLL:
        if (d) {
            HWND hSrc = (HWND)lParam;

            if (hSrc == d->hSysNotesScroll) {
                WORD code = LOWORD(wParam);
                HWND hEdit = GetDlgItem(d->hPage, IDC_AI_SYSTEM_NOTES);
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
                sys_notes_sync_scroll(d);
                return 0;
            }

            if (hSrc == d->hPageScroll) {
                WORD code = LOWORD(wParam);
                RECT rc;
                GetClientRect(hwnd, &rc);
                SettingsMetrics m;
                settings_metrics_init(&m, d->dpi);
                switch (code) {
                case SB_LINEUP:      d->scroll -= m.row_h;   break;
                case SB_LINEDOWN:    d->scroll += m.row_h;   break;
                case SB_PAGEUP:      d->scroll -= rc.bottom; break;
                case SB_PAGEDOWN:    d->scroll += rc.bottom; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    d->scroll = csb_get_trackpos(d->hPageScroll);
                    break;
                case SB_TOP:         d->scroll = 0;          break;
                case SB_BOTTOM:      d->scroll = 0x7FFFFFFF; break;
                }
                relayout(GetParent(hwnd), d);
                return 0;
            }
        }
        break;

    case WM_MOUSEWHEEL:
        if (d) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HWND hHit = WindowFromPoint(pt);
            HWND hNotes = GetDlgItem(d->hPage, IDC_AI_SYSTEM_NOTES);

            if (hNotes && hHit == hNotes && IsWindowVisible(hNotes)) {
                int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
                int scroll = edit_scroll_wheel_delta(zdelta, WHEEL_DELTA, 3);
                SendMessage(hNotes, EM_LINESCROLL, 0, (LPARAM)scroll);
                sys_notes_sync_scroll(d);
            } else {
                SettingsMetrics m;
                settings_metrics_init(&m, d->dpi);
                int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
                int notches = zdelta / WHEEL_DELTA;
                d->scroll -= notches * 3 * m.row_h;
                relayout(GetParent(hwnd), d);
            }
            return 0;
        }
        break;
    }

    return DefWindowProc(hwnd, umsg, wParam, lParam);
}

/* ---- Main window procedure ------------------------------------------------ */

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT umsg,
                                        WPARAM wParam, LPARAM lParam)
{
    SettingsDlgData *d = (SettingsDlgData *)(LONG_PTR)
                         GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (umsg) {
    case WM_CREATE: {
        LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
        SettingsDlgData *nd = (SettingsDlgData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)nd);

        nd->dpi = get_window_dpi(hwnd);
        SettingsMetrics m;
        settings_metrics_init(&m, nd->dpi);

        /* ---- Nav listbox ---- */
        nd->hNav = CreateWindow("LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY,
            0, 0, 10, 10, hwnd, (HMENU)IDC_SETTINGS_NAV, NULL, NULL);
        {
            int n = settings_nav_count();
            for (int i = 0; i < n; i++) {
                const SettingsNavEntry *e = settings_nav_at(i);
                SendMessage(nd->hNav, LB_ADDSTRING, 0, (LPARAM)e->label);
            }
            SendMessage(nd->hNav, LB_SETITEMHEIGHT, 0,
                        (LPARAM)MAKELONG(m.nav_item_h, 0));
        }

        /* ---- Page host ---- */
        nd->hPage = CreateWindowEx(0, PAGE_CLASS, "",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 10, 10, hwnd, NULL, GetModuleHandle(NULL), nd);

        nd->hPageTitle = make_static2(nd->hPage, "");

        nd->n_ctrls = 0;

        /* ==== APPEARANCE ==== */
        {
            HWND lbl = make_label2(nd->hPage, "Colour Scheme:");
            HWND cmb = make_combo2(nd->hPage, (HMENU)IDC_SCHEME_COMBO, CBS_DROPDOWNLIST);
            int sel = 0;
            for (int i = 0; i < NUM_UI_THEMES; i++) {
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)ui_theme_name(i));
                if (_stricmp(nd->cfg->settings.colour_scheme, ui_theme_name(i)) == 0)
                    sel = i;
            }
            SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_APPEARANCE, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd->hPage, "Terminal Font:");
            HWND cmb = make_combo2(nd->hPage, (HMENU)IDC_FONT_COMBO, CBS_DROPDOWNLIST);
            int sel = 0, idx = 0;
            for (int i = 0; i < NUM_FONTS; i++) {
                if (!font_is_installed(k_fonts[i])) continue;
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)k_fonts[i]);
                if (_stricmp(nd->cfg->settings.font, k_fonts[i]) == 0) sel = idx;
                idx++;
            }
            SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_APPEARANCE, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd->hPage, "Font Size:");
            HWND cmb = make_combo2(nd->hPage, (HMENU)IDC_FONTSIZE_COMBO, CBS_DROPDOWNLIST);
            int sel = 0;
            for (int i = 0; i < NUM_FONT_SIZES; i++) {
                char buf[8];
                (void)snprintf(buf, sizeof(buf), "%d", k_app_font_sizes[i]);
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)buf);
                if (k_app_font_sizes[i] == nd->cfg->settings.font_size) sel = i;
            }
            SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_APPEARANCE, 80, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd->hPage, "AI Assist Font:");
            HWND cmb = make_combo2(nd->hPage, (HMENU)IDC_AI_FONT_COMBO, CBS_DROPDOWNLIST);
            int sel = 0, idx = 0;
            for (int i = 0; i < NUM_FONTS; i++) {
                if (!font_is_installed(k_fonts[i])) continue;
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)k_fonts[i]);
                if (_stricmp(nd->cfg->settings.ai_font, k_fonts[i]) == 0) sel = idx;
                idx++;
            }
            SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_APPEARANCE, 0, 0, 1, 0, 1, 0);
        }

        /* ==== TERMINAL ==== */
        {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d", nd->cfg->settings.scrollback_lines);
            HWND lbl = make_label2(nd->hPage, "Scrollback Lines:");
            HWND ed = make_edit2(nd->hPage, buf, (HMENU)IDC_SCROLLBACK_EDIT, 0);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_TERMINAL, 100, 0, 1, 0, 1, 0);
        }
        {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d", nd->cfg->settings.paste_delay_ms);
            HWND lbl = make_label2(nd->hPage, "Paste Delay (ms):");
            HWND ed = make_edit2(nd->hPage, buf, (HMENU)IDC_PASTEDELAY_EDIT, 0);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_TERMINAL, 100, 0, 1, 0, 1, 0);
        }
        {
            HWND chk = make_check2(nd->hPage, "Confirm before pasting",
                                   (HMENU)IDC_PASTE_CONFIRM);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.paste_confirm ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_TERMINAL, 0, 0, 1, 0, 1, 1);
        }

        /* ==== LOGGING ==== */
        {
            HWND lbl = make_label2(nd->hPage, "Log Directory:");
            HWND ed = make_edit2(nd->hPage, nd->cfg->settings.log_dir,
                                 (HMENU)IDC_LOG_DIR_EDIT, 0);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_LOGGING, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd->hPage, "Log Name Format:");
            HWND ed = make_edit2(nd->hPage, nd->cfg->settings.log_format,
                                 (HMENU)IDC_LOG_FMT_EDIT, 0);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_LOGGING, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND chk = make_check2(nd->hPage, "Debug Terminal Log", (HMENU)IDC_DEBUG_TERMINAL);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.debug_terminal ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_LOGGING, 0, 0, 1, 0, 1, 1);
        }

        /* ==== SSH ==== */
        {
            char buf[32];
            (void)snprintf(buf, sizeof(buf), "%d",
                           nd->cfg->settings.ssh_user_idle_timeout_mins);
            HWND lbl = make_label2(nd->hPage, "Idle Timeout (mins):");
            HWND ed = make_edit2(nd->hPage, buf, (HMENU)IDC_SSH_IDLE_EDIT, 0);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_SSH, 100, 0, 1, 0, 1, 0);
        }
        {
            nd->hSshHint = make_static2(nd->hPage,
                "0 disables the idle timeout. Keystrokes, scrolling, tab "
                "switches and AI chat all count as activity.");
            add_ctrl(nd, NULL, nd->hSshHint, NULL, SETTINGS_PAGE_SSH, 0, 0, 2, 0, 1, 1);
        }

        /* ==== STARTUP ==== */
        {
            HWND chk = make_check2(nd->hPage, "Open Session Manager at startup",
                                   (HMENU)IDC_SESSION_MGR_STARTUP);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.open_session_manager_at_start
                            ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_STARTUP, 0, 0, 1, 0, 1, 1);
        }
        {
            HWND chk = make_check2(nd->hPage, "Auto-connect at startup",
                                   (HMENU)IDC_AUTOCONNECT_CHECK);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.auto_connect ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_STARTUP, 0, 0, 1, 0, 1, 1);
        }
        {
            HWND lbl = make_label2(nd->hPage, "Session:");
            HWND cmb = make_combo2(nd->hPage, (HMENU)IDC_AUTOCONNECT_COMBO, CBS_DROPDOWNLIST);
            const char *cur = nd->cfg->settings.auto_connect_session;
            int sel = -1;
            size_t np = vec_size(&nd->cfg->profiles);
            for (size_t pi = 0; pi < np; pi++) {
                const Profile *pr = (const Profile *)vec_get(&nd->cfg->profiles, pi);
                const char *label = (pr->name[0] != '\0') ? pr->name : pr->host;
                int idx = (int)SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)label);
                if (sel < 0 && cur[0] != '\0' && _stricmp(cur, label) == 0)
                    sel = idx;
            }
            /* Stored value no longer matches any session: keep it visible
             * (and selectable) so saving without touching it doesn't lose it. */
            if (sel < 0 && cur[0] != '\0')
                sel = (int)SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)cur);
            if (sel >= 0)
                SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
            EnableWindow(cmb, nd->cfg->settings.auto_connect ? TRUE : FALSE);
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_STARTUP, 0, 0, 1, 0, 1, 0);
        }

        /* ==== AI_PROVIDER ==== */
        {
        int is_custom = (_stricmp(nd->cfg->settings.ai_provider, "custom") == 0);
        {
            HWND lbl = make_label2(nd->hPage, "Provider:");
            HWND cmb = make_combo2(nd->hPage, (HMENU)IDC_AI_PROVIDER_COMBO, CBS_DROPDOWNLIST);
            int sel = 0;
            for (int i = 0; i < NUM_AI_PROVIDERS; i++) {
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)k_ai_providers[i]);
                if (_stricmp(nd->cfg->settings.ai_provider, k_ai_providers[i]) == 0)
                    sel = i;
            }
            SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_AI_PROVIDER, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd->hPage, "API Key:");
            HWND ed = make_edit2(nd->hPage, nd->cfg->settings.ai_api_key,
                                 (HMENU)IDC_AI_KEY_EDIT, ES_PASSWORD);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_AI_PROVIDER, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd->hPage, "Model:");
            HWND cmb = make_combo2(nd->hPage, (HMENU)IDC_AI_CUSTOM_MODEL, CBS_DROPDOWN);
            const char *cur_model = nd->cfg->settings.ai_custom_model;
            if (cur_model && cur_model[0]) SetWindowText(cmb, cur_model);
            HWND refresh = CreateWindowW(L"BUTTON", L"\x21BB",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 10, 10, nd->hPage, (HMENU)IDC_AI_REFRESH, NULL, NULL);
            add_ctrl(nd, lbl, cmb, refresh, SETTINGS_PAGE_AI_PROVIDER, 0, 26, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd->hPage, "Base URL:");
            HWND ed = make_edit2(nd->hPage, nd->cfg->settings.ai_custom_url,
                                 (HMENU)IDC_AI_CUSTOM_URL, 0);
            nd->ctrl_ai_base_url = add_ctrl(nd, lbl, ed, NULL,
                SETTINGS_PAGE_AI_PROVIDER, 0, 0, 1, 0, is_custom, 0);
        }
        }

        /* ==== AI_BEHAVIOUR ==== */
        {
            char buf[16];
            int lines = nd->cfg->settings.ai_max_context_lines;
            if (lines < 1) lines = AI_CONTEXT_LINES_DEFAULT;
            (void)snprintf(buf, sizeof(buf), "%d", lines);
            HWND lbl = make_label2(nd->hPage, "Max Terminal Lines:");
            HWND ed = make_edit2(nd->hPage, buf, (HMENU)IDC_AI_CONTEXT_LINES, ES_NUMBER);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 100, 0, 1, 0, 1, 0);
        }
        {
            nd->hCtxHint = make_static2(nd->hPage,
                "Each line becomes part of the context sent with every "
                "message. A larger window gives the assistant more of your "
                "session to reason about, at a proportionate cost in tokens. "
                "The maximum is 50,000 lines.");
            add_ctrl(nd, NULL, nd->hCtxHint, NULL, SETTINGS_PAGE_AI_BEHAVIOUR,
                     0, 0, 4, 0, 1, 1);
        }
        {
            HWND lbl = make_label2(nd->hPage, "System Instructions:");
            HWND ed = CreateWindow("EDIT", nd->cfg->settings.ai_system_notes,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
                0, 0, 10, 10, nd->hPage, (HMENU)IDC_AI_SYSTEM_NOTES, NULL, NULL);
            SendMessage(ed, EM_SETLIMITTEXT, (WPARAM)2559, 0);
            SendMessage(ed, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                        MAKELPARAM(3, 3));
            SendMessageW(ed, EM_SETCUEBANNER, 0,
                         (LPARAM)L"System-wide AI instructions (max 400 words)");
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_AI_BEHAVIOUR,
                     0, CSB_WIDTH, 6, 1, 1, 0);
        }
        {
            HWND chk = make_check2(nd->hPage, "Render AI markdown", (HMENU)IDC_AI_MD_RENDER);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.markdown_render_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 1, 0, 1, 1);
        }
        {
            HWND chk = make_check2(nd->hPage,
                                   "Auto Approve also covers write/critical commands",
                                   (HMENU)IDC_AI_AUTO_APPROVE_ALL);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.ai_auto_approve_all ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 1, 0, 1, 1);
        }

        /* ==== AI_WEB ==== */
        {
        int is_custom_search =
            (_stricmp(nd->cfg->settings.ai_search_provider, "custom") == 0);
        {
            HWND lbl = make_label2(nd->hPage, "Search Engine:");
            HWND cmb = make_combo2(nd->hPage, (HMENU)IDC_AI_SEARCH_COMBO, CBS_DROPDOWNLIST);
            int sel = 0;
            for (int i = 0; i < NUM_SEARCH_PROVIDERS; i++) {
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)k_search_providers[i].label);
                if (_stricmp(nd->cfg->settings.ai_search_provider,
                             k_search_providers[i].value) == 0)
                    sel = i;
            }
            SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_AI_WEB, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd->hPage, "Search URL:");
            HWND ed = make_edit2(nd->hPage, nd->cfg->settings.ai_search_url,
                                 (HMENU)IDC_AI_SEARCH_URL, 0);
            nd->ctrl_ai_search_url = add_ctrl(nd, lbl, ed, NULL,
                SETTINGS_PAGE_AI_WEB, 0, 0, 1, 0, is_custom_search, 0);
        }
        }
        {
            char buf[8];
            int max_r = nd->cfg->settings.ai_max_search_results;
            if (max_r <= 0) max_r = 7;
            (void)snprintf(buf, sizeof(buf), "%d", max_r);
            HWND lbl = make_label2(nd->hPage, "Max Results:");
            HWND ed = make_edit2(nd->hPage, buf, (HMENU)IDC_AI_MAX_RESULTS, ES_NUMBER);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_AI_WEB, 70, 0, 1, 0, 1, 0);
        }
        {
            HWND chk = make_check2(nd->hPage, "Permit Web Fetch", (HMENU)IDC_AI_WEB_FETCH);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.ai_web_fetch_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_AI_WEB, 0, 0, 1, 0, 1, 1);
        }

        /* ==== ABOUT ==== */
        {
            HWND s1 = make_static2(nd->hPage, "Nutshell v" APP_VERSION);
            add_ctrl(nd, NULL, s1, NULL, SETTINGS_PAGE_ABOUT, 0, 0, 2, 0, 1, 1);
        }
        {
            HWND s2 = make_static2(nd->hPage, "Copyright (C) 2026 Thomas Sulkiewicz");
            add_ctrl(nd, NULL, s2, NULL, SETTINGS_PAGE_ABOUT, 0, 0, 1, 0, 1, 1);
        }
        {
            nd->hAboutBlurb = make_static2(nd->hPage,
                "Windows SSH terminal with built-in AI assistance.");
            add_ctrl(nd, NULL, nd->hAboutBlurb, NULL, SETTINGS_PAGE_ABOUT, 0, 0, 1, 0, 1, 1);
        }

        /* ---- Buttons + footer (children of the main window) ---- */
        nd->hBtnCancel = CreateWindow("BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd, (HMENU)IDCANCEL, NULL, NULL);
        nd->hBtnOK = CreateWindow("BUTTON", "Save",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd, (HMENU)IDOK, NULL, NULL);
        nd->hFooter = CreateWindow("STATIC", "Nutshell v" APP_VERSION,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 10, 10, hwnd, NULL, NULL, NULL);

        /* ---- Tooltip window ---- */
        nd->hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | TTS_BALLOON,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        if (nd->hTooltip)
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)300);

        /* ---- Fonts ---- */
        {
            int h = -MulDiv(APP_FONT_UI_SIZE, nd->dpi, 72);
            nd->hDlgFont = CreateFont(
                h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                APP_FONT_UI_FACE);
            nd->hBoldFont = CreateFont(
                h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                APP_FONT_UI_FACE);
            if (nd->hDlgFont)
                EnumChildWindows(hwnd, SetFontProc, (LPARAM)nd->hDlgFont);
            if (nd->hBoldFont && nd->hPageTitle)
                SendMessage(nd->hPageTitle, WM_SETFONT, (WPARAM)nd->hBoldFont, TRUE);
        }

        /* ---- Theme: look up from config, create brushes, apply chrome ---- */
        {
            int idx = ui_theme_find(nd->cfg->settings.colour_scheme);
            nd->theme = ui_theme_get(idx);
            nd->hBrBgPrimary   = CreateSolidBrush(theme_cr(nd->theme->bg_primary));
            nd->hBrBgSecondary = CreateSolidBrush(theme_cr(nd->theme->bg_secondary));
            themed_apply_title_bar(hwnd, nd->theme);
            themed_apply_borders(hwnd, nd->theme);
        }

        /* ---- Scrollbars: whole-page, and AI instructions ---- */
        csb_register(GetModuleHandle(NULL));
        nd->hPageScroll = csb_create(nd->hPage, 0, 0, CSB_WIDTH, 10,
                                     nd->theme, GetModuleHandle(NULL));
        {
            HWND hEdit = GetDlgItem(nd->hPage, IDC_AI_SYSTEM_NOTES);
            if (hEdit) {
                HDC hdc = GetDC(hEdit);
                HGDIOBJ old = SelectObject(hdc, (HGDIOBJ)nd->hDlgFont);
                TEXTMETRIC tm;
                GetTextMetrics(hdc, &tm);
                nd->sys_notes_line_h = tm.tmHeight + tm.tmExternalLeading;
                if (nd->sys_notes_line_h < 1) nd->sys_notes_line_h = 16;
                SelectObject(hdc, old);
                ReleaseDC(hEdit, hdc);

                nd->hSysNotesScroll = csb_create(nd->hPage, 0, 0, CSB_WIDTH, 10,
                                                 nd->theme, GetModuleHandle(NULL));
                SetTimer(hwnd, IDT_SYSNOTES_SCROLL, 50, NULL);
            }
        }

        /* ---- Tooltips ---- */
        if (nd->hTooltip) {
            for (int i = 0; i < NUM_TOOLTIPS; i++) {
                HWND tool = GetDlgItem(nd->hPage, k_tooltips[i].id);
                if (tool)
                    add_tooltip(nd->hTooltip, tool, k_tooltips[i].text);
            }
        }

        /* ---- Initial nav selection + first layout pass ---- */
        {
            int idx = settings_nav_first_page();
            SendMessage(nd->hNav, LB_SETCURSEL, (WPARAM)idx, 0);
            const SettingsNavEntry *e = settings_nav_at(idx);
            nd->cur_page = e ? e->page_id : SETTINGS_PAGE_APPEARANCE;
            SetWindowText(nd->hPageTitle, settings_page_title(nd->cur_page));
        }
        relayout(hwnd, nd);

        return 0;
    }

    case WM_SIZE:
        if (d && wParam != SIZE_MINIMIZED) {
            relayout(hwnd, d);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        int ddpi = d ? d->dpi : 96;
        SettingsMetrics m;
        settings_metrics_init(&m, ddpi);
        RECT wr = {0, 0, m.min_w, m.min_h};
        AdjustWindowRect(&wr, (DWORD)GetWindowLong(hwnd, GWL_STYLE), FALSE);
        mmi->ptMinTrackSize.x = wr.right - wr.left;
        mmi->ptMinTrackSize.y = wr.bottom - wr.top;
        return 0;
    }

    case WM_AI_MODELS_DONE: {
        FetchModelsCtx *ctx = (FetchModelsCtx *)lParam;
        if (ctx && ctx->result) {
            if (!d) {
                /* Window is tearing down; nothing left to update. */
                free(ctx->result);
                free(ctx);
                return 0;
            }

            /* Re-enable the refresh button */
            EnableWindow(GetDlgItem(d->hPage, IDC_AI_REFRESH), TRUE);

            if (strncmp(ctx->result, "Error:", 6) == 0) {
                MessageBox(hwnd, ctx->result, "Refresh Models",
                           MB_OK | MB_ICONWARNING);
            } else {
                /* Parse newline-separated model IDs into the combo */
                HWND hCombo = GetDlgItem(d->hPage, IDC_AI_CUSTOM_MODEL);
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

    case WM_TIMER:
        if (wParam == IDT_SYSNOTES_SCROLL && d) {
            sys_notes_sync_scroll(d);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        if (d && d->theme) {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, d->hBrBgPrimary);

            SettingsMetrics m;
            settings_metrics_init(&m, d->dpi);
            SettingsRect nav, content, buttons;
            settings_layout_regions(rc.right, rc.bottom, &m, &nav, &content, &buttons);

            HPEN hPen = CreatePen(PS_SOLID, 1, theme_cr(d->theme->border));
            HGDIOBJ old = SelectObject(hdc, hPen);
            MoveToEx(hdc, nav.w, 0, NULL);
            LineTo(hdc, nav.w, nav.h);
            MoveToEx(hdc, 0, buttons.y, NULL);
            LineTo(hdc, rc.right, buttons.y);
            SelectObject(hdc, old);
            DeleteObject(hPen);
            return 1;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_dim));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_primary));
            return (LRESULT)d->hBrBgPrimary;
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
            if ((int)dis->CtlID == IDC_SETTINGS_NAV) {
                draw_nav_item(d, dis);
                return TRUE;
            }
            int is_primary = ((int)dis->CtlID == IDOK);
            draw_themed_button(dis, d->theme, is_primary);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case IDC_SETTINGS_NAV:
            if (HIWORD(wParam) == LBN_SELCHANGE && d) {
                int new_idx = (int)SendMessage(d->hNav, LB_GETCURSEL, 0, 0);
                int old_idx = settings_nav_index_of_page(d->cur_page);
                int dir = (new_idx >= old_idx) ? 1 : -1;
                new_idx = resolve_page_index(new_idx, dir);
                SendMessage(d->hNav, LB_SETCURSEL, (WPARAM)new_idx, 0);
                const SettingsNavEntry *e = settings_nav_at(new_idx);
                if (e) {
                    d->cur_page = e->page_id;
                    d->scroll = 0;
                    SetWindowText(d->hPageTitle, settings_page_title(d->cur_page));
                    relayout(hwnd, d);
                    InvalidateRect(d->hPage, NULL, TRUE);
                }
            }
            break;

        case IDC_AI_SEARCH_COMBO:
            /* Show/hide custom search URL field */
            if (HIWORD(wParam) == CBN_SELCHANGE && d) {
                int sel = (int)SendDlgItemMessage(d->hPage, IDC_AI_SEARCH_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_SEARCH_PROVIDERS && d->ctrl_ai_search_url) {
                    d->ctrl_ai_search_url->visible =
                        (_stricmp(k_search_providers[sel].value, "custom") == 0);
                    relayout(hwnd, d);
                    InvalidateRect(d->hPage, NULL, TRUE);
                }
            }
            break;

        case IDC_AI_PROVIDER_COMBO:
            /* Show/hide custom URL field; repopulate model combo */
            if (HIWORD(wParam) == CBN_SELCHANGE && d) {
                int sel = (int)SendDlgItemMessage(d->hPage, IDC_AI_PROVIDER_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_AI_PROVIDERS && d->ctrl_ai_base_url) {
                    d->ctrl_ai_base_url->visible =
                        (_stricmp(k_ai_providers[sel], "custom") == 0);
                    /* Clear model — user must press refresh to populate */
                    HWND hMdl = GetDlgItem(d->hPage, IDC_AI_CUSTOM_MODEL);
                    SendMessage(hMdl, CB_RESETCONTENT, 0, 0);
                    SetWindowText(hMdl, "");
                    relayout(hwnd, d);
                    InvalidateRect(d->hPage, NULL, TRUE);
                }
            }
            break;

        case IDC_AI_REFRESH:
            /* Fetch available models from the provider's API */
            if (d) {
                /* Get current provider */
                int psel = (int)SendDlgItemMessage(d->hPage, IDC_AI_PROVIDER_COMBO,
                                                   CB_GETCURSEL, 0, 0);
                const char *prov = (psel >= 0 && psel < NUM_AI_PROVIDERS)
                                   ? k_ai_providers[psel] : "";
                const char *models_url = ai_provider_models_url(prov);

                /* For custom provider, derive models URL from base URL */
                char custom_models_url[512] = {0};
                if (!models_url) {
                    char base[256];
                    GetDlgItemText(d->hPage, IDC_AI_CUSTOM_URL, base, (int)sizeof(base));
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
                GetDlgItemText(d->hPage, IDC_AI_KEY_EDIT, api_key, (int)sizeof(api_key));
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
                        EnableWindow(GetDlgItem(d->hPage, IDC_AI_REFRESH), FALSE);
                    } else {
                        free(ctx);
                    }
                }
            }
            break;

        case IDC_AUTOCONNECT_CHECK:
            if (d)
                EnableWindow(GetDlgItem(d->hPage, IDC_AUTOCONNECT_COMBO),
                             IsDlgButtonChecked(d->hPage, IDC_AUTOCONNECT_CHECK)
                                 == BST_CHECKED ? TRUE : FALSE);
            break;

        case IDOK: {
            if (!d) { DestroyWindow(hwnd); break; }
            Settings *s = &d->cfg->settings;

            /* Terminal font from combo */
            {
                char buf[CFG_STR_MAX];
                GetDlgItemText(d->hPage, IDC_FONT_COMBO, buf, (int)sizeof(buf));
                if (buf[0])
                    snprintf(s->font, sizeof(s->font), "%s", buf);
            }

            /* AI Assist font from combo */
            {
                char buf[CFG_STR_MAX];
                GetDlgItemText(d->hPage, IDC_AI_FONT_COMBO, buf, (int)sizeof(buf));
                if (buf[0])
                    snprintf(s->ai_font, sizeof(s->ai_font), "%s", buf);
            }

            /* Font size from discrete combo */
            {
                int sel = (int)SendDlgItemMessage(d->hPage, IDC_FONTSIZE_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_FONT_SIZES)
                    s->font_size = k_app_font_sizes[sel];
            }

            /* Numeric fields; keep existing value on parse failure */
            BOOL ok;
            UINT v;

            v = GetDlgItemInt(d->hPage, IDC_SCROLLBACK_EDIT, &ok, FALSE);
            if (ok) s->scrollback_lines = (int)v;

            v = GetDlgItemInt(d->hPage, IDC_PASTEDELAY_EDIT, &ok, FALSE);
            if (ok) s->paste_delay_ms = (int)v;

            /* Confirm before pasting */
            s->paste_confirm = (IsDlgButtonChecked(d->hPage, IDC_PASTE_CONFIRM)
                                 == BST_CHECKED) ? 1 : 0;

            /* Log directory & format */
            GetDlgItemText(d->hPage, IDC_LOG_DIR_EDIT,
                           s->log_dir, (int)sizeof(s->log_dir));
            GetDlgItemText(d->hPage, IDC_LOG_FMT_EDIT,
                           s->log_format, (int)sizeof(s->log_format));

            /* Debug terminal log checkbox */
            s->debug_terminal = (IsDlgButtonChecked(d->hPage, IDC_DEBUG_TERMINAL)
                                  == BST_CHECKED) ? 1 : 0;

            /* AI provider from combo */
            {
                int sel = (int)SendDlgItemMessage(d->hPage, IDC_AI_PROVIDER_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_AI_PROVIDERS) {
                    strncpy(s->ai_provider, k_ai_providers[sel],
                            sizeof(s->ai_provider) - 1);
                    s->ai_provider[sizeof(s->ai_provider) - 1] = '\0';
                }
            }

            /* AI API key */
            GetDlgItemText(d->hPage, IDC_AI_KEY_EDIT,
                           s->ai_api_key, (int)sizeof(s->ai_api_key));

            /* AI custom URL */
            GetDlgItemText(d->hPage, IDC_AI_CUSTOM_URL,
                           s->ai_custom_url, (int)sizeof(s->ai_custom_url));

            /* AI model — read combo text (works for both selection and free-text) */
            GetDlgItemText(d->hPage, IDC_AI_CUSTOM_MODEL,
                           s->ai_custom_model, (int)sizeof(s->ai_custom_model));

            /* Colour scheme from combo */
            {
                int sel = (int)SendDlgItemMessage(d->hPage, IDC_SCHEME_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_UI_THEMES) {
                    strncpy(s->colour_scheme, ui_theme_name(sel),
                            sizeof(s->colour_scheme) - 1);
                    s->colour_scheme[sizeof(s->colour_scheme) - 1] = '\0';
                }
            }

            /* AI system-wide instructions */
            GetDlgItemText(d->hPage, IDC_AI_SYSTEM_NOTES,
                           s->ai_system_notes, (int)sizeof(s->ai_system_notes));

            /* AI max context lines */
            v = GetDlgItemInt(d->hPage, IDC_AI_CONTEXT_LINES, &ok, FALSE);
            if (ok) s->ai_max_context_lines = ai_context_clamp_lines((int)v);
            /* on parse failure: retain previous value */

            /* AI search provider from combo */
            {
                int sel = (int)SendDlgItemMessage(d->hPage, IDC_AI_SEARCH_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_SEARCH_PROVIDERS) {
                    strncpy(s->ai_search_provider,
                            k_search_providers[sel].value,
                            sizeof(s->ai_search_provider) - 1);
                    s->ai_search_provider[sizeof(s->ai_search_provider) - 1] = '\0';
                }
            }

            /* Custom search URL */
            GetDlgItemText(d->hPage, IDC_AI_SEARCH_URL,
                           s->ai_search_url, (int)sizeof(s->ai_search_url));

            /* Max search results */
            v = GetDlgItemInt(d->hPage, IDC_AI_MAX_RESULTS, &ok, FALSE);
            if (ok && v >= 1 && v <= 20)
                s->ai_max_search_results = (int)v;

            /* Permit web fetch */
            s->ai_web_fetch_enabled = (IsDlgButtonChecked(d->hPage, IDC_AI_WEB_FETCH)
                                        == BST_CHECKED) ? 1 : 0;

            /* Render AI markdown */
            s->markdown_render_enabled = (IsDlgButtonChecked(d->hPage, IDC_AI_MD_RENDER)
                                           == BST_CHECKED) ? 1 : 0;

            /* Auto Approve also covers write/critical commands */
            s->ai_auto_approve_all = (IsDlgButtonChecked(d->hPage, IDC_AI_AUTO_APPROVE_ALL)
                                       == BST_CHECKED) ? 1 : 0;

            /* SSH user idle timeout */
            v = GetDlgItemInt(d->hPage, IDC_SSH_IDLE_EDIT, &ok, FALSE);
            if (ok && v <= 10080u)
                s->ssh_user_idle_timeout_mins = (int)v;
            /* on parse failure or out-of-range: retain previous value */

            /* Open Session Manager at startup */
            s->open_session_manager_at_start =
                (IsDlgButtonChecked(d->hPage, IDC_SESSION_MGR_STARTUP)
                     == BST_CHECKED) ? 1 : 0;

            /* Auto-connect at startup */
            s->auto_connect = (IsDlgButtonChecked(d->hPage, IDC_AUTOCONNECT_CHECK)
                                == BST_CHECKED) ? 1 : 0;
            GetDlgItemText(d->hPage, IDC_AUTOCONNECT_COMBO,
                           s->auto_connect_session,
                           (int)sizeof(s->auto_connect_session));

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
            if (d->hDlgFont)  DeleteObject(d->hDlgFont);
            if (d->hBoldFont) DeleteObject(d->hBoldFont);
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

    return DefWindowProc(hwnd, umsg, wParam, lParam);
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

    WNDCLASSEX wcPage;
    memset(&wcPage, 0, sizeof(wcPage));
    wcPage.cbSize        = sizeof(WNDCLASSEX);
    wcPage.style         = CS_HREDRAW | CS_VREDRAW;
    wcPage.lpfnWndProc   = SettingsPageProc;
    wcPage.hInstance     = GetModuleHandle(NULL);
    wcPage.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcPage.hbrBackground = NULL;
    wcPage.lpszClassName = PAGE_CLASS;
    RegisterClassEx(&wcPage);

    /* Default client size, DPI-scaled; AdjustWindowRect turns that into the
     * window size the non-client area (title bar, borders) needs. */
    int pdpi = get_window_dpi(parent);
    int client_w = settings_scale(760, pdpi);
    int client_h = settings_scale(560, pdpi);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                  WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
    RECT wr = {0, 0, client_w, client_h};
    AdjustWindowRect(&wr, style, FALSE);

    HWND hwnd = CreateWindowEx(
        0, SETTINGS_CLASS, "Settings",
        style | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
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

