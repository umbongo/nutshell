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

static const char *CLASS_NAME = "CongaSSH_Window";
static const char *APP_TITLE = "Conga.SSH";

#define TAB_HEIGHT 32
#define WM_SHOW_SESSION_MANAGER (WM_USER + 1)

typedef struct Session {
    Terminal *term;
    SshSession *ssh;
    SSHChannel *channel;
    FILE *session_log;   /* NULL when logging disabled */
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

static Session *create_session(int rows, int cols, const char *msg) {
    Session *s = xmalloc(sizeof(Session));
    s->term = term_init(rows, cols, 3000);
    s->ssh = NULL;
    s->channel = NULL;
    s->session_log = NULL;
    s->next = g_session_list;
    g_session_list = s;
    term_process(s->term, msg, strlen(msg));
    return s;
}

static void free_session(Session *s) {
    if (s) {
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
    /* Redraw terminal area */
    HWND hParent = GetParent(g_hwndTabs);
    InvalidateRect(hParent, NULL, FALSE);
    /* Ensure main window has focus to capture keys */
    SetFocus(hParent);
}

static void on_tab_close(int index, void *user_data) {
    Session *s = (Session *)user_data;
    
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
    
    /* Force repaint of window to clear terminal area if no session active */
    InvalidateRect(GetParent(g_hwndTabs), NULL, FALSE);
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
        wc.lpszClassName = "CongaPassDlg";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        class_done = 1;
    }

    PassCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    HWND hwnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "CongaPassDlg", "SSH Key Passphrase",
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
    return 0;
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
        char appdata[MAX_PATH];
        if (GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata)) == 0) {
            (void)snprintf(appdata, sizeof(appdata), ".");
        }
        (void)snprintf(log_dir, sizeof(log_dir), "%s\\sshclient\\logs",
                       appdata);
    }

    /* Create the directory (OK if already exists) */
    CreateDirectoryA(log_dir, NULL);

    /* Format timestamp using log_format from config */
    const char *fmt = g_config->settings.log_format[0]
                        ? g_config->settings.log_format
                        : "%Y-%m-%d_%H-%M-%S";
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

    return fopen(path, "a");
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

    char msg[512];
    snprintf(msg, sizeof(msg), "Connecting to %s@%s:%d...\r\n",
             info->username[0] ? info->username : "user",
             info->host[0] ? info->host : "localhost",
             info->port);

    Session *s = create_session(rows, cols, msg);
    char title[32];
    snprintf(title, sizeof(title), "%s", info->name[0] ? info->name : (info->host[0] ? info->host : "Session"));

    int idx = tabs_add(g_hwndTabs, title, s);
    tabs_set_active(g_hwndTabs, idx);
    tabs_set_status(g_hwndTabs, idx, TAB_CONNECTING);

    /* Initiate Connection */
    s->ssh = ssh_session_new();
    if (ssh_connect(s->ssh, info->host, info->port) != 0) {
        char err[512];
        snprintf(err, sizeof(err), "Cannot connect to %s:%d\n\n%s",
                 info->host, info->port, s->ssh->last_error);
        term_process(s->term, err, strlen(err));
        term_process(s->term, "\r\n", 2);
        MessageBoxA(GetParent(g_hwndTabs), err,
                    "Connection Error", MB_OK | MB_ICONERROR);
        tabs_set_status(g_hwndTabs, idx, TAB_DISCONNECTED);
        return;
    }

    /* ---- TOFU host key verification ---- */
    {
        size_t key_len = 0;
        int key_type = 0;
        const char *key = libssh2_session_hostkey(s->ssh->session, &key_len, &key_type);
        if (!key || key_len == 0) {
            term_process(s->term, "Error: could not retrieve host key.\r\n", 38);
            return;
        }

        char kh_path[MAX_PATH];
        get_knownhosts_path(kh_path, sizeof(kh_path));

        KnownHosts kh;
        if (knownhosts_init(&kh, s->ssh->session, kh_path) != KNOWNHOSTS_OK) {
            term_process(s->term, "Warning: could not open known_hosts file.\r\n", 43);
            /* Non-fatal — continue without TOFU */
        } else {
            char fingerprint[128];
            int tofu = knownhosts_check(&kh, info->host, info->port,
                                         key, key_len,
                                         fingerprint, sizeof(fingerprint));
            if (tofu == KNOWNHOSTS_NEW) {
                char dlg_msg[512];
                snprintf(dlg_msg, sizeof(dlg_msg),
                    "The authenticity of host '%s:%d' can't be established.\n\n"
                    "Host key fingerprint:\n%s\n\n"
                    "Do you want to trust this host and continue connecting?",
                    info->host, info->port, fingerprint);
                int ans = MessageBoxA(GetParent(g_hwndTabs), dlg_msg,
                                      "Unknown Host", MB_YESNO | MB_ICONWARNING);
                if (ans == IDYES) {
                    knownhosts_add(&kh, info->host, info->port, key, key_len);
                } else {
                    term_process(s->term, "Connection aborted by user.\r\n", 29);
                    knownhosts_free(&kh);
                    return;
                }
            } else if (tofu == KNOWNHOSTS_MISMATCH) {
                char dlg_msg[512];
                snprintf(dlg_msg, sizeof(dlg_msg),
                    "WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!\n\n"
                    "Host: %s:%d\nNew fingerprint:\n%s\n\n"
                    "This may indicate a MitM attack.\n"
                    "Connect anyway and update the stored key?",
                    info->host, info->port, fingerprint);
                int ans = MessageBoxA(GetParent(g_hwndTabs), dlg_msg,
                                      "Host Key Changed!", MB_YESNO | MB_ICONSTOP);
                if (ans == IDYES) {
                    knownhosts_add(&kh, info->host, info->port, key, key_len);
                } else {
                    term_process(s->term, "Connection aborted: host key mismatch.\r\n", 40);
                    knownhosts_free(&kh);
                    return;
                }
            }
            knownhosts_free(&kh);
        }
    }

    int auth_rc = -1;
    if (info->auth_type == AUTH_KEY) {
        auth_rc = ssh_auth_key(s->ssh, info->username, info->key_path, info->password);
        if (auth_rc != 0) {
            /* Key auth failed — may need passphrase.  Prompt and retry once. */
            char passphrase[256];
            memset(passphrase, 0, sizeof(passphrase));
            if (prompt_passphrase(GetParent(g_hwndTabs), passphrase, (int)sizeof(passphrase))) {
                auth_rc = ssh_auth_key(s->ssh, info->username, info->key_path, passphrase);
                if (auth_rc == 0) {
                    /* Cache passphrase in session memory (never written to disk) */
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
        char auth_err[256];
        snprintf(auth_err, sizeof(auth_err),
                 "Authentication failed for %s@%s.",
                 info->username, info->host);
        term_process(s->term, auth_err, strlen(auth_err));
        term_process(s->term, "\r\n", 2);
        MessageBoxA(GetParent(g_hwndTabs), auth_err,
                    "Authentication Error", MB_OK | MB_ICONERROR);
        tabs_set_status(g_hwndTabs, idx, TAB_DISCONNECTED);
        return;
    }

    s->channel = ssh_channel_open(s->ssh);
    if (s->channel) {
        ssh_pty_request(s->channel, "xterm", cols, rows);
        ssh_pty_shell(s->channel);
        ssh_session_set_blocking(s->ssh, false); /* Non-blocking for I/O loop */
        term_process(s->term, "Connected.\r\n", 12);
        s->session_log = open_session_log(info->host);
        tabs_set_connect_info(g_hwndTabs, idx,
                              info->username, info->host,
                              (unsigned long long)GetTickCount64());
        tabs_set_status(g_hwndTabs, idx, TAB_CONNECTED);
    } else {
        char ch_err[256];
        snprintf(ch_err, sizeof(ch_err),
                 "Could not open SSH channel to %s.", info->host);
        term_process(s->term, ch_err, strlen(ch_err));
        term_process(s->term, "\r\n", 2);
        MessageBoxA(GetParent(g_hwndTabs), ch_err,
                    "Channel Error", MB_OK | MB_ICONERROR);
        tabs_set_status(g_hwndTabs, idx, TAB_DISCONNECTED);
    }
}

static void on_tab_new(void) {
    Profile p;
    memset(&p, 0, sizeof(Profile));

    if (SessionManager_Show(g_hInst, GetParent(g_hwndTabs), &p)) {
        on_session_connect(&p);
    }
}

static void on_settings_clicked(void) {
    settings_dlg_show(GetParent(g_hwndTabs), g_config);
}

/* ---- Paste helper -------------------------------------------------------- */

/* Send clipboard text to the active session, with multi-line confirmation
 * when content is longer than PASTE_CONFIRM_THRESHOLD bytes or contains a
 * newline.  Lines are separated by paste_delay_ms milliseconds. */
#define PASTE_CONFIRM_THRESHOLD 64

static void do_paste(HWND hwnd)
{
    if (!g_active_session || !g_active_session->channel) return;

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

    /* Ask for confirmation when content is large or multi-line */
    int confirmed = 1;
    if (raw_len > PASTE_CONFIRM_THRESHOLD || line_count > 0) {
        char msg[512];
        /* Build a preview: first 80 printable chars */
        char preview[81];
        int pi = 0;
        for (size_t i = 0; i < raw_len && pi < 80; i++) {
            char c = raw[i];
            if (c == '\r') continue;
            preview[pi++] = (c == '\n') ? ' ' : c;
        }
        preview[pi] = '\0';

        (void)snprintf(msg, sizeof(msg),
            "Paste %d line%s (%zu chars)?\n\nPreview:\n%.80s%s",
            line_count + 1,
            line_count == 1 ? "" : "s",
            raw_len,
            preview,
            raw_len > 80 ? "..." : "");

        int ans = MessageBoxA(hwnd, msg, "Confirm Paste",
                              MB_YESNO | MB_ICONQUESTION);
        confirmed = (ans == IDYES);
    }

    if (confirmed) {
        int delay_ms = g_config ? g_config->settings.paste_delay_ms : 0;

        /* Send line by line, applying inter-line delay */
        const char *p = raw;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t chunk;
            if (nl) {
                chunk = (size_t)(nl - p) + 1u; /* include the '\n' */
            } else {
                chunk = strlen(p);
            }

            /* Skip bare \r characters */
            for (size_t i = 0; i < chunk; i++) {
                if (p[i] != '\r') {
                    ssh_channel_write(g_active_session->channel, &p[i], 1);
                }
            }

            p += chunk;

            /* Apply delay between lines (not after the last chunk) */
            if (nl && *p && delay_ms > 0) {
                Sleep((DWORD)delay_ms);
            }
        }
        g_active_session->term->scrollback_offset = 0;
        InvalidateRect(hwnd, NULL, FALSE);
    }

    GlobalUnlock(hClip);
    CloseClipboard();
}

/* ---- Zoom helper --------------------------------------------------------- */

/* Reinitialise the renderer at a new font size, then resize all terminals.
 * delta is typically +1 or -1. */
static void apply_zoom(HWND hwnd, int delta)
{
    int sz = g_config->settings.font_size + delta;
    if (sz < 6)  sz = 6;
    if (sz > 72) sz = 72;
    if (sz == g_config->settings.font_size) return;

    g_config->settings.font_size = sz;

    renderer_free(&g_renderer);
    renderer_init(&g_renderer, g_config->settings.font, sz);

    /* Recalculate terminal dimensions based on new character size */
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width  = rc.right;
    int height = rc.bottom;

    if (g_renderer.charWidth > 0 && g_renderer.charHeight > 0) {
        int term_h = height - TAB_HEIGHT;
        if (term_h < 1) term_h = 1;
        int cols = width / g_renderer.charWidth;
        int rows = term_h / g_renderer.charHeight;
        if (cols < 1) cols = 1;
        if (rows < 1) rows = 1;

        Session *s = g_session_list;
        while (s) {
            if (s->term &&
                (cols != s->term->cols || rows != s->term->rows)) {
                term_resize(s->term, rows, cols);
                if (s->channel)
                    ssh_pty_resize(s->channel, cols, rows);
            }
            s = s->next;
        }
    }

    InvalidateRect(hwnd, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_config = config_load("config.json");
            if (!g_config) {
                MessageBoxA(hwnd,
                    "Could not load config.json.\n\nStarting with default settings.",
                    "Configuration Warning", MB_OK | MB_ICONWARNING);
                g_config = config_new_default();
            }

            g_hInst = ((LPCREATESTRUCT)lParam)->hInstance;
            tabs_init(g_hInst);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            g_hwndTabs = tabs_create(hwnd, 0, 0, rc.right, TAB_HEIGHT);
            tabs_set_callbacks(g_hwndTabs, on_tab_select, on_tab_new, on_tab_close, on_settings_clicked);
            
            renderer_init(&g_renderer,
                          g_config->settings.font[0]
                              ? g_config->settings.font : "Consolas",
                          g_config->settings.font_size > 0
                              ? g_config->settings.font_size : 12);
            renderer_apply_theme(hwnd, g_renderer.defaultBg);

            /* Start I/O Timer (10ms) */
            SetTimer(hwnd, 1, 10, NULL);
            
            PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0);
            return 0;

        case WM_TIMER:
            if (wParam == 1) {
                /* Poll all active sessions */
                Session *s = g_session_list;
                while (s) {
                    if (s->channel) {
                        int poll_rc = ssh_io_poll(s->channel, s->term,
                                                      s->session_log);
                        if (poll_rc > 0) {
                            InvalidateRect(hwnd, NULL, FALSE);
                        } else if (poll_rc == -2) {
                            /* EOF */
                            term_process(s->term, "\r\n[Connection Closed]\r\n", 23);
                            ssh_channel_free(s->channel);
                            s->channel = NULL;
                            int tidx = tabs_find(g_hwndTabs, s);
                            if (tidx >= 0)
                                tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    s = s->next;
                }
            }
            return 0;

        case WM_SHOW_SESSION_MANAGER:
            on_tab_new();
            return 0;

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            
            if (g_hwndTabs) {
                SetWindowPos(g_hwndTabs, NULL, 0, 0, width, TAB_HEIGHT, SWP_NOZORDER);
            }
            
            if (g_active_session && g_active_session->term && g_renderer.charWidth > 0 && g_renderer.charHeight > 0) {
                int term_h = height - TAB_HEIGHT;
                if (term_h < 1) term_h = 1;
                
                int cols = width / g_renderer.charWidth;
                int rows = term_h / g_renderer.charHeight;
                if (cols < 1) cols = 1;
                if (rows < 1) rows = 1;

                if (cols != g_active_session->term->cols || rows != g_active_session->term->rows) {
                    term_resize(g_active_session->term, rows, cols);
                    if (g_active_session->channel)
                        ssh_pty_resize(g_active_session->channel, cols, rows);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            if (g_active_session && g_active_session->term) {
                renderer_draw(&g_renderer, hdc, g_active_session->term, 0, TAB_HEIGHT, &ps.rcPaint);

                /* Fill gutter areas not covered by complete character cells.
                 * Integer division floors cols/rows, leaving a residual strip
                 * at the right and bottom that must be explicitly cleared to
                 * avoid stale pixels after a resize. */
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
                    ssh_channel_write(g_active_session->channel, &c, 1);
                } else {
                    /* Local echo if not connected (debug) */
                    // term_process(g_active_session->term, &c, 1);
                }
                g_active_session->term->scrollback_offset = 0;
                InvalidateRect(hwnd, NULL, FALSE);
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
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                        return 0;
                    case VK_NEXT:  /* Page Down */
                        if (g_active_session->term->scrollback_offset > 0) {
                            g_active_session->term->scrollback_offset--;
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                        return 0;
                }
                if (seq) {
                    if (g_active_session->channel) {
                        ssh_channel_write(g_active_session->channel, seq, strlen(seq));
                    } else {
                        // term_process(g_active_session->term, seq, strlen(seq));
                    }
                    g_active_session->term->scrollback_offset = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            /* Ctrl+Scroll zooms the font */
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                apply_zoom(hwnd, delta > 0 ? 1 : -1);
                return 0;
            }
            /* Plain scroll: move scrollback */
            if (g_active_session && g_active_session->term) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                if (delta > 0) {
                    if (g_active_session->term->scrollback_offset <
                            g_active_session->term->max_scrollback)
                        g_active_session->term->scrollback_offset++;
                } else {
                    if (g_active_session->term->scrollback_offset > 0)
                        g_active_session->term->scrollback_offset--;
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
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