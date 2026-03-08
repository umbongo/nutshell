#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat.h"

#ifdef _WIN32

#include "ai_prompt.h"
#include "ai_http.h"
#include "app_font.h"
#include "ui_theme.h"
#include "themed_button.h"
#include "term_extract.h"
#include "ssh_channel.h"
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <richedit.h>
#include <commctrl.h>

static const char *AI_CHAT_CLASS = "Nutshell_AIChat";

#define IDC_CHAT_DISPLAY  2001
#define IDC_CHAT_INPUT    2002
#define IDC_CHAT_SEND     2003
#define IDC_CHAT_NEWCHAT  2004
#define IDC_CHAT_PERMIT   2005

#define WM_AI_RESPONSE   (WM_USER + 100)
#define WM_AI_CONTINUE   (WM_USER + 101)

#define TERM_CONTEXT_ROWS 50
#define CONTINUE_DELAY_MS 2000  /* Wait for terminal output before continuing */
#define TIMER_CONTINUE    1
#define TIMER_CMD_QUEUE   2     /* Delayed command execution (paste delay) */

/* Forward declaration for input subclass */
static LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData);

typedef struct {
    HWND hwnd;
    HWND hDisplay;
    HWND hInput;
    HWND hSendBtn;
    HWND hNewChatBtn;
    HWND hPermitBtn;
    int permit_write;     /* 0 = read-only (red), 1 = read/write (green) */
    HFONT hFont;
    char font_name[64];
    const ThemeColors *theme;
    HBRUSH hBrBgPrimary;
    HBRUSH hBrBgSecondary;

    /* AI state */
    AiConversation conv;
    char api_key[256];
    char provider[64];
    char custom_url[256];
    char custom_model[256];

    /* Active session references */
    Terminal   *active_term;
    SSHChannel *active_channel;

    /* Background thread */
    volatile int busy;
    CRITICAL_SECTION cs;

    /* Auto-continue: when AI only gives partial commands, re-prompt */
    int commands_executed;   /* number of commands just executed */
    char pending_request[2048]; /* original user request for context */

    /* Position of the thinking/continuing indicator so we can remove it */
    int indicator_pos;  /* char offset where indicator starts, or -1 */

    /* Batch command execution with paste delay */
    int paste_delay_ms;
    char queued_cmds[16][1024];
    int queued_count;
    int queued_next;       /* index of next command to execute */
} AiChatData;

/* Append colored text to the RichEdit chat display.
 * Auto-scrolls to the bottom only if the user was already at the bottom.
 * If italic != 0, the text is rendered in italic. */
static void chat_append_styled(HWND hDisplay, const char *text,
                                COLORREF color, int italic)
{
    /* Check if scrolled to bottom before appending */
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hDisplay, SB_VERT, &si);
    int was_at_bottom = (si.nPos + (int)si.nPage >= si.nMax) || (si.nMax == 0);

    int len = GetWindowTextLength(hDisplay);
    SendMessage(hDisplay, EM_SETSEL, (WPARAM)len, (LPARAM)len);

    CHARFORMAT2 cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_ITALIC;
    cf.crTextColor = color;
    cf.dwEffects = italic ? CFE_ITALIC : 0;
    SendMessage(hDisplay, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    /* Convert UTF-8 to UTF-16 so the Unicode RichEdit displays emoji
     * and other non-ASCII characters correctly. */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wlen > 0) {
        wchar_t *wtext = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
        if (wtext) {
            MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);
            SendMessageW(hDisplay, EM_REPLACESEL, FALSE, (LPARAM)wtext);
            free(wtext);
        }
    }

    if (was_at_bottom) {
        SendMessage(hDisplay, WM_VSCROLL, SB_BOTTOM, 0);
    }
}

/* Convenience: non-italic colored text */
static void chat_append_color(HWND hDisplay, const char *text, COLORREF color)
{
    chat_append_styled(hDisplay, text, color, 0);
}

/* Append ops/status text: italic + dim gray for clear contrast */
static void chat_append_ops(HWND hDisplay, const char *text)
{
    chat_append_styled(hDisplay, text, RGB(140, 140, 140), 1);
}

/* Format AI response for display:
 *   - Convert \n to \r\n for Win32 EDIT control
 *   - Replace [EXEC]cmd[/EXEC] with "  > cmd" blocks
 *   - Result must be freed by caller */
static char *format_ai_text(const char *raw)
{
    if (!raw) return NULL;

    /* Worst case: every char doubles (\n -> \r\n) + marker replacements.
     * Allocate generously. */
    size_t raw_len = strlen(raw);
    size_t alloc = raw_len * 2 + 256;
    char *out = (char *)malloc(alloc);
    if (!out) return NULL;

    size_t oi = 0;
    const char *p = raw;

    while (*p && oi + 16 < alloc) {
        /* Replace [EXEC]...[/EXEC] with formatted command block */
        if (strncmp(p, "[EXEC]", 6) == 0) {
            const char *cmd_start = p + 6;
            const char *cmd_end = strstr(cmd_start, "[/EXEC]");
            if (cmd_end) {
                /* Write "  > command\r\n" */
                out[oi++] = '\r'; out[oi++] = '\n';
                out[oi++] = ' '; out[oi++] = ' ';
                out[oi++] = '>'; out[oi++] = ' ';
                /* Copy command, converting inner \n to \r\n */
                for (const char *c = cmd_start; c < cmd_end && oi + 4 < alloc; c++) {
                    if (*c == '\n') {
                        out[oi++] = '\r'; out[oi++] = '\n';
                        out[oi++] = ' '; out[oi++] = ' ';
                        out[oi++] = ' '; out[oi++] = ' ';
                    } else {
                        out[oi++] = *c;
                    }
                }
                out[oi++] = '\r'; out[oi++] = '\n';
                p = cmd_end + 7; /* skip [/EXEC] */
                continue;
            }
        }

        /* Convert bare \n to \r\n */
        if (*p == '\n') {
            if (oi > 0 && out[oi - 1] != '\r') {
                out[oi++] = '\r';
            }
            out[oi++] = '\n';
            p++;
            continue;
        }

        out[oi++] = *p++;
    }

    out[oi] = '\0';
    return out;
}

/* Background thread: call AI API */
static unsigned __stdcall ai_thread_proc(void *arg)
{
    AiChatData *d = (AiChatData *)arg;

    EnterCriticalSection(&d->cs);

    /* Build request body */
    char body[AI_BODY_MAX];
    size_t body_len = ai_build_request_body(&d->conv, body, sizeof(body));

    char api_key_copy[256];
    char provider_copy[64];
    char custom_url_copy[256];
    strncpy(api_key_copy, d->api_key, sizeof(api_key_copy) - 1);
    api_key_copy[sizeof(api_key_copy) - 1] = '\0';
    strncpy(provider_copy, d->provider, sizeof(provider_copy) - 1);
    provider_copy[sizeof(provider_copy) - 1] = '\0';
    strncpy(custom_url_copy, d->custom_url, sizeof(custom_url_copy) - 1);
    custom_url_copy[sizeof(custom_url_copy) - 1] = '\0';

    LeaveCriticalSection(&d->cs);

    if (body_len == 0) {
        PostMessage(d->hwnd, WM_AI_RESPONSE, 0, (LPARAM)_strdup("Error: failed to build request"));
        d->busy = 0;
        return 0;
    }

    /* Make HTTP request */
    const char *url = ai_provider_url(provider_copy);
    if (!url && strcmp(provider_copy, "custom") == 0 && custom_url_copy[0])
        url = custom_url_copy;
    if (!url) {
        PostMessage(d->hwnd, WM_AI_RESPONSE, 0, (LPARAM)_strdup("Error: unknown AI provider (set a custom URL for custom provider)"));
        d->busy = 0;
        return 0;
    }

    /* Anthropic uses x-api-key header instead of Bearer token.
     * ai_http_post wraps the value as "Authorization: <value>",
     * so for Anthropic we pass a dummy and add headers manually.
     * For now, use Bearer for all — Anthropic support is TODO. */
    char auth[300];
    (void)snprintf(auth, sizeof(auth), "Bearer %s", api_key_copy);

    AiHttpResponse resp;
    int rc = ai_http_post(url, auth, body, body_len, &resp);

    if (rc != 0 || resp.status_code < 200 || resp.status_code >= 300) {
        char errbuf[1024];
        if (rc != 0 && resp.error[0]) {
            snprintf(errbuf, sizeof(errbuf), "HTTP error: %s", resp.error);
        } else if (resp.body && resp.body[0]) {
            snprintf(errbuf, sizeof(errbuf), "HTTP %d: %.*s",
                     resp.status_code, 800, resp.body);
        } else {
            snprintf(errbuf, sizeof(errbuf), "HTTP %d: request failed",
                     resp.status_code);
        }
        ai_http_response_free(&resp);
        PostMessage(d->hwnd, WM_AI_RESPONSE, 0, (LPARAM)_strdup(errbuf));
        d->busy = 0;
        return 0;
    }

    /* Parse response */
    char content[AI_MSG_MAX];
    if (ai_parse_response(resp.body, content, sizeof(content)) != 0) {
        char errbuf[1024];
        snprintf(errbuf, sizeof(errbuf),
                 "Error: failed to parse AI response:\n%.900s",
                 resp.body ? resp.body : "(null body)");
        ai_http_response_free(&resp);
        PostMessage(d->hwnd, WM_AI_RESPONSE, 0, (LPARAM)_strdup(errbuf));
        d->busy = 0;
        return 0;
    }

    ai_http_response_free(&resp);

    /* Add assistant message to conversation */
    EnterCriticalSection(&d->cs);
    ai_conv_add(&d->conv, AI_ROLE_ASSISTANT, content);
    LeaveCriticalSection(&d->cs);

    PostMessage(d->hwnd, WM_AI_RESPONSE, 1, (LPARAM)_strdup(content));
    d->busy = 0;
    return 0;
}

static void send_user_message(AiChatData *d)
{
    if (!d || d->busy) return;

    char input[2048];
    GetWindowText(d->hInput, input, (int)sizeof(input));
    if (input[0] == '\0') return;

    SetWindowText(d->hInput, "");

    /* Display user message */
    COLORREF col_user = d->theme ? theme_cr(d->theme->accent) : RGB(0, 120, 215);
    chat_append_ops(d->hDisplay, "\r\n--- You ---\r\n");
    chat_append_color(d->hDisplay, input, col_user);
    chat_append_color(d->hDisplay, "\r\n", col_user);

    /* Extract terminal context */
    char term_text[8192] = "";
    if (d->active_term) {
        term_extract_last_n(d->active_term, TERM_CONTEXT_ROWS,
                           term_text, sizeof(term_text));
    }

    EnterCriticalSection(&d->cs);

    /* On first message, add system prompt */
    if (d->conv.msg_count == 0) {
        char sys_prompt[AI_MSG_MAX];
        ai_build_system_prompt(sys_prompt, sizeof(sys_prompt), term_text);
        ai_conv_add(&d->conv, AI_ROLE_SYSTEM, sys_prompt);
    } else if (d->active_term) {
        /* Update system prompt with fresh terminal context */
        char sys_prompt[AI_MSG_MAX];
        ai_build_system_prompt(sys_prompt, sizeof(sys_prompt), term_text);
        /* Replace the first (system) message */
        snprintf(d->conv.messages[0].content,
                 sizeof(d->conv.messages[0].content), "%s", sys_prompt);
    }

    ai_conv_add(&d->conv, AI_ROLE_USER, input);
    LeaveCriticalSection(&d->cs);

    /* Show "thinking..." indicator — record position for later removal.
     * Use EM_EXGETSEL (not GetWindowTextLength) because RichEdit counts
     * line breaks as 1 char internally but GetWindowTextLength counts
     * them as 2 (\r\n), causing EM_SETSEL to start at the wrong offset. */
    SendMessage(d->hDisplay, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    CHARRANGE cr_ind;
    SendMessage(d->hDisplay, EM_EXGETSEL, 0, (LPARAM)&cr_ind);
    d->indicator_pos = cr_ind.cpMax;
    chat_append_ops(d->hDisplay, "\r\n(thinking...)");

    /* Spawn background thread */
    d->busy = 1;
    _beginthreadex(NULL, 0, ai_thread_proc, d, 0, NULL);
}

static void execute_command(AiChatData *d, const char *cmd)
{
    if (!d || !d->active_channel || !cmd || !cmd[0]) return;

    /* Send command + CR to SSH channel (CR = Enter key, same as WM_CHAR) */
    ssh_channel_write(d->active_channel, cmd, (size_t)strlen(cmd));
    ssh_channel_write(d->active_channel, "\r", 1);

    chat_append_ops(d->hDisplay, "  $ ");
    chat_append_ops(d->hDisplay, cmd);
    chat_append_ops(d->hDisplay, "\r\n");
}

static void send_continue_message(AiChatData *d)
{
    if (!d || d->busy) return;

    /* Extract fresh terminal context after command execution */
    char term_text[8192] = "";
    if (d->active_term) {
        term_extract_last_n(d->active_term, TERM_CONTEXT_ROWS,
                           term_text, sizeof(term_text));
    }

    EnterCriticalSection(&d->cs);

    /* Update system prompt with fresh terminal context */
    if (d->conv.msg_count > 0 && d->active_term) {
        char sys_prompt[AI_MSG_MAX];
        ai_build_system_prompt(sys_prompt, sizeof(sys_prompt), term_text);
        snprintf(d->conv.messages[0].content,
                 sizeof(d->conv.messages[0].content), "%s", sys_prompt);
    }

    ai_conv_add(&d->conv, AI_ROLE_USER,
        "The commands above have been executed. Look at the updated terminal "
        "output and continue with any remaining tasks from my original request. "
        "If there are more commands to run, include ALL of them now. "
        "If everything is done, just summarize what was accomplished.");

    LeaveCriticalSection(&d->cs);

    SendMessage(d->hDisplay, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    CHARRANGE cr_cont;
    SendMessage(d->hDisplay, EM_EXGETSEL, 0, (LPARAM)&cr_cont);
    d->indicator_pos = cr_cont.cpMax;
    chat_append_ops(d->hDisplay, "\r\n(continuing...)");

    d->busy = 1;
    _beginthreadex(NULL, 0, ai_thread_proc, d, 0, NULL);
}

/* Subclass proc for the multiline input: Enter sends, Shift+Enter inserts newline */
static LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData)
{
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        int shift = GetKeyState(VK_SHIFT) & 0x8000;
        AiInputAction action = ai_input_key_action(1, shift ? 1 : 0);
        if (action == AI_INPUT_SEND) {
            /* Trigger the send button */
            HWND parent = GetParent(hwnd);
            PostMessage(parent, WM_COMMAND,
                        MAKEWPARAM(IDC_CHAT_SEND, BN_CLICKED), 0);
            return 0; /* eat the Enter key */
        }
        /* AI_INPUT_NEWLINE: fall through to default (inserts newline) */
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, InputSubclassProc, uIdSubclass);
    }
    (void)dwRefData;
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK AiChatWndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
        AiChatData *nd = (AiChatData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)nd);
        nd->hwnd = hwnd;

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;
        int btn_h = 28; /* New Chat button height */
        int top_y = 5 + btn_h + 5; /* display starts below button */

        /* New Chat button (owner-drawn for theme) */
        nd->hNewChatBtn = CreateWindow("BUTTON", "New Chat",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            5, 5, 90, btn_h,
            hwnd, (HMENU)IDC_CHAT_NEWCHAT, NULL, NULL);

        /* Permit Write button */
        nd->permit_write = 0; /* default: read-only */
        nd->hPermitBtn = CreateWindow("BUTTON", "Permit Write",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            100, 5, 130, btn_h,
            hwnd, (HMENU)IDC_CHAT_PERMIT, NULL, NULL);

        /* Chat display: read-only RichEdit for colored text */
        int input_h = 46; /* ~2 lines for multiline input */
        nd->hDisplay = CreateWindowW(L"RichEdit20W", L"",
            WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_BORDER |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            5, top_y, cw - 10, ch - input_h - top_y - 10,
            hwnd, (HMENU)IDC_CHAT_DISPLAY, NULL, NULL);

        /* Set RichEdit background from theme */
        SendMessage(nd->hDisplay, EM_SETBKGNDCOLOR, 0,
                    (LPARAM)(nd->theme ? theme_cr(nd->theme->bg_secondary)
                                       : GetSysColor(COLOR_WINDOW)));

        /* Input field: multiline, Enter sends via subclass, Shift+Enter = newline */
        nd->hInput = CreateWindow("EDIT", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            5, ch - input_h - 5, cw - 55, input_h,
            hwnd, (HMENU)IDC_CHAT_INPUT, NULL, NULL);
        SetWindowSubclass(nd->hInput, InputSubclassProc, 0, 0);

        /* Send button (owner-drawn for theme) */
        nd->hSendBtn = CreateWindow("BUTTON", ">",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw - 45, ch - input_h - 5, 40, input_h,
            hwnd, (HMENU)IDC_CHAT_SEND, NULL, NULL);

        /* Font — use configured font at UI size */
        HDC hdc = GetDC(hwnd);
        int h = -MulDiv(APP_FONT_UI_SIZE, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(hwnd, hdc);
        nd->hFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               FIXED_PITCH | FF_MODERN, nd->font_name);
        if (nd->hFont) {
            SendMessage(nd->hDisplay, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
            SendMessage(nd->hInput, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
            SendMessage(nd->hSendBtn, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
            SendMessage(nd->hNewChatBtn, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
            SendMessage(nd->hPermitBtn, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
        }

        /* Apply theme title bar + borders */
        if (nd->theme) {
            themed_apply_title_bar(hwnd, nd->theme);
            themed_apply_borders(hwnd, nd->theme);
        }

        chat_append_ops(nd->hDisplay,
            "AI Chat - Type a message and press Enter or click Send.\r\n"
            "The AI can see your terminal and execute commands.\r\n"
            "---\r\n");

        SetFocus(nd->hInput);
        return 0;
    }

    case WM_SIZE: {
        if (!d) break;
        int cw = LOWORD(lParam);
        int ch = HIWORD(lParam);
        int btn_h = 28;
        int top_y = 5 + btn_h + 5;
        int input_h = 46;
        MoveWindow(d->hNewChatBtn, 5, 5, 90, btn_h, TRUE);
        MoveWindow(d->hPermitBtn, 100, 5, 130, btn_h, TRUE);
        MoveWindow(d->hDisplay, 5, top_y, cw - 10, ch - input_h - top_y - 10, TRUE);
        MoveWindow(d->hInput, 5, ch - input_h - 5, cw - 55, input_h, TRUE);
        MoveWindow(d->hSendBtn, cw - 45, ch - input_h - 5, 40, input_h, TRUE);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHAT_SEND:
            send_user_message(d);
            SetFocus(d->hInput);
            return 0;
        case IDC_CHAT_NEWCHAT:
            if (d && !d->busy) {
                /* Reset conversation state */
                ai_conv_reset(&d->conv);
                d->indicator_pos = -1;
                d->commands_executed = 0;
                d->pending_request[0] = '\0';
                d->queued_count = 0;
                d->queued_next = 0;
                /* Clear display and show welcome message */
                SetWindowTextW(d->hDisplay, L"");
                chat_append_ops(d->hDisplay,
                    "AI Chat - Type a message and press Enter or click Send.\r\n"
                    "The AI can see your terminal and execute commands.\r\n"
                    "---\r\n");
                /* Clear input field */
                SetWindowText(d->hInput, "");
                SetFocus(d->hInput);
            }
            return 0;
        case IDC_CHAT_PERMIT:
            if (d) {
                d->permit_write = !d->permit_write;
                InvalidateRect(d->hPermitBtn, NULL, TRUE);
            }
            return 0;
        case IDC_CHAT_INPUT:
            /* Handle Enter key in input (via EN_CHANGE notification is wrong;
             * we handle it via WM_KEYDOWN subclass or default button) */
            break;
        }
        break;

    case WM_AI_RESPONSE: {
        if (!d) break;
        char *text = (char *)lParam;
        if (!text) break;

        /* Remove thinking/continuing indicator using saved position */
        if (d->indicator_pos >= 0) {
            int end_pos = GetWindowTextLength(d->hDisplay);
            SendMessage(d->hDisplay, EM_SETSEL,
                        (WPARAM)d->indicator_pos, (LPARAM)end_pos);
            SendMessageW(d->hDisplay, EM_REPLACESEL, FALSE, (LPARAM)L"");
            d->indicator_pos = -1;
        }

        if (wParam == 1) {
            COLORREF col_ai = d->theme ? theme_cr(d->theme->text_main) : GetSysColor(COLOR_WINDOWTEXT);

            chat_append_ops(d->hDisplay, "\r\n--- AI ---\r\n");
            char *formatted = format_ai_text(text);
            if (formatted) {
                chat_append_color(d->hDisplay, formatted, col_ai);
                free(formatted);
            } else {
                chat_append_color(d->hDisplay, text, col_ai);
            }
            chat_append_color(d->hDisplay, "\r\n", col_ai);

            /* Check for executable commands (supports multiple) */
            char cmds[16][1024];
            int ncmds = ai_extract_commands(text, cmds, 16);

            /* Filter out write commands when permit_write is off */
            if (ncmds > 0 && !d->permit_write) {
                int ncmds_before = ncmds;
                char filtered[16][1024];
                int nfiltered = 0;
                for (int ci = 0; ci < ncmds; ci++) {
                    if (ai_command_is_readonly(cmds[ci])) {
                        memcpy(filtered[nfiltered], cmds[ci],
                               sizeof(filtered[0]));
                        nfiltered++;
                    } else {
                        chat_append_ops(d->hDisplay,
                            "  [blocked: ");
                        chat_append_ops(d->hDisplay,
                            cmds[ci]);
                        chat_append_ops(d->hDisplay,
                            "]\r\n");
                    }
                }
                /* Inform the AI about blocked commands so it doesn't
                 * falsely claim they were executed */
                if (nfiltered < ncmds_before) {
                    char bmsg[2048];
                    int bp = snprintf(bmsg, sizeof(bmsg),
                        "NOTE: The following commands were BLOCKED by "
                        "the user's read-only security policy and were "
                        "NOT executed:\n");
                    for (int ci = 0; ci < ncmds; ci++) {
                        if (!ai_command_is_readonly(cmds[ci]))
                            bp += snprintf(bmsg + bp,
                                sizeof(bmsg) - (size_t)bp,
                                "  - %s\n", cmds[ci]);
                    }
                    snprintf(bmsg + bp, sizeof(bmsg) - (size_t)bp,
                        "Do NOT claim these commands were executed. "
                        "If the user needs these actions, tell them "
                        "to enable 'Permit Write' and try again.");
                    EnterCriticalSection(&d->cs);
                    ai_conv_add(&d->conv, AI_ROLE_USER, bmsg);
                    LeaveCriticalSection(&d->cs);
                }
                memcpy(cmds, filtered,
                       (size_t)nfiltered * sizeof(cmds[0]));
                ncmds = nfiltered;
            }

            if (ncmds > 0) {
                /* Build a single confirmation dialog listing all commands */
                char confirm[4096];
                size_t clen = ai_build_confirm_text(cmds, ncmds,
                                                     confirm, sizeof(confirm));
                if (clen == 0)
                    snprintf(confirm, sizeof(confirm), "Execute %d command(s)?", ncmds);

                int result = MessageBox(hwnd, confirm, "Execute Commands",
                                       MB_YESNO | MB_ICONQUESTION);
                if (result == IDYES) {
                    chat_append_ops(d->hDisplay,
                                    "\r\n--- Commands ---\r\n");
                    /* Queue commands for delayed execution */
                    memcpy(d->queued_cmds, cmds,
                           (size_t)ncmds * sizeof(cmds[0]));
                    d->queued_count = ncmds;
                    d->queued_next = 0;
                    /* Execute first command immediately */
                    execute_command(d, d->queued_cmds[0]);
                    d->queued_next = 1;
                    if (ncmds > 1 && d->paste_delay_ms > 0) {
                        SetTimer(hwnd, TIMER_CMD_QUEUE,
                                 (UINT)d->paste_delay_ms, NULL);
                    } else {
                        /* No delay: execute remaining immediately */
                        for (int ci = 1; ci < ncmds; ci++)
                            execute_command(d, d->queued_cmds[ci]);
                        d->queued_next = ncmds;
                        d->commands_executed = ncmds;
                        SetTimer(hwnd, TIMER_CONTINUE,
                                 CONTINUE_DELAY_MS, NULL);
                    }
                } else {
                    chat_append_ops(d->hDisplay,
                                    "\r\n  [all commands cancelled]\r\n");
                }
            }
        } else {
            /* Error */
            chat_append_ops(d->hDisplay,
                            "\r\n--- Error ---\r\n");
            chat_append_ops(d->hDisplay, text);
            chat_append_ops(d->hDisplay, "\r\n");
        }

        free(text);
        return 0;
    }

    case WM_TIMER:
        if (!d) return 0;
        if (wParam == TIMER_CMD_QUEUE) {
            /* Execute next queued command with paste delay */
            if (d->queued_next < d->queued_count) {
                execute_command(d, d->queued_cmds[d->queued_next]);
                d->queued_next++;
            }
            if (d->queued_next >= d->queued_count) {
                /* All commands executed — stop timer, start continue */
                KillTimer(hwnd, TIMER_CMD_QUEUE);
                d->commands_executed = d->queued_count;
                SetTimer(hwnd, TIMER_CONTINUE, CONTINUE_DELAY_MS, NULL);
            }
        } else if (wParam == TIMER_CONTINUE) {
            KillTimer(hwnd, TIMER_CONTINUE);
            if (d->commands_executed > 0 && !d->busy) {
                d->commands_executed = 0;
                send_continue_message(d);
            }
        }
        return 0;

    case WM_ERASEBKGND:
        if (d && d->theme) {
            HDC hdc_bg = (HDC)wParam;
            RECT rc_bg;
            GetClientRect(hwnd, &rc_bg);
            FillRect(hdc_bg, &rc_bg, d->hBrBgPrimary);
            return 1;
        }
        break;

    case WM_DRAWITEM:
        if (d && d->theme) {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            int is_primary = ((int)dis->CtlID == IDC_CHAT_SEND);
            draw_themed_button(dis, d->theme, is_primary);
            return TRUE;
        }
        break;

    case WM_CTLCOLOREDIT:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_secondary));
            return (LRESULT)d->hBrBgSecondary;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_primary));
            return (LRESULT)d->hBrBgPrimary;
        }
        break;

    case WM_DESTROY:
        if (d) {
            DeleteCriticalSection(&d->cs);
            if (d->hFont) DeleteObject(d->hFont);
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

void ai_chat_init(HINSTANCE hInstance)
{
    /* Load RichEdit control library.
     * Riched20.dll registers "RichEdit20A"; Msftedit.dll registers
     * "RICHEDIT50W" and may also register 20A on some systems.
     * Load both to ensure RichEdit20A is always available. */
    LoadLibrary("Riched20.dll");
    LoadLibrary("Msftedit.dll");

    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = AiChatWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = AI_CHAT_CLASS;
    RegisterClassEx(&wc);
}

HWND ai_chat_show(HWND parent, const char *api_key, const char *provider,
                  const char *custom_url, const char *custom_model,
                  int paste_delay_ms, const char *font_name,
                  const char *colour_scheme)
{
    AiChatData *d = (AiChatData *)calloc(1, sizeof(AiChatData));
    if (!d) return NULL;

    InitializeCriticalSection(&d->cs);
    d->indicator_pos = -1;
    d->paste_delay_ms = paste_delay_ms;
    if (font_name && font_name[0])
        strncpy(d->font_name, font_name, sizeof(d->font_name) - 1);
    else
        strncpy(d->font_name, APP_FONT_DEFAULT, sizeof(d->font_name) - 1);

    /* Theme lookup */
    {
        int idx = ui_theme_find(colour_scheme ? colour_scheme : "");
        d->theme = ui_theme_get(idx);
        d->hBrBgPrimary   = CreateSolidBrush(theme_cr(d->theme->bg_primary));
        d->hBrBgSecondary = CreateSolidBrush(theme_cr(d->theme->bg_secondary));
    }

    if (api_key)
        strncpy(d->api_key, api_key, sizeof(d->api_key) - 1);
    if (provider)
        strncpy(d->provider, provider, sizeof(d->provider) - 1);
    if (custom_url)
        strncpy(d->custom_url, custom_url, sizeof(d->custom_url) - 1);
    if (custom_model)
        strncpy(d->custom_model, custom_model, sizeof(d->custom_model) - 1);

    /* Initialize conversation with user-selected model (if set),
     * otherwise fall back to provider default */
    const char *model = (custom_model && custom_model[0])
                        ? custom_model : ai_provider_model(provider);
    ai_conv_init(&d->conv, model ? model : "deepseek-chat");

    HWND hwnd = CreateWindowEx(
        0, AI_CHAT_CLASS, "AI Chat",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 600,
        parent, NULL, GetModuleHandle(NULL), d);

    if (!hwnd) {
        DeleteCriticalSection(&d->cs);
        free(d);
    }

    return hwnd;
}

void ai_chat_set_session(HWND hwnd, Terminal *term, SSHChannel *channel)
{
    if (!hwnd) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->active_term = term;
    d->active_channel = channel;
}

void ai_chat_update_key(HWND hwnd, const char *api_key, const char *provider,
                        const char *custom_url, const char *custom_model)
{
    if (!hwnd) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    EnterCriticalSection(&d->cs);
    if (api_key)
        snprintf(d->api_key, sizeof(d->api_key), "%s", api_key);
    if (custom_url)
        snprintf(d->custom_url, sizeof(d->custom_url), "%s", custom_url);
    if (custom_model)
        snprintf(d->custom_model, sizeof(d->custom_model), "%s", custom_model);
    if (provider) {
        snprintf(d->provider, sizeof(d->provider), "%s", provider);
        const char *model = (custom_model && custom_model[0])
                            ? custom_model : ai_provider_model(provider);
        if (model)
            snprintf(d->conv.model, sizeof(d->conv.model), "%s", model);
    }
    LeaveCriticalSection(&d->cs);
}

void ai_chat_close(HWND hwnd)
{
    if (hwnd && IsWindow(hwnd))
        DestroyWindow(hwnd);
}

#endif /* _WIN32 */
