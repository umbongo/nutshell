#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat.h"

#ifdef _WIN32

#include "ai_prompt.h"
#include "ai_http.h"
#include "term_extract.h"
#include "ssh_channel.h"
#include <stdio.h>
#include <string.h>
#include <process.h>

static const char *AI_CHAT_CLASS = "Nutshell_AIChat";

#define IDC_CHAT_DISPLAY 2001
#define IDC_CHAT_INPUT   2002
#define IDC_CHAT_SEND    2003

#define WM_AI_RESPONSE   (WM_USER + 100)

#define TERM_CONTEXT_ROWS 50

typedef struct {
    HWND hwnd;
    HWND hDisplay;
    HWND hInput;
    HWND hSendBtn;
    HFONT hFont;

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
} AiChatData;

/* Append text to the chat display (thread-safe via PostMessage) */
static void chat_append(HWND hDisplay, const char *text)
{
    int len = GetWindowTextLength(hDisplay);
    SendMessage(hDisplay, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hDisplay, EM_REPLACESEL, FALSE, (LPARAM)text);
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
        ai_http_response_free(&resp);
        PostMessage(d->hwnd, WM_AI_RESPONSE, 0, (LPARAM)_strdup("Error: failed to parse AI response"));
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
    chat_append(d->hDisplay, "\r\nYou: ");
    chat_append(d->hDisplay, input);
    chat_append(d->hDisplay, "\r\n");

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

    /* Show "thinking..." indicator */
    chat_append(d->hDisplay, "AI: (thinking...)\r\n");

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

    chat_append(d->hDisplay, "[Executed: ");
    chat_append(d->hDisplay, cmd);
    chat_append(d->hDisplay, "]\r\n");
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

        /* Chat display: read-only multiline edit */
        nd->hDisplay = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_VISIBLE | WS_CHILD | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            5, 5, cw - 10, ch - 40,
            hwnd, (HMENU)IDC_CHAT_DISPLAY, NULL, NULL);

        /* Input field */
        nd->hInput = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            5, ch - 30, cw - 55, 23,
            hwnd, (HMENU)IDC_CHAT_INPUT, NULL, NULL);

        /* Send button */
        nd->hSendBtn = CreateWindow("BUTTON", ">",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            cw - 45, ch - 30, 40, 23,
            hwnd, (HMENU)IDC_CHAT_SEND, NULL, NULL);

        /* Font */
        HDC hdc = GetDC(hwnd);
        int h = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(hwnd, hdc);
        nd->hFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Consolas");
        if (nd->hFont) {
            SendMessage(nd->hDisplay, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
            SendMessage(nd->hInput, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
            SendMessage(nd->hSendBtn, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
        }

        chat_append(nd->hDisplay,
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
        MoveWindow(d->hDisplay, 5, 5, cw - 10, ch - 40, TRUE);
        MoveWindow(d->hInput, 5, ch - 30, cw - 55, 23, TRUE);
        MoveWindow(d->hSendBtn, cw - 45, ch - 30, 40, 23, TRUE);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHAT_SEND:
            send_user_message(d);
            SetFocus(d->hInput);
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

        /* Remove "thinking..." indicator */
        {
            int tlen = GetWindowTextLength(d->hDisplay);
            char *tbuf = (char *)malloc((size_t)tlen + 1);
            if (tbuf) {
                GetWindowText(d->hDisplay, tbuf, tlen + 1);
                char *thinking = strstr(tbuf, "AI: (thinking...)\r\n");
                if (thinking) {
                    char *after = thinking + strlen("AI: (thinking...)\r\n");
                    memmove(thinking, after, strlen(after) + 1);
                    SetWindowText(d->hDisplay, tbuf);
                }
                free(tbuf);
            }
        }

        if (wParam == 1) {
            chat_append(d->hDisplay, "AI: ");
            chat_append(d->hDisplay, text);
            chat_append(d->hDisplay, "\r\n");

            /* Check for executable command */
            char cmd[1024];
            if (ai_extract_command(text, cmd, sizeof(cmd))) {
                char confirm[1200];
                (void)snprintf(confirm, sizeof(confirm),
                    "The AI wants to execute:\n\n%s\n\nAllow?", cmd);
                int result = MessageBox(hwnd, confirm, "Execute Command",
                                       MB_YESNO | MB_ICONQUESTION);
                if (result == IDYES) {
                    execute_command(d, cmd);
                } else {
                    chat_append(d->hDisplay, "[Command cancelled by user]\r\n");
                }
            }
        } else {
            /* Error */
            chat_append(d->hDisplay, "Error: ");
            chat_append(d->hDisplay, text);
            chat_append(d->hDisplay, "\r\n");
        }

        free(text);
        return 0;
    }

    case WM_DESTROY:
        if (d) {
            DeleteCriticalSection(&d->cs);
            if (d->hFont) DeleteObject(d->hFont);
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
    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = AiChatWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = AI_CHAT_CLASS;
    RegisterClassEx(&wc);
}

HWND ai_chat_show(HWND parent, const char *api_key, const char *provider,
                  const char *custom_url, const char *custom_model)
{
    AiChatData *d = (AiChatData *)calloc(1, sizeof(AiChatData));
    if (!d) return NULL;

    InitializeCriticalSection(&d->cs);

    if (api_key)
        strncpy(d->api_key, api_key, sizeof(d->api_key) - 1);
    if (provider)
        strncpy(d->provider, provider, sizeof(d->provider) - 1);
    if (custom_url)
        strncpy(d->custom_url, custom_url, sizeof(d->custom_url) - 1);
    if (custom_model)
        strncpy(d->custom_model, custom_model, sizeof(d->custom_model) - 1);

    /* Initialize conversation with the provider's default model,
     * or custom model if provider is "custom" */
    const char *model = ai_provider_model(provider);
    if (!model && custom_model && custom_model[0])
        model = custom_model;
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
        const char *model = ai_provider_model(provider);
        if (!model && custom_model && custom_model[0])
            model = custom_model;
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
