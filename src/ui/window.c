#ifdef _WIN32

#include <winsock2.h>   /* Must come before windows.h */
#include "ui.h"
#include "logger.h"
#include "renderer.h"
#include "term.h"
#include "session_io.h"
#include "tabs.h"
#include "xmalloc.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include "ssh_timeout.h"
#include "resource.h"
#include "session_manager.h"
#include "settings_dlg.h"
#include "help_guide.h"
#include "ssh_session.h"
#include "ssh_pty.h"
#include "ssh_io.h"
#include "local_shell.h"
#include "local_pty.h"
#include "knownhosts.h"
#include "log_format.h"
#include "edit_scroll.h"
#include "paste_dlg.h"
#include "ai_chat.h"
#include "ai_chat_testable.h"
#include "ui_demo.h"
#include "cmd_policy.h"
#include "selection.h"
#include "app_font.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ns_draw.h"
#include "ui_theme.h"
#include "ns_tokens.h"
#include "themed_button.h"
#include "about_layout.h"
#include "ns_motion.h"
#include "ns_reduced_motion.h"
#include "custom_scrollbar.h"
#include "menubar_line.h"
#include "dpi_util.h"
#include "redraw_log.h"
#include "cmd_classify.h"
#include "cmd_detect.h"
#include "term_extract.h"
#include "key_encode.h"
#include <windowsx.h>  /* GET_X_LPARAM, GET_Y_LPARAM */
#include <dwmapi.h>
#include <gdiplus.h>       /* GDI+ flat API (includes gdiplusflat.h) */

static void hide_ai_panel(HWND parent);

static const char *CLASS_NAME = "Nutshell_Window";
static const char *APP_TITLE = "Nutshell";


#define TAB_HEIGHT_BASE 32
#define TERM_LEFT_MARGIN 6
static int g_dpi = 96;
static int g_tab_height = TAB_HEIGHT_BASE;
static int g_left_margin = TERM_LEFT_MARGIN;

#define WM_SHOW_SESSION_MANAGER (WM_USER + 1)
#define WM_CONN_DONE            (WM_USER + 2)
#define WM_STARTUP_CONNECT      (WM_USER + 3)

/* Bound on platform-detection attempts per session: counts poll ticks that
 * delivered new bytes (the transport poll returning > 0), not raw timer ticks, so a
 * quiet session doesn't burn through the budget while idle. A login banner
 * that hasn't shown up within this many data-bearing reads isn't coming --
 * give up and stay on CMD_PLATFORM_UNKNOWN (still safe: see cmd_classify's
 * unknown-platform overlay) rather than scanning every poll forever. */
#define PLATFORM_DETECT_MAX_TICKS 40

typedef enum { CONN_IDLE, CONN_CONNECTING } ConnState;

typedef struct Session {
    Terminal   *term;
    /* ssh/channel now exist only for the SSH-specific paths -- the
     * connection thread, the host-key prompt, keepalive/idle timeout and
     * bytes_read_total -- everything else goes through io. */
    SshSession *ssh;
    SSHChannel *channel;
    SessionIo   io;            /* transport vtable; io.ctx != NULL is the "connected" predicate */
    FILE       *session_log;   /* NULL when logging disabled */
    FILE       *debug_log;    /* NULL when debug_terminal disabled */
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
    /* Local sessions only: which shell the resolver picked, as the AI system
     * prompt names it ("busybox", "Git bash", "MSYS2", "custom"). Empty for
     * an SSH session, which is what the panel is told then. */
    char      shell_name[32];
    int       platform_locked;   /* profile named an explicit platform -- detection may not override it */
    int       platform_scanned;  /* detection is done (resolved via banner, or gave up after the tick bound) */
    int       platform_scan_ticks; /* data-bearing poll ticks spent scanning so far; bounds platform_scanned */
    DWORD     last_socket_data_tick;  /* GetTickCount() of last libssh2 recv() */
    DWORD     last_keepalive_tick;    /* GetTickCount() of last keepalive_send() */
    DWORD     last_user_input_tick;   /* GetTickCount() of last user activity */
    uint64_t  prev_bytes_read;        /* SshSession.bytes_read_total snapshot */
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
static CliAction g_startup_action = CLI_RUN;
static char g_startup_arg[256];
static char g_startup_demo_state[CLI_DEMO_STATE_MAX];
static char g_startup_theme[CLI_THEME_MAX];
static HWND g_hwndAiChat = NULL;
static HFONT g_hMenuFont = NULL;
static HWND g_hwndScrollbar = NULL;
static const ThemeColors *g_theme = NULL;
static ThemeTokens g_tokens; /* resolved from g_theme wherever g_theme is (re)assigned */

const ThemeTokens *ns_tokens(void) { return &g_tokens; }

static void update_scrollbar(HWND hwnd); /* forward declaration */
static void paste_cancel(void);          /* forward declaration */
static HMENU create_app_menu(void);      /* forward declaration */
static void on_ai_clicked(void);         /* forward declaration */
static void key_oneshot_clear(void);     /* forward declaration */

/* ---- Docked AI panel state ---- */
#include "ai_dock.h"
static int g_ai_docked = 1;           /* 1=docked (default), 0=floating */
static int g_ai_panel_width = 0;      /* current docked panel width in px, 0=closed */
static int g_ai_target_width = 0;     /* animation target width */
static int g_ai_last_width = 0;       /* remembered width for re-open (resets on app start) */
static int g_ai_anim_from = 0;        /* animation start width (for close: current, open: 0) */
static int g_ai_splitter_dragging = 0;
static int g_ai_reopen_after_connect = 0; /* reopen AI panel after first session connects */

/* ---- Acorn watermark state ---- */
static ULONG_PTR g_gdip_token = 0;
static GpImage  *g_acorn_image = NULL;

/* ---- Animation timer (Design-System Foundation, task 8) ----
 * One 16ms WM_TIMER for the whole main window, driving a small NsAnimList.
 * Each animated element (currently just the AI dock slide) gets an id in
 * ANIM_ID_AI_DOCK below; the timer steps every tracked animation, applies
 * whatever changed, and kills itself once the list goes empty. */
#define ANIM_TIMER_ID        4
#define ANIM_TIMER_INTERVAL 16           /* ~60fps */
enum { ANIM_ID_AI_DOCK = 0 };
static NsAnimList g_anim_list = {0};

/* Reduced-motion flag: read at startup and on WM_SETTINGCHANGE via
 * SPI_GETCLIENTAREAANIMATION. Non-zero means animations should snap
 * straight to their end state. */
static int g_reduced_motion = 0;

int ns_reduced_motion(void) { return g_reduced_motion; }

/* Read the current system "client area animation" setting into
 * g_reduced_motion. Called at WM_CREATE and on every WM_SETTINGCHANGE. */
static void refresh_reduced_motion(void)
{
    BOOL anim_enabled = TRUE;
    SystemParametersInfoA(SPI_GETCLIENTAREAANIMATION, 0, &anim_enabled, 0);
    g_reduced_motion = anim_enabled ? 0 : 1;
}

void session_mark_user_active(void) {
    if (g_active_session) {
        g_active_session->last_user_input_tick = GetTickCount();
    }
}

/* Invalidate only the terminal area (below the tab strip), not the tabs. */
static void invalidate_terminal(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.top = g_tab_height;
    REDRAW_LOG("[REDRAW] invalidate_terminal: rect=(%ld,%ld,%ld,%ld)\n",
               rc.left, rc.top, rc.right, rc.bottom);
    InvalidateRect(hwnd, &rc, FALSE);
}

/* Force a full repaint: marks every terminal row dirty AND invalidates the
 * display-buffer shadow, so the next WM_PAINT redraws every cell. Use for
 * state transitions outside WM_SIZE — tab switch, disconnect, reconnect —
 * because InvalidateRect alone is not enough: the renderer's cell-level
 * dirty check would still skip cells that match the stale shadow. */
static void force_full_terminal_repaint(HWND hwnd, Terminal *term)
{
    if (term) term_mark_all_dirty(term);
    dispbuf_invalidate(&g_renderer.dispbuf);
    invalidate_terminal(hwnd);
}

/* Bring a session's terminal grid and remote PTY up to the current client
 * area. WM_SIZE only touches the active session, so this is needed whenever a
 * session becomes visible after the window may have changed size without it:
 * on tab switch, and when a connection completes (auth can take seconds and
 * WM_SIZE skips the PTY while io.ctx is NULL). No-op when nothing changed. */
static void sync_session_grid(HWND hwnd, Session *s)
{
    if (!s || !s->term)
        return;

    /* GetClientRect reports 0x0 while the window is iconic; the helper
     * refuses to fit a grid to that and we keep the current one (see the
     * SIZE_MINIMIZED note in WM_SIZE). */
    RECT rc;
    GetClientRect(hwnd, &rc);
    int ai_w = 0;
    if (g_ai_docked && g_hwndAiChat && IsWindowVisible(g_hwndAiChat))
        ai_w = g_ai_panel_width;
    int rows, cols;
    if (!ai_dock_terminal_grid(rc.right, rc.bottom, g_tab_height,
                               ai_w, CSB_WIDTH, g_left_margin,
                               g_renderer.charWidth, g_renderer.charHeight,
                               &rows, &cols))
        return;

    if (cols != s->term->cols || rows != s->term->rows) {
        term_resize(s->term, rows, cols);
        term_mark_all_dirty(s->term);
        dispbuf_resize(&g_renderer.dispbuf, rows, cols);
        if (s->io.ctx) s->io.resize(s->io.ctx, cols, rows);
    }
}

/* Paint cooldown: cap repaints at ~60fps to prevent thrashing on heavy output */
#define PAINT_COOLDOWN_MS 16
static DWORD g_last_paint_tick;

/* Set by WM_SIZE(SIZE_MINIMIZED), cleared by the next WM_SIZE. The window
 * surface does not survive the iconic state, but the renderer's
 * display-buffer shadow does, so the restore repaint would skip every
 * "unchanged" cell and leave the terminal band blank. */
static bool g_iconic;

/* ---- Paste state machine (timer-driven, non-blocking) ------------------- */
#define PASTE_TIMER_ID 3

typedef struct {
    char       *buf;       /* malloc'd copy of clipboard text */
    char       *pos;       /* current read position within buf */
    HWND        hwnd;      /* window to repaint */
    int         delay_ms;  /* inter-line delay */
    void       *io_ctx;    /* target transport, compared by identity (paste continues across tab switches) */
    int       (*io_write)(void *ctx, const char *data, size_t len);
    bool        bracketed; /* send \033[201~ when paste completes */
    bool        local;     /* send \r for a line end: a console child reads Enter, not LF */
} PasteState;

/* Send one clipboard line to a transport, mapping line ends the way that
 * transport expects. SSH gets what it always got -- \r dropped, \n sent. A
 * local session gets \r for the line end instead, because the console child
 * on the other side of the pseudo-console reads Enter, not LF (spec section
 * 3, "Writing"). */
static void paste_chunk_write(void *ctx,
                              int (*write_fn)(void *, const char *, size_t),
                              const char *p, size_t chunk, bool local)
{
    for (size_t i = 0; i < chunk; i++) {
        if (p[i] == '\r') continue;
        if (p[i] == '\n' && local) {
            static const char CR = '\r';
            write_fn(ctx, &CR, 1);
        } else {
            write_fn(ctx, &p[i], 1);
        }
    }
}

static PasteState g_paste = {0};
static Selection g_selection = {0};

/* Set in WM_KEYDOWN when Ctrl+C (or Ctrl+Shift+C) copies a selection, so the
 * WM_CHAR that TranslateMessage still queues for 0x03 can be swallowed
 * instead of being sent to the shell as SIGINT. Same pattern as Ctrl+V's
 * 0x16, which WM_CHAR always swallows because WM_KEYDOWN always consumes it. */
static bool g_ctrlc_swallow_char = false;

static Session *create_session(int rows, int cols) {
    Session *s = xmalloc(sizeof(Session));
    s->term = term_init(rows, cols, 3000);
    s->ssh = NULL;
    s->channel = NULL;
    memset(&s->io, 0, sizeof(s->io));
    s->session_log = NULL;
    s->debug_log = NULL;
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
    cmd_batch_set_init(&s->ai_state.batches);
    /* Unconnected/pre-detect state must be the safe one, not Linux (value 0). */
    s->ai_state.platform = (int)CMD_PLATFORM_UNKNOWN;
    s->shell_name[0] = '\0';
    s->platform_locked = 0;
    s->platform_scanned = 0;
    s->platform_scan_ticks = 0;
    s->next = g_session_list;
    g_session_list = s;
    return s;
}

/* The pointer the AI panel is handed: NULL unless this session actually has
 * a transport, so "no session" reads exactly as it did when the panel was
 * given s->channel. */
static SessionIo *session_io_ptr(Session *s)
{
    return (s && s->io.ctx) ? &s->io : NULL;
}

/* Tear the transport down through the vtable and put the SSH-specific fields
 * back in sync: io.close() releases the channel and the SSH session together
 * (session_io_ssh(), src/config/ssh_io.c), so both pointers are cleared here.
 * A session that got as far as ssh_session_new() but never opened a channel
 * has no io at all; its session is freed directly. */
static void session_close_io(Session *s)
{
    if (!s) return;
    if (s->io.ctx && s->io.close) {
        s->io.close(s->io.ctx);
        s->channel = NULL;
        s->ssh     = NULL;
    }
    memset(&s->io, 0, sizeof(s->io));
    if (s->ssh) { ssh_session_free(s->ssh); s->ssh = NULL; }
    s->channel = NULL;
}

/* Drop the AI panel's pointers into this session before its transport dies.
 * The panel holds a SessionIo * into the Session struct, so it must be
 * cleared BEFORE io.close() -- spec section 2, "EOF and close order". */
static void ai_panel_detach(Session *s)
{
    if (s == g_active_session && g_hwndAiChat && IsWindow(g_hwndAiChat)) {
        ai_chat_set_session(g_hwndAiChat, NULL, NULL);
        ai_chat_set_shell_name(g_hwndAiChat, NULL);
    }
}

static void free_session(Session *s) {
    if (s) {
        key_oneshot_clear();
        if (s->conn_thread) {
            s->conn_cancelled = 1;
            WaitForSingleObject(s->conn_thread, 30000);
            CloseHandle(s->conn_thread);
        }
        DeleteCriticalSection(&s->conn_cs);  /* H-1 */
        session_close_io(s);
        if (s->session_log) fclose(s->session_log);
        if (s->debug_log)   fclose(s->debug_log);
        cmd_batch_set_free(&s->ai_state.batches);
        free(s->ai_state.stream_content);
        free(s->ai_state.stream_thinking);
        term_free(s->term);
        free(s);
    }
}

static void on_tab_select(int index, void *user_data) {
    (void)index;
    key_oneshot_clear();
    g_active_session = (Session *)user_data;
    if (g_active_session)
        g_active_session->last_user_input_tick = GetTickCount();
    HWND hParent = GetParent(g_hwndTabs);
    /* WM_SIZE only resizes the active session, so a tab that sat in the
     * background during a resize still has the old grid and PTY size.
     * Bring it up to date before painting it. */
    sync_session_grid(hParent, g_active_session);
    update_scrollbar(hParent);
    force_full_terminal_repaint(hParent,
        g_active_session ? g_active_session->term : NULL);
    SetFocus(hParent);

    /* Switch AI chat to the new session's conversation */
    if (g_hwndAiChat && IsWindow(g_hwndAiChat) && g_active_session) {
        ai_chat_switch_session(g_hwndAiChat,
                               &g_active_session->ai_state,
                               g_active_session->term,
                               session_io_ptr(g_active_session),
                               g_active_session->conn_profile.ai_notes,
                               g_config->settings.ai_system_notes,
                               g_active_session->conn_profile.name);
        ai_chat_set_shell_name(g_hwndAiChat,
                               g_active_session->shell_name[0]
                                 ? g_active_session->shell_name : NULL);
    }
}

static void on_tab_close(int index, void *user_data) {
    Session *s = (Session *)user_data;
    key_oneshot_clear();

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
        /* The panel must not keep pointers into a session about to be freed. */
        if (g_hwndAiChat && IsWindow(g_hwndAiChat)) {
            ai_chat_set_session(g_hwndAiChat, NULL, NULL);
            ai_chat_set_shell_name(g_hwndAiChat, NULL);
        }
    }

    /* Notify AI chat before freeing so it can clear dangling pointers */
    if (g_hwndAiChat && IsWindow(g_hwndAiChat))
        ai_chat_notify_session_closed(g_hwndAiChat, &s->ai_state);

    free_session(s);
    /* If s was the active tab, tabs_remove fires on_tab_select for the
     * neighbour that takes over, which re-points g_active_session at it and
     * repaints its terminal.  Closing a non-active tab leaves it untouched. */
    tabs_remove(g_hwndTabs, index);

    /* Auto-hide AI panel when no active session remains */
    if (!g_active_session)
        hide_ai_panel(GetParent(g_hwndTabs));

    /* Force repaint of terminal area after tab close */
    force_full_terminal_repaint(GetParent(g_hwndTabs),
        g_active_session ? g_active_session->term : NULL);
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
 * name falls back to hostname (then "session") when empty.
 * Returns NULL if logging_enabled is false or on any error.
 * Caller is responsible for fclose(). */
static FILE *open_session_log(const char *name, const char *hostname)
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

    /* Prefer the profile name, then the host. A local profile has no host at
     * all, so the name is what names the log (spec section 5); "session" is
     * the last resort for a profile with neither. */
    const char *safe_name = (name && name[0]) ? name
                          : ((hostname && hostname[0]) ? hostname : "session");
    time_t now = time(NULL);
    char path[MAX_PATH];
    log_format_filename(safe_name, log_dir, g_config->settings.log_format,
                        localtime(&now), path, sizeof(path));

    return fopen(path, "ab");
}

/* Open a raw debug log file for escape-sequence diagnostics.
 * Filename: <session_name>-debug-<YYYY-MM-DD_HH-MM-SS>.log in the exe dir.
 * Returns NULL if debug_terminal is false or on error. */
static FILE *open_debug_log(const char *session_name)
{
    if (!g_config || !g_config->settings.debug_terminal) return NULL;

    char dir[MAX_PATH];
    get_exe_dir(dir, sizeof(dir));
    if (dir[0] == '\0') (void)snprintf(dir, sizeof(dir), ".");

    /* Sanitise session name for use as a filename component */
    char safe_name[64];
    size_t ni = 0u;
    for (const char *p = session_name; *p && ni < sizeof(safe_name) - 1u; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            safe_name[ni++] = c;
        } else {
            safe_name[ni++] = '_';
        }
    }
    if (ni == 0u) { safe_name[0] = 's'; ni = 1u; }
    safe_name[ni] = '\0';

    time_t now = time(NULL);
    const struct tm *t = localtime(&now);
    char ts[32];
    if (t) strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", t);
    else   (void)snprintf(ts, sizeof(ts), "unknown");

    char path[MAX_PATH];
    (void)snprintf(path, sizeof(path), "%s\\%s-debug-%s.log", dir, safe_name, ts);
    return fopen(path, "wb");
}

/* ---- Local shell sessions (spec 2026-09-22-local-shell-design.md) -------- */

/* A local profile has kind "local"; everything else, including a profile
 * saved by a version that predates the key, is an SSH profile. */
static int profile_is_local(const Profile *p)
{
    return (p && strcmp(p->kind, "local") == 0) ? 1 : 0;
}

/* The three questions local_shell_resolve() may ask the machine. They live
 * here rather than in src/core/local_shell.c so that file stays free of
 * <windows.h> and testable on any host. */

static int probe_exists(void *ctx, const char *path)
{
    (void)ctx;
    if (!path || !path[0]) return 0;
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES &&
            !(attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

static int probe_env(void *ctx, const char *name, char *out, size_t out_size)
{
    (void)ctx;
    if (!name || !out || out_size == 0u) return 0;
    out[0] = '\0';
    DWORD n = GetEnvironmentVariableA(name, out, (DWORD)out_size);
    if (n == 0u || n >= (DWORD)out_size) { out[0] = '\0'; return 0; }
    return out[0] ? 1 : 0;
}

/* key is "HKLM\\SOFTWARE\\..." -- only HKLM and HKCU are understood, which
 * is all spec 4.2 asks for. Reads both the 64- and 32-bit views so a 32-bit
 * Git install is still found. */
static int probe_registry_string(void *ctx, const char *key, const char *value,
                                 char *out, size_t out_size)
{
    (void)ctx;
    if (!key || !value || !out || out_size == 0u) return 0;
    out[0] = '\0';

    HKEY root;
    const char *sub;
    if (strncmp(key, "HKLM\\", 5) == 0)      { root = HKEY_LOCAL_MACHINE; sub = key + 5; }
    else if (strncmp(key, "HKCU\\", 5) == 0) { root = HKEY_CURRENT_USER;  sub = key + 5; }
    else return 0;

    static const DWORD views[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (int i = 0; i < 2; i++) {
        HKEY h;
        if (RegOpenKeyExA(root, sub, 0,
                          KEY_QUERY_VALUE | views[i], &h) != ERROR_SUCCESS)
            continue;
        DWORD type = 0;
        DWORD len = (DWORD)out_size;
        LONG rc = RegQueryValueExA(h, value, NULL, &type,
                                   (LPBYTE)out, &len);
        RegCloseKey(h);
        if (rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
            if (len >= (DWORD)out_size) len = (DWORD)out_size - 1u;
            out[len] = '\0';
            /* RegQueryValueExA counts the NUL; trim any extra. */
            out[out_size - 1u] = '\0';
            if (out[0]) return 1;
        }
        out[0] = '\0';
    }
    return 0;
}

static void fill_local_probe(LocalShellProbe *probe, const char *exe_dir)
{
    memset(probe, 0, sizeof(*probe));
    probe->exists          = probe_exists;
    probe->env             = probe_env;
    probe->registry_string = probe_registry_string;
    probe->ctx             = NULL;
    probe->exe_dir         = exe_dir;
}

/* Hand the AI panel this session's terminal, transport and shell name in one
 * place, so the three never drift apart. NULL detaches. */
static void ai_panel_attach(Session *s)
{
    if (!g_hwndAiChat || !IsWindow(g_hwndAiChat)) return;
    ai_chat_set_session(g_hwndAiChat, s ? s->term : NULL, session_io_ptr(s));
    ai_chat_set_shell_name(g_hwndAiChat,
                           (s && s->shell_name[0]) ? s->shell_name : NULL);
}

/* Resolve the shell, spawn it and publish the transport on `s`. Everything
 * happens on the UI thread: there is no handshake and no authentication to
 * wait for, so the connection thread the SSH path needs would only add a
 * round trip. Returns 1 when the shell is running, 0 when it is not (the
 * reason is already in the terminal and the tab is DISCONNECTED).
 *
 * `tidx` is the session's tab index, or -1 if it has none. */
static int start_local_shell(HWND hwnd, Session *s, int tidx)
{
    if (!s) return 0;

    char exe_dir[MAX_PATH];
    get_exe_dir(exe_dir, sizeof(exe_dir));

    LocalShellProbe probe;
    fill_local_probe(&probe, exe_dir[0] ? exe_dir : NULL);

    /* LocalShellSpec carries a full PATH, so it is too big for the stack of
     * a thread with the Windows default reserve; the UI thread has room, but
     * the heap keeps it honest either way. */
    LocalShellSpec *spec = (LocalShellSpec *)calloc(1u, sizeof(*spec));
    if (!spec) {
        term_process(s->term, "\r\nOut of memory starting the local shell.\r\n", 43);
        if (tidx >= 0) tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
        return 0;
    }

    LocalShellKind kind = local_shell_resolve(s->conn_profile.shell, &probe, spec);

    char err[512];
    err[0] = '\0';
    LocalPty *pty = NULL;
    if (kind != SHELL_NONE) {
        int cols = (s->term && s->term->cols > 0) ? s->term->cols : 80;
        int rows = (s->term && s->term->rows > 0) ? s->term->rows : 24;
        pty = local_pty_open(spec, cols, rows, err, sizeof(err));
    } else {
        (void)snprintf(err, sizeof(err), "%s",
                       spec->error[0] ? spec->error : LOCAL_SHELL_NONE_MESSAGE);
    }

    if (!pty) {
        char msg[600];
        int n = snprintf(msg, sizeof(msg), "\r\n%s\r\n",
                         err[0] ? err : "Could not start the local shell.");
        if (n > 0) term_process(s->term, msg, strlen(msg));
        if (tidx >= 0) tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
        free(spec);
        return 0;
    }

    s->io = session_io_local(pty);

    /* The shell's name, for the AI system prompt (spec section 6). */
    {
        const char *name = local_shell_kind_name(kind);
        (void)snprintf(s->shell_name, sizeof(s->shell_name), "%s",
                       name ? name : "");
    }

    /* Platform. An explicit profile setting always wins (on_session_connect
     * already locked it). Otherwise a known POSIX-ish shell pins Linux with
     * no banner scan -- there is no login banner to scan; a custom command
     * could be anything, so it stays on `auto` and the scan runs. */
    if (!s->platform_locked && local_shell_kind_is_posix(kind)) {
        s->ai_state.platform  = (int)CMD_PLATFORM_LINUX;
        s->platform_locked    = 1;
        s->platform_scanned   = 1;
    }

    DWORD tick_now = GetTickCount();
    s->last_socket_data_tick = tick_now;
    s->last_keepalive_tick   = tick_now;
    s->last_user_input_tick  = tick_now;
    s->conn_state            = CONN_IDLE;

    if (!s->session_log) {
        /* Host is empty for a local profile, so the log is named after the
         * profile (spec section 5). */
        s->session_log = open_session_log(s->conn_profile.name, "local");
    }
    s->debug_log = open_debug_log(s->conn_profile.name[0]
                                    ? s->conn_profile.name : "local");

    if (tidx >= 0) {
        char user[256], machine[256];
        if (!probe_env(NULL, "USERNAME", user, sizeof(user)))
            (void)snprintf(user, sizeof(user), "%s", "local");
        if (!probe_env(NULL, "COMPUTERNAME", machine, sizeof(machine)))
            (void)snprintf(machine, sizeof(machine), "%s", "this PC");
        tabs_set_connect_info(g_hwndTabs, tidx, user, machine,
                              (unsigned long long)GetTickCount64());
        tabs_set_status(g_hwndTabs, tidx, TAB_CONNECTED);
        tabs_set_logging(g_hwndTabs, tidx, s->session_log ? 1 : 0);
    }

    if (s == g_active_session) ai_panel_attach(s);

    sync_session_grid(hwnd, s);
    free(spec);
    return 1;
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

    /* An explicit profile setting always wins: lock the platform so the
     * poll-site detector (below) never overwrites what the operator chose.
     * "auto" (and any unrecognised token) maps to CMD_PLATFORM_UNKNOWN,
     * which leaves the session unlocked and awaiting detection. */
    s->ai_state.platform = (int)cmd_platform_from_name(s->conn_profile.platform);
    s->platform_locked   = (s->ai_state.platform != (int)CMD_PLATFORM_UNKNOWN);
    s->platform_scanned  = 0;
    s->platform_scan_ticks = 0;

    int is_local = profile_is_local(info);

    /* A local shell has nothing to connect to, so it never shows the
     * "Connecting..." animation -- start_local_shell() below either has a
     * running shell a moment later or an error line in the terminal. */
    if (!is_local)
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

    if (is_local) {
        HWND parent = GetParent(g_hwndTabs);
        s->conn_state = CONN_IDLE;
        if (start_local_shell(parent, s, idx)) {
            /* Reopen the AI panel that was closed for the first session --
             * the SSH path does this in WM_CONN_DONE, which a local session
             * never reaches. */
            if (g_ai_reopen_after_connect && s == g_active_session) {
                g_ai_reopen_after_connect = 0;
                on_ai_clicked();
            }
        }
        update_scrollbar(parent);
        force_full_terminal_repaint(parent, s->term);
        return;
    }

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

static void apply_config_colors(void)
{
    /* g_theme is resolved in WM_CREATE before this window is ever painted
     * or apply_config_colors() is ever called (Design-System Foundation,
     * task 10) -- always use the theme's terminal colours, no raw-hex
     * config fallback needed. */
    unsigned int tfg = g_theme->terminal_fg;
    unsigned int tbg = g_theme->terminal_bg;
    g_renderer.defaultFg = RGB((tfg >> 16) & 0xFF, (tfg >> 8) & 0xFF, tfg & 0xFF);
    g_renderer.defaultBg = RGB((tbg >> 16) & 0xFF, (tbg >> 8) & 0xFF, tbg & 0xFF);
}

/* initial_page: a SETTINGS_PAGE_* to open Settings on, or -1 for the
 * default page (see settings_dlg_show()). Lets the AI Assist panel's
 * no-key state's "Open Settings" button land on Provider. */
static void on_settings_clicked_page(int initial_page) {
    HWND parent = GetParent(g_hwndTabs);
    settings_dlg_show(parent, g_config, initial_page);

    /* Reload theme from config (colour scheme may have changed) */
    {
        int idx = ui_theme_find(g_config->settings.colour_scheme);
        g_theme = ui_theme_get(idx);
        ui_theme_resolve(g_theme, &g_tokens);
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

    /* The terminal font/size feeds ns_font's FONT_MONO face too. This only
     * flushes the cache if something actually changed; either way, refetch
     * everything that keeps a font handle cached across paints below. */
    ns_font_set_faces(APP_FONT_UI_FACE, g_config->settings.font,
                      g_config->settings.font_size);
    g_hMenuFont = ns_font(FONT_BODY, g_dpi);
    if (g_hwndAiChat && IsWindow(g_hwndAiChat))
        ai_chat_refresh_fonts(g_hwndAiChat);

    /* Update tab strip font (may have changed) */
    tabs_set_font(g_hwndTabs, g_config->settings.font, 0);

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
        ai_chat_set_markdown(g_hwndAiChat,
                             g_config->settings.markdown_render_enabled);
        ai_chat_set_policy_default(g_hwndAiChat,
                                   g_config->settings.ai_policy_default);
        ai_chat_set_theme(g_hwndAiChat,
                          g_config->settings.colour_scheme);
        ai_chat_update_tools(g_hwndAiChat,
                             g_config->settings.ai_search_provider,
                             g_config->settings.ai_search_url,
                             g_config->settings.ai_max_search_results,
                             g_config->settings.ai_web_fetch_enabled);
        ai_chat_set_context_lines(g_hwndAiChat,
                                  g_config->settings.ai_max_context_lines);
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

static void on_settings_clicked(void) {
    on_settings_clicked_page(-1);
}

/* Start the slide animation for the docked AI panel.
 * Animates from the current panel width to target_w. */
static void start_ai_panel_anim(HWND hwnd, int target_w)
{
    g_ai_anim_from = g_ai_panel_width;
    g_ai_target_width = target_w;
    ns_anim_list_add(&g_anim_list, ANIM_ID_AI_DOCK,
                      (unsigned long)GetTickCount(), MOTION_BASE);
    SetTimer(hwnd, ANIM_TIMER_ID, ANIM_TIMER_INTERVAL, NULL);
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
    HWND hwnd = ai_chat_show(parent,
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
    if (hwnd) {
        ai_chat_update_tools(hwnd,
                             g_config->settings.ai_search_provider,
                             g_config->settings.ai_search_url,
                             g_config->settings.ai_max_search_results,
                             g_config->settings.ai_web_fetch_enabled);
        ai_chat_set_markdown(hwnd, g_config->settings.markdown_render_enabled);
        ai_chat_set_context_lines(hwnd, g_config->settings.ai_max_context_lines);
        ai_chat_set_policy_default(hwnd, g_config->settings.ai_policy_default);
    }
    return hwnd;
}

/* --ui-demo: build a fake "demo" tab with canned terminal output and an AI
 * panel populated from a fixed script -- no SSH, no network, no API key.
 * (Design-System Foundation, spec section 5 / plan task 9.) */
static void create_demo_session(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int term_w = rc.right - g_left_margin - CSB_WIDTH;
    int term_h = rc.bottom - g_tab_height;
    if (term_h < 1) term_h = 1;
    int cols = 80, rows = 24;
    if (g_renderer.charWidth > 0 && g_renderer.charHeight > 0) {
        cols = term_w / g_renderer.charWidth;
        rows = term_h / g_renderer.charHeight;
    }

    const char *state = g_startup_demo_state[0] ? g_startup_demo_state : "all";
    int demo_local = (strcmp(state, "local") == 0);

    Session *s = create_session(rows, cols);
    memset(&s->conn_profile, 0, sizeof(s->conn_profile));
    snprintf(s->conn_profile.name, sizeof(s->conn_profile.name), "%s",
             demo_local ? "Local shell" : "demo");
    if (demo_local) {
        snprintf(s->conn_profile.kind, sizeof(s->conn_profile.kind), "local");
        snprintf(s->shell_name, sizeof(s->shell_name), "busybox");
    }
    /* s->channel / s->ssh are already NULL from create_session -- no
     * connection exists or ever will for this tab, local demo included:
     * the "local" state is tab chrome and a status line, no process
     * (spec section 5). */

    char term_buf[8192];
    ApprovalQueue demo_approval, demo_approval2;
    if (ui_demo_build(state, &s->ai_state.conv, &demo_approval, &demo_approval2,
                      term_buf, sizeof(term_buf)) != 0) {
        /* Unreachable in practice -- cli_parse already validated the
         * state -- but never launch with a half-built demo. */
        state = "all";
        ui_demo_build(state, &s->ai_state.conv, &demo_approval, &demo_approval2,
                      term_buf, sizeof(term_buf));
    }
    s->ai_state.valid = 1;

    /* Give the demo a policy with both markers off their defaults, so the
     * gallery actually covers the status-line control's three cell states
     * (unattended / allowed-but-asks / blocked) and a partly-filled rail
     * rather than four identical screenshots of {read, none}. The demo's
     * approval cards are static data built by ui_demo_build() against their
     * own queue policies, so this only changes what the control paints. */
    cmd_policy_set_allowed(&s->ai_state.policy, CMD_WRITE);
    cmd_policy_set_unattended(&s->ai_state.policy, CMD_READ);
    s->ai_state.policy_seeded = 1;

    term_process(s->term, term_buf, strlen(term_buf));

    int idx = tabs_add(g_hwndTabs, demo_local ? "Local shell" : "demo", s);
    if (idx < 0) {
        if (g_session_list == s) g_session_list = s->next;
        free_session(s);
        return;
    }
    tabs_set_active(g_hwndTabs, idx);        /* -> on_tab_select: g_active_session = s */
    if (demo_local) {
        /* The local demo shows the chrome a live local session has: a
         * CONNECTED dot and a user@machine status line, from the real
         * environment, with no process behind it. */
        char user[256], machine[256];
        if (!probe_env(NULL, "USERNAME", user, sizeof(user)))
            (void)snprintf(user, sizeof(user), "%s", "local");
        if (!probe_env(NULL, "COMPUTERNAME", machine, sizeof(machine)))
            (void)snprintf(machine, sizeof(machine), "%s", "this PC");
        tabs_set_connect_info(g_hwndTabs, idx, user, machine,
                              (unsigned long long)GetTickCount64());
        tabs_set_status(g_hwndTabs, idx, TAB_CONNECTED);
    } else {
        tabs_set_status(g_hwndTabs, idx, TAB_IDLE); /* neutral -- never connects */
    }
    invalidate_terminal(hwnd);

    /* --theme <name>: apply only if it names a real theme; an unknown
     * name silently keeps whatever the config already selected. */
    if (g_startup_theme[0]) {
        for (int i = 0; i < NUM_UI_THEMES; i++) {
            if (strcmp(ui_theme_name(i), g_startup_theme) == 0) {
                g_theme = ui_theme_get(i);
                ui_theme_resolve(g_theme, &g_tokens);
                /* create_ai_chat() looks the theme up again from the config
                 * string (colour_scheme), not from g_theme -- update it too
                 * so the AI panel matches the terminal instead of staying
                 * on whatever the config file had. */
                snprintf(g_config->settings.colour_scheme,
                        sizeof(g_config->settings.colour_scheme),
                        "%s", ui_theme_name(i));
                tabs_set_theme(g_hwndTabs, g_theme);
                if (g_hwndScrollbar) csb_set_theme(g_hwndScrollbar, g_theme);
                apply_config_colors();
                renderer_apply_theme(hwnd, g_renderer.defaultBg);
                HMENU oldMenu = GetMenu(hwnd);
                SetMenu(hwnd, create_app_menu());
                if (oldMenu) DestroyMenu(oldMenu);
                DrawMenuBar(hwnd);
                break;
            }
        }
    }

    /* Open the AI panel docked -- bypassing on_ai_clicked()'s "no active
     * SSH session" and "no AI API key" gates entirely, since demo mode has
     * neither and needs neither. */
    if (!g_hwndAiChat) {
        g_hwndAiChat = create_ai_chat(hwnd);
    } else {
        ai_chat_switch_session(g_hwndAiChat, &s->ai_state, s->term, NULL,
                               NULL, NULL, "demo");
    }
    if (g_hwndAiChat) {
        ai_chat_apply_demo_extras(g_hwndAiChat, state, &demo_approval, &demo_approval2);

        if (g_ai_docked) {
            RECT rc2;
            GetClientRect(hwnd, &rc2);
            int target = ai_dock_pct_to_px(rc2.right, AI_DOCK_DEFAULT_PCT,
                                           AI_DOCK_MIN_PCT, AI_DOCK_MAX_PCT);
            int splitter = AI_DOCK_SPLITTER_W;
            SetWindowPos(g_hwndAiChat, NULL,
                rc2.right - target + splitter, g_tab_height,
                target - splitter, rc2.bottom - g_tab_height, SWP_NOZORDER);
            g_ai_panel_width = target;
            g_ai_target_width = target;
            ShowWindow(g_hwndAiChat, SW_SHOW);
        } else {
            ShowWindow(g_hwndAiChat, SW_SHOW);
        }
    }

    relayout_main(hwnd);
    force_full_terminal_repaint(hwnd, s->term);
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

    /* The panel always opens now, session or API key or neither -- it
     * shows the empty/no-key/no-session state itself (ai_panel_states.h,
     * decided in ai_chat.c's update_panel_state()). No MessageBox gate
     * here any more (AI Assist Panel task 4). */
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

    g_hwndAiChat = create_ai_chat(parent);

    /* Set the active session if one exists */
    if (g_hwndAiChat && g_active_session) {
        ai_panel_attach(g_active_session);
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
            if (g_paste.io_ctx == s->io.ctx)
                paste_cancel();
            term_process(s->term, "\r\n[Disconnected by user]\r\n", 25);
            ai_panel_detach(s);
            session_close_io(s);
            if (s->session_log) { fclose(s->session_log); s->session_log = NULL; }
            if (s->debug_log)   { fclose(s->debug_log);   s->debug_log   = NULL; }
            int tidx = tabs_find(g_hwndTabs, s);
            if (tidx >= 0) {
                tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
                tabs_set_logging(g_hwndTabs, tidx, 0);
            }
            force_full_terminal_repaint(hParent, s->term);
        }
    } else if (status == TAB_DISCONNECTED) {
        /* Attempt reconnect using stored profile */
        int is_local = profile_is_local(&s->conn_profile);
        if (!is_local && s->conn_profile.host[0] == '\0') {
            MessageBoxA(hParent,
                        "No connection profile available for reconnection.",
                        "Reconnect", MB_OK | MB_ICONINFORMATION);
            return;
        }
        /* Clean up any leftover transport state */
        session_close_io(s);
        if (s->session_log) { fclose(s->session_log); s->session_log = NULL; }
        if (s->debug_log)   { fclose(s->debug_log);   s->debug_log   = NULL; }

        if (!is_local) {
            /* Re-populate password from config — it was zeroed after first auth */
            for (size_t i = 0; i < vec_size(&g_config->profiles); i++) {
                const Profile *pr = (const Profile *)vec_get(&g_config->profiles, i);
                if (strcmp(pr->kind, s->conn_profile.kind) == 0 &&
                    strcmp(pr->host, s->conn_profile.host) == 0 &&
                    strcmp(pr->username, s->conn_profile.username) == 0 &&
                    pr->port == s->conn_profile.port) {
                    memcpy(s->conn_profile.password, pr->password,
                           sizeof(s->conn_profile.password));
                    break;
                }
            }
        }

        int tidx = tabs_find(g_hwndTabs, s);

        /* A local profile re-spawns its shell instead of launching the
         * connection thread (spec section 2, "Reconnect"). */
        if (is_local) {
            s->conn_state = CONN_IDLE;
            s->conn_error[0] = '\0';
            s->shell_name[0] = '\0';
            term_process(s->term, "\r\n", 2);
            start_local_shell(hParent, s, tidx);
            update_scrollbar(hParent);
            force_full_terminal_repaint(hParent, s->term);
            return;
        }

        term_process(s->term, "\r\nReconnecting", 14);

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
        force_full_terminal_repaint(hParent, s->term);
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
        const char *name = s->conn_profile.name[0]
                            ? s->conn_profile.name : s->conn_profile.host;
        time_t now = time(NULL);
        char path[512];
        log_format_filename(name, dir,
                            g_config ? g_config->settings.log_format : NULL,
                            localtime(&now), path, sizeof(path));
        s->session_log = fopen(path, "ab");
        if (s->session_log) {
            tabs_set_logging(g_hwndTabs, tidx, 1);
        }
    }
    (void)index;
}

/* ---- Paste helper -------------------------------------------------------- */

/* Send clipboard text to the active session. Shows a preview dialog first
 * when settings.paste_confirm is on. Multi-line content is sent one line
 * at a time, separated by paste_delay_ms milliseconds via a non-blocking
 * WM_TIMER, so the UI stays responsive. */

/* Send one line from g_paste.pos, advance pos past the '\n'.
 * Returns true if there is more data to send. */
static bool paste_send_next_line(void)
{
    if (!g_paste.buf || !g_paste.pos || !*g_paste.pos) return false;
    if (!g_paste.io_ctx || !g_paste.io_write) return false;

    const char *p = g_paste.pos;
    const char *nl = strchr(p, '\n');
    size_t chunk = nl ? (size_t)(nl - p) + 1u : strlen(p);

    paste_chunk_write(g_paste.io_ctx, g_paste.io_write, p, chunk, g_paste.local);

    g_paste.pos += chunk;
    return *g_paste.pos != '\0';
}

/* Cancel any in-progress paste and free state (no bracket-close sent) */
static void paste_cancel(void)
{
    if (g_paste.buf) {
        KillTimer(g_paste.hwnd, PASTE_TIMER_ID);
        free(g_paste.buf);
        g_paste.buf      = NULL;
        g_paste.pos      = NULL;
        g_paste.io_ctx   = NULL;
        g_paste.io_write = NULL;
        g_paste.bracketed = false;
        g_paste.local     = false;
    }
}

/* Called when paste completes naturally — sends bracket-close if needed, then cleans up */
static void paste_finish(void)
{
    static const char BRACKET_CLOSE[] = "\033[201~";
    if (g_paste.bracketed && g_paste.io_ctx)
        g_paste.io_write(g_paste.io_ctx, BRACKET_CLOSE, sizeof(BRACKET_CLOSE) - 1);
    paste_cancel();
}

/* Called by WM_TIMER when wParam == PASTE_TIMER_ID */
static void paste_timer_tick(void)
{
    session_mark_user_active();
    bool more = paste_send_next_line();
    if (g_active_session && g_active_session->term) {
        g_active_session->term->scrollback_offset = 0;
        invalidate_terminal(g_paste.hwnd);
    }
    if (!more) paste_finish();
}

static void do_paste(HWND hwnd)
{
    if (!g_active_session || !g_active_session->io.ctx) return;
    session_mark_user_active();

    /* Cancel any in-progress paste */
    paste_cancel();

    if (!IsClipboardFormatAvailable(CF_TEXT)) return;
    if (!OpenClipboard(hwnd)) return;

    HANDLE hClip = GetClipboardData(CF_TEXT);
    if (!hClip) { CloseClipboard(); return; }

    const char *raw = (const char *)GlobalLock(hClip);
    if (!raw) { CloseClipboard(); return; }

    /* Copy to a local buffer so the clipboard is free before the dialog blocks.
     * The user must be able to copy new text while the preview is open. */
    char *local = _strdup(raw);
    GlobalUnlock(hClip);
    CloseClipboard();

    if (!local) return;

    /* Count newlines */
    int line_count = 0;
    for (size_t i = 0; local[i]; i++) {
        if (local[i] == '\n') line_count++;
    }

    /* Ask for confirmation before pasting, unless disabled in settings */
    int confirmed = 1;
    if (g_config->settings.paste_confirm) {
        confirmed = paste_preview_show(hwnd, local,
            g_config->settings.foreground_colour,
            g_config->settings.background_colour,
            g_config->settings.font,
            g_config->settings.font_size,
            g_config->settings.colour_scheme);
    }

    if (!confirmed) {
        free(local);
        return;
    }

    bool bpm = g_active_session->term &&
               g_active_session->term->bracketed_paste_mode;
    bool local_line_ends = (g_active_session->io.kind == SESSION_LOCAL);
    int  delay_ms = g_config ? g_config->settings.paste_delay_ms : 0;

    static const char BRACKET_OPEN[]  = "\033[200~";
    static const char BRACKET_CLOSE[] = "\033[201~";

    /* Bracketed paste mode: send the whole block at once — the remote app
     * receives it as a unit and handles newlines itself, so inter-line delay
     * would only add pointless latency. */
    if (line_count == 0 || delay_ms <= 0 || bpm) {
        if (bpm)
            g_active_session->io.write(g_active_session->io.ctx,
                              BRACKET_OPEN, sizeof(BRACKET_OPEN) - 1);
        const char *p = local;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t chunk = nl ? (size_t)(nl - p) + 1u : strlen(p);
            paste_chunk_write(g_active_session->io.ctx,
                              g_active_session->io.write,
                              p, chunk, local_line_ends);
            p += chunk;
        }
        if (bpm)
            g_active_session->io.write(g_active_session->io.ctx,
                              BRACKET_CLOSE, sizeof(BRACKET_CLOSE) - 1);
        g_active_session->term->scrollback_offset = 0;
        invalidate_terminal(hwnd);
        free(local);
    } else {
        /* Multi-line with delay: use timer-driven paste (takes ownership of local) */
        g_paste.buf       = local;
        g_paste.pos       = g_paste.buf;
        g_paste.hwnd      = hwnd;
        g_paste.delay_ms  = delay_ms;
        g_paste.io_ctx    = g_active_session->io.ctx;
        g_paste.io_write  = g_active_session->io.write;
        g_paste.bracketed = bpm;
        g_paste.local     = local_line_ends;

        if (bpm)
            g_active_session->io.write(g_active_session->io.ctx,
                              BRACKET_OPEN, sizeof(BRACKET_OPEN) - 1);

        /* Send the first line immediately */
        paste_send_next_line();
        g_active_session->term->scrollback_offset = 0;
        invalidate_terminal(hwnd);

        /* Start timer for remaining lines, or finish if buffer exhausted */
        if (g_paste.pos && *g_paste.pos) {
            SetTimer(hwnd, PASTE_TIMER_ID, (UINT)delay_ms, NULL);
        } else {
            paste_finish();
        }
    }
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
    int    nested;       /* 1 = a submenu inside a drop-down, not on the bar */
} MenuItemData;

/* Small pool of MenuItemData — never freed (lives for app lifetime) */
#define MAX_MENU_ITEMS 64
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
    mi->nested = 0;
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
    mi->nested = 0;
    mi->text[0] = '\0';
    mi->accel[0] = '\0';
    AppendMenu(menu, MF_OWNERDRAW | MF_SEPARATOR, 0, (LPCSTR)mi);
}

static MenuItemData *menu_add_popup(HMENU bar, HMENU sub, const char *label)
{
    MenuItemData *mi = alloc_menu_item();
    mi->id   = 0;
    mi->hSub = sub;
    mi->is_separator = 0;
    mi->nested = 0;
    mi->accel[0] = '\0';
    (void)snprintf(mi->text, sizeof(mi->text), "%s", label);
    AppendMenu(bar, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)sub, (LPCSTR)mi);
    return mi;
}

/* A submenu inside a drop-down (Edit > Send Key): measured like an item,
 * with room for the arrow Windows draws, not like a menu-bar title. */
static void menu_add_submenu(HMENU menu, HMENU sub, const char *label)
{
    menu_add_popup(menu, sub, label)->nested = 1;
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
    menu_add_item(hFile, IDM_FILE_NEW_SESSION, "New Session\tCtrl+Shift+T");
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
    menu_add_item(hEdit, IDM_EDIT_SELECT_ALL, "Select All");
    /* Send Key: the keys Nutshell or Windows keep for themselves, and
     * the function row a compact keyboard lacks (special-keys spec,
     * section 4A). */
    HMENU hSendKey = CreatePopupMenu();
    for (int f = 0; f < 12; f++) {
        char label[8];
        (void)snprintf(label, sizeof(label), "F%d", f + 1);
        menu_add_item(hSendKey, (UINT)(IDM_SENDKEY_F1 + f), label);
    }
    menu_add_separator(hSendKey);
    menu_add_item(hSendKey, IDM_SENDKEY_PGUP, "Page Up");
    menu_add_item(hSendKey, IDM_SENDKEY_PGDN, "Page Down");
    menu_add_separator(hSendKey);
    menu_add_item(hSendKey, IDM_SENDKEY_CTRL_V, "Ctrl+V");
    menu_add_item(hSendKey, IDM_SENDKEY_SHIFT_INSERT, "Shift+Insert");
    menu_add_item(hSendKey, IDM_SENDKEY_CTRL_EQUALS, "Ctrl+=");
    menu_add_item(hSendKey, IDM_SENDKEY_CTRL_MINUS, "Ctrl+-");
    menu_add_separator(hSendKey);
    menu_add_item(hSendKey, IDM_SENDKEY_RAW_NEXT, "Send Next Key Raw");
    menu_add_item(hSendKey, IDM_SENDKEY_ALT_NEXT, "Send Next Key with Alt");
    menu_add_submenu(hEdit, hSendKey, "Send Key");
    menu_add_separator(hEdit);
    menu_add_item(hEdit, IDM_EDIT_SETTINGS, "Settings...");
    menu_add_popup(hMenu, hEdit, "Edit");

    HMENU hView = CreatePopupMenu();
    menu_add_item(hView, IDM_VIEW_AI_CHAT, "AI Assist\tCtrl+Shift+Space");
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
        menu_set_bg(hSendKey, bg);
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

/* Copy current selection to clipboard, then clear the highlight and force a
 * repaint (Ctrl+C / Ctrl+Shift+C). No-op if there is no selection. */
static void copy_selection_and_clear(HWND hwnd)
{
    if (!g_selection.valid) return;
    do_copy(hwnd);
    g_selection.valid = false;
    /* Force a full repaint so the highlighted cells are redrawn without the
     * highlight — the dirty-row optimisation would otherwise skip them. */
    if (g_active_session && g_active_session->term)
        term_mark_all_dirty(g_active_session->term);
    invalidate_terminal(hwnd);
}

/* ---- Custom About dialog with acorn icon ---- */

#define ABOUT_CLASS     "Nutshell_About"
#define ABOUT_TAGLINE   "Windows SSH terminal with built-in AI assistance."
#define ABOUT_COPYRIGHT "Copyright \xA9 2026 Thomas Sulkiewicz"

/* Pixel height of one row of `role` text: the ramp's pixel size times its
 * line height, both from ns_type.h. */
static int about_line_h(NsFontRole role, int dpi)
{
    const NsFontSpec *spec = ns_type_font(role);
    double px = (double)ns_type_font_px(role, dpi, 0) * spec->line_height;
    return (int)(px + 0.5);
}

/* The About window's content stack, measured from the type ramp. */
static void about_compute_layout(AboutLayout *out, int dpi)
{
    about_layout_compute(out, dpi, ns_scale(ABOUT_ICON_96, dpi),
                         about_line_h(FONT_HEADING, dpi),
                         about_line_h(FONT_BODY, dpi),
                         about_line_h(FONT_CAPTION, dpi));
}

/* Draw one centred text row of the About stack. The row spans the real
 * client width so the text stays centred whatever the window ended up. */
static void about_draw_row(HDC hdc, const AboutRect *row, int client_w,
                           NsFontRole role, unsigned int colour, int dpi,
                           const char *text)
{
    RECT rc = { 0, row->y, client_w, row->y + row->h };
    HFONT font = ns_font(role, dpi);
    HGDIOBJ old = font ? SelectObject(hdc, (HGDIOBJ)font) : NULL;
    SetTextColor(hdc, theme_cr(colour));
    DrawTextA(hdc, text, -1, &rc,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
}

/* Centre the OK button on the current client width, at the layout's y. */
static void about_place_button(HWND hwnd)
{
    HWND hBtn = GetDlgItem(hwnd, IDOK);
    if (!hBtn) return;
    AboutLayout lay;
    about_compute_layout(&lay, get_window_dpi(hwnd));
    RECT cr;
    GetClientRect(hwnd, &cr);
    MoveWindow(hBtn, (cr.right - lay.button.w) / 2, lay.button.y,
               lay.button.w, lay.button.h, TRUE);
}

static LRESULT CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            int dpi = get_window_dpi(hwnd);
            /* Themed owner-draw button; WM_SIZE does the placing. */
            HWND hBtn = CreateWindowA("BUTTON", "OK",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                          0, 0, 10, 10, hwnd, (HMENU)IDOK, g_hInst, NULL);
            if (hBtn) {
                HFONT font = ns_font(FONT_BODY, dpi);
                if (font)
                    SendMessage(hBtn, WM_SETFONT, (WPARAM)font, TRUE);
            }
            about_place_button(hwnd);
            if (g_theme)
                themed_apply_title_bar(hwnd, g_theme);
            return 0;
        }
        case WM_SIZE:
            about_place_button(hwnd);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int dpi = get_window_dpi(hwnd);
            const ThemeTokens *tk = ns_tokens();
            AboutLayout lay;
            about_compute_layout(&lay, dpi);

            /* Background */
            HBRUSH bgBrush = CreateSolidBrush(theme_cr(tk->bg_primary.base));
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);

            /* Acorn icon, centred at the top */
            HICON hIcon = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_APPICON),
                                           IMAGE_ICON, lay.icon.w, lay.icon.h,
                                           LR_DEFAULTCOLOR);
            if (hIcon) {
                DrawIconEx(hdc, (rc.right - lay.icon.w) / 2, lay.icon.y,
                           hIcon, lay.icon.w, lay.icon.h, 0, NULL, DI_NORMAL);
                DestroyIcon(hIcon);
            }

            /* Title, tagline, copyright */
            SetBkMode(hdc, TRANSPARENT);
            about_draw_row(hdc, &lay.title, rc.right, FONT_HEADING,
                           tk->text_main, dpi, "Nutshell v" APP_VERSION);
            about_draw_row(hdc, &lay.tagline, rc.right, FONT_BODY,
                           tk->text_main, dpi, ABOUT_TAGLINE);
            about_draw_row(hdc, &lay.copyright, rc.right, FONT_CAPTION,
                           tk->text_dim, dpi, ABOUT_COPYRIGHT);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (g_theme && dis && (int)dis->CtlID == IDOK) {
                draw_themed_button(dis, g_theme, 1);
                return TRUE;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
                DestroyWindow(hwnd);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_RETURN)
                DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            EnableWindow(GetParent(hwnd), TRUE);
            SetForegroundWindow(GetParent(hwnd));
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

static void show_about_dialog(HWND parent) {
    static int registered = 0;
    if (!registered) {
        WNDCLASSEXA wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AboutDlgProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = ABOUT_CLASS;
        RegisterClassExA(&wc);
        registered = 1;
    }

    /* Size the window from its content, not a magic constant. */
    int dpi = get_window_dpi(parent);
    AboutLayout lay;
    about_compute_layout(&lay, dpi);

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT wr = { 0, 0, lay.client_w, lay.client_h };
    AdjustWindowRectEx(&wr, style, FALSE, WS_EX_DLGMODALFRAME);
    int dlgW = wr.right - wr.left;
    int dlgH = wr.bottom - wr.top;

    RECT parentRc;
    GetWindowRect(parent, &parentRc);
    int x = parentRc.left + (parentRc.right - parentRc.left - dlgW) / 2;
    int y = parentRc.top + (parentRc.bottom - parentRc.top - dlgH) / 2;

    HWND dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME, ABOUT_CLASS, "About Nutshell",
        style | WS_VISIBLE,
        x, y, dlgW, dlgH,
        parent, NULL, g_hInst, NULL);
    if (!dlg) return;

    /* AdjustWindowRectEx sizes the caption at the system DPI; on a
     * per-monitor-DPI window that can be off by a few pixels, so trim the
     * window until the client area is exactly the computed layout. */
    {
        RECT cr, outer;
        GetClientRect(dlg, &cr);
        GetWindowRect(dlg, &outer);
        int dw = lay.client_w - cr.right;
        int dh = lay.client_h - cr.bottom;
        if (dw != 0 || dh != 0)
            SetWindowPos(dlg, NULL, x - dw / 2, y,
                         (outer.right - outer.left) + dw,
                         (outer.bottom - outer.top) + dh,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    }

    EnableWindow(parent, FALSE);

    /* Focus the OK button now the popup is shown and activated -- doing it
     * in WM_CREATE would be undone by the activation that follows. */
    {
        HWND hBtn = GetDlgItem(dlg, IDOK);
        if (hBtn) SetFocus(hBtn);
    }

    MSG m;
    while (IsWindow(dlg) && GetMessage(&m, NULL, 0, 0) > 0) {
        /* Enter/Escape close the dialog wherever the focus is: once the OK
         * button has focus the key goes to the button, not here, and
         * IsDialogMessage's default-button handling never reaches an
         * owner-draw button. */
        if (m.message == WM_KEYDOWN &&
            (m.wParam == VK_ESCAPE || m.wParam == VK_RETURN) &&
            (m.hwnd == dlg || IsChild(dlg, m.hwnd))) {
            DestroyWindow(dlg);
            continue;
        }
        if (!IsDialogMessage(dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
}

/* ---- Keyboard: what each key sends (special-keys spec, 2026-09-23) -------
 *
 * The bytes come from key_encode() (src/core/key_encode.c, xterm's
 * encoding); this block only maps Win32 onto it and decides which message
 * handles which key:
 *
 *   - keys that never produce a character (arrows, Home, End, Insert,
 *     Delete, PgUp, PgDn, F1-F12) are encoded from WM_KEYDOWN /
 *     WM_SYSKEYDOWN;
 *   - keys that do are encoded from WM_CHAR / WM_SYSCHAR, where Windows has
 *     already applied the layout, AltGr, dead keys and the IME;
 *   - a key-down that is handled here and must not be followed by its
 *     character (Backspace, Shift+Tab, Ctrl+Shift+W/T/Space) removes that
 *     character from the queue: ui_run() calls TranslateMessage before
 *     DispatchMessage, so by the time WndProc sees the key-down the WM_CHAR
 *     is already queued and returning 0 does not stop it. A dead-key layout
 *     can queue two characters off one key-down (the accent, then the
 *     key's own character), both sharing that key-down's scan code, so
 *     drop_queued_char() loops rather than dropping just the first;
 *   - an Alt chord that key_on_syskeydown sends to the shell still lets its
 *     WM_SYSKEYDOWN reach DefWindowProc afterward (key_on_syskeydown
 *     returns SYSKEYDOWN_SENT_AND_PASS, not SYSKEYDOWN_HANDLED), so
 *     Windows' own "Alt pressed alone" tracking clears the same way it
 *     would for a key it handled itself; there is no g_alt_consumed flag to
 *     swallow the matching WM_SYSKEYUP any more. WM_SYSCHAR for the
 *     character itself stays fully consumed, since passing that on to
 *     DefWindowProc is what would open menu mode;
 *   - g_key_oneshot / g_key_oneshot_char are cleared together, via
 *     key_oneshot_clear(), whenever the active session changes or focus
 *     leaves the terminal, so an armed one-shot never lands on a key meant
 *     for a different tab or window.
 */

/* Edit > Send Key's two one-shot items. g_key_oneshot is armed by the menu
 * and consumed by the next non-modifier key-down; when that key produces a
 * character, the key-down hands the state on to g_key_oneshot_char for the
 * WM_CHAR that follows. A key-down Windows keeps for itself (Alt+F4,
 * Alt+Space, Alt+Esc, Alt+Enter, Alt+Tab, Alt+numpad) leaves the one-shot
 * armed rather than consuming it: the raw one-shot bypasses Nutshell's own
 * shortcuts, not the chords Windows keeps. */
typedef enum { KEY_ONESHOT_NONE = 0, KEY_ONESHOT_RAW, KEY_ONESHOT_ALT } KeyOneShot;
static KeyOneShot g_key_oneshot      = KEY_ONESHOT_NONE;
static KeyOneShot g_key_oneshot_char = KEY_ONESHOT_NONE;

/* Clears both one-shots when the active session changes or focus leaves the
 * terminal (on_tab_select, on_tab_close, free_session, WM_KILLFOCUS), so a
 * stale arm from a previous tab or window never reaches an unrelated key. */
static void key_oneshot_clear(void)
{
    g_key_oneshot      = KEY_ONESHOT_NONE;
    g_key_oneshot_char = KEY_ONESHOT_NONE;
}

static bool key_is_down(int vk)
{
    return (GetKeyState(vk) & 0x8000) != 0;
}

static unsigned int key_mods(bool shift, bool ctrl, bool alt)
{
    return (shift ? NSK_MOD_SHIFT : 0u) | (alt ? NSK_MOD_ALT : 0u) |
           (ctrl ? NSK_MOD_CTRL : 0u);
}

/* Keys that only change the state of others: their key-downs never consume
 * a one-shot and never reach the encoder. */
static bool key_is_modifier(WPARAM vk)
{
    switch (vk) {
        case VK_SHIFT: case VK_CONTROL: case VK_MENU:
        case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
        case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL: case VK_PROCESSKEY:
            return true;
        default:
            return false;
    }
}

/* The keys that never produce a character, or NSK_NONE. */
static NsKey key_special_from_vk(WPARAM vk)
{
    switch (vk) {
        case VK_UP:     return NSK_UP;
        case VK_DOWN:   return NSK_DOWN;
        case VK_RIGHT:  return NSK_RIGHT;
        case VK_LEFT:   return NSK_LEFT;
        case VK_HOME:   return NSK_HOME;
        case VK_END:    return NSK_END;
        case VK_INSERT: return NSK_INSERT;
        case VK_DELETE: return NSK_DELETE;
        case VK_PRIOR:  return NSK_PGUP;
        case VK_NEXT:   return NSK_PGDN;
        default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F12)
        return (NsKey)((int)NSK_F1 + (int)(vk - VK_F1));
    return NSK_NONE;
}

/* Write bytes to the active session: drain pending transport data first so
 * the write can succeed in non-blocking mode (without it
 * libssh2_channel_write returns EAGAIN when the session has unread inbound
 * data, and the transport's retry loop blocks the UI thread -- a deadlock
 * that silently drops keystrokes), then return a scrolled-back view to the
 * live screen. */
static void session_send_bytes(HWND hwnd, const char *bytes, size_t n)
{
    Session *s = g_active_session;
    if (!s || !s->term || n == 0) return;
    if (s->io.ctx) {
        s->io.poll(s->io.ctx, s->term, s->session_log, s->debug_log);
        s->io.write(s->io.ctx, bytes, n);
    }
    if (s->term->scrollback_offset != 0) {
        s->term->scrollback_offset = 0;
        update_scrollbar(hwnd);
        invalidate_terminal(hwnd);
    }
}

/* Encode one key for the active session and send it. */
static void session_send_key(HWND hwnd, NsKey key, unsigned char ch,
                             unsigned int mods)
{
    Session *s = g_active_session;
    if (!s || !s->term) return;
    unsigned int flags = 0;
    if (s->term->app_cursor_keys)   flags |= NSK_FLAG_APP_CURSOR;
    if (s->io.kind == SESSION_LOCAL) flags |= NSK_FLAG_LOCAL;
    char buf[KEY_ENCODE_MAX];
    size_t n = key_encode(key, ch, mods, flags, buf, sizeof(buf));
    if (n > 0) session_send_bytes(hwnd, buf, n);
}

/* Remove the character(s) TranslateMessage queued for a key-down this
 * window has handled itself. Matched on the scan code (and extended bit)
 * that the WM_CHAR copies from the key-down's lParam, so a character
 * belonging to some other key is never eaten. A dead-key layout can queue
 * two messages off a single key-down -- the dead accent, then the key's own
 * character -- both stamped with that key-down's scan code, so each range
 * is drained in a loop rather than dropping only the first match. */
static void drop_queued_char(HWND hwnd, LPARAM keydown_lparam)
{
    const ULONG_PTR scan = ((ULONG_PTR)keydown_lparam >> 16) & 0x1FFu;
    static const UINT ranges[2][2] = {
        { WM_CHAR,    WM_DEADCHAR },
        { WM_SYSCHAR, WM_SYSDEADCHAR },
    };
    for (int i = 0; i < 2; i++) {
        MSG m;
        while (PeekMessage(&m, hwnd, ranges[i][0], ranges[i][1], PM_NOREMOVE) &&
               (((ULONG_PTR)m.lParam >> 16) & 0x1FFu) == scan) {
            (void)PeekMessage(&m, hwnd, ranges[i][0], ranges[i][1], PM_REMOVE);
        }
    }
}

/* Ctrl+Alt+key: is it AltGr on this layout (a character, which the WM_CHAR
 * that follows sends), or a Ctrl+Alt chord for the shell? Asks the layout
 * with ToUnicodeEx's "do not change keyboard state" flag (0x4, honoured
 * from Windows 10 1607 -- older than anything ConPTY runs on), so a pending
 * dead key survives the question. If the keyboard state cannot be read,
 * answer "a character": that leaves the key to WM_CHAR, today's behaviour. */
static bool key_layout_has_char(WPARAM vk, LPARAM lParam)
{
    BYTE ks[256];
    if (!GetKeyboardState(ks)) return true;
    WCHAR buf[8];
    UINT scan = (UINT)(((ULONG_PTR)lParam >> 16) & 0xFFu);
    int rc = ToUnicodeEx((UINT)vk, scan, ks, buf, 8, 0x4u, GetKeyboardLayout(0));
    if (rc < 0) return true;              /* a dead key: the layout is composing */
    return rc > 0 && buf[0] >= 0x20;      /* a control character is not AltGr */
}

static void close_active_tab(void)
{
    /* Close the tab that owns g_active_session -- the same pair the X
     * button hands on_tab_close -- rather than pairing the strip's active
     * index with our session pointer and trusting they still agree. */
    if (!g_active_session) return;
    int idx = tabs_find(g_hwndTabs, g_active_session);
    if (idx >= 0) on_tab_close(idx, g_active_session);
}

/* WM_KEYDOWN. Returns true when the key was handled (WndProc returns 0). */
static bool key_on_keydown(HWND hwnd, WPARAM vk, LPARAM lParam)
{
    if (key_is_modifier(vk)) return false;

    const bool shift = key_is_down(VK_SHIFT);
    const bool ctrl  = key_is_down(VK_CONTROL);
    const bool alt   = key_is_down(VK_MENU);
    /* The AltGr guard: during an AltGr character GetKeyState(VK_CONTROL)
     * is down too, so every Ctrl shortcut requires Alt up. */
    const bool ctrl_only = ctrl && !alt;
    /* Auto-repeat (lParam bit 30, "the key was already down"): the toggle
     * shortcuts below must fire once per press, not once per repeated
     * WM_KEYDOWN while the chord is held. */
    const bool is_repeat = (lParam & (1L << 30)) != 0;

    /* The previous key's character has been dispatched by now. */
    g_key_oneshot_char = KEY_ONESHOT_NONE;

    /* Edit > Send Key's one-shots: this key bypasses every shortcut. */
    if (g_key_oneshot != KEY_ONESHOT_NONE) {
        KeyOneShot shot = g_key_oneshot;
        g_key_oneshot = KEY_ONESHOT_NONE;
        unsigned int mods = key_mods(shift, ctrl, alt) |
                            (shot == KEY_ONESHOT_ALT ? NSK_MOD_ALT : 0u);
        NsKey k = key_special_from_vk(vk);
        if (vk == VK_BACK) k = NSK_BACKSPACE;
        if (vk == VK_TAB)  k = NSK_TAB;
        if (k != NSK_NONE) {
            drop_queued_char(hwnd, lParam);
            session_send_key(hwnd, k, 0, mods);
        } else {
            g_key_oneshot_char = shot;   /* WM_CHAR finishes the job */
        }
        return true;
    }

    /* F11 -- toggle fullscreen; with any modifier it goes to the shell.
     * Auto-repeat is consumed but ignored, so holding F11 toggles once. */
    if (vk == VK_F11 && !shift && !ctrl && !alt) {
        if (!is_repeat) SendMessage(hwnd, WM_COMMAND, IDM_VIEW_FULLSCREEN, 0);
        return true;
    }
    /* Ctrl+Shift+T new tab, Ctrl+Shift+W close tab, Ctrl+Shift+Space AI
     * panel. Plain Ctrl+T (transpose), Ctrl+W (kill word) and Ctrl+Space
     * (NUL) belong to the shell. Each still drops the queued character on
     * auto-repeat (so it never leaks to the shell) but skips the action
     * itself, so holding the chord fires it once. */
    if (ctrl_only && shift) {
        if (vk == (WPARAM)'T') {
            drop_queued_char(hwnd, lParam);
            if (!is_repeat) on_tab_new();
            return true;
        }
        if (vk == (WPARAM)'W') {
            drop_queued_char(hwnd, lParam);
            if (!is_repeat) close_active_tab();
            return true;
        }
        if (vk == VK_SPACE) {
            drop_queued_char(hwnd, lParam);
            if (!is_repeat) on_ai_clicked();
            return true;
        }
    }
    /* Ctrl+C -- copy selection if one exists, otherwise fall through so
     * WM_CHAR sends 0x03 (SIGINT) as before. Ctrl+Shift+C always copies
     * (no-op if nothing selected) and never reaches the shell. */
    if (ctrl_only && vk == (WPARAM)'C') {
        if (shift || g_selection.valid) {
            copy_selection_and_clear(hwnd);
            g_ctrlc_swallow_char = true;
            return true;
        }
    }
    /* Ctrl+V / Ctrl+Shift+V -- paste with confirmation (WM_CHAR swallows
     * the 0x16). */
    if (ctrl_only && vk == (WPARAM)'V') {
        do_paste(hwnd);
        return true;
    }
    /* Shift+Insert -- alternative paste shortcut */
    if (shift && !alt && vk == VK_INSERT) {
        do_paste(hwnd);
        return true;
    }
    /* Ctrl+= / Ctrl+- zoom */
    if (ctrl_only) {
        if (vk == VK_OEM_PLUS || vk == (WPARAM)'=') {
            apply_zoom(hwnd, 1);
            return true;
        }
        if (vk == VK_OEM_MINUS || vk == (WPARAM)'-') {
            apply_zoom(hwnd, -1);
            return true;
        }
    }

    if (!g_active_session || !g_active_session->term) return false;
    Terminal *t = g_active_session->term;

    /* PgUp/PgDn: the local scrollback on the primary screen, the program on
     * the alternate screen (less, vim, man, Edit -- it has no scrollback);
     * Shift+PgUp/PgDn always scroll back. */
    if (vk == VK_PRIOR || vk == VK_NEXT) {
        bool scroll = !ctrl && !alt && (shift || !t->alt_screen_active);
        if (scroll) {
            if (vk == VK_PRIOR)
                t->scrollback_offset = scroll_page_up(t->scrollback_offset,
                                                      t->rows,
                                                      t->max_scrollback);
            else
                t->scrollback_offset = scroll_page_down(t->scrollback_offset,
                                                        t->rows);
            update_scrollbar(hwnd);
            invalidate_terminal(hwnd);
            return true;
        }
    }
    /* Backspace: DEL for a local session, BS over SSH (with Alt, ESC
     * first). Ctrl+Backspace is left to its WM_CHAR (0x7F) as before. */
    if (vk == VK_BACK && !ctrl) {
        drop_queued_char(hwnd, lParam);
        session_send_key(hwnd, NSK_BACKSPACE, 0, key_mods(shift, false, alt));
        return true;
    }
    /* Shift+Tab: back-tab, CSI Z. Plain Tab is its WM_CHAR. */
    if (vk == VK_TAB && shift && !ctrl) {
        drop_queued_char(hwnd, lParam);
        session_send_key(hwnd, NSK_TAB, 0, NSK_MOD_SHIFT);
        return true;
    }
    /* Ctrl+Alt+letter the layout does not turn into a character: ESC then
     * the control byte. When it is AltGr, the WM_CHAR sends the character. */
    if (ctrl && alt && vk >= (WPARAM)'A' && vk <= (WPARAM)'Z' &&
        !key_layout_has_char(vk, lParam)) {
        drop_queued_char(hwnd, lParam);
        session_send_key(hwnd, NSK_CHAR, (unsigned char)('a' + (vk - 'A')),
                         NSK_MOD_CTRL | NSK_MOD_ALT);
        return true;
    }

    NsKey k = key_special_from_vk(vk);
    if (k != NSK_NONE) {
        session_send_key(hwnd, k, 0, key_mods(shift, ctrl, alt));
        return true;
    }
    return false;   /* a character key: its WM_CHAR does the sending */
}

/* WM_SYSKEYDOWN's outcome: whether -- and how -- the key was handled.
 * SENT_AND_PASS exists because an Alt chord sent to the shell must still
 * reach DefWindowProc afterward, so Windows' own "Alt pressed alone"
 * tracking clears the same way it would for a key it handled itself; only
 * HANDLED (F10 and Shift+F10) fully consumes the key-down. */
typedef enum {
    SYSKEYDOWN_NOT_HANDLED = 0,  /* DefWindowProc runs, nothing was sent */
    SYSKEYDOWN_HANDLED,          /* consumed: WndProc returns 0 */
    SYSKEYDOWN_SENT_AND_PASS     /* sent to the shell, DefWindowProc still runs */
} SysKeyDownResult;

/* WM_SYSKEYDOWN: Alt held without Ctrl, or F10. */
static SysKeyDownResult key_on_syskeydown(HWND hwnd, WPARAM vk, LPARAM lParam)
{
    const bool alt_ctx  = (lParam & (1L << 29)) != 0;
    const bool extended = (lParam & (1L << 24)) != 0;

    if (key_is_modifier(vk)) return SYSKEYDOWN_NOT_HANDLED;
    /* No transport (a disconnected tab): Alt behaves as before -- untouched
     * by any of this, straight to DefWindowProc. */
    if (!g_active_session || !g_active_session->term || !g_active_session->io.ctx)
        return SYSKEYDOWN_NOT_HANDLED;

    const bool shift = key_is_down(VK_SHIFT);
    const bool ctrl  = key_is_down(VK_CONTROL);

    if (!alt_ctx) {
        /* F10 and Shift+F10 arrive here without Alt: the shell's, not the
         * menu bar's (and Shift+F10 no longer becomes WM_CONTEXTMENU). */
        if (vk == VK_F10) {
            KeyOneShot shot = g_key_oneshot;
            g_key_oneshot = KEY_ONESHOT_NONE;
            g_key_oneshot_char = KEY_ONESHOT_NONE;
            const unsigned int shot_alt = shot == KEY_ONESHOT_ALT ? NSK_MOD_ALT : 0u;
            session_send_key(hwnd, NSK_F10, 0,
                             key_mods(shift, ctrl, false) | shot_alt);
            return SYSKEYDOWN_HANDLED;
        }
        return SYSKEYDOWN_NOT_HANDLED;
    }

    /* Left to Windows, one-shot armed or not: Alt+F4 (close), Alt+Space
     * (system menu; its WM_SYSCHAR is passed too), Alt+Esc (window
     * cycling), Alt+Enter, Alt+Tab, and Alt with a numpad key or a
     * non-extended navigation key, so Alt+0233 still composes a character.
     * A one-shot armed for one of these key-downs is left armed rather than
     * consumed: the raw one-shot bypasses Nutshell's own shortcuts, not the
     * chords Windows keeps for itself. */
    switch (vk) {
        case VK_F4: case VK_SPACE: case VK_ESCAPE: case VK_RETURN: case VK_TAB:
        case VK_CLEAR:
            return SYSKEYDOWN_NOT_HANDLED;
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT:
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
            if (!extended) return SYSKEYDOWN_NOT_HANDLED;
            break;
        default:
            if (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) return SYSKEYDOWN_NOT_HANDLED;
            break;
    }

    /* Past this point the key-down is ours: any armed one-shot has done its
     * job (this key already reaches the shell) and is consumed. */
    g_key_oneshot = KEY_ONESHOT_NONE;
    g_key_oneshot_char = KEY_ONESHOT_NONE;

    NsKey k = key_special_from_vk(vk);
    if (k != NSK_NONE) {
        session_send_key(hwnd, k, 0, key_mods(shift, ctrl, true));
        return SYSKEYDOWN_SENT_AND_PASS;
    }
    if (vk == VK_BACK) {
        drop_queued_char(hwnd, lParam);
        session_send_key(hwnd, NSK_BACKSPACE, 0, NSK_MOD_ALT);
        return SYSKEYDOWN_SENT_AND_PASS;
    }
    /* A character key: its WM_SYSCHAR sends ESC and the character. The
     * key-down itself still reaches DefWindowProc (SYSKEYDOWN_SENT_AND_PASS)
     * so Windows' Alt-alone tracking clears, but WM_SYSCHAR stays fully
     * consumed below so the character itself never opens menu mode. */
    return SYSKEYDOWN_SENT_AND_PASS;
}

/* WM_SYSCHAR: an Alt+character. Returns true when sent to the shell (and
 * therefore consumed, not passed to DefWindowProc). */
static bool key_on_syschar(HWND hwnd, WPARAM ch, LPARAM lParam)
{
    if (ch == (WPARAM)' ') return false;             /* Alt+Space: system menu */
    if (!(lParam & (1L << 29))) return false;         /* not an Alt character */
    /* No transport (a disconnected tab): Alt behaves as before. */
    if (!g_active_session || !g_active_session->term || !g_active_session->io.ctx)
        return false;
    if (ch == (WPARAM)'\r' || ch == (WPARAM)0x1B) return false;  /* Alt+Enter, Alt+Esc */
    /* Alt+numpad composition keys (non-extended scan codes 0x47-0x53). */
    UINT scan = (UINT)(((ULONG_PTR)lParam >> 16) & 0xFFu);
    if (!(lParam & (1L << 24)) && scan >= 0x47u && scan <= 0x53u) return false;

    g_key_oneshot_char = KEY_ONESHOT_NONE;
    session_send_key(hwnd, NSK_CHAR, (unsigned char)ch, NSK_MOD_ALT);
    return true;
}

/* WM_CHAR: a character from the layout -- printable, Ctrl+letter's control
 * byte, Enter, Tab, Escape. */
static void key_on_char(HWND hwnd, WPARAM wParam)
{
    if (!g_active_session || !g_active_session->term) return;
    unsigned char c = (unsigned char)wParam;
    KeyOneShot shot = g_key_oneshot_char;
    g_key_oneshot_char = KEY_ONESHOT_NONE;

    if (shot == KEY_ONESHOT_NONE) {
        if (c == 0x16) return;  /* Ctrl+V -- handled via do_paste in WM_KEYDOWN */
        if (c == 0x03 && g_ctrlc_swallow_char) {
            /* Ctrl+C copied a selection in WM_KEYDOWN -- don't send SIGINT */
            g_ctrlc_swallow_char = false;
            return;
        }
    }
    unsigned int mods = 0;
    /* Ctrl+Space arrives as 0x20 with Ctrl down: NUL (Emacs set-mark, a
     * common tmux prefix). The AltGr guard keeps an AltGr space a space. */
    if (c == 0x20 && key_is_down(VK_CONTROL) && !key_is_down(VK_MENU))
        mods |= NSK_MOD_CTRL;
    if (shot == KEY_ONESHOT_ALT) mods |= NSK_MOD_ALT;
    session_send_key(hwnd, NSK_CHAR, c, mods);
}

/* Edit > Send Key. */
static void key_on_send_menu(HWND hwnd, UINT id)
{
    if (id >= IDM_SENDKEY_F1 && id <= IDM_SENDKEY_F12) {
        session_send_key(hwnd, (NsKey)((int)NSK_F1 + (int)(id - IDM_SENDKEY_F1)),
                         0, 0);
        return;
    }
    switch (id) {
        case IDM_SENDKEY_PGUP:
            session_send_key(hwnd, NSK_PGUP, 0, 0);
            break;
        case IDM_SENDKEY_PGDN:
            session_send_key(hwnd, NSK_PGDN, 0, 0);
            break;
        case IDM_SENDKEY_CTRL_V:
            session_send_key(hwnd, NSK_CHAR, (unsigned char)'v', NSK_MOD_CTRL);
            break;
        case IDM_SENDKEY_SHIFT_INSERT:
            session_send_key(hwnd, NSK_INSERT, 0, NSK_MOD_SHIFT);
            break;
        case IDM_SENDKEY_CTRL_EQUALS:
            session_send_key(hwnd, NSK_CHAR, (unsigned char)'=', NSK_MOD_CTRL);
            break;
        case IDM_SENDKEY_CTRL_MINUS:
            session_send_key(hwnd, NSK_CHAR, (unsigned char)'-', NSK_MOD_CTRL);
            break;
        case IDM_SENDKEY_RAW_NEXT:
            g_key_oneshot = KEY_ONESHOT_RAW;
            break;
        case IDM_SENDKEY_ALT_NEXT:
            g_key_oneshot = KEY_ONESHOT_ALT;
            break;
        default:
            break;
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

                /* First start by a version that knows about local shells:
                 * give the user a real saved "Local shell" profile at the
                 * top of the list (spec section 5). It is an ordinary row
                 * from then on -- rename it, edit it, delete it. */
                if (config_ensure_local_profile(g_config))
                    (void)config_save(g_config, g_config_path);
            }

            g_hInst = ((LPCREATESTRUCT)lParam)->hInstance;

            /* Read the system "reduced motion" preference once at
             * startup; WM_SETTINGCHANGE keeps it current afterward. */
            refresh_reduced_motion();

            /* Look up colour theme from config */
            {
                int idx = ui_theme_find(g_config->settings.colour_scheme);
                g_theme = ui_theme_get(idx);
                ui_theme_resolve(g_theme, &g_tokens);
            }

            /* Compute DPI-scaled tab height and terminal left margin */
            g_dpi = get_window_dpi(hwnd);
            g_tab_height = ns_scale(TAB_HEIGHT_BASE, g_dpi);
            g_left_margin = ns_scale(TERM_LEFT_MARGIN, g_dpi);

            app_font_load_ui();

            /* Configure the ns_font cache's faces before anything asks it
             * for a font: proportional Inter for chrome, the configured
             * terminal font/size for FONT_MONO. */
            ns_font_set_faces(APP_FONT_UI_FACE,
                              g_config->settings.font[0]
                                  ? g_config->settings.font : APP_FONT_DEFAULT,
                              g_config->settings.font_size > 0
                                  ? g_config->settings.font_size : 12);

            /* UI font for owner-drawn menus */
            g_hMenuFont = ns_font(FONT_BODY, g_dpi);

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

            /* Initialize GDI+ for acorn watermark rendering */
            {
                GdiplusStartupInput gdip_input;
                gdip_input.GdiplusVersion = 1;
                gdip_input.DebugEventCallback = NULL;
                gdip_input.SuppressBackgroundThread = FALSE;
                gdip_input.SuppressExternalCodecs = FALSE;
                if (GdiplusStartup(&g_gdip_token, &gdip_input, NULL) == 0) {
                    /* Load acorn PNG from embedded RCDATA resource */
                    HRSRC hRes = FindResource(g_hInst,
                                              MAKEINTRESOURCE(IDR_ACORN_PNG),
                                              RT_RCDATA);
                    if (hRes) {
                        DWORD sz = SizeofResource(g_hInst, hRes);
                        HGLOBAL hMem = LoadResource(g_hInst, hRes);
                        if (hMem && sz > 0) {
                            const void *data = LockResource(hMem);
                            HGLOBAL hBuf = GlobalAlloc(GMEM_MOVEABLE, sz);
                            if (hBuf) {
                                void *buf = GlobalLock(hBuf);
                                memcpy(buf, data, sz);
                                GlobalUnlock(hBuf);
                                IStream *stream = NULL;
                                if (CreateStreamOnHGlobal(hBuf, TRUE,
                                                         &stream) == S_OK) {
                                    GdipCreateBitmapFromStream(stream,
                                        (GpBitmap **)&g_acorn_image);
                                    stream->lpVtbl->Release(stream);
                                }
                            }
                        }
                    }
                }
            }

            PostMessage(hwnd, WM_STARTUP_CONNECT, 0, 0);

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
            if (LOWORD(wParam) >= IDM_SENDKEY_F1 &&
                LOWORD(wParam) <= IDM_SENDKEY_ALT_NEXT) {
                key_on_send_menu(hwnd, LOWORD(wParam));
                return 0;
            }
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
                    /* lParam carries a SETTINGS_PAGE_* override when this
                     * is posted synthetically (ai_chat.c's no-key state
                     * wants Provider) rather than a real menu click,
                     * which always has lParam 0 -- indistinguishable from
                     * "no override" here since page 0 (Appearance) is
                     * also the default settings_nav_first_page() opens
                     * on. */
                    on_settings_clicked_page(lParam ? (int)lParam : -1);
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
                    show_about_dialog(hwnd);
                    return 0;
            }
            break;

        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lParam;
            if (mis->CtlType == ODT_MENU) {
                MenuItemData *mi = (MenuItemData *)(ULONG_PTR)mis->itemData;
                if (mi && mi->is_separator) {
                    mis->itemHeight = (UINT)ns_scale(6, g_dpi);
                    mis->itemWidth  = 0;
                    return TRUE;
                }
                if (mi) {
                    HDC hdc = GetDC(hwnd);
                    HFONT oldFont = g_hMenuFont ? (HFONT)SelectObject(hdc, g_hMenuFont) : NULL;
                    SIZE sz = {0, 0};
                    GetTextExtentPoint32A(hdc, mi->text, (int)strlen(mi->text), &sz);
                    /* Top-level menu bar items: tighter padding */
                    int hpad = (mi->hSub && !mi->nested) ? ns_scale(10, g_dpi)
                                                         : ns_scale(24, g_dpi);
                    mis->itemWidth = (UINT)sz.cx + (UINT)hpad;
                    if (mi->accel[0]) {
                        SIZE az;
                        GetTextExtentPoint32A(hdc, mi->accel, (int)strlen(mi->accel), &az);
                        mis->itemWidth += (UINT)az.cx + (UINT)ns_scale(12, g_dpi);
                    }
                    mis->itemHeight = (UINT)sz.cy + (UINT)ns_scale(4, g_dpi);
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
                /* Text on the selected (accent-filled) row: the token
                 * system's contrast-optimal label for accent, and a
                 * slightly dimmed version of it for the accelerator
                 * column (Design-System Foundation, task 10). */
                COLORREF cSelFg    = g_theme ? menu_tc(ns_tokens()->accent.label)
                                             : GetSysColor(COLOR_HIGHLIGHTTEXT);
                COLORREF cSelFgDim = g_theme ? rgb_alpha(cSelFg, cSel, 0.75f)
                                             : cSelFg;

                if (mid->is_separator) {
                    HBRUSH bgBr = CreateSolidBrush(cBg);
                    FillRect(hdc, &rcItem, bgBr);
                    DeleteObject(bgBr);
                    int my = (rcItem.top + rcItem.bottom) / 2;
                    HPEN sepPen = CreatePen(PS_SOLID, 1, cBord);
                    HPEN oldPen = (HPEN)SelectObject(hdc, sepPen);
                    MoveToEx(hdc, rcItem.left + ns_scale(4, g_dpi), my, NULL);
                    LineTo(hdc, rcItem.right - ns_scale(4, g_dpi), my);
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
                SetTextColor(hdc, selected ? cSelFg : cFg);

                /* Text — left aligned with padding */
                int xPad = ns_scale(8, g_dpi);
                RECT rcText = rcItem;
                rcText.left += xPad;
                DrawTextA(hdc, mid->text, -1, &rcText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                /* Accelerator — right aligned */
                if (mid->accel[0]) {
                    SetTextColor(hdc, selected ? cSelFgDim : cDim);
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
                    if (s->io.ctx && s->conn_state == CONN_IDLE) {
                        int poll_rc = s->io.poll(s->io.ctx, s->term,
                                                      s->session_log,
                                                      s->debug_log);

                        /* ConPTY never delivers a real pipe EOF just because
                         * the shell exited -- conhost keeps the session
                         * alive until ClosePseudoConsole runs (local_pty.c,
                         * local_pty_close()), so local_pty_poll() correctly
                         * never reports -2 for that by itself any more. A
                         * local session's "disconnected" signal is instead
                         * the shell process itself, watched here once this
                         * tick's pending output (if any) has been drained. */
                        if (poll_rc == 0 && s->io.kind == SESSION_LOCAL &&
                            local_pty_child_exited((const LocalPty *)s->io.ctx)) {
                            poll_rc = -2;
                        }

                        if (poll_rc > 0) {
                            update_scrollbar(hwnd);

                            /* Platform auto-detect: only while the profile
                             * didn't pin an explicit platform and we haven't
                             * already resolved (or given up). Re-scanning
                             * every data-bearing tick is what lets a banner
                             * split across multiple reads still be caught. */
                            if (!s->platform_locked && !s->platform_scanned) {
                                char detect_buf[4096];
                                size_t detect_len = term_extract_last_n(
                                    s->term, 40, detect_buf, sizeof detect_buf);
                                CmdDetectConfidence detect_conf = CMD_DETECT_NONE;
                                CmdPlatform detected = cmd_detect_platform(
                                    detect_buf, detect_len, &detect_conf);
                                if (detect_conf != CMD_DETECT_NONE)
                                    s->ai_state.platform = (int)detected;

                                s->platform_scan_ticks++;
                                if (detect_conf == CMD_DETECT_BANNER ||
                                    s->platform_scan_ticks >= PLATFORM_DETECT_MAX_TICKS)
                                    s->platform_scanned = 1;
                            }
                        }

                        /* Keepalive, the socket-liveness counter and the two
                         * timeout rails are libssh2-specific and are skipped
                         * for a local session (spec section 2). */
                        if (s->io.kind == SESSION_SSH) {
                            DWORD now_tick = GetTickCount();

                            /* Track socket-level liveness via the libssh2 RECV
                             * callback's byte counter.  Any change since last
                             * tick — including silently-consumed keepalive
                             * replies — proves the link is alive. */
                            if (s->ssh) {
                                uint64_t curr = s->ssh->bytes_read_total;
                                if (curr != s->prev_bytes_read) {
                                    s->last_socket_data_tick = now_tick;
                                    s->prev_bytes_read = curr;
                                }
                            }

                            /* Drive libssh2 keepalive ~once per second.  The
                             * library handles the 30s send cadence internally;
                             * we just need to give it CPU time. */
                            if (now_tick - s->last_keepalive_tick >= 1000u) {
                                int next_secs = 0;
                                if (s->ssh && s->ssh->session)
                                    libssh2_keepalive_send(s->ssh->session,
                                                           &next_secs);
                                s->last_keepalive_tick = now_tick;
                            }

                            /* Network-failure rail: no socket bytes at all
                             * (not even keepalive replies) for the threshold. */
                            if (poll_rc != -2 && s->io.ctx &&
                                ssh_network_should_timeout(now_tick,
                                                           s->last_socket_data_tick,
                                                           NETWORK_FAILURE_TIMEOUT_MS)) {
                                dispbuf_invalidate(&g_renderer.dispbuf);
                                if (g_paste.io_ctx == s->io.ctx)
                                    paste_cancel();
                                term_process(s->term,
                                             "\r\n[Connection timed out]\r\n", 26);
                                ai_panel_detach(s);
                                session_close_io(s);
                                int tidx_net = tabs_find(g_hwndTabs, s);
                                if (tidx_net >= 0)
                                    tabs_set_status(g_hwndTabs, tidx_net, TAB_DISCONNECTED);
                                if (s == g_active_session) hide_ai_panel(hwnd);
                            }

                            /* User-idle rail: configurable, 0 = disabled. */
                            if (poll_rc != -2 && s->io.ctx &&
                                ssh_idle_should_timeout(
                                    now_tick,
                                    s->last_user_input_tick,
                                    g_config->settings.ssh_user_idle_timeout_mins)) {
                                char banner[64];
                                int n = snprintf(banner, sizeof(banner),
                                                 "\r\n[Disconnected after %d min idle]\r\n",
                                                 g_config->settings.ssh_user_idle_timeout_mins);
                                if (n < 0) n = 0;
                                if (n > (int)sizeof(banner)) n = (int)sizeof(banner) - 1;
                                dispbuf_invalidate(&g_renderer.dispbuf);
                                if (g_paste.io_ctx == s->io.ctx)
                                    paste_cancel();
                                term_process(s->term, banner, (size_t)n);
                                ai_panel_detach(s);
                                session_close_io(s);
                                int tidx_idle = tabs_find(g_hwndTabs, s);
                                if (tidx_idle >= 0)
                                    tabs_set_status(g_hwndTabs, tidx_idle, TAB_DISCONNECTED);
                                if (s == g_active_session) hide_ai_panel(hwnd);
                            }
                        }

                        if (poll_rc == -2) {
                            /* EOF — the final data chunk (e.g. alt-screen-exit
                             * sequence) was already fed to term_process by
                             * the transport poll, so force a full display-buffer
                             * invalidation before handling the disconnect. */
                            dispbuf_invalidate(&g_renderer.dispbuf);
                            if (g_paste.io_ctx == s->io.ctx)
                                paste_cancel();
                            term_process(s->term, "\r\n[Connection Closed]\r\n", 23);
                            ai_panel_detach(s);
                            session_close_io(s);
                            int tidx = tabs_find(g_hwndTabs, s);
                            if (tidx >= 0)
                                tabs_set_status(g_hwndTabs, tidx, TAB_DISCONNECTED);
                            /* Auto-hide AI panel when the active session disconnects */
                            if (s == g_active_session)
                                hide_ai_panel(hwnd);
                        }
                        if (poll_rc > 0 || term_has_dirty_rows(s->term)) {
                            DWORD now = GetTickCount();
                            bool critical = (s == g_active_session &&
                                             s->term->full_redraw_needed);
                            if (critical || now - g_last_paint_tick >= PAINT_COOLDOWN_MS) {
                                if (poll_rc > 0 || critical)
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
            } else if (wParam == ANIM_TIMER_ID) {
                unsigned long now = (unsigned long)GetTickCount();
                int active = ns_anim_list_step(&g_anim_list, now,
                                                g_reduced_motion);

                NsAnimStep step;
                if (ns_anim_list_get(&g_anim_list, ANIM_ID_AI_DOCK, now,
                                      g_reduced_motion, &step)) {
                    g_ai_panel_width = g_ai_anim_from
                        + (int)((g_ai_target_width - g_ai_anim_from) * step.t);
                } else {
                    /* No longer tracked: the slide just finished (or was
                     * never running) -- snap to the exact target. */
                    g_ai_panel_width = g_ai_target_width;
                    /* Hide the window after close animation finishes */
                    if (g_ai_target_width == 0
                        && g_hwndAiChat && IsWindow(g_hwndAiChat))
                        ShowWindow(g_hwndAiChat, SW_HIDE);
                }
                if (active == 0)
                    KillTimer(hwnd, ANIM_TIMER_ID);
                relayout_main(hwnd);
            }
            return 0;

        case WM_SHOW_SESSION_MANAGER:
            on_tab_new();
            return 0;

        case WM_STARTUP_CONNECT: {
            if (g_startup_action == CLI_UI_DEMO) {
                create_demo_session(hwnd);
                return 0;
            }
            if (g_startup_action == CLI_CONNECT_LOCAL) {
                /* --local starts from a transient profile and never looks a
                 * name up, so a saved profile called "Local shell" that
                 * points at a host cannot hijack the flag (spec section 5).
                 * Nothing is saved: this profile exists for one session. */
                Profile local_pr;
                memset(&local_pr, 0, sizeof(local_pr));
                (void)snprintf(local_pr.name, sizeof(local_pr.name), "%s",
                               "Local shell");
                (void)snprintf(local_pr.kind, sizeof(local_pr.kind), "%s",
                               "local");
                (void)snprintf(local_pr.platform, sizeof(local_pr.platform),
                               "%s", "auto");
                local_pr.port = 22;
                local_pr.auth_type = AUTH_PASSWORD;
                on_session_connect(&local_pr);
                return 0;
            }
            const Profile *pr = NULL;
            const char *wanted = NULL;
            if (g_startup_action == CLI_CONNECT_NAME) {
                wanted = g_startup_arg;
                pr = config_find_profile_by_name(g_config, wanted);
            } else if (g_startup_action == CLI_CONNECT_HOST) {
                wanted = g_startup_arg;
                pr = config_find_profile_by_host(g_config, wanted);
            } else if (g_startup_action == CLI_RUN
                       && g_config->settings.auto_connect
                       && g_config->settings.auto_connect_session[0] != '\0') {
                /* Settings dropdown shows host for unnamed sessions, so the
                 * stored string may be either: name first, then host. */
                wanted = g_config->settings.auto_connect_session;
                pr = config_find_profile_by_name(g_config, wanted);
                if (!pr) pr = config_find_profile_by_host(g_config, wanted);
            } else {
                /* Nothing to auto-connect. Open the Session Manager if the
                 * user asked for that at startup — but not for -nc
                 * (CLI_RUN_NO_CONNECT), which explicitly suppresses it. */
                if (g_startup_action == CLI_RUN
                    && g_config->settings.open_session_manager_at_start) {
                    PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0);
                }
                return 0;
            }
            if (pr) {
                on_session_connect(pr);
            } else {
                char nf_text[512];
                (void)snprintf(nf_text, sizeof(nf_text),
                               "Session \"%s\" not found.", wanted);
                MessageBoxA(hwnd, nf_text, "Session Not Found",
                            MB_OK | MB_ICONWARNING);
                PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0);
            }
            return 0;
        }

        case WM_CONN_DONE: {
            Session *s = (Session *)lParam;
            s->conn_state = CONN_IDLE;
            DWORD tick_now = GetTickCount();
            s->last_socket_data_tick = tick_now;
            s->last_keepalive_tick   = tick_now;
            s->last_user_input_tick  = tick_now;
            s->prev_bytes_read       = (s->ssh ? s->ssh->bytes_read_total : 0);
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
                /* Publish the transport here, on the UI thread, so no other
                 * UI-thread reader (WM_SIZE, sync_session_grid) can see a
                 * half-copied SessionIo with ctx set and resize NULL. The
                 * channel and session were opened by the connection thread,
                 * which has posted this message and touches neither again. */
                s->io = session_io_ssh(s->ssh, s->channel);
                term_process(s->term, "\r\nConnected.\r\n", 14);
                /* Keep a log the user already started from the File menu
                 * while the tab was still connecting; only auto-open one
                 * (per the logging_enabled setting) if none is open. */
                if (!s->session_log)
                    s->session_log = open_session_log(s->conn_profile.name,
                                                      s->conn_profile.host);
                s->debug_log   = open_debug_log(s->conn_profile.name);
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
                        s->io.ctx,
                        s == g_active_session)) {
                    ai_chat_set_session(g_hwndAiChat, s->term, session_io_ptr(s));
                    /* SSH: no shell name, so the prompt keeps its SSH text. */
                    ai_chat_set_shell_name(g_hwndAiChat, NULL);
                }

                /* Reopen AI panel that was closed before first session */
                if (g_ai_reopen_after_connect && s == g_active_session) {
                    g_ai_reopen_after_connect = 0;
                    on_ai_clicked();
                }
            }
            /* Sync PTY size to actual window dimensions — the window may have
             * been resized while the connection thread was running (auth can
             * take several seconds), and WM_SIZE skips the transport resize
             * while io.ctx is NULL. Without this, TUI apps like nano start with
             * the pre-connection window size and appear blank until the user
             * manually resizes. */
            if (conn_result == 0)
                sync_session_grid(hwnd, s);
            update_scrollbar(hwnd);
            force_full_terminal_repaint(hwnd, s ? s->term : NULL);
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
            /* Minimise reports a 0x0 client. Fitting the grid to that
             * (1x1) and pushing it to the remote PTY made the shell redraw
             * its prompt one column wide while iconic; each wrapped
             * character scrolled the buffer and, via term_scroll()'s
             * smart-scroll anchor, moved a scrolled-back view up a line,
             * so the view came back a page higher after restore. Nothing
             * is visible while iconic, and SW_RESTORE delivers a proper
             * WM_SIZE, so leave layout, grid and PTY exactly as they are. */
            if (wParam == SIZE_MINIMIZED) {
                g_iconic = true;
                return 0;
            }
            if (g_iconic) {
                g_iconic = false;
                force_full_terminal_repaint(hwnd,
                    g_active_session ? g_active_session->term : NULL);
            }

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

            /* Reposition custom scrollbar (left of AI panel if docked).
             * When docked, make the rightmost SPLITTER_PAD pixels of the
             * scrollbar fall through to the parent so the splitter is
             * grabbable from the scrollbar side. */
            if (g_hwndScrollbar) {
                SetWindowPos(g_hwndScrollbar, NULL,
                    width - ai_w - CSB_WIDTH, g_tab_height,
                    CSB_WIDTH, height - g_tab_height,
                    SWP_NOZORDER);
                csb_set_right_pad(g_hwndScrollbar,
                    g_ai_docked ? AI_DOCK_SPLITTER_PAD : 0);
            }

            int rows, cols;
            if (g_active_session && g_active_session->term &&
                ai_dock_terminal_grid(width, height, g_tab_height,
                                      ai_w, CSB_WIDTH, g_left_margin,
                                      g_renderer.charWidth, g_renderer.charHeight,
                                      &rows, &cols)) {
                if (cols != g_active_session->term->cols || rows != g_active_session->term->rows) {
                    term_resize(g_active_session->term, rows, cols);
                    term_mark_all_dirty(g_active_session->term);
                    dispbuf_resize(&g_renderer.dispbuf, rows, cols);
                    if (g_active_session->io.ctx)
                        g_active_session->io.resize(g_active_session->io.ctx, cols, rows);
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
            g_tab_height = ns_scale(TAB_HEIGHT_BASE, g_dpi);
            g_left_margin = ns_scale(TERM_LEFT_MARGIN, g_dpi);

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

            /* DPI changed: every cached role/dpi/face slot is now stale for
             * this window, so flush the whole cache once and let each
             * consumer re-fetch at the new DPI. */
            ns_font_flush();
            g_hMenuFont = ns_font(FONT_BODY, g_dpi);
            tabs_set_font(g_hwndTabs, g_config->settings.font, newDpi);
            if (g_hwndAiChat && IsWindow(g_hwndAiChat))
                ai_chat_refresh_fonts(g_hwndAiChat);

            /* Recalculate terminal grid via WM_SIZE */
            RECT dpi_rc;
            GetClientRect(hwnd, &dpi_rc);
            SendMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                        MAKELPARAM(dpi_rc.right, dpi_rc.bottom));
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        case WM_SETTINGCHANGE:
            refresh_reduced_motion();
            return 0;

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
#ifdef REDRAW_DEBUG
                {
                    Terminal *_t = g_active_session->term;
                    int _dirty = 0;
                    for (int _ri = 0; _ri < _t->rows; _ri++) {
                        int _li = ((_t->lines_count >= _t->rows)
                                   ? (_t->lines_count - _t->rows) : 0) + _ri;
                        int _pi = (_t->lines_start + _li) % _t->lines_capacity;
                        if (_t->lines[_pi] && _t->lines[_pi]->dirty) _dirty++;
                    }
                    REDRAW_LOG("[REDRAW] WM_PAINT: paintRect=(%ld,%ld,%ld,%ld) "
                               "dirty_rows=%d full_redraw=%d\n",
                               ps.rcPaint.left, ps.rcPaint.top,
                               ps.rcPaint.right, ps.rcPaint.bottom,
                               _dirty, _t->full_redraw_needed);
                }
#endif
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

                /* Draw ghosted acorn watermark centered with 15% margins */
                if (g_acorn_image) {
                    int area_w = term_right_edge;
                    int area_h = client.bottom - g_tab_height;
                    /* 15% inset on each side => 70% usable */
                    int box_w = area_w * 70 / 100;
                    int box_h = area_h * 70 / 100;
                    /* Fit square (locked aspect ratio) to shorter dimension */
                    int img_sz = box_w < box_h ? box_w : box_h;
                    if (img_sz > 0) {
                        int cx = (area_w - img_sz) / 2;
                        int cy = g_tab_height + (area_h - img_sz) / 2;

                        GpGraphics *gfx = NULL;
                        GdipCreateFromHDC(hdc, &gfx);
                        if (gfx) {
                            GdipSetInterpolationMode(gfx, 7); /* HighQualityBicubic */

                            /* Color matrix: grayscale (luminance weights)
                             * with alpha = 0.06 for a subtle watermark. */
                            ColorMatrix cm = {{
                                {0.299f, 0.299f, 0.299f, 0,      0},
                                {0.587f, 0.587f, 0.587f, 0,      0},
                                {0.114f, 0.114f, 0.114f, 0,      0},
                                {0,      0,      0,      0.06f,  0},
                                {0,      0,      0,      0,      0}
                            }};
                            GpImageAttributes *attr = NULL;
                            GdipCreateImageAttributes(&attr);
                            if (attr) {
                                GdipSetImageAttributesColorMatrix(attr,
                                    0, TRUE, &cm, NULL, 0);

                                UINT src_w = 0, src_h = 0;
                                GdipGetImageWidth(g_acorn_image, &src_w);
                                GdipGetImageHeight(g_acorn_image, &src_h);

                                GdipDrawImageRectRectI(gfx, g_acorn_image,
                                    cx, cy, img_sz, img_sz,
                                    0, 0, (INT)src_w, (INT)src_h,
                                    2, /* UnitPixel */
                                    attr, NULL, NULL);
                                GdipDisposeImageAttributes(attr);
                            }
                            GdipDeleteGraphics(gfx);
                        }
                    }
                }

                /* Version label at bottom-right, same opacity as logo */
                {
                    int vpad = ns_scale(10, g_dpi);
                    HFONT vfont = ns_font(FONT_CAPTION, g_dpi);
                    HGDIOBJ old_vf = SelectObject(hdc, vfont);
                    SetBkMode(hdc, TRANSPARENT);
                    /* Blend text color at 6% against background */
                    COLORREF bg_cr = g_renderer.defaultBg;
                    int vr = GetRValue(bg_cr) + (int)((255 - GetRValue(bg_cr)) * 0.10);
                    int vg = GetGValue(bg_cr) + (int)((255 - GetGValue(bg_cr)) * 0.10);
                    int vb = GetBValue(bg_cr) + (int)((255 - GetBValue(bg_cr)) * 0.10);
                    SetTextColor(hdc, RGB(vr, vg, vb));
                    RECT vrc;
                    vrc.left   = 0;
                    vrc.top    = 0;
                    vrc.right  = term_right_edge - vpad;
                    vrc.bottom = client.bottom - vpad;
                    DrawTextA(hdc, "v" APP_VERSION, -1, &vrc,
                              DT_SINGLELINE | DT_RIGHT | DT_BOTTOM | DT_NOPREFIX);
                    SelectObject(hdc, old_vf);
                    /* vfont comes from the ns_font cache — no delete. */
                }
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

        case WM_CHAR:
            session_mark_user_active();
            key_on_char(hwnd, wParam);
            return 0;

        case WM_SYSCHAR:
            session_mark_user_active();
            if (key_on_syschar(hwnd, wParam, lParam)) return 0;
            break;  /* Alt+Space, Alt+Enter, ... : DefWindowProc */

        case WM_KEYDOWN:
            session_mark_user_active();
            if (key_on_keydown(hwnd, wParam, lParam)) return 0;
            break;  /* a character key: TranslateMessage has queued its WM_CHAR */

        case WM_SYSKEYDOWN: {
            session_mark_user_active();
            SysKeyDownResult r = key_on_syskeydown(hwnd, wParam, lParam);
            if (r == SYSKEYDOWN_HANDLED) return 0;
            if (r == SYSKEYDOWN_SENT_AND_PASS)
                /* Sent to the shell, but the key-down still reaches
                 * DefWindowProc so Windows' "Alt pressed alone" tracking
                 * clears as it would for any key it handled itself. */
                return DefWindowProc(hwnd, msg, wParam, lParam);
            break;  /* SYSKEYDOWN_NOT_HANDLED: Alt+F4, Alt+numpad, a lone Alt, ... */
        }

        case WM_SYSKEYUP:
            /* F10's key-up is where DefWindowProc would enter menu mode for
             * it; keep it consumed like the key-down. A lone Alt tap's
             * release, and the release after a chord passed through above,
             * both reach DefWindowProc -- there is no swallow flag for Alt
             * any more. */
            if (wParam == VK_F10 && g_active_session && g_active_session->term)
                return 0;
            break;

        case WM_KILLFOCUS:
            /* Focus leaving the terminal (e.g. to the AI panel or another
             * window) must not leave an armed one-shot for a keystroke that
             * lands somewhere else entirely. */
            key_oneshot_clear();
            break;

        case WM_MOUSEWHEEL: {
            /* Ctrl+Scroll zooms the font */
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                apply_zoom(hwnd, delta > 0 ? 1 : -1);
                return 0;
            }
            /* Plain scroll: scroll 3 lines per notch */
            if (g_active_session && g_active_session->term) {
                g_active_session->last_user_input_tick = GetTickCount();
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
                                          AI_DOCK_SPLITTER_W,
                                          AI_DOCK_SPLITTER_PAD,
                                          pt.y, g_tab_height)) {
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
                                          AI_DOCK_SPLITTER_W,
                                          AI_DOCK_SPLITTER_PAD,
                                          my, g_tab_height)) {
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
            g_hMenuFont = NULL;
            ns_font_flush();
            app_font_free_ui();
            if (g_acorn_image) {
                GdipDisposeImage(g_acorn_image);
                g_acorn_image = NULL;
            }
            if (g_gdip_token) {
                GdiplusShutdown(g_gdip_token);
                g_gdip_token = 0;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ui_set_startup_action(CliAction action, const char *arg,
                           const char *demo_state, const char *theme)
{
    g_startup_action = action;
    (void)snprintf(g_startup_arg, sizeof(g_startup_arg), "%s",
                   arg ? arg : "");
    (void)snprintf(g_startup_demo_state, sizeof(g_startup_demo_state), "%s",
                   demo_state ? demo_state : "");
    (void)snprintf(g_startup_theme, sizeof(g_startup_theme), "%s",
                   theme ? theme : "");
}

void ui_init(HINSTANCE instance) {
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_APPICON),
                                IMAGE_ICON,
                                GetSystemMetrics(SM_CXICON),
                                GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_APPICON),
                                   IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON),
                                   LR_DEFAULTCOLOR);
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
    int winW = ns_scale(880, dpi);
    int winH = ns_scale(570, dpi);
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