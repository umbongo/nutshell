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
#include "help_guide.h"
#include "ssh_session.h"
#include "ssh_pty.h"
#include "ssh_io.h"
#include "knownhosts.h"
#include "log_format.h"
#include "paste_dlg.h"
#include "ai_chat.h"
#include "ai_chat_testable.h"
#include "selection.h"
#include "app_font.h"
#include "ui_theme.h"
#include "custom_scrollbar.h"
#include "menubar_line.h"
#include <windowsx.h>  /* GET_X_LPARAM, GET_Y_LPARAM */
#include <dwmapi.h>

static void hide_ai_panel(HWND parent);

static const char *CLASS_NAME = "Nutshell_Window";
static const char *APP_TITLE = "Nutshell v" APP_VERSION;

/* Per-monitor DPI helper.  Dynamically loads GetDpiForWindow (Win10 1607+)
 * and falls back to the system-wide LOGPIXELSY value on older builds. */
typedef UINT (WINAPI *GetDpiForWindow_fn)(HWND);
static int get_window_dpi(HWND hwnd)
{
    static GetDpiForWindow_fn pfn = NULL;
    static int resolved = 0;
    if (!resolved) {
        HMODULE h = GetModuleHandleA("user32.dll");
        if (h) pfn = (GetDpiForWindow_fn)(void (*)(void))GetProcAddress(h, "GetDpiForWindow");
        resolved = 1;
    }
    if (pfn && hwnd) return (int)pfn(hwnd);
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    return dpi;
}

#define TAB_HEIGHT_BASE 32
#define TERM_LEFT_MARGIN 6
static int g_dpi = 96;
static int g_tab_height = TAB_HEIGHT_BASE;
static int g_left_margin = TERM_LEFT_MARGIN;

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
    AiSessionState ai_state;       /* per-session AI conversation */
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
static HFONT g_hMenuFont = NULL;
static HWND g_hwndScrollbar = NULL;
static const ThemeColors *g_theme = NULL;
static void update_scrollbar(HWND hwnd); /* forward declaration */
static void paste_cancel(void);          /* forward declaration */
static HMENU create_app_menu(void);      /* forward declaration */

/* ---- Docked AI panel state ---- */
#include "ai_dock.h"
static int g_ai_docked = 1;           /* 1=docked (default), 0=floating */
static int g_ai_panel_width = 0;      /* current docked panel width in px, 0=closed */
static int g_ai_target_width = 0;     /* animation target width */
static int g_ai_last_width = 0;       /* remembered width for re-open (resets on app start) */
static int g_ai_anim_from = 0;        /* animation start width (for close: current, open: 0) */
static int g_ai_splitter_dragging = 0;
static ULONGLONG g_ai_anim_start = 0;
static int g_ai_reopen_after_connect = 0; /* reopen AI panel after first session connects */
#define AI_ANIM_TIMER_ID  4
#define AI_ANIM_INTERVAL  16           /* ~60fps */

/* Invalidate only the terminal area (below the tab strip), not the tabs. */
static void invalidate_terminal(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.top = g_tab_height;
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
    memset(&s->ai_state, 0, sizeof(s->ai_state));
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
        free(s->ai_state.pending_cmds);
        free(s->ai_state.stream_content);
        free(s->ai_state.stream_thinking);
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

    /* Switch AI chat to the new session's conversation */
    if (g_hwndAiChat && IsWindow(g_hwndAiChat) && g_active_session) {
        ai_chat_switch_session(g_hwndAiChat,
                               &g_active_session->ai_state,
                               g_active_session->term,
                               g_active_session->channel,
                               g_active_session->conn_profile.ai_notes,
                               g_config->settings.ai_system_notes,
                               g_active_session->conn_profile.name);
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

    /* Notify AI chat before freeing so it can clear dangling pointers */
    if (g_hwndAiChat && IsWindow(g_hwndAiChat))
        ai_chat_notify_session_closed(g_hwndAiChat, &s->ai_state);

    free_session(s);
    tabs_remove(g_hwndTabs, index);

    /* Auto-hide AI panel when no active session remains */
    if (!g_active_session)
        hide_ai_panel(GetParent(g_hwndTabs));

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
    int ai_w = 0;
    if (g_ai_docked && g_hwndAiChat && IsWindowVisible(g_hwndAiChat))
        ai_w = g_ai_panel_width;
    int term_w = ai_dock_terminal_width(rc.right, ai_w, CSB_WIDTH, g_left_margin);
    int term_h = rc.bottom - g_tab_height;
    if (term_h < 1) term_h = 1;

    int cols = 80;
    int rows = 24;
    if (g_renderer.charWidth > 0 && g_renderer.charHeight > 0) {
        cols = term_w / g_renderer.charWidth;
        rows = term_h / g_renderer.charHeight;
    }

    /* First session: close AI panel during connection, reopen after */
    if (g_session_list == NULL && g_hwndAiChat && IsWindow(g_hwndAiChat)
        && IsWindowVisible(g_hwndAiChat)) {
        hide_ai_panel(GetParent(g_hwndTabs));
        g_ai_reopen_after_connect = 1;
    }

    /* Open the tab immediately so the user sees activity at once */
    Session *s = create_session(rows, cols);
    s->conn_profile  = *info; /* copy profile early — tab callbacks read it */
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
        /* Rebuild menu bar with new theme colours */
        HMENU oldMenu = GetMenu(parent);
        SetMenu(parent, create_app_menu());
        if (oldMenu) DestroyMenu(oldMenu);
        DrawMenuBar(parent);
    }

    /* Reinitialise renderer — font or size may have changed */
    renderer_free(&g_renderer);
    renderer_init(&g_renderer, g_config->settings.font,
                  g_config->settings.font_size,
                  get_window_dpi(parent));
    apply_config_colors();
    renderer_apply_theme(parent, g_renderer.defaultBg);

    /* Update tab strip font (may have changed) */
    tabs_set_font(g_hwndTabs, g_config->settings.font);

    /* Update AI button state (green/grey based on API key) */
    tabs_set_ai_active(g_hwndTabs,
                       g_config->settings.ai_api_key[0] != '\0');

    /* Update AI chat window with new key/provider/theme if open */
    if (g_hwndAiChat && IsWindow(g_hwndAiChat)) {
        ai_chat_update_key(g_hwndAiChat,
                           g_config->settings.ai_api_key,
                           g_config->settings.ai_provider,
                           g_config->settings.ai_custom_url,
                           g_config->settings.ai_custom_model);
        ai_chat_update_notes(g_hwndAiChat,
                            g_active_session ? g_active_session->conn_profile.ai_notes : NULL,
                            g_config->settings.ai_system_notes);
        ai_chat_set_theme(g_hwndAiChat,
                          g_config->settings.colour_scheme);
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

/* Start the slide animation for the docked AI panel.
 * Animates from the current panel width to target_w. */
static void start_ai_panel_anim(HWND hwnd, int target_w)
{
    g_ai_anim_from = g_ai_panel_width;
    g_ai_target_width = target_w;
    g_ai_anim_start = GetTickCount64();
    SetTimer(hwnd, AI_ANIM_TIMER_ID, AI_ANIM_INTERVAL, NULL);
}

/* Helper: trigger relayout */
static void relayout_main(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    SendMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                MAKELPARAM(rc.right, rc.bottom));
}

static HWND create_ai_chat(HWND parent)
{
    return ai_chat_show(parent,
                        g_config->settings.ai_api_key,
                        g_config->settings.ai_provider,
                        g_config->settings.ai_custom_url,
                        g_config->settings.ai_custom_model,
                        g_config->settings.paste_delay_ms,
                        g_config->settings.font,
                        g_config->settings.ai_font,
                        g_config->settings.colour_scheme,
                        g_active_session ? g_active_session->conn_profile.ai_notes : NULL,
                        g_config->settings.ai_system_notes,
                        g_active_session ? &g_active_session->ai_state : NULL,
                        g_active_session ? g_active_session->conn_profile.name : NULL,
                        g_ai_docked);
}

static void hide_ai_panel(HWND parent) {
    if (!g_hwndAiChat || !IsWindow(g_hwndAiChat)) return;
    if (g_ai_docked) {
        g_ai_last_width = g_ai_panel_width;
        /* Animate closed — ShowWindow(SW_HIDE) happens when anim finishes */
        start_ai_panel_anim(parent, 0);
    } else {
        ShowWindow(g_hwndAiChat, SW_HIDE);
    }
}

static void on_ai_clicked(void) {
    HWND parent = GetParent(g_hwndTabs);
    int has_session = g_active_session && g_active_session->channel;

    /* No connected session — block open, allow hide */
    if (!has_session) {
        if (g_hwndAiChat && IsWindow(g_hwndAiChat)
            && IsWindowVisible(g_hwndAiChat)) {
            hide_ai_panel(parent);
        } else {
            MessageBoxA(parent,
                "No active SSH session.\nPlease connect to a session first.",
                "AI Assist", MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    if (g_hwndAiChat && IsWindow(g_hwndAiChat)) {
        if (g_ai_docked) {
            /* Docked toggle: hide and reclaim terminal space */
            if (IsWindowVisible(g_hwndAiChat)) {
                hide_ai_panel(parent);
            } else {
                ShowWindow(g_hwndAiChat, SW_SHOW);
                RECT rc;
                GetClientRect(parent, &rc);
                int target = g_ai_last_width > 0
                    ? g_ai_last_width
                    : ai_dock_pct_to_px(rc.right, AI_DOCK_DEFAULT_PCT,
                                        AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
                start_ai_panel_anim(parent, target);
            }
        } else {
            /* Floating toggle */
            if (IsWindowVisible(g_hwndAiChat))
                ShowWindow(g_hwndAiChat, SW_HIDE);
            else {
                ShowWindow(g_hwndAiChat, SW_SHOW);
                SetForegroundWindow(g_hwndAiChat);
            }
        }
        return;
    }

    if (!g_config || g_config->settings.ai_api_key[0] == '\0') {
        MessageBoxA(parent,
            "No AI API key configured.\nPlease set one in Settings.",
            "AI Chat", MB_OK | MB_ICONINFORMATION);
        return;
    }

    g_hwndAiChat = create_ai_chat(parent);

    /* Set the active session if one exists */
    if (g_hwndAiChat && g_active_session) {
        ai_chat_set_session(g_hwndAiChat,
                           g_active_session->term,
                           g_active_session->channel);
    }

    if (g_ai_docked && g_hwndAiChat) {
        RECT rc;
        GetClientRect(parent, &rc);
        int target = g_ai_last_width > 0
            ? g_ai_last_width
            : ai_dock_pct_to_px(rc.right, AI_DOCK_DEFAULT_PCT,
                                AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
        /* Give the window a proper initial size before showing it,
         * so child controls are created/laid out at a valid size. */
        int splitter = AI_DOCK_SPLITTER_W;
        SetWindowPos(g_hwndAiChat, NULL,
            rc.right - target + splitter, g_tab_height,
            target - splitter, rc.bottom - g_tab_height,
            SWP_NOZORDER);
        ShowWindow(g_hwndAiChat, SW_SHOW);
        start_ai_panel_anim(parent, target);
    }
}

/* Toggle between docked and floating mode */
static void on_ai_dock_toggle(HWND hwnd) {
    g_ai_docked = !g_ai_docked;

    /* Close current AI window (saves state via WM_DESTROY) */
    if (g_hwndAiChat && IsWindow(g_hwndAiChat)) {
        ai_chat_close(g_hwndAiChat);
        g_hwndAiChat = NULL;
    }
    g_ai_panel_width = 0;
    relayout_main(hwnd);

    /* Reopen in new mode */
    on_ai_clicked();
}

static void on_status_click(int index, void *user_data, TabStatus status) {
    Session *s = (Session *)user_data;
    if (!s) return;
    HWND hParent = GetParent(g_hwndTabs);

    if (status == TAB_CONNECTED) {
        /* Ask to disconnect */
        int ans = MessageBoxA(hParent,
                              "Disconnect this session?",
                              "Disconnect",
                              MB_YESNO | MB_ICONQUESTION);
        if (ans == IDYES) {
            if (g_paste.channel == s->channel)
                paste_cancel();
            term_process(s->term, "\r\n[Disconnected by user]\r\n", 25);
            if (s->channel) { ssh_channel_free(s->channel); s->channel = NULL; }
            if (s->ssh)     { ssh_session_free(s->ssh);     s->ssh = NULL; }
            if (s->session_log) { fclose(s->session_log); s->session_log = NULL; }
            int tidx = tabs_find(g_hwndTabs, s);
            if (tidx >= 0) {
                tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
                tabs_set_logging(g_hwndTabs, tidx, 0);
            }
            invalidate_terminal(hParent);
        }
    } else if (status == TAB_DISCONNECTED) {
        /* Attempt reconnect using stored profile */
        if (s->conn_profile.host[0] == '\0') {
            MessageBoxA(hParent,
                        "No connection profile available for reconnection.",
                        "Reconnect", MB_OK | MB_ICONINFORMATION);
            return;
        }
        /* Clean up any leftover SSH state */
        if (s->channel) { ssh_channel_free(s->channel); s->channel = NULL; }
        if (s->ssh)     { ssh_session_free(s->ssh);     s->ssh = NULL; }
        if (s->session_log) { fclose(s->session_log); s->session_log = NULL; }

        /* Re-populate password from config — it was zeroed after first auth */
        for (size_t i = 0; i < vec_size(&g_config->profiles); i++) {
            const Profile *pr = (const Profile *)vec_get(&g_config->profiles, i);
            if (strcmp(pr->host, s->conn_profile.host) == 0 &&
                strcmp(pr->username, s->conn_profile.username) == 0 &&
                pr->port == s->conn_profile.port) {
                memcpy(s->conn_profile.password, pr->password,
                       sizeof(s->conn_profile.password));
                break;
            }
        }

        term_process(s->term, "\r\nReconnecting", 14);

        int tidx = tabs_find(g_hwndTabs, s);
        if (tidx >= 0)
            tabs_set_status(g_hwndTabs, tidx, TAB_CONNECTING);

        s->conn_state    = CONN_CONNECTING;
        s->conn_cancelled = 0;
        s->conn_result   = 0;
        s->conn_error[0] = '\0';
        s->conn_start_ms = GetTickCount64();
        s->conn_dots     = 0;
        s->conn_hwnd     = hParent;

        s->conn_thread = CreateThread(NULL, 0, connection_thread, s, 0, NULL);
        if (!s->conn_thread) {
            term_process(s->term, "\r\nFailed to start connection thread.\r\n", 38);
            if (tidx >= 0)
                tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
            s->conn_state = CONN_IDLE;
        }
        invalidate_terminal(hParent);
    }
    /* TAB_CONNECTING and TAB_IDLE: no action */
    (void)index;
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
        g_config->settings.font_size,
        g_config->settings.colour_scheme);

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
        if (IsWindowVisible(g_hwndScrollbar))
            ShowWindow(g_hwndScrollbar, SW_HIDE);
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

    /* Auto show/hide: only display when content overflows */
    if (total > rows) {
        if (!IsWindowVisible(g_hwndScrollbar))
            ShowWindow(g_hwndScrollbar, SW_SHOWNOACTIVATE);
    } else {
        if (IsWindowVisible(g_hwndScrollbar))
            ShowWindow(g_hwndScrollbar, SW_HIDE);
    }
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
    renderer_init(&g_renderer, g_config->settings.font, new_size,
                  get_window_dpi(hwnd));
    apply_config_colors();
    renderer_apply_theme(hwnd, g_renderer.defaultBg);

    /* Mark all rows dirty so the renderer repaints with the new font */
    if (g_active_session && g_active_session->term)
        term_mark_all_dirty(g_active_session->term);
    dispbuf_invalidate(&g_renderer.dispbuf);

    /* Send synthetic WM_SIZE so terminals resize to the new character grid. */
    RECT rc;
    GetClientRect(hwnd, &rc);
    SendMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                MAKELPARAM(rc.right, rc.bottom));
    invalidate_terminal(hwnd);
}

/* ---- Owner-draw menu for colour-scheme integration ---- */

/* Per-item data attached via MF_OWNERDRAW */
typedef struct {
    UINT   id;           /* command ID (or 0 for popup) */
    HMENU  hSub;         /* submenu handle, or NULL */
    char   text[64];     /* display text */
    char   accel[32];    /* accelerator text (after \t), or "" */
    int    is_separator; /* 1 = separator line */
} MenuItemData;

/* Small pool of MenuItemData — never freed (lives for app lifetime) */
#define MAX_MENU_ITEMS 32
static MenuItemData g_menu_items[MAX_MENU_ITEMS];
static int g_menu_item_count;

static MenuItemData *alloc_menu_item(void)
{
    if (g_menu_item_count >= MAX_MENU_ITEMS) return &g_menu_items[0];
    return &g_menu_items[g_menu_item_count++];
}

/* Helper: add an owner-draw item to a menu */
static void menu_add_item(HMENU menu, UINT id, const char *label)
{
    MenuItemData *mi = alloc_menu_item();
    mi->id   = id;
    mi->hSub = NULL;
    mi->is_separator = 0;
    /* Split "Text\tAccel" */
    const char *tab = strchr(label, '\t');
    if (tab) {
        size_t n = (size_t)(tab - label);
        if (n >= sizeof(mi->text)) n = sizeof(mi->text) - 1;
        memcpy(mi->text, label, n);
        mi->text[n] = '\0';
        (void)snprintf(mi->accel, sizeof(mi->accel), "%s", tab + 1);
    } else {
        (void)snprintf(mi->text, sizeof(mi->text), "%s", label);
        mi->accel[0] = '\0';
    }
    AppendMenu(menu, MF_OWNERDRAW, id, (LPCSTR)mi);
}

static void menu_add_separator(HMENU menu)
{
    MenuItemData *mi = alloc_menu_item();
    mi->id = 0;
    mi->hSub = NULL;
    mi->is_separator = 1;
    mi->text[0] = '\0';
    mi->accel[0] = '\0';
    AppendMenu(menu, MF_OWNERDRAW | MF_SEPARATOR, 0, (LPCSTR)mi);
}

static void menu_add_popup(HMENU bar, HMENU sub, const char *label)
{
    MenuItemData *mi = alloc_menu_item();
    mi->id   = 0;
    mi->hSub = sub;
    mi->is_separator = 0;
    mi->accel[0] = '\0';
    (void)snprintf(mi->text, sizeof(mi->text), "%s", label);
    AppendMenu(bar, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)sub, (LPCSTR)mi);
}

/* Apply theme background to a menu handle */
static void menu_set_bg(HMENU menu, COLORREF bg)
{
    MENUINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask  = MIM_BACKGROUND;
    mi.hbrBack = CreateSolidBrush(bg);
    SetMenuInfo(menu, &mi);
}

/* Convert 0xRRGGBB to COLORREF */
static COLORREF menu_tc(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/* Build the application menu bar */
static HMENU create_app_menu(void)
{
    g_menu_item_count = 0;

    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    menu_add_item(hFile, IDM_FILE_NEW_SESSION, "New Session\tCtrl+T");
    menu_add_separator(hFile);
    menu_add_item(hFile, IDM_FILE_CONNECT, "Session Manager...");
    menu_add_item(hFile, IDM_FILE_DISCONNECT, "Disconnect");
    menu_add_separator(hFile);
    menu_add_item(hFile, IDM_FILE_LOG_START, "Start Logging");
    menu_add_item(hFile, IDM_FILE_LOG_STOP, "Stop Logging");
    menu_add_separator(hFile);
    menu_add_item(hFile, IDM_FILE_SAVE_AI, "Save AI Chat...");
    menu_add_separator(hFile);
    menu_add_item(hFile, IDM_FILE_EXIT, "Exit");
    menu_add_popup(hMenu, hFile, "File");

    HMENU hEdit = CreatePopupMenu();
    menu_add_item(hEdit, IDM_EDIT_COPY, "Copy\tCtrl+C");
    menu_add_item(hEdit, IDM_EDIT_PASTE, "Paste\tCtrl+V");
    menu_add_item(hEdit, IDM_EDIT_SELECT_ALL, "Select All\tCtrl+A");
    menu_add_separator(hEdit);
    menu_add_item(hEdit, IDM_EDIT_SETTINGS, "Settings...");
    menu_add_popup(hMenu, hEdit, "Edit");

    HMENU hView = CreatePopupMenu();
    menu_add_item(hView, IDM_VIEW_AI_CHAT, "AI Assist\tCtrl+Space");
    menu_add_item(hView, IDM_VIEW_AI_UNDOCK, "Undock AI Assist");
    menu_add_item(hView, IDM_VIEW_FULLSCREEN, "Fullscreen\tF11");
    menu_add_popup(hMenu, hView, "View");

    HMENU hHelp = CreatePopupMenu();
    menu_add_item(hHelp, IDM_HELP_GUIDE, "User Guide");
    menu_add_separator(hHelp);
    menu_add_item(hHelp, IDM_ABOUT, "About");
    menu_add_popup(hMenu, hHelp, "Help");

    /* Apply theme background to all menus */
    if (g_theme) {
        COLORREF bg = menu_tc(g_theme->bg_secondary);
        menu_set_bg(hMenu, bg);
        menu_set_bg(hFile, bg);
        menu_set_bg(hEdit, bg);
        menu_set_bg(hView, bg);
        menu_set_bg(hHelp, bg);
    }

    return hMenu;
}

/* Copy current selection to clipboard */
static void do_copy(HWND hwnd)
{
    if (!g_active_session || !g_active_session->term) return;
    if (!g_selection.valid) return;
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
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            /* M-8: resolve config file to an absolute path at startup so
             * GetOpenFileNameA (file browse dialogs) cannot change CWD and
             * cause saves to go to the wrong directory. */
            {
                char exe_dir[MAX_PATH];
                get_exe_dir(exe_dir, sizeof(exe_dir));
                if (exe_dir[0] != '\0')
                    (void)snprintf(g_config_path, sizeof(g_config_path),
                                   "%s\\" CONFIG_FILENAME, exe_dir);
                else
                    (void)snprintf(g_config_path, sizeof(g_config_path),
                                   CONFIG_FILENAME);

                g_config = config_load(g_config_path);
                if (!g_config) {
                    MessageBoxA(hwnd,
                        "Could not load " CONFIG_FILENAME ".\n\nStarting with default settings.",
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

            /* Compute DPI-scaled tab height and terminal left margin */
            g_dpi = get_window_dpi(hwnd);
            g_tab_height = MulDiv(TAB_HEIGHT_BASE, g_dpi, 96);
            g_left_margin = MulDiv(TERM_LEFT_MARGIN, g_dpi, 96);

            app_font_load_ui();

            /* Create UI font for owner-drawn menus */
            {
                int mh = -MulDiv(APP_FONT_UI_SIZE, g_dpi, 72);
                g_hMenuFont = CreateFont(mh, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
            }

            tabs_init(g_hInst);
            csb_register(g_hInst);

            RECT rc;
            GetClientRect(hwnd, &rc);

            g_hwndTabs = tabs_create(hwnd, 0, 0, rc.right, g_tab_height);

            /* Custom themed scrollbar on the right edge */
            g_hwndScrollbar = csb_create(hwnd,
                rc.right - CSB_WIDTH, g_tab_height,
                CSB_WIDTH, rc.bottom - g_tab_height,
                g_theme, g_hInst);
            tabs_set_callbacks(g_hwndTabs, on_tab_select, on_tab_new, on_tab_close, on_settings_clicked, on_log_toggle);
            tabs_set_ai_callback(g_hwndTabs, on_ai_clicked);
            tabs_set_status_click_callback(g_hwndTabs, on_status_click);
            tabs_set_ai_active(g_hwndTabs,
                               g_config->settings.ai_api_key[0] != '\0');
            tabs_set_theme(g_hwndTabs, g_theme);
            ai_chat_init(g_hInst);

            renderer_init(&g_renderer,
                          g_config->settings.font[0]
                              ? g_config->settings.font : APP_FONT_DEFAULT,
                          g_config->settings.font_size > 0
                              ? g_config->settings.font_size : 12,
                          get_window_dpi(hwnd));
            apply_config_colors();
            renderer_apply_theme(hwnd, g_renderer.defaultBg);

            /* Start I/O timer (10ms) and animation timer (500ms) */
            SetTimer(hwnd, 1, 10,  NULL);
            SetTimer(hwnd, 2, 500, NULL);
            
            /* Attach menu bar */
            SetMenu(hwnd, create_app_menu());

            PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0);
            return 0;

        case WM_INITMENUPOPUP: {
            /* Grey out "Save AI Chat..." when no content or panel hidden */
            HMENU hBar = GetMenu(hwnd);
            if (hBar) {
                int has = g_hwndAiChat && IsWindow(g_hwndAiChat)
                          && ai_chat_has_content(g_hwndAiChat);
                EnableMenuItem(hBar, IDM_FILE_SAVE_AI,
                               (UINT)(MF_BYCOMMAND
                                      | (has ? MF_ENABLED : MF_GRAYED)));
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_FILE_NEW_SESSION:
                    on_tab_new();
                    return 0;
                case IDM_FILE_CONNECT:
                    PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0);
                    return 0;
                case IDM_FILE_DISCONNECT:
                    if (g_active_session) {
                        int tidx = tabs_find(g_hwndTabs, g_active_session);
                        if (tidx >= 0)
                            on_status_click(tidx, g_active_session,
                                            tabs_get_status(g_hwndTabs, tidx));
                    }
                    return 0;
                case IDM_FILE_LOG_START:
                    if (g_active_session) {
                        int tidx = tabs_find(g_hwndTabs, g_active_session);
                        if (tidx >= 0 && !tabs_get_logging(g_hwndTabs, tidx))
                            on_log_toggle(tidx, g_active_session);
                    }
                    return 0;
                case IDM_FILE_LOG_STOP:
                    if (g_active_session) {
                        int tidx = tabs_find(g_hwndTabs, g_active_session);
                        if (tidx >= 0 && tabs_get_logging(g_hwndTabs, tidx))
                            on_log_toggle(tidx, g_active_session);
                    }
                    return 0;
                case IDM_FILE_EXIT:
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                case IDM_EDIT_COPY:
                    do_copy(hwnd);
                    return 0;
                case IDM_EDIT_PASTE:
                    do_paste(hwnd);
                    return 0;
                case IDM_EDIT_SELECT_ALL:
                    if (g_active_session && g_active_session->term) {
                        g_selection.start_row = 0;
                        g_selection.start_col = 0;
                        g_selection.end_row = g_active_session->term->rows - 1;
                        g_selection.end_col = g_active_session->term->cols - 1;
                        g_selection.valid = true;
                        invalidate_terminal(hwnd);
                    }
                    return 0;
                case IDM_EDIT_SETTINGS:
                    on_settings_clicked();
                    return 0;
                case IDM_FILE_SAVE_AI:
                    /* Forward to the AI chat's save button (IDC_CHAT_SAVE) */
                    if (g_hwndAiChat && IsWindow(g_hwndAiChat))
                        SendMessage(g_hwndAiChat, WM_COMMAND,
                                    MAKEWPARAM(4010 /*IDC_CHAT_SAVE*/,
                                               BN_CLICKED), 0);
                    return 0;
                case IDM_VIEW_AI_CHAT:
                    on_ai_clicked();
                    return 0;
                case IDM_VIEW_AI_UNDOCK:
                    on_ai_dock_toggle(hwnd);
                    return 0;
                case IDM_VIEW_FULLSCREEN: {
                    static WINDOWPLACEMENT wp_prev;
                    static int wp_init = 0;
                    if (!wp_init) { memset(&wp_prev, 0, sizeof(wp_prev)); wp_prev.length = sizeof(wp_prev); wp_init = 1; }
                    LONG style = GetWindowLong(hwnd, GWL_STYLE);
                    if (style & WS_OVERLAPPEDWINDOW) {
                        MONITORINFO mi;
                        mi.cbSize = sizeof(mi);
                        if (GetWindowPlacement(hwnd, &wp_prev) &&
                            GetMonitorInfo(MonitorFromWindow(hwnd,
                                           MONITOR_DEFAULTTOPRIMARY), &mi)) {
                            SetWindowLong(hwnd, GWL_STYLE,
                                          (LONG)((DWORD)style & ~(DWORD)WS_OVERLAPPEDWINDOW));
                            SetWindowPos(hwnd, HWND_TOP,
                                (int)mi.rcMonitor.left, (int)mi.rcMonitor.top,
                                (int)(mi.rcMonitor.right - mi.rcMonitor.left),
                                (int)(mi.rcMonitor.bottom - mi.rcMonitor.top),
                                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                        }
                    } else {
                        SetWindowLong(hwnd, GWL_STYLE,
                                      (LONG)((DWORD)style | (DWORD)WS_OVERLAPPEDWINDOW));
                        SetWindowPlacement(hwnd, &wp_prev);
                        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                    }
                    return 0;
                }
                case IDM_HELP_GUIDE:
                    help_guide_show(hwnd, g_config->settings.colour_scheme);
                    return 0;
                case IDM_ABOUT:
                    MessageBoxA(hwnd,
                        "Nutshell SSH Client v" APP_VERSION
                        "\n\nA lightweight SSH terminal emulator."
                        "\n\nCopyright \xA9 2026 Thomas Sulkiewicz",
                        "About Nutshell", MB_OK | MB_ICONINFORMATION);
                    return 0;
            }
            break;

        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lParam;
            if (mis->CtlType == ODT_MENU) {
                MenuItemData *mi = (MenuItemData *)(ULONG_PTR)mis->itemData;
                if (mi && mi->is_separator) {
                    mis->itemHeight = (UINT)MulDiv(6, g_dpi, 96);
                    mis->itemWidth  = 0;
                    return TRUE;
                }
                if (mi) {
                    HDC hdc = GetDC(hwnd);
                    HFONT oldFont = g_hMenuFont ? (HFONT)SelectObject(hdc, g_hMenuFont) : NULL;
                    SIZE sz = {0, 0};
                    GetTextExtentPoint32A(hdc, mi->text, (int)strlen(mi->text), &sz);
                    /* Top-level menu bar items: tighter padding */
                    int hpad = mi->hSub ? MulDiv(10, g_dpi, 96)
                                        : MulDiv(24, g_dpi, 96);
                    mis->itemWidth = (UINT)sz.cx + (UINT)hpad;
                    if (mi->accel[0]) {
                        SIZE az;
                        GetTextExtentPoint32A(hdc, mi->accel, (int)strlen(mi->accel), &az);
                        mis->itemWidth += (UINT)az.cx + (UINT)MulDiv(12, g_dpi, 96);
                    }
                    mis->itemHeight = (UINT)sz.cy + (UINT)MulDiv(4, g_dpi, 96);
                    if (oldFont) SelectObject(hdc, oldFont);
                    ReleaseDC(hwnd, hdc);
                    return TRUE;
                }
            }
            break;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;
            if (dis->CtlType == ODT_MENU) {
                MenuItemData *mid = (MenuItemData *)(ULONG_PTR)dis->itemData;
                if (!mid) break;
                HDC hdc = dis->hDC;
                HFONT oldFont = g_hMenuFont ? (HFONT)SelectObject(hdc, g_hMenuFont) : NULL;
                RECT rcItem = dis->rcItem;
                int selected = (dis->itemState & ODS_SELECTED) != 0;

                /* Theme colours */
                COLORREF cBg   = g_theme ? menu_tc(g_theme->bg_secondary) : GetSysColor(COLOR_MENU);
                COLORREF cFg   = g_theme ? menu_tc(g_theme->text_main)    : GetSysColor(COLOR_MENUTEXT);
                COLORREF cSel  = g_theme ? menu_tc(g_theme->accent)       : GetSysColor(COLOR_HIGHLIGHT);
                COLORREF cDim  = g_theme ? menu_tc(g_theme->text_dim)     : GetSysColor(COLOR_GRAYTEXT);
                COLORREF cBord = g_theme ? menu_tc(g_theme->border)       : GetSysColor(COLOR_MENUHILIGHT);

                if (mid->is_separator) {
                    HBRUSH bgBr = CreateSolidBrush(cBg);
                    FillRect(hdc, &rcItem, bgBr);
                    DeleteObject(bgBr);
                    int my = (rcItem.top + rcItem.bottom) / 2;
                    HPEN sepPen = CreatePen(PS_SOLID, 1, cBord);
                    HPEN oldPen = (HPEN)SelectObject(hdc, sepPen);
                    MoveToEx(hdc, rcItem.left + MulDiv(4, g_dpi, 96), my, NULL);
                    LineTo(hdc, rcItem.right - MulDiv(4, g_dpi, 96), my);
                    SelectObject(hdc, oldPen);
                    DeleteObject(sepPen);
                    if (oldFont) SelectObject(hdc, oldFont);
                    return TRUE;
                }

                /* Background */
                HBRUSH itemBr = CreateSolidBrush(selected ? cSel : cBg);
                FillRect(hdc, &rcItem, itemBr);
                DeleteObject(itemBr);

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, selected ? RGB(255, 255, 255) : cFg);

                /* Text — left aligned with padding */
                int xPad = MulDiv(8, g_dpi, 96);
                RECT rcText = rcItem;
                rcText.left += xPad;
                DrawTextA(hdc, mid->text, -1, &rcText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                /* Accelerator — right aligned */
                if (mid->accel[0]) {
                    SetTextColor(hdc, selected ? RGB(220, 220, 220) : cDim);
                    RECT rcAccel = rcItem;
                    rcAccel.right -= xPad;
                    DrawTextA(hdc, mid->accel, -1, &rcAccel,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                }
                if (oldFont) SelectObject(hdc, oldFont);
                return TRUE;
            }
            break;
        }

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
                            /* Auto-hide AI panel when the active session disconnects */
                            if (s == g_active_session)
                                hide_ai_panel(hwnd);
                        }
                        if (poll_rc > 0 || term_has_dirty_rows(s->term)) {
                            DWORD now = GetTickCount();
                            if (now - g_last_paint_tick >= PAINT_COOLDOWN_MS) {
                                if (poll_rc > 0)
                                    dispbuf_invalidate(&g_renderer.dispbuf);
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
            } else if (wParam == AI_ANIM_TIMER_ID) {
                ULONGLONG now = GetTickCount64();
                double t = (double)(now - g_ai_anim_start)
                           / (double)AI_DOCK_ANIM_MS;
                if (t >= 1.0) t = 1.0;
                g_ai_panel_width = ai_dock_anim_lerp(
                    g_ai_anim_from, g_ai_target_width, t);
                if (t >= 1.0) {
                    g_ai_panel_width = g_ai_target_width;
                    KillTimer(hwnd, AI_ANIM_TIMER_ID);
                    /* Hide the window after close animation finishes */
                    if (g_ai_target_width == 0
                        && g_hwndAiChat && IsWindow(g_hwndAiChat))
                        ShowWindow(g_hwndAiChat, SW_HIDE);
                }
                relayout_main(hwnd);
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

                /* Update AI chat channel — fixes commands failing when
                 * the AI window was opened before the session connected. */
                if (ai_chat_should_update_channel(
                        g_hwndAiChat != NULL,
                        NULL,  /* don't care about current chat channel */
                        s->channel,
                        s == g_active_session)) {
                    ai_chat_set_session(g_hwndAiChat, s->term, s->channel);
                }

                /* Reopen AI panel that was closed before first session */
                if (g_ai_reopen_after_connect && s == g_active_session) {
                    g_ai_reopen_after_connect = 0;
                    on_ai_clicked();
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
            /* Account for the custom scrollbar width and left margin */
            nc_w += CSB_WIDTH + g_left_margin;

            int client_w = (wr->right  - wr->left) - nc_w;
            int client_h = (wr->bottom - wr->top)  - nc_h;

            int snapped_w, snapped_h;
            snap_calc(client_w, client_h,
                      g_renderer.charWidth, g_renderer.charHeight,
                      nc_w, nc_h, g_tab_height,
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

            /* Docked AI panel — leave a splitter gap between terminal and panel */
            int ai_w = 0;
            if (g_ai_docked && g_hwndAiChat && IsWindowVisible(g_hwndAiChat)) {
                ai_w = g_ai_panel_width;
                /* Clamp to max if window was resized smaller */
                if (ai_w > width * AI_DOCK_MAX_PCT / 100)
                    ai_w = width * AI_DOCK_MAX_PCT / 100;
                int splitter = AI_DOCK_SPLITTER_W;
                SetWindowPos(g_hwndAiChat, NULL,
                    width - ai_w + splitter, g_tab_height,
                    ai_w - splitter, height - g_tab_height,
                    SWP_NOZORDER | SWP_NOCOPYBITS);
            }

            if (g_hwndTabs) {
                SetWindowPos(g_hwndTabs, NULL, 0, 0, width, g_tab_height, SWP_NOZORDER);
            }

            /* Reposition custom scrollbar (left of AI panel if docked) */
            if (g_hwndScrollbar) {
                SetWindowPos(g_hwndScrollbar, NULL,
                    width - ai_w - CSB_WIDTH, g_tab_height,
                    CSB_WIDTH, height - g_tab_height,
                    SWP_NOZORDER);
            }

            if (g_active_session && g_active_session->term && g_renderer.charWidth > 0 && g_renderer.charHeight > 0) {
                int term_h = height - g_tab_height;
                if (term_h < 1) term_h = 1;

                int term_w = ai_dock_terminal_width(width, ai_w, CSB_WIDTH, g_left_margin);
                int cols = term_w / g_renderer.charWidth;
                int rows = term_h / g_renderer.charHeight;
                if (cols < 1) cols = 1;
                if (rows < 1) rows = 1;

                if (cols != g_active_session->term->cols || rows != g_active_session->term->rows) {
                    term_resize(g_active_session->term, rows, cols);
                    term_mark_all_dirty(g_active_session->term);
                    dispbuf_resize(&g_renderer.dispbuf, rows, cols);
                    if (g_active_session->channel)
                        ssh_pty_resize(g_active_session->channel, cols, rows);
                }
            }
            /* Always repaint the full window on resize so gutter areas
             * (below/right of the character grid) get filled with the
             * background colour instead of showing GDI artifacts. */
            InvalidateRect(hwnd, NULL, FALSE);
            update_scrollbar(hwnd);
            return 0;
        }

        case WM_DPICHANGED: {
            int oldDpi = g_dpi;
            int newDpi = (int)HIWORD(wParam);
            g_dpi = newDpi;
            g_tab_height = MulDiv(TAB_HEIGHT_BASE, g_dpi, 96);
            g_left_margin = MulDiv(TERM_LEFT_MARGIN, g_dpi, 96);

            /* Rescale docked AI panel width proportionally */
            if (g_ai_panel_width > 0)
                g_ai_panel_width = MulDiv(g_ai_panel_width, newDpi, oldDpi);
            if (g_ai_last_width > 0)
                g_ai_last_width = MulDiv(g_ai_last_width, newDpi, oldDpi);

            RECT *suggested = (RECT *)lParam;
            SetWindowPos(hwnd, NULL,
                suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);

            /* Recreate renderer fonts at new DPI */
            renderer_free(&g_renderer);
            renderer_init(&g_renderer, g_config->settings.font,
                          g_config->settings.font_size, newDpi);
            apply_config_colors();
            renderer_apply_theme(hwnd, g_renderer.defaultBg);

            /* Recreate menu font at new DPI */
            if (g_hMenuFont) DeleteObject(g_hMenuFont);
            {
                int mh = -MulDiv(APP_FONT_UI_SIZE, g_dpi, 72);
                g_hMenuFont = CreateFont(mh, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
            }

            /* Recreate tab strip fonts */
            tabs_set_font(g_hwndTabs, g_config->settings.font);

            /* Recalculate terminal grid via WM_SIZE */
            RECT dpi_rc;
            GetClientRect(hwnd, &dpi_rc);
            SendMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                        MAKELPARAM(dpi_rc.right, dpi_rc.bottom));
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            /* Determine terminal area right edge (excludes docked AI panel) */
            int ai_w = 0;
            if (g_ai_docked && g_hwndAiChat && IsWindowVisible(g_hwndAiChat))
                ai_w = g_ai_panel_width;
            RECT client;
            GetClientRect(hwnd, &client);
            int term_right_edge = client.right - ai_w;

            if (g_active_session && g_active_session->term) {
                renderer_draw(&g_renderer, hdc, g_active_session->term, g_left_margin, g_tab_height, &ps.rcPaint, &g_selection);

                /* Fill gutter areas not covered by complete character cells. */
                HBRUSH bg = CreateSolidBrush(g_renderer.defaultBg);

                int text_bottom = g_tab_height +
                    g_active_session->term->rows * g_renderer.charHeight;
                if (text_bottom < client.bottom) {
                    RECT r = { 0, text_bottom, term_right_edge, client.bottom };
                    FillRect(hdc, &r, bg);
                }

                int text_right = g_left_margin +
                    g_active_session->term->cols * g_renderer.charWidth;
                if (text_right < term_right_edge) {
                    RECT r = { text_right, g_tab_height, term_right_edge, text_bottom };
                    FillRect(hdc, &r, bg);
                }

                /* Left margin gutter */
                if (g_left_margin > 0) {
                    RECT r = { 0, g_tab_height, g_left_margin, text_bottom };
                    FillRect(hdc, &r, bg);
                }

                DeleteObject(bg);
            } else {
                HBRUSH brush = CreateSolidBrush(g_renderer.defaultBg);
                RECT fill = { ps.rcPaint.left, ps.rcPaint.top,
                              term_right_edge < ps.rcPaint.right ? term_right_edge : ps.rcPaint.right,
                              ps.rcPaint.bottom };
                FillRect(hdc, &fill, brush);
                DeleteObject(brush);
            }

            /* Fill the splitter gap between terminal and docked AI panel */
            if (ai_w > 0 && g_theme) {
                int splitter = AI_DOCK_SPLITTER_W;
                /* Fill entire gap with bg_primary so no white shows through */
                unsigned int bg_rgb = g_theme->bg_primary;
                HBRUSH gapBr = CreateSolidBrush(RGB((bg_rgb >> 16) & 0xFF,
                                                     (bg_rgb >>  8) & 0xFF,
                                                      bg_rgb        & 0xFF));
                RECT gr = { term_right_edge, g_tab_height,
                            term_right_edge + splitter, client.bottom };
                FillRect(hdc, &gr, gapBr);
                DeleteObject(gapBr);

                /* Draw 1px splitter line at the left edge of the gap */
                unsigned int sc = g_theme->border;
                HBRUSH sbr = CreateSolidBrush(RGB((sc >> 16) & 0xFF,
                                                   (sc >>  8) & 0xFF,
                                                    sc        & 0xFF));
                RECT sr = { term_right_edge, g_tab_height,
                            term_right_edge + 1, client.bottom };
                FillRect(hdc, &sr, sbr);
                DeleteObject(sbr);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCPAINT: {
            /* Let Windows paint the non-client area first */
            DefWindowProc(hwnd, msg, wParam, lParam);

            /* Paint over the bright 1px line at the bottom of the menu bar.
             * Windows draws this line in the system highlight color, which
             * is jarring on dark themes. */
            unsigned int line_rgb = menubar_line_color(g_theme, 0xF0F0F0);
            COLORREF line_cr = RGB((line_rgb >> 16) & 0xFF,
                                   (line_rgb >>  8) & 0xFF,
                                    line_rgb        & 0xFF);

            /* Get the 1px rect just above the client area */
            RECT wrc;
            GetWindowRect(hwnd, &wrc);
            POINT client_org = {0, 0};
            ClientToScreen(hwnd, &client_org);

            int lx, ly, lw, lh;
            menubar_line_rect(wrc.left, wrc.top, client_org.y,
                              wrc.right - wrc.left, &lx, &ly, &lw, &lh);

            /* Paint in screen coordinates using the window DC */
            HDC hdc = GetWindowDC(hwnd);
            if (hdc) {
                RECT lr = { lx - wrc.left, ly - wrc.top,
                            lx - wrc.left + lw, ly - wrc.top + lh };
                HBRUSH br = CreateSolidBrush(line_cr);
                FillRect(hdc, &lr, br);
                DeleteObject(br);
                ReleaseDC(hwnd, hdc);
            }
            return 0;
        }

        case WM_NCACTIVATE: {
            /* Repaint non-client area on activation change to keep the
             * menu bar line covered after Windows redraws it. */
            LRESULT res = DefWindowProc(hwnd, msg, wParam, lParam);
            SendMessage(hwnd, WM_NCPAINT, 0, 0);
            return res;
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
            /* F11 — toggle fullscreen */
            if (wParam == VK_F11) {
                SendMessage(hwnd, WM_COMMAND, IDM_VIEW_FULLSCREEN, 0);
                return 0;
            }
            /* Ctrl+T — new tab */
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == (WPARAM)'T') {
                on_tab_new();
                return 0;
            }
            /* Ctrl+Space — toggle AI Assist panel */
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == VK_SPACE) {
                on_ai_clicked();
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

        case WM_SETCURSOR: {
            if (g_ai_docked && g_ai_panel_width > 0 && g_hwndAiChat) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                RECT crc;
                GetClientRect(hwnd, &crc);
                int splitter_x = crc.right - g_ai_panel_width;
                if (ai_dock_splitter_hit(pt.x, splitter_x,
                                          AI_DOCK_SPLITTER_HIT, pt.y,
                                          g_tab_height)) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            /* Check splitter drag first */
            if (g_ai_docked && g_ai_panel_width > 0 && g_hwndAiChat) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                RECT crc;
                GetClientRect(hwnd, &crc);
                int splitter_x = crc.right - g_ai_panel_width;
                if (ai_dock_splitter_hit(mx, splitter_x,
                                          AI_DOCK_SPLITTER_HIT, my,
                                          g_tab_height)) {
                    g_ai_splitter_dragging = 1;
                    SetCapture(hwnd);
                    return 0;
                }
            }
            if (!g_active_session || !g_active_session->term) break;
            SetFocus(hwnd);  /* reclaim keyboard focus from AI panel */
            SetCapture(hwnd);
            int mx = LOWORD(lParam) - g_left_margin, my = HIWORD(lParam);
            selection_pixel_to_cell(mx, my,
                g_renderer.charWidth, g_renderer.charHeight,
                g_tab_height, g_active_session->term->rows,
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
            if (g_ai_splitter_dragging) {
                RECT crc;
                GetClientRect(hwnd, &crc);
                int mx = GET_X_LPARAM(lParam);
                int new_w = crc.right - mx;
                new_w = ai_dock_clamp_width(new_w, crc.right,
                                             AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
                g_ai_panel_width = new_w;
                g_ai_last_width = new_w;
                relayout_main(hwnd);
                /* Force synchronous repaint so no ghosting between frames */
                RedrawWindow(hwnd, NULL, NULL,
                             RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                return 0;
            }
            if (!g_selection.active) break;
            if (!(wParam & MK_LBUTTON)) {
                g_selection.active = false;
                break;
            }
            if (!g_active_session || !g_active_session->term) break;
            int mx = LOWORD(lParam) - g_left_margin, my = HIWORD(lParam);
            selection_pixel_to_cell(mx, my,
                g_renderer.charWidth, g_renderer.charHeight,
                g_tab_height, g_active_session->term->rows,
                g_active_session->term->cols,
                &g_selection.end_row, &g_selection.end_col);
            g_selection.valid = (g_selection.start_row != g_selection.end_row ||
                                 g_selection.start_col != g_selection.end_col);
            invalidate_terminal(hwnd);
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_ai_splitter_dragging) {
                g_ai_splitter_dragging = 0;
                ReleaseCapture();
                return 0;
            }
            if (!g_selection.active) break;
            ReleaseCapture();
            g_selection.active = false;
            if (!g_active_session || !g_active_session->term) break;
            /* Update end position from final mouse coordinates —
             * WM_MOUSEMOVE may not fire at the exact release point. */
            int mx = LOWORD(lParam) - g_left_margin, my = HIWORD(lParam);
            selection_pixel_to_cell(mx, my,
                g_renderer.charWidth, g_renderer.charHeight,
                g_tab_height, g_active_session->term->rows,
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
            if (g_hMenuFont) { DeleteObject(g_hMenuFont); g_hMenuFont = NULL; }
            app_font_free_ui();
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
    wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hIconSm = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_APPICON),
                                   IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); /* Will be overwritten by WM_PAINT */
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassEx(&wc)) {
        log_write(LOG_LEVEL_ERROR, "Window Registration Failed!");
        MessageBox(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return;
    }

    /* Initial size: slightly larger than the Session Manager dialog
     * (530×317 DLU ≈ 795×513 px at 96 DPI).  Add ~10% margin. */
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int dpi = get_window_dpi(NULL);
    int winW = MulDiv(880, dpi, 96);
    int winH = MulDiv(570, dpi, 96);
    if (winW > screenW) winW = screenW;
    if (winH > screenH) winH = screenH;
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        APP_TITLE,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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