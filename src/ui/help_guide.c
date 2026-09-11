#include "help_guide.h"
#include "resource.h"
#include "dpi_util.h"
#include "app_font.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ui_theme.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "edit_scroll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <commctrl.h>

#define IDC_GUIDE_TEXT  3001
#define IDC_GUIDE_CLOSE 3002
#define IDT_GUIDE_SCROLL 60

static const char *GUIDE_CLASS = "Nutshell_HelpGuide";

/* ---- Guide text --------------------------------------------------------- */

/* The guide is one document, split into GUIDE_PART_1..6 only because C99
   caps a single string literal at 4095 chars. They are declared in reading
   order and joined in that order by GUIDE_PARTS below; when a part outgrows
   the limit, split it at a section heading and add the new part to that
   table. */
static const char GUIDE_PART_1[] =
"NUTSHELL USER GUIDE\r\n"
"===================\r\n"
"\r\n"
"Nutshell is a lightweight SSH terminal emulator for Windows with\r\n"
"built-in AI assistance.\r\n"
"\r\n"
"\r\n"
"GETTING STARTED\r\n"
"---------------\r\n"
"\r\n"
"1. Open the Session Manager (File > Session Manager, or Ctrl+T)\r\n"
"2. Click \"New\" to create a session profile\r\n"
"3. Enter the connection details:\r\n"
"   - Name:  A label for this session (optional)\r\n"
"   - Host:  The server hostname or IP address\r\n"
"   - Port:  SSH port (default 22)\r\n"
"   - User:  Your username on the server\r\n"
"   - Auth:  Password or SSH Key\r\n"
"   - Platform: which CLI this host speaks, used to judge\r\n"
"      command safety. Auto-detect (default) reads the login\r\n"
"      banner and prompt; or pin it to Linux, Cisco IOS /\r\n"
"      NX-OS / ASA, HP ProCurve, HPE Comware, Aruba OS-CX,\r\n"
"      ArubaOS, PAN-OS, Junos, FortiOS, VyOS or RouterOS\r\n"
"4. Click \"Save\" to store the profile\r\n"
"5. Select the profile and click \"Connect\"\r\n"
"\r\n"
"On first connection to a new host, Nutshell shows the server's\r\n"
"SSH fingerprint. Verify it matches the expected value before\r\n"
"accepting. Accepted fingerprints are saved so you won't be\r\n"
"asked again unless the server's key changes.\r\n"
"\r\n"
"\r\n"
"TABS & SESSIONS\r\n"
"---------------\r\n"
"\r\n"
"Each connection opens in its own tab. You can have multiple\r\n"
"sessions open at once.\r\n"
"\r\n"
"  - New tab:     Ctrl+T or click \"+\" on the tab bar\r\n"
"  - Close tab:   Ctrl+W or click the X on the tab\r\n"
"  - Switch tabs: Click on a tab\r\n"
"\r\n"
"Tab colours indicate connection status:\r\n"
"  - Grey:    Idle (not connected)\r\n"
"  - Yellow:  Connecting\r\n"
"  - Green:   Connected\r\n"
"  - Red:     Disconnected\r\n"
"\r\n"
"An [L] badge appears on tabs where session logging is active.\r\n"
"\r\n"
"\r\n"
"TERMINAL\r\n"
"--------\r\n"
"\r\n"
"Nutshell provides a full terminal emulator with:\r\n"
"  - 256-colour and truecolour (24-bit) support\r\n"
"  - Bold, dim, underline, blink, and reverse text styles\r\n"
"  - Alternate screen buffer (vim, nano, htop, etc.)\r\n"
"  - Configurable scrollback (100 to 50,000 lines)\r\n"
"  - Scroll a full page with Page Up / Page Down, or use the scrollbar\r\n"
"\r\n"
"\r\n"
"COPY & PASTE\r\n"
"------------\r\n"
"\r\n"
"  - Select text: Click and drag in the terminal\r\n"
"  - Copy:        Ctrl+C copies the selection and clears it;\r\n"
"                 with no selection, Ctrl+C sends SIGINT as usual\r\n"
"  - Copy always: Ctrl+Shift+C (never sent to the shell)\r\n"
"  - Paste:       Ctrl+V, Ctrl+Shift+V, or Shift+Insert\r\n"
"  - Select All:  Edit menu (no shortcut; Ctrl+A goes to shell)\r\n"
"\r\n"
"\"Confirm before pasting\" (Settings, on by default) shows a\r\n"
"preview before every paste. An inter-line paste delay is also\r\n"
"available, for servers that need time between lines.\r\n"
"\r\n"
"\r\n"
"ZOOM\r\n"
"----\r\n"
"\r\n"
"  - Zoom in:   Ctrl+= or Ctrl+Scroll Up\r\n"
"  - Zoom out:  Ctrl+- or Ctrl+Scroll Down\r\n"
"\r\n"
"Available font sizes: 6, 8, 10, 12, 14, 16, 18, 20 pt.\r\n"
"The terminal grid resizes automatically when you zoom.\r\n"
"\r\n"
"\r\n"
;

static const char GUIDE_PART_2[] =
"SETTINGS\r\n"
"--------\r\n"
"\r\n"
"Open Settings from Edit > Settings.  Pick a category on the\r\n"
"left; the window is resizable.\r\n"
"\r\n"
"General > Appearance:\r\n"
"  - Colour Scheme:  Four built-in themes\r\n"
"      Onyx Synapse  - Dark with blue accents (default)\r\n"
"      Onyx Light    - Light with blue accents\r\n"
"      Sage & Sand   - Dark earthy tones\r\n"
"      Moss & Mist   - Light pastel greens\r\n"
"  - Terminal Font, Font Size (6-20 pt), AI Assist Font\r\n"
"\r\n"
"General > Terminal:\r\n"
"  - Scrollback:   Number of lines kept in history\r\n"
"  - Paste Delay:  Milliseconds between lines when pasting\r\n"
"  - Confirm before pasting: preview dialog before pasting\r\n"
"    (default: on)\r\n"
"\r\n"
"General > Logging:\r\n"
"  - Log Directory, and Log Name Format using strftime\r\n"
"    codes (default %Y-%m-%d_%H-%M-%S)\r\n"
"  - Debug Terminal Log: capture the raw byte stream\r\n"
"\r\n"
"Connection > SSH:\r\n"
"  - Idle Timeout: disconnect after N minutes (0 = never)\r\n"
"\r\n"
"Connection > Startup:\r\n"
"  - Open Session Manager at startup (default: off)\r\n"
"  - Auto-connect on launch, and the session to open\r\n"
"\r\n"
"AI Assistant > Provider, Behaviour, Web Access:\r\n"
"  - See AI ASSIST below\r\n"
"\r\n"
"\r\n"
"SESSION LOGGING\r\n"
"---------------\r\n"
"\r\n"
"Record terminal output to a file:\r\n"
"  1. File > Start Logging (or click the [L] icon on the tab)\r\n"
"  2. All terminal output is saved as plain text (ANSI stripped)\r\n"
"  3. File > Stop Logging to end recording\r\n"
"\r\n"
"Files are named <Log Name Format>_<name>.log (name = profile\r\n"
"name, falling back to host) in the configured Log Directory.\r\n";

static const char GUIDE_PART_3[] =
"\r\n"
"AI ASSIST\r\n"
"---------\r\n"
"\r\n"
"Nutshell includes an AI chat assistant that can see your terminal\r\n"
"and help with commands, troubleshooting, and system administration.\r\n"
"\r\n"
"Setup:\r\n"
"  1. Open Settings (Edit > Settings)\r\n"
"  2. Choose an AI provider: Anthropic, OpenAI, Gemini,\r\n"
"     Moonshot, DeepSeek, or a custom OpenAI-compatible endpoint\r\n"
"  3. Enter your API key\r\n"
"  4. Click the refresh button next to AI Model to load the\r\n"
"     available models, then select one from the dropdown\r\n"
"  5. Click Save\r\n"
"\r\n"
"The AI Model field stays empty until you select a provider,\r\n"
"enter an API key, and press the refresh button. This fetches\r\n"
"the list of models available for your account.\r\n"
"\r\n"
"The AI button in the tab bar turns green when configured, but\r\n"
"opens the panel either way -- press it, press Ctrl+Space, or go\r\n"
"to View > AI Assist.\r\n"
"\r\n"
"Header (top of the panel):\r\n"
"  - Session name, a model chip, and three icon buttons:\r\n"
"    New chat (clear and start fresh), Save chat (export the\r\n"
"    conversation as a text file), Undock/Dock (pop the panel\r\n"
"    into its own window, or dock it back)\r\n"
"\r\n"
;

/* Split purely to stay under the C99 4095-char string-literal limit --
   GUIDE_PARTS below joins every block back into one document. */
static const char GUIDE_PART_4[] =
"Status line (above the input box):\r\n"
"  - Policy control: one scale of four stops --\r\n"
"      Read | Unknown | Write | Critical -- carrying two\r\n"
"      markers. Click a stop's LABEL to move the upper\r\n"
"      marker, \"allowed\": commands above it are blocked\r\n"
"      outright and show as held; commands at or below it\r\n"
"      may run, and ask you first. Click the RAIL under a\r\n"
"      stop to move the lower marker, \"runs unattended\":\r\n"
"      anything at or below that runs the moment the AI\r\n"
"      suggests it, with no card to click. Everything\r\n"
"      between the two markers asks first. Clicking the rail\r\n"
"      under the stop it already sits on empties it, so\r\n"
"      nothing runs unattended. The lower marker can never\r\n"
"      pass the upper one, and the whole control takes its\r\n"
"      colour from the upper one -- green at Read, amber at\r\n"
"      Write, red at Critical -- so a permissive session is\r\n"
"      obvious at a glance. New sessions start from\r\n"
"      Settings > AI Assistant > Behaviour > \"Allowed\" and\r\n"
"      \"Runs unattended\"; clicking here changes only this\r\n"
"      session\r\n"
"  - Context meter: a bar plus used/limit showing how much of\r\n"
"      the model's context window the conversation is using\r\n"
"\r\n"
"When the AI suggests commands, they land in one approval card:\r\n"
"a header (\"N commands . M held\"), a checkbox + risk tag\r\n"
"(READ/UNKNOWN/WRITE/CRITICAL) per row, and two actions --\r\n"
"\"Deny all\" and \"Run N selected\" (runs the checked rows;\r\n"
"disabled when none are checked). Rows at or below the\r\n"
"unattended marker never reach the card at all -- they run.\r\n"
"\r\n"
;

static const char GUIDE_PART_5[] =
"The four risk tags, which are also the four stops on the\r\n"
"policy scale, in order:\r\n"
"  READ      provably read-only -- it matched a rule saying so\r\n"
"  UNKNOWN   no rule claimed it. Might read, might wipe the\r\n"
"            disk -- Nutshell does not guess, and gates it\r\n"
"            like a write\r\n"
"  WRITE     changes state, recoverable (mv, apt install,\r\n"
"            configure terminal)\r\n"
"  CRITICAL  outage, data loss or lock-out (rm -rf, reload,\r\n"
"            write erase, commit, no username admin)\r\n"
"\r\n"
"Which rules apply depends on the profile's Platform, since\r\n"
"the same word differs by system: reload reboots a Cisco\r\n"
"switch, commit applies a firewall policy on PAN-OS and\r\n"
"Junos, undo removes config on Comware. A pipeline is judged\r\n"
"segment by segment, and it runs unattended only if EVERY\r\n"
"segment is at or below the unattended marker -- so\r\n"
"\"something | tee /etc/hosts\" still asks when the unknown\r\n"
"half is above it.\r\n"
"\r\n"
"When a reply carries reasoning, a \"Thinking\" row appears above\r\n"
"it -- click to expand or collapse. It opens automatically while\r\n"
"streaming, then collapses once the reply text starts unless you\r\n"
"expanded it yourself.\r\n"
"\r\n"
"The panel always opens, even with nothing to show yet: an empty\r\n"
"conversation offers three suggestion chips to send as a prompt;\r\n"
"no API key shows an \"Open Settings\" button (jumps straight to\r\n"
"AI Assistant > Provider); no connected session shows an \"Open\r\n"
"Session Manager\" button.\r\n"
"\r\n"
"AI Notes (per session):\r\n"
"  In the Session Manager, each profile has an \"AI Notes\" field.\r\n"
"  Use it to give the AI context about the server, e.g.:\r\n"
"  \"This is the production database. Be extra careful.\"\r\n"
"\r\n"
"System-wide AI Instructions:\r\n"
"  Settings > AI Assistant > Behaviour has a \"System\r\n"
"  Instructions\" field that applies to every conversation.\r\n"
"\r\n"
"AI Settings:\r\n"
"  Provider    - API provider, key, model and base URL\r\n"
"  Behaviour   - Max Terminal Lines, System Instructions,\r\n"
"                markdown rendering\r\n"
"  Web Access  - Search engine, max results, web fetch\r\n"
"\r\n"
"Max Terminal Lines (default 1000, maximum 50000) sets how\r\n"
"much of the terminal the assistant reads.  Every line is\r\n"
"part of the context sent with each message, so a larger\r\n"
"window costs proportionately more tokens.\r\n"
"\r\n"
"You can undock the AI panel into its own window via\r\n"
"View > Undock AI Assist.\r\n"
"\r\n"
"\r\n"
"AUTHENTICATION\r\n"
"--------------\r\n"
"\r\n"
"Nutshell supports two authentication methods:\r\n"
"\r\n"
"Password:\r\n"
"  Enter your password in the Session Manager. Passwords are\r\n"
"  encrypted at rest using AES-256-GCM in the config file.\r\n"
"\r\n"
"SSH Key:\r\n"
"  1. Set Auth to \"SSH Key\" in the Session Manager\r\n"
"  2. Browse to your private key file (.pem, .ppk, etc.)\r\n"
"  3. If the key has a passphrase, enter it in the Password field\r\n"
;

static const char GUIDE_PART_6[] =
"\r\n"
"\r\n"
"SECURITY\r\n"
"--------\r\n"
"\r\n"
"  - Passwords and API keys are encrypted at rest (AES-256-GCM)\r\n"
"  - Host key verification on first connection (TOFU)\r\n"
"  - Warning shown if a server's key changes unexpectedly\r\n"
"  - AI command execution requires explicit user approval\r\n"
"  - Known hosts stored in %APPDATA%\\sshclient\\known_hosts\r\n"
"\r\n"
"\r\n"
"KEYBOARD SHORTCUTS\r\n"
"------------------\r\n"
"\r\n"
"  Ctrl+T           New session / Session Manager\r\n"
"  Ctrl+W           Close active tab\r\n"
"  Ctrl+C           Copy selection (clears it); SIGINT if none\r\n"
"  Ctrl+Shift+C     Copy selection (never sent to the shell)\r\n"
"  Ctrl+V           Paste from clipboard\r\n"
"  Ctrl+Shift+V     Paste from clipboard (always)\r\n"
"  Shift+Insert     Paste from clipboard\r\n"
"  Ctrl+=           Zoom in\r\n"
"  Ctrl+-           Zoom out\r\n"
"  Ctrl+Scroll      Zoom with mouse wheel\r\n"
"  Page Up          Scroll up a full page through history\r\n"
"  Page Down        Scroll down a full page through history\r\n"
"  Ctrl+Space       Toggle AI Assist panel\r\n"
"  F11              Toggle fullscreen\r\n"
"  Enter            Send message (in AI chat)\r\n"
"  Shift+Enter      New line (in AI chat)\r\n"
"\r\n"
"\r\n"
"CONFIGURATION FILE\r\n"
"------------------\r\n"
"\r\n"
"All settings and session profiles are stored in nutshell.config\r\n"
"in the same directory as the executable. The file is JSON format\r\n"
"and is updated automatically when you save changes in the UI.\r\n"
;

/* ---- Dialog state ------------------------------------------------------- */

typedef struct {
    const ThemeColors *theme;
    HBRUSH hBrBgPrimary;
    HBRUSH hBrBgSecondary;
    HFONT  hDlgFont;
    HFONT  hTitleFont;
    HWND   hScrollbar;
    int    line_h;
} HelpGuideData;

/* Sync scrollbar with the edit control */
static void guide_sync_scroll(HWND hwnd, HelpGuideData *d)
{
    HWND hEdit = GetDlgItem(hwnd, IDC_GUIDE_TEXT);
    csb_sync_edit(hEdit, d->hScrollbar, d->line_h);
}

/* Subclass proc for the EDIT control — forwards WM_MOUSEWHEEL to parent */
static LRESULT CALLBACK GuideEditSubclass(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    if (msg == WM_MOUSEWHEEL) {
        HelpGuideData *d = (HelpGuideData *)dwRefData;
        int zd = GET_WHEEL_DELTA_WPARAM(wParam);
        int lines = 3 * zd / WHEEL_DELTA;
        if (lines != 0)
            SendMessage(hwnd, EM_LINESCROLL, 0, (LPARAM)(-lines));
        if (d) {
            HWND hParent = GetParent(hwnd);
            if (hParent) guide_sync_scroll(hParent, d);
        }
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* ---- Window procedure --------------------------------------------------- */

static LRESULT CALLBACK HelpGuideWndProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam)
{
    HelpGuideData *d = (HelpGuideData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {

    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        d = (HelpGuideData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)d);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;

        int dpi = get_window_dpi(hwnd);

        int margin = ns_scale(12, dpi);
        int btn_h  = ns_scale(25, dpi);
        int btn_w  = ns_scale(75, dpi);
        int edit_w = cw - 2 * margin - CSB_WIDTH;
        int edit_h = ch - 3 * margin - btn_h;

        /* Multiline read-only text area */
        HWND hEdit = CreateWindow("EDIT", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            margin, margin, edit_w, edit_h,
            hwnd, (HMENU)(UINT_PTR)IDC_GUIDE_TEXT, NULL, NULL);
        SendMessage(hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                    MAKELPARAM(6, 6));
        SetWindowSubclass(hEdit, GuideEditSubclass, 0, (DWORD_PTR)d);

        /* Close button (owner-drawn for theme) */
        CreateWindow("BUTTON", "Close",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw / 2 - btn_w / 2, ch - margin - btn_h, btn_w, btn_h,
            hwnd, (HMENU)(UINT_PTR)IDC_GUIDE_CLOSE, NULL, NULL);

        /* Apply Inter UI font */
        {
            d->hDlgFont = ns_font(FONT_BODY, dpi);
            if (d->hDlgFont) {
                HWND hChild = NULL;
                while ((hChild = FindWindowEx(hwnd, hChild, NULL, NULL)) != NULL)
                    SendMessage(hChild, WM_SETFONT, (WPARAM)d->hDlgFont, TRUE);
            }
        }

        /* Theme */
        themed_apply_title_bar(hwnd, d->theme);
        themed_apply_borders(hwnd, d->theme);

        /* Set text after font is applied. The guide is split into blocks
           only because C99 caps a single string literal at 4095 chars; they
           are one document, joined here in order. */
        {
            static const char *const GUIDE_PARTS[] = {
                GUIDE_PART_1, GUIDE_PART_2, GUIDE_PART_3,
                GUIDE_PART_4, GUIDE_PART_5, GUIDE_PART_6
            };
            const size_t part_n = sizeof GUIDE_PARTS / sizeof GUIDE_PARTS[0];
            size_t total = 0;
            for (size_t i = 0; i < part_n; i++)
                total += strlen(GUIDE_PARTS[i]);
            char *full = (char *)malloc(total + 1);
            if (full) {
                size_t at = 0;
                for (size_t i = 0; i < part_n; i++) {
                    size_t len = strlen(GUIDE_PARTS[i]);
                    memcpy(full + at, GUIDE_PARTS[i], len);
                    at += len;
                }
                full[at] = '\0';
                SetWindowTextA(hEdit, full);
                free(full);
            }
        }
        SendMessage(hEdit, EM_SETSEL, (WPARAM)-1, 0);  /* deselect */

        /* Custom scrollbar */
        {
            /* Measure line height */
            HDC edc = GetDC(hEdit);
            HGDIOBJ old = SelectObject(edc, (HGDIOBJ)d->hDlgFont);
            TEXTMETRIC tm;
            GetTextMetrics(edc, &tm);
            d->line_h = tm.tmHeight + tm.tmExternalLeading;
            if (d->line_h < 1) d->line_h = 16;
            SelectObject(edc, old);
            ReleaseDC(hEdit, edc);

            d->hScrollbar = csb_create(hwnd,
                margin + edit_w, margin,
                CSB_WIDTH, edit_h,
                d->theme, GetModuleHandle(NULL));
            guide_sync_scroll(hwnd, d);
            SetTimer(hwnd, IDT_GUIDE_SCROLL, 60, NULL);
        }

        return 0;
    }

    case WM_VSCROLL:
        if (d && d->hScrollbar && (HWND)lParam == d->hScrollbar) {
            WORD code = LOWORD(wParam);
            HWND hEdit = GetDlgItem(hwnd, IDC_GUIDE_TEXT);
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
                    csb_get_trackpos(d->hScrollbar), first);
                break;
            case SB_TOP:       delta = -first; break;
            case SB_BOTTOM:    delta = 99999;  break;
            }
            if (delta != 0)
                SendMessage(hEdit, EM_LINESCROLL, 0, (LPARAM)delta);
            guide_sync_scroll(hwnd, d);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == IDT_GUIDE_SCROLL && d) {
            guide_sync_scroll(hwnd, d);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        if (d && d->theme) {
            HDC edc = (HDC)wParam;
            RECT erc;
            GetClientRect(hwnd, &erc);
            FillRect(edc, &erc, d->hBrBgPrimary);
            return 1;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_secondary));
            return (LRESULT)d->hBrBgSecondary;
        }
        break;

    case WM_CTLCOLOREDIT:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_secondary));
            return (LRESULT)d->hBrBgSecondary;
        }
        break;

    case WM_DRAWITEM:
        if (d && d->theme) {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            draw_themed_button(dis, d->theme, 1);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_GUIDE_CLOSE)
            DestroyWindow(hwnd);
        return 0;

    case WM_MOUSEWHEEL:
        if (d) {
            HWND hEdit = GetDlgItem(hwnd, IDC_GUIDE_TEXT);
            int zd = GET_WHEEL_DELTA_WPARAM(wParam);
            int lines = 3 * zd / WHEEL_DELTA;
            if (lines != 0)
                SendMessage(hEdit, EM_LINESCROLL, 0, (LPARAM)(-lines));
            guide_sync_scroll(hwnd, d);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_GUIDE_SCROLL);
        if (d) {
            /* d->hDlgFont comes from the ns_font cache — owned there. */
            if (d->hTitleFont)     DeleteObject(d->hTitleFont);
            if (d->hBrBgPrimary)   DeleteObject(d->hBrBgPrimary);
            if (d->hBrBgSecondary) DeleteObject(d->hBrBgSecondary);
            free(d);
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ---- Public API --------------------------------------------------------- */

void help_guide_show(HWND parent, const char *colour_scheme)
{
    HelpGuideData *d = (HelpGuideData *)calloc(1u, sizeof(HelpGuideData));
    if (!d) return;

    int idx = ui_theme_find(colour_scheme);
    d->theme = ui_theme_get(idx);
    d->hBrBgPrimary   = CreateSolidBrush(theme_cr(d->theme->bg_primary));
    d->hBrBgSecondary = CreateSolidBrush(theme_cr(d->theme->bg_secondary));

    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = HelpGuideWndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = GUIDE_CLASS;
    RegisterClassEx(&wc);

    int pdpi = get_window_dpi(parent);

    HWND hwnd = CreateWindowEx(
        0, GUIDE_CLASS, "Nutshell User Guide",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        ns_scale(520, pdpi), ns_scale(600, pdpi),
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
