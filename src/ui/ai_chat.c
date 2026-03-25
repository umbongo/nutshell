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
#include "custom_scrollbar.h"
#include "edit_scroll.h"
#include "term_extract.h"
#include "ssh_channel.h"
#include "resource.h"
#include "ai_dock.h"
#include "string_utils.h"
#include <windowsx.h>  /* GET_X_LPARAM, GET_Y_LPARAM */
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <richedit.h>
#include <commctrl.h>
#include <commdlg.h>

static const char *AI_CHAT_CLASS = "Nutshell_AIChat";

#define IDC_CHAT_DISPLAY  2001
#define IDC_CHAT_INPUT    2002
#define IDC_CHAT_SEND     2003
#define IDC_CHAT_NEWCHAT  2004
#define IDC_CHAT_PERMIT   2005
#define IDC_CHAT_THINKING 2006
#define IDC_CONTEXT_BAR   2007
#define IDC_CONTEXT_LABEL 2008
#define IDC_SESSION_LABEL 2009
#define IDC_CHAT_SAVE     2010
#define IDC_CHAT_ALLOW    2011
#define IDC_THINKING_BOX  2014
#define IDC_CHAT_DENY     2012
#define IDC_CHAT_UNDOCK   2013

#define WM_AI_RESPONSE   (WM_USER + 100)
#define WM_AI_CONTINUE   (WM_USER + 101)
#define WM_AI_STREAM     (WM_USER + 102)  /* wParam: 0=thinking, 1=content; lParam: char* */

#define TERM_CONTEXT_ROWS 50
#define CONTINUE_DELAY_MS 2000  /* Wait for terminal output before continuing */
#define TIMER_CONTINUE    1
#define TIMER_CMD_QUEUE   2     /* Delayed command execution (paste delay) */
#define TIMER_SCROLL_SYNC 4     /* Sync custom scrollbar with RichEdit */
#define TIMER_THINKING    5     /* Animated thinking indicator */
#define THINKING_ANIM_MS  400   /* Dot animation interval */

/* Forward declaration for input subclass */
static LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData);

/* Subclass for the RichEdit display: forward mouse wheel to parent */
static LRESULT CALLBACK DisplaySubclassProc(HWND hwnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam,
                                             UINT_PTR uIdSubclass,
                                             DWORD_PTR dwRefData)
{
    (void)dwRefData;
    if (msg == WM_MOUSEWHEEL) {
        HWND hParent = GetParent(hwnd);
        if (hParent)
            return SendMessage(hParent, WM_MOUSEWHEEL, wParam, lParam);
        return 0;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, DisplaySubclassProc, uIdSubclass);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

typedef struct {
    HWND hwnd;
    HWND hDisplay;
    HWND hThinkingBox;  /* Embedded textbox for thinking/processing content */
    HWND hInput;
    HWND hSendBtn;
    HWND hNewChatBtn;
    HWND hPermitBtn;
    HWND hSaveBtn;
    HWND hUndockBtn;
    HWND hAllowBtn;
    HWND hDenyBtn;
    HWND hThinkingBtn;
    HWND hTooltip;        /* Win32 tooltip control */
    int permit_write;     /* 0 = read-only (red), 1 = read/write (green) */
    int show_thinking;    /* 0 = hide reasoning, 1 = show reasoning */
    HFONT hFont;
    HFONT hSmallFont;     /* small bold font for indicator label */
    char font_name[64];
    char ai_font_name[64];   /* font for AI markdown code blocks */
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
    CRITICAL_SECTION cs;

    /* Auto-continue: when AI only gives partial commands, re-prompt */
    int commands_executed;   /* number of commands just executed */
    char pending_request[2048]; /* original user request for context */

    /* Position of the animated indicator so we can remove/update it */
    int indicator_pos;  /* char offset where indicator starts, or -1 */
    int thinking_tick;  /* animation frame counter 0-2 for dot cycling */
    char indicator_base[64]; /* base text without dots, e.g. "thinking" */

    /* Batch command execution with paste delay */
    int paste_delay_ms;
    char queued_cmds[16][1024];
    int queued_count;
    int queued_next;       /* index of next command to execute */
    int pending_approval;  /* 1 = waiting for user to Allow/Deny commands */
    int dpi;
    int stream_phase;  /* 0=not started, 1=in thinking, 2=in content */

    /* AI notes for system prompt context */
    char session_notes[2560];
    char system_notes[2560];

    /* Per-session conversation tracking */
    AiSessionState *active_state;     /* points to current session's ai_state */

    /* Session name label */
    HWND hSessionLabel;
    char session_name[256];

    /* Custom scrollbar for chat display */
    HWND hDisplayScrollbar;
    int  display_line_h;   /* cached line height in px */

    /* Context window usage bar */
    HWND hContextBar;
    HWND hContextLabel;
    int  context_limit;  /* token limit for model, 0=unknown */

    /* Thinking history: per-assistant-message thinking text.
     * Indexed by conv.messages[] index (only meaningful for ASSISTANT roles).
     * Heap-allocated strings; NULL if no thinking for that message. */
    char *thinking_history[AI_MAX_MESSAGES];

    /* Live stream thinking accumulation (UI thread only) */
    char stream_thinking[AI_MSG_MAX];
    size_t stream_thinking_len;

    /* Live stream content accumulation (for rebuild mid-stream) */
    char stream_content[AI_MSG_MAX];
    size_t stream_content_len;

    /* RichEdit char position where AI response text began (for markdown re-render) */
    int stream_display_start;

    /* Wheel delta accumulator for high-precision scroll devices */
    int wheel_accum;

    /* Current UI font size (points) for zoom — starts at APP_FONT_UI_SIZE */
    int ui_font_size;

    /* Custom scrollbar for input text box */
    HWND hInputScrollbar;
    int  input_line_h;     /* cached line height in px for input */

    /* Docked mode: 1 = child window in main frame, 0 = floating */
    int docked;

    /* Compact button mode: 1 = icon-only buttons when frame is narrow */
    int compact_buttons;

    /* Fluent UI Icon Font */
    HFONT hIconFont;
} AiChatData;

/* Helper: check if the currently active session has a busy AI stream */
#define ACTIVE_BUSY(d) ((d)->active_state && (d)->active_state->busy)

/* Forward declarations */
static void do_session_switch(AiChatData *d,
                              AiSessionState *new_state,
                              Terminal *term, SSHChannel *channel,
                              const char *session_notes,
                              const char *system_notes,
                              const char *session_name);

/* Append colored text to the RichEdit chat display.
 * Auto-scrolls to the bottom only if the user was already at the bottom.
 * If italic != 0, the text is rendered in italic. */
static void chat_append_styled(HWND hDisplay, const char *text,
                                COLORREF color, int italic)
{
    /* Check if scrolled to bottom before appending.
     * Use EM_ messages which work with or without WS_VSCROLL. */
    int total_lns = (int)SendMessage(hDisplay, EM_GETLINECOUNT, 0, 0);
    int first_vis = (int)SendMessage(hDisplay, EM_GETFIRSTVISIBLELINE, 0, 0);
    RECT rc_disp;
    GetClientRect(hDisplay, &rc_disp);
    /* Estimate visible lines from client height / font height (approx) */
    int font_h = 1;
    {
        HDC hdc_a = GetDC(hDisplay);
        TEXTMETRIC tm_a;
        GetTextMetrics(hdc_a, &tm_a);
        font_h = tm_a.tmHeight + tm_a.tmExternalLeading;
        ReleaseDC(hDisplay, hdc_a);
    }
    int vis_lns = (font_h > 0) ? ((rc_disp.bottom - rc_disp.top) / font_h) : 1;
    int was_at_bottom = (first_vis + vis_lns + 2 >= total_lns) || (total_lns <= vis_lns);

    SendMessage(hDisplay, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    CHARFORMAT2 cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_ITALIC | CFM_BOLD | CFM_STRIKEOUT;
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
        SendMessage(hDisplay, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessage(hDisplay, EM_SCROLLCARET, 0, 0);
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

/* Extended styled append: supports bold, font face, font size, background color.
 * effects: 0 or combination of CFE_BOLD | CFE_ITALIC | CFE_STRIKEOUT
 * font: NULL to keep current; fontSize: twips, 0 to keep current.
 * bgColor: CLR_DEFAULT to skip background coloring. */
static void chat_append_styled_ex(HWND hDisplay, const char *text,
                                   COLORREF color, COLORREF bgColor,
                                   DWORD effects, const char *font,
                                   int fontSize)
{
    /* Auto-scroll detection (same as chat_append_styled) */
    int total_lns = (int)SendMessage(hDisplay, EM_GETLINECOUNT, 0, 0);
    int first_vis = (int)SendMessage(hDisplay, EM_GETFIRSTVISIBLELINE, 0, 0);
    RECT rc_disp;
    GetClientRect(hDisplay, &rc_disp);
    int font_h = 1;
    {
        HDC hdc_a = GetDC(hDisplay);
        TEXTMETRIC tm_a;
        GetTextMetrics(hdc_a, &tm_a);
        font_h = tm_a.tmHeight + tm_a.tmExternalLeading;
        ReleaseDC(hDisplay, hdc_a);
    }
    int vis_lns = (font_h > 0) ? ((rc_disp.bottom - rc_disp.top) / font_h) : 1;
    int was_at_bottom = (first_vis + vis_lns + 2 >= total_lns) || (total_lns <= vis_lns);

    SendMessage(hDisplay, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    CHARFORMAT2 cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    /* Include underline in mask so we can explicitly control it */
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_STRIKEOUT | CFM_LINK | CFM_UNDERLINE;
    cf.crTextColor = color;
    /* Copy effects but force underline OFF - Windows tends to add underline to links automatically */
    cf.dwEffects = effects;
    /* Explicitly clear underline bit */
    cf.dwEffects &= ~((DWORD)CFE_UNDERLINE);

    if (bgColor != CLR_DEFAULT) {
        cf.dwMask |= CFM_BACKCOLOR;
        cf.crBackColor = bgColor;
    }
    if (font) {
        cf.dwMask |= CFM_FACE;
        strncpy(cf.szFaceName, font, LF_FACESIZE - 1);
        cf.szFaceName[LF_FACESIZE - 1] = '\0';
    }
    if (fontSize > 0) {
        cf.dwMask |= CFM_SIZE;
        cf.yHeight = fontSize;
    }
    SendMessage(hDisplay, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

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
        SendMessage(hDisplay, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessage(hDisplay, EM_SCROLLCARET, 0, 0);
    }
}

#include "markdown.h"

/* Render a markdown-formatted AI response into the RichEdit display.
 * Handles block-level elements (headings, code blocks, lists, tables,
 * horizontal rules, blockquotes) and inline formatting (bold, italic,
 * code, strikethrough). */
static void chat_append_markdown(HWND hDisplay, const char *raw,
                                  COLORREF baseColor,
                                  const ThemeColors *theme,
                                  const char *code_font)
{
    if (!raw || !raw[0]) return;

    COLORREF col_dim = theme ? theme_cr(theme->text_dim) : RGB(140, 140, 140);
    if (!code_font || !code_font[0]) code_font = "Consolas";

    /* Split into lines (work on a copy since we modify in-place) */
    size_t raw_len = strlen(raw);
    char *buf = (char *)malloc(raw_len + 2);
    if (!buf) return;
    memcpy(buf, raw, raw_len + 1);

    int in_code_block = 0;
    char *p = buf;

    while (*p) {
        /* Extract one line */
        char *eol = strchr(p, '\n');
        if (eol) *eol = '\0';

        /* Strip trailing \r */
        size_t llen = strlen(p);
        if (llen > 0 && p[llen - 1] == '\r') p[--llen] = '\0';

        MdLineInfo info = md_classify_line(p, in_code_block);

        switch (info.type) {
        case MD_LINE_EMPTY:
            chat_append_styled_ex(hDisplay, "\r\n", baseColor, CLR_DEFAULT,
                                  0, NULL, 0);
            break;

        case MD_LINE_CODE_FENCE:
            in_code_block = !in_code_block;
            /* Don't render the fence line itself */
            break;

        case MD_LINE_CODE:
            chat_append_styled_ex(hDisplay, p, baseColor, CLR_DEFAULT,
                                  0, code_font, 0);
            chat_append_styled_ex(hDisplay, "\r\n", baseColor, CLR_DEFAULT,
                                  0, NULL, 0);
            break;

        case MD_LINE_HEADING: {
            int twips = 0;
            switch (info.heading_level) {
            case 1: twips = 280; break;
            case 2: twips = 240; break;
            case 3: twips = 210; break;
            default: twips = 200; break;
            }
            /* Parse inline spans to strip markers like ** from heading text */
            const char *h_text = p + info.content_offset;
            MdSpan h_spans[MD_MAX_SPANS];
            int nh = md_parse_inline(h_text, (int)strlen(h_text), h_spans);
            for (int s = 0; s < nh; s++) {
                char tmp[2048];
                int slen = h_spans[s].end - h_spans[s].start;
                if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
                memcpy(tmp, h_text + h_spans[s].start, (size_t)slen);
                tmp[slen] = '\0';
                DWORD eff = CFE_BOLD; /* headings are always bold */
                const char *sfont = NULL;
                if (h_spans[s].type == MD_SPAN_CODE)
                    sfont = code_font;
                else if (h_spans[s].type == MD_SPAN_ITALIC ||
                         h_spans[s].type == MD_SPAN_BOLD_ITALIC)
                    eff |= CFE_ITALIC;
                chat_append_styled_ex(hDisplay, tmp, baseColor, CLR_DEFAULT,
                                      eff, sfont, twips);
            }
            chat_append_styled_ex(hDisplay, "\r\n", baseColor, CLR_DEFAULT,
                                  0, NULL, 0);
            break;
        }

        case MD_LINE_HRULE:
            chat_append_styled_ex(hDisplay,
                "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
                "\r\n",
                col_dim, CLR_DEFAULT, 0, NULL, 0);
            break;

        case MD_LINE_ULIST:
            chat_append_styled_ex(hDisplay, "  \xe2\x80\xa2  ", baseColor,
                                  CLR_DEFAULT, 0, NULL, 0);
            /* fall through to render inline content */
            info.content_offset = info.content_offset; /* use content_offset */
            goto render_inline;

        case MD_LINE_OLIST: {
            /* Render "  N.  " prefix */
            char prefix[16];
            int nlen = info.content_offset - 2; /* digits + dot */
            snprintf(prefix, sizeof(prefix), "  %.*s  ", nlen, p);
            chat_append_styled_ex(hDisplay, prefix, baseColor,
                                  CLR_DEFAULT, 0, NULL, 0);
            goto render_inline;
        }

        case MD_LINE_TABLE:
            /* Render tables in monospace so columns align */
            chat_append_styled_ex(hDisplay, p, baseColor, CLR_DEFAULT,
                                  0, code_font, 0);
            chat_append_styled_ex(hDisplay, "\r\n", baseColor, CLR_DEFAULT,
                                  0, NULL, 0);
            break;

        case MD_LINE_BLOCKQUOTE: {
            const char *bq_text = p + info.content_offset;
            COLORREF col_exec = RGB(255, 140, 0); /* orange */
            /* Exec/Quotes */
            chat_append_styled_ex(hDisplay,
                "\xe2\x96\xb6 ", col_exec, CLR_DEFAULT, CFE_BOLD, code_font, 180);
            /* Render inline formatting within blockquote */
            MdSpan bq_spans[MD_MAX_SPANS];
            int nbq = md_parse_inline(bq_text, (int)strlen(bq_text), bq_spans);
            for (int s = 0; s < nbq; s++) {
                char tmp[2048];
                int slen = bq_spans[s].end - bq_spans[s].start;
                if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
                memcpy(tmp, bq_text + bq_spans[s].start, (size_t)slen);
                tmp[slen] = '\0';
                DWORD eff = 0; /* blockquotes non-italic so executed commands are visible */
                const char *sfont = code_font;
                switch (bq_spans[s].type) {
                case MD_SPAN_BOLD:        eff |= CFE_BOLD; break;
                case MD_SPAN_BOLD_ITALIC: eff |= CFE_BOLD; break;
                case MD_SPAN_STRIKETHROUGH: eff |= CFE_STRIKEOUT; break;
                default: break;
                }
                chat_append_styled_ex(hDisplay, tmp, col_exec, CLR_DEFAULT,
                                      eff, sfont, 0);
            }
            chat_append_styled_ex(hDisplay, "\r\n", baseColor, CLR_DEFAULT,
                                  0, NULL, 0);
            break;
        }

        case MD_LINE_PARAGRAPH:
        default:
        render_inline: {
            /* Parse and render inline spans */
            const char *il_text = p + info.content_offset;
            MdSpan spans[MD_MAX_SPANS];
            int nspans = md_parse_inline(il_text, (int)strlen(il_text), spans);
            for (int s = 0; s < nspans; s++) {
                char tmp[2048];
                int slen = spans[s].end - spans[s].start;
                if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
                memcpy(tmp, il_text + spans[s].start, (size_t)slen);
                tmp[slen] = '\0';
                DWORD eff = 0;
                const char *sfont = NULL;
                switch (spans[s].type) {
                case MD_SPAN_BOLD:        eff = CFE_BOLD; break;
                case MD_SPAN_ITALIC:      eff = CFE_ITALIC; break;
                case MD_SPAN_BOLD_ITALIC: eff = CFE_BOLD | CFE_ITALIC; break;
                case MD_SPAN_CODE:        sfont = code_font; break;
                case MD_SPAN_STRIKETHROUGH: eff = CFE_STRIKEOUT; break;
                default: break;
                }
                chat_append_styled_ex(hDisplay, tmp, baseColor,
                                      CLR_DEFAULT, eff, sfont, 0);
            }
            chat_append_styled_ex(hDisplay, "\r\n", baseColor,
                                  CLR_DEFAULT, 0, NULL, 0);
            break;
        }
        } /* switch */

        if (eol)
            p = eol + 1;
        else
            break;
    }

    free(buf);
}

/* Format AI response for display:
 *   - Convert \n to \r\n for Win32 EDIT control
 *   - Replace [EXEC]cmd[/EXEC] with "  > cmd" blocks
 *   - Result must be freed by caller */
__attribute__((unused)) static char *format_ai_text(const char *raw)
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

/* Heap-allocated struct to pass both content and thinking from thread to UI.
 * Also carries the target session so the UI thread can route the response
 * to the correct session when multiple streams run concurrently. */
typedef struct {
    AiSessionState *session;   /* which session this response is for */
    char *content;
    char *thinking;
} AiResponseMsg;

/* Heap-allocated chunk posted via WM_AI_STREAM.  Replaces the old plain
 * char* lParam so the UI thread can tell which session the chunk belongs to. */
typedef struct {
    AiSessionState *session;
    char *delta;
} AiStreamChunk;

/* Thread argument: everything the background thread needs, decoupled from
 * AiChatData so multiple threads can run for different sessions. */
typedef struct {
    HWND hwnd;                      /* target window for PostMessage */
    AiSessionState *target;         /* session this request is for */
    CRITICAL_SECTION *cs;           /* shared CS for conv writes */
    char api_key[256];
    char provider[64];
    char custom_url[256];
    char body[AI_BODY_MAX];         /* pre-built JSON request body */
    size_t body_len;
} AiStreamThreadArg;

/* Context for SSE streaming callback */
typedef struct {
    HWND hwnd;                   /* target window for PostMessage */
    AiSessionState *target;      /* session this stream belongs to */
    char line_buf[8192];     /* SSE line accumulation buffer */
    size_t line_len;
    char full_content[AI_MSG_MAX];   /* accumulated full content */
    size_t content_len;
    char full_thinking[AI_MSG_MAX];  /* accumulated full thinking */
    size_t thinking_len;
    int in_thinking;         /* 1 while receiving thinking chunks */
    int header_sent;         /* bitmask: 1=thinking header, 2=content header */
} StreamContext;

/* Process a single SSE line from the stream */
static void stream_process_line(StreamContext *ctx, const char *line, size_t len)
{
    /* Skip empty lines */
    if (len == 0) return;

    /* SSE lines start with "data: " */
    if (len < 6 || strncmp(line, "data: ", 6) != 0) return;

    const char *json = line + 6;
    char content_delta[1024] = "";
    char thinking_delta[1024] = "";

    int rc = ai_parse_stream_chunk(json, content_delta, sizeof(content_delta),
                                   thinking_delta, sizeof(thinking_delta));

    if (rc == 1) {
        /* [DONE] — stream finished */
        return;
    }
    if (rc < 0) return;

    /* Post thinking delta to UI */
    if (thinking_delta[0]) {
        /* Accumulate */
        size_t dlen = strlen(thinking_delta);
        if (ctx->thinking_len + dlen < AI_MSG_MAX - 1) {
            memcpy(ctx->full_thinking + ctx->thinking_len, thinking_delta, dlen);
            ctx->thinking_len += dlen;
            ctx->full_thinking[ctx->thinking_len] = '\0';
        }
        /* Post to UI for realtime display */
        AiStreamChunk *chunk = calloc(1, sizeof(*chunk));
        if (chunk) {
            chunk->session = ctx->target;
            chunk->delta = _strdup(thinking_delta);
            PostMessage(ctx->hwnd, WM_AI_STREAM, 0, (LPARAM)chunk);
        }
    }

    /* Post content delta to UI */
    if (content_delta[0]) {
        /* Accumulate */
        size_t dlen = strlen(content_delta);
        if (ctx->content_len + dlen < AI_MSG_MAX - 1) {
            memcpy(ctx->full_content + ctx->content_len, content_delta, dlen);
            ctx->content_len += dlen;
            ctx->full_content[ctx->content_len] = '\0';
        }
        /* Post to UI for realtime display */
        AiStreamChunk *chunk = calloc(1, sizeof(*chunk));
        if (chunk) {
            chunk->session = ctx->target;
            chunk->delta = _strdup(content_delta);
            PostMessage(ctx->hwnd, WM_AI_STREAM, 1, (LPARAM)chunk);
        }
    }
}

/* SSE stream callback — accumulates lines and processes them */
static int stream_callback(const char *data, size_t len, void *userdata)
{
    StreamContext *ctx = (StreamContext *)userdata;

    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            /* Strip trailing \r */
            if (ctx->line_len > 0 && ctx->line_buf[ctx->line_len - 1] == '\r')
                ctx->line_len--;
            ctx->line_buf[ctx->line_len] = '\0';
            stream_process_line(ctx, ctx->line_buf, ctx->line_len);
            ctx->line_len = 0;
        } else {
            if (ctx->line_len < sizeof(ctx->line_buf) - 1)
                ctx->line_buf[ctx->line_len++] = c;
        }
    }
    return 0;
}

/* Helper: post an error AiResponseMsg tagged with the target session.
 * The WM_AI_RESPONSE handler is responsible for setting busy = 0. */
static void post_error_response(HWND hwnd, AiSessionState *target, const char *msg)
{
    AiResponseMsg *rmsg = (AiResponseMsg *)calloc(1, sizeof(*rmsg));
    if (rmsg) {
        rmsg->session = target;
        rmsg->content = _strdup(msg);
        PostMessage(hwnd, WM_AI_RESPONSE, 0, (LPARAM)rmsg);
    } else {
        /* Fallback: can't allocate — clear busy here since handler won't run */
        target->busy = 0;
    }
}

/* Background thread: streaming AI API call.
 * Receives a heap-allocated AiStreamThreadArg and frees it before returning. */
static unsigned __stdcall ai_stream_thread_proc(void *raw_arg)
{
    AiStreamThreadArg *arg = (AiStreamThreadArg *)raw_arg;

    if (arg->body_len == 0) {
        post_error_response(arg->hwnd, arg->target, "Error: failed to build request");
        free(arg);
        return 0;
    }

    const char *url = ai_provider_url(arg->provider);
    if (!url && strcmp(arg->provider, "custom") == 0 && arg->custom_url[0])
        url = arg->custom_url;
    if (!url) {
        post_error_response(arg->hwnd, arg->target, "Error: unknown AI provider");
        free(arg);
        return 0;
    }

    char auth[300];
    (void)snprintf(auth, sizeof(auth), "Bearer %s", arg->api_key);

    StreamContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.hwnd = arg->hwnd;
    ctx.target = arg->target;

    int status = 0;
    char errbuf[256] = "";
    int rc = ai_http_post_stream(url, auth, arg->body, arg->body_len,
                                 stream_callback, &ctx,
                                 &status, errbuf, sizeof(errbuf));

    if (rc != 0 || status < 200 || status >= 300) {
        char msg[1024];
        if (rc != 0 && errbuf[0])
            snprintf(msg, sizeof(msg), "HTTP error: %s", errbuf);
        else
            snprintf(msg, sizeof(msg), "HTTP %d: streaming request failed", status);
        post_error_response(arg->hwnd, arg->target, msg);
        free(arg);
        return 0;
    }

    /* Add assistant message to the target session's conversation. */
    EnterCriticalSection(arg->cs);
    ai_conv_add(&arg->target->conv, AI_ROLE_ASSISTANT, ctx.full_content);
    arg->target->valid = 1;
    LeaveCriticalSection(arg->cs);

    /* Signal stream done — wParam=2 means "streaming complete, do command extraction".
     * busy is cleared by the WM_AI_RESPONSE handler on the UI thread to prevent
     * a race where the user sends a new message before cleanup completes. */
    AiResponseMsg *rmsg = (AiResponseMsg *)calloc(1, sizeof(*rmsg));
    if (rmsg) {
        rmsg->session = arg->target;
        rmsg->content = _strdup(ctx.full_content);
        rmsg->thinking = (ctx.full_thinking[0] != '\0') ? _strdup(ctx.full_thinking) : NULL;
        PostMessage(arg->hwnd, WM_AI_RESPONSE, 2, (LPARAM)rmsg);
    } else {
        /* Fallback: can't allocate — clear busy here */
        arg->target->busy = 0;
    }
    free(arg);
    return 0;
}


static void update_context_bar(AiChatData *d)
{
    if (!d || !d->hContextBar) return;
    
    LONG style = GetWindowLong(d->hContextBar, GWL_STYLE);
    if (style & PBS_MARQUEE) {
        SendMessage(d->hContextBar, PBM_SETMARQUEE, 0, 0);
        SetWindowLong(d->hContextBar, GWL_STYLE, style & ~PBS_MARQUEE);
    }

    if (d->context_limit <= 0) {
        SendMessage(d->hContextBar, PBM_SETPOS, 0, 0);
        SetWindowText(d->hContextLabel, "Context: N/A");
        EnableWindow(d->hContextBar, FALSE);
        return;
    }
    int tokens = ai_context_estimate_tokens(&d->conv);
    int pct = (tokens * 100) / d->context_limit;
    if (pct > 100) pct = 100;
    SendMessage(d->hContextBar, PBM_SETPOS, (WPARAM)pct, 0);
    SendMessage(d->hContextBar, PBM_SETBARCOLOR, 0,
                (LPARAM)(pct > 80 ? RGB(220, 50, 50) :
                         pct > 50 ? RGB(220, 180, 50) : RGB(50, 180, 50)));
    char label[64];
    snprintf(label, sizeof(label), "Context: %d%%", pct);
    SetWindowText(d->hContextLabel, label);
    EnableWindow(d->hContextBar, TRUE);
}

/* Start (or replace) the animated indicator with the given base text.
 * Replaces the usual Context progress bar with a Marquee while busy. */
static void start_indicator(AiChatData *d, const char *base)
{
    if (d->hContextBar) {
        LONG style = GetWindowLong(d->hContextBar, GWL_STYLE);
        SetWindowLong(d->hContextBar, GWL_STYLE, style | PBS_MARQUEE);
        SendMessage(d->hContextBar, PBM_SETMARQUEE, 1, 50);
        /* Red/Orange color for Marquee */
        SendMessage(d->hContextBar, PBM_SETBARCOLOR, 0, (LPARAM)RGB(220, 100, 50));
        EnableWindow(d->hContextBar, TRUE);
    }
    if (d->hContextLabel) {
        char label[128];
        snprintf(label, sizeof(label), "%c%s",
            base[0] >= 'a' && base[0] <= 'z' ? (char)(base[0]-32) : base[0], base+1);
        if (strcmp(base, "thinking") == 0) {
            snprintf(label, sizeof(label), "Thinking... (Click to toggle)");
        }
        SetWindowText(d->hContextLabel, label);
    }
}

/* Free all thinking history entries. */
static void thinking_history_clear(AiChatData *d)
{
    for (int i = 0; i < AI_MAX_MESSAGES; i++) {
        free(d->thinking_history[i]);
        d->thinking_history[i] = NULL;
    }
    d->stream_thinking[0] = '\0';
    d->stream_thinking_len = 0;
}

/* Rebuild the chat display from the conversation history.
 * Used when switching sessions to replay the loaded conversation. */
static void chat_rebuild_display(AiChatData *d)
{
    if (!d || !d->hDisplay) return;

    SetWindowTextW(d->hDisplay, L"");

    /* Welcome header — same as WM_CREATE / IDC_CHAT_NEWCHAT */
    chat_append_ops(d->hDisplay,
        "AI Assist - Type a message and press Enter or click Send.\r\n"
        "The AI can see your terminal and execute commands.\r\n"
        "---\r\n");

    COLORREF col_user = d->theme ? theme_cr(d->theme->accent) : RGB(0, 120, 215);
    COLORREF col_ai   = d->theme ? theme_cr(d->theme->text_main)
                                 : GetSysColor(COLOR_WINDOWTEXT);

    /* Replay messages, skipping the system prompt at index 0 */
    for (int i = 1; i < d->conv.msg_count; i++) {
        const AiMessage *msg = &d->conv.messages[i];

        if (msg->role == AI_ROLE_USER) {
            chat_append_styled(d->hDisplay, "\r\n\r\n", col_user, 0);
            chat_append_styled_ex(d->hDisplay, "You\r\n", col_user, CLR_DEFAULT, CFE_BOLD, NULL, 220);
            chat_append_color(d->hDisplay, msg->content, col_user);
            chat_append_color(d->hDisplay, "\r\n", col_user);
        } else if (msg->role == AI_ROLE_ASSISTANT) {
            chat_append_styled(d->hDisplay, "\r\n\r\n", col_ai, 0);
            chat_append_styled_ex(d->hDisplay, "AI\r\n", RGB(0, 150, 200), CLR_DEFAULT, CFE_BOLD, NULL, 220);
            /* Show thinking indicator if it exists - thinking content shown in embedded textbox when clicked */
            if (d->thinking_history[i] && d->thinking_history[i][0]) {
                COLORREF col_purple = RGB(150, 100, 200);
                chat_append_styled_ex(d->hDisplay, "> Thinking...\r\n", col_purple, CLR_DEFAULT, CFE_BOLD | CFE_LINK, NULL, 180);
            }
            chat_append_markdown(d->hDisplay, msg->content, col_ai,
                                 d->theme, d->ai_font_name);
        }
        /* Skip system messages injected mid-conversation */
    }

    /* Scroll to bottom */
    SendMessage(d->hDisplay, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessage(d->hDisplay, EM_SCROLLCARET, 0, 0);
    update_context_bar(d);
}

/* Build an AiStreamThreadArg from current AiChatData state under the CS,
 * and launch the background thread.  Sets active_state->busy = 1. */
static void launch_stream_thread(AiChatData *d)
{
    AiStreamThreadArg *arg = (AiStreamThreadArg *)calloc(1, sizeof(*arg));
    if (!arg) return;

    arg->hwnd = d->hwnd;
    arg->target = d->active_state;
    arg->cs = &d->cs;

    EnterCriticalSection(&d->cs);
    strncpy(arg->api_key, d->api_key, sizeof(arg->api_key) - 1);
    strncpy(arg->provider, d->provider, sizeof(arg->provider) - 1);
    strncpy(arg->custom_url, d->custom_url, sizeof(arg->custom_url) - 1);
    arg->body_len = ai_build_request_body_ex(&d->conv, arg->body,
                                              sizeof(arg->body), 1);
    LeaveCriticalSection(&d->cs);

    /* Allocate per-session stream accumulators */
    free(d->active_state->stream_content);
    d->active_state->stream_content = (char *)calloc(1, AI_MSG_MAX);
    d->active_state->stream_content_len = 0;
    free(d->active_state->stream_thinking);
    d->active_state->stream_thinking = (char *)calloc(1, AI_MSG_MAX);
    d->active_state->stream_thinking_len = 0;
    d->active_state->stream_phase = 0;

    d->active_state->busy = 1;

    /* Reset display-side stream buffers */
    d->stream_thinking[0] = '\0';
    d->stream_thinking_len = 0;
    d->stream_content[0] = '\0';
    d->stream_content_len = 0;
    d->stream_phase = 0;

    /* Show initial processing indicator immediately so user sees activity */
    COLORREF col_purple = RGB(150, 100, 200);
    chat_append_styled(d->hDisplay, "\r\n\r\n", col_purple, 0);
    GETTEXTLENGTHEX gtx = { .flags = GTL_NUMCHARS, .codepage = 1200 };
    d->indicator_pos = (int)SendMessage(d->hDisplay, EM_GETTEXTLENGTHEX, (WPARAM)&gtx, 0);
    chat_append_styled_ex(d->hDisplay, "> processing...\r\n", col_purple, CLR_DEFAULT, CFE_BOLD | CFE_LINK, NULL, 180);

    _beginthreadex(NULL, 0, ai_stream_thread_proc, arg, 0, NULL);
}

static void send_user_message(AiChatData *d)
{
    if (!d || !d->active_state || d->active_state->busy || d->pending_approval)
        return;

    char input[2048];
    GetWindowText(d->hInput, input, (int)sizeof(input));
    if (input[0] == '\0') return;

    SetWindowText(d->hInput, "");

    /* Display user message */
    COLORREF col_user = d->theme ? theme_cr(d->theme->accent) : RGB(0, 120, 215);
    chat_append_styled(d->hDisplay, "\r\n\r\n", col_user, 0);
    chat_append_styled_ex(d->hDisplay, "You\r\n", col_user, CLR_DEFAULT, CFE_BOLD, NULL, 220);
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
        ai_build_system_prompt(sys_prompt, sizeof(sys_prompt), term_text,
                               d->session_notes, d->system_notes);
        ai_conv_add(&d->conv, AI_ROLE_SYSTEM, sys_prompt);
    } else if (d->active_term) {
        /* Update system prompt with fresh terminal context */
        char sys_prompt[AI_MSG_MAX];
        ai_build_system_prompt(sys_prompt, sizeof(sys_prompt), term_text,
                               d->session_notes, d->system_notes);
        /* Replace the first (system) message */
        snprintf(d->conv.messages[0].content,
                 sizeof(d->conv.messages[0].content), "%s", sys_prompt);
    }

    ai_conv_add(&d->conv, AI_ROLE_USER, input);
    LeaveCriticalSection(&d->cs);

    update_context_bar(d);

    d->stream_display_start = -1;
    start_indicator(d, "thinking");

    launch_stream_thread(d);
}

static void execute_command(AiChatData *d, const char *cmd)
{
    if (!d || !cmd || !cmd[0]) return;
    if (!d->active_channel) {
        chat_append_ops(d->hDisplay,
                        "  [error: no active SSH channel]\r\n");
        return;
    }

    /* Clear any existing text on the line before pasting:
       Ctrl+E (end of line) + Ctrl+U (kill to start of line) */
    ssh_channel_write(d->active_channel, "\x05\x15", 2);

    /* Send command + CR to SSH channel (CR = Enter key, same as WM_CHAR) */
    ssh_channel_write(d->active_channel, cmd, (size_t)strlen(cmd));
    ssh_channel_write(d->active_channel, "\r", 1);

    chat_append_ops(d->hDisplay, "  $ ");
    chat_append_ops(d->hDisplay, cmd);
    chat_append_ops(d->hDisplay, "\r\n");
}

static void send_continue_message(AiChatData *d)
{
    if (!d || !d->active_state || d->active_state->busy) return;

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
        ai_build_system_prompt(sys_prompt, sizeof(sys_prompt), term_text,
                               d->session_notes, d->system_notes);
        snprintf(d->conv.messages[0].content,
                 sizeof(d->conv.messages[0].content), "%s", sys_prompt);
    }

    ai_conv_add(&d->conv, AI_ROLE_USER,
        "The commands above have been executed. Look at the updated terminal "
        "output and continue with any remaining tasks from my original request. "
        "If there are more commands to run, include ALL of them now. "
        "If everything is done, just summarize what was accomplished.");

    LeaveCriticalSection(&d->cs);

    start_indicator(d, "continuing");

    launch_stream_thread(d);
}

static void input_sync_scroll(AiChatData *d);

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
    if (msg == WM_MOUSEWHEEL) {
        int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
        int scroll = edit_scroll_wheel_delta(zdelta, WHEEL_DELTA, 3);
        SendMessage(hwnd, EM_LINESCROLL, 0, (LPARAM)scroll);
        /* Sync input scrollbar */
        HWND parent = GetParent(hwnd);
        if (parent) {
            AiChatData *d = (AiChatData *)GetWindowLongPtr(parent,
                                                           GWLP_USERDATA);
            if (d) input_sync_scroll(d);
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, InputSubclassProc, uIdSubclass);
    }
    (void)dwRefData;
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* Add a tooltip to a child control */
static void add_tooltip(HWND hTooltip, HWND hCtrl, const char *text)
{
    if (!hTooltip || !hCtrl) return;
    TOOLINFO ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize   = sizeof(ti);
    ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd     = GetParent(hCtrl);
    ti.uId      = (UINT_PTR)hCtrl;
    ti.lpszText = (LPSTR)text;
    SendMessage(hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

/* Match the tab strip layout constants for consistent appearance (base values at 96 DPI) */
#define AI_INDICATOR_W_BASE   12  /* same as INDICATOR_W in tabs.c */
#define AI_INDICATOR_GAP_BASE  3  /* same as INDICATOR_GAP in tabs.c */

/* Draw a tab-style button: same shape, border, font and indicator style
 * as the session tabs in the main window's tab strip. */
static void draw_tab_button(LPDRAWITEMSTRUCT dis, const ThemeColors *theme,
                             AiChatData *d)
{
    if (!dis || !theme) return;
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int btnH = rc.bottom - rc.top;
    int pressed = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF bg = theme_cr(pressed ? theme->bg_primary : theme->bg_secondary);
    COLORREF fg = theme_cr(theme->text_main);
    COLORREF border_cr = theme_cr(theme->border);

    /* Clear with parent bg so rounded corners are clean */
    HBRUSH hParentBr = CreateSolidBrush(theme_cr(theme->bg_primary));
    FillRect(hdc, &rc, hParentBr);
    DeleteObject(hParentBr);

    /* Rounded rect background + border — radius 6 matches tabs */
    HBRUSH hBr = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, border_cr);
    HGDIOBJ oldBr = SelectObject(hdc, hBr);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(hPen);
    DeleteObject(hBr);

    /* Indicator height: same formula as tabs — button height minus 10px */
    int indicH = btnH - 10;
    if (indicH < 4) indicH = 4;
    int indY = rc.top + (btnH - indicH) / 2;

    /* For Permit Write indicator: draw status indicator */
    int text_left = rc.left;
    if (((int)dis->CtlID == IDC_CHAT_PERMIT) && d) {
        int is_active = d->permit_write;
        int indW = MulDiv(AI_INDICATOR_W_BASE, d->dpi, 96);
        int indGap = MulDiv(AI_INDICATOR_GAP_BASE, d->dpi, 96);
        int indX = rc.left + indGap;
        COLORREF dot_col = is_active ? RGB(0, 160, 80) : RGB(200, 50, 50);
        HBRUSH hDot = CreateSolidBrush(dot_col);
        HPEN hDotPen = CreatePen(PS_SOLID, 1, dot_col);
        HGDIOBJ oBr = SelectObject(hdc, hDot);
        HGDIOBJ oPen = SelectObject(hdc, hDotPen);
        RoundRect(hdc, indX, indY,
                  indX + indW, indY + indicH, 3, 3);
        SelectObject(hdc, oPen);
        SelectObject(hdc, oBr);
        DeleteObject(hDotPen);
        DeleteObject(hDot);

        /* Draw letter on indicator: W for write */
        {
            const char *letter = "W";
            HFONT hSmall = CreateFont(
                -MulDiv(7, d->dpi, 72), 0, 0, 0, FW_BOLD,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                APP_FONT_UI_FACE);
            HFONT hOldF = (HFONT)SelectObject(hdc, hSmall);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT indRect = { indX, indY, indX + indW, indY + indicH };
            DrawText(hdc, letter, 1, &indRect,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOldF);
            DeleteObject(hSmall);
        }

        text_left = indX + indW + indGap;
    }

    /* Text — skip for indicator buttons when in compact mode (icon-only) */
    int is_indicator_btn = ((int)dis->CtlID == IDC_CHAT_PERMIT ||
                            (int)dis->CtlID == IDC_CHAT_NEWCHAT);
    if (!d || !d->compact_buttons || !is_indicator_btn) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);

        HFONT hOldFont = NULL;
        if (d && d->hFont)
            hOldFont = (HFONT)SelectObject(hdc, d->hFont);

        wchar_t text[64];
        GetWindowTextW(dis->hwndItem, text, (int)(sizeof(text)/sizeof(text[0])));
        RECT rcText = rc;
        rcText.left = text_left;
        rcText.right -= MulDiv(AI_INDICATOR_GAP_BASE, d ? d->dpi : 96, 96);
        DrawTextW(hdc, text, -1, &rcText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (hOldFont) SelectObject(hdc, hOldFont);
    } else if (d && d->compact_buttons &&
               (int)dis->CtlID == IDC_CHAT_NEWCHAT) {
        /* Compact mode: draw a "+" icon for New Chat */
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);
        HFONT hBold = CreateFont(
            -MulDiv(10, d->dpi, 72), 0, 0, 0, FW_BOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
            APP_FONT_UI_FACE);
        HFONT hOldF = (HFONT)SelectObject(hdc, hBold);
        DrawText(hdc, "+", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldF);
        DeleteObject(hBold);
    }
}

/* Reposition all child controls.  Called from WM_SIZE and when
 * the approval bar is shown / hidden so the display shrinks to
 * make room for the Allow / Deny buttons. */
static void relayout(AiChatData *d)
{
    if (!d || !d->hwnd) return;
    RECT rc;
    GetClientRect(d->hwnd, &rc);
    int cw = rc.right;
    int ch = rc.bottom;
    #define S(px) MulDiv((px), d->dpi, 96)
    int btn_h = S(24);
    int pad = S(4);
    int top_y = pad + btn_h + pad;
    int input_h = S(46);
    int margin = S(5);
    int send_w = S(40);
    int approve_h = d->pending_approval ? (btn_h + pad) : 0;

    /* Right-side icon buttons (always shown) */
    int right_w = pad + btn_h + pad + btn_h + pad;
    if (d->hSaveBtn)
        MoveWindow(d->hSaveBtn, cw - pad - btn_h, pad, btn_h, btn_h, TRUE);
    if (d->hUndockBtn)
        MoveWindow(d->hUndockBtn, cw - pad - btn_h - pad - btn_h, pad,
                   btn_h, btn_h, TRUE);

    /* Decide if left-side buttons fit with full text or need compact mode.
     * Full: New Chat (78) + Permit Write (115)
     * Compact: New Chat (78) + indicator-only (btn_h) */
    int full_w = pad + S(78) + pad + S(115) + pad + S(115);
    int avail = cw - right_w;
    d->compact_buttons = (full_w > avail);
    int pw = d->compact_buttons ? btn_h : S(115);
    int nw = d->compact_buttons ? btn_h : S(78);
    /* tw removed - thinking button no longer exists */

    if (d->hNewChatBtn)
        MoveWindow(d->hNewChatBtn, pad, pad, nw, btn_h, TRUE);
    if (d->hPermitBtn)
        MoveWindow(d->hPermitBtn, pad + nw + pad, pad, pw, btn_h, TRUE);
    /* Thinking button removed - thinking is now inline in chat display */
    {
        int bar_h = S(16);
        int ctx_w = S(120);
        int label_w = cw - ctx_w - pad * 3;
        if (d->hSessionLabel)
            MoveWindow(d->hSessionLabel, pad, top_y, label_w, bar_h, TRUE);
        if (d->hContextBar)
            MoveWindow(d->hContextBar, cw - ctx_w - pad, top_y, ctx_w, bar_h, TRUE);
        if (d->hContextLabel)
            MoveWindow(d->hContextLabel, cw - ctx_w - pad, top_y, ctx_w, bar_h, TRUE);
        top_y += bar_h + pad;
    }
    {
        int disp_w, disp_h;
        ai_dock_chat_layout(cw, ch, top_y, input_h, approve_h, margin,
                            CSB_WIDTH, &disp_w, &disp_h);

        /* If thinking box is visible, split the display area */
        int think_h = S(100);  /* Height of thinking box */
        int actual_disp_h = disp_h;
        if (d->hThinkingBox && IsWindowVisible(d->hThinkingBox)) {
            actual_disp_h = disp_h - think_h - pad;
            if (actual_disp_h < S(50)) actual_disp_h = S(50);  /* Minimum display height */

            /* Position thinking box below display */
            MoveWindow(d->hThinkingBox, margin, top_y + actual_disp_h + pad,
                      disp_w, think_h, TRUE);
        }

        if (d->hDisplay)
            MoveWindow(d->hDisplay, margin, top_y, disp_w, actual_disp_h, TRUE);
        if (d->hDisplayScrollbar)
            MoveWindow(d->hDisplayScrollbar, margin + disp_w, top_y,
                       CSB_WIDTH, actual_disp_h, TRUE);
    }
    /* Approval buttons — between display and input */
    if (d->hAllowBtn && d->hDenyBtn) {
        if (d->pending_approval) {
            int approve_y = ch - input_h - approve_h - margin;
            int abw = S(80);
            MoveWindow(d->hAllowBtn, margin, approve_y, abw, btn_h, TRUE);
            MoveWindow(d->hDenyBtn, margin + abw + pad, approve_y, abw, btn_h, TRUE);
            ShowWindow(d->hAllowBtn, SW_SHOW);
            ShowWindow(d->hDenyBtn, SW_SHOW);
        } else {
            ShowWindow(d->hAllowBtn, SW_HIDE);
            ShowWindow(d->hDenyBtn, SW_HIDE);
        }
    }
    {
        int input_y = ch - input_h - margin;
        if (input_y < top_y) input_y = top_y;
        int input_w = cw - send_w - margin * 3 - CSB_WIDTH;
        if (input_w < 1) input_w = 1;
        if (d->hInput)
            MoveWindow(d->hInput, margin, input_y, input_w, input_h, TRUE);
        if (d->hInputScrollbar)
            MoveWindow(d->hInputScrollbar, margin + input_w, input_y,
                       CSB_WIDTH, input_h, TRUE);
        if (d->hSendBtn)
            MoveWindow(d->hSendBtn, cw - send_w - margin, input_y, send_w, input_h, TRUE);
    }
    #undef S
}

/* Sync the custom scrollbar with the RichEdit display's scroll state */
static void display_sync_scroll(AiChatData *d)
{
    if (!d) return;
    int lh = d->display_line_h > 0 ? d->display_line_h : 1;
    csb_sync_edit(d->hDisplay, d->hDisplayScrollbar, lh);
}

/* Sync the input scrollbar with the EDIT control's scroll state */
static void input_sync_scroll(AiChatData *d)
{
    if (!d || !d->hInputScrollbar) return;
    int lh = d->input_line_h > 0 ? d->input_line_h : 1;
    csb_sync_edit(d->hInput, d->hInputScrollbar, lh);
}

/* Zoom the AI chat font: recreate hFont at the new size, apply to controls,
 * and re-measure line height for scroll sync. */
static void chat_apply_zoom(AiChatData *d, int delta)
{
    if (!d) return;
    int new_size = app_font_zoom(d->ui_font_size, delta);
    if (new_size == d->ui_font_size)
        return;
    d->ui_font_size = new_size;

    /* Recreate main font at new size */
    if (d->hFont) DeleteObject(d->hFont);
    if (d->hIconFont) DeleteObject(d->hIconFont);
    int h = -MulDiv(new_size, d->dpi, 72);
    d->hFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_TT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
                          
    int ih = -MulDiv(new_size, d->dpi, 72);
    d->hIconFont = CreateFont(ih, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Segoe Fluent Icons");
    if (!d->hIconFont) {
        d->hIconFont = CreateFont(ih, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_TT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, "Segoe MDL2 Assets");
    }

    if (d->hFont) {
        SendMessage(d->hDisplay, WM_SETFONT, (WPARAM)d->hFont, TRUE);
        SendMessage(d->hInput, WM_SETFONT, (WPARAM)d->hFont, TRUE);
        /* Re-measure line height */
        HDC hdc = GetDC(d->hDisplay);
        HGDIOBJ old = SelectObject(hdc, (HGDIOBJ)d->hFont);
        TEXTMETRIC tm;
        GetTextMetrics(hdc, &tm);
        d->display_line_h = tm.tmHeight + tm.tmExternalLeading;
        d->input_line_h = d->display_line_h;
        SelectObject(hdc, old);
        ReleaseDC(d->hDisplay, hdc);
    }
    /* Rebuild the display so all text is re-rendered with proper colors
     * and markdown formatting.  WM_SETFONT alone strips per-character
     * formatting (colors, bold, etc.) from the RichEdit control. */
    chat_rebuild_display(d);
    display_sync_scroll(d);
    input_sync_scroll(d);
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

        /* Get per-monitor DPI for layout scaling */
        {
            HDC hdc_dpi = GetDC(hwnd);
            nd->dpi = GetDeviceCaps(hdc_dpi, LOGPIXELSY);
            ReleaseDC(hwnd, hdc_dpi);
        }
        #define S(px) MulDiv((px), nd->dpi, 96)

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;
        int btn_h = S(24); /* compact — matches BTN_SIZE in tab strip */
        int pad = S(4);
        int top_y = pad + btn_h + pad; /* display starts below buttons */

        /* New Chat button (owner-drawn for theme) */
        nd->hNewChatBtn = CreateWindow("BUTTON", "New Chat",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            pad, pad, S(78), btn_h,
            hwnd, (HMENU)IDC_CHAT_NEWCHAT, NULL, NULL);

        /* Permit Write button */
        nd->permit_write = 0; /* default: read-only */
        nd->hPermitBtn = CreateWindow("BUTTON", "Permit Write",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            pad + S(78) + pad, pad, S(115), btn_h,
            hwnd, (HMENU)IDC_CHAT_PERMIT, NULL, NULL);

        nd->show_thinking = 0; /* default: collapsed (user must click '>' to expand) */
        nd->hThinkingBtn = NULL; /* Thinking button removed - now inline in chat */

        /* Save button — square, right-aligned in button row */
        nd->hSaveBtn = CreateWindow("BUTTON", "",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw - pad - btn_h, pad, btn_h, btn_h,
            hwnd, (HMENU)IDC_CHAT_SAVE, NULL, NULL);

        /* Undock/Dock button — square, left of save button */
        nd->hUndockBtn = CreateWindow("BUTTON", "",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw - pad - btn_h - pad - btn_h, pad, btn_h, btn_h,
            hwnd, (HMENU)IDC_CHAT_UNDOCK, NULL, NULL);

        /* Allow/Deny buttons — hidden until command approval needed.
         * Initial position is off-screen; relayout() places them. */
        nd->hAllowBtn = CreateWindow("BUTTON", "Allow",
            WS_CHILD | BS_OWNERDRAW,
            S(5), 0, S(80), btn_h,
            hwnd, (HMENU)IDC_CHAT_ALLOW, NULL, NULL);
        nd->hDenyBtn = CreateWindow("BUTTON", "Deny",
            WS_CHILD | BS_OWNERDRAW,
            S(5) + S(80) + pad, 0, S(80), btn_h,
            hwnd, (HMENU)IDC_CHAT_DENY, NULL, NULL);

        /* Session name (left) + context bar (right) row */
        {
            int bar_h = S(16);
            int ctx_w = S(120);  /* fixed width for context bar */
            int label_w = cw - ctx_w - pad * 3;

            /* Session name label on the left */
            nd->hSessionLabel = CreateWindow("STATIC",
                nd->session_name[0] ? nd->session_name : "",
                WS_VISIBLE | WS_CHILD | SS_LEFT | SS_ENDELLIPSIS,
                pad, top_y, label_w, bar_h,
                hwnd, (HMENU)IDC_SESSION_LABEL, NULL, NULL);

            /* Context bar and label on the right */
            nd->hContextBar = CreateWindow(PROGRESS_CLASS, "",
                WS_VISIBLE | WS_CHILD,
                cw - ctx_w - pad, top_y, ctx_w, bar_h,
                hwnd, (HMENU)IDC_CONTEXT_BAR, NULL, NULL);
            SendMessage(nd->hContextBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(nd->hContextBar, PBM_SETPOS, 0, 0);

            /* Clickable label overlay (SS_NOTIFY enables STN_CLICKED) */
            nd->hContextLabel = CreateWindow("STATIC",
                nd->context_limit > 0 ? "Context: 0%" : "Context: N/A",
                WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
                cw - ctx_w - pad, top_y, ctx_w, bar_h,
                hwnd, (HMENU)IDC_CONTEXT_LABEL, NULL, NULL);

            top_y += bar_h + pad;
        }

        /* Chat display: read-only RichEdit for colored text.
         * In docked mode the initial window may be 1x1, so clamp
         * all dimensions to >=1 — relayout() fixes them on first WM_SIZE. */
        int input_h = S(46); /* ~2 lines for multiline input */
        int margin = S(5);
        int disp_w = cw - margin * 2 - CSB_WIDTH;
        if (disp_w < 1) disp_w = 1;
        int disp_h = ch - input_h - top_y - margin * 2;
        if (disp_h < 1) disp_h = 1;
        nd->hDisplay = CreateWindowW(L"RichEdit20W", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            margin, top_y, disp_w, disp_h,
            hwnd, (HMENU)IDC_CHAT_DISPLAY, NULL, NULL);
        if (nd->hDisplay) {
            SetWindowSubclass(nd->hDisplay, DisplaySubclassProc, 1, 0);
            DWORD em = (DWORD)SendMessage(nd->hDisplay, EM_GETEVENTMASK, 0, 0);
            SendMessage(nd->hDisplay, EM_SETEVENTMASK, 0, em | ENM_LINK);
        }

        /* Custom themed scrollbar for chat display */
        csb_register(GetModuleHandle(NULL));
        nd->hDisplayScrollbar = csb_create(hwnd,
            margin + disp_w, top_y, CSB_WIDTH, disp_h,
            nd->theme, GetModuleHandle(NULL));
        SetTimer(hwnd, TIMER_SCROLL_SYNC, 50, NULL);

        /* Set RichEdit background from theme */
        if (nd->hDisplay)
            SendMessage(nd->hDisplay, EM_SETBKGNDCOLOR, 0,
                        (LPARAM)(nd->theme ? theme_cr(nd->theme->bg_secondary)
                                           : GetSysColor(COLOR_WINDOW)));

        /* Thinking box: initially hidden, appears when indicator is clicked */
        nd->hThinkingBox = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            margin, top_y + disp_h - S(100), disp_w, S(100),
            hwnd, (HMENU)IDC_THINKING_BOX, NULL, NULL);
        if (nd->hThinkingBox) {
            /* Apply theme colors - use same as display for consistency */
            COLORREF bg = nd->theme ? theme_cr(nd->theme->bg_secondary) : GetSysColor(COLOR_WINDOW);
            SendMessage(nd->hThinkingBox, EM_SETBKGNDCOLOR, 0, (LPARAM)bg);

            /* Apply font to match main display */
            if (nd->hFont) {
                SendMessage(nd->hThinkingBox, WM_SETFONT, (WPARAM)nd->hFont, FALSE);
            }

            ShowWindow(nd->hThinkingBox, SW_HIDE);  /* Hidden by default */
        }

        /* Input field: multiline, Enter sends via subclass, Shift+Enter = newline */
        int send_w = S(40);
        int input_y = ch - input_h - margin;
        if (input_y < 0) input_y = 0;
        int input_w = cw - send_w - margin * 3 - CSB_WIDTH;
        if (input_w < 1) input_w = 1;
        nd->hInput = CreateWindow("EDIT", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            margin, input_y, input_w, input_h,
            hwnd, (HMENU)IDC_CHAT_INPUT, NULL, NULL);
        if (nd->hInput)
            SetWindowSubclass(nd->hInput, InputSubclassProc, 0, 0);

        /* Custom themed scrollbar for input */
        nd->hInputScrollbar = csb_create(hwnd,
            margin + input_w, input_y, CSB_WIDTH, input_h,
            nd->theme, GetModuleHandle(NULL));

        /* Send button (owner-drawn for theme) */
        nd->hSendBtn = CreateWindow("BUTTON", ">",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw - send_w - margin, input_y, send_w, input_h,
            hwnd, (HMENU)IDC_CHAT_SEND, NULL, NULL);

        /* Font — use Inter UI font */
        nd->ui_font_size = APP_FONT_UI_SIZE;
        int h = -MulDiv(APP_FONT_UI_SIZE, nd->dpi, 72);
        nd->hFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
        /* Small bold font — DPI-scaled to match tab strip indicator labels */
        int sh = -MulDiv(7, nd->dpi, 72);
        nd->hSmallFont = CreateFont(sh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_TT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);

        /* Icon Font for Fluent UI */
        int ih = -MulDiv(APP_FONT_UI_SIZE, nd->dpi, 72);
        nd->hIconFont = CreateFont(ih, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_TT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, "Segoe Fluent Icons");
        if (!nd->hIconFont) {
            nd->hIconFont = CreateFont(ih, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_TT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, "Segoe MDL2 Assets");
        }
        #undef S
        if (nd->hFont) {
            SendMessage(nd->hDisplay, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
            SendMessage(nd->hInput, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
            /* Measure line height for scrollbar sync */
            HDC hdc_m = GetDC(nd->hDisplay);
            HGDIOBJ old_m = SelectObject(hdc_m, (HGDIOBJ)nd->hFont);
            TEXTMETRIC tm_m;
            GetTextMetrics(hdc_m, &tm_m);
            nd->display_line_h = tm_m.tmHeight + tm_m.tmExternalLeading;
            nd->input_line_h = nd->display_line_h;
            SelectObject(hdc_m, old_m);
            ReleaseDC(nd->hDisplay, hdc_m);
        }
        if (nd->hSmallFont) {
            SendMessage(nd->hSessionLabel, WM_SETFONT, (WPARAM)nd->hSmallFont, TRUE);
            SendMessage(nd->hContextLabel, WM_SETFONT, (WPARAM)nd->hSmallFont, TRUE);
        }

        /* Apply theme title bar + borders */
        if (nd->theme) {
            themed_apply_title_bar(hwnd, nd->theme);
            themed_apply_borders(hwnd, nd->theme);
        }

        /* Create tooltip control and add tips for all buttons */
        nd->hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        if (nd->hTooltip) {
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, 300);
            add_tooltip(nd->hTooltip, nd->hNewChatBtn,
                "New Chat\nClear the conversation and start fresh.");
            add_tooltip(nd->hTooltip, nd->hPermitBtn,
                "Permit Write\n"
                "Toggle read/write mode for AI commands.\n"
                "Green = AI can execute any command.\n"
                "Red = AI can only run read-only commands\n"
                "(ls, cat, pwd, etc).");
            /* Thinking tooltip removed - click '>' in chat to expand/collapse */
            add_tooltip(nd->hTooltip, nd->hSaveBtn,
                "Save\nSave the conversation as a text file.");
            if (nd->hUndockBtn)
                add_tooltip(nd->hTooltip, nd->hUndockBtn,
                    nd->docked ? "Undock\nOpen AI Assist in a separate window."
                               : "Dock\nDock AI Assist inside the main window.");
            add_tooltip(nd->hTooltip, nd->hSendBtn,
                "Send\nSend your message to the AI.\n"
                "Shortcut: press Enter in the input box.");
        }

        /* Show loaded conversation or fresh welcome message */
        if (nd->conv.msg_count > 0) {
            chat_rebuild_display(nd);
        } else {
            chat_append_ops(nd->hDisplay,
                "AI Assist - Type a message and press Enter or click Send.\r\n"
                "The AI can see your terminal and execute commands.\r\n"
                "---\r\n");
        }

        SetFocus(nd->hInput);
        return 0;
    }

    case WM_SIZE: {
        if (!d) break;
        relayout(d);
        /* Force repaint of all owner-drawn buttons after layout change.
         * Erase background (TRUE) so newly exposed areas during the
         * slide-out animation get filled with the theme colour. */
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_NCHITTEST: {
        /* When docked, make the left edge transparent to mouse clicks
         * so the parent window can handle splitter dragging. */
        if (d && d->docked) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (pt.x < AI_DOCK_SPLITTER_HIT / 2)
                return HTTRANSPARENT;
        }
        break;
    }

    case WM_NOTIFY: {
        NMHDR *nmh = (NMHDR *)lParam;
        if (d && nmh->hwndFrom == d->hDisplay && nmh->code == EN_LINK) {
            ENLINK *enm = (ENLINK *)lParam;
            if (enm->msg == WM_LBUTTONUP) {
                /* Toggle thinking textbox visibility */
                d->show_thinking = !d->show_thinking;

                if (d->hThinkingBox) {
                    if (d->show_thinking) {
                        /* Show thinking box and populate it */
                        ShowWindow(d->hThinkingBox, SW_SHOW);

                        /* Clear and populate with current thinking content */
                        SetWindowTextA(d->hThinkingBox, "");
                        if (ACTIVE_BUSY(d) && d->stream_thinking_len > 0) {
                            /* Streaming - show current thinking buffer */
                            SetWindowTextA(d->hThinkingBox, d->stream_thinking);
                        } else {
                            /* Find thinking from history for last AI message */
                            for (int i = d->conv.msg_count - 1; i >= 0; i--) {
                                if (d->conv.messages[i].role == AI_ROLE_ASSISTANT &&
                                    d->thinking_history[i] && d->thinking_history[i][0]) {
                                    SetWindowTextA(d->hThinkingBox, d->thinking_history[i]);
                                    break;
                                }
                            }
                        }

                        /* Trigger layout to adjust display/thinking box sizes */
                        RECT rc;
                        GetClientRect(d->hwnd, &rc);
                        SendMessage(d->hwnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
                    } else {
                        /* Hide thinking box */
                        ShowWindow(d->hThinkingBox, SW_HIDE);

                        /* Trigger layout to restore display size */
                        RECT rc;
                        GetClientRect(d->hwnd, &rc);
                        SendMessage(d->hwnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
                    }
                }
            }
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHAT_SEND:
            send_user_message(d);
            SetFocus(d->hInput);
            return 0;
        case IDC_CHAT_NEWCHAT:
            if (d && !ACTIVE_BUSY(d) && !d->pending_approval) {
                /* Reset only the ACTIVE session's conversation.
                 * Other sessions' AiSessionState objects are untouched. */
                ai_conv_reset(&d->conv);
                if (d->active_state) {
                    ai_conv_reset(&d->active_state->conv);
                    d->active_state->valid = 1;
                }
                d->indicator_pos = -1;
                d->commands_executed = 0;
                d->pending_request[0] = '\0';
                d->queued_count = 0;
                d->queued_next = 0;
                d->stream_thinking[0] = '\0';
                d->stream_thinking_len = 0;
                d->stream_content[0] = '\0';
                d->stream_content_len = 0;
                d->stream_phase = 0;
                thinking_history_clear(d);
                /* Clear display and show welcome message */
                SetWindowTextW(d->hDisplay, L"");
                chat_append_ops(d->hDisplay,
                    "AI Assist - Type a message and press Enter or click Send.\r\n"
                    "The AI can see your terminal and execute commands.\r\n"
                    "---\r\n");
                /* Clear input field */
                SetWindowText(d->hInput, "");
                SetFocus(d->hInput);
                update_context_bar(d);
            }
            return 0;
        case IDC_CHAT_SAVE:
            if (d) {
                /* Build save text from conversation */
                char *save_buf = (char *)malloc(AI_BODY_MAX);
                if (save_buf) {
                    size_t n = ai_build_save_text(&d->conv,
                        d->thinking_history, d->show_thinking,
                        save_buf, AI_BODY_MAX);
                    if (n > 0) {
                        char fname[MAX_PATH] = "ai_chat.txt";
                        OPENFILENAME ofn;
                        memset(&ofn, 0, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0"
                                          "All Files (*.*)\0*.*\0";
                        ofn.lpstrFile = fname;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                        ofn.lpstrDefExt = "txt";
                        if (GetSaveFileName(&ofn)) {
                            FILE *f = fopen(fname, "w");
                            if (f) {
                                fwrite(save_buf, 1, n, f);
                                fclose(f);
                            } else {
                                MessageBox(hwnd,
                                    "Failed to save file.",
                                    "Save Error",
                                    MB_OK | MB_ICONERROR);
                            }
                        }
                    }
                    free(save_buf);
                }
            }
            return 0;
        case IDC_CHAT_UNDOCK:
            if (d) {
                /* Post (not Send) so the AI window finishes its message
                 * loop before the main window destroys and recreates it. */
                HWND owner = GetParent(hwnd);
                if (!owner) owner = GetWindow(hwnd, GW_OWNER);
                if (owner)
                    PostMessage(owner, WM_COMMAND,
                                MAKEWPARAM(IDM_VIEW_AI_UNDOCK, 0), 0);
            }
            return 0;
        case IDC_CHAT_PERMIT:
            if (d) {
                d->permit_write = !d->permit_write;
                InvalidateRect(d->hPermitBtn, NULL, TRUE);
            }
            return 0;

        /* IDC_CHAT_THINKING removed - thinking toggle now inline in chat */

        case IDC_CHAT_ALLOW:
            if (d && d->pending_approval) {
                d->pending_approval = 0;
                if (d->active_state) {
                    d->active_state->pending_approval = 0;
                    free(d->active_state->pending_cmds);
                    d->active_state->pending_cmds = NULL;
                    d->active_state->pending_cmd_count = 0;
                }
                relayout(d);
                chat_append_ops(d->hDisplay,
                                "\r\n--- Commands ---\r\n");
                execute_command(d, d->queued_cmds[0]);
                d->queued_next = 1;
                if (d->queued_count > 1 && d->paste_delay_ms > 0) {
                    char prog[80];
                    snprintf(prog, sizeof(prog),
                             "executing %d/%d", 1, d->queued_count);
                    start_indicator(d, prog);
                    SetTimer(hwnd, TIMER_CMD_QUEUE,
                             (UINT)d->paste_delay_ms, NULL);
                } else {
                    for (int ci = 1; ci < d->queued_count; ci++)
                        execute_command(d, d->queued_cmds[ci]);
                    d->queued_next = d->queued_count;
                    d->commands_executed = d->queued_count;
                    start_indicator(d, "waiting for output");
                    SetTimer(hwnd, TIMER_CONTINUE,
                             CONTINUE_DELAY_MS, NULL);
                }
                SetFocus(d->hInput);
            }
            return 0;
        case IDC_CHAT_DENY:
            if (d && d->pending_approval) {
                d->pending_approval = 0;
                d->queued_count = 0;
                d->queued_next = 0;
                if (d->active_state) {
                    d->active_state->pending_approval = 0;
                    free(d->active_state->pending_cmds);
                    d->active_state->pending_cmds = NULL;
                    d->active_state->pending_cmd_count = 0;
                }
                relayout(d);
                chat_append_ops(d->hDisplay,
                                "\r\n  [commands denied]\r\n");
                SetFocus(d->hInput);
            }
            return 0;
        case IDC_CONTEXT_LABEL:
            if (d && HIWORD(wParam) == STN_CLICKED) {
                /* Context label click - removed thinking toggle (now inline in chat) */
                if (d->context_limit <= 0) {
                    MessageBox(hwnd,
                        "Context usage tracking is not available\n"
                        "for this model (unknown context limit).",
                        "Compact Context", MB_OK | MB_ICONINFORMATION);
                } else {
                    int r = MessageBox(hwnd,
                        "Trim older messages to free context space?\n"
                        "The 3 most recent exchanges will be kept.",
                        "Compact Context", MB_YESNO | MB_ICONQUESTION);
                    if (r == IDYES) {
                        EnterCriticalSection(&d->cs);
                        int removed = ai_conv_compact(&d->conv, 3);
                        LeaveCriticalSection(&d->cs);
                        if (removed > 0) {
                            char cbuf[128];
                            snprintf(cbuf, sizeof(cbuf),
                                     "\r\n[compacted: removed %d older messages]\r\n",
                                     removed);
                            chat_append_ops(d->hDisplay, cbuf);
                            update_context_bar(d);
                        } else {
                            chat_append_ops(d->hDisplay,
                                "\r\n[nothing to compact]\r\n");
                        }
                    }
                }
            }
            return 0;
        case IDC_CHAT_INPUT:
            /* Handle Enter key in input (via EN_CHANGE notification is wrong;
             * we handle it via WM_KEYDOWN subclass or default button) */
            break;
        }
        break;

    case WM_AI_STREAM: {
        /* Realtime streaming chunk: wParam 0=thinking, 1=content */
        if (!d) break;
        AiStreamChunk *chunk = (AiStreamChunk *)lParam;
        if (!chunk) break;
        char *delta = chunk->delta;
        AiSessionState *src = chunk->session;
        free(chunk);  /* free wrapper; delta freed below */
        if (!delta) return 0;

        /* Always accumulate to the source session's stream buffers */
        if (src) {
            size_t dlen = strlen(delta);
            if (wParam == 0) {
                if (src->stream_thinking &&
                    src->stream_thinking_len + dlen < AI_MSG_MAX - 1) {
                    memcpy(src->stream_thinking + src->stream_thinking_len,
                           delta, dlen);
                    src->stream_thinking_len += dlen;
                    src->stream_thinking[src->stream_thinking_len] = '\0';
                }
                if (src->stream_phase == 0)
                    src->stream_phase = 1;
            } else {
                if (src->stream_content &&
                    src->stream_content_len + dlen < AI_MSG_MAX - 1) {
                    memcpy(src->stream_content + src->stream_content_len,
                           delta, dlen);
                    src->stream_content_len += dlen;
                    src->stream_content[src->stream_content_len] = '\0';
                }
                if (src->stream_phase < 2)
                    src->stream_phase = 2;
            }
        }

        /* If this chunk is for a different session, don't touch the display */
        if (src != d->active_state) {
            free(delta);
            return 0;
        }

        /* Also accumulate to display-side buffers */
        {
            size_t dlen = strlen(delta);
            if (wParam == 0) {
                if (d->stream_thinking_len + dlen < AI_MSG_MAX - 1) {
                    memcpy(d->stream_thinking + d->stream_thinking_len,
                           delta, dlen);
                    d->stream_thinking_len += dlen;
                    d->stream_thinking[d->stream_thinking_len] = '\0';
                }
                if (d->stream_phase == 0)
                    d->stream_phase = 1;
            } else {
                if (d->stream_content_len + dlen < AI_MSG_MAX - 1) {
                    memcpy(d->stream_content + d->stream_content_len,
                           delta, dlen);
                    d->stream_content_len += dlen;
                    d->stream_content[d->stream_content_len] = '\0';
                }
            }
        }

        /* Remove the animated indicator only when we'll show something.
         * Hidden thinking chunks (wParam==0, show_thinking off) must NOT
         * remove it — otherwise the user sees nothing until content arrives. */
        {
            int should_remove = 0;
            if (wParam == 1)
                should_remove = 1;  /* content chunk — always visible */
            else if (d->show_thinking)
                should_remove = 1;  /* thinking chunk, visible */

            if (should_remove && d->indicator_pos >= 0) {
                KillTimer(d->hwnd, TIMER_THINKING);
                SendMessage(d->hDisplay, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
                CHARRANGE cr_rm;
                SendMessage(d->hDisplay, EM_EXGETSEL, 0, (LPARAM)&cr_rm);
                CHARRANGE cr_sel = { d->indicator_pos, cr_rm.cpMax };
                SendMessage(d->hDisplay, EM_EXSETSEL, 0, (LPARAM)&cr_sel);
                SendMessageW(d->hDisplay, EM_REPLACESEL, FALSE, (LPARAM)L"");
                d->indicator_pos = -1;
            }
        }

        /* Display the chunk */
        if (wParam == 0) {
            /* Thinking delta */
            if (d->stream_phase < 1) {
                d->stream_phase = 1;

                /* Update "> processing..." to "> Thinking..." in place */
                COLORREF col_purple = RGB(150, 100, 200);
                if (d->indicator_pos >= 0) {
                    /* Replace the text but keep it visible */
                    CHARRANGE cr_sel = { d->indicator_pos, d->indicator_pos + 16 };  /* Length of "> processing..." */
                    SendMessage(d->hDisplay, EM_EXSETSEL, 0, (LPARAM)&cr_sel);

                    /* Set format for replacement text */
                    CHARFORMAT2 cf;
                    memset(&cf, 0, sizeof(cf));
                    cf.cbSize = sizeof(cf);
                    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_LINK;
                    cf.crTextColor = col_purple;
                    cf.dwEffects = CFE_BOLD | CFE_LINK;
                    SendMessage(d->hDisplay, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

                    /* Replace text */
                    SendMessageW(d->hDisplay, EM_REPLACESEL, FALSE, (LPARAM)L"> Thinking...\r\n");
                }
            }

            /* Update thinking box if visible */
            if (d->show_thinking && d->hThinkingBox && delta && delta[0]) {
                SetWindowTextA(d->hThinkingBox, d->stream_thinking);
            }
        } else {
            /* Content delta (no thinking - simple response) */
            COLORREF col_ai = d->theme ? theme_cr(d->theme->text_main)
                                       : GetSysColor(COLOR_WINDOWTEXT);
            if (d->stream_phase < 2) {
                /* Simple response - just keep "> processing..." or add AI header after it */
                if (d->stream_phase == 1)
                    chat_append_ops(d->hDisplay, "\r\n");
                else {
                    /* Phase 0 - remove "> processing..." and add "AI" header */
                    if (d->indicator_pos >= 0) {
                        CHARRANGE cr_sel = { d->indicator_pos, d->indicator_pos + 16 };
                        SendMessage(d->hDisplay, EM_EXSETSEL, 0, (LPARAM)&cr_sel);
                        SendMessageW(d->hDisplay, EM_REPLACESEL, FALSE, (LPARAM)L"");
                        d->indicator_pos = -1;
                    }
                }
                chat_append_styled_ex(d->hDisplay, "AI\r\n", RGB(0, 150, 200), CLR_DEFAULT, CFE_BOLD, NULL, 220);
                d->stream_phase = 2;
                /* Record where the AI content starts for markdown re-render */
                GETTEXTLENGTHEX gtx = { .flags = GTL_NUMCHARS, .codepage = 1200 };
                d->stream_display_start =
                    (int)SendMessage(d->hDisplay, EM_GETTEXTLENGTHEX,
                                     (WPARAM)&gtx, 0);
            }
            chat_append_color(d->hDisplay, delta, col_ai);
        }

        free(delta);
        return 0;
    }

    case WM_AI_RESPONSE: {
        if (!d) break;
        AiResponseMsg *rmsg = (AiResponseMsg *)lParam;
        if (!rmsg) break;
        AiSessionState *src = rmsg->session;

        /* Free per-session stream accumulators */
        if (src) {
            free(src->stream_content);
            src->stream_content = NULL;
            src->stream_content_len = 0;
            free(src->stream_thinking);
            src->stream_thinking = NULL;
            src->stream_thinking_len = 0;
            src->stream_phase = 0;
        }

        /* If this response is for a different session than the one displayed,
         * the thread already committed its result to src->conv.
         * Extract commands for deferred approval, then clean up. */
        if (src != d->active_state) {
            if (wParam == 2 && src && rmsg->content) {
                char cmds[16][1024];
                int ncmds = ai_extract_commands(rmsg->content, cmds, 16);
                if (ncmds > 0) {
                    free(src->pending_cmds);
                    size_t sz = (size_t)ncmds * sizeof(cmds[0]);
                    src->pending_cmds = malloc(sz);
                    if (src->pending_cmds) {
                        memcpy(src->pending_cmds, cmds, sz);
                        src->pending_cmd_count = ncmds;
                        src->pending_approval = 1;
                    }
                }
            }
            if (src) src->busy = 0;
            free(rmsg->content);
            free(rmsg->thinking);
            free(rmsg);
            return 0;
        }

        /* Don't remove indicators - they stay visible with embedded textbox approach */
        KillTimer(hwnd, TIMER_THINKING);
        d->indicator_pos = -1;

        if (wParam == 2) {
            /* Streaming complete — text already displayed, do command extraction */
            char *text = rmsg->content;

            /* The thread committed the assistant message to src->conv.
             * Sync d->conv (the working copy) so it includes the response. */
            if (text) {
                EnterCriticalSection(&d->cs);
                ai_conv_add(&d->conv, AI_ROLE_ASSISTANT, text);
                LeaveCriticalSection(&d->cs);
            }

            /* Save thinking for this assistant message in history.
             * The assistant message was just added to conv above,
             * so it's at msg_count-1. */
            if (d->stream_thinking_len > 0 &&
                d->conv.msg_count > 0) {
                int idx = d->conv.msg_count - 1;
                free(d->thinking_history[idx]);
                d->thinking_history[idx] =
                    _strdup(d->stream_thinking);
            }
            d->stream_thinking[0] = '\0';
            d->stream_thinking_len = 0;

            /* Replace the plain-text stream with markdown-rendered version */
            if (d->stream_display_start >= 0 && d->stream_content_len > 0) {
                /* Select from stream start to end and delete */
                SendMessage(d->hDisplay, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
                CHARRANGE cr_end;
                SendMessage(d->hDisplay, EM_EXGETSEL, 0, (LPARAM)&cr_end);
                CHARRANGE cr_repl = { d->stream_display_start, cr_end.cpMax };
                SendMessage(d->hDisplay, EM_EXSETSEL, 0, (LPARAM)&cr_repl);
                SendMessageW(d->hDisplay, EM_REPLACESEL, FALSE, (LPARAM)L"");

                /* Re-render with markdown formatting */
                COLORREF col_ai = d->theme
                    ? theme_cr(d->theme->text_main)
                    : GetSysColor(COLOR_WINDOWTEXT);
                chat_append_markdown(d->hDisplay, d->stream_content,
                                     col_ai, d->theme, d->ai_font_name);
            } else {
                chat_append_color(d->hDisplay, "\r\n",
                    d->theme ? theme_cr(d->theme->text_main)
                             : GetSysColor(COLOR_WINDOWTEXT));
            }
            d->stream_display_start = -1;
            d->stream_phase = 0;

            /* Extract commands from the full accumulated content */
            char cmds[16][1024];
            int ncmds = text ? ai_extract_commands(text, cmds, 16) : 0;

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
                        chat_append_ops(d->hDisplay, "  [blocked: ");
                        chat_append_ops(d->hDisplay, cmds[ci]);
                        chat_append_ops(d->hDisplay, "]\r\n");
                    }
                }
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
                /* Show commands inline in chat and present
                 * Allow / Deny buttons instead of a modal dialog */
                char confirm[4096];
                size_t clen = ai_build_confirm_text(cmds, ncmds,
                                                     confirm, sizeof(confirm));
                if (clen == 0)
                    snprintf(confirm, sizeof(confirm),
                             "Execute %d command(s)?", ncmds);
                chat_append_ops(d->hDisplay, "\r\n");
                COLORREF dim = d->theme
                    ? theme_cr(d->theme->text_dim)
                    : GetSysColor(COLOR_GRAYTEXT);
                chat_append_color(d->hDisplay, confirm, dim);
                chat_append_ops(d->hDisplay, "\r\n");

                /* Stash commands and show approval buttons */
                memcpy(d->queued_cmds, cmds,
                       (size_t)ncmds * sizeof(cmds[0]));
                d->queued_count = ncmds;
                d->queued_next = 0;
                d->pending_approval = 1;
                relayout(d);
                SetFocus(d->hAllowBtn);
            }

            free(text);
            free(rmsg->thinking);
            free(rmsg);
            update_context_bar(d);
        } else {
            /* Error */
            char *text = rmsg->content;
            d->stream_phase = 0;
            if (text) {
                chat_append_ops(d->hDisplay,
                                "\r\n--- Error ---\r\n");
                chat_append_ops(d->hDisplay, text);
                chat_append_ops(d->hDisplay, "\r\n");
                free(text);
            }
            free(rmsg->thinking);
            free(rmsg);
        }

        if (src) src->busy = 0;
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
                /* All commands executed — stop timer, show waiting */
                KillTimer(hwnd, TIMER_CMD_QUEUE);
                d->commands_executed = d->queued_count;
                start_indicator(d, "waiting for output");
                SetTimer(hwnd, TIMER_CONTINUE, CONTINUE_DELAY_MS, NULL);
            } else {
                /* More commands pending — show progress after cmd text */
                char prog[80];
                snprintf(prog, sizeof(prog), "executing %d/%d",
                         d->queued_next + 1, d->queued_count);
                start_indicator(d, prog);
            }
        } else if (wParam == TIMER_CONTINUE) {
            KillTimer(hwnd, TIMER_CONTINUE);
            if (d->commands_executed > 0 && !ACTIVE_BUSY(d)) {
                d->commands_executed = 0;
                send_continue_message(d);
            }
        } else if (wParam == TIMER_SCROLL_SYNC) {
            display_sync_scroll(d);
            input_sync_scroll(d);
        }
        return 0;

    case WM_VSCROLL:
        if (d && d->hDisplayScrollbar &&
            (HWND)lParam == d->hDisplayScrollbar) {
            WORD code = LOWORD(wParam);
            int first = (int)SendMessage(d->hDisplay,
                            EM_GETFIRSTVISIBLELINE, 0, 0);
            int delta = 0;
            RECT erc_v;
            GetClientRect(d->hDisplay, &erc_v);
            int vis = edit_scroll_visible_lines(erc_v.bottom - erc_v.top,
                          d->display_line_h > 0 ? d->display_line_h : 1);
            switch (code) {
            case SB_LINEUP:    delta = -1;   break;
            case SB_LINEDOWN:  delta =  1;   break;
            case SB_PAGEUP:    delta = -vis; break;
            case SB_PAGEDOWN:  delta =  vis; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                delta = edit_scroll_line_delta(
                    csb_get_trackpos(d->hDisplayScrollbar), first);
                break;
            case SB_TOP:    delta = -first;  break;
            case SB_BOTTOM: delta =  99999;  break;
            }
            if (delta != 0)
                SendMessage(d->hDisplay, EM_LINESCROLL, 0, (LPARAM)delta);
            display_sync_scroll(d);
            return 0;
        }
        if (d && d->hInputScrollbar &&
            (HWND)lParam == d->hInputScrollbar) {
            WORD code = LOWORD(wParam);
            int first = (int)SendMessage(d->hInput,
                            EM_GETFIRSTVISIBLELINE, 0, 0);
            int delta = 0;
            RECT erc_i;
            GetClientRect(d->hInput, &erc_i);
            int vis = edit_scroll_visible_lines(erc_i.bottom - erc_i.top,
                          d->input_line_h > 0 ? d->input_line_h : 1);
            switch (code) {
            case SB_LINEUP:    delta = -1;   break;
            case SB_LINEDOWN:  delta =  1;   break;
            case SB_PAGEUP:    delta = -vis; break;
            case SB_PAGEDOWN:  delta =  vis; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                delta = edit_scroll_line_delta(
                    csb_get_trackpos(d->hInputScrollbar), first);
                break;
            case SB_TOP:    delta = -first;  break;
            case SB_BOTTOM: delta =  99999;  break;
            }
            if (delta != 0)
                SendMessage(d->hInput, EM_LINESCROLL, 0, (LPARAM)delta);
            input_sync_scroll(d);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (d && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (wParam == VK_OEM_PLUS || wParam == (WPARAM)'=') {
                chat_apply_zoom(d, 1);
                return 0;
            }
            if (wParam == VK_OEM_MINUS || wParam == (WPARAM)'-') {
                chat_apply_zoom(d, -1);
                return 0;
            }
        }
        break;

    case WM_MOUSEWHEEL:
        if (d && d->hDisplay) {
            int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
            /* Ctrl+Scroll zooms the font */
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                chat_apply_zoom(d, zdelta > 0 ? 1 : -1);
                return 0;
            }
            int scroll = edit_scroll_wheel_accum(zdelta, WHEEL_DELTA, 3,
                                                 &d->wheel_accum);
            if (scroll != 0)
                SendMessage(d->hDisplay, EM_LINESCROLL, 0, (LPARAM)scroll);
            display_sync_scroll(d);
            return 0;
        }
        break;

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
            if ((int)dis->CtlID == IDC_CHAT_SEND) {
                draw_themed_button(dis, d->theme, 1);
            } else if ((int)dis->CtlID == IDC_CHAT_SAVE) {
                /* Square save button with floppy disk icon */
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                int pressed = (dis->itemState & ODS_SELECTED) != 0;
                COLORREF bg = theme_cr(pressed ? d->theme->bg_primary
                                               : d->theme->bg_secondary);
                COLORREF fg = theme_cr(d->theme->text_main);
                COLORREF bdr = theme_cr(d->theme->border);

                HBRUSH hBgBr = CreateSolidBrush(theme_cr(d->theme->bg_primary));
                FillRect(hdc, &rc, hBgBr);
                DeleteObject(hBgBr);

                HBRUSH hBr = CreateSolidBrush(bg);
                HPEN hPen = CreatePen(PS_SOLID, 1, bdr);
                HGDIOBJ oBr = SelectObject(hdc, hBr);
                HGDIOBJ oPen = SelectObject(hdc, hPen);
                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
                SelectObject(hdc, oPen);
                SelectObject(hdc, oBr);
                DeleteObject(hPen);
                DeleteObject(hBr);

                /* Draw Fluent UI floppy disk icon (\xE74E Save) */
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, fg);
                HFONT prevF = (HFONT)SelectObject(hdc, d->hIconFont ? d->hIconFont : d->hFont);
                RECT txtRc = rc;
                DrawTextW(hdc, L"\xE74E", -1, &txtRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, prevF);
            } else if ((int)dis->CtlID == IDC_CHAT_UNDOCK) {
                /* Square undock button with 3D pop-out/dock-in icon */
                HDC hdc = dis->hDC;
                RECT brc = dis->rcItem;
                int pressed = (dis->itemState & ODS_SELECTED) != 0;
                COLORREF bg = theme_cr(pressed ? d->theme->bg_primary
                                               : d->theme->bg_secondary);
                COLORREF fg = theme_cr(d->theme->text_main);
                COLORREF bdr = theme_cr(d->theme->border);

                HBRUSH hBgBr = CreateSolidBrush(theme_cr(d->theme->bg_primary));
                FillRect(hdc, &brc, hBgBr);
                DeleteObject(hBgBr);

                HBRUSH hBr = CreateSolidBrush(bg);
                HPEN hPen = CreatePen(PS_SOLID, 1, bdr);
                HGDIOBJ oBr = SelectObject(hdc, hBr);
                HGDIOBJ oPen = SelectObject(hdc, hPen);
                RoundRect(hdc, brc.left, brc.top, brc.right, brc.bottom, 6, 6);
                SelectObject(hdc, oPen);
                SelectObject(hdc, oBr);
                DeleteObject(hPen);
                DeleteObject(hBr);

                /* Draw Fluent UI Pop-out/Dock icon */
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, fg);
                HFONT prevF = (HFONT)SelectObject(hdc, d->hIconFont ? d->hIconFont : d->hFont);
                /* \xE8A7 = FullScreen (outer arrows), \xE923 = BackToWindow */
                DrawTextW(hdc, d->docked ? L"\xE8A7" : L"\xE923", -1, &brc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, prevF);
            } else if ((int)dis->CtlID == IDC_CHAT_ALLOW) {
                /* Green Allow button */
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                int pressed = (dis->itemState & ODS_SELECTED) != 0;
                COLORREF greenBg = pressed ? RGB(0, 120, 60) : RGB(0, 160, 80);
                COLORREF fg = RGB(255, 255, 255);
                HBRUSH hParBr = CreateSolidBrush(theme_cr(d->theme->bg_primary));
                FillRect(hdc, &rc, hParBr);
                DeleteObject(hParBr);
                HBRUSH hBr = CreateSolidBrush(greenBg);
                HPEN hPen = CreatePen(PS_SOLID, 1, greenBg);
                SelectObject(hdc, hBr);
                SelectObject(hdc, hPen);
                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
                SelectObject(hdc, GetStockObject(NULL_BRUSH));
                SelectObject(hdc, GetStockObject(NULL_PEN));
                DeleteObject(hPen);
                DeleteObject(hBr);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, fg);
                HFONT hOld = d->hFont
                    ? (HFONT)SelectObject(hdc, d->hFont) : NULL;
                DrawText(hdc, "Allow", -1, &rc,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (hOld) SelectObject(hdc, hOld);
            } else if ((int)dis->CtlID == IDC_CHAT_DENY) {
                /* Red Deny button */
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                int pressed = (dis->itemState & ODS_SELECTED) != 0;
                COLORREF redBg = pressed ? RGB(160, 30, 30) : RGB(200, 50, 50);
                COLORREF fg = RGB(255, 255, 255);
                HBRUSH hParBr = CreateSolidBrush(theme_cr(d->theme->bg_primary));
                FillRect(hdc, &rc, hParBr);
                DeleteObject(hParBr);
                HBRUSH hBr = CreateSolidBrush(redBg);
                HPEN hPen = CreatePen(PS_SOLID, 1, redBg);
                SelectObject(hdc, hBr);
                SelectObject(hdc, hPen);
                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
                SelectObject(hdc, GetStockObject(NULL_BRUSH));
                SelectObject(hdc, GetStockObject(NULL_PEN));
                DeleteObject(hPen);
                DeleteObject(hBr);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, fg);
                HFONT hOld = d->hFont
                    ? (HFONT)SelectObject(hdc, d->hFont) : NULL;
                DrawText(hdc, "Deny", -1, &rc,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (hOld) SelectObject(hdc, hOld);
            } else {
                draw_tab_button(dis, d->theme, d);
            }
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
            KillTimer(hwnd, TIMER_SCROLL_SYNC);
            /* Save conversation back to session before cleanup */
            if (d->active_state) {
                memcpy(&d->active_state->conv, &d->conv, sizeof(AiConversation));
                d->active_state->valid = 1;
            }
            thinking_history_clear(d);
            DeleteCriticalSection(&d->cs);
            if (d->hFont) DeleteObject(d->hFont);
            if (d->hSmallFont) DeleteObject(d->hSmallFont);
            if (d->hIconFont) DeleteObject(d->hIconFont);
            if (d->hTooltip) DestroyWindow(d->hTooltip);
            if (d->hBrBgPrimary)   DeleteObject(d->hBrBgPrimary);
            if (d->hBrBgSecondary) DeleteObject(d->hBrBgSecondary);
            free(d);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)NULL);
        }
        return 0;

    case WM_CLOSE:
        if (d && d->docked) {
            /* Docked: ask parent to close the panel (preserves state) */
            SendMessage(GetParent(hwnd), WM_COMMAND,
                        MAKEWPARAM(IDM_VIEW_AI_CHAT, 0), 0);
            return 0;
        }
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
                  const char *ai_font,
                  const char *colour_scheme,
                  const char *session_notes, const char *system_notes,
                  AiSessionState *initial_state,
                  const char *session_name,
                  int docked)
{
    AiChatData *d = (AiChatData *)calloc(1, sizeof(AiChatData));
    if (!d) return NULL;

    InitializeCriticalSection(&d->cs);
    d->indicator_pos = -1;
    d->stream_display_start = -1;
    d->paste_delay_ms = paste_delay_ms;
    if (font_name && font_name[0])
        strncpy(d->font_name, font_name, sizeof(d->font_name) - 1);
    else
        strncpy(d->font_name, APP_FONT_DEFAULT, sizeof(d->font_name) - 1);
    if (ai_font && ai_font[0])
        strncpy(d->ai_font_name, ai_font, sizeof(d->ai_font_name) - 1);
    else
        strncpy(d->ai_font_name, APP_FONT_AI_DEFAULT, sizeof(d->ai_font_name) - 1);

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
    d->context_limit = ai_model_context_limit(model ? model : "deepseek-chat");

    /* Load existing conversation from session state if available */
    d->active_state = initial_state;
    if (initial_state && initial_state->valid) {
        memcpy(&d->conv, &initial_state->conv, sizeof(AiConversation));
    }

    if (session_notes)
        strncpy(d->session_notes, session_notes, sizeof(d->session_notes) - 1);
    if (system_notes)
        strncpy(d->system_notes, system_notes, sizeof(d->system_notes) - 1);
    if (session_name)
        strncpy(d->session_name, session_name, sizeof(d->session_name) - 1);

    d->docked = docked;

    /* Scale window size for DPI */
    int pdpi;
    {
        HDC hdc = GetDC(parent);
        pdpi = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(parent, hdc);
    }

    DWORD style = docked
        ? (WS_CHILD | WS_CLIPCHILDREN)       /* hidden until first resize */
        : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);

    HWND hwnd = CreateWindowEx(
        0, AI_CHAT_CLASS, "AI Assist",
        style,
        docked ? 0 : CW_USEDEFAULT,
        docked ? 0 : CW_USEDEFAULT,
        docked ? 1 : MulDiv(500, pdpi, 96),
        docked ? 1 : MulDiv(600, pdpi, 96),
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

void ai_chat_update_notes(HWND hwnd, const char *session_notes,
                          const char *system_notes)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    EnterCriticalSection(&d->cs);
    if (session_notes)
        strncpy(d->session_notes, session_notes, sizeof(d->session_notes) - 1);
    if (system_notes)
        strncpy(d->system_notes, system_notes, sizeof(d->system_notes) - 1);
    LeaveCriticalSection(&d->cs);
}

void ai_chat_set_theme(HWND hwnd, const char *colour_scheme)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    int idx = ui_theme_find(colour_scheme ? colour_scheme : "");
    d->theme = ui_theme_get(idx);

    /* Recreate cached brushes */
    if (d->hBrBgPrimary)   DeleteObject(d->hBrBgPrimary);
    if (d->hBrBgSecondary) DeleteObject(d->hBrBgSecondary);
    d->hBrBgPrimary   = CreateSolidBrush(theme_cr(d->theme->bg_primary));
    d->hBrBgSecondary = CreateSolidBrush(theme_cr(d->theme->bg_secondary));

    /* Update RichEdit background */
    SendMessage(d->hDisplay, EM_SETBKGNDCOLOR, 0,
                (LPARAM)theme_cr(d->theme->bg_secondary));

    /* Update custom scrollbar themes */
    if (d->hDisplayScrollbar)
        csb_set_theme(d->hDisplayScrollbar, d->theme);
    if (d->hInputScrollbar)
        csb_set_theme(d->hInputScrollbar, d->theme);

    /* Update title bar and borders */
    themed_apply_title_bar(hwnd, d->theme);
    themed_apply_borders(hwnd, d->theme);

    /* Force full repaint */
    InvalidateRect(hwnd, NULL, TRUE);
}

/* Internal helper: perform the actual session switch (save/load/rebuild).
 * Safe to call even while busy — each thread targets its own session. */
static void do_session_switch(AiChatData *d,
                              AiSessionState *new_state,
                              Terminal *term, SSHChannel *channel,
                              const char *session_notes,
                              const char *system_notes,
                              const char *session_name)
{
    /* Save current conversation to old session state.
     * Skip if the session is busy — the thread will commit to its own conv. */
    if (d->active_state && d->active_state != new_state &&
        !d->active_state->busy) {
        memcpy(&d->active_state->conv, &d->conv, sizeof(AiConversation));
        d->active_state->valid = 1;
    }

    /* Kill command timers — they belong to the old session */
    KillTimer(d->hwnd, TIMER_CMD_QUEUE);
    KillTimer(d->hwnd, TIMER_CONTINUE);

    /* Save pending approval state to old session (heap-allocated) */
    if (d->active_state && d->active_state != new_state) {
        /* Free any previous pending commands */
        free(d->active_state->pending_cmds);
        d->active_state->pending_cmds = NULL;
        d->active_state->pending_approval = d->pending_approval;
        d->active_state->pending_cmd_count = 0;
        if (d->pending_approval && d->queued_count > 0) {
            size_t sz = (size_t)d->queued_count * sizeof(d->queued_cmds[0]);
            d->active_state->pending_cmds = malloc(sz);
            if (d->active_state->pending_cmds) {
                memcpy(d->active_state->pending_cmds,
                       d->queued_cmds, sz);
                d->active_state->pending_cmd_count = d->queued_count;
            }
        }
    }

    /* Clear thinking history — it belongs to the old session */
    thinking_history_clear(d);

    /* Load new session's conversation */
    if (new_state && new_state != d->active_state) {
        if (new_state->valid) {
            memcpy(&d->conv, &new_state->conv, sizeof(AiConversation));
        } else {
            char model[64];
            strncpy(model, d->conv.model, sizeof(model) - 1);
            model[sizeof(model) - 1] = '\0';
            ai_conv_init(&d->conv, model);
        }
    }

    d->active_state = new_state;
    d->active_term = term;
    d->active_channel = channel;

    /* Update notes */
    if (session_notes)
        strncpy(d->session_notes, session_notes, sizeof(d->session_notes) - 1);
    else
        d->session_notes[0] = '\0';
    if (system_notes)
        strncpy(d->system_notes, system_notes, sizeof(d->system_notes) - 1);
    else
        d->system_notes[0] = '\0';

    /* Update session name label */
    if (session_name)
        strncpy(d->session_name, session_name, sizeof(d->session_name) - 1);
    else
        d->session_name[0] = '\0';
    if (d->hSessionLabel)
        SetWindowText(d->hSessionLabel, d->session_name);

    /* Reset transient UI state */
    d->indicator_pos = -1;
    d->commands_executed = 0;
    d->pending_request[0] = '\0';
    d->stream_phase = 0;

    /* Restore pending approval state from new session */
    if (new_state && new_state->pending_approval &&
        new_state->pending_cmds && new_state->pending_cmd_count > 0) {
        int nc = new_state->pending_cmd_count;
        if (nc > 16) nc = 16;
        memcpy(d->queued_cmds, new_state->pending_cmds,
               (size_t)nc * sizeof(d->queued_cmds[0]));
        d->queued_count = nc;
        d->queued_next = 0;
        d->pending_approval = 1;
    } else {
        d->pending_approval = 0;
        d->queued_count = 0;
        d->queued_next = 0;
    }
    relayout(d);

    chat_rebuild_display(d);

    /* Re-show the command approval prompt if switching back to a
     * session with pending approval */
    if (d->pending_approval && d->queued_count > 0) {
        char confirm[4096];
        size_t clen = ai_build_confirm_text(d->queued_cmds,
                         d->queued_count, confirm, sizeof(confirm));
        if (clen == 0)
            snprintf(confirm, sizeof(confirm),
                     "Execute %d command(s)?", d->queued_count);
        chat_append_ops(d->hDisplay, "\r\n");
        COLORREF dim = d->theme
            ? theme_cr(d->theme->text_dim)
            : GetSysColor(COLOR_GRAYTEXT);
        chat_append_color(d->hDisplay, confirm, dim);
        chat_append_ops(d->hDisplay, "\r\n");
    }

    /* If switching to a session that's still streaming,
     * re-append any accumulated content so the user sees progress.
     * Use the per-session stream buffers (populated by WM_AI_STREAM). */
    if (new_state && new_state->busy) {
        /* Copy session stream buffers into display-side buffers */
        d->stream_thinking[0] = '\0';
        d->stream_thinking_len = 0;
        d->stream_content[0] = '\0';
        d->stream_content_len = 0;
        if (new_state->stream_thinking && new_state->stream_thinking_len > 0) {
            size_t tlen = new_state->stream_thinking_len;
            if (tlen >= AI_MSG_MAX) tlen = AI_MSG_MAX - 1;
            memcpy(d->stream_thinking, new_state->stream_thinking, tlen);
            d->stream_thinking_len = tlen;
            d->stream_thinking[tlen] = '\0';
        }
        if (new_state->stream_content && new_state->stream_content_len > 0) {
            size_t clen = new_state->stream_content_len;
            if (clen >= AI_MSG_MAX) clen = AI_MSG_MAX - 1;
            memcpy(d->stream_content, new_state->stream_content, clen);
            d->stream_content_len = clen;
            d->stream_content[clen] = '\0';
        }
        d->stream_phase = new_state->stream_phase;

        if (d->stream_thinking_len > 0) {
            COLORREF col_purple = RGB(150, 100, 200);
            chat_append_styled(d->hDisplay, "\r\n\r\n", col_purple, 0);
            chat_append_styled_ex(d->hDisplay, "> Thinking...\r\n", col_purple, CLR_DEFAULT, CFE_BOLD | CFE_LINK, NULL, 180);
            if (d->show_thinking) {
                chat_append_styled(d->hDisplay, d->stream_thinking, col_purple, 1);
            }
            d->stream_phase = 1;
        }
        if (d->stream_content_len > 0) {
            if (d->stream_phase == 1)
                chat_append_ops(d->hDisplay, "\r\n");
            COLORREF col_ai = d->theme ? theme_cr(d->theme->text_main)
                                       : GetSysColor(COLOR_WINDOWTEXT);
            chat_append_styled(d->hDisplay, "\r\n\r\n", col_ai, 0);
            chat_append_styled_ex(d->hDisplay, "AI\r\n", RGB(0, 150, 200), CLR_DEFAULT, CFE_BOLD, NULL, 220);
            chat_append_color(d->hDisplay, d->stream_content, col_ai);
            d->stream_phase = 2;
        } else if (d->stream_phase <= 1) {
            /* Still in thinking phase or no content yet — show indicator */
            start_indicator(d, "thinking");
        }
    }
}

void ai_chat_switch_session(HWND hwnd,
                            AiSessionState *new_state,
                            Terminal *term, SSHChannel *channel,
                            const char *session_notes,
                            const char *system_notes,
                            const char *session_name)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    /* If the active session is busy, save the current conversation so the
     * thread can commit its result on top of this snapshot. */
    if (d->active_state && d->active_state->busy) {
        memcpy(&d->active_state->conv, &d->conv, sizeof(AiConversation));
        d->active_state->valid = 1;
    }

    /* Kill any indicator timer — it belongs to the old session's display */
    if (d->indicator_pos >= 0) {
        KillTimer(hwnd, TIMER_THINKING);
        d->indicator_pos = -1;
    }

    /* Switch immediately — any running thread continues in the background
     * targeting its own session directly. */
    do_session_switch(d, new_state, term, channel,
                      session_notes, system_notes, session_name);
}

void ai_chat_notify_session_closed(HWND hwnd, AiSessionState *state)
{
    if (!hwnd || !IsWindow(hwnd) || !state) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    /* Free heap-allocated pending commands */
    free(state->pending_cmds);
    state->pending_cmds = NULL;

    /* Free per-session stream buffers */
    free(state->stream_content);
    state->stream_content = NULL;
    free(state->stream_thinking);
    state->stream_thinking = NULL;
    state->busy = 0;

    if (d->active_state == state)
        d->active_state = NULL;
}

void ai_chat_close(HWND hwnd)
{
    if (hwnd && IsWindow(hwnd))
        DestroyWindow(hwnd);
}

int ai_chat_has_content(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return 0;
    AiChatData *d = (AiChatData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return 0;
    /* At least one user or assistant message beyond the system prompt */
    return d->conv.msg_count > 1;
}

#endif /* _WIN32 */
