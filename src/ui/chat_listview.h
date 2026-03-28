/* src/ui/chat_listview.h */
#ifndef NUTSHELL_CHAT_LISTVIEW_H
#define NUTSHELL_CHAT_LISTVIEW_H

#include <windows.h>
#include "chat_msg.h"
#include "chat_activity.h"
#include "ui_theme.h"

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
    HFONT hIconFont;

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

    /* DPI scaling factor (1.0 = 96 DPI) */
    float dpi_scale;

    /* Layout constants (scaled) */
    int msg_gap;            /* Inter-message gap (12px base) */
    int user_pad_h;         /* User bubble horizontal padding (10px) */
    int user_pad_v;         /* User bubble vertical padding (8px) */
    int ai_indent;          /* AI content left indent (30px) */
    int code_pad;           /* Code block padding (6px) */

    /* External custom scrollbar (not owned) */
    HWND ext_scrollbar;

} ChatListView;

/* Register the window class. Call once at startup. */
void chat_listview_register(HINSTANCE hInstance);

/* Create the list view as a child window. */
HWND chat_listview_create(HWND parent, int x, int y, int w, int h,
                          ChatMsgList *msg_list, const ThemeColors *theme);

/* Set fonts (called after creation or font change). */
void chat_listview_set_fonts(HWND hwnd, HFONT font, HFONT mono,
                             HFONT bold, HFONT small_font, HFONT icon);

/* Set theme (triggers full repaint). */
void chat_listview_set_theme(HWND hwnd, const ThemeColors *theme);

/* Notify that the message list has changed. Triggers remeasure + repaint. */
void chat_listview_invalidate(HWND hwnd);

/* Scroll to bottom (e.g., after new message). */
void chat_listview_scroll_to_bottom(HWND hwnd);

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

#endif /* NUTSHELL_CHAT_LISTVIEW_H */
