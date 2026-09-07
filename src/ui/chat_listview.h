/* src/ui/chat_listview.h */
#ifndef NUTSHELL_CHAT_LISTVIEW_H
#define NUTSHELL_CHAT_LISTVIEW_H

#include <windows.h>
#include "chat_msg.h"
#include "chat_activity.h"
#include "ui_theme.h"
#include "ns_hover.h"

typedef struct {
    HWND hwnd;              /* The owner-drawn panel window */
    ChatMsgList *msg_list;  /* Pointer to message list (not owned) */
    const ThemeColors *theme;

    /* Scroll state */
    int scroll_y;           /* Current scroll offset in pixels */
    int total_height;       /* Total content height in pixels */
    int viewport_height;    /* Visible area height */

    /* Fonts (not owned, set by parent) */
    HFONT hFont;
    HFONT hMonoFont;
    HFONT hBoldFont;
    HFONT hSmallFont;

    /* Activity indicator (not owned, set by parent) */
    ActivityState *activity;
    int pulse_toggle;           /* Animation toggle for pulsing dot */

    /* Text selection (content-space Y coordinates) */
    int sel_start_y;            /* Content Y where mouse-drag started */
    int sel_end_y;              /* Content Y where mouse-drag currently is */
    int sel_active;             /* 1 = mouse is held down, dragging */
    int sel_valid;              /* 1 = a selection exists to highlight/copy */

    /* Command list collapse state (0 = collapsed, 1 = expanded) */
    int cmd_list_expanded;

    /* Command container scroll state */
    int cmd_scroll_y;           /* Scroll offset within command container */
    int cmd_total_h;            /* Total height of all command cards */
    int cmd_visible_h;          /* Visible content height (capped) */
    int cmd_count;              /* Number of command items */
    int cmd_heights[16];        /* Individual card heights (pre-container) */
    int cmd_text_w[16];         /* Full (unellipsised) command text width, px */

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
     * task 6. Element ids: CLV_ROW_HIT_ID/CLV_CARD_HIT_ID/CLV_HOVER_RETRY
     * in chat_listview.c. */
    NsHover hover;
    int hover_tracking;         /* 1 while TrackMouseEvent(TME_LEAVE) is armed */

    /* "Nothing to show yet" state (ai_panel_states.h's AiPanelStateId, or
     * -1 for none), painted by on_paint() only while the message list has
     * zero items -- see chat_listview_set_state() and AI Assist Panel
     * task 4's "Empty and blocked states" section. state_context_lines
     * feeds AI_STATE_EMPTY's "last N lines" body. */
    int state_id;
    int state_context_lines;

} ChatListView;

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
