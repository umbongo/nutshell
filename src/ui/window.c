#ifdef _WIN32

#include <winsock2.h>   /* Must come before windows.h */
#include "ui.h"
#include "logger.h"
#include "renderer.h"
#include "term.h"
#include "tabs.h"
#include "xmalloc.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "resource.h"
#include "session_manager.h"
#include "settings_dlg.h"
#include "ssh_session.h"
#include "ssh_pty.h"
#include "ssh_io.h"
#include "knownhosts.h"
#include "log_format.h"
#include "paste_dlg.h"
#include "ai_chat.h"
#include "selection.h"
#include "app_font.h"
#include "ui_theme.h"
#include "custom_scrollbar.h"

static const char *CLASS_NAME = "Nutshell_Window";
static const char *APP_TITLE = "Nutshell";

#define TAB_HEIGHT 32
#define WM_SHOW_SESSION_MANAGER (WM_USER + 1)
#define WM_CONN_DONE            (WM_USER + 2)

typedef enum { CONN_IDLE, CONN_CONNECTING } ConnState;

typedef struct Session {
    Terminal   *term;
    SshSession *ssh;
    SSHChannel *channel;
    FILE       *session_log;   /* NULL when logging disabled */
    /* Connection thread state */
    ConnState       conn_state;
    volatile int    conn_cancelled;
    HANDLE          conn_thread;
    HWND            conn_hwnd;      /* main window HWND for PostMessage */
    Profile         conn_profile;  /* copy of profile for thread */
    int             conn_result;   /* 0=ok, 1=tcp/ssh, 2=auth, 3=channel */
    char            conn_error[512];
    ULONGLONG       conn_start_ms;
    int             conn_dots;     /* dots appended so far */
    CRITICAL_SECTION conn_cs;      /* H-1: guards conn_result/conn_error/ssh/channel */
    struct Session *next;
} Session;

/* Build the known_hosts file path: %APPDATA%\sshclient\known_hosts */
static void get_knownhosts_path(char *buf, size_t n)
{
    char appdata[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata)) == 0) {
        snprintf(buf, n, "known_hosts");
        return;
    }
    snprintf(buf, n, "%s\\sshclient\\known_hosts", appdata);
    /* Create the directory if it doesn't exist */
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\sshclient", appdata);
    CreateDirectoryA(dir, NULL); /* OK if already exists */
}

static HWND g_hwndTabs = NULL;
static Renderer g_renderer = {0};
static Config *g_config = NULL;
static HINSTANCE g_hInst = NULL;
static Session *g_active_session = NULL;
static Session *g_session_list = NULL;
static char g_config_path[MAX_PATH]; /* M-8: absolute path resolved at startup */
static HWND g_hwndAiChat = NULL;
static HWND g_hwndScrollbar = NULL;
static const ThemeColors *g_theme = NULL;
static void update_scrollbar(HWND hwnd); /* forward declaration */
static void paste_cancel(void);          /* forward declaration */

/* Invalidate only the terminal area (below the tab strip), not the tabs. */
static void invalidate_terminal(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.top = TAB_HEIGHT;
    InvalidateRect(hwnd, &rc, FALSE);
}

/* Paint cooldown: cap repaints at ~60fps to prevent thrashing on heavy output */
#define PAINT_COOLDOWN_MS 16
static DWORD g_last_paint_tick;

/* ---- Paste state machine (timer-driven, non-blocking) ------------------- */
#define PASTE_TIMER_ID 3

typedef struct {
    char       *buf;       /* malloc'd copy of clipboard text */
    char       *pos;       /* current read position within buf */
    HWND        hwnd;      /* window to repaint */
    int         delay_ms;  /* inter-line delay */
    SSHChannel *channel;   /* target channel (paste continues across tab switches) */
} PasteState;

static PasteState g_paste = {0};
static Selection g_selection = {0};

static Session *create_session(int rows, int cols) {
    Session *s = xmalloc(sizeof(Session));
    s->term = term_init(rows, cols, 3000);
    s->ssh = NULL;
    s->channel = NULL;
    s->session_log = NULL;
    s->conn_state = CONN_IDLE;
    s->conn_cancelled = 0;
    s->conn_thread = NULL;
    s->conn_hwnd = NULL;
    s->conn_result = 0;
    s->conn_error[0] = '\0';
    s->conn_start_ms = 0;
    s->conn_dots = 0;
    InitializeCriticalSection(&s->conn_cs);  /* H-1 */
    s->next = g_session_list;
    g_session_list = s;
    return s;
}

static void free_session(Session *s) {
    if (s) {
        if (s->conn_thread) {
            s->conn_cancelled = 1;
            WaitForSingleObject(s->conn_thread, 30000);
            CloseHandle(s->conn_thread);
        }
        DeleteCriticalSection(&s->conn_cs);  /* H-1 */
        if (s->channel) ssh_channel_free(s->channel);
        if (s->ssh) ssh_session_free(s->ssh);
        if (s->session_log) fclose(s->session_log);
        term_free(s->term);
        free(s);
    }
}

static void on_tab_select(int index, void *user_data) {
    (void)index;
    g_active_session = (Session *)user_data;
    HWND hParent = GetParent(g_hwndTabs);
    update_scrollbar(hParent);
    invalidate_terminal(hParent);
    SetFocus(hParent);

    /* Update AI chat with the new active session */
    if (g_hwndAiChat && IsWindow(g_hwndAiChat) && g_active_session) {
        ai_chat_set_session(g_hwndAiChat,
                           g_active_session->term,
                           g_active_session->channel);
    }
}

static void on_tab_close(int index, void *user_data) {
    Session *s = (Session *)user_data;

    if (s->conn_state == CONN_CONNECTING) {
        MessageBoxA(GetParent(g_hwndTabs),
                    "Connection in progress. Please wait.",
                    "Close Tab", MB_OK | MB_ICONINFORMATION);
        return;
    }

    /* Remove from linked list */
    if (g_session_list == s) {
        g_session_list = s->next;
    } else {
        Session *prev = g_session_list;
        while (prev && prev->next != s) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = s->next;
        }
    }

    if (g_active_session == s) {
        g_active_session = NULL;
    }

    free_session(s);
    tabs_remove(g_hwndTabs, index);
    
    /* Force repaint of terminal area after tab close */
    invalidate_terminal(GetParent(g_hwndTabs));
}

/* ---- Passphrase prompt --------------------------------------------------- */

typedef struct { char buf[256]; int ok; } PassCtx;

static LRESULT CALLBACK pass_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PassCtx *ctx = (PassCtx *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;
        ctx = (PassCtx *)cs->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
        CreateWindowExA(0, "STATIC", "Enter key passphrase:",
                        WS_CHILD | WS_VISIBLE,
                        8, 8, 264, 18, hwnd, NULL, NULL, NULL);
        CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
                        8, 30, 264, 22, hwnd, (HMENU)(UINT_PTR)101, NULL, NULL);
        CreateWindowExA(0, "BUTTON", "OK",
                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        100, 62, 80, 26, hwnd, (HMENU)(UINT_PTR)IDOK, NULL, NULL);
        CreateWindowExA(0, "BUTTON", "Cancel",
                        WS_CHILD | WS_VISIBLE,
                        190, 62, 80, 26, hwnd, (HMENU)(UINT_PTR)IDCANCEL, NULL, NULL);
        SetFocus(GetDlgItem(hwnd, 101));
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK && ctx) {
            GetWindowTextA(GetDlgItem(hwnd, 101), ctx->buf, (int)sizeof(ctx->buf));
            ctx->ok = 1;
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == IDCANCEL && ctx) {
            ctx->buf[0] = '\0';
            ctx->ok = 0;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* Returns 1 if user pressed OK, 0 if cancelled. Writes passphrase into out. */
static int prompt_passphrase(HWND parent, char *out, int out_size)
{
    static int class_done = 0;
    if (!class_done) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = pass_wnd_proc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = "NutshellPassDlg";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        class_done = 1;
    }

    PassCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    HWND hwnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "NutshellPassDlg", "SSH Key Passphrase",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 296, 128,
        parent, NULL, g_hInst, &ctx);
    if (!hwnd) return 0;

    if (parent) {
        RECT pr, wr;
        GetWindowRect(parent, &pr);
        GetWindowRect(hwnd, &wr);
        int w = wr.right  - wr.left;
        int h = wr.bottom - wr.top;
        int x = pr.left + (pr.right  - pr.left - w) / 2;
        int y = pr.top  + (pr.bottom - pr.top  - h) / 2;
        SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }

    if (parent) EnableWindow(parent, TRUE);

    if (ctx.ok && out && out_size > 0) {
        strncpy(out, ctx.buf, (size_t)(out_size - 1));
        out[out_size - 1] = '\0';
        SecureZeroMemory(ctx.buf, sizeof(ctx.buf));
        return 1;
    }
    /* M-5: zero passphrase buffer on cancellation too */
    SecureZeroMemory(ctx.buf, sizeof(ctx.buf));
    return 0;
}

/* Return the directory that contains the running executable. */
static void get_exe_dir(char *buf, size_t n)
{
    GetModuleFileNameA(NULL, buf, (DWORD)n);
    char *last = strrchr(buf, '\\');
    if (last) *last = '\0';
    else if (n > 0) buf[0] = '\0';
}

/* Open a timestamped session log file under log_dir.
 * Returns NULL if logging_enabled is false or on any error.
 * Caller is responsible for fclose(). */
static FILE *open_session_log(const char *hostname)
{
    if (!g_config || !g_config->settings.logging_enabled) return NULL;

    /* Determine log directory — default to %APPDATA%\sshclient\logs */
    char log_dir[MAX_PATH];
    if (g_config->settings.log_dir[0] != '\0') {
        (void)snprintf(log_dir, sizeof(log_dir), "%s",
                       g_config->settings.log_dir);
    } else {
        get_exe_dir(log_dir, sizeof(log_dir));
        if (log_dir[0] == '\0')
            (void)snprintf(log_dir, sizeof(log_dir), ".");
    }

    /* Create the directory (OK if already exists) */
    CreateDirectoryA(log_dir, NULL);

    /* M-6: validate log_format against a whitelist of safe strftime specifiers
     * before passing a user-controlled string to strftime. */
    static const char ALLOWED_SPECS[] = "YymdHMSjAaBbpZz%";
    const char *raw_fmt = g_config->settings.log_format[0]
                            ? g_config->settings.log_format
                            : "%Y-%m-%d_%H-%M-%S";
    int fmt_ok = 1;
    for (const char *q = raw_fmt; *q; q++) {
        if (*q == '%') {
            q++;
            if (!*q || !strchr(ALLOWED_SPECS, *q)) { fmt_ok = 0; break; }
        }
    }
    const char *fmt = fmt_ok ? raw_fmt : "%Y-%m-%d_%H-%M-%S";
    time_t now = time(NULL);
    const struct tm *t = localtime(&now);
    char ts[64];
    if (t) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        strftime(ts, sizeof(ts), fmt, t);
#pragma GCC diagnostic pop
    } else {
        (void)snprintf(ts, sizeof(ts), "unknown");
    }

    /* Sanitise hostname for use as a filename component */
    char safe_host[64];
    size_t hi = 0u;
    for (const char *p = hostname; *p && hi < sizeof(safe_host) - 1u; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '.') {
            safe_host[hi++] = c;
        } else {
            safe_host[hi++] = '_';
        }
    }
    safe_host[hi] = '\0';

    char path[MAX_PATH];
    (void)snprintf(path, sizeof(path), "%s\\%s_%s.log",
                   log_dir, ts, safe_host);

    return fopen(path, "ab");
}

/* ---- Background connection thread --------------------------------------- */

static DWORD WINAPI connection_thread(LPVOID param)
{
    Session *s = (Session *)param;
    const Profile *info = &s->conn_profile;
    HWND hwnd = s->conn_hwnd;

/* H-1: guard conn_result/conn_error writes with conn_cs before posting. */
#define CONN_FAIL(code) \
    do { EnterCriticalSection(&s->conn_cs); \
         s->conn_result = (code); \
         LeaveCriticalSection(&s->conn_cs); \
         PostMessage(hwnd, WM_CONN_DONE, 0, (LPARAM)s); return 0; } while(0)

    /* TCP connect + SSH handshake */
    s->ssh = ssh_session_new();
    if (ssh_connect(s->ssh, info->host, info->port) != 0) {
        snprintf(s->conn_error, sizeof(s->conn_error),
                 "Cannot connect to %s:%d\n\n%s",
                 info->host, info->port, s->ssh->last_error);
        CONN_FAIL(1);
    }

    if (s->conn_cancelled) { snprintf(s->conn_error, sizeof(s->conn_error), "Cancelled."); CONN_FAIL(1); }

    /* TOFU host key verification (MessageBoxA is thread-safe on Win32) */
    {
        size_t key_len = 0;
        int    key_type = 0;
        const char *key = libssh2_session_hostkey(s->ssh->session, &key_len, &key_type);
        if (!key || key_len == 0) {
            snprintf(s->conn_error, sizeof(s->conn_error), "Could not retrieve host key.");
            CONN_FAIL(1);
        }

        char kh_path[MAX_PATH];
        get_knownhosts_path(kh_path, sizeof(kh_path));

        KnownHosts kh;
        if (knownhosts_init(&kh, s->ssh->session, kh_path) == KNOWNHOSTS_OK) {
            char fingerprint[128];
            int tofu = knownhosts_check(&kh, info->host, info->port,
                                        key, key_len,
                                        fingerprint, sizeof(fingerprint));
            if (tofu == KNOWNHOSTS_NEW || tofu == KNOWNHOSTS_MISMATCH) {
                char dlg_msg[1024];
                const char *title;
                UINT icon;
                if (tofu == KNOWNHOSTS_NEW) {
                    snprintf(dlg_msg, sizeof(dlg_msg),
                        "The authenticity of host '%s:%d' can't be established.\n\n"
                        "Host key fingerprint:\n%s\n\n"
                        "Do you want to trust this host and continue connecting?",
                        info->host, info->port, fingerprint);
                    title = "Unknown Host";
                    icon  = MB_ICONWARNING;
                } else {
                    snprintf(dlg_msg, sizeof(dlg_msg),
                        "WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!\n\n"
                        "Host: %s:%d\nNew fingerprint:\n%s\n\n"
                        "This may indicate a MitM attack.\n"
                        "Connect anyway and update the stored key?",
                        info->host, info->port, fingerprint);
                    title = "Host Key Changed!";
                    icon  = MB_ICONSTOP;
                }
                int ans = MessageBoxA(hwnd, dlg_msg, title, (UINT)MB_YESNO | icon);
                if (ans == IDYES) {
                    knownhosts_add(&kh, info->host, info->port, key, key_len, key_type);
                } else {
                    snprintf(s->conn_error, sizeof(s->conn_error), "Connection aborted by user.");
                    knownhosts_free(&kh);
                    CONN_FAIL(1);
                }
            }
            knownhosts_free(&kh);
        }
    }

    if (s->conn_cancelled) { snprintf(s->conn_error, sizeof(s->conn_error), "Cancelled."); CONN_FAIL(1); }

    /* Authentication */
    int auth_rc = -1;
    if (info->auth_type == AUTH_KEY) {
        auth_rc = ssh_auth_key(s->ssh, info->username, info->key_path, info->password);
        if (auth_rc != 0) {
            char passphrase[256];
            memset(passphrase, 0, sizeof(passphrase));
            if (prompt_passphrase(hwnd, passphrase, (int)sizeof(passphrase))) {
                auth_rc = ssh_auth_key(s->ssh, info->username, info->key_path, passphrase);
                if (auth_rc == 0) {
                    strncpy(s->ssh->cached_passphrase, passphrase,
                            sizeof(s->ssh->cached_passphrase) - 1u);
                    s->ssh->cached_passphrase[sizeof(s->ssh->cached_passphrase) - 1u] = '\0';
                }
            }
            SecureZeroMemory(passphrase, sizeof(passphrase));
        }
    } else {
        auth_rc = ssh_auth_password(s->ssh, info->username, info->password);
    }

    if (auth_rc != 0) {
        snprintf(s->conn_error, sizeof(s->conn_error),
                 "Authentication failed for %s@%s.", info->username, info->host);
        CONN_FAIL(2);
    }

    if (s->conn_cancelled) { snprintf(s->conn_error, sizeof(s->conn_error), "Cancelled."); CONN_FAIL(2); }

    /* Open channel, request PTY, start shell */
    s->channel = ssh_channel_open(s->ssh);
    if (!s->channel) {
        snprintf(s->conn_error, sizeof(s->conn_error),
                 "Could not open SSH channel to %s.", info->host);
        CONN_FAIL(3);
    }
    ssh_pty_request(s->channel, "xterm", s->term->cols, s->term->rows);
    ssh_pty_shell(s->channel);
    ssh_session_set_blocking(s->ssh, false); /* non-blocking for I/O loop */

    /* H-3: zero plaintext password from memory once auth is complete */
    SecureZeroMemory(s->conn_profile.password, sizeof(s->conn_profile.password));

    EnterCriticalSection(&s->conn_cs);  /* H-1 */
    s->conn_result = 0;
    LeaveCriticalSection(&s->conn_cs);
    PostMessage(hwnd, WM_CONN_DONE, 0, (LPARAM)s);
    return 0;

#undef CONN_FAIL
}

static void on_session_connect(const Profile *info) {
    RECT rc;
    GetClientRect(GetParent(g_hwndTabs), &rc);
    int term_w = rc.right;
    int term_h = rc.bottom - TAB_HEIGHT;
    if (term_h < 1) term_h = 1;

    int cols = 80;
    int rows = 24;
    if (g_renderer.charWidth > 0 && g_renderer.charHeight > 0) {
        cols = term_w / g_renderer.charWidth;
        rows = term_h / g_renderer.charHeight;
    }

    /* Open the tab immediately so the user sees activity at once */
    Session *s = create_session(rows, cols);
    term_process(s->term, "Connecting", 10); /* dots appended by 500ms timer */

    char title[32];
    snprintf(title, sizeof(title), "%s",
             info->name[0] ? info->name : (info->host[0] ? info->host : "Session"));

    int idx = tabs_add(g_hwndTabs, title, s);
    if (idx < 0) {
        /* I-3: tab limit reached — clean up and notify user */
        MessageBoxA(GetParent(g_hwndTabs),
                    "Maximum number of tabs reached.",
                    "Tab Limit", MB_OK | MB_ICONINFORMATION);
        if (g_session_list == s) g_session_list = s->next;
        free_session(s);
        return;
    }
    tabs_set_active(g_hwndTabs, idx);
    tabs_set_status(g_hwndTabs, idx, TAB_CONNECTING);
    invalidate_terminal(GetParent(g_hwndTabs));

    /* Store state for the worker thread */
    s->conn_state    = CONN_CONNECTING;
    s->conn_profile  = *info;
    s->conn_result   = 0;
    s->conn_error[0] = '\0';
    s->conn_start_ms = GetTickCount64();
    s->conn_dots     = 0;
    s->conn_hwnd     = GetParent(g_hwndTabs);

    s->conn_thread = CreateThread(NULL, 0, connection_thread, s, 0, NULL);
    if (!s->conn_thread) {
        term_process(s->term, "\r\nFailed to start connection thread.\r\n", 38);
        tabs_set_status(g_hwndTabs, idx, TAB_DISCONNECTED);
        s->conn_state = CONN_IDLE;
    }
}

static void on_tab_new(void) {
    Profile p;
    memset(&p, 0, sizeof(Profile));

    if (SessionManager_Show(g_hInst, GetParent(g_hwndTabs), g_config, g_config_path, &p)) {
        on_session_connect(&p);
    }
}

static COLORREF parse_hex_color(const char *hex, COLORREF fallback)
{
    unsigned int r = 0, g = 0, b = 0;
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return fallback;
    if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) != 3) return fallback;
    return RGB((BYTE)r, (BYTE)g, (BYTE)b);
}

static void apply_config_colors(void)
{
    if (g_theme) {
        /* Use theme terminal colours as the base, overridden by explicit config */
        unsigned int tfg = g_theme->terminal_fg;
        unsigned int tbg = g_theme->terminal_bg;
        g_renderer.defaultFg = RGB((tfg >> 16) & 0xFF, (tfg >> 8) & 0xFF, tfg & 0xFF);
        g_renderer.defaultBg = RGB((tbg >> 16) & 0xFF, (tbg >> 8) & 0xFF, tbg & 0xFF);
    } else {
        g_renderer.defaultFg = parse_hex_color(
            g_config->settings.foreground_colour, RGB(0, 0, 0));
        g_renderer.defaultBg = parse_hex_color(
            g_config->settings.background_colour, RGB(255, 255, 255));
    }
}

static void on_settings_clicked(void) {
    HWND parent = GetParent(g_hwndTabs);
    settings_dlg_show(parent, g_config);

    /* Reload theme from config (colour scheme may have changed) */
    {
        int idx = ui_theme_find(g_config->settings.colour_scheme);
        g_theme = ui_theme_get(idx);
        tabs_set_theme(g_hwndTabs, g_theme);
        if (g_hwndScrollbar) csb_set_theme(g_hwndScrollbar, g_theme);
    }

    /* Reinitialise renderer — font or size may have changed */
    renderer_free(&g_renderer);
    renderer_init(&g_renderer, g_config->settings.font,
                  g_config->settings.font_size);
    apply_config_colors();
    renderer_apply_theme(parent, g_renderer.defaultBg);

    /* Update tab strip font (may have changed) */
    tabs_set_font(g_hwndTabs, g_config->settings.font);

    /* Update AI button state (green/grey based on API key) */
    tabs_set_ai_active(g_hwndTabs,
                       g_config->settings.ai_api_key[0] != '\0');

    /* Update AI chat window with new key/provider if open */
    if (g_hwndAiChat && IsWindow(g_hwndAiChat)) {
        ai_chat_update_key(g_hwndAiChat,
                           g_config->settings.ai_api_key,
                           g_config->settings.ai_provider,
                           g_config->settings.ai_custom_url,
                           g_config->settings.ai_custom_model);
    }

    /* Resize all terminals to the new character grid */
    RECT rc;
    GetClientRect(parent, &rc);
    SendMessage(parent, WM_SIZE, SIZE_RESTORED,
                MAKELPARAM(rc.right, rc.bottom));
    InvalidateRect(parent, NULL, TRUE);
    /* Force tab strip to repaint with new theme */
    InvalidateRect(g_hwndTabs, NULL, TRUE);
}

static void on_ai_clicked(void) {
    if (g_hwndAiChat && IsWindow(g_hwndAiChat)) {
        /* Already open — toggle: hide if visible, show if hidden */
        if (IsWindowVisible(g_hwndAiChat)) {
            ShowWindow(g_hwndAiChat, SW_HIDE);
        } else {
            ShowWindow(g_hwndAiChat, SW_SHOW);
            SetForegroundWindow(g_hwndAiChat);
        }
        return;
    }

    if (!g_config || g_config->settings.ai_api_key[0] == '\0') {
        MessageBoxA(GetParent(g_hwndTabs),
            "No AI API key configured.\nPlease set one in Settings.",
            "AI Chat", MB_OK | MB_ICONINFORMATION);
        return;
    }

    HWND parent = GetParent(g_hwndTabs);
    g_hwndAiChat = ai_chat_show(parent,
                                g_config->settings.ai_api_key,
                                g_config->settings.ai_provider,
                                g_config->settings.ai_custom_url,
                                g_config->settings.ai_custom_model,
                                g_config->settings.paste_delay_ms,
                                g_config->settings.font,
                                g_config->settings.colour_scheme);

    /* Set the active session if one exists */
    if (g_hwndAiChat && g_active_session) {
        ai_chat_set_session(g_hwndAiChat,
                           g_active_session->term,
                           g_active_session->channel);
    }
}

static void on_log_toggle(int index, void *user_data) {
    Session *s = (Session *)user_data;
    if (!s) return;
    int tidx = tabs_find(g_hwndTabs, s);
    if (tidx < 0) return;

    if (s->session_log) {
        /* Logging is on — turn it off */
        fclose(s->session_log);
        s->session_log = NULL;
        tabs_set_logging(g_hwndTabs, tidx, 0);
    } else {
        /* Logging is off — turn it on */
        char dir_buf[MAX_PATH];
        const char *dir;
        if (g_config && g_config->settings.log_dir[0]) {
            dir = g_config->settings.log_dir;
        } else {
            get_exe_dir(dir_buf, sizeof(dir_buf));
            dir = dir_buf[0] ? dir_buf : ".";
        }
        char path[512];
        log_format_filename(s->conn_profile.name, dir, path, sizeof(path));
        s->session_log = fopen(path, "ab");
        if (s->session_log) {
            tabs_set_logging(g_hwndTabs, tidx, 1);
        }
    }
    (void)index;
}

/* ---- Paste helper -------------------------------------------------------- */

/* Send clipboard text to the active session, with multi-line confirmation
 * when content is longer than PASTE_CONFIRM_THRESHOLD bytes or contains a
 * newline.  Lines are separated by paste_delay_ms milliseconds via a
 * non-blocking WM_TIMER so the UI stays responsive. */
#define PASTE_CONFIRM_THRESHOLD 64

/* Send one line from g_paste.pos, advance pos past the '\n'.
 * Returns true if there is more data to send. */
static bool paste_send_next_line(void)
{
    if (!g_paste.buf || !g_paste.pos || !*g_paste.pos) return false;
    if (!g_paste.channel) return false;

    const char *p = g_paste.pos;
    const char *nl = strchr(p, '\n');
    size_t chunk = nl ? (size_t)(nl - p) + 1u : strlen(p);

    for (size_t i = 0; i < chunk; i++) {
        if (p[i] != '\r')
            ssh_channel_write(g_paste.channel, &p[i], 1);
    }

    g_paste.pos += chunk;
    return *g_paste.pos != '\0';
}

/* Cancel any in-progress paste and free state */
static void paste_cancel(void)
{
    if (g_paste.buf) {
        KillTimer(g_paste.hwnd, PASTE_TIMER_ID);
        free(g_paste.buf);
        g_paste.buf = NULL;
        g_paste.pos = NULL;
        g_paste.channel = NULL;
    }
}

/* Called by WM_TIMER when wParam == PASTE_TIMER_ID */
static void paste_timer_tick(void)
{
    bool more = paste_send_next_line();
    if (g_active_session && g_active_session->term) {
        g_active_session->term->scrollback_offset = 0;
        invalidate_terminal(g_paste.hwnd);
    }
    if (!more) paste_cancel();
}

static void do_paste(HWND hwnd)
{
    if (!g_active_session || !g_active_session->channel) return;

    /* Cancel any in-progress paste */
    paste_cancel();

    if (!IsClipboardFormatAvailable(CF_TEXT)) return;
    if (!OpenClipboard(hwnd)) return;

    HANDLE hClip = GetClipboardData(CF_TEXT);
    if (!hClip) { CloseClipboard(); return; }

    const char *raw = (const char *)GlobalLock(hClip);
    if (!raw) { CloseClipboard(); return; }

    size_t raw_len = strlen(raw);

    /* Count newlines */
    int line_count = 0;
    for (size_t i = 0; i < raw_len; i++) {
        if (raw[i] == '\n') line_count++;
    }

    /* Always ask for confirmation before pasting */
    int confirmed = paste_preview_show(hwnd, raw,
        g_config->settings.foreground_colour,
        g_config->settings.background_colour,
        g_config->settings.font,
        g_config->settings.font_size);

    if (confirmed) {
        int delay_ms = g_config ? g_config->settings.paste_delay_ms : 0;

        /* Single line or no delay: send everything immediately */
        if (line_count == 0 || delay_ms <= 0) {
            const char *p = raw;
            while (*p) {
                const char *nl = strchr(p, '\n');
                size_t chunk = nl ? (size_t)(nl - p) + 1u : strlen(p);
                for (size_t i = 0; i < chunk; i++) {
                    if (p[i] != '\r')
                        ssh_channel_write(g_active_session->channel, &p[i], 1);
                }
                p += chunk;
            }
            g_active_session->term->scrollback_offset = 0;
            invalidate_terminal(hwnd);
        } else {
            /* Multi-line with delay: use timer-driven paste */
            g_paste.buf = _strdup(raw);
            g_paste.pos = g_paste.buf;
            g_paste.hwnd = hwnd;
            g_paste.delay_ms = delay_ms;
            g_paste.channel = g_active_session->channel;

            /* Send the first line immediately */
            paste_send_next_line();
            g_active_session->term->scrollback_offset = 0;
            invalidate_terminal(hwnd);

            /* Start timer for remaining lines */
            if (g_paste.pos && *g_paste.pos) {
                SetTimer(hwnd, PASTE_TIMER_ID, (UINT)delay_ms, NULL);
            } else {
                paste_cancel();
            }
        }
    }

    GlobalUnlock(hClip);
    CloseClipboard();
}

/* ---- Scrollbar helper ---------------------------------------------------- */

/* Sync the window's vertical scrollbar to the active terminal's state.
 * Scrollbar: top = oldest lines, bottom = newest (live view).
 * nPos = lines_count - rows - scrollback_offset  (first visible line index).
 *
 * Win64 note: SCROLLINFO fields are 32-bit (int/UINT) regardless of target
 * arch.  nTrackPos is an *output* field — SetScrollInfo ignores it. */
static void update_scrollbar(HWND hwnd)
{
    if (!g_hwndScrollbar) return;

    if (!g_active_session || !g_active_session->term) {
        csb_set_range(g_hwndScrollbar, 0, 0, 1);
        csb_set_pos(g_hwndScrollbar, 0);
        return;
    }

    Terminal *t = g_active_session->term;
    int total = t->lines_count;
    int rows  = (t->rows > 0) ? t->rows : 1;
    int nPos  = (total > rows) ? (total - rows - t->scrollback_offset) : 0;
    if (nPos < 0) nPos = 0;

    int nMax = (total > 1) ? (total - 1) : 0;
    csb_set_range(g_hwndScrollbar, 0, nMax, rows);
    csb_set_pos(g_hwndScrollbar, nPos);
    (void)hwnd;
}

/* ---- Cell-snapping helpers ----------------------------------------------- */
#include "snap.h"
#include "zoom.h"
#include "connect_anim.h"

/* Return total non-client pixel size (border + title bar) for hwnd. */
static void get_nc_size(HWND hwnd, int *nc_w, int *nc_h)
{
    RECT nc = {0, 0, 0, 0};
    AdjustWindowRectEx(&nc,
                       (DWORD)GetWindowLong(hwnd, GWL_STYLE),
                       FALSE,
                       (DWORD)GetWindowLong(hwnd, GWL_EXSTYLE));
    *nc_w = nc.right  - nc.left;
    *nc_h = nc.bottom - nc.top;
}

/* ---- Zoom helper --------------------------------------------------------- */

/* Step to the next/previous discrete font size.
 * delta is +1 (zoom in / larger) or -1 (zoom out / smaller). */
static void apply_zoom(HWND hwnd, int delta)
{
    int cur = g_config->settings.font_size;
    int new_size = app_font_zoom(cur, delta);
    if (new_size == cur)
        return;

    g_config->settings.font_size = new_size;
    renderer_free(&g_renderer);
    renderer_init(&g_renderer, g_config->settings.font, new_size);
    apply_config_colors();
    renderer_apply_theme(hwnd, g_renderer.defaultBg);

    /* Send synthetic WM_SIZE so terminals resize to the new character grid. */
    RECT rc;
    GetClientRect(hwnd, &rc);
    SendMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                MAKELPARAM(rc.right, rc.bottom));
    invalidate_terminal(hwnd);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            /* M-8: resolve config.json to an absolute path at startup so
             * GetOpenFileNameA (file browse dialogs) cannot change CWD and
             * cause saves to go to the wrong directory. */
            {
                char exe_dir[MAX_PATH];
                get_exe_dir(exe_dir, sizeof(exe_dir));
                if (exe_dir[0] != '\0')
                    (void)snprintf(g_config_path, sizeof(g_config_path),
                                   "%s\\config.json", exe_dir);
                else
                    (void)snprintf(g_config_path, sizeof(g_config_path),
                                   "config.json");

                g_config = config_load(g_config_path);
                if (!g_config) {
                    MessageBoxA(hwnd,
                        "Could not load config.json.\n\nStarting with default settings.",
                        "Configuration Warning", MB_OK | MB_ICONWARNING);
                    g_config = config_new_default();
                }

                /* Default log_dir to the exe directory when not set */
                if (g_config->settings.log_dir[0] == '\0' && exe_dir[0] != '\0')
                    (void)snprintf(g_config->settings.log_dir,
                                   sizeof(g_config->settings.log_dir),
                                   "%s", exe_dir);
            }

            g_hInst = ((LPCREATESTRUCT)lParam)->hInstance;

            /* Look up colour theme from config */
            {
                int idx = ui_theme_find(g_config->settings.colour_scheme);
                g_theme = ui_theme_get(idx);
            }

            tabs_init(g_hInst);
            csb_register(g_hInst);

            RECT rc;
            GetClientRect(hwnd, &rc);

            g_hwndTabs = tabs_create(hwnd, 0, 0, rc.right, TAB_HEIGHT);

            /* Custom themed scrollbar on the right edge */
            g_hwndScrollbar = csb_create(hwnd,
                rc.right - CSB_WIDTH, TAB_HEIGHT,
                CSB_WIDTH, rc.bottom - TAB_HEIGHT,
                g_theme, g_hInst);
            tabs_set_callbacks(g_hwndTabs, on_tab_select, on_tab_new, on_tab_close, on_settings_clicked, on_log_toggle);
            tabs_set_ai_callback(g_hwndTabs, on_ai_clicked);
            tabs_set_ai_active(g_hwndTabs,
                               g_config->settings.ai_api_key[0] != '\0');
            tabs_set_theme(g_hwndTabs, g_theme);
            ai_chat_init(g_hInst);

            renderer_init(&g_renderer,
                          g_config->settings.font[0]
                              ? g_config->settings.font : "Cascadia Code",
                          g_config->settings.font_size > 0
                              ? g_config->settings.font_size : 12);
            apply_config_colors();
            renderer_apply_theme(hwnd, g_renderer.defaultBg);

            /* Start I/O timer (10ms) and animation timer (500ms) */
            SetTimer(hwnd, 1, 10,  NULL);
            SetTimer(hwnd, 2, 500, NULL);
            
            PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0);
            return 0;

        case WM_TIMER:
            if (wParam == 1) {
                /* Poll connected sessions only (skip those still connecting) */
                Session *s = g_session_list;
                while (s) {
                    if (s->channel && s->conn_state == CONN_IDLE) {
                        int poll_rc = ssh_io_poll(s->channel, s->term,
                                                      s->session_log);
                        if (poll_rc > 0)
                            update_scrollbar(hwnd);
                        if (poll_rc == -2) {
                            /* EOF */
                            if (g_paste.channel == s->channel)
                                paste_cancel();
                            term_process(s->term, "\r\n[Connection Closed]\r\n", 23);
                            ssh_channel_free(s->channel);
                            s->channel = NULL;
                            int tidx = tabs_find(g_hwndTabs, s);
                            if (tidx >= 0)
                                tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
                        }
                        if (term_has_dirty_rows(s->term)) {
                            DWORD now = GetTickCount();
                            if (now - g_last_paint_tick >= PAINT_COOLDOWN_MS) {
                                invalidate_terminal(hwnd);
                                g_last_paint_tick = now;
                            }
                        }
                    }
                    s = s->next;
                }
            } else if (wParam == 2) {
                /* 500ms animation: append dots to each connecting session */
                BOOL needs_repaint = FALSE;
                Session *s = g_session_list;
                while (s) {
                    if (s->conn_state == CONN_CONNECTING) {
                        ULONGLONG now_ms = GetTickCount64();
                        unsigned long elapsed =
                            (unsigned long)(now_ms - s->conn_start_ms);
                        int expected = connect_anim_dots(elapsed, 500, 20);
                        for (int i = s->conn_dots; i < expected; i++) {
                            term_process(s->term, ".", 1);
                        }
                        if (expected > s->conn_dots) {
                            s->conn_dots = expected;
                            needs_repaint = TRUE;
                        }
                    }
                    s = s->next;
                }
                if (needs_repaint) invalidate_terminal(hwnd);
            } else if (wParam == PASTE_TIMER_ID) {
                paste_timer_tick();
            }
            return 0;

        case WM_SHOW_SESSION_MANAGER:
            on_tab_new();
            return 0;

        case WM_CONN_DONE: {
            Session *s = (Session *)lParam;
            s->conn_state = CONN_IDLE;
            CloseHandle(s->conn_thread);
            s->conn_thread = NULL;

            /* H-1: read shared fields under the lock */
            EnterCriticalSection(&s->conn_cs);
            int conn_result  = s->conn_result;
            char conn_error[512];
            memcpy(conn_error, s->conn_error, sizeof(conn_error));
            LeaveCriticalSection(&s->conn_cs);

            int tidx = tabs_find(g_hwndTabs, s);
            if (conn_result != 0) {
                char errmsg[600];
                snprintf(errmsg, sizeof(errmsg), "\r\n%s\r\n", conn_error);
                term_process(s->term, errmsg, strlen(errmsg));
                MessageBoxA(hwnd, conn_error, "Connection Error",
                            MB_OK | MB_ICONERROR);
                if (tidx >= 0)
                    tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
            } else {
                term_process(s->term, "\r\nConnected.\r\n", 14);
                s->session_log = open_session_log(s->conn_profile.host);
                if (tidx >= 0) {
                    tabs_set_connect_info(g_hwndTabs, tidx,
                                         s->conn_profile.username,
                                         s->conn_profile.host,
                                         (unsigned long long)GetTickCount64());
                    tabs_set_status(g_hwndTabs, tidx, TAB_CONNECTED);
                    /* Sync L indicator with actual logging state */
                    tabs_set_logging(g_hwndTabs, tidx,
                                     s->session_log ? 1 : 0);
                }
            }
            update_scrollbar(hwnd);
            invalidate_terminal(hwnd);
            return 0;
        }

        case WM_SIZING: {
            /* Snap the proposed window rect to whole character cells. */
            if (g_renderer.charWidth <= 0 || g_renderer.charHeight <= 0)
                break;

            RECT *wr = (RECT *)lParam;
            int nc_w, nc_h;
            get_nc_size(hwnd, &nc_w, &nc_h);
            /* Account for the custom scrollbar width */
            nc_w += CSB_WIDTH;

            int client_w = (wr->right  - wr->left) - nc_w;
            int client_h = (wr->bottom - wr->top)  - nc_h;

            int snapped_w, snapped_h;
            snap_calc(client_w, client_h,
                      g_renderer.charWidth, g_renderer.charHeight,
                      nc_w, nc_h, TAB_HEIGHT,
                      NULL, NULL, &snapped_w, &snapped_h);

            int l = (int)wr->left, t = (int)wr->top,
                r = (int)wr->right, b = (int)wr->bottom;
            snap_adjust(&l, &t, &r, &b, snapped_w, snapped_h, (int)wParam);
            wr->left = l; wr->top = t; wr->right = r; wr->bottom = b;
            return TRUE;
        }

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);

            if (g_hwndTabs) {
                SetWindowPos(g_hwndTabs, NULL, 0, 0, width, TAB_HEIGHT, SWP_NOZORDER);
            }

            /* Reposition custom scrollbar */
            if (g_hwndScrollbar) {
                SetWindowPos(g_hwndScrollbar, NULL,
                    width - CSB_WIDTH, TAB_HEIGHT,
                    CSB_WIDTH, height - TAB_HEIGHT,
                    SWP_NOZORDER);
            }

            if (g_active_session && g_active_session->term && g_renderer.charWidth > 0 && g_renderer.charHeight > 0) {
                int term_h = height - TAB_HEIGHT;
                if (term_h < 1) term_h = 1;

                int term_w = width - CSB_WIDTH;
                if (term_w < 1) term_w = 1;
                int cols = term_w / g_renderer.charWidth;
                int rows = term_h / g_renderer.charHeight;
                if (cols < 1) cols = 1;
                if (rows < 1) rows = 1;

                if (cols != g_active_session->term->cols || rows != g_active_session->term->rows) {
                    term_resize(g_active_session->term, rows, cols);
                    if (g_active_session->channel)
                        ssh_pty_resize(g_active_session->channel, cols, rows);
                    invalidate_terminal(hwnd);
                }
            }
            update_scrollbar(hwnd);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            if (g_active_session && g_active_session->term) {
                renderer_draw(&g_renderer, hdc, g_active_session->term, 0, TAB_HEIGHT, &ps.rcPaint, &g_selection);

                /* Fill gutter areas not covered by complete character cells. */
                RECT client;
                GetClientRect(hwnd, &client);
                HBRUSH bg = CreateSolidBrush(g_renderer.defaultBg);

                int text_bottom = TAB_HEIGHT +
                    g_active_session->term->rows * g_renderer.charHeight;
                if (text_bottom < client.bottom) {
                    RECT r = { 0, text_bottom, client.right, client.bottom };
                    FillRect(hdc, &r, bg);
                }

                int text_right = g_active_session->term->cols * g_renderer.charWidth;
                if (text_right < client.right) {
                    RECT r = { text_right, TAB_HEIGHT, client.right, text_bottom };
                    FillRect(hdc, &r, bg);
                }

                DeleteObject(bg);
            } else {
                HBRUSH brush = CreateSolidBrush(g_renderer.defaultBg);
                FillRect(hdc, &ps.rcPaint, brush);
                DeleteObject(brush);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CHAR: {
            if (g_active_session && g_active_session->term) {
                char c = (char)wParam;
                if (c == 0x17) { /* Ctrl+W */
                     int idx = tabs_get_active(g_hwndTabs);
                     if (idx >= 0) on_tab_close(idx, g_active_session);
                     return 0;
                }
                if (c == 0x16) { /* Ctrl+V — handled via do_paste in WM_KEYDOWN */
                    return 0;
                }
                if (g_active_session->channel) {
                    /* Drain pending transport data so the write can
                     * succeed in non-blocking mode.  Without this,
                     * libssh2_channel_write returns EAGAIN when the
                     * session has unread inbound data, and the retry
                     * loop in ssh_channel_write blocks the UI thread
                     * (preventing WM_TIMER / ssh_io_poll from running)
                     * — a deadlock that silently drops keystrokes. */
                    ssh_io_poll(g_active_session->channel,
                                g_active_session->term,
                                g_active_session->session_log);
                    ssh_channel_write(g_active_session->channel, &c, 1);
                }
                /* Only invalidate if we were scrolled back */
                if (g_active_session->term->scrollback_offset != 0) {
                    g_active_session->term->scrollback_offset = 0;
                    update_scrollbar(hwnd);
                    invalidate_terminal(hwnd);
                }
            }
            return 0;
        }

        case WM_KEYDOWN: {
            /* Ctrl+T — new tab */
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == (WPARAM)'T') {
                on_tab_new();
                return 0;
            }
            /* Ctrl+V — paste with confirmation */
            if ((GetKeyState(VK_CONTROL) & 0x8000) &&
                (wParam == (WPARAM)'V' || wParam == (WPARAM)0x56)) {
                do_paste(hwnd);
                return 0;
            }
            /* Shift+Insert — alternative paste shortcut */
            if ((GetKeyState(VK_SHIFT) & 0x8000) && wParam == VK_INSERT) {
                do_paste(hwnd);
                return 0;
            }
            /* Ctrl+= / Ctrl+- zoom keyboard shortcuts */
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (wParam == VK_OEM_PLUS || wParam == (WPARAM)'=') {
                    apply_zoom(hwnd, 1);
                    return 0;
                }
                if (wParam == VK_OEM_MINUS || wParam == (WPARAM)'-') {
                    apply_zoom(hwnd, -1);
                    return 0;
                }
            }
            if (g_active_session && g_active_session->term) {
                const char *seq = NULL;
                bool app_keys = g_active_session->term->app_cursor_keys;
                switch (wParam) {
                    case VK_UP:    seq = app_keys ? "\x1BOA" : "\x1B[A"; break;
                    case VK_DOWN:  seq = app_keys ? "\x1BOB" : "\x1B[B"; break;
                    case VK_RIGHT: seq = app_keys ? "\x1BOC" : "\x1B[C"; break;
                    case VK_LEFT:  seq = app_keys ? "\x1BOD" : "\x1B[D"; break;
                    case VK_HOME:  seq = "\x1B[H"; break;
                    case VK_END:   seq = "\x1B[4~"; break; /* VT sequence for End */
                    case VK_DELETE: seq = "\x1B[3~"; break;
                    case VK_INSERT: seq = "\x1B[2~"; break;
                    case VK_PRIOR: /* Page Up */
                        if (g_active_session->term->scrollback_offset < g_active_session->term->max_scrollback) {
                            g_active_session->term->scrollback_offset++;
                            update_scrollbar(hwnd);
                            invalidate_terminal(hwnd);
                        }
                        return 0;
                    case VK_NEXT:  /* Page Down */
                        if (g_active_session->term->scrollback_offset > 0) {
                            g_active_session->term->scrollback_offset--;
                            update_scrollbar(hwnd);
                            invalidate_terminal(hwnd);
                        }
                        return 0;
                }
                if (seq) {
                    if (g_active_session->channel) {
                        ssh_io_poll(g_active_session->channel,
                                    g_active_session->term,
                                    g_active_session->session_log);
                        ssh_channel_write(g_active_session->channel, seq, strlen(seq));
                    }
                    if (g_active_session->term->scrollback_offset != 0) {
                        g_active_session->term->scrollback_offset = 0;
                        update_scrollbar(hwnd);
                        invalidate_terminal(hwnd);
                    }
                    return 0;
                }
            }
            break; /* Let DefWindowProc generate WM_CHAR for unhandled keys */
        }

        case WM_MOUSEWHEEL: {
            /* Ctrl+Scroll zooms the font */
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                apply_zoom(hwnd, delta > 0 ? 1 : -1);
                return 0;
            }
            /* Plain scroll: scroll 3 lines per notch */
            if (g_active_session && g_active_session->term) {
                Terminal *t = g_active_session->term;
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                int lines = 3 * delta / WHEEL_DELTA;
                int max_off = (t->lines_count > t->rows)
                                  ? (t->lines_count - t->rows) : 0;
                if (max_off > t->max_scrollback) max_off = t->max_scrollback;
                int off = t->scrollback_offset + lines;
                if (off < 0) off = 0;
                if (off > max_off) off = max_off;
                t->scrollback_offset = off;
                update_scrollbar(hwnd);
                invalidate_terminal(hwnd);
            }
            return 0;
        }

        case WM_RBUTTONDOWN: {
            /* Right-click pastes the clipboard */
            do_paste(hwnd);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (!g_active_session || !g_active_session->term) break;
            SetCapture(hwnd);
            int mx = LOWORD(lParam), my = HIWORD(lParam);
            selection_pixel_to_cell(mx, my,
                g_renderer.charWidth, g_renderer.charHeight,
                TAB_HEIGHT, g_active_session->term->rows,
                g_active_session->term->cols,
                &g_selection.start_row, &g_selection.start_col);
            g_selection.end_row = g_selection.start_row;
            g_selection.end_col = g_selection.start_col;
            g_selection.active = true;
            g_selection.valid = false;
            invalidate_terminal(hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!g_selection.active) break;
            if (!(wParam & MK_LBUTTON)) {
                g_selection.active = false;
                break;
            }
            if (!g_active_session || !g_active_session->term) break;
            int mx = LOWORD(lParam), my = HIWORD(lParam);
            selection_pixel_to_cell(mx, my,
                g_renderer.charWidth, g_renderer.charHeight,
                TAB_HEIGHT, g_active_session->term->rows,
                g_active_session->term->cols,
                &g_selection.end_row, &g_selection.end_col);
            g_selection.valid = (g_selection.start_row != g_selection.end_row ||
                                 g_selection.start_col != g_selection.end_col);
            invalidate_terminal(hwnd);
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!g_selection.active) break;
            ReleaseCapture();
            g_selection.active = false;
            if (!g_active_session || !g_active_session->term) break;
            /* Update end position from final mouse coordinates —
             * WM_MOUSEMOVE may not fire at the exact release point. */
            int mx = LOWORD(lParam), my = HIWORD(lParam);
            selection_pixel_to_cell(mx, my,
                g_renderer.charWidth, g_renderer.charHeight,
                TAB_HEIGHT, g_active_session->term->rows,
                g_active_session->term->cols,
                &g_selection.end_row, &g_selection.end_col);
            g_selection.valid = true;
            /* Extract selected text and copy to clipboard */
            char buf[8192];
            size_t n = selection_extract_text(&g_selection,
                g_active_session->term, buf, sizeof(buf));
            if (n > 0 && OpenClipboard(hwnd)) {
                EmptyClipboard();
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, n + 1);
                if (hg) {
                    char *dst = (char *)GlobalLock(hg);
                    memcpy(dst, buf, n + 1);
                    GlobalUnlock(hg);
                    SetClipboardData(CF_TEXT, hg);
                }
                CloseClipboard();
            }
            g_selection.valid = false;
            /* Force full repaint so highlighted cells are redrawn without highlight.
             * The dirty-row optimisation in the renderer would skip clean rows,
             * leaving stale highlighted pixels on screen. */
            for (int i = 0; i < g_active_session->term->lines_count; i++) {
                int idx = (g_active_session->term->lines_start + i) % g_active_session->term->lines_capacity;
                if (g_active_session->term->lines[idx])
                    g_active_session->term->lines[idx]->dirty = true;
            }
            invalidate_terminal(hwnd);
            return 0;
        }

        case WM_VSCROLL: {
            if (!g_active_session || !g_active_session->term) return 0;
            Terminal *t = g_active_session->term;
            int max_off = (t->lines_count > t->rows) ? (t->lines_count - t->rows) : 0;
            if (max_off > t->max_scrollback) max_off = t->max_scrollback;
            int off = t->scrollback_offset;
            switch (LOWORD(wParam)) {
            case SB_LINEUP:       off++; break;
            case SB_LINEDOWN:     off--; break;
            case SB_PAGEUP:       off += t->rows; break;
            case SB_PAGEDOWN:     off -= t->rows; break;
            case SB_TOP:          off = max_off; break;
            case SB_BOTTOM:       off = 0; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                /* Custom scrollbar stores the full 32-bit position internally,
                 * so no HIWORD(wParam) truncation issue (unlike WS_VSCROLL). */
                int trackpos = csb_get_trackpos(g_hwndScrollbar);
                /* nPos = lines_count - rows - off  =>  off = lines_count - rows - nPos */
                off = t->lines_count - t->rows - trackpos;
                break;
            }
            default: return 0;
            }
            if (off < 0) off = 0;
            if (off > max_off) off = max_off;
            t->scrollback_offset = off;
            update_scrollbar(hwnd);
            invalidate_terminal(hwnd);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            {
                Session *s = g_session_list;
                while (s) {
                    Session *next = s->next;
                    free_session(s);
                    s = next;
                }
            }
            if (g_hwndAiChat && IsWindow(g_hwndAiChat))
                ai_chat_close(g_hwndAiChat);
            g_hwndAiChat = NULL;
            if (g_config) config_free(g_config);
            renderer_free(&g_renderer);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ui_init(HINSTANCE instance) {
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); /* Use system default icon */
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); /* Will be overwritten by WM_PAINT */
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassEx(&wc)) {
        log_write(LOG_LEVEL_ERROR, "Window Registration Failed!");
        MessageBox(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return;
    }

    /* Initial size: 1024 x 680 px, centred on screen */
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 1024;
    int winH = 680;
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        APP_TITLE,
        WS_OVERLAPPEDWINDOW,
        x, y, winW, winH,
        NULL, NULL, instance, NULL
    );

    if (hwnd == NULL) {
        log_write(LOG_LEVEL_ERROR, "Window Creation Failed!");
        MessageBox(NULL, "Window Creation Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

void ui_run(void) {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

#endif /* _WIN32 */