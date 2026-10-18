/* tests/test_chat_select_win.c -- make wintest: the real GDI renderer and
 * chat list view behind character selection (spec docs/superpowers/specs/
 * 2026-10-18-ai-panel-text-selection-design.md section 7). A hidden list
 * view is filled with a fixture, painted into a memory DC through
 * WM_PRINTCLIENT at two widths and two DPIs, and its recorded runs checked:
 * cum[] monotonic, the centre of every run hit-tests back into it, every
 * item's measured height equals its painted height, and the painted words
 * equal chat_copy_visible() with whitespace set aside. */

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "chat_listview.h"
#include "chat_msg.h"
#include "chat_copy.h"
#include "text_runs.h"
#include "ui_theme.h"
#include "ns_reduced_motion.h"
#include "ai_prompt.h"

/* window.c owns these in the app; this harness links without it. */
static ThemeTokens g_win_tokens;
const ThemeTokens *ns_tokens(void) { return &g_win_tokens; }
int ns_reduced_motion(void) { return 1; }

/* ai_prompt.c drags in the HTTP and tool stack; the list view only needs
 * this for the Thinking summary's word count. */
int ai_word_count(const char *text)
{
    int n = 0, in = 0;
    for (; text && *text; text++) {
        int sp = (*text == ' ' || *text == '\t' || *text == '\n' ||
                  *text == '\r');
        if (!sp && !in) n++;
        in = !sp;
    }
    return n;
}

static const char FIX_USER[] =
    "Can you check disk usage?\n"
    "  indented line kept\n"
    "averyveryveryverylongwordthatcannotfitonanylineofthebubbleatall";

static const char FIX_THINK[] =
    "Plan: run df, then du on /var/log, then summarise.";

static const char FIX_AI[] =
    "## Disk **Usage**\n"
    "I checked the mounts first:\n"
    "[EXEC]df -h / && echo done[/EXEC]\n"
    "Here's what I found on **web-01** with `du`:\n"
    "\n"
    "- Root partition: 42% used\n"
    "1. first\n"
    "1. second, a longer item that wraps at the narrow width for sure\n"
    "\n"
    "> quoted *note*\n"
    "| Mount | Used |\n"
    "|---|--:|\n"
    "| / | 42% |\n"
    "\n"
    "```bash\n"
    "cd /var/log && ls -la | sort -rh | head -3\n"
    "```\n"
    "Gr\xC3\xBC\xC3\x9F" "e \xF0\x9F\x98\x80 done.";

static const char FIX_STATUS[] = "Stream cancelled by user.";

static const char FIX_CARD_CMD[] = "zzcardcmdzz --never-copied";

static LRESULT CALLBACK host_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static HFONT make_font(int pt, int dpi, int weight, const char *face)
{
    return CreateFontA(-MulDiv(pt, dpi, 72), 0, 0, 0, weight, 0, 0, 0,
                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

/* Strip spaces, tabs and line breaks. */
static void squeeze(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (; *in && n + 1 < cap; in++)
        if (*in != ' ' && *in != '\t' && *in != '\n' && *in != '\r')
            out[n++] = *in;
    out[n] = '\0';
}

/* The painted words of one part of an item, in paint order. */
static void painted_text(const ChatMsgItem *it, int part, const char *text,
                         char *out, size_t cap)
{
    size_t n = 0;
    out[0] = '\0';
    for (int i = 0; i < it->runs.n; i++) {
        const TextRun *r = &it->runs.v[i];
        if (r->part != part) continue;
        const char *s = r->synthetic[0] ? r->synthetic : text + r->start;
        size_t k = r->synthetic[0] ? strlen(r->synthetic)
                                   : (size_t)(r->end - r->start);
        if (n + k + 1 > cap) k = cap - 1 - n;
        memcpy(out + n, s, k);
        n += k;
        out[n] = '\0';
    }
}

static int check_item(const ChatMsgItem *it, int markdown, int *checked)
{
    int bad = 0;
    if (it->measured_height <= 0 || it->type == CHAT_ITEM_COMMAND) return 0;
    (*checked)++;
    if (it->painted_height != it->measured_height) {
        printf("  item %d: measured %d, painted %d\n", it->id,
               it->measured_height, it->painted_height);
        bad++;
    }
    if (it->runs.n == 0) {
        printf("  item %d recorded no runs\n", it->id);
        return bad + 1;
    }
    for (int i = 0; i < it->runs.n; i++) {
        const TextRun *r = &it->runs.v[i];
        for (int k = 1; k < r->n_cum; k++) {
            if (it->runs.cum[r->first_cum + k] < it->runs.cum[r->first_cum + k - 1] ||
                it->runs.cum_byte[r->first_cum + k] <= it->runs.cum_byte[r->first_cum + k - 1]) {
                printf("  item %d run %d: cum not monotonic\n", it->id, i);
                bad++;
                break;
            }
        }
        if (r->synthetic[0] || r->w <= 0) continue;
        int part = -1;
        int off = text_runs_hit(&it->runs, r->x + r->w / 2, r->y + r->h / 2,
                                &part);
        if (part != r->part || off < r->start || off > r->end) {
            printf("  item %d run %d [%d,%d): centre hit %d part %d\n",
                   it->id, i, r->start, r->end, off, part);
            bad++;
        }
    }
    /* painted words == copied text, whitespace aside */
    static char painted[8192], copied_sq[8192], painted_sq[8192];
    char *copied = NULL;
    int flags = (it->type == CHAT_ITEM_AI_TEXT)
        ? (CHAT_COPY_EXEC | (markdown ? CHAT_COPY_MD : 0)) : 0;
    chat_copy_visible(it->text, (int)it->text_len, 0, (int)it->text_len,
                      flags, &copied);
    painted_text(it, TR_PART_MAIN, it->text, painted, sizeof(painted));
    squeeze(painted, painted_sq, sizeof(painted_sq));
    squeeze(copied ? copied : "", copied_sq, sizeof(copied_sq));
    if (strcmp(painted_sq, copied_sq) != 0) {
        printf("  item %d: painted \"%s\"\n         copied  \"%s\"\n", it->id,
               painted_sq, copied_sq);
        bad++;
    }
    free(copied);
    if (it->type == CHAT_ITEM_AI_TEXT && it->u.ai.thinking_text) {
        painted_text(it, TR_PART_THINKING, it->u.ai.thinking_text, painted,
                     sizeof(painted));
        squeeze(painted, painted_sq, sizeof(painted_sq));
        squeeze(it->u.ai.thinking_text, copied_sq, sizeof(copied_sq));
        if (strcmp(painted_sq, copied_sq) != 0) {
            printf("  item %d Thinking: painted \"%s\"\n", it->id, painted_sq);
            bad++;
        }
    }
    return bad;
}

/* A hidden host window holding a chat list view over `list`, painted
 * once into a memory DC at `width` x CLV_TEST_H and `dpi`. */
#define CLV_TEST_H 3000
typedef struct {
    HWND host, lv;
    HFONT f, m, b, s;
} TestView;

static int view_open(TestView *tv, ChatMsgList *list, int width, int dpi,
                     int markdown)
{
    static int registered = 0;
    HINSTANCE inst = GetModuleHandleA(NULL);
    memset(tv, 0, sizeof(*tv));
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = host_proc;
        wc.hInstance = inst;
        wc.lpszClassName = "NsSelTestHost";
        RegisterClassA(&wc);
        chat_listview_register(inst);
        ui_theme_resolve(ui_theme_get(0), &g_win_tokens);
        registered = 1;
    }
    tv->host = CreateWindowA("NsSelTestHost", "", WS_POPUP, 0, 0, width,
                             CLV_TEST_H, NULL, NULL, inst, NULL);
    if (!tv->host) return 0;
    tv->lv = chat_listview_create(tv->host, 0, 0, width, CLV_TEST_H, list,
                                  ui_theme_get(0));
    if (!tv->lv) return 0;
    if (dpi != 96) {
        RECT r = { 0, 0, width, CLV_TEST_H };
        SendMessageA(tv->lv, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), (LPARAM)&r);
    }
    tv->f = make_font(10, dpi, FW_NORMAL, "Segoe UI");
    tv->m = make_font(10, dpi, FW_NORMAL, "Consolas");
    tv->b = make_font(10, dpi, FW_BOLD, "Segoe UI");
    tv->s = make_font(9, dpi, FW_NORMAL, "Segoe UI");
    chat_listview_set_fonts(tv->lv, tv->f, tv->m, tv->b, tv->s);
    chat_listview_set_render_markdown(tv->lv, markdown);
    chat_listview_invalidate(tv->lv);
    return 1;
}

/* Paint the list view into a memory DC (no WM_PAINT reaches a hidden
 * window); the paint records every item's runs. */
static void view_paint(TestView *tv, int width)
{
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, width, CLV_TEST_H);
    HGDIOBJ old = SelectObject(mem, bmp);
    SendMessageA(tv->lv, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void view_close(TestView *tv)
{
    if (tv->lv) DestroyWindow(tv->lv);
    if (tv->host) DestroyWindow(tv->host);
    if (tv->f) DeleteObject(tv->f);
    if (tv->m) DeleteObject(tv->m);
    if (tv->b) DeleteObject(tv->b);
    if (tv->s) DeleteObject(tv->s);
}

static void fill_fixture(ChatMsgList *list)
{
    chat_msg_list_init(list);
    chat_msg_append(list, CHAT_ITEM_USER, FIX_USER);
    ChatMsgItem *ai = chat_msg_append(list, CHAT_ITEM_AI_TEXT, FIX_AI);
    chat_msg_set_thinking(ai, FIX_THINK);
    ai->u.ai.thinking_collapsed = 0;
    ai->u.ai.thinking_complete = 1;
    /* A command card between two text items: it records no runs. */
    ChatMsgItem *card = chat_msg_append(list, CHAT_ITEM_COMMAND, "");
    chat_msg_set_command(card, FIX_CARD_CMD, CMD_READ, 0);
    chat_msg_set_batch(card, 1);
    chat_msg_append(list, CHAT_ITEM_STATUS, FIX_STATUS);
}

/* One configuration: list width, DPI, markdown on or off. "Off" is a real
 * toggle: laid out and painted with markdown on first, then switched off,
 * which must remeasure every item (a stale height is the bug this catches). */
static int run_config(int width, int dpi, int markdown)
{
    ChatMsgList list;
    fill_fixture(&list);
    TestView tv;
    int bad = 0;
    if (!view_open(&tv, &list, width, dpi, 1)) {
        printf("  could not create the windows (error %lu)\n",
               (unsigned long)GetLastError());
        bad = 1;
    } else {
        view_paint(&tv, width);
        ChatMsgItem *user = list.head, *ai = user->next;
        ChatMsgItem *card = ai->next, *status = card->next;
        int h_on = ai->measured_height;
        if (!markdown) {
            chat_listview_set_render_markdown(tv.lv, 0);
            view_paint(&tv, width);
            if (ai->measured_height == h_on) {
                printf("  AI height still %d after markdown off\n", h_on);
                bad++;
            }
        }
        if (user->measured_height <= 0 || ai->measured_height <= 0 ||
            status->measured_height <= 0) {
            printf("  nothing was laid out (heights %d %d %d)\n",
                   user->measured_height, ai->measured_height,
                   status->measured_height);
            bad++;
        }
        if (card->runs.n != 0) {
            printf("  the command card recorded %d runs\n", card->runs.n);
            bad++;
        }
        int checked = 0;
        for (ChatMsgItem *it = list.head; it; it = it->next)
            bad += check_item(it, markdown, &checked);
        if (checked != 3) {
            printf("  checked %d text items, want 3\n", checked);
            bad++;
        }
    }
    view_close(&tv);
    chat_msg_list_clear(&list);
    if (bad) printf("  (width %d, dpi %d, markdown %d)\n", width, dpi, markdown);
    return bad;
}

int test_chat_select_win_runs_match_paint(void)
{
    TEST_BEGIN();
    static const int widths[2] = { 420, 760 };
    static const int dpis[2] = { 96, 144 };
    for (int w = 0; w < 2; w++)
        for (int d = 0; d < 2; d++) {
            int bad = run_config(widths[w], dpis[d], 1);
            ASSERT_EQ(bad, 0);
        }
    int plain_bad = run_config(420, 96, 0);
    ASSERT_EQ(plain_bad, 0);
    TEST_END();
}

/* The run of `it` (main part) whose text starts with `word`; *wx gets the
 * pixel width of the word itself (trailing blanks excluded). */
static const TextRun *find_word(const ChatMsgItem *it, const char *word,
                                int *wx)
{
    int n = (int)strlen(word);
    for (int i = 0; i < it->runs.n; i++) {
        const TextRun *r = &it->runs.v[i];
        if (r->part != TR_PART_MAIN || r->synthetic[0] || r->end - r->start < n)
            continue;
        if (memcmp(it->text + r->start, word, (size_t)n) != 0) continue;
        *wx = r->w;
        for (int k = 0; k < r->n_cum; k++)
            if (it->runs.cum_byte[r->first_cum + k] == n)
                *wx = it->runs.cum[r->first_cum + k];
        return r;
    }
    return NULL;
}

/* The selection's visible text, as Ctrl+C would copy it. */
static int sel_is(HWND lvh, const ChatMsgList *list, const char *want)
{
    ChatListView *lv = (ChatListView *)GetWindowLongPtrA(lvh, GWLP_USERDATA);
    char *out = NULL;
    if (lv && lv->sel_valid)
        chat_copy_selection(list, lv->sel_anchor, lv->sel_focus,
                            lv->render_markdown, &out);
    int ok = out && strcmp(out, want) == 0;
    if (!ok) printf("  selection \"%s\", want \"%s\"\n", out ? out : "(none)", want);
    free(out);
    return ok;
}

static LPARAM pt(int x, int y) { return MAKELPARAM(x, y); }

/* The mouse paths of section 5 through the real window procedure: a drag
 * across two words, a click that clears, a double-click on a word and on a
 * code token, a triple-click on a list line, Ctrl+A and Escape. */
int test_chat_select_win_mouse_and_keys(void)
{
    TEST_BEGIN();
    ChatMsgList list;
    fill_fixture(&list);
    TestView tv;
    int opened = view_open(&tv, &list, 760, 96, 1);
    ASSERT_TRUE(opened);
    if (opened) {
        view_paint(&tv, 760);
        const ChatMsgItem *ai = list.head->next;
        int w1 = 0, w2 = 0, w3 = 0, w4 = 0;
        const TextRun *a = find_word(ai, "mounts", &w1);
        const TextRun *b = find_word(ai, "first:", &w2);
        const TextRun *c = find_word(ai, "partition:", &w3);
        const TextRun *d = find_word(ai, "cd /var/log && ls", &w4);
        ASSERT_NOT_NULL(a); ASSERT_NOT_NULL(b); ASSERT_NOT_NULL(c);
        ASSERT_NOT_NULL(d);
        if (a && b && c && d) {
            int ay = a->y + a->h / 2, by = b->y + b->h / 2;
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(a->x + 1, ay));
            SendMessageA(tv.lv, WM_MOUSEMOVE, MK_LBUTTON, pt(b->x + w2 / 2, by));
            SendMessageA(tv.lv, WM_MOUSEMOVE, MK_LBUTTON, pt(b->x + w2 - 1, by));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(b->x + w2 - 1, by));
            ASSERT_TRUE(sel_is(tv.lv, &list, "mounts first:"));

            /* a click with no movement clears it */
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(a->x + 1, ay));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(a->x + 2, ay));
            ChatListView *lv = (ChatListView *)GetWindowLongPtrA(tv.lv, GWLP_USERDATA);
            ASSERT_TRUE(lv && !lv->sel_valid);

            /* double-click a word, then a third click on the same spot */
            int cy = c->y + c->h / 2;
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(c->x + 2, cy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(c->x + 2, cy));
            SendMessageA(tv.lv, WM_LBUTTONDBLCLK, MK_LBUTTON, pt(c->x + 2, cy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(c->x + 2, cy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "partition:"));
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(c->x + 2, cy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(c->x + 2, cy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "- Root partition: 42% used"));

            /* double-click inside a code line takes the token, not the line */
            int dy = d->y + d->h / 2;
            Sleep(GetDoubleClickTime() + 50);   /* no triple-click this time */
            SendMessageA(tv.lv, WM_LBUTTONDBLCLK, MK_LBUTTON, pt(d->x + w4 - 2, dy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(d->x + w4 - 2, dy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "ls"));

            /* the third click takes the source line */
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(d->x + w4 - 2, dy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(d->x + w4 - 2, dy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "cd /var/log && ls -la | sort -rh | head -3"));

            /* a fourth click is paired into another double-click: the word
             * again, not the line */
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(d->x + w4 - 2, dy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(d->x + w4 - 2, dy));
            SendMessageA(tv.lv, WM_LBUTTONDBLCLK, MK_LBUTTON, pt(d->x + w4 - 2, dy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(d->x + w4 - 2, dy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "ls"));

            /* double-click on the blank between two code tokens: a blank is
             * not a token, so the pointer's nearer neighbour is taken --
             * left of the blank's middle "cd", right of it "/var/log" */
            int wcd = 0, wcds = 0;
            ASSERT_NOT_NULL(find_word(ai, "cd", &wcd));
            ASSERT_NOT_NULL(find_word(ai, "cd ", &wcds));
            ASSERT_TRUE(wcds > wcd);
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(d->x + wcd + 1, dy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(d->x + wcd + 1, dy));
            SendMessageA(tv.lv, WM_LBUTTONDBLCLK, MK_LBUTTON, pt(d->x + wcd + 1, dy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(d->x + wcd + 1, dy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "cd"));
            SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(d->x + wcds - 1, dy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(d->x + wcds - 1, dy));
            SendMessageA(tv.lv, WM_LBUTTONDBLCLK, MK_LBUTTON, pt(d->x + wcds - 1, dy));
            SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(d->x + wcds - 1, dy));
            ASSERT_TRUE(sel_is(tv.lv, &list, "/var/log"));

            /* double-click on a bullet marker's synthetic run selects
             * nothing: the marker is never copied by a double-click */
            SendMessageA(tv.lv, WM_KEYDOWN, VK_ESCAPE, 0);
            ASSERT_TRUE(!lv->sel_valid);
            const TextRun *mk = NULL;
            for (int i = 0; i < ai->runs.n && !mk; i++)
                if (ai->runs.v[i].part == TR_PART_MAIN &&
                    ai->runs.v[i].synthetic[0] && ai->runs.v[i].w > 0)
                    mk = &ai->runs.v[i];
            ASSERT_NOT_NULL(mk);
            if (mk) {
                int mx = mk->x + mk->w / 2, my = mk->y + mk->h / 2;
                SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(mx, my));
                SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(mx, my));
                SendMessageA(tv.lv, WM_LBUTTONDBLCLK, MK_LBUTTON, pt(mx, my));
                SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(mx, my));
                ASSERT_TRUE(!lv->sel_valid);
            }

            /* a command card records no runs: a hit in its band resolves to
             * the edge of the nearest text item and never copies card text */
            ChatMsgItem *card = ai->next;
            const ChatMsgItem *st = card ? card->next : NULL;
            ASSERT_TRUE(card && card->type == CHAT_ITEM_COMMAND && st);
            if (card && st && lv) {
                int cy0 = lv->msg_gap - lv->scroll_y;
                for (const ChatMsgItem *q = list.head; q != card; q = q->next)
                    cy0 += q->measured_height + lv->msg_gap;
                int sy = st->runs.v[0].y + st->runs.v[0].h / 2;
                int sx = st->runs.v[0].x;
                int swx = st->runs.v[0].w;
                /* down in the lower part of the card's band (nearer the
                 * item below), drag to the end of the status text */
                int low = cy0 + card->measured_height + lv->msg_gap - 1;
                SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(sx + 1, low));
                SendMessageA(tv.lv, WM_MOUSEMOVE, MK_LBUTTON, pt(sx + swx - 1, sy));
                SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(sx + swx - 1, sy));
                ASSERT_TRUE(lv->sel_valid);
                ASSERT_EQ(lv->sel_anchor.msg_id, st->id);
                ASSERT_EQ(lv->sel_anchor.off, 0);
                char *cardsel = NULL;
                chat_copy_selection(&list, lv->sel_anchor, lv->sel_focus, 1, &cardsel);
                ASSERT_TRUE(cardsel && !strstr(cardsel, "zzcardcmdzz"));
                free(cardsel);
                /* down in the upper part of the band, drag into the status
                 * text: the anchor is the end of the item above */
                int high = cy0 + 1;
                SendMessageA(tv.lv, WM_LBUTTONDOWN, MK_LBUTTON, pt(sx + 1, high));
                SendMessageA(tv.lv, WM_MOUSEMOVE, MK_LBUTTON, pt(sx + swx - 1, sy));
                SendMessageA(tv.lv, WM_LBUTTONUP, 0, pt(sx + swx - 1, sy));
                ASSERT_TRUE(lv->sel_valid);
                ASSERT_EQ(lv->sel_anchor.msg_id, ai->id);
                ASSERT_EQ(lv->sel_anchor.off, (int)ai->text_len);
                cardsel = NULL;
                chat_copy_selection(&list, lv->sel_anchor, lv->sel_focus, 1, &cardsel);
                ASSERT_TRUE(cardsel && !strstr(cardsel, "zzcardcmdzz"));
                free(cardsel);
            }

            /* Ctrl+A selects every item; Escape clears */
            BYTE keys[256], saved[256];
            GetKeyboardState(saved);
            memcpy(keys, saved, sizeof(keys));
            keys[VK_CONTROL] = 0x80;
            SetKeyboardState(keys);
            SendMessageA(tv.lv, WM_KEYDOWN, 'A', 0);
            SetKeyboardState(saved);
            char *all = NULL;
            ChatSelPos sa, sb;
            ASSERT_TRUE(chat_sel_all(&list, &sa, &sb));
            chat_copy_selection(&list, sa, sb, 1, &all);
            ASSERT_NOT_NULL(all);
            if (all) ASSERT_TRUE(sel_is(tv.lv, &list, all));
            free(all);
            SendMessageA(tv.lv, WM_KEYDOWN, VK_ESCAPE, 0);
            ASSERT_TRUE(lv && !lv->sel_valid);
        }
    }
    view_close(&tv);
    chat_msg_list_clear(&list);
    TEST_END();
}

#endif /* _WIN32 */
