#include "settings_dlg.h"
#include "resource.h"
#include "dpi_util.h"
#include "app_font.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ui_theme.h"
#include "notice_dlg.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "edit_scroll.h"
#include "settings_layout.h"
#include "settings_tab.h"
#include "ai_prompt.h"
#include "thinking_mode.h"
#include "ai_http.h"
#include "ai_netcancel.h"
#include "worker_life.h"
#include "secure_zero.h"
#include "thread_join.h"
#include "ai_gate.h"
#include "cmd_policy.h"
#include "tool_decision.h"
#include "json_parser.h"
#include "dlog.h"
#include "open_dir.h"
#include "settings_debug.h"
#include "pw_reveal.h"
#include "net_guard.h"
#include "ns_tokens.h"
#include "ns_draw.h"
#include "ns_check.h"
#include "ns_slider.h"
#include "ns_tip.h"
#include "voice_lang.h"
#include "settings_help.h"
#include "search_providers.h"
#include "settings_entry.h"
#include "opt_feature.h"
#include "voice_audio_win.h"
#include "voice_config.h"
#include "voice_gate.h"
#include "voice_level.h"
#include "voice_test.h"
#include "voice_stt.h"
#include "voice_live.h"
#include "voice_win.h"
#include "voice_conv_win.h"
#include "voice_tts_win.h"
#include "voice_voices_win.h"
#include "voice_aispeech.h"
#include "voice_aispeech_win.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

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
#define IDT_HELP_POP        52  /* F1 help showing: hide it when the focus moves */

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
    HWND hExtra2;   /* optional third control, right of hExtra (the Debugging folder row's Open), or NULL */
    int  page;      /* SETTINGS_PAGE_* */
    int  ctrl_w;    /* 96-dpi width hint; 0 = stretch to the content pane */
    int  extra_w;   /* 96-dpi width reserved on the right (hExtra, or a
                      * dedicated scrollbar with no companion control) */
    int  extra2_w;  /* 96-dpi width of hExtra2, the rightmost control */
    int  rows;      /* minimum vertical row span (1 for a normal row) */
    int  stretch_v; /* 1 = absorb leftover vertical space on this page */
    int  visible;   /* conditional visibility (custom URL fields) */
    int  full_w;    /* 1 = no label column; control spans the whole pane */
    int  extra_off; /* 1 = hExtra stays hidden although the row shows (the Other language box) */
} SettingsCtrl;

/* Raise this when a page outgrows it; add_ctrl refuses to overflow. */
#define MAX_SETTINGS_CTRLS 96

/* ---- Dialog state ------------------------------------------------------- */

/* A control and the help id whose tip it shows (recorded by page_track()). */
typedef struct {
    HWND           hwnd;
    SettingsHelpId help;
} SettingsTip;

typedef struct {
    Config  *cfg;
    const char *config_path; /* M-8/H6: absolute path resolved at startup --
                               * never the bare CONFIG_FILENAME, which would
                               * save into whatever the CWD has become. */
    HWND     hTip;       /* the help tip (ns_tip), themed from `theme` */
    SettingsTip tips[128]; /* every control with a help id, in creation order */
    int      n_tips;
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
    HWND     hToolTimeoutNote; /* dim note under the tool approval timeout */
    HWND     hToolRoundsNote;  /* dim note under the automatic tool rounds */
    HWND     hUrlWarn;    /* warning-coloured "not encrypted" line under Base URL */
    HWND     hDbgDim[6];  /* dim statics on the Debugging page */
    int      n_dbg_dim;
    SettingsCtrl *dbg[SDBG_COUNT]; /* the Debugging page's rows, by SettingsDebugSlot */
    int      demo;        /* --ui-demo: 1 = settings-debug (debugging ticked in memory), 2 = settings-voice (AI and voice ticked, no device list); Save never writes */
    HWND     tip_focus;   /* the control whose F1 help is showing, or NULL */
    int      saving;      /* the Auto-approve confirmation box is up: Save/Cancel/close are no-ops */
    struct FetchModelsCtx *models;   /* the Refresh Models request in flight (UI's reference), or NULL */
    ThreadJoinList models_thread;    /* its thread's handle, kept so closing can join it */

    SettingsCtrl *ctrl_ai_base_url;   /* toggled by provider combo */
    SettingsCtrl *ctrl_ai_url_warn;   /* shown for a clear-text custom endpoint */
    SettingsCtrl *ctrl_ai_search_url; /* toggled by search-provider combo */

    /* The Voice page: the rows that come and go, and the dim notes. */
    HWND     hVoiceBlock;    /* dim "Needs the AI assistant" under Enable voice */
    HWND     hVoiceAiNote;   /* dim reason "Use current AI assistant" cannot be ticked */
    HWND     hVoiceHead[3];  /* "Speech to Text" / "Microphone" / "Conversation" section labels */
    HWND     hVoiceUrlWarn;  /* warning-coloured "not encrypted" line under the voice Base URL */
    SettingsCtrl *ctrl_voice_block;
    SettingsCtrl *ctrl_voice_ai_note;
    SettingsCtrl *ctrl_voice_provider;
    SettingsCtrl *ctrl_voice_key;
    SettingsCtrl *ctrl_voice_url;
    SettingsCtrl *ctrl_voice_url_warn;
    int      voice_use_ai_wanted; /* the user's choice, kept while the AI provider cannot do speech */
    int      voice_dev_listed;    /* the device combo lists microphones (voice saved on) */
    HWND     hDlg;                /* the dialog window (timers, relayout) */
    HWND     hVoiceTestNote;      /* dim "No microphone was found." under the meter */
    SettingsCtrl *ctrl_voice_test_note;
    HWND     hVoiceLangHint;      /* the Other language box's hint, under the Language row */
    SettingsCtrl *ctrl_voice_lang;       /* the Language row (its extra is the Other box) */
    SettingsCtrl *ctrl_voice_lang_hint;
    HWND     hVoiceNoteVoice;     /* dim "The saved voice is not installed on this PC: using Automatic" */
    HWND     hVoiceNoteUk;        /* dim "No British English woman's voice is installed: ..." */
    HWND     hVoiceNoteAec;       /* dim "Your microphone reports no echo cancellation: ..." */
    HWND     hVoiceNoteDuck;      /* dim "Windows may lower other sounds during a conversation. ..." */
    SettingsCtrl *ctrl_voice_note_voice;
    SettingsCtrl *ctrl_voice_note_uk;
    SettingsCtrl *ctrl_voice_note_aec;
    SettingsCtrl *ctrl_voice_note_duck;
    size_t   voice_win_n;         /* installed Windows voices read when the page was built */
    VoiceInfo voice_win[VOICE_INFO_MAX];   /* entry i is combo item i + 1 (item 0 is Automatic) */
    int      voice_win_missing;   /* the saved voice is not in the list */
    int      voice_win_touched;   /* the user changed the voice combo */
    /* AI speech rows (Speech = AI speech) */
    HWND     hVoiceTtsWarn;       /* warning "not encrypted" line under the Speech base URL */
    HWND     hVoicePrivacy;       /* dim "Each spoken reply's text is sent to <host>." */
    SettingsCtrl *ctrl_tts_provider;
    SettingsCtrl *ctrl_tts_url;
    SettingsCtrl *ctrl_tts_url_warn;
    SettingsCtrl *ctrl_tts_key;
    SettingsCtrl *ctrl_ais_voice;
    SettingsCtrl *ctrl_ais_model;
    SettingsCtrl *ctrl_ais_style;
    SettingsCtrl *ctrl_ais_privacy;
    int      voice_uk_installed;  /* a British English woman's voice is installed */
    int      voice_tts_key_touched; /* the user edited the speech key this session (R43) */
    int      voice_tts_key_setting; /* the page itself is setting the key edit's text */
    char     voice_tts_key_bound[CFG_STR_MAX]; /* the page binding when the key was last typed (R43) */
    HWND     hVoiceLiveNote;      /* dim "Live text as you speak needs OpenAI or Mistral" */
    SettingsCtrl *ctrl_voice_live;       /* the Live model row (OpenAI or Mistral) */
    SettingsCtrl *ctrl_voice_live_note;  /* the note in the same slot otherwise */
    VoiceLiveProvider voice_live_seeded; /* the provider the Live model list was last seeded from */
    int      voice_lang_tone;     /* 0 neutral, 1 error: the hint's colour */
    char     voice_lang_prev[32]; /* the language saved when the page was built */
    char     voice_lang_loaded_other[32]; /* what the Other box was filled with at load ("" if not Other) */
    int      voice_testing;       /* the Test runs until a stop trigger (voice_test_next owns it) */
    VoiceMeter vmeter;            /* the meter's ballistics, hold tick and 1 s window */
    ULONGLONG vmeter_tick;        /* GetTickCount64 of the last VAM_LEVEL (or the start) */
    int      voice_dev_touched;   /* the user changed the device combo */
    int      voice_dev_missing;   /* the saved microphone is not in the list */
    int      voice_dev_n;         /* microphones listed (entry i is combo item i + 1) */
    struct { char id[256]; char name[256]; } voice_dev[32];   /* UTF-8 */
    const SttProvider *voice_seeded; /* the provider the Model list was last seeded from */

    int      cur_page; /* SETTINGS_PAGE_* currently shown */
    int      scroll;   /* current page scroll offset, px */
    int      initial_page; /* SETTINGS_PAGE_* to open on, or -1 for default */

    SettingsCtrl ctrls[MAX_SETTINGS_CTRLS];
    int          n_ctrls;
    int      think_initial; /* the Thinking display combo's selection when the page was built:
                             * OK writes it back only if the user changed it, so a click on a
                             * Thinking row while the dialog is open is not overwritten */
} SettingsDlgData;

static void relayout(HWND hwnd, SettingsDlgData *d);
static void ai_url_warn_sync(SettingsDlgData *d);
static void ai_url_warn_update(SettingsDlgData *d);
static void sys_notes_sync_scroll(SettingsDlgData *d);
static void ai_enable_sync(SettingsDlgData *d);
static void voice_enable_sync(SettingsDlgData *d);
static void voice_test_apply(SettingsDlgData *d, VoiceTestEvent ev);
static void voice_test_note(SettingsDlgData *d, const char *text);
static void voice_page_read(SettingsDlgData *d, Settings *s);

/* The open Settings dialog that listens for WM_VOICE_AUDIO (UI thread only). */
static SettingsDlgData *g_voice_dlg;

/* ---- Layout helpers ----------------------------------------------------- */
/* Controls are created with placeholder geometry; relayout() positions them
 * for real once every control on every page exists. */

/* Record a control for the help tip: the attach loop in WM_CREATE adds one
 * tool per record. SH_NONE (a hint, a title) records nothing. */
static void page_track(SettingsDlgData *d, HWND h, SettingsHelpId help)
{
    if (!h || help == SH_NONE) return;
    if (d->n_tips >= (int)(sizeof(d->tips) / sizeof(d->tips[0]))) return;
    d->tips[d->n_tips].hwnd = h;
    d->tips[d->n_tips].help = help;
    d->n_tips++;
}

/* The one way to create a control on a page: a child of the page host, with
 * its help id (SH_NONE for the few with none). A static gets SS_NOTIFY so it
 * answers the mouse and its tip can show. A combo is created tall: that rect
 * also defines the drop-down. relayout() positions everything for real. */
static HWND page_ctrl(SettingsDlgData *d, const char *cls, const char *text,
                      DWORD style, int ctrl_id, SettingsHelpId help)
{
    DWORD st = WS_CHILD | WS_VISIBLE | style;
    if (_stricmp(cls, "STATIC") == 0) st |= SS_NOTIFY;
    int h = (_stricmp(cls, "COMBOBOX") == 0) ? 200 : 10;
    HWND w = CreateWindow(cls, text, st, 0, 0, 10, h, d->hPage,
                          (HMENU)(UINT_PTR)ctrl_id, NULL, NULL);
    page_track(d, w, help);
    return w;
}

/* The same with a wide class and text, for the refresh glyph. */
static HWND page_ctrl_w(SettingsDlgData *d, const WCHAR *cls, const WCHAR *text,
                        DWORD style, int ctrl_id, SettingsHelpId help)
{
    HWND w = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                           d->hPage, (HMENU)(UINT_PTR)ctrl_id, NULL, NULL);
    page_track(d, w, help);
    return w;
}

/* A static with no help id: the page title, the dim hints and notes, the
 * warning line and the About text. */
static HWND make_hint(SettingsDlgData *d, const char *text)
{
    return page_ctrl(d, "STATIC", text, SS_LEFT, 0, SH_NONE);
}

/* A label takes its text from the help table, so the tip's heading and the
 * label on screen are one string. */
static HWND make_label2(SettingsDlgData *d, SettingsHelpId help)
{
    return page_ctrl(d, "STATIC", settings_help_get((int)help)->label, SS_RIGHT,
                     0, help);
}

static HWND make_edit2(SettingsDlgData *d, const char *text, int id,
                       DWORD extra_style, SettingsHelpId help)
{
    HWND h = page_ctrl(d, "EDIT", text,
                       WS_BORDER | ES_AUTOHSCROLL | extra_style, id, help);
    SendMessage(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELPARAM(3, 3));
    return h;
}

static HWND make_combo2(SettingsDlgData *d, int id, DWORD type_style,
                        SettingsHelpId help)
{
    return page_ctrl(d, "COMBOBOX", "", CBS_HASSTRINGS | WS_VSCROLL | type_style,
                     id, help);
}

/* A drop-down list that is a wide window, so the wide CB_ADDSTRING keeps a
 * name outside the ANSI code page (a microphone's) intact. */
static HWND make_combo2_wide(SettingsDlgData *d, int id, SettingsHelpId help)
{
    HWND w = CreateWindowW(L"COMBOBOX", L"",
                           WS_CHILD | WS_VISIBLE | CBS_HASSTRINGS | WS_VSCROLL | CBS_DROPDOWNLIST,
                           0, 0, 10, 200, d->hPage, (HMENU)(UINT_PTR)id, NULL, NULL);
    page_track(d, w, help);
    return w;
}

static HWND make_check2(SettingsDlgData *d, int id, SettingsHelpId help)
{
    HWND chk = page_ctrl(d, "BUTTON", settings_help_get((int)help)->label,
                         BS_AUTOCHECKBOX, id, help);
    /* The themed painter draws the box and the label from the tokens, so the
     * control needs no classic-rendering opt-out. */
    ns_check_attach(chk);
    return chk;
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
    c->hExtra2   = NULL;
    c->extra2_w  = 0;
    c->page      = page;
    c->ctrl_w    = ctrl_w;
    c->extra_w   = extra_w;
    c->rows      = rows;
    c->stretch_v = stretch_v;
    c->visible   = visible;
    c->full_w    = full_w;
    c->extra_off = 0;
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

/* Shared by the settings window and the thread (a WorkerLife: two
 * references). The window cancels the request when it closes; whoever drops
 * the last reference wipes the key and frees it. */
typedef struct FetchModelsCtx {
    WorkerLife  life;
    AiNetCancel nc;        /* fired when Settings closes: the blocked GET returns */
    HWND  hwnd;        /* settings window to post result to */
    char  url[512];    /* models endpoint URL */
    char  headers[3][300]; /* raw HTTP headers, the API key among them (NULL-terminated array) */
    char *result;      /* heap-allocated newline-separated model IDs, or error */
} FetchModelsCtx;

static void models_ctx_release(FetchModelsCtx *ctx)
{
    if (!ctx || !worker_life_release(&ctx->life)) return;
    secure_zero(ctx->headers, sizeof(ctx->headers));   /* the API key */
    free(ctx->result);
    free(ctx);
}

/* The thread's last act: hand the result to the window, unless Settings has
 * closed meanwhile (its HWND may be reused), then drop the thread's own
 * reference. The window's reference is dropped by the window: in
 * WM_AI_MODELS_DONE when it takes the result, or in WM_DESTROY. */
static void models_post_and_release(FetchModelsCtx *ctx)
{
    if (!ai_nc_cancelled(&ctx->nc))
        (void)PostMessage(ctx->hwnd, WM_AI_MODELS_DONE, 0, (LPARAM)ctx);
    models_ctx_release(ctx);
}

static DWORD WINAPI fetch_models_thread(LPVOID param)
{
    FetchModelsCtx *ctx = (FetchModelsCtx *)param;
    dlog_set_thread_tag("set");

    /* Build NULL-terminated header pointer array */
    const char *hdrs[4] = {NULL, NULL, NULL, NULL};
    for (int i = 0; i < 3 && ctx->headers[i][0]; i++)
        hdrs[i] = ctx->headers[i];

    AiHttpResponse resp;
    int rc = ai_http_get(ctx->url, hdrs[0] ? hdrs : NULL, &resp, &ctx->nc);
    DLOG(DLOG_GENERAL, DLOG_INFO, 0u, "settings.models", "rc=%d status=%d", rc,
         resp.status_code);

    if (rc != 0 || resp.status_code < 200 || resp.status_code >= 300) {
        char err[512];
        if (resp.error[0])
            snprintf(err, sizeof(err), "Error: %s", resp.error);
        else
            snprintf(err, sizeof(err), "Error: HTTP %d", resp.status_code);
        ctx->result = _strdup(err);
        ai_http_response_free(&resp);
        models_post_and_release(ctx);
        return 0;
    }

    /* Parse JSON: {"data": [{"id": "model-name"}, ...]} */
    JsonNode *root = json_parse(resp.body);
    ai_http_response_free(&resp);

    if (!root) {
        ctx->result = _strdup("Error: invalid JSON response");
        models_post_and_release(ctx);
        return 0;
    }

    JsonNode *data = json_obj_get(root, "data");
    if (!data || data->type != JSON_ARRAY) {
        json_free(root);
        ctx->result = _strdup("Error: no 'data' array in response");
        models_post_and_release(ctx);
        return 0;
    }

    /* Build newline-separated list of model IDs */
    size_t cap = 4096;
    char *buf = malloc(cap);
    if (!buf) {
        json_free(root);
        ctx->result = _strdup("Error: out of memory");
        models_post_and_release(ctx);
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
    models_post_and_release(ctx);
    return 0;
}

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
    int indent = (e->depth > 0) ? ns_scale(14, d->dpi) : 0;

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
            if (c->hExtra2) ShowWindow(c->hExtra2, SW_HIDE);
            continue;
        }

        int eff_rows = c->rows + ((c == stretch) ? extra_rows : 0);
        int ctrl_w_scaled = (c->ctrl_w > 0) ? ns_scale(c->ctrl_w, m->dpi) : 0;

        SettingsRect lrc, crc;
        settings_row_rects(row, usable_w, ctrl_w_scaled, d->scroll, m, &lrc, &crc);

        if (c->full_w) {
            /* No label column. An explicit ctrl_w still sizes the control
             * (a tickbox with a combo beside it); 0 spans the pane. */
            crc.x = m->pad;
            crc.w = ctrl_w_scaled > 0 ? ctrl_w_scaled : usable_w - 2 * m->pad;
            if (crc.w < 0) crc.w = 0;
        }

        if (eff_rows > 1)
            crc.h = eff_rows * m->row_h - (m->row_h - m->ctrl_h);

        if (c->hExtra2) {
            /* Rightmost control, carved off first; hExtra then sits left of it. */
            int w2 = ns_scale(c->extra2_w, m->dpi);
            int new_w = crc.w - w2 - m->gap;
            if (new_w < 0) new_w = 0;
            SetWindowPos(c->hExtra2, NULL, crc.x + new_w + m->gap, crc.y, w2, crc.h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(c->hExtra2, SW_SHOW);
            crc.w = new_w;
        }

        if (c->hExtra) {
            /* A hidden extra (the Other language box) reserves no width. */
            int extra_w_scaled = c->extra_off ? 0 : ns_scale(c->extra_w, m->dpi);
            int new_w = crc.w - extra_w_scaled - (c->extra_off ? 0 : m->gap);
            if (new_w < 0) new_w = 0;
            SettingsRect erc = crc;
            erc.x = crc.x + new_w + m->gap;
            erc.w = extra_w_scaled;
            if (is_combo_class(c->hExtra))
                erc.h = m->ctrl_h + ns_scale(200, m->dpi);
            crc.w = new_w;
            SetWindowPos(c->hExtra, NULL, erc.x, erc.y, erc.w, erc.h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(c->hExtra, c->extra_off ? SW_HIDE : SW_SHOW);
        } else if (c->extra_w > 0) {
            /* Reserve space with no companion control (the AI notes csb,
             * positioned separately below once the edit's real rect is
             * known). */
            crc.w -= c->extra_w;
            if (crc.w < 0) crc.w = 0;
        }

        if (is_combo_class(c->hCtrl))
            crc.h = m->ctrl_h + ns_scale(200, m->dpi);

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

    /* Move the two rect tools (the controls that can be disabled) to their
     * rows; an empty rect while the row is hidden. */
    if (d->hTip) {
        static const int k_rect_tools[] = { IDC_AUTOCONNECT_COMBO,
                                            IDC_DBG_RECORDER_LEVEL };
        for (size_t i = 0; i < sizeof(k_rect_tools) / sizeof(k_rect_tools[0]); i++) {
            HWND hc = GetDlgItem(d->hPage, k_rect_tools[i]);
            if (hc && (GetWindowLongA(hc, GWL_STYLE) & WS_VISIBLE) != 0) {
                RECT rc;
                GetWindowRect(hc, &rc);
                MapWindowPoints(NULL, d->hPage, (POINT *)&rc, 2);
                ns_tip_move_rect(d->hTip, d->hPage, (UINT_PTR)k_rect_tools[i], &rc);
            } else {
                ns_tip_move_rect(d->hTip, d->hPage, (UINT_PTR)k_rect_tools[i], NULL);
            }
        }
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

    int fh = ns_scale(16, m.dpi);
    int fy = buttons.y + (buttons.h - fh) / 2;
    int fw = ok_x - m.gap - (buttons.x + m.pad);
    if (fw < 0) fw = 0;
    SetWindowPos(d->hFooter, NULL, buttons.x + m.pad, fy, fw, fh,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    relayout_page(d, &m, page_w, content.h);
}

/* The clear-text warning under Base URL: shown while the provider is
 * "custom" and the typed URL is plain http to a host that is not loopback.
 * sync() only sets the flag (returns 1 if it changed); update() also relays
 * the page out. The URL is read as typed; nothing here changes what is saved. */
static int ai_url_warn_apply(SettingsDlgData *d)
{
    if (!d || !d->ctrl_ai_url_warn) return 0;
    int sel = (int)SendDlgItemMessage(d->hPage, IDC_AI_PROVIDER_COMBO,
                                      CB_GETCURSEL, 0, 0);
    int show = 0;
    if (sel >= 0 && sel < NUM_AI_PROVIDERS &&
        _stricmp(k_ai_providers[sel], "custom") == 0) {
        char url[512];
        url[0] = '\0';
        GetDlgItemText(d->hPage, IDC_AI_CUSTOM_URL, url, (int)sizeof(url));
        show = net_url_cleartext_remote(url);
    }
    int changed = (d->ctrl_ai_url_warn->visible != show);
    d->ctrl_ai_url_warn->visible = show;
    return changed;
}

/* The Enable AI assistant box decides whether the rest of the three AI pages
 * can be used: every control on them, the box itself excepted, is enabled
 * only while it is ticked. Refresh also needs the gate open, because a key
 * typed here is not used (ns_winhttp_open refuses) until the setting is saved
 * on. Called when the box is clicked and when a model fetch ends. */
static void ai_enable_sync(SettingsDlgData *d)
{
    if (!d || !d->hPage) return;
    int on = (IsDlgButtonChecked(d->hPage, IDC_AI_ENABLE) == BST_CHECKED);
    HWND box = GetDlgItem(d->hPage, IDC_AI_ENABLE);
    for (int i = 0; i < d->n_ctrls; i++) {
        const SettingsCtrl *c = &d->ctrls[i];
        if (c->page != SETTINGS_PAGE_AI_PROVIDER &&
            c->page != SETTINGS_PAGE_AI_BEHAVIOUR &&
            c->page != SETTINGS_PAGE_AI_WEB)
            continue;
        if (c->hLabel) EnableWindow(c->hLabel, on ? TRUE : FALSE);
        if (c->hCtrl && c->hCtrl != box) EnableWindow(c->hCtrl, on ? TRUE : FALSE);
        if (c->hExtra)  EnableWindow(c->hExtra, on ? TRUE : FALSE);
        if (c->hExtra2) EnableWindow(c->hExtra2, on ? TRUE : FALSE);
    }
    HWND refresh = GetDlgItem(d->hPage, IDC_AI_REFRESH);
    if (refresh)
        EnableWindow(refresh, (on && ai_gate_on()) ? TRUE : FALSE);
}

/* ---- Voice page ---------------------------------------------------------- */

/* The Voice page follows the two switches in this dialog, not the saved
 * ones: Enable voice needs the AI tick, every other row needs both. */
static int voice_ai_ticked(const SettingsDlgData *d)
{
    return IsDlgButtonChecked(d->hPage, IDC_AI_ENABLE) == BST_CHECKED;
}

static int voice_ticked(const SettingsDlgData *d)
{
    return IsDlgButtonChecked(d->hPage, IDC_VOICE_ENABLE) == BST_CHECKED;
}

/* Why "Use current AI assistant" cannot be ticked for the provider and URL
 * now showing on the AI Provider page, or NULL. */
static const char *voice_use_ai_reason(const SettingsDlgData *d)
{
    int sel = (int)SendDlgItemMessage(d->hPage, IDC_AI_PROVIDER_COMBO,
                                      CB_GETCURSEL, 0, 0);
    const char *prov = (sel >= 0 && sel < NUM_AI_PROVIDERS) ? k_ai_providers[sel] : "";
    char url[512];
    url[0] = '\0';
    GetDlgItemText(d->hPage, IDC_AI_CUSTOM_URL, url, (int)sizeof(url));
    return voice_stt_use_ai_reason(prov, url);
}

/* The speech-to-text provider the model list is seeded from: the chat
 * provider while the assistant's own is used, else the Voice page's combo. */
static const SttProvider *voice_effective_provider(const SettingsDlgData *d)
{
    if (IsDlgButtonChecked(d->hPage, IDC_VOICE_USE_AI) == BST_CHECKED) {
        int sel = (int)SendDlgItemMessage(d->hPage, IDC_AI_PROVIDER_COMBO,
                                          CB_GETCURSEL, 0, 0);
        const SttProvider *p = (sel >= 0 && sel < NUM_AI_PROVIDERS)
                                   ? voice_stt_provider_find(k_ai_providers[sel]) : NULL;
        return p ? p : voice_stt_provider_find("custom");
    }
    size_t n = 0;
    const SttProvider *tab = voice_stt_providers(&n);
    int sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_PROVIDER, CB_GETCURSEL, 0, 0);
    return (sel >= 0 && (size_t)sel < n) ? &tab[sel] : NULL;
}

/* Refill the Model list from the effective provider; the text now in the box
 * (the current value) is kept. */
static void voice_model_seed(SettingsDlgData *d)
{
    HWND cmb = GetDlgItem(d->hPage, IDC_VOICE_MODEL);
    if (!cmb) return;
    const SttProvider *p = voice_effective_provider(d);
    if (p == d->voice_seeded && SendMessage(cmb, CB_GETCOUNT, 0, 0) > 0) return;
    const SttProvider *prev = d->voice_seeded;
    d->voice_seeded = p;
    char cur[CFG_STR_MAX];
    cur[0] = '\0';
    GetWindowText(cmb, cur, (int)sizeof(cur));
    /* Empty means "the provider's default model": show that default. A text
     * that is one of the previous provider's seeds follows the provider too;
     * one the user typed is kept. */
    int follow = (cur[0] == '\0');
    for (size_t k = 0; !follow && prev && prev != p && prev->models && prev->models[k]; k++)
        if (strcmp(cur, prev->models[k]) == 0) follow = 1;
    SendMessage(cmb, CB_RESETCONTENT, 0, 0);
    if (p && p->models)
        for (size_t i = 0; p->models[i]; i++)
            SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)p->models[i]);
    if (follow && p && p->models && p->models[0])
        SetWindowText(cmb, p->models[0]);
    else
        SetWindowText(cmb, cur);
}

/* The page's current choice, not the saved settings: the Provider combo, or the
 * AI provider while Use current AI assistant is ticked. */
static int voice_live_page_kind(const SettingsDlgData *d, VoiceLiveProvider *prov)
{
    size_t n = 0;
    const SttProvider *tab = voice_stt_providers(&n);
    int sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_PROVIDER, CB_GETCURSEL, 0, 0);
    const char *pid = (sel >= 0 && (size_t)sel < n) ? tab[sel].id : NULL;
    int use_ai = IsDlgButtonChecked(d->hPage, IDC_VOICE_USE_AI) == BST_CHECKED;
    int asel = (int)SendDlgItemMessage(d->hPage, IDC_AI_PROVIDER_COMBO, CB_GETCURSEL, 0, 0);
    const char *aid = (asel >= 0 && asel < NUM_AI_PROVIDERS) ? k_ai_providers[asel] : NULL;
    int kind = voice_live_row_kind(pid, use_ai, aid);
    const char *eff = use_ai ? aid : pid;
    if (prov)
        *prov = !kind ? VLP_NONE : (strcmp(eff, "mistral") == 0 ? VLP_MISTRAL : VLP_OPENAI);
    return kind;
}

/* Refill the Live model list from the effective provider. Empty means the
 * provider's default (shown as the cue); a text that is one of the previous
 * provider's seeds is cleared with the provider, one the user typed is kept. */
static void voice_live_seed(SettingsDlgData *d)
{
    HWND cmb = GetDlgItem(d->hPage, IDC_VOICE_LIVE_MODEL);
    if (!cmb) return;
    VoiceLiveProvider p = VLP_NONE;
    (void)voice_live_page_kind(d, &p);
    if (p == VLP_NONE || (p == d->voice_live_seeded && SendMessage(cmb, CB_GETCOUNT, 0, 0) > 0))
        return;
    char cur[CFG_STR_MAX];
    cur[0] = '\0';
    GetWindowText(cmb, cur, (int)sizeof(cur));
    if (d->voice_live_seeded != VLP_NONE && d->voice_live_seeded != p) {
        const char *const *prev = voice_live_models(d->voice_live_seeded);
        for (size_t k = 0; prev && prev[k]; k++)
            if (strcmp(cur, prev[k]) == 0) cur[0] = '\0';
    }
    d->voice_live_seeded = p;
    SendMessage(cmb, CB_RESETCONTENT, 0, 0);
    const char *const *models = voice_live_models(p);
    for (size_t i = 0; models && models[i]; i++)
        SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)models[i]);
    SetWindowText(cmb, cur);
}

static int voice_url_is_cleartext(const SettingsDlgData *d, int edit_id)
{
    char url[512];
    url[0] = '\0';
    GetDlgItemText(d->hPage, edit_id, url, (int)sizeof(url));
    return net_url_cleartext_remote(url);
}

/* Which Voice rows show: the reason notes, and the Provider, API key and Base
 * URL rows (hidden while the assistant's own provider is used). Returns 1
 * when a row came or went, so the caller relays the page out. */
static int voice_rows_apply(SettingsDlgData *d)
{
    if (!d || !d->hPage || !d->ctrl_voice_provider) return 0;
    int ai_on = voice_ai_ticked(d);
    const char *why = voice_use_ai_reason(d);
    int use_ai = d->voice_use_ai_wanted && why == NULL;
    int sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_PROVIDER, CB_GETCURSEL, 0, 0);
    size_t n = 0;
    const SttProvider *tab = voice_stt_providers(&n);
    int custom = (sel >= 0 && (size_t)sel < n && strcmp(tab[sel].id, "custom") == 0);

    CheckDlgButton(d->hPage, IDC_VOICE_USE_AI, use_ai ? BST_CHECKED : BST_UNCHECKED);
    SetWindowText(d->hVoiceAiNote, why ? why : "");
    const char *blocked = NULL;
    {
        Settings tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.ai_enabled = ai_on;
        blocked = opt_feature_blocked_reason(&tmp, OPT_AI);
    }
    SetWindowText(d->hVoiceBlock, blocked ? blocked : "");

    int changed = 0;
    struct { SettingsCtrl *c; int show; } rows[] = {
        { d->ctrl_voice_block,    blocked != NULL },
        { d->ctrl_voice_ai_note,  why != NULL },
        { d->ctrl_voice_provider, !use_ai },
        { d->ctrl_voice_key,      !use_ai },
        { d->ctrl_voice_url,      !use_ai && custom },
        { d->ctrl_voice_url_warn, !use_ai && custom && voice_url_is_cleartext(d, IDC_VOICE_URL) },
        { d->ctrl_voice_live,      voice_live_page_kind(d, NULL) },
        { d->ctrl_voice_live_note, !voice_live_page_kind(d, NULL) },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        if (!rows[i].c) continue;
        if (rows[i].c->visible != rows[i].show) changed = 1;
        rows[i].c->visible = rows[i].show;
    }
    voice_model_seed(d);
    voice_live_seed(d);
    return changed;
}

/* The Mode combo's choice (the saved mode until the combo exists). */
static int voice_mode_selected(const SettingsDlgData *d)
{
    HWND cmb = GetDlgItem(d->hPage, IDC_VOICE_MODE);
    if (!cmb) return d->cfg->settings.voice_mode;
    return SendMessage(cmb, CB_GETCURSEL, 0, 0) == 1 ? VOICE_MODE_CONVERSATION
                                                     : VOICE_MODE_DICTATION;
}

/* Enable or disable the control `id` and its label together. */
static void voice_row_enable(const SettingsDlgData *d, int id, int enable)
{
    HWND h = GetDlgItem(d->hPage, id);
    if (!h) return;
    EnableWindow(h, enable ? TRUE : FALSE);
    for (int i = 0; i < d->n_ctrls; i++) {
        const SettingsCtrl *c = &d->ctrls[i];
        if (c->page != SETTINGS_PAGE_VOICE || c->hCtrl != h) continue;
        if (c->hLabel) EnableWindow(c->hLabel, enable ? TRUE : FALSE);
        if (c->hExtra) EnableWindow(c->hExtra, enable ? TRUE : FALSE);
        break;
    }
}

/* The voice Play sample speaks: the combo's choice, with Automatic resolved
 * as voice_choose_auto does ("" = the Windows default voice). */
static const char *voice_win_chosen_id(const SettingsDlgData *d)
{
    int sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_WIN_VOICE, CB_GETCURSEL, 0, 0);
    if (sel >= 1 && (size_t)(sel - 1) < d->voice_win_n) return d->voice_win[sel - 1].id;
    if (sel >= 1) return d->cfg->settings.voice_windows_voice;
    int at = voice_choose_auto(d->voice_win, d->voice_win_n);
    return at >= 0 ? d->voice_win[at].id : "";
}

/* ---- AI speech rows ------------------------------------------------------ */

/* The Speech combo's choice (the saved engine until the combo exists). */
static int voice_speech_selected(const SettingsDlgData *d)
{
    HWND cmb = GetDlgItem(d->hPage, IDC_VOICE_SPEECH);
    if (!cmb) return d->cfg->settings.voice_speech_engine;
    return SendMessage(cmb, CB_GETCURSEL, 0, 0) == 1 ? VOICE_SPEECH_AI : VOICE_SPEECH_WINDOWS;
}

/* The provider the page has selected, or NULL (an unknown stored id shows no
 * selection, R38). */
static const VoiceAisProvider *voice_ais_page_provider(const SettingsDlgData *d)
{
    size_t n = 0;
    const VoiceAisProvider *tab = voice_ais_providers(&n);
    int sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_TTS_PROVIDER, CB_GETCURSEL, 0, 0);
    return (sel >= 0 && (size_t)sel < n) ? &tab[sel] : NULL;
}

/* Fills the AI voice and AI speech model lists from the provider's seeds, the
 * default first. voice_txt / model_txt are the text to show; NULL means "a
 * provider change": Mistral's default, Custom's empty. An unknown provider
 * keeps its lists empty and its text. */
static void voice_ais_seed(SettingsDlgData *d, const char *voice_txt, const char *model_txt)
{
    const VoiceAisProvider *p = voice_ais_page_provider(d);
    HWND cv = GetDlgItem(d->hPage, IDC_VOICE_AI_VOICE);
    HWND cm = GetDlgItem(d->hPage, IDC_VOICE_AI_MODEL);
    if (!cv || !cm) return;
    SendMessage(cv, CB_RESETCONTENT, 0, 0);
    SendMessage(cm, CB_RESETCONTENT, 0, 0);
    if (!p) {
        if (voice_txt) SetWindowText(cv, voice_txt);
        if (model_txt) SetWindowText(cm, model_txt);
        return;
    }
    for (size_t i = 0; p->voices && p->voices[i]; i++)
        SendMessage(cv, CB_ADDSTRING, 0, (LPARAM)p->voices[i]);
    for (size_t i = 0; p->models && p->models[i]; i++)
        SendMessage(cm, CB_ADDSTRING, 0, (LPARAM)p->models[i]);
    const int deflt = strcmp(p->id, "mistral") == 0;
    const char *dv = deflt && p->voices ? p->voices[0] : "";
    const char *dm = deflt && p->models ? p->models[0] : "";
    SetWindowText(cv, voice_txt ? (voice_txt[0] ? voice_txt : dv) : dv);
    SetWindowText(cm, model_txt ? (model_txt[0] ? model_txt : dm) : dm);
}

/* A copy of the saved settings with the Voice page's current values read into
 * it (nothing saved). It holds keys: secure_zero and free it (voice_page_free). */
static Settings *voice_page_copy(SettingsDlgData *d)
{
    Settings *c = (Settings *)malloc(sizeof(*c));
    if (!c) return NULL;
    *c = d->cfg->settings;
    voice_page_read(d, c);
    return c;
}

static void voice_page_free(Settings *c)
{
    if (!c) return;
    secure_zero(c, sizeof(*c));
    free(c);
}

/* Sets the speech key edit without counting it as the user's edit. */
static void voice_tts_key_show(SettingsDlgData *d, const char *text)
{
    char cur[CFG_STR_MAX];
    cur[0] = '\0';
    GetDlgItemText(d->hPage, IDC_VOICE_TTS_KEY, cur, (int)sizeof(cur));
    if (strcmp(cur, text) != 0) {
        d->voice_tts_key_setting = 1;
        SetDlgItemText(d->hPage, IDC_VOICE_TTS_KEY, text);
        d->voice_tts_key_setting = 0;
    }
    secure_zero(cur, sizeof(cur));
}

/* The speech rows for the page's own state: which show, the key edit following
 * the provider binding while the user has not touched it (a saved key shows
 * only for the provider it was entered for, R43), and the privacy line.
 * conv is the Conversation section's state. Returns 1 when a row came or went. */
static int voice_speech_apply(SettingsDlgData *d, int conv)
{
    if (!d->ctrl_tts_provider) return 0;
    Settings *c = voice_page_copy(d);
    if (!c) return 0;
    if (!d->voice_tts_key_touched) {
        char bind[CFG_STR_MAX];
        voice_ais_binding(c, bind, sizeof(bind));
        const Settings *sv = &d->cfg->settings;
        int match = bind[0] && strcmp(sv->tts_api_key_for, bind) == 0;
        voice_tts_key_show(d, match ? sv->tts_api_key : "");
        secure_zero(bind, sizeof(bind));
    }
    const int shared = voice_ais_key_shared(c) != VAIS_KEY_OWN;
    VoiceSpeechEnables en;
    voice_speech_enables(conv, c->voice_speech_engine, c->voice_tts_provider, shared,
                         d->voice_uk_installed, &en);
    char host[256];
    host[0] = '\0';
    {
        VoiceAisTarget t;
        (void)voice_ais_resolve(c, &t, NULL, 0);   /* a failed resolve keeps the host it found */
        (void)snprintf(host, sizeof(host), "%s", t.host);
    }
    char line[320];
    (void)snprintf(line, sizeof(line), "Each spoken reply's text is sent to %s.",
                   host[0] ? host : "the speech server");
    SetWindowText(d->hVoicePrivacy, line);
    const int warn = en.url && voice_url_is_cleartext(d, IDC_VOICE_TTS_URL);
    struct { SettingsCtrl *c; int show; } rows[] = {
        { d->ctrl_tts_provider, en.ai_rows },
        { d->ctrl_tts_url,      en.url },
        { d->ctrl_tts_url_warn, warn },
        { d->ctrl_tts_key,      en.key },
        { d->ctrl_ais_voice,    en.ai_rows },
        { d->ctrl_ais_model,    en.ai_rows },
        { d->ctrl_ais_style,    en.style },
        { d->ctrl_ais_privacy,  en.privacy },
        { d->ctrl_voice_note_uk, en.uk_line },
    };
    int changed = 0;
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        if (!rows[i].c) continue;
        if (rows[i].c->visible != rows[i].show) changed = 1;
        rows[i].c->visible = rows[i].show;
    }
    voice_row_enable(d, IDC_VOICE_SPEECH, en.speech);
    voice_page_free(c);
    return changed;
}

/* Play sample: a conversation's speech is already stopped (the dialog is modal, so the
 * conversation is Paused); it is not ended here. Then speak. The
 * WM_VOICE_TTS messages go to the main window (the Settings window's owner),
 * which reaps the speech thread on VTT_EXITED. sapi.dll loads here, on the click.
 * AI speech: the page's unsaved values are resolved (R17) and a resolve error
 * shows in the dim note under the meter; nothing is sent when it fails. */
static void voice_play_sample(SettingsDlgData *d)
{
    HWND owner = GetWindow(GetParent(d->hPage), GW_OWNER);
    if (!owner) return;
    if (voice_speech_selected(d) == VOICE_SPEECH_AI) {
        Settings *c = voice_page_copy(d);
        if (!c) return;
        VoiceAisTarget t;
        char err[160];
        if (!voice_ais_resolve(c, &t, err, sizeof(err))) {
            voice_test_note(d, err);
        } else if (!voice_ais_sample(owner, &t, voice_ais_key(c, &t))) {
            voice_test_note(d, "Voice is not running.");
        } else {
            voice_test_note(d, "");
        }
        voice_page_free(c);
        return;
    }
    (void)voice_tts_sample(owner, voice_win_chosen_id(d));
}

/* The Enable voice box is usable only while the AI tick is on; every other
 * Voice row only while both ticks are. Use current AI assistant is also
 * disabled (and unticked) while the provider has no speech-to-text, the
 * device list and Test only once voice has been saved on, and Test never in
 * this revision. Called wherever ai_enable_sync is, and when a box or the AI
 * provider changes. */
static void voice_enable_sync(SettingsDlgData *d)
{
    if (!d || !d->hPage) return;
    int ai_on = voice_ai_ticked(d);
    int on = ai_on && voice_ticked(d);
    if (!on) voice_test_apply(d, VT_EV_VOICE_OFF);   /* a ticked-off feature ends the Test */
    HWND box = GetDlgItem(d->hPage, IDC_VOICE_ENABLE);
    for (int i = 0; i < d->n_ctrls; i++) {
        const SettingsCtrl *c = &d->ctrls[i];
        if (c->page != SETTINGS_PAGE_VOICE) continue;
        if (c->hLabel) EnableWindow(c->hLabel, on ? TRUE : FALSE);
        if (c->hCtrl && c->hCtrl != box) EnableWindow(c->hCtrl, on ? TRUE : FALSE);
        if (c->hExtra)  EnableWindow(c->hExtra, on ? TRUE : FALSE);
    }
    if (box) EnableWindow(box, ai_on ? TRUE : FALSE);
    int changed = voice_rows_apply(d);
    HWND use_ai = GetDlgItem(d->hPage, IDC_VOICE_USE_AI);
    if (use_ai)
        EnableWindow(use_ai, (on && voice_use_ai_reason(d) == NULL) ? TRUE : FALSE);
    HWND dev = GetDlgItem(d->hPage, IDC_VOICE_DEVICE);
    if (dev) EnableWindow(dev, (on && d->voice_dev_listed) ? TRUE : FALSE);
    HWND gain = GetDlgItem(d->hPage, IDC_VOICE_GAIN);
    if (gain) EnableWindow(gain, (on && (d->voice_dev_listed || d->demo == 2)) ? TRUE : FALSE);   /* the demo shows the slider live */
    /* Test needs the listed microphones (voice saved on); while it runs the
     * button is the Stop control, so it stays enabled. */
    HWND test = GetDlgItem(d->hPage, IDC_VOICE_TEST);
    if (test)
        EnableWindow(test, (on && d->voice_dev_listed) ? TRUE : FALSE);
    /* Mode: Conversation greys Key mode, the Mic start delay and auto-send;
     * Dictation greys the whole Conversation section, Play sample included. */
    {
        VoiceModeEnables en;
        voice_mode_enables(on, voice_mode_selected(d),
                           (int)SendDlgItemMessage(d->hPage, IDC_VOICE_PTT_MODE, CB_GETCURSEL, 0, 0), &en);
        voice_row_enable(d, IDC_VOICE_PTT_MODE, en.key_mode);
        voice_row_enable(d, IDC_VOICE_PTT_KEY, on);
        voice_row_enable(d, IDC_VOICE_MIC_DELAY, en.mic_delay);
        voice_row_enable(d, IDC_VOICE_AUTO_SEND, en.auto_send);
        voice_row_enable(d, IDC_VOICE_PAUSE, en.conversation);
        voice_row_enable(d, IDC_VOICE_WIN_VOICE, en.conversation);
        voice_row_enable(d, IDC_VOICE_PLAY_SAMPLE, en.conversation);
        if (d->hVoiceHead[2]) EnableWindow(d->hVoiceHead[2], en.conversation ? TRUE : FALSE);
        if (d->hVoiceNoteVoice) EnableWindow(d->hVoiceNoteVoice, en.conversation ? TRUE : FALSE);
        if (d->hVoiceNoteUk) EnableWindow(d->hVoiceNoteUk, en.conversation ? TRUE : FALSE);
        if (d->hVoiceNoteAec) EnableWindow(d->hVoiceNoteAec, en.conversation ? TRUE : FALSE);
        /* The ducking line is for Conversation mode only. */
        if (d->ctrl_voice_note_duck) {
            const int show = voice_mode_selected(d) == VOICE_MODE_CONVERSATION;
            if (d->ctrl_voice_note_duck->visible != show) changed = 1;
            d->ctrl_voice_note_duck->visible = show;
        }
        if (voice_speech_apply(d, en.conversation)) changed = 1;
    }
    if (changed && d->cur_page == SETTINGS_PAGE_VOICE) {
        relayout(GetParent(d->hPage), d);
        InvalidateRect(d->hPage, NULL, TRUE);
    }
}

static void ai_url_warn_sync(SettingsDlgData *d)
{
    (void)ai_url_warn_apply(d);
}

static void ai_url_warn_update(SettingsDlgData *d)
{
    if (!ai_url_warn_apply(d)) return;
    HWND main_wnd = GetParent(d->hPage);
    relayout(main_wnd, d);
    InvalidateRect(d->hPage, NULL, TRUE);
}

/* ---- Page host window procedure ------------------------------------------
 * Every page control is a child of this window. It handles theme colours,
 * owner-draw for the refresh button, forwards WM_COMMAND to the main
 * window, and owns scrolling (both the AI notes edit and the whole page). */

/* ---- Voice page: microphones, Test and the meter ------------------------ */

/* The Language drop-down holds the table's labels and "Other..." last. */
static int voice_lang_other_index(void)
{
    size_t n = 0;
    (void)voice_lang_table(&n);
    return (int)n;
}

static int voice_lang_is_other(const SettingsDlgData *d)
{
    return (int)SendDlgItemMessage(d->hPage, IDC_VOICE_LANG, CB_GETCURSEL, 0, 0)
           == voice_lang_other_index();
}

/* Show the Other box and its hint only while "Other..." is picked, and keep the
 * hint's text and tone in step with the box. Relays the page out when a row
 * came or went. */
static void voice_lang_sync(SettingsDlgData *d, int relay)
{
    if (!d || !d->hPage || !d->ctrl_voice_lang || !d->ctrl_voice_lang_hint) return;
    int other = voice_lang_is_other(d);
    int changed = (d->ctrl_voice_lang->extra_off == other) ||
                  (d->ctrl_voice_lang_hint->visible != other);
    d->ctrl_voice_lang->extra_off = other ? 0 : 1;
    d->ctrl_voice_lang_hint->visible = other;
    if (other) {
        char txt[64], hint[160];
        txt[0] = '\0';
        GetDlgItemText(d->hPage, IDC_VOICE_LANG_OTHER, txt, (int)sizeof(txt));
        d->voice_lang_tone = voice_lang_other_hint_loaded(txt, d->voice_lang_loaded_other, hint, sizeof(hint));
        SetWindowText(d->hVoiceLangHint, hint);
        InvalidateRect(d->hVoiceLangHint, NULL, TRUE);
    }
    if (relay && changed && d->cur_page == SETTINGS_PAGE_VOICE) {
        relayout(GetParent(d->hPage), d);
        InvalidateRect(d->hPage, NULL, TRUE);
    }
}

static void voice_meter_redraw(SettingsDlgData *d)
{
    HWND m = GetDlgItem(d->hPage, IDC_VOICE_METER);
    if (m) InvalidateRect(m, NULL, FALSE);
}

/* The dim line under the meter ("" hides the row). */
static void voice_test_note(SettingsDlgData *d, const char *text)
{
    if (!d->hVoiceTestNote || !d->ctrl_voice_test_note) return;
    SetWindowText(d->hVoiceTestNote, text);
    int show = text[0] ? 1 : 0;
    if (d->ctrl_voice_test_note->visible != show) {
        d->ctrl_voice_test_note->visible = show;
        if (d->cur_page == SETTINGS_PAGE_VOICE) {
            relayout(d->hDlg, d);
            InvalidateRect(d->hPage, NULL, TRUE);
        }
    }
}

/* The slider's gain in dB (the saved value until the slider exists). */
static int voice_gain_now(const SettingsDlgData *d)
{
    HWND sl = GetDlgItem(d->hPage, IDC_VOICE_GAIN);
    return sl ? voice_gain_clamp(ns_slider_get(sl))
              : voice_gain_clamp(d->cfg->settings.voice_mic_gain_db);
}

/* The slider moved: refresh the "+12 dB" label and, while a Test runs, retune
 * the live capture. */
static void voice_gain_changed(SettingsDlgData *d)
{
    char txt[16];
    voice_gain_label(voice_gain_now(d), txt, sizeof(txt));
    SetDlgItemText(d->hPage, IDC_VOICE_GAIN_VALUE, txt);
    if (d->voice_testing) voice_audio_set_gain(voice_gain_now(d));
}

/* The endpoint ID the device combo currently stands for ("" = Windows default). */
static const char *voice_dev_selected_id(const SettingsDlgData *d)
{
    int sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_DEVICE, CB_GETCURSEL, 0, 0);
    if (sel >= 1 && sel - 1 < d->voice_dev_n) return d->voice_dev[sel - 1].id;
    if (sel >= 1) return d->cfg->settings.voice_input_device;   /* before the list arrives */
    return "";
}

static void voice_test_button_sync(SettingsDlgData *d)
{
    HWND test = GetDlgItem(d->hPage, IDC_VOICE_TEST);
    if (!test) return;
    SetWindowText(test, voice_test_button_label(d->voice_testing));
    InvalidateRect(test, NULL, TRUE);
}

/* Begins a capture on the selected microphone at the slider's gain with a
 * fresh meter. Returns 0 (and leaves nothing running) when voice is off. */
static int voice_test_begin(SettingsDlgData *d)
{
    /* The page was built with voice on; it may have gone off since. */
    if (!voice_gate_on() || !voice_feature_running()) {
        voice_test_note(d, "Voice is not running.");
        return 0;
    }
    voice_test_note(d, "");
    voice_meter_reset(&d->vmeter);
    d->vmeter_tick = GetTickCount64();
    voice_audio_capture_begin(voice_dev_selected_id(d), 0, 1, voice_gain_now(d), VOICE_CAP_MS, 0);
    return 1;
}

/* The one place a Test starts, stops or restarts: every trigger names its
 * event and voice_test_next (the tested state machine) decides. */
static void voice_test_apply(SettingsDlgData *d, VoiceTestEvent ev)
{
    if (!d || !d->hPage) return;
    int can_start = voice_ai_ticked(d) && voice_ticked(d) && d->voice_dev_listed;
    VoiceTestAction act = voice_test_next(&d->voice_testing, ev, can_start);
    switch (act) {
    case VT_ACT_START:
        if (!voice_test_begin(d)) d->voice_testing = 0;
        break;
    case VT_ACT_STOP:
        voice_audio_capture_end(0);   /* the test is over, nothing is kept */
        voice_meter_reset(&d->vmeter);
        voice_test_note(d, "");
        break;
    case VT_ACT_RESTART:
        voice_audio_capture_end(0);
        if (!voice_test_begin(d)) d->voice_testing = 0;
        break;
    case VT_ACT_NONE:
    default:
        return;
    }
    voice_test_button_sync(d);
    voice_meter_redraw(d);
}

/* Adds a UTF-8 name to a combo as wide text, so a non-Latin microphone name
 * shows as itself (an ANSI CB_ADDSTRING would turn it into question marks). */
static int voice_combo_add_utf8(HWND cmb, const char *utf8)
{
    wchar_t w[320];
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, (int)(sizeof(w) / sizeof(w[0]))) <= 0)
        w[0] = L'\0';
    return (int)SendMessageW(cmb, CB_ADDSTRING, 0, (LPARAM)w);
}

/* The device list arrived: refill the combo and keep the selection by ID. */
static void voice_dev_take_list(SettingsDlgData *d, const VoiceDeviceList *l)
{
    char want[256];
    snprintf(want, sizeof(want), "%s", d->voice_dev_touched
                 ? voice_dev_selected_id(d)
                 : d->cfg->settings.voice_input_device);
    d->voice_dev_n = 0;
    for (int i = 0; i < l->n && i < 32; i++) {
        if (WideCharToMultiByte(CP_UTF8, 0, l->dev[i].id, -1, d->voice_dev[d->voice_dev_n].id,
                                (int)sizeof(d->voice_dev[0].id), NULL, NULL) <= 0)
            continue;
        snprintf(d->voice_dev[d->voice_dev_n].name, sizeof(d->voice_dev[0].name),
                 "%s", l->dev[i].name);
        d->voice_dev_n++;
    }
    int sel = 0;
    for (int i = 0; i < d->voice_dev_n; i++)
        if (want[0] && strcmp(want, d->voice_dev[i].id) == 0) sel = i + 1;
    d->voice_dev_missing = (want[0] && sel == 0 && !d->voice_dev_touched) ? 1 : 0;

    char first[300];
    if (d->voice_dev_missing) {
        const Settings *vs = &d->cfg->settings;
        snprintf(first, sizeof(first), "Windows default (saved microphone not found: %s)",
                 vs->voice_input_device_name[0] ? vs->voice_input_device_name
                                                : vs->voice_input_device);
    } else {
        snprintf(first, sizeof(first), "Windows default");
    }
    HWND cmb = GetDlgItem(d->hPage, IDC_VOICE_DEVICE);
    if (!cmb) return;
    SendMessage(cmb, CB_RESETCONTENT, 0, 0);
    voice_combo_add_utf8(cmb, first);
    for (int i = 0; i < d->voice_dev_n; i++)
        voice_combo_add_utf8(cmb, d->voice_dev[i].name);
    SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
}

void settings_voice_sample_failed(long hr)
{
    SettingsDlgData *d = g_voice_dlg;   /* cleared in WM_DESTROY before the dialog's data is freed */
    if (!d || !d->hDlg || !IsWindow(d->hDlg) || !d->hPage || d->cur_page != SETTINGS_PAGE_VOICE) return;
    char note[256];
    voice_ais_sample_failure_note(hr, note, sizeof(note));
    voice_test_note(d, note);
}

int settings_voice_on_audio(WPARAM wp, LPARAM lp)
{
    SettingsDlgData *d = g_voice_dlg;
    if (!d || !d->voice_dev_listed || !d->hPage) return 0;
    switch ((VoiceAudioMsg)wp) {
    case VAM_DEVICES:
        voice_dev_take_list(d, (const VoiceDeviceList *)lp);
        voice_audio_free_devices((VoiceDeviceList *)lp);
        return 1;
    case VAM_DEVICES_CHANGED:
        voice_audio_enumerate();
        return 1;
    case VAM_LEVEL:
        if (d->voice_testing) {
            {   /* raw window peak 0..32768 into the meter, with the time since the last one */
                int pk = (int)lp < 0 ? 0 : ((int)lp > 32768 ? 32768 : (int)lp);
                ULONGLONG now = GetTickCount64();
                ULONGLONG dt = now - d->vmeter_tick;
                d->vmeter_tick = now;
                voice_meter_feed(&d->vmeter, pk, dt > 1000u ? 1000u : (uint32_t)dt);
            }
            voice_meter_redraw(d);
        }
        return 1;
    case VAM_ERROR:
        /* Only a capture error is the Test's result; an enumeration failure
         * (VAE_COM_FAILED) arriving meanwhile is not. */
        if (d->voice_testing && (VoiceAudioErr)lp != VAE_COM_FAILED) {
            voice_test_apply(d, VT_EV_CAPTURE_FAILED);
            switch ((VoiceAudioErr)lp) {
            case VAE_NO_DEVICE:    voice_test_note(d, "No microphone was found."); break;
            case VAE_DEVICE_LOST:  voice_test_note(d, "The microphone stopped responding."); break;
            case VAE_ACCESS_DENIED:
                voice_test_note(d, "Windows may be blocking the microphone: Settings > Privacy & security > Microphone");
                break;
            default:               voice_test_note(d, "The microphone could not be opened."); break;
            }
        }
        return 1;
    case VAM_READY:
        return 1;
    default:
        return 0;
    }
}

/* The meter row: the zoned bar on the left, the verdict on the right. The
 * label's width is reserved from the widest text it can show, measured in
 * the caption font, so the bar does not jump as the verdict changes. */
static void voice_meter_paint(const SettingsDlgData *d, const DRAWITEMSTRUCT *dis)
{
    static const char k_idle[] = "Press Test to check the level";
    const ThemeTokens *tk = ns_tokens();
    HDC hdc = dis->hDC;
    const RECT *rc = &dis->rcItem;
    FillRect(hdc, rc, d->hBrBgPrimary);

    const char *text = k_idle;
    unsigned int text_rgb = tk->text_dim;
    double level = 0.0, hold = 0.0;
    if (d->voice_testing || d->demo == 2) {
        double level_db = d->vmeter.level_db, hold_db = d->vmeter.hold_db;
        VoiceLevelClass c;
        if (d->demo == 2) {
            /* --ui-demo=settings-voice: a fixed GOOD level with a hold tick, no capture. */
            level_db = (VOICE_LEVEL_GOOD_LO_DB + VOICE_LEVEL_GOOD_HI_DB) / 2.0;
            hold_db = level_db + 3.0;
            c = VLEVEL_GOOD;
        } else {
            c = voice_level_classify(voice_meter_window_peak_db(&d->vmeter));
        }
        level = voice_meter_frac(level_db);
        hold = voice_meter_frac(hold_db);
        text = voice_level_label(c);
        switch (c) {
        case VLEVEL_SILENT: text_rgb = tk->text_dim; break;
        case VLEVEL_QUIET:  text_rgb = tk->warning.base; break;
        case VLEVEL_LOUD:   text_rgb = tk->danger.base; break;
        default:            text_rgb = tk->success.base; break;
        }
    }

    HFONT old_font = (HFONT)SelectObject(hdc, ns_font(FONT_CAPTION, d->dpi));
    int label_w = 0;
    {
        const char *all[] = { k_idle, voice_level_label(VLEVEL_SILENT), voice_level_label(VLEVEL_QUIET),
                              voice_level_label(VLEVEL_GOOD), voice_level_label(VLEVEL_HOT),
                              voice_level_label(VLEVEL_LOUD) };
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
            wchar_t w[128];
            if (MultiByteToWideChar(CP_UTF8, 0, all[i], -1, w, 128) <= 0) continue;
            SIZE sz;
            if (GetTextExtentPoint32W(hdc, w, (int)wcslen(w), &sz) && sz.cx > label_w)
                label_w = sz.cx;
        }
    }
    int total_w = rc->right - rc->left;
    int gap = ns_scale(SP_MD, d->dpi);
    if (label_w > total_w / 2) label_w = total_w / 2;
    int bar_w = total_w - label_w - gap;

    int row_h = rc->bottom - rc->top;
    int bar_h = ns_scale(SP_MD, d->dpi);
    if (bar_h > row_h) bar_h = row_h;
    if (bar_w > 0) {
        RECT bar = { rc->left, rc->top + (row_h - bar_h) / 2, rc->left + bar_w,
                     rc->top + (row_h - bar_h) / 2 + bar_h };
        const double zf[4] = { voice_meter_frac(VOICE_LEVEL_SILENT_DB),
                               voice_meter_frac(VOICE_LEVEL_GOOD_LO_DB),
                               voice_meter_frac(VOICE_LEVEL_GOOD_HI_DB),
                               voice_meter_frac(VOICE_LEVEL_LOUD_DB) };
        ns_draw_level_meter(hdc, &bar, level, hold, zf, tk);
    }

    wchar_t wt[128];
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wt, 128) > 0) {
        RECT tr = { rc->right - label_w, rc->top, rc->right, rc->bottom };
        int old_bk = SetBkMode(hdc, TRANSPARENT);
        COLORREF old_tc = SetTextColor(hdc, theme_cr(text_rgb));
        DrawTextW(hdc, wt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SetTextColor(hdc, old_tc);
        SetBkMode(hdc, old_bk);
    }
    SelectObject(hdc, old_font);
}

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
            if ((d->hUrlWarn && hCtl == d->hUrlWarn) ||
                (d->hVoiceUrlWarn && hCtl == d->hVoiceUrlWarn) ||
                (d->hVoiceTtsWarn && hCtl == d->hVoiceTtsWarn)) {
                SetTextColor((HDC)wParam, theme_cr(ns_tokens()->warning.base));
                SetBkColor((HDC)wParam, theme_cr(d->theme->bg_primary));
                return (LRESULT)d->hBrBgPrimary;
            }
            if (d->hVoiceLangHint && hCtl == d->hVoiceLangHint && d->voice_lang_tone == 1) {
                SetTextColor((HDC)wParam, theme_cr(ns_tokens()->warning.base));
                SetBkColor((HDC)wParam, theme_cr(d->theme->bg_primary));
                return (LRESULT)d->hBrBgPrimary;
            }
            int dim = (hCtl == d->hSshHint || hCtl == d->hAboutBlurb ||
                       hCtl == d->hCtxHint || hCtl == d->hToolTimeoutNote ||
                       hCtl == d->hToolRoundsNote ||
                       hCtl == d->hVoiceBlock || hCtl == d->hVoiceAiNote ||
                       hCtl == d->hVoiceLangHint || hCtl == d->hVoicePrivacy);
            for (int di = 0; di < d->n_dbg_dim && !dim; di++)
                dim = (hCtl == d->hDbgDim[di]);
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
            if ((int)dis->CtlID == IDC_AI_REFRESH ||
                (int)dis->CtlID == IDC_DBG_BROWSE ||
                (int)dis->CtlID == IDC_DBG_OPEN ||
                (int)dis->CtlID == IDC_VOICE_TEST ||
                (int)dis->CtlID == IDC_VOICE_PLAY_SAMPLE) {
                draw_themed_button(dis, d->theme, 0);
                return TRUE;
            }
            if ((int)dis->CtlID == IDC_VOICE_METER) {
                voice_meter_paint(d, dis);
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

/* ---- Debugging page helpers -------------------------------------------------
 * The row table is settings_debug_layout() (settings_layout.c) and the text
 * and number rules are settings_debug.c; this is only the Win32 glue. */

static SettingsCtrl *dbg_add(SettingsDlgData *d, int slot, HWND lbl, HWND ctrl,
                             HWND extra, int ctrl_w, int extra_w, int full_w)
{
    SettingsCtrl *c = add_ctrl(d, lbl, ctrl, extra, SETTINGS_PAGE_DEBUG, ctrl_w,
                               extra_w, settings_debug_slot_rows(slot), 0, 1, full_w);
    if (slot >= 0 && slot < SDBG_COUNT) d->dbg[slot] = c;
    return c;
}

/* A wrapped static in the dim text colour (WM_CTLCOLORSTATIC checks hDbgDim). */
static HWND dbg_dim_static(SettingsDlgData *d, const char *text)
{
    HWND h = make_hint(d, text);
    if (d->n_dbg_dim < (int)(sizeof(d->hDbgDim) / sizeof(d->hDbgDim[0])))
        d->hDbgDim[d->n_dbg_dim++] = h;
    return h;
}

/* The help id of each category's level row, in DlogCat order. */
static const SettingsHelpId k_dbg_level_help[DLOG_CAT_COUNT] = {
    SH_DBG_LEVEL_RENDER, SH_DBG_LEVEL_TERM, SH_DBG_LEVEL_AI,
    SH_DBG_LEVEL_SSH, SH_DBG_LEVEL_SHELL, SH_DBG_LEVEL_GENERAL
};

static void dbg_fill_level_combo(HWND cmb, int level)
{
    for (int l = 0; l < DLOG_LVL_COUNT; l++)
        SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)settings_debug_level_label(l));
    if (level < 0 || level >= DLOG_LVL_COUNT) level = DLOG_DEFAULT_LEVEL;
    SendMessage(cmb, CB_SETCURSEL, (WPARAM)level, 0);
}

/* Show the rows the tickbox calls for: unticked, the tickbox, its hint and the
 * current file; ticked, everything else too. */
static void debug_apply_visibility(SettingsDlgData *d)
{
    SettingsDebugPos pos[SDBG_COUNT];
    int on = IsDlgButtonChecked(d->hPage, IDC_DBG_ENABLE) == BST_CHECKED;
    (void)settings_debug_layout(on, pos);
    for (int i = 0; i < SDBG_COUNT; i++)
        if (d->dbg[i]) d->dbg[i]->visible = pos[i].visible;
}

/* The directory holding nutshell.exe ("" if it cannot be told). */
static void settings_exe_dir(char *buf, size_t n)
{
    if (!buf || n == 0) return;
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)n);
    if (len == 0u || len >= (DWORD)n) { buf[0] = '\0'; return; }
    char *last = strrchr(buf, '\\');
    if (last) *last = '\0';
    else buf[0] = '\0';
}

/* Browse...: pick a folder; one below the exe's folder is shown relative to it. */
static void debug_browse_folder(HWND hwnd, SettingsDlgData *d)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    char disp[MAX_PATH];
    disp[0] = '\0';
    BROWSEINFOA bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner      = hwnd;
    bi.pszDisplayName = disp;
    bi.lpszTitle      = "Choose the debug log folder";
    bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            char exe_dir[MAX_PATH];
            char rel[DLOG_MAX_PATH];
            settings_exe_dir(exe_dir, sizeof(exe_dir));
            if (dlog_relativize_dir(path, exe_dir, rel, sizeof(rel)))
                SetDlgItemTextA(d->hPage, IDC_DBG_FOLDER, rel);
        }
        CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(hr)) CoUninitialize();
}

/* Open: the folder as typed (resolved the way the logger resolves it), made if
 * it does not exist yet so Explorer has something to show. */
static void debug_open_folder(HWND hwnd, SettingsDlgData *d)
{
    char setting[CFG_STR_MAX];
    char exe_dir[MAX_PATH];
    char dir[DLOG_MAX_PATH];
    GetDlgItemTextA(d->hPage, IDC_DBG_FOLDER, setting, (int)sizeof(setting));
    settings_exe_dir(exe_dir, sizeof(exe_dir));
    if (!dlog_resolve_dir(setting, exe_dir, dir, sizeof(dir))) {
        (void)notice_dlg_msg(hwnd, NOTICE_MSG_LOG_FOLDER_INVALID, NULL, d->theme);
        return;
    }
    const DlogPlatform *pf = dlog_platform_system();
    if (pf && pf->mkdir_p) (void)pf->mkdir_p(dir);
    (void)open_dir_in_explorer(hwnd, dir, "Open Log Folder", d->theme);
}

/* The page's controls, in the order settings_debug_layout() packs them. */
static void debug_build_page(SettingsDlgData *nd)
{
    const Settings *st = &nd->cfg->settings;
    const int on = st->debug_enabled || nd->demo == 1;
    DlogStatus dst;
    unsigned long long used_bytes = 0;
    unsigned used_files = 0;
    char line[2 * DLOG_MAX_PATH + 320];

    dlog_status(&dst);
    dlog_folder_usage(&used_bytes, &used_files);

    {
        HWND chk = make_check2(nd, IDC_DBG_ENABLE, SH_DBG_ENABLE);
        SendMessage(chk, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        dbg_add(nd, SDBG_ENABLE, NULL, chk, NULL, 0, 0, 1);
    }
    dbg_add(nd, SDBG_HINT_ON, NULL,
            dbg_dim_static(nd, "Records what Nutshell does so an AI can diagnose a "
                               "problem you mark."), NULL, 0, 0, 1);
    dbg_add(nd, SDBG_HINT_OFF, NULL,
            dbg_dim_static(nd, "Errors are still written to the log folder."),
            NULL, 0, 0, 1);

    for (int cat = 0; cat < DLOG_CAT_COUNT; cat++) {
        HWND lbl = make_label2(nd, k_dbg_level_help[cat]);
        HWND cmb = make_combo2(nd, IDC_DBG_LEVEL_BASE + cat, CBS_DROPDOWNLIST,
                               k_dbg_level_help[cat]);
        dbg_fill_level_combo(cmb, st->debug_levels[cat]);
        dbg_add(nd, SDBG_LEVEL_RENDER + cat, lbl, cmb, NULL, 120, 0, 0);
    }

    {
        /* A tickbox with its level combo beside it: ctrl_w covers both. */
        HWND chk = make_check2(nd, IDC_DBG_RECORDER, SH_DBG_RECORDER);
        SendMessage(chk, BM_SETCHECK, st->debug_recorder ? BST_CHECKED : BST_UNCHECKED, 0);
        HWND cmb = make_combo2(nd, IDC_DBG_RECORDER_LEVEL, CBS_DROPDOWNLIST,
                               SH_DBG_RECORDER);
        dbg_fill_level_combo(cmb, st->debug_recorder_level);
        EnableWindow(cmb, st->debug_recorder ? TRUE : FALSE);
        dbg_add(nd, SDBG_RECORDER, NULL, chk, cmb, 300 + 10 + 90, 90, 1);
    }
    {
        HWND chk = make_check2(nd, IDC_DBG_SCREENSHOT, SH_DBG_SCREENSHOT);
        SendMessage(chk, BM_SETCHECK, st->debug_screenshot ? BST_CHECKED : BST_UNCHECKED, 0);
        dbg_add(nd, SDBG_SCREENSHOT, NULL, chk, NULL, 0, 0, 1);
    }
    {
        HWND chk = make_check2(nd, IDC_DBG_CONTENT, SH_DBG_CONTENT);
        SendMessage(chk, BM_SETCHECK, st->debug_include_content ? BST_CHECKED : BST_UNCHECKED, 0);
        dbg_add(nd, SDBG_CONTENT, NULL, chk, NULL, 0, 0, 1);
    }
    dbg_add(nd, SDBG_CONTENT_WARN, NULL,
            dbg_dim_static(nd, "Adds terminal text and the echo of what you type, AI "
                               "commands, chat text and host names. Passwords and "
                               "keys are never logged."),
            NULL, 0, 0, 1);

    {
        HWND lbl = make_label2(nd, SH_DBG_FOLDER);
        HWND ed  = make_edit2(nd, st->debug_log_dir, IDC_DBG_FOLDER, 0, SH_DBG_FOLDER);
        SendMessage(ed, EM_SETLIMITTEXT, (WPARAM)(sizeof(st->debug_log_dir) - 1u), 0);
        HWND browse = page_ctrl(nd, "BUTTON", "Browse...", BS_OWNERDRAW,
                                IDC_DBG_BROWSE, SH_DBG_BROWSE);
        HWND open = page_ctrl(nd, "BUTTON", "Open", BS_OWNERDRAW,
                              IDC_DBG_OPEN, SH_DBG_OPEN);
        SettingsCtrl *c = dbg_add(nd, SDBG_FOLDER, lbl, ed, browse, 0, 84, 0);
        if (c) { c->hExtra2 = open; c->extra2_w = 56; }
    }
    {
        char buf[16];
        (void)snprintf(buf, sizeof(buf), "%d", st->debug_max_file_mb);
        HWND lbl = make_label2(nd, SH_DBG_MAX_FILE);
        HWND ed  = make_edit2(nd, buf, IDC_DBG_MAX_FILE, ES_NUMBER, SH_DBG_MAX_FILE);
        dbg_add(nd, SDBG_MAX_FILE, lbl, ed, NULL, 70, 0, 0);
    }
    {
        char buf[16];
        (void)snprintf(buf, sizeof(buf), "%d", st->debug_max_total_mb);
        HWND lbl = make_label2(nd, SH_DBG_MAX_TOTAL);
        HWND ed  = make_edit2(nd, buf, IDC_DBG_MAX_TOTAL, ES_NUMBER, SH_DBG_MAX_TOTAL);
        (void)settings_debug_usage_text(line, sizeof(line), used_bytes, used_files);
        HWND use = page_ctrl(nd, "STATIC", line,
                             SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0, SH_DBG_USAGE);
        if (nd->n_dbg_dim < (int)(sizeof(nd->hDbgDim) / sizeof(nd->hDbgDim[0])))
            nd->hDbgDim[nd->n_dbg_dim++] = use;
        dbg_add(nd, SDBG_MAX_TOTAL, lbl, ed, use, 70 + 10 + 260, 260, 0);
    }
    {
        HWND lbl = make_label2(nd, SH_DBG_CURRENT);
        (void)settings_debug_current_text(line, sizeof(line), &dst);
        HWND ed = make_edit2(nd, line, IDC_DBG_CURRENT, ES_READONLY, SH_DBG_CURRENT);
        dbg_add(nd, SDBG_CURRENT, lbl, ed, NULL, 0, 0, 0);
    }
    dbg_add(nd, SDBG_NOTE, NULL,
            dbg_dim_static(nd, "Give this folder to a Claude Code session. AI Assist can "
                               "read it too; what it reads goes to your AI provider. "
                               "Mark issue: Ctrl+Shift+M"),
            NULL, 0, 0, 1);

    debug_apply_visibility(nd);
}

/* Save: read the page back into the settings. Controls that are hidden still
 * hold the stored values, so unticking "Enable debugging" keeps the rest. */
static void debug_read_page(SettingsDlgData *d, Settings *s)
{
    HWND h = d->hPage;
    s->debug_enabled = IsDlgButtonChecked(h, IDC_DBG_ENABLE) == BST_CHECKED ? 1 : 0;
    for (int cat = 0; cat < DLOG_CAT_COUNT; cat++) {
        int sel = (int)SendDlgItemMessage(h, IDC_DBG_LEVEL_BASE + cat, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < DLOG_LVL_COUNT) s->debug_levels[cat] = (unsigned char)sel;
    }
    s->debug_recorder = IsDlgButtonChecked(h, IDC_DBG_RECORDER) == BST_CHECKED ? 1 : 0;
    {
        int sel = (int)SendDlgItemMessage(h, IDC_DBG_RECORDER_LEVEL, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < DLOG_LVL_COUNT) s->debug_recorder_level = (unsigned char)sel;
    }
    s->debug_screenshot = IsDlgButtonChecked(h, IDC_DBG_SCREENSHOT) == BST_CHECKED ? 1 : 0;
    s->debug_include_content = IsDlgButtonChecked(h, IDC_DBG_CONTENT) == BST_CHECKED ? 1 : 0;
    {
        char buf[CFG_STR_MAX];
        GetDlgItemTextA(h, IDC_DBG_FOLDER, buf, (int)sizeof(buf));
        /* Trim spaces; an empty field is the default folder. */
        char *a = buf;
        while (*a == ' ' || *a == '\t') a++;
        size_t n = strlen(a);
        while (n > 0 && (a[n - 1] == ' ' || a[n - 1] == '\t')) a[--n] = '\0';
        (void)snprintf(s->debug_log_dir, sizeof(s->debug_log_dir), "%s", n ? a : "logs");
    }
    {
        char buf[32];
        GetDlgItemTextA(h, IDC_DBG_MAX_FILE, buf, (int)sizeof(buf));
        s->debug_max_file_mb = settings_debug_parse_mb(buf, SETTINGS_DEBUG_FILE_MB_MIN,
                                                       SETTINGS_DEBUG_FILE_MB_MAX,
                                                       s->debug_max_file_mb);
        GetDlgItemTextA(h, IDC_DBG_MAX_TOTAL, buf, (int)sizeof(buf));
        s->debug_max_total_mb = settings_debug_parse_mb(buf, SETTINGS_DEBUG_TOTAL_MB_MIN,
                                                        SETTINGS_DEBUG_TOTAL_MB_MAX,
                                                        s->debug_max_total_mb);
    }
}

/* A number box read at Save: the parsed value when it lies in the entry's
 * range in the help table (0 too, where 0 means "never"), else `current`. */
static int keep_entry(SettingsHelpId id, BOOL parsed_ok, UINT v, int current)
{
    const SettingsHelp *h = settings_help_get((int)id);
    if (!h) return current;
    return settings_entry_or_keep(parsed_ok ? 1 : 0, v, h->min, h->max,
                                  (h->flags & SH_F_ZERO_MEANS_NEVER) != 0, current);
}

/* The Voice page's values, read into *s (Save, and the throw-away copy of Play
 * sample and the shared-key check). */
static void voice_page_read(SettingsDlgData *d, Settings *s)
{
    BOOL ok;
    UINT v;
    /* Voice page. Enable voice is saved as ticked (the assistant's
     * own switch is applied by voice_feature_apply: it needs both). */
    s->voice_enabled = (IsDlgButtonChecked(d->hPage, IDC_VOICE_ENABLE)
                        == BST_CHECKED) ? 1 : 0;
    s->voice_stt_use_ai = (IsDlgButtonChecked(d->hPage, IDC_VOICE_USE_AI)
                           == BST_CHECKED) ? 1 : 0;
    {
        size_t np = 0;
        const SttProvider *tab = voice_stt_providers(&np);
        int sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_PROVIDER,
                                          CB_GETCURSEL, 0, 0);
        if (sel >= 0 && (size_t)sel < np)
            (void)snprintf(s->voice_stt_provider, sizeof(s->voice_stt_provider),
                           "%s", tab[sel].id);
    }
    GetDlgItemText(d->hPage, IDC_VOICE_KEY,
                   s->stt_api_key, (int)sizeof(s->stt_api_key));
    /* As the AI key above: a new, non-empty key drops a stale blob. */
    config_secret_drop_stale_preserved(s->stt_api_key,
        s->stt_api_key_enc_preserved, sizeof(s->stt_api_key_enc_preserved));
    {
        /* A base URL, not an endpoint: a pasted /audio/transcriptions
         * is trimmed. Text that is not a URL is kept as typed, so the
         * user sees what they entered. */
        char raw[512], norm[512];
        raw[0] = '\0';
        GetDlgItemText(d->hPage, IDC_VOICE_URL, raw, (int)sizeof(raw));
        const char *use = raw;
        if (raw[0] && voice_base_url_normalise(raw, norm, sizeof(norm))) use = norm;
        (void)snprintf(s->voice_stt_url, sizeof(s->voice_stt_url), "%s", use);
    }
    GetDlgItemText(d->hPage, IDC_VOICE_MODEL,
                   s->voice_stt_model, (int)sizeof(s->voice_stt_model));
    GetDlgItemText(d->hPage, IDC_VOICE_LIVE_MODEL,
                   s->voice_stt_live_model, (int)sizeof(s->voice_stt_live_model));
    {
        /* The shown default is stored as "" so a later default follows. */
        const SttProvider *ep = voice_effective_provider(d);
        if (ep && ep->models && ep->models[0] &&
            strcmp(s->voice_stt_model, ep->models[0]) == 0)
            s->voice_stt_model[0] = '\0';
    }
    {
        char other[64];
        other[0] = '\0';
        GetDlgItemText(d->hPage, IDC_VOICE_LANG_OTHER, other, (int)sizeof(other));
        int lsel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_LANG,
                                           CB_GETCURSEL, 0, 0);
        voice_lang_save_value(lsel, other, d->voice_lang_prev,
                              s->voice_stt_language, sizeof(s->voice_stt_language));
    }
    {
        int sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_DEVICE,
                                          CB_GETCURSEL, 0, 0);
        /* "Windows default" clears the choice, unless it stands for a saved
         * microphone that is not plugged in and the user left it alone;
         * a listed microphone sets it; the saved entry shown before the
         * list arrives and the "Save to list microphones" placeholder
         * leave it unchanged. */
        if (d->voice_dev_listed) {
            if (sel == 0) {
                if (!(d->voice_dev_missing && !d->voice_dev_touched)) {
                    s->voice_input_device[0] = '\0';
                    s->voice_input_device_name[0] = '\0';
                }
            } else if (sel - 1 < d->voice_dev_n) {
                (void)snprintf(s->voice_input_device, sizeof(s->voice_input_device),
                               "%s", d->voice_dev[sel - 1].id);
                (void)snprintf(s->voice_input_device_name,
                               sizeof(s->voice_input_device_name),
                               "%s", d->voice_dev[sel - 1].name);
            }
        }
    }
    {
        int sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_PTT_KEY,
                                          CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < VOICE_PTT_COUNT) s->voice_ptt_key = sel;
        sel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_PTT_MODE,
                                      CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < VOICE_MODE_COUNT) s->voice_ptt_mode = sel;
    }
    s->voice_mode = voice_mode_selected(d);
    {
        char pt[32];
        pt[0] = '\0';
        GetDlgItemText(d->hPage, IDC_VOICE_PAUSE, pt, (int)sizeof(pt));
        s->voice_pause_ms = voice_pause_parse(pt, s->voice_pause_ms);
    }
    {
        int vsel = (int)SendDlgItemMessage(d->hPage, IDC_VOICE_WIN_VOICE,
                                           CB_GETCURSEL, 0, 0);
        /* Automatic stores nothing, unless it stands for a saved voice that
         * is not installed and the user left it alone (like the microphone). */
        if (vsel == 0) {
            if (!(d->voice_win_missing && !d->voice_win_touched)) {
                s->voice_windows_voice[0] = '\0';
                s->voice_windows_voice_name[0] = '\0';
            }
        } else if (vsel >= 1 && (size_t)(vsel - 1) < d->voice_win_n) {
            const VoiceInfo *vi = &d->voice_win[vsel - 1];
            (void)snprintf(s->voice_windows_voice, sizeof(s->voice_windows_voice),
                           "%s", vi->id);
            voice_display_name(vi, s->voice_windows_voice_name,
                               sizeof(s->voice_windows_voice_name));
        }
    }
    v = GetDlgItemInt(d->hPage, IDC_VOICE_MIC_DELAY, &ok, FALSE);
    s->voice_mic_delay_ms =
        keep_entry(SH_VOICE_MIC_DELAY, ok, v, s->voice_mic_delay_ms);
    s->voice_mic_gain_db = voice_gain_now(d);
    s->voice_auto_send = (IsDlgButtonChecked(d->hPage, IDC_VOICE_AUTO_SEND)
                          == BST_CHECKED) ? 1 : 0;
    {
        /* Speech (PR 3). A provider the page shows no selection for (an unknown id,
         * R38) keeps the stored id. A voice or model equal to Mistral's default is
         * stored as "" so a later default follows. */
        s->voice_speech_engine = voice_speech_selected(d);
        const VoiceAisProvider *ap = voice_ais_page_provider(d);
        if (ap) (void)snprintf(s->voice_tts_provider, sizeof(s->voice_tts_provider), "%s", ap->id);
        {
            char raw[512], norm[512];
            raw[0] = '\0';
            GetDlgItemText(d->hPage, IDC_VOICE_TTS_URL, raw, (int)sizeof(raw));
            const char *use = raw;
            if (raw[0] && voice_base_url_normalise(raw, norm, sizeof(norm))) use = norm;
            (void)snprintf(s->voice_tts_url, sizeof(s->voice_tts_url), "%s", use);
        }
        GetDlgItemText(d->hPage, IDC_VOICE_AI_VOICE, s->voice_ai_voice, (int)sizeof(s->voice_ai_voice));
        GetDlgItemText(d->hPage, IDC_VOICE_AI_MODEL, s->voice_ai_tts_model,
                       (int)sizeof(s->voice_ai_tts_model));
        {
            const VoiceAisProvider *cur = voice_ais_provider_find(s->voice_tts_provider);
            if (cur && strcmp(cur->id, "mistral") == 0) {
                if (cur->voices && strcmp(s->voice_ai_voice, cur->voices[0]) == 0)
                    s->voice_ai_voice[0] = '\0';
                if (cur->models && strcmp(s->voice_ai_tts_model, cur->models[0]) == 0)
                    s->voice_ai_tts_model[0] = '\0';
            }
        }
        {
            char raw[2200];
            size_t o = 0;
            raw[0] = '\0';
            GetDlgItemText(d->hPage, IDC_VOICE_AI_STYLE, raw, (int)sizeof(raw));
            for (const char *q = raw; *q && o + 1 < sizeof(s->voice_ai_style); q++)
                if (*q != '\r') s->voice_ai_style[o++] = *q;
            s->voice_ai_style[o] = '\0';
        }
        /* The speech key is rebound to the provider on the page only when the user
         * edited it this session (R43); a stale preserved blob goes with a new key. */
        /* A key typed for another binding than the page has now is dropped, not rebound. */
        if (d->voice_tts_key_touched) {
            char now[CFG_STR_MAX];
            voice_ais_binding(s, now, sizeof(now));
            if (!voice_ais_key_still_bound(d->voice_tts_key_bound, now))
                d->voice_tts_key_touched = 0;
        }
        if (d->voice_tts_key_touched) {
            GetDlgItemText(d->hPage, IDC_VOICE_TTS_KEY, s->tts_api_key, (int)sizeof(s->tts_api_key));
            voice_ais_binding(s, s->tts_api_key_for, sizeof(s->tts_api_key_for));
            config_secret_drop_stale_preserved(s->tts_api_key,
                s->tts_api_key_enc_preserved, sizeof(s->tts_api_key_enc_preserved));
        }
    }
}

/* ---- Main window procedure ------------------------------------------------ */

/* ---- Keyboard path: Tab order and F1 help --------------------------------
 * The dialog is a plain window, not a dialog box, so it does its own key
 * navigation. The order and the stepping are in settings_tab (portable,
 * tested); this is the wiring. */

/* The control a focus window stands for: the edit inside an editable combo
 * is the combo. */
static HWND focus_owner(HWND h)
{
    HWND p = h ? GetParent(h) : NULL;
    if (p && is_combo_class(p)) return p;
    return h;
}

/* Can this control take focus by Tab? Labels, hints and the meter are statics. */
static int tab_focusable(HWND h)
{
    char cls[32];
    if (!h) return 0;
    GetClassNameA(h, cls, (int)sizeof(cls));
    return _stricmp(cls, "Static") != 0;
}

static void tab_item_add(SettingsTabItem *it, HWND *hw, int *n, HWND h,
                         int group, HWND coord_parent)
{
    if (!h || *n >= 2 * MAX_SETTINGS_CTRLS + 8) return;
    RECT rc;
    GetWindowRect(h, &rc);
    /* One common space: the dialog's client coordinates. */
    POINT pt = { rc.left, rc.top };
    ScreenToClient(coord_parent, &pt);
    it[*n].group = group;
    it[*n].x = pt.x;
    it[*n].y = pt.y;
    it[*n].enabled = IsWindowEnabled(h) ? 1 : 0;
    it[*n].visible = IsWindowVisible(h) ? 1 : 0;
    hw[*n] = h;
    (*n)++;
}

/* Tab / Shift+Tab: move the focus to the next control in visual order. */
static void tab_move(SettingsDlgData *d, int dir)
{
    SettingsTabItem it[2 * MAX_SETTINGS_CTRLS + 8];
    HWND hw[2 * MAX_SETTINGS_CTRLS + 8];
    int n = 0;
    tab_item_add(it, hw, &n, d->hNav, STAB_GROUP_NAV, d->hDlg);
    for (int i = 0; i < d->n_ctrls; i++) {
        const SettingsCtrl *c = &d->ctrls[i];
        if (c->page != d->cur_page || !c->visible) continue;
        HWND hs[3] = { c->hCtrl, c->hExtra, c->hExtra2 };
        for (int k = 0; k < 3; k++)
            if (tab_focusable(hs[k]))
                tab_item_add(it, hw, &n, hs[k], STAB_GROUP_PAGE, d->hDlg);
    }
    tab_item_add(it, hw, &n, d->hBtnOK, STAB_GROUP_BUTTONS, d->hDlg);
    tab_item_add(it, hw, &n, d->hBtnCancel, STAB_GROUP_BUTTONS, d->hDlg);

    int order[2 * MAX_SETTINGS_CTRLS + 8];
    int count = settings_tab_order(it, n, order);
    HWND cur = focus_owner(GetFocus());
    int cur_idx = -1;
    for (int i = 0; i < n; i++)
        if (hw[i] == cur) { cur_idx = i; break; }
    int next = settings_tab_step(order, count, cur_idx, dir);
    if (next >= 0) SetFocus(hw[next]);
}

static void help_pop_hide(SettingsDlgData *d)
{
    if (!d->tip_focus) return;
    d->tip_focus = NULL;
    KillTimer(d->hDlg, IDT_HELP_POP);
    ns_tip_unpop(d->hTip, d->hPage);
}

/* F1: show the help of the focused control, if it has any. */
static void help_pop_show(SettingsDlgData *d)
{
    HWND f = focus_owner(GetFocus());
    help_pop_hide(d);
    if (!f || !d->hTip) return;
    for (int i = 0; i < d->n_tips; i++) {
        if (d->tips[i].hwnd != f) continue;
        const SettingsHelp *e = settings_help_get((int)d->tips[i].help);
        RECT rc;
        GetWindowRect(f, &rc);
        /* A combo's window is tall (its drop-down): anchor to the closed box. */
        if (is_combo_class(f)) {
            SettingsMetrics m;
            settings_metrics_init(&m, d->dpi);
            rc.bottom = rc.top + m.ctrl_h;
        }
        if (e && ns_tip_pop(d->hTip, d->hPage, e->text, &rc)) {
            d->tip_focus = f;
            SetTimer(d->hDlg, IDT_HELP_POP, 100, NULL);
        }
        return;
    }
}

/* Called for every message before it is translated. Returns non-zero when
 * the key was handled here. */
static int settings_key_pretranslate(HWND hwnd, const MSG *msg)
{
    if (msg->message != WM_KEYDOWN) return 0;
    if (msg->hwnd != hwnd && !IsChild(hwnd, msg->hwnd)) return 0;
    SettingsDlgData *d = (SettingsDlgData *)(LONG_PTR)
                         GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d || d->saving) return 0;
    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (ctrl || alt) return 0;

    switch (msg->wParam) {
    case VK_TAB: {
        /* An open drop-down list uses the key itself. */
        HWND f = focus_owner(GetFocus());
        if (f && is_combo_class(f) && SendMessage(f, CB_GETDROPPEDSTATE, 0, 0))
            return 0;
        help_pop_hide(d);
        tab_move(d, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : +1);
        return 1;
    }
    case VK_F1:
        help_pop_show(d);
        return 1;
    case VK_ESCAPE:
        if (d->tip_focus) { help_pop_hide(d); return 1; }
        return 0;
    default:
        return 0;
    }
}

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

        nd->hDlg = hwnd;
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

        nd->hPageTitle = make_hint(nd, "");

        nd->n_ctrls = 0;

        /* ==== APPEARANCE ==== */
        {
            HWND lbl = make_label2(nd, SH_COLOUR_SCHEME);
            HWND cmb = make_combo2(nd, IDC_SCHEME_COMBO, CBS_DROPDOWNLIST,
                                   SH_COLOUR_SCHEME);
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
            HWND lbl = make_label2(nd, SH_TERMINAL_FONT);
            HWND cmb = make_combo2(nd, IDC_FONT_COMBO, CBS_DROPDOWNLIST,
                                   SH_TERMINAL_FONT);
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
            HWND lbl = make_label2(nd, SH_FONT_SIZE);
            HWND cmb = make_combo2(nd, IDC_FONTSIZE_COMBO, CBS_DROPDOWNLIST,
                                   SH_FONT_SIZE);
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
            HWND lbl = make_label2(nd, SH_AI_FONT);
            HWND cmb = make_combo2(nd, IDC_AI_FONT_COMBO, CBS_DROPDOWNLIST,
                                   SH_AI_FONT);
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
            HWND lbl = make_label2(nd, SH_SCROLLBACK);
            HWND ed = make_edit2(nd, buf, IDC_SCROLLBACK_EDIT, 0, SH_SCROLLBACK);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_TERMINAL, 100, 0, 1, 0, 1, 0);
        }
        {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d", nd->cfg->settings.paste_delay_ms);
            HWND lbl = make_label2(nd, SH_PASTE_DELAY);
            HWND ed = make_edit2(nd, buf, IDC_PASTEDELAY_EDIT, 0, SH_PASTE_DELAY);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_TERMINAL, 100, 0, 1, 0, 1, 0);
        }
        {
            HWND chk = make_check2(nd, IDC_PASTE_CONFIRM, SH_PASTE_CONFIRM);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.paste_confirm ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_TERMINAL, 0, 0, 1, 0, 1, 1);
        }

        /* ==== LOGGING ==== */
        {
            HWND lbl = make_label2(nd, SH_LOG_DIR);
            HWND ed = make_edit2(nd, nd->cfg->settings.log_dir,
                                 IDC_LOG_DIR_EDIT, 0, SH_LOG_DIR);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_LOGGING, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd, SH_LOG_FORMAT);
            HWND ed = make_edit2(nd, nd->cfg->settings.log_format,
                                 IDC_LOG_FMT_EDIT, 0, SH_LOG_FORMAT);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_LOGGING, 0, 0, 1, 0, 1, 0);
        }

        /* ==== SSH ==== */
        {
            char buf[32];
            (void)snprintf(buf, sizeof(buf), "%d",
                           nd->cfg->settings.ssh_user_idle_timeout_mins);
            HWND lbl = make_label2(nd, SH_SSH_IDLE);
            HWND ed = make_edit2(nd, buf, IDC_SSH_IDLE_EDIT, 0, SH_SSH_IDLE);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_SSH, 100, 0, 1, 0, 1, 0);
        }
        {
            nd->hSshHint = make_hint(nd,
                "0 disables the idle timeout. Keystrokes, scrolling, tab "
                "switches and AI chat all count as activity.");
            add_ctrl(nd, NULL, nd->hSshHint, NULL, SETTINGS_PAGE_SSH, 0, 0, 2, 0, 1, 1);
        }

        /* ==== STARTUP ==== */
        {
            HWND chk = make_check2(nd, IDC_SESSION_MGR_STARTUP, SH_SESSION_MGR_STARTUP);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.open_session_manager_at_start
                            ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_STARTUP, 0, 0, 1, 0, 1, 1);
        }
        {
            HWND chk = make_check2(nd, IDC_AUTOCONNECT_CHECK, SH_AUTOCONNECT);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.auto_connect ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_STARTUP, 0, 0, 1, 0, 1, 1);
        }
        {
            HWND lbl = make_label2(nd, SH_AUTOCONNECT_SESSION);
            HWND cmb = make_combo2(nd, IDC_AUTOCONNECT_COMBO, CBS_DROPDOWNLIST,
                                   SH_AUTOCONNECT_SESSION);
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
            /* The master switch: while it is unticked every other AI control
             * on the three AI pages stays created (the help tips stay
             * counted) but disabled (ai_enable_sync). The stored value, never
             * the --ui-demo override, decides the initial state -- except
             * --ui-demo=settings-voice (demo == 2), which ticks it in memory. */
            HWND chk = make_check2(nd, IDC_AI_ENABLE, SH_AI_ENABLE);
            SendMessage(chk, BM_SETCHECK,
                        (nd->cfg->settings.ai_enabled || nd->demo == 2) ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_AI_PROVIDER, 0, 0, 1, 0, 1, 1);
        }
        {
            HWND lbl = make_label2(nd, SH_AI_PROVIDER);
            HWND cmb = make_combo2(nd, IDC_AI_PROVIDER_COMBO, CBS_DROPDOWNLIST,
                                   SH_AI_PROVIDER);
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
            HWND lbl = make_label2(nd, SH_AI_KEY);
            HWND ed = make_edit2(nd, nd->cfg->settings.ai_api_key,
                                 IDC_AI_KEY_EDIT, ES_PASSWORD, SH_AI_KEY);
            /* Match the edit box's limit to Settings.ai_api_key's buffer
             * size -- the IDOK handler's GetDlgItemText() already
             * truncates safely at that size, but without this the box
             * lets the user type (and believe they saved) more than will
             * ever actually be kept. */
            SendMessage(ed, EM_SETLIMITTEXT,
                        (WPARAM)(sizeof(nd->cfg->settings.ai_api_key) - 1u), 0);
            pw_reveal_attach(ed);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_AI_PROVIDER, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd, SH_AI_MODEL);
            HWND cmb = make_combo2(nd, IDC_AI_CUSTOM_MODEL, CBS_DROPDOWN, SH_AI_MODEL);
            const char *cur_model = nd->cfg->settings.ai_custom_model;
            if (cur_model && cur_model[0]) SetWindowText(cmb, cur_model);
            HWND refresh = page_ctrl_w(nd, L"BUTTON", L"\x21BB", BS_OWNERDRAW,
                                       IDC_AI_REFRESH, SH_AI_REFRESH);
            add_ctrl(nd, lbl, cmb, refresh, SETTINGS_PAGE_AI_PROVIDER, 0, 26, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd, SH_AI_BASE_URL);
            HWND ed = make_edit2(nd, nd->cfg->settings.ai_custom_url,
                                 IDC_AI_CUSTOM_URL, 0, SH_AI_BASE_URL);
            nd->ctrl_ai_base_url = add_ctrl(nd, lbl, ed, NULL,
                SETTINGS_PAGE_AI_PROVIDER, 0, 0, 1, 0, is_custom, 0);
        }
        {
            /* Shown only for a custom provider with a clear-text, non-loopback
             * URL (ai_url_warn_sync); a hidden row takes no space. Two rows
             * so the sentence wraps rather than clips on a narrow pane. */
            nd->hUrlWarn = make_hint(nd,
                "Not encrypted: your API key and terminal text cross the "
                "network in plain text.");
            nd->ctrl_ai_url_warn = add_ctrl(nd, NULL, nd->hUrlWarn, NULL,
                SETTINGS_PAGE_AI_PROVIDER, 0, 0, 2, 0, 0, 1);
        }
        {
            /* "Commands as tool calls": a row of its own, shown for every
             * provider (Base URL's row above is custom-only). The item index
             * is the AI_RUNCMD_* value (Automatic 0, On 1, Off 2). */
            HWND lbl = make_label2(nd, SH_AI_RUNCMD_MODE);
            HWND cmb = make_combo2(nd, IDC_AI_RUNCMD_MODE, CBS_DROPDOWNLIST,
                                   SH_AI_RUNCMD_MODE);
            static const char *const k_runcmd_modes[] = { "Automatic", "On", "Off" };
            for (int i = 0; i < 3; i++)
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)k_runcmd_modes[i]);
            int rsel = nd->cfg->settings.ai_runcmd_mode;
            if (rsel != AI_RUNCMD_ON && rsel != AI_RUNCMD_OFF) rsel = AI_RUNCMD_AUTO;
            SendMessage(cmb, CB_SETCURSEL, (WPARAM)rsel, 0);
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_AI_PROVIDER, 0, 0, 1, 0, 1, 0);
        }
        }

        /* ==== AI_BEHAVIOUR ==== */
        {
            char buf[16];
            int lines = nd->cfg->settings.ai_max_context_lines;
            if (lines < 1) lines = AI_CONTEXT_LINES_DEFAULT;
            (void)snprintf(buf, sizeof(buf), "%d", lines);
            HWND lbl = make_label2(nd, SH_AI_CONTEXT_LINES);
            HWND ed = make_edit2(nd, buf, IDC_AI_CONTEXT_LINES, ES_NUMBER,
                                 SH_AI_CONTEXT_LINES);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 100, 0, 1, 0, 1, 0);
        }
        {
            nd->hCtxHint = make_hint(nd,
                "Each line becomes part of the context sent with every "
                "message. A larger window gives the assistant more of your "
                "session to reason about, at a proportionate cost in tokens. "
                "The maximum is 50,000 lines.");
            add_ctrl(nd, NULL, nd->hCtxHint, NULL, SETTINGS_PAGE_AI_BEHAVIOUR,
                     0, 0, 4, 0, 1, 1);
        }
        {
            HWND lbl = make_label2(nd, SH_AI_SYSTEM_NOTES);
            HWND ed = page_ctrl(nd, "EDIT", nd->cfg->settings.ai_system_notes,
                WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
                IDC_AI_SYSTEM_NOTES, SH_AI_SYSTEM_NOTES);
            SendMessage(ed, EM_SETLIMITTEXT, (WPARAM)2559, 0);
            SendMessage(ed, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                        MAKELPARAM(3, 3));
            SendMessageW(ed, EM_SETCUEBANNER, 0,
                         (LPARAM)L"System-wide AI instructions (max 400 words)");
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_AI_BEHAVIOUR,
                     0, CSB_WIDTH, 6, 1, 1, 0);
        }
        {
            HWND chk = make_check2(nd, IDC_AI_MD_RENDER, SH_AI_MARKDOWN);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.markdown_render_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 1, 0, 1, 1);
        }
        {
            /* "Thinking display": the item index is the ThinkPref value
             * (Automatic 0, Always collapsed 1, Always compact 2, Always open 3,
             * Always extended 4). */
            HWND lbl = make_label2(nd, SH_AI_THINKING_MODE);
            HWND cmb = make_combo2(nd, IDC_AI_THINKING_MODE, CBS_DROPDOWNLIST,
                                   SH_AI_THINKING_MODE);
            static const char *const k_think_modes[] = {
                "Automatic", "Always collapsed", "Always compact", "Always open",
                "Always extended" };
            _Static_assert(sizeof k_think_modes / sizeof k_think_modes[0] ==
                               THINK_PREF_MAX + 1,
                           "one combo label per ThinkPref value");
            for (int i = 0; i <= THINK_PREF_MAX; i++)
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)k_think_modes[i]);
            int tsel = nd->cfg->settings.ai_thinking_mode;
            if (tsel < THINK_PREF_AUTO || tsel > THINK_PREF_MAX) tsel = THINK_PREF_AUTO;
            SendMessage(cmb, CB_SETCURSEL, (WPARAM)tsel, 0);
            nd->think_initial = tsel;
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 1, 0, 1, 0);
        }
        {
            /* The one policy setting: the level, and whether a new session
             * starts with Auto-approve on -- the pill and the button on the
             * AI panel's status line (spec 2026-10-18 section 2). */
            CmdPolicy pol = nd->cfg->settings.ai_policy_default;
            cmd_policy_clamp(&pol);
            {
                HWND lbl = make_label2(nd, SH_AI_POLICY_ALLOWED);
                HWND cmb = make_combo2(nd, IDC_AI_POLICY_ALLOWED,
                                       CBS_DROPDOWNLIST, SH_AI_POLICY_ALLOWED);
                for (int i = 0; i < POLICY_STOP_COUNT; i++)
                    SendMessage(cmb, CB_ADDSTRING, 0,
                                (LPARAM)cmd_policy_stop_label(i));
                SendMessage(cmb, CB_SETCURSEL, (WPARAM)pol.allowed, 0);
                add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 1, 0, 1, 0);
            }
            {
                HWND chk = make_check2(nd, IDC_AI_POLICY_AUTO, SH_AI_POLICY_AUTO);
                SendMessage(chk, BM_SETCHECK,
                            cmd_policy_auto_on(pol) ? BST_CHECKED : BST_UNCHECKED, 0);
                add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 1, 0, 1, 1);
            }
        }
        {
            /* The label is wider than the 150 px label column, so the row is
             * full-width: the text left-aligned, the number box beside it
             * (the tickbox-with-a-control-beside-it form). The label is kept
             * short enough to stay on one line in its 240 px (96 dpi) box;
             * what "0" means goes in the dim note under the row. */
            char buf[16];
            int sec = nd->cfg->settings.ai_tool_timeout_sec;
            if (sec != AI_TOOL_TIMEOUT_SEC_NEVER &&
                (sec < AI_TOOL_TIMEOUT_SEC_MIN || sec > AI_TOOL_TIMEOUT_SEC_MAX))
                sec = AI_TOOL_TIMEOUT_SEC_DEFAULT;
            (void)snprintf(buf, sizeof(buf), "%d", sec);
            HWND lbl = page_ctrl(nd, "STATIC", settings_help_get((int)SH_AI_TOOL_TIMEOUT)->label,
                                SS_LEFT, 0, SH_AI_TOOL_TIMEOUT);
            HWND ed = make_edit2(nd, buf, IDC_AI_TOOL_TIMEOUT, ES_NUMBER,
                                 SH_AI_TOOL_TIMEOUT);
            add_ctrl(nd, NULL, lbl, ed, SETTINGS_PAGE_AI_BEHAVIOUR, 320, 70, 1, 0, 1, 1);
        }
        {
            nd->hToolTimeoutNote = make_hint(nd,
                "0 means never: the card waits until you answer it or stop "
                "the reply.");
            add_ctrl(nd, NULL, nd->hToolTimeoutNote, NULL,
                     SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 2, 0, 1, 1);
        }
        {
            /* Same form as the timeout row above, directly under it. */
            char buf[16];
            int rounds = nd->cfg->settings.ai_tool_auto_rounds;
            if (rounds < AI_TOOL_ROUNDS_MIN || rounds > AI_TOOL_ROUNDS_MAX)
                rounds = AI_TOOL_ROUNDS_DEFAULT;
            (void)snprintf(buf, sizeof(buf), "%d", rounds);
            HWND lbl = page_ctrl(nd, "STATIC", settings_help_get((int)SH_AI_TOOL_ROUNDS)->label,
                                SS_LEFT, 0, SH_AI_TOOL_ROUNDS);
            HWND ed = make_edit2(nd, buf, IDC_AI_TOOL_ROUNDS, ES_NUMBER,
                                 SH_AI_TOOL_ROUNDS);
            add_ctrl(nd, NULL, lbl, ed, SETTINGS_PAGE_AI_BEHAVIOUR, 320, 70, 1, 0, 1, 1);
        }
        {
            nd->hToolRoundsNote = make_hint(nd,
                "Fetches the assistant may run on its own after one prompt "
                "before it asks; your next prompt resets the count.");
            add_ctrl(nd, NULL, nd->hToolRoundsNote, NULL,
                     SETTINGS_PAGE_AI_BEHAVIOUR, 0, 0, 3, 0, 1, 1);
        }

        /* ==== VOICE ==== */
        {
            const Settings *vs = &nd->cfg->settings;
            nd->voice_use_ai_wanted = vs->voice_stt_use_ai;
            nd->voice_dev_listed = voice_gate_on() && !nd->demo;   /* a demo never lists microphones (no COM) */
            {
                HWND chk = make_check2(nd, IDC_VOICE_ENABLE, SH_VOICE_ENABLE);
                SendMessage(chk, BM_SETCHECK,
                            (vs->voice_enabled || nd->demo == 2) ? BST_CHECKED : BST_UNCHECKED, 0);
                add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 1);
            }
            {
                /* Mode decides what everything below does: Dictation or Conversation. */
                HWND lbl = make_label2(nd, SH_VOICE_MODE);
                HWND cmb = make_combo2(nd, IDC_VOICE_MODE, CBS_DROPDOWNLIST, SH_VOICE_MODE);
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)"Dictation");
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)"Conversation");
                SendMessage(cmb, CB_SETCURSEL,
                            (WPARAM)(vs->voice_mode == VOICE_MODE_CONVERSATION ? 1 : 0), 0);
                add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 0);
            }
            {
                /* "Needs the AI assistant" while the dialog's AI box is off. */
                nd->hVoiceBlock = make_hint(nd, "");
                nd->ctrl_voice_block = add_ctrl(nd, NULL, nd->hVoiceBlock, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 0, 1);
            }
            {
                nd->hVoiceHead[0] = make_hint(nd, "Speech to Text");
                add_ctrl(nd, NULL, nd->hVoiceHead[0], NULL, SETTINGS_PAGE_VOICE,
                         0, 0, 1, 0, 1, 1);
            }
            {
                HWND chk = make_check2(nd, IDC_VOICE_USE_AI, SH_VOICE_STT_USE_AI);
                add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 1);
            }
            {
                nd->hVoiceAiNote = make_hint(nd, "");
                nd->ctrl_voice_ai_note = add_ctrl(nd, NULL, nd->hVoiceAiNote, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 0, 1);
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_STT_PROVIDER);
                HWND cmb = make_combo2(nd, IDC_VOICE_PROVIDER, CBS_DROPDOWNLIST,
                                       SH_VOICE_STT_PROVIDER);
                size_t np = 0;
                const SttProvider *tab = voice_stt_providers(&np);
                int sel = 0;
                for (size_t i = 0; i < np; i++) {
                    SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)tab[i].label);
                    if (_stricmp(vs->voice_stt_provider, tab[i].id) == 0) sel = (int)i;
                }
                SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
                nd->ctrl_voice_provider = add_ctrl(nd, lbl, cmb, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 0);
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_STT_KEY);
                HWND ed = make_edit2(nd, vs->stt_api_key, IDC_VOICE_KEY,
                                     ES_PASSWORD, SH_VOICE_STT_KEY);
                SendMessage(ed, EM_SETLIMITTEXT,
                            (WPARAM)(sizeof(vs->stt_api_key) - 1u), 0);
                pw_reveal_attach(ed);
                nd->ctrl_voice_key = add_ctrl(nd, lbl, ed, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 0);
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_STT_URL);
                HWND ed = make_edit2(nd, vs->voice_stt_url, IDC_VOICE_URL, 0,
                                     SH_VOICE_STT_URL);
                SendMessage(ed, EM_SETLIMITTEXT,
                            (WPARAM)(sizeof(vs->voice_stt_url) - 1u), 0);
                nd->ctrl_voice_url = add_ctrl(nd, lbl, ed, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 0, 0);
            }
            {
                nd->hVoiceUrlWarn = make_hint(nd,
                    "Not encrypted: your API key and recordings cross the "
                    "network in plain text.");
                nd->ctrl_voice_url_warn = add_ctrl(nd, NULL, nd->hVoiceUrlWarn, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 2, 0, 0, 1);
            }
            {
                /* Editable: the list is only a suggestion (voice_model_seed). */
                HWND lbl = make_label2(nd, SH_VOICE_STT_MODEL);
                HWND cmb = make_combo2(nd, IDC_VOICE_MODEL, CBS_DROPDOWN,
                                       SH_VOICE_STT_MODEL);
                if (vs->voice_stt_model[0]) SetWindowText(cmb, vs->voice_stt_model);
                add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 0);
            }
            {
                /* Live model: OpenAI and Mistral only (voice_rows_apply shows
                 * one of the row and the note). Editable, the list a suggestion. */
                HWND lbl = make_label2(nd, SH_VOICE_STT_LIVE_MODEL);
                HWND cmb = make_combo2(nd, IDC_VOICE_LIVE_MODEL, CBS_DROPDOWN,
                                       SH_VOICE_STT_LIVE_MODEL);
                if (vs->voice_stt_live_model[0])
                    SetWindowText(cmb, vs->voice_stt_live_model);
                SendMessageW(cmb, CB_SETCUEBANNER, 0,
                             (LPARAM)L"the provider's default");   /* empty = the default */
                nd->ctrl_voice_live = add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_VOICE,
                                               0, 0, 1, 0, 1, 0);
                nd->hVoiceLiveNote = make_hint(nd, "Live text as you speak needs OpenAI or Mistral");
                nd->ctrl_voice_live_note = add_ctrl(nd, NULL, nd->hVoiceLiveNote, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 2, 0, 0, 0);   /* control column, same slot */
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_LANGUAGE);
                HWND cmb = make_combo2_wide(nd, IDC_VOICE_LANG, SH_VOICE_LANGUAGE);
                size_t nl = 0;
                const VoiceLang *lt = voice_lang_table(&nl);
                for (size_t i = 0; i < nl; i++)
                    voice_combo_add_utf8(cmb, lt[i].label);
                /* The closed box shrinks beside the Other box; the list keeps room for the names. */
                SendMessage(cmb, CB_SETDROPPEDWIDTH, (WPARAM)ns_scale(170, nd->dpi), 0);
                voice_combo_add_utf8(cmb, "Other\xE2\x80\xA6");   /* "Other..." with the real ellipsis */
                int lsel = voice_lang_combo_index(vs->voice_stt_language);
                if (nd->demo == 2) lsel = voice_lang_combo_index("en-GB");   /* the demo forces English (UK) */
                SendMessage(cmb, CB_SETCURSEL, (WPARAM)lsel, 0);
                (void)snprintf(nd->voice_lang_prev, sizeof(nd->voice_lang_prev), "%s",
                               vs->voice_stt_language);
                (void)snprintf(nd->voice_lang_loaded_other, sizeof(nd->voice_lang_loaded_other),
                               "%s", lsel == (int)nl ? vs->voice_stt_language : "");
                HWND ed = make_edit2(nd, lsel == (int)nl ? vs->voice_stt_language : "",
                                     IDC_VOICE_LANG_OTHER, 0, SH_VOICE_LANGUAGE_OTHER);
                SendMessage(ed, EM_SETLIMITTEXT, 31, 0);
                {
                    wchar_t cue[96];
                    if (MultiByteToWideChar(CP_UTF8, 0, VOICE_LANG_OTHER_CUE, -1, cue,
                                            (int)(sizeof(cue) / sizeof(cue[0]))) > 0)
                        SendMessageW(ed, EM_SETCUEBANNER, TRUE, (LPARAM)cue);   /* TRUE: still shown while focused */
                }
                nd->ctrl_voice_lang = add_ctrl(nd, lbl, cmb, ed, SETTINGS_PAGE_VOICE,
                                               0, 270, 1, 0, 1, 0);
                nd->hVoiceLangHint = make_hint(nd, "");
                nd->ctrl_voice_lang_hint = add_ctrl(nd, NULL, nd->hVoiceLangHint, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 2, 0, 0, 0);   /* control column, under the combo */
                voice_lang_sync(nd, 0);
            }
            {
                nd->hVoiceHead[1] = make_hint(nd, "Microphone");
                add_ctrl(nd, NULL, nd->hVoiceHead[1], NULL, SETTINGS_PAGE_VOICE,
                         0, 0, 1, 0, 1, 1);
            }
            {
                /* The list is filled only once voice has been saved on (the
                 * audio thread enumerates then); until then one placeholder. */
                HWND lbl = make_label2(nd, SH_VOICE_DEVICE);
                HWND cmb = make_combo2_wide(nd, IDC_VOICE_DEVICE, SH_VOICE_DEVICE);
                if (nd->voice_dev_listed) {
                    voice_combo_add_utf8(cmb, "Windows default");
                    int sel = 0;
                    if (vs->voice_input_device[0]) {
                        const char *nm = vs->voice_input_device_name[0]
                                             ? vs->voice_input_device_name
                                             : vs->voice_input_device;
                        sel = voice_combo_add_utf8(cmb, nm);
                    }
                    SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
                } else {
                    SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)"Save to list microphones");
                    SendMessage(cmb, CB_SETCURSEL, 0, 0);
                }
                HWND test = page_ctrl(nd, "BUTTON",
                                      settings_help_get((int)SH_VOICE_TEST)->label,
                                      BS_OWNERDRAW, IDC_VOICE_TEST, SH_VOICE_TEST);
                add_ctrl(nd, lbl, cmb, test, SETTINGS_PAGE_VOICE, 0, 70, 1, 0, 1, 0);
            }
            {
                /* The level meter, painted in WM_DRAWITEM; empty until Test. */
                HWND meter = page_ctrl(nd, "STATIC", "", SS_OWNERDRAW,
                                       IDC_VOICE_METER, SH_NONE);
                add_ctrl(nd, NULL, meter, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 0);
            }
            {
                /* "No microphone was found." and the like, after a Test. */
                nd->hVoiceTestNote = make_hint(nd, "");
                nd->ctrl_voice_test_note = add_ctrl(nd, NULL, nd->hVoiceTestNote, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 0, 1);
            }
            {
                /* Microphone gain: the themed slider, its "+12 dB" value beside it. */
                HWND lbl = make_label2(nd, SH_VOICE_MIC_GAIN);
                int g0 = nd->demo == 2 ? 6 : voice_gain_clamp(vs->voice_mic_gain_db);   /* the demo shows +6 dB */
                HWND sl = ns_slider_create(nd->hPage, IDC_VOICE_GAIN,
                                           VOICE_GAIN_MIN_DB, VOICE_GAIN_MAX_DB, g0);
                page_track(nd, sl, SH_VOICE_MIC_GAIN);
                char gtxt[16];
                voice_gain_label(g0, gtxt, sizeof(gtxt));
                HWND val = page_ctrl(nd, "STATIC", gtxt, SS_LEFT,
                                     IDC_VOICE_GAIN_VALUE, SH_VOICE_MIC_GAIN);
                add_ctrl(nd, lbl, sl, val, SETTINGS_PAGE_VOICE, 0, 60, 1, 0, 1, 0);
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_PTT_KEY);
                HWND cmb = make_combo2(nd, IDC_VOICE_PTT_KEY, CBS_DROPDOWNLIST,
                                       SH_VOICE_PTT_KEY);
                for (int i = 0; i < VOICE_PTT_COUNT; i++)
                    SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)voice_ptt_key_label(i));
                int ksel = vs->voice_ptt_key;
                if (ksel < 0 || ksel >= VOICE_PTT_COUNT) ksel = VOICE_PTT_RIGHT_SHIFT;
                SendMessage(cmb, CB_SETCURSEL, (WPARAM)ksel, 0);
                add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 0);
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_PTT_MODE);
                HWND cmb = make_combo2(nd, IDC_VOICE_PTT_MODE, CBS_DROPDOWNLIST,
                                       SH_VOICE_PTT_MODE);
                for (int i = 0; i < VOICE_MODE_COUNT; i++)
                    SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)voice_ptt_mode_label(i));
                int msel = vs->voice_ptt_mode;
                if (msel < 0 || msel >= VOICE_MODE_COUNT) msel = VOICE_MODE_TOGGLE;
                SendMessage(cmb, CB_SETCURSEL, (WPARAM)msel, 0);
                add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 0);
            }
            {
                char buf[16];
                (void)snprintf(buf, sizeof(buf), "%d", vs->voice_mic_delay_ms);
                HWND lbl = make_label2(nd, SH_VOICE_MIC_DELAY);
                HWND ed = make_edit2(nd, buf, IDC_VOICE_MIC_DELAY, ES_NUMBER,
                                     SH_VOICE_MIC_DELAY);
                add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_VOICE, 70, 0, 1, 0, 1, 0);
            }
            {
                HWND chk = make_check2(nd, IDC_VOICE_AUTO_SEND, SH_VOICE_AUTO_SEND);
                SendMessage(chk, BM_SETCHECK,
                            vs->voice_auto_send ? BST_CHECKED : BST_UNCHECKED, 0);
                add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 1);
            }
            {
                nd->hVoiceHead[2] = make_hint(nd, "Conversation");
                add_ctrl(nd, NULL, nd->hVoiceHead[2], NULL, SETTINGS_PAGE_VOICE,
                         0, 0, 1, 0, 1, 1);
            }
            {
                /* Pause before sending: seconds with one decimal; Save clamps to 0.5..3.0. */
                char pbuf[16];
                voice_pause_format(vs->voice_pause_ms, pbuf, sizeof(pbuf));
                HWND lbl = make_label2(nd, SH_VOICE_PAUSE);
                HWND ed = make_edit2(nd, pbuf, IDC_VOICE_PAUSE, 0, SH_VOICE_PAUSE);
                SendMessage(ed, EM_SETLIMITTEXT, 8, 0);
                HWND secs = make_hint(nd, "seconds");
                add_ctrl(nd, lbl, ed, secs, SETTINGS_PAGE_VOICE, 140, 70, 1, 0, 1, 0);
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_SPEECH);
                HWND cmb = make_combo2(nd, IDC_VOICE_SPEECH, CBS_DROPDOWNLIST, SH_VOICE_SPEECH);
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)"Windows speech");
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)"AI speech");
                SendMessage(cmb, CB_SETCURSEL,
                            (WPARAM)(vs->voice_speech_engine == VOICE_SPEECH_AI ? 1 : 0), 0);
                add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 0);
            }
            {
                /* Windows voice: Automatic first, then every installed voice. The list
                 * is read from the registry (never sapi.dll); the saved voice is
                 * selected by token id. */
                nd->voice_win_n = voice_voices_list(nd->voice_win, VOICE_INFO_MAX);
                int auto_idx = voice_choose_auto(nd->voice_win, nd->voice_win_n);
                char auto_label[200];
                if (auto_idx >= 0) {
                    char nm[128];
                    voice_display_name(&nd->voice_win[auto_idx], nm, sizeof(nm));
                    (void)snprintf(auto_label, sizeof(auto_label), "Automatic (%s)", nm);
                } else {
                    (void)snprintf(auto_label, sizeof(auto_label),
                                   "Automatic (the Windows default voice)");
                }
                HWND lbl = make_label2(nd, SH_VOICE_WIN_VOICE);
                HWND cmb = make_combo2_wide(nd, IDC_VOICE_WIN_VOICE, SH_VOICE_WIN_VOICE);
                voice_combo_add_utf8(cmb, auto_label);
                for (size_t i = 0; i < nd->voice_win_n; i++) {
                    char nm[128];
                    voice_display_name(&nd->voice_win[i], nm, sizeof(nm));
                    voice_combo_add_utf8(cmb, nm);
                }
                int sel = 0;
                if (vs->voice_windows_voice[0]) {
                    int at = voice_find_saved(nd->voice_win, nd->voice_win_n,
                                              vs->voice_windows_voice);
                    if (at >= 0) sel = at + 1;
                    else nd->voice_win_missing = 1;
                }
                SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
                add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 1, 0);
            }
            {
                nd->hVoiceNoteVoice = make_hint(nd,
                    "The saved voice is not installed on this PC: using Automatic");
                nd->ctrl_voice_note_voice = add_ctrl(nd, NULL, nd->hVoiceNoteVoice, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 2, 0, nd->voice_win_missing, 0);   /* control column */
            }
            {
                int uk = 0;
                for (size_t i = 0; i < nd->voice_win_n; i++)
                    if (voice_info_is_en_gb(&nd->voice_win[i]) && nd->voice_win[i].female) uk = 1;
                nd->voice_uk_installed = uk;
                nd->hVoiceNoteUk = make_hint(nd,
                    "No British English woman's voice is installed: add English (United "
                    "Kingdom) under Windows Settings > Time & language > Speech");
                nd->ctrl_voice_note_uk = add_ctrl(nd, NULL, nd->hVoiceNoteUk, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 2, 0, !uk, 1);
            }
            {
                /* AI speech rows (shown by voice_speech_apply while Speech is AI speech). */
                size_t np = 0;
                const VoiceAisProvider *tab = voice_ais_providers(&np);
                HWND lbl = make_label2(nd, SH_VOICE_TTS_PROVIDER);
                HWND cmb = make_combo2(nd, IDC_VOICE_TTS_PROVIDER, CBS_DROPDOWNLIST,
                                       SH_VOICE_TTS_PROVIDER);
                int sel = -1;
                for (size_t i = 0; i < np; i++) {
                    SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)tab[i].label);
                    if (strcmp(vs->voice_tts_provider, tab[i].id) == 0) sel = (int)i;
                }
                SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);   /* -1: an unknown id shows none (R38) */
                nd->ctrl_tts_provider = add_ctrl(nd, lbl, cmb, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 0, 0);
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_TTS_URL);
                HWND ed = make_edit2(nd, vs->voice_tts_url, IDC_VOICE_TTS_URL, 0,
                                     SH_VOICE_TTS_URL);
                SendMessage(ed, EM_SETLIMITTEXT, (WPARAM)(sizeof(vs->voice_tts_url) - 1u), 0);
                nd->ctrl_tts_url = add_ctrl(nd, lbl, ed, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 0, 0);
            }
            {
                nd->hVoiceTtsWarn = make_hint(nd,
                    "Not encrypted: your API key and each spoken reply's text cross "
                    "the network in plain text.");
                nd->ctrl_tts_url_warn = add_ctrl(nd, NULL, nd->hVoiceTtsWarn, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 2, 0, 0, 1);
            }
            {
                /* A saved key shows only for the provider it was entered for (R43). */
                char bind[CFG_STR_MAX];
                voice_ais_binding(vs, bind, sizeof(bind));
                const int match = bind[0] && strcmp(vs->tts_api_key_for, bind) == 0;
                HWND lbl = make_label2(nd, SH_VOICE_TTS_KEY);
                HWND ed = make_edit2(nd, match ? vs->tts_api_key : "", IDC_VOICE_TTS_KEY,
                                     ES_PASSWORD, SH_VOICE_TTS_KEY);
                SendMessage(ed, EM_SETLIMITTEXT, (WPARAM)(sizeof(vs->tts_api_key) - 1u), 0);
                nd->ctrl_tts_key = add_ctrl(nd, lbl, ed, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 0, 0);
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_AI_VOICE);
                HWND cmb = make_combo2(nd, IDC_VOICE_AI_VOICE, CBS_DROPDOWN, SH_VOICE_AI_VOICE);
                SendMessage(cmb, CB_LIMITTEXT, (WPARAM)(sizeof(vs->voice_ai_voice) - 1u), 0);
                nd->ctrl_ais_voice = add_ctrl(nd, lbl, cmb, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 0, 0);
            }
            {
                HWND lbl = make_label2(nd, SH_VOICE_AI_MODEL);
                HWND cmb = make_combo2(nd, IDC_VOICE_AI_MODEL, CBS_DROPDOWN, SH_VOICE_AI_MODEL);
                SendMessage(cmb, CB_LIMITTEXT, (WPARAM)(sizeof(vs->voice_ai_tts_model) - 1u), 0);
                nd->ctrl_ais_model = add_ctrl(nd, lbl, cmb, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, 0, 0);
            }
            voice_ais_seed(nd, vs->voice_ai_voice, vs->voice_ai_tts_model);
            {
                /* Accent and style: multi-line, 1,023 bytes; newlines are stored as \n. */
                char shown[2200];
                size_t o = 0;
                for (const char *q = vs->voice_ai_style; *q && o + 2 < sizeof(shown); q++) {
                    if (*q == '\n') shown[o++] = '\r';
                    shown[o++] = *q;
                }
                shown[o] = '\0';
                HWND lbl = make_label2(nd, SH_VOICE_AI_STYLE);
                HWND ed = page_ctrl(nd, "EDIT", shown,
                    WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                    IDC_VOICE_AI_STYLE, SH_VOICE_AI_STYLE);
                SendMessage(ed, EM_SETLIMITTEXT, (WPARAM)(sizeof(vs->voice_ai_style) - 1u), 0);
                SendMessage(ed, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                            MAKELPARAM(3, 3));
                nd->ctrl_ais_style = add_ctrl(nd, lbl, ed, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 3, 0, 0, 0);
            }
            {
                nd->hVoicePrivacy = make_hint(nd, "");
                nd->ctrl_ais_privacy = add_ctrl(nd, NULL, nd->hVoicePrivacy, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 2, 0, 0, 1);
            }
            {
                HWND btn = page_ctrl(nd, "BUTTON", "Play sample", BS_OWNERDRAW,
                                     IDC_VOICE_PLAY_SAMPLE, SH_VOICE_PLAY_SAMPLE);
                add_ctrl(nd, NULL, btn, NULL, SETTINGS_PAGE_VOICE, 140, 0, 1, 0, 1, 1);
            }
            {
                nd->hVoiceNoteAec = make_hint(nd,
                    "Your microphone reports no echo cancellation: a headset works best");
                nd->ctrl_voice_note_aec = add_ctrl(nd, NULL, nd->hVoiceNoteAec, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 1, 0, voice_conv_last_aec() == 0, 1);
            }
            {
                nd->hVoiceNoteDuck = make_hint(nd,
                    "Windows may lower other sounds during a conversation. To stop that: "
                    "Control Panel > Sound > Communications > Do nothing.");
                nd->ctrl_voice_note_duck = add_ctrl(nd, NULL, nd->hVoiceNoteDuck, NULL,
                    SETTINGS_PAGE_VOICE, 0, 0, 2, 0,
                    vs->voice_mode == VOICE_MODE_CONVERSATION, 1);
            }
            /* Voice saved on: ask the audio thread for the microphones and
             * to tell us when one comes or goes. */
            g_voice_dlg = nd;
            if (nd->voice_dev_listed) {
                voice_audio_enumerate();
                voice_audio_watch_devices(1);
            }
        }

        /* ==== AI_WEB ==== */
        {
            HWND chk = make_check2(nd, IDC_AI_WEB_FETCH, SH_AI_WEB_FETCH);
            SendMessage(chk, BM_SETCHECK,
                        nd->cfg->settings.ai_web_fetch_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            add_ctrl(nd, NULL, chk, NULL, SETTINGS_PAGE_AI_WEB, 0, 0, 1, 0, 1, 1);
        }
        {
        int is_custom_search =
            (_stricmp(nd->cfg->settings.ai_search_provider, "custom") == 0);
        {
            HWND lbl = make_label2(nd, SH_AI_SEARCH);
            HWND cmb = make_combo2(nd, IDC_AI_SEARCH_COMBO, CBS_DROPDOWNLIST,
                                   SH_AI_SEARCH);
            int sel = 0;
            for (int i = 0; i < search_provider_count(); i++)
                SendMessage(cmb, CB_ADDSTRING, 0, (LPARAM)search_provider_label(i));
            /* An unknown stored value (-1) shows the first entry. */
            sel = search_provider_index(nd->cfg->settings.ai_search_provider);
            if (sel < 0) sel = 0;
            SendMessage(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
            add_ctrl(nd, lbl, cmb, NULL, SETTINGS_PAGE_AI_WEB, 0, 0, 1, 0, 1, 0);
        }
        {
            HWND lbl = make_label2(nd, SH_AI_SEARCH_URL);
            HWND ed = make_edit2(nd, nd->cfg->settings.ai_search_url,
                                 IDC_AI_SEARCH_URL, 0, SH_AI_SEARCH_URL);
            nd->ctrl_ai_search_url = add_ctrl(nd, lbl, ed, NULL,
                SETTINGS_PAGE_AI_WEB, 0, 0, 1, 0, is_custom_search, 0);
        }
        }
        {
            char buf[8];
            int max_r = nd->cfg->settings.ai_max_search_results;
            if (max_r <= 0) max_r = 7;
            (void)snprintf(buf, sizeof(buf), "%d", max_r);
            HWND lbl = make_label2(nd, SH_AI_MAX_RESULTS);
            HWND ed = make_edit2(nd, buf, IDC_AI_MAX_RESULTS, ES_NUMBER,
                                 SH_AI_MAX_RESULTS);
            add_ctrl(nd, lbl, ed, NULL, SETTINGS_PAGE_AI_WEB, 70, 0, 1, 0, 1, 0);
        }

        /* ==== DEBUGGING ==== */
        debug_build_page(nd);

        /* ==== ABOUT ==== */
        {
            HWND s1 = make_hint(nd, "Nutshell v" APP_VERSION);
            add_ctrl(nd, NULL, s1, NULL, SETTINGS_PAGE_ABOUT, 0, 0, 2, 0, 1, 1);
        }
        {
            HWND s2 = make_hint(nd, "Copyright (C) 2026 Thomas Sulkiewicz");
            add_ctrl(nd, NULL, s2, NULL, SETTINGS_PAGE_ABOUT, 0, 0, 1, 0, 1, 1);
        }
        {
            nd->hAboutBlurb = make_hint(nd,
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

        /* ---- Fonts ---- */
        {
            nd->hDlgFont = ns_font(FONT_BODY, nd->dpi);
            /* Page title is a heading, not just bold body text. */
            nd->hBoldFont = ns_font(FONT_TITLE, nd->dpi);
            if (nd->hDlgFont)
                EnumChildWindows(hwnd, SetFontProc, (LPARAM)nd->hDlgFont);
            if (nd->hBoldFont && nd->hPageTitle)
                SendMessage(nd->hPageTitle, WM_SETFONT, (WPARAM)nd->hBoldFont, TRUE);
            /* The Voice page's three section labels use the heading font too. */
            for (int vi = 0; vi < 3; vi++)
                if (nd->hBoldFont && nd->hVoiceHead[vi])
                    SendMessage(nd->hVoiceHead[vi], WM_SETFONT,
                                (WPARAM)nd->hBoldFont, TRUE);
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

        /* ---- Help tip: one tool per control, from the help table. Created
         * after the fonts and the theme block: its colours come from the
         * dialog's own theme (nd->theme), not the main window's. ---- */
        {
            ThemeTokens tok;
            ui_theme_resolve(nd->theme, &tok);
            nd->hTip = ns_tip_create(hwnd, nd->dpi, nd->hDlgFont, &tok);
            if (nd->hTip) {
                HWND hModel = GetDlgItem(nd->hPage, IDC_AI_CUSTOM_MODEL);
                HWND hVModel = GetDlgItem(nd->hPage, IDC_VOICE_MODEL);
                HWND hVLive = GetDlgItem(nd->hPage, IDC_VOICE_LIVE_MODEL);
                HWND hAVoice = GetDlgItem(nd->hPage, IDC_VOICE_AI_VOICE);
                HWND hAModel = GetDlgItem(nd->hPage, IDC_VOICE_AI_MODEL);
                for (int i = 0; i < nd->n_tips; i++) {
                    const SettingsHelp *e = settings_help_get((int)nd->tips[i].help);
                    if (!e) continue;
                    (void)ns_tip_add(nd->hTip, nd->tips[i].hwnd, e->text);
                    /* The editable combo's own window covers only its button:
                     * its edit gets a second tool with the same text. */
                    if (nd->tips[i].hwnd == hModel)
                        (void)ns_tip_add_combo(nd->hTip, hModel, e->text);
                    if (nd->tips[i].hwnd == hVModel)
                        (void)ns_tip_add_combo(nd->hTip, hVModel, e->text);
                    if (nd->tips[i].hwnd == hVLive)
                        (void)ns_tip_add_combo(nd->hTip, hVLive, e->text);
                    if (nd->tips[i].hwnd == hAVoice)
                        (void)ns_tip_add_combo(nd->hTip, hAVoice, e->text);
                    if (nd->tips[i].hwnd == hAModel)
                        (void)ns_tip_add_combo(nd->hTip, hAModel, e->text);
                }
                /* A disabled child takes no mouse input; the page host sees it
                 * instead, so Session and the recorder level also get a rect
                 * tool, placed by relayout_page(). */
                const SettingsHelp *es = settings_help_get((int)SH_AUTOCONNECT_SESSION);
                const SettingsHelp *er = settings_help_get((int)SH_DBG_RECORDER);
                if (es)
                    (void)ns_tip_add_rect(nd->hTip, nd->hPage, IDC_AUTOCONNECT_COMBO,
                                          NULL, es->text);
                if (er)
                    (void)ns_tip_add_rect(nd->hTip, nd->hPage, IDC_DBG_RECORDER_LEVEL,
                                          NULL, er->text);
            }
        }

        /* ---- Initial nav selection + first layout pass ---- */
        {
            int idx = -1;
            if (nd->initial_page >= 0)
                idx = settings_nav_index_of_page(nd->initial_page);
            if (idx < 0)
                idx = settings_nav_first_page();
            SendMessage(nd->hNav, LB_SETCURSEL, (WPARAM)idx, 0);
            const SettingsNavEntry *e = settings_nav_at(idx);
            nd->cur_page = e ? e->page_id : SETTINGS_PAGE_APPEARANCE;
            SetWindowText(nd->hPageTitle, settings_page_title(nd->cur_page));
        }
        ai_url_warn_sync(nd);
        ai_enable_sync(nd);
        voice_enable_sync(nd);
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
        if (!d) return 0;   /* tearing down: WM_DESTROY dropped the window's reference */
        if (ctx != d->models) return 0;   /* not the request this window is waiting on */
        d->models = NULL;
        thread_join_reap(&d->models_thread);   /* its handle: the thread is on its way out */
        if (ctx && ctx->result) {
            /* Re-enable the refresh button (only while the switch is on) */
            ai_enable_sync(d);
            voice_enable_sync(d);

            if (strncmp(ctx->result, "Error:", 6) == 0) {
                /* ctx stays alive until the release below; d is not read
                 * again after the notice (the window may be gone). */
                (void)notice_dlg_msg(hwnd, NOTICE_MSG_MODELS_ERROR,
                                     ctx->result, d->theme);
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
                    (void)notice_dlg_msg(hwnd, NOTICE_MSG_MODELS_NONE,
                                         NULL, d->theme);
            }
        }
        models_ctx_release(ctx);   /* the window's reference; wipes the key when last */
        return 0;
    }

    case WM_TIMER:
        if (wParam == IDT_SYSNOTES_SCROLL && d) {
            sys_notes_sync_scroll(d);
            return 0;
        }
        if (wParam == IDT_HELP_POP && d) {
            /* The F1 help follows the focus: it goes when the focus moves. */
            if (focus_owner(GetFocus()) != d->tip_focus) help_pop_hide(d);
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
                    if (d->cur_page != SETTINGS_PAGE_VOICE)
                        voice_test_apply(d, VT_EV_PAGE_AWAY);
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
                if (sel >= 0 && sel < search_provider_count() && d->ctrl_ai_search_url) {
                    d->ctrl_ai_search_url->visible =
                        (_stricmp(search_provider_value(sel), "custom") == 0);
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
                    ai_url_warn_sync(d);
                    /* Clear model — user must press refresh to populate */
                    HWND hMdl = GetDlgItem(d->hPage, IDC_AI_CUSTOM_MODEL);
                    SendMessage(hMdl, CB_RESETCONTENT, 0, 0);
                    SetWindowText(hMdl, "");
                    relayout(hwnd, d);
                    InvalidateRect(d->hPage, NULL, TRUE);
                }
                voice_enable_sync(d);   /* the speech-to-text reason and model list follow it */
            }
            break;

        case IDC_AI_ENABLE:
            if (d && HIWORD(wParam) == BN_CLICKED) {
                ai_enable_sync(d);
                voice_enable_sync(d);
            }
            break;

        case IDC_AI_CUSTOM_URL:
            if (HIWORD(wParam) == EN_CHANGE && d) {
                ai_url_warn_update(d);
                voice_enable_sync(d);   /* the reason Use current AI assistant is unavailable depends on it */
            }
            break;

        case IDC_VOICE_ENABLE:
            if (d && HIWORD(wParam) == BN_CLICKED)
                voice_enable_sync(d);
            break;

        case IDC_VOICE_USE_AI:
            if (d && HIWORD(wParam) == BN_CLICKED) {
                d->voice_use_ai_wanted =
                    IsDlgButtonChecked(d->hPage, IDC_VOICE_USE_AI) == BST_CHECKED;
                voice_enable_sync(d);
                relayout(hwnd, d);
                InvalidateRect(d->hPage, NULL, TRUE);
            }
            break;

        case IDC_VOICE_PTT_MODE:
        case IDC_VOICE_MODE:
            if (d && HIWORD(wParam) == CBN_SELCHANGE) voice_enable_sync(d);
            break;

        case IDC_VOICE_WIN_VOICE:
            if (d && HIWORD(wParam) == CBN_SELCHANGE) {
                d->voice_win_touched = 1;   /* a choice replaces "not installed" */
                if (d->ctrl_voice_note_voice && d->ctrl_voice_note_voice->visible) {
                    d->ctrl_voice_note_voice->visible = 0;
                    relayout(hwnd, d);
                    InvalidateRect(d->hPage, NULL, TRUE);
                }
            }
            break;

        case IDC_VOICE_PLAY_SAMPLE:
            if (d && HIWORD(wParam) == BN_CLICKED) voice_play_sample(d);
            break;

        case IDC_VOICE_DEVICE:
            if (d && HIWORD(wParam) == CBN_SELCHANGE) {
                d->voice_dev_touched = 1;
                voice_test_apply(d, VT_EV_DEVICE_CHANGED);   /* a running Test follows the new microphone */
            }
            break;

        case IDC_VOICE_LANG:
            if (d && HIWORD(wParam) == CBN_SELCHANGE) {
                voice_lang_sync(d, 1);
                if (voice_lang_is_other(d))
                    SetFocus(GetDlgItem(d->hPage, IDC_VOICE_LANG_OTHER));
            }
            break;

        case IDC_VOICE_LANG_OTHER:
            if (d && HIWORD(wParam) == EN_CHANGE) voice_lang_sync(d, 1);
            break;

        case IDC_VOICE_TEST:
            if (d && HIWORD(wParam) == BN_CLICKED) voice_test_apply(d, VT_EV_PRESS);
            break;

        case IDC_VOICE_GAIN:
            if (d && HIWORD(wParam) == NSS_CHANGED) voice_gain_changed(d);
            break;

        case IDC_VOICE_PROVIDER:
            if (d && HIWORD(wParam) == CBN_SELCHANGE) {
                /* A model that is empty or one of the old provider's seeds
                 * follows the provider; one the user typed is kept. */
                const SttProvider *prev = d->voice_seeded;
                char cur[CFG_STR_MAX];
                cur[0] = '\0';
                GetDlgItemText(d->hPage, IDC_VOICE_MODEL, cur, (int)sizeof(cur));
                int follow = (cur[0] == '\0');
                for (size_t k = 0; !follow && prev && prev->models && prev->models[k]; k++)
                    if (strcmp(cur, prev->models[k]) == 0) follow = 1;
                voice_enable_sync(d);
                if (follow && d->voice_seeded && d->voice_seeded != prev &&
                    d->voice_seeded->models && d->voice_seeded->models[0])
                    SetDlgItemText(d->hPage, IDC_VOICE_MODEL, d->voice_seeded->models[0]);
                relayout(hwnd, d);
                InvalidateRect(d->hPage, NULL, TRUE);
            }
            break;

        case IDC_VOICE_URL:
            if (d && HIWORD(wParam) == EN_CHANGE)
                voice_enable_sync(d);
            break;

        case IDC_VOICE_SPEECH:
            if (d && HIWORD(wParam) == CBN_SELCHANGE)
                voice_enable_sync(d);   /* relays out when a row comes or goes */
            break;

        case IDC_VOICE_TTS_PROVIDER:
            if (d && HIWORD(wParam) == CBN_SELCHANGE) {
                voice_ais_seed(d, NULL, NULL);   /* the new provider's default first (R16) */
                voice_enable_sync(d);
            }
            break;

        case IDC_VOICE_TTS_URL:
            if (d && HIWORD(wParam) == EN_CHANGE)
                voice_enable_sync(d);
            break;

        case IDC_VOICE_TTS_KEY:
            if (d && HIWORD(wParam) == EN_CHANGE && !d->voice_tts_key_setting) {
                /* Only a key the user typed is rebound at Save, to the binding it was typed
                 * for (R43). */
                Settings *c = voice_page_copy(d);
                if (c) {
                    voice_ais_binding(c, d->voice_tts_key_bound, sizeof(d->voice_tts_key_bound));
                    d->voice_tts_key_touched = 1;
                    voice_page_free(c);
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
                        /* Strip /chat/completions or similar, append /models;
                         * a failure leaves custom_models_url empty. */
                        (void)ai_models_url_from_base(base, custom_models_url,
                                                      sizeof(custom_models_url));
                        models_url = custom_models_url;
                    }
                }

                if (!models_url || !models_url[0]) {
                    (void)notice_dlg_msg(hwnd, NOTICE_MSG_MODELS_NO_URL,
                                         NULL, d->theme);
                    break;
                }

                /* Get API key */
                char api_key[256];
                GetDlgItemText(d->hPage, IDC_AI_KEY_EDIT, api_key, (int)sizeof(api_key));
                if (!api_key[0]) {
                    (void)notice_dlg_msg(hwnd, NOTICE_MSG_MODELS_NO_KEY,
                                         NULL, d->theme);
                    break;
                }

                /* Launch background thread */
                FetchModelsCtx *ctx = d->models ? NULL
                                      : calloc(1, sizeof(FetchModelsCtx));
                if (ctx) {
                    worker_life_init(&ctx->life);   /* the window's and the thread's */
                    ai_nc_init(&ctx->nc);
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
                        /* Kept, not closed: closing Settings fires ctx->nc
                         * and joins it. */
                        d->models = ctx;
                        thread_join_add(&d->models_thread, ht);
                        /* Disable button while fetching */
                        EnableWindow(GetDlgItem(d->hPage, IDC_AI_REFRESH), FALSE);
                    } else {
                        secure_zero(ctx->headers, sizeof(ctx->headers));
                        free(ctx);
                    }
                }
                secure_zero(api_key, sizeof(api_key));
            }
            break;

        case IDC_DBG_ENABLE:
            /* Show or hide every row below the tickbox. */
            if (d && HIWORD(wParam) == BN_CLICKED) {
                debug_apply_visibility(d);
                relayout(hwnd, d);
                InvalidateRect(d->hPage, NULL, TRUE);
            }
            break;

        case IDC_DBG_RECORDER:
            if (d)
                EnableWindow(GetDlgItem(d->hPage, IDC_DBG_RECORDER_LEVEL),
                             IsDlgButtonChecked(d->hPage, IDC_DBG_RECORDER)
                                 == BST_CHECKED ? TRUE : FALSE);
            break;

        case IDC_DBG_BROWSE:
            if (d && HIWORD(wParam) == BN_CLICKED) debug_browse_folder(hwnd, d);
            break;

        case IDC_DBG_OPEN:
            if (d && HIWORD(wParam) == BN_CLICKED) debug_open_folder(hwnd, d);
            break;

        case IDC_AUTOCONNECT_CHECK:
            if (d)
                EnableWindow(GetDlgItem(d->hPage, IDC_AUTOCONNECT_COMBO),
                             IsDlgButtonChecked(d->hPage, IDC_AUTOCONNECT_CHECK)
                                 == BST_CHECKED ? TRUE : FALSE);
            break;

        case IDOK: {
            if (!d) { DestroyWindow(hwnd); break; }
            if (d->saving) break;
            /* --ui-demo=settings-debug: show the page, change nothing. */
            if (d->demo) { DestroyWindow(hwnd); break; }
            Settings *s = &d->cfg->settings;

            /* Turning the assistant off while it works stops the reply and
             * closes the conversations: ask first. A yes/no decision, not a
             * notice. No leaves the dialog open with the box ticked again,
             * before anything has been read back. */
            {
                int want = (IsDlgButtonChecked(d->hPage, IDC_AI_ENABLE)
                            == BST_CHECKED) ? 1 : 0;
                if (s->ai_enabled && !want && ui_ai_is_working()) {
                    /* Modal: d->saving makes a second Save/Cancel/close a
                     * no-op meanwhile, and d is fetched again after it in
                     * case the window went (as the policy confirm below). */
                    d->saving = 1;
                    int ans = MessageBoxA(hwnd,
                        "Turn off the AI assistant? The running reply will be "
                        "stopped and open conversations closed.",
                        "AI assistant", MB_YESNO | MB_ICONWARNING);
                    if (!IsWindow(hwnd)) return 0;
                    d = (SettingsDlgData *)(LONG_PTR)GetWindowLongPtr(
                        hwnd, GWLP_USERDATA);
                    if (!d) return 0;
                    d->saving = 0;
                    s = &d->cfg->settings;
                    if (ans != IDYES) {
                        CheckDlgButton(d->hPage, IDC_AI_ENABLE, BST_CHECKED);
                        ai_enable_sync(d);
                        voice_enable_sync(d);
                        break;
                    }
                }
                s->ai_enabled = want;
            }

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
            /* M-4: a new, non-empty key just typed in drops any stale
             * foreign/corrupt blob sitting in memory from before -- see
             * config_secret_drop_stale_preserved()'s doc comment. Without
             * this, clearing the field again later in the same run (no
             * config reload in between) would resurrect that old blob on
             * save instead of writing "". */
            config_secret_drop_stale_preserved(s->ai_api_key,
                s->ai_api_key_enc_preserved, sizeof(s->ai_api_key_enc_preserved));

            /* AI custom URL */
            GetDlgItemText(d->hPage, IDC_AI_CUSTOM_URL,
                           s->ai_custom_url, (int)sizeof(s->ai_custom_url));

            /* Commands as tool calls: the item index is the mode */
            {
                int sel = (int)SendDlgItemMessage(d->hPage, IDC_AI_RUNCMD_MODE,
                                                  CB_GETCURSEL, 0, 0);
                if (sel == AI_RUNCMD_AUTO || sel == AI_RUNCMD_ON || sel == AI_RUNCMD_OFF)
                    s->ai_runcmd_mode = sel;
            }

            /* Thinking display: the item index is the ThinkPref */
            {
                int sel = (int)SendDlgItemMessage(d->hPage, IDC_AI_THINKING_MODE,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= THINK_PREF_AUTO && sel <= THINK_PREF_MAX &&
                    sel != d->think_initial)
                    s->ai_thinking_mode = sel;
            }

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
                if (sel >= 0 && sel < search_provider_count()) {
                    strncpy(s->ai_search_provider,
                            search_provider_value(sel),
                            sizeof(s->ai_search_provider) - 1);
                    s->ai_search_provider[sizeof(s->ai_search_provider) - 1] = '\0';
                }
            }

            /* Custom search URL */
            GetDlgItemText(d->hPage, IDC_AI_SEARCH_URL,
                           s->ai_search_url, (int)sizeof(s->ai_search_url));

            /* Max search results */
            v = GetDlgItemInt(d->hPage, IDC_AI_MAX_RESULTS, &ok, FALSE);
            s->ai_max_search_results =
                keep_entry(SH_AI_MAX_RESULTS, ok, v, s->ai_max_search_results);

            /* Tool approval timeout: 0 (never) or 15..3600 s; keep the stored
             * value on a parse failure or any other entry, as the idle
             * timeout does */
            v = GetDlgItemInt(d->hPage, IDC_AI_TOOL_TIMEOUT, &ok, FALSE);
            s->ai_tool_timeout_sec =
                keep_entry(SH_AI_TOOL_TIMEOUT, ok, v, s->ai_tool_timeout_sec);

            /* Automatic tool rounds: 1..20; keep the stored value on a parse
             * failure or any other entry (0 is not allowed) */
            v = GetDlgItemInt(d->hPage, IDC_AI_TOOL_ROUNDS, &ok, FALSE);
            s->ai_tool_auto_rounds =
                keep_entry(SH_AI_TOOL_ROUNDS, ok, v, s->ai_tool_auto_rounds);

            voice_page_read(d, s);

            /* Permit web fetch */
            s->ai_web_fetch_enabled = (IsDlgButtonChecked(d->hPage, IDC_AI_WEB_FETCH)
                                        == BST_CHECKED) ? 1 : 0;

            /* Render AI markdown */
            s->markdown_render_enabled = (IsDlgButtonChecked(d->hPage, IDC_AI_MD_RENDER)
                                           == BST_CHECKED) ? 1 : 0;

            /* Command policy for new sessions: the level and Auto-approve.
             * Critical with Auto-approve on asks first, but only when Save
             * would enter that state from the stored policy -- the same
             * rule as the panel's ai_chat_request_policy_change() (spec 2026-10-18
             * section 2); No keeps the stored policy as it was and the
             * rest of the page still saves. The box is modal, so
             * d->saving makes a second Save/Cancel/close during it a no-op,
             * and d is looked up again after it in case the window went. */
            {
                int a = (int)SendDlgItemMessage(d->hPage, IDC_AI_POLICY_ALLOWED,
                                                CB_GETCURSEL, 0, 0);
                int on = (IsDlgButtonChecked(d->hPage, IDC_AI_POLICY_AUTO)
                              == BST_CHECKED) ? 1 : 0;
                if (a >= 0) {
                    CmdPolicy next = s->ai_policy_default;
                    cmd_policy_set_allowed(&next, a);
                    cmd_policy_set_auto(&next, on);
                    int store = 1;
                    if (cmd_policy_needs_confirm(s->ai_policy_default, next)) {
                        d->saving = 1;
                        int answer = MessageBoxA(hwnd, CMD_POLICY_CONFIRM_TEXT,
                                                 "Auto-approve by default",
                                                 MB_YESNO | MB_ICONWARNING |
                                                 MB_DEFBUTTON2);
                        if (!IsWindow(hwnd)) return 0;
                        d = (SettingsDlgData *)(LONG_PTR)GetWindowLongPtr(
                            hwnd, GWLP_USERDATA);
                        if (!d) return 0;
                        d->saving = 0;
                        s = &d->cfg->settings;
                        store = (answer == IDYES);
                    }
                    if (store)
                        s->ai_policy_default = next;
                }
            }

            /* SSH user idle timeout */
            v = GetDlgItemInt(d->hPage, IDC_SSH_IDLE_EDIT, &ok, FALSE);
            s->ssh_user_idle_timeout_mins =
                keep_entry(SH_SSH_IDLE, ok, v, s->ssh_user_idle_timeout_mins);
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

            /* Debugging page. window.c applies it to the live logger
             * (dlog_configure) once this dialog has closed. */
            debug_read_page(d, s);

            /* Clamp out-of-range values before persisting */
            settings_validate(s);
            /* M5: report a save failure instead of silently discarding the
             * changes just applied above -- the file may be read-only,
             * locked by another program, or saving may have been disabled
             * for this session because nutshell.config could not be read
             * at startup (see window.c's WM_CREATE / M3). */
            if (config_save(d->cfg, d->config_path) != 0) {
                Notice sf;
                notice_save_failed(&sf,
                    "Could not save nutshell.config. Your settings changes "
                    "have not been written to disk.");
                (void)notice_dlg_show(hwnd, &sf, d->theme);
            }
            DestroyWindow(hwnd);
            break;
        }

        case IDCANCEL:
            if (d && d->saving) break;
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        if (d) {
            /* A Refresh Models request still out: close its handle so the
             * blocked GET returns now, give the thread a moment to finish,
             * then drop the window's reference (the thread frees the rest,
             * wiping the key, if it is still running). Its result message,
             * if already posted, dies with this window. */
            if (d->models) {
                ai_nc_fire(&d->models->nc);
                (void)thread_join_wait(&d->models_thread, 1000);
                models_ctx_release(d->models);
                d->models = NULL;
            }
            thread_join_detach_all(&d->models_thread);   /* whatever is left, finished or not */
            KillTimer(hwnd, IDT_SYSNOTES_SCROLL);
            if (g_voice_dlg == d) {
                voice_test_apply(d, VT_EV_CLOSE);
                if (d->voice_testing) { voice_audio_capture_end(0); d->voice_testing = 0; }
                if (d->voice_dev_listed) voice_audio_watch_devices(0);
                g_voice_dlg = NULL;
            }
            if (d->hTip) DestroyWindow(d->hTip);
            /* d->hDlgFont / d->hBoldFont come from the ns_font cache. */
            if (d->hBrBgPrimary)   DeleteObject(d->hBrBgPrimary);
            if (d->hBrBgSecondary) DeleteObject(d->hBrBgSecondary);
            free(d);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)NULL);
        }
        return 0;

    case WM_ACTIVATE:
        /* Mask a held-open API key when the window loses focus; the edit
         * is a descendant of the page window. Falls through to
         * DefWindowProc below. */
        if (LOWORD(wParam) == WA_INACTIVE)
            pw_reveal_mask_all(hwnd);
        break;

    case WM_CLOSE:
        if (d && d->saving) return 0;
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProc(hwnd, umsg, wParam, lParam);
}

/* ---- Public API --------------------------------------------------------- */

/* The one open Settings dialog, or NULL. A second request while it is open
 * brings it to the front instead of creating another. */
static HWND s_open = NULL;

/* Returns 1 when it ran a dialog (opened and closed), 0 when it ran none:
 * one is already open, cfg is NULL, or the window could not be created. */
static int settings_dlg_run(HWND parent, Config *cfg, const char *config_path,
                            int initial_page, int demo)
{
    if (s_open && IsWindow(s_open)) {
        SetForegroundWindow(s_open);
        return 0;
    }
    if (!cfg) return 0;

    /* Allocate dialog state — freed in WM_DESTROY */
    SettingsDlgData *d = (SettingsDlgData *)calloc(1u, sizeof(SettingsDlgData));
    if (!d) return 0;
    d->cfg = cfg;
    d->config_path = config_path;
    d->initial_page = initial_page;
    d->demo = demo;

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

    /* The window is created hidden at the owner monitor's work-area top-left
     * at a provisional size, so it lands on that monitor and WM_CREATE reads
     * that monitor's DPI. Once the pages are built it is sized and placed by
     * settings_initial_placement() and only then shown. */
    int pdpi = get_window_dpi(parent);
    int client_w = ns_scale(760, pdpi);
    int client_h = ns_scale(560, pdpi);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                  WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
    RECT wr = {0, 0, client_w, client_h};
    AdjustWindowRect(&wr, style, FALSE);

    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
    if (!mon || !GetMonitorInfo(mon, &mi)) {
        mi.rcWork.left = 0;
        mi.rcWork.top = 0;
        mi.rcWork.right = GetSystemMetrics(SM_CXSCREEN);
        mi.rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    HWND hwnd = CreateWindowEx(
        0, SETTINGS_CLASS, "Settings",
        style,
        mi.rcWork.left, mi.rcWork.top,
        wr.right - wr.left, wr.bottom - wr.top,
        parent, NULL, GetModuleHandle(NULL), d);

    if (hwnd) {
        s_open = hwnd;
        EnableWindow(parent, FALSE);

        /* Behaviour's rows (title included) set the opening height. Read
         * everything from d now: WM_DESTROY frees it. */
        int target_rows = 1;
        for (int i = 0; i < d->n_ctrls; i++) {
            if (d->ctrls[i].page == SETTINGS_PAGE_AI_BEHAVIOUR)
                target_rows += d->ctrls[i].rows;
        }
        SettingsMetrics pm;
        settings_metrics_init(&pm, d->dpi);
        /* AdjustWindowRect gives the frame at the system DPI, but the
         * client is laid out at d->dpi (the owner monitor's). The window
         * exists, hidden, on that monitor: measure its real frame, and keep
         * the AdjustWindowRect size only if a call fails. */
        int ncw = (int)(wr.right - wr.left) - client_w;
        int nch = (int)(wr.bottom - wr.top) - client_h;
        RECT fw, fc;
        if (GetWindowRect(hwnd, &fw) && GetClientRect(hwnd, &fc)) {
            ncw = (int)((fw.right - fw.left) - (fc.right - fc.left));
            nch = (int)((fw.bottom - fw.top) - (fc.bottom - fc.top));
        }
        int cw = 0, ch = 0, wx = 0, wy = 0;
        settings_initial_placement(&pm, target_rows,
                                   (int)mi.rcWork.left, (int)mi.rcWork.top,
                                   (int)(mi.rcWork.right - mi.rcWork.left),
                                   (int)(mi.rcWork.bottom - mi.rcWork.top),
                                   ncw, nch, &cw, &ch, &wx, &wy);
        SetWindowPos(hwnd, NULL, wx, wy, cw + ncw, ch + nch,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(hwnd, SW_SHOW);

        MSG msg;
        while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0)) {
            if (settings_key_pretranslate(hwnd, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        s_open = NULL;
        EnableWindow(parent, TRUE);
        SetFocus(parent);
        return 1;
    }
    free(d);
    return 0;
}

int settings_dlg_show(HWND parent, Config *cfg, const char *config_path,
                      int initial_page)
{
    return settings_dlg_run(parent, cfg, config_path, initial_page, 0);
}

void settings_dlg_show_demo_debug(HWND parent, Config *cfg)
{
    (void)settings_dlg_run(parent, cfg, "", SETTINGS_PAGE_DEBUG, 1);
}

void settings_dlg_show_demo_page(HWND parent, Config *cfg, int page)
{
    (void)settings_dlg_run(parent, cfg, "", page, 2);
}

#endif

