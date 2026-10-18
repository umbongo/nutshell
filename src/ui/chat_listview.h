/* src/ui/chat_listview.h */
#ifndef NUTSHELL_CHAT_LISTVIEW_H
#define NUTSHELL_CHAT_LISTVIEW_H

#include <windows.h>
#include "chat_msg.h"
#include "chat_activity.h"
#include "ui_theme.h"
#include "ns_hover.h"
#include "chat_copy.h"

/* One copy glyph on a fenced code block in a reply, as last painted:
 * recorded by on_paint() so hover and click hit-test exactly what is on
 * screen. `rc` is the glyph's rect in client coordinates, `text` a heap copy
 * of the block body (no trailing newline) taken at paint time, so a click
 * copies what the user saw even if the reply grew since. */
#define CLV_CODE_HITS_MAX 64
typedef struct {
    int     item_id;    /* ChatMsgItem.id of the reply */
    int     block;      /* block number within the item, 0..15 */
    RECT    rc;
    char   *text;       /* heap, owned by the list view */
    size_t  len;
} ClvCodeHit;

typedef struct {
    HWND hwnd;              /* The owner-drawn panel window */
    ChatMsgList *msg_list;  /* Pointer to message list (not owned) */
    const ThemeColors *theme;

    /* Scroll state */
    int scroll_y;           /* Current scroll offset in pixels */
    int total_height;       /* Total content height in pixels */
    int viewport_height;    /* Visible area height */
    int stick_to_bottom;    /* 1 = follow content growth (classic stick-to-bottom) */

    /* Fonts (not owned, set by parent) */
    HFONT hFont;
    HFONT hMonoFont;
    HFONT hBoldFont;
    HFONT hSmallFont;

    /* Activity indicator (not owned, set by parent) */
    ActivityState *activity;
    int pulse_toggle;           /* Animation toggle for pulsing dot */

    /* Character selection (spec 2026-10-18 section 5): anchor and focus as
     * (message id, part, byte offset), resolved by id on every use; the
     * selection is dropped when either id no longer names an item. */
    ChatSelPos sel_anchor;
    ChatSelPos sel_focus;
    int sel_active;             /* 1 = the button is down, dragging */
    int sel_valid;              /* 1 = a non-empty selection exists */
    int sel_moved;              /* 1 = this drag passed the 4 px threshold */
    int sel_down_x, sel_down_y; /* where the button went down */
    int sel_last_x, sel_last_y; /* last drag point, re-hit after a wheel */
    int sel_keep_on_up;         /* a triple-click: keep it on button-up */
    int dbl_armed;              /* a handled double-click may become triple */
    DWORD dbl_time;             /* GetTickCount() of that double-click */
    int dbl_x, dbl_y;           /* where it was */

    /* Id of the user item whose copy on_paint() pinned at the top of the
     * list this frame (its runs are in pinned_runs), 0 when none. */
    int pinned_id;

    /* 1 under --ui-demo: answer CLV_MSG_QUERY_WORD (the integration
     * harness asks where a word was painted). */
    int query_enabled;

    /* Command list collapse state (0 = collapsed, 1 = expanded) */
    int cmd_list_expanded;

    /* Command container scroll/height state now lives per-container: see
     * ChatMsgItem.u.cmd.container_scroll (chat_msg.h) and
     * cmd_container_measure() in chat_listview.c, which computes a
     * container's row count/heights/text widths on demand from its first
     * item -- one container per run of consecutive unsettled command items
     * sharing a batch id (pending command batches). */
    int cmd_heights[16];        /* Individual card heights (pre-container) */

    /* DPI scaling factor (1.0 = 96 DPI) */
    float dpi_scale;

    /* Layout constants (scaled) */
    int msg_gap;            /* Inter-message gap (12px base) */
    int user_pad_h;         /* User bubble horizontal padding (10px) */
    int user_pad_v;         /* User bubble vertical padding (8px) */
    int ai_indent;          /* AI content left indent (30px) */
    int code_pad;           /* Code block padding (6px) */

    /* Model name for AI label display */
    char model_name[64];

    /* External custom scrollbar (not owned) */
    HWND ext_scrollbar;

    int render_markdown;        /* 1 = markdown render on, 0 = plain text */

    /* Hover tracking for painted elements (approval card rows/actions,
     * the [Retry] link) -- see ns_hover.h and Design-System Foundation
     * task 6. Element ids: CLV_CMD_ROW_HIT_ID/CLV_CMD_CARD_HIT_ID/
     * CLV_HOVER_RETRY in chat_listview.c (see the id-scheme comment there
     * for how a command-container id also encodes which container). */
    NsHover hover;
    int hover_tracking;         /* 1 while TrackMouseEvent(TME_LEAVE) is armed */

    /* "Nothing to show yet" state (ai_panel_states.h's AiPanelStateId, or
     * -1 for none), painted by on_paint() only while the message list has
     * zero items -- see chat_listview_set_state() and AI Assist Panel
     * task 4's "Empty and blocked states" section. state_context_lines
     * feeds AI_STATE_EMPTY's "last N lines" body. */
    int state_id;
    int state_context_lines;

    /* y coordinate at which on_paint() last placed the inline activity
     * indicator -- computed from the actually-painted bottom of the item
     * list, not just the measured layout, so it never lands on top of an
     * item that painted taller than it measured (Activity indicator
     * overlap fix). chatlv_items_bottom_y() returns this cached value so
     * hover/click hit-testing for the [Retry] link can never disagree
     * with where on_paint() actually drew it. */
    int indicator_y;

    /* Copy glyphs on code blocks, rebuilt by every on_paint() (see
     * ClvCodeHit). */
    ClvCodeHit code_hits[CLV_CODE_HITS_MAX];
    int n_code_hits;

    /* Bottom edge (client y) of the pinned user bubble when on_paint() drew
     * one over the list, 0 when it did not. Anything under it is covered, so
     * the code-block and settled-row hit tests ignore points above it. */
    int sticky_bottom;

} ChatListView;

/* The risk tag's chip colours and text for a command's safety level -- the
 * one place the card, the settled rows and the command details popup take
 * them from, so the tag always looks the same. */
void chatlv_safety_tag_colors(CmdSafetyLevel level, COLORREF *bg, COLORREF *fg);
const char *chatlv_safety_tag_text(CmdSafetyLevel level);

/* Register the window class. Call once at startup. */
void chat_listview_register(HINSTANCE hInstance);

/* Create the list view as a child window. */
HWND chat_listview_create(HWND parent, int x, int y, int w, int h,
                          ChatMsgList *msg_list, const ThemeColors *theme);

/* Set fonts (called after creation or font change). */
void chat_listview_set_fonts(HWND hwnd, HFONT font, HFONT mono,
                             HFONT bold, HFONT small_font);

/* Set theme (triggers full repaint). */
void chat_listview_set_theme(HWND hwnd, const ThemeColors *theme);

/* --ui-demo only (see chat_listview_enable_query): where was a word
 * painted? wParam: LOWORD = the item's position in the list (0 = first),
 * HIWORD = 1 for its Thinking body, 0 for its text; lParam: a global atom
 * (GlobalAddAtom) naming the word, ASCII. Returns the rect, in list-view
 * client pixels, from the left edge of the first painted run that begins
 * with the word to the end of the word itself, packed as
 * x | y << 16 | w << 32 | h << 48 (16 bits each), or 0 when no painted run
 * begins with it, the rect lies above or left of the client area, or the
 * query is not enabled. Runs are recorded for the whole of every item a
 * paint touched, so a word below the visible area still answers (with
 * y >= the client height). */
#define CLV_MSG_QUERY_WORD (WM_APP + 0x51)

/* Answer CLV_MSG_QUERY_WORD from now on. Called by the --ui-demo setup only. */
void chat_listview_enable_query(HWND hwnd);


/* Select from `a` to `b` (either order) and repaint -- the --ui-demo=chat
 * preset (spec 2026-10-18 section 6). */
void chat_listview_set_selection(HWND hwnd, ChatSelPos a, ChatSelPos b);

/* Edit > Copy: when this list view has keyboard focus and a selection,
 * copy the selection's visible text to the clipboard and return 1;
 * otherwise return 0 and touch nothing (the caller copies the terminal's
 * selection instead). */
int chat_listview_copy_selection(HWND hwnd);

/* Notify that the message list has changed. Triggers remeasure + repaint. */
void chat_listview_invalidate(HWND hwnd);

/* Scroll to bottom (e.g., after new message). */
void chat_listview_scroll_to_bottom(HWND hwnd);

/* Scroll to top (e.g., to reveal a Thinking disclosure above a long reply). */
void chat_listview_scroll_to_top(HWND hwnd);

/* Returns 1 if the list is scrolled near the bottom. */
int chat_listview_is_near_bottom(HWND hwnd);

/* Recalculate layout after resize. */
void chat_listview_relayout(HWND hwnd);

/* Set activity state pointer (not owned). */
void chat_listview_set_activity(HWND hwnd, ActivityState *activity);

/* Set pulse toggle (called by parent on heartbeat timer). */
void chat_listview_set_pulse(HWND hwnd, int toggle);

/* Toggle command list expand/collapse state. */
void chat_listview_toggle_cmd_expand(HWND hwnd);

/* Reset command list expand state (e.g., on new AI response). */
void chat_listview_reset_cmd_expand(HWND hwnd);

/* Set external custom scrollbar to sync with. */
void chat_listview_set_scrollbar(HWND hwnd, HWND scrollbar);

/* Set the AI model name displayed next to "AI" label. */
void chat_listview_set_model(HWND hwnd, const char *model);

/* Toggle markdown rendering. Triggers a redraw. Default after create: 1. */
void chat_listview_set_render_markdown(HWND hwnd, int enabled);

/* Set (or clear, with state_id = -1) the "nothing to show yet" state to
 * paint when the message list has no items -- an AiPanelStateId from
 * ai_panel_states.h (AI_STATE_EMPTY/NO_KEY/NO_SESSION). context_lines is
 * substituted into AI_STATE_EMPTY's body ("last N lines"); ignored by the
 * other states. Triggers a repaint; a no-op HWND is ignored. */
void chat_listview_set_state(HWND hwnd, int state_id, int context_lines);

#endif /* NUTSHELL_CHAT_LISTVIEW_H */
